// Copyright 2026 The doccraft Authors. All Rights Reserved.
// TDD contract for the CFB layer (M1). These tests are the specification:
// they intentionally fail against the M1 stub until Codex implements the
// reader. Fixture-derived tests document the [MS-CFB] behaviors we commit to.

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cfb/cfb_reader.h"
#include "cfb_test_util.h"
#include <gtest/gtest.h>

namespace doccraft::cfb {
namespace {

using test::CfbBuilder;
using test::MakePayload;

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
  auto visited = reader->Visit([&](const DirEntry&, std::string_view path, bool) {
    paths.emplace_back(path);
    return true;
  });
  ASSERT_TRUE(visited.has_value());
  EXPECT_EQ(paths, (std::vector<std::string>{"Root Entry", "Root Entry/ObjectPool",
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
  for (size_t i = 0; i < 4; ++i) bad_sector[76 + i] = 0x7F;
  EXPECT_FALSE(CfbReader::Open(bad_sector).has_value());
  auto bad_chain = good;
  // First stream FAT link is invalid.
  const uint32_t fat_sector = static_cast<uint32_t>(good[76]) |
                              (static_cast<uint32_t>(good[77]) << 8);
  const size_t fat_offset = (static_cast<size_t>(fat_sector) + 1) * 512;
  // Directory is sector zero, stream starts at one.
  for (size_t i = 0; i < 4; ++i) bad_chain[fat_offset + 4 + i] = 0xFF;
  auto opened = CfbReader::Open(bad_chain);
  ASSERT_TRUE(opened.has_value());
  EXPECT_FALSE(opened->ReadStream("Data").has_value());
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
  std::ifstream file(std::string(DOCCRAFT_SOURCE_DIR) +
                     "/tests/corpus/corpus/plain-text.doc", std::ios::binary);
  ASSERT_TRUE(file.good());
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
  std::vector<std::string> names;
  uint64_t word_size = 0;
  uint64_t table_size = 0;
  auto visited = reader->Visit([&](const DirEntry& entry, std::string_view, bool) {
    if (entry.type == EntryType::kStream) {
      names.push_back(entry.name);
      if (entry.name == "WordDocument") word_size = entry.size;
      if (entry.name == "1Table") table_size = entry.size;
    }
    return true;
  });
  ASSERT_TRUE(visited.has_value());
  for (const char* name : {"WordDocument", "1Table"}) {
    EXPECT_NE(std::find(names.begin(), names.end(), name), names.end());
    auto stream = reader->ReadStream(name);
    ASSERT_TRUE(stream.has_value()) << name << ": " << stream.error().message;
    EXPECT_FALSE(stream->empty());
    EXPECT_EQ(stream->size(), std::string_view(name) == "WordDocument" ? word_size : table_size);
  }
  EXPECT_EQ(std::find(names.begin(), names.end(), "Data"), names.end());
  EXPECT_EQ(reader->ReadStream("Data").error().code, ErrorCode::kStreamNotFound);
}

TEST(CfbReaderTest, BuilderRepackagesLibreOfficeStreams) {
  std::ifstream file(std::string(DOCCRAFT_SOURCE_DIR) +
                     "/tests/corpus/corpus/plain-text.doc", std::ios::binary);
  ASSERT_TRUE(file.good());
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
  auto original = CfbReader::Open(bytes);
  ASSERT_TRUE(original.has_value());
  CfbBuilder builder;
  std::vector<std::string> names;
  auto visited = original->Visit([&](const DirEntry& entry, std::string_view, bool) {
    if (entry.type != EntryType::kStream) return true;
    auto stream = original->ReadStream(entry.name);
    if (!stream) return false;
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
