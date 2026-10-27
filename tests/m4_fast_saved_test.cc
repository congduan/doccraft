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
#include "third_party/pugixml/pugixml.hpp"

namespace {
using namespace doccraft;

void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
  bytes[at] = static_cast<uint8_t>(value);
  bytes[at + 1] = static_cast<uint8_t>(value >> 8);
}

void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  Put16(bytes, at, static_cast<uint16_t>(value));
  Put16(bytes, at + 2, static_cast<uint16_t>(value >> 16));
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

TEST(M4FastSaved, HeaderStoryAppliesPrcRunFormatting) {
  doc::Fib fib;
  fib.ccp_text = 5;
  fib.ccp_hdd = 6;
  fib.fc_clx = 40;
  fib.lcb_clx = 39;
  fib.headers = {100, 52};
  fib.complex = true;
  std::vector<uint8_t> word(1024), table(200);
  // Prc: sprmCFBold=true. Main piece is plain; header piece uses Prm1.
  table[40] = 1;
  Put16(table, 41, 3);
  table[43] = 0x35;
  table[44] = 0x08;
  table[45] = 1;
  table[46] = 2;
  Put32(table, 47, 28);
  Put32(table, 51, 0);
  Put32(table, 55, 5);
  Put32(table, 59, 11);
  Put32(table, 65, 0x40000000u | (512u * 2));
  Put32(table, 73, 0x40000000u | (520u * 2));
  Put16(table, 77, 1);
  for (size_t i = 0; i <= 6; i++)
    Put32(table, 100 + 4 * i, 0);
  for (size_t i = 7; i < 13; i++)
    Put32(table, 100 + 4 * i, 6);
  for (size_t i = 0; i < 5; i++)
    word[512 + i] = "Body\r"[i];
  for (size_t i = 0; i < 6; i++)
    word[520 + i] = "Head\r\r"[i];
  auto parsed = doc::ParseFormatted(word, table, fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->section.headers[0].size(), 1U);
  ASSERT_EQ(parsed->section.headers[0][0].runs.size(), 1U);
  EXPECT_EQ(parsed->section.headers[0][0].runs[0].text, "Head");
  EXPECT_TRUE(parsed->section.headers[0][0].runs[0].bold);
  auto written = docx::Write(*parsed);
  ASSERT_TRUE(written) << written.error().message;
  EXPECT_NE(Part(*written, "word/header1.xml").find("<w:b"), std::string::npos);
}

TEST(M4FastSaved, PiecePItapPlacesParagraphInsideTable) {
  doc::Fib fib;
  fib.ccp_text = 4;
  fib.fc_clx = 0;
  fib.lcb_clx = 30;
  fib.complex = true;
  std::vector<uint8_t> word(1024), table(40);
  table[0] = 1;
  Put16(table, 1, 6);
  Put16(table, 3, 0x6649);  // sprmPItap
  Put32(table, 5, 1);
  table[9] = 2;
  Put32(table, 10, 16);
  Put32(table, 14, 0);
  Put32(table, 18, 4);
  Put32(table, 24, 0x40000000u | (512u * 2));
  Put16(table, 28, 1);  // Prm1 references sprmPItap.
  word[512] = 'A';
  word[513] = '\r';
  word[514] = 'B';
  word[515] = 0x07;
  auto parsed = doc::ParseFormatted(word, table, fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->blocks.size(), 1U);
  EXPECT_TRUE(parsed->blocks[0].table);
  ASSERT_EQ(parsed->tables.size(), 1U);
  ASSERT_EQ(parsed->tables[0].rows.size(), 1U);
  EXPECT_EQ(parsed->paragraphs[0].runs[0].text, "A");
}

TEST(M4FastSaved, RealReusableTextboxGuardsKeepXmlValid) {
  const std::string path = std::string(DOCCRAFT_SOURCE_DIR) +
                           "/tests/fixtures/fast-saved-o-kurs.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb) << cfb.error().message;
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word) << word.error().message;
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_TRUE(fib->complex);
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table) << table.error().message;
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->textboxes.empty());
  EXPECT_FALSE(parsed->paragraphs.empty());
  auto written = docx::Write(*parsed);
  ASSERT_TRUE(written) << written.error().message;
  auto xml = Part(*written, "word/document.xml");
  pugi::xml_document document;
  EXPECT_TRUE(document.load_buffer(xml.data(), xml.size()))
      << "fast-saved field codes must not produce invalid XML controls";
}
}  // namespace
