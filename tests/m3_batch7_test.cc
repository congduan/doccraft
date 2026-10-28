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

#include "doc/format.h"
#include "doc/format/sprm.h"
#include "docx/writer.h"
#include "third_party/miniz/miniz.h"

namespace {
namespace doc = doccraft::doc;
namespace docx = doccraft::docx;
namespace model = doccraft::model;

using doc::detail::Bytes;

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t n) {
  b[p] = n & 255;
  b[p + 1] = n >> 8;
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t n) {
  Put16(b, p, n & 65535);
  Put16(b, p + 2, n >> 16);
}

std::vector<uint8_t> Prl(uint16_t op, std::vector<uint8_t> value) {
  std::vector<uint8_t> result{static_cast<uint8_t>(op),
                              static_cast<uint8_t>(op >> 8)};
  result.insert(result.end(), value.begin(), value.end());
  return result;
}

std::vector<uint8_t> Sprms(std::initializer_list<std::vector<uint8_t>> list) {
  std::vector<uint8_t> out;
  for (const auto& item : list)
    out.insert(out.end(), item.begin(), item.end());
  return out;
}

std::vector<uint8_t> OutlineStyles() {
  std::vector<uint8_t> stsh(20, 0);
  Put16(stsh, 0, 18);
  Put16(stsh, 2, 11);
  Put16(stsh, 4, 10);
  for (int i = 0; i < 11; ++i) {
    std::vector<uint8_t> record(10, 0);
    Put16(record, 2, static_cast<uint16_t>(1 | ((i ? i - 1 : 0x0FFF) << 4)));
    Put16(record, 4, 1);
    const std::string name = i ? "Heading " + std::to_string(i) : "Normal";
    record.push_back(static_cast<uint8_t>(name.size()));
    record.push_back(0);
    for (char c : name) {
      record.push_back(static_cast<uint8_t>(c));
      record.push_back(0);
    }
    record.insert(record.end(), {0, 0});
    if (record.size() & 1)
      record.push_back(0);
    const auto sprms = i == 1 ? Prl(0x2640, {5}) : std::vector<uint8_t>{};
    const size_t upx = record.size();
    record.resize(upx + 4 + sprms.size());
    Put16(record, upx, static_cast<uint16_t>(2 + sprms.size()));
    std::copy(sprms.begin(), sprms.end(), record.begin() + upx + 4);
    if (record.size() & 1)
      record.push_back(0);
    const size_t pos = stsh.size();
    stsh.resize(pos + 2 + record.size());
    Put16(stsh, pos, static_cast<uint16_t>(record.size()));
    std::copy(record.begin(), record.end(), stsh.begin() + pos + 2);
  }
  return stsh;
}
// Minimal single-paragraph document: one piece "X\r" with one CHPX and one
// PAPX.
std::expected<model::Document, doc::Error> Synthetic(
    std::vector<uint8_t> pap, std::vector<uint8_t> chp,
    std::vector<uint8_t> data = {}, uint16_t style = 0,
    const std::vector<uint8_t>& stsh = {}) {
  std::vector<uint8_t> word(1536), table(64);
  word[1024] = 'X';
  word[1025] = '\r';
  table[0] = 2;
  Put32(table, 1, 16);
  Put32(table, 5, 0);
  Put32(table, 9, 2);
  Put32(table, 15, 0x40000000u + 2048);
  auto fkp = [&](size_t base, size_t page, std::vector<uint8_t> sprms,
                 bool paragraph) {
    Put32(word, base, 1024);
    Put32(word, base + 4, 1026);
    word[base + 8] = 20;
    word[base + 511] = 1;
    if (paragraph) {
      if ((sprms.size() & 1) == 0) {
        auto nop = Prl(0x0000, {0});
        sprms.insert(sprms.end(), nop.begin(), nop.end());
      }
      word[base + 40] = static_cast<uint8_t>((sprms.size() + 3) / 2);
      Put16(word, base + 41, style);
      std::copy(sprms.begin(), sprms.end(), word.begin() + base + 43);
    } else {
      word[base + 40] = static_cast<uint8_t>(sprms.size());
      std::copy(sprms.begin(), sprms.end(), word.begin() + base + 41);
    }

    Put32(table, page, 1024);
    Put32(table, page + 4, 1026);
    Put32(table, page + 8, static_cast<uint32_t>(base / 512));
  };
  fkp(0, 32, pap, true);
  fkp(512, 48, chp, false);
  doc::Fib fib;
  fib.ccp_text = 2;
  fib.fc_clx = 0;
  fib.lcb_clx = 21;
  fib.papx_bte = {32, 12};
  fib.chpx_bte = {48, 12};
  if (!stsh.empty()) {
    fib.stsh = {64, static_cast<uint32_t>(stsh.size())};
    table.resize(64 + stsh.size());
    std::copy(stsh.begin(), stsh.end(), table.begin() + 64);
  }
  return doc::ParseFormatted(word, table, fib, Bytes(data));
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

std::string RoundTrip(const std::vector<uint8_t>& pap,
                      const std::vector<uint8_t>& chp) {
  auto d = Synthetic(pap, chp);
  if (!d)
    return "parse error: " + d.error().message;
  return Xml(*d);
}

TEST(M3Batch7, CharacterEmphasisMark) {
  struct Case {
    uint8_t code;
    const char* value;
  };
  const Case cases[] = {
      {0, "none"}, {1, "dot"}, {2, "comma"}, {3, "circle"}, {4, "underDot"}};
  for (const auto& c : cases) {
    SCOPED_TRACE(static_cast<int>(c.code));
    EXPECT_NE(RoundTrip({}, Prl(0x2A34, {c.code}))
                  .find(std::string("<w:em w:val=\"") + c.value + "\"/>"),
              std::string::npos);
  }

  EXPECT_EQ(RoundTrip({}, Prl(0x2A34, {9})).find("<w:em "), std::string::npos);
}

TEST(M3Batch7, HiddenTextSources) {
  // sprmCFFldVanish and sprmCFSdtVanish both hide text in the output.
  EXPECT_NE(RoundTrip({}, Prl(0x0802, {1})).find("<w:vanish/>"),
            std::string::npos);
  EXPECT_NE(RoundTrip({}, Prl(0x2A90, {1})).find("<w:vanish/>"),
            std::string::npos);
  EXPECT_NE(RoundTrip({}, Prl(0x2A90, {1, 0})).find("<w:vanish/>"),
            std::string::npos);
}

TEST(M3Batch7, PlainResetsDirectCharacterFormatting) {
  EXPECT_NE(RoundTrip({}, Sprms({Prl(0x0835, {1})})).find("<w:b/>"),
            std::string::npos);
  std::string after =
      RoundTrip({}, Sprms({Prl(0x0835, {1}), Prl(0x2A33, {0})}));
  EXPECT_EQ(after.find("<w:b/>"), std::string::npos);
  std::string before =
      RoundTrip({}, Sprms({Prl(0x2A33, {0}), Prl(0x0835, {1})}));
  EXPECT_NE(before.find("<w:b/>"), std::string::npos);
}

TEST(M3Batch7, Shd80PaletteToRgb) {
  // icoFore = red (6), icoBack = yellow (7), ipat = solid (1).
  const std::vector<uint8_t> shade{0xE6, 0x04};
  std::string run = RoundTrip({}, Prl(0x4866, shade));
  EXPECT_NE(run.find("w:val=\"solid\" w:fill=\"FFFF00\" w:color=\"FF0000\""),
            std::string::npos);
  std::string para = RoundTrip(Prl(0x442D, shade), {});
  EXPECT_NE(para.find("w:val=\"solid\" w:fill=\"FFFF00\" w:color=\"FF0000\""),
            std::string::npos);
}

TEST(M3Batch7, ParagraphFrameGeometry) {
  std::string xml =
      RoundTrip(Sprms({Prl(0x8418, {0xD0, 0x02}), Prl(0x8419, {0x88, 0xFF}),
                       Prl(0x841A, {0xE0, 0x10}), Prl(0x442B, {0x60, 0x89}),
                       Prl(0x842E, {0x78, 0x00}), Prl(0x842F, {0x90, 0x01}),
                       Prl(0x2430, {1})}),
                {});
  for (const char* expected :
       {"w:x=\"720\"", "w:y=\"-120\"", "w:w=\"4320\"", "w:h=\"2400\"",
        "w:hRule=\"exact\"", "w:vSpace=\"120\"", "w:hSpace=\"400\"",
        "w:lock=\"1\""})
    EXPECT_NE(xml.find(expected), std::string::npos) << expected;
  EXPECT_NE(xml.find("<w:framePr"), std::string::npos);
}

TEST(M3Batch7, ParagraphNestAndListLevelIncrement) {
  std::string nested = RoundTrip(
      Sprms({Prl(0x840F, {0xE8, 0x03}), Prl(0x4610, {0xC8, 0x00})}), {});
  EXPECT_NE(nested.find("w:left=\"1200\""), std::string::npos);
  std::string legacy = RoundTrip(
      Sprms({Prl(0x840F, {0xE8, 0x03}), Prl(0x465F, {0xC8, 0x00})}), {});
  EXPECT_NE(legacy.find("w:left=\"1200\""), std::string::npos);
  std::string list = RoundTrip(
      Sprms({Prl(0x460B, {1, 0}), Prl(0x260A, {1}), Prl(0x2602, {2})}), {});
  EXPECT_NE(list.find("<w:ilvl w:val=\"3\"/>"), std::string::npos);
  std::string clamped = RoundTrip(
      Sprms({Prl(0x460B, {1, 0}), Prl(0x260A, {7}), Prl(0x2602, {5})}), {});
  EXPECT_NE(clamped.find("<w:ilvl w:val=\"8\"/>"), std::string::npos);
}

TEST(M3Batch7, ParagraphStyleLevelIncrementWithoutList) {
  auto document = Synthetic(Prl(0x2602, {1}), {}, {}, 2);
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_FALSE(document->paragraphs.empty());
  EXPECT_EQ(document->paragraphs[0].style, 3);
  EXPECT_EQ(document->paragraphs[0].list_id, -1);
  EXPECT_NE(Xml(*document).find("<w:pStyle w:val=\"Heading3\"/>"),
            std::string::npos);

  auto listed = Synthetic(
      Sprms({Prl(0x460B, {1, 0}), Prl(0x260A, {1}), Prl(0x2602, {1})}), {}, {},
      2);
  ASSERT_TRUE(listed) << listed.error().message;
  EXPECT_EQ(listed->paragraphs[0].style, 3);
  EXPECT_NE(Xml(*listed).find("<w:ilvl w:val=\"2\"/>"), std::string::npos);
}

TEST(M3Batch7, ParagraphStyleLevelIncrementClampsAndOffsetsOutline) {
  // istd 1..9: clamp to [1, 9].
  auto hi = Synthetic(Prl(0x2602, {1}), {}, {}, 9);
  ASSERT_TRUE(hi) << hi.error().message;
  EXPECT_EQ(hi->paragraphs[0].style, 9);
  auto lo = Synthetic(Prl(0x2602, {0xFF}), {}, {}, 1);
  ASSERT_TRUE(lo) << lo.error().message;
  EXPECT_EQ(lo->paragraphs[0].style, 1);

  // Other styles: offset an explicit outline level and clamp to [0, 9].
  auto out = Synthetic(Sprms({Prl(0x2640, {4}), Prl(0x2602, {2})}), {});
  ASSERT_TRUE(out) << out.error().message;
  EXPECT_EQ(out->paragraphs[0].outline, 6);
  EXPECT_NE(Xml(*out).find("<w:outlineLvl w:val=\"6\"/>"), std::string::npos);

  // Outline level 9 is body text and is not incremented.
  auto body = Synthetic(Sprms({Prl(0x2640, {9}), Prl(0x2602, {1})}), {});
  ASSERT_TRUE(body) << body.error().message;
  EXPECT_EQ(body->paragraphs[0].outline, 9);

  // Canonical ispmd order applies POutLvl after PIncLvl.
  auto canon = Synthetic(Sprms({Prl(0x2602, {2}), Prl(0x2640, {4})}), {});
  ASSERT_TRUE(canon) << canon.error().message;
  EXPECT_EQ(canon->paragraphs[0].outline, 4);
}

TEST(M3Batch7, ParagraphStyleIncrementInheritsTargetOutline) {
  const auto stsh = OutlineStyles();
  auto inherited = Synthetic(Prl(0x2602, {1}), {}, {}, 2, stsh);
  ASSERT_TRUE(inherited) << inherited.error().message;
  ASSERT_FALSE(inherited->paragraphs.empty());
  EXPECT_EQ(inherited->paragraphs[0].style, 3);
  EXPECT_EQ(inherited->paragraphs[0].outline, 2);
  EXPECT_NE(Xml(*inherited).find("<w:outlineLvl w:val=\"2\"/>"),
            std::string::npos);

  auto explicit_level =
      Synthetic(Sprms({Prl(0x2602, {1}), Prl(0x2640, {2})}), {}, {}, 2, stsh);
  ASSERT_TRUE(explicit_level) << explicit_level.error().message;
  EXPECT_EQ(explicit_level->paragraphs[0].outline, 2);
  EXPECT_NE(Xml(*explicit_level).find("<w:outlineLvl w:val=\"2\"/>"),
            std::string::npos);

  auto non_heading = Synthetic(Prl(0x2602, {1}), {}, {}, 10, stsh);
  ASSERT_TRUE(non_heading) << non_heading.error().message;
  EXPECT_EQ(non_heading->paragraphs[0].style, 10);
  EXPECT_EQ(non_heading->paragraphs[0].outline, 6);
  EXPECT_NE(Xml(*non_heading).find("<w:outlineLvl w:val=\"6\"/>"),
            std::string::npos);
}

TEST(M3Batch7, ParagraphStyleIncrementIgnoresHeadingStyleOutline) {
  // Heading 1's UPX says outline 5, but final istd 1 requires outline 0.
  auto heading = Synthetic(Prl(0x2602, {0xFF}), {}, {}, 2, OutlineStyles());
  ASSERT_TRUE(heading) << heading.error().message;
  EXPECT_EQ(heading->paragraphs[0].style, 1);
  EXPECT_EQ(heading->paragraphs[0].outline, 0);
  EXPECT_NE(Xml(*heading).find("<w:outlineLvl w:val=\"0\"/>"),
            std::string::npos);
}

TEST(M3Batch7, ParagraphWithoutIncrementKeepsInheritedOutlineInStyle) {
  auto heading = Synthetic({}, {}, {}, 1, OutlineStyles());
  ASSERT_TRUE(heading) << heading.error().message;
  EXPECT_EQ(heading->paragraphs[0].outline, -1);
  EXPECT_EQ(Xml(*heading).find("<w:outlineLvl"), std::string::npos);
}

TEST(M3Batch7, EmbeddedObjectWithoutOleUsesPlaceholder) {
  auto document = Synthetic({}, Prl(0x0856, {1}));
  ASSERT_TRUE(document) << document.error().message;
  ASSERT_EQ(document->warnings.size(), 1u);
  EXPECT_EQ(document->warnings[0],
            "embedded object at CP 0 has no OLE payload; placeholder emitted");
  ASSERT_FALSE(document->paragraphs.empty());
  ASSERT_FALSE(document->paragraphs[0].runs.empty());
  EXPECT_EQ(document->paragraphs[0].runs[0].text, "[Embedded object]");
  EXPECT_NE(Xml(*document).find("[Embedded object]"), std::string::npos);
}

TEST(M3Batch7, IndirectParagraphSprms) {
  std::vector<uint8_t> nested = Prl(0x2407, {1});
  std::vector<uint8_t> data(2 + nested.size());
  Put16(data, 0, static_cast<uint16_t>(nested.size()));
  std::copy(nested.begin(), nested.end(), data.begin() + 2);
  auto direct = Synthetic(Prl(0x6646, {0, 0, 0, 0}), {}, data);
  ASSERT_TRUE(direct) << direct.error().message;
  EXPECT_NE(Xml(*direct).find("<w:pageBreakBefore/>"), std::string::npos);
  auto broken = Synthetic(Prl(0x6646, {0xF0, 0x00, 0, 0}), {}, data);
  ASSERT_TRUE(broken) << broken.error().message;
  EXPECT_EQ(Xml(*broken).find("<w:pageBreakBefore/>"), std::string::npos);
}

TEST(M3Batch7, TableStyleAndFloatingPosition) {
  model::Table t;
  model::RowProperties row;
  row.cells.resize(2);
  struct Case {
    uint16_t op;
    std::vector<uint8_t> value;
  };
  const Case cases[] = {
      {0x563A, {2, 0}},       {0x940E, {0xD0, 0x02}}, {0x940F, {0x88, 0xFF}},
      {0x9410, {0x64, 0x00}}, {0x9411, {0xC8, 0x00}}, {0x941E, {0x2C, 0x01}},
      {0x941F, {0x90, 0x01}},
  };
  for (const auto& c : cases)
    ASSERT_TRUE(doc::detail::ApplyTable(t, row, c.op, Bytes(c.value))) << c.op;
  EXPECT_EQ(t.style, 2);
  EXPECT_EQ(t.tblp_x, 720);
  EXPECT_EQ(t.tblp_y, -120);
  EXPECT_EQ(t.from_text[0], 100);
  EXPECT_EQ(t.from_text[1], 200);
  EXPECT_EQ(t.from_text[2], 300);
  EXPECT_EQ(t.from_text[3], 400);
  t.rows =
      std::vector<std::vector<model::Cell>>(1, std::vector<model::Cell>(1));
  t.row_properties = {row};
  model::Document d;
  d.tables.push_back(t);
  d.blocks.push_back({true, 0});
  std::string xml = Xml(d);
  for (const char* expected :
       {"<w:tblStyle w:val=\"Heading2\"", "w:leftFromText=\"100\"",
        "w:topFromText=\"200\"", "w:rightFromText=\"300\"",
        "w:bottomFromText=\"400\"", "w:tblpX=\"720\"", "w:tblpY=\"-120\"",
        "w:horzAnchor=\"margin\"", "w:vertAnchor=\"margin\""})
    EXPECT_NE(xml.find(expected), std::string::npos) << expected;
}

TEST(M3Batch7, TableRowShd80) {
  model::Table t;
  model::RowProperties row;
  row.cells.resize(2);
  const std::vector<uint8_t> v{4, 0xE6, 0x04, 0xC1, 0x04};

  ASSERT_TRUE(doc::detail::ApplyTable(t, row, 0xD609, Bytes(v)));
  EXPECT_EQ(row.cells[0].shade, 0x00FFFF);
  EXPECT_EQ(row.cells[0].shade_fore, 0x0000FF);
  EXPECT_EQ(row.cells[1].shade, 0x0000FF);
  t.rows =
      std::vector<std::vector<model::Cell>>(1, std::vector<model::Cell>(1));
  t.row_properties = {row};
  model::Document d;
  d.tables.push_back(t);
  d.blocks.push_back({true, 0});
  EXPECT_NE(Xml(d).find("w:fill=\"FFFF00\""), std::string::npos);
  ASSERT_FALSE(doc::detail::ApplyTable(t, row, 0xD609,
                                       Bytes(std::vector<uint8_t>{3, 1, 0})));
}

TEST(M3Batch7, SectionPaperSourceAndEndnotePosition) {
  model::Section s;
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x5007, Bytes(std::vector<uint8_t>{1, 0})));
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x5008, Bytes(std::vector<uint8_t>{2, 0})));
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x3012, Bytes(std::vector<uint8_t>{1})));
  EXPECT_EQ(s.paper_first, 1);
  EXPECT_EQ(s.paper_other, 2);
  model::Document d;
  d.section = s;
  std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:paperSrc w:first=\"1\" w:other=\"2\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:pos w:val=\"sectEnd\"/>"), std::string::npos);
  model::Section automatic;
  ASSERT_TRUE(doc::detail::ApplySection(
      automatic, 0x5007, Bytes(std::vector<uint8_t>{0xFF, 0xFF})));
  EXPECT_EQ(automatic.paper_first, -1);
}

TEST(M3Batch7, SectionColumnWidthAndSpacing) {
  model::Section s;
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x500B, Bytes(std::vector<uint8_t>{1, 0})));
  ASSERT_TRUE(doc::detail::ApplySection(
      s, 0xF203, Bytes(std::vector<uint8_t>{0, 0x40, 0x1F})));
  ASSERT_TRUE(doc::detail::ApplySection(
      s, 0xF204, Bytes(std::vector<uint8_t>{0, 0x2C, 0x01})));
  EXPECT_EQ(s.columns, 2);
  ASSERT_EQ(s.column_specs.size(), 1u);
  EXPECT_EQ(s.column_specs[0].width, 8000);
  EXPECT_EQ(s.column_specs[0].space, 300);
  model::Document d;
  d.section = s;
  std::string xml = Xml(d);
  EXPECT_NE(xml.find("w:equalWidth=\"0\""), std::string::npos);
  EXPECT_NE(xml.find("<w:col w:w=\"8000\" w:space=\"300\"/>"),
            std::string::npos);
  ASSERT_FALSE(doc::detail::ApplySection(
      s, 0xF203, Bytes(std::vector<uint8_t>{44, 0, 0})));
}
}  // namespace
