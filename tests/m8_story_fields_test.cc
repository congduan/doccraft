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

// Non-main story fields: PlcfFldHdr/Ftn/Atn/Edn (FIB pairs 17/18/19/48) give
// header, footnote, endnote and comment stories the same field semantics as
// the main story - fldChar hierarchy, fldLock/dirty flags, and the top-level
// HYPERLINK/PAGE mappings - in their own package parts. Comment range tests
// cover adjacent and nested bookmarks, SttbfAtnBkmk tag matching, and point
// anchor fallback when a tag or pairing table is missing.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "miniz.h"

#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[p + i] = static_cast<uint8_t>(v >> (8 * i));
}

// Word and table streams with the story texts laid out in text-plane order
// (main, footnote, header, annotation, endnote) as one compressed piece; the
// CLX sits at the start of the table stream so tests can append PLCs after
// offset 21.
struct Synthetic {
  doc::Fib fib;
  std::vector<uint8_t> word, table;
  uint32_t ftn_base = 0, hdd_base = 0, atn_base = 0, edn_base = 0;
};

Synthetic Build(const std::string& main, const std::string& ftn,
                const std::string& hdd, const std::string& atn,
                const std::string& edn) {
  Synthetic s;
  s.fib.ccp_text = static_cast<uint32_t>(main.size());
  s.fib.ccp_ftn = static_cast<uint32_t>(ftn.size());
  s.fib.ccp_hdd = static_cast<uint32_t>(hdd.size());
  s.fib.ccp_atn = static_cast<uint32_t>(atn.size());
  s.fib.ccp_edn = static_cast<uint32_t>(edn.size());
  s.ftn_base = s.fib.ccp_text;
  s.hdd_base = s.ftn_base + s.fib.ccp_ftn;
  s.atn_base = s.hdd_base + s.fib.ccp_hdd;
  s.edn_base = s.atn_base + s.fib.ccp_atn;
  const std::string all = main + ftn + hdd + atn + edn;
  const uint32_t fc = 512;
  s.word.resize(4096);
  for (size_t i = 0; i < all.size(); i++)
    s.word[fc + i] = static_cast<uint8_t>(all[i]);
  // CLX = Pcdt: clxt(1) lcb(4) then a one-piece PlcPcd.
  s.fib.fc_clx = 0;
  s.fib.lcb_clx = 21;
  s.table.assign(21, 0);
  s.table[0] = 2;
  Put32(s.table, 1, 16);
  Put32(s.table, 5, 0);
  Put32(s.table, 9, static_cast<uint32_t>(all.size()));
  Put32(s.table, 15, 0x40000000u | (fc * 2));
  return s;
}

// Appends a Plcfld for one begin/separator/end field whose CPs are relative to
// its story, with `grffld` on the end entry, and returns its Fib range.
doc::Fib::Range PutFieldPlc(Synthetic& s, const std::string& story,
                            uint8_t grffld) {
  const size_t at = s.table.size();
  const uint32_t begin = static_cast<uint32_t>(story.find('\x13'));
  const uint32_t sep = static_cast<uint32_t>(story.find('\x14'));
  const uint32_t end = static_cast<uint32_t>(story.find('\x15'));
  s.table.resize(at + 22);
  Put32(s.table, at, begin);
  Put32(s.table, at + 4, sep);
  Put32(s.table, at + 8, end);
  Put32(s.table, at + 12, end + 1);
  s.table[at + 16] = 0x13;
  s.table[at + 18] = 0x14;
  s.table[at + 20] = 0x15;
  s.table[at + 21] = grffld;
  return {static_cast<uint32_t>(at), 22};
}

// Appends a one-section PlcfHdd whose story `story` (0-5) covers [0, len) and
// whose other stories are empty.
doc::Fib::Range PutHeaderPlc(Synthetic& s, uint32_t story, uint32_t len) {
  const size_t at = s.table.size();
  s.table.resize(at + 52, 0);  // 13 CPs: preamble + 6 stories + guard
  Put32(s.table, at + 4 * (6 + story), 0);
  for (size_t i = 7 + story; i < 13; i++)
    Put32(s.table, at + 4 * i, len);
  return {static_cast<uint32_t>(at), 52};
}

// Appends a PlcffndRef/PlcfendRef with one reference at `ref_cp` plus the
// guard CP and an empty FRD.
doc::Fib::Range PutNoteRefPlc(Synthetic& s, uint32_t ref_cp) {
  const size_t at = s.table.size();
  s.table.resize(at + 10);
  Put32(s.table, at, ref_cp);
  Put32(s.table, at + 4, ref_cp + 1);
  return {static_cast<uint32_t>(at), 10};
}

// Appends a PlcffndTxt/PlcfendTxt/PlcfandTxt with one story range [0, len).
doc::Fib::Range PutTextCpPlc(Synthetic& s, uint32_t len) {
  const size_t at = s.table.size();
  s.table.resize(at + 8);
  Put32(s.table, at, 0);
  Put32(s.table, at + 4, len);
  return {static_cast<uint32_t>(at), 8};
}

// Appends a PlcfandRef with one anchor at `ref_cp`: two CPs plus a zeroed
// 30-byte ATRDPre10 (empty initials, author index 0).
doc::Fib::Range PutCommentRefPlc(Synthetic& s, uint32_t ref_cp) {
  const size_t at = s.table.size();
  s.table.resize(at + 38);
  Put32(s.table, at, ref_cp);
  Put32(s.table, at + 4, ref_cp + 1);
  return {static_cast<uint32_t>(at), 38};
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

size_t Count(const std::string& text, const std::string& needle) {
  size_t hits = 0, at = 0;
  while ((at = text.find(needle, at)) != std::string::npos) {
    ++hits;
    at += needle.size();
  }
  return hits;
}

// Standard bookmark PLC CPs are absolute across document parts. A single
// SttbfBkmk entry and matching FBKF/Plcfbkl entry select the header text.
void PutBookmark(Synthetic& s, uint32_t begin, uint32_t end,
                 const std::string& name) {
  size_t at = s.table.size();
  s.fib.sttb_bkmk = {static_cast<uint32_t>(at),
                     static_cast<uint32_t>(8 + name.size() * 2)};
  s.table.resize(at + s.fib.sttb_bkmk.size);
  Put16(s.table, at, 0xffff);
  Put16(s.table, at + 2, 1);
  Put16(s.table, at + 4, 0);
  Put16(s.table, at + 6, static_cast<uint16_t>(name.size()));
  for (size_t i = 0; i < name.size(); ++i)
    Put16(s.table, at + 8 + 2 * i, static_cast<uint8_t>(name[i]));
  at = s.table.size();
  s.fib.bkf = {static_cast<uint32_t>(at), 12};
  s.table.resize(at + 12);
  Put32(s.table, at, begin);
  Put32(s.table, at + 4, end + 1);
  Put16(s.table, at + 8, 0);   // FBKF.ibkl
  Put16(s.table, at + 10, 1);  // FBKF.bkc, not part of ibkl
  at = s.table.size();
  s.fib.bkl = {static_cast<uint32_t>(at), 8};
  s.table.resize(at + 8);
  Put32(s.table, at, end);
  Put32(s.table, at + 4, end + 1);
}
}  // namespace

