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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/charset.h"
#include "doc/format/internal.h"
#include "doc/format/sprm.h"
#include "doc/format/sprm_codes.h"

namespace doccraft::doc::detail {

using base::DecodeAnsiChar;

std::expected<std::vector<Char>, Error> ReadChars(
    Bytes word, Bytes table, const Fib& fib, uint32_t limit, uint32_t begin,
    std::vector<std::vector<uint8_t>>* prcs,
    std::vector<std::string>* warnings) {
  if (fib.legacy) {
    // Word 6/95 stores its fc/lcb tables and CLX in WordDocument. Its PCD
    // stores a direct byte offset; FibBase.fExtChar selects 16-bit text for
    // every piece, as LibreOffice does for pre-Word 97 files (WW8Cp2Fc and
    // WW8PLCFx_PCD keep bVer67 addresses untransformed). Normalize only the
    // validated text descriptors into the Word 97 PCD representation so both
    // format families share the character decoder.
    constexpr uint32_t kModernAnsiPiece = 0x40000000;
    constexpr size_t kLegacyPcdSize = 8;
    const bool ansi_text = !fib.ext_char;
    std::vector<uint32_t> cps;
    std::vector<uint32_t> fcs;
    std::vector<bool> ansi;
    if (fib.lcb_clx) {
      if (!Valid(word, {fib.fc_clx, fib.lcb_clx}))
        return std::unexpected(Error{"Word 6/95 CLX outside WordDocument"});
      Bytes clx = word.subspan(fib.fc_clx, fib.lcb_clx);
      if (clx.size() < 5 || clx[0] != 2)
        return std::unexpected(Error{"unsupported Word 6/95 CLX"});
      const size_t bytes = U32(clx, 1);
      if (bytes < 16 || bytes != clx.size() - 5 || (bytes - 4) % 12 != 0)
        return std::unexpected(Error{"invalid Word 6/95 PlcPcd size"});
      const size_t count = (bytes - 4) / 12;
      const size_t descriptor = 5 + 4 * (count + 1);
      for (size_t i = 0; i <= count; ++i)
        cps.push_back(U32(clx, 5 + 4 * i));
      for (size_t i = 0; i < count; ++i) {
        Bytes pcd =
            clx.subspan(descriptor + kLegacyPcdSize * i, kLegacyPcdSize);
        fcs.push_back(U32(pcd, 2));
        ansi.push_back(ansi_text);
      }
    } else {
      // Non-complex Word 6/95 files keep body, footnote and header text in
      // one contiguous run from fcMin, so the single implicit piece reaches
      // whatever story the caller asked for.
      cps = {0, limit};
      fcs = {fib.fc_min};
      ansi = {ansi_text};
    }
    if (cps.empty() || cps.front() != 0 || cps.back() < limit)
      return std::unexpected(Error{"invalid Word 6/95 piece coverage"});
    const size_t count = fcs.size();
    const size_t bytes = 4 + 12 * count;
    std::vector<uint8_t> normalized(5 + bytes);
    auto put32 = [&](size_t at, uint32_t value) {
      for (size_t j = 0; j < 4; ++j)
        normalized[at + j] = static_cast<uint8_t>(value >> (8 * j));
    };
    normalized[0] = 2;
    put32(1, static_cast<uint32_t>(bytes));
    for (size_t i = 0; i <= count; ++i)
      put32(5 + 4 * i, cps[i]);
    const size_t descriptor = 5 + 4 * (count + 1);
    for (size_t i = 0; i < count; ++i) {
      uint32_t fc = fcs[i];
      if (ansi[i]) {
        if (fc > (0x3FFFFFFFu / 2))
          return std::unexpected(Error{"Word 6/95 piece FC overflow"});
        fc = kModernAnsiPiece | (fc * 2);
      }
      put32(descriptor + 8 * i + 2, fc);
    }
    Fib modern = fib;
    modern.legacy = false;
    modern.fc_clx = 0;
    modern.lcb_clx = static_cast<uint32_t>(normalized.size());
    return ReadChars(word, normalized, modern, limit, begin, prcs, warnings);
  }
  if (!Valid(table, {fib.fc_clx, fib.lcb_clx}))
    return std::unexpected(Error{"CLX outside table stream"});
  Bytes b = table.subspan(fib.fc_clx, fib.lcb_clx);
  if (prcs)
    prcs->clear();
  size_t p = 0;
  size_t prc_count = 0;
  while (p < b.size() && b[p] == 1) {
    if (p + 3 > b.size())
      return std::unexpected(Error{"truncated CLX Prc"});
    size_t n = U16(b, p + 1);
    // [MS-DOC] §2.9.210: cbGrpprl is signed and at most 0x3FA2.
    if (n > 0x3FA2)
      return std::unexpected(Error{"invalid CLX Prc size"});
    if (n > b.size() - p - 3)
      return std::unexpected(Error{"truncated CLX grpprl"});
    if (!Walk(b.subspan(p + 3, n), [](uint16_t, Bytes) {}))
      return std::unexpected(Error{"invalid CLX grpprl"});
    if (prcs)
      prcs->emplace_back(b.begin() + p + 3, b.begin() + p + 3 + n);
    ++prc_count;
    p += 3 + n;
  }
  if (p + 5 > b.size() || b[p] != 2)
    return std::unexpected(Error{"missing CLX Pcdt"});
  size_t nbytes = U32(b, p + 1);
  p += 5;
  // The Pcdt is the last element of the CLX, so it must end the slice exactly.
  if (nbytes < 16 || nbytes > b.size() - p || (nbytes - 4) % 12 ||
      p + nbytes != b.size())
    return std::unexpected(Error{"invalid PlcPcd size"});
  size_t count = (nbytes - 4) / 12, desc = p + 4 * (count + 1);
  const uint32_t covered = U32(b, p + 4 * count);
  if (warnings && !fib.complex && !fib.legacy && begin == 0 &&
      limit == fib.ccp_text && covered < limit) {
    warnings->push_back("non-complex piece coverage short: expected " +
                        std::to_string(limit) + " CP, actual " +
                        std::to_string(covered) + " CP");
  }
  std::vector<Char> out;
  // Each emitted character consumes at least one WordDocument byte. Reserve
  // the CP upper bound up front so a large body never briefly holds both
  // sides of a geometric vector reallocation.
  out.reserve(std::min<size_t>(limit - std::min(limit, begin), word.size()));
  size_t emitted = 0;
  for (size_t i = 0; i < count; i++) {
    uint32_t cp = U32(b, p + 4 * i), end = U32(b, p + 4 * (i + 1));
    if (end <= cp)
      return std::unexpected(Error{"invalid piece CP order"});
    if (cp >= limit)
      break;
    end = std::min(end, limit);
    size_t length = end - cp;
    if (length > word.size() - emitted)
      return std::unexpected(Error{"piece table over-covers WordDocument"});
    emitted += length;
    uint32_t raw = U32(b, desc + 8 * i + 2);
    uint16_t prm = U16(b, desc + 8 * i + 6);
    if ((prm & 1) && (prm >> 1) >= prc_count)
      return std::unexpected(Error{"CLX Prm1 index outside Prc array"});
    bool compressed = raw & kFcCompressed;
    uint64_t start = compressed ? (raw & kFcMask) / 2 : raw & kFcMask;
    uint64_t bytes = static_cast<uint64_t>(length) * (compressed ? 1 : 2);
    if (start > word.size() || bytes > word.size() - start)
      return std::unexpected(Error{"piece outside WordDocument"});
    if (end <= begin)
      continue;
    for (uint32_t j = begin > cp ? begin - cp : 0; j < length;) {
      uint32_t character_cp = cp + j;
      uint32_t fc = start + j * (compressed ? 1 : 2);
      if (compressed) {
        // Compressed pieces use the document's ANSI code page (FibBase.lid).
        Char ch{character_cp, fc};
        ch.prm = prm;
        if (fib.codepage == base::kDefaultCodepage &&
            word[start + j] < kCp1252First) {
          const char value = static_cast<char>(word[start + j]);
          ch.newline = value == '\r' || value == '\n';
          ch.utf8[0] = ch.newline ? '\n' : value;
          ch.length = 1;
          out.push_back(ch);
          ++j;
          continue;
        }
        std::string decoded;
        size_t used =
            DecodeAnsiChar(fib.codepage, word.subspan(start + j), decoded);
        if (used == 0 || used > length - j) {
          decoded.assign("\xEF\xBF\xBD");  // U+FFFD
          used = 1;
        }
        ch.newline = decoded == "\r" || decoded == "\n";
        if (ch.newline)
          decoded = "\n";
        if (!ch.SetText(decoded))
          return std::unexpected(
              Error{"ANSI character expands past UTF-8 scalar"});
        out.push_back(std::move(ch));
        j += static_cast<uint32_t>(used);
        continue;
      }
      uint32_t c = U16(word, start + j * 2);
      uint32_t step = 1;
      if (c >= kUtf16HighSurrogateMin && c <= kUtf16HighSurrogateMax &&
          j + 1 < length) {
        uint32_t low = U16(word, start + (j + 1) * 2);
        if (low >= kUtf16LowSurrogateMin && low <= kUtf16LowSurrogateMax) {
          c = kUtf16SurrogateBase + ((c - kUtf16HighSurrogateMin) << 10) +
              (low - kUtf16LowSurrogateMin);
          step = 2;
        }
      }
      if (c == kNonCharacterMin ||
          c == static_cast<uint32_t>(kNonCharacterMin) + 1 ||
          (c >= kUtf16HighSurrogateMin && c <= kUtf16LowSurrogateMax))
        c = kReplacementCharacter;
      Char ch{character_cp, fc};
      ch.prm = prm;
      ch.newline = c == 13;
      if (c < kCp1252First) {
        ch.utf8[0] = ch.newline ? '\n' : static_cast<char>(c);
        ch.length = 1;
        out.push_back(ch);
        j += step;
        continue;
      }
      std::string encoded;
      AppendUtf8(encoded, c == 13 ? 10 : c);
      if (!ch.SetText(encoded))
        return std::unexpected(
            Error{"UTF-16 character expands past UTF-8 scalar"});
      out.push_back(std::move(ch));
      j += step;
    }
  }
  return out;
}

namespace {
void AppendLegacySprm(std::vector<uint8_t>& out, uint16_t op, Bytes value) {
  out.push_back(static_cast<uint8_t>(op));
  out.push_back(static_cast<uint8_t>(op >> 8));
  out.insert(out.end(), value.begin(), value.end());
}

// The ChgTabs operands normally start with a one-byte cb. A cb of 0xFF instead
// switches to a ChgTabsOperand whose first bytes are the deleted and inserted
// tab counts: nDel entries of 4 bytes, then nIns entries of 3 bytes, so the
// operand is 2 + 4*nDel + 3*nIns bytes after the cb. Transcribed from
// LibreOffice sw/source/filter/ww8/ww8scan.cxx GetSprmTailLen (case 23 /
// 0xC615); the byte counts come from the operand, not from a fixed width.
constexpr uint8_t kChgTabsExtendedCb = 0xFF;
constexpr size_t kChgTabsCountBytes = 2;
constexpr size_t kChgTabsDelEntrySize = 4;
constexpr size_t kChgTabsInsEntrySize = 3;

// `p` is the offset of the cb byte; the result counts the operand bytes that
// follow the opcode. No value means the counts run past the buffer.
std::optional<size_t> ChgTabsOperandSize(Bytes raw, size_t p) {
  if (p >= raw.size())
    return std::nullopt;
  if (raw[p] != kChgTabsExtendedCb)
    return static_cast<size_t>(raw[p]) + 1;
  const size_t del_index = p + 1;
  if (del_index >= raw.size())
    return std::nullopt;
  const size_t del = raw[del_index];
  const size_t ins_index = del_index + 1 + kChgTabsDelEntrySize * del;
  if (ins_index >= raw.size())
    return std::nullopt;
  const size_t ins = raw[ins_index];
  return kChgTabsCountBytes + kChgTabsDelEntrySize * del +
         kChgTabsInsEntrySize * ins + 1;
}

// Word 6/95 table operands, from wvWare's sprm.c and LibreOffice's
// GetWW6SprmSearcher (these layouts predate [MS-DOC] section 2.9):
// sprmTTableBorders is six two-byte BRC6 borders, sprmTDefTable(10) is a
// two-byte cb, itcMac, itcMac+1 XAS centres and itcMac ten-byte TC6 cells, and
// sprmTDefTableShd is a cb followed by two-byte SHD palette pairs.
constexpr size_t kLegacyTableBordersSize = 12;
constexpr size_t kLegacyBrc6Size = 2;
constexpr size_t kLegacyTc6Size = 10;
constexpr size_t kLegacyTc6BorderCount = 4;

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

void PutU16At(std::vector<uint8_t>& out, size_t p, uint16_t v) {
  out[p] = static_cast<uint8_t>(v);
  out[p + 1] = static_cast<uint8_t>(v >> 8);
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  PutU16(out, static_cast<uint16_t>(v));
  PutU16(out, static_cast<uint16_t>(v >> 16));
}

// BRC6: width(3), type(2), shadow(1), ico(5), space(5) (wvGetBRC_internal6).
// The modern Brc is COLORREF(4), width, type, flags, reserved. The all-ones
// word is treated as the two-byte analogue of the BRC80 undefined sentinel.
void AppendBrc6(std::vector<uint8_t>& out, uint16_t v) {
  if (v == kUndefinedU16) {
    out.insert(out.end(), kBrcSize, 0xFF);
    return;
  }
  PutU32(out, static_cast<uint32_t>(Ico((v >> 6) & 0x1F)));
  out.push_back(static_cast<uint8_t>(v & 0x07));
  out.push_back(static_cast<uint8_t>((v >> 3) & 0x03));
  uint8_t flags = static_cast<uint8_t>((v >> 11) & kBrcSpaceMask);
  if ((v >> 5) & 1)
    flags |= kBrcShadow;
  out.push_back(flags);
  out.push_back(0);
}

// TC6: two-byte TCGRF, then four two-byte BRC10 borders. TCGRF has the same
// merge and text-flow layout as TC80, so it is copied; the width is left zero
// because the column grid supplies it. The BRC10 borders are not converted:
// the corpus carries no one-to-one case (the only non-zero word is 0x03CA, a
// two-line hairline) and wvWare's own mapping is marked uncertain.
void AppendTc80FromTc6(std::vector<uint8_t>& out, Bytes tc6) {
  PutU16(out, U16(tc6, 0));
  PutU16(out, 0);
  for (size_t i = 0; i < kLegacyTc6BorderCount; i++)
    PutU32(out, 0);
}

bool LegacyTableBordersAllZero(Bytes v) {
  for (size_t i = 0; i < kTableBorderCount; i++)
    if (U16(v, i * kLegacyBrc6Size))
      return false;
  return true;
}

void RebuildLegacyTableBorders(Bytes v, std::vector<uint8_t>& out) {
  out.clear();
  out.push_back(static_cast<uint8_t>(kTableBorderCount * kBrcSize));
  for (size_t i = 0; i < kTableBorderCount; i++)
    AppendBrc6(out, U16(v, i * kLegacyBrc6Size));
}

// Rebuilds a Word 6/95 sprmTDefTable operand as a modern sprmTDefTable
// (20-byte TC80 cells) when the TC6 array is complete, or as the grid-only
// sprmTDefTableObsolete when it is short (wvApplysprmTDefTable keeps the grid
// and returns before the cells in that case).
bool RebuildLegacyTableDefinition(Bytes v, std::vector<uint8_t>& out,
                                  uint16_t& modern) {
  if (v.size() < kTDefHeaderSize)
    return false;
  const size_t count = v[kTDefCountOffset];
  const size_t header = kTDefHeaderSize + kTableRangeSize * (count + 1);
  if (count > kMaxTableCells || v.size() < header)
    return false;
  const size_t cells = v.size() - header;
  out.assign(2, 0);
  out.insert(out.end(), v.begin() + 2, v.begin() + header);
  if (cells >= count * kLegacyTc6Size) {
    for (size_t i = 0; i < count; i++)
      AppendTc80FromTc6(out,
                        v.subspan(header + i * kLegacyTc6Size, kLegacyTc6Size));
    modern = kSprmTDefTable;
  } else {
    modern = kSprmTDefTableObsolete;
  }
  PutU16At(out, 0, static_cast<uint16_t>(out.size() - 1));
  return true;
}

enum class LegacyTableTransform { kNone, kBorders, kDefinition };
}  // namespace

// Word 6/95 grpprls use one-byte opcodes. Translate the scalar properties
// understood by this converter and step over the ones whose width the Word 6
// table knows, so a harmless modifier can no longer discard every later
// property. An opcode outside the table (or a truncated operand) still stops
// the scan: without a width its operand bytes could be mistaken for opcodes.
LegacySprmScan ScanLegacySprms(Bytes raw, std::vector<uint8_t>& out) {
  bool partial = false;
  for (size_t p = 0; p < raw.size();) {
    const uint8_t code = raw[p++];
    if (code == 0)
      continue;  // PAPX word alignment.
    size_t size = 0;
    uint16_t modern = 0;
    // A legacy opcode can expand to a second modern sprm. Only sprmCLid uses
    // this today: Word 6/95 stores one LID for both the default and the East
    // Asian language, so the operand is written twice.
    uint16_t modern_east_asia = 0;
    bool mask_low_byte = false;
    uint8_t masked_low_byte[2] = {};
    bool mapped = true;
    LegacyTableTransform transform = LegacyTableTransform::kNone;
    switch (code) {
      case kLegacySprmPJc:
        modern = kSprmPJc80;
        size = 1;
        break;
      case kLegacySprmPFKeep:
        modern = kSprmPFKeep;
        size = 1;
        break;
      case kLegacySprmPFKeepFollow:
        modern = kSprmPFKeepFollow;
        size = 1;
        break;
      case kLegacySprmPFPageBreakBefore:
        modern = kSprmPFPageBreakBefore;
        size = 1;
        break;
      case kLegacySprmPChgTabsPapx:
      case kLegacySprmPChgTabs: {
        // The cb byte selects between a flat length and the ChgTabsOperand
        // counts; both must resolve inside the buffer. The two opcodes keep
        // their Word 97+ split: the Papx form (0x0F) deletes 2-byte XAS
        // positions, the plain form (0x17) 32-bit ones ([MS-DOC] 2.9.180 and
        // 2.9.183), which is exactly how sprmPChgTabsPapx/sprmPChgTabs read
        // the operand, so the bytes pass through unchanged. Word 95 samples
        // use the 2-byte form: 080_080742.doc carries
        // `0F 07 01 0000 01 30FD 00`, one deleted position in two bytes.
        const std::optional<size_t> operand = ChgTabsOperandSize(raw, p);
        if (!operand)
          return LegacySprmScan::kStopped;
        size = *operand;
        if (raw[p] == kChgTabsExtendedCb) {
          // Only sprmPChgTabs documents the count-based ChgTabsOperand;
          // PChgTabsPapxOperand caps cb at 254 ([MS-DOC] 2.9.180, 2.9.183).
          // For the Papx form the width is therefore unknowable: stop instead
          // of measuring its operand with counts that select 4-byte deleted
          // entries, which would let the FKP reader slice later bytes as
          // opcodes. The stop raises the existing unsupported-sprm warning.
          if (code == kLegacySprmPChgTabsPapx)
            return LegacySprmScan::kStopped;
          // Consume the resolved width and drop the modifier rather than hand
          // a misaligned operand to the modern reader.
          partial = true;
        } else {
          modern = code == kLegacySprmPChgTabsPapx ? kSprmPChgTabsPapx
                                                   : kSprmPChgTabs;
        }
        break;
      }
      case kLegacySprmPDxaRight:
        modern = kSprmPDxaRight80;
        size = 2;
        break;
      case kLegacySprmPDxaLeft:
        modern = kSprmPDxaLeft80;
        size = 2;
        break;
      case kLegacySprmPDxaLeft1:
        modern = kSprmPDxaLeft180;
        size = 2;
        break;
      case kLegacySprmPDyaLine:
        modern = kSprmPDyaLine;
        size = 4;
        break;
      case kLegacySprmPDyaBefore:
        modern = kSprmPDyaBefore;
        size = 2;
        break;
      case kLegacySprmPDyaAfter:
        modern = kSprmPDyaAfter;
        size = 2;
        break;
      case kLegacySprmPFInTable:
        modern = kSprmPFInTable;
        size = 1;
        break;
      case kLegacySprmPFTtp:
        modern = kSprmPFTtp;
        size = 1;
        break;
      case kLegacySprmPWr:
        modern = kSprmPWr;
        size = 1;
        break;
      case kLegacySprmPFWidowControl:
        modern = kSprmPFWidowControl;
        size = 1;
        break;
      // The Word 6/95 East Asian paragraph flags share their opcode numbers
      // with [MS-DOC] 2.9.215 (Prm0 isprm) and map one-to-one onto the modern
      // Bool8 sprms, so the operand passes through unchanged.
      case kLegacySprmPFKinsoku:
        modern = kSprmPFKinsoku;
        size = 1;
        break;
      case kLegacySprmPFWordWrap:
        modern = kSprmPFWordWrap;
        size = 1;
        break;
      case kLegacySprmPFOverflowPunct:
        modern = kSprmPFOverflowPunct;
        size = 1;
        break;
      case kLegacySprmPFTopLinePunct:
        modern = kSprmPFTopLinePunct;
        size = 1;
        break;
      case kLegacySprmPFAutoSpaceDE:
        modern = kSprmPFAutoSpaceDE;
        size = 1;
        break;
      case kLegacySprmPFAutoSpaceDN:
        modern = kSprmPFAutoSpaceDN;
        size = 1;
        break;
      case kLegacySprmPFNoAutoHyph:
        modern = kSprmPFNoAutoHyph;
        size = 1;
        break;
      // Word 6/95 table properties. The scalar layout matches their Word 97+
      // namesakes, so the operands pass through unchanged; sprmTJc is the
      // exception (see below). Evidence: LibreOffice ww8scan.cxx
      // GetWW6SprmSearcher lists `tap.dxaGapHalf`, `tap.rgdxaCenter [0]`,
      // `tap.fCantSplit`, `tap.fTableHeader` and `tap.dyaRowHeight` with the
      // same widths, and [MS-DOC] 2.9.144/2.9.145/2.9.169 give the modern
      // handlers those operands.
      case kLegacySprmTTableBorders:
        modern = kSprmTTableBorders;
        size = kLegacyTableBordersSize;
        transform = LegacyTableTransform::kBorders;
        break;
      case kLegacySprmTDefTable10:
      case kLegacySprmTDefTable:
        modern = kSprmTDefTable;
        if (raw.size() - p < 2)
          return LegacySprmScan::kStopped;
        {
          const size_t count = U16(raw, p);
          if (!count)
            return LegacySprmScan::kStopped;
          size = count + 1;  // Two-byte count, then count - 1 bytes.
        }
        transform = LegacyTableTransform::kDefinition;
        break;
      case kLegacySprmTDefTableShd:
        // One cb byte then 2-byte SHD pairs, exactly the DefTableShd80Operand
        // the modern handler reads ([MS-DOC] 2.9.52). An odd cb cannot be a
        // whole number of cells, so consume the modifier and drop it instead
        // of handing the modern reader an operand it would reject.
        if (p >= raw.size())
          return LegacySprmScan::kStopped;
        size = static_cast<size_t>(raw[p]) + 1;
        if (raw[p] % kShd80Size) {
          modern = 0;
          partial = true;
        } else {
          modern = kSprmTDefTableShd80;
        }
        break;
      // Word 6/95 table edits share the modern operand layout, so the bytes
      // pass through unchanged: TInsertOperand is itcFirst/ctc/dxaCol (4),
      // TDxaColOperand is itcFirst/itcLim/dxaCol (4), and TDelete/TMerge/
      // TSplit are ItcFirstLim (2). wvWare sprm.c rgsprmWord6 maps these
      // opcodes onto the modern ids and wvApplysprmTInsert/TDelete/TDxaCol/
      // TMerge/TSplit read exactly those fields ([MS-DOC] 2.6.3); LibreOffice
      // GetWW6SprmSearcher lists the same 4/2/4/2/2 widths. Its v6 table
      // switch maps only 0xC2..0xC4; the 0xC5/0xC6 range reading comes from
      // wvWare, whose apply functions use ItcFirstLim for both.
      case kLegacySprmTInsert:
        modern = kSprmTInsert;
        size = kTableInsertSize;
        break;
      case kLegacySprmTDelete:
        modern = kSprmTDelete;
        size = kTableRangeSize;
        break;
      case kLegacySprmTDxaCol:
        modern = kSprmTDxaCol;
        size = kTableColumnWidthSize;
        break;
      case kLegacySprmTMerge:
        modern = kSprmTMerge;
        size = kTableRangeSize;
        break;
      case kLegacySprmTSplit:
        modern = kSprmTSplit;
        size = kTableRangeSize;
        break;
      case kLegacySprmTDxaLeft:
        modern = kSprmTDxaLeft;
        size = 2;
        break;
      case kLegacySprmTDxaGapHalf:
        modern = kSprmTDxaGapHalf;
        size = 2;
        break;
      case kLegacySprmTFCantSplit:
        modern = kSprmTFCantSplit;
        size = 1;
        break;
      case kLegacySprmTTableHeader:
        modern = kSprmTTableHeader;
        size = 1;
        break;
      case kLegacySprmTDyaRowHeight:
        modern = kSprmTDyaRowHeight;
        size = 2;
        break;
      case kLegacySprmTJc:
        // Only the low byte of the legacy sprmTJc word is significant
        // (LibreOffice: "tap.jc jc (low order byte is significant)"). The
        // modern handler reads the full word and rejects values above 2, so a
        // stray high byte would surface as an invalid TAPX operand and reject
        // the whole document instead of a table property.
        modern = kSprmTJc;
        size = 2;
        mask_low_byte = true;
        break;
      case kLegacySprmCFBold:
        modern = kSprmCFBold;
        size = 1;
        break;
      case kLegacySprmCFItalic:
        modern = kSprmCFItalic;
        size = 1;
        break;
      case kLegacySprmCFStrike:
        modern = kSprmCFStrike;
        size = 1;
        break;
      case kLegacySprmCFtc:
        modern = kSprmCRgFtc0;
        size = 2;
        break;
      case kLegacySprmCKul:
        modern = kSprmCKul;
        size = 1;
        break;
      case kLegacySprmCIco:
        modern = kSprmCIco;
        size = 1;
        break;
      case kLegacySprmCDxaSpace:
        modern = kSprmCDxaSpace;
        size = 2;
        break;
      case kLegacySprmCLid:
        // Word 6/95 stores one 2-byte LID and wvWare applies it to both
        // lidDefault and lidFE. [MS-DOC] splits that across sprmCRgLid0
        // (non-East-Asian) and sprmCRgLid1 (East Asian), so the same operand
        // is written to both modern opcodes.
        modern = kSprmCRgLid0;
        modern_east_asia = kSprmCRgLid1;
        size = 2;
        break;
      case kLegacySprmCHps:
        modern = kSprmCHps;
        size = 2;  // Word 6 stores the full half-point word, not a byte.
        break;
      case kLegacySprmCPicLocation:
        modern = kSprmCPicLocation;
        size = 4;
        break;
      default:
        mapped = false;
        break;
    }
    if (!mapped) {
      const LegacySprmSpec* spec = FindLegacySprmSpec(code);
      if (!spec)
        return LegacySprmScan::kStopped;
      switch (spec->width) {
        case LegacySprmWidth::kFix:
          size = spec->size;
          break;
        case LegacySprmWidth::kVar:
          if (p >= raw.size())
            return LegacySprmScan::kStopped;
          size = static_cast<size_t>(raw[p]) + 1 + spec->size;
          break;
        case LegacySprmWidth::kVar2:
          if (raw.size() - p < 2)
            return LegacySprmScan::kStopped;
          {
            const size_t count = U16(raw, p);
            if (!count)
              return LegacySprmScan::kStopped;
            size = count + 1;  // Two-byte count prefix, then count - 1 bytes.
          }
          break;
      }
      partial = true;
    }
    if (size > raw.size() - p)
      return LegacySprmScan::kStopped;
    Bytes operand = raw.subspan(p, size);
    // Rebuild the legacy table operands into the modern byte layout before
    // they reach the shared handlers. `rebuilt` outlives the append below.
    std::vector<uint8_t> rebuilt;
    if (transform == LegacyTableTransform::kBorders) {
      // An all-zero operand is the Word 6/95 default (no borders); drop it
      // instead of materialising six explicit nil borders.
      if (LegacyTableBordersAllZero(operand)) {
        modern = 0;
      } else {
        RebuildLegacyTableBorders(operand, rebuilt);
        operand = rebuilt;
      }
    } else if (transform == LegacyTableTransform::kDefinition) {
      if (RebuildLegacyTableDefinition(operand, rebuilt, modern)) {
        operand = rebuilt;
      } else {
        // The width is known even when the payload is not a valid grid, so
        // step over it and drop the modifier instead of stranding later
        // properties in the same grpprl.
        modern = 0;
        partial = true;
      }
    }
    if (mask_low_byte) {
      // Rebuild the operand as low byte + zero high byte (sprmTJc only).
      masked_low_byte[0] = operand[0];
      operand = masked_low_byte;
    }
    if (modern)
      AppendLegacySprm(out, modern, operand);
    if (modern_east_asia)
      AppendLegacySprm(out, modern_east_asia, operand);
    p += size;
  }
  return partial ? LegacySprmScan::kPartial : LegacySprmScan::kComplete;
}

// Keeps the historical "not fully successful" result while the internal scan
// distinguishes a harmless skip from a stop. Callers that only warn when the
// grpprl is truly truncated use ScanLegacySprms directly.
bool NormalizeLegacySprms(Bytes raw, std::vector<uint8_t>& out) {
  return ScanLegacySprms(raw, out) == LegacySprmScan::kComplete;
}

std::expected<std::vector<FormatRange>, Error> ReadLegacyFkps(
    Bytes word, Fib::Range r, bool para, std::vector<std::string>* warnings) {
  std::vector<FormatRange> out;
  if (!r.size)
    return out;
  if (!Valid(word, r) || r.size < 10 || (r.size - 4) % 6)
    return std::unexpected(Error{"invalid Word 6/95 BTE table"});
  Bytes plc = word.subspan(r.offset, r.size);
  const size_t pages = (r.size - 4) / 6;
  if (pages > word.size() / 512)
    return std::unexpected(Error{"Word 6/95 BTE page count exceeds stream"});
  std::vector<uint8_t> seen(word.size() / 512);
  bool warned = false;
  for (size_t k = 0; k < pages; ++k) {
    const size_t pn = U16(plc, 4 * (pages + 1) + 2 * k);
    if (pn >= seen.size() || seen[pn]++)
      return std::unexpected(Error{"invalid Word 6/95 FKP page"});
    Bytes f = word.subspan(pn * 512, 512);
    const size_t count = f[511], stride = para ? 7 : 1;
    if (!count || 4 * (count + 1) + stride * count > 511)
      return std::unexpected(Error{"invalid Word 6/95 FKP run count"});
    for (size_t j = 0; j < count; ++j) {
      const uint32_t first = U32(f, 4 * j), last = U32(f, 4 * (j + 1));
      if (last <= first)
        return std::unexpected(Error{"invalid Word 6/95 FKP FC order"});
      FormatRange range{first, last, {}};
      const size_t off = 2 * f[4 * (count + 1) + stride * j];
      if (off) {
        if (off >= 511)
          return std::unexpected(Error{"invalid Word 6/95 FKP offset"});
        const size_t length = para ? 2 * f[off] : f[off];
        if (length > 511 - off - 1 || (para && length < 2))
          return std::unexpected(Error{"invalid Word 6/95 FKP payload"});
        size_t start = off + 1;
        if (para) {
          range.style = U16(f, start);
          start += 2;
        }
        if (ScanLegacySprms(f.subspan(start, length - (para ? 2 : 0)),
                            range.sprms) == LegacySprmScan::kStopped &&
            !warned && warnings) {
          warnings->push_back("Word 6/95 FKP contains unsupported sprms");
          warned = true;
        }
      }
      out.push_back(std::move(range));
    }
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });
  for (size_t i = 1; i < out.size(); ++i)
    if (out[i].first < out[i - 1].last)
      return std::unexpected(Error{"overlapping Word 6/95 FKP ranges"});
  return out;
}

