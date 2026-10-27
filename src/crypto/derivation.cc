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

#include "crypto/derivation.h"

#include <cstddef>

#include "crypto/sha1.h"

namespace doccraft::crypto {

std::expected<std::array<uint8_t, 20>, std::string_view> HashPassword(
    std::string_view password, std::span<const uint8_t, 16> salt) {
  std::vector<uint8_t> bytes(salt.begin(), salt.end());
  size_t units = 0;
  for (size_t i = 0; i < password.size();) {
    const uint8_t first = static_cast<uint8_t>(password[i]);
    uint32_t cp = 0;
    size_t count = 0;
    if (first < 0x80) {
      cp = first;
      count = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      cp = first & 0x1f;
      count = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      cp = first & 0x0f;
      count = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      cp = first & 0x07;
      count = 4;
    } else {
      return std::unexpected("invalid UTF-8 password");
    }
    if (count > password.size() - i)
      return std::unexpected("invalid UTF-8 password");
    for (size_t j = 1; j < count; ++j) {
      const uint8_t next = static_cast<uint8_t>(password[i + j]);
      if ((next & 0xc0) != 0x80)
        return std::unexpected("invalid UTF-8 password");
      cp = (cp << 6) | (next & 0x3f);
    }
    if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) ||
        (count == 4 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff))
      return std::unexpected("invalid UTF-8 password");
    auto append = [&](uint16_t unit) {
      bytes.push_back(static_cast<uint8_t>(unit));
      bytes.push_back(static_cast<uint8_t>(unit >> 8));
      ++units;
    };
    if (cp < 0x10000) {
      append(static_cast<uint16_t>(cp));
    } else {
      cp -= 0x10000;
      append(static_cast<uint16_t>(0xd800 | (cp >> 10)));
      append(static_cast<uint16_t>(0xdc00 | (cp & 0x3ff)));
    }
    if (units > 255) return std::unexpected("password exceeds 255 UTF-16 units");
    i += count;
  }
  return Sha1(bytes);
}

std::vector<uint8_t> DeriveRc4Key(
    std::span<const uint8_t, 20> password_hash, uint32_t block,
    uint32_t key_bits) {
  std::array<uint8_t, 24> input{};
  for (size_t i = 0; i < 20; ++i) input[i] = password_hash[i];
  for (size_t i = 0; i < 4; ++i)
    input[20 + i] = static_cast<uint8_t>(block >> (i * 8));
  const auto digest = Sha1(input);
  if (key_bits == 40) {
    std::vector<uint8_t> key(digest.begin(), digest.begin() + 5);
    key.resize(16);
    return key;
  }
  return {digest.begin(), digest.begin() + key_bits / 8};
}

}  // namespace doccraft::crypto
