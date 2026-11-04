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

// Comments: PlcfandRef anchors (FIB pair 4), PlcfandTxt (pair 5), and
// GrpXstAtnOwners (pair 36) produce comments.xml and body references.
// Annotation bookmark PLCs (pairs 42/43) provide the range boundaries;
// SttbfAtnBkmk (pair 37) supplies tags for matching bookmarks to anchors.

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

std::expected<model::Document, doc::Error> ParseFixture(const char* name) {
  std::ifstream file(
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/" + name + ".doc",
      std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  if (!cfb) {
    return std::unexpected(doc::Error{"cfb open failed"});
  }
  auto word = cfb->ReadStream("WordDocument");
  if (!word) {
    return std::unexpected(doc::Error{"missing WordDocument"});
  }
  auto fib = doc::ParseFib(*word);
  if (!fib) {
    return std::unexpected(fib.error());
  }
  auto table = cfb->ReadStream(fib->table_stream());
  if (!table) {
    return std::unexpected(doc::Error{"missing table stream"});
  }
  return doc::ParseFormatted(*word, *table, *fib);
}

std::string ParagraphText(const model::Paragraph& paragraph) {
  std::string out;
  for (const auto& run : paragraph.runs) {
    out += run.text;
  }
  return out;
}

std::string Part(const std::vector<uint8_t>& zip_bytes, const char* name) {
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip_bytes.data(), zip_bytes.size(), 0)) {
    return {};
  }
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

size_t Count(const std::string& text, const std::string& needle) {
  size_t hits = 0, at = 0;
  while ((at = text.find(needle, at)) != std::string::npos) {
    ++hits;
    at += needle.size();
  }
  return hits;
}
}  // namespace

TEST(M3Comments, ReadsBodiesAuthorsAndAnchors) {
  auto d = ParseFixture("comments-basic");
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 2u);
  EXPECT_EQ(d->comments[0].author, "Alice");
  ASSERT_EQ(d->comments[0].paragraphs.size(), 2u);
  EXPECT_EQ(ParagraphText(d->comments[0].paragraphs[0]), "First comment body.");
  EXPECT_EQ(ParagraphText(d->comments[0].paragraphs[1]),
            "Second comment paragraph.");
  EXPECT_EQ(d->comments[1].author, "Bob");
  ASSERT_EQ(d->comments[1].paragraphs.size(), 1u);
  EXPECT_EQ(ParagraphText(d->comments[1].paragraphs[0]), "Another comment.");

  // The 0x05 marks are gone from the text and replaced by anchor runs.
  ASSERT_EQ(d->paragraphs.size(), 2u);
  EXPECT_EQ(ParagraphText(d->paragraphs[0]),
            "Main text with an annotated phrase and the rest.");
  const auto anchor_of = [](const model::Paragraph& paragraph) {
    int id = -1;
    for (const auto& run : paragraph.runs) {
      if (run.comment_ref >= 0) {
        id = run.comment_ref;
      }
    }
    return id;
  };
  EXPECT_EQ(anchor_of(d->paragraphs[0]), 1);
  EXPECT_EQ(anchor_of(d->paragraphs[1]), 2);
}

TEST(M3Comments, RangeBookmarkSurroundsAnnotatedText) {
  auto d = ParseFixture("comments-range");
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 1u);
  EXPECT_EQ(ParagraphText(d->paragraphs[0]), "Before annotated phrase after.");
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t start = xml.find("<w:commentRangeStart w:id=\"1\"/>");
  const size_t phrase = xml.find("annotated phrase");
  const size_t end = xml.find("<w:commentRangeEnd w:id=\"1\"/>");
  const size_t ref = xml.find("<w:commentReference w:id=\"1\"/>");
  ASSERT_NE(start, std::string::npos);
  ASSERT_NE(phrase, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  ASSERT_NE(ref, std::string::npos);
  EXPECT_LT(start, phrase);
  EXPECT_LT(phrase, end);
  EXPECT_LT(end, ref);
}

TEST(M3Comments, WritesCommentsPartAndAnchors) {
  auto d = ParseFixture("comments-basic");
  ASSERT_TRUE(d) << d.error().message;
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  const std::string document = Part(*zip, "word/document.xml");
  EXPECT_EQ(Count(document, "<w:commentRangeStart"), 2u);
  EXPECT_EQ(Count(document, "<w:commentRangeEnd"), 2u);
  EXPECT_EQ(Count(document, "<w:commentReference"), 2u);
  EXPECT_NE(document.find("<w:commentReference w:id=\"1\"/>"),
            std::string::npos);

  const std::string comments = Part(*zip, "word/comments.xml");
  ASSERT_FALSE(comments.empty());
  EXPECT_NE(comments.find("<w:comment w:id=\"1\" w:author=\"Alice\" w:date=\""),
            std::string::npos);
  EXPECT_NE(comments.find("<w:comment w:id=\"2\" w:author=\"Bob\">"),
            std::string::npos);
  EXPECT_NE(comments.find(">Second comment paragraph.</w:t>"),
            std::string::npos);

  const std::string types = Part(*zip, "[Content_Types].xml");
  EXPECT_NE(types.find("wordprocessingml.comments+xml"), std::string::npos);
  const std::string rels = Part(*zip, "word/_rels/document.xml.rels");
  EXPECT_NE(rels.find("Target=\"comments.xml\""), std::string::npos);
}

