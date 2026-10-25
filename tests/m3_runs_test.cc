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

// Run merging: adjacent characters share a run only when every character
// property matches. Two CHPX ranges over one piece exercise that decision.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "miniz.h"

#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;

constexpr uint32_t kTextFc = 1536;  // first character of the piece
constexpr uint32_t kChpxPage = 1;   // 512-byte FKP page holding the CHPX
constexpr size_t kChpxBase = kChpxPage * 512;
constexpr uint32_t kClxFc = 0;
constexpr uint32_t kClxSize = 21;
constexpr uint32_t kChpxBteFc = 48;
constexpr uint32_t kChpxBteSize = 12;

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[p + i] = static_cast<uint8_t>(v >> (8 * i));
}

std::vector<uint8_t> Prl(uint16_t op, const std::vector<uint8_t>& value) {
  std::vector<uint8_t> out{static_cast<uint8_t>(op),
                           static_cast<uint8_t>(op >> 8)};
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

// `first`/`second` are the CHPX grpprls of the two characters "AB".
std::string Convert(const std::vector<uint8_t>& first,
                    const std::vector<uint8_t>& second) {
  std::vector<uint8_t> word(2560);
  std::vector<uint8_t> table(64);
  word[0] = 0xEC;
  word[1] = 0xA5;  // signature
  Put16(word, 2, 0x00C1);
  Put32(word, 24, kTextFc);
  Put32(word, 28, kTextFc + 2);
  Put16(word, 32, 14);
  Put16(word, 62, 22);
  Put32(word, 64 + 3 * 4, 2);  // ccpText
  Put16(word, 152, 93);
  Put32(word, 154 + 33 * 8, kClxFc);
  Put32(word, 158 + 33 * 8, kClxSize);
  Put32(word, 154 + 12 * 8, kChpxBteFc);  // PlcfBteChpx
  Put32(word, 158 + 12 * 8, kChpxBteSize);

  word[kTextFc] = 'A';
  word[kTextFc + 1] = 'B';

  // CLX: one compressed piece covering both characters.
  table[kClxFc] = 2;
  Put32(table, kClxFc + 1, 16);
  Put32(table, kClxFc + 5, 0);
  Put32(table, kClxFc + 9, 2);
  Put16(table, kClxFc + 13, 0);
  Put32(table, kClxFc + 15, 0x40000000u | (kTextFc * 2));
  Put16(table, kClxFc + 19, 0);

  // PlcfBteChpx: two FCs then the single FKP page number.
  Put32(table, kChpxBteFc, kTextFc);
  Put32(table, kChpxBteFc + 4, kTextFc + 2);
  Put32(table, kChpxBteFc + 8, kChpxPage);

  // CHPX FKP: FCs, run count, then per-run grpprl offsets.
  Put32(word, kChpxBase, kTextFc);
  Put32(word, kChpxBase + 4, kTextFc + 1);
  Put32(word, kChpxBase + 8, kTextFc + 2);
  word[kChpxBase + 511] = 2;
  // The rgb array stores the grpprl offset divided by two.
  const size_t slot0 = 20, slot1 = 24;
  word[kChpxBase + 12] = first.empty() ? 0 : static_cast<uint8_t>(slot0);
  word[kChpxBase + 13] = second.empty() ? 0 : static_cast<uint8_t>(slot1);
  if (!first.empty()) {
    word[kChpxBase + 2 * slot0] = static_cast<uint8_t>(first.size());
    std::copy(first.begin(), first.end(),
              word.begin() + kChpxBase + 2 * slot0 + 1);
  }
  if (!second.empty()) {
    word[kChpxBase + 2 * slot1] = static_cast<uint8_t>(second.size());
    std::copy(second.begin(), second.end(),
              word.begin() + kChpxBase + 2 * slot1 + 1);
  }

  auto fib = doc::ParseFib(word);
  if (!fib)
    return "fib error";
  auto document = doc::ParseFormatted(word, table, *fib);
  if (!document)
    return "parse error: " + document.error().message;
  auto zip = docx::Write(*document);
  if (!zip)
    return "write error";
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip->data(), zip->size(), 0))
    return "zip error";
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

size_t Count(const std::string& xml, const std::string& needle) {
  size_t hits = 0, at = 0;
  while ((at = xml.find(needle, at)) != std::string::npos) {
    ++hits;
    at += needle.size();
  }
  return hits;
}

std::vector<std::string> TextRuns(const std::string& xml) {
  std::vector<std::string> out;
  size_t pos = 0;
  while ((pos = xml.find("<w:t", pos)) != std::string::npos) {
    size_t open = xml.find('>', pos);
    size_t close = xml.find("</w:t>", open);
    if (open == std::string::npos || close == std::string::npos)
      break;
    out.push_back(xml.substr(open + 1, close - open - 1));
    pos = close;
  }
  return out;
}
}  // namespace

TEST(M3Runs, IdenticalFormattingMerges) {
  const std::vector<uint8_t> bold = Prl(0x0835, {1});
  const std::string xml = Convert(bold, bold);
  ASSERT_NE(xml.find("error"), 0u) << xml;
  EXPECT_EQ(TextRuns(xml), std::vector<std::string>{"AB"});
  EXPECT_EQ(Count(xml, "<w:r>"), 1u);
}

TEST(M3Runs, RevisionMarkKeepsRunsApart) {
  // A is plain, B carries sprmCFRMarkIns: the two characters must not merge,
  // otherwise B would silently lose its revision mark.
  const std::string xml = Convert({}, Prl(0x0801, {1}));
  ASSERT_NE(xml.find("error"), 0u) << xml;
  EXPECT_EQ(TextRuns(xml), (std::vector<std::string>{"A", "B"}));
  EXPECT_NE(xml.find("<w:ins "), std::string::npos);
}

TEST(M3Runs, DeletedRevisionKeepsRunsApart) {
  const std::string xml = Convert(Prl(0x0800, {1}), {});
  ASSERT_NE(xml.find("error"), 0u) << xml;
  // Deleted text is written as w:delText, so only "B" stays a w:t run.
  EXPECT_EQ(TextRuns(xml), (std::vector<std::string>{"B"}));
  EXPECT_NE(xml.find("<w:del "), std::string::npos);
  EXPECT_NE(xml.find("<w:delText xml:space=\"preserve\">A</w:delText>"),
            std::string::npos);
}

TEST(M3Runs, BidiFlagKeepsRunsApart) {
  const std::string xml = Convert({}, Prl(0x085A, {1}));
  ASSERT_NE(xml.find("error"), 0u) << xml;
  EXPECT_EQ(TextRuns(xml), (std::vector<std::string>{"A", "B"}));
  EXPECT_NE(xml.find("<w:rtl/>"), std::string::npos);
}

TEST(M3Runs, LanguageAndScriptFlagsKeepRunsApart) {
  // sprmCRgLid0 changes w:lang only in the second run; merging would drop it.
  const std::string xml = Convert({}, Prl(0x4873, {0x09, 0x04}));
  ASSERT_NE(xml.find("error"), 0u) << xml;
  EXPECT_EQ(TextRuns(xml), (std::vector<std::string>{"A", "B"}));
  EXPECT_NE(xml.find("w:val=\"en-US\""), std::string::npos);
}
