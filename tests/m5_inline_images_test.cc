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
#include "doc/format/internal.h"
#include "doc/format/sprm_codes.h"
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
  return doc::ParseFormatted(*word, *table, *fib,
                             data ? *data : std::vector<uint8_t>{});
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

TEST(M5InlineShapes, PicfOfficeArtShapeWritesInlinePlaceholder) {
  constexpr size_t kPicf = 68, kRecord = 8, kFsp = 8;
  std::vector<uint8_t> data(kPicf + kRecord + kRecord + kFsp);
  Put32(data, 0, static_cast<uint32_t>(data.size()));
  Put16(data, 4, kPicf);
  Put16(data, 28, 1440);
  Put16(data, 30, 720);
  Put16(data, 32, 1000);
  Put16(data, 34, 1000);
  Put16(data, kPicf, 0x000F);  // OfficeArtSpContainer.
  Put16(data, kPicf + 2, 0xF004);
  Put32(data, kPicf + 4, kRecord + kFsp);
  Put16(data, kPicf + kRecord, 0x0012);  // Rectangle FSP, version 2.
  Put16(data, kPicf + kRecord + 2, 0xF00A);
  Put32(data, kPicf + kRecord + 4, kFsp);
  Put32(data, kPicf + kRecord + kRecord, 37);
  auto image = doc::ReadInlinePicture(data, 0);
  ASSERT_FALSE(image);
  EXPECT_EQ(image.error().message, "PICF has no supported embedded BLIP");
  auto shape = doc::ReadInlineShape(data, 0);
  ASSERT_TRUE(shape) << shape.error().message;
  EXPECT_EQ(shape->id, 37u);
  EXPECT_EQ(shape->type, model::kShapeRectangle);
  EXPECT_EQ(shape->width, 1440);
  EXPECT_EQ(shape->height, 720);
  EXPECT_TRUE(shape->inline_shape);
  model::Document document;
  document.shapes.push_back(*shape);
  document.paragraphs.push_back({});
  model::Run run;
  run.shape_index = 0;
  document.paragraphs.back().runs.push_back(run);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<v:rect id=\"Shape37\""), std::string::npos);
  EXPECT_NE(xml.find("width:72.000000pt;height:36.000000pt"),
            std::string::npos);
  EXPECT_EQ(xml.find("position:absolute"), std::string::npos);
}

TEST(M5FloatingImages, LibreOfficeGoldenAnchor) {
  auto parsed = ParseFixture("floating-image");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->images.size(), 1u);
  auto converted = docx::Write(*parsed);
  ASSERT_TRUE(converted) << converted.error().message;
  EXPECT_NE(Part(*converted, "word/document.xml").find("<wp:anchor"),
            std::string::npos);
  // LibreOffice's own .docx round-trip of this fixture writes
  // distT/distB/distL/ distR="0" (the .doc FOPT carries no nonzero wrap
  // distances); asserting OOXML's 114300 default would invent properties absent
  // from the source.
  EXPECT_NE(Part(*converted, "word/document.xml").find("distL=\"0\""),
            std::string::npos);
  EXPECT_NE(Part(*converted, "word/document.xml").find("<wp:wrapSquare"),
            std::string::npos);
}

