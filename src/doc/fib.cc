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

#include "doc/fib.h"

#include <cstddef>

#include "base/binary.h"
#include "base/charset.h"

namespace doccraft::doc {

// Byte readers from the shared base layer; [MS-DOC] is little-endian.
using base::U16;
using base::U32;

uint32_t ReadSummaryCodepage(std::span<const uint8_t> summary) {
  // [MS-OLEPS] SummaryInformation: one property set, with PID 1 as VT_I2.
  constexpr size_t kSectionOffsetPosition = 44;
  constexpr size_t kPropertyEntrySize = 8;
  constexpr uint32_t kCodepageProperty = 1;
  constexpr uint16_t kSignedInt16Type = 2;
  if (summary.size() < 48 || U16(summary, 0) != 0xFFFE || U32(summary, 24) != 1)
    return 0;
  size_t section = U32(summary, kSectionOffsetPosition);
  if (section > summary.size() || summary.size() - section < 8)
    return 0;
  const size_t size = U32(summary, section);
  const size_t count = U32(summary, section + 4);
  if (size < 8 || size > summary.size() - section ||
      count > (size - 8) / kPropertyEntrySize)
    return 0;
  for (size_t i = 0; i < count; ++i) {
    size_t entry = section + 8 + kPropertyEntrySize * i;
    if (U32(summary, entry) != kCodepageProperty)
      continue;
    const size_t offset = U32(summary, entry + 4);
    if (offset > size || size - offset < 8)
      return 0;
    const size_t typed = section + offset;
    return U16(summary, typed) == kSignedInt16Type ? U16(summary, typed + 4)
                                                   : 0;
  }
  return 0;
}

std::expected<Fib, Error> ParseFib(std::span<const uint8_t> word) {
  if (word.size() < 34)
    return std::unexpected(Error{"truncated FIB base"});
  const uint16_t ident = U16(word, 0);
  const uint16_t version = U16(word, 2);
  constexpr uint16_t kWord6Ident = 0xA5DC;
  constexpr uint16_t kWord95Ident = 0xA699;
  constexpr uint16_t kWord97Ident = 0xA5EC;
  if (ident != kWord97Ident && ident != kWord6Ident && ident != kWord95Ident)
    return std::unexpected(Error{"invalid WordDocument signature"});
  Fib f;
  f.n_fib = version;
  if (ident != kWord97Ident) {
    constexpr size_t kLegacyHeaderSize = 88;
    constexpr size_t kLegacyPairCount = 38;
    constexpr size_t kLegacyPairStart = 88;
    constexpr size_t kLegacyClxPair = 33;
    if ((ident == kWord6Ident && version != 0x0065 && version != 0x0068) ||
        (ident == kWord95Ident && version != 0x0068))
      return std::unexpected(Error{"unsupported legacy nFib version"});
    if (word.size() < kLegacyHeaderSize + kLegacyPairCount * 8)
      return std::unexpected(Error{"truncated Word 6/95 FIB"});
    f.legacy = true;
    f.lid = U16(word, 6);
    f.codepage = base::CodepageForLid(f.lid);
    f.complex = (U16(word, 10) & 0x0004) != 0;
    f.encrypted = (U16(word, 10) & 0x0100) != 0;
    if (f.encrypted)
      return std::unexpected(Error{"encrypted Word 6/95 unsupported"});
    f.fc_min = U32(word, 24);
    const uint32_t fc_max = U32(word, 28);
    f.ccp_text = U32(word, 52);
    if (f.fc_min < kLegacyHeaderSize + kLegacyPairCount * 8 ||
        f.fc_min > fc_max || fc_max > word.size() || f.ccp_text > word.size())
      return std::unexpected(Error{"invalid Word 6/95 text range"});
    if (!f.complex && f.ccp_text > fc_max - f.fc_min)
      return std::unexpected(Error{"Word 6/95 text exceeds fcMac"});
    f.fc_clx = U32(word, kLegacyPairStart + kLegacyClxPair * 8);
    f.lcb_clx = U32(word, kLegacyPairStart + kLegacyClxPair * 8 + 4);
    // The Word 6/95 BTE PLCs use two-byte page numbers in WordDocument.
    constexpr size_t kLegacyChpxPair = 12;
    constexpr size_t kLegacyPapxPair = 13;
    f.chpx_bte = {U32(word, kLegacyPairStart + kLegacyChpxPair * 8),
                  U32(word, kLegacyPairStart + kLegacyChpxPair * 8 + 4)};
    f.papx_bte = {U32(word, kLegacyPairStart + kLegacyPapxPair * 8),
                  U32(word, kLegacyPairStart + kLegacyPapxPair * 8 + 4)};
    if (f.lcb_clx &&
        (f.fc_clx > word.size() || f.lcb_clx > word.size() - f.fc_clx))
      return std::unexpected(Error{"Word 6/95 CLX outside WordDocument"});
    if (f.complex && !f.lcb_clx)
      return std::unexpected(Error{"missing Word 6/95 CLX"});
    // Some Word 6 writers leave lid=en-US even for Cyrillic ANSI text. The
    // FFN character set identifies the actual code page in those files.
    constexpr size_t kLegacyFontPair = 15;
    constexpr uint8_t kRussianCharset = 0xCC;
    const size_t font_fc = U32(word, kLegacyPairStart + kLegacyFontPair * 8);
    const size_t font_size =
        U32(word, kLegacyPairStart + kLegacyFontPair * 8 + 4);
    // The Word 6/95 fc/lcb array keeps the modern slot numbering: pair 1 is
    // fcStshfOld and pair 15 is fcSttbfFfn, both inside WordDocument.
    f.stsh = {U32(word, kLegacyPairStart + 8),
              U32(word, kLegacyPairStart + 12)};
    f.fonts = {static_cast<uint32_t>(font_fc),
               static_cast<uint32_t>(font_size)};
    if (f.codepage == base::kDefaultCodepage && font_size >= 7 &&
        font_fc <= word.size() && font_size <= word.size() - font_fc) {
      size_t pos = font_fc + 2;
      const size_t end = font_fc + font_size;
      while (pos + 5 <= end) {
        const size_t next = pos + static_cast<size_t>(word[pos]) + 1;
        if (next > end || next <= pos + 4)
          break;
        if (word[pos + 4] == kRussianCharset) {
          f.codepage = 1251;
          break;
        }
        pos = next;
      }
    }
    return f;
  }
  if (f.n_fib < 0x00C1 || f.n_fib > 0x011A)
    return std::unexpected(Error{"unsupported nFib version"});
  f.lid = U16(word, 6);
  f.codepage = base::CodepageForLid(f.lid);
  const uint16_t flags = U16(word, 10);
  f.complex = flags & 0x0004;
  f.encrypted = flags & 0x0100;
  f.which_table = flags & 0x0200;
  if (f.encrypted)
    return std::unexpected(Error{flags & 0x8000
                                     ? "XOR-obfuscated document must be "
                                       "decrypted with a password first"
                                     : "RC4-encrypted document must be "
                                       "decrypted with a password first"});
  const size_t lw_count_pos = 34u + static_cast<size_t>(U16(word, 32)) * 2;
  if (lw_count_pos + 2 > word.size())
    return std::unexpected(Error{"truncated FIB csw"});
  const uint16_t cslw = U16(word, lw_count_pos);
  const size_t lw = lw_count_pos + 2;
  if (static_cast<size_t>(cslw) * 4 > word.size() - lw || cslw < 11)
    return std::unexpected(Error{"truncated FIB cslw"});
  f.ccp_text = U32(word, lw + 12);
  f.ccp_ftn = U32(word, lw + 16);
  f.ccp_hdd = U32(word, lw + 20);
  f.ccp_atn = U32(word, lw + 28);
  f.ccp_edn = U32(word, lw + 32);
  f.ccp_txbx = U32(word, lw + 36);
  f.ccp_hdr_txbx = U32(word, lw + 40);
  const size_t pair_count_pos = lw + static_cast<size_t>(cslw) * 4;
  if (pair_count_pos + 2 > word.size())
    return std::unexpected(Error{"truncated FIB pairs"});
  const uint16_t pairs = U16(word, pair_count_pos);
  const size_t pair_start = pair_count_pos + 2;
  if (static_cast<size_t>(pairs) * 8 > word.size() - pair_start || pairs < 34)
    return std::unexpected(Error{"truncated FIB fc/lcb pairs"});
  f.fc_clx = U32(word, pair_start + 33 * 8);
  f.lcb_clx = U32(word, pair_start + 33 * 8 + 4);
  auto pair = [&](size_t i) -> Fib::Range {
    if (i >= pairs)
      return {};
    return {U32(word, pair_start + i * 8), U32(word, pair_start + i * 8 + 4)};
  };
  f.stsh = pair(0);
  f.sed = pair(6);
  f.chpx_bte = pair(12);
  f.papx_bte = pair(13);
  f.fonts = pair(15);
  f.headers = pair(11);
  f.fields_main = pair(16);
  // [MS-DOC] FibRgFcLcb97 rows 18-20 (1-based): fcPlcfFldHdr/Ftn/Atn. The
  // endnote story's PLC is row 49 (fcPlcfFldEdn), right after fcPlcfendTxt;
  // row 21 (fcPlcfFldMcr) is undefined per spec and stays unread.
  f.fields_hdr = pair(17);
  f.fields_ftn = pair(18);
  f.fields_atn = pair(19);
  f.fields_edn = pair(48);
  f.and_ref = pair(4);
  f.and_txt = pair(5);
  f.atn_owners = pair(36);
  f.atn_bkmk = pair(37);
  f.atn_bkf = pair(42);
  f.atn_bkl = pair(43);
  f.sttb_bkmk = pair(21);
  f.bkf = pair(22);
  f.bkl = pair(23);
  // [MS-DOC] FibRgFcLcb97: SttbfRMark is row 52 of the pair table, read here
  // as pair(51) (0-based, anchored by fcClx = pair(33) = 0x01A2), right after
  // fcDggInfo = pair(50). It names the authors of sprmCIbstRMark /
  // sprmCIbstRMarkDel / sprmCPropRMark. Row 25 (pair(24)) is fcCmds.
  f.rmark_authors = pair(51);
  f.fnd_ref = pair(2);
  f.fnd_txt = pair(3);
  // FibRgFcLcb97 pairs 44/45 are fcPms/fcFormFldSttbs; the endnote PLCs are
  // 46/47 (verified against a Word 2002 (nFib 0x101) sample).
  f.end_ref = pair(46);
  f.end_txt = pair(47);
  // [MS-DOC] FibRgFcLcb97: PlcfSpaMom and OfficeArtContent.
  constexpr size_t kSpaMainPair = 40, kDrawingGroupPair = 50;
  f.spa_main = pair(kSpaMainPair);
  f.spa_hdr = pair(41);
  f.drawing_group = pair(kDrawingGroupPair);
  // [MS-DOC] FibRgFcLcb97: textbox story ranges and flow break descriptors.
  f.txbx_txt = pair(56);
  f.txbx_bkd = pair(75);
  f.hdr_txbx_txt = pair(58);
  // FibRgFcLcb97 pairs 57/59 are PlcfFldTxbx/PlcffldHdrTxbx.
  f.fields_txbx = pair(57);
  f.fields_hdr_txbx = pair(59);
  f.hdr_txbx_bkd = pair(76);
  f.lists = pair(73);
  f.list_overrides = pair(74);
  const size_t new_pos = pair_start + static_cast<size_t>(pairs) * 8;
  if (new_pos + 2 <= word.size()) {
    const size_t csw_new = U16(word, new_pos);
    if (csw_new && csw_new * 2 <= word.size() - new_pos - 2) {
      f.n_fib = U16(word, new_pos + 2);
      if (f.n_fib < 0x00C1 || f.n_fib > 0x011A)
        return std::unexpected(Error{"unsupported nFib version"});
    }
  }
  return f;
}
}  // namespace doccraft::doc
