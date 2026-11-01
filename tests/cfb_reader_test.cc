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

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "tests/cfb_test_util.h"

namespace doccraft::cfb {
namespace {

using test::CfbBuilder;
using test::MakePayload;

void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
  bytes[at] = static_cast<uint8_t>(value);
  bytes[at + 1] = static_cast<uint8_t>(value >> 8);
}

void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t Get32(const std::vector<uint8_t>& bytes, size_t at) {
  return static_cast<uint32_t>(bytes[at]) |
         (static_cast<uint32_t>(bytes[at + 1]) << 8) |
         (static_cast<uint32_t>(bytes[at + 2]) << 16) |
         (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

// Byte offset of directory slot `slot` in a builder-produced v3 container.
size_t DirSlotOffset(const std::vector<uint8_t>& bytes, size_t slot) {
  const uint32_t sector_size = 1u << (bytes[30] | (bytes[31] << 8));
  const uint32_t dir_start = Get32(bytes, 48);
  return (static_cast<size_t>(dir_start) + 1) * sector_size + slot * 128;
}

// Rewrite the declared size of directory slot `slot` without touching the
// sectors the builder allocated for it. Used to model a chain that carries a
// trailing sector beyond the declared length.
void SetStreamSize(std::vector<uint8_t>& bytes, size_t slot, uint64_t size) {
  const size_t at = DirSlotOffset(bytes, slot) + 120;
  Put32(bytes, at, static_cast<uint32_t>(size));
  Put32(bytes, at + 4, static_cast<uint32_t>(size >> 32));
}

// Payload whose 512-byte sectors carry different fills. Uniform payloads make
// a shifted chain indistinguishable from the correct one.
std::vector<uint8_t> SectorPattern(size_t size) {
  constexpr size_t kSector = 512;
  std::vector<uint8_t> out(size);
  for (size_t i = 0; i < size; ++i)
    out[i] = static_cast<uint8_t>(0x21 + (i / kSector) * 0x11);
  return out;
}

// Concatenates the named sectors of a SectorPattern payload.
std::vector<uint8_t> SelectSectors(const std::vector<uint8_t>& payload,
                                   std::initializer_list<size_t> sectors) {
  constexpr size_t kSector = 512;
  std::vector<uint8_t> out;
  for (size_t sector : sectors) {
    out.insert(
        out.end(), payload.begin() + static_cast<ptrdiff_t>(sector * kSector),
        payload.begin() + static_cast<ptrdiff_t>((sector + 1) * kSector));
  }
  return out;
}

// Byte offset of the FAT entry describing `sector` when the whole FAT lives in
// the header DIFAT array (the shape CfbBuilder emits).
size_t FatEntryOffset(const std::vector<uint8_t>& bytes, uint32_t sector) {
  const uint32_t sector_size = 1u << (bytes[30] | (bytes[31] << 8));
  const uint32_t fat_sector = Get32(bytes, 76);
  return (static_cast<size_t>(fat_sector) + 1) * sector_size + sector * 4;
}

bool HasWarning(const CfbReader& reader, const std::string& needle) {
  return std::any_of(reader.warnings().begin(), reader.warnings().end(),
                     [&](const std::string& warning) {
                       return warning.find(needle) != std::string::npos;
                     });
}

// Start sector of a named stream in a healthy builder-produced container.
uint32_t StreamStart(const std::vector<uint8_t>& bytes, std::string_view name) {
  auto reader = CfbReader::Open(bytes);
  if (!reader)
    return DirEntry::kNoFollow;
  uint32_t start = DirEntry::kNoFollow;
  auto visited =
      reader->Visit([&](const DirEntry& entry, std::string_view, bool storage) {
        if (!storage && entry.name == name) {
          start = entry.start_sector;
          return false;
        }
        return true;
      });
  return visited ? start : DirEntry::kNoFollow;
}

// First data sector index past the physical end of a v3 container.
uint32_t PhysicalSectorEnd(const std::vector<uint8_t>& bytes) {
  return static_cast<uint32_t>(bytes.size() / 512) - 1;
}

// A compact v4 container whose FAT table describes far more sectors than exist.
std::vector<uint8_t> AllFatV4() {
  constexpr uint32_t kFatCount = 128;
  constexpr uint32_t kSectorSize = 4096;
  constexpr uint32_t kDifatSector = kFatCount + 1;
  constexpr uint32_t kEnd = 0xFFFFFFFE;
  constexpr uint32_t kFree = 0xFFFFFFFF;
  std::vector<uint8_t> bytes((kDifatSector + 2) * kSectorSize, 0);
  const uint8_t signature[] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

  std::copy(std::begin(signature), std::end(signature), bytes.begin());
  bytes[26] = 4;
  bytes[28] = 0xFE;
  bytes[29] = 0xFF;
  bytes[30] = 12;
  bytes[32] = 6;
  Put32(bytes, 40, 1);
  Put32(bytes, 44, kFatCount);
  Put32(bytes, 48, 0);
  Put32(bytes, 56, 4096);
  Put32(bytes, 60, kEnd);
  Put32(bytes, 68, kDifatSector);
  Put32(bytes, 72, 1);
  for (uint32_t i = 0; i < 109; ++i)
    Put32(bytes, 76 + 4 * i, i + 1);
  const size_t difat_at = (static_cast<size_t>(kDifatSector) + 1) * kSectorSize;
  for (uint32_t i = 0; i < 1023; ++i) {
    Put32(bytes, difat_at + 4 * i, i < kFatCount - 109 ? i + 110 : kFree);
  }

  Put32(bytes, difat_at + kSectorSize - 4, kEnd);
  for (uint32_t i = 0; i < kFatCount; ++i) {
    const size_t at = (static_cast<size_t>(i) + 2) * kSectorSize;
    for (uint32_t j = 0; j < 1024; ++j)
      Put32(bytes, at + 4 * j, kFree);
  }
  auto fat = [&](uint32_t sector, uint32_t value) {
    const size_t at = (static_cast<size_t>(sector / 1024) + 2) * kSectorSize;
    Put32(bytes, at + 4 * (sector % 1024), value);
  };
  fat(0, kEnd);
  for (uint32_t i = 1; i <= kFatCount; ++i)
    fat(i, 0xFFFFFFFD);
  fat(kDifatSector, 0xFFFFFFFC);
  auto entry = [&](size_t at, const char* name, uint8_t type, uint32_t child,
                   uint32_t start, uint64_t size) {
    const size_t length = std::strlen(name);
    for (size_t i = 0; i < length; ++i)
      bytes[at + 2 * i] = static_cast<uint8_t>(name[i]);
    bytes[at + 64] = static_cast<uint8_t>((length + 1) * 2);
    bytes[at + 66] = type;
    Put32(bytes, at + 68, kFree);
    Put32(bytes, at + 72, kFree);
    Put32(bytes, at + 76, child);
    Put32(bytes, at + 116, start);
    Put32(bytes, at + 120, static_cast<uint32_t>(size));
    Put32(bytes, at + 124, static_cast<uint32_t>(size >> 32));
  };
  entry(kSectorSize, "Root Entry", 5, 1, kEnd, 0);
  entry(kSectorSize + 128, "Data", 2, kFree, 1,
        static_cast<uint64_t>(kFatCount) * 1024 * kSectorSize);
  return bytes;
}

TEST(CfbReaderTest, RejectsNonCfbBytes) {
  std::vector<uint8_t> garbage(1024, 0x42);
  auto result = CfbReader::Open(garbage);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kNotCompoundFile);
}

TEST(CfbReaderTest, RejectsTruncatedHeader) {
  std::vector<uint8_t> bytes(8);  // shorter than 512-byte header
  auto result = CfbReader::Open(bytes);
  ASSERT_FALSE(result.has_value());
}

// The following tests define the M1 acceptance surface. They are DISABLED_
// until the builder + reader are implemented (TDD red phase tracked in
// docs/DESIGN.md §7 M1); enable by removing the prefix.

TEST(CfbReaderTest, OpensSyntheticSingleStream) {
  CfbBuilder b;
  auto payload = MakePayload(8192, 0xAB);
  b.AddStream("WordDocument", payload);
  auto container = b.Build();
  auto reader = CfbReader::Open(container);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  auto stream = reader->ReadStream("WordDocument");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(stream->size(), payload.size());
  EXPECT_EQ(std::memcmp(stream->data(), payload.data(), payload.size()), 0);
}

TEST(CfbReaderTest, ReportsSectorSize) {
  CfbBuilder b;
  b.AddStream("s", MakePayload(4096, 1));
  auto reader = CfbReader::Open(b.Build());
  ASSERT_TRUE(reader.has_value());
  EXPECT_EQ(reader->sector_size(), 512u);
  EXPECT_EQ(reader->major_version(), 3u);
}

TEST(CfbReaderTest, ReadsMiniStreamAndNestedDirectory) {
  CfbBuilder b;
  auto payload = MakePayload(129, 0x5A);
  b.AddStream("ObjectPool/Item/Data", payload);
  auto bytes = b.Build();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  auto stream = reader->ReadStream("ObjectPool/Item/Data");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, payload);
  std::vector<std::string> paths;
  auto visited =
      reader->Visit([&](const DirEntry&, std::string_view path, bool) {
        paths.emplace_back(path);
        return true;
      });
  ASSERT_TRUE(visited.has_value());
  EXPECT_EQ(paths,
            (std::vector<std::string>{"Root Entry", "Root Entry/ObjectPool",
                                      "Root Entry/ObjectPool/Item",
                                      "Root Entry/ObjectPool/Item/Data"}));
  paths.clear();
  visited = reader->Visit([&](const DirEntry&, std::string_view path, bool) {
    paths.emplace_back(path);
    return false;
  });
  ASSERT_TRUE(visited.has_value());
  EXPECT_EQ(paths.size(), 1u);
}

TEST(CfbReaderTest, ReadsFourKilobyteSectors) {
  CfbBuilder b;
  b.SetSectorSize(4096).AddStream("Data", MakePayload(5000, 0x21));
  auto bytes = b.Build();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  EXPECT_EQ(reader->sector_size(), 4096u);
  EXPECT_EQ(reader->major_version(), 4u);
  auto stream = reader->ReadStream("Data");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(5000, 0x21));
}

