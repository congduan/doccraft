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
#include <utility>
#include <vector>

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "doc/text.h"
#include "docx/writer.h"
#include "tests/cfb_test_util.h"
#include "third_party/miniz/miniz.h"

namespace {
void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[p + i] = static_cast<uint8_t>(v >> (8 * i));
}

std::vector<uint8_t> Fib(uint16_t flags = 0, uint16_t lid = 0) {
  std::vector<uint8_t> b(1024);
  Put16(b, 0, 0xA5EC);
  Put16(b, 2, 0x00C1);
  Put16(b, 6, lid);
  Put16(b, 10, flags);
  Put32(b, 24, 512);
  Put32(b, 28, 520);
  Put16(b, 32, 14);
  Put16(b, 62, 22);
  Put32(b, 64 + 3 * 4, 5);
  Put16(b, 152, 93);
  Put32(b, 154 + 33 * 8, 40);
  Put32(b, 158 + 33 * 8, 33);
  return b;
}

TEST(FibTest, ParsesFlagsAndOffsets) {
  auto fib = doccraft::doc::ParseFib(Fib(0x0204));
  ASSERT_TRUE(fib.has_value());
  EXPECT_TRUE(fib->complex);
  EXPECT_FALSE(fib->encrypted);
  EXPECT_TRUE(fib->which_table);
  EXPECT_EQ(fib->table_stream(), "1Table");
  EXPECT_EQ(fib->ccp_text, 5u);
  EXPECT_EQ(fib->fc_clx, 40u);
  EXPECT_EQ(fib->lcb_clx, 33u);
}

TEST(FibTest, RejectsEncryption) {
  auto fib = doccraft::doc::ParseFib(Fib(0x0100));
  ASSERT_FALSE(fib.has_value());
  EXPECT_EQ(fib.error().message,
            "RC4 or RC4 CryptoAPI encryption unsupported (M6)");
  auto xor_fib = doccraft::doc::ParseFib(Fib(0x8100));
  ASSERT_FALSE(xor_fib.has_value());
  EXPECT_EQ(xor_fib.error().message, "XOR obfuscation unsupported (M6)");
}

TEST(PieceTableTest, ReadsFastSavedPiecesAndPrc) {
  auto fib = doccraft::doc::ParseFib(Fib(0x0004));
  ASSERT_TRUE(fib);
  std::vector<uint8_t> table(100), word(1024);
  // A Prc followed by the final piece table. The two pieces describe an
  // insertion before older text, as in an incremental save.
  fib->lcb_clx = 39;
  table[40] = 1;
  Put16(table, 41, 3);
  table[43] = 0x35;  // sprmCFBold
  table[44] = 0x08;
  table[45] = 1;
  table[46] = 2;
  Put32(table, 47, 28);
  Put32(table, 51, 0);
  Put32(table, 55, 2);
  Put32(table, 59, 5);
  Put32(table, 65, 0x40000000u | (520u * 2));
  Put16(table, 69, 1);  // Prm1 references the first Prc.
  Put32(table, 73, 0x40000000u | (512u * 2));
  word[520] = 'N';
  word[521] = 'e';
  word[512] = 'w';
  word[513] = '!';
  word[514] = '\r';
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_TRUE(text) << text.error().message;
  EXPECT_EQ(*text, "New!\n");
  auto formatted = doccraft::doc::ParseFormatted(word, table, *fib);
  ASSERT_TRUE(formatted) << formatted.error().message;
  ASSERT_FALSE(formatted->paragraphs.empty());
  ASSERT_GE(formatted->paragraphs[0].runs.size(), 2u);
  EXPECT_TRUE(formatted->paragraphs[0].runs[0].bold);
  EXPECT_EQ(formatted->paragraphs[0].runs[0].text, "Ne");
  EXPECT_EQ(formatted->paragraphs[0].runs[1].text, "w!");
  auto zip = doccraft::docx::Write(*formatted);
  ASSERT_TRUE(zip) << zip.error().message;
  mz_zip_archive archive = {};
  ASSERT_TRUE(mz_zip_reader_init_mem(&archive, zip->data(), zip->size(), 0));
  size_t xml_size = 0;
  void* xml_data = mz_zip_reader_extract_file_to_heap(
      &archive, "word/document.xml", &xml_size, 0);
  ASSERT_NE(xml_data, nullptr);
  std::string xml(static_cast<const char*>(xml_data), xml_size);
  mz_free(xml_data);
  mz_zip_reader_end(&archive);
  EXPECT_NE(xml.find("<w:b"), std::string::npos);
}