TEST(M5FloatingImages, LibreOfficeWrapVariants) {
  for (const auto& [name, wrap, element] :
       {std::tuple{"floating-top-bottom", model::ImageWrap::kTopAndBottom,
                   "<wp:wrapTopAndBottom"},
        std::tuple{"floating-none", model::ImageWrap::kNone, "<wp:wrapNone"},
        std::tuple{"floating-tight", model::ImageWrap::kTight,
                   "<wp:wrapTight"}}) {
    SCOPED_TRACE(name);
    auto parsed = ParseFixture(name);
    ASSERT_TRUE(parsed) << parsed.error().message;
    ASSERT_EQ(parsed->images.size(), 1u);
    const auto& image = parsed->images.front();
    ASSERT_TRUE(image.anchor);
    EXPECT_EQ(image.anchor->wrap, wrap);
    EXPECT_EQ(image.anchor->x, 567);
    EXPECT_EQ(image.anchor->y, 283);
    EXPECT_EQ(image.width, 1440);
    EXPECT_EQ(image.height, 900);
    auto zip = docx::Write(*parsed);
    ASSERT_TRUE(zip) << zip.error().message;
    EXPECT_NE(Part(*zip, "word/document.xml").find(element), std::string::npos);
    EXPECT_EQ(Part(*zip, "word/media/image1.png"),
              Part(ReadFile(std::string(DOCCRAFT_SOURCE_DIR) +
                            "/tests/fixtures/" + name + ".docx"),
                   "word/media/image1.png"));
  }
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

// One compressed piece and one CHPX covering both text and picture markers.
std::expected<model::Document, doc::Error> ParsePictures(
    const std::string& text, const std::vector<uint8_t>& data,
    bool binary_data = false) {
  constexpr size_t kTextFc = 1024, kPageSize = 512;
  std::vector<uint8_t> word(kTextFc + text.size()), table(44);
  std::copy(text.begin(), text.end(), word.begin() + kTextFc);
  table[0] = 2;
  Put32(table, 1, 16);
  Put32(table, 9, text.size());
  Put32(table, 15, 0x40000000u + kTextFc * 2);
  Put32(table, 32, kTextFc);
  Put32(table, 36, word.size());
  Put32(table, 40, 1);
  Put32(word, kPageSize, kTextFc);
  Put32(word, kPageSize + 4, word.size());
  word[kPageSize + 8] = 20;
  word[kPageSize + 511] = 1;
  word[kPageSize + 40] = 9;
  Put16(word, kPageSize + 41, doc::detail::kSprmCPicLocation);
  Put32(word, kPageSize + 43, 0);
  Put16(word, kPageSize + 47, doc::detail::kSprmCFData);
  word[kPageSize + 49] = binary_data;
  doc::Fib fib;
  fib.ccp_text = text.size();
  fib.lcb_clx = 21;
  fib.chpx_bte = {32, 12};
  return doc::ParseFormatted(word, table, fib, data);
}

TEST(M5InlineImages, AggregateImageLimitAcceptsBoundaryRejectsOverflow) {
  // Each decoded metafile is legal; repeated references still allocate images.
  const auto data =
      MetafilePicf(0xF01B, 0x216, true,
                   std::vector<uint8_t>(doc::detail::kMaxImageSize, 'X'));
  ASSERT_FALSE(data.empty());
  const size_t count =
      doc::detail::kMaxTotalImageSize / doc::detail::kMaxImageSize;
  {
    auto parsed = ParsePictures(std::string(count, '\x01'), data);
    ASSERT_TRUE(parsed) << parsed.error().message;
    ASSERT_EQ(parsed->images.size(), count);
    size_t total = 0;
    for (const auto& image : parsed->images)
      total += image.bytes.size();
    EXPECT_EQ(total, doc::detail::kMaxTotalImageSize);
  }
  auto rejected = ParsePictures(std::string(count + 1, '\x01'), data);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().message, "total image size exceeds limit");
}

TEST(M5InlineImages, SameChpxTextDoesNotAbsorbPicture) {
  const auto data = MetafilePicf(0xF01B, 0x216, false, {'W', 'M', 'F'});
  // Also exercise the same fast path inside a field result.
  for (const std::string& text : {std::string("A\x01"
                                              "B\r"),
                                  std::string("\x13HYPERLINK \"url\"\x14"
                                              "A\x01"
                                              "B\x15\r")}) {
    auto parsed = ParsePictures(text, data);
    ASSERT_TRUE(parsed) << parsed.error().message;
    ASSERT_EQ(parsed->images.size(), 1u);
    const auto& runs = parsed->paragraphs[0].runs;
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].text, "A");
    EXPECT_EQ(runs[1].image_index, 0);
    EXPECT_TRUE(runs[1].text.empty());
    EXPECT_EQ(runs[2].text, "B");
    auto zip = docx::Write(*parsed);
    ASSERT_TRUE(zip) << zip.error().message;
    EXPECT_NE(Part(*zip, "word/document.xml").find("<w:drawing>"),
              std::string::npos);
    EXPECT_EQ(Part(*zip, "word/media/image1.wmf"), "WMF");
  }
}