TEST(CfbReaderTest, ReadsDifatExtension) {
  CfbBuilder b;
  b.AddStream("Large", MakePayload(7 * 1024 * 1024, 0x37));
  auto bytes = b.Build();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  auto stream = reader->ReadStream("Large");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(7 * 1024 * 1024, 0x37));
}

TEST(CfbReaderTest, ReadsLibreOfficeFreeTerminatedDifat) {
  CfbBuilder b;
  b.AddStream("Large", MakePayload(7 * 1024 * 1024, 0x37));
  auto bytes = b.Build();
  auto u32 = [&](size_t at) {
    return static_cast<uint32_t>(bytes[at]) |
           (static_cast<uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<uint32_t>(bytes[at + 3]) << 24);
  };
  ASSERT_GT(u32(72), 0u);
  const size_t difat_at = (static_cast<size_t>(u32(68)) + 2) * 512 - 4;
  Put32(bytes, difat_at, 0xFFFFFFFFu);  // LibreOffice's final DIFAT link.
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  auto stream = reader->ReadStream("Large");
  ASSERT_TRUE(stream.has_value()) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(7 * 1024 * 1024, 0x37));
}

TEST(CfbReaderTest, RejectsCorruptLinksAndPartialSector) {
  CfbBuilder b;
  b.AddStream("Data", MakePayload(4096, 0x31));
  auto good = b.Build();
  auto truncated = good;
  truncated.pop_back();
  EXPECT_FALSE(CfbReader::Open(truncated).has_value());
  auto bad_dir = good;
  // Root child points back to root.
  bad_dir[512 + 76] = 0;
  bad_dir[512 + 77] = 0;
  bad_dir[512 + 78] = 0;
  bad_dir[512 + 79] = 0;
  auto dir_result = CfbReader::Open(bad_dir);
  ASSERT_FALSE(dir_result.has_value());
  EXPECT_EQ(dir_result.error().code, ErrorCode::kCorruptDirectory);
  auto bad_sector = good;
  // First DIFAT entry refers outside the file.
  for (size_t i = 0; i < 4; ++i)
    bad_sector[76 + i] = 0x7F;
  EXPECT_FALSE(CfbReader::Open(bad_sector).has_value());
}

// A chain that meets FREESECT before the declared size is the same truncation
// as ENDOFCHAIN: the bytes before it are real, so they are kept with a
// warning. (This shape used to be rejected; r48 moved it to the salvage path.)
TEST(CfbReaderTest, SalvagesStreamChainEndingAtFreeSector) {
  CfbBuilder builder;
  builder.AddStream("Data", MakePayload(4096, 0x31));
  auto bytes = builder.Build();
  const uint32_t start = StreamStart(bytes, "Data");
  ASSERT_NE(start, DirEntry::kNoFollow);
  Put32(bytes, FatEntryOffset(bytes, start), 0xFFFFFFFFu);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  auto data = reader->ReadStream("Data");
  ASSERT_TRUE(data) << data.error().message;
  EXPECT_EQ(data->size(), 512u);
  EXPECT_EQ(*data, MakePayload(512, 0x31));
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Data"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "kept 512 of 4096 declared bytes"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
}

// [MS-CFB] §2.6.1 gives the root entry no siblings: its left and right links
// are NOSTREAM. The LibreOffice sw/qa samples ofz-delflyinrange-1, ofz41398-1,
// ofz42330-1 and ofz45140-1 instead carry an in-file slot number there. The
// link cannot reach anything the child tree does not already reach, so it is
// ignored with one warning; the container and its streams stay usable.
TEST(CfbReaderTest, IgnoresRootLeftSiblingLink) {
  CfbBuilder builder;
  builder.AddStream("Alpha", MakePayload(4096, 0x61));
  builder.AddStream("Beta", MakePayload(4096, 0x62));
  auto bytes = builder.Build();
  Put32(bytes, DirSlotOffset(bytes, 0) + 68, 1);
  bool threw = false;
  try {
    auto reader = CfbReader::Open(bytes);
    ASSERT_TRUE(reader) << reader.error().message;
    ASSERT_EQ(reader->warnings().size(), 1u);
    EXPECT_TRUE(HasWarning(*reader, "root"));
    EXPECT_TRUE(HasWarning(*reader, "left sibling"));
    EXPECT_TRUE(HasWarning(*reader, "ignored"))
        << (reader->warnings().empty() ? "" : reader->warnings()[0]);
    for (const auto& [name, fill] :
         {std::pair{"Alpha", 0x61}, std::pair{"Beta", 0x62}}) {
      auto stream = reader->ReadStream(name);
      ASSERT_TRUE(stream) << name << ": " << stream.error().message;
      EXPECT_EQ(*stream, MakePayload(4096, static_cast<uint8_t>(fill))) << name;
    }
  } catch (...) {
    threw = true;
  }

  EXPECT_FALSE(threw);
}

