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
#include <string>
#include <vector>

#include "miniz.h"

#include "doc/format.h"
#include "doc/format/sprm.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;
using doc::detail::Bytes;

std::string Part(const model::Document& d, const char* name) {
  auto zip = docx::Write(d);
  if (!zip)
    return {};
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip->data(), zip->size(), 0))
    return {};
  size_t size = 0;
  void* p = mz_zip_reader_extract_file_to_heap(&z, name, &size, 0);
  std::string xml;
  if (p) {
    xml.assign(static_cast<const char*>(p), size);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return xml;
}

std::string Xml(const model::Document& d) {
  return Part(d, "word/document.xml");
}

model::Document WithRun(const model::Run& run) {
  model::Document d;
  model::Paragraph p;
  p.runs.push_back(run);
  d.paragraphs.push_back(p);
  return d;
}

// CNFOperand: cb(1), cnfc(2), then the nested grpprl.
std::vector<uint8_t> Cnf(int cnfc, const std::vector<uint8_t>& grpprl) {
  std::vector<uint8_t> out(3 + grpprl.size());
  out[0] = static_cast<uint8_t>(grpprl.size() + 2);
  out[1] = static_cast<uint8_t>(cnfc & 0xFF);
  out[2] = static_cast<uint8_t>((cnfc >> 8) & 0xFF);
  std::copy(grpprl.begin(), grpprl.end(), out.begin() + 3);
  return out;
}

