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

// Note/comment text PLC tail margin: Word 97+ producers (LibreOffice, Aspose,
// POI, wvWare) end PlcffndTxt/PlcfendTxt/PlcfandTxt up to two CPs past the
// story. The reader accepts that margin and clamps the affected range to the
// story length; larger overshoots, CP disorder and count mismatches still
// reject the whole story.

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

// Appends a PlcffndRef/PlcfendRef with one reference per element of `refs`
// plus the guard CP and one empty FRD per reference.
doc::Fib::Range PutNoteRefPlc(Synthetic& s, const std::vector<uint32_t>& refs) {
  const size_t at = s.table.size();
  s.table.resize(at + 4 * (refs.size() + 1) + 2 * refs.size(), 0);
  for (size_t i = 0; i < refs.size(); ++i)
    Put32(s.table, at + 4 * i, refs[i]);
  Put32(s.table, at + 4 * refs.size(), refs.back() + 1);
  return {static_cast<uint32_t>(at),
          static_cast<uint32_t>(4 * (refs.size() + 1) + 2 * refs.size())};
}

doc::Fib::Range PutNoteRefPlc(Synthetic& s, uint32_t ref_cp) {
  return PutNoteRefPlc(s, std::vector<uint32_t>{ref_cp});
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

// Appends a text PLC (n + 1 CPs) for the given CP sequence.
doc::Fib::Range PutTextCpPlc(Synthetic& s, const std::vector<uint32_t>& cps) {
  const size_t at = s.table.size();
  s.table.resize(at + 4 * cps.size(), 0);
  for (size_t i = 0; i < cps.size(); ++i)
    Put32(s.table, at + 4 * i, cps[i]);
  return {static_cast<uint32_t>(at), static_cast<uint32_t>(4 * cps.size())};
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
}  // namespace

TEST(M15NoteTextGuard, FootnoteTailCpWithinGuardClampsNote) {
  Synthetic s = Build("Body\x02\r", "Foot\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 4);
  s.fib.fnd_txt = PutTextCpPlc(s, {0, s.fib.ccp_ftn + 2});
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  ASSERT_EQ(parsed->notes.size(), 1u);
  ASSERT_FALSE(parsed->notes[0].paragraphs.empty());
  EXPECT_EQ(parsed->notes[0].paragraphs[0].runs[0].text, "Foot");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string footnotes = Part(*zip, "word/footnotes.xml");
  ASSERT_FALSE(footnotes.empty());
  EXPECT_EQ(Count(footnotes, "<w:footnote w:id="), 1u);
  EXPECT_NE(footnotes.find(">Foot</w:t>"), std::string::npos);
  EXPECT_EQ(Count(Part(*zip, "word/document.xml"),
                  "<w:footnoteReference w:id=\"1\"/>"),
            1u);
}

TEST(M15NoteTextGuard, FootnoteTailCpClampsLastOfTwoNotes) {
  Synthetic s = Build("A\x02\rB\x02\r", "One\rTwo\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, {1, 4});
  s.fib.fnd_txt = PutTextCpPlc(s, {0, 4, s.fib.ccp_ftn + 2});
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  ASSERT_EQ(parsed->notes.size(), 2u);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string footnotes = Part(*zip, "word/footnotes.xml");
  ASSERT_FALSE(footnotes.empty());
  EXPECT_EQ(Count(footnotes, "<w:footnote w:id="), 2u);
  EXPECT_NE(footnotes.find(">One</w:t>"), std::string::npos);
  EXPECT_NE(footnotes.find(">Two</w:t>"), std::string::npos);
}

TEST(M15NoteTextGuard, EndnoteTailCpWithinGuardClampsNote) {
  Synthetic s = Build("Body\x02\r", "", "", "", "End\r\r");
  s.fib.end_ref = PutNoteRefPlc(s, 4);
  s.fib.end_txt = PutTextCpPlc(s, {0, s.fib.ccp_edn + 2});
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  ASSERT_EQ(parsed->notes.size(), 1u);
  EXPECT_TRUE(parsed->notes[0].endnote);
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string endnotes = Part(*zip, "word/endnotes.xml");
  ASSERT_FALSE(endnotes.empty());
  EXPECT_EQ(Count(endnotes, "<w:endnote w:id="), 1u);
  EXPECT_NE(endnotes.find(">End</w:t>"), std::string::npos);
}

TEST(M15NoteTextGuard, CommentTailCpWithinGuardClampsComment) {
  Synthetic s = Build("Body\x05\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 4);
  s.fib.and_txt = PutTextCpPlc(s, {0, s.fib.ccp_atn + 2});
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_TRUE(parsed->warnings.empty());
  ASSERT_EQ(parsed->comments.size(), 1u);
  ASSERT_FALSE(parsed->comments[0].paragraphs.empty());
  EXPECT_EQ(parsed->comments[0].paragraphs[0].runs[0].text, "Comment");
  auto zip = docx::Write(*parsed);
  ASSERT_TRUE(zip);
  const std::string comments = Part(*zip, "word/comments.xml");
  ASSERT_FALSE(comments.empty());
  EXPECT_EQ(Count(comments, "<w:comment w:id="), 1u);
  EXPECT_NE(comments.find(">Comment</w:t>"), std::string::npos);
}

TEST(M15NoteTextGuard, FootnoteTailCpBeyondGuardSkipsStory) {
  Synthetic s = Build("Body\r", "Foot\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, 1);
  s.fib.fnd_txt = PutTextCpPlc(s, {0, s.fib.ccp_ftn + 3});
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

TEST(M15NoteTextGuard, FootnoteMiddleCpBeyondStorySkipsStory) {
  // A middle CP far past the story pushes the tail past the two-CP guard, so
  // the whole PLC is rejected instead of salvaging the earlier note.
  Synthetic s = Build("A\x02\rB\x02\r", "One\rTwo\r\r", "", "", "");
  s.fib.fnd_ref = PutNoteRefPlc(s, {1, 4});
  s.fib.fnd_txt = PutTextCpPlc(s, {0, s.fib.ccp_ftn + 5, s.fib.ccp_ftn + 6});
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped footnote story PLC: invalid note text PLC CP range");
  EXPECT_TRUE(parsed->notes.empty());
}

TEST(M15NoteTextGuard, CommentTailCpBeyondGuardSkipsStory) {
  Synthetic s = Build("Body\x05\r", "", "", "Comment\r\r", "");
  s.fib.and_ref = PutCommentRefPlc(s, 1);
  s.fib.and_txt = PutTextCpPlc(s, {0, s.fib.ccp_atn + 3});
  auto parsed = doc::ParseFormatted(s.word, s.table, s.fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->warnings.size(), 1u);
  EXPECT_EQ(parsed->warnings[0],
            "skipped comment story PLC: invalid comment PLC CP range");
  EXPECT_TRUE(parsed->comments.empty());
}