TEST(M3Comments, CommentsRelationshipUsesCommentsType) {
  auto d = ParseFixture("comments-basic");
  ASSERT_TRUE(d) << d.error().message;
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  const std::string rels = Part(*zip, "word/_rels/document.xml.rels");
  // The comments part must be related with Type .../comments: Word refuses to
  // open a package whose main-document rels carry an unexpected Type.
  EXPECT_NE(rels.find("relationships/comments\" Target=\"comments.xml\""),
            std::string::npos);
  EXPECT_EQ(rels.find("relationships/footer\" Target=\"comments.xml\""),
            std::string::npos);
}

TEST(M3Comments, DocumentWithoutCommentsHasNoPart) {
  model::Document d;
  model::Paragraph p;
  model::Run r;
  r.text = "plain";
  p.runs.push_back(r);
  d.paragraphs.push_back(p);
  auto zip = docx::Write(d);
  ASSERT_TRUE(zip);
  EXPECT_TRUE(Part(*zip, "word/comments.xml").empty());
  EXPECT_EQ(Count(Part(*zip, "word/document.xml"), "commentReference"), 0u);
}

namespace comment_date {

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    b[p + i] = static_cast<uint8_t>(v >> (8 * i));
  }
}

// FibRgFcLcb97 row 113, read here as pair(112) (0-based pairs anchored by
// fcClx = pair(33) at byte 0x01A2): fcAtrdExtra/lcbAtrdExtra point at the
// ATRDPOST10 date array in the table stream ([MS-DOC] §2.16.6; LibreOffice
// ww8scan.cxx reads the same pair at FIB offset 0x41A).
TEST(M3Comments, FibReadsAtrdExtraFromPair112) {
  std::vector<uint8_t> word(4096);
  Put16(word, 0, 0xA5EC);
  Put16(word, 2, 0x00C1);
  Put16(word, 32, 14);                // csw
  Put16(word, 62, 22);                // cslw
  Put16(word, 152, 115);              // cbRgFcLcb > 113
  Put32(word, 154 + 112 * 8, 0x3C2);  // fcAtrdExtra
  Put32(word, 158 + 112 * 8, 36);     // lcbAtrdExtra: two ATRDPOST10
  auto fib = doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->atrd_extra, (doc::Fib::Range{0x3C2, 36}));
}

// Synthetic comment story in the shape of ParseFixture("comments-basic"),
// plus an optional raw ATRDPOST10 array appended to the table stream.
struct SyntheticComments {
  doc::Fib fib;
  std::vector<uint8_t> word, table;
};

SyntheticComments BuildComments(std::vector<uint8_t> atrd_post10) {
  SyntheticComments s;
  // Main story: one anchored comment mark; annotation story: "AB\r\r".
  const std::string main = "X\x05\r";
  const std::string atn = "AB\r\r";
  s.fib.ccp_text = static_cast<uint32_t>(main.size());
  s.fib.ccp_atn = static_cast<uint32_t>(atn.size());
  const std::string all = main + atn;
  constexpr uint32_t kFc = 512;
  s.word.assign(4096, 0);
  for (size_t i = 0; i < all.size(); i++) {
    s.word[kFc + i] = static_cast<uint8_t>(all[i]);
  }
  // CLX = Pcdt: clxt(1) lcb(4) then a one-piece PlcPcd.
  s.fib.fc_clx = 0;
  s.fib.lcb_clx = 21;
  s.table.assign(21, 0);
  s.table[0] = 2;
  Put32(s.table, 1, 16);
  Put32(s.table, 5, 0);
  Put32(s.table, 9, static_cast<uint32_t>(all.size()));
  Put32(s.table, 15, 0x40000000u | (kFc * 2));
  // PlcfandRef: one anchor at CP 1 plus the guard CP, 30-byte ATRDPre10.
  s.fib.and_ref = {21, 38};
  s.table.resize(21 + 38);
  Put32(s.table, 21, 1);
  Put32(s.table, 25, 2);
  // PlcfandTxt: one story range [0, ccp_atn) plus the guard CP.
  s.fib.and_txt = {59, 8};
  s.table.resize(59 + 8);
  Put32(s.table, 59, 0);
  Put32(s.table, 63, s.fib.ccp_atn);
  // GrpXstAtnOwners: one Xst "Alice" (2-byte cch + 5 UTF-16 chars); the
  // array ends with the buffer, so it must hold exactly the one Xst — a
  // trailing zero cch would be rejected as damaged.
  const size_t owners = s.table.size();
  s.table.resize(owners + 12);
  Put16(s.table, owners, 5);
  for (size_t i = 0; i < 5; i++) {
    Put16(s.table, owners + 2 + 2 * i, static_cast<uint16_t>("Alice"[i]));
  }
  s.fib.atn_owners = {static_cast<uint32_t>(owners), 12};
  if (!atrd_post10.empty()) {
    const size_t at = s.table.size();
    s.table.insert(s.table.end(), atrd_post10.begin(), atrd_post10.end());
    s.fib.atrd_extra = {static_cast<uint32_t>(at),
                        static_cast<uint32_t>(atrd_post10.size())};
  }
  return s;
}

