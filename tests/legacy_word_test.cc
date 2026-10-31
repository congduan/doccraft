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

// Returns each <w:tc> element intact so a test can assert per-cell properties
// and cell text instead of only the table grid.
std::vector<std::string> TableCells(const std::string& xml) {
  constexpr const char* kOpen = "<w:tc>";
  constexpr const char* kClose = "</w:tc>";
  std::vector<std::string> cells;
  size_t p = 0;
  while ((p = xml.find(kOpen, p)) != std::string::npos) {
    const size_t end = xml.find(kClose, p);
    if (end == std::string::npos)
      break;
    const size_t after = end + std::string(kClose).size();
    cells.push_back(xml.substr(p, after - p));
    p = after;
  }
  return cells;
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

// Builds a Word 6/95 document whose single PAPX FKP range carries `sprms`,
// with the BX at page offset 100 so the grpprl may run to several dozen bytes.
// `sprms` must be an even number of bytes.
std::vector<uint8_t> Word6PapxDocumentWithWideGrpprl(
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
  // PAPX FKP: one range, BX at page offset 100 (rgb word 50).
  Put32(word, 1536, 768);
  Put32(word, 1540, 772);
  word[1544] = 50;
  word[1636] = static_cast<uint8_t>((2 + sprms.size()) / 2);
  std::copy(sprms.begin(), sprms.end(), word.begin() + 1639);
  word[2047] = 1;
  return word;
}

// Builds a Word 6/95 document holding one one-row table with `cell_count`
// physical cells (default two). Each non-final cell paragraph and its cell
// mark carry `cell_sprms`; the final cell paragraph and the row-end mark (0x07
// with sprmPFTtp) carry `row_sprms`, and the closing CR is a body paragraph
// outside the table with default properties. Both grpprls must be an even
// number of bytes so the BX length covers exactly the two style bytes plus the
// modifiers.
std::vector<uint8_t> Word6TableDocument(const std::vector<uint8_t>& cell_sprms,
                                        const std::vector<uint8_t>& row_sprms,
                                        size_t cell_count = 2) {
  std::vector<uint8_t> word(2560);
  Put16(word, 0, 0xA5DC);
  Put16(word, 2, 0x65);
  Put16(word, 6, 0x0409);
  const uint32_t text_begin = 768;
  const uint32_t text_end = text_begin + 2 * cell_count + 1;
  Put32(word, 24, text_begin);
  Put32(word, 28, text_end);
  Put32(word, 52, 2 * cell_count + 1);
  for (size_t i = 0; i < cell_count; ++i) {
    word[text_begin + 2 * i] = static_cast<uint8_t>('A' + i);
    word[text_begin + 2 * i + 1] = 0x07;  // Cell or row-end mark.
  }
  word[text_end - 1] = '\r';  // Body paragraph after the table.
  // PAPX BTE PLC at 2060 covers the whole body FC range.
  Put32(word, 88 + 13 * 8, 2060);
  Put32(word, 92 + 13 * 8, 10);
  Put32(word, 2060, text_begin);
  Put32(word, 2064, text_end);
  Put16(word, 2068, 3);  // FKP page 3.
  // PAPX FKP with one range per cell plus the trailing body paragraph. The
  // final cell range also owns the row-end mark, so it carries `row_sprms`.
  const size_t ranges = cell_count + 1;
  for (size_t j = 0; j < ranges; ++j) {
    Put32(word, 1536 + 4 * j, text_begin + 2 * j);
    Put32(word, 1540 + 4 * j,
          j + 1 == ranges ? text_end : text_begin + 2 * (j + 1));
  }
  const size_t rgb = 1536 + 4 * (ranges + 1);
  for (size_t j = 0; j < ranges; ++j) {
    // Cell PAPXes sit near the front; the row PAPX gets the wide region so a
    // long edit grpprl still fits before the body PAPX at page offset 480.
    size_t offset = 480;
    if (j + 1 != ranges) {
      offset = j + 1 == cell_count ? 64 + 16 * (cell_count - 1) : 64 + 16 * j;
    }
    word[rgb + 7 * j] = static_cast<uint8_t>(offset / 2);
    if (j + 1 == ranges) {
      word[1536 + offset] = 1;  // Body PAPX: style only, no modifiers.
      continue;
    }
    const auto& sprms = j + 1 == cell_count ? row_sprms : cell_sprms;
    word[1536 + offset] = static_cast<uint8_t>((2 + sprms.size()) / 2);
    std::copy(sprms.begin(), sprms.end(), word.begin() + 1536 + offset + 3);
  }
  word[2047] = static_cast<uint8_t>(ranges);
  return word;
}

std::expected<doccraft::model::Document, doccraft::doc::Error> ParseWord6(
    const std::vector<uint8_t>& word) {
  auto fib = doccraft::doc::ParseFib(word);
  if (!fib)
    return std::unexpected(fib.error());
  return doccraft::doc::ParseFormatted(word, word, *fib);
}

// Word 6/95 sprmTDefTable (0xBE) for a two-column 720/720 row: cb, itcMac,
// three XAS centres and two default ten-byte TC6 cells.
const std::vector<uint8_t> kLegacyTwoColumnDefinition = {
    0xBE, 0x1C, 0x00, 0x02, 0x00, 0x00, 0xD0, 0x02, 0xA0, 0x05,
    0x00, 0x00, 0,    0,    0,    0,    0,    0,    0,    0,
    0x00, 0x00, 0,    0,    0,    0,    0,    0,    0,    0,
};

// Builds a table with `cell_count` physical cells whose row-end PAPX appends
// `edit_sprms` after the two-column definition, then closes the row with
// sprmPFTtp/sprmPFInTable. The physical cell count must match the definition
// after the edits for the writer to emit a cell for every definition entry.
std::expected<doccraft::model::Document, doccraft::doc::Error>
ParseWord6TableEdits(const std::vector<uint8_t>& edit_sprms,
                     size_t cell_count = 2) {
  std::vector<uint8_t> row = kLegacyTwoColumnDefinition;
  row.insert(row.end(), edit_sprms.begin(), edit_sprms.end());
  row.insert(row.end(), {0x19, 1, 0x18, 1});  // sprmPFTtp, sprmPFInTable.
  if (row.size() % 2)
    row.push_back(0);  // PAPX word alignment.
  return ParseWord6(Word6TableDocument({0x18, 1}, row, cell_count));
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

TEST(LegacyWord, LegacyScanTranslatesChgTabsOpcode) {
  // sprmPChgTabsPapx (0x0F) and sprmPChgTabs (0x17) are translated onto the
  // Word 97+ opcodes that already feed Paragraph::tabs; the operand keeps its
  // byte layout (both carry cb first, then the deleted and added counts).
  std::vector<uint8_t> out;
  // cb=2: no deleted stop, no added stop.
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x0F, 0x02, 0x00, 0x00}, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  ASSERT_EQ(out.size(), 5u);
  EXPECT_EQ(out[0], 0x0D);
  EXPECT_EQ(out[1], 0xC6);  // little-endian sprmPChgTabsPapx (0xC60D).
  EXPECT_EQ(out[2], 0x02);  // cb, then the two zero counts.
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x17, 0x02, 0x00, 0x00}, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  ASSERT_EQ(out.size(), 5u);
  EXPECT_EQ(out[0], 0x15);
  EXPECT_EQ(out[1], 0xC6);  // little-endian sprmPChgTabs (0xC615).
}

TEST(LegacyWord, Word6ChgTabsPapxReachesDocx) {
  // Word 6/95 sprmPChgTabsPapx uses the PChgTabsPapxOperand layout: a cb byte,
  // one deleted count with 2-byte XAS entries, then one added count with
  // 2-byte positions and one TBD byte each ([MS-DOC] 2.9.180/2.9.183). Real
  // Word 95 samples confirm the 2-byte entry width: 080_080742.doc carries
  // 0F 07 01 0000 01 30FD 00 (cb=7 with a 2-byte deleted position).
  auto document = ParseWord6(
      Word6PapxDocumentWithWideGrpprl({0x0F, 0x0A, 0x01, 0x30, 0xFD, 0x02, 0xD0,
                                       0x02, 0xA0, 0x05, 0x09, 0x02}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  const auto& tabs = document->paragraphs[0].tabs;
  ASSERT_EQ(tabs.size(), 3u);
  EXPECT_TRUE(tabs[0].clear);
  EXPECT_EQ(tabs[0].position, -720);
  EXPECT_EQ(tabs[1].position, 720);
  EXPECT_EQ(tabs[1].align, 1);
  EXPECT_EQ(tabs[1].leader, 1);
  EXPECT_EQ(tabs[2].position, 1440);
  EXPECT_EQ(tabs[2].align, 2);
  EXPECT_EQ(tabs[2].leader, 0);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:tab w:val=\"clear\" w:pos=\"-720\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:tab w:val=\"center\" w:pos=\"720\" w:leader=\"dot\""),
            std::string::npos);
  EXPECT_NE(xml.find("<w:tab w:val=\"right\" w:pos=\"1440\""),
            std::string::npos);
}

TEST(LegacyWord, Word6ChgTabsClearOnlyReachesDocx) {
  // The gap r36 closes: a paragraph that only deletes a style-inherited stop.
  // sprmPChgTabsPapx with one deleted position and no additions must still
  // emit w:tab w:val="clear" so Word drops the inherited stop.
  auto document = ParseWord6(
      Word6PapxDocumentWithWideGrpprl({0x0F, 0x04, 0x01, 0xD0, 0x02, 0x00}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  const auto& tabs = document->paragraphs[0].tabs;
  ASSERT_EQ(tabs.size(), 1u);
  EXPECT_TRUE(tabs[0].clear);
  EXPECT_EQ(tabs[0].position, 720);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/document.xml")
                .find("<w:tab w:val=\"clear\" w:pos=\"720\"/>"),
            std::string::npos);
}

TEST(LegacyWord, Word6ChgTabsFlatFormReachesDocx) {
  // sprmPChgTabs (0x17) carries a ChgTabsOperand: the deleted positions are
  // 32-bit (LibreOffice ww8scan.cxx GetSprmTailLen resolves the same width for
  // its case 23 / 0xC615). The trailing zero is PAPX word padding.
  auto document = ParseWord6(
      Word6PapxDocumentWithWideGrpprl({0x17, 0x09, 0x01, 0x30, 0xFD, 0xFF, 0xFF,
                                       0x01, 0xD0, 0x02, 0x09, 0x00}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  const auto& tabs = document->paragraphs[0].tabs;
  ASSERT_EQ(tabs.size(), 2u);
  EXPECT_TRUE(tabs[0].clear);
  EXPECT_EQ(tabs[0].position, -720);
  EXPECT_EQ(tabs[1].position, 720);
  EXPECT_EQ(tabs[1].align, 1);
  EXPECT_EQ(tabs[1].leader, 1);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:tab w:val=\"clear\" w:pos=\"-720\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:tab w:val=\"center\" w:pos=\"720\" w:leader=\"dot\""),
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
  const std::vector<uint8_t> raw = {0x17, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00,
                                    0x01, 0x00, 0x00, 0x00, 0x56, 0x01};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kPartial);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], 0x36);
  EXPECT_EQ(out[1], 0x08);  // little-endian sprmCFItalic (0x0836).
  EXPECT_EQ(out[2], 0x01);
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

