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

std::expected<Fib, Error> ParseFib(std::span<const uint8_t> word) {
  if (word.size() < 34)
    return std::unexpected(Error{"truncated FIB base"});
  if (U16(word, 0) != 0xA5EC)
    return std::unexpected(Error{"invalid WordDocument signature"});
  Fib f;
  f.n_fib = U16(word, 2);
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
                                     ? "XOR obfuscation unsupported (M6)"
                                     : "RC4 or RC4 CryptoAPI encryption "
                                       "unsupported (M6)"});
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
  f.and_ref = pair(4);
  f.and_txt = pair(5);
  f.atn_owners = pair(36);
  f.sttb_bkmk = pair(21);
  f.bkf = pair(22);
  f.bkl = pair(23);
  f.fnd_ref = pair(2);
  f.fnd_txt = pair(3);
  // FibRgFcLcb97 pairs 44/45 are fcPms/fcFormFldSttbs; the endnote PLCs are
  // 46/47 (verified against a Word 2002 (nFib 0x101) sample).
  f.end_ref = pair(46);
  f.end_txt = pair(47);
  // [MS-DOC] FibRgFcLcb97: PlcfSpaMom and OfficeArtContent.
  constexpr size_t kSpaMainPair = 40, kDrawingGroupPair = 50;
  f.spa_main = pair(kSpaMainPair);
  f.drawing_group = pair(kDrawingGroupPair);
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
