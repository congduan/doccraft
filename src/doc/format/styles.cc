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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "base/charset.h"
#include "doc/format/sprm.h"

namespace doccraft::doc::detail {
std::vector<std::string> ReadFonts(Bytes table, Fib::Range r) {
  std::vector<std::string> out;
  if (!r.size || !Valid(table, r) || r.size < 4)
    return out;
  Bytes b = table.subspan(r.offset, r.size);
  unsigned count = U16(b, 0);
  size_t p = 4;
  for (unsigned i = 0; i < count && p < b.size(); i++) {
    size_t len = b[p], end = p + 1 + len;
    if (end > b.size() || len < 40)
      break;
    std::string name;
    for (size_t x = p + 40; x + 1 < end; x += 2) {
      uint16_t c = U16(b, x);
      if (!c)
        break;
      AppendUtf8(name, c);
    }
    out.push_back(std::move(name));
    p = end;
  }
  return out;
}

void OverlayRun(model::Run& dst, const model::Run& src) {
  if (!src.font.empty())
    dst.font = src.font;
  if (src.size)
    dst.size = src.size;
  if (src.bold_set) {
    dst.bold = src.bold;
    dst.bold_set = true;
  }
  if (src.italic_set) {
    dst.italic = src.italic;
    dst.italic_set = true;
  }
  if (src.strike_set) {
    dst.strike = src.strike;
    dst.strike_set = true;
  }
  if (src.double_strike_set) {
    dst.double_strike = src.double_strike;
    dst.double_strike_set = true;
  }
  if (src.hidden_set) {
    dst.hidden = src.hidden;
    dst.hidden_set = true;
  }
  if (src.shadow_set) {
    dst.shadow = src.shadow;
    dst.shadow_set = true;
  }
  if (src.emboss_set) {
    dst.emboss = src.emboss;
    dst.emboss_set = true;
  }
  if (src.imprint_set) {
    dst.imprint = src.imprint;
    dst.imprint_set = true;
  }
  if (src.outline_set) {
    dst.outline = src.outline;
    dst.outline_set = true;
  }
  if (src.small_caps_set) {
    dst.small_caps = src.small_caps;
    dst.small_caps_set = true;
  }
  if (src.caps_set) {
    dst.caps = src.caps;
    dst.caps_set = true;
  }
  if (src.underline_set) {
    dst.underline = src.underline;
    dst.underline_set = true;
  }
  if (src.emphasis >= 0)
    dst.emphasis = src.emphasis;
  if (src.super_sub_set) {
    dst.super_sub = src.super_sub;
    dst.super_sub_set = true;
  }
  if (src.spacing_set) {
    dst.spacing = src.spacing;
    dst.spacing_set = true;
  }
  if (src.scale_set) {
    dst.scale = src.scale;
    dst.scale_set = true;
  }
  if (src.color >= 0) {
    dst.color = src.color;
    dst.color_rgb = src.color_rgb;
  }
  if (src.highlight >= 0)
    dst.highlight = src.highlight;
  if (src.shade >= 0)
    dst.shade = src.shade;
  if (src.shade_fore >= 0)
    dst.shade_fore = src.shade_fore;
  if (src.shade >= 0 || src.shade_fore >= 0)
    dst.shade_pattern = src.shade_pattern;
  if (src.border.set)
    dst.border = src.border;
}

void OverlayParagraph(model::Paragraph& dst, const model::Paragraph& src) {
  if (src.align >= 0)
    dst.align = src.align;
  if (src.left_set) {
    dst.left = src.left;
    dst.left_set = true;
  }
  if (src.right_set) {
    dst.right = src.right;
    dst.right_set = true;
  }
  if (src.first_set) {
    dst.first = src.first;
    dst.first_set = true;
  }
  if (src.before >= 0)
    dst.before = src.before;
  if (src.after >= 0)
    dst.after = src.after;
  if (src.line) {
    dst.line = src.line;
    dst.line_auto = src.line_auto;
  }
  if (src.keep_next_set) {
    dst.keep_next = src.keep_next;
    dst.keep_next_set = true;
  }
  if (src.keep_lines_set) {
    dst.keep_lines = src.keep_lines;
    dst.keep_lines_set = true;
  }
  if (src.page_break_before_set) {
    dst.page_break_before = src.page_break_before;
    dst.page_break_before_set = true;
  }
  if (src.outline >= 0)
    dst.outline = src.outline;
  if (src.list_id >= 0) {
    dst.list_id = src.list_id;
    dst.list_level = src.list_level;
  }
  if (src.list_level_inc)
    dst.list_level_inc = src.list_level_inc;
  if (!src.tabs.empty())
    dst.tabs = src.tabs;
  for (size_t k = 0; k < 4; k++)
    if (src.borders[k].set)
      dst.borders[k] = src.borders[k];
  for (size_t k = 0; k < 3; k++)
    if (src.extra_borders[k].set)
      dst.extra_borders[k] = src.extra_borders[k];
  if (src.shade >= 0)
    dst.shade = src.shade;
  if (src.shade_fore >= 0)
    dst.shade_fore = src.shade_fore;
  if (src.shade >= 0 || src.shade_fore >= 0)
    dst.shade_pattern = src.shade_pattern;
}

// Resolves `base` chains into resolved_run/resolved_paragraph. A style that is
// already being visited (state 1) terminates the chain, which breaks cycles.
void ResolveStyle(std::vector<model::Style>& styles,
                  std::vector<uint8_t>& state, size_t index) {
  if (state[index] != 0) {
    return;
  }
  state[index] = 1;
  int base = styles[index].base;
  if (base >= 0 && static_cast<size_t>(base) < styles.size() &&
      static_cast<size_t>(base) != index) {
    ResolveStyle(styles, state, base);
    if (state[base] == 2) {
      styles[index].resolved_run = styles[base].resolved_run;
      styles[index].resolved_paragraph = styles[base].resolved_paragraph;
    }
  }
  OverlayRun(styles[index].resolved_run, styles[index].run);
  OverlayParagraph(styles[index].resolved_paragraph, styles[index].paragraph);
  state[index] = 2;
}

// Reads the STD sheet (stsh): one record per style, each with its UPX array of
// paragraph and character exception blocks.
std::vector<model::Style> ReadStyleSheet(
    Bytes table, Fib::Range r, const std::vector<std::string>& fonts) {
  std::vector<model::Style> out;
  if (!r.size || !Valid(table, r) || r.size < 20)
    return out;
  Bytes b = table.subspan(r.offset, r.size);
  size_t info = U16(b, 0);
  if (info < 18 || info + 2 > b.size())
    return out;
  unsigned count = U16(b, 2), base_size = U16(b, 4);
  if (count > 4096 || (base_size != 10 && base_size != 18))
    return out;
  size_t p = 2 + info;
  out.resize(count);
  for (unsigned i = 0; i < count && p + 2 <= b.size(); i++) {
    size_t len = U16(b, p);
    p += 2;
    if (len > b.size() - p)
      break;
    Bytes d = b.subspan(p, len);
    p += len + (len & 1);
    if (len < base_size + 4)
      continue;
    auto& s = out[i];
    s.type = U16(d, 2) & 15;
    s.base = U16(d, 2) >> 4;
    s.next = U16(d, 4) >> 4;
    if (s.base == 0x0fff)
      s.base = -1;
    size_t x = base_size, chars = U16(d, x);
    x += 2;
    if (chars > (d.size() - x) / 2 || x + 2 * chars + 2 > d.size())
      continue;
    for (size_t j = 0; j < chars; j++)
      AppendUtf8(s.name, U16(d, x + 2 * j));
    x += 2 * chars + 2;
    if (x & 1)
      x++;
    unsigned upx = U16(d, 4) & 15;
    for (unsigned j = 0; j < upx && x + 2 <= d.size(); j++) {
      size_t n = U16(d, x);
      x += 2;
      if (n > d.size() - x)
        break;
      Bytes payload = d.subspan(x, n);
      if (s.type == 3 && j == 0) {
        s.table_sprms.assign(payload.begin(), payload.end());
        // Table styles keep their tblPr defaults and their per-condition blocks
        // in the TAPX; both are needed for w:tblPr / w:tblStylePr output.
        model::Table table;
        model::RowProperties row;
        Walk(payload, [&](uint16_t op, Bytes v) {
          if (!ApplyConditional(s, op, v, fonts))
            ApplyTable(table, row, op, v);
        });
        s.row_band = table.row_band;
        s.col_band = table.col_band;
        s.cell_margins = table.cell_margins;
      }
      if (((s.type == 1 || s.type == 4) && j == 0) || (s.type == 3 && j == 1)) {
        if (n >= 2) {
          Walk(payload.subspan(2), [&](uint16_t op, Bytes v) {
            if (!ApplyConditional(s, op, v, fonts))
              ApplyPara(s.paragraph, op, v);
          });
        }
      }
      if ((s.type == 1 && j == 1) || (s.type == 2 && j == 0) ||
          (s.type == 3 && j == 2)) {
        Walk(payload, [&](uint16_t op, Bytes v) {
          if (!ApplyConditional(s, op, v, fonts))
            ApplyRun(s.run, op, v, fonts);
        });
      }
      x += n;
      if (x & 1)
        x++;
    }
  }
  return out;
}

// Orders conditional blocks and resolves based-on chains for a parsed sheet.
std::vector<model::Style> FinishStyles(std::vector<model::Style> styles) {
  // [MS-DOC] 2.4.6.6: later matching conditions override earlier ones.
  // These CNFC values are a precedence table, not their STSH appearance order.
  constexpr int kCnfPrecedence[] = {0x0040, 0x0080, 0x0010, 0x0020,
                                    0x0004, 0x0008, 0x0001, 0x0002,
                                    0x0100, 0x0200, 0x0400, 0x0800};
  auto priority = [&](int type) {
    auto found =
        std::find(std::begin(kCnfPrecedence), std::end(kCnfPrecedence), type);
    return found - std::begin(kCnfPrecedence);
  };
  for (auto& style : styles) {
    std::stable_sort(style.conditionals.begin(), style.conditionals.end(),
                     [&](const auto& a, const auto& b) {
                       return priority(a.type) < priority(b.type);
                     });
  }
  std::vector<uint8_t> state(styles.size());
  for (size_t i = 0; i < styles.size(); i++) {
    ResolveStyle(styles, state, i);
  }
  return styles;
}

std::vector<model::Style> ReadStyles(Bytes table, Fib::Range r,
                                     const std::vector<std::string>& fonts) {
  return FinishStyles(ReadStyleSheet(table, r, fonts));
}

namespace {
// Decodes a NUL-terminated ANSI run in the document code page.
std::string DecodeAnsiName(Bytes b, uint32_t codepage) {
  std::string out;
  for (size_t i = 0; i < b.size();) {
    if (!b[i])
      break;
    size_t used = base::DecodeAnsiChar(codepage, b.subspan(i), out);
    if (used == 0) {
      out += "\xEF\xBF\xBD";  // U+FFFD
      used = 1;
    }
    i += used;
  }
  return out;
}
}  // namespace

std::vector<std::string> ReadLegacyFonts(Bytes word, Fib::Range r,
                                         uint32_t codepage,
                                         std::vector<std::string>* warnings) {
  std::vector<std::string> out;
  if (!r.size)
    return out;
  if (!Valid(word, r) || r.size < 3) {
    if (warnings)
      warnings->push_back("invalid Word 6/95 font table");
    return out;
  }
  Bytes b = word.subspan(r.offset, r.size);
  // The table opens with its own total byte count, then cbFfnM1-sized
  // entries: flags, wWeight, chs, ixchAlt and the ANSI name.
  size_t p = 2;
  while (p < b.size()) {
    const size_t cb_ffn_m1 = b[p];
    if (cb_ffn_m1 < 6 || cb_ffn_m1 + 1 > b.size() - p) {
      if (warnings)
        warnings->push_back("Word 6/95 font table has a malformed entry");
      break;
    }
    out.push_back(DecodeAnsiName(b.subspan(p + 6, cb_ffn_m1 - 5), codepage));
    p += cb_ffn_m1 + 1;
  }
  return out;
}

std::vector<model::Style> ReadLegacyStyles(
    Bytes word, Fib::Range r, const std::vector<std::string>& fonts,
    uint32_t codepage, std::vector<std::string>* warnings) {
  std::vector<model::Style> out;
  if (!r.size)
    return out;
  const char* kInvalid = "invalid Word 6/95 stylesheet";
  if (!Valid(word, r) || r.size < 6) {
    if (warnings)
      warnings->push_back(kInvalid);
    return FinishStyles(std::move(out));
  }
  Bytes b = word.subspan(r.offset, r.size);
  const size_t info = U16(b, 0);
  const unsigned count = U16(b, 2), base_size = U16(b, 4);
  if (info < 4 || 2 + info > b.size() || count > 4096 || base_size != 8) {
    if (warnings)
      warnings->push_back(kInvalid);
    return FinishStyles(std::move(out));
  }
  size_t p = 2 + info;
  out.resize(count);
  std::string partial_style;
  bool malformed_name = false;
  // Word 6/95 STSHI stores its standard CHP font indexes after the 12-byte
  // fixed fields. Some files contain only the first of the three slots.
  constexpr size_t kStandardFontOffset = 14;
  const int standard_font =
      info >= kStandardFontOffset && U16(b, kStandardFontOffset) < fonts.size()
          ? U16(b, kStandardFontOffset)
          : -1;
  for (unsigned i = 0; i < count && p + 2 <= b.size(); i++) {
    const size_t len = U16(b, p);
    p += 2;
    if (len > b.size() - p) {
      if (warnings)
        warnings->push_back("Word 6/95 stylesheet has a truncated record");
      break;
    }
    Bytes d = b.subspan(p, len);
    p += len;
    if (len < base_size) {
      if (len && warnings)
        warnings->push_back("Word 6/95 stylesheet has a truncated record");
      continue;  // Zero-length empty slot keeps its index in the sheet.
    }
    auto& s = out[i];
    s.type = U16(d, 2) & 15;
    s.base = U16(d, 2) >> 4;
    s.next = U16(d, 4) >> 4;
    if (s.base == 0x0fff)
      s.base = -1;
    size_t x = base_size;
    if (x >= d.size()) {
      if (!malformed_name && warnings)
        warnings->push_back("Word 6/95 style record has a malformed name");
      malformed_name = true;
      continue;
    }
    const size_t name_len = d[x];
    if (x + 1 + name_len + 1 > d.size()) {
      if (!malformed_name && warnings)
        warnings->push_back("Word 6/95 style record has a malformed name");
      malformed_name = true;
      continue;
    }
    s.name = DecodeAnsiName(d.subspan(x + 1, name_len), codepage);
    x += 1 + name_len + 1;
    if (x & 1)
      x++;  // Parity-align relative to the record start (base_size is even).
    const unsigned upx = U16(d, 4) & 15;
    for (unsigned j = 0; j < upx; j++) {
      if (x & 1)
        x++;
      if (x + 2 > d.size()) {
        if (warnings)
          warnings->push_back("Word 6/95 style record has a malformed UPX");
        break;
      }
      const size_t n = U16(d, x);
      x += 2;
      if (n > d.size() - x) {
        if (warnings)
          warnings->push_back("Word 6/95 style record has a malformed UPX");
        break;
      }
      Bytes payload = d.subspan(x, n);
      x += n;
      const bool paragraph_upx = s.type == 1 && j == 0;
      Bytes grpprl = payload;
      if (paragraph_upx) {
        if (n < 2)
          continue;  // The PAPX UPX leads with its istd.
        grpprl = payload.subspan(2);
      }
      std::vector<uint8_t> normalized;
      if (ScanLegacySprms(grpprl, normalized) == LegacySprmScan::kStopped &&
          partial_style.empty())
        partial_style = s.name;
      Walk(normalized, [&](uint16_t op, Bytes v) {
        if (paragraph_upx)
          ApplyPara(s.paragraph, op, v);
        else
          ApplyRun(s.run, op, v, fonts);
      });
    }
  }
  if (!out.empty() && out[0].type == 1 && out[0].base == -1 &&
      out[0].run.font.empty() && standard_font >= 0)
    out[0].run.font = fonts[standard_font];
  if (!partial_style.empty() && warnings)
    warnings->push_back("Word 6/95 style '" + partial_style +
                        "' has unsupported sprms");
  return FinishStyles(std::move(out));
}

model::Run InheritedRun(const model::Document& doc, int style) {
  if (style >= 0 && static_cast<size_t>(style) < doc.styles.size()) {
    model::Run value = doc.styles[style].resolved_run;
    // Inherited properties are read for ToggleOperand evaluation, but are
    // represented by the paragraph style in OOXML rather than direct rPr.
    value.font.clear();
    value.size = 0;
    value.underline_set = false;
    value.super_sub_set = false;
    value.spacing_set = false;
    value.scale_set = false;
    value.color = -1;
    value.highlight = -1;
    value.shade = -1;
    value.emphasis = -1;
    value.shade_fore = -1;
    value.border = {};
    value.bold_set = value.italic_set = value.strike_set = false;
    value.double_strike_set = value.hidden_set = false;
    value.shadow_set = value.emboss_set = value.imprint_set = false;
    value.outline_set = value.small_caps_set = value.caps_set = false;
    return value;
  }
  return {};
}
}  // namespace doccraft::doc::detail
