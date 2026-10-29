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

// M20 writer property matrix. These tests build the IR directly (no DOC
// parsing) so each OOXML mapping is pinned independently of the layer that
// reads the binary file.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "miniz.h"

#include "docx/writer.h"
#include "model/document.h"

namespace {
using namespace doccraft;

std::string Part(const model::Document& d, const char* name) {
  auto zip = docx::Write(d);
  if (!zip) {
    return {};
  }
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip->data(), zip->size(), 0)) {
    return {};
  }
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

size_t Count(const std::string& text, const std::string& needle) {
  size_t hits = 0, at = 0;
  while ((at = text.find(needle, at)) != std::string::npos) {
    ++hits;
    at += needle.size();
  }
  return hits;
}

model::Paragraph ParagraphWith(const std::string& text) {
  model::Paragraph p;
  model::Run r;
  r.text = text;
  p.runs.push_back(std::move(r));
  return p;
}

model::Document WithParagraphs(std::vector<model::Paragraph> paragraphs) {
  model::Document d;
  d.paragraphs = std::move(paragraphs);
  return d;
}

model::Table EmptyTable(int columns, int rows) {
  model::Table t;
  for (int i = 0; i < columns; ++i) {
    t.columns.push_back(2000);
  }
  for (int r = 0; r < rows; ++r) {
    std::vector<model::Cell> row(static_cast<size_t>(columns));
    t.rows.push_back(std::move(row));
    t.row_properties.emplace_back();
    t.row_properties.back().cells.resize(static_cast<size_t>(columns));
  }
  return t;
}

model::Document WithTable(model::Table table) {
  model::Document d;
  d.tables.push_back(std::move(table));
  d.blocks.push_back({true, 0});
  return d;
}

TEST(M20WriterMatrix, ParagraphAlignmentFive) {
  model::Document d;
  const char* expected[] = {"left", "center", "right", "both", "distribute"};
  for (int align = 0; align < 5; ++align) {
    model::Paragraph p = ParagraphWith("x");
    p.align = align;
    d.paragraphs.push_back(std::move(p));
  }
  const std::string xml = Xml(d);
  for (const char* value : expected) {
    EXPECT_NE(xml.find(std::string("<w:jc w:val=\"") + value + "\"/>"),
              std::string::npos)
        << value;
  }
  EXPECT_EQ(Count(xml, "<w:jc "), 5u);
}

TEST(M20WriterMatrix, ParagraphIndentFour) {
  model::Document d;
  model::Paragraph left = ParagraphWith("a");
  left.left_set = true;
  left.left = 720;
  left.right_set = true;
  left.right = 360;
  left.first_set = true;
  left.first = 240;
  d.paragraphs.push_back(left);

  model::Paragraph hanging = ParagraphWith("b");
  hanging.first_set = true;
  hanging.first = -360;
  d.paragraphs.push_back(hanging);

  model::Paragraph chars = ParagraphWith("c");
  chars.left_chars_set = true;
  chars.left_chars = 200;
  chars.right_chars_set = true;
  chars.right_chars = 100;
  chars.first_line_chars_set = true;
  chars.first_line_chars = 150;
  d.paragraphs.push_back(chars);

  model::Paragraph nested = ParagraphWith("d");
  nested.nest_set = true;
  nested.nest = 240;
  d.paragraphs.push_back(nested);

  const std::string xml = Xml(d);
  EXPECT_NE(
      xml.find("<w:ind w:left=\"720\" w:right=\"360\" w:firstLine=\"240\"/>"),
      std::string::npos);
  EXPECT_NE(xml.find("<w:ind w:hanging=\"360\"/>"), std::string::npos);
  EXPECT_NE(xml.find("w:leftChars=\"200\""), std::string::npos);
  EXPECT_NE(xml.find("w:rightChars=\"100\""), std::string::npos);
  EXPECT_NE(xml.find("w:firstLineChars=\"150\""), std::string::npos);
  EXPECT_NE(xml.find("<w:ind w:left=\"240\"/>"), std::string::npos);
}

