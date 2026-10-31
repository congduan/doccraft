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
                                      0x800080, 0x000080, 0x008080, 0x808080};
  return index < 16 ? palette[index] : -1;
}

bool Toggle(bool old, uint8_t v) {
  return v == kToggleKeep ? old : v == kToggleInvert ? !old : v != 0;
}

bool ReadBorder(model::Border& b, Bytes v, bool old) {
  size_t start = old ? 0 : kCbPrefixSize;
  if (v.size() != (old ? kBrc80Size : kBrcPrefixedSize) ||
      (!old && v[0] != kBrcCb))
    return false;
  b.set = true;
  b.color = old ? -1 : static_cast<int>(U32(v, start));
  b.width = v[start + (old ? 0 : kBrcColorSize)];
  b.type = v[start + (old ? 1 : kBrcTypeOffset)];
  uint8_t flags = v[start + (old ? 3 : kBrcFlagsOffset)];
  b.space = flags & kBrcSpaceMask;
  b.shadow = (flags & kBrcShadow) != 0;
  b.frame = (flags & kBrcFrame) != 0;
  return true;
}

bool ReadShade(int& back, int& fore, int& pattern, Bytes v) {
  if (v.size() != kShdPrefixedSize || v[0] != kShdCb)
    return false;
  fore = static_cast<int>(U32(v, kCbPrefixSize + kShdForeOffset));
  back = static_cast<int>(U32(v, kCbPrefixSize + kShdBackOffset));
  pattern = U16(v, kCbPrefixSize + kShdPatternOffset);
  return true;
}

bool TableBorder(model::Border& b, Bytes v, bool old) {
  if (v.size() == (old ? kBrc80Size : kBrcSize) && U32(v, 0) == kUndefinedU32 &&
      (old || U32(v, 4) == kUndefinedU32)) {
    b = {};
    b.set = true;
    return true;
  }
  if (old)
    return ReadBorder(b, v, true);
  if (v.size() != kBrcSize)
    return false;
  b.set = true;
  b.color = static_cast<int>(U32(v, 0));
  b.width = v[kBrcColorSize];
  b.type = v[kBrcTypeOffset];
  b.space = v[kBrcFlagsOffset] & kBrcSpaceMask;
  b.shadow = (v[kBrcFlagsOffset] & kBrcShadow) != 0;
  b.frame = (v[kBrcFlagsOffset] & kBrcFrame) != 0;
  return true;
}

bool TableShade(model::CellProperties& c, Bytes v) {
  if (v.size() != kShdSize)
    return false;
  c.shade_fore = static_cast<int>(U32(v, kShdForeOffset));
  c.shade = static_cast<int>(U32(v, kShdBackOffset));
  c.shade_pattern = U16(v, kShdPatternOffset);
  return true;
}

bool TableRange(model::RowProperties& row, Bytes v, size_t offset,
                size_t& first, size_t& limit) {
  if (v.size() < offset + kTableRangeSize)
    return false;
  first = v[offset];
  limit = v[offset + 1];
  // [MS-DOC] 2.9.123 ItcFirstLim: itcFirst MUST be less than the row's cell
  // count, and itcLim MUST be in [itcFirst, cell count]. The empty range is
  // therefore itcFirst == itcLim with itcFirst < cell count; [count, count) is
  // a malformed operand, not a no-op, and every caller rejects it as such.
  return first <= limit && first < row.cells.size() &&
         limit <= row.cells.size();
}
// Table SPRMs are dispatch-heavy ([MS-DOC] §2.6.3). Each handler owns one
// property family and reports kUnmatched when the opcode is out of scope.
enum class OpResult { kUnmatched, kOk, kError };

