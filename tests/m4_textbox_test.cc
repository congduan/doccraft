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
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1U);
  ASSERT_EQ(parsed->textboxes[0].paragraphs.size(), 2U);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text +
                parsed->textboxes[0].paragraphs[1].runs[0].text,
            "Box sample line");

  // Duplicate CPs are malformed and must never be used as offsets.
  Put32(*table, fib->txbx_bkd.offset + 4, 0);
  auto invalid = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().message, "invalid textbox PLC CP order");
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
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped textbox field PLC: invalid field PLC entry");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/document.xml");
  EXPECT_NE(xml.find("<w:fldChar w:fldCharType=\"begin\"/>"),
            std::string::npos);
  EXPECT_EQ(xml.find("w:fldLock"), std::string::npos);
}
}  // namespace
