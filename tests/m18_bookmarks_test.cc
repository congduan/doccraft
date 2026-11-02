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

#include "base/binary.h"
#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"
#include "tests/textbox_test_util.h"

namespace {
using namespace doccraft;
using namespace doccraft::test::textbox;

void PutBookmark(std::vector<uint8_t>* table, doc::Fib* fib, uint32_t begin,
                 uint32_t end, const std::string& name) {
  size_t at = table->size();
  fib->sttb_bkmk = {static_cast<uint32_t>(at),
                    static_cast<uint32_t>(8 + name.size() * 2)};
  table->resize(at + fib->sttb_bkmk.size);
  Put16(*table, at, 0xffff);
  Put16(*table, at + 2, 1);
  Put16(*table, at + 4, 0);
  Put16(*table, at + 6, static_cast<uint16_t>(name.size()));
  for (size_t i = 0; i < name.size(); ++i)
    Put16(*table, at + 8 + 2 * i, static_cast<uint8_t>(name[i]));
  at = table->size();
  fib->bkf = {static_cast<uint32_t>(at), 12};
  table->resize(at + 12);
  Put32(*table, at, begin);
  Put32(*table, at + 4, end + 1);
  Put16(*table, at + 8, 0);
  at = table->size();
  fib->bkl = {static_cast<uint32_t>(at), 8};
  table->resize(at + 8);
  Put32(*table, at, end);
  Put32(*table, at + 4, end + 1);
}

TEST(M18Bookmarks, WholeMainStoryEndsAtCcpText) {
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
  auto baseline = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(baseline) << baseline.error().message;
  PutBookmark(&*table, &*fib, 0, fib->ccp_text, "WholeMain");
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  EXPECT_EQ(parsed->paragraphs.size(), baseline->paragraphs.size());
  ASSERT_FALSE(parsed->paragraphs.empty());
  const auto& last = parsed->paragraphs.back().runs;
  ASSERT_FALSE(last.empty());
  EXPECT_EQ(last.back().bookmark_id, 0);
  EXPECT_FALSE(last.back().bookmark_start);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t start =
      xml.find("<w:bookmarkStart w:id=\"0\" w:name=\"WholeMain\"/>");
  const size_t end = xml.find("<w:bookmarkEnd w:id=\"0\"/>");
  ASSERT_NE(start, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  EXPECT_LT(start, end);
}

TEST(M18Bookmarks, BodyAndHeaderTextboxStoryBookmarks) {
  for (bool header : {false, true}) {
    const std::string filename = header ? "header-textbox.doc" : "textbox.doc";
    const std::string path =
        std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/" + filename;
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
    const uint32_t base = fib->ccp_text + fib->ccp_ftn + fib->ccp_hdd +
                          fib->ccp_atn + fib->ccp_edn +
                          (header ? fib->ccp_txbx : 0);
    const uint32_t length = header ? fib->ccp_hdr_txbx : fib->ccp_txbx;
    const auto range = header ? fib->hdr_txbx_txt : fib->txbx_txt;
    ASSERT_GT(length, 0u);
    const uint32_t box_end = base::U32(*table, range.offset + 4);
    PutBookmark(&*table, &*fib, base, base + box_end - 1, "BoxMark");
    auto parsed = doc::ParseFormatted(*word, *table, *fib);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_TRUE(parsed->warnings.empty());
    ASSERT_EQ(parsed->textboxes.size(), 1u);
    ASSERT_FALSE(parsed->textboxes[0].paragraphs.empty());
    const auto& first = parsed->textboxes[0].paragraphs.front().runs;
    EXPECT_TRUE(first.front().bookmark_start);
    const auto& last = parsed->textboxes[0].paragraphs.back().runs;
    EXPECT_FALSE(last.back().bookmark_start);
    EXPECT_EQ(last.back().bookmark_id, 0);
    auto zip = docx::Write(*parsed);
    ASSERT_TRUE(zip);
    std::string xml = Part(*zip, "word/document.xml");
    if (header) {
      xml.clear();
      for (int i = 1; i <= 6; ++i)
        xml += Part(*zip, ("word/header" + std::to_string(i) + ".xml").c_str());
    }
    EXPECT_NE(xml.find("<w:bookmarkStart w:id=\"0\" w:name=\"BoxMark\"/>"),
              std::string::npos);
    EXPECT_NE(xml.find("<w:bookmarkEnd w:id=\"0\"/>"), std::string::npos);
  }
}

TEST(M18Bookmarks, LinkedTextboxBreakEmitsBookmarkEndOnce) {
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

  // One visible FTXBXS [0, 5), split into linked Tbkd ranges [0, 2)
  // and [2, 5). The following FTXBXS/Tbkd is reusable.
  Put32(*table, fib->txbx_txt.offset + 4, 5);
  std::vector<uint8_t> breaks(34);
  Put32(breaks, 0, 0);
  Put32(breaks, 4, 2);
  Put32(breaks, 8, 5);
  Put32(breaks, 12, fib->ccp_txbx);
  fib->txbx_bkd = {static_cast<uint32_t>(table->size()),
                   static_cast<uint32_t>(breaks.size())};
  table->insert(table->end(), breaks.begin(), breaks.end());
  AddLinkedAnchor(&*table, &*fib);

  const uint32_t base =
      fib->ccp_text + fib->ccp_ftn + fib->ccp_hdd + fib->ccp_atn + fib->ccp_edn;
  PutBookmark(&*table, &*fib, base, base + 2, "BreakEnd");
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 2u);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:txbxContent>", xml.find("<w:txbxContent>") + 1),
            std::string::npos);
  const std::string start = "<w:bookmarkStart w:id=\"0\" w:name=\"BreakEnd\"/>";
  const std::string end = "<w:bookmarkEnd w:id=\"0\"/>";
  EXPECT_EQ(xml.find(start), xml.rfind(start));
  EXPECT_EQ(xml.find(end), xml.rfind(end));
  EXPECT_NE(xml.find(start), std::string::npos);
  EXPECT_NE(xml.find(end), std::string::npos);
  EXPECT_LT(xml.find(start), xml.find(end));

  // A start at the same break belongs before the next segment's first char.
  PutBookmark(&*table, &*fib, base + 2, base + 4, "BreakStart");
  auto starts_at_break = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(starts_at_break) << starts_at_break.error().message;
  ASSERT_EQ(starts_at_break->textboxes.size(), 2u);
  const auto& paragraphs = starts_at_break->textboxes[1].paragraphs;
  ASSERT_EQ(paragraphs.size(), 1u);
  ASSERT_GE(paragraphs[0].runs.size(), 2u);
  EXPECT_TRUE(paragraphs[0].runs[0].bookmark_start);
  EXPECT_EQ(paragraphs[0].runs[0].bookmark_id, 0);
  EXPECT_EQ(paragraphs[0].runs[1].text, "x ");
  auto start_zip = docx::Write(*starts_at_break);
  ASSERT_TRUE(start_zip) << start_zip.error().message;
  const std::string start_xml = Part(*start_zip, "word/document.xml");
  const std::string start_mark =
      "<w:bookmarkStart w:id=\"0\" w:name=\"BreakStart\"/>";
  EXPECT_EQ(start_xml.find(start_mark), start_xml.rfind(start_mark));
  EXPECT_NE(start_xml.find(start_mark), std::string::npos);
}

}  // namespace