TEST(M17StoryBookmarks, FootnoteBookmarkReachesXml) {
  Synthetic s = Build("X\x02\r", "AB\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  PutBookmark(s, s.ftn_base, s.ftn_base + 2, "FootMark");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->notes.size(), 1u);
  ASSERT_FALSE(parsed->notes[0].paragraphs.empty());
  const auto& runs = parsed->notes[0].paragraphs[0].runs;
  ASSERT_GE(runs.size(), 3u);
  EXPECT_TRUE(runs[0].bookmark_start);
  EXPECT_EQ(runs[0].bookmark_name, "FootMark");
  EXPECT_EQ(runs[1].text, "AB");
  EXPECT_FALSE(runs[2].bookmark_start);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/footnotes.xml");
  EXPECT_NE(xml.find("<w:bookmarkStart w:id=\"0\" w:name=\"FootMark\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:bookmarkEnd w:id=\"0\"/>"), std::string::npos);
}

TEST(M17StoryBookmarks, EndnoteBookmarkReachesXml) {
  Synthetic s = Build("X\x02\r", "", "", "", "AB\r\r");
  s.fib.end_ref = PutNoteRefPlc(s, 1);
  s.fib.end_txt = PutTextCpPlc(s, s.fib.ccp_edn);
  PutBookmark(s, s.edn_base, s.edn_base + 2, "EndMark");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->notes.size(), 1u);
  EXPECT_TRUE(parsed->notes[0].endnote);
  ASSERT_FALSE(parsed->notes[0].paragraphs.empty());
  EXPECT_TRUE(parsed->notes[0].paragraphs[0].runs[0].bookmark_start);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/endnotes.xml");
  EXPECT_NE(xml.find("<w:bookmarkStart w:id=\"0\" w:name=\"EndMark\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:bookmarkEnd w:id=\"0\"/>"), std::string::npos);
}

