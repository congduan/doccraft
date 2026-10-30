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

// The platform backend (iconv on POSIX, MultiByteToWideChar on Windows) must
// agree with what SupportsCodepage() reports. These are the tests that actually
// exercise the Windows backend in CI; on a build with no backend (WebAssembly)
// each iteration is skipped, leaving CP1252 as the only decoder.
TEST(CharsetTest, SupportedLatinCodepagesDecodeTheirBytes) {
  // Pages that define nearly the whole 0x80-0xFF range; the handful of
  // undefined positions are backend-specific and stay out of the count.
  for (uint32_t codepage : {1250u, 1251u, 1253u, 1254u, 1257u}) {
    SCOPED_TRACE(codepage);
    if (!SupportsCodepage(codepage))
      continue;
    int decoded = 0;
    for (int byte = 0x80; byte <= 0xFF; ++byte) {
      if (!Decode(codepage, {static_cast<uint8_t>(byte)}).empty())
        ++decoded;
    }
    // 0x80-0xFF is 128 positions and only a handful are undefined in these
    // pages; a backend that decodes nothing at all would fall far below this.
    EXPECT_GE(decoded, 100);
  }
}

TEST(CharsetTest, SupportedCodepagesDecodeKnownLetters) {
  struct Sample {
    uint32_t codepage;
    uint8_t byte;
    const char* utf8;
  };
  // Thai ko kai and Arabic alef. CP1255 and CP1258 are left out on purpose:
  // glibc implements both as stateful tables that hold a character back until
  // the next byte arrives, so a single byte cannot be asserted here; the known
  // gap is recorded in docs/KNOWN-SIMPLIFICATIONS.md.
  for (const Sample& sample :
       {Sample{874, 0xA1, "\xE0\xB8\x81"}, Sample{1256, 0xC7, "\xD8\xA7"}}) {
    SCOPED_TRACE(sample.codepage);
    if (!SupportsCodepage(sample.codepage))
      continue;
    EXPECT_EQ(Decode(sample.codepage, {sample.byte}), sample.utf8);
  }
}

// 437 (DOS US) is not an ANSI code page a .doc can name, so no backend may
// claim it; the byte then falls through the documented fallback path.
TEST(CharsetTest, UnknownCodepageIsNotClaimed) {
  EXPECT_FALSE(SupportsCodepage(437));
  EXPECT_TRUE(SupportsCodepage(1252));
}
