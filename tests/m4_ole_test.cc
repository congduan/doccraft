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

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "miniz.h"

#include "cfb/cfb_reader.h"
#include "cfb/cfb_writer.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"
#include "tests/cfb_test_util.h"

namespace {
using namespace doccraft;

void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}

std::string Part(const std::vector<uint8_t>& archive, const char* name) {
  mz_zip_archive zip = {};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
    return {};
  size_t size = 0;
  void* bytes = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
  std::string content;
  if (bytes) {
    content.assign(static_cast<const char*>(bytes), size);
    mz_free(bytes);
  }
  mz_zip_reader_end(&zip);
  return content;
}

TEST(M4Ole, RealObjectPoolSubstorageIsIndependentCfbPart) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/ole-embedded.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb) << cfb.error().message;
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word) << word.error().message;
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table) << table.error().message;
  auto data = cfb->ReadStream("Data");
  ASSERT_TRUE(data) << data.error().message;
  auto parsed = doc::ParseFormatted(*word, *table, *fib, *data);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->ole_objects.size(), 3U);
  EXPECT_EQ(parsed->ole_objects[0].program_id, "Excel.Sheet.8");
  EXPECT_EQ(parsed->ole_objects[1].program_id, "PowerPoint.Show.8");
  for (auto& object : parsed->ole_objects) {
    ASSERT_GT(object.storage_id, 0);
    auto packaged = cfb::RepackageStorage(
        *cfb, "Root Entry/ObjectPool/_" + std::to_string(object.storage_id));
    ASSERT_TRUE(packaged) << packaged.error().message;
    auto nested = cfb::CfbReader::Open(*packaged);
    ASSERT_TRUE(nested) << nested.error().message;
    auto comp = nested->ReadStream(
        "\x01"
        "CompObj");
    ASSERT_TRUE(comp) << comp.error().message;
    object.bytes = std::move(*packaged);
  }
  auto written = docx::Write(*parsed);
  ASSERT_TRUE(written) << written.error().message;
  const auto xml = Part(*written, "word/document.xml");
  EXPECT_NE(xml.find("<w:object"), std::string::npos);
  EXPECT_NE(xml.find("<o:OLEObject"), std::string::npos);
  EXPECT_NE(xml.find("ProgID=\"Excel.Sheet.8\""), std::string::npos);
  EXPECT_NE(Part(*written, "word/_rels/document.xml.rels")
                .find("relationships/oleObject"),
            std::string::npos);
  EXPECT_NE(Part(*written, "[Content_Types].xml").find("oleObject"),
            std::string::npos);
  const auto embedded = Part(*written, "word/embeddings/oleObject1.bin");
  EXPECT_EQ(embedded.size(), parsed->ole_objects[0].bytes.size());
}

TEST(M4Ole, MissingObjectPoolPayloadUsesPlaceholder) {
  model::Document document;
  model::Paragraph paragraph;
  model::Run marker;
  marker.ole_index = 0;
  paragraph.runs.push_back(marker);
  document.paragraphs.push_back(paragraph);
  document.ole_objects.emplace_back();
  auto written = docx::Write(document);
  ASSERT_TRUE(written) << written.error().message;
  EXPECT_NE(Part(*written, "word/document.xml").find("[Embedded object]"),
            std::string::npos);
  EXPECT_EQ(Part(*written, "word/document.xml").find("<o:OLEObject"),
            std::string::npos);
}