TEST(M17StoryBookmarks, CommentBookmarkReachesXml) {
  Synthetic s = Build("X\x05\r", "", "", "AB\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  PutBookmark(s, s.atn_base, s.atn_base + 2, "CommentMark");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  ASSERT_FALSE(parsed->comments[0].paragraphs.empty());
  EXPECT_TRUE(parsed->comments[0].paragraphs[0].runs[0].bookmark_start);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/comments.xml");
  EXPECT_NE(xml.find("<w:bookmarkStart w:id=\"0\" w:name=\"CommentMark\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:bookmarkEnd w:id=\"0\"/>"), std::string::npos);
}

TEST(M17StoryBookmarks, CrossMainAndFootnoteIsSkipped) {
  Synthetic s = Build("X\x02\r", "AB\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  PutBookmark(s, 0, s.ftn_base + 1, "CrossMark");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_FALSE(parsed->warnings.empty());
  EXPECT_NE(parsed->warnings[0].find("bookmark"), std::string::npos);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  for (const char* part : {"word/document.xml", "word/footnotes.xml"}) {
    const std::string xml = Part(*zip, part);
    EXPECT_EQ(xml.find("w:bookmarkStart"), std::string::npos);
    EXPECT_EQ(xml.find("w:bookmarkEnd"), std::string::npos);
  }
}

TEST(M17StoryBookmarks, CommentPairOutsideStoryIsSkipped) {
  Synthetic s = Build("X\x05\r", "", "", "AB\r\r", "CD\r\r");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  PutBookmark(s, s.atn_base, s.edn_base + 1, "CrossComment");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  ASSERT_FALSE(parsed->comments[0].paragraphs.empty());
  EXPECT_EQ(parsed->comments[0].paragraphs[0].runs[0].text, "AB");
  ASSERT_FALSE(parsed->warnings.empty());
  EXPECT_NE(parsed->warnings[0].find("skipped comment bookmark PLC"),
            std::string::npos);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/comments.xml");
  EXPECT_EQ(xml.find("w:bookmarkStart"), std::string::npos);
  EXPECT_EQ(xml.find("w:bookmarkEnd"), std::string::npos);
}

TEST(M17StoryBookmarks, FootnoteGuardClosesBookmark) {
  Synthetic s = Build("X\x02\r", "AB\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  PutBookmark(s, s.ftn_base, s.ftn_base + 3, "GuardMark");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->notes.size(), 1u);
  const auto& runs = parsed->notes[0].paragraphs[0].runs;
  ASSERT_GE(runs.size(), 3u);
  EXPECT_TRUE(runs.front().bookmark_start);
  EXPECT_EQ(runs.back().bookmark_id, 0);
  EXPECT_FALSE(runs.back().bookmark_start);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string xml = Part(*zip, "word/footnotes.xml");
  EXPECT_EQ(Count(xml, "<w:bookmarkStart w:id=\"0\""), 1u);
  EXPECT_EQ(Count(xml, "<w:bookmarkEnd w:id=\"0\"/>"), 1u);
}

TEST(M9HeaderBookmarks, StandardBookmarkAppearsInHeaderPart) {
  Synthetic s = Build("Body\r", "", "ABCD\r\r", "", "");
  s.fib.headers = PutHeaderPlc(s, 1, s.fib.ccp_hdd);
  PutBookmark(s, s.hdd_base + 1, s.hdd_base + 3, "HeaderMark");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->section.headers[1].size(), 1u);
  const auto& runs = parsed->section.headers[1][0].runs;
  ASSERT_EQ(runs.size(), 5u);
  EXPECT_EQ(runs[0].text, "A");
  EXPECT_TRUE(runs[1].bookmark_start);
  EXPECT_EQ(runs[1].bookmark_id, 0);
  EXPECT_EQ(runs[1].bookmark_name, "HeaderMark");
  EXPECT_EQ(runs[2].text, "BC");
  EXPECT_EQ(runs[3].bookmark_id, 0);
  EXPECT_FALSE(runs[3].bookmark_start);
  EXPECT_EQ(runs[4].text, "D");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string header = Part(*zip, "word/header1.xml");
  const size_t start = header.find(
      "<w:bookmarkStart w:id=\"0\" "
      "w:name=\"HeaderMark\"/>");
  const size_t end = header.find("<w:bookmarkEnd w:id=\"0\"/>");
  ASSERT_NE(start, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  EXPECT_LT(header.find(">A</w:t>"), start);
  EXPECT_LT(start, header.find(">BC</w:t>"));
  EXPECT_LT(header.find(">BC</w:t>"), end);
  EXPECT_LT(end, header.find(">D</w:t>"));
}

TEST(M9HeaderBookmarks, InvalidHeaderRangeKeepsStoryWithWarning) {
  Synthetic s = Build("Body\r", "", "ABCD\r\r", "", "");
  s.fib.headers = PutHeaderPlc(s, 1, s.fib.ccp_hdd);
  PutBookmark(s, s.hdd_base + 1, s.hdd_base + s.fib.ccp_hdd + 1,
              "BadHeaderMark");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->section.headers[1].size(), 1u);
  EXPECT_EQ(parsed->section.headers[1][0].runs[0].text, "ABCD");
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_NE(parsed->warnings[0].find("skipped header bookmark PLC:"),
            std::string::npos);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string header = Part(*zip, "word/header1.xml");
  EXPECT_NE(header.find(">ABCD</w:t>"), std::string::npos);
  EXPECT_EQ(header.find("w:bookmarkStart"), std::string::npos);
}

TEST(M10HeaderBookmarks, WholeParagraphEndsAtStoryGuard) {
  Synthetic s = Build("Body\r", "", "AB\r\r", "", "");
  s.fib.headers = PutHeaderPlc(s, 1, s.fib.ccp_hdd);
  PutBookmark(s, s.hdd_base, s.hdd_base + 3, "WholePara");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  ASSERT_EQ(parsed->section.headers[1].size(), 1u);
  const auto& runs = parsed->section.headers[1][0].runs;
  ASSERT_EQ(runs.size(), 3u);
  EXPECT_TRUE(runs[0].bookmark_start);
  EXPECT_EQ(runs[1].text, "AB");
  EXPECT_EQ(runs[2].bookmark_id, 0);
  EXPECT_FALSE(runs[2].bookmark_start);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string header = Part(*zip, "word/header1.xml");
  EXPECT_EQ(Count(header, "<w:bookmarkStart w:id=\"0\""), 1u);
  EXPECT_EQ(Count(header, "<w:bookmarkEnd w:id=\"0\"/>"), 1u);
  EXPECT_LT(header.find("<w:bookmarkStart w:id=\"0\""),
            header.find("<w:bookmarkEnd w:id=\"0\"/>"));
}

TEST(M10HeaderBookmarks, CrossStoryPairIsSkippedWithWarning) {
  Synthetic s = Build("Body\r", "", "AB\rCD\r", "", "");
  const size_t at = s.table.size();
  s.table.resize(at + 52, 0);
  Put32(s.table, at + 4 * 7, 3);  // Story 1 ends after AB\r.
  for (size_t i = 8; i < 13; ++i)
    Put32(s.table, at + 4 * i, s.fib.ccp_hdd);
  s.fib.headers = {static_cast<uint32_t>(at), 52};
  PutBookmark(s, s.hdd_base, s.hdd_base + 4, "Cross");
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_NE(
      parsed->warnings[0].find("skipped header bookmark PLC: invalid range"),
      std::string::npos);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  for (const char* part : {"word/header1.xml", "word/header2.xml"}) {
    const std::string header = Part(*zip, part);
    EXPECT_FALSE(header.empty());
    EXPECT_EQ(header.find("w:bookmarkStart"), std::string::npos);
    EXPECT_EQ(header.find("w:bookmarkEnd"), std::string::npos);
  }
}

TEST(M8StoryFields, FibExposesNonMainFieldPlcs) {
  std::vector<uint8_t> word(4096);
  Put16(word, 0, 0xA5EC);
  Put16(word, 2, 0x00C1);
  Put16(word, 32, 14);   // csw
  Put16(word, 62, 22);   // cslw
  Put16(word, 152, 93);  // cbRgFcLcb
  const size_t pairs[4] = {17, 18, 19, 48};
  for (size_t i = 0; i < 4; i++) {
    Put32(word, 154 + pairs[i] * 8, 100 + static_cast<uint32_t>(i));
    Put32(word, 158 + pairs[i] * 8, 22);
  }
  auto fib = doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->fields_hdr, (doc::Fib::Range{100, 22}));
  EXPECT_EQ(fib->fields_ftn, (doc::Fib::Range{101, 22}));
  EXPECT_EQ(fib->fields_atn, (doc::Fib::Range{102, 22}));
  EXPECT_EQ(fib->fields_edn, (doc::Fib::Range{103, 22}));
}

TEST(M11TextboxFields, FibExposesBodyAndHeaderTextboxFieldPlcs) {
  std::vector<uint8_t> word(4096);
  Put16(word, 0, 0xA5EC);
  Put16(word, 2, 0x00C1);
  Put16(word, 32, 14);
  Put16(word, 62, 22);
  Put16(word, 152, 93);
  // With csw=14/cslw=22, fc/lcb pairs start at byte 154. Pairs 57/59
  // occupy byte 610/614 and 626/630 respectively.
  Put32(word, 610, 100);
  Put32(word, 614, 22);
  Put32(word, 626, 200);
  Put32(word, 630, 22);
  auto fib = doc::ParseFib(word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->fields_txbx, (doc::Fib::Range{100, 22}));
  EXPECT_EQ(fib->fields_hdr_txbx, (doc::Fib::Range{200, 22}));
}

TEST(M11HeaderFields, DoublyCorruptPlcsKeepBothWarnings) {
  Synthetic s = Build("Body\r", "", "ABCD\r\r", "", "");
  const size_t at = s.table.size();
  s.table.resize(at + 32, 0);  // Eight CPs, short of six header stories.
  Put32(s.table, at + 4 * 7, s.fib.ccp_hdd);
  s.fib.headers = {static_cast<uint32_t>(at), 32};
  s.fib.fields_hdr = {0, 3};  // Invalid field PLC size.
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 2u);
  EXPECT_EQ(parsed->warnings[0], "skipped header field PLC: invalid field PLC");
  EXPECT_EQ(parsed->warnings[1],
            "skipped header stories: header story PLC is shorter than the "
            "section count");
}

