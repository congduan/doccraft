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
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/sprm.h"
#include "docx/writer.h"
#include "third_party/miniz/miniz.h"

namespace {
namespace cfb = doccraft::cfb;
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

// Little-endian 4-byte DTTM ([MS-DOC] 2.9.60) for sprmCDttmRMark operands.
std::vector<uint8_t> Dttm(uint32_t value) {
  return {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
          static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
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

// SttbfRMark is FibRgFcLcb97 row 52, read here as pair(51) (0-based pairs,
// anchored by fcClx = pair(33) at byte 0x01A2), right after fcDggInfo =
// pair(50). Regression guard: the first draft read pair(24), which is fcCmds
// (a 2-byte Tcg in real Word 97 files), so real documents never found their
// author table; the decoy pair(24) value below must stay unread.
TEST(M19Revisions, FibReadsSttbfRMarkFromPair51) {
  std::vector<uint8_t> word(4096);
  Put16(word, 0, 0xA5EC);
  Put16(word, 2, 0x00C1);
  Put16(word, 32, 14);               // csw
  Put16(word, 62, 22);               // cslw
  Put16(word, 152, 93);              // cbRgFcLcb
  Put32(word, 154 + 51 * 8, 0x2A2);  // fcSttbfRMark
  Put32(word, 158 + 51 * 8, 8);      // lcbSttbfRMark
  Put32(word, 154 + 24 * 8, 0x999);  // fcCmds decoy: must not be read
  Put32(word, 158 + 24 * 8, 8);
  auto fib = doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->rmark_authors, (doc::Fib::Range{0x2A2, 8}));
}

// Parses a checked-in fixture through the real CFB -> FIB -> formatted path
// (no injected Fib), so the pair offset exercised is the production one.
std::expected<model::Document, doc::Error> ParseFixture(
    const std::string& filename) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/" + filename;
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::unexpected(doc::Error{"cannot open " + path});
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  if (!cfb)
    return std::unexpected(doc::Error{cfb.error().message});
  auto word = cfb->ReadStream("WordDocument");
  if (!word)
    return std::unexpected(doc::Error{word.error().message});
  auto fib = doc::ParseFib(*word);
  if (!fib)
    return std::unexpected(fib.error());
  auto table = cfb->ReadStream(fib->table_stream());
  if (!table)
    return std::unexpected(doc::Error{table.error().message});
  auto data = cfb->ReadStream("Data");
  if (data)
    return doc::ParseFormatted(*word, *table, *fib, *data);
  return doc::ParseFormatted(*word, *table, *fib);
}

// Real Word 97 bytes pin the offset: both fixtures carry a 1-entry
// SttbfRMark ("Unknown", cData=1, cbExtra=0) at pair(51) while pair(24)
// holds fcCmds (lcb=2, a Tcg). Before the pair(51) fix the table never
// parsed and conversion warned "truncated SttbfRMark" on both files.
TEST(M19Revisions, RealWord97SttbfRMarkParsesThroughFib) {
  for (const char* name : {"fast-saved-o-kurs.doc", "ole-embedded.doc"}) {
    auto parsed = ParseFixture(name);
    ASSERT_TRUE(parsed.has_value()) << name << ": " << parsed.error().message;
    const std::vector<std::string> expected{"Unknown"};
    EXPECT_EQ(expected, parsed->revision_authors) << name;
    for (const auto& warning : parsed->warnings)
      EXPECT_EQ(std::string::npos, warning.find("SttbfRMark"))
          << name << ": " << warning;
  }
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
  EXPECT_TRUE(WarnsAbout(*d, "out of range"));
}

// Revision marks without any SttbfRMark keep the placeholder author and
// record a warning.
TEST(M19Revisions, MissingTableWarns) {
  auto d = Synthetic(Prl(0x0000, {0}), Sprms({Prl(0x0801, {1})}));
  ASSERT_TRUE(d.has_value());
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="doccraft")"));
  EXPECT_TRUE(WarnsAbout(*d, "without SttbfRMark"));
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

// sprmCDttmRMark carries the insertion date as a 4-byte DTTM; the raw bits
// reach the IR and w:ins exposes them as an ISO-8601 @w:date.
TEST(M19Revisions, InsertDateFromDttm) {
  // 2026-10-29T14:05:00Z: minute 5, hour 14, day 29, month 10, year 1900+126.
  constexpr uint32_t kDttm =
      5 | (14 << 6) | (29 << 11) | (10 << 16) | (126 << 20);
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0x0801, {1}), Prl(0x6805, Dttm(kDttm))}),
                     AuthorsTable({"Unknown"}));
  ASSERT_TRUE(d.has_value());
  ASSERT_EQ(1u, d->paragraphs.size());
  ASSERT_EQ(1u, d->paragraphs[0].runs.size());
  EXPECT_EQ(kDttm, d->paragraphs[0].runs[0].revision_date_insert);
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(<w:ins )"));
  EXPECT_NE(std::string::npos, xml.find(R"(w:date="2026-10-29T14:05:00Z")"));
}

