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

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"

namespace {
std::vector<uint8_t> ReadFile(const std::string& name) {
  const bool fixture = name == "header-footer" || name == "landscape" ||
                       name == "style-override" || name == "color-shading" ||
                       name == "character-effects" ||
                       name == "table-multi-paragraph";
  std::string path = std::string(DOCCRAFT_SOURCE_DIR) + "/tests/" +
                     (fixture ? "fixtures/" : "corpus/corpus/") + name + ".doc";
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

doccraft::model::Document Parse(const std::string& name) {
  auto b = ReadFile(name);
  auto c = doccraft::cfb::CfbReader::Open(b);
  if (!c)
    return {};
  auto w = c->ReadStream("WordDocument");
  if (!w)
    return {};
  auto fib = doccraft::doc::ParseFib(*w);
  if (!fib)
    return {};
  auto t = c->ReadStream(fib->table_stream());
  if (!t)
    return {};
  auto d = doccraft::doc::ParseFormatted(*w, *t, *fib);
  return d ? *d : doccraft::model::Document{};
}

TEST(FormatTest, KeepsEveryCellParagraph) {
  // A cell with two paragraphs must stay one cell with two paragraphs, not
  // become two cells, and the first paragraph must not drift into the body.
  auto d = Parse("table-multi-paragraph");
  ASSERT_EQ(d.tables.size(), 1u);
  const auto& table = d.tables[0];
  ASSERT_EQ(table.rows.size(), 1u);
  ASSERT_EQ(table.rows[0].size(), 2u);
  const auto text_at = [&d](const doccraft::model::Block& block) {
    return d.paragraphs[block.index].runs[0].text;
  };
  ASSERT_EQ(table.rows[0][0].blocks.size(), 2u);
  EXPECT_EQ(text_at(table.rows[0][0].blocks[0]), "P1");
  EXPECT_EQ(text_at(table.rows[0][0].blocks[1]), "P2");
  ASSERT_EQ(table.rows[0][1].blocks.size(), 1u);
  EXPECT_EQ(text_at(table.rows[0][1].blocks[0]), "Q1");
  // The body keeps its own paragraphs on both sides of the table.
  ASSERT_EQ(d.blocks.size(), 3u);
  EXPECT_FALSE(d.blocks[0].table);
  EXPECT_TRUE(d.blocks[1].table);
  EXPECT_FALSE(d.blocks[2].table);
  EXPECT_EQ(text_at(d.blocks[0]), "Before");
  EXPECT_EQ(text_at(d.blocks[2]), "After");
}

TEST(FormatTest, ReadsDirectCharacterFormatting) {
  auto d = Parse("bold-italic-underline");
  ASSERT_EQ(d.paragraphs.size(), 1u);
  ASSERT_EQ(d.paragraphs[0].runs.size(), 3u);
  const auto& r = d.paragraphs[0].runs[1];
  EXPECT_EQ(r.text, "bold italic underline");
  EXPECT_TRUE(r.bold);
  EXPECT_TRUE(r.italic);
  EXPECT_EQ(r.underline, 1);
  EXPECT_EQ(d.styles[0].run.font, "Liberation Serif");
  EXPECT_EQ(d.styles[0].run.size, 24);
}

TEST(FormatTest, ReadsParagraphSpacingAndIndent) {
  auto d = Parse("alignment-indents");
  ASSERT_EQ(d.paragraphs.size(), 2u);
  EXPECT_EQ(d.paragraphs[0].align, 1);
  EXPECT_EQ(d.paragraphs[0].before, 288);
  EXPECT_EQ(d.paragraphs[0].after, 288);
  EXPECT_EQ(d.paragraphs[1].align, 3);
  EXPECT_EQ(d.paragraphs[1].left, 720);
}

TEST(FormatTest, ReadsStyleChainAndListLevels) {
  auto d = Parse("headings-list");
  ASSERT_GT(d.styles.size(), 16u);
  EXPECT_EQ(d.styles[1].name, "Heading 1");
  EXPECT_EQ(d.styles[1].base, 16);
  EXPECT_TRUE(d.styles[1].run.bold);
  EXPECT_EQ(d.styles[1].resolved_run.font, "Liberation Sans");
  EXPECT_EQ(d.styles[1].resolved_run.size, 36);
  ASSERT_EQ(d.lists.size(), 2u);
  EXPECT_EQ(d.lists[0].levels.size(), 9u);
  EXPECT_EQ(d.lists[1].levels[0].format, 0x17);
  EXPECT_EQ(d.lists[1].levels[0].left, 720);
  EXPECT_EQ(d.lists[1].levels[0].first, -360);
  EXPECT_EQ(d.lists[1].levels[0].font, "starbats");
  EXPECT_EQ(d.list_overrides.size(), 2u);
  EXPECT_EQ(d.paragraphs[3].list_id, 2);
}

TEST(FormatTest, AdvancesPastListLevelOverrides) {
  std::vector<uint8_t> word(512), table(136);
  auto u16 = [&](size_t p, uint16_t v) {
    table[p] = v & 255;
    table[p + 1] = v >> 8;
  };
  auto u32 = [&](size_t p, uint32_t v) {
    u16(p, v & 65535);
    u16(p + 2, v >> 16);
  };
  // One Pcdt with no requested text, followed by one single-level list.
  table[0] = 2;
  u32(1, 16);
  u32(5, 0);
  u32(9, 1);
  u16(26, 1);
  u32(28, 42);
  table[28 + 26] = 1;
  u32(56, 1);
  u16(84, 0);
  // First LFO has one four-byte level override; the next LFO starts at 120.
  u32(96, 2);
  u32(100, 42);
  table[112] = 1;
  u32(120, 42);
  doccraft::doc::Fib fib;
  fib.fc_clx = 0;
  fib.lcb_clx = 21;
  fib.lists = {26, 60};
  fib.list_overrides = {96, 40};
  auto parsed = doccraft::doc::ParseFormatted(word, table, fib);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->list_overrides.size(), 2u);
  EXPECT_EQ(parsed->list_overrides[0], 0);
  EXPECT_EQ(parsed->list_overrides[1], 0);
}

TEST(FormatTest, ReadsTableGridAndCells) {
  auto d = Parse("simple-table");
  ASSERT_EQ(d.tables.size(), 1u);
  EXPECT_EQ(d.tables[0].columns, (std::vector<int>{4985, 4987}));
  ASSERT_EQ(d.tables[0].rows.size(), 2u);
  const auto cell_text = [&d](const doccraft::model::Block& block) {
    return d.paragraphs[block.index].runs[0].text;
  };
  EXPECT_EQ(cell_text(d.tables[0].rows[0][0].blocks[0]), "A1");
  EXPECT_EQ(cell_text(d.tables[0].rows[1][1].blocks[0]), "B2");
}

TEST(FormatTest, ReadsFirstOddEvenHeadersAndMargins) {
  auto d = Parse("header-footer");
  EXPECT_EQ(d.section.left, 1152);
  EXPECT_EQ(d.section.header_distance, 1152);
  EXPECT_TRUE(d.section.first_page_different);
  EXPECT_TRUE(d.section.odd_even_different);
  for (const auto& story : d.section.headers)
    ASSERT_FALSE(story.empty());
  EXPECT_EQ(d.section.headers[0][0].runs[0].text, "E");
  EXPECT_EQ(d.section.headers[4][0].runs[0].text, "F");
}

TEST(FormatTest, ReadsLandscapePageGeometry) {
  auto d = Parse("landscape");
  EXPECT_EQ(d.section.width, 15840);
  EXPECT_EQ(d.section.height, 12240);
  EXPECT_EQ(d.section.left, 1584);
  EXPECT_EQ(d.section.right, 864);
}

TEST(FormatTest, DirectFormattingCanTurnBoldOff) {
  auto d = Parse("style-override");
  ASSERT_EQ(d.paragraphs.size(), 1u);
  ASSERT_EQ(d.paragraphs[0].runs.size(), 3u);
  EXPECT_TRUE(d.paragraphs[0].runs[0].bold);
  EXPECT_TRUE(d.paragraphs[0].runs[1].bold_set);
  EXPECT_FALSE(d.paragraphs[0].runs[1].bold);
  EXPECT_TRUE(d.paragraphs[0].runs[2].bold);
}

TEST(FormatTest, ReadsRgbColorAndCharacterShading) {
  auto d = Parse("color-shading");
  ASSERT_EQ(d.paragraphs.size(), 1u);
  ASSERT_EQ(d.paragraphs[0].runs.size(), 3u);
  const auto& r = d.paragraphs[0].runs[1];
  EXPECT_TRUE(r.color_rgb);
  EXPECT_EQ(r.color, 0x000000ff);
  EXPECT_EQ(r.shade, 0x0000ffff);
}

TEST(FormatTest, ReadsSuperSubAndStrike) {
  auto d = Parse("character-effects");
  ASSERT_EQ(d.paragraphs.size(), 1u);
  bool super = false, sub = false, strike = false;
  for (const auto& r : d.paragraphs[0].runs) {
    if (r.text == "super")
      super = r.super_sub == 1;
    if (r.text == "sub")
      sub = r.super_sub == 2;
    if (r.text == "strike")
      strike = r.strike;
  }

  EXPECT_TRUE(super);
  EXPECT_TRUE(sub);
  EXPECT_TRUE(strike);
}
}  // namespace
