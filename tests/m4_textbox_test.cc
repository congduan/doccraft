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
#include "docx/internal.h"
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

void PutFieldPlc(std::vector<uint8_t>& table, doc::Fib::Range* range,
                 uint8_t flags) {
  const size_t at = table.size();
  range->offset = static_cast<uint32_t>(at);
  range->size = 22;
  table.resize(at + 22);
  Put32(table, at, 0);
  Put32(table, at + 4, 8);
  Put32(table, at + 8, 10);
  Put32(table, at + 12, 11);
  table[at + 16] = 0x13;
  table[at + 18] = 0x14;
  table[at + 20] = 0x15;
  table[at + 21] = flags;
}

void ReplaceTextboxStory(std::vector<uint8_t>& word,
                         const std::vector<uint8_t>& table, const doc::Fib& fib,
                         bool header) {
  const uint32_t base = fib.ccp_text + fib.ccp_ftn + fib.ccp_hdd + fib.ccp_atn +
                        fib.ccp_edn + (header ? fib.ccp_txbx : 0);
  const uint32_t length = header ? fib.ccp_hdr_txbx : fib.ccp_txbx;
  auto chars = doc::detail::ReadChars(word, table, fib, base + length, base);
  ASSERT_TRUE(chars) << chars.error().message;
  const std::string field =
      "\x13 OTHER \x14"
      "X\x15";
  ASSERT_GE(chars->size(), field.size() + 2);
  for (size_t i = 0; i < field.size(); ++i) {
    const uint32_t fc = (*chars)[i].fc;
    word[fc] = static_cast<uint8_t>(field[i]);
    if (i + 1 < chars->size() && (*chars)[i + 1].fc - fc == 2)
      word[fc + 1] = 0;
  }
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

TEST(M4Textbox, LibreOfficeFloatingTextboxKeepsStoryAtAnchor) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/textbox.doc";
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
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1U);
  ASSERT_EQ(parsed->textboxes[0].paragraphs.size(), 1U);
  ASSERT_EQ(parsed->textboxes[0].paragraphs[0].runs.size(), 1U);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text, "Box sample line");
  auto written = docx::Write(*parsed);
  ASSERT_TRUE(written) << written.error().message;
  const std::string xml = Part(*written, "word/document.xml");
  EXPECT_NE(xml.find("<w:txbxContent>"), std::string::npos);
  EXPECT_NE(xml.find("Box sample line"), std::string::npos);
  EXPECT_NE(xml.find("<w:pict>"), std::string::npos);
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

TEST(M4Textbox, SyntheticFlowBreaksStayInStoryOrder) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/textbox.doc";
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
  // Two text ranges inside the same FTXBXS chain, plus its final guard.
  std::vector<uint8_t> breaks(34);
  Put32(breaks, 0, 0);
  Put32(breaks, 4, 8);
  Put32(breaks, 8, 17);
  Put32(breaks, 12, 18);
  fib->txbx_bkd = {static_cast<uint32_t>(table->size()),
                   static_cast<uint32_t>(breaks.size())};
  table->insert(table->end(), breaks.begin(), breaks.end());
  AddLinkedAnchor(&*table, &*fib);
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 2U);
  ASSERT_EQ(parsed->textboxes[0].paragraphs.size(), 1U);
  ASSERT_EQ(parsed->textboxes[1].paragraphs.size(), 1U);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text +
                parsed->textboxes[1].paragraphs[0].runs[0].text,
            "Box sample line");

  // Duplicate CPs are malformed and must never be used as offsets.
  Put32(*table, fib->txbx_bkd.offset + 4, 0);
  auto invalid = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(invalid) << invalid.error().message;
  EXPECT_TRUE(invalid->textboxes.empty());
  ASSERT_FALSE(invalid->warnings.empty());
  EXPECT_NE(invalid->warnings[0].find("skipped textbox story PLC"),
            std::string::npos);
}

