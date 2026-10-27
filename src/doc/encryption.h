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

#ifndef SRC_DOC_ENCRYPTION_H_
#define SRC_DOC_ENCRYPTION_H_

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include "doc/fib.h"

namespace doccraft::doc {

std::expected<std::string_view, Error> DetectRc4Type(
    std::span<const uint8_t> word, std::span<const uint8_t> table);

// Decrypts the three CFB streams in place and parses the resulting FIB.
std::expected<Fib, Error> DecryptRc4(std::span<uint8_t> word,
                                     std::span<uint8_t> table,
                                     std::span<uint8_t> data,
                                     std::string_view password);

std::expected<Fib, Error> DecryptXor(std::span<uint8_t> word,
                                     std::span<uint8_t> table,
                                     std::span<uint8_t> data,
                                     std::string_view password);

}  // namespace doccraft::doc
#endif  // SRC_DOC_ENCRYPTION_H_
