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
// HYPERLINK/PAGE mappings - in their own package parts.

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
}  // namespace

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
