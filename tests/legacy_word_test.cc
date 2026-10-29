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

// Builds a Word 6/95 document whose only CHPX range references a 66-byte PICF
// at `pic` declaring `payload` bytes of MFPF anisotropic metafile behind it.
std::vector<uint8_t> Word6PictureDocument(size_t size, size_t pic,
                                          size_t payload) {
  constexpr size_t kHeader = 66;
  constexpr size_t kPicfTypeOffset = 6;
  constexpr size_t kPicfGoalOffset = 28;
  constexpr size_t kPicfScaleOffset = 32;
  std::vector<uint8_t> word(size);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put16(word, 6, 0x0409);
  Put32(word, 24, 768);
  Put32(word, 28, 770);
  Put32(word, 52, 2);
  word[768] = 1;  // Picture marker.
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
  Put32(word, 1526, static_cast<uint32_t>(pic));
  word[1535] = 1;
  Put32(word, pic, kHeader + payload);
  Put16(word, pic + 4, kHeader);
  Put16(word, pic + kPicfTypeOffset, 8);  // MFPF anisotropic metafile.
  Put16(word, pic + kPicfGoalOffset, 1440);
  Put16(word, pic + kPicfGoalOffset + 2, 720);
  Put16(word, pic + kPicfScaleOffset, 1000);
  Put16(word, pic + kPicfScaleOffset + 2, 1000);
  return word;
}

TEST(LegacyWord, Word6PicfMetafileBecomesMediaPart) {
  constexpr size_t kPic = 2200;
  constexpr size_t kWmf = 18;
  auto word = Word6PictureDocument(2560, kPic, kWmf);
  constexpr size_t kHeader = 66;
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
  EXPECT_EQ(image.error().kind, doccraft::doc::ErrorKind::kMalformed);
}