TEST(PieceTableTest, RejectsInvalidFastSavedPrcReference) {
  auto fib = doccraft::doc::ParseFib(Fib(0x0004));
  ASSERT_TRUE(fib);
  fib->lcb_clx = 21;
  std::vector<uint8_t> table(80), word(1024);
  table[40] = 2;
  Put32(table, 41, 16);
  Put32(table, 45, 0);
  Put32(table, 49, 5);
  Put32(table, 55, 0x40000000u | (512u * 2));
  Put16(table, 59, 1);  // Prm1 index zero, but there is no Prc.
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_FALSE(text);
  EXPECT_EQ(text.error().message, "CLX Prm1 index outside Prc array");
}

TEST(PieceTableTest, RejectsTruncatedFastSavedPrc) {
  auto fib = doccraft::doc::ParseFib(Fib(0x0004));
  ASSERT_TRUE(fib);
  fib->lcb_clx = 3;
  std::vector<uint8_t> table(80), word(1024);
  table[40] = 1;
  Put16(table, 41, 4);
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_FALSE(text);
  EXPECT_EQ(text.error().message, "truncated CLX grpprl");
}

TEST(PieceTableTest, ReadsCompressedAndUtf16Pieces) {
  auto fib = doccraft::doc::ParseFib(Fib());
  ASSERT_TRUE(fib);
  std::vector<uint8_t> table(80), word(1024);
  table[40] = 2;
  Put32(table, 41, 28);
  Put32(table, 45, 0);
  Put32(table, 49, 2);
  Put32(table, 53, 5);
  Put32(table, 59, 0x40000000u | (512u * 2));
  Put32(table, 67, 600);
  word[512] = 'A';
  word[513] = 0x80;
  Put16(word, 600, 'B');
  Put16(word, 602, 0x0D);
  Put16(word, 604, 'C');
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_TRUE(text);
  EXPECT_EQ(*text,
            "A\xE2\x82\xAC"
            "B\nC");
}

TEST(PieceTableTest, ReplacesNonXmlCharsWithReplacementChar) {
  // U+FFFE/U+FFFF are outside the XML 1.0 Char production; a corrupted UTF-16
  // piece must not be able to produce a not-well-formed document.xml.
  auto fib = doccraft::doc::ParseFib(Fib());
  ASSERT_TRUE(fib);
  std::vector<uint8_t> table(80), word(1024);
  // PlcPcd with 2 pieces: size = 4*3 CPs + 8*2 PCDs = 28; clx total = 33.
  table[40] = 2;
  Put32(table, 41, 28);
  Put32(table, 45, 0);
  Put32(table, 49, 3);
  Put32(table, 53, 5);
  Put16(table, 57, 0);
  Put32(table, 59, 600);  // PCD0: uncompressed UTF-16 @600 (flags, fc, prm)
  Put16(table, 63, 0);
  Put16(table, 65, 0);
  Put32(table, 67, 612);  // PCD1: uncompressed UTF-16 @612
  Put16(table, 71, 0);
  Put16(word, 600, 0xFFFF);
  Put16(word, 602, 0xFFFE);
  Put16(word, 604, 0x0D);
  Put16(word, 612, 'E');
  Put16(word, 614, 'F');
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_TRUE(text);
  EXPECT_EQ(*text,
            "\xEF\xBF\xBD"
            "\xEF\xBF\xBD"
            "\nEF");
}