TEST(M8StoryFields, LockedHeaderFieldKeepsFldCharHierarchy) {
  const std::string header_text =
      "\x13 HYPERLINK \"https://example.com\" \x14site\x15\r\r";
  Synthetic s = Build("Body\r", "", header_text, "", "");
  s.fib.headers = PutHeaderPlc(s, 0, s.fib.ccp_hdd);
  s.fib.fields_hdr = PutFieldPlc(s, header_text, 0x10);  // grffldEnd.fLocked
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->section.headers[0].size(), 1u);
  bool locked_begin = false;
  for (const auto& run : parsed->section.headers[0][0].runs) {
    if (run.field_char == model::kFieldCharBegin && run.field_locked)
      locked_begin = true;
  }
  EXPECT_TRUE(locked_begin);

  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string header = Part(*zip, "word/header1.xml");
  ASSERT_FALSE(header.empty());
  // Every marker carries the lock, exactly like the main story does.
  EXPECT_NE(header.find("<w:fldChar w:fldCharType=\"begin\" w:fldLock=\"1\"/>"),
            std::string::npos);
  EXPECT_NE(header.find("<w:instrText xml:space=\"preserve\"> HYPERLINK "
                        "\"https://example.com\" </w:instrText>"),
            std::string::npos);
  EXPECT_NE(
      header.find("<w:fldChar w:fldCharType=\"separate\" w:fldLock=\"1\"/>"),
      std::string::npos);
  EXPECT_NE(header.find("<w:fldChar w:fldCharType=\"end\" w:fldLock=\"1\"/>"),
            std::string::npos);
  // A locked field must not degrade: the fldChar hierarchy survives in full
  // and no simple-field mapping takes over.
  EXPECT_EQ(Count(header, "<w:fldChar"), 3u);
  EXPECT_EQ(header.find("w:fldSimple"), std::string::npos);
}

TEST(M8StoryFields, UnlockedHeaderHyperlinkMapsToWHyperlink) {
  const std::string header_text =
      "\x13 HYPERLINK \"https://example.com\" \x14site\x15\r\r";
  Synthetic s = Build("Body\r", "", header_text, "", "");
  s.fib.headers = PutHeaderPlc(s, 0, s.fib.ccp_hdd);
  s.fib.fields_hdr = PutFieldPlc(s, header_text, 0);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string header = Part(*zip, "word/header1.xml");
  EXPECT_NE(header.find("<w:hyperlink r:id="), std::string::npos);
  EXPECT_NE(header.find(">site</w:t>"), std::string::npos);
  EXPECT_EQ(header.find("w:fldChar"), std::string::npos);
}

TEST(M8StoryFields, FootnotePageFieldAndEndnoteGenericField) {
  Synthetic s = Build("A\x02\rB\x02\r",  // main
                      "\x13 PAGE \x14"
                      "7\x15\r\r",  // footnote
                      "", "",
                      "\x13 TIME \x14later\x15\r\r");  // endnote
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  s.fib.end_ref = PutNoteRefPlc(s, 5);
  s.fib.end_txt = PutTextCpPlc(s, s.fib.ccp_edn);
  s.fib.fields_ftn = PutFieldPlc(s,
                                 "\x13 PAGE \x14"
                                 "7\x15\r\r",
                                 0);
  s.fib.fields_edn = PutFieldPlc(s, "\x13 TIME \x14later\x15\r\r", 0);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->notes.size(), 2u);

  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string footnotes = Part(*zip, "word/footnotes.xml");
  ASSERT_FALSE(footnotes.empty());
  EXPECT_NE(footnotes.find("<w:fldSimple w:instr=\"PAGE\">"),
            std::string::npos);
  EXPECT_NE(footnotes.find(">7</w:t>"), std::string::npos);
  EXPECT_EQ(footnotes.find("w:fldChar"), std::string::npos);

  const std::string endnotes = Part(*zip, "word/endnotes.xml");
  ASSERT_FALSE(endnotes.empty());
  EXPECT_NE(endnotes.find("<w:fldChar w:fldCharType=\"begin\"/>"),
            std::string::npos);
  EXPECT_NE(endnotes.find("<w:instrText xml:space=\"preserve\"> TIME "
                          "</w:instrText>"),
            std::string::npos);
  EXPECT_NE(endnotes.find("<w:fldChar w:fldCharType=\"separate\"/>"),
            std::string::npos);
  EXPECT_NE(endnotes.find("<w:fldChar w:fldCharType=\"end\"/>"),
            std::string::npos);
  EXPECT_NE(endnotes.find(">later</w:t>"), std::string::npos);
}