TEST(LegacyWord, LegacyScanStopsOnPapxChgTabsExtendedCb) {
  // PChgTabsPapxOperand defines cb in 2..254 ([MS-DOC] 2.9.180), so cb=0xFF
  // has no width for sprmPChgTabsPapx (0x0F). The ChgTabsOperand counts belong
  // to sprmPChgTabs (0x17) only: measuring the 0x0F operand with them would
  // guess at a 4-byte deleted-entry layout the Papx form never carries. Stop
  // the scan instead, so the FKP reader reports the unsupported sprm.
  std::vector<uint8_t> out;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0x0F, 0xFF, 0x00, 0x00}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  EXPECT_TRUE(out.empty());
  // A preceding modifier survives, but the sprmPJc behind the extended operand
  // is never reached: its operand bytes would otherwise be read as opcodes.
  out.clear();
  EXPECT_EQ(
      doccraft::doc::detail::ScanLegacySprms(
          std::vector<uint8_t>{0x55, 1, 0x0F, 0xFF, 0x00, 0x00, 0x05, 2}, out),
      doccraft::doc::detail::LegacySprmScan::kStopped);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], 0x35);
  EXPECT_EQ(out[1], 0x08);  // little-endian sprmCFBold (0x0835).
  EXPECT_EQ(out[2], 0x01);
  // The FKP path turns the stop into its unsupported-sprm warning, and the
  // paragraph keeps no tab stops.
  auto document = ParseWord6(Word6PapxDocument({0x0F, 0xFF, 0x00, 0x00}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_TRUE(document->paragraphs[0].tabs.empty());
  bool warned = false;
  for (const auto& warning : document->warnings)
    warned = warned || warning == "Word 6/95 FKP contains unsupported sprms";
  EXPECT_TRUE(warned);
}

TEST(LegacyWord, LegacyScanMapsEastAsianParagraphFlags) {
  // [MS-DOC] 2.9.215 lists the one-byte isprm space shared with Word 6/95:
  // 0x35..0x3A are sprmPFKinsoku, sprmPFWordWrap, sprmPFOverflowPunct,
  // sprmPFTopLinePunct, sprmPFAutoSpaceDE and sprmPFAutoSpaceDN, each with a
  // one-byte Bool8 operand. This synthetic byte stream exercises the same
  // flags; the `37 00 3A 00 39 00` tail observed in Bug51944.doc's '內文'
  // style motivates translating them instead of stopping.
  std::vector<uint8_t> out;
  const std::vector<uint8_t> raw = {0x35, 0, 0x36, 0, 0x37, 0,
                                    0x38, 0, 0x39, 0, 0x3A, 0};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> expected = {
      0x33, 0x24, 0,   // sprmPFKinsoku (0x2433)
      0x34, 0x24, 0,   // sprmPFWordWrap (0x2434)
      0x35, 0x24, 0,   // sprmPFOverflowPunct (0x2435)
      0x36, 0x24, 0,   // sprmPFTopLinePunct (0x2436)
      0x37, 0x24, 0,   // sprmPFAutoSpaceDE (0x2437)
      0x38, 0x24, 0};  // sprmPFAutoSpaceDN (0x2438)
  EXPECT_EQ(out, expected);
  // A modifier behind the flags still reaches the IR.
  auto document = ParseWord6(
      Word6PapxDocumentWithRoom({0x37, 0, 0x3A, 0, 0x39, 0, 0x05, 2}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].align, 2);
  const auto& paragraph = document->paragraphs[0];
  EXPECT_TRUE(paragraph.overflow_punct_set);
  EXPECT_FALSE(paragraph.overflow_punct);
  EXPECT_TRUE(paragraph.auto_space_de_set);
  EXPECT_FALSE(paragraph.auto_space_de);
  EXPECT_TRUE(paragraph.auto_space_dn_set);
  EXPECT_FALSE(paragraph.auto_space_dn);
}

TEST(LegacyWord, LegacyScanConsumesTableShadingWithoutExtraFixedByte) {
  // sprmTDefTableShd (0xBF) is a spra=6 operand ([MS-DOC] 2.2.5.1): the cb
  // byte counts the operand bytes that follow it, so the operand is cb + 1
  // bytes. This synthetic grpprl is built from the `BF 04 01 05 01 05` byte
  // sequence (cb = 4 and four shading bytes) observed at the end of a
  // Bug50955.doc PAPX grpprl; adding the one fixed byte LibreOffice lists for
  // L_VAR overruns the grpprl by one byte and stops the scan. The six bytes
  // map unchanged to sprmTDefTableShd80 (0xD609), whose DefTableShd80Operand
  // is the same cb + 2-byte palette-index pairs ([MS-DOC] 2.9.52).
  std::vector<uint8_t> out;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0xBF, 0x04, 0x01, 0x05, 0x01, 0x05}, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  EXPECT_EQ(out,
            (std::vector<uint8_t>{0x09, 0xD6, 0x04, 0x01, 0x05, 0x01, 0x05}));
  // The paragraph modifier behind the skipped operand still applies, and no
  // unsupported-sprm warning is raised.
  auto document = ParseWord6(Word6PapxDocumentWithWideGrpprl(
      {0xBF, 0x04, 0x01, 0x05, 0x01, 0x05, 0x05, 0x02}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].align, 2);
  for (const auto& warning : document->warnings)
    EXPECT_NE(warning, "Word 6/95 FKP contains unsupported sprms");
}

TEST(LegacyWord, LegacyScanMapsTableProperties) {
  // Word 6/95 table sprms 0xB6..0xBD share the TAP layout that Word 97+
  // keeps: LibreOffice ww8scan.cxx GetWW6SprmSearcher lists 0xB6/0xB7/0xB8
  // and 0xBD as two-byte, 0xB9/0xBA as one-byte operands ("tap.jc jc (low
  // order byte is significant)", "tap.fCantSplit 1 or 0 byte", ...). The
  // modern handlers read sprmTJc/sprmTDxaLeft/sprmTDxaGapHalf as two bytes
  // ([MS-DOC] 2.9.144/2.9.145), sprmTFCantSplit/sprmTTableHeader as Bool8 and
  // sprmTDyaRowHeight as a signed YAS. All operands pass through unchanged
  // except sprmTJc: only its low byte is significant, so a stray high byte
  // is cleared (see the case below).
  std::vector<uint8_t> out;
  const std::vector<uint8_t> raw = {0xB6, 0x01, 0x00, 0xB7, 0x40, 0x01,
                                    0xB8, 0x6C, 0x00, 0xB9, 0x01, 0xBA,
                                    0x01, 0xBD, 0x2C, 0x01};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> expected = {
      0x8A, 0x54, 0x01, 0x00,  // sprmTJc (0x548A), center.
      0x01, 0x96, 0x40, 0x01,  // sprmTDxaLeft (0x9601), 320 twips.
      0x02, 0x96, 0x6C, 0x00,  // sprmTDxaGapHalf (0x9602), 108 twips.
      0x66, 0x34, 0x01,        // sprmTFCantSplit (0x3466).
      0x04, 0x34, 0x01,        // sprmTTableHeader (0x3404).
      0x07, 0x94, 0x2C, 0x01,  // sprmTDyaRowHeight (0x9407), 300 twips.
  };
  EXPECT_EQ(out, expected);
  // Only the low byte of the legacy sprmTJc word is significant, so a stray
  // high byte is cleared: the modern handler rejects values above 2.
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0xB6, 0x02, 0xFF}, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  EXPECT_EQ(out, (std::vector<uint8_t>{0x8A, 0x54, 0x02, 0x00}));
  // A truncated operand still stops the scan.
  out.clear();
  EXPECT_EQ(
      doccraft::doc::detail::ScanLegacySprms(std::vector<uint8_t>{0xB9}, out),
      doccraft::doc::detail::LegacySprmScan::kStopped);
  EXPECT_TRUE(out.empty());
}

TEST(LegacyWord, LegacyScanMapsLegacyTableEdits) {
  // Word 6/95 table-edit sprms 0xC2..0xC6 keep the operand layout their Word
  // 97+ namesakes use, so the bytes pass through unchanged:
  //   sprmTInsert  (4): itcFirst, ctc, dxaCol;
  //   sprmTDelete  (2): ItcFirstLim;
  //   sprmTDxaCol  (4): itcFirst, itcLim, dxaCol;
  //   sprmTMerge   (2): ItcFirstLim;
  //   sprmTSplit   (2): ItcFirstLim.
  // Evidence: wvWare sprm.c rgsprmWord6 maps 194..198 onto the modern ids and
  // wvApplysprmTInsert/TDelete/TDxaCol/TMerge/TSplit read exactly those
  // fields; [MS-DOC] 2.6.3 gives sprmTInsert a TInsertOperand, sprmTDelete/
  // sprmTMerge/sprmTSplit an ItcFirstLim and sprmTDxaCol a TDxaColOperand.
  // LibreOffice ww8scan.cxx GetWW6SprmSearcher lists the same 4/2/4/2/2
  // widths, and its ProcessSprmTInsert/TDelete/TDxaCol read the same fields.
  std::vector<uint8_t> out;
  const std::vector<uint8_t> raw = {
      0xC2, 0x01, 0x01, 0x20, 0x03,  // Insert at 1, one 800-twip cell.
      0xC3, 0x00, 0x01,              // Delete [0, 1).
      0xC4, 0x00, 0x02, 0x84, 0x03,  // Width [0, 2) = 900 twips.
      0xC5, 0x00, 0x02,              // Merge [0, 2).
      0xC6, 0x01, 0x02,              // Split [1, 2).
      0x05, 0x02,                    // sprmPJc right: later bytes still read.
  };
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> expected = {
      0x21, 0x76, 0x01, 0x01, 0x20, 0x03,  // sprmTInsert (0x7621).
      0x22, 0x56, 0x00, 0x01,              // sprmTDelete (0x5622).
      0x23, 0x76, 0x00, 0x02, 0x84, 0x03,  // sprmTDxaCol (0x7623).
      0x24, 0x56, 0x00, 0x02,              // sprmTMerge (0x5624).
      0x25, 0x56, 0x01, 0x02,              // sprmTSplit (0x5625).
      0x03, 0x24, 0x02,                    // sprmPJc80 right.
  };
  EXPECT_EQ(out, expected);
  // Each operand is 2 or 4 bytes; an opcode alone is still truncated.
  for (uint8_t code : {0xC2, 0xC3, 0xC4, 0xC5, 0xC6}) {
    out.clear();
    EXPECT_EQ(
        doccraft::doc::detail::ScanLegacySprms(std::vector<uint8_t>{code}, out),
        doccraft::doc::detail::LegacySprmScan::kStopped);
    EXPECT_TRUE(out.empty());
  }
}

