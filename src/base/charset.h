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

#ifndef SRC_BASE_CHARSET_H_
#define SRC_BASE_CHARSET_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "base/binary.h"

namespace doccraft::base {

// Compressed ("8-bit") pieces in [MS-DOC] §2.4.1 are encoded in the document's
// ANSI code page, which FibBase.lid selects. These helpers keep that mapping
// and the decoding in one place: no layer below the writer may assume 1252.

// Default ANSI code page used when the LID is unknown or zero.
inline constexpr uint32_t kDefaultCodepage = 1252;

// Maps a Windows LID (FibBase.lid, [MS-DOC] §2.5.1) to its ANSI code page.
uint32_t CodepageForLid(uint16_t lid);

// True when this build can decode `codepage` (iconv present, or 1252 itself).
bool SupportsCodepage(uint32_t codepage);

// Decodes the character at the start of `in` (UTF-8 output appended to `out`).
// Returns the number of input bytes consumed, or 0 when the bytes are not a
// valid character in `codepage` (the caller then emits U+FFFD and skips one).
size_t DecodeAnsiChar(uint32_t codepage, Bytes in, std::string& out);

}  // namespace doccraft::base

#endif  // SRC_BASE_CHARSET_H_
