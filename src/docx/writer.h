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

#ifndef SRC_DOCX_WRITER_H_
#define SRC_DOCX_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "model/document.h"
namespace doccraft::docx {
struct Error {
  std::string message;
};

uint32_t Crc32(std::string_view bytes);

std::expected<std::vector<uint8_t>, Error> Write(
    const model::Document& document);
// Internal limit injection for writer tests; production callers use Write().
std::expected<std::vector<uint8_t>, Error> WriteWithBufferLimit(
    const model::Document& document, size_t max_buffer_bytes);
}  // namespace doccraft::docx

#endif  // SRC_DOCX_WRITER_H_
