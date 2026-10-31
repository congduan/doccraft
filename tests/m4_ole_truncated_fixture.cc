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

// Rebuilds a .doc with an embedded OLE object whose ObjectPool payload is
// truncated: the chain of one regular stream inside the object storage ends
// after four sectors. ReadStream() salvages that prefix and records a warning,
// which only the CLI can turn into stderr output (see m4_ole_truncated_cli.py).

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb/cfb_reader.h"
#include "tests/cfb_test_util.h"

namespace {
constexpr uint32_t kEndOfChain = 0xFFFFFFFEu;
constexpr uint32_t kNoSector = 0xFFFFFFFFu;
constexpr std::string_view kRootPrefix = "Root Entry/";

void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t Get32(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

// Byte offset of the FAT entry for `sector`. The container is produced by
// CfbBuilder, so every FAT sector is listed in the header DIFAT array.
size_t FatEntryOffset(const std::vector<uint8_t>& bytes, uint32_t sector) {
  const uint32_t sector_size = 1u << (bytes[30] | (bytes[31] << 8));
  const uint32_t per_fat = sector_size / 4;
  const uint32_t fat_sector = Get32(bytes, 76 + (sector / per_fat) * 4);
  return (static_cast<size_t>(fat_sector) + 1) * sector_size +
         static_cast<size_t>(sector % per_fat) * 4;
}

uint32_t StreamStart(const std::vector<uint8_t>& bytes, std::string_view name) {
  auto reader = doccraft::cfb::CfbReader::Open(bytes);
  if (!reader)
    return kNoSector;
  uint32_t start = kNoSector;
  auto visited = reader->Visit([&](const doccraft::cfb::DirEntry& entry,
                                   std::string_view, bool storage) {
    if (!storage && entry.name == name) {
      start = entry.start_sector;
      return false;
    }
    return true;
  });
  return visited ? start : kNoSector;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3)
    return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = doccraft::cfb::CfbReader::Open(bytes);
  if (!cfb)
    return 3;
  doccraft::cfb::test::CfbBuilder builder;
  bool copied = true;
  auto visited = cfb->Visit(
      [&](const doccraft::cfb::DirEntry&, std::string_view path, bool storage) {
        if (storage)
          return true;
        auto stream = cfb->ReadStream(path);
        if (!stream || !path.starts_with(kRootPrefix)) {
          copied = false;
          return false;
        }
        builder.AddStream(std::string(path.substr(kRootPrefix.size())),
                          std::move(*stream));
        return true;
      });
  if (!visited || !copied)
    return 4;
  auto output = builder.Build();
  // "Pictures" is a 15495-byte regular stream inside ObjectPool/_1345471035.
  // Ending its chain after four sectors keeps 2048 bytes of salvageable data.
  const uint32_t start = StreamStart(output, "Pictures");
  if (start == kNoSector)
    return 5;
  Put32(output, FatEntryOffset(output, start + 3), kEndOfChain);
  std::ofstream out(argv[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(output.data()), output.size());
  return out ? 0 : 6;
}
