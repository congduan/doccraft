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

// A valid container with one mini stream; callers corrupt one header field.
std::vector<uint8_t> ValidContainer() {
  return CfbBuilder().AddStream("s", MakePayload(100, 7)).Build();
}

TEST(CfbLenientHeader, ToleratesInvalidByteOrderMark) {
  auto bytes = ValidContainer();
  Put16(bytes, 28, 0xFF20);
  auto reader = CfbReader::Open(bytes);
  ASSERT_TRUE(reader.has_value()) << reader.error().message;
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

TEST(CfbLenientHeader, RejectsCyclicDirectoryChain) {
  auto bytes = ValidContainer();
  const uint32_t dir_start = U32(bytes, 48);
  Put32(bytes, FatEntryOffset(bytes, dir_start), dir_start);
  auto reader = CfbReader::Open(bytes);
  ASSERT_FALSE(reader.has_value());
  EXPECT_EQ(reader.error().code, ErrorCode::kCorruptDirectory);
}

TEST(CfbLenientHeader, RejectsDirectoryChainTwoSectorsPastEnd) {
  auto bytes = ValidContainer();
  const uint32_t total =
      static_cast<uint32_t>(bytes.size() / (1u << U16(bytes, 30)) - 1);
  const uint32_t dir_start = U32(bytes, 48);
  Put32(bytes, FatEntryOffset(bytes, dir_start), total + 1);
  auto reader = CfbReader::Open(bytes);
  ASSERT_FALSE(reader.has_value());
  EXPECT_EQ(reader.error().code, ErrorCode::kCorruptDirectory);
}

}  // namespace
}  // namespace doccraft::cfb
