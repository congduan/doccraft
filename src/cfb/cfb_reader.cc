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

#include "cfb/cfb_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "base/binary.h"
#include "base/utf8.h"

namespace doccraft::cfb {

// Byte readers and UTF-8 encoding from the shared base layer; [MS-CFB] is
// little-endian.
using base::AppendUtf8;
using base::Bytes;
using base::U16;
using base::U32;
using base::U64;

namespace {
constexpr uint32_t kFree = 0xFFFFFFFF;
constexpr uint32_t kEnd = 0xFFFFFFFE;
constexpr uint32_t kFat = 0xFFFFFFFD;
constexpr uint32_t kDifat = 0xFFFFFFFC;

// [MS-CFB] §2.2 fixed structural sizes.
constexpr size_t kHeaderSize = 512;
constexpr size_t kDirEntrySize = 128;
constexpr uint32_t kMiniStreamCutoff = 4096;
constexpr uint16_t kLittleEndianMark = 0xFFFE;
constexpr uint16_t kSectorShiftV3 = 9;   // 2^9  = 512-byte sectors
constexpr uint16_t kSectorShiftV4 = 12;  // 2^12 = 4096-byte sectors

Error Fail(ErrorCode code, std::string message) {
  return {code, std::move(message)};
}

// Parsed fields of the CFB header ([MS-CFB] §2.2).
struct Header {
  uint16_t major_version = 3;
  uint32_t sector_size = 512;
  uint32_t total_sectors = 0;
  uint32_t fat_count = 0;
  uint32_t difat_count = 0;
  uint32_t first_dir_sector = 0;
  uint32_t mini_fat_start = 0;
  uint32_t num_mini_fat = 0;
  uint32_t difat_start = 0;
};

std::span<const uint8_t> SectorView(std::span<const uint8_t> bytes,
                                    uint32_t sector_size, uint32_t sector) {
  return bytes.subspan((static_cast<size_t>(sector) + 1) * sector_size,
                       sector_size);
}

std::expected<Header, Error> ParseHeader(std::span<const uint8_t> bytes) {
  constexpr std::array<uint8_t, 8> signature{0xD0, 0xCF, 0x11, 0xE0,
                                             0xA1, 0xB1, 0x1A, 0xE1};
  if (bytes.size() < 8 ||
      !std::equal(signature.begin(), signature.end(), bytes.begin())) {
    return std::unexpected(
        Fail(ErrorCode::kNotCompoundFile, "invalid CFB signature"));
  }
  if (bytes.size() < kHeaderSize) {
    return std::unexpected(Fail(ErrorCode::kTruncated, "header truncated"));
  }
  Header header;
  header.major_version = U16(bytes, 26);
  if (header.major_version != 3 && header.major_version != 4) {
    return std::unexpected(
        Fail(ErrorCode::kUnsupportedRevision, "unsupported CFB version"));
  }
  const uint16_t shift = U16(bytes, 30);
  if (shift != (header.major_version == 3 ? kSectorShiftV3 : kSectorShiftV4) ||
      U16(bytes, 32) != 6) {
    return std::unexpected(
        Fail(ErrorCode::kUnsupportedSectorSize, "invalid sector shift"));
  }
  if (U16(bytes, 28) != kLittleEndianMark ||
      U32(bytes, 56) != kMiniStreamCutoff) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptFat, "invalid byte order or mini cutoff"));
  }
  header.sector_size = 1u << shift;
  if (bytes.size() < header.sector_size ||
      bytes.size() % header.sector_size != 0) {
    return std::unexpected(Fail(ErrorCode::kTruncated, "partial sector"));
  }
  header.first_dir_sector = U32(bytes, 48);
  header.mini_fat_start = U32(bytes, 60);
  header.num_mini_fat = U32(bytes, 64);
  header.total_sectors =
      static_cast<uint32_t>(bytes.size() / header.sector_size - 1);
  header.fat_count = U32(bytes, 44);
  header.difat_count = U32(bytes, 72);
  header.difat_start = U32(bytes, 68);
  if (header.fat_count == 0 || header.fat_count > header.total_sectors ||
      header.difat_count > header.total_sectors ||
      header.num_mini_fat > header.total_sectors) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptFat, "impossible sector count"));
  }
  return header;
}

