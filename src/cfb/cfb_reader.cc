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

void Warn(std::vector<std::string>* warnings, std::string message) {
  if (warnings != nullptr)
    warnings->push_back(std::move(message));
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
  uint32_t num_dir_sectors = 0;
};

std::span<const uint8_t> SectorView(std::span<const uint8_t> bytes,
                                    uint32_t sector_size, uint32_t sector) {
  return bytes.subspan((static_cast<size_t>(sector) + 1) * sector_size,
                       sector_size);
}

// [MS-CFB] §2.2 describes the container as a whole number of sectors after the
// 512-byte header. Some real-world producers append a trailing fragment that is
// shorter than one sector (the wvWare samples end with a single zero byte).
// Such a fragment cannot belong to any FAT chain, so ignore it instead of
// rejecting the file. Anything reaching a chain that references the dropped
// bytes still fails cleanly in ReadChain ("sector outside file").
size_t UsableSize(size_t size, uint32_t sector_size) {
  return size / sector_size * sector_size;
}

std::expected<Header, Error> ParseHeader(std::span<const uint8_t> bytes,
                                         std::vector<std::string>* warnings) {
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
  // [MS-CFB] §2.2: Byte Order (offset 28) and Mini Stream Cutoff Size
  // (offset 56) are informational for this reader. Neither is used for
  // addressing (streams below a fixed 4096-byte threshold are read from the
  // mini stream), so tolerate the corrupt values seen in LibreOffice fuzz
  // regressions instead of rejecting the whole document.
  if (U16(bytes, 28) != kLittleEndianMark) {
    Warn(warnings, "header byte order mark is not 0xFFFE; ignoring it");
  }
  if (U32(bytes, 56) != kMiniStreamCutoff) {
    Warn(warnings,
         "header mini stream cutoff is not 4096; treating it as 4096");
  }
  header.sector_size = 1u << shift;
  if (bytes.size() < header.sector_size) {
    return std::unexpected(Fail(ErrorCode::kTruncated, "partial sector"));
  }
  // [MS-CFB] §2.2: Number of Directory Sectors (offset 40). Must be 0 for
  // v3; for v4 it holds the actual count of directory chain sectors. The v3
  // value is not used for addressing - the directory is reached through the
  // first-dir-sector FAT chain - so a non-zero field is only a warning.
  header.num_dir_sectors = U32(bytes, 40);
  if (header.major_version == 3 && header.num_dir_sectors != 0) {
    Warn(warnings,
         "v3 header has non-zero directory sector count; ignoring it");
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
  // LibreOffice writes FREESECT in the final DIFAT link. MS-CFB requires
  // ENDOFCHAIN, but accepting this terminator after the declared sector count
  // keeps its large DOC files readable without relaxing chain validation.
  const bool valid_end = at == kEnd || (header.difat_count > 0 && at == kFree);
  if (!valid_end || difat.entries.size() != header.fat_count) {
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

// [MS-CFB] §2.6.1 compares directory names with the Word up-wchar uppercase
// table. This reader folds ASCII only (no Unicode table dependency): all Word
// stream names are ASCII, so the 47950 lower/upper-case samples resolve while
// non-ASCII case variants remain distinct (KNOWN-SIMPLIFICATIONS).
bool AsciiEqualsNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char lhs = a[i];
    char rhs = b[i];
    if (lhs >= 'A' && lhs <= 'Z')
      lhs = static_cast<char>(lhs - 'A' + 'a');
    if (rhs >= 'A' && rhs <= 'Z')
      rhs = static_cast<char>(rhs - 'A' + 'a');
    if (lhs != rhs)
      return false;
  }
  return true;
}

// Decodes a UTF-16LE directory entry name. The declared length delimits the
// name even when the producer omitted the NUL terminator (POI test-fields.doc
// stores "Y:\Desktop\~WRD0000.tmp" with len=22); the extra declared bytes are
// then part of the name instead of an error.
std::expected<std::string, Error> DecodeEntryName(Bytes p, uint16_t len) {
  if (len < 2 || len > 64 || len % 2 != 0) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptDirectory, "invalid directory name"));
  }
  const size_t name_bytes = U16(p, len - 2) == 0 ? len - 2 : len;
  std::string name;
  for (size_t j = 0; j < name_bytes; j += 2) {
    uint32_t cp = U16(p, j);
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      if (j + 4 > name_bytes) {
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
      return std::unexpected(Fail(ErrorCode::kCorruptDirectory, "bad UTF-16"));
    }
    AppendUtf8(name, cp);
  }
  return name;
}