TEST(M5InlineImages, BinaryDataMarkerDoesNotReadPicture) {
  auto parsed = ParsePictures(
      "A\x01"
      "B\r",
      {}, true);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->images.empty());
  ASSERT_FALSE(parsed->paragraphs[0].runs.empty());
  EXPECT_TRUE(parsed->paragraphs[0].runs[0].binary_data);
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
std::vector<uint8_t> Art(uint16_t type, uint16_t header,
                         const std::vector<uint8_t>& body) {
  std::vector<uint8_t> out(8);
  Put16(out, 0, header);
  Put16(out, 2, type);
  Put32(out, 4, body.size());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}
void Append(std::vector<uint8_t>& out, const std::vector<uint8_t>& bytes) {
  out.insert(out.end(), bytes.begin(), bytes.end());
}
struct FloatingStream {
  std::vector<uint8_t> word, table;
  doc::Fib fib;
};
FloatingStream Floating(uint16_t flags, bool delayed = true, size_t count = 1,
                        uint16_t shape_type = 0x4B2, bool has_pib = true,
                        const std::string& shape_text = "") {
  FloatingStream s;
  std::vector<uint8_t> raster(17);
  Append(raster, {0x89, 'P', 'N', 'G'});
  const auto blip = Art(0xF01E, 0x6E00, raster);
  std::vector<uint8_t> fbse(36);
  Put32(fbse, 28, 0);
  if (delayed)
    s.word = blip;
  else
    Append(fbse, blip);
  auto group = Art(0xF000, 15, Art(0xF001, 31, Art(0xF007, 0x62, fbse)));
  s.table = group;
  s.table.push_back(0);  // OfficeArtWordDrawing main story label.
  std::vector<uint8_t> fsp(8), props(6);
  Put32(fsp, 0, 1026);
  Put16(props, 0, 0x4104);
  Put32(props, 2, 1);
  auto children = Art(0xF00A, (shape_type << 4) | 2, fsp);
  if (has_pib)
    Append(children, Art(0xF00B, 0x13, props));
  if (!shape_text.empty()) {
    std::vector<uint8_t> unicode((shape_text.size() + 1) * 2);
    for (size_t i = 0; i < shape_text.size(); ++i)
      Put16(unicode, 2 * i, static_cast<uint8_t>(shape_text[i]));
    std::vector<uint8_t> property(6);
    Put16(property, 0, 0x80C0);
    Put32(property, 2, unicode.size());
    Append(property, unicode);
    Append(children, Art(0xF00B, 0x13, property));
  }
  Append(s.table, Art(0xF002, 15, Art(0xF004, 15, children)));
  s.fib.drawing_group = {0, static_cast<uint32_t>(s.table.size())};
  std::vector<uint8_t> plc(4 + count * 30);
  for (size_t i = 0; i <= count; ++i)
    Put32(plc, i * 4, i);
  for (size_t i = 0; i < count; ++i) {
    const size_t entry = 4 * (count + 1) + i * 26;
    Put32(plc, entry, 1026);
    Put32(plc, entry + 4, static_cast<uint32_t>(-120));
    Put32(plc, entry + 8, 240);
    Put32(plc, entry + 12, 1320);
    Put32(plc, entry + 16, 960);
    Put16(plc, entry + 20, flags);
  }
  s.fib.spa_main = {static_cast<uint32_t>(s.table.size()),
                    static_cast<uint32_t>(plc.size())};
  s.fib.ccp_text = count + 1;
  Append(s.table, plc);
  return s;
}

