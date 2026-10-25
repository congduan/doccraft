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

#ifndef SRC_BASE_BINARY_H_
#define SRC_BASE_BINARY_H_

#include <cstddef>
#include <cstdint>
#include <span>

namespace doccraft::base {

// Non-owning view over read-only bytes. Every binary layer passes buffers in
// this form, so a slice never loses its length.
using Bytes = std::span<const uint8_t>;

// Little-endian scalar reads shared by the CFB container layer and the
// [MS-DOC] parser: both on-disk formats are little-endian ([MS-CFB] §2.2,
// [MS-DOC] §2.5.1), so the byte order lives here and nowhere else.
//
// These are unchecked by design. Callers slice with `Bytes::subspan` or check
// `p + sizeof(T) <= b.size()` before reading; adding a bounds-checking cursor
// is deferred to the M6 hardening pass (docs/KNOWN-SIMPLIFICATIONS.md) because
// it changes every call site rather than just its definition.
inline uint16_t U16(Bytes b, size_t p) {
  return static_cast<uint16_t>(b[p]) | static_cast<uint16_t>(b[p + 1]) << 8;
}

inline uint32_t U32(Bytes b, size_t p) {
  return static_cast<uint32_t>(U16(b, p)) | static_cast<uint32_t>(U16(b, p + 2))
                                                << 16;
}

inline uint64_t U64(Bytes b, size_t p) {
  return static_cast<uint64_t>(U32(b, p)) | static_cast<uint64_t>(U32(b, p + 4))
                                                << 32;
}

// Two's-complement 16-bit operand (e.g. sprmSDxaLeft in [MS-DOC] §2.6.1).
inline int16_t S16(Bytes b, size_t p) {
  return static_cast<int16_t>(U16(b, p));
}

}  // namespace doccraft::base

#endif  // SRC_BASE_BINARY_H_