OpResult ApplyTableDefinition(model::Table& table, model::RowProperties& row,
                              uint16_t op, Bytes v) {
  auto& cells = row.cells;
  if (op == kSprmTDefTable || op == kSprmTDefTableObsolete) {
    if (v.size() < kTDefMinSize)
      return OpResult::kError;
    size_t count = v[kTDefCountOffset];
    if (count > kMaxTableCells ||
        kTDefHeaderSize + kTableRangeSize * (count + 1) > v.size())
      return OpResult::kError;
    row.cells.assign(count, {});
    table.columns.clear();
    for (size_t i = 0; i < count; i++) {
      int width = S16(v, kTDefHeaderSize + kTableRangeSize * (i + 1)) -
                  S16(v, kTDefHeaderSize + kTableRangeSize * i);
      if (width < 0)
        return OpResult::kError;
      table.columns.push_back(width);
    }
    if (op == kSprmTDefTableObsolete)
      return OpResult::kOk;  // obsolete BRC10 cells are not TC80 cells
    size_t start = kTDefHeaderSize + kTableRangeSize * (count + 1);
    if ((v.size() - start) % kTDefCellSize)
      return OpResult::kError;
    for (size_t i = 0; i < count && start + kTDefCellSize <= v.size();
         i++, start += kTDefCellSize) {
      auto& c = cells[i];
      uint16_t flags = U16(v, start);
      c.width = U16(v, start + kTDefWidthOffset)
                    ? U16(v, start + kTDefWidthOffset)
                    : table.columns[i];
      c.width_type = (flags >> kTcWidthTypeShift) & kTcWidthTypeMask;
      unsigned horizontal = flags & kTcMergeMask;
      c.horizontal_merge =
          horizontal >= kTcMergeStart ? kTcMergeStart : horizontal;
      unsigned vertical = (flags >> kTcVertMergeShift) & kTcMergeMask;
      c.vertical_merge =
          vertical == kTcVertMergeRestart ? kTcMergeStart : vertical;
      unsigned flow = (flags >> kTcTextFlowShift) & kTcTextFlowMask;
      if (flow == kFlowHorizontal || flow == kFlowVertical ||
          flow == kFlowVerticalBackwards || flow == kFlowRotateLeft ||
          flow == kFlowRotateRight)
        c.text_direction = flow ? static_cast<int>(flow) : -1;
      for (size_t side = 0; side < kCellSideCount; side++) {
        Bytes brc = v.subspan(start + kTDefBordersOffset + side * kBrc80Size,
                              kBrc80Size);
        if (U32(brc, 0) != kUndefinedU32 && U32(brc, 0) != 0)
          TableBorder(c.borders[side], brc, true);
      }
    }
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyTableRowProperties(model::Table& table, model::RowProperties& row,
                                 uint16_t op, Bytes v) {
  if (op == kSprmTFCantSplit90 || op == kSprmTFCantSplit) {
    row.cant_split = v[0] != 0;
    return OpResult::kOk;
  }
  if (op == kSprmTTableHeader) {
    row.header = v[0] != 0;
    return OpResult::kOk;
  }
  if (op == kSprmTFKeepFollow) {
    table.keep_next = v[0] != 0;
    return OpResult::kOk;
  }
  if (op == kSprmTFNoAllowOverlap) {
    table.no_overlap = v[0] != 0;
    return OpResult::kOk;
  }
  if (op == kSprmTDyaRowHeight) {
    row.height = S16(v, 0);
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyTableLayout(model::Table& table, uint16_t op, Bytes v) {
  if (op == kSprmTTableWidth) {
    if (v.size() != kTableWidthSize)
      return OpResult::kError;
    // [MS-DOC] Fts: 0 = nil, 1 = auto, 2 = percent, 3 = dxa; any other value
    // "MUST be ignored" — skip this sprm, keep the rest of the row usable.
    if (v[0] != kFtsNil && v[0] != kFtsAuto && v[0] != kFtsPct &&
        v[0] != kFtsDxa) {
      return OpResult::kOk;
    }
    table.width_type = v[0];
    table.width = U16(v, kTableWidthValueOffset);
    return OpResult::kOk;
  }
  if (op == kSprmTFAutofit) {
    table.auto_fit = v[0] != 0;
    return OpResult::kOk;
  }
  if (op == kSprmTWidthIndent) {
    if (v.size() != kTableWidthSize || v[0] != kFtsDxa)
      return OpResult::kError;
    table.indent = U16(v, kTableWidthValueOffset);
    return OpResult::kOk;
  }
  if (op == kSprmTIstd) {
    table.style = U16(v, 0);
    return OpResult::kOk;
  }
  if (op == kSprmTDxaAbs) {
    if (S16(v, 0) < -kMaxTwips)
      return OpResult::kError;
    table.tblp_x = S16(v, 0);
    return OpResult::kOk;
  }
  if (op == kSprmTDyaAbs) {
    if (S16(v, 0) < -kMaxTwips)
      return OpResult::kError;
    table.tblp_y = S16(v, 0);
    return OpResult::kOk;
  }
  if (op == kSprmTDxaFromText || op == kSprmTDyaFromText ||
      op == kSprmTDxaFromTextRight || op == kSprmTDyaFromTextBottom) {
    if (U16(v, 0) > kMaxTwips)
      return OpResult::kError;
    size_t slot = op == kSprmTDxaFromText        ? 0
                  : op == kSprmTDyaFromText      ? 1
                  : op == kSprmTDxaFromTextRight ? 2
                                                 : 3;
    table.from_text[slot] = U16(v, 0);
    return OpResult::kOk;
  }
  if (op == kSprmTJc90 || op == kSprmTJc) {
    if (U16(v, 0) > kMaxTableAlignment)
      return OpResult::kError;
    table.align = U16(v, 0);
    return OpResult::kOk;
  }
  if (op == kSprmTDxaLeft) {
    table.left_origin = S16(v, 0);
    table.indent = table.left_origin - table.gap_half;
    return OpResult::kOk;
  }
  if (op == kSprmTDxaGapHalf) {
    if (S16(v, 0) < 0)
      return OpResult::kError;
    table.gap_half = S16(v, 0);
    if (table.left_origin >= 0)
      table.indent = table.left_origin - table.gap_half;
    return OpResult::kOk;
  }
  if (op == kSprmTCellSpacingDefault) {
    if (v.size() != kCssaSize || v[0] != kCssaCb ||
        v[1] != kCssaFixedItcFirst || v[2] != kCssaFixedItcLim ||
        v[kCssaSidesOffset] != kCssaSidesOnly ||
        (v[kCssaWidthTypeOffset] != kFtsDxa &&
         v[kCssaWidthTypeOffset] != kFtsNil &&
         v[kCssaWidthTypeOffset] != kCellSpacingDxaAlt) ||
        S16(v, kCssaWidthOffset) < 0 ||
        S16(v, kCssaWidthOffset) > kMaxCellSpacingTwips)
      return OpResult::kError;
    table.cell_spacing =
        v[kCssaWidthTypeOffset] == kFtsNil ? 0 : S16(v, kCssaWidthOffset);
    return OpResult::kOk;
  }
  if (op == kSprmTFBiDi || op == kSprmTFBiDi90) {
    table.bidi = U16(v, 0) != 0;
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyTableStyleOptions(model::Table& table, uint16_t op, Bytes v) {
  if (op == kSprmTTlp) {
    // TLP: itl(2 bytes) + grfatl(2 bytes). Only the Fatl flags survive into
    // w:tblLook; bit 5..10 hold first/last row/column and the band flags.
    if (v.size() != kTlpCb)
      return OpResult::kError;
    unsigned fatl = U16(v, kTlpFatlOffset);
    int look = static_cast<int>(fatl & kFatlLookMask);
    if ((fatl & kFatlBandRow) == 0)
      look |= kLookNoHBand;
    if ((fatl & kFatlBandCol) == 0)
      look |= kLookNoVBand;
    table.look = look;
    return OpResult::kOk;
  }
  if (op == kSprmTPc) {
    // PositionCodeOperand: padding(4 bits) + pcVert(2) + pcHorz(2).
    if (v.size() != 1)
      return OpResult::kError;
    unsigned vertical = (v[0] >> kPositionVertShift) & kPositionAnchorMask,
             horizontal = (v[0] >> kPositionHorzShift) & kPositionAnchorMask;
    table.anchor_v = vertical < kPositionNone ? static_cast<int>(vertical) : -1;
    table.anchor_h =
        horizontal < kPositionNone ? static_cast<int>(horizontal) : -1;
    return OpResult::kOk;
  }
  if (op == kSprmTCHorzBands || op == kSprmTCVertBands) {
    if (v[0] < 1 || v[0] > kMaxBandSize)
      return OpResult::kError;
    if (op == kSprmTCHorzBands)
      table.row_band = v[0];
    else
      table.col_band = v[0];
    return OpResult::kOk;
  }
  if (op == kSprmTCellPaddingStyle) {
    // CSSAOperand defaulted on the whole row: it belongs to the table.
    if (v.size() != kCssaSize || v[0] != kCssaCb ||
        v[1] != kCssaFixedItcFirst || v[2] != kCssaFixedItcLim ||
        (v[kCssaSidesOffset] & ~kCssaSidesOnly) ||
        v[kCssaWidthTypeOffset] != kFtsDxa ||
        U16(v, kCssaWidthOffset) > kMaxTwips)
      return OpResult::kError;
    for (size_t side = 0; side < kCellSideCount; side++)
      if (v[kCssaSidesOffset] & (1 << side))
        table.cell_margins[side] = U16(v, kCssaWidthOffset);
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyTableBorders(model::Table& table, uint16_t op, Bytes v) {
  if (op == kSprmTSetShdTable) {
    return ReadShade(table.shade, table.shade_fore, table.shade_pattern, v)
               ? OpResult::kOk
               : OpResult::kError;
  }
  if (op == kSprmTTableBorders80 || op == kSprmTTableBorders) {
    bool old = op == kSprmTTableBorders80;
    size_t size = old ? kBrc80Size : kBrcSize;
    if (v.size() != kCbPrefixSize + kTableBorderCount * size ||
        v[0] != kTableBorderCount * size)
      return OpResult::kError;
    for (size_t i = 0; i < kTableBorderCount; i++) {
      Bytes b = v.subspan(kCbPrefixSize + i * size, size);
      if (!old || U32(b, 0) != kUndefinedU32)
        if (!TableBorder(table.borders[i], b, old))
          return OpResult::kError;
    }
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyTableStructure(model::Table& table, model::RowProperties& row,
                             uint16_t op, Bytes v) {
  auto& cells = row.cells;
  if (op == kSprmTInsert) {
    if (v.size() != kTableInsertSize || !v[1] || v[0] > cells.size() ||
        cells.size() + v[1] > kMaxTableCells)
      return OpResult::kError;
    cells.insert(cells.begin() + v[0], v[1], model::CellProperties{});
    table.columns.insert(
        table.columns.begin() + std::min<size_t>(v[0], table.columns.size()),
        v[1], U16(v, kTableInsertWidthOffset));
    return OpResult::kOk;
  }
  if (op == kSprmTDelete) {
    size_t first, limit;
    if (!TableRange(row, v, 0, first, limit))
      return OpResult::kError;
    // A legal empty range (first == limit below the cell count) deletes
    // nothing; TableRange has already rejected first == limit == cell count.
    if (first == limit)
      return OpResult::kOk;
    if (limit - first == cells.size())
      return OpResult::kError;
    cells.erase(cells.begin() + first, cells.begin() + limit);
    if (limit <= table.columns.size())
      table.columns.erase(table.columns.begin() + first,
                          table.columns.begin() + limit);
    return OpResult::kOk;
  }
  if (op == kSprmTDxaCol) {
    size_t first, limit;
    if (v.size() != kTableColumnWidthSize ||
        !TableRange(row, v, 0, first, limit))
      return OpResult::kError;
    for (size_t i = first; i < limit; i++) {
      cells[i].width = U16(v, kTableColumnWidthOffset);
      if (i < table.columns.size())
        table.columns[i] = U16(v, kTableColumnWidthOffset);
    }
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyCellWidthAndMerge(model::RowProperties& row, uint16_t op,
                                Bytes v) {
  auto& cells = row.cells;
  if (op == kSprmTCellWidth) {
    size_t first, limit;
    if (v.size() != kCellWidthSize || v[0] != kCellWidthCb ||
        !TableRange(row, v, 1, first, limit))
      return OpResult::kError;
    // Same Fts rule as sprmTTableWidth: unknown types MUST be ignored.
    if (v[kCellWidthTypeOffset] != kFtsNil &&
        v[kCellWidthTypeOffset] != kFtsAuto &&
        v[kCellWidthTypeOffset] != kFtsPct &&
        v[kCellWidthTypeOffset] != kFtsDxa)
      return OpResult::kOk;
    for (size_t i = first; i < limit; i++) {
      cells[i].width_type = v[kCellWidthTypeOffset];
      cells[i].width = U16(v, kCellWidthValueOffset);
    }
    return OpResult::kOk;
  }
  if (op == kSprmTMerge || op == kSprmTSplit) {
    size_t first, limit;
    if (!TableRange(row, v, 0, first, limit))
      return OpResult::kError;
    for (size_t i = first; i < limit; i++)
      cells[i].horizontal_merge = op == kSprmTSplit ? 0
                                  : i == first      ? kTcMergeStart
                                                    : kTcMergeContinue;
    return OpResult::kOk;
  }
  if (op == kSprmTVertMerge) {
    if (v.size() != kVertMergeSize || v[0] != kVertMergeCb ||
        v[kVertMergeCellOffset] >= cells.size() ||
        (v[kVertMergeValueOffset] != 0 &&
         v[kVertMergeValueOffset] != kTcMergeContinue &&
         v[kVertMergeValueOffset] != kTcVertMergeRestart))
      return OpResult::kError;
    cells[v[kVertMergeCellOffset]].vertical_merge =
        v[kVertMergeValueOffset] == kTcVertMergeRestart
            ? kTcMergeStart
            : v[kVertMergeValueOffset];
    return OpResult::kOk;
  }
  if (op == kSprmTTextFlow) {
    size_t first, limit;
    if (!TableRange(row, v, 0, first, limit) || v.size() != kCellTextFlowSize ||
        U16(v, kCellTextFlowValueOffset) > kTcTextFlowMax ||
        U16(v, kCellTextFlowValueOffset) == kFlowReserved)
      return OpResult::kError;
    for (size_t i = first; i < limit; i++)
      cells[i].text_direction = U16(v, kCellTextFlowValueOffset);
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyCellLayout(model::RowProperties& row, uint16_t op, Bytes v) {
  auto& cells = row.cells;
  if (op == kSprmTVertAlign || op == kSprmTFCellNoWrap ||
      op == kSprmTCellFHideMark) {
    size_t first, limit;
    if (v.size() != kCellLayoutSize || v[0] != kCellLayoutCb ||
        !TableRange(row, v, 1, first, limit))
      return OpResult::kError;
    if (op == kSprmTVertAlign &&
        v[kCellLayoutValueOffset] > kMaxCellVerticalAlign)
      return OpResult::kError;
    for (size_t i = first; i < limit; i++) {
      if (op == kSprmTVertAlign)
        cells[i].vertical_align = v[kCellLayoutValueOffset];
      else if (op == kSprmTFCellNoWrap)
        cells[i].no_wrap = v[kCellLayoutValueOffset] != 0;
      else
        cells[i].hide_mark = v[kCellLayoutValueOffset] != 0;
    }
    return OpResult::kOk;
  }
  if (op == kSprmTFitText) {
    size_t first, limit;
    if (v.size() != kCellFitTextSize || !TableRange(row, v, 0, first, limit))
      return OpResult::kError;
    for (size_t i = first; i < limit; i++)
      cells[i].fit_text = v[kCellFitTextValueOffset] != 0;
    return OpResult::kOk;
  }
  if (op == kSprmTCellPadding || op == kSprmTCellPaddingDefault) {
    if (v.size() != kCssaSize || v[0] != kCssaCb ||
        (v[kCssaSidesOffset] & ~kCssaSidesOnly) ||
        (v[kCssaWidthTypeOffset] != kFtsNil &&
         v[kCssaWidthTypeOffset] != kFtsDxa))
      return OpResult::kError;
    if (op == kSprmTCellPaddingDefault &&
        (v[1] != kCssaFixedItcFirst || v[2] != kCssaFixedItcLim))
      return OpResult::kError;
    // sprmTCellPadding carries a half-open range. Real Word 97 documents
    // contain empty ranges, and LibreOffice's WW8TabBandDesc::
    // ProcessSpecificSpacing returns silently for start >= end and ignores
    // cell indices past the row. Mirror that instead of rejecting the whole
    // document: clamp the limit to the row, leaving an empty range a no-op.
    // Other TAPX handlers keep the strict TableRange contract.
    size_t first = 0, limit = cells.size();
    if (op == kSprmTCellPadding) {
      first = v[1];
      limit = std::min<size_t>(v[2], cells.size());
    }
    int width =
        v[kCssaWidthTypeOffset] == kFtsDxa ? U16(v, kCssaWidthOffset) : 0;
    if (width > kMaxTwips)
      return OpResult::kError;
    for (size_t i = first; i < limit; i++)
      for (size_t side = 0; side < kCellSideCount; side++)
        if (v[kCssaSidesOffset] & (1 << side))
          cells[i].margins[side] = width;
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyCellShading(model::RowProperties& row, uint16_t op, Bytes v) {
  auto& cells = row.cells;
  if (op == kSprmTSetShd || op == kSprmTSetShdOdd) {
    size_t first, limit;
    if (v.size() != kCellShadingSize || v[0] != kCellShadingCb ||
        !TableRange(row, v, 1, first, limit))
      return OpResult::kError;
    for (size_t i = first; i < limit; i++)
      if (op == kSprmTSetShd || ((i - first) & 1) == 0)
        if (!TableShade(cells[i], v.subspan(kCellShadingOffset, kShdSize)))
          return OpResult::kError;
    return OpResult::kOk;
  }
  if (op == kSprmTDefTableShd80) {
    if (v.empty() || static_cast<size_t>(v[0]) + 1 != v.size() ||
        v[0] % kShd80Size)
      return OpResult::kError;
    // SHD80 carries palette indices; a later modern SHD operand overrides it.
    for (size_t i = 0; i < v[0] / kShd80Size && i < cells.size(); i++) {
      unsigned sh = U16(v, kCbPrefixSize + kShd80Size * i);
      cells[i].shade_fore = Ico(sh & kShd80ColorMask);
      cells[i].shade = Ico((sh >> kShd80BackShift) & kShd80ColorMask);
      cells[i].shade_pattern = (sh >> kShd80PatternShift) & kShd80PatternMask;
    }
    return OpResult::kOk;
  }
  if (op == kSprmTDefTableShd || op == kSprmTDefTableShd2nd ||
      op == kSprmTDefTableShd3rd || op == kSprmTDefTableShdRaw ||
      op == kSprmTDefTableShdRaw2nd || op == kSprmTDefTableShdRaw3rd) {
    size_t first =
        (op == kSprmTDefTableShd2nd || op == kSprmTDefTableShdRaw2nd)
            ? kShadingCellGroupSize
        : (op == kSprmTDefTableShd3rd || op == kSprmTDefTableShdRaw3rd)
            ? 2 * kShadingCellGroupSize
            : 0;
    if (v.empty() || static_cast<size_t>(v[0]) + 1 != v.size() ||
        v[0] % kShdSize)
      return OpResult::kError;
    for (size_t i = 0; i < v[0] / kShdSize && first + i < cells.size(); i++)
      if (!TableShade(cells[first + i],
                      v.subspan(kCbPrefixSize + i * kShdSize, kShdSize)))
        return OpResult::kError;
    return OpResult::kOk;
  }
  return OpResult::kUnmatched;
}

OpResult ApplyCellBorders(model::RowProperties& row, uint16_t op, Bytes v) {
  auto& cells = row.cells;
  if (op == kSprmTBrcTopCv || op == kSprmTBrcLeftCv ||
      op == kSprmTBrcBottomCv || op == kSprmTBrcRightCv) {
    // BrcCvOperand: cb(1) = 4 per cell, then one COLORREF per cell.
    if (v.empty() || v[0] % kBrcCvColorSize ||
        static_cast<size_t>(v[0]) + 1 != v.size() ||
        v[0] / kBrcCvColorSize > cells.size())
      return OpResult::kError;
    size_t side = op == kSprmTBrcTopCv      ? 0
                  : op == kSprmTBrcLeftCv   ? 1
                  : op == kSprmTBrcBottomCv ? 2
                                            : 3;
    for (size_t i = 0; i < v[0] / kBrcCvColorSize; i++)
      cells[i].borders[side].color =
          static_cast<int>(U32(v, kCbPrefixSize + kBrcCvColorSize * i));
    return OpResult::kOk;
  }
  if (op == kSprmTSetBrc || op == kSprmTSetBrc80) {
    size_t first, limit;
    if (v.size() !=
            (op == kSprmTSetBrc ? kCellBorderSize : kCellBorder80Size) ||
        v[0] != v.size() - 1 || !TableRange(row, v, 1, first, limit) ||
        (v[kCellBorderSidesOffset] & ~kCellBorderSidesMask))
      return OpResult::kError;
    bool old = op == kSprmTSetBrc80;
    for (size_t i = first; i < limit; i++)
      for (size_t side = 0; side < kTableBorderCount; side++)
        if (v[kCellBorderSidesOffset] & (1 << side))
          if (!TableBorder(
                  cells[i].borders[side],
                  v.subspan(kCellBorderBrcOffset, old ? kBrc80Size : kBrcSize),
                  old))
            return OpResult::kError;
    return OpResult::kOk;
  }

  return OpResult::kUnmatched;
}

OpResult ApplyTableOp(model::Table& table, model::RowProperties& row,
                      uint16_t op, Bytes v) {
  OpResult result = ApplyTableDefinition(table, row, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyTableRowProperties(table, row, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyTableLayout(table, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyTableStyleOptions(table, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyTableBorders(table, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyTableStructure(table, row, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyCellWidthAndMerge(row, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyCellLayout(row, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyCellShading(row, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  result = ApplyCellBorders(row, op, v);
  if (result != OpResult::kUnmatched) {
    return result;
  }
  return OpResult::kOk;
}

bool ApplyTable(model::Table& table, model::RowProperties& row, uint16_t op,
                Bytes v) {
  return ApplyTableOp(table, row, op, v) != OpResult::kError;
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
    case kSprmCIbstRMark:
      if (v.size() >= kRevisionAuthorMinSize)
        r.revision_author_insert = S16(v, 0);
      break;
    case kSprmCIbstRMarkDel:
      if (v.size() >= kRevisionAuthorMinSize)
        r.revision_author_delete = S16(v, 0);
      break;
    case kSprmCDttmRMark:
      if (v.size() >= kRevisionDateMinSize)
        r.revision_date_insert = U32(v, 0);
      break;
    case kSprmCDttmRMarkDel:
      if (v.size() >= kRevisionDateMinSize)
        r.revision_date_delete = U32(v, 0);
      break;
    case kSprmCPropRMark:
      if (v.size() > 1)
        r.property_revision = v[1] != 0;
      // PropRMarkOperand ([MS-DOC] 2.9.218): cb, fPropRMark, ibst(2), dttm.
      if (v.size() >= kRevisionPropertyMinSize)
        r.revision_property = S16(v, 2);
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
      if (v[0] < kEmphasisCount)
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
      r.color_cs = Ico(U16(v, 0));
      break;
    case kSprmCFitText: {
      // CFitTextOperand: cb(1)=8, dxaFitText(4), FitTextID(4).
      if (v.size() != kFitTextSize || v[0] != kFitTextCb)
        break;
      int32_t width = static_cast<int32_t>(U32(v, kFitTextWidthOffset));
      if (width == 0)
        break;
      r.fit_text = width;
      r.fit_text_id = static_cast<int32_t>(U32(v, kFitTextIdOffset));
      break;
    }
    case kSprmCFELayout: {
      // FarEastLayoutOperand: cb(1)=6, ufel UFEL(2), lFELayoutID(4).
      if (v.size() != kFarEastLayoutSize || v[0] != kFarEastLayoutCb)
        break;
      unsigned ufel = U16(v, 1);
      r.ea_layout_set = true;
      r.ea_vert = (ufel & kUfelTny) != 0;
      r.ea_combine = (ufel & kUfelWarichu) != 0;
      r.ea_brackets =
          r.ea_combine ? (ufel >> kUfelBracketShift) & kUfelBracketMask : 0;
      r.ea_vert_compress = (ufel & kUfelTnyCompress) != 0;
      r.ea_id = static_cast<int>(U32(v, kFarEastLayoutIdOffset));
      break;
    }
    case kSprmCShd80: {
      unsigned sh = U16(v, 0);
      r.shade_fore = Ico(sh & kShd80ColorMask);
      r.shade = Ico((sh >> kShd80BackShift) & kShd80ColorMask);
      r.shade_pattern = (sh >> kShd80PatternShift) & kShd80PatternMask;
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
    if (v.size() < kTabChangeMinSize)
      return;
    size_t x = 1, deleted = v[x++],
           delbytes = (op == kSprmPChgTabs ? kTabDelClosePositionSize
                                           : kTabDelPositionSize) *
                      deleted;
    if (deleted > kMaxTabStops || delbytes > v.size() - x)
      return;
    const size_t del_offset = x;
    x += delbytes;
    if (x >= v.size())
      return;
    size_t added = v[x++];
    if (added > kMaxTabStops || added * kTabEntrySize > v.size() - x)
      return;
    // Deleted stops precede added ones in the operand and keep that order: a
    // later add of the same position wins in Word. Both operand forms hold the
    // deleted positions as one contiguous 2-byte XAS array; the close ranges
    // that follow them in sprmPChgTabs only say how near a stop must be.
    for (size_t i = 0; i < deleted; i++) {
      model::TabStop stop;
      stop.position = S16(v, del_offset + kTabDelPositionSize * i);
      stop.clear = true;
      p.tabs.push_back(stop);
    }
    for (size_t i = 0; i < added; i++) {
      int pos = S16(v, x + 2 * i), tbd = v[x + 2 * added + i];
      p.tabs.push_back(
          {pos, tbd & kTabKindMask, (tbd >> kTabLeaderShift) & kTabLeaderMask});
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
      if (v[0] <= kMaxFrameWrap)
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
      p.shade_fore = Ico(sh & kShd80ColorMask);
      p.shade = Ico((sh >> kShd80BackShift) & kShd80ColorMask);
      p.shade_pattern = (sh >> kShd80PatternShift) & kShd80PatternMask;
      break;
    }
    case kSprmPIncLvl:
      p.list_level_inc = static_cast<int8_t>(v[0]);
      // [MS-DOC] 2.6.2: heading istd 1..9 is offset and clamped to that
      // range. Other styles offset the outline level instead.
      if (p.style >= kFirstHeadingStyle && p.style <= kLastHeadingStyle)
        p.style = std::clamp(p.style + p.list_level_inc,
                             static_cast<int>(kFirstHeadingStyle),
                             static_cast<int>(kLastHeadingStyle));
      else if (p.outline >= 0 && p.outline < kMaxOutlineLevel)
        p.outline = std::clamp(p.outline + p.list_level_inc, 0,
                               static_cast<int>(kMaxOutlineLevel));
      break;
    case kSprmPNest80:
    case kSprmPNest:
      p.nest = S16(v, 0);
      p.nest_set = true;
      break;
    case kSprmPDcs: {
      // DCS: fdct(3 bits) + cl(5 bits), then one reserved byte.
      unsigned fdct = v[0] & kDcsTypeMask,
               lines = (v[0] >> kDcsLinesShift) & kDcsLinesMask;
      if ((fdct == kDropCapNormal || fdct == kDropCapMargin) && lines >= 1 &&
          lines <= kMaxDropCapLines) {
        p.drop_cap = static_cast<int>(fdct);
        p.drop_cap_lines = static_cast<int>(lines);
      }
      break;
    }
    case kSprmPWAlignFont:
      if (U16(v, 0) <= kMaxTextAlignment)
        p.text_alignment = U16(v, 0);
      break;
    case kSprmPFrameTextFlow:
      if ((U16(v, 0) & kFrameFlowVertical) == 0)
        break;  // horizontal flow is the default, nothing to write
      p.text_flow = (U16(v, 0) & kFrameFlowBackwards) != 0 ? 2 : 1;
      break;
    case kSprmPDxcRight:
      p.right_chars = S16(v, 0);
      p.right_chars_set = true;
      break;
    case kSprmPDxcLeft:
      p.left_chars = S16(v, 0);
      p.left_chars_set = true;
      break;
    case kSprmPDxcLeft1:
      p.first_line_chars = S16(v, 0);
      p.first_line_chars_set = true;
      break;
    case kSprmPDylBefore:
      p.before_lines = S16(v, 0);
      p.before_lines_set = true;
      break;
    case kSprmPDylAfter:
      p.after_lines = S16(v, 0);
      p.after_lines_set = true;
      break;
    case kSprmPItap:
      // spra 3: the absolute table depth is a 4-byte integer. Clamp rather
      // than reject so the field names the depth the frame stack builds.
      p.table_depth = ClampTableDepth(S32(v, 0));
      break;
    case kSprmPDtap:
      p.table_depth =
          ClampTableDepth(static_cast<int64_t>(p.table_depth) + S32(v, 0));
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
    if (v[0] > kMaxSectionColumnIndex)
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
    case kSprmSFProtected:
      s.form_unprotected = v[0] != 0;
      s.form_unprotected_set = true;
      break;
    case kSprmSPgbProp: {
      // SPgbPropOperand: pgbApplyTo(3) + pgbPageDepth(2) + pgbOffsetFrom(3).
      if (v.size() != kPgbPropSize || v[1] != 0)
        return false;
      unsigned apply_to = v[0] & kPgbApplyToMask,
               offset_from = (v[0] >> kPgbOffsetFromShift) & kPgbOffsetFromMask;
      if (apply_to > kPgbApplyToMax || offset_from > kPgbOffsetFromMax)
        return false;
      s.page_border_apply_to = static_cast<int>(apply_to);
      s.page_border_offset_from = static_cast<int>(offset_from);
      break;
    }
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
      if (v[0] > kMaxSectionVerticalJustify)
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
      if (U16(v, 0) > kMaxSectionColumnIndex)
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
      if (U32(v, 0) > kMaxPageStart)
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
      if (U16(v, 0) > kMaxGridMode)
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

namespace {
model::ConditionalProperties* FindConditional(model::Style& style, int type) {
  for (auto& conditional : style.conditionals)
    if (conditional.type == type)
      return &conditional;
  style.conditionals.push_back({});
  style.conditionals.back().type = type;
  return &style.conditionals.back();
}

// The nine "…Style" cell SPRMs only appear inside a CNFOperand block.
bool ApplyConditionalCell(model::ConditionalProperties& c, uint16_t op,
                          Bytes v) {
  switch (op) {
    case kSprmTCellBrcTopStyle:
      return ReadBorder(c.borders[0], v, false);
    case kSprmTCellBrcLeftStyle:
      return ReadBorder(c.borders[1], v, false);
    case kSprmTCellBrcBottomStyle:
      return ReadBorder(c.borders[2], v, false);
    case kSprmTCellBrcRightStyle:
      return ReadBorder(c.borders[3], v, false);
    case kSprmTCellBrcInsideHStyle:
      return ReadBorder(c.borders[4], v, false);
    case kSprmTCellBrcInsideVStyle:
      return ReadBorder(c.borders[5], v, false);
    case kSprmTCellBrcTL2BRStyle:
      return ReadBorder(c.borders[6], v, false);
    case kSprmTCellBrcTR2BLStyle:
      return ReadBorder(c.borders[7], v, false);
    case kSprmTCellShdStyle:
      return ReadShade(c.shade, c.shade_fore, c.shade_pattern, v);
    case kSprmTCellVertAlignStyle:
      if (v[0] > kMaxCellVerticalAlign)
        return false;
      c.vertical_align = v[0];
      return true;
    case kSprmTCellNoWrapStyle:
      c.no_wrap = v[0] != 0;
      c.no_wrap_set = true;
      return true;
    default:
      break;
  }
  return false;
}

// Cell-shaping SPRMs that are not CNF-specific still apply to the condition.
bool ApplyConditionalTable(model::ConditionalProperties& c, uint16_t op,
                           Bytes v) {
  model::Table table;
  model::RowProperties row;
  row.cells.resize(kMaxTableCells);
  if (ApplyTableOp(table, row, op, v) == OpResult::kError)
    return false;
  const auto& cell = row.cells[0];
  for (size_t i = 0; i < kCellSideCount; i++)
    if (cell.borders[i].set)
      c.borders[i] = cell.borders[i];
  if (cell.borders[4].set)
    c.borders[6] = cell.borders[4];  // tl2br
  if (cell.borders[5].set)
    c.borders[7] = cell.borders[5];  // tr2bl
  if (cell.shade >= 0 || cell.shade_fore >= 0) {
    c.shade = cell.shade;
    c.shade_fore = cell.shade_fore;
    c.shade_pattern = cell.shade_pattern;
  }
  if (cell.vertical_align >= 0)
    c.vertical_align = cell.vertical_align;
  if (cell.no_wrap) {
    c.no_wrap = true;
    c.no_wrap_set = true;
  }
  for (size_t i = 0; i < kCellSideCount; i++)
    if (cell.margins[i] >= 0)
      c.margins[i] = cell.margins[i];
  return true;
}
}  // namespace

bool ApplyConditional(model::Style& style, uint16_t op, Bytes v,
                      const std::vector<std::string>& fonts) {
  if (op != kSprmTCnf && op != kSprmPCnf && op != kSprmCCnf)
    return false;
  // CNFOperand: cb(1), cnfc(2), grpprl(cb - 2).
  if (v.size() < kCnfMinSize)
    return true;
  size_t cb = v[0];
  if (cb != v.size() - 1 || cb < kCnfCnfcSize)
    return true;
  model::ConditionalProperties* conditional = FindConditional(style, S16(v, 1));
  bool valid = true;
  Walk(v.subspan(kCnfMinSize, cb - kCnfCnfcSize),
       [&](uint16_t nested, Bytes nv) {
         switch ((nested >> kSprmSgcShift) & kSprmSgcMask) {
           case kSgcParagraph:
             ApplyPara(conditional->paragraph, nested, nv);
             break;
           case kSgcCharacter:
             ApplyRun(conditional->run, nested, nv, fonts);
             break;
           case kSgcTable:
             valid &= ApplyConditionalCell(*conditional, nested, nv) ||
                      ApplyConditionalTable(*conditional, nested, nv);
             break;
           default:
             break;
         }
       });
  return valid;
}
}  // namespace doccraft::doc::detail
