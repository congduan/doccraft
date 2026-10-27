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

#include "doc/encryption.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "base/binary.h"
#include "crypto/derivation.h"
#include "crypto/md5.h"
#include "crypto/rc4.h"
#include "crypto/sha1.h"

namespace doccraft::doc {
namespace {
constexpr size_t kFibPlainBytes = 68;
constexpr size_t kBlockBytes = 512;
constexpr size_t kCryptoApiPrefixBytes = 12;
constexpr size_t kEncryptionHeaderMinBytes = 32;
constexpr size_t kVerifierBytes = 60;
constexpr size_t kLegacyHeaderBytes = 52;
constexpr uint32_t kRc4Algorithm = 0x6801;
constexpr uint32_t kSha1Algorithm = 0x8004;

template <typename KeyForBlock>
void CryptStream(std::span<uint8_t> stream, size_t plain_prefix,
                 KeyForBlock key_for_block) {
  for (size_t start = 0; start < stream.size(); start += kBlockBytes) {
    const auto key = key_for_block(static_cast<uint32_t>(start / kBlockBytes));
    crypto::Rc4 rc4(key);
    const size_t end = std::min(start + kBlockBytes, stream.size());
    const size_t skipped =
        std::min(end, plain_prefix) - std::min(start, plain_prefix);
    std::array<uint8_t, kBlockBytes> discard{};
    rc4.Xor(std::span(discard).first(skipped));
    const size_t crypt_start = start + skipped;
    rc4.Xor(stream.subspan(crypt_start, end - crypt_start));
  }
}
enum class Rc4Version { kLegacy, kCryptoApi };

struct Rc4HeaderInfo {
  Rc4Version version;
  uint32_t header_size;
};

// Shared guard for both public entry points: validates the FIB encryption
// flags, reads the table-stream header size and classifies the version.
std::expected<Rc4HeaderInfo, Error> DetectRc4Header(
    std::span<const uint8_t> word, std::span<const uint8_t> table) {
  if (word.size() < kFibPlainBytes)
    return std::unexpected(Error{"truncated encrypted FIB"});
  const uint16_t flags = base::U16(word, 10);
  if (!(flags & 0x0100) || (flags & 0x8000))
    return std::unexpected(Error{"not an RC4 encrypted document"});
  const uint32_t header_size = base::U32(word, 14);
  if (header_size > table.size() || header_size < 4)
    return std::unexpected(Error{"truncated RC4 encryption header"});
  const uint16_t major = base::U16(table, 0);
  const uint16_t minor = base::U16(table, 2);
  if (major == 1 && minor == 1)
    return Rc4HeaderInfo{Rc4Version::kLegacy, header_size};
  if (major >= 2 && major <= 4 && minor == 2)
    return Rc4HeaderInfo{Rc4Version::kCryptoApi, header_size};
  return std::unexpected(Error{"unsupported RC4 encryption version"});
}
}  // namespace

std::expected<std::string_view, Error> DetectRc4Type(
    std::span<const uint8_t> word, std::span<const uint8_t> table) {
  const auto info = DetectRc4Header(word, table);
  if (!info)
    return std::unexpected(info.error());
  return info->version == Rc4Version::kLegacy ? "legacy RC4/MD5"
                                              : "RC4 CryptoAPI";
}

std::expected<Fib, Error> DecryptRc4(std::span<uint8_t> word,
                                     std::span<uint8_t> table,
                                     std::span<uint8_t> data,
                                     std::string_view password) {
  const auto info = DetectRc4Header(word, table);
  if (!info)
    return std::unexpected(info.error());
  const uint32_t header_size = info->header_size;
  const std::span<const uint8_t> header = table.first(header_size);
  if (info->version == Rc4Version::kLegacy) {
    if (header_size < kLegacyHeaderBytes)
      return std::unexpected(Error{"truncated legacy RC4 header"});
    const auto encoded = crypto::EncodePassword(password);
    if (!encoded)
      return std::unexpected(Error{std::string(encoded.error())});
    const auto salt = std::span<const uint8_t, 16>(header.data() + 4, 16);
    const auto password_hash = crypto::LegacyPasswordHash(*encoded, salt);
    auto key_for_block = [&](uint32_t block) {
      return crypto::DeriveLegacyRc4Key(password_hash, block);
    };
    crypto::Rc4 verifier_rc4(key_for_block(0));
    std::array<uint8_t, 16> verifier{};
    std::copy_n(header.begin() + 20, 16, verifier.begin());
    verifier_rc4.Xor(verifier);
    std::array<uint8_t, 16> verifier_hash{};
    std::copy_n(header.begin() + 36, 16, verifier_hash.begin());
    verifier_rc4.Xor(verifier_hash);
    const auto expected_hash = crypto::Md5(verifier);
    uint8_t difference = 0;
    for (size_t i = 0; i < verifier_hash.size(); ++i)
      difference |= verifier_hash[i] ^ expected_hash[i];
    if (difference != 0)
      return std::unexpected(Error{"incorrect document password"});
    CryptStream(word, kFibPlainBytes, key_for_block);
    CryptStream(table, header_size, key_for_block);
    CryptStream(data, 0, key_for_block);
    word[11] &= static_cast<uint8_t>(~0x01);
    auto fib = ParseFib(word);
    if (!fib)
      return std::unexpected(
          Error{"invalid decrypted FIB: " + fib.error().message});
    return fib;
  }
  if (header_size <
      kCryptoApiPrefixBytes + kEncryptionHeaderMinBytes + kVerifierBytes)
    return std::unexpected(Error{"truncated RC4 encryption header"});
  const uint32_t inner_size = base::U32(header, 8);
  if (inner_size < kEncryptionHeaderMinBytes ||
      inner_size > header.size() - kCryptoApiPrefixBytes - kVerifierBytes)
    return std::unexpected(Error{"invalid RC4 encryption header size"});
  const size_t verifier_offset = kCryptoApiPrefixBytes + inner_size;
  const uint32_t header_flags = base::U32(header, 12);
  const uint32_t key_bits_raw = base::U32(header, 28);
  const uint32_t key_bits = key_bits_raw == 0 ? 40 : key_bits_raw;
  if (header_flags != base::U32(header, 4) || !(header_flags & 4) ||
      (header_flags & 0x30) || base::U32(header, 16) != 0 ||
      base::U32(header, 20) != kRc4Algorithm ||
      base::U32(header, 24) != kSha1Algorithm || key_bits < 40 ||
      key_bits > 128 || key_bits % 8 != 0 || base::U32(header, 32) != 1 ||
      base::U32(header, 40) != 0 || base::U32(header, verifier_offset) != 16 ||
      base::U32(header, verifier_offset + 36) != 20)
    return std::unexpected(Error{"unsupported RC4 CryptoAPI header"});
  const auto salt = header.subspan(verifier_offset + 4, 16);
  const auto password_hash = crypto::HashPassword(
      password, std::span<const uint8_t, 16>(salt.data(), 16));
  if (!password_hash)
    return std::unexpected(Error{std::string(password_hash.error())});
  const auto key = crypto::DeriveRc4Key(*password_hash, 0, key_bits);
  crypto::Rc4 verifier_rc4(key);
  std::array<uint8_t, 16> verifier{};
  std::copy_n(header.begin() + verifier_offset + 20, 16, verifier.begin());
  verifier_rc4.Xor(verifier);
  std::array<uint8_t, 20> verifier_hash{};
  std::copy_n(header.begin() + verifier_offset + 40, 20, verifier_hash.begin());
  verifier_rc4.Xor(verifier_hash);
  const auto expected_hash = crypto::Sha1(verifier);
  uint8_t difference = 0;
  for (size_t i = 0; i < verifier_hash.size(); ++i)
    difference |= verifier_hash[i] ^ expected_hash[i];
  if (difference != 0)
    return std::unexpected(Error{"incorrect document password"});
  auto key_for_block = [&](uint32_t block) {
    return crypto::DeriveRc4Key(*password_hash, block, key_bits);
  };
  CryptStream(word, kFibPlainBytes, key_for_block);
  CryptStream(table, header_size, key_for_block);
  CryptStream(data, 0, key_for_block);
  word[11] &= static_cast<uint8_t>(~0x01);
  auto fib = ParseFib(word);
  if (!fib)
    return std::unexpected(
        Error{"invalid decrypted FIB: " + fib.error().message});
  return fib;
}

}  // namespace doccraft::doc
