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
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "miniz.h"

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;

void Put16(std::vector<uint8_t>& out, size_t at, uint16_t value) {
  out[at] = static_cast<uint8_t>(value);
  out[at + 1] = static_cast<uint8_t>(value >> 8);
}

void Put32(std::vector<uint8_t>& out, size_t at, uint32_t value) {
  Put16(out, at, static_cast<uint16_t>(value));
  Put16(out, at + 2, static_cast<uint16_t>(value >> 16));
}

std::string Part(const std::vector<uint8_t>& zip_bytes, const char* name) {
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip_bytes.data(), zip_bytes.size(), 0))
    return {};
  size_t size = 0;
  void* p = mz_zip_reader_extract_file_to_heap(&z, name, &size, 0);
  std::string out;
  if (p) {
    out.assign(static_cast<const char*>(p), size);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return out;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

std::expected<model::Document, doc::Error> ParseFixture(
    const std::string& name) {
  auto source = ReadFile(std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/" +
                         name + ".doc");
  auto cfb = cfb::CfbReader::Open(source);
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
  if (!data)
    return std::unexpected(doc::Error{data.error().message});
  return doc::ParseFormatted(*word, *table, *fib, *data);
}

TEST(M5InlineImages, SyntheticPicfBlipToIrAndXml) {
  const std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  constexpr size_t kPicf = 68, kFbse = 36, kBlipHeader = 8;
  constexpr size_t kUid = 16, kTag = 1, kEscherHeader = 8;
  std::vector<uint8_t> data(kPicf + kEscherHeader + kFbse + kBlipHeader + kUid +
                            kTag + png.size());
  Put32(data, 0, static_cast<uint32_t>(data.size()));
  Put16(data, 4, kPicf);
  Put16(data, 28, 1440);
  Put16(data, 30, 720);
  Put16(data, 32, 1000);
  Put16(data, 34, 1000);
  const size_t fbse = kPicf;
  Put16(data, fbse, 0x0062);
  Put16(data, fbse + 2, 0xF007);
  Put32(data, fbse + 4, kFbse + kBlipHeader + kUid + kTag + png.size());
  const size_t blip = fbse + kEscherHeader + kFbse;
  Put16(data, blip, 0x6E00);
  Put16(data, blip + 2, 0xF01E);
  Put32(data, blip + 4, kUid + kTag + png.size());
  std::copy(png.begin(), png.end(),
            data.begin() + blip + kBlipHeader + kUid + kTag);

  auto image = doc::ReadInlinePicture(data, 0);
  ASSERT_TRUE(image) << image.error().message;
  EXPECT_EQ(image->format, model::ImageFormat::kPng);
  EXPECT_EQ(image->width, 1440);
  EXPECT_EQ(image->height, 720);
  EXPECT_EQ(image->bytes, png);

  model::Document document;
  document.images.push_back(*image);
  model::Run run;
  run.image_index = 0;
  document.paragraphs.push_back({});
  document.paragraphs.back().runs.push_back(run);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(
      Part(*zip, "word/document.xml")
          .find("<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"),
      std::string::npos);
  EXPECT_NE(Part(*zip, "word/document.xml").find("r:embed=\"rId3\""),
            std::string::npos);
  EXPECT_EQ(Part(*zip, "word/media/image1.png"),
            std::string(png.begin(), png.end()));
}

TEST(M5InlineImages, LibreOfficeGoldenKeepsMediaBytes) {
  const std::string root =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/inline-image";
  auto parsed = ParseFixture("inline-image");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->images.size(), 1u);
  EXPECT_EQ(parsed->images[0].format, model::ImageFormat::kPng);
  ASSERT_EQ(parsed->paragraphs[0].runs.size(), 3u);
  EXPECT_EQ(parsed->paragraphs[0].runs[1].image_index, 0);
  auto converted = docx::Write(*parsed);
  ASSERT_TRUE(converted) << converted.error().message;
  EXPECT_EQ(Part(*converted, "word/media/image1.png"),
            Part(ReadFile(root + ".docx"), "word/media/image1.png"));
}

