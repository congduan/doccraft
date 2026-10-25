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

// ANSI code page decoding for compressed [MS-DOC] pieces (FibBase.lid).

#include "base/charset.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {
using doccraft::base::Bytes;
using doccraft::base::CodepageForLid;
using doccraft::base::DecodeAnsiChar;
using doccraft::base::SupportsCodepage;

std::string Decode(uint32_t codepage, const std::vector<uint8_t>& bytes,
                   size_t* consumed = nullptr) {
  std::string out;
  size_t used = DecodeAnsiChar(codepage, Bytes(bytes), out);
  if (consumed != nullptr)
    *consumed = used;
  return used == 0 ? std::string() : out;
}
}  // namespace

TEST(CharsetTest, LidMapsToAnsiCodepage) {
  EXPECT_EQ(CodepageForLid(0x0804), 936u);   // zh-CN
  EXPECT_EQ(CodepageForLid(0x0404), 950u);   // zh-TW
  EXPECT_EQ(CodepageForLid(0x0411), 932u);   // ja-JP
  EXPECT_EQ(CodepageForLid(0x0412), 949u);   // ko-KR
  EXPECT_EQ(CodepageForLid(0x0419), 1251u);  // ru-RU
  EXPECT_EQ(CodepageForLid(0x0415), 1250u);  // pl-PL
  EXPECT_EQ(CodepageForLid(0x041E), 874u);   // th-TH
  EXPECT_EQ(CodepageForLid(0x0409), 1252u);  // en-US
  EXPECT_EQ(CodepageForLid(0x0000), 1252u);  // unknown -> default
}

TEST(CharsetTest, AsciiIsCodepageIndependent) {
  for (uint32_t codepage : {1252u, 1251u, 932u, 936u, 950u}) {
    SCOPED_TRACE(codepage);
    size_t used = 0;
    EXPECT_EQ(Decode(codepage, {'A'}, &used), "A");
    EXPECT_EQ(used, 1u);
  }
}

TEST(CharsetTest, Cp1252AccentsAndSymbols) {
  size_t used = 0;
  EXPECT_EQ(Decode(1252, {0x80}, &used), "\xE2\x82\xAC");  // euro sign
  EXPECT_EQ(used, 1u);
  EXPECT_EQ(Decode(1252, {0xE9}), "\xC3\xA9");  // e acute
  EXPECT_EQ(Decode(1252, {0x81}), "\xC2\x81");  // U+0081 control char
}

TEST(CharsetTest, Cp1251Cyrillic) {
  EXPECT_EQ(Decode(1251, {0xC0}), "\xD0\x90");  // A
  EXPECT_EQ(Decode(1251, {0xFF}), "\xD1\x8F");  // ya
}

TEST(CharsetTest, DbcsPairConsumesTwoBytes) {
  size_t used = 0;
  ASSERT_TRUE(SupportsCodepage(936));
  EXPECT_EQ(Decode(936, {0xD6, 0xD0}, &used), "\xE4\xB8\xAD");  // zhong
  EXPECT_EQ(used, 2u);
  EXPECT_EQ(Decode(936, {0xCE, 0xC4}), "\xE6\x96\x87");  // wen
  EXPECT_EQ(Decode(950, {0xA4, 0xA4}), "\xE4\xB8\xAD");  // Big5 zhong
}

TEST(CharsetTest, Cp932HalfWidthKatakana) {
  size_t used = 0;
  EXPECT_EQ(Decode(932, {0xB1}, &used), "\xEF\xBD\xB1");  // half-width a
  EXPECT_EQ(used, 1u);
}

TEST(CharsetTest, InvalidSequenceReportsZero) {
  // A lead byte with nothing behind it cannot be decoded.
  EXPECT_EQ(Decode(936, {0xD6}), std::string());
  EXPECT_EQ(Decode(936, {0xD6, 0x20}), std::string());
  EXPECT_EQ(Decode(1252, {}), std::string());
}

TEST(CharsetTest, ZeroCodePageFallsBackToCp1252) {
  EXPECT_EQ(Decode(0, {0x80}), "\xE2\x82\xAC");
}