std::expected<std::vector<FormatRange>, Error> ReadFkps(
    Bytes word, Bytes table, Fib::Range r, bool para,
    std::vector<std::string>* warnings, bool legacy) {
  if (legacy)
    return ReadLegacyFkps(word, r, para, warnings);
  std::vector<FormatRange> out;
  if (!r.size)
    return out;
  if (!Valid(table, r) || r.size < 12 || (r.size - 4) % 8)
    return std::unexpected(Error{"invalid BTE table"});
  Bytes plc = table.subspan(r.offset, r.size);
  size_t pages = (r.size - 4) / 8;
  if (pages > word.size() / 512)
    return std::unexpected(Error{"BTE page count exceeds WordDocument"});
  std::vector<uint8_t> seen(word.size() / 512);
  for (size_t k = 0; k < pages; k++) {
    size_t pn = U32(plc, 4 * (pages + 1) + 4 * k) & kPnFkpMask;
    const size_t before = out.size();
    auto read_page = [&]() -> std::expected<void, Error> {
      if (pn >= seen.size())
        return std::unexpected(Error{"FKP page outside WordDocument"});
      if (seen[pn]++)
        return std::unexpected(Error{"duplicate FKP page"});
      Bytes f = word.subspan(pn * 512, 512);
      size_t n = f[511], stride = para ? 13 : 1;
      if (n == 0 || n > (para ? 29 : 101) || 4 * (n + 1) + stride * n > 511)
        return std::unexpected(Error{"invalid FKP run count"});
      for (size_t j = 0; j < n; j++) {
        uint32_t first = U32(f, 4 * j), last = U32(f, 4 * (j + 1));
        if (last <= first)
          return std::unexpected(Error{"invalid FKP FC order"});
        size_t off = 2 * f[4 * (n + 1) + stride * j];
        FormatRange x{first, last, {}};
        if (off) {
          if (off >= 511)
            return std::unexpected(Error{"invalid FKP format offset"});
          size_t len = 0, start = 0;
          if (para) {
            size_t cb = f[off];
            if (cb) {
              len = cb * 2 - 1;
              start = off + 1;
            } else {
              if (off + 2 > 511)
                return std::unexpected(Error{"truncated PAPX"});
              len = 2 * f[off + 1];
              start = off + 2;
            }
            if (len < 2 || len > 511 - start)
              return std::unexpected(Error{"invalid PAPX size"});
            x.style = U16(f, start);
            start += 2;
            len -= 2;
          } else {
            len = f[off];
            start = off + 1;
            if (len > 511 - start)
              return std::unexpected(Error{"invalid CHPX size"});
          }
          x.sprms.assign(f.begin() + start, f.begin() + start + len);
          if (!Walk(x.sprms, [](uint16_t, Bytes) {}))
            return std::unexpected(Error{"invalid FKP sprm sequence"});
        }
        out.push_back(std::move(x));
      }
      return {};
    };
    auto page = read_page();
    if (!page) {
      // A malformed sprm stream cannot be localized safely: its operands may
      // carry table definitions and indirect Data-stream references.
      if (!warnings ||
          (para && page.error().message == "invalid FKP sprm sequence"))
        return std::unexpected(page.error());
      out.resize(before);
      if (para) {
        const uint32_t first = U32(plc, 4 * k);
        const uint32_t last = U32(plc, 4 * (k + 1));
        if (last <= first)
          return std::unexpected(Error{"invalid PAPX BTE FC order"});
        FormatRange damaged{first, last, {}};
        damaged.damaged = true;
        out.push_back(std::move(damaged));
      }
      warnings->push_back(std::string("skipped ") + (para ? "PAPX" : "CHPX") +
                          " FKP page " + std::to_string(pn) + ": " +
                          page.error().message);
    }
  }

  std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) {
    return x.first < y.first;
  });
  for (size_t i = 1; i < out.size(); i++)
    if (out[i].first < out[i - 1].last)
      return std::unexpected(Error{"overlapping FKP ranges"});
  return out;
}

const FormatRange* Find(const std::vector<FormatRange>& a, uint32_t fc) {
  auto it = std::upper_bound(a.begin(), a.end(), fc,
                             [](uint32_t value, const FormatRange& x) {
                               return value < x.first;
                             });
  if (it == a.begin())
    return nullptr;
  --it;
  return fc < it->last ? &*it : nullptr;
}
}  // namespace doccraft::doc::detail
