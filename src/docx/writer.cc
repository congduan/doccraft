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

#include "docx/writer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "docx/writer_run.h"
#include "docx/xml_util.h"
#include "third_party/miniz/miniz.h"
#include "third_party/pugixml/pugixml.hpp"

namespace doccraft::docx {
namespace {
using xml::Attr;
using xml::BorderPr;
using xml::Color;
using xml::Id;
using xml::IntAttr;
using xml::kLow16Mask;
using xml::One;
using xml::ShadePr;
using xml::SpecBool;
using xml::Value;
constexpr char kWordmlNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

// Control characters Word stores inline in run text ([MS-DOC] §2.4.1): cell and
// row marks, manual line break, non-breaking and optional hyphens, tab and the
// lowest printable ASCII code point.
constexpr char kCellMark = 0x07;
constexpr char kLineBreak = 0x0B;
constexpr char kTab = 0x09;
constexpr char kNoBreakHyphen = 0x1E;
constexpr char kOptionalHyphen = 0x1F;
constexpr char kFirstPrintable = 32;

// LVLF.nfc values that do not describe a numeric format.
constexpr int kNumberFormatBullet = 0x17;
constexpr int kNumberFormatNone = 0xFF;

constexpr char kRelationshipsNamespace[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

// [Content_Types].xml: fixed head, then one Override per header/footer part.
constexpr char kContentTypesPrefix[] =
    "<?xml version=\"1.0\"?><Types "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/"
    "vnd.openxmlformats-package.relationships+xml\"/><Default "
    "Extension=\"xml\" ContentType=\"application/xml\"/><Override "
    "PartName=\"/word/document.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/styles.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
    "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>";

constexpr char kSettingsContentType[] =
    "<Override PartName=\"/word/settings.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>";

constexpr char kRootRelationships[] =
    "<?xml version=\"1.0\"?><Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/"
    "officeDocument/2006/relationships/officeDocument\" "
    "Target=\"word/document.xml\"/></Relationships>";

constexpr char kSettingsPart[] =
    "<?xml version=\"1.0\"?><w:settings "
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:evenAndOddHeaders/></w:settings>";

// One part of the resulting OPC package (path inside the ZIP plus payload).
struct Part {
  std::string name;
  std::string data;
};

// State shared by the whole writer pass: hyperlink targets, revision ids and
// the header/footer parts discovered while walking the sections. rId1/rId2 are
// reserved for the styles and numbering parts.
struct WriteContext {
  std::map<std::string, int> hyperlinks;
  int revision_id = 0;
  int next_rid = 3;
  int next_part = 1;
  bool odd_even = false;
  std::vector<std::pair<int, std::string>> relations;  // rId -> part name
  std::vector<Part> parts;
};

std::string Save(const pugi::xml_document& d);

void WriteParagraph(pugi::xml_node parent, const model::Paragraph& p,
                    const model::Section* break_section, WriteContext& ctx);

// Emits footnotes.xml / endnotes.xml when the document has notes, and
// registers the parts and relationships.
void WriteNoteParts(const model::Document& document, WriteContext& ctx);

// Returns false when `section` has no header/footer story to reference.
bool WriteSectionReferences(pugi::xml_node sec, const model::Section& section,
                            WriteContext& ctx);

void WriteFramePr(pugi::xml_node pr, const model::Paragraph& p) {
  if (p.frame_wrap < 0 && p.frame_anchor < 0 && p.frame_x < 0 &&
      p.frame_y < 0 && p.frame_width < 0 && p.frame_height < 0 &&
      p.frame_h_space < 0 && p.frame_v_space < 0 && !p.frame_lock_set &&
      p.drop_cap < 0 && p.text_flow < 0) {
    return;
  }
  auto frame = pr.append_child("w:framePr");
  if (p.drop_cap > 0) {
    Attr(frame, "w:dropCap", p.drop_cap == 2 ? "margin" : "drop");
    IntAttr(frame, "w:lines", p.drop_cap_lines);
  }
  if (p.text_flow > 0) {
    Attr(frame, "w:textFlow", p.text_flow == 2 ? "btLr" : "tbRl");
  }
  if (p.frame_wrap >= 0) {
    const char* wraps[] = {"auto", "notBeside", "around",
                           "none", "tight",     "through"};
    Attr(frame, "w:wrap", wraps[p.frame_wrap]);
  }
  if (p.frame_anchor >= 0) {
    int vertical = (p.frame_anchor >> 4) & 3;
    int horizontal = (p.frame_anchor >> 6) & 3;
    if (horizontal < 3) {
      Attr(frame, "w:hAnchor",
           horizontal == 0   ? "text"
           : horizontal == 1 ? "margin"
                             : "page");
    }
    if (vertical < 3) {
      Attr(frame, "w:vAnchor",
           vertical == 0   ? "margin"
           : vertical == 1 ? "page"
                           : "text");
    }
  }
  if (p.frame_x != -1) {
    IntAttr(frame, "w:x", p.frame_x);
  }
  if (p.frame_y != -1) {
    IntAttr(frame, "w:y", p.frame_y);
  }
  if (p.frame_width > 0) {
    IntAttr(frame, "w:w", p.frame_width);
  }
  if (p.frame_height > 0) {
    IntAttr(frame, "w:h", p.frame_height);
    Attr(frame, "w:hRule", p.frame_height_exact ? "exact" : "atLeast");
  }
  if (p.frame_h_space >= 0) {
    IntAttr(frame, "w:hSpace", p.frame_h_space);
  }
  if (p.frame_v_space >= 0) {
    IntAttr(frame, "w:vSpace", p.frame_v_space);
  }
  if (p.frame_lock_set) {
    Attr(frame, "w:lock", p.frame_lock ? "1" : "0");
  }
}

void WriteParagraphIndent(pugi::xml_node pr, const model::Paragraph& p) {
  if (!(p.left_set || p.right_set || p.first_set || p.nest_set ||
        p.left_chars_set || p.right_chars_set || p.first_line_chars_set)) {
    return;
  }
  auto ind = pr.append_child("w:ind");
  if (p.left_set || p.nest_set) {
    IntAttr(ind, "w:left", (p.left_set ? p.left : 0) + p.nest);
  }
  if (p.right_set) {
    IntAttr(ind, "w:right", p.right);
  }
  if (p.first_set && p.first < 0) {
    IntAttr(ind, "w:hanging", -p.first);
  } else if (p.first_set) {
    IntAttr(ind, "w:firstLine", p.first);
  }
  if (p.left_chars_set) {
    IntAttr(ind, "w:leftChars", p.left_chars);
  }
  if (p.right_chars_set) {
    IntAttr(ind, "w:rightChars", p.right_chars);
  }
  if (p.first_line_chars_set) {
    IntAttr(ind, "w:firstLineChars", p.first_line_chars);
  }
}

void WriteParagraphSpacing(pugi::xml_node pr, const model::Paragraph& p) {
  if (p.before < 0 && p.after < 0 && !p.line && !p.before_auto_set &&
      !p.after_auto_set && !p.before_lines_set && !p.after_lines_set) {
    return;
  }
  auto sp = pr.append_child("w:spacing");
  if (p.before_lines_set) {
    IntAttr(sp, "w:beforeLines", p.before_lines);
  }
  if (p.after_lines_set) {
    IntAttr(sp, "w:afterLines", p.after_lines);
  }
  if (p.before >= 0) {
    IntAttr(sp, "w:before", p.before);
  }
  if (p.after >= 0) {
    IntAttr(sp, "w:after", p.after);
  }
  if (p.before_auto_set) {
    Attr(sp, "w:beforeAutospacing", p.before_auto ? "1" : "0");
  }
  if (p.after_auto_set) {
    Attr(sp, "w:afterAutospacing", p.after_auto ? "1" : "0");
  }
  if (p.line) {
    if (p.line_auto) {
      IntAttr(sp, "w:line", p.line);
      Attr(sp, "w:lineRule", "auto");
    } else if (p.line < 0) {
      IntAttr(sp, "w:line", -p.line);
      Attr(sp, "w:lineRule", "exact");
    } else {
      IntAttr(sp, "w:line", p.line);
      Attr(sp, "w:lineRule", "atLeast");
    }
  }
}

void WriteParagraphFlags(pugi::xml_node pr, const model::Paragraph& p) {
  SpecBool(pr, "w:keepNext", p.keep_next, p.keep_next_set);
  SpecBool(pr, "w:keepLines", p.keep_lines, p.keep_lines_set);
  SpecBool(pr, "w:pageBreakBefore", p.page_break_before,
           p.page_break_before_set);
  SpecBool(pr, "w:widowControl", p.widow_control, p.widow_control_set);
  SpecBool(pr, "w:bidi", p.bidi, p.bidi_set);
  SpecBool(pr, "w:suppressAutoHyphens", p.suppress_hyphenation,
           p.suppress_hyphenation_set);
  SpecBool(pr, "w:contextualSpacing", p.contextual_spacing,
           p.contextual_spacing_set);
  SpecBool(pr, "w:mirrorIndents", p.mirror_indents, p.mirror_indents_set);
  SpecBool(pr, "w:snapToGrid", p.snap_to_grid, p.snap_to_grid_set);
  SpecBool(pr, "w:adjustRightInd", p.adjust_right, p.adjust_right_set);
  SpecBool(pr, "w:suppressLineNumbers", p.no_line_number, p.no_line_number_set);
  SpecBool(pr, "w:wordWrap", p.word_wrap, p.word_wrap_set);
  SpecBool(pr, "w:kinsoku", p.kinsoku, p.kinsoku_set);
  SpecBool(pr, "w:overflowPunct", p.overflow_punct, p.overflow_punct_set);
  SpecBool(pr, "w:topLinePunct", p.top_line_punct, p.top_line_punct_set);
  SpecBool(pr, "w:autoSpaceDE", p.auto_space_de, p.auto_space_de_set);
  SpecBool(pr, "w:autoSpaceDN", p.auto_space_dn, p.auto_space_dn_set);
  if (p.outline >= 0 && p.outline <= 9) {
    Value(pr, "w:outlineLvl", p.outline);
  }
  if (p.text_alignment >= 0) {
    const char* align[] = {"top", "center", "baseline", "bottom", "auto"};
    Attr(pr.append_child("w:textAlignment"), "w:val",
         p.text_alignment < 5 ? align[p.text_alignment] : "auto");
  }
}

void WriteParagraphTabs(pugi::xml_node pr, const model::Paragraph& p) {
  if (p.tabs.empty()) {
    return;
  }
  auto tabs = pr.append_child("w:tabs");
  const char* aligns[] = {"left", "center", "right", "decimal", "bar", "num"};
  const char* leaders[] = {"none",       "dot",   "hyphen",
                           "underscore", "heavy", "middleDot"};
  for (const auto& tab : p.tabs) {
    auto stop = tabs.append_child("w:tab");
    Attr(stop, "w:val", tab.align < 6 ? aligns[tab.align] : "left");
    IntAttr(stop, "w:pos", tab.position);
    if (tab.leader) {
      Attr(stop, "w:leader", tab.leader < 6 ? leaders[tab.leader] : "none");
    }
  }
}

void WriteParagraphBorders(pugi::xml_node pr, const model::Paragraph& p) {
  bool has_border = false;
  for (const auto& b : p.borders) {
    has_border |= b.set;
  }
  for (const auto& b : p.extra_borders) {
    has_border |= b.set;
  }
  if (!has_border) {
    return;
  }
  auto bd = pr.append_child("w:pBdr");
  const char* sides[] = {"w:top", "w:left", "w:bottom", "w:right"};
  for (size_t i = 0; i < 4; i++) {
    BorderPr(bd, sides[i], p.borders[i]);
  }
  BorderPr(bd, "w:between", p.extra_borders[0]);
  BorderPr(bd, "w:bar", p.extra_borders[1]);
}

void ParaPr(pugi::xml_node parent, const model::Paragraph& p,
            bool style_props = false) {
  auto pr = parent.append_child("w:pPr");
  if (!style_props && p.style >= 0) {
    Attr(pr.append_child("w:pStyle"), "w:val", Id(p.style));
  }
  WriteFramePr(pr, p);
  if (p.align >= 0) {
    const char* align[] = {"left", "center", "right", "both", "distribute"};
    Attr(pr.append_child("w:jc"), "w:val",
         p.align < 5 ? align[p.align] : "left");
  }
  WriteParagraphIndent(pr, p);
  WriteParagraphSpacing(pr, p);
  WriteParagraphFlags(pr, p);
  WriteParagraphTabs(pr, p);
  WriteParagraphBorders(pr, p);
  ShadePr(pr, p.shade, p.shade_fore, p.shade_pattern);
  if (p.list_id > 0) {
    auto num = pr.append_child("w:numPr");
    Value(num, "w:ilvl", std::clamp(p.list_level + p.list_level_inc, 0, 8));
    Value(num, "w:numId", p.list_id);
  }
}

// `deleted` selects w:delText, which is required inside a w:del wrapper.
void Text(pugi::xml_node run, std::string_view text, bool deleted = false) {
  std::string chunk;
  auto flush = [&]() {
    if (chunk.empty())
      return;
    auto t = run.append_child(deleted ? "w:delText" : "w:t");
    Attr(t, "xml:space", "preserve");
    t.text().set(chunk.c_str());
    chunk.clear();
  };
  for (unsigned char c : text) {
    if (c == kCellMark || c == kLineBreak) {
      flush();
      run.append_child("w:br");
    } else if (c == kTab) {
      flush();
      run.append_child("w:tab");
    } else if (c == kNoBreakHyphen) {
      flush();
      run.append_child("w:noBreakHyphen");
    } else if (c == kOptionalHyphen) {
      flush();
      run.append_child("w:softHyphen");
    } else if (c >= kFirstPrintable) {
      chunk += static_cast<char>(c);
    }
  }
  flush();
}

void WritePageSetup(pugi::xml_node sec, const model::Section& section) {
  auto size = sec.append_child("w:pgSz");
  IntAttr(size, "w:w", section.width);
  IntAttr(size, "w:h", section.height);
  if (section.orientation || section.width > section.height) {
    Attr(size, "w:orient", "landscape");
  }
  auto mar = sec.append_child("w:pgMar");
  IntAttr(mar, "w:left", section.left);
  IntAttr(mar, "w:right", section.right);
  IntAttr(mar, "w:top", section.top);
  IntAttr(mar, "w:bottom", section.bottom);
  if (section.gutter) {
    IntAttr(mar, "w:gutter", section.gutter);
  }
  if (section.header_distance >= 0) {
    IntAttr(mar, "w:header", section.header_distance);
  }
  if (section.footer_distance >= 0) {
    IntAttr(mar, "w:footer", section.footer_distance);
  }
  if (section.paper_first >= 0 || section.paper_other >= 0) {
    auto src = sec.append_child("w:paperSrc");
    if (section.paper_first >= 0) {
      IntAttr(src, "w:first", section.paper_first);
    }
    if (section.paper_other >= 0) {
      IntAttr(src, "w:other", section.paper_other);
    }
  }
}

void WriteColumns(pugi::xml_node sec, const model::Section& section) {
  if (section.columns <= 1 && section.column_space < 0 &&
      section.column_specs.empty()) {
    return;
  }
  bool custom_columns = false;
  for (const auto& spec : section.column_specs) {
    custom_columns |= spec.width >= 0;
  }
  auto cols = sec.append_child("w:cols");
  IntAttr(cols, "w:num", section.columns);
  if (section.column_space >= 0) {
    IntAttr(cols, "w:space", section.column_space);
  }
  if (!section.columns_even || custom_columns) {
    Attr(cols, "w:equalWidth", "0");
  }
  if (section.columns_separator) {
    Attr(cols, "w:sep", "1");
  }
  for (int index = 0; index < section.columns; index++) {
    const model::ColumnSpec* spec = nullptr;
    for (const auto& entry : section.column_specs) {
      if (entry.index == index) {
        spec = &entry;
      }
    }
    if (!spec || spec->width < 0) {
      continue;
    }
    auto col = cols.append_child("w:col");
    IntAttr(col, "w:w", spec->width);
    if (spec->space >= 0) {
      IntAttr(col, "w:space", spec->space);
    }
  }
}

// w:pgNumType carries the page format and, when configured, the chapter
// numbering that restarts it.
void WritePageNumbers(pugi::xml_node sec, const model::Section& section) {
  if (!section.page_restart && section.page_format < 0 &&
      section.chapter_heading < 0 && section.chapter_separator < 0) {
    return;
  }
  auto pages = sec.append_child("w:pgNumType");
  if (section.page_restart) {
    IntAttr(pages, "w:start", section.page_start >= 0 ? section.page_start : 0);
  }
  if (section.page_format >= 0) {
    const char* formats[] = {"decimal", "upperRoman", "lowerRoman",
                             "upperLetter", "lowerLetter"};
    Attr(pages, "w:fmt",
         section.page_format < 5 ? formats[section.page_format] : "decimal");
  }
  if (section.chapter_heading >= 0) {
    IntAttr(pages, "w:chapStyle", section.chapter_heading);
  }
  if (section.chapter_separator >= 0) {
    const char* separators[] = {"hyphen", "period", "colon", "emDash",
                                "enDash"};
    Attr(pages, "w:chapSep",
         section.chapter_separator < 5 ? separators[section.chapter_separator]
                                       : "hyphen");
  }
}

void WriteDocGrid(pugi::xml_node sec, const model::Section& section) {
  if (section.grid_mode <= 0 && section.grid_line_pitch <= 0) {
    return;
  }
  auto grid = sec.append_child("w:docGrid");
  if (section.grid_mode > 0) {
    Attr(grid, "w:type",
         section.grid_mode == 2   ? "lines"
         : section.grid_mode == 3 ? "snapToChars"
                                  : "linesAndChars");
  }
  if (section.grid_line_pitch > 0) {
    IntAttr(grid, "w:linePitch", section.grid_line_pitch);
  }
  if (section.grid_char_space >= 0) {
    IntAttr(grid, "w:charSpace", section.grid_char_space);
  }
}

void WritePageBorders(pugi::xml_node sec, const model::Section& section) {
  bool has_border = false;
  for (const auto& b : section.page_borders) {
    has_border |= b.set;
  }
  if (!has_border) {
    return;
  }
  auto borders = sec.append_child("w:pgBorders");
  if (section.page_border_offset_from >= 0) {
    Attr(borders, "w:offsetFrom",
         section.page_border_offset_from == 1 ? "text" : "page");
  }
  if (section.page_border_apply_to >= 0) {
    const char* displays[] = {"allPages", "firstPage", "notFirstPage"};
    Attr(borders, "w:display", displays[section.page_border_apply_to]);
  }
  const char* sides[] = {"w:top", "w:left", "w:bottom", "w:right"};
  for (size_t i = 0; i < 4; i++) {
    BorderPr(borders, sides[i], section.page_borders[i]);
  }
}

void WriteLineNumbering(pugi::xml_node sec, const model::Section& section) {
  if (section.line_count_by <= 0) {
    return;
  }
  auto line = sec.append_child("w:lnNumType");
  IntAttr(line, "w:countBy", section.line_count_by);
  if (section.line_start) {
    IntAttr(line, "w:start", section.line_start + 1);
  }
  if (section.line_distance >= 0) {
    IntAttr(line, "w:distance", section.line_distance);
  }
  const char* restart[] = {"newPage", "newSection", "continuous"};
  Attr(line, "w:restart",
       section.line_restart < 3 ? restart[section.line_restart] : "newPage");
}

void WriteFootnoteProperties(pugi::xml_node sec,
                             const model::Section& section) {
  if (section.footnote_position < 0 && section.footnote_restart < 0 &&
      section.footnote_start < 0 && section.footnote_format < 0) {
    return;
  }
  auto note = sec.append_child("w:footnotePr");
  if (section.footnote_position >= 0) {
    Attr(note.append_child("w:pos"), "w:val",
         section.footnote_position == 2 ? "beneathText" : "pageBottom");
  }
  if (section.footnote_restart >= 0) {
    const char* restart[] = {"continuous", "eachSect", "eachPage"};
    Attr(note.append_child("w:numRestart"), "w:val",
         section.footnote_restart < 3 ? restart[section.footnote_restart]
                                      : "continuous");
  }
  if (section.footnote_start > 0) {
    Value(note, "w:numStart", section.footnote_start);
  }
  if (section.footnote_format >= 0) {
    const char* format[] = {"decimal", "upperRoman", "lowerRoman",
                            "upperLetter", "lowerLetter"};
    Attr(note.append_child("w:numFmt"), "w:val",
         section.footnote_format < 5 ? format[section.footnote_format]
                                     : "decimal");
  }
}

void WriteEndnoteProperties(pugi::xml_node sec, const model::Section& section) {
  if (section.endnote_restart < 0 && section.endnote_start < 0 &&
      section.endnote_format < 0 && !section.endnote_at_section_end) {
    return;
  }
  auto note = sec.append_child("w:endnotePr");
  if (section.endnote_at_section_end) {
    Attr(note.append_child("w:pos"), "w:val", "sectEnd");
  }
  if (section.endnote_restart >= 0) {
    const char* restart[] = {"continuous", "eachSect", "eachPage"};
    Attr(note.append_child("w:numRestart"), "w:val",
         section.endnote_restart < 3 ? restart[section.endnote_restart]
                                     : "continuous");
  }
  if (section.endnote_start > 0) {
    Value(note, "w:numStart", section.endnote_start);
  }
  if (section.endnote_format >= 0) {
    const char* format[] = {"decimal", "upperRoman", "lowerRoman",
                            "upperLetter", "lowerLetter"};
    Attr(note.append_child("w:numFmt"), "w:val",
         section.endnote_format < 5 ? format[section.endnote_format]
                                    : "decimal");
  }
}

void SectionPr(pugi::xml_node sec, const model::Section& section,
               WriteContext* ctx) {
  if (section.break_specified) {
    const char* types[] = {"continuous", "nextColumn", "nextPage", "evenPage",
                           "oddPage"};
    Attr(sec.append_child("w:type"), "w:val",
         section.break_type < 5 ? types[section.break_type] : "nextPage");
  }
  WritePageSetup(sec, section);
  WriteColumns(sec, section);
  if (section.vertical_justify >= 0) {
    const char* align[] = {"top", "center", "both", "bottom"};
    Attr(sec.append_child("w:vAlign"), "w:val",
         align[section.vertical_justify]);
  }
  if (section.bidi) {
    One(sec, "w:bidi");
  }
  if (section.rtl_gutter) {
    One(sec, "w:rtlGutter");
  }
  WritePageNumbers(sec, section);
  WriteDocGrid(sec, section);
  WritePageBorders(sec, section);
  if (section.text_flow >= 0) {
    const char* directions[] = {"lrTb", "tbRl", "btLr", "tbRl", "lrTb", "tbRl"};
    Attr(sec.append_child("w:textDirection"), "w:val",
         section.text_flow < 6 ? directions[section.text_flow] : "lrTb");
  }
  if (section.form_unprotected_set) {
    // sprmSFProtected states whether the section opts out of form protection.
    SpecBool(sec, "w:formProt", !section.form_unprotected, true);
  }
  WriteLineNumbering(sec, section);
  WriteFootnoteProperties(sec, section);
  WriteEndnoteProperties(sec, section);
  if (ctx != nullptr) {
    WriteSectionReferences(sec, section, *ctx);
  }
}

std::string Save(const pugi::xml_document& d) {
  std::ostringstream out;
  d.save(out, "", pugi::format_raw, pugi::encoding_utf8);
  return out.str();
}

// Emits the w:headerReference / w:footerReference of one section and registers
// its part plus relationship, so every section carries its own stories.
bool WriteSectionReferences(pugi::xml_node sec, const model::Section& section,
                            WriteContext& ctx) {
  bool any = false;
  for (size_t i = 0; i < 6; i++) {
    if (section.headers[i].empty()) {
      continue;
    }
    any = true;
    const bool is_header = i == 0 || i == 1 || i == 4;
    const int rid = ctx.next_rid++;
    const std::string name = (is_header ? "header" : "footer") +
                             std::to_string(ctx.next_part++) + ".xml";
    auto ref =
        sec.append_child(is_header ? "w:headerReference" : "w:footerReference");
    Attr(ref, "w:type",
         i == 0 || i == 2   ? "even"
         : i == 1 || i == 3 ? "default"
                            : "first");
    Attr(ref, "r:id", "rId" + std::to_string(rid));
    ctx.relations.emplace_back(rid, name);
    pugi::xml_document part;
    auto root = part.append_child(is_header ? "w:hdr" : "w:ftr");
    Attr(root, "xmlns:w", kWordmlNamespace);
    Attr(root, "xmlns:r", kRelationshipsNamespace);
    for (const auto& paragraph : section.headers[i]) {
      WriteParagraph(root, paragraph, nullptr, ctx);
    }
    ctx.parts.push_back({name, Save(part)});
  }
  if (section.first_page_different) {
    One(sec, "w:titlePg");
  }
  if (section.odd_even_different) {
    ctx.odd_even = true;
  }
  return any;
}

void WriteNoteParts(const model::Document& document, WriteContext& ctx) {
  bool has_footnotes = false, has_endnotes = false;
  for (const auto& note : document.notes) {
    (note.endnote ? has_endnotes : has_footnotes) = true;
  }
  if (!has_footnotes && !has_endnotes) {
    return;
  }
  auto write_class = [&](bool endnote) {
    const char* part = endnote ? "endnotes.xml" : "footnotes.xml";
    const char* root_name = endnote ? "w:endnotes" : "w:footnotes";
    const char* note_name = endnote ? "w:endnote" : "w:footnote";
    pugi::xml_document doc;
    auto root = doc.append_child(root_name);
    Attr(root, "xmlns:w", kWordmlNamespace);
    Attr(root, "xmlns:r", kRelationshipsNamespace);
    // Word requires the two separator notes; they carry negative ids.
    struct Separator {
      const char* name;
      int id;
    };
    const Separator separators[] = {{"w:separator", -1},
                                    {"w:continuationSeparator", 0}};
    for (const auto& separator : separators) {
      auto note = root.append_child(note_name);
      Attr(note, "w:type", std::string(separator.name + 2));
      IntAttr(note, "w:id", separator.id);
      auto run = note.append_child("w:p").append_child("w:r");
      run.append_child(separator.name);
    }
    int id = 0;
    for (const auto& note : document.notes) {
      if (note.endnote != endnote) {
        continue;
      }
      id++;
      auto node = root.append_child(note_name);
      IntAttr(node, "w:id", id);
      if (note.paragraphs.empty()) {
        node.append_child("w:p");
      }
      for (const auto& paragraph : note.paragraphs) {
        WriteParagraph(node, paragraph, nullptr, ctx);
      }
    }
    const int rid = ctx.next_rid++;
    ctx.relations.emplace_back(rid, part);
    ctx.parts.push_back({part, Save(doc)});
  };
  if (has_footnotes) {
    write_class(false);
  }
  if (has_endnotes) {
    write_class(true);
  }
}

void WriteParagraph(pugi::xml_node parent, const model::Paragraph& p,
                    const model::Section* break_section, WriteContext& ctx) {
  auto pn = parent.append_child("w:p");
  ParaPr(pn, p);
  if (break_section) {
    SectionPr(pn.child("w:pPr").append_child("w:sectPr"), *break_section, &ctx);
  }
  for (const auto& r : p.runs) {
    pugi::xml_node owner = pn;
    if (!r.hyperlink.empty()) {
      auto [it, inserted] =
          ctx.hyperlinks.try_emplace(r.hyperlink, ctx.next_rid++);
      (void)inserted;
      owner = pn.append_child("w:hyperlink");
      Attr(owner, "r:id", "rId" + std::to_string(it->second));
    } else if (r.page_field) {
      owner = pn.append_child("w:fldSimple");
      Attr(owner, "w:instr", "PAGE");
    }
    // sprmCFRMarkDel / sprmCFRMarkIns: tracked text lives inside w:del / w:ins.
    pugi::xml_node wrapper = owner;
    bool deleted = r.revision_delete;
    if (deleted || r.revision_insert) {
      wrapper = owner.append_child(deleted ? "w:del" : "w:ins");
      IntAttr(wrapper, "w:id", ++ctx.revision_id);
      Attr(wrapper, "w:author", "doccraft");
    }
    auto rn = wrapper.append_child("w:r");
    RunPr(rn, r);
    if (r.note_ref >= 0) {
      // A note reference is an empty run carrying the reference element.
      IntAttr(rn.append_child(r.note_endnote ? "w:endnoteReference"
                                             : "w:footnoteReference"),
              "w:id", r.note_ref);
    } else if (r.field_char >= 0) {
      const char* kinds[] = {"begin", "separate", "end"};
      Attr(rn.append_child("w:fldChar"), "w:fldCharType",
           r.field_char < 3 ? kinds[r.field_char] : "begin");
    } else if (r.break_type > 0) {
      auto br = rn.append_child("w:br");
      Attr(br, "w:type", r.break_type == 2 ? "column" : "page");
    } else if (!r.instr_text.empty()) {
      auto instr = rn.append_child("w:instrText");
      Attr(instr, "xml:space", "preserve");
      instr.text().set(r.instr_text.c_str());
    } else if (r.symbol_character >= 0 && !r.symbol_font.empty()) {
      auto sym = rn.append_child("w:sym");
      Attr(sym, "w:font", r.symbol_font);
      char code[5];
      std::snprintf(code, sizeof(code), "%04X",
                    r.symbol_character & kLow16Mask);
      Attr(sym, "w:char", code);
    } else {
      Text(rn, r.text, deleted);
    }
  }
}

// w:tblpPr -- only emitted when the table carries floating position data.
void WriteTablePosition(pugi::xml_node pr, const model::Table& t) {
  bool positioned = t.tblp_x >= 0 || t.tblp_y >= 0;
  for (int space : t.from_text) {
    positioned |= space >= 0;
  }
  if (!positioned) {
    return;
  }
  auto pos = pr.append_child("w:tblpPr");
  if (t.from_text[0] >= 0) {
    IntAttr(pos, "w:leftFromText", t.from_text[0]);
  }
  if (t.from_text[1] >= 0) {
    IntAttr(pos, "w:topFromText", t.from_text[1]);
  }
  if (t.from_text[2] >= 0) {
    IntAttr(pos, "w:rightFromText", t.from_text[2]);
  }
  if (t.from_text[3] >= 0) {
    IntAttr(pos, "w:bottomFromText", t.from_text[3]);
  }
  if (t.tblp_x != -1) {
    IntAttr(pos, "w:tblpX", t.tblp_x);
  }
  if (t.tblp_y != -1) {
    IntAttr(pos, "w:tblpY", t.tblp_y);
  }
  const char* anchors[] = {"margin", "page", "text"};
  Attr(pos, "w:horzAnchor",
       t.anchor_h >= 0 && t.anchor_h < 3 ? anchors[t.anchor_h] : "margin");
  Attr(pos, "w:vertAnchor",
       t.anchor_v >= 0 && t.anchor_v < 3 ? anchors[t.anchor_v] : "margin");
}

void WriteTableBorders(pugi::xml_node pr, const model::Table& t) {
  bool any_border = false;
  for (const auto& b : t.borders) {
    any_border |= b.set;
  }
  if (!any_border) {
    return;
  }
  auto bd = pr.append_child("w:tblBorders");
  const char* sides[] = {"w:top",   "w:left",    "w:bottom",
                         "w:right", "w:insideH", "w:insideV"};
  for (size_t k = 0; k < 6; k++) {
    BorderPr(bd, sides[k], t.borders[k]);
  }
}

// A table without an explicit indent is shifted by half the width of its outer
// left border, which is what Word stores for border-inset tables.
int TableIndent(const model::Table& t, bool* has_indent) {
  if (t.indent >= 0) {
    *has_indent = true;
    return t.indent;
  }
  const model::Border* left = &t.borders[1];
  if (!left->set && !t.row_properties.empty() &&
      !t.row_properties[0].cells.empty()) {
    left = &t.row_properties[0].cells[0].borders[1];
  }
  if (left->set && left->type && left->width > 0) {
    *has_indent = true;
    return -(left->width * 5 + 2) / 4;  // half the outer border, in twips
  }
  *has_indent = false;
  return 0;
}

void WriteTableProperties(pugi::xml_node tn, const model::Table& t) {
  auto pr = tn.append_child("w:tblPr");
  if (t.style >= 0) {
    Attr(pr.append_child("w:tblStyle"), "w:val", Id(t.style));
  }
  WriteTablePosition(pr, t);
  auto width = pr.append_child("w:tblW");
  IntAttr(width, "w:w", t.width);
  Attr(width, "w:type",
       t.width_type == 3   ? "dxa"
       : t.width_type == 2 ? "pct"
                           : "auto");
  Attr(pr.append_child("w:tblLayout"), "w:type",
       t.auto_fit ? "autofit" : "fixed");
  if (t.cell_spacing >= 0) {
    auto spacing = pr.append_child("w:tblCellSpacing");
    IntAttr(spacing, "w:w", t.cell_spacing);
    Attr(spacing, "w:type", "dxa");
  }
  bool has_indent = false;
  int indent = TableIndent(t, &has_indent);
  if (has_indent) {
    auto ind = pr.append_child("w:tblInd");
    IntAttr(ind, "w:w", indent);
    Attr(ind, "w:type", "dxa");
  }
  if (t.align >= 0) {
    const char* aligns[] = {"left", "center", "right"};
    Attr(pr.append_child("w:jc"), "w:val",
         t.align < 3 ? aligns[t.align] : "left");
  }
  if (t.bidi) {
    One(pr, "w:bidiVisual");
  }
  if (t.no_overlap) {
    Attr(pr.append_child("w:tblOverlap"), "w:val", "never");
  }
  ShadePr(pr, t.shade, t.shade_fore, t.shade_pattern);
  WriteTableBorders(pr, t);
  if (t.row_band > 0) {
    Value(pr, "w:tblStyleRowBandSize", t.row_band);
  }
  if (t.col_band > 0) {
    Value(pr, "w:tblStyleColBandSize", t.col_band);
  }
  auto margins = pr.append_child("w:tblCellMar");
  const char* margin_sides[] = {"w:top", "w:left", "w:bottom", "w:right"};
  for (size_t k = 0; k < 4; k++) {
    auto m = margins.append_child(margin_sides[k]);
    IntAttr(m, "w:w", t.cell_margins[k] >= 0 ? t.cell_margins[k] : 0);
    Attr(m, "w:type", "dxa");
  }
  if (t.look >= 0) {
    char mask[8];
    const unsigned look_bits = static_cast<unsigned>(t.look) & 0xFFFFu;
    std::snprintf(mask, sizeof(mask), "%04X", look_bits);
    Attr(pr.append_child("w:tblLook"), "w:val", mask);
  }
}

void WriteRowProperties(pugi::xml_node tr, const model::RowProperties* rp) {
  if (!rp || !(rp->cant_split || rp->header || rp->height)) {
    return;
  }
  auto trp = tr.append_child("w:trPr");
  if (rp->cant_split) {
    One(trp, "w:cantSplit");
  }
  if (rp->header) {
    One(trp, "w:tblHeader");
  }
  if (rp->height) {
    auto h = trp.append_child("w:trHeight");
    IntAttr(h, "w:val", std::abs(rp->height));
    Attr(h, "w:hRule", rp->height < 0 ? "exact" : "atLeast");
  }
}

// Number of grid columns a cell spans: walk the grid until its widths cover the
// cell width recorded in the TAPX operand.
size_t CellGridSpan(const model::Table& t, const model::RowProperties* rp,
                    size_t column, size_t grid_index) {
  if (!rp || column >= rp->columns.size() || grid_index >= t.columns.size()) {
    return 1;
  }
  size_t span = 1;
  int width = t.columns[grid_index];
  while (width < rp->columns[column] && grid_index + span < t.columns.size()) {
    width += t.columns[grid_index + span++];
  }
  return width == rp->columns[column] ? span : 1;
}

void WriteCellBorders(pugi::xml_node tcp, const model::CellProperties& cp) {
  bool any_border = false;
  for (const auto& b : cp.borders) {
    any_border |= b.set;
  }
  if (!any_border) {
    return;
  }
  auto bd = tcp.append_child("w:tcBorders");
  const char* sides[] = {"w:top",   "w:left",  "w:bottom",
                         "w:right", "w:tl2br", "w:tr2bl"};
  for (size_t k = 0; k < 6; k++) {
    BorderPr(bd, sides[k], cp.borders[k]);
  }
}

void WriteCellMargins(pugi::xml_node tcp, const model::CellProperties& cp) {
  bool any_margin = false;
  for (int m : cp.margins) {
    any_margin |= m >= 0;
  }
  if (!any_margin) {
    return;
  }
  auto mar = tcp.append_child("w:tcMar");
  const char* sides[] = {"w:top", "w:left", "w:bottom", "w:right"};
  for (size_t k = 0; k < 4; k++) {
    if (cp.margins[k] >= 0) {
      auto m = mar.append_child(sides[k]);
      IntAttr(m, "w:w", cp.margins[k]);
      Attr(m, "w:type", "dxa");
    }
  }
}

void WriteCellProperties(pugi::xml_node tcp, const model::CellProperties* cp,
                         int cell_width, size_t span) {
  if (cell_width >= 0) {
    auto cw = tcp.append_child("w:tcW");
    IntAttr(cw, "w:w", cell_width);
    Attr(cw, "w:type", cp && cp->width_type == 2 ? "pct" : "dxa");
  }
  if (span > 1) {
    Value(tcp, "w:gridSpan", static_cast<int>(span));
  }
  if (!cp) {
    return;
  }
  WriteCellBorders(tcp, *cp);
  ShadePr(tcp, cp->shade, cp->shade_fore, cp->shade_pattern);
  WriteCellMargins(tcp, *cp);
  if (cp->vertical_merge) {
    Attr(tcp.append_child("w:vMerge"), "w:val",
         cp->vertical_merge == 2 ? "restart" : "continue");
  }
  if (cp->horizontal_merge) {
    Attr(tcp.append_child("w:hMerge"), "w:val",
         cp->horizontal_merge == 2 ? "restart" : "continue");
  }
  if (cp->vertical_align >= 0) {
    const char* aligns[] = {"top", "center", "bottom"};
    Attr(tcp.append_child("w:vAlign"), "w:val",
         cp->vertical_align < 3 ? aligns[cp->vertical_align] : "top");
  }
  if (cp->no_wrap) {
    One(tcp, "w:noWrap");
  }
  if (cp->fit_text) {
    One(tcp, "w:tcFitText");
  }
  if (cp->hide_mark) {
    One(tcp, "w:hideMark");
  }
  if (cp->text_direction >= 0) {
    const char* directions[] = {"lrTb", "tbRl",  "lrTb",
                                "btLr", "lrTbV", "tbRlV"};
    Attr(tcp.append_child("w:textDirection"), "w:val",
         cp->text_direction < 6 ? directions[cp->text_direction] : "lrTb");
  }
}

void WriteTable(pugi::xml_node parent, const model::Table& t,
                const model::Document& document, WriteContext& ctx);

void WriteTableRow(pugi::xml_node tn, const model::Table& t, size_t index,
                   const model::Document& document, WriteContext& ctx) {
  const auto& row = t.rows[index];
  const model::RowProperties* rp =
      index < t.row_properties.size() ? &t.row_properties[index] : nullptr;
  auto tr = tn.append_child("w:tr");
  WriteRowProperties(tr, rp);
  size_t grid_index = 0;
  for (size_t j = 0; j < row.size(); j++) {
    auto tc = tr.append_child("w:tc");
    auto tcp = tc.append_child("w:tcPr");
    const model::CellProperties* cp =
        rp && j < rp->cells.size() ? &rp->cells[j] : nullptr;
    int cell_width = cp && cp->width >= 0           ? cp->width
                     : rp && j < rp->columns.size() ? rp->columns[j]
                     : j < t.columns.size()         ? t.columns[j]
                                                    : -1;
    size_t span = CellGridSpan(t, rp, j, grid_index);
    WriteCellProperties(tcp, cp, cell_width, span);
    grid_index += span;
    // A w:tc must contain at least one paragraph.
    if (row[j].blocks.empty()) {
      WriteParagraph(tc, model::Paragraph{}, nullptr, ctx);
    }
    for (const auto& block : row[j].blocks) {
      if (block.table) {
        if (block.index < document.tables.size()) {
          WriteTable(tc, document.tables[block.index], document, ctx);
        }
      } else if (block.index < document.paragraphs.size()) {
        WriteParagraph(tc, document.paragraphs[block.index], nullptr, ctx);
      }
    }
  }
}

void WriteTable(pugi::xml_node parent, const model::Table& t,
                const model::Document& document, WriteContext& ctx) {
  auto tn = parent.append_child("w:tbl");
  WriteTableProperties(tn, t);
  auto grid = tn.append_child("w:tblGrid");
  for (int column : t.columns) {
    IntAttr(grid.append_child("w:gridCol"), "w:w", column);
  }
  for (size_t i = 0; i < t.rows.size(); i++) {
    WriteTableRow(tn, t, i, document, ctx);
  }
}

const model::Section* SectionBreakFor(const model::Document& document,
                                      size_t index) {
  auto it = std::lower_bound(document.section_breaks.begin(),
                             document.section_breaks.end(), index,
                             [](const auto& entry, size_t value) {
                               return entry.first < value;
                             });
  return it != document.section_breaks.end() && it->first == index ? &it->second
                                                                   : nullptr;
}

void WriteBody(pugi::xml_node body, const model::Document& document,
               WriteContext& ctx) {
  if (document.blocks.empty()) {
    for (size_t i = 0; i < document.paragraphs.size(); i++) {
      WriteParagraph(body, document.paragraphs[i], SectionBreakFor(document, i),
                     ctx);
    }
    return;
  }
  for (const auto& block : document.blocks) {
    if (block.table && block.index < document.tables.size()) {
      WriteTable(body, document.tables[block.index], document, ctx);
    } else if (!block.table && block.index < document.paragraphs.size()) {
      WriteParagraph(body, document.paragraphs[block.index],
                     SectionBreakFor(document, block.index), ctx);
    }
  }
}

// CNF condition (cnfc) -> w:tblStylePr/@w:type.
const char* ConditionalType(int cnfc) {
  switch (cnfc) {
    case 0x0001:
      return "firstRow";
    case 0x0002:
      return "lastRow";
    case 0x0004:
      return "firstCol";
    case 0x0008:
      return "lastCol";
    case 0x0010:
      return "band1Vert";
    case 0x0020:
      return "band2Vert";
    case 0x0040:
      return "band1Horz";
    case 0x0080:
      return "band2Horz";
    case 0x0100:
      return "neCell";
    case 0x0200:
      return "nwCell";
    case 0x0400:
      return "seCell";
    case 0x0800:
      return "swCell";
    default:
      return nullptr;
  }
}

void WriteConditionalCellPr(pugi::xml_node tcpr,
                            const model::ConditionalProperties& c) {
  bool any_border = false;
  for (const auto& b : c.borders) {
    any_border |= b.set;
  }
  if (any_border) {
    auto bd = tcpr.append_child("w:tcBorders");
    const char* sides[] = {"w:top",     "w:left",    "w:bottom", "w:right",
                           "w:insideH", "w:insideV", "w:tl2br",  "w:tr2bl"};
    for (size_t i = 0; i < 8; i++) {
      BorderPr(bd, sides[i], c.borders[i]);
    }
  }
  ShadePr(tcpr, c.shade, c.shade_fore, c.shade_pattern);
  if (c.vertical_align >= 0) {
    const char* aligns[] = {"top", "center", "bottom"};
    Attr(tcpr.append_child("w:vAlign"), "w:val",
         aligns[c.vertical_align < 3 ? c.vertical_align : 0]);
  }
  if (c.no_wrap_set) {
    SpecBool(tcpr, "w:noWrap", c.no_wrap, true);
  }
  bool any_margin = false;
  for (int margin : c.margins) {
    any_margin |= margin >= 0;
  }
  if (any_margin) {
    auto mar = tcpr.append_child("w:tcMar");
    const char* sides[] = {"w:top", "w:left", "w:bottom", "w:right"};
    for (size_t k = 0; k < 4; k++) {
      if (c.margins[k] >= 0) {
        auto m = mar.append_child(sides[k]);
        IntAttr(m, "w:w", c.margins[k]);
        Attr(m, "w:type", "dxa");
      }
    }
  }
}

void WriteStyleConditional(pugi::xml_node sn,
                           const model::ConditionalProperties& c) {
  const char* type = ConditionalType(c.type);
  if (type == nullptr) {
    return;
  }
  auto block = sn.append_child("w:tblStylePr");
  Attr(block, "w:type", type);
  // CT_TblStylePr keeps the ECMA-376 child order: pPr, rPr, tblPr, trPr, tcPr.
  ParaPr(block, c.paragraph, true);
  RunPr(block, c.run);
  WriteConditionalCellPr(block.append_child("w:tcPr"), c);
}

// Table styles carry their band sizes and default cell margins in w:tblPr.
void WriteStyleTablePr(pugi::xml_node sn, const model::Style& style) {
  bool any_margin = false;
  for (int margin : style.cell_margins) {
    any_margin |= margin >= 0;
  }
  if (style.row_band <= 0 && style.col_band <= 0 && !any_margin) {
    return;
  }
  auto pr = sn.append_child("w:tblPr");
  if (style.row_band > 0) {
    Value(pr, "w:tblStyleRowBandSize", style.row_band);
  }
  if (style.col_band > 0) {
    Value(pr, "w:tblStyleColBandSize", style.col_band);
  }
  if (any_margin) {
    auto margins = pr.append_child("w:tblCellMar");
    const char* sides[] = {"w:top", "w:left", "w:bottom", "w:right"};
    for (size_t k = 0; k < 4; k++) {
      if (style.cell_margins[k] >= 0) {
        auto m = margins.append_child(sides[k]);
        IntAttr(m, "w:w", style.cell_margins[k]);
        Attr(m, "w:type", "dxa");
      }
    }
  }
}

void WriteStyle(pugi::xml_node styles, const model::Document& document,
                size_t index, const model::Style& style) {
  const char* types[] = {"", "paragraph", "character", "table", "numbering"};
  auto sn = styles.append_child("w:style");
  Attr(sn, "w:type", types[style.type]);
  Attr(sn, "w:styleId", Id(static_cast<int>(index)));
  if (index == 0) {
    Attr(sn, "w:default", "1");
  }
  Attr(sn.append_child("w:name"), "w:val", style.name);
  if (style.base >= 0 &&
      static_cast<size_t>(style.base) < document.styles.size() &&
      !document.styles[style.base].name.empty()) {
    Attr(sn.append_child("w:basedOn"), "w:val", Id(style.base));
  }
  if (style.next >= 0 &&
      static_cast<size_t>(style.next) < document.styles.size() &&
      style.next != static_cast<int>(index) &&
      !document.styles[style.next].name.empty()) {
    Attr(sn.append_child("w:next"), "w:val", Id(style.next));
  }
  if (style.type == 1 || style.type == 3 || style.type == 4) {
    ParaPr(sn, style.paragraph, true);
  }
  RunPr(sn, style.run);
  // CT_Style places w:tblPr after w:pPr/w:rPr.
  if (style.type == 3) {
    WriteStyleTablePr(sn, style);
  }
  for (const auto& conditional : style.conditionals) {
    WriteStyleConditional(sn, conditional);
  }
}

pugi::xml_document WriteStyles(const model::Document& document) {
  pugi::xml_document styles;
  auto sr = styles.append_child("w:styles");
  Attr(sr, "xmlns:w", kWordmlNamespace);
  auto defaults = sr.append_child("w:docDefaults");
  model::Run default_run;
  default_run.font = "Liberation Serif";
  default_run.size = 24;
  auto run_defaults = defaults.append_child("w:rPrDefault");
  RunPr(run_defaults, default_run);
  Value(run_defaults.child("w:rPr"), "w:kern", 2);
  for (size_t i = 0; i < document.styles.size(); i++) {
    const auto& style = document.styles[i];
    if (style.type < 1 || style.type > 4 || style.name.empty()) {
      continue;
    }
    WriteStyle(sr, document, i, style);
  }
  return styles;
}

void WriteListLevel(pugi::xml_node list, const model::ListLevel& level,
                    size_t index) {
  auto ln = list.append_child("w:lvl");
  IntAttr(ln, "w:ilvl", index);
  Value(ln, "w:start", level.start);
  if (level.style >= 0) {
    Attr(ln.append_child("w:pStyle"), "w:val", Id(level.style));
  }
  const char* format = "decimal";
  if (level.format == kNumberFormatBullet) {
    format = "bullet";
  } else if (level.format == kNumberFormatNone) {
    format = "none";
  } else if (level.format == 1) {
    format = "upperRoman";
  } else if (level.format == 2) {
    format = "lowerRoman";
  } else if (level.format == 3) {
    format = "upperLetter";
  } else if (level.format == 4) {
    format = "lowerLetter";
  }
  Attr(ln.append_child("w:numFmt"), "w:val", format);
  Attr(ln.append_child("w:lvlText"), "w:val", level.text);
  if (level.suffix == 2) {
    Attr(ln.append_child("w:suff"), "w:val", "nothing");
  } else if (level.suffix == 1) {
    Attr(ln.append_child("w:suff"), "w:val", "space");
  }
  auto pp = ln.append_child("w:pPr");
  if (level.left || level.first) {
    auto ind = pp.append_child("w:ind");
    IntAttr(ind, "w:left", level.left);
    if (level.first < 0) {
      IntAttr(ind, "w:hanging", -level.first);
    } else {
      IntAttr(ind, "w:firstLine", level.first);
    }
  }
  if (!level.font.empty()) {
    auto rp = ln.append_child("w:rPr");
    auto rf = rp.append_child("w:rFonts");
    Attr(rf, "w:ascii", level.font);
    Attr(rf, "w:hAnsi", level.font);
  }
}

pugi::xml_document WriteNumbering(const model::Document& document) {
  pugi::xml_document numbering;
  auto nr = numbering.append_child("w:numbering");
  Attr(nr, "xmlns:w", kWordmlNamespace);
  for (size_t i = 0; i < document.lists.size(); i++) {
    const auto& list = document.lists[i];
    auto an = nr.append_child("w:abstractNum");
    IntAttr(an, "w:abstractNumId", static_cast<int>(i));
    for (size_t j = 0; j < list.levels.size(); j++) {
      WriteListLevel(an, list.levels[j], j);
    }
  }
  for (size_t i = 0; i < document.list_overrides.size(); i++) {
    if (document.list_overrides[i] < 0) {
      continue;
    }
    auto num = nr.append_child("w:num");
    IntAttr(num, "w:numId", static_cast<int>(i) + 1);
    Value(num, "w:abstractNumId", document.list_overrides[i]);
  }
  return numbering;
}

std::string BuildContentTypes(const WriteContext& ctx) {
  std::string out = kContentTypesPrefix;
  for (const auto& part : ctx.parts) {
    bool header = part.name.starts_with("header");
    out += "<Override PartName=\"/word/" + part.name +
           "\" "
           "ContentType=\"application/"
           "vnd.openxmlformats-officedocument.wordprocessingml." +
           (header ? std::string("header") : std::string("footer")) +
           "+xml\"/>";
  }
  for (const auto& part : ctx.parts) {
    if (part.name == "footnotes.xml") {
      out +=
          "<Override PartName=\"/word/footnotes.xml\" "
          "ContentType=\"application/"
          "vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/"
          ">";
    } else if (part.name == "endnotes.xml") {
      out +=
          "<Override PartName=\"/word/endnotes.xml\" ContentType=\"application/"
          "vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml\"/>";
    }
  }
  if (ctx.odd_even) {
    out += kSettingsContentType;
  }
  out += "</Types>";
  return out;
}

pugi::xml_document BuildRelationships(const WriteContext& ctx) {
  constexpr const char* kRelBase =
      "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
  pugi::xml_document docrels;
  auto relations = docrels.append_child("Relationships");
  Attr(relations, "xmlns",
       "http://schemas.openxmlformats.org/package/2006/relationships");
  auto style_rel = relations.append_child("Relationship");
  Attr(style_rel, "Id", "rId1");
  Attr(style_rel, "Type", std::string(kRelBase) + "styles");
  Attr(style_rel, "Target", "styles.xml");
  auto number_rel = relations.append_child("Relationship");
  Attr(number_rel, "Id", "rId2");
  Attr(number_rel, "Type", std::string(kRelBase) + "numbering");
  Attr(number_rel, "Target", "numbering.xml");
  const int settings_id = ctx.odd_even ? ctx.next_rid : 0;
  for (const auto& [rid, target] : ctx.relations) {
    const char* type = "footer";
    if (target.starts_with("header")) {
      type = "header";
    } else if (target == "footnotes.xml") {
      type = "footnotes";
    } else if (target == "endnotes.xml") {
      type = "endnotes";
    }
    auto rel = relations.append_child("Relationship");
    Attr(rel, "Id", "rId" + std::to_string(rid));
    Attr(rel, "Type", std::string(kRelBase) + type);
    Attr(rel, "Target", target);
  }
  if (settings_id != 0) {
    auto rel = relations.append_child("Relationship");
    Attr(rel, "Id", "rId" + std::to_string(settings_id));
    Attr(rel, "Type", std::string(kRelBase) + "settings");
    Attr(rel, "Target", "settings.xml");
  }
  for (const auto& [url, id] : ctx.hyperlinks) {
    auto link = relations.append_child("Relationship");
    Attr(link, "Id", "rId" + std::to_string(id));
    Attr(link, "Type", std::string(kRelBase) + "hyperlink");
    Attr(link, "Target", url);
    Attr(link, "TargetMode", "External");
  }
  return docrels;
}

std::expected<std::vector<uint8_t>, Error> PackParts(
    const std::vector<Part>& parts) {
  mz_zip_archive z = {};
  if (!mz_zip_writer_init_heap(&z, 0, 0)) {
    return std::unexpected(Error{"ZIP init failed"});
  }
  bool ok = true;
  for (const auto& part : parts) {
    if (!ok) {
      break;
    }
    ok = mz_zip_writer_add_mem(&z, part.name.c_str(), part.data.data(),
                               part.data.size(), MZ_DEFAULT_LEVEL) != 0;
  }
  void* ptr = nullptr;
  size_t len = 0;
  if (ok) {
    ok = mz_zip_writer_finalize_heap_archive(&z, &ptr, &len) != 0;
  }
  mz_zip_writer_end(&z);
  if (!ok) {
    if (ptr) {
      mz_free(ptr);
    }
    return std::unexpected(Error{"ZIP write failed"});
  }
  auto* bytes = static_cast<uint8_t*>(ptr);
  std::vector<uint8_t> result(bytes, bytes + len);
  mz_free(ptr);
  return result;
}
}  // namespace

uint32_t Crc32(std::string_view bytes) {
  return mz_crc32(0, reinterpret_cast<const unsigned char*>(bytes.data()),
                  bytes.size());
}

std::expected<std::vector<uint8_t>, Error> Write(
    const model::Document& document) {
  WriteContext ctx;
  pugi::xml_document doc;
  auto root = doc.append_child("w:document");
  Attr(root, "xmlns:w", kWordmlNamespace);
  Attr(root, "xmlns:r", kRelationshipsNamespace);
  auto body = root.append_child("w:body");
  WriteBody(body, document, ctx);

  WriteNoteParts(document, ctx);
  auto sec = body.append_child("w:sectPr");
  SectionPr(sec, document.section, &ctx);

  pugi::xml_document styles = WriteStyles(document);
  pugi::xml_document numbering = WriteNumbering(document);
  pugi::xml_document docrels = BuildRelationships(ctx);

  std::vector<Part> parts;
  parts.push_back({"[Content_Types].xml", BuildContentTypes(ctx)});
  parts.push_back({"_rels/.rels", kRootRelationships});
  parts.push_back({"word/_rels/document.xml.rels", Save(docrels)});
  parts.push_back({"word/document.xml", Save(doc)});
  parts.push_back({"word/styles.xml", Save(styles)});
  parts.push_back({"word/numbering.xml", Save(numbering)});
  for (const auto& part : ctx.parts) {
    parts.push_back({"word/" + part.name, part.data});
  }
  if (ctx.odd_even) {
    parts.push_back({"word/settings.xml", kSettingsPart});
  }
  return PackParts(parts);
}
}  // namespace doccraft::docx