TEST(LegacyWord, Word6TablePropertiesReachDocx) {
  // Synthetic byte stream built from the LibreOffice v6 widths (see
  // LegacyScanMapsTableProperties) and the [MS-DOC] operand semantics:
  // sprmTJc center, sprmTDxaLeft 320 twips, sprmTDxaGapHalf 108 twips and
  // sprmTDyaRowHeight 300 twips on the cell paragraph, sprmTFCantSplit and
  // sprmTTableHeader on the row-end mark that also carries sprmPFTtp.
  const std::vector<uint8_t> table = {
      0xB6, 0x01, 0x00,  // sprmTJc: center.
      0xB7, 0x40, 0x01,  // sprmTDxaLeft: 320 twips.
      0xB8, 0x6C, 0x00,  // sprmTDxaGapHalf: 108 twips.
      0xBD, 0x2C, 0x01,  // sprmTDyaRowHeight: 300 twips.
  };
  std::vector<uint8_t> cell = table;
  cell.insert(cell.end(), {0x18, 1});  // sprmPFInTable.
  std::vector<uint8_t> row = table;
  row.insert(row.end(), {0xB9, 1});  // sprmTFCantSplit.
  row.insert(row.end(), {0xBA, 1});  // sprmTTableHeader.
  row.insert(row.end(), {0x19, 1});  // sprmPFTtp.
  row.insert(row.end(), {0x18, 1});  // sprmPFInTable.
  auto document = ParseWord6(Word6TableDocument(cell, row));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->tables.size(), 1u);
  const auto& table_ir = document->tables[0];
  EXPECT_EQ(table_ir.align, 1);
  EXPECT_EQ(table_ir.left_origin, 320);
  EXPECT_EQ(table_ir.gap_half, 108);
  EXPECT_EQ(table_ir.indent, 212);
  ASSERT_EQ(table_ir.rows.size(), 1u);
  ASSERT_EQ(table_ir.row_properties.size(), 1u);
  EXPECT_TRUE(table_ir.row_properties[0].cant_split);
  EXPECT_TRUE(table_ir.row_properties[0].header);
  EXPECT_EQ(table_ir.row_properties[0].height, 300);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  for (const char* fragment :
       {"<w:jc w:val=\"center\"/>", "<w:tblInd w:w=\"212\" w:type=\"dxa\"/>",
        "<w:cantSplit/>", "<w:tblHeader/>",
        "<w:trHeight w:val=\"300\" w:hRule=\"atLeast\"/>"})
    EXPECT_NE(xml.find(fragment), std::string::npos) << fragment << "\n" << xml;
  // sprmTDyaRowHeight carries a signed YAS: a negative operand marks an exact
  // row height. The parser keeps the sign and the writer takes the absolute
  // value with w:hRule="exact", so BD AC FE (-340) must not collapse into a
  // positive atLeast height. The other TAP sprms repeat the positive case so
  // the row structure is identical and only the height differs.
  const std::vector<uint8_t> exact_height = {
      0xB6, 0x01, 0x00,  // sprmTJc: center.
      0xB7, 0x40, 0x01,  // sprmTDxaLeft: 320 twips.
      0xB8, 0x6C, 0x00,  // sprmTDxaGapHalf: 108 twips.
      0xBD, 0xAC, 0xFE,  // sprmTDyaRowHeight: -340 twips.
  };
  std::vector<uint8_t> exact_cell = exact_height;
  exact_cell.insert(exact_cell.end(), {0x18, 1});  // sprmPFInTable.
  std::vector<uint8_t> exact_row = exact_height;
  exact_row.insert(exact_row.end(), {0x19, 1});  // sprmPFTtp.
  exact_row.insert(exact_row.end(), {0x18, 1});  // sprmPFInTable.
  auto exact = ParseWord6(Word6TableDocument(exact_cell, exact_row));
  ASSERT_TRUE(exact) << exact.error().message;
  ASSERT_EQ(exact->tables.size(), 1u);
  ASSERT_EQ(exact->tables[0].row_properties.size(), 1u);
  EXPECT_EQ(exact->tables[0].row_properties[0].height, -340);
  auto exact_zip = doccraft::docx::Write(*exact);
  ASSERT_TRUE(exact_zip) << exact_zip.error().message;
  const std::string exact_xml = Part(*exact_zip, "word/document.xml");
  EXPECT_NE(exact_xml.find("<w:trHeight w:val=\"340\" w:hRule=\"exact\"/>"),
            std::string::npos)
      << exact_xml;
}

TEST(LegacyWord, Word6TableWidthAndInsertReachDocx) {
  // sprmTDxaCol [0,2) = 900 rewrites both columns, then sprmTInsert at
  // position 1 adds one 800-twip column, so the grid becomes 900/800/900.
  // Both operands share the modern layout (see LegacyScanMapsLegacyTableEdits)
  // and are applied by the existing spra=5 table dispatcher. The synthetic
  // row has three physical cells so the text stream matches the three cell
  // definitions that remain after the insert.
  auto document = ParseWord6TableEdits(
      {
          0xC4, 0x00, 0x02, 0x84, 0x03,  // sprmTDxaCol: [0,2) = 900 twips.
          0xC2, 0x01, 0x01, 0x20, 0x03,  // sprmTInsert: at 1, one 800 cell.
      },
      3);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->tables.size(), 1u);
  const auto& table = document->tables[0];
  EXPECT_EQ(table.columns, (std::vector<int>{900, 800, 900}));
  ASSERT_EQ(table.row_properties.size(), 1u);
  ASSERT_EQ(table.row_properties[0].cells.size(), 3u);
  EXPECT_EQ(table.row_properties[0].cells[0].width, 900);
  EXPECT_EQ(table.row_properties[0].cells[1].width, -1);
  EXPECT_EQ(table.row_properties[0].cells[2].width, 900);
  ASSERT_EQ(table.rows.size(), 1u);
  EXPECT_EQ(table.rows[0].size(), 3u);
  for (const auto& warning : document->warnings)
    EXPECT_NE(warning, "Word 6/95 FKP contains unsupported sprms");
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(
      xml.find("<w:tblGrid><w:gridCol w:w=\"900\"/><w:gridCol w:w=\"800\"/>"
               "<w:gridCol w:w=\"900\"/></w:tblGrid>"),
      std::string::npos)
      << xml;
  const auto cells = TableCells(xml);
  ASSERT_EQ(cells.size(), 3u) << xml;
  // The inserted middle cell has no per-cell width of its own and falls back
  // to the inserted grid column; its text is the original second cell "B".
  for (const char* fragment :
       {"<w:tcW w:w=\"900\" w:type=\"dxa\"/>", ">A</w:t>"})
    EXPECT_NE(cells[0].find(fragment), std::string::npos) << fragment << "\n"
                                                          << cells[0];
  for (const char* fragment :
       {"<w:tcW w:w=\"800\" w:type=\"dxa\"/>", ">B</w:t>"})
    EXPECT_NE(cells[1].find(fragment), std::string::npos) << fragment << "\n"
                                                          << cells[1];
  for (const char* fragment :
       {"<w:tcW w:w=\"900\" w:type=\"dxa\"/>", ">C</w:t>"})
    EXPECT_NE(cells[2].find(fragment), std::string::npos) << fragment << "\n"
                                                          << cells[2];
}

TEST(LegacyWord, Word6TableDeleteReachesDocx) {
  // sprmTDelete [0,1) removes the first cell and its grid column, leaving a
  // single 720-twip column. One physical cell keeps the text stream in step
  // with the one definition that remains.
  auto document = ParseWord6TableEdits({0xC3, 0x00, 0x01}, 1);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->tables.size(), 1u);
  const auto& table = document->tables[0];
  EXPECT_EQ(table.columns, (std::vector<int>{720}));
  ASSERT_EQ(table.row_properties.size(), 1u);
  ASSERT_EQ(table.row_properties[0].cells.size(), 1u);
  EXPECT_EQ(table.row_properties[0].cells[0].width, 720);
  ASSERT_EQ(table.rows.size(), 1u);
  EXPECT_EQ(table.rows[0].size(), 1u);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:tblGrid><w:gridCol w:w=\"720\"/></w:tblGrid>"),
            std::string::npos)
      << xml;
  const auto cells = TableCells(xml);
  ASSERT_EQ(cells.size(), 1u) << xml;
  EXPECT_NE(cells[0].find("<w:tcW w:w=\"720\" w:type=\"dxa\"/>"),
            std::string::npos)
      << cells[0];
  EXPECT_NE(cells[0].find(">A</w:t>"), std::string::npos) << cells[0];
}

