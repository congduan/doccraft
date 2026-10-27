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

#include "tests/fuzz_util.h"

#include <fstream>
#include <iterator>
#include <utility>

#include "cfb/cfb_reader.h"
#include "cfb/cfb_writer.h"
#include "doc/encryption.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"

namespace doccraft::test {
namespace {
// The encryption flag lives in FibBase.fQuickSaved byte 2 (word[11], bit 0),
// mirrored by bit 0x0100 of the flags word at offset 10.
constexpr size_t kWordEncryptedByte = 11;
constexpr uint8_t kEncryptedBit = 0x01;
constexpr uint8_t kTableSelectBit = 0x02;
}  // namespace

ConvertOutcome ConvertBytes(std::span<const uint8_t> file,
                            std::string_view password) {
  ConvertOutcome result;
  auto cfb = cfb::CfbReader::Open(file);
  if (!cfb) {
    result.error = cfb.error().message;
    result.stage = "cfb";
    return result;
  }
  auto word = cfb->ReadStream("WordDocument");
  if (!word) {
    result.error = word.error().message;
    result.stage = "streams";
    return result;
  }
  const bool encrypted = word->size() > kWordEncryptedByte &&
                         ((*word)[kWordEncryptedByte] & kEncryptedBit);
  if (encrypted && password.empty()) {
    result.stage = "encrypted";
    const std::string table_name =
        ((*word)[kWordEncryptedByte] & kTableSelectBit) ? "1Table" : "0Table";
    auto table = cfb->ReadStream(table_name);
    if (!table) {
      result.error = table.error().message;
      return result;
    }
    auto type = doc::DetectRc4Type(*word, *table);
    if (!type) {
      result.error = type.error().message;
      return result;
    }
    result.error =
        "encrypted document (" + std::string(*type) + ") requires --password";
    return result;
  }
  auto fib = doc::ParseFib(*word);
  if (!fib && !encrypted) {
    result.error = fib.error().message;
    result.stage = "fib";
    return result;
  }
  const std::string table_name =
      fib                                               ? fib->table_stream()
      : ((*word)[kWordEncryptedByte] & kTableSelectBit) ? "1Table"
                                                        : "0Table";
  auto table = cfb->ReadStream(table_name);
  if (!table) {
    result.error = table.error().message;
    result.stage = "streams";
    return result;
  }
  auto data = cfb->ReadStream("Data");
  if (encrypted) {
    fib = doc::DecryptRc4(
        *word, *table, data ? std::span<uint8_t>(*data) : std::span<uint8_t>(),
        password);
    if (!fib) {
      result.error = fib.error().message;
      result.stage = "decrypt";
      return result;
    }
  }
  auto document = data ? doc::ParseFormatted(*word, *table, *fib, *data)
                       : doc::ParseFormatted(*word, *table, *fib);
  if (!document) {
    result.error = document.error().message;
    result.stage = "parse";
    return result;
  }
  for (auto& object : document->ole_objects) {
    auto packaged = cfb::RepackageStorage(
        *cfb, "Root Entry/ObjectPool/_" + std::to_string(object.storage_id));
    if (!packaged) {
      result.error = packaged.error().message;
      result.stage = "ole";
      return result;
    }
    object.bytes = std::move(*packaged);
  }
  auto archive = docx::Write(*document);
  if (!archive) {
    result.error = archive.error().message;
    result.stage = "writer";
    return result;
  }
  result.ok = true;
  return result;
}

void MutateRegion(std::mt19937& rng, std::vector<uint8_t>& bytes, size_t begin,
                  size_t end) {
  if (begin >= end)
    return;
  std::uniform_int_distribution<size_t> position(begin, end - 1);
  std::uniform_int_distribution<int> operation(0, 2);
  std::uniform_int_distribution<int> value(0, 255);
  const int count = 1 + static_cast<int>(rng() % 64);
  for (int n = 0; n < count; ++n) {
    const size_t at = position(rng);
    switch (operation(rng)) {
      case 0:
        bytes[at] = static_cast<uint8_t>(~bytes[at]);
        break;
      case 1:
        bytes[at] ^= static_cast<uint8_t>(1 + value(rng) % 8);
        break;
      default:
        bytes[at] = static_cast<uint8_t>(value(rng));
        break;
    }
  }
}

std::vector<uint8_t> ReadSeed(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

}  // namespace doccraft::test