TEST(M8StoryFields, CommentGenericFieldEmitsFldChars) {
  Synthetic s = Build("Hey\x05!\r", "", "", "\x13 AUTHOR \x14me\x15\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 3);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  s.fib.fields_atn = PutFieldPlc(s, "\x13 AUTHOR \x14me\x15\r\r", 0);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);

  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string comments = Part(*zip, "word/comments.xml");
  ASSERT_FALSE(comments.empty());
  EXPECT_NE(comments.find("<w:fldChar w:fldCharType=\"begin\"/>"),
            std::string::npos);
  EXPECT_NE(comments.find("<w:instrText xml:space=\"preserve\"> AUTHOR "
                          "</w:instrText>"),
            std::string::npos);
  EXPECT_NE(comments.find("<w:fldChar w:fldCharType=\"separate\"/>"),
            std::string::npos);
  EXPECT_NE(comments.find("<w:fldChar w:fldCharType=\"end\"/>"),
            std::string::npos);
  EXPECT_NE(comments.find(">me</w:t>"), std::string::npos);
}

TEST(M8StoryFields, MalformedStoryFieldPlcWarnsAndSkips) {
  Synthetic s = Build("A\x02\rB\x02\r",
                      "\x13 PAGE \x14"
                      "7\x15\r\r",
                      "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  s.fib.fields_ftn = PutFieldPlc(s,
                                 "\x13 PAGE \x14"
                                 "7\x15\r\r",
                                 0x10);
  // Push the entry CP past the story: the whole PLC is skipped with a
  // warning, and the field keeps its text-driven structure without flags.
  Put32(s.table, s.fib.fields_ftn.offset, s.fib.ccp_ftn + 5);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  bool warned = false;
  for (const auto& warning : parsed->warnings) {
    if (warning.find("footnote field PLC") != std::string::npos)
      warned = true;
  }
  EXPECT_TRUE(warned);

  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string footnotes = Part(*zip, "word/footnotes.xml");
  ASSERT_FALSE(footnotes.empty());
  // Structure survives (fldSimple PAGE) but the locked flag does not.
  EXPECT_NE(footnotes.find("<w:fldSimple w:instr=\"PAGE\">"),
            std::string::npos);
  EXPECT_EQ(footnotes.find("w:fldLock"), std::string::npos);
  // The main story is untouched by the degraded PLC.
  const std::string document = Part(*zip, "word/document.xml");
  EXPECT_NE(document.find(">B</w:t>"), std::string::npos);
}

TEST(M8StoryFields, BadSizedStoryFieldPlcAlsoDegrades) {
  Synthetic s = Build("Hey\x05!\r", "", "", "\x13 AUTHOR \x14me\x15\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 3);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  s.fib.fields_atn = PutFieldPlc(s, "\x13 AUTHOR \x14me\x15\r\r", 0);
  s.fib.fields_atn.size = 23;  // (size - 4) % 6 != 0
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  bool warned = false;
  for (const auto& warning : parsed->warnings) {
    if (warning.find("comment field PLC") != std::string::npos)
      warned = true;
  }
  EXPECT_TRUE(warned);
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_FALSE(parsed->comments[0].paragraphs.empty());
}

TEST(M15StoryPlc, OutOfRangeFootnoteKeepsBodyAndEndnote) {
  Synthetic s = Build("Body\r", "Foot\r\r", "", "", "End\r\r");
  s.fib.fnd_ref = {9999, 10};
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  s.fib.end_ref = PutNoteRefPlc(s, 0);
  s.fib.end_txt = PutTextCpPlc(s, s.fib.ccp_edn);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped footnote story PLC: invalid note reference PLC");
  ASSERT_EQ(parsed->notes.size(), 1u);
  EXPECT_TRUE(parsed->notes[0].endnote);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_NE(Part(*zip, "word/document.xml").find("Body"), std::string::npos);
  EXPECT_TRUE(Part(*zip, "word/footnotes.xml").empty());
  EXPECT_FALSE(Part(*zip, "word/endnotes.xml").empty());
}

TEST(M15StoryPlc, OutOfRangeEndnoteKeepsBody) {
  Synthetic s = Build("Body\r", "", "", "", "End\r\r");
  s.fib.end_ref = PutNoteRefPlc(s, 0);
  s.fib.end_txt = {9999, 8};
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped endnote story PLC: invalid note text PLC");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_NE(Part(*zip, "word/document.xml").find("Body"), std::string::npos);
  EXPECT_TRUE(Part(*zip, "word/endnotes.xml").empty());
}

TEST(M15StoryPlc, OutOfRangeCommentOwnersSkipsWholeStory) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 0);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  s.fib.atn_owners = {9999, 4};
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped comment story PLC: invalid comment owners array");
  EXPECT_TRUE(parsed->comments.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_NE(Part(*zip, "word/document.xml").find("Body"), std::string::npos);
  EXPECT_TRUE(Part(*zip, "word/comments.xml").empty());
}

TEST(M15StoryPlc, NonIncreasingFootnoteCpsSkipStory) {
  Synthetic s = Build("Body\r", "Foot\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  Put32(s.table, s.fib.fnd_ref.offset + 4, 1);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped footnote story PLC: invalid note reference PLC CP order");
  EXPECT_TRUE(parsed->notes.empty());
}

TEST(M15StoryPlc, MismatchedEndnoteCountsSkipStory) {
  Synthetic s = Build("Body\r", "", "", "", "End\r\r");
  s.fib.end_ref = PutNoteRefPlc(s, 1);
  s.fib.end_txt = PutTextCpPlc(s, s.fib.ccp_edn);
  s.fib.end_txt.size = 0;
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped endnote story PLC: note PLC entry count mismatch");
  EXPECT_TRUE(parsed->notes.empty());
}

TEST(M15StoryPlc, OutOfRangeCommentTextSkipsStory) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 0);
  s.fib.and_txt = {9999, 8};
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped comment story PLC: invalid comment text PLC");
  EXPECT_TRUE(parsed->comments.empty());
}

TEST(M15StoryPlc, OutOfRangeCommentAnchorSkipsStory) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = {9999, 38};
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped comment story PLC: invalid comment anchor PLC");
  EXPECT_TRUE(parsed->comments.empty());
}

TEST(M15StoryPlc, MissingFootnoteReferencesWarns) {
  Synthetic s = Build("Body\r", "Foot\r\r", "", "", "");
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped footnote story PLC: note PLC entry count mismatch");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_TRUE(Part(*zip, "word/footnotes.xml").empty());
  EXPECT_NE(Part(*zip, "word/document.xml").find("Body"), std::string::npos);
}

TEST(M15StoryPlc, BadEndnoteTextKeepsFootnote) {
  Synthetic s = Build("Body\r", "Foot\r\r", "", "", "End\r\r");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn);
  s.fib.end_ref = PutNoteRefPlc(s, 2);
  s.fib.end_txt = {9999, 8};
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped endnote story PLC: invalid note text PLC");
  ASSERT_EQ(parsed->notes.size(), 1u);
  EXPECT_FALSE(parsed->notes[0].endnote);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_FALSE(Part(*zip, "word/footnotes.xml").empty());
  EXPECT_TRUE(Part(*zip, "word/endnotes.xml").empty());
}