TEST(LegacyWord, Word6TableEmptyRangesAreNoOps) {
  // [MS-DOC] 2.9.123 ItcFirstLim: itcLim may equal itcFirst, which denotes a
  // zero-cell range. Delete, merge, split and column-width edits must accept
  // that operand and leave the row untouched instead of rejecting the whole
  // document at FinishTableRow.
  auto document = ParseWord6TableEdits({
      0xC3, 0x01, 0x01,              // sprmTDelete: empty range.
      0xC5, 0x01, 0x01,              // sprmTMerge: empty range.
      0xC6, 0x01, 0x01,              // sprmTSplit: empty range.
      0xC4, 0x01, 0x01, 0x84, 0x03,  // sprmTDxaCol: empty range.
  });
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->tables.size(), 1u);
  const auto& table = document->tables[0];
  EXPECT_EQ(table.columns, (std::vector<int>{720, 720}));
  ASSERT_EQ(table.row_properties.size(), 1u);
  ASSERT_EQ(table.row_properties[0].cells.size(), 2u);
  for (const auto& cell : table.row_properties[0].cells) {
    EXPECT_EQ(cell.width, 720);
    EXPECT_EQ(cell.horizontal_merge, 0);
  }
  ASSERT_EQ(table.rows.size(), 1u);
  EXPECT_EQ(table.rows[0].size(), 2u);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_EQ(TableCells(xml).size(), 2u) << xml;
  EXPECT_EQ(xml.find("<w:hMerge"), std::string::npos) << xml;
}

TEST(LegacyWord, Word6TableEditErrorsRejectDocument) {
  // Invalid table edits keep the existing whole-document error path: zero
  // inserted cells, out-of-bounds or impossible ItcFirstLim ranges (including
  // an empty range at the cell count) and an insert beyond the 63-cell cap all
  // surface as the modern opcode rejected by FinishTableRow.
  auto zero_insert = ParseWord6TableEdits({
      0xC2, 0x01, 0x00, 0x20, 0x03,  // sprmTInsert: ctc = 0.
  });
  ASSERT_FALSE(zero_insert);
  EXPECT_NE(zero_insert.error().message.find("invalid TAPX operand 30241"),
            std::string::npos)
      << zero_insert.error().message;

  auto out_of_range = ParseWord6TableEdits({
      0xC3, 0x00, 0x03,  // sprmTDelete: itcLim 3 on a two-cell row.
  });
  ASSERT_FALSE(out_of_range);
  EXPECT_NE(out_of_range.error().message.find("invalid TAPX operand 22050"),
            std::string::npos)
      << out_of_range.error().message;

  // [MS-DOC] 2.9.123 ItcFirstLim: itcFirst MUST be less than the row's cell
  // count, so [2,2) is malformed even though itcLim may equal itcFirst. Only
  // an empty range below the cell count is a no-op; this whole-row operand
  // must still take the whole-document error path.
  auto empty_whole_row_delete = ParseWord6TableEdits({
      0xC3, 0x02, 0x02,  // sprmTDelete: itcFirst == itcLim == 2.
  });
  ASSERT_FALSE(empty_whole_row_delete);
  EXPECT_NE(
      empty_whole_row_delete.error().message.find("invalid TAPX operand 22050"),
      std::string::npos)
      << empty_whole_row_delete.error().message;

  auto empty_whole_row_merge = ParseWord6TableEdits({
      0xC5, 0x02, 0x02,  // sprmTMerge: itcFirst == itcLim == 2.
  });
  ASSERT_FALSE(empty_whole_row_merge);
  EXPECT_NE(
      empty_whole_row_merge.error().message.find("invalid TAPX operand 22052"),
      std::string::npos)
      << empty_whole_row_merge.error().message;

  // Grow the two-cell definition to the 63-cell cap, then insert one more.
  std::vector<uint8_t> edits;
  for (int i = 0; i < 62; ++i)
    edits.insert(edits.end(), {0xC2, 0x01, 0x01, 0x20, 0x03});
  auto over_cap = ParseWord6TableEdits(edits);
  ASSERT_FALSE(over_cap);
  EXPECT_NE(over_cap.error().message.find("invalid TAPX operand 30241"),
            std::string::npos)
      << over_cap.error().message;
}

TEST(LegacyWord, Word6TableMergeAndSplitReachDocx) {
  // sprmTMerge [0,2) marks the first cell as the merge start and the second
  // as a continuation; the writer maps those IR flags to w:hMerge. A following
  // sprmTSplit over the same range clears both flags again.
  auto merged = ParseWord6TableEdits({0xC5, 0x00, 0x02});
  ASSERT_TRUE(merged) << merged.error().message;
  ASSERT_EQ(merged->tables.size(), 1u);
  ASSERT_EQ(merged->tables[0].row_properties.size(), 1u);
  ASSERT_EQ(merged->tables[0].row_properties[0].cells.size(), 2u);
  EXPECT_EQ(merged->tables[0].row_properties[0].cells[0].horizontal_merge, 2);
  EXPECT_EQ(merged->tables[0].row_properties[0].cells[1].horizontal_merge, 1);
  auto merged_zip = doccraft::docx::Write(*merged);
  ASSERT_TRUE(merged_zip) << merged_zip.error().message;
  const std::string merged_xml = Part(*merged_zip, "word/document.xml");
  for (const char* fragment :
       {"<w:hMerge w:val=\"restart\"/>", "<w:hMerge w:val=\"continue\"/>"})
    EXPECT_NE(merged_xml.find(fragment), std::string::npos) << fragment << "\n"
                                                            << merged_xml;

  auto split = ParseWord6TableEdits({0xC5, 0x00, 0x02, 0xC6, 0x00, 0x02});
  ASSERT_TRUE(split) << split.error().message;
  ASSERT_EQ(split->tables.size(), 1u);
  ASSERT_EQ(split->tables[0].row_properties.size(), 1u);
  ASSERT_EQ(split->tables[0].row_properties[0].cells.size(), 2u);
  EXPECT_EQ(split->tables[0].row_properties[0].cells[0].horizontal_merge, 0);
  EXPECT_EQ(split->tables[0].row_properties[0].cells[1].horizontal_merge, 0);
  auto split_zip = doccraft::docx::Write(*split);
  ASSERT_TRUE(split_zip) << split_zip.error().message;
  const std::string split_xml = Part(*split_zip, "word/document.xml");
  EXPECT_EQ(split_xml.find("<w:hMerge"), std::string::npos) << split_xml;
}