struct Difat {
  std::vector<uint32_t> entries;
  std::vector<uint8_t> seen;
};

// Collects the FAT sector numbers from the header array and the DIFAT chain,
// recording which sectors held DIFAT data so their FAT markers can be checked.
std::expected<Difat, Error> ParseDifat(std::span<const uint8_t> bytes,
                                       const Header& header) {
  Difat difat;
  for (size_t i = 0; i < 109 && difat.entries.size() < header.fat_count; ++i) {
    uint32_t id = U32(bytes, 76 + i * 4);
    if (id != kFree) {
      difat.entries.push_back(id);
    }
  }
  difat.seen.assign(header.total_sectors, 0);
  uint32_t at = header.difat_start;
  for (uint32_t i = 0; i < header.difat_count; ++i) {
    if (at >= header.total_sectors || difat.seen[at]) {
      return std::unexpected(
          Fail(ErrorCode::kCorruptFat, "invalid DIFAT chain"));
    }
    difat.seen[at] = 1;
    const Bytes p = SectorView(bytes, header.sector_size, at);
    for (uint32_t j = 0; j < header.sector_size / 4 - 1 &&
                         difat.entries.size() < header.fat_count;
         ++j) {
      uint32_t id = U32(p, j * 4);
      if (id != kFree) {
        difat.entries.push_back(id);
      }
    }
    at = U32(p, header.sector_size - 4);
  }
  if (at != kEnd || difat.entries.size() != header.fat_count) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptFat, "DIFAT count mismatch"));
  }
  return difat;
}

struct FatTables {
  std::vector<uint32_t> fat;
  std::vector<uint32_t> difat;
};

// Builds the DIFAT and FAT allocation tables, validating their markers.
std::expected<FatTables, Error> LoadFatTables(std::span<const uint8_t> bytes,
                                              const Header& header) {
  FatTables tables;
  auto difat = ParseDifat(bytes, header);
  if (!difat) {
    return std::unexpected(difat.error());
  }
  tables.difat = std::move(difat->entries);
  const std::vector<uint8_t>& difat_seen = difat->seen;
  std::vector<uint8_t> fat_seen(header.total_sectors, 0);
  for (uint32_t id : tables.difat) {
    if (id >= header.total_sectors || fat_seen[id]) {
      return std::unexpected(
          Fail(ErrorCode::kCorruptFat, "invalid FAT sector"));
    }
    fat_seen[id] = 1;
    const Bytes p = SectorView(bytes, header.sector_size, id);
    for (uint32_t j = 0; j < header.sector_size / 4; ++j) {
      tables.fat.push_back(U32(p, j * 4));
    }
  }
  if (tables.fat.size() < header.total_sectors) {
    return std::unexpected(Fail(ErrorCode::kCorruptFat, "FAT too short"));
  }
  for (uint32_t id : tables.difat) {
    if (tables.fat[id] != kFat) {
      return std::unexpected(
          Fail(ErrorCode::kCorruptFat, "FAT marker missing"));
    }
  }
  for (uint32_t i = 0; i < header.total_sectors; ++i) {
    if (difat_seen[i] && tables.fat[i] != kDifat) {
      return std::unexpected(
          Fail(ErrorCode::kCorruptFat, "DIFAT marker missing"));
    }
  }
  return tables;
}