// One 18-byte ATRDPOST10 record ([MS-DOC] §2.3.1): the DTTM sits at offset 0;
// LibreOffice ww8par.cxx indexes pExtended[nIndex * 18] and reads the leading
// 4-byte DTTM.
void PutAtrdPost10(std::vector<uint8_t>& b, size_t index, uint32_t dttm) {
  Put32(b, index * 18, dttm);
}

TEST(M3Comments, CommentDateFromAtrdPost10ReachesDocx) {
  std::vector<uint8_t> post10(18, 0);
  // 0x07982284: [MS-DOC] 2.9.60 DTTM for 2021-08-04 10:04 (day 4, month 8,
  // year 121, hour 10, minute 4; the DTTM layout has no second field, so the
  // written attribute always ends in :00).
  PutAtrdPost10(post10, 0, 0x07982284);
  SyntheticComments s = BuildComments(post10);
  auto d = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 1u);
  EXPECT_EQ(d->comments[0].date, 0x07982284u);
  EXPECT_TRUE(d->warnings.empty());
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string comments = Part(*zip, "word/comments.xml");
  EXPECT_NE(comments.find("w:date=\"2021-08-04T10:04:00Z\""),
            std::string::npos);
}

// An all-zero DTTM is invalid per [MS-DOC] 2.9.65 (day and month are 0), so
// no w:date attribute is written and nothing warns.
TEST(M3Comments, ZeroAtrdPost10DttmWritesNoDate) {
  std::vector<uint8_t> post10(18, 0);
  SyntheticComments s = BuildComments(post10);
  auto d = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 1u);
  EXPECT_EQ(d->comments[0].date, 0u);
  EXPECT_TRUE(d->warnings.empty());
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  EXPECT_EQ(Part(*zip, "word/comments.xml").find("w:date="), std::string::npos);
}

// A DTTM with weekday 7 (bits 29-31 all set) is invalid per [MS-DOC] 2.9.65
// (wdy must be 0-6), so no w:date attribute is written and nothing warns.
TEST(M3Comments, WeekdaySevenDttmWritesNoDate) {
  std::vector<uint8_t> post10(18, 0);
  PutAtrdPost10(post10, 0, 0x07982284u | 0xE0000000u);
  SyntheticComments s = BuildComments(post10);
  auto d = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 1u);
  EXPECT_EQ(d->comments[0].date, 0x07982284u | 0xE0000000u);
  EXPECT_TRUE(d->warnings.empty());
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  EXPECT_EQ(Part(*zip, "word/comments.xml").find("w:date="), std::string::npos);
}

// A truncated or count-short ATRDPOST10 array warns once and leaves every
// date unset; the comment bodies, authors and initials stay intact.
TEST(M3Comments, DamagedAtrdPost10WarnsAndKeepsComments) {
  std::vector<uint8_t> post10(9, 0);  // half a record
  SyntheticComments s = BuildComments(post10);
  auto d = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 1u);
  EXPECT_NE(ParagraphText(d->comments[0].paragraphs[0]), "");
  EXPECT_EQ(d->comments[0].author, "Alice");
  ASSERT_EQ(d->warnings.size(), 1u);
  EXPECT_NE(d->warnings[0].find("skipped comment dates"), std::string::npos);
  EXPECT_EQ(d->comments[0].date, 0u);
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  EXPECT_EQ(Part(*zip, "word/comments.xml").find("w:date="), std::string::npos);
}

// Without fcAtrdExtra/lcbAtrdExtra (the pre-Word-2002 shape) no date is
// written and no warning fires — the previous behaviour.
TEST(M3Comments, MissingAtrdPost10StaysSilent) {
  SyntheticComments s = BuildComments({});
  auto d = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 1u);
  EXPECT_EQ(d->comments[0].date, 0u);
  EXPECT_TRUE(d->warnings.empty());
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  EXPECT_EQ(Part(*zip, "word/comments.xml").find("w:date="), std::string::npos);
}

}  // namespace comment_date