TEST(LegacyWord, Word6PictureLimitErrorIsNotDegradable) {
  constexpr size_t kPic = 2200;
  constexpr size_t kWmf = 18;
  auto word = Word6PictureDocument(2560, kPic, kWmf);
  // A payload above the remaining total-image budget is a resource limit, not
  // a local picture defect: formatted parsing must fail the document.
  auto limited = doccraft::doc::ReadLegacyPicture(word, kPic, 0);
  ASSERT_FALSE(limited);
  EXPECT_EQ(limited.error().message, "Word 6/95 image size exceeds limit");
  EXPECT_EQ(limited.error().kind, doccraft::doc::ErrorKind::kResourceLimit);
  // A malformed payload stays a local defect that degrades to a warning.
  auto malformed = doccraft::doc::ReadLegacyPicture(word, kPic - 1);
  ASSERT_FALSE(malformed);
  EXPECT_NE(malformed.error().message, "Word 6/95 image size exceeds limit");
  EXPECT_EQ(malformed.error().kind, doccraft::doc::ErrorKind::kMalformed);
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

// Builds a Word 6/95 document whose single PAPX FKP range carries `sprms`.
// `sprms` must be an even number of bytes so the BX length covers exactly the
// two style bytes plus the modifiers.
std::vector<uint8_t> Word6PapxDocument(const std::vector<uint8_t>& sprms) {
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
  // PAPX BTE PLC at 2060 points at FKP page 3 (1536).
  Put32(word, 88 + 13 * 8, 2060);
  Put32(word, 92 + 13 * 8, 10);
  Put32(word, 2060, 768);
  Put32(word, 2064, 772);
  Put16(word, 2068, 3);
  // PAPX FKP: one range, BX at 504 carrying style 0 and `sprms`.
  Put32(word, 1536, 768);
  Put32(word, 1540, 772);
  word[1544] = 252;
  word[2040] = static_cast<uint8_t>((2 + sprms.size()) / 2);
  std::copy(sprms.begin(), sprms.end(), word.begin() + 2043);
  word[2047] = 1;
  return word;
}

// Builds a Word 6/95 document whose single PAPX FKP range carries `sprms`,
// with the BX parked at page offset 500 so the grpprl has eight bytes instead
// of the four available at the page tail. `sprms` must be an even number of
// bytes.
std::vector<uint8_t> Word6PapxDocumentWithRoom(
    const std::vector<uint8_t>& sprms) {
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
  // PAPX BTE PLC at 2060 points at FKP page 3 (1536).
  Put32(word, 88 + 13 * 8, 2060);
  Put32(word, 92 + 13 * 8, 10);
  Put32(word, 2060, 768);
  Put32(word, 2064, 772);
  Put16(word, 2068, 3);
  // PAPX FKP: one range, BX at page offset 500 (rgb[0] = 250).
  Put32(word, 1536, 768);
  Put32(word, 1540, 772);
  word[1544] = 250;
  word[2036] = static_cast<uint8_t>((2 + sprms.size()) / 2);
  std::copy(sprms.begin(), sprms.end(), word.begin() + 2039);
  word[2047] = 1;
  return word;
}

std::expected<doccraft::model::Document, doccraft::doc::Error> ParseWord6(
    const std::vector<uint8_t>& word) {
  auto fib = doccraft::doc::ParseFib(word);
  if (!fib)
    return std::unexpected(fib.error());
  return doccraft::doc::ParseFormatted(word, word, *fib);
}

TEST(LegacyWord, Word6FkpMapsFrameWrapAndLanguage) {
  // sprmPWr (0x25, one byte) is a paragraph property: wrap=2 followed by
  // sprmPJc (0x05) right align proves the next modifier is still applied.
  auto paragraph = ParseWord6(Word6PapxDocument({0x25, 2, 0x05, 2}));
  ASSERT_TRUE(paragraph) << paragraph.error().message;
  ASSERT_EQ(paragraph->paragraphs.size(), 1u);
  EXPECT_EQ(paragraph->paragraphs[0].frame_wrap, 2);
  EXPECT_EQ(paragraph->paragraphs[0].align, 2);
  auto paragraph_zip = doccraft::docx::Write(*paragraph);
  ASSERT_TRUE(paragraph_zip) << paragraph_zip.error().message;
  EXPECT_NE(Part(*paragraph_zip, "word/document.xml")
                .find("<w:framePr w:wrap=\"around\""),
            std::string::npos);
  // sprmCLid (0x61, two bytes LID) expands to sprmCRgLid0 and sprmCRgLid1 on
  // the run. Bold and italic survive the scan as well, but they also survive
  // the legacy skip path, so only the language assertions prove the mapping.
  auto run =
      ParseWord6(Word6ChpxDocument({0x55, 1, 0x61, 0x09, 0x04, 0x56, 1}));
  ASSERT_TRUE(run) << run.error().message;
  ASSERT_EQ(run->paragraphs.size(), 1u);
  ASSERT_EQ(run->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(run->paragraphs[0].runs[0].bold);
  EXPECT_TRUE(run->paragraphs[0].runs[0].italic);
  EXPECT_EQ(run->paragraphs[0].runs[0].lang, "en-US");
  EXPECT_EQ(run->paragraphs[0].runs[0].lang_east_asia, "en-US");
  auto run_zip = doccraft::docx::Write(*run);
  ASSERT_TRUE(run_zip) << run_zip.error().message;
  EXPECT_NE(Part(*run_zip, "word/document.xml")
                .find("<w:lang w:val=\"en-US\" w:eastAsia=\"en-US\""),
            std::string::npos);
}

TEST(LegacyWord, Word6ClidSetsDefaultAndEastAsianLanguage) {
  // Word 6/95 sprmCLid carries a single LID that wvWare applies to both
  // lidDefault and lidFE, so the modern split must write the same operand to
  // sprmCRgLid0 (w:val) and sprmCRgLid1 (w:eastAsia).
  auto document = ParseWord6(Word6ChpxDocument({0x61, 0x11, 0x04}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].runs[0].lang, "ja-JP");
  EXPECT_EQ(document->paragraphs[0].runs[0].lang_east_asia, "ja-JP");
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/document.xml")
                .find("<w:lang w:val=\"ja-JP\" w:eastAsia=\"ja-JP\""),
            std::string::npos);
}

TEST(LegacyWord, LegacyPWrInCharacterStreamIgnoredAndLaterCharSprmApplies) {
  // r19 normalizes legacy sprmPWr in every grpprl, but the modern opcode is a
  // paragraph property. In a CHPX it must be a no-op for the run stream rather
  // than shifting the scan: the following bold modifier must still apply, and
  // no frame properties may reach the paragraph or the XML.
  auto document = ParseWord6(Word6ChpxDocument({0x25, 2, 0x55, 1}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].frame_wrap, -1);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].bold);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_EQ(xml.find("w:framePr"), std::string::npos);
  EXPECT_NE(xml.find("<w:b/>"), std::string::npos);
}

TEST(LegacyWord, LegacyClidInParagraphStreamIgnoredAndLaterParaSprmApplies) {
  // Legacy sprmCLid expands to sprmCRgLid0/sprmCRgLid1, both character
  // properties. A PAPX carrying it must not leak w:lang onto the run, and the
  // following sprmPDxaRight (0x10) must still set the paragraph indent.
  auto document = ParseWord6(
      Word6PapxDocumentWithRoom({0x61, 0x09, 0x04, 0x10, 0x40, 0x01}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].right, 320);
  EXPECT_TRUE(document->paragraphs[0].right_set);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].lang.empty());
  EXPECT_TRUE(document->paragraphs[0].runs[0].lang_east_asia.empty());
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_EQ(xml.find("w:lang"), std::string::npos);
  EXPECT_NE(xml.find("<w:ind w:right=\"320\""), std::string::npos);
}

