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
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "tests/cfb_test_util.h"

namespace doccraft::cfb {
namespace {

using test::CfbBuilder;
using test::MakePayload;

void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
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

TEST(CfbReaderTest, RejectsCorruptLinksAndTruncation) {
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
  auto bad_chain = good;
  // First stream FAT link is invalid.
  const uint32_t fat_sector =
      static_cast<uint32_t>(good[76]) | (static_cast<uint32_t>(good[77]) << 8);
  const size_t fat_offset = (static_cast<size_t>(fat_sector) + 1) * 512;
  // Directory is sector zero, stream starts at one.
  for (size_t i = 0; i < 4; ++i)
    bad_chain[fat_offset + 4 + i] = 0xFF;
  auto opened = CfbReader::Open(bad_chain);
  ASSERT_TRUE(opened.has_value());
  EXPECT_FALSE(opened->ReadStream("Data").has_value());
}

TEST(CfbReaderTest, RejectsRootSiblingWithoutThrowing) {
  CfbBuilder builder;
  builder.AddStream("Small", MakePayload(1, 0x42));
  auto bytes = builder.Build();
  // Two four-byte patches: root.left points to the stream; root.child is empty.
  Put32(bytes, 512 + 68, 1);
  Put32(bytes, 512 + 76, 0xFFFFFFFF);
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
