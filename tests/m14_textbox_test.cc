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

#include "miniz.h"

#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/internal.h"
#include "doc/format/sprm_codes.h"
#include "docx/writer.h"

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
std::string Xml(const std::vector<uint8_t>& archive) {
  mz_zip_archive zip = {};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
    return {};
  size_t size = 0;
  void* bytes =
      mz_zip_reader_extract_file_to_heap(&zip, "word/document.xml", &size, 0);
  std::string content;
  if (bytes) {
    content.assign(static_cast<const char*>(bytes), size);
    mz_free(bytes);
  }
  mz_zip_reader_end(&zip);
  return content;
}

std::expected<model::Document, doc::Error> Synthetic(uint32_t textbox_id) {
  constexpr size_t kTextFc = 1024, kPage = 512, kPicf = 68;
  constexpr size_t kRecord = 8, kFsp = 8, kClient = 4;
  const std::string text = "A\x01\rBox story\rX";
  std::vector<uint8_t> word(kTextFc + text.size()), table(44);
  std::copy(text.begin(), text.end(), word.begin() + kTextFc);
  table[0] = 2;
  Put32(table, 1, 16);
  Put32(table, 9, text.size());
  Put32(table, 15, 0x40000000u + kTextFc * 2);
  Put32(table, 32, kTextFc);
  Put32(table, 36, word.size());
  Put32(table, 40, 1);
  Put32(word, kPage, kTextFc);
  Put32(word, kPage + 4, word.size());
  word[kPage + 8] = 20;
  word[kPage + 511] = 1;
  word[kPage + 40] = 9;
  Put16(word, kPage + 41, doc::detail::kSprmCPicLocation);
  Put32(word, kPage + 43, 0);
  Put16(word, kPage + 47, doc::detail::kSprmCFData);
  std::vector<uint8_t> data(kPicf + kRecord * 3 + kFsp + kClient);
  Put32(data, 0, data.size());
  Put16(data, 4, kPicf);
  Put16(data, 28, 1440);
  Put16(data, 30, 720);
  Put16(data, 32, 1000);
  Put16(data, 34, 1000);
  Put16(data, kPicf, 0x000f);
  Put16(data, kPicf + 2, doc::detail::kSpContainer);
  Put32(data, kPicf + 4, kRecord * 2 + kFsp + kClient);
  const size_t fsp = kPicf + kRecord;
  Put16(data, fsp, 0x0012);
  Put16(data, fsp + 2, doc::detail::kFsp);
  Put32(data, fsp + 4, kFsp);
  Put32(data, fsp + kRecord, 37);
  const size_t client = fsp + kRecord + kFsp;
  Put16(data, client + 2, doc::detail::kClientTextbox);
  Put32(data, client + 4, kClient);
  Put32(data, client + kRecord, textbox_id);
  doc::Fib fib;
  fib.ccp_text = 3;
  fib.ccp_txbx = 11;
  fib.lcb_clx = 21;
  fib.chpx_bte = {32, 12};
  fib.txbx_txt = {static_cast<uint32_t>(table.size()), 56};
  table.resize(table.size() + 56);
  const size_t txt = fib.txbx_txt.offset;
  Put32(table, txt + 4, 10);
  Put32(table, txt + 8, 11);
  Put32(table, txt + 12 + doc::detail::kFtxbxsLidOffset, 37);
  fib.txbx_bkd = {static_cast<uint32_t>(table.size()), 24};
  table.resize(table.size() + 24);
  const size_t bkd = fib.txbx_bkd.offset;
  Put32(table, bkd + 4, 10);
  Put32(table, bkd + 8, 11);
  return doc::ParseFormatted(word, table, fib, data);
}

TEST(M14InlineTextbox, StoryTextReachesInlineContent) {
  auto parsed = Synthetic(0x00010000);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1u);
  ASSERT_EQ(parsed->textboxes[0].paragraphs.size(), 1u);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text, "Box story");
  EXPECT_FALSE(parsed->textboxes[0].floating);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const auto xml = Xml(*zip);
  EXPECT_NE(xml.find("<w:txbxContent>"), std::string::npos);
  EXPECT_NE(xml.find("Box story</w:t>"), std::string::npos);
  EXPECT_EQ(xml.find("position:absolute"), std::string::npos);
}

TEST(M14InlineTextbox, MissingStoryWarnsAndKeepsEmptyShape) {
  auto parsed = Synthetic(0x00020000);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 2u);
  EXPECT_EQ(parsed->warnings.back(), "inline textbox story range missing");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const auto xml = Xml(*zip);
  EXPECT_NE(xml.find("<w:txbxContent/>"), std::string::npos);
}
}  // namespace
