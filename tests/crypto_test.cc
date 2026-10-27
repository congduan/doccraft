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

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "crypto/rc4.h"
#include "crypto/sha1.h"
#include "gtest/gtest.h"

namespace {
std::string Hex(std::span<const uint8_t> bytes) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  for (uint8_t byte : bytes) {
    out += kDigits[byte >> 4];
    out += kDigits[byte & 15];
  }
  return out;
}
}  // namespace

TEST(Crypto, Rc4Rfc6229FortyBit) {
  const std::array<uint8_t, 5> key{1, 2, 3, 4, 5};
  std::vector<uint8_t> bytes(528);
  doccraft::crypto::Rc4 rc4(key);
  rc4.Xor(bytes);
  EXPECT_EQ(Hex(std::span(bytes).first(16)),
            "b2396305f03dc027ccc3524a0a1118a8");
  EXPECT_EQ(Hex(std::span(bytes).subspan(512, 16)),
            "6459844432a7da923cfb3eb4980661f6");
}

TEST(Crypto, Sha1Fips180Four) {
  const std::string_view input = "abc";
  const auto* data = reinterpret_cast<const uint8_t*>(input.data());
  EXPECT_EQ(Hex(doccraft::crypto::Sha1({data, input.size()})),
            "a9993e364706816aba3e25717850c26c9cd0d89d");
  EXPECT_EQ(Hex(doccraft::crypto::Sha1({})),
            "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}