// sprmCDttmRMarkDel feeds w:del's @w:date from revision_date_delete.
TEST(M19Revisions, DeleteDateFromDttm) {
  // 1999-01-02T03:04:00Z: minute 4, hour 3, day 2, month 1, year 1900+99.
  constexpr uint32_t kDttm = 4 | (3 << 6) | (2 << 11) | (1 << 16) | (99 << 20);
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0x0800, {1}), Prl(0x6864, Dttm(kDttm))}),
                     AuthorsTable({"Unknown"}));
  ASSERT_TRUE(d.has_value());
  ASSERT_EQ(1u, d->paragraphs[0].runs.size());
  EXPECT_EQ(kDttm, d->paragraphs[0].runs[0].revision_date_delete);
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(<w:del )"));
  EXPECT_NE(std::string::npos, xml.find(R"(w:date="1999-01-02T03:04:00Z")"));
}

// Without sprmCDttmRMark* the mark keeps the old shape: no @w:date at all.
TEST(M19Revisions, NoDateWithoutDttm) {
  auto d = Synthetic(Prl(0x0000, {0}), Sprms({Prl(0x0801, {1})}),
                     AuthorsTable({"Unknown"}));
  ASSERT_TRUE(d.has_value());
  ASSERT_EQ(1u, d->paragraphs[0].runs.size());
  EXPECT_EQ(0u, d->paragraphs[0].runs[0].revision_date_insert);
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(<w:ins )"));
  EXPECT_EQ(std::string::npos, xml.find("w:date="));
}

// A DTTM operand shorter than four bytes never reaches ApplyRun through the
// file path: the FKP grpprl walk rejects the malformed sequence and the CHPX
// page is skipped with a warning. ApplyRun's own guard still ignores a short
// span handed in directly.
TEST(M19Revisions, TruncatedDttmIgnored) {
  auto d = Synthetic(Prl(0x0000, {0}),
                     Sprms({Prl(0x0801, {1}), Prl(0x6805, {1, 2, 3})}),
                     AuthorsTable({"Unknown"}));
  ASSERT_TRUE(d.has_value());
  ASSERT_EQ(1u, d->paragraphs[0].runs.size());
  EXPECT_EQ(0u, d->paragraphs[0].runs[0].revision_date_insert);
  EXPECT_FALSE(d->paragraphs[0].runs[0].revision_insert);
  EXPECT_TRUE(WarnsAbout(*d, "skipped CHPX FKP page"));
  EXPECT_EQ(std::string::npos, Xml(*d).find("w:date="));
  model::Run run;
  const std::vector<uint8_t> short_operand{1, 2, 3};
  doc::detail::ApplyRun(run, 0x6805, Bytes(short_operand), {});
  doc::detail::ApplyRun(run, 0x6864, Bytes(short_operand), {});
  EXPECT_EQ(0u, run.revision_date_insert);
  EXPECT_EQ(0u, run.revision_date_delete);
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
  EXPECT_TRUE(WarnsAbout(*d, "truncated SttbfRMark"));
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="doccraft")"));
}

// A non-extended STTB header (fExtend != 0xFFFF) is not SttbfRMark; parsing
// fails closed with a header warning and the placeholder author everywhere.
TEST(M19Revisions, MalformedHeaderFExtendFailsClosed) {
  std::vector<uint8_t> rmark{0x00, 0x00, 1, 0, 0, 0};
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
  EXPECT_TRUE(WarnsAbout(*d, "invalid SttbfRMark header"));
  const std::string xml = Xml(*d);
  EXPECT_NE(std::string::npos, xml.find(R"(w:author="doccraft")"));
}

// [MS-DOC] 2.9.290 requires cbExtra == 0; any other value fails closed the
// same way instead of misreading entry bytes as text.
TEST(M19Revisions, MalformedHeaderCbExtraFailsClosed) {
  std::vector<uint8_t> rmark{0xFF, 0xFF, 1, 0, 4, 0};
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
  EXPECT_TRUE(WarnsAbout(*d, "invalid SttbfRMark header"));
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