TEST(CfbReaderTest, IgnoresRootRightSiblingLink) {
  CfbBuilder builder;
  builder.AddStream("Alpha", MakePayload(4096, 0x71));
  builder.AddStream("Beta", MakePayload(4096, 0x72));
  auto bytes = builder.Build();
  Put32(bytes, DirSlotOffset(bytes, 0) + 72, 2);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "root"));
  EXPECT_TRUE(HasWarning(*reader, "right sibling"));
  EXPECT_TRUE(HasWarning(*reader, "ignored"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  for (const auto& [name, fill] :
       {std::pair{"Alpha", 0x71}, std::pair{"Beta", 0x72}}) {
    auto stream = reader->ReadStream(name);
    ASSERT_TRUE(stream) << name << ": " << stream.error().message;
    EXPECT_EQ(*stream, MakePayload(4096, static_cast<uint8_t>(fill))) << name;
  }
}

TEST(CfbReaderTest, RejectsExcessiveStorageDepthWithoutThrowing) {
  CfbBuilder builder;
  std::string path;
  for (int i = 0; i < 257; ++i)
    path += "s/";
  builder.AddStream(path + "Data", MakePayload(1, 0x42));
  auto bytes = builder.Build();
  bool threw = false;
  try {
    auto reader = CfbReader::Open(bytes);
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(reader.error().code, ErrorCode::kCorruptDirectory);
  } catch (...) {
    threw = true;
  }

  EXPECT_FALSE(threw);
}

TEST(CfbReaderTest, FiveStorageLevelsKeepCorrectPath) {
  CfbBuilder builder;
  builder.AddStream("a/b/c/d/e/Data", MakePayload(17, 0x51));
  auto bytes = builder.Build();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value());
  std::string stream_path;
  auto visited =
      reader->Visit([&](const DirEntry& entry, std::string_view path, bool) {
        if (entry.type == EntryType::kStream)
          stream_path = path;
        return true;
      });
  ASSERT_TRUE(visited.has_value());
  EXPECT_EQ(stream_path, "Root Entry/a/b/c/d/e/Data");
}

TEST(CfbReaderTest, RejectsInflatedAllFatStreamWithoutThrowing) {
  auto bytes = AllFatV4();
  bool threw = false;
  try {
    auto reader = CfbReader::Open(bytes);
    ASSERT_TRUE(reader.has_value()) << reader.error().message;
    auto stream = reader->ReadStream("Data");
    ASSERT_FALSE(stream.has_value());
    EXPECT_TRUE(stream.error().code == ErrorCode::kCorruptFat ||
                stream.error().code == ErrorCode::kTruncated);
  } catch (...) {
    threw = true;
  }

  EXPECT_FALSE(threw);
}

// Header offset 40 directory sector count: for v3 the field does not take
// part in addressing, so a non-zero value is tolerated with a warning
// (r41 leniency, see CfbLenientHeader.ToleratesV3DirectorySectorCount).
TEST(CfbReaderTest, ToleratesV3HeaderWithDirectorySectorCount) {
  auto bytes = CfbBuilder().AddStream("s", MakePayload(100, 7)).Build();
  Put32(bytes, 40, 3);  // v3 normally forbids a non-zero count
  auto result = CfbReader::Open(bytes);
  ASSERT_TRUE(result) << result.error().message;
  auto stream = result->ReadStream("s");
  ASSERT_TRUE(stream) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(100, 7));
}

// v4: the field must equal the actual directory chain length.
TEST(CfbReaderTest, RejectsV4DirectorySectorCountMismatch) {
  auto bytes = CfbBuilder()
                   .SetSectorSize(4096)
                   .AddStream("s", MakePayload(100, 7))
                   .Build();
  // Real count is 1 (single directory sector); claim 2.
  Put32(bytes, 40, 2);
  auto result = CfbReader::Open(bytes);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::kCorruptDirectory);
}

// ReadChain: one trailing ENDOFCHAIN-terminated sector beyond the declared
// stream size is tolerated (real-world producers round up).
TEST(CfbReaderTest, ToleratesTrailingEndOfChainSector) {
  // Build a normal file, then extend one stream's FAT chain by one sector
  // without changing the declared size: the reader must still return exactly
  // `size` bytes and not report "chain longer than stream".
  auto bytes = CfbBuilder().AddStream("s", MakePayload(512, 9)).Build();
  // Locate the stream's start sector via a successful open first.
  auto ok = CfbReader::Open(bytes);
  ASSERT_TRUE(ok);
  uint32_t start = 0;
  ASSERT_TRUE(
      ok->Visit([&](const DirEntry& e, std::string_view, bool is_storage) {
        if (!is_storage && e.name == "s") {
          start = e.start_sector;
          return false;
        }
        return true;
      }));
  // The stream occupies exactly one sector whose FAT entry is ENDOFCHAIN.
  // Append one physical sector and link it: start -> extra -> ENDOFCHAIN.
  const uint32_t sector_size = 512;
  const uint32_t old_count =
      static_cast<uint32_t>(bytes.size() / sector_size) - 1;
  bytes.resize(bytes.size() + sector_size, 0);
  // New FAT entry for the extra sector: ENDOFCHAIN.
  // FAT sector begins at physical offset sector_size.
  const size_t fat_offset = sector_size + start * 4;
  const uint32_t extra_sector = old_count;  // appended sector index
  Put32(bytes, fat_offset, extra_sector);
  Put32(bytes, sector_size + extra_sector * 4, 0xFFFFFFFEu);  // ENDOFCHAIN
  auto result = CfbReader::Open(bytes);
  ASSERT_TRUE(result) << result.error().message;
  auto data = result->ReadStream("s");
  ASSERT_TRUE(data) << data.error().message;
  EXPECT_EQ(data->size(), 512u);
}

// Some real-world producers pad the container with a trailing fragment that
// is shorter than one sector (wvWare test files end with a single zero byte).
// The fragment carries no chain data, so it must be ignored rather than
// rejected; every stream still reads back exactly as declared.
TEST(CfbReaderTest, ToleratesTrailingPartialSector) {
  CfbBuilder builder;
  auto payload = MakePayload(8192, 0xAB);
  builder.AddStream("WordDocument", payload);
  auto container = builder.Build();
  constexpr size_t kSectorSizes[] = {512, 4096};
  for (uint32_t sector_size : kSectorSizes) {
    auto base = sector_size == 512 ? container
                                   : CfbBuilder()
                                         .SetSectorSize(4096)
                                         .AddStream("WordDocument", payload)
                                         .Build();
    for (size_t extra = 1; extra < sector_size; extra = extra * 2 + 1) {
      auto bytes = base;
      bytes.insert(bytes.end(), extra, 0x00);
      auto reader = CfbReader::Open(bytes);
      ASSERT_TRUE(reader.has_value())
          << "sector=" << sector_size << " extra=" << extra << ": "
          << reader.error().message;
      EXPECT_EQ(reader->sector_size(), sector_size);
      auto stream = reader->ReadStream("WordDocument");
      ASSERT_TRUE(stream.has_value()) << stream.error().message;
      EXPECT_EQ(*stream, payload)
          << "sector=" << sector_size << " extra=" << extra;
    }
  }
}

