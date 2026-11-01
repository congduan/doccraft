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

#ifndef SRC_CFB_CFB_READER_H_
#define SRC_CFB_CFB_READER_H_

#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace doccraft::cfb {

// Error type for all CFB layer failures.
enum class ErrorCode {
  kNotCompoundFile,  // Missing/magic signature D0 CF 11 E0 A1 B1 1A E1
  kTruncated,        // File shorter than structure implies
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
  std::string name;  // UTF-8 decoded from UTF-16LE
  EntryType type;
  uint32_t start_sector;
  uint64_t size;  // 64-bit for v4 (4096-byte sectors)
  // Tree relationships (directory entry indices, kNoFollow = NOSTREAM).
  uint32_t left = kNoFollow;
  uint32_t right = kNoFollow;
  uint32_t child = kNoFollow;

  // [MS-CFB] §2.6.4 NOSTREAM: no directory entry follows this link.
  static constexpr uint32_t kNoFollow = 0xFFFFFFFF;
};

// Read-only view over an in-memory CFB (Compound File Binary) container.
// [MS-CFB] v20240627.
class CfbReader {
 public:
  // Takes a non-owning view; caller must keep `bytes` alive.
  static std::expected<CfbReader, Error> Open(std::span<const uint8_t> bytes);

  // Returns the raw bytes of the named stream. Stream names compare ASCII
  // case-insensitively ([MS-CFB] §2.6.1). Streams under storages are addressed
  // "storage/stream" or by entry name.
  std::expected<std::vector<uint8_t>, Error> ReadStream(
      std::string_view name) const;

  // Depth-first visit of the directory tree. `visitor` receives each entry
  // with its full path ("root/storage/stream") and a flag indicating a
  // storage/root entry; returning false stops the walk. Path separator is '/'.
  template <typename F>
    requires std::invocable<F, const DirEntry&, std::string_view, bool>
  std::expected<void, Error> Visit(F&& visitor) const;

  uint32_t sector_size() const {
    return sector_size_;
  }

  uint16_t major_version() const {
    return major_version_;
  }

  // Non-fatal header/chain inconsistencies that Open() tolerated, plus
  // truncations met while reading a stream. Callers such as the CLI surface
  // these as "warning: ..." on stderr. ReadStream may append to this list, so
  // a caller that reads streams after Open() should print it again afterwards.
  const std::vector<std::string>& warnings() const {
    return warnings_;
  }

 private:
  CfbReader() = default;

  // Outcome of one tree walk. Open() uses it to tell links the walk actually
  // stepped on (the tree is truncated there) from links on records the tree
  // never reaches (the records are detached; their links are junk).
  struct WalkReport {
    // One entry per out-of-range link the walk hit: the slot owning the link
    // and the slot it names.
    std::vector<uint32_t> dangling_owners;
    std::vector<uint32_t> dangling_targets;
    std::vector<uint8_t> reached;  // 1 for every slot the walk entered
  };

  using Visitor = std::function<bool(const DirEntry&, std::string_view, bool)>;

  // Depth-first walk shared by Visit() and Open(); `report` is optional.
  std::expected<void, Error> Walk(const Visitor& visitor,
                                  WalkReport* report) const;
  // View of one sector; the span keeps the sector length so callers cannot
  // read past it.
  std::expected<std::span<const uint8_t>, Error> SectorBytes(
      uint32_t sector) const;
  // Reads `size` bytes from the FAT (regular) or MiniFAT chain. A chain that
  // walks off the physical file or ends before `size` yields the readable
  // prefix and records a truncation warning naming `name`; a cycle, an index
  // past the allocation table or a table shorter than the declared size is
  // still fatal.
  std::expected<std::vector<uint8_t>, Error> ReadChain(
      uint32_t start, uint64_t size, bool mini, std::string_view name) const;

  std::span<const uint8_t> data_;
  uint32_t sector_size_ = 512;
  uint32_t mini_sector_size_ = 64;
  uint16_t major_version_ = 3;
  uint32_t first_dir_sector_ = 0;
  uint32_t mini_fat_start_ = 0;
  uint32_t num_mini_fat_ = 0;
  std::vector<uint32_t> fat_;    // Flattened FAT table
  std::vector<uint32_t> difat_;  // Sector numbers of FAT sectors
  std::vector<uint32_t> mini_fat_;
  std::vector<DirEntry> entries_;
  std::vector<uint8_t> mini_stream_;
  // Open() seeds this and a const ReadStream() may append truncation warnings
  // while following a chain, so the physical state stays const-correct.
  mutable std::vector<std::string> warnings_;
  // Set by Open() when a link the tree walk actually stepped on names an entry
  // slot past the table, or when a directory chain was cut short after a
  // readable prefix: the tree is truncated. A name lookup then falls back to a
  // linear scan of `entries_` so physically present streams stay readable. Bad
  // links on records the tree never reaches do not set it; a complete tree
  // stays authoritative for those.
  bool directory_tree_incomplete_ = false;
};

template <typename F>
  requires std::invocable<F, const DirEntry&, std::string_view, bool>
std::expected<void, Error> CfbReader::Visit(F&& visitor) const {
  const Visitor fn(std::forward<F>(visitor));
  return Walk(fn, nullptr);
}

}  // namespace doccraft::cfb

#endif  // SRC_CFB_CFB_READER_H_
