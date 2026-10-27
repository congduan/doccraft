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

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "crypto/derivation.h"
#include "crypto/rc4.h"
#include "crypto/sha1.h"
#include "doc/encryption.h"
#include "gtest/gtest.h"

namespace {
void Put16(std::vector<uint8_t>& bytes, size_t pos, uint16_t value) {
  bytes[pos] = static_cast<uint8_t>(value);
  bytes[pos + 1] = static_cast<uint8_t>(value >> 8);
}
void Put32(std::vector<uint8_t>& bytes, size_t pos, uint32_t value) {
  for (size_t n = 0; n < 4; ++n)
    bytes[pos + n] = static_cast<uint8_t>(value >> (n * 8));
}
void Crypt(std::vector<uint8_t>& bytes, size_t skip,
           std::span<const uint8_t, 20> hash) {
  for (size_t start = 0; start < bytes.size(); start += 512) {
    auto key = doccraft::crypto::DeriveRc4Key(hash, start / 512, 128);
    doccraft::crypto::Rc4 rc4(key);
    const size_t end = std::min(start + 512, bytes.size());
    std::vector<uint8_t> discard(std::min(end, skip) - std::min(start, skip));
    rc4.Xor(discard);
    const size_t begin = start + discard.size();
    rc4.Xor(std::span(bytes).subspan(begin, end - begin));
  }
}
struct Streams {
  std::vector<uint8_t> word, table, data;
};
Streams Encrypted() {
  Streams result{std::vector<uint8_t>(1024), std::vector<uint8_t>(720),
                 std::vector<uint8_t>(600)};
  Put16(result.word, 0, 0xa5ec);
  Put16(result.word, 2, 0x00c1);
  Put16(result.word, 10, 0x0100);
  Put32(result.word, 14, 104);
  Put16(result.word, 32, 14);
  Put16(result.word, 62, 22);
  Put16(result.word, 152, 34);
  Put32(result.word, 154 + 33 * 8, 104);
  Put32(result.word, 158 + 33 * 8, 20);
  for (size_t n = 68; n < result.word.size(); ++n)
    result.word[n] = static_cast<uint8_t>(n);
  // Restore the parseable FIB fields after the stream pattern.
  Put16(result.word, 62, 22);
  Put16(result.word, 152, 34);
  Put32(result.word, 154 + 33 * 8, 104);
  Put32(result.word, 158 + 33 * 8, 20);
  Put16(result.table, 0, 2);
  Put16(result.table, 2, 2);
  Put32(result.table, 4, 4);
  Put32(result.table, 8, 32);
  Put32(result.table, 12, 4);
  Put32(result.table, 20, 0x6801);
  Put32(result.table, 24, 0x8004);
  Put32(result.table, 28, 128);
  Put32(result.table, 32, 1);
  Put32(result.table, 44, 16);
  std::array<uint8_t, 16> salt{};
  std::array<uint8_t, 16> verifier{};
  for (size_t n = 0; n < 16; ++n) {
    salt[n] = static_cast<uint8_t>(n);
    verifier[n] = static_cast<uint8_t>(0xa0 + n);
    result.table[48 + n] = salt[n];
  }
  Put32(result.table, 80, 20);
  const auto hash = *doccraft::crypto::HashPassword("password", salt);
  const auto key = doccraft::crypto::DeriveRc4Key(hash, 0, 128);
  doccraft::crypto::Rc4 rc4(key);
  rc4.Xor(verifier);
  std::copy(verifier.begin(), verifier.end(), result.table.begin() + 64);
  std::array<uint8_t, 16> plain_verifier{};
  for (size_t n = 0; n < 16; ++n)
    plain_verifier[n] = static_cast<uint8_t>(0xa0 + n);
  auto verifier_hash = doccraft::crypto::Sha1(plain_verifier);
  rc4.Xor(verifier_hash);
  std::copy(verifier_hash.begin(), verifier_hash.end(),
            result.table.begin() + 84);
  for (size_t n = 104; n < result.table.size(); ++n)
    result.table[n] = static_cast<uint8_t>(n);
  for (size_t n = 0; n < result.data.size(); ++n)
    result.data[n] = static_cast<uint8_t>(n);
  Crypt(result.word, 68, hash);
  Crypt(result.table, 104, hash);
  Crypt(result.data, 0, hash);
  return result;
}
}  // namespace

TEST(M6Rc4, DecryptsAllStreamsAcrossBlocks) {
  auto streams = Encrypted();
  auto fib = doccraft::doc::DecryptRc4(streams.word, streams.table,
                                       streams.data, "password");
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_FALSE(fib->encrypted);
  EXPECT_EQ(fib->fc_clx, 104u);
  EXPECT_EQ(streams.word[513], static_cast<uint8_t>(513));
  EXPECT_EQ(streams.table[513], static_cast<uint8_t>(513));
  EXPECT_EQ(streams.data[513], static_cast<uint8_t>(513));
}

TEST(M6Rc4, RejectsWrongPasswordAndTruncatedHeader) {
  auto streams = Encrypted();
  auto bad = doccraft::doc::DecryptRc4(streams.word, streams.table,
                                       streams.data, "wrong");
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error().message, "incorrect document password");
  streams.table.resize(50);
  bad = doccraft::doc::DecryptRc4(streams.word, streams.table, streams.data,
                                  "password");
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error().message, "truncated RC4 encryption header");
}