// A directory FAT chain may name the trailing partial sector. The complete
// 128-byte records physically present there are real directory entries: the
// LibreOffice sw/qa samples ofz18414-1 and ofz57592-1 keep their last streams
// (and MsoDataStore children) only in that fragment. Dropping the fragment
// leaves the tree links pointing at entry slots that no longer exist, which
// used to reject the whole container.
TEST(CfbReaderTest, ReadsDirectoryEntriesFromTrailingPartialSector) {
  constexpr uint32_t kSectorSize = 512;
  CfbBuilder builder;
  for (const char* name : {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"})
    builder.AddStream(name, MakePayload(1, static_cast<uint8_t>(name[0])));
  auto bytes = builder.Build();
  const uint32_t dir_start = Get32(bytes, 48);
  ASSERT_EQ(Get32(bytes, FatEntryOffset(bytes, dir_start)), dir_start + 1)
      << "expected a two-sector directory chain";

  // Move the second directory sector (slots 4..7) into a trailing fragment:
  // keep its first three records and end the file right after them.
  const size_t second = (static_cast<size_t>(dir_start) + 2) * kSectorSize;
  const std::vector<uint8_t> fragment(bytes.begin() + second,
                                      bytes.begin() + second + 3 * 128);
  const uint32_t old_total =
      static_cast<uint32_t>(bytes.size() / kSectorSize) - 1;
  bytes.insert(bytes.end(), fragment.begin(), fragment.end());
  // The chain now ends in the fragment, whose sector index is `total_sectors`.
  Put32(bytes, FatEntryOffset(bytes, dir_start), old_total);

  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "trailing partial sector"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  for (const char* name : {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"}) {
    auto stream = reader->ReadStream(name);
    ASSERT_TRUE(stream) << name << ": " << stream.error().message;
    EXPECT_EQ(stream->size(), 1u) << name;
  }
}

// A directory FAT chain whose second ring leaves the file keeps the first
// sector: the LibreOffice sw/qa samples ofz46457-1, ofz53457-1 and ofz18554-1
// carry all four records (root, WordDocument, Data, 1Table) in it while the
// next link is junk. The sectors read are real directory data, so Open uses
// them and says how much of the chain was used; records that lived in the
// dropped sector are gone rather than fabricated.
TEST(CfbReaderTest, SalvagesDirectoryChainCutShortMidway) {
  CfbBuilder builder;
  builder.AddStream("Alpha", MakePayload(16, 0x41));
  builder.AddStream("Beta", MakePayload(16, 0x42));
  builder.AddStream("Gamma", MakePayload(16, 0x47));
  builder.AddStream("Zeta", MakePayload(16, 0x5A));
  auto bytes = builder.Build();
  const uint32_t dir_start = Get32(bytes, 48);
  ASSERT_EQ(Get32(bytes, FatEntryOffset(bytes, dir_start)), dir_start + 1)
      << "expected a two-sector directory chain";
  // The second ring names a sector far outside the file and the FAT table.
  Put32(bytes, FatEntryOffset(bytes, dir_start), 0x1000);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "cut short after 1 sector"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "fall back to name matching"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  for (const auto& [name, fill] :
       {std::pair{"Alpha", 0x41}, std::pair{"Beta", 0x42},
        std::pair{"Gamma", 0x47}}) {
    auto stream = reader->ReadStream(name);
    ASSERT_TRUE(stream) << name << ": " << stream.error().message;
    EXPECT_EQ(*stream, MakePayload(16, static_cast<uint8_t>(fill))) << name;
  }
  // Zeta's record lay in the sector the chain could not reach.
  EXPECT_EQ(reader->ReadStream("Zeta").error().code,
            ErrorCode::kStreamNotFound);
}

// A cut chain also leaves records that the tree simply never links to, even
// though no link is out of range: the dropped sector held the sibling that
// used to reach them. The salvage must open the name fallback so the prefix
// entries stay readable instead of looking like detached junk.
TEST(CfbReaderTest, SalvagedDirectoryPrefixStaysReachableByName) {
  CfbBuilder builder;
  builder.AddStream("Alpha", MakePayload(16, 0x41));
  builder.AddStream("Beta", MakePayload(16, 0x42));
  builder.AddStream("Gamma", MakePayload(16, 0x47));
  builder.AddStream("Zeta", MakePayload(16, 0x5A));
  auto bytes = builder.Build();
  const uint32_t dir_start = Get32(bytes, 48);
  ASSERT_EQ(Get32(bytes, FatEntryOffset(bytes, dir_start)), dir_start + 1)
      << "expected a two-sector directory chain";
  Put32(bytes, FatEntryOffset(bytes, dir_start), 0x1000);
  // Rewire the tree so slot 3 (Gamma) is never entered and no link dangles:
  // root -> Beta, Beta -> Alpha, Gamma's sibling link is NOSTREAM.
  Put32(bytes, DirSlotOffset(bytes, 0) + 76, 2);
  Put32(bytes, DirSlotOffset(bytes, 3) + 72, 0xFFFFFFFF);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "cut short after 1 sector"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  ASSERT_FALSE(HasWarning(*reader, "points past the entry table"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto gamma = reader->ReadStream("Gamma");
  ASSERT_TRUE(gamma) << gamma.error().message;
  EXPECT_EQ(*gamma, MakePayload(16, 0x47));
  // Zeta's record was in the dropped sector.
  EXPECT_EQ(reader->ReadStream("Zeta").error().code,
            ErrorCode::kStreamNotFound);
}

// The same cut at the head of the chain leaves no directory record at all, so
// there is nothing to anchor a recovery: no directory bytes means no root, no
// entries and no way to guess where the real directory sits. The container
// stays rejected (ofz-redlining-1, ofz-trailingpara, ofz21168-1 have
// first_dir_sector == total_sectors).
TEST(CfbReaderTest, RejectsDirectoryChainStartingOutsideTheFile) {
  auto bytes = CfbBuilder().AddStream("Small", MakePayload(16, 0x31)).Build();
  Put32(bytes, 48, PhysicalSectorEnd(bytes));
  auto reader = CfbReader::Open(bytes);
  ASSERT_FALSE(reader.has_value());
  EXPECT_EQ(reader.error().code, ErrorCode::kCorruptDirectory);
  EXPECT_NE(reader.error().message.find("directory chain starts outside"),
            std::string::npos)
      << reader.error().message;
}

// v4 declares its directory length in the header, so a chain cut short by a
// junk link would otherwise trip the count check that guards a lying header.
// The salvage above is the better diagnosis: the chain is demonstrably
// truncated, and the count can no longer be compared.
TEST(CfbReaderTest, SalvagesDirectoryChainCutShortInV4) {
  CfbBuilder builder;
  builder.SetSectorSize(4096);
  for (int i = 0; i < 32; ++i) {
    builder.AddStream("s" + std::to_string(i),
                      MakePayload(1, static_cast<uint8_t>(i)));
  }
  auto bytes = builder.Build();
  const uint32_t dir_start = Get32(bytes, 48);
  ASSERT_EQ(Get32(bytes, FatEntryOffset(bytes, dir_start)), dir_start + 1)
      << "expected a two-sector directory chain";
  const uint32_t total = static_cast<uint32_t>(bytes.size() / 4096) - 1;
  Put32(bytes, FatEntryOffset(bytes, dir_start), total + 1);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "cut short after 1 sector"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto stream = reader->ReadStream("s0");
  ASSERT_TRUE(stream) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(1, 0));
}

// A container with less than one complete sector cannot hold a directory,
// so it stays rejected even after trailing fragments are tolerated.
TEST(CfbReaderTest, RejectsFileShorterThanOneSector) {
  auto container = CfbBuilder()
                       .SetSectorSize(4096)
                       .AddStream("s", MakePayload(8192, 1))
                       .Build();
  ASSERT_GT(container.size(), 4096u);
  container.resize(3000);  // header intact, but no full 4096-byte sector
  auto result = CfbReader::Open(container);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kTruncated);
  EXPECT_NE(result.error().message.find("partial sector"), std::string::npos)
      << result.error().message;
}

// ReadStream: '/' disambiguation — full path wins over bare entry name.
TEST(CfbReaderTest, ReadStreamPrefersFullPathOverEntryName) {
  // storage "a" containing stream "b", plus a top-level stream "a/b".
  auto bytes = CfbBuilder()
                   .AddStream("a/b", MakePayload(16, 1))
                   .AddStream("x/a~b", MakePayload(16, 2))
                   .Build();
  auto result = CfbReader::Open(bytes);
  ASSERT_TRUE(result);
  // Full path "a/b" resolves to the nested stream (payload 1).
  auto data = result->ReadStream("a/b");
  ASSERT_TRUE(data);
  EXPECT_EQ((*data)[0], 1);
}

TEST(CfbReaderTest, HandlesMutatedHeadersAndDirectoryWithoutCrashing) {
  CfbBuilder b;
  b.AddStream("Small", MakePayload(137, 0x7A));
  auto good = b.Build();
  uint32_t state = 0xC0FFEE;
  for (size_t mutation = 0; mutation < 1000; ++mutation) {
    auto bytes = good;
    state = state * 1664525u + 1013904223u;
    size_t offset = state % bytes.size();
    state = state * 1664525u + 1013904223u;
    bytes[offset] = static_cast<uint8_t>(state >> 24);
    auto reader = CfbReader::Open(bytes);
    if (reader) {
      auto stream = reader->ReadStream("Small");
      (void)stream;
    }
  }
}

// [MS-CFB] §2.6.1 compares directory names case-insensitively; POI bug 47950
// covers producers that store the Word stream names in lower or upper case.
TEST(CfbReaderTest, FindsStreamNameCaseInsensitively) {
  auto lower = CfbBuilder()
                   .AddStream("worddocument", MakePayload(4096, 0x11))
                   .AddStream("1table", MakePayload(5000, 0x22))
                   .Build();
  auto lower_reader = CfbReader::Open(lower);
  ASSERT_TRUE(lower_reader) << lower_reader.error().message;
  auto word = lower_reader->ReadStream("WordDocument");
  ASSERT_TRUE(word) << word.error().message;
  EXPECT_EQ(*word, MakePayload(4096, 0x11));
  auto table = lower_reader->ReadStream("1Table");
  ASSERT_TRUE(table) << table.error().message;
  EXPECT_EQ(*table, MakePayload(5000, 0x22));

  auto upper =
      CfbBuilder().AddStream("WORDDOCUMENT", MakePayload(4096, 0x33)).Build();
  auto upper_reader = CfbReader::Open(upper);
  ASSERT_TRUE(upper_reader) << upper_reader.error().message;
  word = upper_reader->ReadStream("WordDocument");
  ASSERT_TRUE(word) << word.error().message;
  EXPECT_EQ(*word, MakePayload(4096, 0x33));

  // Nested paths fold the same way for both the relative and full-path ranks.
  auto nested = CfbBuilder()
                    .AddStream("objectpool/item/data", MakePayload(64, 0x44))
                    .Build();
  auto nested_reader = CfbReader::Open(nested);
  ASSERT_TRUE(nested_reader) << nested_reader.error().message;
  auto data = nested_reader->ReadStream("ObjectPool/Item/Data");
  ASSERT_TRUE(data) << data.error().message;
  EXPECT_EQ(*data, MakePayload(64, 0x44));
  data = nested_reader->ReadStream("ROOT ENTRY/OBJECTPOOL/ITEM/DATA");
  ASSERT_TRUE(data) << data.error().message;
  EXPECT_EQ(*data, MakePayload(64, 0x44));
}

// Case folding must not change the rank rule: both names tie at the same rank,
// so the first entry in directory (Visit) order wins, even when the request
// matches the other variant exactly.
TEST(CfbReaderTest, ReadStreamCaseVariantsKeepDirectoryOrder) {
  auto bytes = CfbBuilder()
                   .AddStream("Data", MakePayload(16, 0x55))
                   .AddStream("data", MakePayload(16, 0x66))
                   .Build();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  // CfbBuilder orders "Data" before "data"; Visit is in-order, so "Data" is
  // the first match for either spelling.
  for (const char* request : {"Data", "data"}) {
    auto stream = reader->ReadStream(request);
    ASSERT_TRUE(stream) << request << ": " << stream.error().message;
    EXPECT_EQ((*stream)[0], 0x55) << request;
  }
}

// A non-root entry whose name fails validation makes only that entry
// unreachable: the container opens, other streams stay readable, and the
// skip is reported with the directory slot number.
TEST(CfbReaderTest, SkipsStreamWithInvalidName) {
  auto bytes = CfbBuilder()
                   .AddStream("Alpha", MakePayload(4096, 0x71))
                   .AddStream("Beta", MakePayload(4096, 0x72))
                   .Build();
  // Entry order follows insertion: slot 1 = Alpha, slot 2 = Beta. An
  // impossible name length (65 bytes) triggers the name check.
  Put16(bytes, DirSlotOffset(bytes, 2) + 64, 65);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 1u);
  EXPECT_TRUE(HasWarning(*reader, "slot 2")) << reader->warnings()[0];
  auto alpha = reader->ReadStream("Alpha");
  ASSERT_TRUE(alpha) << alpha.error().message;
  EXPECT_EQ(*alpha, MakePayload(4096, 0x71));
  auto beta = reader->ReadStream("Beta");
  ASSERT_FALSE(beta);
  EXPECT_EQ(beta.error().code, ErrorCode::kStreamNotFound);
}

// A sibling link that names an entry slot past the table is a dangling link:
// no entry can be reached through it, so the walk ends there like NOSTREAM.
// LibreOffice sw/qa fuzz regressions (ofz19065, ofz38011-1) and POI
// clusterfuzz samples carry such links after their directory chain is cut
// short. Rejecting the whole container is too strict; the link is dropped and
// reported, and the other entries stay reachable.
TEST(CfbReaderTest, IgnoresDirectoryLinkPastEntryTable) {
  auto bytes = CfbBuilder()
                   .AddStream("Alpha", MakePayload(16, 0x21))
                   .AddStream("Beta", MakePayload(16, 0x22))
                   .Build();
  // One directory sector holds four slots (root, two streams, one empty).
  const uint32_t entry_count = 4;
  // Slot 1 (Alpha) keeps its siblings; point its right link past the table.
  Put32(bytes, DirSlotOffset(bytes, 1) + 72, entry_count);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "points past the entry table"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto alpha = reader->ReadStream("Alpha");
  ASSERT_TRUE(alpha) << alpha.error().message;
  EXPECT_EQ(*alpha, MakePayload(16, 0x21));
  auto beta = reader->ReadStream("Beta");
  ASSERT_TRUE(beta) << beta.error().message;
  EXPECT_EQ(*beta, MakePayload(16, 0x22));
}

// When a dangling link is the only route to a sibling, that sibling becomes
// unreachable through the tree even though its directory record is intact
// (LibreOffice sw/qa ofz18534-1, ofz34749-1 and ofz515655013 keep
// WordDocument/1Table/Data that way). The tree walk cannot prove the stream is
// absent, so a name lookup falls back to a linear scan of the records and the
// salvage is reported once at Open time.
TEST(CfbReaderTest, SalvagesOrphanedEntryByName) {
  auto bytes = CfbBuilder()
                   .AddStream("Alpha", MakePayload(16, 0x31))
                   .AddStream("Beta", MakePayload(16, 0x32))
                   .Build();
  const uint32_t entry_count = 4;  // one past the single directory sector
  // Slot 2 (Beta) is the root child; its left link used to reach Alpha.
  Put32(bytes, DirSlotOffset(bytes, 2) + 68, entry_count);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "matched by name only"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto alpha = reader->ReadStream("Alpha");
  ASSERT_TRUE(alpha) << alpha.error().message;
  EXPECT_EQ(*alpha, MakePayload(16, 0x31));
  auto beta = reader->ReadStream("Beta");
  ASSERT_TRUE(beta) << beta.error().message;
  EXPECT_EQ(*beta, MakePayload(16, 0x32));
  // Case folding still applies to the salvage path.
  auto folded = reader->ReadStream("ALPHA");
  ASSERT_TRUE(folded) << folded.error().message;
  EXPECT_EQ(*folded, MakePayload(16, 0x31));
}

// Orphan salvage is gated on a proven-dangling link: a healthy container that
// simply lacks the name still reports "stream not found".
TEST(CfbReaderTest, MissingStreamWithoutDanglingLinkStaysNotFound) {
  auto bytes = CfbBuilder().AddStream("Alpha", MakePayload(16, 0x41)).Build();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(reader->warnings().empty());
  auto missing = reader->ReadStream("Gamma");
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, ErrorCode::kStreamNotFound);
}

// An entry the tree never reaches is not evidence that the tree was cut: the
// reachable tree here is complete and Gamma is a detached record. Its bad
// links must not open the name-only salvage, or a complete tree could hand out
// streams it excludes; the damage is still reported once as a summary.
TEST(CfbReaderTest, UnreachableDanglingLinkDoesNotEnableSalvage) {
  auto bytes = CfbBuilder()
                   .AddStream("Alpha", MakePayload(16, 0x51))
                   .AddStream("Beta", MakePayload(16, 0x52))
                   .AddStream("Gamma", MakePayload(16, 0x53))
                   .Build();
  const uint32_t entry_count = 4;  // one past the single directory sector
  // Builder tree: root -> Beta, Beta.left -> Alpha, Beta.right -> Gamma.
  // Cut Beta's right link: the reachable tree (root, Beta, Alpha) stays
  // complete while Gamma becomes an orphan record.
  Put32(bytes, DirSlotOffset(bytes, 2) + 72, DirEntry::kNoFollow);
  // Give that unreachable record a link past the entry table.
  Put32(bytes, DirSlotOffset(bytes, 3) + 68, entry_count);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_FALSE(HasWarning(*reader, "matched by name only"));
  auto gamma = reader->ReadStream("Gamma");
  ASSERT_FALSE(gamma);
  EXPECT_EQ(gamma.error().code, ErrorCode::kStreamNotFound);
  auto missing = reader->ReadStream("Delta");
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, ErrorCode::kStreamNotFound);
  // The unreachable damage is still reported, aggregated into one line.
  EXPECT_TRUE(HasWarning(*reader, "unreachable directory record"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  ASSERT_EQ(reader->warnings().size(), 1u);
  // The reachable half is unaffected.
  auto beta = reader->ReadStream("Beta");
  ASSERT_TRUE(beta) << beta.error().message;
  EXPECT_EQ(*beta, MakePayload(16, 0x52));
}

// Warning granularity: every damaged entry the walk reaches gets one line
// naming its slot and entry; several bad links from the same entry collapse
// into that single line. One summary line states the salvage.
TEST(CfbReaderTest, WarnsOncePerDanglingEntry) {
  auto bytes = CfbBuilder()
                   .AddStream("Alpha", MakePayload(16, 0x61))
                   .AddStream("Beta", MakePayload(16, 0x62))
                   .AddStream("Gamma", MakePayload(16, 0x63))
                   .AddStream("Delta", MakePayload(16, 0x64))
                   .Build();
  const uint32_t entry_count = 8;  // two directory sectors
  // Builder tree: root -> Delta, Delta.left -> Beta, Beta.left -> Alpha,
  // Delta.right -> Gamma; slot 1 is Alpha, slot 3 is Gamma. Alpha carries two
  // dangling links, Gamma one.
  Put32(bytes, DirSlotOffset(bytes, 1) + 68, entry_count);
  Put32(bytes, DirSlotOffset(bytes, 1) + 72, entry_count + 1);
  Put32(bytes, DirSlotOffset(bytes, 3) + 68, entry_count + 2);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  ASSERT_EQ(reader->warnings().size(), 3u);
  EXPECT_NE(reader->warnings()[0].find("slot 1 ('Alpha')"), std::string::npos)
      << reader->warnings()[0];
  EXPECT_NE(reader->warnings()[0].find("8, 9"), std::string::npos)
      << reader->warnings()[0];
  EXPECT_NE(reader->warnings()[1].find("slot 3 ('Gamma')"), std::string::npos)
      << reader->warnings()[1];
  EXPECT_NE(reader->warnings()[1].find("slot 10"), std::string::npos)
      << reader->warnings()[1];
  EXPECT_TRUE(HasWarning(*reader, "matched by name only"));
  // Each damaged entry is still readable through the tree.
  for (const auto& [name, fill] :
       {std::pair{"Alpha", 0x61}, std::pair{"Gamma", 0x63}}) {
    auto stream = reader->ReadStream(name);
    ASSERT_TRUE(stream) << name << ": " << stream.error().message;
    EXPECT_EQ(*stream, MakePayload(16, static_cast<uint8_t>(fill))) << name;
  }
}

// Mixed damage: the walk lands on a dangling link while another dangling link
// sits on a record the walk never reaches. The reachable damage opens the
// salvage, so the summary line must not claim the tree is complete regardless;
// it only reports the unreachable links and says they do not gate the salvage.
TEST(CfbReaderTest, MixedDanglingLinksKeepSummaryNeutral) {
  auto bytes = CfbBuilder()
                   .AddStream("Alpha", MakePayload(16, 0x71))
                   .AddStream("Beta", MakePayload(16, 0x72))
                   .AddStream("Gamma", MakePayload(16, 0x73))
                   .AddStream("Delta", MakePayload(16, 0x74))
                   .Build();
  const uint32_t entry_count = 8;  // two directory sectors
  // Builder tree: root -> Delta, Delta.left -> Beta, Beta.left -> Alpha,
  // Delta.right -> Gamma. Cutting Delta's right link orphans Gamma; Alpha
  // (reachable) and Gamma (unreachable) each carry a link past the table.
  Put32(bytes, DirSlotOffset(bytes, 4) + 72, DirEntry::kNoFollow);
  Put32(bytes, DirSlotOffset(bytes, 1) + 68, entry_count);
  Put32(bytes, DirSlotOffset(bytes, 3) + 68, entry_count + 1);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  // Reachable damage: one line naming the entry, then the salvage line.
  EXPECT_TRUE(HasWarning(*reader, "slot 1 ('Alpha')"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "matched by name only"));
  // Unreachable damage still gets its own summary.
  EXPECT_TRUE(HasWarning(*reader, "unreachable directory record"));
  ASSERT_EQ(reader->warnings().size(), 3u);
  // The summary must hold whether or not the reachable tree is complete.
  for (const std::string& warning : reader->warnings()) {
    EXPECT_EQ(warning.find("the tree is complete"), std::string::npos)
        << warning;
  }
  // The salvage reaches the orphan record by entry name.
  auto gamma = reader->ReadStream("Gamma");
  ASSERT_TRUE(gamma) << gamma.error().message;
  EXPECT_EQ(*gamma, MakePayload(16, 0x73));
  // The reachable half still reads through the tree.
  for (const auto& [name, fill] :
       {std::pair{"Alpha", 0x71}, std::pair{"Beta", 0x72},
        std::pair{"Delta", 0x74}}) {
    auto stream = reader->ReadStream(name);
    ASSERT_TRUE(stream) << name << ": " << stream.error().message;
    EXPECT_EQ(*stream, MakePayload(16, static_cast<uint8_t>(fill))) << name;
  }
}

// The root entry is a regular chain, so a root whose start lies past the
// physical end cannot build any mini stream. Its readable prefix (here none)
// still opens the container: the business streams live in the regular FAT and
// stay intact, which is the shape of the LibreOffice sw/qa truncation samples
// (ofz18414-1, ofz18534-1, ofz19065, ofz34749-1, ofz38011-1).
TEST(CfbReaderTest, SalvagesRootChainThatLeavesTheFile) {
  CfbBuilder builder;
  builder.AddStream("Big", MakePayload(8192, 0x44));
  builder.AddStream("Small", MakePayload(100, 0x55));
  auto bytes = builder.Build();
  // Root data holds the 100-byte mini stream rounded to two 64-byte units.
  Put32(bytes, DirSlotOffset(bytes, 0) + 116, PhysicalSectorEnd(bytes) + 5);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Root Entry"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "kept 0 of 128 declared bytes"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto big = reader->ReadStream("Big");
  ASSERT_TRUE(big) << big.error().message;
  EXPECT_EQ(*big, MakePayload(8192, 0x44));
  // The mini stream has no backing bytes, so a dependent read degrades to an
  // empty prefix instead of rejecting the container.
  auto small = reader->ReadStream("Small");
  ASSERT_TRUE(small) << small.error().message;
  EXPECT_TRUE(small->empty());
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Small"))
      << (reader->warnings().empty() ? "" : reader->warnings().back());
}

// A root chain that leaves the file after its first sector keeps that sector:
// mini streams inside the readable prefix stay complete and a stream that
// crosses the cut returns its prefix.
TEST(CfbReaderTest, SalvagesReadableRootPrefixForMiniStreams) {
  CfbBuilder builder;
  builder.AddStream("Mini0", MakePayload(300, 0x11));
  builder.AddStream("Mini1", MakePayload(300, 0x22));
  auto bytes = builder.Build();
  auto healthy = CfbReader::Open(bytes);
  ASSERT_TRUE(healthy) << healthy.error().message;
  uint32_t root_start = DirEntry::kNoFollow;
  ASSERT_TRUE(
      healthy->Visit([&](const DirEntry& entry, std::string_view, bool) {
        if (entry.type == EntryType::kRoot) {
          root_start = entry.start_sector;
          return false;
        }
        return true;
      }));
  // 640 bytes of root data occupy two sectors; cut the second one off.
  Put32(bytes, FatEntryOffset(bytes, root_start), PhysicalSectorEnd(bytes) + 5);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Root Entry"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "kept 512 of 640 declared bytes"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto first = reader->ReadStream("Mini0");
  ASSERT_TRUE(first) << first.error().message;
  EXPECT_EQ(*first, MakePayload(300, 0x11));
  // Mini1 starts at mini offset 320 and needs five 64-byte units; three fit.
  auto second = reader->ReadStream("Mini1");
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(second->size(), 192u);
  EXPECT_EQ(*second, MakePayload(192, 0x22));
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Mini1"))
      << (reader->warnings().empty() ? "" : reader->warnings().back());
  EXPECT_TRUE(HasWarning(*reader, "kept 192 of 300 declared bytes"))
      << (reader->warnings().empty() ? "" : reader->warnings().back());
}

// A chain shorter than the declared size keeps the bytes it did read. This is
// the ordinary truncation shape: the FAT terminates before the stream ends.
TEST(CfbReaderTest, SalvagesStreamChainThatEndsEarly) {
  CfbBuilder builder;
  builder.AddStream("Big", MakePayload(8192, 0x66));
  auto bytes = builder.Build();
  const uint32_t start = StreamStart(bytes, "Big");
  ASSERT_NE(start, DirEntry::kNoFollow);
  // Builder chains are contiguous; end the chain after four of 16 sectors.
  Put32(bytes, FatEntryOffset(bytes, start + 3), 0xFFFFFFFEu);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  auto big = reader->ReadStream("Big");
  ASSERT_TRUE(big) << big.error().message;
  EXPECT_EQ(big->size(), 2048u);
  EXPECT_EQ(*big, MakePayload(2048, 0x66));
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Big"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "kept 2048 of 8192 declared bytes"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
}

// The mini FAT is read through the regular chain path. A chain that ends
// before its declared sector count keeps the sector it read (r48), so the
// first 128 mini FAT entries stay usable.
TEST(CfbReaderTest, SalvagesTruncatedMiniFatChain) {
  CfbBuilder builder;
  builder.AddStream("Small", MakePayload(100, 0x33));
  auto bytes = builder.Build();
  // The builder stores one mini FAT sector; declaring two keeps the start in
  // range while the chain still ends early.
  Put32(bytes, 64, 2);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "truncated stream mini FAT"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "kept 512 of 1024 declared bytes"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto small = reader->ReadStream("Small");
  ASSERT_TRUE(small) << small.error().message;
  EXPECT_EQ(*small, MakePayload(100, 0x33));
}

// r49: a mini FAT start outside the file is caught in the header and the mini
// FAT is ignored there, so the dependent stream reports the missing table
// instead of returning fabricated bytes.
TEST(CfbReaderTest, ImpossibleMiniFatStartLeavesNoTable) {
  CfbBuilder builder;
  builder.AddStream("Small", MakePayload(100, 0x33));
  auto bytes = builder.Build();
  Put32(bytes, 60, PhysicalSectorEnd(bytes) + 5);  // header mini FAT start
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(HasWarning(*reader, "mini FAT start sector is impossible"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  auto small = reader->ReadStream("Small");
  ASSERT_FALSE(small);
  EXPECT_EQ(small.error().code, ErrorCode::kCorruptFat);
  EXPECT_NE(small.error().message.find("chain exceeds allocation table"),
            std::string::npos)
      << small.error().message;
}

// A container that declares no mini FAT has no table; a stream that depends on
// it fails at read time rather than being rejected at Open.
TEST(CfbReaderTest, StreamNeedingMissingMiniFatFailsOnRead) {
  CfbBuilder builder;
  builder.AddStream("Small", MakePayload(100, 0x33));
  auto bytes = builder.Build();
  Put32(bytes, 64, 0);  // no mini FAT sectors
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(reader->warnings().empty());
  auto small = reader->ReadStream("Small");
  ASSERT_FALSE(small);
  EXPECT_EQ(small.error().code, ErrorCode::kCorruptFat);
  EXPECT_NE(small.error().message.find("chain exceeds allocation table"),
            std::string::npos)
      << small.error().message;
}

// Salvage must not weaken the cycle guard: a link that revisits a sector is
// still fatal, even though the prefix before it is readable.
TEST(CfbReaderTest, RejectsCyclicStreamChainAfterSalvageWork) {
  CfbBuilder builder;
  builder.AddStream("Big", MakePayload(8192, 0x77));
  auto bytes = builder.Build();
  const uint32_t start = StreamStart(bytes, "Big");
  ASSERT_NE(start, DirEntry::kNoFollow);
  Put32(bytes, FatEntryOffset(bytes, start + 1), start);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  auto big = reader->ReadStream("Big");
  ASSERT_FALSE(big);
  EXPECT_EQ(big.error().code, ErrorCode::kCorruptFat);
  EXPECT_NE(big.error().message.find("cyclic"), std::string::npos)
      << big.error().message;
}

// A link past the allocation table cannot be followed. The sectors read before
// it are real data, so keep the prefix and warn, exactly like an early
// ENDOFCHAIN: LibreOffice sw/qa ofz18414-1 ends the WordDocument chain with
// FAT link 580 while the table only holds 128 entries. A cycle stays fatal.
TEST(CfbReaderTest, SalvagesStreamChainIndexPastAllocationTable) {
  CfbBuilder builder;
  builder.AddStream("Big", MakePayload(8192, 0x88));
  auto bytes = builder.Build();
  const uint32_t start = StreamStart(bytes, "Big");
  ASSERT_NE(start, DirEntry::kNoFollow);
  Put32(bytes, FatEntryOffset(bytes, start), 0x1000);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  auto big = reader->ReadStream("Big");
  ASSERT_TRUE(big) << big.error().message;
  EXPECT_EQ(*big, MakePayload(512, 0x88));
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Big"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "kept 512 of 8192 declared bytes"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
}

// A link that skips one sector is repaired when the skipped sector points at
// the same successor: the sector was an orphaned chain member, not a jump.
// ofz19065's 1Table has 47 -> 49 with fat[48] == 49; following the links
// verbatim shifts every table byte after sector 47. The payload differs per
// sector, so only the restored order reproduces the stream byte for byte.
TEST(CfbReaderTest, RestoresSkippedSectorThatLinksToSameSuccessor) {
  CfbBuilder builder;
  const auto payload = SectorPattern(4096);
  builder.AddStream("Big", payload);
  auto bytes = builder.Build();
  const uint32_t start = StreamStart(bytes, "Big");
  ASSERT_NE(start, DirEntry::kNoFollow);
  Put32(bytes, FatEntryOffset(bytes, start), start + 2);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  auto big = reader->ReadStream("Big");
  ASSERT_TRUE(big) << big.error().message;
  EXPECT_EQ(*big, payload);
  EXPECT_TRUE(HasWarning(*reader, "sector restored"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
}

// The skipped sector is only an orphan when the chain without it is exactly one
// sector short of the declared size. Here the chain already carries enough
// sectors (a trailing one beyond the declared length), so inserting the skipped
// neighbour would shift every byte after it. Keep the link order untouched.
TEST(CfbReaderTest, DoesNotRestoreSkippedSectorWhenChainIsAlreadyComplete) {
  CfbBuilder builder;
  const auto payload = SectorPattern(4608);
  builder.AddStream("Big", payload);
  auto bytes = builder.Build();
  const uint32_t start = StreamStart(bytes, "Big");
  ASSERT_NE(start, DirEntry::kNoFollow);
  // Eight declared sectors, nine allocated: start -> start+2 still reaches
  // sectors 2..8 and covers the declared size without sector start+1.
  SetStreamSize(bytes, 1, 8 * 512);
  Put32(bytes, FatEntryOffset(bytes, start), start + 2);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  auto big = reader->ReadStream("Big");
  ASSERT_TRUE(big) << big.error().message;
  EXPECT_EQ(*big, SelectSectors(payload, {0, 2, 3, 4, 5, 6, 7, 8}));
  EXPECT_FALSE(HasWarning(*reader, "sector restored"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
}

// A skipped sector that is another stream's chain head is not an orphan of
// this stream: inserting it would splice two streams together. The declared
// size is one sector short without it, so only the ownership check blocks the
// restoration; the read then degrades to the readable prefix.
TEST(CfbReaderTest, DoesNotRestoreSkippedSectorThatStartsAnotherStream) {
  CfbBuilder builder;
  const auto big_payload = SectorPattern(4096);
  const auto other_payload = SectorPattern(4096);
  builder.AddStream("Big", big_payload);
  builder.AddStream("Other", other_payload);
  auto bytes = builder.Build();
  const uint32_t start = StreamStart(bytes, "Big");
  ASSERT_NE(start, DirEntry::kNoFollow);
  const uint32_t other_start = StreamStart(bytes, "Other");
  ASSERT_EQ(other_start, start + 8);
  // Big declares ten sectors but its allocated chain stops at start+7, which
  // now links past Other's first sector. fat[other_start] links to the same
  // successor, so the skipped-sector pattern fires on Other's head.
  SetStreamSize(bytes, 1, 10 * 512);
  Put32(bytes, FatEntryOffset(bytes, start + 7), other_start + 1);
  Put32(bytes, FatEntryOffset(bytes, other_start + 1), 0xFFFFFFFE);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  auto big = reader->ReadStream("Big");
  ASSERT_TRUE(big) << big.error().message;
  std::vector<uint8_t> expected = big_payload;
  expected.insert(expected.end(),
                  other_payload.begin() + static_cast<ptrdiff_t>(512),
                  other_payload.begin() + static_cast<ptrdiff_t>(1024));
  EXPECT_EQ(*big, expected);
  EXPECT_FALSE(HasWarning(*reader, "sector restored"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
  EXPECT_TRUE(HasWarning(*reader, "truncated stream Big"))
      << (reader->warnings().empty() ? "" : reader->warnings()[0]);
}

// Regression guard: a healthy container that mixes regular and mini streams
// reads everything back and stays warning-free.
TEST(CfbReaderTest, HealthyContainerEmitsNoTruncationWarning) {
  CfbBuilder builder;
  builder.AddStream("Big", MakePayload(8192, 0x99));
  builder.AddStream("Small", MakePayload(100, 0xAA));
  auto bytes = builder.Build();
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(reader->warnings().empty());
  auto big = reader->ReadStream("Big");
  ASSERT_TRUE(big) << big.error().message;
  EXPECT_EQ(*big, MakePayload(8192, 0x99));
  auto small = reader->ReadStream("Small");
  ASSERT_TRUE(small) << small.error().message;
  EXPECT_EQ(*small, MakePayload(100, 0xAA));
  EXPECT_TRUE(reader->warnings().empty());
}

// The root entry carries no stream identity; a missing NUL terminator inside
// its declared length is tolerated silently (POI test-fields.doc stores
// "Y:\Desktop\~WRD0000.tmp" with len=22, no terminator). The declared bytes
// are decoded as the name.
TEST(CfbReaderTest, ToleratesRootNameWithoutNulTerminator) {
  auto bytes = CfbBuilder().AddStream("Data", MakePayload(16, 0x77)).Build();
  // Root "Root Entry" is 10 characters plus the terminator; overwrite the
  // terminator with a normal character.
  Put16(bytes, DirSlotOffset(bytes, 0) + 20, 'X');
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader) << reader.error().message;
  EXPECT_TRUE(reader->warnings().empty());
  auto stream = reader->ReadStream("Data");
  ASSERT_TRUE(stream) << stream.error().message;
  EXPECT_EQ(*stream, MakePayload(16, 0x77));
}

// Unlike a missing terminator, a structurally impossible root name keeps the
// whole container rejected: the root cannot be addressed or given a path.
TEST(CfbReaderTest, RejectsRootWithInvalidNameLength) {
  auto bytes = CfbBuilder().AddStream("Data", MakePayload(16, 0x78)).Build();
  Put16(bytes, DirSlotOffset(bytes, 0) + 64, 3);  // odd length
  auto reader = CfbReader::Open(bytes);
  ASSERT_FALSE(reader);
  EXPECT_EQ(reader.error().code, ErrorCode::kCorruptDirectory);
  EXPECT_NE(reader.error().message.find("invalid directory name"),
            std::string::npos)
      << reader.error().message;
}

TEST(CfbReaderTest, ReadsLibreOfficePlainTextCorpus) {
  std::ifstream file(
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/corpus/corpus/plain-text.doc",
      std::ios::binary);
  ASSERT_TRUE(file.good());
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  std::vector<std::string> names;
  uint64_t word_size = 0;
  uint64_t table_size = 0;
  auto visited =
      reader->Visit([&](const DirEntry& entry, std::string_view, bool) {
        if (entry.type == EntryType::kStream) {
          names.push_back(entry.name);
          if (entry.name == "WordDocument")
            word_size = entry.size;
          if (entry.name == "1Table")
            table_size = entry.size;
        }
        return true;
      });
  ASSERT_TRUE(visited.has_value());
  for (const char* name : {"WordDocument", "1Table"}) {
    EXPECT_NE(std::find(names.begin(), names.end(), name), names.end());
    auto stream = reader->ReadStream(name);
    ASSERT_TRUE(stream.has_value()) << name << ": " << stream.error().message;
    EXPECT_FALSE(stream->empty());
    EXPECT_EQ(stream->size(), std::string_view(name) == "WordDocument"
                                  ? word_size
                                  : table_size);
  }

  EXPECT_EQ(std::find(names.begin(), names.end(), "Data"), names.end());
  EXPECT_EQ(reader->ReadStream("Data").error().code,
            ErrorCode::kStreamNotFound);
}

TEST(CfbReaderTest, ReadsEveryStreamInDocCorpus) {
  for (const char* filename :
       {"plain-text.doc", "alignment-indents.doc", "headings-list.doc",
        "simple-table.doc", "bold-italic-underline.doc"}) {
    SCOPED_TRACE(filename);
    std::ifstream file(
        std::string(DOCCRAFT_SOURCE_DIR) + "/tests/corpus/corpus/" + filename,
        std::ios::binary);
    ASSERT_TRUE(file.good());
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
    auto reader = CfbReader::Open(bytes);
    ASSERT_TRUE(reader.has_value()) << reader.error().message;
    size_t stream_count = 0;
    auto visited =
        reader->Visit([&](const DirEntry& entry, std::string_view path, bool) {
          if (entry.type != EntryType::kStream)
            return true;
          ++stream_count;
          auto stream = reader->ReadStream(path);
          EXPECT_TRUE(stream.has_value())
              << path << ": " << (stream ? "" : stream.error().message);
          if (stream) {
            EXPECT_EQ(stream->size(), entry.size) << path;
          }
          return true;
        });
    ASSERT_TRUE(visited.has_value()) << visited.error().message;
    EXPECT_GT(stream_count, 0u);
  }
}

TEST(CfbReaderTest, BuilderRepackagesLibreOfficeStreams) {
  std::ifstream file(
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/corpus/corpus/plain-text.doc",
      std::ios::binary);
  ASSERT_TRUE(file.good());
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
  auto original = CfbReader::Open(bytes);
  ASSERT_TRUE(original.has_value());
  CfbBuilder builder;
  std::vector<std::string> names;
  auto visited =
      original->Visit([&](const DirEntry& entry, std::string_view, bool) {
        if (entry.type != EntryType::kStream)
          return true;
        auto stream = original->ReadStream(entry.name);
        if (!stream)
          return false;
        names.push_back(entry.name);
        builder.AddStream(entry.name, std::move(*stream));
        return true;
      });
  ASSERT_TRUE(visited.has_value());
  auto rebuilt_bytes = builder.Build();
  auto rebuilt = CfbReader::Open(rebuilt_bytes);
  ASSERT_TRUE(rebuilt.has_value()) << rebuilt.error().message;
  for (const auto& name : names) {
    auto lhs = rebuilt->ReadStream(name);
    auto rhs = original->ReadStream(name);
    ASSERT_TRUE(lhs.has_value());
    ASSERT_TRUE(rhs.has_value());
    EXPECT_EQ(*lhs, *rhs);
  }
}

}  // namespace
}  // namespace doccraft::cfb