TEST(LegacyWord, LegacyScanMapsLegacyTableBorders) {
  // Word 6/95 sprmTTableBorders (0xBB) is a fixed 12-byte operand made of six
  // two-byte BRC6 values in rgbrcTable order: top, left, bottom, right,
  // insideH, insideV. LibreOffice ww8scan.cxx GetWW6SprmSearcher lists it as
  // {12, L_FIX} ("tap.rgbrcTable complex 12 bytes") and wvWare reads it with
  // wvApplysprmTTableBorders as six cb6BRC values (sprm.c). BRC6 packs
  // width(3), type(2), shadow(1), ico(5) and space(5) (wvGetBRC_internal6).
  // A 4x3-byte layout is contradicted by both sources and by the fixed
  // width: six borders of two bytes fit exactly.
  std::vector<uint8_t> out;
  const std::vector<uint8_t> raw = {
      0xBB, 0x4C, 0x08,  // top: width 4, single, ico 1 (black), space 1.
      0x00, 0x00,        // left: no border.
      0x8A, 0x00,  // bottom: width 2, single, ico 2 (blue in Word's COLORREF
                   // palette).
      0x00, 0x00,  // right: no border.
      0x09, 0x00,  // insideH: width 1, single, ico 0 (auto).
      0x00, 0x00,  // insideV: no border.
  };
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> expected = {
      0x13, 0xD6, 0x30,  // sprmTTableBorders, cb = 0x30 (six 8-byte Brc).
      0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x00,  // top.
      0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,  // left, none.
      0x00, 0x00, 0xFF, 0x00, 0x02, 0x01, 0x00, 0x00,  // bottom.
      0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,  // right, none.
      0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x01, 0x00, 0x00,  // insideH.
      0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,  // insideV, none.
  };
  EXPECT_EQ(out, expected);
  // An all-ones BRC6 word is the 2-byte analogue of the BRC80 undefined
  // sentinel; it becomes a fully undefined modern Brc, which TableBorder
  // resets instead of turning into a width-7 double border.
  out.clear();
  const std::vector<uint8_t> undefined = {0xBB, 0xFF, 0xFF, 0, 0, 0, 0,
                                          0,    0,    0,    0, 0, 0};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(undefined, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  ASSERT_EQ(out.size(), 3u + 6u * 8u);
  for (size_t i = 0; i < 8; i++)
    EXPECT_EQ(out[3 + i], 0xFF) << i;
}

TEST(LegacyWord, LegacyScanMapsLegacyTtlp) {
  // Word 6/95 sprmTTlp (0xC0) is a four-byte TLP: itl (2) then grfatl (2).
  // That is exactly the modern TlpOperand ([MS-DOC] 2.9.154 sprmTTlp), so
  // the operand reaches the shared handler unchanged. Bug50955.doc carries
  // `C0 0800B700` on a real table row, which is why this mapping matters.
  std::vector<uint8_t> out;
  const std::vector<uint8_t> raw = {0xC0, 0x08, 0x00, 0xB7, 0x00};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> expected = {0x0A, 0x74, 0x08, 0x00, 0xB7, 0x00};
  EXPECT_EQ(out, expected);
}

TEST(LegacyWord, LegacyScanMapsLegacyTableSetBrc) {
  // Word 6/95 sprmTSetBrc (0xC1) is a fixed five-byte operand without the cb
  // prefix the modern sprmTSetBrc requires: itcFirst, itcLim, sides and a
  // two-byte BRC6 (wvWare sprm.c reads itcFirst/itcLim/sides/BRC; LibreOffice
  // lists {5, L_FIX}). The modern TSetBrcOperand is cb(11) + the same three
  // bytes + an eight-byte Brc, so the scan rebuilds it byte for byte.
  std::vector<uint8_t> out;
  const std::vector<uint8_t> raw = {
      0xC1, 0x00, 0x02, 0x05, 0x4C, 0x08,
  };
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  // BRC6 0x084C: width 4, single, ico 1 (black), space 1. sides 0x05 is top
  // and bottom, which the translator keeps verbatim for the shared handler.
  const std::vector<uint8_t> expected = {
      0x2F, 0xD6, 0x0B, 0x00, 0x02, 0x05, 0x00,
      0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x00,
  };
  EXPECT_EQ(out, expected);
  // An all-ones BRC6 is the undefined sentinel: eight 0xFF bytes, which the
  // modern TableBorder resets to an explicit nil border.
  out.clear();
  const std::vector<uint8_t> undefined = {0xC1, 0x01, 0x03, 0x01, 0xFF, 0xFF};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(undefined, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  std::vector<uint8_t> undefined_expected = {0x2F, 0xD6, 0x0B,
                                             0x01, 0x03, 0x01};
  undefined_expected.insert(undefined_expected.end(), 8, 0xFF);
  EXPECT_EQ(out, undefined_expected);
}

TEST(LegacyWord, LegacyScanMapsLegacyTableSetBrc10) {
  // Word 6/95 sprmTSetBrc10 (0xC7) is the same five-byte shape as 0xC1 but
  // carries a BRC10 word: dxpLine2Width(3) @0, dxpSpaceBetween(3) @3,
  // dxpLine1Width(3) @6, dxpSpace(5) @9, fShadow(1) @14 (wvGetBRC10_internal).
  // wvConvertBRC10ToBRC maps the (line1, gap, line2) triple to a border type;
  // BRC10 has no colour field and no width byte, so the colour is ico 0
  // (auto, -1) and the line-1 width doubles as the modern width.
  std::vector<uint8_t> out;
  // line1=1, gap=0, line2=0, space=1 -> single, width 1.
  const std::vector<uint8_t> single = {0xC7, 0x00, 0x02, 0x01, 0x40, 0x02};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(single, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> single_expected = {
      0x2F, 0xD6, 0x0B, 0x00, 0x02, 0x01, 0xFF,
      0xFF, 0xFF, 0xFF, 0x01, 0x01, 0x01, 0x00,
  };
  EXPECT_EQ(out, single_expected);
  // line1=4, gap=0, line2=0, space=2, shadow -> double, width 4.
  out.clear();
  const std::vector<uint8_t> doubled = {0xC7, 0x00, 0x02, 0x01, 0x00, 0x45};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(doubled, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> doubled_expected = {
      0x2F, 0xD6, 0x0B, 0x00, 0x02, 0x01, 0xFF,
      0xFF, 0xFF, 0xFF, 0x04, 0x03, 0x22, 0x00,
  };
  EXPECT_EQ(out, doubled_expected);
  // Two single lines (1,1,1) also read as double; a zero word stays a
  // width-zero, type-zero border (the modern handler rejects nothing).
  out.clear();
  const std::vector<uint8_t> two_lines = {0xC7, 0x00, 0x02, 0x01, 0x49, 0x00};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(two_lines, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> two_lines_expected = {
      0x2F, 0xD6, 0x0B, 0x00, 0x02, 0x01, 0xFF,
      0xFF, 0xFF, 0xFF, 0x01, 0x03, 0x00, 0x00,
  };
  EXPECT_EQ(out, two_lines_expected);
  // wvWare's hairline and dotted special cases keep their line width (7/6)
  // in the width byte so a recognisable line survives into OOXML.
  out.clear();
  const std::vector<uint8_t> hairline = {0xC7, 0x00, 0x01, 0x01, 0xC0, 0x01};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(hairline, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> hairline_expected = {
      0x2F, 0xD6, 0x0B, 0x00, 0x01, 0x01, 0xFF,
      0xFF, 0xFF, 0xFF, 0x07, 0x05, 0x00, 0x00,
  };
  EXPECT_EQ(out, hairline_expected);
  out.clear();
  const std::vector<uint8_t> dotted = {0xC7, 0x00, 0x01, 0x01, 0x80, 0x01};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(dotted, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> dotted_expected = {
      0x2F, 0xD6, 0x0B, 0x00, 0x01, 0x01, 0xFF,
      0xFF, 0xFF, 0xFF, 0x06, 0x06, 0x00, 0x00,
  };
  EXPECT_EQ(out, dotted_expected);
  out.clear();
  const std::vector<uint8_t> zero = {0xC7, 0x00, 0x01, 0x01, 0x00, 0x00};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(zero, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> zero_expected = {
      0x2F, 0xD6, 0x0B, 0x00, 0x01, 0x01, 0xFF,
      0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
  };
  EXPECT_EQ(out, zero_expected);
}

TEST(LegacyWord, LegacyScanMapsLegacyTableSetShd) {
  // Word 6/95 sprmTSetShd (0xC8) is a fixed four-byte operand: itcFirst,
  // itcLim and a two-byte SHD with icoFore(5) @0, icoBack(5) @5 and
  // ipat(6) @10 (wvGetSHD_internal). The modern sprmTSetShd takes cb(12) +
  // the range + a ten-byte SHD of two COLORREFs and a two-byte pattern, so
  // the scan expands both palette indices through Ico().
  std::vector<uint8_t> out;
  // icoFore 1 (black), icoBack 8 (white), ipat 1 (solid).
  const std::vector<uint8_t> raw = {0xC8, 0x00, 0x02, 0x01, 0x05};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> expected = {
      0x2D, 0xD6, 0x0C, 0x00, 0x02, 0x00, 0x00, 0x00,
      0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x00,
  };
  EXPECT_EQ(out, expected);
  // Palette indices above 15 have no COLORREF and Ico() saturates them to
  // -1, Word's "automatic"; ipat passes through unchanged.
  out.clear();
  const std::vector<uint8_t> beyond = {0xC8, 0x00, 0x01, 0x30, 0x0A};
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(beyond, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  const std::vector<uint8_t> beyond_expected = {
      0x2D, 0xD6, 0x0C, 0x00, 0x01, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00,
  };
  EXPECT_EQ(out, beyond_expected);
}

TEST(LegacyWord, LegacyScanMapsLegacyTableDefinition) {
  // sprmTDefTable (0xBE) and sprmTDefTable10 (0xBC) share the Word 6/95
  // operand: a two-byte cb (bytes after cb, plus one), one-byte itcMac,
  // itcMac+1 XAS centres, then itcMac ten-byte TC6 cells. wvWare documents
  // the header in wvApplysprmTDefTable and the TC6 size as cb6TC=10
  // (wvGetTC_internal); TC6 is a two-byte TCGRF followed by four two-byte
  // BRC10 borders. The modern sprmTDefTable needs 20-byte TC80 cells, so the
  // scan rebuilds the operand with zero cell widths (the column grid supplies
  // the width) and zero borders when no BRC10 is set.
  std::vector<uint8_t> out;
  const std::vector<uint8_t> raw = {
      0xBE, 0x1C, 0x00,                       // cb = 27 bytes after cb + 1.
      0x02,                                   // itcMac = 2.
      0x00, 0x00,                             // 0.
      0xD0, 0x02,                             // 720 twips.
      0xA0, 0x05,                             // 1440 twips.
      0x03, 0x00, 0,    0, 0, 0, 0, 0, 0, 0,  // TC6 1: horzMerge first.
      0x00, 0x00, 0,    0, 0, 0, 0, 0, 0, 0,  // TC6 2: no merge.
  };
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  std::vector<uint8_t> expected = {
      0x08, 0xD6, 0x30, 0x00, 0x02, 0x00, 0x00, 0xD0,
      0x02, 0xA0, 0x05, 0x03, 0x00, 0x00, 0x00,  // TC80 1: tcgrf, wWidth 0.
  };
  expected.insert(expected.end(), 16, 0);  // Four zero BRC80s.
  expected.insert(expected.end(), 20, 0);  // TC80 2, all defaults.
  EXPECT_EQ(out, expected);
}

TEST(LegacyWord, LegacyScanKeepsGridWhenLegacyCellsAreShort) {
  // Bug49933.doc carries 0xBE operands whose cb covers only part of the TC6
  // array (53 of the 108 corpus operands). wvApplysprmTDefTable keeps the
  // column grid and stops before the cells when the remaining bytes cannot
  // hold cb6TC*itcMac, so the scan rebuilds a grid-only operand.
  const std::vector<uint8_t> raw = {
      0xBE, 0x0B, 0x00,              // cb = 10 bytes after cb + 1.
      0x01,                          // itcMac = 1.
      0x00, 0x00, 0xD0, 0x02,        // 0, 720 twips.
      0x01, 0x00, 0x00, 0x00, 0x00,  // Five bytes of a ten-byte TC6.
  };
  std::vector<uint8_t> out;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(raw, out),
            doccraft::doc::detail::LegacySprmScan::kComplete);
  // sprmTDefTableObsolete only carries the grid; its cb covers the one-byte
  // itcMac plus the two XAS centres, plus one.
  const std::vector<uint8_t> expected = {0x06, 0xD6, 0x06, 0x00, 0x01,
                                         0x00, 0x00, 0xD0, 0x02};
  EXPECT_EQ(out, expected);
}

TEST(LegacyWord, LegacyTableOperandsStopWhenTruncated) {
  // Fixed 12-byte borders, a two-byte count, and a one-byte cb are all
  // measured before any byte is trusted: a truncated operand stops the scan.
  std::vector<uint8_t> out;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0xBB, 0x01, 0x02}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0xBE, 0x20, 0x00, 0x01, 0x00}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0xBF, 0x05, 0x01, 0x02}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  out.clear();
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0xBC, 0x05, 0x00, 0x01}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
}

TEST(LegacyWord, Word6TableBordersGridAndShadingReachDocx) {
  // One row, two cells. The row grpprl carries the three legacy table sprms in
  // document order: 0xBB borders, 0xBE the two-column grid and 0xBF shading.
  // The trailing 0x00 is PAPX word alignment. The 0xBB bytes are the ones from
  // LegacyScanMapsLegacyTableBorders; the grid is 720+720 twips; SHD 0x0500 is
  // white/solid and SHD 0x0041 is blue with a black foreground.
  const std::vector<uint8_t> row = {
      0xBB, 0x4C, 0x08, 0x00, 0x00, 0x8A, 0x00, 0x00, 0x00, 0x09, 0x00,
      0x00, 0x00, 0xBE, 0x1C, 0x00, 0x02, 0x00, 0x00, 0xD0, 0x02, 0xA0,
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBF,
      0x04, 0x00, 0x05, 0x41, 0x00, 0x19, 0x01,  // sprmPFTtp.
      0x18, 0x01,                                // sprmPFInTable.
      0x00,                                      // PAPX word alignment.
  };
  auto document = ParseWord6(Word6TableDocument({0x18, 1}, row));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->tables.size(), 1u);
  const auto& table = document->tables[0];
  EXPECT_EQ(table.columns, (std::vector<int>{720, 720}));
  ASSERT_EQ(table.row_properties.size(), 1u);
  ASSERT_EQ(table.row_properties[0].cells.size(), 2u);
  EXPECT_EQ(table.row_properties[0].cells[0].shade, 0xFFFFFF);
  EXPECT_EQ(table.row_properties[0].cells[0].shade_fore, -1);
  EXPECT_EQ(table.row_properties[0].cells[0].shade_pattern, 1);
  EXPECT_EQ(table.row_properties[0].cells[1].shade, 0xFF0000);
  EXPECT_EQ(table.row_properties[0].cells[1].shade_fore, 0);
  EXPECT_EQ(table.row_properties[0].cells[1].shade_pattern, 0);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  for (const char* fragment :
       {"<w:tblBorders>", "<w:gridCol w:w=\"720\"/>",
        "<w:shd w:val=\"solid\" w:fill=\"FFFFFF\" w:color=\"auto\"/>",
        "<w:shd w:val=\"clear\" w:fill=\"0000FF\" w:color=\"000000\"/>"})
    EXPECT_NE(xml.find(fragment), std::string::npos) << fragment << "\n" << xml;
}

TEST(LegacyWord, LegacyScanStopsAfterTablePropertiesOnUnknownOpcode) {
  // The six table sprms are mapped, but an opcode outside the Word 6 table
  // still stops the scan so its operand bytes are never read as modifiers.
  std::vector<uint8_t> out;
  EXPECT_EQ(doccraft::doc::detail::ScanLegacySprms(
                std::vector<uint8_t>{0xB9, 1, 0xBA, 1, 0xF0, 0xAA}, out),
            doccraft::doc::detail::LegacySprmScan::kStopped);
  EXPECT_EQ(out, (std::vector<uint8_t>{0x66, 0x34, 1, 0x04, 0x34, 1}));
  auto document =
      ParseWord6(Word6PapxDocumentWithRoom({0xB9, 1, 0xBA, 1, 0xF0, 0xAA}));
  ASSERT_TRUE(document) << document.error().message;
  bool warned = false;
  for (const auto& warning : document->warnings)
    warned = warned || warning == "Word 6/95 FKP contains unsupported sprms";
  EXPECT_TRUE(warned);
}

TEST(LegacyWord, Word6TableCellBorderShadingReachDocx) {
  // A real Word 6/95 row: the two-column definition first (the cell range must
  // exist before a range-limited border or shading operand), then sprmTTlp
  // (0xC0), sprmTSetBrc (0xC1) and sprmTSetShd (0xC8). The TTlp bytes are the
  // `C0 0800B700` operand from Bug50955.doc; the border is a 4-wide single
  // black top and the shading is black-on-white solid for both cells.
  const std::vector<uint8_t> row =
      {
          0xBE, 0x1C, 0x00, 0x02, 0x00, 0x00, 0xD0, 0x02, 0xA0,
          0x05, 0x00, 0x00, 0,    0,    0,    0,    0,    0,
          0,    0,    0x00, 0x00, 0,    0,    0,    0,    0,
          0,    0,    0,    0xC0, 0x08, 0x00, 0xB7, 0x00,  // sprmTTlp.
          0xC1, 0x00, 0x02, 0x01, 0x4C, 0x08,  // sprmTSetBrc, top only.
          0xC8, 0x00, 0x02, 0x01, 0x05,        // sprmTSetShd.
          0x19, 0x01, 0x18, 0x01,              // sprmPFTtp, sprmPFInTable.
      };
  auto document = ParseWord6(Word6TableDocument({0x18, 1}, row));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->tables.size(), 1u);
  const auto& table = document->tables[0];
  // grfatl 0x00B7 has neither band flag, so the shared handler adds the two
  // "no band" look bits: (0xB7 & 0x01E0) | 0x0200 | 0x0400 = 0x06A0.
  EXPECT_EQ(table.look, 0x06A0);
  ASSERT_EQ(table.row_properties.size(), 1u);
  ASSERT_EQ(table.row_properties[0].cells.size(), 2u);
  for (const auto& cell : table.row_properties[0].cells) {
    EXPECT_TRUE(cell.borders[0].set);
    EXPECT_EQ(cell.borders[0].type, 1);
    EXPECT_EQ(cell.borders[0].width, 4);
    EXPECT_EQ(cell.borders[0].color, 0);
    EXPECT_EQ(cell.borders[0].space, 1);
    EXPECT_FALSE(cell.borders[2].set);
    EXPECT_EQ(cell.shade, 0xFFFFFF);
    EXPECT_EQ(cell.shade_fore, 0);
    EXPECT_EQ(cell.shade_pattern, 1);
  }
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  for (const char* fragment :
       {"<w:tblLook w:val=\"06A0\"/>",
        "<w:tcBorders><w:top w:val=\"single\" w:sz=\"4\" "
        "w:color=\"000000\" w:space=\"1\"/></w:tcBorders>",
        "<w:shd w:val=\"solid\" w:fill=\"FFFFFF\" w:color=\"000000\"/>"})
    EXPECT_NE(xml.find(fragment), std::string::npos) << fragment << "\n" << xml;
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

// Builds a Word 6/95 document whose text plane is body text followed by the
// header/footer stories. `section_flags` holds each section's sprmSGprfIhdt
// value; the stories are laid out in PLC order after the body.
struct LegacyHeaderSample {
  std::vector<std::string> stories;
  std::vector<uint8_t> section_flags;
  std::vector<uint32_t> section_ends;
  std::vector<uint32_t> plcf_cps;
  bool facing_pages = false;
  bool title_page = false;
  uint8_t word_ihdt = 0;
  bool truncate_plcf = false;
  bool missing_sed = false;
  // Extra section sprms appended to each section's SEPX, in section order; a
  // missing entry means the section carries no extra properties.
  std::vector<std::vector<uint8_t>> section_sprms;
  // Leaves every SED's fcSepx at the undefined value so the section has no
  // SEPX at all.
  bool no_sepx = false;
  uint32_t dop_lcb = 88;
};

std::vector<uint8_t> Word6HeaderDocument(uint16_t ident, uint16_t nfib,
                                         const std::string& body,
                                         const LegacyHeaderSample& sample) {
  constexpr size_t kFcMin = 768;
  constexpr size_t kDop = 400;
  constexpr size_t kSed = 600;
  constexpr size_t kSepx = 660;
  constexpr size_t kHdd = 720;
  std::vector<uint8_t> word(4096, 0);
  Put16(word, 0, ident);
  Put16(word, 2, nfib);
  Put16(word, 6, 0x0409);
  Put32(word, 24, kFcMin);
  uint32_t story_bytes = 0;
  for (const auto& story : sample.stories)
    story_bytes += static_cast<uint32_t>(story.size());
  Put32(word, 52, static_cast<uint32_t>(body.size()));
  Put32(word, 56, 0);
  Put32(word, 60, story_bytes);
  Put32(word, 28, static_cast<uint32_t>(kFcMin + body.size() + story_bytes));
  std::copy(body.begin(), body.end(), word.begin() + kFcMin);
  size_t at = kFcMin + body.size();
  for (const auto& story : sample.stories) {
    std::copy(story.begin(), story.end(), word.begin() + at);
    at += story.size();
  }
  // DOP: fFacingPages plus the word-level grpfIhdt skip count (zero here).
  Put32(word, 88 + 31 * 8, kDop);
  Put32(word, 92 + 31 * 8, sample.dop_lcb);
  word[kDop] = sample.facing_pages ? 1 : 0;
  word[kDop + 1] = sample.word_ihdt;
  // Plcfsed: aCP array then 12-byte SEDs. Word 6/95 keeps it in WordDocument.
  const size_t sections = sample.section_flags.size();
  if (!sample.missing_sed) {
    Put32(word, 88 + 6 * 8, kSed);
    Put32(word, 92 + 6 * 8,
          static_cast<uint32_t>(4 * (sections + 1) + 12 * sections));
    for (size_t i = 0; i < sections; ++i)
      Put32(word, kSed + 4 * (i + 1), sample.section_ends[i]);
    for (size_t i = 0; i < sections; ++i) {
      size_t pos = kSed + 4 * (sections + 1) + 12 * i;
      size_t sepx = kSepx + 40 * i;
      Put32(word, pos + 2,
            sample.no_sepx ? 0xFFFFFFFFu : static_cast<uint32_t>(sepx));
      Put32(word, pos + 8, 0xFFFFFFFF);
      if (sample.no_sepx)
        continue;
      size_t at = sepx + 2;
      word[at++] = 0x99;  // sprmSGprfIhdt.
      word[at++] = sample.section_flags[i];
      if (sample.title_page) {
        word[at++] = 0x8F;  // sprmSFTitlePage.
        word[at++] = 1;
      }
      if (i < sample.section_sprms.size()) {
        const auto& extra = sample.section_sprms[i];
        std::copy(extra.begin(), extra.end(), word.begin() + at);
        at += extra.size();
      }
      Put16(word, sepx, static_cast<uint16_t>(at - sepx - 2));
    }
  }
  // PlcfHdd: only the stories present in the document are stored.
  Put32(word, 88 + 11 * 8, kHdd);
  const size_t cps = sample.plcf_cps.size();
  Put32(word, 92 + 11 * 8,
        sample.truncate_plcf ? 8 : static_cast<uint32_t>(4 * cps));
  for (size_t i = 0; i < cps && (i < 2 || !sample.truncate_plcf); ++i)
    Put32(word, kHdd + 4 * i, sample.plcf_cps[i]);
  return word;
}

LegacyHeaderSample HeaderSample(std::vector<std::string> stories,
                                std::vector<uint8_t> flags,
                                std::vector<uint32_t> ends) {
  LegacyHeaderSample sample;
  sample.section_ends = std::move(ends);
  sample.plcf_cps.push_back(0);
  for (const auto& story : stories)
    sample.plcf_cps.push_back(sample.plcf_cps.back() +
                              static_cast<uint32_t>(story.size()));
  sample.stories = std::move(stories);
  sample.section_flags = std::move(flags);
  return sample;
}

TEST(LegacyWord, Word6HeaderAndFooterStoriesReachDocx) {
  auto sample = HeaderSample({"HDR\r", "FTR\r"}, {0x0A}, {4});
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->ccp_hdd, 8u);
  EXPECT_EQ(fib->ccp_ftn, 0u);
  auto document = doccraft::doc::ParseFormatted(word, word, *fib);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->section.headers[1].size(), 1u);
  EXPECT_EQ(document->section.headers[1][0].runs[0].text, "HDR");
  ASSERT_EQ(document->section.headers[3].size(), 1u);
  EXPECT_EQ(document->section.headers[3][0].runs[0].text, "FTR");
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/header1.xml").find("HDR"), std::string::npos);
  EXPECT_NE(Part(*zip, "word/footer2.xml").find("FTR"), std::string::npos);
}

TEST(LegacyWord, Word95EvenHeaderFollowsFacingPagesFlag) {
  auto sample = HeaderSample({"EVEN\r", "ODD\r"}, {0x03}, {6});
  sample.facing_pages = true;
  auto word = Word6HeaderDocument(0xA699, 0x68, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->section.headers[0].size(), 1u);
  EXPECT_EQ(document->section.headers[0][0].runs[0].text, "EVEN");
  ASSERT_EQ(document->section.headers[1].size(), 1u);
  EXPECT_EQ(document->section.headers[1][0].runs[0].text, "ODD");
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("w:headerReference w:type=\"even\""), std::string::npos);
  EXPECT_NE(xml.find("w:headerReference w:type=\"default\""),
            std::string::npos);
}