// Decodes one 128-byte directory entry, including its UTF-16LE name.
std::expected<DirEntry, Error> ParseDirEntry(Bytes p, uint16_t version) {
  DirEntry entry{};
  const uint16_t len = U16(p, 64);
  const uint8_t type = p[66];
  if (type == 1 || type == 2 || type == 5) {
    if (len < 2 || len > 64 || len % 2 != 0 || U16(p, len - 2) != 0) {
      return std::unexpected(
          Fail(ErrorCode::kCorruptDirectory, "invalid directory name"));
    }
    for (size_t j = 0; j + 2 < len; j += 2) {
      uint32_t cp = U16(p, j);
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (j + 4 >= len) {
          return std::unexpected(
              Fail(ErrorCode::kCorruptDirectory, "bad UTF-16"));
        }
        uint32_t low = U16(p, j + 2);
        if (low < 0xDC00 || low > 0xDFFF) {
          return std::unexpected(
              Fail(ErrorCode::kCorruptDirectory, "bad UTF-16"));
        }
        cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
        j += 2;
      } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        return std::unexpected(
            Fail(ErrorCode::kCorruptDirectory, "bad UTF-16"));
      }
      AppendUtf8(entry.name, cp);
    }
  }
  entry.type = static_cast<EntryType>(type);
  entry.left = U32(p, 68);
  entry.right = U32(p, 72);
  entry.child = U32(p, 76);
  entry.start_sector = U32(p, 116);
  entry.size = version == 3 ? U32(p, 120) : U64(p, 120);
  return entry;
}

// Walks the FAT directory chain and decodes every entry.
std::expected<std::vector<DirEntry>, Error> LoadDirectory(
    std::span<const uint8_t> bytes, const Header& header,
    const std::vector<uint32_t>& fat) {
  std::vector<uint8_t> directory;
  std::vector<uint8_t> seen(header.total_sectors, 0);
  uint32_t at = header.first_dir_sector;
  while (at != kEnd) {
    if (at >= header.total_sectors || at >= fat.size() || seen[at]) {
      return std::unexpected(
          Fail(ErrorCode::kCorruptDirectory, "invalid directory chain"));
    }
    seen[at] = 1;
    const Bytes p = SectorView(bytes, header.sector_size, at);
    directory.insert(directory.end(), p.begin(), p.end());
    at = fat[at];
  }
  if (directory.empty()) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptDirectory, "empty directory"));
  }
  std::vector<DirEntry> entries;
  for (size_t i = 0; i + kDirEntrySize <= directory.size();
       i += kDirEntrySize) {
    auto entry = ParseDirEntry(Bytes(directory).subspan(i, kDirEntrySize),
                               header.major_version);
    if (!entry) {
      return std::unexpected(entry.error());
    }
    entries.push_back(std::move(*entry));
  }
  return entries;
}
}  // namespace

std::expected<Bytes, Error> CfbReader::SectorBytes(uint32_t sector) const {
  const uint64_t count = data_.size() / sector_size_ - 1;
  if (sector >= count) {
    return std::unexpected(Fail(ErrorCode::kTruncated, "sector outside file"));
  }
  return data_.subspan((static_cast<size_t>(sector) + 1) * sector_size_,
                       sector_size_);
}

std::expected<std::vector<uint8_t>, Error> CfbReader::ReadChain(
    uint32_t start, uint64_t size, bool mini) const {
  if (size == 0)
    return std::vector<uint8_t>{};
  const uint32_t unit = mini ? mini_sector_size_ : sector_size_;
  const uint64_t count = size / unit + (size % unit != 0);
  const auto& table = mini ? mini_fat_ : fat_;
  if (count > table.size()) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptFat, "chain exceeds allocation table"));
  }
  std::vector<uint8_t> result;
  // A valid chain cannot return more bytes than its backing container. Start
  // small and let vector grow geometrically as verified sectors are copied.
  const uint64_t backing_size = mini ? mini_stream_.size() : data_.size();
  result.reserve(
      static_cast<size_t>(std::min({size, backing_size, uint64_t{65536}})));
  std::vector<uint8_t> seen(table.size(), 0);
  uint32_t at = start;
  for (uint64_t i = 0; i < count; ++i) {
    if (at >= table.size() || seen[at]) {
      return std::unexpected(
          Fail(ErrorCode::kCorruptFat, "invalid or cyclic FAT chain"));
    }
    seen[at] = 1;
    Bytes chunk;
    if (mini) {
      const uint64_t offset = static_cast<uint64_t>(at) * unit;
      if (offset + unit > mini_stream_.size()) {
        return std::unexpected(
            Fail(ErrorCode::kCorruptFat, "mini sector outside root stream"));
      }
      chunk = Bytes(mini_stream_).subspan(static_cast<size_t>(offset), unit);
    } else {
      auto sector = SectorBytes(at);
      if (!sector)
        return std::unexpected(sector.error());
      chunk = *sector;
    }
    const size_t take =
        static_cast<size_t>(std::min<uint64_t>(unit, size - result.size()));
    result.insert(result.end(), chunk.begin(), chunk.begin() + take);
    uint32_t next = table[at];
    if (i + 1 == count) {
      if (next != kEnd) {
        return std::unexpected(
            Fail(ErrorCode::kCorruptFat, "chain longer than stream"));
      }
    } else if (next == kEnd || next == kFree || next == kFat ||
               next == kDifat) {
      return std::unexpected(Fail(ErrorCode::kCorruptFat, "chain ends early"));
    }
    at = next;
  }
  return result;
}

