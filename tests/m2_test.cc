#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "doc/fib.h"
#include "doc/text.h"
#include "docx/writer.h"

namespace {
void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}
void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[p + i] = static_cast<uint8_t>(v >> (8 * i));
}
std::vector<uint8_t> Fib(uint16_t flags = 0) {
  std::vector<uint8_t> b(1024);
  Put16(b, 0, 0xA5EC);
  Put16(b, 2, 0x00C1);
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
  EXPECT_NE(fib.error().message.find("unsupported (M6)"), std::string::npos);
}
TEST(PieceTableTest, RejectsFastSavedDocuments) {
  auto fib = doccraft::doc::ParseFib(Fib(0x0004));
  ASSERT_TRUE(fib);
  std::vector<uint8_t> table(80), word(1024);
  auto text = doccraft::doc::ExtractText(word, table, *fib);
  ASSERT_FALSE(text);
  EXPECT_NE(text.error().message.find("unsupported (M2 known gap)"),
            std::string::npos);
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
TEST(ZipTest, CrcKnownVector) {
  EXPECT_EQ(doccraft::docx::Crc32("123456789"), 0xCBF43926u);
}
}  // namespace
