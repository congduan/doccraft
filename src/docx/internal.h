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

#ifndef SRC_DOCX_INTERNAL_H_
#define SRC_DOCX_INTERNAL_H_

#include <cstddef>

#include "docx/writer.h"

namespace doccraft::docx::detail {
// Maximum sum of serialized OPC part payloads held before ZIP packing.
inline constexpr size_t kMaxWriterBufferBytes = 512 * 1024 * 1024;
// Maximum serialized size of any one OPC part, including document, headers,
// footers, media and embedded objects; checked before adding the part.
inline constexpr size_t kMaxWriterPartBytes = 512 * 1024 * 1024;
// Internal limit injection for writer tests; production callers use Write().
std::expected<std::vector<uint8_t>, Error> WriteWithBufferLimit(
    const model::Document& document, size_t max_buffer_bytes,
    size_t max_part_bytes = kMaxWriterPartBytes);
}  // namespace doccraft::docx::detail

#endif  // SRC_DOCX_INTERNAL_H_