TEST(M4Textbox, HeaderTextboxAppearsInHeaderPart) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/header-textbox.doc";
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
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1U);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text, "Header box text");
  auto written = docx::Write(*parsed);
  ASSERT_TRUE(written) << written.error().message;
  bool found = false;
  for (int i = 1; i <= 6; ++i) {
    const auto xml =
        Part(*written, ("word/header" + std::to_string(i) + ".xml").c_str());
    found |= xml.find("Header box text") != std::string::npos &&
             xml.find("<w:txbxContent>") != std::string::npos;
  }
  EXPECT_TRUE(found);
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
  EXPECT_NE(parsed->warnings[0].find("skipped textbox story PLC"),
            std::string::npos);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:body>"), std::string::npos);
  EXPECT_NE(xml.find("Before  After textbox."), std::string::npos);
  EXPECT_EQ(xml.find("Box sample line"), std::string::npos);
}

TEST(M6WriterBuffer, SerializedPartsLimitReturnsError) {
  model::Document document;
  constexpr size_t kTestLimit = 8192;
  auto baseline = docx::detail::WriteWithBufferLimit(document, kTestLimit);
  ASSERT_TRUE(baseline) << baseline.error().message;
  document.paragraphs.push_back({});
  document.paragraphs.back().runs.push_back({});
  document.paragraphs.back().runs.back().text = std::string(16384, 'x');
  auto zip = docx::detail::WriteWithBufferLimit(document, kTestLimit);
  ASSERT_FALSE(zip);
  EXPECT_EQ(zip.error().message, "writer buffer limit exceeded");
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
  EXPECT_NE(parsed->warnings[0].find("skipped header textbox story PLC"),
            std::string::npos);
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
  EXPECT_NE(parsed->warnings[0].find("FTXBXS 2"), std::string::npos);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:txbxContent>"), std::string::npos);
  EXPECT_NE(xml.find("Box sam"), std::string::npos);
  EXPECT_EQ(xml.find("Box sample line"), std::string::npos);
}

TEST(M11TextboxFields, FieldFlagsReachBodyAndHeaderTextboxXml) {
  for (bool header : {false, true}) {
    const std::string filename = header ? "header-textbox.doc" : "textbox.doc";
    const std::string path =
        std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/" + filename;
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
    ReplaceTextboxStory(*word, *table, *fib, header);
    PutFieldPlc(*table, header ? &fib->fields_hdr_txbx : &fib->fields_txbx,
                0x14);
    auto parsed = doc::ParseFormatted(*word, *table, *fib);
    ASSERT_TRUE(parsed) << parsed.error().message;
    ASSERT_EQ(parsed->textboxes.size(), 1u);
    auto zip = docx::Write(*parsed);
    ASSERT_TRUE(zip) << zip.error().message;
    std::string xml = Part(*zip, "word/document.xml");
    if (header) {
      xml.clear();
      for (int i = 1; i <= 6; ++i)
        xml += Part(*zip, ("word/header" + std::to_string(i) + ".xml").c_str());
    }
    for (const char* kind : {"begin", "separate", "end"}) {
      const std::string marker = std::string("<w:fldChar w:fldCharType=\"") +
                                 kind + "\" w:fldLock=\"1\" w:dirty=\"1\"/>";
      EXPECT_NE(xml.find(marker), std::string::npos) << filename << " " << kind;
    }
  }
}

TEST(M11TextboxFields, InvalidPlcWarnsAndKeepsHierarchy) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/textbox.doc";
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
  ReplaceTextboxStory(*word, *table, *fib, false);
  PutFieldPlc(*table, &fib->fields_txbx, 0x14);
  Put32(*table, fib->fields_txbx.offset + 4, fib->ccp_txbx + 1);
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_EQ(std::count(parsed->warnings.begin(), parsed->warnings.end(),
                       "skipped textbox field PLC: invalid field PLC entry"),
            1u);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:fldChar w:fldCharType=\"begin\"/>"),
            std::string::npos);
  EXPECT_EQ(xml.find("w:fldLock"), std::string::npos);
}
}  // namespace
