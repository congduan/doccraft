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

void WriteStyleConditional(pugi::xml_node sn,
                           const model::ConditionalProperties& c,
                           const model::Document& document) {
  const char* type = ConditionalType(c.type);
  if (type == nullptr) {
    return;
  }
  auto block = sn.append_child("w:tblStylePr");
  Attr(block, "w:type", type);
  // CT_TblStylePr keeps the ECMA-376 child order: pPr, rPr, tblPr, trPr, tcPr.
  ParaPr(block, c.paragraph, true);
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
  RunPr(sn, style.run, &document);
  // CT_Style places w:tblPr after w:pPr/w:rPr.
  if (style.type == 3) {
    WriteStyleTablePr(sn, style);
  }
  for (const auto& conditional : style.conditionals) {
    WriteStyleConditional(sn, conditional, document);
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
  RunPr(run_defaults, default_run, &document);
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

