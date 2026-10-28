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
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "miniz.h"

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/internal.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;

void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; i++)
    bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}

void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
  bytes[at] = static_cast<uint8_t>(value);
  bytes[at + 1] = static_cast<uint8_t>(value >> 8);
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

void AddLinkedAnchor(std::vector<uint8_t>* table, doc::Fib* fib) {
  const auto old = fib->spa_main;
  const uint32_t cp = base::U32(*table, old.offset);
  constexpr size_t kSpa = doc::detail::kSpaSize;
  std::vector<uint8_t> anchors(12 + 2 * kSpa);
  Put32(anchors, 0, cp);
  Put32(anchors, 4, cp + 1);
  Put32(anchors, 8, fib->ccp_text);
  std::copy_n(table->begin() + old.offset + 8, kSpa, anchors.begin() + 12);
  std::copy_n(table->begin() + old.offset + 8, kSpa,
              anchors.begin() + 12 + kSpa);
  Put32(anchors, 12 + kSpa, 500);
  fib->spa_main = {static_cast<uint32_t>(table->size()),
                   static_cast<uint32_t>(anchors.size())};
  table->insert(table->end(), anchors.begin(), anchors.end());
}

TEST(M6TextboxDegradation, InvalidBodyPlcKeepsMainText) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/textbox.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb);
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word);
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib);
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table);
  Put32(*table, fib->txbx_bkd.offset + 4, 0);
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->textboxes.empty());
  ASSERT_FALSE(parsed->warnings.empty());
  EXPECT_TRUE(std::any_of(parsed->warnings.begin(), parsed->warnings.end(),
                          [](const std::string& warning) {
                            return warning ==
                                   "skipped textbox story PLC at FTXBXS 1";
                          }))
      << ::testing::PrintToString(parsed->warnings);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:body>"), std::string::npos);
  EXPECT_NE(xml.find("Before  After textbox."), std::string::npos);
  EXPECT_EQ(xml.find("Box sample line"), std::string::npos);
}

TEST(M6TextboxDegradation, InvalidHeaderPlcKeepsHeaderText) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/header-textbox.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb);
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word);
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib);
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table);
  Put32(*table, fib->hdr_txbx_bkd.offset + 4, 0);
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_FALSE(parsed->warnings.empty());
  EXPECT_TRUE(std::any_of(parsed->warnings.begin(), parsed->warnings.end(),
                          [](const std::string& warning) {
                            return warning.find(
                                       "skipped header textbox story PLC") !=
                                   std::string::npos;
                          }));
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  std::string headers;
  for (int i = 1; i <= 6; ++i)
    headers += Part(*zip, ("word/header" + std::to_string(i) + ".xml").c_str());
  EXPECT_NE(headers.find("<w:hdr"), std::string::npos);
  EXPECT_NE(headers.find(">Header <"), std::string::npos);
  EXPECT_EQ(headers.find("Header box text"), std::string::npos);
}