TEST(PieceTableTest, DecodesCompressedPiecesInDocumentCodepage) {
  // A ru-RU document stores compressed pieces in CP1251. DBCS text is always
  // written as UTF-16 pieces, so a compressed piece only ever needs the
  // single-byte mapping of the document code page.
  std::vector<uint8_t> word = Fib(0, 0x0419);
  Put32(word, 158 + 33 * 8, 21);  // lcb_clx: Pcdt with a single piece
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib);
  EXPECT_EQ(fib->codepage, 1251u);
  EXPECT_EQ(fib->lcb_clx, 21u);
  std::vector<uint8_t> table(80);
  table[40] = 2;
  Put32(table, 41, 16);  // Pcdt: 2 CPs (8 bytes) + 1 PCD (8 bytes)
  Put32(table, 45, 0);
  Put32(table, 49, 2);
  Put16(table, 53, 0);                         // PCD flags
  Put32(table, 55, 0x40000000u | (512u * 2));  // compressed, byte offset 512
  Put16(table, 59, 0);                         // PCD prm
  word[512] = 0xC0;                            // A
  word[513] = 0xC1;                            // B
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_TRUE(text) << text.error().message;
  EXPECT_EQ(*text, "\xD0\x90\xD0\x91");
}

TEST(PieceTableTest, DecodesCp932HalfWidthKatakana) {
  std::vector<uint8_t> word = Fib(0, 0x0411);
  Put32(word, 158 + 33 * 8, 21);
  auto fib = doccraft::doc::ParseFib(word);
  ASSERT_TRUE(fib);
  EXPECT_EQ(fib->codepage, 932u);
  std::vector<uint8_t> table(80);
  table[40] = 2;
  Put32(table, 41, 16);
  Put32(table, 45, 0);
  Put32(table, 49, 1);
  Put16(table, 53, 0);
  Put32(table, 55, 0x40000000u | (512u * 2));
  Put16(table, 59, 0);
  word[512] = 0xB1;
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_TRUE(text) << text.error().message;
  EXPECT_EQ(*text, "\xEF\xBD\xB1");
}

TEST(PieceTableTest, UnknownCodepageKeepsCp1252) {
  auto fib = doccraft::doc::ParseFib(Fib(0, 0x0409));
  ASSERT_TRUE(fib);
  EXPECT_EQ(fib->codepage, 1252u);
}

TEST(PieceTableTest, RejectsOverlappingPiecesThatOverCoverWordDocument) {
  constexpr uint32_t kPieces = 9;
  constexpr uint32_t kPieceLength = 1024;
  std::vector<uint8_t> word = Fib();
  word.resize(8192, 'A');
  Put32(word, 64 + 3 * 4, kPieces * kPieceLength);
  Put32(word, 154 + 33 * 8, 0);
  Put32(word, 158 + 33 * 8, 5 + 4 + 12 * kPieces);

  std::vector<uint8_t> table(5 + 4 + 12 * kPieces);
  table[0] = 2;
  Put32(table, 1, 4 + 12 * kPieces);
  const size_t pcd = 5 + 4 * (kPieces + 1);
  for (uint32_t i = 0; i <= kPieces; ++i)
    Put32(table, 5 + 4 * i, i * kPieceLength);
  for (uint32_t i = 0; i < kPieces; ++i)
    Put32(table, pcd + 8 * i + 2, 0x40000000u | (512u * 2));

  doccraft::cfb::test::CfbBuilder builder;
  auto container = builder.AddStream("WordDocument", std::move(word))
                       .AddStream("0Table", std::move(table))
                       .Build();
  auto cfb = doccraft::cfb::CfbReader::Open(container);
  ASSERT_TRUE(cfb);
  auto word_stream = cfb->ReadStream("WordDocument");
  auto table_stream = cfb->ReadStream("0Table");
  ASSERT_TRUE(word_stream);
  ASSERT_TRUE(table_stream);
  auto fib = doccraft::doc::ParseFib(*word_stream);
  ASSERT_TRUE(fib);
  auto text = doccraft::doc::ExtractText(*word_stream, *table_stream, *fib);
  ASSERT_FALSE(text);
  EXPECT_EQ(text.error().message, "piece table over-covers WordDocument");
}

TEST(ZipTest, CrcKnownVector) {
  EXPECT_EQ(doccraft::docx::Crc32("123456789"), 0xCBF43926u);
}
}  // namespace
