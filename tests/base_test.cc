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

#include <cstdint>
#include <string>
#include <vector>

#include "base/binary.h"
#include "base/utf8.h"

namespace doccraft::base {
namespace {

// 34 12 | 78 56 BC 9A F0 DE | 11 22 33 44
const std::vector<uint8_t> kBytes = {0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A,
                                     0xF0, 0xDE, 0x11, 0x22, 0x33, 0x44};

TEST(BinaryTest, ReadsLittleEndianScalars) {
  const Bytes b = kBytes;
  EXPECT_EQ(U16(b, 0), 0x1234);
  EXPECT_EQ(U32(b, 0), 0x56781234);
  EXPECT_EQ(U64(b, 0), 0xDEF09ABC56781234ULL);
  EXPECT_EQ(U32(b, 8), 0x44332211UL);
}

TEST(BinaryTest, ReadsAtSliceOffset) {
  const Bytes slice = Bytes(kBytes).subspan(4);
  EXPECT_EQ(U16(slice, 0), 0x9ABC);
  EXPECT_EQ(U32(slice, 4), 0x44332211UL);
}

TEST(BinaryTest, InterpretsSignedOperandsAsTwosComplement) {
  const std::vector<uint8_t> negative = {0xFF, 0xFF};
  const std::vector<uint8_t> minimum = {0x00, 0x80};
  const std::vector<uint8_t> positive = {0xD0, 0x07};
  EXPECT_EQ(S16(negative, 0), -1);
  EXPECT_EQ(S16(minimum, 0), -32768);
  EXPECT_EQ(S16(positive, 0), 2000);
}

TEST(Utf8Test, AppendsCodePointsOfEachWidth) {
  std::string s;
  AppendUtf8(s, 0x41);     // U+0041  A
  AppendUtf8(s, 0xE9);     // U+00E9  e-acute
  AppendUtf8(s, 0x4E2D);   // U+4E2D  CJK
  AppendUtf8(s, 0x1F600);  // U+1F600  supplementary plane
  EXPECT_EQ(s, "\x41\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80");
}

TEST(Utf8Test, SwitchesWidthAtEncodingBoundaries) {
  std::string s;
  AppendUtf8(s, 0x7F);     // 1 byte
  AppendUtf8(s, 0x80);     // 2 bytes
  AppendUtf8(s, 0x7FF);    // 2 bytes
  AppendUtf8(s, 0x800);    // 3 bytes
  AppendUtf8(s, 0xFFFF);   // 3 bytes
  AppendUtf8(s, 0x10000);  // 4 bytes
  EXPECT_EQ(s.size(), static_cast<size_t>(15));
}

}  // namespace
}  // namespace doccraft::base
