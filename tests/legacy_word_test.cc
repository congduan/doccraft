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

#include <cstdint>
#include <string>
#include <vector>

#include "doc/fib.h"
#include "doc/format.h"
#include "doc/text.h"

namespace {
void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}
void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  Put16(b, p, static_cast<uint16_t>(v));
  Put16(b, p + 2, static_cast<uint16_t>(v >> 16));
}

TEST(LegacyWord, SimpleWord6TextAndSignatureGate) {
  std::vector<uint8_t> word(1024);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put16(word, 6, 0x0409);
  Put32(word, 24, 768);
  Put32(word, 28, 772);
  Put32(word, 52, 4);
  word[768] = 'A';
  word[769] = 'B';
  word[770] = 'C';
  word[771] = '\r';
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_TRUE(fib->legacy);
  auto text = doccraft::doc::ExtractText(word, word, *fib);
  ASSERT_TRUE(text) << text.error().message;
  EXPECT_EQ(*text, "ABC\n");
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1U);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1U);
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "ABC");
  Put16(word, 0, 0x0052);
  EXPECT_FALSE(doccraft::doc::ParseFib(word));
}

TEST(LegacyWord, Word6FkpAppliesCharacterAndParagraphFormatting) {
  std::vector<uint8_t> word(2560);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put16(word, 6, 0x0409);
  Put32(word, 24, 768);
  Put32(word, 28, 772);
  Put32(word, 52, 4);
  word[768] = 'A';
  word[769] = 'B';
  word[770] = 'C';
  word[771] = '\r';
  // Word 6/95 BTEs are 4-byte FC bounds followed by a 2-byte page number.
  Put32(word, 88 + 12 * 8, 2048);
  Put32(word, 92 + 12 * 8, 10);
  Put32(word, 2048, 768);
  Put32(word, 2052, 772);
  Put16(word, 2056, 2);
  Put32(word, 88 + 13 * 8, 2060);
  Put32(word, 92 + 13 * 8, 10);
  Put32(word, 2060, 768);
  Put32(word, 2064, 772);
  Put16(word, 2068, 3);
  // CHPX FKP: one run, 8-bit sprm 0x55 (bold).
  Put32(word, 1024, 768);
  Put32(word, 1028, 772);
  word[1032] = 250;
  word[1524] = 2;
  word[1525] = 0x55;
  word[1526] = 1;
  word[1535] = 1;
  // PAPX FKP: seven-byte BX and 8-bit sprm 0x05 (right align).
  Put32(word, 1536, 768);
  Put32(word, 1540, 772);
  word[1544] = 252;
  word[2040] = 3;
  Put16(word, 2041, 0);
  word[2043] = 5;
  word[2044] = 2;
  word[2047] = 1;
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].align, 2);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].bold);
}

TEST(LegacyWord, ComplexWord95UsesDirectFcAndUtf16) {
  std::vector<uint8_t> word(1024);
  Put16(word, 0, 0xA699);
  Put16(word, 2, 0x68);
  Put16(word, 6, 0x0409);
  Put16(word, 10, 0x0004);
  Put32(word, 24, 768);
  Put32(word, 28, 774);
  Put32(word, 52, 3);
  // The Word 6/95 fc/lcb array starts at 0x58. CLX is pair 33.
  Put32(word, 88 + 33 * 8, 400);
  Put32(word, 92 + 33 * 8, 21);
  word[400] = 2;
  Put32(word, 401, 16);
  Put32(word, 405, 0);
  Put32(word, 409, 3);
  word[413] = 0x60;
  Put32(word, 415, 768);
  Put16(word, 768, 0x4E2D);
  Put16(word, 770, 0x6587);
  Put16(word, 772, '\r');
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto text = doccraft::doc::ExtractText(word, word, *fib);
  ASSERT_TRUE(text) << text.error().message;
  EXPECT_EQ(*text, "中文\n");
  Put32(word, 415, 1023);
  EXPECT_FALSE(doccraft::doc::ExtractText(word, word, *fib));
}

TEST(LegacyWord, Word95AnsiPieceUsesTraditionalChineseCodepage) {
  std::vector<uint8_t> word(1024);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x68);
  Put16(word, 6, 0x0404);  // Traditional Chinese -> Windows 950.
  Put16(word, 10, 4);
  Put32(word, 24, 768);
  Put32(word, 28, 771);
  Put32(word, 52, 3);
  Put32(word, 88 + 33 * 8, 400);
  Put32(word, 92 + 33 * 8, 21);
  word[400] = 2;
  Put32(word, 401, 16);
  Put32(word, 405, 0);
  Put32(word, 409, 3);
  word[413] = 0x70;
  Put32(word, 415, 768);
  word[768] = 0xA4;
  word[769] = 0xA4;  // Big5/CP950 中.
  word[770] = '\r';
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->codepage, 950U);
  auto text = doccraft::doc::ExtractText(word, word, *fib);
  ASSERT_TRUE(text) << text.error().message;
  EXPECT_EQ(*text, "中\n");
}

TEST(LegacyWord, RejectsTruncatedFibAndMalformedClx) {
  std::vector<uint8_t> word(400);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put32(word, 24, 768);
  Put32(word, 28, 769);
  Put32(word, 52, 1);
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_FALSE(fib);
  EXPECT_EQ(fib.error().message, "invalid Word 6/95 text range");
  word.resize(1024);
  Put16(word, 10, 4);
  Put32(word, 88 + 33 * 8, 400);
  Put32(word, 92 + 33 * 8, 21);
  fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto text = doccraft::doc::ExtractText(word, word, *fib);
  ASSERT_FALSE(text);
  EXPECT_EQ(text.error().message, "unsupported Word 6/95 CLX");
  word[400] = 2;
  Put32(word, 401, 0xFFFFFFFF);
  text = doccraft::doc::ExtractText(word, word, *fib);
  ASSERT_FALSE(text);
  EXPECT_EQ(text.error().message, "invalid Word 6/95 PlcPcd size");
}

TEST(LegacyWord, ReadsBoundedSummaryCodepage) {
  std::vector<uint8_t> summary(72);
  Put16(summary, 0, 0xFFFE);
  Put32(summary, 24, 1);
  Put32(summary, 44, 48);
  Put32(summary, 48, 24);
  Put32(summary, 52, 1);
  Put32(summary, 56, 1);
  Put32(summary, 60, 16);
  Put16(summary, 64, 2);
  Put16(summary, 68, 950);
  EXPECT_EQ(doccraft::doc::ReadSummaryCodepage(summary), 950U);
  Put32(summary, 60, 0xFFFFFFFF);
  EXPECT_EQ(doccraft::doc::ReadSummaryCodepage(summary), 0U);
  summary.resize(47);
  EXPECT_EQ(doccraft::doc::ReadSummaryCodepage(summary), 0U);
}
}  // namespace