TEST(M5InlineImages, LibreOfficeJpegGoldenKeepsMediaBytes) {
  const std::string root =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/inline-jpeg";
  auto parsed = ParseFixture("inline-jpeg");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->images.size(), 1u);
  EXPECT_EQ(parsed->images[0].format, model::ImageFormat::kJpeg);
  auto converted = docx::Write(*parsed);
  ASSERT_TRUE(converted) << converted.error().message;
  EXPECT_EQ(Part(*converted, "word/media/image1.jpg"),
            Part(ReadFile(root + ".docx"), "word/media/image1.jpeg"));
}

std::vector<uint8_t> MetafilePicf(uint16_t blip_type, uint16_t instance,
                                  bool compressed,
                                  const std::vector<uint8_t>& original) {
  std::vector<uint8_t> stored = original;
  if (compressed) {
    stored.resize(mz_compressBound(original.size()));
    mz_ulong size = stored.size();
    if (mz_compress(stored.data(), &size, original.data(), original.size()) !=
        MZ_OK)
      return {};
    stored.resize(size);
  }
  constexpr size_t kPicf = 68, kRecord = 8, kFbse = 36, kUid = 16;
  constexpr size_t kMeta = 34;
  const size_t blip_size = kUid + kMeta + stored.size();
  std::vector<uint8_t> data(kPicf + kRecord + kFbse + kRecord + blip_size);
  Put32(data, 0, static_cast<uint32_t>(data.size()));
  Put16(data, 4, kPicf);
  Put16(data, 28, 1440);
  Put16(data, 30, 720);
  Put16(data, 32, 1000);
  Put16(data, 34, 1000);
  Put16(data, kPicf, 0x0062);
  Put16(data, kPicf + 2, 0xF007);
  Put32(data, kPicf + 4, kFbse + kRecord + blip_size);
  const size_t blip = kPicf + kRecord + kFbse;
  Put16(data, blip, instance << 4);
  Put16(data, blip + 2, blip_type);
  Put32(data, blip + 4, blip_size);
  const size_t meta = blip + kRecord + kUid;
  Put32(data, meta, original.size());
  Put32(data, meta + 28, stored.size());
  data[meta + 32] = compressed ? 0 : 0xFE;
  data[meta + 33] = 0xFE;
  std::copy(stored.begin(), stored.end(), data.begin() + meta + kMeta);
  return data;
}

TEST(M5InlineImages, WmfAndEmfPreserveDecodedBytes) {
  const std::vector<uint8_t> bytes{'M', 'E', 'T', 'A', 'F', 'I', 'L', 'E'};
  for (const auto& [type, instance, format, extension] :
       {std::tuple<uint16_t, uint16_t, model::ImageFormat, const char*>{
            0xF01B, 0x216, model::ImageFormat::kWmf, "wmf"},
        {0xF01A, 0x3D4, model::ImageFormat::kEmf, "emf"}}) {
    for (bool compressed : {false, true}) {
      auto data = MetafilePicf(type, instance, compressed, bytes);
      auto image = doc::ReadInlinePicture(data, 0);
      ASSERT_TRUE(image) << image.error().message;
      EXPECT_EQ(image->format, format);
      EXPECT_EQ(image->bytes, bytes);
      model::Document document;
      document.images.push_back(*image);
      model::Run run;
      run.image_index = 0;
      document.paragraphs.push_back({});
      document.paragraphs.back().runs.push_back(run);
      auto zip = docx::Write(document);
      ASSERT_TRUE(zip) << zip.error().message;
      EXPECT_EQ(
          Part(*zip, (std::string("word/media/image1.") + extension).c_str()),
          std::string(bytes.begin(), bytes.end()));
    }
  }
}

TEST(M5InlineImages, RejectsTruncatedPicfAndEscher) {
  std::vector<uint8_t> data(68);
  Put16(data, 4, 68);
  Put16(data, 28, 1440);
  Put16(data, 30, 720);
  Put16(data, 32, 1000);
  Put16(data, 34, 1000);
  Put32(data, 0, 69);
  EXPECT_FALSE(doc::ReadInlinePicture(data, 0));
  data.resize(76);
  Put32(data, 0, 76);
  Put16(data, 68, 0x000F);
  Put16(data, 70, 0xF004);
  Put32(data, 72, 100);
  EXPECT_FALSE(doc::ReadInlinePicture(data, 0));
}
}  // namespace
