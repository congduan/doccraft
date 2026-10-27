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

#ifndef SRC_CRYPTO_XOR_H_
#define SRC_CRYPTO_XOR_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace doccraft::crypto {

struct XorKey {
  uint32_t verifier;
  std::array<uint8_t, 16> mask;
};

std::expected<XorKey, std::string_view> DeriveXorKey(std::string_view password);
void XorTransform(std::span<uint8_t> bytes, size_t plain_prefix,
                  const std::array<uint8_t, 16>& mask);

}  // namespace doccraft::crypto
#endif  // SRC_CRYPTO_XOR_H_
