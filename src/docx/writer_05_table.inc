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

void WriteCellMargins(pugi::xml_node tcp, const model::CellProperties& cp,
                      bool correct_left_inset) {
  // DOC cell insets round one twip lower when LibreOffice imports OOXML.
  // The left correction preserves the original DOC text origin.
  constexpr size_t kLeftMarginIndex = 1;
  constexpr int kInsetRoundTripTwip = 1;
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
      IntAttr(m, "w:w",
              cp.margins[k] + (correct_left_inset && k == kLeftMarginIndex
                                   ? kInsetRoundTripTwip
                                   : 0));
      Attr(m, "w:type", "dxa");
    }
  }
}

void WriteCellProperties(pugi::xml_node tcp, const model::CellProperties* cp,
                         int cell_width, size_t span, bool correct_left_inset) {
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
  WriteCellMargins(tcp, *cp, correct_left_inset);
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

std::expected<void, Error> WriteTable(pugi::xml_node parent,
                                      const model::Table& t,
                                      const model::Document& document,
                                      WriteContext& ctx);

std::expected<void, Error> WriteTableRow(pugi::xml_node tn,
                                         const model::Table& t, size_t index,
                                         const model::Document& document,
                                         WriteContext& ctx,
                                         bool correct_left_inset) {
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
    WriteCellProperties(tcp, cp, cell_width, span, correct_left_inset);
    grid_index += span;
    // A w:tc must contain at least one paragraph.
    if (row[j].blocks.empty()) {
      auto result = WriteParagraph(tc, model::Paragraph{}, nullptr, ctx);
      if (!result)
        return std::unexpected(result.error());
    }
    for (const auto& block : row[j].blocks) {
      if (block.table) {
        if (block.index < document.tables.size()) {
          auto result =
              WriteTable(tc, document.tables[block.index], document, ctx);
          if (!result)
            return std::unexpected(result.error());
        }
      } else if (block.index < document.paragraphs.size()) {
        auto result =
            WriteParagraph(tc, document.paragraphs[block.index], nullptr, ctx);
        if (!result)
          return std::unexpected(result.error());
      }
    }
  }
  return {};
}

std::expected<void, Error> WriteTable(pugi::xml_node parent,
                                      const model::Table& t,
                                      const model::Document& document,
                                      WriteContext& ctx) {
  bool correct_left_inset = false;
  for (const auto& row : t.row_properties) {
    for (const auto& cell : row.cells) {
      correct_left_inset |= cell.margins[1] > 0;
      for (const auto& border : cell.borders) {
        correct_left_inset |= border.set && border.type > 0;
      }
    }
  }
  auto tn = parent.append_child("w:tbl");
  WriteTableProperties(tn, t);
  auto grid = tn.append_child("w:tblGrid");
  for (int column : t.columns) {
    IntAttr(grid.append_child("w:gridCol"), "w:w", column);
  }
  for (size_t i = 0; i < t.rows.size(); i++) {
    auto result = WriteTableRow(tn, t, i, document, ctx, correct_left_inset);
    if (!result)
      return std::unexpected(result.error());
  }
  return {};
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

std::expected<void, Error> WriteBody(pugi::xml_node body,
                                     const model::Document& document,
                                     WriteContext& ctx) {
  if (document.blocks.empty()) {
    for (size_t i = 0; i < document.paragraphs.size(); i++) {
      auto result = WriteParagraph(body, document.paragraphs[i],
                                   SectionBreakFor(document, i), ctx);
      if (!result)
        return std::unexpected(result.error());
    }
    return {};
  }
  for (const auto& block : document.blocks) {
    if (block.table && block.index < document.tables.size()) {
      auto result =
          WriteTable(body, document.tables[block.index], document, ctx);
      if (!result)
        return std::unexpected(result.error());
    } else if (!block.table && block.index < document.paragraphs.size()) {
      auto result = WriteParagraph(body, document.paragraphs[block.index],
                                   SectionBreakFor(document, block.index), ctx);
      if (!result)
        return std::unexpected(result.error());
    }
  }
  return {};
}

// CNF condition (cnfc) -> w:tblStylePr/@w:type.