TEST(M6TextboxDegradation, HeaderAnchorOutsideStoriesKeepsOtherBox) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/header-textbox.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb);
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word);
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib);
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table);

  // Split one textbox story into two linked boxes. The second FSPA anchor
  // points at the last header CP, outside the header PLC's story intervals.
  const uint32_t story_end = base::U32(*table, fib->hdr_txbx_bkd.offset + 4);
  ASSERT_GT(story_end, 4u);
  auto original = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(original) << original.error().message;
  std::vector<uint8_t> breaks(16 + 3 * doc::detail::kTbkdSize);
  Put32(breaks, 0, 0);
  Put32(breaks, 4, story_end / 2);
  Put32(breaks, 8, story_end);
  Put32(breaks, 12, fib->ccp_hdr_txbx);
  fib->hdr_txbx_bkd = {static_cast<uint32_t>(table->size()),
                       static_cast<uint32_t>(breaks.size())};
  table->insert(table->end(), breaks.begin(), breaks.end());

  const auto old = fib->spa_hdr;
  constexpr size_t kSpa = doc::detail::kSpaSize;
  std::vector<uint8_t> anchors(12 + 2 * kSpa);
  Put32(anchors, 0, base::U32(*table, old.offset));
  Put32(anchors, 4, fib->ccp_hdd - 1);
  Put32(anchors, 8, fib->ccp_hdd);
  std::copy_n(table->begin() + old.offset + 8, kSpa, anchors.begin() + 12);
  std::copy_n(table->begin() + old.offset + 8, kSpa,
              anchors.begin() + 12 + kSpa);
  Put32(anchors, 12 + kSpa, 500);
  fib->spa_hdr = {static_cast<uint32_t>(table->size()),
                  static_cast<uint32_t>(anchors.size())};
  table->insert(table->end(), anchors.begin(), anchors.end());

  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1u);
  EXPECT_EQ(parsed->textboxes[0].shape_id, base::U32(*table, old.offset + 8));
  EXPECT_NE(std::find(parsed->warnings.begin(), parsed->warnings.end(),
                      "skipped header textbox: anchor outside header stories"),
            parsed->warnings.end());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:body>"), std::string::npos);
  std::string body_text;
  std::string original_body_text;
  for (const auto& paragraph : parsed->paragraphs)
    for (const auto& run : paragraph.runs)
      body_text += run.text;
  for (const auto& paragraph : original->paragraphs)
    for (const auto& run : paragraph.runs)
      original_body_text += run.text;
  EXPECT_FALSE(original_body_text.empty());
  EXPECT_EQ(body_text, original_body_text);
}

TEST(M6TextboxDegradation, BadSecondStoryKeepsFirstTextbox) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/textbox.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb);
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word);
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib);
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table);
  constexpr size_t kItem = doc::detail::kFtxbxsSize;
  std::vector<uint8_t> stories(16 + 3 * kItem);
  for (size_t i = 0; i < 4; ++i)
    Put32(stories, i * 4,
          static_cast<uint32_t>(i == 0   ? 0
                                : i == 1 ? 8
                                : i == 2 ? 17
                                         : 18));
  const size_t old_items = fib->txbx_txt.offset + 12;
  std::copy_n(table->begin() + old_items, kItem, stories.begin() + 16);
  std::copy_n(table->begin() + old_items, kItem, stories.begin() + 16 + kItem);
  std::copy_n(table->begin() + old_items + kItem, kItem,
              stories.begin() + 16 + 2 * kItem);
  Put32(stories, 16 + kItem + doc::detail::kFtxbxsLidOffset, 500);
  fib->txbx_txt = {static_cast<uint32_t>(table->size()),
                   static_cast<uint32_t>(stories.size())};
  table->insert(table->end(), stories.begin(), stories.end());
  std::vector<uint8_t> breaks(16 + 3 * doc::detail::kTbkdSize);
  Put32(breaks, 0, 0);
  Put32(breaks, 4, 8);
  Put32(breaks, 8, fib->ccp_txbx + 1);
  Put32(breaks, 12, fib->ccp_txbx + 2);
  Put16(breaks, 16 + doc::detail::kTbkdSize, 1);
  fib->txbx_bkd = {static_cast<uint32_t>(table->size()),
                   static_cast<uint32_t>(breaks.size())};
  table->insert(table->end(), breaks.begin(), breaks.end());
  AddLinkedAnchor(&*table, &*fib);
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1u);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text, "Box sam");
  ASSERT_FALSE(parsed->warnings.empty());
  EXPECT_TRUE(std::any_of(parsed->warnings.begin(), parsed->warnings.end(),
                          [](const std::string& warning) {
                            return warning ==
                                   "skipped textbox story PLC at FTXBXS 2";
                          }))
      << ::testing::PrintToString(parsed->warnings);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:txbxContent>"), std::string::npos);
  EXPECT_NE(xml.find("Box sam"), std::string::npos);
  EXPECT_EQ(xml.find("Box sample line"), std::string::npos);
}

}  // namespace