TEST(LegacyWord, Word6PerSectionHeaderStoriesFollowSectionOrder) {
  auto sample = HeaderSample({"ONE\r", "TWO\r"}, {0x02, 0x02}, {3, 5});
  auto word = Word6HeaderDocument(0xA5DC, 0x65,
                                  "A\r\x0c"
                                  "B\r",
                                  sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->section_breaks.size(), 1u);
  ASSERT_EQ(document->section_breaks[0].second.headers[1].size(), 1u);
  EXPECT_EQ(document->section_breaks[0].second.headers[1][0].runs[0].text,
            "ONE");
  ASSERT_EQ(document->section.headers[1].size(), 1u);
  EXPECT_EQ(document->section.headers[1][0].runs[0].text, "TWO");
}

TEST(LegacyWord, Word6SepxSectionPropertiesReachDocx) {
  // SXaPage/SYaPage/SDxaLeft/SDyaTop, two columns with spacing, the
  // three-byte sprmSDxaColWidth form and sprmSFTitlePage.
  auto sample = HeaderSample({"HDR\r"}, {0x02}, {4});
  sample.section_sprms = {{0xA4, 0x84, 0x2E, 0xA5, 0xC6, 0x41, 0xA6, 0x6E,
                           0x04, 0xA8, 0x6E, 0x04, 0x90, 0x01, 0x00, 0x91,
                           0x88, 0x01, 0x88, 0x00, 0xF0, 0x05, 0x8F, 0x01}};
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  EXPECT_EQ(document->section.width, 11908);
  EXPECT_EQ(document->section.height, 16838);
  EXPECT_EQ(document->section.left, 1134);
  EXPECT_EQ(document->section.top, 1134);
  EXPECT_EQ(document->section.columns, 2);
  EXPECT_EQ(document->section.column_space, 392);
  ASSERT_EQ(document->section.column_specs.size(), 1u);
  EXPECT_EQ(document->section.column_specs[0].index, 0);
  EXPECT_EQ(document->section.column_specs[0].width, 1520);
  EXPECT_TRUE(document->section.first_page_different);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:pgSz w:w=\"11908\" w:h=\"16838\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:pgMar w:left=\"1134\" w:right=\"1134\" "
                     "w:top=\"1134\" w:bottom=\"1134\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:titlePg/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:cols w:num=\"2\" w:space=\"392\" "
                     "w:equalWidth=\"0\"><w:col w:w=\"1520\"/></w:cols>"),
            std::string::npos);
}

