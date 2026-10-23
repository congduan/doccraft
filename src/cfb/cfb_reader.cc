// Copyright 2026 The doccraft Authors. All Rights Reserved.
#include "cfb/cfb_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace doccraft::cfb {
namespace {
constexpr uint32_t kFree = 0xFFFFFFFF;
constexpr uint32_t kEnd = 0xFFFFFFFE;
constexpr uint32_t kFat = 0xFFFFFFFD;
constexpr uint32_t kDifat = 0xFFFFFFFC;
uint16_t U16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
uint32_t U32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t U64(const uint8_t* p) {
  return static_cast<uint64_t>(U32(p)) | (static_cast<uint64_t>(U32(p + 4)) << 32);
}
Error Fail(ErrorCode code, std::string message) { return {code, std::move(message)}; }
void AppendUtf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}
}  // namespace

std::expected<const uint8_t*, Error> CfbReader::SectorBytes(uint32_t sector) const {
  const uint64_t count = data_.size() / sector_size_ - 1;
  if (sector >= count) {
    return std::unexpected(Fail(ErrorCode::kTruncated, "sector outside file"));
  }
  return data_.data() + (static_cast<size_t>(sector) + 1) * sector_size_;
}

std::expected<std::vector<uint8_t>, Error> CfbReader::ReadChain(
    uint32_t start, uint64_t size, bool mini) const {
  if (size == 0) return std::vector<uint8_t>{};
  const uint32_t unit = mini ? mini_sector_size_ : sector_size_;
  const uint64_t count = size / unit + (size % unit != 0);
  const auto& table = mini ? mini_fat_ : fat_;
  if (count > table.size() || size > std::numeric_limits<size_t>::max()) {
    return std::unexpected(Fail(ErrorCode::kCorruptFat, "chain exceeds allocation table"));
  }
  std::vector<uint8_t> result;
  result.reserve(static_cast<size_t>(size));
  std::vector<uint8_t> seen(table.size(), 0);
  uint32_t at = start;
  for (uint64_t i = 0; i < count; ++i) {
    if (at >= table.size() || seen[at]) {
      return std::unexpected(Fail(ErrorCode::kCorruptFat, "invalid or cyclic FAT chain"));
    }
    seen[at] = 1;
    const uint8_t* p = nullptr;
    if (mini) {
      const uint64_t offset = static_cast<uint64_t>(at) * unit;
      if (offset + unit > mini_stream_.size()) {
        return std::unexpected(Fail(ErrorCode::kCorruptFat, "mini sector outside root stream"));
      }
      p = mini_stream_.data() + offset;
    } else {
      auto sector = SectorBytes(at);
      if (!sector) return std::unexpected(sector.error());
      p = *sector;
    }
    const size_t take = static_cast<size_t>(std::min<uint64_t>(unit, size - result.size()));
    result.insert(result.end(), p, p + take);
    uint32_t next = table[at];
    if (i + 1 == count) {
      if (next != kEnd) {
        return std::unexpected(Fail(ErrorCode::kCorruptFat, "chain longer than stream"));
      }
    } else if (next == kEnd || next == kFree || next == kFat || next == kDifat) {
      return std::unexpected(Fail(ErrorCode::kCorruptFat, "chain ends early"));
    }
    at = next;
  }
  return result;
}