TEST(M4Ole, SyntheticObjectPoolStreamFamilyRoundTrips) {
  const std::vector<uint8_t> native{'f', 'i', 'l', 'e'};
  const std::vector<uint8_t> preview{'E', 'M', 'F'};
  const auto source = cfb::test::CfbBuilder()
                          .AddStream(
                              "ObjectPool/_42/\x01"
                              "Ole10Native",
                              native)
                          .AddStream(
                              "ObjectPool/_42/\x01"
                              "CompObj",
                              {1, 2})
                          .AddStream(
                              "ObjectPool/_42/\x01"
                              "OlePres000",
                              preview)
                          .Build();
  auto cfb = cfb::CfbReader::Open(source);
  ASSERT_TRUE(cfb) << cfb.error().message;
  auto packaged = cfb::RepackageStorage(*cfb, "Root Entry/ObjectPool/_42");
  ASSERT_TRUE(packaged) << packaged.error().message;
  auto nested = cfb::CfbReader::Open(*packaged);
  ASSERT_TRUE(nested) << nested.error().message;
  EXPECT_EQ(*nested->ReadStream("\x01"
                                "Ole10Native"),
            native);
  EXPECT_EQ(*nested->ReadStream("\x01"
                                "OlePres000"),
            preview);
  auto missing = cfb::RepackageStorage(*cfb, "Root Entry/ObjectPool/_43");
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, cfb::ErrorCode::kStreamNotFound);
}

TEST(M4Ole, OlePresentationMetafilesBecomePreviewParts) {
  for (const auto format : {3U, 14U}) {
    std::vector<uint8_t> payload(format == 3 ? 18 : 88);
    if (format == 3) {
      payload[0] = 1;
      payload[2] = 9;
      payload[4] = 0x00;
      payload[5] = 0x03;
    } else {
      Put32(payload, 0, 1);
      Put32(payload, 4, 88);
      Put32(payload, 40, 0x464D4520);
    }
    std::vector<uint8_t> pres(40 + payload.size() + (format == 3 ? 18 : 0));
    Put32(pres, 0, 0xFFFFFFFF);
    Put32(pres, 4, format);
    Put32(pres, 8, 4);
    Put32(pres, 28, 2540);
    Put32(pres, 32, 1270);
    Put32(pres, 36, payload.size());
    std::copy(payload.begin(), payload.end(), pres.begin() + 40);
    const auto source = cfb::test::CfbBuilder()
                            .AddStream(
                                "ObjectPool/_42/\x01"
                                "OlePres000",
                                pres)
                            .Build();
    auto container = cfb::CfbReader::Open(source);
    ASSERT_TRUE(container) << container.error().message;
    auto preview = container->ReadStream(
        "Root Entry/ObjectPool/_42/\x01"
        "OlePres000");
    ASSERT_TRUE(preview) << preview.error().message;
    auto packaged =
        cfb::RepackageStorage(*container, "Root Entry/ObjectPool/_42");
    ASSERT_TRUE(packaged) << packaged.error().message;
    auto image = doc::ReadOlePresentation(*preview);
    ASSERT_TRUE(image) << image.error().message;
    ASSERT_TRUE(*image);
    EXPECT_EQ((*image)->width, 1440);
    EXPECT_EQ((*image)->height, 720);
    EXPECT_EQ((*image)->bytes, payload);
    EXPECT_FALSE(doc::ReadOlePresentation(pres, payload.size() - 1));
    model::Document document;
    document.images.push_back(std::move(**image));
    model::OleObject object;
    object.storage_id = 42;
    object.program_id = "Package";
    object.bytes = std::move(*packaged);
    object.preview_image_index = 0;
    document.ole_objects.push_back(std::move(object));
    document.paragraphs.push_back({});
    model::Run run;
    run.ole_index = 0;
    document.paragraphs.back().runs.push_back(run);
    auto zip = docx::Write(document);
    ASSERT_TRUE(zip) << zip.error().message;
    EXPECT_NE(Part(*zip, "word/document.xml").find("<v:imagedata"),
              std::string::npos);
    EXPECT_EQ(Part(*zip, format == 3 ? "word/media/image1.wmf"
                                     : "word/media/image1.emf"),
              std::string(payload.begin(), payload.end()));
    Put32(pres, 36, payload.size() + 1);
    EXPECT_FALSE(doc::ReadOlePresentation(pres));
  }
}

