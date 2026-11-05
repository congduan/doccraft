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

// A w:tblStylePr block applies on top of its table style, so its tab operand
// is evaluated against the table style's resolved set, exactly like paragraph
// tabs against their paragraph style. With no operand there is nothing to
// write; with an empty inherited set, every unmatched clear is dropped.
std::vector<model::TabStop> ConditionalTabs(
    const model::Style& style, const model::ConditionalProperties& c) {
  if (c.paragraph.tabs.empty()) {
    return {};
  }
  return MergedTabs(style.resolved_paragraph.tabs, c.paragraph.tabs);
}

void WriteStyleConditional(pugi::xml_node sn,
                           const model::Style& style,
                           const model::ConditionalProperties& c,
                           const model::Document& document) {
  const char* type = ConditionalType(c.type);
  if (type == nullptr) {
    return;
  }
  auto block = sn.append_child("w:tblStylePr");
  Attr(block, "w:type", type);
  // CT_TblStylePr keeps the ECMA-376 child order: pPr, rPr, tblPr, trPr, tcPr.
  std::vector<model::TabStop> tabs = ConditionalTabs(style, c);
  ParaPr(block, c.paragraph, true, &tabs);
  RunPr(block, c.run, &document);
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

// Tabs to write into a style's w:pPr. The resolved set already contains the
// based-on stops; LibreOffice writes that full set into the derived style. If
// the style keeps its w:basedOn link, stops removed from the base also need a
// w:val="clear" entry so the link does not re-introduce them.
std::vector<model::TabStop> WrittenStyleTabs(const model::Document& document,
                                             const model::Style& style,
                                             bool based_on) {
  // A style with no tab change of its own inherits the based-on set through
  // w:basedOn; re-emitting it would duplicate the link's work.
  if (style.paragraph.tabs.empty()) {
    return {};
  }
  std::vector<model::TabStop> tabs = style.resolved_paragraph.tabs;
  SortTabsByPosition(&tabs);
  if (!based_on) {
    return tabs;
  }
  const std::vector<model::TabStop>& base =
      document.styles[style.base].resolved_paragraph.tabs;
  std::vector<model::TabStop> clears;
  for (const auto& stop : base) {
    if (!HasTabAt(tabs, stop.position)) {
      model::TabStop clear;
      clear.position = stop.position;
      clear.clear = true;
      clears.push_back(clear);
    }
  }
  SortTabsByPosition(&clears);
  clears.insert(clears.end(), tabs.begin(), tabs.end());
  return clears;
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
  const bool based_on =
      style.base >= 0 &&
      static_cast<size_t>(style.base) < document.styles.size() &&
      !document.styles[style.base].name.empty();
  if (based_on) {
    Attr(sn.append_child("w:basedOn"), "w:val", Id(style.base));
  }
  if (style.next >= 0 &&
      static_cast<size_t>(style.next) < document.styles.size() &&
      style.next != static_cast<int>(index) &&
      !document.styles[style.next].name.empty()) {
    Attr(sn.append_child("w:next"), "w:val", Id(style.next));
  }
  if (style.type == 1 || style.type == 3 || style.type == 4) {
    std::vector<model::TabStop> tabs =
        WrittenStyleTabs(document, style, based_on);
    ParaPr(sn, style.paragraph, true, &tabs);
  }
  RunPr(sn, style.run, &document);
  // CT_Style places w:tblPr after w:pPr/w:rPr.
  if (style.type == 3) {
    WriteStyleTablePr(sn, style);
  }
  for (const auto& conditional : style.conditionals) {
    WriteStyleConditional(sn, style, conditional, document);
  }
}

// Text that names no style or run formatting inherits w:docDefaults. Word 97
// seeds that from Normal, whose built-in defaults are Times New Roman 12pt.
// Prefer the sheet's Normal (or its first paragraph style, which is the
// localized Normal in Word 6/95 sheets) so localized and custom documents keep
// their own font and size.
const model::Style* DefaultStyle(const model::Document& document) {
  for (const auto& style : document.styles) {
    if (style.type == 1 && style.name == "Normal") {
      return &style;
    }
  }
  for (const auto& style : document.styles) {
    if (style.type == 1) {
      return &style;
    }
  }
  return nullptr;
}

model::Run DefaultRun(const model::Document& document) {
  model::Run run;
  run.font = "Times New Roman";
  run.size = 24;
  const model::Style* base = DefaultStyle(document);
  if (base == nullptr) {
    return run;
  }
  if (!base->resolved_run.font.empty()) {
    run.font = base->resolved_run.font;
  }
  if (base->resolved_run.size > 0) {
    run.size = base->resolved_run.size;
  }
  return run;
}

// Paragraph defaults are the same Normal resolved set DefaultRun uses. Only
// fields the writer already emits through ParaPr() and whose IR carries a
// clean tri-state (value + *_set, or a negative sentinel) are copied. Tabs,
// numbering, borders, shading, frames, line numbering and outline level stay on
// the style itself: promoting them to the document defaults would change
// paragraphs that name another style.
bool HasDefaultParagraphProperties(const model::Paragraph& p) {
  return p.align >= 0 || p.left_set || p.right_set || p.first_set ||
         p.nest_set || p.left_chars_set || p.right_chars_set ||
         p.first_line_chars_set || p.before >= 0 || p.after >= 0 ||
         p.line != 0 || p.before_lines_set || p.after_lines_set ||
         p.before_auto_set || p.after_auto_set || p.widow_control_set ||
         p.suppress_hyphenation_set || p.contextual_spacing_set ||
         p.mirror_indents_set || p.snap_to_grid_set || p.adjust_right_set ||
         p.kinsoku_set || p.overflow_punct_set || p.top_line_punct_set ||
         p.auto_space_de_set || p.auto_space_dn_set || p.word_wrap_set ||
         p.bidi_set || p.keep_next_set || p.keep_lines_set ||
         p.page_break_before_set;
}

model::Paragraph DefaultParagraph(const model::Paragraph& p) {
  model::Paragraph q;
  q.align = p.align;
  q.left = p.left;
  q.left_set = p.left_set;
  q.right = p.right;
  q.right_set = p.right_set;
  q.first = p.first;
  q.first_set = p.first_set;
  q.nest = p.nest;
  q.nest_set = p.nest_set;
  q.left_chars = p.left_chars;
  q.left_chars_set = p.left_chars_set;
  q.right_chars = p.right_chars;
  q.right_chars_set = p.right_chars_set;
  q.first_line_chars = p.first_line_chars;
  q.first_line_chars_set = p.first_line_chars_set;
  q.before = p.before;
  q.after = p.after;
  q.line = p.line;
  q.line_auto = p.line_auto;
  q.before_lines = p.before_lines;
  q.before_lines_set = p.before_lines_set;
  q.after_lines = p.after_lines;
  q.after_lines_set = p.after_lines_set;
  q.before_auto = p.before_auto;
  q.before_auto_set = p.before_auto_set;
  q.after_auto = p.after_auto;
  q.after_auto_set = p.after_auto_set;
  q.widow_control = p.widow_control;
  q.widow_control_set = p.widow_control_set;
  q.suppress_hyphenation = p.suppress_hyphenation;
  q.suppress_hyphenation_set = p.suppress_hyphenation_set;
  q.contextual_spacing = p.contextual_spacing;
  q.contextual_spacing_set = p.contextual_spacing_set;
  q.mirror_indents = p.mirror_indents;
  q.mirror_indents_set = p.mirror_indents_set;
  q.snap_to_grid = p.snap_to_grid;
  q.snap_to_grid_set = p.snap_to_grid_set;
  q.adjust_right = p.adjust_right;
  q.adjust_right_set = p.adjust_right_set;
  q.kinsoku = p.kinsoku;
  q.kinsoku_set = p.kinsoku_set;
  q.overflow_punct = p.overflow_punct;
  q.overflow_punct_set = p.overflow_punct_set;
  q.top_line_punct = p.top_line_punct;
  q.top_line_punct_set = p.top_line_punct_set;
  q.auto_space_de = p.auto_space_de;
  q.auto_space_de_set = p.auto_space_de_set;
  q.auto_space_dn = p.auto_space_dn;
  q.auto_space_dn_set = p.auto_space_dn_set;
  q.word_wrap = p.word_wrap;
  q.word_wrap_set = p.word_wrap_set;
  q.bidi = p.bidi;
  q.bidi_set = p.bidi_set;
  q.keep_next = p.keep_next;
  q.keep_next_set = p.keep_next_set;
  q.keep_lines = p.keep_lines;
  q.keep_lines_set = p.keep_lines_set;
  q.page_break_before = p.page_break_before;
  q.page_break_before_set = p.page_break_before_set;
  return q;
}

pugi::xml_document WriteStyles(const model::Document& document) {
  pugi::xml_document styles;
  auto sr = styles.append_child("w:styles");
  Attr(sr, "xmlns:w", kWordmlNamespace);
  auto defaults = sr.append_child("w:docDefaults");
  model::Run default_run = DefaultRun(document);
  auto run_defaults = defaults.append_child("w:rPrDefault");
  RunPr(run_defaults, default_run, &document);
  Value(run_defaults.child("w:rPr"), "w:kern", 2);
  // CT_DocDefaults keeps rPrDefault before pPrDefault. An empty result is
  // omitted so a document without paragraph formatting keeps a minimal
  // w:docDefaults.
  const model::Style* base = DefaultStyle(document);
  if (base != nullptr) {
    const model::Paragraph paragraph =
        DefaultParagraph(base->resolved_paragraph);
    if (HasDefaultParagraphProperties(paragraph)) {
      std::vector<model::TabStop> no_tabs;
      ParaPr(defaults.append_child("w:pPrDefault"), paragraph, true, &no_tabs);
    }
  }
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
  // CT_Lvl order: start, numFmt, lvlRestart, pStyle, isLgl, suff, lvlText.
  Value(ln, "w:start", level.start);
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
  if (level.style >= 0) {
    Attr(ln.append_child("w:pStyle"), "w:val", Id(level.style));
  }
  if (level.suffix == 2) {
    Attr(ln.append_child("w:suff"), "w:val", "nothing");
  } else if (level.suffix == 1) {
    Attr(ln.append_child("w:suff"), "w:val", "space");
  }
  Attr(ln.append_child("w:lvlText"), "w:val", level.text);
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