std::vector<uint8_t> Prl(uint16_t op, const std::vector<uint8_t>& value) {
  std::vector<uint8_t> out{static_cast<uint8_t>(op),
                           static_cast<uint8_t>(op >> 8)};
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

std::vector<uint8_t> Sprms(std::initializer_list<std::vector<uint8_t>> list) {
  std::vector<uint8_t> out;
  for (const auto& item : list)
    out.insert(out.end(), item.begin(), item.end());
  return out;
}

TEST(M3Batch8, RevisionMarksWrapRuns) {
  model::Run deleted;
  deleted.text = "gone";
  deleted.revision_delete = true;
  std::string xml = Xml(WithRun(deleted));
  EXPECT_NE(xml.find("<w:del w:id=\"1\" w:author=\"doccraft\">"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:delText"), std::string::npos);

  model::Run inserted;
  inserted.text = "new";
  inserted.revision_insert = true;
  std::string ins = Xml(WithRun(inserted));
  EXPECT_NE(ins.find("<w:ins w:id=\"1\" w:author=\"doccraft\">"),
            std::string::npos);
  EXPECT_NE(ins.find("<w:t xml:space=\"preserve\">new</w:t>"),
            std::string::npos);
}

TEST(M3Batch8, FitTextAndEastAsianLayout) {
  model::Run fit;
  fit.text = "X";
  // CFitTextOperand: cb = 8, dxaFitText = 1440, FitTextID = 7.
  const std::vector<uint8_t> fit_value{8, 0xA0, 0x05, 0, 0, 7, 0, 0, 0};
  doc::detail::ApplyRun(fit, 0xCA76, Bytes(fit_value), {});
  EXPECT_EQ(fit.fit_text, 1440);
  EXPECT_EQ(fit.fit_text_id, 7);
  std::string xml = Xml(WithRun(fit));
  EXPECT_NE(xml.find("<w:fitText w:val=\"1440\" w:id=\"7\"/>"),
            std::string::npos);

  model::Run ea;
  ea.text = "X";
  // FarEastLayoutOperand: cb = 6, ufel = fTNY | fWarichu | bracket 2,
  // lFELayoutID = 9.
  const std::vector<uint8_t> ea_value{6, 0x03, 0x12, 9, 0, 0, 0};
  doc::detail::ApplyRun(ea, 0xCA78, Bytes(ea_value), {});
  std::string ea_xml = Xml(WithRun(ea));
  EXPECT_NE(ea_xml.find("w:vert=\"1\""), std::string::npos);
  EXPECT_NE(ea_xml.find("w:combine=\"1\""), std::string::npos);
  EXPECT_NE(ea_xml.find("w:combineBrackets=\"square\""), std::string::npos);
  EXPECT_NE(ea_xml.find("w:vertCompress=\"1\""), std::string::npos);
  // A zero-length operand must not be applied.
  model::Run bad;
  doc::detail::ApplyRun(bad, 0xCA76, Bytes(std::vector<uint8_t>{8, 0}), {});
  EXPECT_EQ(bad.fit_text, 0);
}

TEST(M3Batch8, ComplexScriptColor) {
  model::Run run;
  run.text = "X";
  run.rtl = true;
  run.rtl_set = true;
  const std::vector<uint8_t> red{6, 0};  // Ico 6 = red
  doc::detail::ApplyRun(run, 0x4A60, Bytes(red), {});
  std::string xml = Xml(WithRun(run));
  EXPECT_NE(xml.find("<w:color w:val=\"FF0000\"/>"), std::string::npos);
}

TEST(M3Batch8, DropCapAndFrameTextFlow) {
  model::Paragraph p;
  const std::vector<uint8_t> dcs{0x09, 0};  // fdct = 1, cl = 1
  doc::detail::ApplyPara(p, 0x442C, Bytes(dcs));
  EXPECT_EQ(p.drop_cap, 1);
  EXPECT_EQ(p.drop_cap_lines, 1);
  model::Document d;
  d.paragraphs.push_back(p);
  std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:framePr w:dropCap=\"drop\" w:lines=\"1\""),
            std::string::npos);

  model::Paragraph flow;
  const std::vector<uint8_t> vertical{0x01, 0};
  doc::detail::ApplyPara(flow, 0x443A, Bytes(vertical));
  model::Document d2;
  d2.paragraphs.push_back(flow);
  EXPECT_NE(Xml(d2).find("w:textFlow=\"tbRl\""), std::string::npos);
}

TEST(M3Batch8, CharacterUnitIndentsAndLineSpacing) {
  model::Paragraph p;
  doc::detail::ApplyPara(p, 0x4456, Bytes(std::vector<uint8_t>{0x20, 0x03}));
  doc::detail::ApplyPara(p, 0x4455, Bytes(std::vector<uint8_t>{0x10, 0x01}));
  doc::detail::ApplyPara(p, 0x4457, Bytes(std::vector<uint8_t>{0x00, 0x02}));
  doc::detail::ApplyPara(p, 0x4458, Bytes(std::vector<uint8_t>{0x64, 0x00}));
  doc::detail::ApplyPara(p, 0x4459, Bytes(std::vector<uint8_t>{0xC8, 0x00}));
  EXPECT_EQ(p.left_chars, 800);
  EXPECT_EQ(p.right_chars, 272);
  EXPECT_EQ(p.first_line_chars, 512);
  model::Document d;
  d.paragraphs.push_back(p);
  std::string xml = Xml(d);
  EXPECT_NE(xml.find("w:leftChars=\"800\""), std::string::npos);
  EXPECT_NE(xml.find("w:rightChars=\"272\""), std::string::npos);
  EXPECT_NE(xml.find("w:firstLineChars=\"512\""), std::string::npos);
  EXPECT_NE(xml.find("w:beforeLines=\"100\""), std::string::npos);
  EXPECT_NE(xml.find("w:afterLines=\"200\""), std::string::npos);
}

TEST(M3Batch8, VerticalFontAlignment) {
  model::Paragraph p;
  doc::detail::ApplyPara(p, 0x4439, Bytes(std::vector<uint8_t>{0x02, 0x00}));
  EXPECT_EQ(p.text_alignment, 2);
  model::Document d;
  d.paragraphs.push_back(p);
  EXPECT_NE(Xml(d).find("<w:textAlignment w:val=\"baseline\"/>"),
            std::string::npos);
  model::Paragraph bad;
  doc::detail::ApplyPara(bad, 0x4439, Bytes(std::vector<uint8_t>{0x09, 0x00}));
  EXPECT_EQ(bad.text_alignment, -1);
}

TEST(M3Batch8, TableStyleOptionsAndAnchors) {
  model::Table t;
  model::RowProperties row;
  // TLP: itl = -1, grfatl = firstRow | lastRow | firstCol.
  const std::vector<uint8_t> tlp{0xFF, 0xFF, 0xE0, 0x00};
  ASSERT_TRUE(doc::detail::ApplyTable(t, row, 0x740A, Bytes(tlp)));
  EXPECT_EQ(t.look, 0x06E0);
  ASSERT_TRUE(
      doc::detail::ApplyTable(t, row, 0x3488, Bytes(std::vector<uint8_t>{2})));
  ASSERT_TRUE(
      doc::detail::ApplyTable(t, row, 0x3489, Bytes(std::vector<uint8_t>{1})));
  // PositionCodeOperand: pcVert = 1 (page), pcHorz = 2 (text).
  ASSERT_TRUE(doc::detail::ApplyTable(t, row, 0x360D,
                                      Bytes(std::vector<uint8_t>{0x91})));
  ASSERT_TRUE(doc::detail::ApplyTable(t, row, 0x940E,
                                      Bytes(std::vector<uint8_t>{0xD0, 0x02})));
  // CSSAOperand: itcFirst = 0, itcLim = 1, ftsDxa, all sides, 108 twips.
  ASSERT_TRUE(doc::detail::ApplyTable(
      t, row, 0xD63E, Bytes(std::vector<uint8_t>{6, 0, 1, 15, 3, 108, 0})));
  EXPECT_EQ(t.anchor_v, 1);
  EXPECT_EQ(t.anchor_h, 2);
  EXPECT_EQ(t.cell_margins[0], 108);
  EXPECT_EQ(t.row_band, 2);
  EXPECT_EQ(t.col_band, 1);
  t.rows = {{{}}};
  t.row_properties = {row};
  model::Document d;
  d.tables.push_back(t);
  d.blocks.push_back({true, 0});
  std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:tblLook w:val=\"06E0\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:tblStyleRowBandSize w:val=\"2\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:tblStyleColBandSize w:val=\"1\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("w:horzAnchor=\"text\""), std::string::npos);
  EXPECT_NE(xml.find("w:vertAnchor=\"page\""), std::string::npos);
}

