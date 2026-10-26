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
#include "doc/format/internal.h"

namespace doccraft::doc::detail {

using base::DecodeAnsiChar;

std::expected<std::vector<Char>, Error> ReadChars(Bytes word, Bytes table,
                                                  const Fib& fib,
                                                  uint32_t limit) {
  if (fib.complex)
    return std::unexpected(
        Error{"fast-saved document unsupported (M2 known gap)"});
  if (!Valid(table, {fib.fc_clx, fib.lcb_clx}))
    return std::unexpected(Error{"CLX outside table stream"});
  Bytes b = table.subspan(fib.fc_clx, fib.lcb_clx);
  size_t p = 0;
  while (p < b.size() && b[p] == 1) {
    if (p + 3 > b.size())
      return std::unexpected(Error{"truncated CLX Prc"});
    size_t n = U16(b, p + 1);
    if (n > b.size() - p - 3)
      return std::unexpected(Error{"truncated CLX grpprl"});
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
  std::vector<Char> out;
  // Each emitted character consumes at least one WordDocument byte. Reserve
  // the CP upper bound up front so a large body never briefly holds both
  // sides of a geometric vector reallocation.
  out.reserve(std::min<size_t>(limit, word.size()));
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
    bool compressed = raw & kFcCompressed;
    uint64_t start = compressed ? (raw & kFcMask) / 2 : raw & kFcMask;
    uint64_t bytes = static_cast<uint64_t>(length) * (compressed ? 1 : 2);
    if (start > word.size() || bytes > word.size() - start)
      return std::unexpected(Error{"piece outside WordDocument"});
    for (uint32_t j = 0; j < length;) {
      uint32_t character_cp = cp + j;
      uint32_t fc = start + j * (compressed ? 1 : 2);
      if (compressed) {
        // Compressed pieces use the document's ANSI code page (FibBase.lid).
        Char ch{character_cp, fc};
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
        ch.SetText(decoded);
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
      ch.newline = c == 13;
      std::string encoded;
      AppendUtf8(encoded, c == 13 ? 10 : c);
      ch.SetText(encoded);
      out.push_back(std::move(ch));
      j += step;
    }
  }
  return out;
}

std::expected<std::vector<FormatRange>, Error> ReadFkps(Bytes word, Bytes table,
                                                        Fib::Range r,
                                                        bool para) {
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
      }
      out.push_back(std::move(x));
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