TEST(M15StoryPlc, OutOfRangeFootnoteTextCpSkipsStoryWithoutClamp) {
  // Word 97+ has no legacy text-PLC guard: a note range past ccpFtn still
  // skips the story PLC with a warning instead of clamping to the story.
  Synthetic s = Build("Body\r", "Foot\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, s.fib.ccp_ftn + 1);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped footnote story PLC: invalid note text PLC CP range");
  EXPECT_TRUE(parsed->notes.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_TRUE(Part(*zip, "word/footnotes.xml").empty());
  EXPECT_NE(Part(*zip, "word/document.xml").find("Body"), std::string::npos);
}

TEST(M15StoryPlc, EqualCommentAnchorCpsKeepBothBodies) {
  Synthetic s = Build("Body\r", "", "", "One\rTwo\r\r", "");
  const size_t at = s.table.size();
  s.table.resize(at + 72);
  Put32(s.table, at, 1);
  Put32(s.table, at + 4, 1);
  Put32(s.table, at + 8, 2);
  s.fib.and_ref = {static_cast<uint32_t>(at), 72};
  const size_t text_at = s.table.size();
  s.table.resize(text_at + 12);
  Put32(s.table, text_at, 0);
  Put32(s.table, text_at + 4, 4);
  Put32(s.table, text_at + 8, s.fib.ccp_atn);
  s.fib.and_txt = {static_cast<uint32_t>(text_at), 12};
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  ASSERT_EQ(parsed->comments.size(), 2u);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_EQ(Count(Part(*zip, "word/comments.xml"), "<w:comment w:id="), 2u);
}

TEST(M15StoryPlc, AdjacentCommentRangesCrossRunsAndControl) {
  Synthetic s = Build(
      "Aab\tcd\x05"
      "ef\x05\r",
      "", "", "One\rTwo\r\r", "");
  size_t at = s.table.size();
  s.table.resize(at + 72);
  Put32(s.table, at, 6);
  Put32(s.table, at + 4, 9);
  Put32(s.table, at + 8, 10);
  s.fib.and_ref = {static_cast<uint32_t>(at), 72};
  at = s.table.size();
  s.table.resize(at + 12);
  Put32(s.table, at, 0);
  Put32(s.table, at + 4, 4);
  Put32(s.table, at + 8, s.fib.ccp_atn);
  s.fib.and_txt = {static_cast<uint32_t>(at), 12};
  at = s.table.size();
  s.table.resize(at + 20);
  Put32(s.table, at, 1);
  Put32(s.table, at + 4, 6);
  Put32(s.table, at + 8, 10);
  Put16(s.table, at + 12, 0);
  Put16(s.table, at + 16, 1);
  s.fib.atn_bkf = {static_cast<uint32_t>(at), 20};
  at = s.table.size();
  s.table.resize(at + 12);
  Put32(s.table, at, 6);
  Put32(s.table, at + 4, 9);
  Put32(s.table, at + 8, 10);
  s.fib.atn_bkl = {static_cast<uint32_t>(at), 12};
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 2u);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t start1 = xml.find("<w:commentRangeStart w:id=\"1\"/>");
  const size_t end1 = xml.find("<w:commentRangeEnd w:id=\"1\"/>");
  const size_t start2 = xml.find("<w:commentRangeStart w:id=\"2\"/>");
  const size_t end2 = xml.find("<w:commentRangeEnd w:id=\"2\"/>");
  ASSERT_NE(start1, std::string::npos);
  ASSERT_NE(end1, std::string::npos);
  ASSERT_NE(start2, std::string::npos);
  ASSERT_NE(end2, std::string::npos);
  EXPECT_LT(start1, xml.find("<w:tab/>", start1));
  EXPECT_LT(xml.find("<w:tab/>", start1), end1);
  EXPECT_LT(end1, start2);
  EXPECT_LT(start2, end2);
  EXPECT_LT(end2, xml.find("<w:commentReference w:id=\"2\"/>"));
}

// The FIB pair table is read from WordDocument so this test covers the
// SttbfAtnBkmk slot as well as the tag-to-bookmark lookup.
Synthetic TaggedComments() {
  Synthetic s = Build("Aabcdef\x05gh\x05\r", "", "", "One\rTwo\r\r", "");
  size_t at = s.table.size();
  s.table.resize(at + 72, 0);
  Put32(s.table, at, 7);
  Put32(s.table, at + 4, 10);
  Put32(s.table, at + 8, 12);
  Put32(s.table, at + 12 + 26, 100);       // A, second bookmark.
  Put32(s.table, at + 12 + 30 + 26, 200);  // B, first bookmark.
  s.fib.and_ref = {static_cast<uint32_t>(at), 72};
  at = s.table.size();
  s.table.resize(at + 12, 0);
  Put32(s.table, at + 4, 4);
  Put32(s.table, at + 8, s.fib.ccp_atn);
  s.fib.and_txt = {static_cast<uint32_t>(at), 12};
  at = s.table.size();
  s.table.resize(at + 20, 0);
  Put32(s.table, at, 1);  // B begins before A.
  Put32(s.table, at + 4, 3);
  Put32(s.table, at + 8, 12);
  Put16(s.table, at + 12, 1);  // B ends at 9.
  s.fib.atn_bkf = {static_cast<uint32_t>(at), 20};
  at = s.table.size();
  s.table.resize(at + 12, 0);
  Put32(s.table, at, 6);  // A ends first.
  Put32(s.table, at + 4, 9);
  Put32(s.table, at + 8, 12);
  s.fib.atn_bkl = {static_cast<uint32_t>(at), 12};
  at = s.table.size();
  s.table.resize(at + 30, 0);
  Put16(s.table, at, 0xffff);
  Put16(s.table, at + 2, 2);
  Put16(s.table, at + 4, 10);
  Put16(s.table, at + 8, 0x0100);
  Put32(s.table, at + 10, 200);  // First bookmark belongs to B.
  Put32(s.table, at + 14, UINT32_MAX);
  Put16(s.table, at + 20, 0x0100);
  Put32(s.table, at + 22, 100);  // Second bookmark belongs to A.
  Put32(s.table, at + 26, UINT32_MAX);
  s.fib.atn_bkmk = {static_cast<uint32_t>(at), 30};

  Put16(s.word, 0, 0xa5ec);
  Put16(s.word, 2, 0x00c1);
  Put16(s.word, 10, 0x0004);  // Complex document, text from CLX.
  Put16(s.word, 32, 0);       // csw
  Put16(s.word, 34, 11);      // cslw
  Put32(s.word, 48, s.fib.ccp_text);
  Put32(s.word, 64, s.fib.ccp_atn);
  Put16(s.word, 80, 44);  // pair count; pair table begins at byte 82.
  const auto pair = [&](size_t index, doc::Fib::Range range) {
    Put32(s.word, 82 + 8 * index, range.offset);
    Put32(s.word, 86 + 8 * index, range.size);
  };
  pair(4, s.fib.and_ref);
  pair(5, s.fib.and_txt);
  pair(33, {s.fib.fc_clx, s.fib.lcb_clx});
  pair(37, s.fib.atn_bkmk);
  pair(42, s.fib.atn_bkf);
  pair(43, s.fib.atn_bkl);
  return s;
}

TEST(M15StoryPlc, CommentTagsPairReorderedBookmarks) {
  Synthetic s = TaggedComments();
  auto fib = doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto parsed = doc::ParseFormatted(s.word, s.table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 2u);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t b_start = xml.find("<w:commentRangeStart w:id=\"2\"/>");
  const size_t a_start = xml.find("<w:commentRangeStart w:id=\"1\"/>");
  const size_t a_end = xml.find("<w:commentRangeEnd w:id=\"1\"/>");
  const size_t a_ref = xml.find("<w:commentReference w:id=\"1\"/>");
  const size_t b_end = xml.find("<w:commentRangeEnd w:id=\"2\"/>");
  const size_t b_ref = xml.find("<w:commentReference w:id=\"2\"/>");
  ASSERT_NE(b_start, std::string::npos);
  ASSERT_NE(a_start, std::string::npos);
  ASSERT_NE(a_end, std::string::npos);
  ASSERT_NE(a_ref, std::string::npos);
  ASSERT_NE(b_end, std::string::npos);
  ASSERT_NE(b_ref, std::string::npos);
  EXPECT_LT(b_start, a_start);
  EXPECT_LT(a_start, a_end);
  EXPECT_LT(a_end, a_ref);
  EXPECT_LT(a_ref, b_end);
  EXPECT_LT(b_end, b_ref);
}

