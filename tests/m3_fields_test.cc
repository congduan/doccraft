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

// Complex field parsing: \x13 instruction \x14 result \x15 -> w:fldChar,
// w:instrText, with HYPERLINK/PAGE keeping their dedicated mappings.

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

// Minimal FIB with the csw/cslw/pairs scaffolding ParseFib requires; the CLX
// lives at offset 0 of the table stream.
std::vector<uint8_t> FibWord(uint32_t text_length) {
  std::vector<uint8_t> word(4096);
  Put16(word, 0, 0xA5EC);
  Put16(word, 2, 0x00C1);
  Put32(word, 24, 512);                  // fcMin
  Put32(word, 28, 520);                  // fcMac
  Put16(word, 32, 14);                   // csw
  Put16(word, 62, 22);                   // cslw
  Put32(word, 64 + 3 * 4, text_length);  // ccpText
  Put16(word, 152, 93);                  // ccpairs
  Put32(word, 154 + 33 * 8, 0);          // fcClx
  Put32(word, 158 + 33 * 8, 21);         // lcbClx
  return word;
}

// One paragraph whose text is `text`, stored as a single compressed piece.
std::expected<model::Document, doc::Error> Synthetic(const std::string& text) {
  // Keep the text clear of the FIB rgFcLcb array (it reaches ~0x380 for 93
  // pairs).
  const uint32_t text_fc = 1024;
  const uint32_t cp_count = static_cast<uint32_t>(text.size());
  std::vector<uint8_t> word = FibWord(cp_count);
  std::vector<uint8_t> table(21);
  for (size_t i = 0; i < text.size(); ++i)
    word[text_fc + i] = static_cast<uint8_t>(text[i]);

  table[0] = 2;
  Put32(table, 1, 16);  // Pcdt: 2 CPs (8 bytes) + one 8-byte PCD
  Put32(table, 5, 0);
  Put32(table, 9, cp_count);
  Put16(table, 13, 0);                            // PCD flags
  Put32(table, 15, 0x40000000u | (text_fc * 2));  // compressed piece
  Put16(table, 19, 0);                            // PCD prm

  auto fib = doc::ParseFib(word);
  if (!fib)
    return std::unexpected(fib.error());
  return doc::ParseFormatted(word, table, *fib);
}

std::string Xml(const model::Document& d) {
  auto zip = docx::Write(d);
  if (!zip)
    return {};
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip->data(), zip->size(), 0))
    return {};
  size_t size = 0;
  void* p =
      mz_zip_reader_extract_file_to_heap(&z, "word/document.xml", &size, 0);
  std::string xml;
  if (p) {
    xml.assign(static_cast<const char*>(p), size);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return xml;
}

// Contents of every <w:t> run, in document order.
std::vector<std::string> TextRuns(const std::string& xml) {
  std::vector<std::string> out;
  size_t pos = 0;
  while ((pos = xml.find("<w:t", pos)) != std::string::npos) {
    size_t open = xml.find('>', pos);
    size_t close = xml.find("</w:t>", open);
    if (open == std::string::npos || close == std::string::npos)
      break;
    out.push_back(xml.substr(open + 1, close - open - 1));
    pos = close;
  }
  return out;
}

std::string Convert(const std::string& text) {
  auto d = Synthetic(text);
  if (!d)
    return "parse error: " + d.error().message;
  return Xml(*d);
}
}  // namespace

TEST(M3Fields, GenericFieldEmitsFldCharsAndInstruction) {
  // \x13 TOC \o "1-3" \x14 Contents \x15
  const std::string text =
      "\x13 TOC \\o \"1-3\" \x14"
      "Contents"
      "\x15";
  const std::string xml = Convert(text);
  ASSERT_NE(xml.find("parse error"), 0u) << xml;
  EXPECT_NE(xml.find("<w:fldChar w:fldCharType=\"begin\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:instrText xml:space=\"preserve\"> TOC \\o \"1-3\" "
                     "</w:instrText>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:fldChar w:fldCharType=\"separate\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:fldChar w:fldCharType=\"end\"/>"), std::string::npos);
  // The instruction text belongs to w:instrText, not to a w:t run.
  std::vector<std::string> runs = TextRuns(xml);
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0], "Contents");
}

TEST(M3Fields, HyperlinkKeepsDedicatedMapping) {
  const std::string text =
      "\x13 HYPERLINK \"https://example.com\" \x14"
      "site"
      "\x15";
  const std::string xml = Convert(text);
  EXPECT_NE(xml.find("<w:hyperlink r:id="), std::string::npos);
  EXPECT_EQ(xml.find("w:fldChar"), std::string::npos);
  EXPECT_EQ(xml.find("w:instrText"), std::string::npos);
}

TEST(M3Fields, PageFieldKeepsFldSimple) {
  const std::string text =
      "\x13 PAGE \x14"
      "7"
      "\x15";
  const std::string xml = Convert(text);
  EXPECT_NE(xml.find("<w:fldSimple w:instr=\"PAGE\">"), std::string::npos);
  EXPECT_EQ(xml.find("w:fldChar"), std::string::npos);
}

TEST(M3Fields, NestedFieldsNestTheirMarkers) {
  // \x13 TOC \x14 \x13 PAGEREF _Toc1 \x14 3 \x15 \x15
  const std::string text =
      "\x13 TOC \x14"
      "\x13 PAGEREF _Toc1 \x14"
      "3"
      "\x15"
      "\x15";
  const std::string xml = Convert(text);
  const auto count = [&xml](const char* needle) {
    size_t hits = 0, at = 0;
    while ((at = xml.find(needle, at)) != std::string::npos) {
      ++hits;
      at += 1;
    }
    return hits;
  };
  EXPECT_EQ(count("<w:fldChar w:fldCharType=\"begin\"/>"), 2u);
  EXPECT_EQ(count("<w:fldChar w:fldCharType=\"separate\"/>"), 2u);
  EXPECT_EQ(count("<w:fldChar w:fldCharType=\"end\"/>"), 2u);
  EXPECT_NE(xml.find("PAGEREF _Toc1"), std::string::npos);
}

TEST(M3Fields, ManualPageBreakIsKept) {
  // \x0c that is not a section boundary is a manual page break.
  const std::string xml = Convert(
      "A\x0c"
      "B");
  EXPECT_NE(xml.find("<w:br w:type=\"page\"/>"), std::string::npos);
  std::vector<std::string> runs = TextRuns(xml);
  EXPECT_EQ(runs, (std::vector<std::string>{"A", "B"}));
}

TEST(M3Fields, FieldWithoutSeparatorStillCloses) {
  // A malformed field (no \x14) must not swallow the rest of the paragraph.
  const std::string text =
      "\x13 BROKEN \x15"
      "tail";
  const std::string xml = Convert(text);
  EXPECT_NE(xml.find("<w:fldChar w:fldCharType=\"end\"/>"), std::string::npos);
  std::vector<std::string> runs = TextRuns(xml);
  ASSERT_FALSE(runs.empty());
  EXPECT_EQ(runs.back(), "tail");
}
