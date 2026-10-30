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

// CT_PPr is a long xs:sequence: paragraph flags that look related belong to
// different positions in it. These helpers are split so ParaPr() can emit them
// in schema order (see the generated table: w:pPr particle).
void WriteParagraphFlowFlags(pugi::xml_node pr, const model::Paragraph& p) {
  SpecBool(pr, "w:keepNext", p.keep_next, p.keep_next_set);
  SpecBool(pr, "w:keepLines", p.keep_lines, p.keep_lines_set);
  SpecBool(pr, "w:pageBreakBefore", p.page_break_before,
           p.page_break_before_set);
}

// After w:tabs, before w:bidi/w:adjustRightInd/w:snapToGrid.
void WriteParagraphEastAsianFlags(pugi::xml_node pr,
                                 const model::Paragraph& p) {
  SpecBool(pr, "w:suppressAutoHyphens", p.suppress_hyphenation,
           p.suppress_hyphenation_set);
  SpecBool(pr, "w:kinsoku", p.kinsoku, p.kinsoku_set);
  SpecBool(pr, "w:wordWrap", p.word_wrap, p.word_wrap_set);
  SpecBool(pr, "w:overflowPunct", p.overflow_punct, p.overflow_punct_set);
  SpecBool(pr, "w:topLinePunct", p.top_line_punct, p.top_line_punct_set);
  SpecBool(pr, "w:autoSpaceDE", p.auto_space_de, p.auto_space_de_set);
  SpecBool(pr, "w:autoSpaceDN", p.auto_space_dn, p.auto_space_dn_set);
  SpecBool(pr, "w:bidi", p.bidi, p.bidi_set);
  SpecBool(pr, "w:adjustRightInd", p.adjust_right, p.adjust_right_set);
  SpecBool(pr, "w:snapToGrid", p.snap_to_grid, p.snap_to_grid_set);
}

// After w:ind, before w:jc.
void WriteParagraphPostIndentFlags(pugi::xml_node pr,
                                  const model::Paragraph& p) {
  SpecBool(pr, "w:contextualSpacing", p.contextual_spacing,
           p.contextual_spacing_set);
  SpecBool(pr, "w:mirrorIndents", p.mirror_indents, p.mirror_indents_set);
}

// After w:jc.
void WriteParagraphTrailingPr(pugi::xml_node pr, const model::Paragraph& p) {
  if (p.text_alignment >= 0) {
    const char* align[] = {"top", "center", "baseline", "bottom", "auto"};
    Attr(pr.append_child("w:textAlignment"), "w:val",
         p.text_alignment < 5 ? align[p.text_alignment] : "auto");
  }
  if (p.outline >= 0 && p.outline <= 9) {
    Value(pr, "w:outlineLvl", p.outline);
  }
}

bool HasTabAt(const std::vector<model::TabStop>& tabs, int position) {
  return std::any_of(tabs.begin(), tabs.end(), [&](const auto& tab) {
    return tab.position == position;
  });
}

void SortTabsByPosition(std::vector<model::TabStop>* tabs) {
  std::stable_sort(tabs->begin(), tabs->end(),
                   [](const auto& a, const auto& b) {
                     return a.position < b.position;
                   });
}

