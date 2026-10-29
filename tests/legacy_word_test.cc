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
#include <span>
#include <string>
#include <vector>

#include "miniz.h"

#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/internal.h"
#include "doc/text.h"
#include "docx/writer.h"

namespace {
void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}
void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  Put16(b, p, static_cast<uint16_t>(v));
  Put16(b, p + 2, static_cast<uint16_t>(v >> 16));
}

std::string Part(const std::vector<uint8_t>& bytes, const char* name) {
  mz_zip_archive zip = {};
  if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
    return {};
  size_t size = 0;
  void* data = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
  std::string result;
  if (data) {
    result.assign(static_cast<const char*>(data), size);
    mz_free(data);
  }
  mz_zip_reader_end(&zip);
  return result;
}

TEST(LegacyWord, Word6PicfMetafileBecomesMediaPart) {
  std::vector<uint8_t> word(2560);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put16(word, 6, 0x0409);
  Put32(word, 24, 768);
  Put32(word, 28, 770);
  Put32(word, 52, 2);
  word[768] = 1;
  word[769] = '\r';
  Put32(word, 88 + 12 * 8, 2048);
  Put32(word, 92 + 12 * 8, 10);
  Put32(word, 2048, 768);
  Put32(word, 2052, 770);
  Put16(word, 2056, 2);
  Put32(word, 1024, 768);
  Put32(word, 1028, 770);
  word[1032] = 250;
  word[1524] = 5;
  word[1525] = 0x44;  // Word 6 sprmCPicLocation, four-byte FC.
  Put32(word, 1526, 2200);
  word[1535] = 1;
  constexpr size_t kPic = 2200;
  constexpr size_t kHeader = 66;
  constexpr size_t kWmf = 18;
  Put32(word, kPic, kHeader + kWmf);
  Put16(word, kPic + 4, kHeader);
  Put16(word, kPic + 6, 8);  // MFPF anisotropic metafile.
  Put16(word, kPic + 28, 1440);
  Put16(word, kPic + 30, 720);
  Put16(word, kPic + 32, 1000);
  Put16(word, kPic + 34, 1000);
  Put16(word, kPic + kHeader, 1);      // WMF memory metafile.
  Put16(word, kPic + kHeader + 2, 9);  // Header size in words.
  Put16(word, kPic + kHeader + 4, 0x0300);
  Put32(word, kPic + kHeader + 6, 9);
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto parsed = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->images.size(), 1u);
  EXPECT_EQ(parsed->images[0].format, doccraft::model::ImageFormat::kWmf);
  EXPECT_EQ(parsed->images[0].width, 1440);
  EXPECT_EQ(parsed->images[0].height, 720);
  ASSERT_EQ(parsed->paragraphs[0].runs.size(), 1u);
  EXPECT_EQ(parsed->paragraphs[0].runs[0].image_index, 0);
  auto zip = doccraft::docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_EQ(
      Part(*zip, "word/media/image1.wmf"),
      std::string(reinterpret_cast<const char*>(word.data() + kPic + kHeader),
                  kWmf));
}

TEST(LegacyWord, Word6BitmapPicfReportsUnsupportedType) {
  std::vector<uint8_t> word(100);
  Put32(word, 0, 100);
  Put16(word, 4, 66);
  Put16(word, 6, 99);  // MFPF bitmap.
  Put16(word, 28, 1440);
  Put16(word, 30, 720);
  Put16(word, 32, 1000);
  Put16(word, 34, 1000);
  auto image = doccraft::doc::ReadLegacyPicture(word, 0);
  ASSERT_FALSE(image);
  EXPECT_EQ(image.error().message, "unsupported Word 6/95 bitmap PICF");
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

// Builds a Word 6/95 document whose single CHPX FKP range carries `sprms`.
std::vector<uint8_t> Word6ChpxDocument(const std::vector<uint8_t>& sprms) {
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
  Put32(word, 88 + 12 * 8, 2048);
  Put32(word, 92 + 12 * 8, 10);
  Put32(word, 2048, 768);
  Put32(word, 2052, 772);
  Put16(word, 2056, 2);
  // CHPX FKP: one run at FC 768-772.
  Put32(word, 1024, 768);
  Put32(word, 1028, 772);
  word[1032] = 250;
  word[1524] = static_cast<uint8_t>(sprms.size());
  std::copy(sprms.begin(), sprms.end(), word.begin() + 1525);
  word[1535] = 1;
  return word;
}

std::expected<doccraft::model::Document, doccraft::doc::Error> ParseWord6(
    const std::vector<uint8_t>& word) {
  auto fib = doccraft::doc::ParseFib(word);
  if (!fib)
    return std::unexpected(fib.error());
  return doccraft::doc::ParseFormatted(word, word, *fib);
}

TEST(LegacyWord, Word6SkipsUnmappedSizedSprmAndKeepsFollowing) {
  // bold, sprmPWr (0x25, one byte, unmapped), italic.
  auto document = ParseWord6(Word6ChpxDocument({0x55, 1, 0x25, 0, 0x56, 1}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].bold);
  EXPECT_TRUE(document->paragraphs[0].runs[0].italic);
}

TEST(LegacyWord, Word6SkipsUnmappedTwoByteSprmAndKeepsFollowing) {
  // bold, sprmCLid (0x61, two bytes, unmapped), italic.
  auto document =
      ParseWord6(Word6ChpxDocument({0x55, 1, 0x61, 0x09, 0x04, 0x56, 1}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].bold);
  EXPECT_TRUE(document->paragraphs[0].runs[0].italic);
}

TEST(LegacyWord, Word6UnknownOpcodeStillStopsParsing) {
  // italic, opcode 0xF0 is absent from the Word 6 table, then bold. Parsing
  // must stop so 0xF0's operand bytes are not read as modifiers.
  auto word = Word6ChpxDocument({0x56, 1, 0xF0, 0xAA, 0xBB, 0x55, 1});
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].italic);
  EXPECT_FALSE(document->paragraphs[0].runs[0].bold);
  std::vector<uint8_t> normalized;
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      {word.data() + 1525, 7}, normalized));
}

