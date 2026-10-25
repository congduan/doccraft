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

#include "doc/format/sprm.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "doc/format/sprm_codes.h"

namespace doccraft::doc::detail {
// Windows LCID values ([MS-LCID]); several LCIDs share one BCP-47 tag.
std::string LanguageTag(uint16_t lid) {
  // Common Windows LIDs; retain uncommon IDs as a private BCP 47 tag.
  switch (lid) {
    case 0x0409:
      return "en-US";
    case 0x0809:
      return "en-GB";
    case 0x0404:
      return "zh-TW";
    case 0x0804:
      return "zh-CN";
    case 0x0C04:
      return "zh-HK";
    case 0x0411:
      return "ja-JP";
    case 0x0412:
      return "ko-KR";
    case 0x0407:
      return "de-DE";
    case 0x040C:
      return "fr-FR";
    case 0x040A:
      return "es-ES";
    case 0x0410:
      return "it-IT";
    case 0x0419:
      return "ru-RU";
    case 0x0416:
      return "pt-BR";
    case 0x0816:
      return "pt-PT";
    case 0x0401:
      return "ar-SA";
    case 0x040D:
      return "he-IL";
    case 0x0439:
      return "hi-IN";
  }
  char tag[16];
  std::snprintf(tag, sizeof(tag), "x-lcid-%04x", lid);
  return tag;
}

int Ico(unsigned index) {
  // Ico is a 4-bit index into Word's fixed palette; 0 means "auto".
  static constexpr int palette[16] = {-1,       0x000000, 0xff0000, 0xffff00,
                                      0x008000, 0xff00ff, 0x0000ff, 0x00ffff,
                                      0xffffff, 0x800000, 0x808000, 0x008000,
                                      0x808000, 0x000080, 0x008080, 0x808080};
  return index < 16 ? palette[index] : -1;
}

bool Toggle(bool old, uint8_t v) {
  return v == 0x80 ? old : v == 0x81 ? !old : v != 0;
}

bool ReadBorder(model::Border& b, Bytes v, bool old) {
  size_t start = old ? 0 : 1;
  if (v.size() != (old ? 4u : 9u) || (!old && v[0] != 8))
    return false;
  b.set = true;
  b.color = old ? -1 : static_cast<int>(U32(v, start));
  b.width = v[start + (old ? 0 : 4)];
  b.type = v[start + (old ? 1 : 5)];
  uint8_t flags = v[start + (old ? 3 : 6)];
  b.space = flags & 31;
  b.shadow = (flags & 32) != 0;
  b.frame = (flags & 64) != 0;
  return true;
}

bool ReadShade(int& back, int& fore, int& pattern, Bytes v) {
  if (v.size() != 11 || v[0] != 10)
    return false;
  fore = static_cast<int>(U32(v, 1));
  back = static_cast<int>(U32(v, 5));
  pattern = U16(v, 9);
  return true;
}

bool TableBorder(model::Border& b, Bytes v, bool old) {
  if (v.size() == (old ? 4u : 8u) && U32(v, 0) == kUndefinedU32 &&
      (old || U32(v, 4) == kUndefinedU32)) {
    b = {};
    b.set = true;
    return true;
  }
  if (old)
    return ReadBorder(b, v, true);
  if (v.size() != 8)
    return false;
  b.set = true;
  b.color = static_cast<int>(U32(v, 0));
  b.width = v[4];
  b.type = v[5];
  b.space = v[6] & 31;
  b.shadow = (v[6] & 32) != 0;
  b.frame = (v[6] & 64) != 0;
  return true;
}

bool TableShade(model::CellProperties& c, Bytes v) {
  if (v.size() != 10)
    return false;
  c.shade_fore = static_cast<int>(U32(v, 0));
  c.shade = static_cast<int>(U32(v, 4));
  c.shade_pattern = U16(v, 8);
  return true;
}

bool TableRange(model::RowProperties& row, Bytes v, size_t offset,
                size_t& first, size_t& limit) {
  if (v.size() < offset + 2)
    return false;
  first = v[offset];
  limit = v[offset + 1];
  return first < limit && limit <= row.cells.size();
}

bool ApplyTable(model::Table& table, model::RowProperties& row, uint16_t op,
                Bytes v) {
  auto& cells = row.cells;
  if (op == kSprmTDefTable || op == kSprmTDefTableObsolete) {
    if (v.size() < 5)
      return false;
    size_t count = v[2];
    if (count > 63 || 3 + 2 * (count + 1) > v.size())
      return false;
    row.cells.assign(count, {});
    table.columns.clear();
    for (size_t i = 0; i < count; i++) {
      int width = S16(v, 3 + 2 * (i + 1)) - S16(v, 3 + 2 * i);
      if (width < 0)
        return false;
      table.columns.push_back(width);
    }
    if (op == kSprmTDefTableObsolete)
      return true;  // obsolete BRC10 cells are not TC80 cells
    size_t start = 3 + 2 * (count + 1);
    if ((v.size() - start) % 20)
      return false;
    for (size_t i = 0; i < count && start + 20 <= v.size(); i++, start += 20) {
      auto& c = cells[i];
      uint16_t flags = U16(v, start);
      c.width = U16(v, start + 2) ? U16(v, start + 2) : table.columns[i];
      c.width_type = (flags >> 9) & 7;
      unsigned horizontal = flags & 3;
      c.horizontal_merge = horizontal >= 2 ? 2 : horizontal;
      unsigned vertical = (flags >> 5) & 3;
      c.vertical_merge = vertical == 3 ? 2 : vertical;
      unsigned flow = (flags >> 2) & 7;
      if (flow == 0 || flow == 1 || flow == 3 || flow == 4 || flow == 5)
        c.text_direction = flow ? static_cast<int>(flow) : -1;
      for (size_t side = 0; side < 4; side++) {
        Bytes brc = v.subspan(start + 4 + side * 4, 4);
        if (U32(brc, 0) != kUndefinedU32 && U32(brc, 0) != 0)
          TableBorder(c.borders[side], brc, true);
      }
    }
    return true;
  }
  if (op == kSprmTFCantSplit90 || op == kSprmTFCantSplit) {
    row.cant_split = v[0] != 0;
    return true;
  }
  if (op == kSprmTTableHeader) {
    row.header = v[0] != 0;
    return true;
  }
  if (op == kSprmTFKeepFollow) {
    table.keep_next = v[0] != 0;
    return true;
  }
  if (op == kSprmTFNoAllowOverlap) {
    table.no_overlap = v[0] != 0;
    return true;
  }
  if (op == kSprmTDyaRowHeight) {
    row.height = S16(v, 0);
    return true;
  }
  if (op == kSprmTTableWidth) {
    if (v.size() != 3 || (v[0] != 0 && v[0] != 2 && v[0] != 3))
      return false;
    table.width_type = v[0];
    table.width = U16(v, 1);
    return true;
  }
  if (op == kSprmTFAutofit) {
    table.auto_fit = v[0] != 0;
    return true;
  }
  if (op == kSprmTWidthIndent) {
    if (v.size() != 3 || v[0] != 3)
      return false;
    table.indent = U16(v, 1);
    return true;
  }
  if (op == kSprmTIstd) {
    table.style = U16(v, 0);
    return true;
  }
  if (op == kSprmTDxaAbs) {
    if (S16(v, 0) < -kMaxTwips)
      return false;
    table.tblp_x = S16(v, 0);
    return true;
  }
  if (op == kSprmTDyaAbs) {
    if (S16(v, 0) < -kMaxTwips)
      return false;
    table.tblp_y = S16(v, 0);
    return true;
  }
  if (op == kSprmTDxaFromText || op == kSprmTDyaFromText ||
      op == kSprmTDxaFromTextRight || op == kSprmTDyaFromTextBottom) {
    if (U16(v, 0) > kMaxTwips)
      return false;
    size_t slot = op == kSprmTDxaFromText        ? 0
                  : op == kSprmTDyaFromText      ? 1
                  : op == kSprmTDxaFromTextRight ? 2
                                                 : 3;
    table.from_text[slot] = U16(v, 0);
    return true;
  }
  if (op == kSprmTJc90 || op == kSprmTJc) {
    if (U16(v, 0) > 2)
      return false;
    table.align = U16(v, 0);
    return true;
  }
  if (op == kSprmTDxaLeft) {
    table.left_origin = S16(v, 0);
    table.indent = table.left_origin - table.gap_half;
    return true;
  }
  if (op == kSprmTDxaGapHalf) {
    if (S16(v, 0) < 0)
      return false;
    table.gap_half = S16(v, 0);
    if (table.left_origin >= 0)
      table.indent = table.left_origin - table.gap_half;
    return true;
  }
  if (op == kSprmTCellSpacingDefault) {
    if (v.size() != 7 || v[0] != 6 || v[1] != 0 || v[2] != 1 || v[3] != 15 ||
        (v[4] != 3 && v[4] != 0 && v[4] != 0x13) || S16(v, 5) < 0 ||
        S16(v, 5) > 15840)
      return false;
    table.cell_spacing = v[4] == 0 ? 0 : S16(v, 5);
    return true;
  }
  if (op == kSprmTFBiDi || op == kSprmTFBiDi90) {
    table.bidi = U16(v, 0) != 0;
    return true;
  }
  if (op == kSprmTSetShdTable) {
    return ReadShade(table.shade, table.shade_fore, table.shade_pattern, v);
  }
  if (op == kSprmTTableBorders80 || op == kSprmTTableBorders) {
    bool old = op == kSprmTTableBorders80;
    size_t size = old ? 4 : 8;
    if (v.size() != 1 + 6 * size || v[0] != 6 * size)
      return false;
    for (size_t i = 0; i < 6; i++) {
      Bytes b = v.subspan(1 + i * size, size);
      if (!old || U32(b, 0) != kUndefinedU32)
        if (!TableBorder(table.borders[i], b, old))
          return false;
    }
    return true;
  }
  if (op == kSprmTInsert) {
    if (v.size() != 4 || !v[1] || v[0] > cells.size() ||
        cells.size() + v[1] > 63)
      return false;
    cells.insert(cells.begin() + v[0], v[1], model::CellProperties{});
    table.columns.insert(
        table.columns.begin() + std::min<size_t>(v[0], table.columns.size()),
        v[1], U16(v, 2));
    return true;
  }
  if (op == kSprmTDelete) {
    size_t first, limit;
    if (!TableRange(row, v, 0, first, limit) || limit - first == cells.size())
      return false;
    cells.erase(cells.begin() + first, cells.begin() + limit);
    if (limit <= table.columns.size())
      table.columns.erase(table.columns.begin() + first,
                          table.columns.begin() + limit);
    return true;
  }
  if (op == kSprmTDxaCol) {
    size_t first, limit;
    if (v.size() != 4 || !TableRange(row, v, 0, first, limit))
      return false;
    for (size_t i = first; i < limit; i++) {
      cells[i].width = U16(v, 2);
      if (i < table.columns.size())
        table.columns[i] = U16(v, 2);
    }
    return true;
  }
  if (op == kSprmTCellWidth) {
    size_t first, limit;
    if (v.size() != 6 || v[0] != 5 || !TableRange(row, v, 1, first, limit) ||
        (v[3] != 0 && v[3] != 2 && v[3] != 3))
      return false;
    for (size_t i = first; i < limit; i++) {
      cells[i].width_type = v[3];
      cells[i].width = U16(v, 4);
    }
    return true;
  }
  if (op == kSprmTMerge || op == kSprmTSplit) {
    size_t first, limit;
    if (!TableRange(row, v, 0, first, limit))
      return false;
    for (size_t i = first; i < limit; i++)
      cells[i].horizontal_merge = op == kSprmTSplit ? 0 : i == first ? 2 : 1;
    return true;
  }
  if (op == kSprmTVertMerge) {
    if (v.size() != 3 || v[0] != 2 || v[1] >= cells.size() ||
        (v[2] != 0 && v[2] != 1 && v[2] != 3))
      return false;
    cells[v[1]].vertical_merge = v[2] == 3 ? 2 : v[2];
    return true;
  }
  if (op == kSprmTTextFlow) {
    size_t first, limit;
    if (!TableRange(row, v, 0, first, limit) || v.size() != 4 ||
        U16(v, 2) > 5 || U16(v, 2) == 2)
      return false;
    for (size_t i = first; i < limit; i++)
      cells[i].text_direction = U16(v, 2);
    return true;
  }
  if (op == kSprmTVertAlign || op == kSprmTFCellNoWrap ||
      op == kSprmTCellFHideMark) {
    size_t first, limit;
    if (v.size() != 4 || v[0] != 3 || !TableRange(row, v, 1, first, limit))
      return false;
    if (op == kSprmTVertAlign && v[3] > 2)
      return false;
    for (size_t i = first; i < limit; i++) {
      if (op == kSprmTVertAlign)
        cells[i].vertical_align = v[3];
      else if (op == kSprmTFCellNoWrap)
        cells[i].no_wrap = v[3] != 0;
      else
        cells[i].hide_mark = v[3] != 0;
    }
    return true;
  }
  if (op == kSprmTFitText) {
    size_t first, limit;
    if (v.size() != 3 || !TableRange(row, v, 0, first, limit))
      return false;
    for (size_t i = first; i < limit; i++)
      cells[i].fit_text = v[2] != 0;
    return true;
  }
  if (op == kSprmTCellPadding || op == kSprmTCellPaddingDefault) {
    if (v.size() != 7 || v[0] != 6 || (v[3] & ~15) || (v[4] != 0 && v[4] != 3))
      return false;
    if (op == kSprmTCellPaddingDefault && (v[1] != 0 || v[2] != 1))
      return false;
    size_t first = 0, limit = cells.size();
    if (op == kSprmTCellPadding && !TableRange(row, v, 1, first, limit))
      return false;
    int width = v[4] == 3 ? U16(v, 5) : 0;
    if (width > kMaxTwips)
      return false;
    for (size_t i = first; i < limit; i++)
      for (size_t side = 0; side < 4; side++)
        if (v[3] & (1 << side))
          cells[i].margins[side] = width;
    return true;
  }
  if (op == kSprmTSetShd || op == kSprmTSetShdOdd) {
    size_t first, limit;
    if (v.size() != 13 || v[0] != 12 || !TableRange(row, v, 1, first, limit))
      return false;
    for (size_t i = first; i < limit; i++)
      if (op == kSprmTSetShd || ((i - first) & 1) == 0)
        if (!TableShade(cells[i], v.subspan(3, 10)))
          return false;
    return true;
  }
  if (op == kSprmTDefTableShd80) {
    if (v.empty() || static_cast<size_t>(v[0]) + 1 != v.size() || v[0] % 2)
      return false;
    // SHD80 carries palette indices; a later modern SHD operand overrides it.
    for (size_t i = 0; i < v[0] / 2 && i < cells.size(); i++) {
      unsigned sh = U16(v, 1 + 2 * i);
      cells[i].shade_fore = Ico(sh & 31);
      cells[i].shade = Ico((sh >> 5) & 31);
      cells[i].shade_pattern = (sh >> 10) & 63;
    }
    return true;
  }
  if (op == kSprmTDefTableShd || op == kSprmTDefTableShd2nd ||
      op == kSprmTDefTableShd3rd || op == kSprmTDefTableShdRaw ||
      op == kSprmTDefTableShdRaw2nd || op == kSprmTDefTableShdRaw3rd) {
    size_t first =
        (op == kSprmTDefTableShd2nd || op == kSprmTDefTableShdRaw2nd)   ? 22
        : (op == kSprmTDefTableShd3rd || op == kSprmTDefTableShdRaw3rd) ? 44
                                                                        : 0;
    if (v.empty() || static_cast<size_t>(v[0]) + 1 != v.size() || v[0] % 10)
      return false;
    for (size_t i = 0; i < v[0] / 10 && first + i < cells.size(); i++)
      if (!TableShade(cells[first + i], v.subspan(1 + i * 10, 10)))
        return false;
    return true;
  }
  if (op == kSprmTSetBrc || op == kSprmTSetBrc80) {
    size_t first, limit;
    if (v.size() != (op == kSprmTSetBrc ? 12u : 8u) || v[0] != v.size() - 1 ||
        !TableRange(row, v, 1, first, limit) || (v[3] & ~63))
      return false;
    bool old = op == kSprmTSetBrc80;
    for (size_t i = first; i < limit; i++)
      for (size_t side = 0; side < 6; side++)
        if (v[3] & (1 << side))
          if (!TableBorder(cells[i].borders[side], v.subspan(4, old ? 4 : 8),
                           old))
            return false;
    return true;
  }
  return true;
}

void ApplyRun(model::Run& r, uint16_t op, Bytes v,
              const std::vector<std::string>& fonts) {
  auto flag = [&](bool& x, bool& set) {
    x = Toggle(x, v[0]);
    set = true;
  };
  switch (op) {
    case kSprmCFRMarkDel:
      r.revision_delete = Toggle(r.revision_delete, v[0]);
      break;
    case kSprmCFRMarkIns:
      r.revision_insert = Toggle(r.revision_insert, v[0]);
      break;
    case kSprmCPropRMark:
      if (v.size() > 1)
        r.property_revision = v[1] != 0;
      break;
    case kSprmCPicLocation:
      r.object_offset = static_cast<int>(U32(v, 0));
      break;
    case kSprmCFData:
      r.binary_data = v[0] != 0;
      break;
    case kSprmCFOle2:
      r.ole_object = v[0] != 0;
      break;
    case kSprmCFObj:
      r.embedded_object = v[0] != 0;
      break;
    case kSprmCFBold:
      flag(r.bold, r.bold_set);
      break;
    case kSprmCFItalic:
      flag(r.italic, r.italic_set);
      break;
    case kSprmCFStrike:
      flag(r.strike, r.strike_set);
      break;
    case kSprmCFDStrike:
      flag(r.double_strike, r.double_strike_set);
      break;
    case kSprmCFVanish:
      flag(r.hidden, r.hidden_set);
      break;
    case kSprmCFFldVanish:
      flag(r.field_hidden, r.field_hidden_set);
      break;
    case kSprmCFWebHidden:
      flag(r.web_hidden, r.web_hidden_set);
      break;
    case kSprmCFSpecVanish:
      flag(r.spec_vanish, r.spec_vanish_set);
      break;
    case kSprmCFBiDi:
      flag(r.rtl, r.rtl_set);
      break;
    case kSprmCFUsePgsuSettings:
      flag(r.snap_to_grid, r.snap_to_grid_set);
      break;
    case kSprmCFNoProof:
      flag(r.no_proof, r.no_proof_set);
      break;
    case kSprmCFComplexScripts:
      flag(r.complex_script, r.complex_script_set);
      break;
    case kSprmCSfxText:
      r.text_effect = v[0];
      break;
    case kSprmCIdctHint:
      r.font_hint = v[0];
      break;
    case kSprmCCvUl:
      r.underline_color = static_cast<int>(U32(v, 0));
      break;
    case kSprmCLidBi:
      r.lang_cs = LanguageTag(U16(v, 0));
      break;
    case kSprmCRgLid0_80:
    case kSprmCRgLid0:
      r.lang = LanguageTag(U16(v, 0));
      break;
    case kSprmCRgLid1_80:
    case kSprmCRgLid1:
      r.lang_east_asia = LanguageTag(U16(v, 0));
      break;
    case kSprmCFOutline:
      flag(r.outline, r.outline_set);
      break;
    case kSprmCFShadow:
      flag(r.shadow, r.shadow_set);
      break;
    case kSprmCFSmallCaps:
      flag(r.small_caps, r.small_caps_set);
      break;
    case kSprmCFCaps:
      flag(r.caps, r.caps_set);
      break;
    case kSprmCFImprint:
      flag(r.imprint, r.imprint_set);
      break;
    case kSprmCFEmboss:
      flag(r.emboss, r.emboss_set);
      break;
    case kSprmCKul:
      r.underline = v[0];
      r.underline_set = true;
      break;
    case kSprmCKcd:
      if (v[0] < 5)
        r.emphasis = v[0];
      break;
    case kSprmCFSdtVanish:
      flag(r.hidden, r.hidden_set);
      break;  // hidden with XML delimiters
    case kSprmCHps:
      r.size = U16(v, 0);
      break;
    case kSprmCHpsBi:
      r.size_cs = U16(v, 0);
      break;
    case kSprmCFBoldBi:
      flag(r.bold_cs, r.bold_cs_set);
      break;
    case kSprmCFItalicBi:
      flag(r.italic_cs, r.italic_cs_set);
      break;
    case kSprmCHpsKern:
      r.kern = S16(v, 0);
      break;
    case kSprmCHpsPos:
      r.position = S16(v, 0);
      r.position_set = true;
      break;
    case kSprmCIss:
      r.super_sub = v[0];
      r.super_sub_set = true;
      break;
    case kSprmCDxaSpace:
      r.spacing = S16(v, 0);
      r.spacing_set = true;
      break;
    case kSprmCCharScale:
      r.scale = U16(v, 0);
      r.scale_set = true;
      break;
    case kSprmCIco:
      r.color = v[0];
      r.color_rgb = false;
      break;
    case kSprmCIcoBi:
      r.color_cs = U16(v, 0);
      break;
    case kSprmCShd80: {
      unsigned sh = U16(v, 0);
      r.shade_fore = Ico(sh & 31);
      r.shade = Ico((sh >> 5) & 31);
      r.shade_pattern = (sh >> 10) & 63;
      break;
    }
    case kSprmCCv:
      r.color = U32(v, 0);
      r.color_rgb = true;
      break;
    case kSprmCHighlight:
      r.highlight = v[0];
      break;
    case kSprmCShd:
      ReadShade(r.shade, r.shade_fore, r.shade_pattern, v);
      break;
    case kSprmCBrc80:
      ReadBorder(r.border, v, true);
      break;
    case kSprmCBrc:
      ReadBorder(r.border, v, false);
      break;
    case kSprmCIstd:
      r.style = U16(v, 0);
      break;
    case kSprmCRgFtc0:
    case kSprmCRgFtc1:
    case kSprmCRgFtc2:
    case kSprmCFtcBi: {
      unsigned n = U16(v, 0);
      if (n < fonts.size()) {
        if (op == kSprmCRgFtc1)
          r.font_east_asia = fonts[n];
        else if (op == kSprmCRgFtc2)
          r.font_hansi = fonts[n];
        else if (op == kSprmCFtcBi)
          r.font_cs = fonts[n];
        else
          r.font = fonts[n];
      }
      break;
    }
    case kSprmCSymbol: {
      unsigned n = U16(v, 0);
      if (n < fonts.size()) {
        r.symbol_font = fonts[n];
        r.symbol_character = U16(v, 2);
      }
      break;
    }
    default:
      break;
  }
}

void ApplyPara(model::Paragraph& p, uint16_t op, Bytes v) {
  if (op == kSprmPChgTabs || op == kSprmPChgTabsPapx) {
    if (v.size() < 3)
      return;
    size_t x = 1, deleted = v[x++],
           delbytes = (op == kSprmPChgTabs ? 4 : 2) * deleted;
    if (delbytes > v.size() - x)
      return;
    x += delbytes;
    if (x >= v.size())
      return;
    size_t added = v[x++];
    if (added > 64 || added * 3 > v.size() - x)
      return;
    for (size_t i = 0; i < added; i++) {
      int pos = S16(v, x + 2 * i), tbd = v[x + 2 * added + i];
      p.tabs.push_back({pos, tbd & 7, (tbd >> 3) & 7});
    }
    return;
  }
  if (op >= kSprmPBrcTop && op <= kSprmPBrcBar) {
    size_t i = op - kSprmPBrcTop;
    ReadBorder(i < 4 ? p.borders[i] : p.extra_borders[i - 4], v, false);
    return;
  }
  if ((op >= kSprmPBrcTop80 && op <= kSprmPBrcBetween80) ||
      op == kSprmPBrcBar80) {
    size_t i = op == kSprmPBrcBar80 ? 5 : op - kSprmPBrcTop80;
    ReadBorder(i < 4 ? p.borders[i] : p.extra_borders[i - 4], v, true);
    return;
  }
  if (op == kSprmPShd) {
    ReadShade(p.shade, p.shade_fore, p.shade_pattern, v);
    return;
  }
  switch (op) {
    case kSprmPIstd:
      p.style = U16(v, 0);
      break;
    case kSprmPFNoLineNumb:
      p.no_line_number = v[0] != 0;
      p.no_line_number_set = true;
      break;
    case kSprmPFNoAutoHyph:
      p.suppress_hyphenation = v[0] != 0;
      p.suppress_hyphenation_set = true;
      break;
    case kSprmPFWidowControl:
      p.widow_control = v[0] != 0;
      p.widow_control_set = true;
      break;
    case kSprmPFKinsoku:
      p.kinsoku = v[0] != 0;
      p.kinsoku_set = true;
      break;
    case kSprmPFOverflowPunct:
      p.overflow_punct = v[0] != 0;
      p.overflow_punct_set = true;
      break;
    case kSprmPFTopLinePunct:
      p.top_line_punct = v[0] != 0;
      p.top_line_punct_set = true;
      break;
    case kSprmPFAutoSpaceDE:
      p.auto_space_de = v[0] != 0;
      p.auto_space_de_set = true;
      break;
    case kSprmPFAutoSpaceDN:
      p.auto_space_dn = v[0] != 0;
      p.auto_space_dn_set = true;
      break;
    case kSprmPFWordWrap:
      p.word_wrap = v[0] != 0;
      p.word_wrap_set = true;
      break;
    case kSprmPFBiDi:
      p.bidi = v[0] != 0;
      p.bidi_set = true;
      break;
    case kSprmPFUsePgsuSettings:
      p.snap_to_grid = v[0] != 0;
      p.snap_to_grid_set = true;
      break;
    case kSprmPFAdjustRight:
      p.adjust_right = v[0] != 0;
      p.adjust_right_set = true;
      break;
    case kSprmPFDyaBeforeAuto:
      p.before_auto = v[0] != 0;
      p.before_auto_set = true;
      break;
    case kSprmPFDyaAfterAuto:
      p.after_auto = v[0] != 0;
      p.after_auto_set = true;
      break;
    case kSprmPFContextualSpacing:
      p.contextual_spacing = v[0] != 0;
      p.contextual_spacing_set = true;
      break;
    case kSprmPFMirrorIndents:
      p.mirror_indents = v[0] != 0;
      p.mirror_indents_set = true;
      break;
    case kSprmPJc80:
    case kSprmPJc:
      p.align = v[0];
      break;
    case kSprmPDxaLeft80:
    case kSprmPDxaLeft:
      p.left = S16(v, 0);
      p.left_set = true;
      break;
    case kSprmPDxaRight80:
    case kSprmPDxaRight:
      p.right = S16(v, 0);
      p.right_set = true;
      break;
    case kSprmPDxaLeft180:
    case kSprmPDxaLeft1:
      p.first = S16(v, 0);
      p.first_set = true;
      break;
    case kSprmPDyaBefore:
      p.before = U16(v, 0);
      break;
    case kSprmPDyaAfter:
      p.after = U16(v, 0);
      break;
    case kSprmPDyaLine:
      p.line = S16(v, 0);
      p.line_auto = U16(v, 2) != 0;
      break;
    case kSprmPFKeep:
      p.keep_lines = v[0] != 0;
      p.keep_lines_set = true;
      break;
    case kSprmPFKeepFollow:
      p.keep_next = v[0] != 0;
      p.keep_next_set = true;
      break;
    case kSprmPFPageBreakBefore:
      p.page_break_before = v[0] != 0;
      p.page_break_before_set = true;
      break;
    case kSprmPOutLvl:
      p.outline = v[0];
      break;
    case kSprmPIlfo:
      p.list_id = U16(v, 0);
      break;
    case kSprmPIlvl:
      p.list_level = v[0];
      break;
    case kSprmPWr:
      if (v[0] <= 5)
        p.frame_wrap = v[0];
      break;
    case kSprmPPc:
      p.frame_anchor = v[0];
      break;
    case kSprmPFLocked:
      p.frame_lock = v[0] != 0;
      p.frame_lock_set = true;
      break;
    case kSprmPDxaAbs:
      if (S16(v, 0) >= -kMaxTwips)
        p.frame_x = S16(v, 0);
      break;
    case kSprmPDyaAbs:
      if (S16(v, 0) >= -kMaxTwips)
        p.frame_y = S16(v, 0);
      break;
    case kSprmPDxaWidth:
      if (U16(v, 0) <= kMaxTwips)
        p.frame_width = U16(v, 0);
      break;
    case kSprmPWHeightAbs: {
      unsigned h = U16(v, 0) & kFrameHeightMask;
      if (h <= kMaxTwips) {
        p.frame_height = h;
        p.frame_height_exact = (U16(v, 0) & kFrameHeightExact) != 0;
      }
      break;
    }
    case kSprmPDyaFromText:
      if (U16(v, 0) <= kMaxTwips)
        p.frame_v_space = U16(v, 0);
      break;
    case kSprmPDxaFromText:
      if (U16(v, 0) <= kMaxTwips)
        p.frame_h_space = U16(v, 0);
      break;
    case kSprmPShd80: {
      unsigned sh = U16(v, 0);
      p.shade_fore = Ico(sh & 31);
      p.shade = Ico((sh >> 5) & 31);
      p.shade_pattern = (sh >> 10) & 63;
      break;
    }
    case kSprmPIncLvl:
      p.list_level_inc = static_cast<int8_t>(v[0]);
      break;
    case kSprmPNest80:
    case kSprmPNest:
      p.nest = S16(v, 0);
      p.nest_set = true;
      break;
    default:
      break;
  }
}

bool ApplySection(model::Section& s, uint16_t op, Bytes v) {
  if (op >= kSprmSBrcTop80 && op <= kSprmSBrcRight80)
    return ReadBorder(s.page_borders[op - kSprmSBrcTop80], v, true);
  if (op >= kSprmSBrcTop && op <= kSprmSBrcRight)
    return ReadBorder(s.page_borders[op - kSprmSBrcTop], v, false);
  if (op == kSprmSDxaColWidth || op == kSprmSDxaColSpacing) {
    if (v[0] > 43)
      return false;
    model::ColumnSpec* spec = nullptr;
    for (auto& c : s.column_specs)
      if (c.index == v[0])
        spec = &c;
    if (!spec) {
      s.column_specs.push_back({static_cast<int>(v[0]), -1, -1});
      spec = &s.column_specs.back();
    }
    if (op == kSprmSDxaColSpacing) {
      spec->space = U16(v, 1);
      return true;
    }
    spec->width = U16(v, 1);
    return true;
  }
  switch (op) {
    case kSprmSDmBinFirst:
      if (U16(v, 0) != kUndefinedU16)
        s.paper_first = U16(v, 0);
      break;
    case kSprmSDmBinOther:
      if (U16(v, 0) != kUndefinedU16)
        s.paper_other = U16(v, 0);
      break;
    case kSprmSFEndnote:
      s.endnote_at_section_end = v[0] != 0;
      break;
    case kSprmScnsPgn:
      s.chapter_separator = v[0];
      break;
    case kSprmSiHeadingPgn:
      s.chapter_heading = v[0];
      break;
    case kSprmSDxtCharSpace:
      s.grid_char_space = static_cast<int>(U32(v, 0));
      break;
    case kSprmSRncEdn:
      s.endnote_restart = v[0];
      break;
    case kSprmSNEdn:
      s.endnote_start = U16(v, 0);
      break;
    case kSprmSNfcEdnRef:
      s.endnote_format = U16(v, 0);
      break;
    case kSprmSBkc:
      s.break_type = v[0];
      s.break_specified = true;
      break;
    case kSprmSBOrientation:
      s.orientation = v[0];
      break;
    case kSprmSFTitlePage:
      s.first_page_different = v[0] != 0;
      break;
    case kSprmSFEvenlySpaced:
      s.columns_even = v[0] != 0;
      break;
    case kSprmSLBetween:
      s.columns_separator = v[0] != 0;
      break;
    case kSprmSVjc:
      if (v[0] > 3)
        return false;
      s.vertical_justify = v[0];
      break;
    case kSprmSFBiDi:
      s.bidi = v[0] != 0;
      break;
    case kSprmSFRTLGutter:
      s.rtl_gutter = v[0] != 0;
      break;
    case kSprmSCcolumns:
      if (U16(v, 0) > 43)
        return false;
      s.columns = U16(v, 0) + 1;
      break;
    case kSprmSDxaColumns:
      s.column_space = U16(v, 0);
      break;
    case kSprmSFPgnRestart:
      s.page_restart = v[0] != 0;
      break;
    case kSprmSPgnStart97:
      s.page_start = U16(v, 0);
      break;
    case kSprmSPgnStart:
      if (U32(v, 0) > 2147483646u)
        return false;
      s.page_start = static_cast<int>(U32(v, 0));
      break;
    case kSprmSNfcPgn:
      s.page_format = v[0];
      break;
    case kSprmSDyaLinePitch:
      if (U16(v, 0) < 1 || U16(v, 0) > kMaxTwips)
        return false;
      s.grid_line_pitch = U16(v, 0);
      break;
    case kSprmSClm:
      if (U16(v, 0) > 3)
        return false;
      s.grid_mode = U16(v, 0);
      break;
    case kSprmSTextFlow:
      s.text_flow = U16(v, 0);
      break;
    case kSprmSLnc:
      s.line_restart = v[0];
      break;
    case kSprmSNLnnMod:
      s.line_count_by = U16(v, 0);
      break;
    case kSprmSDxaLnn:
      s.line_distance = U16(v, 0);
      break;
    case kSprmSLnnMin:
      s.line_start = U16(v, 0);
      break;
    case kSprmSFpc:
      s.footnote_position = v[0];
      break;
    case kSprmSRncFtn:
      s.footnote_restart = v[0];
      break;
    case kSprmSNFtn:
      s.footnote_start = U16(v, 0);
      break;
    case kSprmSNfcFtnRef:
      s.footnote_format = U16(v, 0);
      break;
    case kSprmSDyaHdrTop:
      s.header_distance = U16(v, 0);
      break;
    case kSprmSDyaHdrBottom:
      s.footer_distance = U16(v, 0);
      break;
    case kSprmSXaPage:
      s.width = U16(v, 0);
      break;
    case kSprmSYaPage:
      s.height = U16(v, 0);
      break;
    case kSprmSDxaLeft:
      s.left = U16(v, 0);
      break;
    case kSprmSDxaRight:
      s.right = U16(v, 0);
      break;
    case kSprmSDyaTop:
      s.top = std::abs(S16(v, 0));
      break;
    case kSprmSDyaBottom:
      s.bottom = std::abs(S16(v, 0));
      break;
    case kSprmSDzaGutter:
      s.gutter = U16(v, 0);
      break;
    default:
      break;
  }
  return true;
}
}  // namespace doccraft::doc::detail