// Decodes one directory entry. A non-root entry with an undecodable name is
// degraded to kUnknown (unreachable) and reported; the root keeps rejecting
// because no stream path can be rooted.
std::expected<DirEntry, Error> ParseDirEntry(
    Bytes p, uint16_t version, size_t slot,
    std::vector<std::string>* warnings) {
  DirEntry entry{};
  entry.type = static_cast<EntryType>(p[66]);
  entry.left = U32(p, 68);
  entry.right = U32(p, 72);
  entry.child = U32(p, 76);
  entry.start_sector = U32(p, 116);
  entry.size = version == 3 ? U32(p, 120) : U64(p, 120);
  if (entry.type != EntryType::kStorage && entry.type != EntryType::kStream &&
      entry.type != EntryType::kRoot) {
    return entry;
  }
  auto name = DecodeEntryName(p, U16(p, 64));
  if (!name) {
    if (slot == 0)
      return std::unexpected(name.error());
    Warn(warnings, "invalid directory entry name at slot " +
                       std::to_string(slot) + "; entry skipped");
    entry.name.clear();
    entry.type = EntryType::kUnknown;
    entry.child = DirEntry::kNoFollow;
    return entry;
  }
  entry.name = std::move(*name);
  return entry;
}

// Walks the FAT directory chain and decodes every entry. `tail` is the
// trailing fragment that UsableSize dropped (empty when the file ends on a
// sector boundary); the directory chain may name it, in which case its
// complete 128-byte records are real directory entries.
std::expected<std::vector<DirEntry>, Error> LoadDirectory(
    std::span<const uint8_t> bytes, const Header& header,
    const std::vector<uint32_t>& fat, std::span<const uint8_t> tail,
    std::vector<std::string>* warnings) {
  std::vector<uint8_t> directory;
  std::vector<uint8_t> seen(header.total_sectors, 0);
  uint32_t at = header.first_dir_sector;
  while (at != kEnd) {
    // A directory FAT chain may name `total_sectors`, the sector just past the
    // last complete one. When the file ends with a physically present partial
    // fragment, that index is the fragment itself (r34 drops it before sector
    // reads); otherwise it is one full sector past the end. In both shapes the
    // sectors before it are intact, so stop there instead of rejecting the
    // container; deeper overruns still fail below.
    if (at == header.total_sectors && directory.size() >= header.sector_size) {
      if (!tail.empty()) {
        // The fragment is not FAT-addressable, but its complete records are
        // ordinary directory entries: LibreOffice sw/qa ofz18414-1 and
        // ofz57592-1 keep their last streams only there.
        directory.insert(directory.end(), tail.begin(), tail.end());
        Warn(warnings, "directory chain names the trailing partial sector; " +
                           std::to_string(tail.size() / kDirEntrySize) +
                           " complete entries read from it");
      } else {
        Warn(warnings,
             "directory chain ends one sector past end of file; "
             "using the sectors read");
      }
      break;
    }
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
  // [MS-CFB] §2.2: for v4 the header field must equal the directory chain
  // length; a mismatch means the header lies about its own structure.
  const uint64_t chain_sectors = directory.size() / header.sector_size;
  if (header.major_version == 4 && chain_sectors != header.num_dir_sectors) {
    return std::unexpected(
        Fail(ErrorCode::kCorruptDirectory, "directory sector count mismatch"));
  }
  std::vector<DirEntry> entries;
  for (size_t i = 0; i + kDirEntrySize <= directory.size();
       i += kDirEntrySize) {
    const size_t slot = i / kDirEntrySize;
    auto entry = ParseDirEntry(Bytes(directory).subspan(i, kDirEntrySize),
                               header.major_version, slot, warnings);
    if (!entry) {
      return std::unexpected(entry.error());
    }
    entries.push_back(std::move(*entry));
  }
  return entries;
}

// A directory link naming a slot at or past the end of the entry table cannot
// reach any record. [MS-CFB] §2.6.4 spells the terminator NOSTREAM; a link
// past the table is treated the same way by the tree walk. Only links the walk
// actually steps on prove the tree is truncated; the same damage on a record
// the walk never reaches is junk in a complete tree.
bool IsDanglingLink(uint32_t link, size_t entry_count) {
  return link != DirEntry::kNoFollow && link >= entry_count;
}

bool CarriesDanglingLink(const DirEntry& entry, size_t entry_count) {
  return IsDanglingLink(entry.left, entry_count) ||
         IsDanglingLink(entry.right, entry_count) ||
         IsDanglingLink(entry.child, entry_count);
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
      // Tolerate one trailing chain sector beyond the declared size when it
      // is properly ENDOFCHAIN-terminated: some real-world producers round
      // streams up to an extra sector and keep the terminator on that one.
      if (next != kEnd) {
        if (next >= table.size() || seen[next]) {
          return std::unexpected(
              Fail(ErrorCode::kCorruptFat, "chain longer than stream"));
        }
        auto extra = mini ? Bytes() : SectorBytes(next);
        if (!mini && !extra) {
          return std::unexpected(extra.error());
        }
        seen[next] = 1;
        at = next;
        continue;  // the following iteration must see ENDOFCHAIN
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
  std::vector<std::string> warnings;
  auto header = ParseHeader(bytes, &warnings);
  if (!header) {
    return std::unexpected(header.error());
  }
  // Drop any trailing partial sector before sector, FAT or directory reads.
  // The unchecked byte helpers (U16/U32) would otherwise be able to run past
  // the real end of the buffer when the last sector is only partially present.
  // Keep the dropped fragment separate so the directory chain can tell
  // "names the trailing partial sector" from "one full sector past end".
  const size_t usable = UsableSize(bytes.size(), header->sector_size);
  const std::span<const uint8_t> tail = bytes.subspan(usable);
  bytes = bytes.first(usable);
  auto tables = LoadFatTables(bytes, *header);
  if (!tables) {
    return std::unexpected(tables.error());
  }
  auto entries = LoadDirectory(bytes, *header, tables->fat, tail, &warnings);
  if (!entries) {
    return std::unexpected(entries.error());
  }
  CfbReader out;
  out.warnings_ = std::move(warnings);
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
  WalkReport walk;
  auto visited = out.Walk(
      [](const DirEntry&, std::string_view, bool) {
        return true;
      },
      &walk);
  if (!visited) {
    return std::unexpected(visited.error());
  }
  // A link the walk stepped on is where the tree ends: drop it, warn once per
  // damaged entry (slot and name, its links merged) and open the name-only
  // salvage. Sorting keeps the warning order stable.
  std::vector<std::pair<uint32_t, uint32_t>> hits;
  hits.reserve(walk.dangling_owners.size());
  for (size_t i = 0; i < walk.dangling_owners.size(); ++i)
    hits.emplace_back(walk.dangling_owners[i], walk.dangling_targets[i]);
  std::sort(hits.begin(), hits.end());
  for (size_t i = 0; i < hits.size();) {
    const uint32_t slot = hits[i].first;
    std::vector<uint32_t> targets;
    for (; i < hits.size() && hits[i].first == slot; ++i) {
      if (targets.empty() || targets.back() != hits[i].second)
        targets.push_back(hits[i].second);
    }
    std::string list;
    for (uint32_t target : targets) {
      if (!list.empty())
        list += ", ";
      list += std::to_string(target);
    }
    const std::string owner =
        "slot " + std::to_string(slot) + " ('" + out.entries_[slot].name + "')";
    if (targets.size() == 1) {
      Warn(&out.warnings_, "directory link from " + owner + " to slot " + list +
                               " points past the entry table; link ignored");
    } else {
      Warn(&out.warnings_, "directory links from " + owner + " to slots " +
                               list +
                               " point past the entry table; links "
                               "ignored");
    }
    out.directory_tree_incomplete_ = true;
  }
  if (!hits.empty()) {
    Warn(&out.warnings_,
         "directory tree is incomplete; unreachable entries are matched by "
         "name only");
  }
  // The same damage on records the tree never reaches does not prove the tree
  // is truncated, so it neither sets the flag nor opens the salvage by itself;
  // a complete tree stays authoritative. The summary therefore must not claim
  // the tree is complete: the reachable half may carry its own damage. One line
  // keeps a corrupt table from flooding the log.
  size_t stranded = 0;
  for (size_t slot = 0; slot < out.entries_.size(); ++slot) {
    const DirEntry& entry = out.entries_[slot];
    if (walk.reached[slot])
      continue;
    if (entry.type != EntryType::kRoot && entry.type != EntryType::kStorage &&
        entry.type != EntryType::kStream) {
      continue;
    }
    if (CarriesDanglingLink(entry, out.entries_.size()))
      ++stranded;
  }
  if (stranded != 0) {
    Warn(&out.warnings_,
         std::to_string(stranded) +
             (stranded == 1 ? " unreachable directory record has a link"
                            : " unreachable directory records have links") +
             " past the entry table; ignored, does not enable the name "
             "fallback by itself");
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

std::expected<void, Error> CfbReader::Walk(const Visitor& visitor,
                                           WalkReport* report) const {
  if (entries_.empty()) {
    return std::unexpected(Error{ErrorCode::kCorruptDirectory, "missing root"});
  }
  if (report != nullptr)
    report->reached.assign(entries_.size(), 0);

  std::vector<uint8_t> seen(entries_.size(), 0);

  std::vector<uint32_t> parents(entries_.size(), DirEntry::kNoFollow);
  constexpr uint32_t kMaxDepth = 256;
  struct Task {
    uint32_t index;
    uint32_t parent;
    uint32_t depth;
    bool emit;
    uint32_t owner;  // slot whose link queued this task
  };
  std::vector<Task> tasks{
      {0, DirEntry::kNoFollow, 0, false, DirEntry::kNoFollow}};

  while (!tasks.empty()) {
    Task task = tasks.back();
    tasks.pop_back();

    if (task.index == DirEntry::kNoFollow)
      continue;
    if (task.index >= entries_.size()) {
      // [MS-CFB] §2.6.4 links name slot indices in the directory. A link past
      // the table reaches no record, so it terminates the walk like NOSTREAM.
      // Open() uses the report to tell this truncated-tree case from the same
      // damage on a record the walk never reached.
      if (report != nullptr && task.owner != DirEntry::kNoFollow) {
        report->dangling_owners.push_back(task.owner);
        report->dangling_targets.push_back(task.index);
      }
      continue;
    }
    if (task.depth > kMaxDepth) {
      return std::unexpected(Error{ErrorCode::kCorruptDirectory,
                                   "directory nesting exceeds limit"});
    }
    const DirEntry& entry = entries_[task.index];
    if (task.emit) {
      if (entry.type != EntryType::kRoot && entry.type != EntryType::kStorage &&
          entry.type != EntryType::kStream) {
        // LoadDirectory degraded an invalid non-root name to kUnknown: it has
        // no path and its children are unreachable. Siblings stay queued.
        continue;
      }
      std::vector<std::string_view> parts;
      parts.reserve(task.depth + 1);

      for (uint32_t at = task.index; at != DirEntry::kNoFollow;
           at = parents[at]) {
        parts.push_back(entries_[at].name);
      }
      std::string path;
      for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!path.empty())
          path += '/';
        path += *it;
      }
      if (!static_cast<bool>(visitor(entry, path,
                                     entry.type == EntryType::kRoot ||
                                         entry.type == EntryType::kStorage)))
        return {};

      if (entry.child != DirEntry::kNoFollow) {
        if (entry.type != EntryType::kRoot &&
            entry.type != EntryType::kStorage) {
          return std::unexpected(
              Error{ErrorCode::kCorruptDirectory, "stream has children"});
        }
        tasks.push_back(
            {entry.child, task.index, task.depth + 1, false, task.index});
      }
      continue;
    }
    if (seen[task.index]) {
      return std::unexpected(Error{ErrorCode::kCorruptDirectory,
                                   "directory link is invalid or cyclic"});
    }
    seen[task.index] = 1;
    if (report != nullptr)
      report->reached[task.index] = 1;
    parents[task.index] = task.parent;
    tasks.push_back({entry.right, task.parent, task.depth, false, task.index});
    tasks.push_back(
        {task.index, task.parent, task.depth, true, DirEntry::kNoFollow});
    tasks.push_back({entry.left, task.parent, task.depth, false, task.index});
  }
  return {};
}

std::expected<std::vector<uint8_t>, Error> CfbReader::ReadStream(
    std::string_view name) const {
  // Disambiguate '/' by priority: full path > relative path > entry name.
  // Names compare ASCII case-insensitively ([MS-CFB] §2.6.1); ties keep the
  // existing directory-order-first rule. Collect the best match instead of
  // returning the first directory-order hit, so a stream named "a/b" can no
  // longer shadow storage "a"/stream "b".
  const DirEntry* best = nullptr;
  int best_rank = -1;  // 2 = full path, 1 = relative, 0 = entry name
  auto visited = Visit([&](const DirEntry& entry, std::string_view path, bool) {
    if (entry.type != EntryType::kStream)
      return true;
    const size_t prefix_length = entries_[0].name.size() + 1;
    if (path.size() <= prefix_length) {
      return true;  // skip malformed; handled by directory validation
    }
    const std::string_view relative = path.substr(prefix_length);
    int rank = -1;
    if (AsciiEqualsNoCase(path, name))
      rank = 2;
    else if (AsciiEqualsNoCase(relative, name))
      rank = 1;
    else if (AsciiEqualsNoCase(entry.name, name))
      rank = 0;
    if (rank > best_rank) {
      best_rank = rank;
      best = &entry;
    }
    return true;
  });
  if (!visited)
    return std::unexpected(visited.error());
  if (!best) {
    if (!directory_tree_incomplete_) {
      return std::unexpected(
          Fail(ErrorCode::kStreamNotFound, "stream not found"));
    }
    // The walk hit a link past the entry table, so at least one record may be
    // unreachable through the tree. The tree cannot prove the stream absent,
    // so scan the entry table for a name match (still ASCII case-insensitive).
    // Salvaged entries are matched by their own name; their storage path is
    // unknown.
    for (const DirEntry& entry : entries_) {
      if (entry.type == EntryType::kStream && !entry.name.empty() &&
          AsciiEqualsNoCase(entry.name, name)) {
        best = &entry;
        break;
      }
    }
    if (!best) {
      return std::unexpected(
          Fail(ErrorCode::kStreamNotFound, "stream not found"));
    }
  }
  return ReadChain(best->start_sector, best->size, best->size < 4096);
}
}  // namespace doccraft::cfb
