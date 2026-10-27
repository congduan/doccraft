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
}  // namespace