TEST(M5FloatingImages, SyntheticWrapOriginsSidesToIrAndXml) {
  const model::ImageWrap wraps[] = {
      model::ImageWrap::kSquare, model::ImageWrap::kTopAndBottom,
      model::ImageWrap::kSquare, model::ImageWrap::kNone,
      model::ImageWrap::kTight,  model::ImageWrap::kThrough};
  const char* names[] = {"wp:wrapSquare", "wp:wrapTopAndBottom",
                         "wp:wrapSquare", "wp:wrapNone",
                         "wp:wrapTight",  "wp:wrapThrough"};
  const char* sides[] = {"bothSides", "left", "right", "largest"};
  const char* horizontal[] = {"margin", "page", "column"};
  const char* vertical[] = {"margin", "page", "paragraph"};
  for (unsigned wrap = 0; wrap < 6; ++wrap) {
    for (unsigned side = 0; side < 4; ++side) {
      for (unsigned origin = 0; origin < 3; ++origin) {
        SCOPED_TRACE(std::to_string(wrap) + "/" + std::to_string(side) + "/" +
                     std::to_string(origin));
        auto s = Floating(
            0xC000 | (wrap << 5) | (side << 9) | (origin << 1) | (origin << 3),
            side % 2);
        auto result = doc::ReadFloatingPictures(s.word, s.table, s.fib);
        ASSERT_TRUE(result) << result.error().message;
        ASSERT_EQ(result->size(), 1u);
        auto& image = result->front().image;
        ASSERT_TRUE(image.anchor);
        EXPECT_EQ(image.width, 1440);
        EXPECT_EQ(image.height, 720);
        EXPECT_EQ(image.anchor->x, -120);
        EXPECT_EQ(image.anchor->y, 240);
        EXPECT_EQ(image.anchor->horizontal, origin);
        EXPECT_EQ(image.anchor->vertical, origin);
        EXPECT_EQ(image.anchor->wrap, wraps[wrap]);
        EXPECT_EQ(image.anchor->wrap_side, wrap == 1 || wrap == 3 ? 0 : side);
        EXPECT_EQ(image.anchor->behind_text, wrap == 3);
        EXPECT_TRUE(image.anchor->locked);
        model::Document document;
        document.images.push_back(std::move(image));
        document.paragraphs.push_back({});
        model::Run run;
        run.image_index = 0;
        document.paragraphs.back().runs.push_back(run);
        auto zip = docx::Write(document);
        ASSERT_TRUE(zip) << zip.error().message;
        const auto xml = Part(*zip, "word/document.xml");
        EXPECT_NE(xml.find(std::string("<") + names[wrap]), std::string::npos);
        EXPECT_NE(xml.find(std::string("<wp:positionH relativeFrom=\"") +
                           horizontal[origin] + "\""),
                  std::string::npos);
        EXPECT_NE(xml.find(std::string("<wp:positionV relativeFrom=\"") +
                           vertical[origin] + "\""),
                  std::string::npos);
        EXPECT_NE(xml.find("<wp:posOffset>-76200</wp:posOffset>"),
                  std::string::npos);
        EXPECT_NE(xml.find("<wp:posOffset>152400</wp:posOffset>"),
                  std::string::npos);
        if (wrap != 1 && wrap != 3) {
          EXPECT_NE(xml.find(std::string("wrapText=\"") + sides[side] + "\""),
                    std::string::npos);
        }
        if (wrap >= 4) {
          EXPECT_NE(xml.find("<wp:wrapPolygon"), std::string::npos);
          EXPECT_NE(xml.find("<wp:lineTo x=\"21600\" y=\"21600\""),
                    std::string::npos);
        }
      }
    }
  }
}

TEST(M5FloatingImages, GeometryShapePlaceholderKeepsAnchor) {
  auto s = Floating(0x54, true, 1, 1, false);
  auto result = doc::ReadFloatingPictures(s.word, s.table, s.fib);
  ASSERT_TRUE(result) << result.error().message;
  ASSERT_EQ(result->size(), 1U);
  ASSERT_TRUE(result->front().shape);
  EXPECT_EQ(result->front().shape->type, 1);
  EXPECT_EQ(result->front().shape->width, 1440);
  EXPECT_EQ(result->front().shape->height, 720);
  model::Document document;
  document.shapes.push_back(*result->front().shape);
  document.paragraphs.push_back({});
  model::Run run;
  run.shape_index = 0;
  document.paragraphs.back().runs.push_back(run);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  const auto xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<v:rect"), std::string::npos);
  EXPECT_NE(xml.find("shape: rectangle"), std::string::npos);
  EXPECT_NE(xml.find("<w10:wrap type=\"square\""), std::string::npos);
  EXPECT_NE(xml.find("width:72.000000pt"), std::string::npos);
}

TEST(M5FloatingImages, LibreOfficeBasicShapesKeepGeometry) {
  auto parsed = ParseFixture("basic-shapes");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->shapes.size(), 2U);
  EXPECT_EQ(parsed->shapes[0].type, 1);
  EXPECT_EQ(parsed->shapes[1].type, 3);
  EXPECT_EQ(parsed->shapes[0].width, 1440);
  EXPECT_EQ(parsed->shapes[0].height, 720);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const auto xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<v:rect"), std::string::npos);
  EXPECT_NE(xml.find("<v:oval"), std::string::npos);
}

