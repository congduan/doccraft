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
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

#include "base/binary.h"
#include "cfb/cfb_reader.h"
#include "crypto/derivation.h"
#include "crypto/md5.h"
#include "crypto/rc4.h"
#include "crypto/sha1.h"
#include "tests/cfb_test_util.h"

namespace {
void Put16(std::vector<uint8_t>& bytes, size_t pos, uint16_t value) {
  bytes[pos] = static_cast<uint8_t>(value);
  bytes[pos + 1] = static_cast<uint8_t>(value >> 8);
}
void Put32(std::vector<uint8_t>& bytes, size_t pos, uint32_t value) {
  for (size_t n = 0; n < 4; ++n)
    bytes[pos + n] = static_cast<uint8_t>(value >> (8 * n));
}
uint16_t Rotate16(uint16_t value) {
  return static_cast<uint16_t>((value << 1) | (value >> 15));
}
void XorTransform(std::vector<uint8_t>& bytes, size_t skip,
                  const std::array<uint8_t, 16>& key) {
  for (size_t i = skip; i < bytes.size(); ++i) {
    const uint8_t changed = bytes[i] ^ key[i % key.size()];
    if (bytes[i] && changed)
      bytes[i] = changed;
  }
}
int MakeXor(const char* input_path, const char* output_path) {
  std::ifstream input(input_path, std::ios::binary);
  std::vector<uint8_t> source{std::istreambuf_iterator<char>(input), {}};
  auto cfb = doccraft::cfb::CfbReader::Open(source);
  if (!cfb)
    return 1;
  auto word = cfb->ReadStream("WordDocument");
  auto table = cfb->ReadStream("1Table");
  auto data = cfb->ReadStream("Data");
  if (!word || !table)
    return 1;
  constexpr std::string_view password = "doccraft-m6";
  uint16_t base = 0x8000, end = 0xFFFF, key = 0;
  for (auto it = password.rbegin(); it != password.rend(); ++it) {
    uint8_t ch = static_cast<uint8_t>(*it) & 0x7F;
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
    verifier = static_cast<uint16_t>(
        (((verifier << 1) & 0x7FFF) | ((verifier >> 14) & 1)) ^
        static_cast<uint8_t>(*it));
  verifier = static_cast<uint16_t>(
      (((verifier << 1) & 0x7FFF) | ((verifier >> 14) & 1)) ^ password.size() ^
      0xCE4B);
  std::array<uint8_t, 16> mask{};
  constexpr std::array<uint8_t, 16> pad = {0xBB, 0xFF, 0xFF, 0xBA, 0xFF, 0xFF,
                                           0xB9, 0x80, 0x00, 0xBE, 0x0F, 0x00,
                                           0xBF, 0x0F, 0x00, 0x00};
  for (size_t i = 0; i < mask.size(); ++i) {
    const uint8_t ch = i < password.size() ? static_cast<uint8_t>(password[i])
                                           : pad[i - password.size()];
    const uint8_t half = static_cast<uint8_t>(i % 2 ? key >> 8 : key);
    const uint8_t x = ch ^ half;
    mask[i] = static_cast<uint8_t>((x >> 1) | (x << 7));
  }
  (*word)[11] |= 0x81;
  Put32(*word, 14, (static_cast<uint32_t>(key) << 16) | verifier);
  XorTransform(*word, 68, mask);
  XorTransform(*table, 0, mask);
  if (data)
    XorTransform(*data, 0, mask);
  auto builder = doccraft::cfb::test::CfbBuilder()
                     .AddStream("WordDocument", *word)
                     .AddStream("1Table", *table);
  if (data)
    builder.AddStream("Data", *data);
  auto output_bytes = builder.Build();
  std::ofstream output(output_path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(output_bytes.data()),
               static_cast<std::streamsize>(output_bytes.size()));
  return output ? 0 : 1;
}
template <typename KeyForBlock>
void Crypt(std::vector<uint8_t>& bytes, size_t skip,
           KeyForBlock key_for_block) {
  for (size_t start = 0; start < bytes.size(); start += 512) {
    auto key = key_for_block(start / 512);
    doccraft::crypto::Rc4 rc4(key);
    const size_t end = std::min(start + 512, bytes.size());
    std::vector<uint8_t> discard(std::min(end, skip) - std::min(start, skip));
    rc4.Xor(discard);
    const size_t begin = start + discard.size();
    rc4.Xor(std::span(bytes).subspan(begin, end - begin));
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 4)
    return 2;
  if (std::string_view(argv[3]) == "xor")
    return MakeXor(argv[1], argv[2]);
  const bool legacy = std::string_view(argv[3]) == "legacy";
  if (!legacy && std::string_view(argv[3]) != "cryptoapi")
    return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input)
    return 1;
  std::vector<uint8_t> source{std::istreambuf_iterator<char>(input), {}};
  auto cfb = doccraft::cfb::CfbReader::Open(source);
  if (!cfb)
    return 1;
  auto word = cfb->ReadStream("WordDocument");
  auto table = cfb->ReadStream("1Table");
  if (!word || !table)
    return 1;
  const uint32_t header_bytes = legacy ? 52 : 104;
  const size_t lw_count = 34 + doccraft::base::U16(*word, 32) * 2;
  const size_t pair_count =
      lw_count + 2 + doccraft::base::U16(*word, lw_count) * 4;
  const size_t pair_start = pair_count + 2;
  const uint16_t pairs = doccraft::base::U16(*word, pair_count);
  for (size_t n = 0; n < pairs; ++n) {
    const size_t pos = pair_start + n * 8;
    if (doccraft::base::U32(*word, pos + 4) != 0)
      Put32(*word, pos, doccraft::base::U32(*word, pos) + header_bytes);
  }
  (*word)[11] |= 1;
  Put32(*word, 14, header_bytes);
  std::vector<uint8_t> encrypted_table(header_bytes);
  encrypted_table.insert(encrypted_table.end(), table->begin(), table->end());
  Put16(encrypted_table, 0, legacy ? 1 : 2);
  Put16(encrypted_table, 2, legacy ? 1 : 2);
  std::array<uint8_t, 16> salt{};
  std::array<uint8_t, 16> verifier{};
  for (size_t n = 0; n < 16; ++n) {
    salt[n] = static_cast<uint8_t>(n + 1);
    verifier[n] = static_cast<uint8_t>(n + 0x70);
  }
  if (legacy) {
    std::copy(salt.begin(), salt.end(), encrypted_table.begin() + 4);
    const auto encoded = *doccraft::crypto::EncodePassword("doccraft-m6");
    const auto hash = doccraft::crypto::LegacyPasswordHash(encoded, salt);
    auto key_for_block = [&](uint32_t block) {
      return doccraft::crypto::DeriveLegacyRc4Key(hash, block);
    };
    doccraft::crypto::Rc4 rc4(key_for_block(0));
    auto encrypted_verifier = verifier;
    rc4.Xor(encrypted_verifier);
    std::copy(encrypted_verifier.begin(), encrypted_verifier.end(),
              encrypted_table.begin() + 20);
    auto verifier_hash = doccraft::crypto::Md5(verifier);
    rc4.Xor(verifier_hash);
    std::copy(verifier_hash.begin(), verifier_hash.end(),
              encrypted_table.begin() + 36);
    Crypt(*word, 68, key_for_block);
    Crypt(encrypted_table, header_bytes, key_for_block);
  } else {
    Put32(encrypted_table, 4, 4);
    Put32(encrypted_table, 8, 32);
    Put32(encrypted_table, 12, 4);
    Put32(encrypted_table, 20, 0x6801);
    Put32(encrypted_table, 24, 0x8004);
    Put32(encrypted_table, 28, 128);
    Put32(encrypted_table, 32, 1);
    Put32(encrypted_table, 44, 16);
    std::copy(salt.begin(), salt.end(), encrypted_table.begin() + 48);
    const auto hash = *doccraft::crypto::HashPassword("doccraft-m6", salt);
    auto key_for_block = [&](uint32_t block) {
      return doccraft::crypto::DeriveRc4Key(hash, block, 128);
    };
    doccraft::crypto::Rc4 rc4(key_for_block(0));
    auto encrypted_verifier = verifier;
    rc4.Xor(encrypted_verifier);
    std::copy(encrypted_verifier.begin(), encrypted_verifier.end(),
              encrypted_table.begin() + 64);
    Put32(encrypted_table, 80, 20);
    auto verifier_hash = doccraft::crypto::Sha1(verifier);
    rc4.Xor(verifier_hash);
    std::copy(verifier_hash.begin(), verifier_hash.end(),
              encrypted_table.begin() + 84);
    Crypt(*word, 68, key_for_block);
    Crypt(encrypted_table, header_bytes, key_for_block);
  }
  auto output_bytes = doccraft::cfb::test::CfbBuilder()
                          .AddStream("WordDocument", *word)
                          .AddStream("1Table", encrypted_table)
                          .Build();
  std::ofstream output(argv[2], std::ios::binary);
  output.write(reinterpret_cast<const char*>(output_bytes.data()),
               static_cast<std::streamsize>(output_bytes.size()));
  return output ? 0 : 1;
}
