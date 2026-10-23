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

#ifndef DOCCRAFT_CFB_CFB_READER_H_
#define DOCCRAFT_CFB_CFB_READER_H_

#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace doccraft::cfb {

// Error type for all CFB layer failures.
enum class ErrorCode {
  kNotCompoundFile,   // Missing/magic signature D0 CF 11 E0 A1 B1 1A E1
  kTruncated,         // File shorter than structure implies
  kUnsupportedSectorSize,
  kUnsupportedRevision,
  kCorruptFat,        // FAT/DIFAT chain inconsistency
  kCorruptDirectory,  // Directory tree inconsistency / cycle
  kStreamNotFound,
};

struct Error {
  ErrorCode code;
  std::string message;
};

// Entry type of a directory object.
enum class EntryType : uint8_t {
  kUnknown = 0,
  kStorage = 1,
  kStream = 2,
  kRoot = 5,
};

// One directory entry (red-black tree node).
struct DirEntry {
  std::string name;      // UTF-8 decoded from UTF-16LE
  EntryType type;
  uint32_t start_sector;
  uint64_t size;         // 64-bit for v4 (4096-byte sectors)
  // Tree relationships (directory entry indices, kNoFollow = none).
  uint32_t left = kNoFollow;
  uint32_t right = kNoFollow;
  uint32_t child = kNoFollow;

  static constexpr uint32_t kNoFollow = 0xFFFFFFFF;
};

// Read-only view over an in-memory CFB (Compound File Binary) container.
// [MS-CFB] v20240627.
class CfbReader {
 public:
  // Takes a non-owning view; caller must keep `bytes` alive.
  static std::expected<CfbReader, Error> Open(std::span<const uint8_t> bytes);

  // Returns the raw bytes of the named stream (first match in directory
  // order). Streams under storages are addressed "storage/stream" or by
  // exact entry name.
  std::expected<std::vector<uint8_t>, Error> ReadStream(
      std::string_view name) const;

  // Depth-first visit of the directory tree. `visitor` receives each entry
  // with its full path ("root/storage/stream") and a flag indicating a
  // storage/root entry; returning false stops the walk. Path separator is '/'.
  template <typename F>
    requires std::invocable<F, const DirEntry&, std::string_view, bool>
  std::expected<void, Error> Visit(F&& visitor) const;

  uint32_t sector_size() const { return sector_size_; }
  uint16_t major_version() const { return major_version_; }

 private:
  CfbReader() = default;
  std::expected<const uint8_t*, Error> SectorBytes(uint32_t sector) const;
  std::expected<std::vector<uint8_t>, Error> ReadChain(
      uint32_t start, uint64_t size, bool mini) const;

  std::span<const uint8_t> data_;
  uint32_t sector_size_ = 512;
  uint32_t mini_sector_size_ = 64;
  uint16_t major_version_ = 3;
  uint32_t first_dir_sector_ = 0;
  uint32_t mini_fat_start_ = 0;
  uint32_t num_mini_fat_ = 0;
  std::vector<uint32_t> fat_;         // Flattened FAT table
  std::vector<uint32_t> difat_;       // Sector numbers of FAT sectors
  std::vector<uint32_t> mini_fat_;
  std::vector<DirEntry> entries_;
  std::vector<uint8_t> mini_stream_;
};

template <typename F>
  requires std::invocable<F, const DirEntry&, std::string_view, bool>
std::expected<void, Error> CfbReader::Visit(F&& visitor) const {
  if (entries_.empty()) {
    return std::unexpected(Error{ErrorCode::kCorruptDirectory, "missing root"});
  }
  std::vector<uint8_t> seen(entries_.size(), 0);
  std::vector<uint32_t> parents(entries_.size(), DirEntry::kNoFollow);
  constexpr uint32_t kMaxDepth = 256;
  struct Task {
    uint32_t index;
    uint32_t parent;
    uint32_t depth;
    bool emit;
  };
  std::vector<Task> tasks{{0, DirEntry::kNoFollow, 0, false}};
  while (!tasks.empty()) {
    Task task = tasks.back();
    tasks.pop_back();
    if (task.index == DirEntry::kNoFollow) continue;
    if (task.index >= entries_.size()) {
      return std::unexpected(Error{ErrorCode::kCorruptDirectory,
                                   "directory link is invalid or cyclic"});
    }
    if (task.depth > kMaxDepth) {
      return std::unexpected(Error{ErrorCode::kCorruptDirectory,
                                   "directory nesting exceeds limit"});
    }
    const DirEntry& entry = entries_[task.index];
    if (task.emit) {
      std::vector<std::string_view> parts;
      parts.reserve(task.depth + 1);
      for (uint32_t at = task.index; at != DirEntry::kNoFollow; at = parents[at]) {
        parts.push_back(entries_[at].name);
      }
      std::string path;
      for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!path.empty()) path += '/';
        path += *it;
      }
      if (!static_cast<bool>(visitor(entry, path,
                                     entry.type == EntryType::kRoot ||
                                         entry.type == EntryType::kStorage))) return {};
      if (entry.child != DirEntry::kNoFollow) {
        if (entry.type != EntryType::kRoot && entry.type != EntryType::kStorage) {
          return std::unexpected(Error{ErrorCode::kCorruptDirectory,
                                       "stream has children"});
        }
        tasks.push_back({entry.child, task.index, task.depth + 1, false});
      }
      continue;
    }
    if (seen[task.index]) {
      return std::unexpected(Error{ErrorCode::kCorruptDirectory,
                                   "directory link is invalid or cyclic"});
    }
    seen[task.index] = 1;
    parents[task.index] = task.parent;
    if (entry.type != EntryType::kRoot && entry.type != EntryType::kStorage &&
        entry.type != EntryType::kStream) {
      return std::unexpected(Error{ErrorCode::kCorruptDirectory,
                                   "directory link points to empty entry"});
    }
    tasks.push_back({entry.right, task.parent, task.depth, false});
    tasks.push_back({task.index, task.parent, task.depth, true});
    tasks.push_back({entry.left, task.parent, task.depth, false});
  }
  return {};
}

}  // namespace doccraft::cfb

#endif  // DOCCRAFT_CFB_CFB_READER_H_