TEST(LegacyWord, Word6SkipsVariableLengthSprmAndKeepsFollowing) {
  // bold, sprmCDefault (0x52, L_VAR with cb=2), then italic.
  auto document =
      ParseWord6(Word6ChpxDocument({0x55, 1, 0x52, 0x02, 0xAA, 0xBB, 0x56, 1}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].bold);
  EXPECT_TRUE(document->paragraphs[0].runs[0].italic);
}

TEST(LegacyWord, Word6SkipsTwoByteLengthSprmAndKeepsFollowing) {
  // bold, sprmTDefTable (0xBE, L_VAR2, length word = payload + 1), then italic.
  auto document = ParseWord6(
      Word6ChpxDocument({0x55, 1, 0xBE, 0x03, 0x00, 0xAA, 0xBB, 0x56, 1}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].bold);
  EXPECT_TRUE(document->paragraphs[0].runs[0].italic);
}

TEST(LegacyWord, NormalizeLegacySprmsRejectsTruncatedOperands) {
  std::vector<uint8_t> out;
  // sprmPDxaRight (0x10) needs two operand bytes, only one is present.
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      std::vector<uint8_t>{0x10, 0x01}, out));
  // sprmCLid (0x61) is known-width but still truncated.
  out.clear();
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      std::vector<uint8_t>{0x61, 0x01}, out));
  // sprmCDefault (0x52) declares five bytes but supplies one.
  out.clear();
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      std::vector<uint8_t>{0x52, 0x05, 0xAA}, out));
  // sprmTDefTable (0xBE) declares a three-byte count but only one byte follows.
  out.clear();
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      std::vector<uint8_t>{0xBE, 0x03}, out));
}

TEST(LegacyWord, NormalizeLegacySprmsSkipsKnownWidthsAndReportsPartial) {
  std::vector<uint8_t> out;
  // bold, sprmPWr, sprmCLid, italic -> both mapped sprms survive, but the
  // result is reported as partial because two sprms were skipped.
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      std::vector<uint8_t>{0x55, 1, 0x25, 0, 0x61, 0x09, 0x04, 0x56, 1}, out));
  ASSERT_EQ(out.size(), 6u);
  EXPECT_EQ(out[0], 0x35);
  EXPECT_EQ(out[1], 0x08);  // little-endian sprmCFBold (0x0835)
  EXPECT_EQ(out[3], 0x36);
  EXPECT_EQ(out[4], 0x08);  // little-endian sprmCFItalic (0x0836)
}

TEST(LegacyWord, LegacyScanDistinguishesSkipFromStop) {
  std::vector<uint8_t> out;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x55, 1, 0x25, 0}, out),
            doccraft::doc::detail::LegacySprmScan::kPartial);
  out.clear();
  EXPECT_EQ(
      doccraft::doc::detail::ScanLegacySprms(std::vector<uint8_t>{0xF0}, out),
      doccraft::doc::detail::LegacySprmScan::kStopped);
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x55, 1}, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
}

TEST(LegacyWord, Word6BadPictureReferenceIsSkippedWithWarning) {
  std::vector<uint8_t> word(2560);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put16(word, 6, 0x0409);
  Put32(word, 24, 768);
  Put32(word, 28, 770);
  Put32(word, 52, 2);
  word[768] = 0x01;  // Picture marker.
  word[769] = '\r';
  Put32(word, 88 + 12 * 8, 2048);
  Put32(word, 92 + 12 * 8, 10);
  Put32(word, 2048, 768);
  Put32(word, 2052, 770);
  Put16(word, 2056, 2);
  Put32(word, 1024, 768);
  Put32(word, 1028, 770);
  word[1032] = 250;
  word[1524] = 5;
  word[1525] = 0x44;          // sprmCPicLocation.
  Put32(word, 1526, 0xFFFF);  // Far outside the 2560-byte stream.
  word[1535] = 1;
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  EXPECT_TRUE(document->images.empty());
  bool warned = false;
  for (const auto& warning : document->warnings)
    warned = warned || warning.find("Word 6/95 picture") != std::string::npos;
  EXPECT_TRUE(warned);
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
