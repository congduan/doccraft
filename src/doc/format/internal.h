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

#ifndef SRC_DOC_FORMAT_INTERNAL_H_
#define SRC_DOC_FORMAT_INTERNAL_H_
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "doc/format.h"

namespace doccraft::doc::detail {
using Bytes = std::span<const uint8_t>;

// [MS-DOC] §2.9.1: frame geometry is expressed in twips and capped at 22 inch,
// the largest value Word writes for a page object.
inline constexpr uint16_t kMaxTwips = 31680;

// Operands that mean "absent"/"automatic" carry a saturated value instead of a
// separate flag.
inline constexpr uint16_t kUndefinedU16 = 0xFFFF;
inline constexpr uint32_t kUndefinedU32 = 0xFFFFFFFF;

// sprmPWHeightAbs: bit 15 marks an exact height, bits 0-14 hold the height.
inline constexpr uint16_t kFrameHeightExact = 0x8000;
inline constexpr uint16_t kFrameHeightMask = 0x7FFF;

// [MS-DOC] §2.5.1 text is UTF-16LE; a surrogate pair encodes one code point.
inline constexpr uint32_t kUtf16HighSurrogateMin = 0xD800;
inline constexpr uint32_t kUtf16HighSurrogateMax = 0xDBFF;
inline constexpr uint32_t kUtf16LowSurrogateMin = 0xDC00;
inline constexpr uint32_t kUtf16LowSurrogateMax = 0xDFFF;
inline constexpr uint32_t kUtf16SurrogateBase = 0x10000;

// Code points that must not reach the XML output: the noncharacters U+FFFE /
// U+FFFF and lone surrogates are replaced by U+FFFD.
inline constexpr uint32_t kReplacementCharacter = 0xFFFD;
inline constexpr uint32_t kNonCharacterMin = 0xFFFE;

// PlcPcd.PnFkp carries the FKP page number in its low 22 bits.
inline constexpr uint32_t kPnFkpMask = 0x3FFFFF;

// CP1252 runs (0x80-0x9F) are remapped to Unicode by a 32-entry table.
inline constexpr uint8_t kCp1252First = 0x80;
inline constexpr uint8_t kCp1252Last = 0x9F;

// PlcPcd entry: bit 30 marks CP1252 ("compressed") text, bits 0-29 hold the
// offset inside the WordDocument stream.
inline constexpr uint32_t kFcCompressed = 0x40000000;
inline constexpr uint32_t kFcMask = 0x3FFFFFFF;
inline uint16_t U16(Bytes b, size_t p) {
  return static_cast<uint16_t>(b[p]) | static_cast<uint16_t>(b[p + 1]) << 8;
}

inline uint32_t U32(Bytes b, size_t p) {
  return static_cast<uint32_t>(U16(b, p)) | static_cast<uint32_t>(U16(b, p + 2))
                                                << 16;
}

inline int16_t S16(Bytes b, size_t p) {
  return static_cast<int16_t>(U16(b, p));
}

inline void Utf8(std::string& s, uint32_t c) {
  if (c < 0x80) {
    s += static_cast<char>(c);
  } else if (c < 0x800) {
    s += static_cast<char>(0xc0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 63));
  } else if (c < 0x10000) {
    s += static_cast<char>(0xe0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 63));
    s += static_cast<char>(0x80 | (c & 63));
  } else {
    s += static_cast<char>(0xf0 | (c >> 18));
    s += static_cast<char>(0x80 | ((c >> 12) & 63));
    s += static_cast<char>(0x80 | ((c >> 6) & 63));
    s += static_cast<char>(0x80 | (c & 63));
  }
}

struct Char {
  std::string text;
  uint32_t cp, fc;
  bool newline;
};

struct FormatRange {
  uint32_t first, last;
  std::vector<uint8_t> sprms;
  int style = -1;
};

inline bool Valid(Bytes b, Fib::Range r) {
  return r.offset <= b.size() && r.size <= b.size() - r.offset;
}

std::expected<std::vector<Char>, Error> ReadChars(Bytes word, Bytes table,
                                                  const Fib& fib,
                                                  uint32_t limit);

std::expected<std::vector<FormatRange>, Error> ReadFkps(Bytes word, Bytes table,
                                                        Fib::Range r,
                                                        bool para);

const FormatRange* Find(const std::vector<FormatRange>& a, uint32_t fc);

std::vector<std::string> ReadFonts(Bytes table, Fib::Range r);

std::vector<model::Style> ReadStyles(Bytes table, Fib::Range r,
                                     const std::vector<std::string>& fonts);

void ReadLists(model::Document& doc, Bytes table, const Fib& fib);

model::Run InheritedRun(const model::Document& doc, int style);

std::expected<void, Error> ReadHeaders(model::Document& doc, Bytes word,
                                       Bytes table, const Fib& fib,
                                       const std::vector<FormatRange>& chpx,
                                       const std::vector<FormatRange>& papx);
}  // namespace doccraft::doc::detail

#endif  // SRC_DOC_FORMAT_INTERNAL_H_
