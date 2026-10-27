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

#include "crypto/sha1.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <vector>

namespace doccraft::crypto {

std::array<uint8_t, 20> Sha1(std::span<const uint8_t> input) {
  std::vector<uint8_t> bytes(input.begin(), input.end());
  const uint64_t bit_count = static_cast<uint64_t>(bytes.size()) * 8;
  bytes.push_back(0x80);
  while (bytes.size() % 64 != 56)
    bytes.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<uint8_t>(bit_count >> shift));
  uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  for (size_t offset = 0; offset < bytes.size(); offset += 64) {
    uint32_t w[80] = {};
    for (size_t n = 0; n < 16; ++n) {
      const size_t p = offset + n * 4;
      w[n] = (static_cast<uint32_t>(bytes[p]) << 24) |
             (static_cast<uint32_t>(bytes[p + 1]) << 16) |
             (static_cast<uint32_t>(bytes[p + 2]) << 8) | bytes[p + 3];
    }
    for (size_t n = 16; n < 80; ++n)
      w[n] = std::rotl(w[n - 3] ^ w[n - 8] ^ w[n - 14] ^ w[n - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (size_t n = 0; n < 80; ++n) {
      uint32_t f = 0, k = 0;
      if (n < 20) {
        f = (b & c) | (~b & d);
        k = 0x5a827999;
      } else if (n < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1;
      } else if (n < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdc;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6;
      }
      const uint32_t temp = std::rotl(a, 5) + f + e + k + w[n];
      e = d;
      d = c;
      c = std::rotl(b, 30);
      b = a;
      a = temp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  std::array<uint8_t, 20> digest{};
  for (size_t n = 0; n < 20; ++n)
    digest[n] = static_cast<uint8_t>(h[n / 4] >> (24 - 8 * (n % 4)));
  return digest;
}

}  // namespace doccraft::crypto