TEST(M20WriterMatrix, ParagraphLineSpacingThree) {
  model::Document d;
  model::Paragraph at_least = ParagraphWith("a");
  at_least.line = 360;
  d.paragraphs.push_back(at_least);

  model::Paragraph exact = ParagraphWith("b");
  exact.line = -360;
  d.paragraphs.push_back(exact);

  model::Paragraph automatic = ParagraphWith("c");
  automatic.line = 240;
  automatic.line_auto = true;
  d.paragraphs.push_back(automatic);

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:spacing w:line=\"360\" w:lineRule=\"atLeast\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:spacing w:line=\"360\" w:lineRule=\"exact\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:spacing w:line=\"240\" w:lineRule=\"auto\"/>"),
            std::string::npos);
}

TEST(M20WriterMatrix, ParagraphSpacingAndFlowFlags) {
  model::Document d;
  model::Paragraph p = ParagraphWith("a");
  p.before = 120;
  p.after = 240;
  p.keep_next = true;
  p.keep_next_set = true;
  p.page_break_before = true;
  p.page_break_before_set = true;
  p.widow_control = false;
  p.widow_control_set = true;
  p.outline = 2;
  p.text_alignment = 1;
  d.paragraphs.push_back(p);

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:spacing w:before=\"120\" w:after=\"240\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:keepNext/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:pageBreakBefore/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:widowControl w:val=\"0\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:textAlignment w:val=\"center\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:outlineLvl w:val=\"2\"/>"), std::string::npos);
}

TEST(M20WriterMatrix, RunBooleanDecorations) {
  model::Run run;
  run.text = "x";
  run.bold = true;
  run.bold_set = true;
  run.italic = true;
  run.italic_set = true;
  run.strike = true;
  run.strike_set = true;
  run.double_strike = true;
  run.double_strike_set = true;
  run.outline = true;
  run.outline_set = true;
  run.shadow = true;
  run.shadow_set = true;
  run.small_caps = true;
  run.small_caps_set = true;
  run.caps = false;
  run.caps_set = true;
  run.hidden = true;
  run.hidden_set = true;

  model::Document d = WithParagraphs({ParagraphWith("")});
  d.paragraphs[0].runs.clear();
  d.paragraphs[0].runs.push_back(run);
  const std::string xml = Xml(d);
  for (const char* flag :
       {"<w:b/>", "<w:i/>", "<w:strike/>", "<w:dstrike/>", "<w:outline/>",
        "<w:shadow/>", "<w:smallCaps/>", "<w:vanish/>"}) {
    EXPECT_NE(xml.find(flag), std::string::npos) << flag;
  }
  EXPECT_NE(xml.find("<w:caps w:val=\"0\"/>"), std::string::npos);
}

TEST(M20WriterMatrix, RunUnderlineKinds) {
  model::Document d;
  const int kinds[] = {1, 3, 4, 11, 0x2B};
  const char* names[] = {"single", "double", "dotted", "wave", "wavyDouble"};
  for (int i = 0; i < 5; ++i) {
    model::Paragraph p;
    model::Run r;
    r.text = "u";
    r.underline = kinds[i];
    r.underline_set = true;
    r.underline_color = 0xFF0000;
    p.runs.push_back(std::move(r));
    d.paragraphs.push_back(std::move(p));
  }
  const std::string xml = Xml(d);
  for (const char* name : names) {
    EXPECT_NE(xml.find(std::string("<w:u w:val=\"") + name +
                       "\" w:color=\"0000FF\"/>"),
              std::string::npos)
        << name;
  }
}

TEST(M20WriterMatrix, RunVertAlignThree) {
  model::Document d;
  for (int mode = 0; mode < 3; ++mode) {
    model::Paragraph p;
    model::Run r;
    r.text = "v";
    r.super_sub = mode;
    r.super_sub_set = true;
    p.runs.push_back(std::move(r));
    d.paragraphs.push_back(std::move(p));
  }
  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:vertAlign w:val=\"superscript\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:vertAlign w:val=\"subscript\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:vertAlign w:val=\"baseline\"/>"), std::string::npos);
}

TEST(M20WriterMatrix, RunHighlightAndColor) {
  model::Document d;
  model::Paragraph p;
  model::Run highlighted;
  highlighted.text = "h";
  highlighted.highlight = 7;
  p.runs.push_back(highlighted);
  model::Run indexed;
  indexed.text = "i";
  indexed.color = 2;
  p.runs.push_back(indexed);
  model::Run rgb;
  rgb.text = "r";
  // COLORREF is 0x00BBGGRR; 0x563412 prints as RGB "123456".
  rgb.color = 0x563412;
  rgb.color_rgb = true;
  p.runs.push_back(rgb);
  d.paragraphs.push_back(std::move(p));

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:highlight w:val=\"yellow\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:color w:val=\"0000FF\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:color w:val=\"123456\"/>"), std::string::npos);
}