TEST(LegacyWord, Word6PerSectionSepxPropertiesFollowSection) {
  auto sample = HeaderSample({"ONE\r", "TWO\r"}, {0x02, 0x02}, {3, 5});
  sample.section_sprms = {{0xA4, 0x84, 0x2E}, {0xA4, 0xD0, 0x2F}};
  auto word = Word6HeaderDocument(0xA5DC, 0x65,
                                  "A\r\x0c"
                                  "B\r",
                                  sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->section_breaks.size(), 1u);
  EXPECT_EQ(document->section_breaks[0].second.width, 11908);
  EXPECT_EQ(document->section.width, 12240);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:pgSz w:w=\"11908\""), std::string::npos);
  EXPECT_NE(xml.find("<w:pgSz w:w=\"12240\""), std::string::npos);
}

TEST(LegacyWord, Word6SectionWithoutSepxKeepsDefaultProperties) {
  auto sample = HeaderSample({"HDR\r"}, {0x02}, {4});
  sample.no_sepx = true;
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  EXPECT_EQ(document->section.width, 12240);
  EXPECT_EQ(document->section.height, 15840);
  EXPECT_EQ(document->section.left, 1134);
  EXPECT_EQ(document->section.columns, 1);
  EXPECT_FALSE(document->section.first_page_different);
}

TEST(LegacyWord, Word6UnsupportedSectionSprmRejectsDocument) {
  auto sample = HeaderSample({"HDR\r"}, {0x02}, {4});
  sample.section_sprms = {{0xF0, 0xAA}};
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_FALSE(document);
  EXPECT_EQ(document.error().message, "unsupported Word 6/95 SEPX sprm");
}

TEST(LegacyWord, Word6TruncatedSectionSprmRejectsDocument) {
  auto sample = HeaderSample({"HDR\r"}, {0x02}, {4});
  sample.section_sprms = {{0xA4, 0x01}};  // SXaPage needs two operand bytes.
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_FALSE(document);
  EXPECT_EQ(document.error().message, "truncated Word 6/95 SEPX");
}

TEST(LegacyWord, Word6WordLevelStoryBitsSkipLeadingPlcEntries) {
  // DOP.grpfIhdt reserves the leading two stories before the section's own
  // header; only the third PLC entry may become the default header.
  auto sample = HeaderSample({"XX", "YY", "HDR\r"}, {0x02}, {4});
  sample.plcf_cps = {0, 2, 4, 8};
  sample.word_ihdt = 0x03;
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->section.headers[1].size(), 1u);
  EXPECT_EQ(document->section.headers[1][0].runs[0].text, "HDR");
}

TEST(LegacyWord, TruncatedHeaderStoryPlcKeepsBodyWithWarning) {
  auto sample = HeaderSample({"HDR\r", "FTR\r"}, {0x0A}, {4});
  sample.truncate_plcf = true;
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "Body");
  EXPECT_TRUE(document->section.headers[1].empty());
  bool warned = false;
  for (const auto& warning : document->warnings)
    warned =
        warned || warning.find("skipped header stories") != std::string::npos;
  EXPECT_TRUE(warned);
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

size_t CountWarnings(const std::vector<std::string>& warnings,
                     const std::string& needle) {
  size_t hits = 0;
  for (const auto& warning : warnings)
    if (warning.find(needle) != std::string::npos)
      ++hits;
  return hits;
}

size_t Count(const std::string& text, const std::string& needle) {
  size_t hits = 0, at = 0;
  while ((at = text.find(needle, at)) != std::string::npos) {
    ++hits;
    at += needle.size();
  }
  return hits;
}

// Builds a Word 6/95 document with body "A\x02\r" and one footnote body
// "\x02F\r". Pair 2/3 carry the reference and text PLCs in WordDocument, as
// Word 97+ keeps them in the table stream. The footnote story is contiguous
// after the body because this document is not complex.
struct LegacyFootnoteSample {
  bool corrupt_reference_cps = false;
  // Word 6/95 text PLCs may end up to two CPs past the story.
  bool overshoot_text_cp = false;
};

std::vector<uint8_t> Word6FootnoteDocument(uint16_t ident, uint16_t nfib,
                                           const LegacyFootnoteSample& sample) {
  constexpr size_t kFcMin = 768;
  constexpr size_t kRefPlc = 900;
  constexpr size_t kTxtPlc = 920;
  std::string body = "A";
  body.push_back('\x02');
  body.push_back('\r');
  std::string note;
  note.push_back('\x02');
  note += "F\r";
  std::vector<uint8_t> word(2048, 0);
  Put16(word, 0, ident);
  Put16(word, 2, nfib);
  Put16(word, 6, 0x0409);
  Put32(word, 24, kFcMin);
  Put32(word, 52, static_cast<uint32_t>(body.size()));
  Put32(word, 56, static_cast<uint32_t>(note.size()));
  Put32(word, 60, 0);
  Put32(word, 28, static_cast<uint32_t>(kFcMin + body.size() + note.size()));
  std::copy(body.begin(), body.end(), word.begin() + kFcMin);
  std::copy(note.begin(), note.end(), word.begin() + kFcMin + body.size());
  // PlcffndRef: two CPs (reference site 1, guard 3) then one 2-byte FRD.
  Put32(word, 88 + 2 * 8, kRefPlc);
  Put32(word, 92 + 2 * 8, 10);
  Put32(word, kRefPlc, 1);
  Put32(word, kRefPlc + 4, sample.corrupt_reference_cps ? 0 : 3);
  // PlcffndTxt: one note plus a trailing guard.
  Put32(word, 88 + 3 * 8, kTxtPlc);
  Put32(word, 92 + 3 * 8, 8);
  Put32(word, kTxtPlc, 0);
  Put32(word, kTxtPlc + 4,
        static_cast<uint32_t>(note.size()) +
            (sample.overshoot_text_cp ? 2u : 0u));
  return word;
}

void ExpectSingleFootnote(const doccraft::model::Document& document) {
  ASSERT_EQ(document.notes.size(), 1u);
  EXPECT_FALSE(document.notes[0].endnote);
  ASSERT_EQ(document.notes[0].paragraphs.size(), 1u);
  ASSERT_EQ(document.notes[0].paragraphs[0].runs.size(), 1u);
  EXPECT_EQ(document.notes[0].paragraphs[0].runs[0].text, "F");
  bool has_reference = false;
  for (const auto& run : document.paragraphs[0].runs)
    has_reference = has_reference || run.note_ref == 1;
  EXPECT_TRUE(has_reference);
}

