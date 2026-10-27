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
#include <string>
#include <vector>

#include "doc/fib.h"
#include "doc/format.h"

namespace {
void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}
void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  Put16(b, p, static_cast<uint16_t>(v));
  Put16(b, p + 2, static_cast<uint16_t>(v >> 16));
}
void Append16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}

// One Word 6 FFN entry: cbFfnM1, flags, wWeight, chs, ibszAlt, szFfn, NUL.
std::vector<uint8_t> LegacyFfnEntry(uint8_t chs, const std::string& name) {
  std::vector<uint8_t> e;
  e.push_back(static_cast<uint8_t>(6 + name.size()));
  e.push_back(0x16);  // fTrueType, ff roman.
  Append16(e, 400);
  e.push_back(chs);
  e.push_back(0);  // No alternate name.
  e.insert(e.end(), name.begin(), name.end());
  e.push_back(0);
  return e;
}

std::vector<uint8_t> LegacyFfn(
    const std::vector<std::vector<uint8_t>>& entries) {
  std::vector<uint8_t> b;
  size_t total = 2;
  for (const auto& e : entries)
    total += e.size();
  Append16(b, static_cast<uint16_t>(total));
  for (const auto& e : entries)
    b.insert(b.end(), e.begin(), e.end());
  return b;
}

// One Word 6 STD record: 8-byte fixed part, Pascal name, parity-aligned UPXs.
std::vector<uint8_t> LegacyStd(int sgc, int base, int next,
                               const std::string& name,
                               const std::vector<uint8_t>* papx_sprms,
                               const std::vector<uint8_t>* chpx_sprms) {
  std::vector<uint8_t> d(8, 0);
  const int cupx = (papx_sprms ? 1 : 0) + (chpx_sprms ? 1 : 0);
  Put16(d, 2, static_cast<uint16_t>((base << 4) | sgc));
  Put16(d, 4, static_cast<uint16_t>((next << 4) | cupx));
  d.push_back(static_cast<uint8_t>(name.size()));
  d.insert(d.end(), name.begin(), name.end());
  d.push_back(0);  // Trailing NUL after the Pascal string.
  if (d.size() & 1)
    d.push_back(0);
  if (papx_sprms) {
    Append16(d, static_cast<uint16_t>(2 + papx_sprms->size()));
    Append16(d, 0);  // istd carried by the PAPX UPX.
    d.insert(d.end(), papx_sprms->begin(), papx_sprms->end());
    if (d.size() & 1)
      d.push_back(0);
  }
  if (chpx_sprms) {
    Append16(d, static_cast<uint16_t>(chpx_sprms->size()));
    d.insert(d.end(), chpx_sprms->begin(), chpx_sprms->end());
    if (d.size() & 1)
      d.push_back(0);
  }
  Put16(d, 6, static_cast<uint16_t>(d.size()));  // bchUpe == cbStd.
  return d;
}

std::vector<uint8_t> LegacyStsh(
    const std::vector<std::vector<uint8_t>>& records) {
  std::vector<uint8_t> b;
  Append16(b, 4);  // cbStshi: cstd + cbSTDBase only.
  Append16(b, static_cast<uint16_t>(records.size()));
  Append16(b, 8);  // cbSTDBase for Word 6/95.
  for (const auto& r : records) {
    Append16(b, static_cast<uint16_t>(r.size()));
    b.insert(b.end(), r.begin(), r.end());
  }
  return b;
}

void PutBlob(std::vector<uint8_t>& word, size_t fc,
             const std::vector<uint8_t>& blob) {
  std::copy(blob.begin(), blob.end(), word.begin() + static_cast<long>(fc));
}

std::vector<uint8_t> MakeWord6Base(size_t size) {
  std::vector<uint8_t> word(size);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put16(word, 6, 0x0409);
  Put32(word, 24, 768);
  Put32(word, 28, 771);
  Put32(word, 52, 3);
  word[768] = 'A';
  word[769] = 'B';
  word[770] = '\r';
  return word;
}

// Style sheet with a Normal style (font + size + paragraph props), an empty
// slot, and a Quote style based on Normal with its own indents and italics.
std::vector<uint8_t> QuoteStylesheet() {
  const std::vector<uint8_t> normal_papx = {0x05, 0x01, 0x2C, 0x01};
  const std::vector<uint8_t> normal_chpx = {0x5D, 0x01, 0x00, 0x63, 0x18, 0x00};
  const std::vector<uint8_t> quote_papx = {0x11, 0xD0, 0x02};
  const std::vector<uint8_t> quote_chpx = {0x56, 0x01};
  return LegacyStsh(
      {LegacyStd(1, 0x0FFF, 0, "Normal", &normal_papx, &normal_chpx),
       {},
       LegacyStd(1, 0, 0, "Quote", &quote_papx, &quote_chpx)});
}

