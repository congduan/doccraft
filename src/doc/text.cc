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

#include "doc/text.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
namespace doccraft::doc {
namespace {
uint16_t U16(std::span<const uint8_t> b, size_t p) {
  return static_cast<uint16_t>(b[p]) | static_cast<uint16_t>(b[p + 1]) << 8;
}

uint32_t U32(std::span<const uint8_t> b, size_t p) {
  return static_cast<uint32_t>(U16(b, p)) | static_cast<uint32_t>(U16(b, p + 2))
                                                << 16;
}

void Utf8(std::string& out, uint32_t c) {
  if (c < 0x80) {
    out += static_cast<char>(c);
  } else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 63));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 63));
    out += static_cast<char>(0x80 | (c & 63));
  } else {
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 63));
    out += static_cast<char>(0x80 | ((c >> 6) & 63));
    out += static_cast<char>(0x80 | (c & 63));
  }
}

uint32_t Cp1252(uint8_t c) {
  constexpr uint16_t kMap[32] = {
      0x20AC, 0x81,   0x201A, 0x192,  0x201E, 0x2026, 0x2020, 0x2021,
      0x2C6,  0x2030, 0x160,  0x2039, 0x152,  0x8D,   0x17D,  0x8F,
      0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x2DC,  0x2122, 0x161,  0x203A, 0x153,  0x9D,   0x17E,  0x178};
  return c >= 0x80 && c <= 0x9F ? kMap[c - 0x80] : c;
}

void Append(std::string& out, uint32_t c) {
  // U+FFFE/U+FFFF are not in the XML 1.0 Char production; emit U+FFFD so a
  // corrupted piece can never produce a not-well-formed document.xml.
  if (c == 0xFFFE || c == 0xFFFF)
    c = 0xFFFD;
  Utf8(out, c == 0x0D ? 0x0A : c);
}
}  // namespace
std::expected<std::string, Error> ExtractText(std::span<const uint8_t> word,
                                              std::span<const uint8_t> table,
                                              const Fib& fib) {
  if (fib.complex)
    return std::unexpected(
        Error{"fast-saved document unsupported (M2 known gap)"});
  if (fib.fc_clx > table.size() || fib.lcb_clx > table.size() - fib.fc_clx)
    return std::unexpected(Error{"CLX outside table stream"});
  auto clx = table.subspan(fib.fc_clx, fib.lcb_clx);
  size_t pos = 0;
  while (pos < clx.size() && clx[pos] == 1) {
    if (clx.size() - pos < 3)
      return std::unexpected(Error{"truncated CLX Prc"});
    const size_t n = U16(clx, pos + 1);
    if (n > clx.size() - pos - 3)
      return std::unexpected(Error{"truncated CLX grpprl"});
    pos += 3 + n;
  }
  if (clx.size() - pos < 5 || clx[pos] != 2)
    return std::unexpected(Error{"missing CLX Pcdt"});
  const size_t size = U32(clx, pos + 1);
  pos += 5;
  if (size > clx.size() - pos || size < 16 || (size - 4) % 12 != 0 ||
      pos + size != clx.size())
    return std::unexpected(Error{"invalid PlcPcd size"});
  const size_t count = (size - 4) / 12;
  const size_t pcd = pos + 4 * (count + 1);
  std::string out;
  size_t emitted = 0;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t cp0 = U32(clx, pos + 4 * i),
                   cp1 = U32(clx, pos + 4 * (i + 1));
    if (cp1 <= cp0)
      return std::unexpected(Error{"invalid piece CP order"});
    if (cp0 >= fib.ccp_text)
      break;
    const uint32_t length = std::min(cp1, fib.ccp_text) - cp0;
    if (length > word.size() - emitted)
      return std::unexpected(Error{"piece table over-covers WordDocument"});
    emitted += length;
    const uint32_t raw = U32(clx, pcd + 8 * i + 2);
    const bool compressed = raw & 0x40000000u;
    const uint64_t start =
        compressed ? (raw & 0x3FFFFFFFu) / 2 : raw & 0x3FFFFFFFu;
    const uint64_t bytes = static_cast<uint64_t>(length) * (compressed ? 1 : 2);
    if (start > word.size() || bytes > word.size() - start)
      return std::unexpected(Error{"piece outside WordDocument"});
    for (uint32_t j = 0; j < length; ++j) {
      uint32_t c =
          compressed ? Cp1252(word[start + j]) : U16(word, start + 2 * j);
      if (!compressed && c >= 0xD800 && c <= 0xDBFF) {
        if (++j >= length)
          return std::unexpected(Error{"unpaired UTF-16 surrogate"});
        const uint32_t low = U16(word, start + 2 * j);
        if (low < 0xDC00 || low > 0xDFFF)
          return std::unexpected(Error{"unpaired UTF-16 surrogate"});
        c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
      } else if (!compressed && c >= 0xDC00 && c <= 0xDFFF) {
        return std::unexpected(Error{"unpaired UTF-16 surrogate"});
      }

      Append(out, c);
    }
  }
  return out;
}
}  // namespace doccraft::doc