TEST(M20WriterMatrix, RunSizeFontsAndLang) {
  model::Paragraph p;
  model::Run r;
  r.text = "f";
  r.size = 24;
  r.size_cs = 22;
  r.font = "Arial";
  r.font_east_asia = "SimSun";
  r.font_hint = 1;
  r.lang = "en-US";
  r.lang_east_asia = "zh-CN";
  r.lang_cs = "ar-SA";
  p.runs.push_back(std::move(r));
  model::Document d = WithParagraphs({std::move(p)});

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\" "
                     "w:eastAsia=\"SimSun\" w:hint=\"eastAsia\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:sz w:val=\"24\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:szCs w:val=\"22\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:lang w:val=\"en-US\" w:eastAsia=\"zh-CN\" "
                     "w:bidi=\"ar-SA\"/>"),
            std::string::npos);
}

TEST(M20WriterMatrix, RunSpacingScalePositionAndEmphasis) {
  model::Paragraph p;
  model::Run r;
  r.text = "s";
  r.spacing = 20;
  r.spacing_set = true;
  r.scale = 80;
  r.scale_set = true;
  r.position = 6;
  r.position_set = true;
  r.kern = 24;
  r.emphasis = 1;
  p.runs.push_back(std::move(r));
  model::Document d = WithParagraphs({std::move(p)});

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:spacing w:val=\"20\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:w w:val=\"80\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:position w:val=\"6\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:kern w:val=\"24\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:em w:val=\"dot\"/>"), std::string::npos);
}

TEST(M20WriterMatrix, TableHorizontalMerge) {
  model::Document d = WithTable(EmptyTable(3, 1));
  auto& cells = d.tables[0].row_properties[0].cells;
  cells[0].horizontal_merge = 2;
  cells[1].horizontal_merge = 1;
  cells[2].horizontal_merge = 0;

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:hMerge w:val=\"restart\"/>"), std::string::npos);
  EXPECT_EQ(Count(xml, "<w:hMerge w:val=\"continue\"/>"), 1u);
}

TEST(M20WriterMatrix, TableVerticalMerge) {
  model::Document d = WithTable(EmptyTable(1, 2));
  d.tables[0].row_properties[0].cells[0].vertical_merge = 2;
  d.tables[0].row_properties[1].cells[0].vertical_merge = 1;

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:vMerge w:val=\"restart\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:vMerge w:val=\"continue\"/>"), std::string::npos);
}

TEST(M20WriterMatrix, TableBidirectionalMerge) {
  model::Document d = WithTable(EmptyTable(2, 2));
  auto& rows = d.tables[0].row_properties;
  rows[0].cells[0].horizontal_merge = 2;
  rows[0].cells[0].vertical_merge = 2;
  rows[0].cells[1].horizontal_merge = 1;
  rows[0].cells[1].vertical_merge = 2;
  rows[1].cells[0].horizontal_merge = 2;
  rows[1].cells[0].vertical_merge = 1;
  rows[1].cells[1].horizontal_merge = 1;
  rows[1].cells[1].vertical_merge = 1;

  const std::string xml = Xml(d);
  EXPECT_EQ(Count(xml, "<w:hMerge w:val=\"restart\"/>"), 2u);
  EXPECT_EQ(Count(xml, "<w:hMerge w:val=\"continue\"/>"), 2u);
  EXPECT_EQ(Count(xml, "<w:vMerge w:val=\"restart\"/>"), 2u);
  EXPECT_EQ(Count(xml, "<w:vMerge w:val=\"continue\"/>"), 2u);
}

TEST(M20WriterMatrix, TableLShapeMerge) {
  model::Document d = WithTable(EmptyTable(3, 3));
  auto& rows = d.tables[0].row_properties;
  rows[0].cells[0].horizontal_merge = 2;
  rows[0].cells[1].horizontal_merge = 1;
  rows[1].cells[0].vertical_merge = 2;
  rows[2].cells[0].vertical_merge = 1;

  const std::string xml = Xml(d);
  EXPECT_EQ(Count(xml, "<w:hMerge w:val=\"restart\"/>"), 1u);
  EXPECT_EQ(Count(xml, "<w:hMerge w:val=\"continue\"/>"), 1u);
  EXPECT_EQ(Count(xml, "<w:vMerge w:val=\"restart\"/>"), 1u);
  EXPECT_EQ(Count(xml, "<w:vMerge w:val=\"continue\"/>"), 1u);
}