TEST(M15StoryPlc, CommentRangeMayExtendPastReference) {
  Synthetic s = TaggedComments();
  Put32(s.table, s.fib.atn_bkl.offset, 8);  // A: [3, 8], reference at 7.
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t range_end = xml.find("<w:commentRangeEnd w:id=\"1\"/>");
  const size_t reference = xml.find("<w:commentReference w:id=\"1\"/>");
  ASSERT_NE(range_end, std::string::npos);
  ASSERT_NE(reference, std::string::npos);
  EXPECT_LT(reference, range_end);
}

TEST(M15StoryPlc, ZeroLengthCommentRangeKeepsOtherRange) {
  Synthetic s = TaggedComments();
  Put32(s.table, s.fib.atn_bkl.offset, 3);  // A: [3, 3].
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 2u);
  EXPECT_TRUE(parsed->warnings.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t a_start = xml.find("<w:commentRangeStart w:id=\"1\"/>");
  const size_t a_ref = xml.find("<w:commentReference w:id=\"1\"/>");
  const size_t a_end = xml.find("<w:commentRangeEnd w:id=\"1\"/>");
  const size_t b_start = xml.find("<w:commentRangeStart w:id=\"2\"/>");
  const size_t b_end = xml.find("<w:commentRangeEnd w:id=\"2\"/>");
  const size_t b_ref = xml.find("<w:commentReference w:id=\"2\"/>");
  ASSERT_NE(a_start, std::string::npos);
  ASSERT_NE(a_ref, std::string::npos);
  ASSERT_NE(a_end, std::string::npos);
  ASSERT_NE(b_start, std::string::npos);
  ASSERT_NE(b_end, std::string::npos);
  ASSERT_NE(b_ref, std::string::npos);
  EXPECT_LT(a_start, a_ref);
  EXPECT_LT(a_ref, a_end);
  EXPECT_LT(b_start, b_end);
  EXPECT_LT(b_end, b_ref);
}

TEST(M15StoryPlc, InvalidCommentRangeKeepsOtherRange) {
  Synthetic s = TaggedComments();
  Put32(s.table, s.fib.atn_bkf.offset + 4, 8);  // A: [8, 6].
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 2u);
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0], "skipped invalid comment range: id 1");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t b_start = xml.find("<w:commentRangeStart w:id=\"2\"/>");
  const size_t b_end = xml.find("<w:commentRangeEnd w:id=\"2\"/>");
  const size_t b_ref = xml.find("<w:commentReference w:id=\"2\"/>");
  const size_t a_start = xml.find("<w:commentRangeStart w:id=\"1\"/>");
  const size_t a_ref = xml.find("<w:commentReference w:id=\"1\"/>");
  const size_t a_end = xml.find("<w:commentRangeEnd w:id=\"1\"/>");
  ASSERT_NE(b_start, std::string::npos);
  ASSERT_NE(b_end, std::string::npos);
  ASSERT_NE(b_ref, std::string::npos);
  ASSERT_NE(a_start, std::string::npos);
  ASSERT_NE(a_ref, std::string::npos);
  ASSERT_NE(a_end, std::string::npos);
  EXPECT_LT(b_start, b_end);
  EXPECT_LT(b_end, b_ref);
  EXPECT_LT(a_start, a_ref);
  EXPECT_LT(a_ref, a_end);
}

void ExpectInvalidCommentRangeTable(const Synthetic& s) {
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 2u);
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped comment range PLC: invalid annotation bookmark range");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string body = Part(*zip, "word/document.xml");
  const std::string comments = Part(*zip, "word/comments.xml");
  EXPECT_NE(comments.find("One"), std::string::npos);
  EXPECT_NE(comments.find("Two"), std::string::npos);
  for (int id = 1; id <= 2; ++id) {
    const std::string suffix = " w:id=\"" + std::to_string(id) + "\"/>";
    const size_t start = body.find("<w:commentRangeStart" + suffix);
    const size_t ref = body.find("<w:commentReference" + suffix);
    const size_t end = body.find("<w:commentRangeEnd" + suffix);
    ASSERT_NE(start, std::string::npos);
    ASSERT_NE(ref, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    EXPECT_LT(start, ref);
    EXPECT_LT(ref, end);
    EXPECT_EQ(Count(body, "<w:commentRangeStart" + suffix), 1u);
    EXPECT_EQ(Count(body, "<w:commentRangeEnd" + suffix), 1u);
  }
}

TEST(M15StoryPlc, DescendingCommentRangeStartsFallBackToPointAnchors) {
  Synthetic s = TaggedComments();
  Put32(s.table, s.fib.atn_bkf.offset, 4);
  ExpectInvalidCommentRangeTable(s);
}

TEST(M15StoryPlc, DescendingCommentRangeEndsFallBackToPointAnchors) {
  Synthetic s = TaggedComments();
  Put32(s.table, s.fib.atn_bkl.offset, 10);
  ExpectInvalidCommentRangeTable(s);
}

TEST(M15StoryPlc, OutOfBoundsCommentEndIndexFallsBackToPointAnchors) {
  Synthetic s = TaggedComments();
  Put16(s.table, s.fib.atn_bkf.offset + 12, 2);
  ExpectInvalidCommentRangeTable(s);
}

