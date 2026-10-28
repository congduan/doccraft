// Copyright 2026 The doccraft Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string>
#include <vector>

#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/sprm.h"
#include "docx/writer.h"
#include "third_party/miniz/miniz.h"

namespace {
namespace doc = doccraft::doc;
namespace docx = doccraft::docx;
namespace model = doccraft::model;

using doc::detail::Bytes;

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t n) {
  b[p] = n & 255;
  b[p + 1] = n >> 8;
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t n) {
  Put16(b, p, n & 65535);
  Put16(b, p + 2, n >> 16);
}

std::vector<uint8_t> Prl(uint16_t op, std::vector<uint8_t> value) {
  std::vector<uint8_t> result{static_cast<uint8_t>(op),
                              static_cast<uint8_t>(op >> 8)};
  result.insert(result.end(), value.begin(), value.end());
  return result;
}

std::vector<uint8_t> Sprms(std::initializer_list<std::vector<uint8_t>> list) {
  std::vector<uint8_t> out;
  for (const auto& item : list) {
    out.insert(out.end(), item.begin(), item.end());
  }
  return out;
}

// SttbfRMark bytes ([MS-DOC] 2.9.290): an extended STTB with a 2-byte cData
// and cbExtra == 0, so each entry is cchData(2) + UTF-16LE text. The first
// entry MUST be "Unknown".
std::vector<uint8_t> AuthorsTable(std::initializer_list<std::string> names) {
  std::vector<uint8_t> b{0xFF, 0xFF, 0, 0, 0, 0};
  Put16(b, 2, static_cast<uint16_t>(names.size()));
  for (const auto& name : names) {
    b.push_back(static_cast<uint8_t>(name.size()));
    b.push_back(0);
    for (char c : name) {
      b.push_back(static_cast<uint8_t>(c));
      b.push_back(0);
    }
  }
  return b;
}

// Minimal single-paragraph document: one piece "X\r" with one CHPX and one
// PAPX, plus an optional SttbfRMark appended to the table stream at 64.
std::expected<model::Document, doc::Error> Synthetic(
    std::vector<uint8_t> pap, std::vector<uint8_t> chp,
    std::vector<uint8_t> rmark = {}) {
  std::vector<uint8_t> word(1536), table(64 + rmark.size());
  word[1024] = 'X';
  word[1025] = '\r';
  table[0] = 2;
  Put32(table, 1, 16);
  Put32(table, 5, 0);
  Put32(table, 9, 2);
  Put32(table, 15, 0x40000000u + 2048);
  auto fkp = [&](size_t base, size_t page, std::vector<uint8_t> sprms,
                 bool paragraph) {
    Put32(word, base, 1024);
    Put32(word, base + 4, 1026);
    word[base + 8] = 20;
    word[base + 511] = 1;
    if (paragraph) {
      if ((sprms.size() & 1) == 0) {
        auto nop = Prl(0x0000, {0});
        sprms.insert(sprms.end(), nop.begin(), nop.end());
      }
      word[base + 40] = static_cast<uint8_t>((sprms.size() + 3) / 2);
      Put16(word, base + 41, 0);
      std::copy(sprms.begin(), sprms.end(), word.begin() + base + 43);
    } else {
      word[base + 40] = static_cast<uint8_t>(sprms.size());
      std::copy(sprms.begin(), sprms.end(), word.begin() + base + 41);
    }

    Put32(table, page, 1024);
    Put32(table, page + 4, 1026);
    Put32(table, page + 8, static_cast<uint32_t>(base / 512));
  };
  fkp(0, 32, pap, true);
  fkp(512, 48, chp, false);
  if (!rmark.empty()) {
    std::copy(rmark.begin(), rmark.end(), table.begin() + 64);
  }
  doc::Fib fib;
  fib.ccp_text = 2;
  fib.fc_clx = 0;
  fib.lcb_clx = 21;
  fib.papx_bte = {32, 12};
  fib.chpx_bte = {48, 12};
  fib.rmark_authors = {64, static_cast<uint32_t>(rmark.size())};
  return doc::ParseFormatted(word, table, fib, Bytes{});
}

// SttbfRMark is FibRgFcLcb97 row 25, read here as pair(24) (0-based pairs,
// anchored by fcClx = pair(33) at byte 0x01A2). Regression guard: the first
// draft read pair(51), which is fcCmds, so real documents never found their
// author table.
TEST(M19Revisions, FibReadsSttbfRMarkFromPair24) {
  std::vector<uint8_t> word(4096);
  Put16(word, 0, 0xA5EC);
  Put16(word, 2, 0x00C1);
  Put16(word, 32, 14);               // csw
  Put16(word, 62, 22);               // cslw
  Put16(word, 152, 93);              // cbRgFcLcb
  Put32(word, 154 + 24 * 8, 0x2A2);  // fcSttbfRMark
  Put32(word, 158 + 24 * 8, 8);      // lcbSttbfRMark
  auto fib = doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->rmark_authors, (doc::Fib::Range{0x2A2, 8}));
}