TEST(M20WriterMatrix, TableBorderSixSidesThreeTypes) {
  // Cross product: every side x {single, double, dotted}.
  const int types[] = {1, 3, 6};
  const char* values[] = {"single", "double", "dotted"};
  const char* names[] = {"w:top",   "w:left",    "w:bottom",
                         "w:right", "w:insideH", "w:insideV"};
  model::Document d;
  for (int t = 0; t < 3; ++t) {
    model::Table table = EmptyTable(1, 1);
    for (int side = 0; side < 6; ++side) {
      model::Border b;
      b.set = true;
      b.type = types[t];
      b.width = 8 + t;
      b.color = 0;
      table.borders[side] = b;
    }
    d.tables.push_back(std::move(table));
    d.blocks.push_back({true, static_cast<size_t>(t)});
  }

  const std::string xml = Xml(d);
  for (int t = 0; t < 3; ++t) {
    for (int side = 0; side < 6; ++side) {
      const std::string fragment = std::string("<") + names[side] +
                                   " w:val=\"" + values[t] + "\" w:sz=\"" +
                                   std::to_string(8 + t) +
                                   "\" w:color=\"000000\" w:space=\"0\"/>";
      EXPECT_NE(xml.find(fragment), std::string::npos)
          << names[side] << "/" << values[t];
    }
  }
}

TEST(M20WriterMatrix, TableNestedTwoLevels) {
  model::Document d;
  d.tables.push_back(EmptyTable(1, 1));
  d.tables.push_back(EmptyTable(1, 1));
  d.blocks.push_back({true, 0});
  d.paragraphs.push_back(ParagraphWith("Outer"));
  d.paragraphs.push_back(ParagraphWith("Inner"));
  d.tables[0].rows[0][0].blocks = {{false, 0}, {true, 1}};
  d.tables[1].rows[0][0].blocks = {{false, 1}};

  const std::string xml = Xml(d);
  EXPECT_EQ(Count(xml, "<w:tbl>"), 2u);
  const size_t outer_cell = xml.find("<w:tc>");
  ASSERT_NE(outer_cell, std::string::npos);
  EXPECT_NE(xml.find("<w:tbl>", outer_cell), std::string::npos);
  EXPECT_NE(xml.find(">Inner</w:t>"), std::string::npos);
  EXPECT_EQ(xml.find(">Inner</w:t>"), xml.rfind(">Inner</w:t>"));
}

TEST(M20WriterMatrix, TableCellShadingMarginsAndAlign) {
  model::Document d = WithTable(EmptyTable(2, 1));
  auto& cell = d.tables[0].row_properties[0].cells[0];
  cell.shade = 0x00FF00;
  cell.shade_fore = 0x000000;
  cell.shade_pattern = 1;
  cell.margins = {{10, 20, 30, 40}};
  cell.vertical_align = 1;
  cell.no_wrap = true;
  cell.fit_text = true;
  cell.text_direction = 3;

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:shd w:val=\"solid\" w:fill=\"00FF00\" "
                     "w:color=\"000000\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:noWrap/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:textDirection w:val=\"btLr\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:tcFitText/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:vAlign w:val=\"center\"/>"), std::string::npos);
  EXPECT_NE(xml.find("<w:tcMar><w:top w:w=\"10\" w:type=\"dxa\"/>"
                     "<w:left w:w=\"21\" w:type=\"dxa\"/>"),
            std::string::npos);
}

TEST(M20WriterMatrix, SectionBreakTypes) {
  model::Document d = WithParagraphs(
      {ParagraphWith("one"), ParagraphWith("two"), ParagraphWith("three")});
  const int types[] = {0, 2, 4};
  const char* names[] = {"continuous", "nextPage", "oddPage"};
  for (int i = 0; i < 3; ++i) {
    model::Section section;
    section.break_specified = true;
    section.break_type = types[i];
    d.section_breaks.emplace_back(static_cast<size_t>(i), section);
  }
  const std::string xml = Xml(d);
  for (const char* name : names) {
    EXPECT_NE(xml.find(std::string("<w:type w:val=\"") + name + "\"/>"),
              std::string::npos)
        << name;
  }
  EXPECT_EQ(Count(xml, "<w:type w:val="), 3u);
}