TEST(M15StoryPlc, UnmatchedCommentTagFallsBackToPointAnchor) {
  Synthetic s = TaggedComments();
  Put32(s.table, s.fib.atn_bkmk.offset + 22, 0x12345678);
  auto fib = doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto parsed = doc::ParseFormatted(s.word, s.table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const size_t a_start = xml.find("<w:commentRangeStart w:id=\"1\"/>");
  const size_t a_ref = xml.find("<w:commentReference w:id=\"1\"/>");
  const size_t a_end = xml.find("<w:commentRangeEnd w:id=\"1\"/>");
  const size_t b_start = xml.find("<w:commentRangeStart w:id=\"2\"/>");
  const size_t b_end = xml.find("<w:commentRangeEnd w:id=\"2\"/>");
  ASSERT_NE(a_start, std::string::npos);
  ASSERT_NE(a_ref, std::string::npos);
  ASSERT_NE(a_end, std::string::npos);
  ASSERT_NE(b_start, std::string::npos);
  ASSERT_NE(b_end, std::string::npos);
  EXPECT_LT(b_start, a_start);
  EXPECT_LT(a_start, a_ref);
  EXPECT_LT(a_ref, a_end);
  EXPECT_LT(a_end, b_end);
}

TEST(M15StoryPlc, MissingTagTableWithUnequalCountsWarnsAndUsesPointAnchors) {
  Synthetic s = TaggedComments();
  Put32(s.word, 86 + 8 * 37, 0);  // No SttbfAtnBkmk.
  size_t at = s.table.size();
  s.table.resize(at + 12, 0);
  Put32(s.table, at, 3);
  Put32(s.table, at + 4, 12);
  Put32(s.word, 82 + 8 * 42, static_cast<uint32_t>(at));
  Put32(s.word, 86 + 8 * 42, 12);
  at = s.table.size();
  s.table.resize(at + 8, 0);
  Put32(s.table, at, 6);
  Put32(s.table, at + 4, 12);
  Put32(s.word, 82 + 8 * 43, static_cast<uint32_t>(at));
  Put32(s.word, 86 + 8 * 43, 8);
  auto fib = doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto parsed = doc::ParseFormatted(s.word, s.table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 2u);
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "comment range bookmarks not paired; falling back to point "
            "anchors");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  for (int id = 1; id <= 2; ++id) {
    const std::string suffix = " w:id=\"" + std::to_string(id) + "\"/>";
    const size_t start = xml.find("<w:commentRangeStart" + suffix);
    const size_t ref = xml.find("<w:commentReference" + suffix);
    const size_t end = xml.find("<w:commentRangeEnd" + suffix);
    ASSERT_NE(start, std::string::npos);
    ASSERT_NE(ref, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    EXPECT_LT(start, ref);
    EXPECT_LT(ref, end);
  }
}

TEST(M15StoryPlc, CommentOwnersTrailingByteKeepsAuthor) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  const size_t at = s.table.size();
  s.table.resize(at + 5);
  Put16(s.table, at, 1);
  Put16(s.table, at + 2, 'A');
  s.fib.atn_owners = {static_cast<uint32_t>(at), 5};
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_EQ(parsed->comments[0].author, "A");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  EXPECT_NE(Part(*zip, "word/comments.xml").find("w:author=\"A\""),
            std::string::npos);
}

TEST(M15StoryPlc, CommentInitialsUseTwoByteCountAndUtf16Payload) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  const size_t record = s.fib.and_ref.offset + 8;
  Put16(s.table, record, 2);
  Put16(s.table, record + 2, 'A');
  Put16(s.table, record + 4, 'B');
  Put16(s.table, record + 20, 0xffff);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_EQ(parsed->comments[0].initials, "AB");
  EXPECT_EQ(parsed->comments[0].author, "AB");
  EXPECT_TRUE(parsed->warnings.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/comments.xml").find("w:initials=\"AB\""),
            std::string::npos);
}

TEST(M15StoryPlc, CommentInitialsDropInvalidXmlCharacters) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  const size_t record = s.fib.and_ref.offset + 8;
  Put16(s.table, record, 3);
  Put16(s.table, record + 2, 0);
  Put16(s.table, record + 4, 0x001f);
  Put16(s.table, record + 6, 'A');
  Put16(s.table, record + 20, 0xffff);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_EQ(parsed->comments[0].initials, "A");
  EXPECT_EQ(parsed->comments[0].author, "A");
  EXPECT_TRUE(parsed->warnings.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string comments = Part(*zip, "word/comments.xml");
  EXPECT_NE(comments.find("w:initials=\"A\""), std::string::npos);
  EXPECT_NE(comments.find("w:author=\"A\""), std::string::npos);
  EXPECT_NE(comments.find("Comment"), std::string::npos);
  EXPECT_EQ(comments.find('\0'), std::string::npos);
}

TEST(M15StoryPlc, CommentInitialsKeepSupplementaryCharacter) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  const size_t record = s.fib.and_ref.offset + 8;
  Put16(s.table, record, 2);
  Put16(s.table, record + 2, 0xd83d);
  Put16(s.table, record + 4, 0xde00);
  Put16(s.table, record + 20, 0xffff);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  const std::string emoji = "\xf0\x9f\x98\x80";
  EXPECT_EQ(parsed->comments[0].initials, emoji);
  EXPECT_EQ(parsed->comments[0].author, emoji);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string comments = Part(*zip, "word/comments.xml");
  EXPECT_NE(comments.find("w:initials=\"" + emoji + "\""), std::string::npos);
  EXPECT_NE(comments.find("w:author=\"" + emoji + "\""), std::string::npos);
}

TEST(M15StoryPlc, CommentInitialsDropTrailingHighSurrogate) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  const size_t record = s.fib.and_ref.offset + 8;
  Put16(s.table, record, 1);
  Put16(s.table, record + 2, 0xd83d);
  Put16(s.table, record + 20, 0xffff);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_TRUE(parsed->comments[0].initials.empty());
  EXPECT_TRUE(parsed->comments[0].author.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/comments.xml").find("w:author=\"doccraft\""),
            std::string::npos);
}

TEST(M15StoryPlc, CommentInitialsCheckCodeUnitAfterHighSurrogate) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  const size_t record = s.fib.and_ref.offset + 8;
  Put16(s.table, record, 3);
  Put16(s.table, record + 2, 0xd83d);
  Put16(s.table, record + 4, 0x0020);
  Put16(s.table, record + 6, 'A');
  Put16(s.table, record + 20, 0xffff);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_EQ(parsed->comments[0].initials, " A");
  EXPECT_EQ(parsed->comments[0].author, " A");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/comments.xml").find("w:initials=\" A\""),
            std::string::npos);
}

TEST(M15StoryPlc, CommentInitialsFillNineCharacterBuffer) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  const size_t record = s.fib.and_ref.offset + 8;
  Put16(s.table, record, 9);
  for (size_t i = 0; i < 9; ++i)
    Put16(s.table, record + 2 + 2 * i, 'A' + i);
  Put16(s.table, record + 20, 0xffff);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_EQ(parsed->comments[0].initials, "ABCDEFGHI");
  EXPECT_EQ(parsed->comments[0].author, "ABCDEFGHI");
  EXPECT_TRUE(parsed->warnings.empty());
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/comments.xml").find("w:initials=\"ABCDEFGHI\""),
            std::string::npos);
}

TEST(M15StoryPlc, OversizedCommentInitialsWarnAndKeepComment) {
  Synthetic s = Build("Body\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, s.fib.ccp_atn);
  Put16(s.table, s.fib.and_ref.offset + 8, 10);
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->comments.size(), 1u);
  EXPECT_TRUE(parsed->comments[0].initials.empty());
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0], "skipped invalid comment initials: id 1");
}