std::string Xml(const model::Document& d) {
  auto zip = docx::Write(d);
  if (!zip) {
    return {};
  }
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip->data(), zip->size(), 0)) {
    return {};
  }
  size_t size = 0;
  void* p =
      mz_zip_reader_extract_file_to_heap(&z, "word/document.xml", &size, 0);
  std::string xml;
  if (p) {
    xml.assign(static_cast<const char*>(p), size);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return xml;
}

bool WarnsAbout(const model::Document& d, const char* needle) {
  for (const auto& warning : d.warnings) {
    if (warning.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// sprmCIbstRMark names the author of inserted text; w:ins must carry that
// author instead of the placeholder "doccraft".
TEST(M19Revisions, InsertAuthorFromSttbfRMark) {
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0x0801, {1}), Prl(0x4804, {1, 0})}),
                     AuthorsTable({"Unknown", "Alice", "Bob"}));
  ASSERT_TRUE(d.has_value());
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="Alice")"));
  EXPECT_EQ(std::string::npos, xml.find(R"(w:author="doccraft")"));
}

// sprmCIbstRMarkDel names the author of deleted text for w:del.
TEST(M19Revisions, DeleteAuthorFromSttbfRMark) {
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0x0800, {1}), Prl(0x4863, {2, 0})}),
                     AuthorsTable({"Unknown", "Alice", "Bob"}));
  ASSERT_TRUE(d.has_value());
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(<w:del )"));
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="Bob")"));
}

// sprmCPropRMark carries its author index inside the PropRMark operand at
// offset 2 (cb, fPropRMark, ibst(2), dttm(4)).
TEST(M19Revisions, PropRMarkAuthorFromOperand) {
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0xCA89, {7, 1, 2, 0, 0, 0, 0, 0})}),
                     AuthorsTable({"Unknown", "Alice", "Bob"}));
  ASSERT_TRUE(d.has_value());
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(<w:rPrChange )"));
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="Bob")"));
}

// An author index outside SttbfRMark degrades to the existing placeholder
// author and records a warning.
TEST(M19Revisions, OutOfRangeIndexFallsBackAndWarns) {
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0x0801, {1}), Prl(0x4804, {9, 0})}),
                     AuthorsTable({"Unknown", "Alice", "Bob"}));
  ASSERT_TRUE(d.has_value());
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="doccraft")"));
  EXPECT_TRUE(WarnsAbout(*d, "SttbfRMark"));
}

// Revision marks without any SttbfRMark keep the placeholder author and
// record a warning.
TEST(M19Revisions, MissingTableWarns) {
  auto d = Synthetic(Prl(0x0000, {0}), Sprms({Prl(0x0801, {1})}));
  ASSERT_TRUE(d.has_value());
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="doccraft")"));
  EXPECT_TRUE(WarnsAbout(*d, "SttbfRMark"));
}

// The parser stores the decoded table and the per-run indices; sprmCPropRMark
// takes its index from operand offset 2.
TEST(M19Revisions, StoresIndicesAndTable) {
  auto d = Synthetic(
      Prl(0x0000, {0}),
      Sprms({Prl(0x0801, {1}), Prl(0x4804, {1, 0}), Prl(0x0800, {1}),
             Prl(0x4863, {2, 0}), Prl(0xCA89, {7, 1, 2, 0, 0, 0, 0, 0})}),
      AuthorsTable({"Unknown", "Alice", "Bob"}));
  ASSERT_TRUE(d.has_value());
  ASSERT_EQ(1u, d->paragraphs.size());
  ASSERT_EQ(1u, d->paragraphs[0].runs.size());
  const model::Run& run = d->paragraphs[0].runs[0];
  EXPECT_TRUE(run.revision_insert);
  EXPECT_TRUE(run.revision_delete);
  EXPECT_TRUE(run.property_revision);
  EXPECT_EQ(1, run.revision_author_insert);
  EXPECT_EQ(2, run.revision_author_delete);
  EXPECT_EQ(2, run.revision_property);
  const std::vector<std::string> expected{"Unknown", "Alice", "Bob"};
  EXPECT_EQ(expected, d->revision_authors);
}

// A structurally invalid SttbfRMark fails closed: no authors, a warning, and
// the placeholder author everywhere.
TEST(M19Revisions, MalformedTableFailsClosed) {
  // cData says 3 entries but the table ends after "Unknown".
  std::vector<uint8_t> rmark{0xFF, 0xFF, 3, 0, 0, 0};
  const std::string unknown = "Unknown";
  rmark.push_back(7);
  rmark.push_back(0);
  for (char c : unknown) {
    rmark.push_back(static_cast<uint8_t>(c));
    rmark.push_back(0);
  }
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0x0801, {1}), Prl(0x4804, {1, 0})}), rmark);
  ASSERT_TRUE(d.has_value());
  EXPECT_TRUE(d->revision_authors.empty());
  EXPECT_TRUE(WarnsAbout(*d, "SttbfRMark"));
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="doccraft")"));
}

// A run marked without any sprmCIbstRMark keeps the spec default index 0,
// which names the mandated "Unknown" first entry.
TEST(M19Revisions, UnindexedMarkNamesUnknown) {
  auto d = Synthetic(Prl(0x0000, {0}), Sprms({Prl(0x0801, {1})}),
                     AuthorsTable({"Unknown", "Alice"}));
  ASSERT_TRUE(d.has_value());
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="Unknown")"));
}
}  // namespace