TEST(M3Batch8, RowBorderColors) {
  model::Table t;
  model::RowProperties row;
  // TDefTable with a single 4000 twip column and one zeroed TC80 cell.
  std::vector<uint8_t> def(27, 0);
  def[2] = 1;
  def[5] = 0xA0;
  def[6] = 0x0F;
  ASSERT_TRUE(doc::detail::ApplyTable(t, row, 0xD608, Bytes(def)));
  ASSERT_EQ(row.cells.size(), 1u);
  // BrcCvOperand: cb = 4, one COLORREF per cell.
  const std::vector<uint8_t> colors{4, 0, 0, 255, 0};
  ASSERT_TRUE(doc::detail::ApplyTable(t, row, 0xD61A, Bytes(colors)));
  EXPECT_EQ(row.cells[0].borders[0].color, 0x00FF0000);
  // A colour does not create a border on its own.
  EXPECT_FALSE(row.cells[0].borders[0].set);
  ASSERT_FALSE(doc::detail::ApplyTable(
      t, row, 0xD61A, Bytes(std::vector<uint8_t>{8, 0, 0, 0, 0})));
}

TEST(M3Batch8, PageBorderScopeAndFormProtection) {
  model::Section s;
  // SPgbPropOperand: pgbApplyTo = 2 (all but first), pgbOffsetFrom = 1 (text).
  ASSERT_TRUE(doc::detail::ApplySection(s, 0x522F,
                                        Bytes(std::vector<uint8_t>{0x22, 0})));
  ASSERT_TRUE(doc::detail::ApplySection(
      s, 0xD234, Bytes(std::vector<uint8_t>{8, 0, 0, 255, 0, 8, 3, 2, 0})));
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x3006, Bytes(std::vector<uint8_t>{1})));
  EXPECT_EQ(s.page_border_apply_to, 2);
  EXPECT_EQ(s.page_border_offset_from, 1);
  EXPECT_TRUE(s.form_unprotected);
  model::Document d;
  d.section = s;
  std::string xml = Xml(d);
  EXPECT_NE(xml.find("w:offsetFrom=\"text\""), std::string::npos);
  EXPECT_NE(xml.find("w:display=\"notFirstPage\""), std::string::npos);
  EXPECT_NE(xml.find("<w:formProt w:val=\"0\"/>"), std::string::npos);
  ASSERT_FALSE(
      doc::detail::ApplySection(s, 0x522F, Bytes(std::vector<uint8_t>{0, 1})));
}

