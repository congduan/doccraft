// Copyright 2026 The doccraft Authors. All Rights Reserved.
// TDD contract for the CFB layer (M1). These tests are the specification:
// they intentionally fail against the M1 stub until Codex implements the
// reader. Fixture-derived tests document the [MS-CFB] behaviors we commit to.

#include <array>
#include <cstring>
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

TEST(CfbReaderTest, DISABLED_OpensSyntheticSingleStream) {
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

TEST(CfbReaderTest, DISABLED_ReportsSectorSize) {
  CfbBuilder b;
  b.AddStream("s", MakePayload(4096, 1));
  auto reader = CfbReader::Open(b.Build());
  ASSERT_TRUE(reader.has_value());
  EXPECT_EQ(reader->sector_size(), 512u);
  EXPECT_EQ(reader->major_version(), 3u);
}

}  // namespace
}  // namespace doccraft::cfb
