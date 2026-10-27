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

#include "crypto/xor.h"

namespace doccraft::crypto {
namespace {
uint16_t Rotate16(uint16_t value) {
  return static_cast<uint16_t>((value << 1) | (value >> 15));
}
uint16_t StepVerifier(uint16_t value, uint8_t ch) {
  return static_cast<uint16_t>((((value << 1) & 0x7FFF) | ((value >> 14) & 1)) ^
                               ch);
}
}  // namespace

std::expected<XorKey, std::string_view> DeriveXorKey(
    std::string_view password) {
  // [MS-OFFCRYPTO] 2.3.7.4 requires at most 15 single-byte characters.
  if (password.empty() || password.size() > 15)
    return std::unexpected("XOR password must contain 1-15 bytes");
  for (unsigned char ch : password) {
    if (ch == 0 || ch > 0x7F)
      return std::unexpected("XOR password must be ASCII");
  }
  // Equivalent recurrence to CreateXorKey_Method1 in [MS-OFFCRYPTO]
  // 2.3.7.2; see LibreOffice MSCodec_Xor95::InitKey/lclGetKey.
  uint16_t base = 0x8000, end = 0xFFFF, key = 0;
  for (auto it = password.rbegin(); it != password.rend(); ++it) {
    uint8_t ch = static_cast<uint8_t>(*it);
    for (int bit = 0; bit < 8; ++bit) {
      base = Rotate16(base);
      if (base & 1)
        base ^= 0x1020;
      if (ch & 1)
        key ^= base;
      ch >>= 1;
      end = Rotate16(end);
      if (end & 1)
        end ^= 0x1020;
    }
  }
  key ^= end;
  uint16_t verifier = 0;
  for (auto it = password.rbegin(); it != password.rend(); ++it)
    verifier = StepVerifier(verifier, static_cast<uint8_t>(*it));
  verifier = StepVerifier(verifier, static_cast<uint8_t>(password.size()));
  verifier ^= 0xCE4B;
  constexpr std::array<uint8_t, 16> kPad = {0xBB, 0xFF, 0xFF, 0xBA, 0xFF, 0xFF,
                                            0xB9, 0x80, 0x00, 0xBE, 0x0F, 0x00,
                                            0xBF, 0x0F, 0x00, 0x00};
  XorKey result{(static_cast<uint32_t>(key) << 16) | verifier, {}};
  for (size_t i = 0; i < result.mask.size(); ++i) {
    const uint8_t ch = i < password.size() ? static_cast<uint8_t>(password[i])
                                           : kPad[i - password.size()];
    const uint8_t half = static_cast<uint8_t>(i % 2 ? key >> 8 : key);
    const uint8_t x = ch ^ half;
    result.mask[i] = static_cast<uint8_t>((x >> 1) | (x << 7));
  }
  return result;
}

void XorTransform(std::span<uint8_t> bytes, size_t plain_prefix,
                  const std::array<uint8_t, 16>& mask) {
  // [MS-OFFCRYPTO] 2.3.7.6: both zero input and zero XOR result stay intact.
  // The mask position still advances over the plaintext FIB prefix.
  for (size_t i = plain_prefix; i < bytes.size(); ++i) {
    const uint8_t changed = bytes[i] ^ mask[i % mask.size()];
    if (bytes[i] && changed)
      bytes[i] = changed;
  }
}
}  // namespace doccraft::crypto