std::expected<CfbReader, Error> CfbReader::Open(std::span<const uint8_t> bytes) {
  constexpr std::array<uint8_t, 8> signature{0xD0, 0xCF, 0x11, 0xE0,
                                              0xA1, 0xB1, 0x1A, 0xE1};
  if (bytes.size() < 8 || !std::equal(signature.begin(), signature.end(), bytes.begin())) {
    return std::unexpected(Fail(ErrorCode::kNotCompoundFile, "invalid CFB signature"));
  }
  if (bytes.size() < 512) return std::unexpected(Fail(ErrorCode::kTruncated, "header truncated"));
  CfbReader out;
  out.major_version_ = U16(bytes.data() + 26);
  out.minor_version_ = U16(bytes.data() + 24);
  if (out.major_version_ != 3 && out.major_version_ != 4) {
    return std::unexpected(Fail(ErrorCode::kUnsupportedRevision, "unsupported CFB version"));
  }
  const uint16_t shift = U16(bytes.data() + 30);
  if (shift != (out.major_version_ == 3 ? 9 : 12) || U16(bytes.data() + 32) != 6) {
    return std::unexpected(Fail(ErrorCode::kUnsupportedSectorSize, "invalid sector shift"));
  }
  if (U16(bytes.data() + 28) != 0xFFFE || U32(bytes.data() + 56) != 4096) {
    return std::unexpected(Fail(ErrorCode::kCorruptFat, "invalid byte order or mini cutoff"));
  }
  out.sector_size_ = 1u << shift;
  if (bytes.size() < out.sector_size_ || bytes.size() % out.sector_size_ != 0) {
    return std::unexpected(Fail(ErrorCode::kTruncated, "partial sector"));
  }
  out.data_ = bytes;
  out.first_dir_sector_ = U32(bytes.data() + 48);
  out.mini_fat_start_ = U32(bytes.data() + 60);
  out.num_mini_fat_ = U32(bytes.data() + 64);
  const uint32_t total_sectors = static_cast<uint32_t>(bytes.size() / out.sector_size_ - 1);
  const uint32_t fat_count = U32(bytes.data() + 44);
  const uint32_t difat_count = U32(bytes.data() + 72);
  if (fat_count == 0 || fat_count > total_sectors || difat_count > total_sectors ||
      out.num_mini_fat_ > total_sectors) {
    return std::unexpected(Fail(ErrorCode::kCorruptFat, "impossible sector count"));
  }
  for (size_t i = 0; i < 109 && out.difat_.size() < fat_count; ++i) {
    uint32_t id = U32(bytes.data() + 76 + i * 4);
    if (id != kFree) out.difat_.push_back(id);
  }
  uint32_t difat_at = U32(bytes.data() + 68);
  std::vector<uint8_t> difat_seen(total_sectors, 0);
  for (uint32_t i = 0; i < difat_count; ++i) {
    if (difat_at >= total_sectors || difat_seen[difat_at]) {
      return std::unexpected(Fail(ErrorCode::kCorruptFat, "invalid DIFAT chain"));
    }
    difat_seen[difat_at] = 1;
    const uint8_t* p = *out.SectorBytes(difat_at);
    for (uint32_t j = 0; j < out.sector_size_ / 4 - 1 && out.difat_.size() < fat_count; ++j) {
      uint32_t id = U32(p + j * 4);
      if (id != kFree) out.difat_.push_back(id);
    }
    difat_at = U32(p + out.sector_size_ - 4);
  }
  if (difat_at != kEnd || out.difat_.size() != fat_count) {
    return std::unexpected(Fail(ErrorCode::kCorruptFat, "DIFAT count mismatch"));
  }
  std::vector<uint8_t> fat_seen(total_sectors, 0);
  for (uint32_t id : out.difat_) {
    if (id >= total_sectors || fat_seen[id]) {
      return std::unexpected(Fail(ErrorCode::kCorruptFat, "invalid FAT sector"));
    }
    fat_seen[id] = 1;
    const uint8_t* p = *out.SectorBytes(id);
    for (uint32_t j = 0; j < out.sector_size_ / 4; ++j) out.fat_.push_back(U32(p + j * 4));
  }
  if (out.fat_.size() < total_sectors) {
    return std::unexpected(Fail(ErrorCode::kCorruptFat, "FAT too short"));
  }
  for (uint32_t id : out.difat_) {
    if (out.fat_[id] != kFat) return std::unexpected(Fail(ErrorCode::kCorruptFat, "FAT marker missing"));
  }
  for (uint32_t i = 0; i < total_sectors; ++i) {
    if (difat_seen[i] && out.fat_[i] != kDifat) {
      return std::unexpected(Fail(ErrorCode::kCorruptFat, "DIFAT marker missing"));
    }
  }
  std::vector<uint8_t> directory;
  std::vector<uint8_t> dir_seen(total_sectors, 0);
  uint32_t dir_at = out.first_dir_sector_;
  while (dir_at != kEnd) {
    if (dir_at >= total_sectors || dir_at >= out.fat_.size() || dir_seen[dir_at]) {
      return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "invalid directory chain"));
    }
    dir_seen[dir_at] = 1;
    const uint8_t* p = *out.SectorBytes(dir_at);
    directory.insert(directory.end(), p, p + out.sector_size_);
    dir_at = out.fat_[dir_at];
  }
  if (directory.empty()) return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "empty directory"));
  for (size_t i = 0; i + 128 <= directory.size(); i += 128) {
    const uint8_t* p = directory.data() + i;
    uint16_t len = U16(p + 64);
    uint8_t type = p[66];
    DirEntry entry{};
    if (type == 1 || type == 2 || type == 5) {
      if (len < 2 || len > 64 || len % 2 != 0 || U16(p + len - 2) != 0) {
        return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "invalid directory name"));
      }
      for (size_t j = 0; j + 2 < len; j += 2) {
        uint32_t cp = U16(p + j);
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          if (j + 4 >= len) return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "bad UTF-16"));
          uint32_t low = U16(p + j + 2);
          if (low < 0xDC00 || low > 0xDFFF) return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "bad UTF-16"));
          cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
          j += 2;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "bad UTF-16"));
        }
        AppendUtf8(entry.name, cp);
      }
    }
    entry.type = static_cast<EntryType>(type);
    entry.left = U32(p + 68);
    entry.right = U32(p + 72);
    entry.child = U32(p + 76);
    entry.start_sector = U32(p + 116);
    entry.size = out.major_version_ == 3 ? U32(p + 120) : U64(p + 120);
    out.entries_.push_back(std::move(entry));
  }
  if (out.entries_[0].type != EntryType::kRoot) {
    return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "first entry is not root"));
  }
  auto visited = out.Visit([](const DirEntry&, std::string_view, bool) { return true; });
  if (!visited) return std::unexpected(visited.error());
  if (out.num_mini_fat_) {
    auto raw = out.ReadChain(out.mini_fat_start_,
                             static_cast<uint64_t>(out.num_mini_fat_) * out.sector_size_, false);
    if (!raw) return std::unexpected(raw.error());
    for (size_t i = 0; i < raw->size(); i += 4) out.mini_fat_.push_back(U32(raw->data() + i));
  }
  if (out.entries_[0].size) {
    auto root = out.ReadChain(out.entries_[0].start_sector, out.entries_[0].size, false);
    if (!root) return std::unexpected(root.error());
    out.mini_stream_ = std::move(*root);
  }
  return out;
}

std::expected<std::vector<uint8_t>, Error> CfbReader::ReadStream(std::string_view name) const {
  std::expected<std::vector<uint8_t>, Error> result =
      std::unexpected(Fail(ErrorCode::kStreamNotFound, "stream not found"));
  auto visited = Visit([&](const DirEntry& entry, std::string_view path, bool) {
    if (entry.type != EntryType::kStream) return true;
    const std::string_view relative = path.substr(entries_[0].name.size() + 1);
    if (path != name && relative != name && entry.name != name) return true;
    result = ReadChain(entry.start_sector, entry.size, entry.size < 4096);
    return false;
  });
  if (!visited) return std::unexpected(visited.error());
  return result;
}
}  // namespace doccraft::cfb
