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

// Fallback width for non-evenly-spaced columns that lack a sprmSDxaColWidth:
// the value LibreOffice substitutes when importing such columns.
constexpr int kColumnWidthFallbackTwips = 1440;

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
  // sprmSDxaColWidth/Spacing apply "in case columns are not evenly spaced"
  // ([MS-DOC] 2.6.4), and every column of a non-even section MUST carry a
  // width sprm. Non-conforming files that leave a column without one keep
  // their column count and spacing by falling back to the width LibreOffice
  // substitutes on import (sw/source/filter/ww8/ww8par6.cxx: 1440 twips).
  // Evenly spaced sections never emit per-column overrides, so they keep
  // dropping spec entries.
  const bool fallback_widths = !section.columns_even && section.columns > 1;
  for (int index = 0; index < section.columns; index++) {
    const model::ColumnSpec* spec = nullptr;
    for (const auto& entry : section.column_specs) {
      if (entry.index == index) {
        spec = &entry;
      }
    }
    const bool has_width = spec && spec->width >= 0;
    if (!has_width && !fallback_widths) {
      continue;
    }
    auto col = cols.append_child("w:col");
    IntAttr(col, "w:w", has_width ? spec->width : kColumnWidthFallbackTwips);
    if (spec && spec->space >= 0) {
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

std::expected<void, Error> SectionPr(pugi::xml_node sec,
                                     const model::Section& section,
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
    auto result = WriteSectionReferences(sec, section, *ctx);
    if (!result)
      return std::unexpected(result.error());
  }
  return {};
}