TEST(LegacyStyles, ReadsStyleSheetFontsAndInheritance) {
  std::vector<uint8_t> word = MakeWord6Base(2560);
  const std::vector<uint8_t> stsh = QuoteStylesheet();
  PutBlob(word, 1024, stsh);
  Put32(word, 88 + 8, 1024);
  Put32(word, 92 + 8, static_cast<uint32_t>(stsh.size()));
  const std::vector<uint8_t> ffn =
      LegacyFfn({LegacyFfnEntry(0x00, "Times New Roman"),
                 LegacyFfnEntry(0x00, "Courier New")});
  PutBlob(word, 2048, ffn);
  Put32(word, 88 + 15 * 8, 2048);
  Put32(word, 92 + 15 * 8, static_cast<uint32_t>(ffn.size()));
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->fonts.size(), 2U);
  EXPECT_EQ(document->fonts[0], "Times New Roman");
  EXPECT_EQ(document->fonts[1], "Courier New");
  ASSERT_EQ(document->styles.size(), 3U);
  EXPECT_EQ(document->styles[0].name, "Normal");
  EXPECT_EQ(document->styles[0].type, 1);
  EXPECT_EQ(document->styles[0].base, -1);
  EXPECT_EQ(document->styles[0].next, 0);
  EXPECT_EQ(document->styles[1].name, "");  // Empty slot keeps the index.
  EXPECT_EQ(document->styles[2].name, "Quote");
  EXPECT_EQ(document->styles[2].base, 0);
  // Normal carries its own direct formatting.
  EXPECT_EQ(document->styles[0].run.font, "Courier New");
  EXPECT_EQ(document->styles[0].run.size, 24);  // sprmCHps, half-points.
  EXPECT_EQ(document->styles[0].paragraph.align, 1);
  EXPECT_TRUE(document->styles[0].paragraph.suppress_hyphenation);
  // Quote inherits Normal's run through the based-on chain and adds its own.
  EXPECT_EQ(document->styles[2].resolved_run.font, "Courier New");
  EXPECT_EQ(document->styles[2].resolved_run.size, 24);
  EXPECT_TRUE(document->styles[2].resolved_run.italic);
  EXPECT_EQ(document->styles[2].paragraph.left, 720);
  EXPECT_TRUE(document->styles[2].paragraph.left_set);
}

TEST(LegacyStyles, PapxStyleLinksParagraphToStyles) {
  std::vector<uint8_t> word = MakeWord6Base(2560);
  // PAPX FKP page 3 with one paragraph bound to istd 2 (Quote).
  Put32(word, 1536, 768);
  Put32(word, 1540, 771);
  word[1544] = 252;  // BX: word offset of the PAPX payload.
  word[2040] = 2;    // cw: the istd word only.
  Put16(word, 2041, 2);
  word[2047] = 1;  // Run count.
  Put32(word, 88 + 13 * 8, 2200);
  Put32(word, 92 + 13 * 8, 10);
  Put32(word, 2200, 768);
  Put32(word, 2204, 771);
  Put16(word, 2208, 3);  // Page 3.
  const std::vector<uint8_t> stsh = QuoteStylesheet();
  PutBlob(word, 2048, stsh);
  Put32(word, 88 + 8, 2048);
  Put32(word, 92 + 8, static_cast<uint32_t>(stsh.size()));
  const std::vector<uint8_t> ffn = LegacyFfn({LegacyFfnEntry(0x00, "Arial")});
  PutBlob(word, 2176, ffn);
  Put32(word, 88 + 15 * 8, 2176);
  Put32(word, 92 + 15 * 8, static_cast<uint32_t>(ffn.size()));
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->styles.size(), 3U);
  ASSERT_EQ(document->paragraphs.size(), 1U);
  EXPECT_EQ(document->paragraphs[0].style, 2);
  EXPECT_EQ(document->styles[2].name, "Quote");
}

TEST(LegacyStyles, DecodesNamesWithSniffedCyrillicCodepage) {
  std::vector<uint8_t> word = MakeWord6Base(2048);
  // FFN entry with the Russian charset: the fib.cc sniff picks codepage 1251,
  // so both font and style names decode as Cyrillic.
  const std::vector<uint8_t> ffn =
      LegacyFfn({LegacyFfnEntry(0xCC, std::string("\xD2\xE0\xE9\xEC\xF1", 5))});
  PutBlob(word, 1536, ffn);
  Put32(word, 88 + 15 * 8, 1536);
  Put32(word, 92 + 15 * 8, static_cast<uint32_t>(ffn.size()));
  const std::vector<uint8_t> normal_chpx = {0x5D, 0x00, 0x00};
  const std::vector<uint8_t> stsh = LegacyStsh(
      {LegacyStd(1, 0x0FFF, 0, std::string("\xCE\xE1\xFB\xF7\xED\xFB\xE9", 7),
                 nullptr, &normal_chpx)});
  PutBlob(word, 1600, stsh);
  Put32(word, 88 + 8, 1600);
  Put32(word, 92 + 8, static_cast<uint32_t>(stsh.size()));
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->codepage, 1251U);
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->fonts.size(), 1U);
  EXPECT_EQ(document->fonts[0], std::string("\xD0\xA2\xD0\xB0\xD0\xB9\xD0\xBC"
                                            "\xD1\x81",
                                            10));  // Таймс
  ASSERT_EQ(document->styles.size(), 1U);
  EXPECT_EQ(document->styles[0].name,
            std::string("\xD0\x9E\xD0\xB1\xD1\x8B\xD1\x87\xD0\xBD\xD1\x8B"
                        "\xD0\xB9",
                        14));  // Обычный
}

