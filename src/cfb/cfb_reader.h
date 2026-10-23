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

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
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
  kIoError,
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
  // with its full path ("root/storage/stream"); returning false stops the
  // walk. Path separator is '/'.
  template <typename F>
    requires std::invocable<F, const DirEntry&, std::string_view, bool>
  std::expected<void, Error> Visit(F&& visitor) const;

  uint32_t sector_size() const { return sector_size_; }
  uint16_t minor_version() const { return minor_version_; }
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
  uint16_t minor_version_ = 0x003E;
  uint32_t first_dir_sector_ = 0;
  uint32_t mini_fat_start_ = 0;
  uint32_t num_mini_fat_ = 0;
  std::vector<uint32_t> fat_;         // Flattened FAT table
  std::vector<uint32_t> difat_;       // Sector numbers of FAT sectors
  std::vector<uint32_t> mini_fat_;
  std::vector<DirEntry> entries_;
};

}  // namespace doccraft::cfb

#endif  // DOCCRAFT_CFB_CFB_READER_H_