TEST(M5FloatingImages, LineAndWordArtTypesKeepIdentity) {
  for (const uint16_t type : {uint16_t{20}, uint16_t{0x88}}) {
    auto s = Floating(0x54, true, 1, type, false,
                      type == 0x88 ? "WordArt words" : "");
    auto result = doc::ReadFloatingPictures(s.word, s.table, s.fib);
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->size(), 1U);
    ASSERT_TRUE(result->front().shape);
    EXPECT_EQ(result->front().shape->type, type);
    if (type == 0x88) {
      EXPECT_EQ(result->front().shape->text, "WordArt words");
    }
    model::Document document;
    document.shapes.push_back(*result->front().shape);
    document.paragraphs.push_back({});
    model::Run run;
    run.shape_index = 0;
    document.paragraphs.back().runs.push_back(run);
    auto zip = docx::Write(document);
    ASSERT_TRUE(zip) << zip.error().message;
    const auto xml = Part(*zip, "word/document.xml");
    EXPECT_NE(xml.find(type == 20 ? "<v:line" : "shape: WordArt"),
              std::string::npos);
    if (type == 0x88) {
      EXPECT_NE(xml.find("WordArt words"), std::string::npos);
    }
    if (type == 0x88) {
      for (size_t i = 0; i + 6 <= s.fib.drawing_group.size; ++i) {
        if (s.table[i] == 0xC0 && s.table[i + 1] == 0x80) {
          Put32(s.table, i + 2, 0xFFFFFFFF);
          break;
        }
      }
      EXPECT_FALSE(doc::ReadFloatingPictures(s.word, s.table, s.fib));
    }
  }
  model::Document document;
  model::Textbox box;
  box.shape_type = 0x88;
  box.width = 1440;
  box.height = 720;
  model::Run text;
  text.text = "Original WordArt words";
  box.paragraphs.push_back({});
  box.paragraphs.back().runs.push_back(text);
  document.textboxes.push_back(box);
  document.paragraphs.push_back({});
  model::Run anchor;
  anchor.textbox_index = 0;
  document.paragraphs.back().runs.push_back(anchor);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  const auto xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("shape: WordArt"), std::string::npos);
  EXPECT_NE(xml.find("Original WordArt words"), std::string::npos);
}

TEST(M5FloatingImages, RejectsMalformedRangesRecordsAndReferences) {
  auto check = [](const FloatingStream& s) {
    EXPECT_FALSE(doc::ReadFloatingPictures(s.word, s.table, s.fib));
  };
  auto s = Floating(0x54);
  s.fib.spa_main.size--;
  check(s);
  s = Floating(0x54);
  s.fib.drawing_group.offset = s.table.size();
  check(s);
  s = Floating(0x54);
  Put32(s.table, 4, 0xFFFFFFFF);
  check(s);
  s = Floating(0x54);
  s.word.resize(7);
  check(s);
  s = Floating(0x54);
  Put32(s.table, s.fib.spa_main.offset, s.fib.ccp_text);
  check(s);
  s = Floating(0x54);
  Put32(s.table, s.fib.spa_main.offset + 8 + 12, 0x80000000);
  check(s);
  check(Floating(6 << 5));
  check(Floating(3 << 1));
  check(Floating(4 << 9));
}

TEST(M5FloatingImages, DelayedMetafileAggregateBudgetBeforeInflate) {
  auto s = Floating(0x54, true, 4);
  auto picf =
      MetafilePicf(0xF01B, 0x216, true,
                   std::vector<uint8_t>(doc::detail::kMaxImageSize, 'X'));
  // Reuse exactly the inline extractor's compressed BLIP bytes.
  s.word.assign(picf.begin() + 68 + 8 + 36, picf.end());
  {
    auto result = doc::ReadFloatingPictures(s.word, s.table, s.fib);
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->size(), 4u);
    EXPECT_EQ(result->back().image.bytes.size(), doc::detail::kMaxImageSize);
  }
  auto overflow = Floating(0x54, true, 5);
  overflow.word = s.word;
  auto rejected =
      doc::ReadFloatingPictures(overflow.word, overflow.table, overflow.fib);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().message, "total image size exceeds limit");
  Put32(s.word, 8 + 16, doc::detail::kMaxImageSize + 1);
  EXPECT_FALSE(doc::ReadFloatingPictures(s.word, s.table, s.fib));
}

}  // namespace