TEST(LegacyStyles, MalformedTablesWarnInsteadOfFailing) {
  std::vector<uint8_t> word = MakeWord6Base(2048);
  // cbStshi of zero leaves no readable STSHI; a zero-length font entry stops
  // the FFN walk. Conversion must continue without styles or fonts.
  const std::vector<uint8_t> bad_stsh = {0x00, 0x00, 0x03, 0x00, 0x08, 0x00};
  PutBlob(word, 1024, bad_stsh);
  Put32(word, 88 + 8, 1024);
  Put32(word, 92 + 8, static_cast<uint32_t>(bad_stsh.size()));
  word[1536] = 0x04;
  word[1537] = 0x00;
  word[1538] = 0xFF;  // cbFfnM1 runs past the table.
  word[1539] = 0x00;
  Put32(word, 88 + 15 * 8, 1536);
  Put32(word, 92 + 15 * 8, 4);
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  EXPECT_TRUE(document->styles.empty());
  EXPECT_TRUE(document->fonts.empty());
  EXPECT_FALSE(document->warnings.empty());
  ASSERT_EQ(document->paragraphs.size(), 1U);
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "AB");
}

TEST(LegacyStyles, MalformedRecordsWarnInsteadOfFailing) {
  const std::vector<uint8_t> chpx = {0x5D, 0x00, 0x00};
  auto name = LegacyStd(1, 0x0FFF, 0, "Normal", nullptr, &chpx);
  name[8] = 0xFF;  // Pascal name extends beyond the record.
  auto upx = LegacyStd(1, 0x0FFF, 0, "Normal", nullptr, &chpx);
  Put16(upx, 16, 0xFFFF);  // CHPX length extends beyond the record.
  const std::vector<uint8_t> short_record = {0, 0, 0, 0};
  const std::vector<std::vector<uint8_t>> records = {name, name, upx,
                                                     short_record};
  auto stsh = LegacyStsh(records);
  Append16(stsh, 0xFFFF);  // Final record length extends beyond STSH.
  Put16(stsh, 2, static_cast<uint16_t>(records.size() + 1));
  std::vector<uint8_t> word = MakeWord6Base(2048);
  PutBlob(word, 1024, stsh);
  Put32(word, 88 + 8, 1024);
  Put32(word, 92 + 8, static_cast<uint32_t>(stsh.size()));
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  const auto& warnings = document->warnings;
  EXPECT_EQ(std::count(warnings.begin(), warnings.end(),
                       "Word 6/95 style record has a malformed name"),
            1);
  EXPECT_EQ(std::count(warnings.begin(), warnings.end(),
                       "Word 6/95 style record has a malformed UPX"),
            1);
  EXPECT_EQ(std::count(warnings.begin(), warnings.end(),
                       "Word 6/95 stylesheet has a truncated record"),
            2);
}

TEST(LegacyStyles, StshiFontFillsOnlyMissingNormalFont) {
  const auto normal = LegacyStd(1, 0x0FFF, 0, "Normal", nullptr, nullptr);
  auto stsh = LegacyStsh({normal});
  stsh.insert(stsh.begin() + 6, 10, 0);  // Extend STSHI from 4 to 14 bytes.
  Put16(stsh, 0, 14);
  Put16(stsh, 14, 1);  // STSHI standard ASCII font index.
  std::vector<uint8_t> word = MakeWord6Base(2048);
  PutBlob(word, 1024, stsh);
  Put32(word, 88 + 8, 1024);
  Put32(word, 92 + 8, static_cast<uint32_t>(stsh.size()));
  const auto ffn = LegacyFfn(
      {LegacyFfnEntry(0, "Arial"), LegacyFfnEntry(0, "Times New Roman")});
  PutBlob(word, 1536, ffn);
  Put32(word, 88 + 15 * 8, 1536);
  Put32(word, 92 + 15 * 8, static_cast<uint32_t>(ffn.size()));
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->styles.size(), 1U);
  EXPECT_EQ(document->styles[0].run.font, "Times New Roman");
  EXPECT_EQ(document->styles[0].resolved_run.font, "Times New Roman");
  // These STSHI variants have no default half-point size field.
  EXPECT_EQ(document->styles[0].run.size, 0);
}
}  // namespace