TEST(M4Ole, OlePresentationBitmapBecomesBmpPart) {
  // CF_DIB presentation: BITMAPINFOHEADER plus pixel data, no file header.
  constexpr size_t kHeader = 40;
  constexpr size_t kPixels = 16;  // 2 rows of a 2x2 24-bit bitmap.
  std::vector<uint8_t> dib(kHeader + kPixels, 0);
  Put32(dib, 0, kHeader);  // biSize
  Put32(dib, 4, 2);        // biWidth
  Put32(dib, 8, 2);        // biHeight
  dib[12] = 1;             // biPlanes
  dib[14] = 24;            // biBitCount
  dib[kHeader] = 0x11;     // first pixel byte
  std::vector<uint8_t> pres(40 + dib.size());
  Put32(pres, 0, 0xFFFFFFFF);
  Put32(pres, 4, 8);  // CF_DIB
  Put32(pres, 8, 4);
  Put32(pres, 28, 2540);
  Put32(pres, 32, 1270);
  Put32(pres, 36, dib.size());
  std::copy(dib.begin(), dib.end(), pres.begin() + 40);

  auto image = doc::ReadOlePresentation(pres);
  ASSERT_TRUE(image) << image.error().message;
  ASSERT_TRUE(*image);
  EXPECT_EQ((*image)->format, model::ImageFormat::kBmp);
  EXPECT_EQ((*image)->width, 1440);
  EXPECT_EQ((*image)->height, 720);
  ASSERT_EQ((*image)->bytes.size(), 14 + dib.size());
  const auto& bmp = (*image)->bytes;
  EXPECT_EQ(bmp[0], 'B');
  EXPECT_EQ(bmp[1], 'M');
  // bfSize and bfOffBits: 14-byte file header plus the 40-byte DIB header.
  const auto le32 = [&bmp](size_t at) {
    return static_cast<uint32_t>(bmp[at]) |
           static_cast<uint32_t>(bmp[at + 1]) << 8 |
           static_cast<uint32_t>(bmp[at + 2]) << 16 |
           static_cast<uint32_t>(bmp[at + 3]) << 24;
  };
  EXPECT_EQ(le32(2), static_cast<uint32_t>(14 + dib.size()));
  EXPECT_EQ(le32(10), static_cast<uint32_t>(14 + kHeader));
  EXPECT_EQ(std::string(bmp.begin() + 14, bmp.end()),
            std::string(dib.begin(), dib.end()));

  model::Document document;
  document.images.push_back(**image);
  model::OleObject object;
  object.storage_id = 42;
  object.program_id = "Package";
  object.bytes = {1, 2, 3};
  object.preview_image_index = 0;
  document.ole_objects.push_back(std::move(object));
  document.paragraphs.push_back({});
  model::Run run;
  run.ole_index = 0;
  document.paragraphs.back().runs.push_back(run);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_EQ(Part(*zip, "word/media/image1.bmp"),
            std::string(bmp.begin(), bmp.end()));
  EXPECT_NE(Part(*zip, "[Content_Types].xml")
                .find("<Default Extension=\"bmp\" ContentType=\"image/bmp\"/>"),
            std::string::npos);
  EXPECT_NE(Part(*zip, "word/document.xml").find("<v:imagedata"),
            std::string::npos);

  // Malformed payloads stay errors rather than producing an unreadable part.
  auto bad_bits = pres;
  bad_bits[40 + 14] = 3;  // biBitCount
  EXPECT_FALSE(doc::ReadOlePresentation(bad_bits));
  auto bad_palette = pres;
  bad_palette[40 + 14] = 8;              // 256 palette entries...
  Put32(bad_palette, 40 + 32, 0x10000);  // ...but a far larger biClrUsed.
  EXPECT_FALSE(doc::ReadOlePresentation(bad_palette));
  const std::vector<uint8_t> truncated(pres.begin(), pres.begin() + 40 + 10);
  EXPECT_FALSE(doc::ReadOlePresentation(truncated));
}
}  // namespace
