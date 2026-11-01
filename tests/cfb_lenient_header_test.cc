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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "cfb/cfb_reader.h"
#include "tests/cfb_test_util.h"

namespace doccraft::cfb {
namespace {

using test::CfbBuilder;
using test::MakePayload;

constexpr uint32_t kEnd = 0xFFFFFFFE;
constexpr uint32_t kFree = 0xFFFFFFFF;
constexpr uint32_t kDifat = 0xFFFFFFFC;
constexpr size_t kHeaderDifatOffset = 76;
constexpr size_t kHeaderDifatSlots = 109;

uint16_t U16(const std::vector<uint8_t>& bytes, size_t at) {
  return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

uint32_t U32(const std::vector<uint8_t>& bytes, size_t at) {
  return static_cast<uint32_t>(bytes[at]) |
         (static_cast<uint32_t>(bytes[at + 1]) << 8) |
         (static_cast<uint32_t>(bytes[at + 2]) << 16) |
         (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
  bytes[at] = static_cast<uint8_t>(value);
  bytes[at + 1] = static_cast<uint8_t>(value >> 8);
}

void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}

bool HasWarning(const CfbReader& reader, const std::string& needle) {
  return std::any_of(reader.warnings().begin(), reader.warnings().end(),
                     [&](const std::string& warning) {
                       return warning.find(needle) != std::string::npos;
                     });
}

// Offset of the FAT entry that describes `sector` in a builder-produced
// container whose FAT lives entirely in the header DIFAT array.
size_t FatEntryOffset(const std::vector<uint8_t>& bytes, uint32_t sector) {
  const uint32_t sector_size = 1u << U16(bytes, 30);
  const uint32_t fat_sector = U32(bytes, 76);
  return (static_cast<size_t>(fat_sector) + 1) * sector_size + sector * 4;
}

size_t HeaderDifatSlot(size_t slot) {
  return kHeaderDifatOffset + slot * 4;
}

uint32_t TotalSectors(const std::vector<uint8_t>& bytes) {
  return static_cast<uint32_t>(bytes.size() / (1u << U16(bytes, 30)) - 1);
}

// A valid container with one mini stream; callers corrupt one header field.
std::vector<uint8_t> ValidContainer() {
  return CfbBuilder().AddStream("s", MakePayload(100, 7)).Build();
}

// Appends a one-sector DIFAT chain naming the existing FAT sector and stores
// `marker` in the FAT entry describing the new DIFAT sector. kDifat builds a
// self-consistent container; any other value models the broken self-marker
// that r55 tolerates.
std::vector<uint8_t> ContainerWithDifatSector(uint32_t marker) {
  auto bytes = ValidContainer();
  const uint32_t sector_size = 1u << U16(bytes, 30);
  const uint32_t difat_sector = TotalSectors(bytes);
  bytes.resize(bytes.size() + sector_size, 0);
  Put32(bytes, 68, difat_sector);  // difat_start
  Put32(bytes, 72, 1);             // difat_count
  const size_t base = (static_cast<size_t>(difat_sector) + 1) * sector_size;
  Put32(bytes, base, U32(bytes, 76));  // the container's real FAT sector
  for (uint32_t j = 1; j + 1 < sector_size / 4; ++j)
    Put32(bytes, base + j * 4, kFree);
  Put32(bytes, base + sector_size - 4, kEnd);
  Put32(bytes, FatEntryOffset(bytes, difat_sector), marker);
  return bytes;
}

TEST(CfbLenientHeader, ToleratesInvalidByteOrderMark) {
  auto bytes = ValidContainer();
  Put16(bytes, 28, 0xFF20);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "byte order"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

TEST(CfbLenientHeader, ToleratesInvalidMiniStreamCutoff) {
  auto bytes = ValidContainer();
  Put32(bytes, 56, 0x0010);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "mini stream cutoff"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

TEST(CfbLenientHeader, ToleratesV3DirectorySectorCount) {
  auto bytes = ValidContainer();
  Put32(bytes, 40, 3);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "directory sector count"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

TEST(CfbLenientHeader, ToleratesDirectoryChainOneSectorPastEnd) {
  auto bytes = ValidContainer();
  const uint32_t total =
      static_cast<uint32_t>(bytes.size() / (1u << U16(bytes, 30)) - 1);
  const uint32_t dir_start = U32(bytes, 48);
  Put32(bytes, FatEntryOffset(bytes, dir_start), total);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "one sector past"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

// A directory FAT chain may end at the sector just past the last complete one
// when the file ends with a physically present partial sector (r34 drops the
// fragment before sector reads). The fragment's sector index equals
// total_sectors, so the last link resolves to it; Open must keep the sectors
// already read and report exactly one warning that names the partial sector.
TEST(CfbLenientHeader, ToleratesDirectoryChainIntoTailingPartialSector) {
  constexpr size_t kExtraSizes[] = {1, 100, 511};
  for (uint32_t sector_size : {512u, 4096u}) {
    auto base = sector_size == 512 ? ValidContainer()
                                   : CfbBuilder()
                                         .SetSectorSize(4096)
                                         .AddStream("s", MakePayload(100, 7))
                                         .Build();
    const uint32_t total = static_cast<uint32_t>(base.size() / sector_size - 1);
    const uint32_t dir_start = U32(base, 48);
    for (size_t extra : kExtraSizes) {
      if (extra >= sector_size)
        continue;
      auto bytes = base;
      bytes.insert(bytes.end(), extra, 0x00);
      // Appending fewer than sector_size bytes leaves total_sectors unchanged
      // and gives the fragment the index total_sectors.
      ASSERT_EQ(static_cast<uint32_t>(bytes.size() / sector_size - 1), total);
      Put32(bytes, FatEntryOffset(bytes, dir_start), total);
      auto reader = CfbReader::Open(bytes);
      ASSERT_TRUE(reader.has_value())
          << "sector=" << sector_size << " extra=" << extra << ": "
          << reader.error().message;
      ASSERT_EQ(reader->warnings().size(), 1u)
          << "sector=" << sector_size << " extra=" << extra;
      EXPECT_TRUE(HasWarning(*reader, "partial"))
          << "sector=" << sector_size << " extra=" << extra;
      auto stream = reader->ReadStream("s");
      ASSERT_TRUE(stream.has_value()) << stream.error().message;
      EXPECT_EQ(*stream, MakePayload(100, 7));
    }
  }
}

TEST(CfbLenientHeader, RejectsCyclicDirectoryChain) {
  auto bytes = ValidContainer();
  const uint32_t dir_start = U32(bytes, 48);
  Put32(bytes, FatEntryOffset(bytes, dir_start), dir_start);
  auto reader = CfbReader::Open(bytes);
  ASSERT_FALSE(reader.has_value());
  EXPECT_EQ(reader.error().code, ErrorCode::kCorruptDirectory);
}

// A directory chain that leaves the file after its first sector keeps that
// sector (r52). Only a chain whose very first sector lies outside the file has
// no record to anchor a recovery and stays rejected; that boundary is pinned
// by CfbReaderTest.RejectsDirectoryChainStartingOutsideTheFile.
TEST(CfbLenientHeader, SalvagesDirectoryChainTwoSectorsPastEnd) {
  auto bytes = ValidContainer();
  const uint32_t total = TotalSectors(bytes);
  const uint32_t dir_start = U32(bytes, 48);
  Put32(bytes, FatEntryOffset(bytes, dir_start), total + 1);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "cut short after 1 sector"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

// r49: when the allocation metadata in the header cannot be believed, the FAT
// sector numbers are re-derived from the 109-slot header DIFAT array. This is
// the cluster-A shape: fat_count (34) is one past the physical sector count
// (33) while the array itself still names one valid FAT sector and then
// FREESECT. Exactly one warning names the field that was discarded.
TEST(CfbLenientHeader, DerivesFatSectorsWhenFatCountIsImpossible) {
  auto bytes = ValidContainer();
  Put32(bytes, 44, TotalSectors(bytes) + 1);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "FAT sector count is impossible"));
  EXPECT_TRUE(HasWarning(*reader, "deriving FAT sectors"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

// r49 cluster C: the header DIFAT array holds a valid sector, then three
// out-of-range junk entries, then ENDOFCHAIN. The junk is skipped without
// aborting the scan and the terminator ends it, so one FAT sector is found.
// difat_count (3595) is itself impossible, which is what triggers the
// re-derivation; no DIFAT chain is followed.
TEST(CfbLenientHeader, SkipsOutOfRangeHeaderDifatEntriesWhileDeriving) {
  auto bytes = ValidContainer();
  const uint32_t total = TotalSectors(bytes);
  Put32(bytes, 68, kEnd);       // difat_start
  Put32(bytes, 72, total + 1);  // difat_count
  Put32(bytes, HeaderDifatSlot(1), 0xFF18FFFF);
  Put32(bytes, HeaderDifatSlot(2), 0x7FFFFFFF);
  Put32(bytes, HeaderDifatSlot(3), 0xFFB7FFFF);
  Put32(bytes, HeaderDifatSlot(4), kEnd);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "DIFAT sector count is impossible"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

// r49 cluster D: every header DIFAT slot is out-of-range junk, so the array
// yields no FAT sector at all. The container stays rejected at the CFB layer.
TEST(CfbLenientHeader, RejectsWhenHeaderDifatYieldsNoFatSector) {
  auto bytes = ValidContainer();
  Put32(bytes, 44, TotalSectors(bytes) + 1);
  for (size_t slot = 0; slot < kHeaderDifatSlots; ++slot)
    Put32(bytes, HeaderDifatSlot(slot), 0xFFFF0000);
  auto reader = CfbReader::Open(bytes);
  ASSERT_FALSE(reader.has_value());
  EXPECT_EQ(reader.error().code, ErrorCode::kCorruptFat);
  EXPECT_NE(reader.error().message.find("impossible sector count"),
            std::string::npos)
      << reader.error().message;
}

// Re-derivation follows the same r55 marker policy: pointing the array at an
// ordinary sector is now a warning and that sector's bytes are used as the
// allocation table; out-of-range indices still fail in LoadFatTables.
TEST(CfbLenientHeader, DerivedFatSectorWithoutMarkerIsTrusted) {
  auto bytes = ValidContainer();
  Put32(bytes, 44, TotalSectors(bytes) + 1);
  Put32(bytes, HeaderDifatSlot(0), U32(bytes, 48));  // the directory sector
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  EXPECT_TRUE(
      HasWarning(*reader, "FAT sector 0 is not marked as a FAT sector"));
}

// r55: a FAT table that leaves FREESECT in the entry describing itself stays
// readable. Only the self-marker is ignored; the DIFAT still supplies the
// sector and the tables are used as-is.
TEST(CfbLenientHeader, UnmarkedFatSectorIsTrusted) {
  auto bytes = ValidContainer();
  const uint32_t fat_sector = U32(bytes, 76);
  Put32(bytes, FatEntryOffset(bytes, fat_sector), kFree);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "FAT sector " + std::to_string(fat_sector) +
                                      " is not marked as a FAT sector"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

// r55: the companion check for a DIFAT chain sector. The crafted container is
// first validated with the correct marker (no warning), then the marker is
// replaced with FREESECT and only the warning remains.
TEST(CfbLenientHeader, UnmarkedDifatSectorIsTrusted) {
  auto valid = ContainerWithDifatSector(kDifat);
  auto healthy = CfbReader::Open(valid);
  ASSERT_TRUE(healthy.has_value()) << healthy.error().message;
  EXPECT_TRUE(healthy->warnings().empty());
  auto stream = healthy->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));

  auto bytes = ContainerWithDifatSector(kFree);
  const uint32_t difat_sector = U32(bytes, 68);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "sector " + std::to_string(difat_sector) +
                                      " is claimed as a DIFAT sector"));
  stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

// r49: an impossible mini FAT sector count disables the mini FAT instead of
// rejecting the container. A regular (>=4096-byte) stream stays readable.
TEST(CfbLenientHeader, ImpossibleMiniFatCountIsIgnored) {
  auto bytes = CfbBuilder().AddStream("s", MakePayload(5000, 9)).Build();
  Put32(bytes, 64, TotalSectors(bytes) + 1);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "mini FAT sector count is impossible"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(5000, 9));
}

// The start sector is the companion field of the count: a mini FAT whose
// declared start lies outside the file cannot be read, so it is ignored the
// same way.
TEST(CfbLenientHeader, ImpossibleMiniFatStartIsIgnored) {
  auto bytes = CfbBuilder().AddStream("s", MakePayload(5000, 9)).Build();
  Put32(bytes, 64, 1);  // num_mini_fat
  Put32(bytes, 60, TotalSectors(bytes) + 1);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "mini FAT start sector is impossible"));
  auto stream = reader->ReadStream("s");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(5000, 9));
}

// A container whose header fields are self-consistent takes the original path
// and gains no r49 warning.
TEST(CfbLenientHeader, HealthyHeaderKeepsNoAllocationWarning) {
  auto bytes = ValidContainer();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  EXPECT_TRUE(reader->warnings().empty());
}

}  // namespace
}  // namespace doccraft::cfb