TEST(M20WriterMatrix, SectionIndependentHeadersFooters) {
  model::Document d =
      WithParagraphs({ParagraphWith("one"), ParagraphWith("two")});
  model::Section first;
  first.headers[1] = {ParagraphWith("HeaderOne")};
  first.headers[3] = {ParagraphWith("FooterOne")};
  model::Section second;
  second.headers[1] = {ParagraphWith("HeaderTwo")};
  second.headers[3] = {ParagraphWith("FooterTwo")};
  d.section_breaks = {{0, first}, {1, second}};

  const std::string xml = Xml(d);
  EXPECT_EQ(Count(xml, "<w:headerReference "), 2u);
  EXPECT_EQ(Count(xml, "<w:footerReference "), 2u);
  EXPECT_EQ(Count(xml, "w:type=\"default\""), 4u);
  EXPECT_NE(Part(d, "word/header1.xml").find("HeaderOne"), std::string::npos);
  EXPECT_NE(Part(d, "word/header3.xml").find("HeaderTwo"), std::string::npos);
  EXPECT_NE(Part(d, "word/footer2.xml").find("FooterOne"), std::string::npos);
  EXPECT_NE(Part(d, "word/footer4.xml").find("FooterTwo"), std::string::npos);
}

TEST(M20WriterMatrix, SectionOddEvenAndFirstPageHeaders) {
  model::Document d = WithParagraphs({ParagraphWith("body")});
  model::Section section;
  section.break_specified = true;
  section.break_type = 2;
  section.headers[0] = {ParagraphWith("EvenHeader")};
  section.headers[1] = {ParagraphWith("DefaultHeader")};
  section.headers[4] = {ParagraphWith("FirstHeader")};
  section.odd_even_different = true;
  section.first_page_different = true;
  d.section_breaks = {{0, section}};

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("w:type=\"even\""), std::string::npos);
  EXPECT_NE(xml.find("w:type=\"default\""), std::string::npos);
  EXPECT_NE(xml.find("w:type=\"first\""), std::string::npos);
  EXPECT_NE(xml.find("<w:titlePg/>"), std::string::npos);
  EXPECT_NE(Part(d, "word/settings.xml").find("<w:evenAndOddHeaders/>"),
            std::string::npos);
  EXPECT_NE(Part(d, "word/header1.xml").find("EvenHeader"), std::string::npos);
  EXPECT_NE(Part(d, "word/header3.xml").find("FirstHeader"), std::string::npos);
}

TEST(M20WriterMatrix, SectionFootnoteProperties) {
  model::Document d = WithParagraphs({ParagraphWith("body")});
  d.section.footnote_position = 2;
  d.section.footnote_format = 1;
  d.section.footnote_start = 3;
  d.section.footnote_restart = 1;

  const std::string xml = Xml(d);
  EXPECT_NE(xml.find("<w:footnotePr><w:pos w:val=\"beneathText\"/>"
                     "<w:numFmt w:val=\"upperRoman\"/>"
                     "<w:numStart w:val=\"3\"/>"
                     "<w:numRestart w:val=\"eachSect\"/></w:footnotePr>"),
            std::string::npos);
}

TEST(M20WriterMatrix, SectionPageSetupAndColumns) {
  model::Document d = WithParagraphs({ParagraphWith("body")});
  d.section.width = 15840;
  d.section.height = 12240;
  d.section.orientation = 1;
  d.section.left = 1000;
  d.section.right = 1100;
  d.section.top = 1200;
  d.section.bottom = 1300;
  d.section.gutter = 50;
  d.section.columns = 2;
  d.section.column_space = 720;
  d.section.columns_separator = true;
  d.section.vertical_justify = 1;

  const std::string xml = Xml(d);
  EXPECT_NE(
      xml.find("<w:pgSz w:w=\"15840\" w:h=\"12240\" w:orient=\"landscape\"/>"),
      std::string::npos);
  EXPECT_NE(xml.find("<w:pgMar w:left=\"1000\" w:right=\"1100\" w:top=\"1200\" "
                     "w:bottom=\"1300\" w:gutter=\"50\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:cols w:num=\"2\" w:space=\"720\" w:sep=\"1\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:vAlign w:val=\"center\"/>"), std::string::npos);
}

}  // namespace