std::expected<CfbReader, Error> CfbReader::Open(
    std::span<const uint8_t> bytes) {
  auto header = ParseHeader(bytes);
  if (!header) {
    return std::unexpected(header.error());
  }
  auto tables = LoadFatTables(bytes, *header);
  if (!tables) {
    return std::unexpected(tables.error());
  }
  auto entries = LoadDirectory(bytes, *header, tables->fat);
  if (!entries) {
    return std::unexpected(entries.error());
  }
  CfbReader out;
  out.major_version_ = header->major_version;
  out.sector_size_ = header->sector_size;
  out.data_ = bytes;
  out.first_dir_sector_ = header->first_dir_sector;
  out.mini_fat_start_ = header->mini_fat_start;
  out.num_mini_fat_ = header->num_mini_fat;
  out.fat_ = std::move(tables->fat);
  out.difat_ = std::move(tables->difat);
  out.entries_ = std::move(*entries);
  if (out.entries_[0].type != EntryType::kRoot) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptDirectory, "first entry is not root"));
  }
  if (out.entries_[0].left != DirEntry::kNoFollow ||
      out.entries_[0].right != DirEntry::kNoFollow) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptDirectory, "root has sibling"));
  }
  auto visited = out.Visit([](const DirEntry&, std::string_view, bool) {
    return true;
  });
  if (!visited) {
    return std::unexpected(visited.error());
  }
  if (out.num_mini_fat_) {
    auto raw = out.ReadChain(
        out.mini_fat_start_,
        static_cast<uint64_t>(out.num_mini_fat_) * out.sector_size_, false);
    if (!raw) {
      return std::unexpected(raw.error());
    }
    for (size_t i = 0; i < raw->size(); i += 4) {
      out.mini_fat_.push_back(U32(*raw, i));
    }
  }
  if (out.entries_[0].size) {
    auto root = out.ReadChain(out.entries_[0].start_sector,
                              out.entries_[0].size, false);
    if (!root) {
      return std::unexpected(root.error());
    }
    out.mini_stream_ = std::move(*root);
  }
  return out;
}

std::expected<std::vector<uint8_t>, Error> CfbReader::ReadStream(
    std::string_view name) const {
  std::expected<std::vector<uint8_t>, Error> result =
      std::unexpected(Fail(ErrorCode::kStreamNotFound, "stream not found"));
  auto visited = Visit([&](const DirEntry& entry, std::string_view path, bool) {
    if (entry.type != EntryType::kStream)
      return true;
    const size_t prefix_length = entries_[0].name.size() + 1;
    if (path.size() <= prefix_length) {
      result = std::unexpected(
          Fail(ErrorCode::kCorruptDirectory, "stream path lacks root prefix"));
      return false;
    }
    const std::string_view relative = path.substr(prefix_length);
    if (path != name && relative != name && entry.name != name)
      return true;
    result = ReadChain(entry.start_sector, entry.size, entry.size < 4096);
    return false;
  });
  if (!visited)
    return std::unexpected(visited.error());
  return result;
}
}  // namespace doccraft::cfb