// Builds a table style whose TAPX carries one sprmTCnf block.
model::Style TableStyleWithCondition(int cnfc,
                                     const std::vector<uint8_t>& grpprl) {
  model::Style style;
  style.type = 3;
  style.name = "Doccraft Table";
  std::vector<uint8_t> cnf = Cnf(cnfc, grpprl);
  EXPECT_TRUE(doc::detail::ApplyConditional(style, 0xD66A, Bytes(cnf), {}));
  return style;
}

TEST(M3Batch8, TableStyleConditionalFormatting) {
  const std::vector<uint8_t> brc{8, 0, 0, 255, 0, 8, 3, 2, 0};
  const std::vector<uint8_t> shd{10, 0, 0, 255, 0, 255, 255, 0, 0, 8, 0};
  std::vector<uint8_t> grpprl = Sprms({
      Prl(0xD47F, brc), Prl(0xD683, brc), Prl(0xD687, shd), Prl(0x347C, {1}),
      Prl(0x347D, {1}), Prl(0xC64E, brc),  // paragraph top border
      Prl(0x4A30, {0, 0}),  // paragraph style via the character group
      Prl(0x0835, {1}),     // bold
  });
  model::Style style = TableStyleWithCondition(0x0001, grpprl);
  ASSERT_EQ(style.conditionals.size(), 1u);
  const auto& c = style.conditionals[0];
  EXPECT_EQ(c.type, 1);
  EXPECT_TRUE(c.borders[0].set);
  EXPECT_TRUE(c.borders[4].set);
  EXPECT_EQ(c.shade, 0x00FFFF);
  EXPECT_EQ(c.vertical_align, 1);
  EXPECT_TRUE(c.no_wrap);
  EXPECT_TRUE(c.paragraph.borders[0].set);
  EXPECT_TRUE(c.run.bold);

  model::Document d;
  d.styles.push_back(style);
  std::string xml = Part(d, "word/styles.xml");
  EXPECT_NE(xml.find("<w:tblStylePr w:type=\"firstRow\">"), std::string::npos);
  EXPECT_NE(xml.find("<w:top w:val=\"double\""), std::string::npos);
  EXPECT_NE(xml.find("w:insideH"), std::string::npos);
  EXPECT_NE(xml.find("<w:vAlign w:val=\"center\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:noWrap/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:b/>"), std::string::npos);
  // Conditions outside the CNF table are not emitted.
  EXPECT_EQ(xml.find("<w:tblStylePr w:type=\"band1Horz\">"), std::string::npos);
  model::Style alien;
  EXPECT_FALSE(doc::detail::ApplyConditional(
      alien, 0x0835, Bytes(std::vector<uint8_t>{1}), {}));
}
}  // namespace