TEST(LegacyWord, Word6FootnoteBodyReachesDocx) {
  auto word = Word6FootnoteDocument(0xA5DC, 0x65, {});
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ExpectSingleFootnote(*document);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string footnotes = Part(*zip, "word/footnotes.xml");
  ASSERT_FALSE(footnotes.empty());
  EXPECT_EQ(Count(footnotes, "<w:footnote w:id="), 1u);
  EXPECT_NE(footnotes.find(">F</w:t>"), std::string::npos);
  EXPECT_NE(
      Part(*zip, "word/document.xml").find("<w:footnoteReference w:id=\"1\"/>"),
      std::string::npos);
}

TEST(LegacyWord, Word95FootnoteBodyReachesDocx) {
  auto word = Word6FootnoteDocument(0xA699, 0x68, {});
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ExpectSingleFootnote(*document);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/footnotes.xml").find(">F</w:t>"),
            std::string::npos);
}

TEST(LegacyWord, LegacyFootnoteTextGuardClampsLastNote) {
  // The legacy guard allows the text PLC to end ccpFtn + 2 CPs past the
  // story. The single note must survive by clamping to the story length; the
  // reference in document.xml must keep a matching footnotes.xml entry.
  const struct {
    uint16_t ident;
    uint16_t nfib;
  } cases[] = {{0xA5DC, 0x65}, {0xA699, 0x68}};
  for (const auto& item : cases) {
    LegacyFootnoteSample sample;
    sample.overshoot_text_cp = true;
    auto word = Word6FootnoteDocument(item.ident, item.nfib, sample);
    auto document = ParseWord6(word);
    ASSERT_TRUE(document) << document.error().message;
    ExpectSingleFootnote(*document);
    auto zip = doccraft::docx::Write(*document);
    ASSERT_TRUE(zip) << zip.error().message;
    const std::string footnotes = Part(*zip, "word/footnotes.xml");
    ASSERT_FALSE(footnotes.empty()) << "nfib " << item.nfib;
    EXPECT_EQ(Count(footnotes, "<w:footnote w:id="), 1u);
    EXPECT_NE(footnotes.find(">F</w:t>"), std::string::npos);
    EXPECT_EQ(Count(Part(*zip, "word/document.xml"),
                    "<w:footnoteReference w:id=\"1\"/>"),
              1u);
  }
}

TEST(LegacyWord, LegacyCorruptFootnotePlcWarnsAndKeepsBody) {
  LegacyFootnoteSample sample;
  sample.corrupt_reference_cps = true;
  auto word = Word6FootnoteDocument(0xA5DC, 0x65, sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  EXPECT_TRUE(document->notes.empty());
  ASSERT_FALSE(document->paragraphs.empty());
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "A");
  EXPECT_EQ(CountWarnings(document->warnings, "skipped footnote story PLC"),
            1u);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
}

// Builds a Word 6/95 document with body "A\x02\r" and one endnote body
// "\x02E\r". 0x02 is the in-text mark for both note kinds (0x05 is the comment
// reference). Pairs 46/47 carry PlcfendRef/PlcfendTxt in WordDocument, but
// Word 6/95 skips a 2-byte reserved field after pair 37 and then reads four
// 16-bit BTE indices, so those slots sit ten bytes past the Word 97 numbering
// (LibreOffice ww8scan.cxx Ver67 branch; ww8scan.hxx lists fcPlcfendRef at
// 0x1D2 rather than 0x1C8).
struct LegacyEndnoteSample {
  bool corrupt_reference_cps = false;
  bool end_ref_slot_outside_word_document = false;
};

std::vector<uint8_t> Word6EndnoteDocument(uint16_t ident, uint16_t nfib,
                                          const LegacyEndnoteSample& sample) {
  constexpr size_t kFcMin = 768;
  constexpr size_t kRefPlc = 900;
  constexpr size_t kTxtPlc = 920;
  constexpr size_t kLegacyPairShift = 10;
  constexpr size_t kEndRefSlot = 88 + 46 * 8 + kLegacyPairShift;
  constexpr size_t kEndTxtSlot = 88 + 47 * 8 + kLegacyPairShift;
  std::string body = "A";
  body.push_back('\x02');
  body.push_back('\r');
  std::string note;
  note.push_back('\x02');
  note += "E\r";
  std::vector<uint8_t> word(2048, 0);
  Put16(word, 0, ident);
  Put16(word, 2, nfib);
  Put16(word, 6, 0x0409);
  Put32(word, 24, kFcMin);
  Put32(word, 52, static_cast<uint32_t>(body.size()));
  Put32(word, 56, 0);
  Put32(word, 60, 0);
  Put32(word, 68, 0);
  Put32(word, 72, static_cast<uint32_t>(note.size()));
  Put32(word, 28, static_cast<uint32_t>(kFcMin + body.size() + note.size()));
  std::copy(body.begin(), body.end(), word.begin() + kFcMin);
  std::copy(note.begin(), note.end(), word.begin() + kFcMin + body.size());
  // PlcfendRef: two CPs (reference site 1, guard 3) then one 2-byte FRD.
  Put32(word, kEndRefSlot,
        sample.end_ref_slot_outside_word_document
            ? static_cast<uint32_t>(word.size())
            : static_cast<uint32_t>(kRefPlc));
  Put32(word, kEndRefSlot + 4, 10);
  Put32(word, kRefPlc, 1);
  Put32(word, kRefPlc + 4, sample.corrupt_reference_cps ? 0 : 3);
  // PlcfendTxt: one note plus a trailing guard.
  Put32(word, kEndTxtSlot, kTxtPlc);
  Put32(word, kEndTxtSlot + 4, 8);
  Put32(word, kTxtPlc, 0);
  Put32(word, kTxtPlc + 4, static_cast<uint32_t>(note.size()));
  return word;
}

void ExpectSingleEndnote(const doccraft::model::Document& document) {
  ASSERT_EQ(document.notes.size(), 1u);
  EXPECT_TRUE(document.notes[0].endnote);
  ASSERT_EQ(document.notes[0].paragraphs.size(), 1u);
  ASSERT_EQ(document.notes[0].paragraphs[0].runs.size(), 1u);
  EXPECT_EQ(document.notes[0].paragraphs[0].runs[0].text, "E");
  bool has_reference = false;
  for (const auto& run : document.paragraphs[0].runs)
    has_reference = has_reference || (run.note_ref == 1 && run.note_endnote);
  EXPECT_TRUE(has_reference);
}

TEST(LegacyWord, Word6EndnoteBodyReachesDocx) {
  auto word = Word6EndnoteDocument(0xA5DC, 0x65, {});
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ExpectSingleEndnote(*document);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string endnotes = Part(*zip, "word/endnotes.xml");
  ASSERT_FALSE(endnotes.empty());
  EXPECT_EQ(Count(endnotes, "<w:endnote w:id="), 1u);
  EXPECT_NE(endnotes.find(">E</w:t>"), std::string::npos);
  EXPECT_NE(
      Part(*zip, "word/document.xml").find("<w:endnoteReference w:id=\"1\"/>"),
      std::string::npos);
}

TEST(LegacyWord, Word95EndnoteBodyReachesDocx) {
  auto word = Word6EndnoteDocument(0xA699, 0x68, {});
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ExpectSingleEndnote(*document);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/endnotes.xml").find(">E</w:t>"),
            std::string::npos);
  EXPECT_NE(
      Part(*zip, "word/document.xml").find("<w:endnoteReference w:id=\"1\"/>"),
      std::string::npos);
}

TEST(LegacyWord, LegacyCorruptEndnotePlcWarnsAndKeepsBody) {
  LegacyEndnoteSample sample;
  sample.corrupt_reference_cps = true;
  auto word = Word6EndnoteDocument(0xA5DC, 0x65, sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  EXPECT_TRUE(document->notes.empty());
  ASSERT_FALSE(document->paragraphs.empty());
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "A");
  EXPECT_EQ(CountWarnings(document->warnings, "skipped endnote story PLC"), 1u);
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_TRUE(Part(*zip, "word/endnotes.xml").empty());
}

TEST(LegacyWord, Word95EndnoteSlotOutsideWordDocumentFails) {
  const struct {
    uint16_t ident;
    uint16_t nfib;
  } kSignatures[] = {{0xA5DC, 0x65}, {0xA699, 0x68}};
  for (const auto& signature : kSignatures) {
    SCOPED_TRACE(signature.nfib);
    LegacyEndnoteSample sample;
    sample.end_ref_slot_outside_word_document = true;
    auto word = Word6EndnoteDocument(signature.ident, signature.nfib, sample);
    auto document = ParseWord6(word);
    ASSERT_FALSE(document);
    // An fc/lcb slot pointing past WordDocument is FIB structural damage, so
    // the whole document is rejected like the footnote pairs.
    EXPECT_NE(document.error().message.find("FIB range"), std::string::npos);
  }
}

TEST(LegacyWord, MissingLegacySectionTableWarnsAndDefaultsStories) {
  auto sample = HeaderSample({"EH\r", "OH\r", "EF\r", "OF\r"}, {0x12}, {5});
  sample.missing_sed = true;
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "Body");
  EXPECT_EQ(
      CountWarnings(document->warnings, "legacy section table unavailable"),
      1u);
  // The fallback consumes the even/odd header and footer entries; even stories
  // stay unattached without fFacingPages, so only the odd pair is emitted.
  ASSERT_EQ(document->section.headers[1].size(), 1u);
  EXPECT_EQ(document->section.headers[1][0].runs[0].text, "OH");
  ASSERT_EQ(document->section.headers[3].size(), 1u);
  EXPECT_EQ(document->section.headers[3][0].runs[0].text, "OF");
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
}

TEST(LegacyWord, TruncatedLegacyDopWarnsAndKeepsBody) {
  auto sample = HeaderSample({"HDR\r", "FTR\r"}, {0x0A}, {4});
  sample.dop_lcb = 1;
  auto word = Word6HeaderDocument(0xA5DC, 0x65, "Body\r", sample);
  auto document = ParseWord6(word);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->paragraphs.size(), 1u);
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "Body");
  EXPECT_EQ(
      CountWarnings(document->warnings, "legacy document properties truncated"),
      1u);
  ASSERT_EQ(document->section.headers[1].size(), 1u);
  EXPECT_EQ(document->section.headers[1][0].runs[0].text, "HDR");
  ASSERT_EQ(document->section.headers[3].size(), 1u);
  EXPECT_EQ(document->section.headers[3][0].runs[0].text, "FTR");
  auto zip = doccraft::docx::Write(*document);
  ASSERT_TRUE(zip) << zip.error().message;
}
}  // namespace