// Tabs to write for a paragraph. OOXML still inherits the style's stops
// through w:pStyle, so only the paragraph's own changes are emitted: an add
// overrides the inherited stop, and a clear is kept only when the inherited
// set really has that position. An unmatched clear is dropped because it would
// name a stop no consumer can hold (LibreOffice does the same). Without a
// resolvable paragraph style there is nothing to compare against, so the
// operand is written verbatim.
std::vector<model::TabStop> WrittenParagraphTabs(
    const model::Paragraph& p, const model::Document* document) {
  if (p.tabs.empty() || !document || p.style < 0 ||
      static_cast<size_t>(p.style) >= document->styles.size()) {
    return p.tabs;
  }
  const model::Style& style = document->styles[p.style];
  if (style.type != 1) {
    return p.tabs;
  }
  const std::vector<model::TabStop>& inherited = style.resolved_paragraph.tabs;
  std::vector<model::TabStop> effective = inherited;
  std::vector<int> added;
  for (const auto& tab : p.tabs) {
    auto it = std::find_if(effective.begin(), effective.end(),
                           [&](const auto& stop) {
                             return stop.position == tab.position;
                           });
    if (tab.clear) {
      if (it != effective.end())
        effective.erase(it);
      added.erase(std::remove(added.begin(), added.end(), tab.position),
                  added.end());
    } else {
      if (it != effective.end())
        *it = tab;
      else
        effective.push_back(tab);
      if (std::find(added.begin(), added.end(), tab.position) == added.end())
        added.push_back(tab.position);
    }
  }
  std::vector<model::TabStop> clears;
  for (const auto& stop : inherited) {
    if (!HasTabAt(effective, stop.position) &&
        !HasTabAt(clears, stop.position)) {
      model::TabStop clear;
      clear.position = stop.position;
      clear.clear = true;
      clears.push_back(clear);
    }
  }
  std::vector<model::TabStop> additions;
  for (const auto& stop : effective) {
    if (std::find(added.begin(), added.end(), stop.position) != added.end())
      additions.push_back(stop);
  }
  SortTabsByPosition(&clears);
  SortTabsByPosition(&additions);
  clears.insert(clears.end(), additions.begin(), additions.end());
  return clears;
}

void WriteParagraphTabs(pugi::xml_node pr,
                        const std::vector<model::TabStop>& tabs_in) {
  if (tabs_in.empty()) {
    return;
  }
  auto tabs = pr.append_child("w:tabs");
  const char* aligns[] = {"left", "center", "right", "decimal", "bar", "num"};
  const char* leaders[] = {"none",       "dot",   "hyphen",
                           "underscore", "heavy", "middleDot"};
  for (const auto& tab : tabs_in) {
    auto stop = tabs.append_child("w:tab");
    // A deleted stop removes a style-inherited one; Word writes no leader for
    // it. Unstyled paragraphs keep the operand order (deletes first), so a
    // later add at the same position wins exactly as it does in Word; styled
    // paragraphs have already been merged into clear/add groups by
    // WrittenParagraphTabs().
    if (tab.clear) {
      Attr(stop, "w:val", "clear");
    } else {
      Attr(stop, "w:val", tab.align < 6 ? aligns[tab.align] : "left");
    }
    IntAttr(stop, "w:pos", tab.position);
    if (!tab.clear && tab.leader) {
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
            bool style_props = false,
            const std::vector<model::TabStop>* tabs = nullptr) {
  auto pr = parent.append_child("w:pPr");
  if (!style_props && p.style >= 0) {
    Attr(pr.append_child("w:pStyle"), "w:val", Id(p.style));
  }
  WriteParagraphFlowFlags(pr, p);
  WriteFramePr(pr, p);
  SpecBool(pr, "w:widowControl", p.widow_control, p.widow_control_set);
  if (p.list_id > 0) {
    auto num = pr.append_child("w:numPr");
    Value(num, "w:ilvl", std::clamp(p.list_level + p.list_level_inc, 0, 8));
    Value(num, "w:numId", p.list_id);
  }
  SpecBool(pr, "w:suppressLineNumbers", p.no_line_number, p.no_line_number_set);
  WriteParagraphBorders(pr, p);
  ShadePr(pr, p.shade, p.shade_fore, p.shade_pattern);
  WriteParagraphTabs(pr, tabs ? *tabs : p.tabs);
  WriteParagraphEastAsianFlags(pr, p);
  WriteParagraphSpacing(pr, p);
  WriteParagraphIndent(pr, p);
  WriteParagraphPostIndentFlags(pr, p);
  if (p.align >= 0) {
    const char* align[] = {"left", "center", "right", "both", "distribute"};
    Attr(pr.append_child("w:jc"), "w:val",
         p.align < 5 ? align[p.align] : "left");
  }
  WriteParagraphTrailingPr(pr, p);
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