TEST(LegacyWord, Word6OversizedPictureFailsWholeDocument) {
  constexpr size_t kPic = 2200;
  constexpr size_t kHeader = 66;
  const size_t payload = doccraft::doc::detail::kMaxImageSize + 1;
  auto word = Word6PictureDocument(kPic + kHeader + payload, kPic, payload);
  // KNOWN-SIMPLIFICATIONS keeps the per-image 64MiB limit fatal; only picture
  // payload defects may be dropped with a warning.
  auto parsed = ParseWord6(word);
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error().message, "Word 6/95 image size exceeds limit");
}

TEST(LegacyWord, Word6SkipsUnmappedSizedSprmAndKeepsFollowing) {
  // bold, sprmPFSideBySide (0x06, one byte, unmapped), italic.
  auto document = ParseWord6(Word6ChpxDocument({0x55, 1, 0x06, 0, 0x56, 1}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  ASSERT_EQ(document->paragraphs[0].runs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].runs[0].bold);
  EXPECT_TRUE(document->paragraphs[0].runs[0].italic);
}

TEST(LegacyWord, Word6SkipsUnmappedTwoByteSprmAndKeepsFollowing) {
  // bold, sprmCIbstRMark (0x45, two bytes, unmapped), italic.
  auto document =
      ParseWord6(Word6ChpxDocument({0x55, 1, 0x45, 0x09, 0x04, 0x56, 1}));
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
  // bold, sprmPFSideBySide, sprmCIbstRMark, italic -> both mapped sprms
  // survive, but the result is reported as partial because two sprms were
  // skipped.
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      std::vector<uint8_t>{0x55, 1, 0x06, 0, 0x45, 0x09, 0x04, 0x56, 1}, out));
  ASSERT_EQ(out.size(), 6u);
  EXPECT_EQ(out[0], 0x35);
  EXPECT_EQ(out[1], 0x08);  // little-endian sprmCFBold (0x0835)
  EXPECT_EQ(out[3], 0x36);
  EXPECT_EQ(out[4], 0x08);  // little-endian sprmCFItalic (0x0836)
}

TEST(LegacyWord, LegacyScanDistinguishesSkipFromStop) {
  std::vector<uint8_t> out;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x55, 1, 0x06, 0}, out),
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

TEST(LegacyWord, LegacyScanStopsWhenMappedSprmLacksLengthByte) {
  std::vector<uint8_t> out;
  // sprmPChgTabsPapx (0x0F) is mapped and takes a cb byte, but the stream ends
  // right after the opcode. Its width is unknowable, so the scan must stop
  // instead of reporting a complete parse.
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x55, 1, 0x0F}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  // Only the leading bold survives; the unmappable 0x0F emits nothing.
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[2], 0x01);  // sprmCFBold operand.
  // A cb byte promising five operand bytes with only one present is truncated.
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x0F, 0x05, 0xAA}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  // NormalizeLegacySprms keeps its historical "not complete" answer.
  EXPECT_FALSE(doccraft::doc::detail::NormalizeLegacySprms(
      std::vector<uint8_t>{0x0F}, out));
}

TEST(LegacyWord, LegacyScanResolvesChgTabsExtendedLength) {
  // sprmPChgTabs (0x17) with cb=0xFF carries a ChgTabsOperand: the deleted and
  // inserted tab counts replace the byte count (LibreOffice ww8scan.cxx
  // GetSprmTailLen, case 23). One deleted tab occupies 4 bytes, one inserted
  // tab 3, so the operand ends after 3 + 4 + 3 bytes and italic still applies.
  std::vector<uint8_t> out;
  std::vector<uint8_t> raw = {0x17, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00,
                              0x01, 0x00, 0x00, 0x00, 0x56, 0x01};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kPartial);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], 0x36);
  EXPECT_EQ(out[1], 0x08);  // little-endian sprmCFItalic (0x0836).
  EXPECT_EQ(out[2], 0x01);
  // sprmPChgTabsPapx (0x0F) shares the layout and is dropped, not translated.
  out.clear();
  raw[0] = 0x0F;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], 0x36);
  // Empty counts leave a three-byte operand: 0xFF, nDel=0, nIns=0.
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x17, 0xFF, 0x00, 0x00, 0x56, 0x01}, out),
            doccraft::doc::detail::LegacySprmScan::kPartial);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], 0x36);
  // nDel claims four entries but only two bytes follow it.
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x17, 0xFF, 0x04, 0x00, 0x00}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  // nIns points past the buffer.
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x17, 0xFF, 0x00, 0x03, 0x00, 0x00}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
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
  // fComplex plus FibBase.fExtChar: Word 6/95 stores 16-bit text and says so
  // in the FIB, not in the piece descriptors.
  Put16(word, 10, 0x1004);
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
