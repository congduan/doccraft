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

// Nested tables: sprmPItap gives each paragraph its absolute table depth, so a
// table whose rows end at depth 2 becomes a table reachable from a cell of the
// depth-1 table instead of being flattened into it.
//
// No real sample can be generated locally: LibreOffice flattens a nested table
// into plain paragraphs when it exports .doc, so the mark stream below is
// assembled by hand from the [MS-DOC] definitions.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "miniz.h"

#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/sprm.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;

constexpr uint32_t kTextFc = 2048;
// Page 3 keeps the FKP clear of the FIB (pages 0-1 hold it).
constexpr uint32_t kPapxPage = 3;

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[p + i] = static_cast<uint8_t>(v >> (8 * i));
}

std::vector<uint8_t> Prl(uint16_t op, const std::vector<uint8_t>& value) {
  std::vector<uint8_t> out{static_cast<uint8_t>(op),
                           static_cast<uint8_t>(op >> 8)};
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

// One paragraph's PAPX: fInTable, an optional row/table terminator and the
// absolute table depth from sprmPItap.
std::vector<uint8_t> PapxSpms(int depth, bool row_end, bool inner_row_end) {
  std::vector<uint8_t> sprms = Prl(0x2416, {1});
  if (row_end) {
    const auto terminator = Prl(0x2417, {1});
    sprms.insert(sprms.end(), terminator.begin(), terminator.end());
  }
  if (inner_row_end) {
    const auto terminator = Prl(0x244C, {1});
    sprms.insert(sprms.end(), terminator.begin(), terminator.end());
  }
  const auto itap = Prl(0x6649, {static_cast<uint8_t>(depth), 0, 0, 0});
  sprms.insert(sprms.end(), itap.begin(), itap.end());
  return sprms;
}

// A PAPX payload is a 2-byte istd followed by the grpprl, and its recorded
// length is odd (cb * 2 - 1), so an even grpprl gets a one-byte pad.
std::vector<uint8_t> PapxPayload(const std::vector<uint8_t>& sprms) {
  std::vector<uint8_t> body{0, 0};
  body.insert(body.end(), sprms.begin(), sprms.end());
  if ((body.size() & 1) == 0)
    body.push_back(0);
  std::vector<uint8_t> out;
  out.push_back(static_cast<uint8_t>((body.size() + 1) / 2));
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

std::expected<model::Document, doc::Error> BuildDocument() {
  // Marks:  O1 <cell> O2 <para> I1 <cell> I2 <inner row> O2 tail <para>
  //         <cell> <outer row>
  const std::string text =
      "O1\x07"
      "O2\x0d"
      "I1\x07"
      "I2\x07"
      "O2 tail\x0d"
      "\x07"
      "\x07";
  std::vector<uint32_t> mark_fc;  // FC of every paragraph mark, in order
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\x07' || text[i] == '\x0d')
      mark_fc.push_back(kTextFc + static_cast<uint32_t>(i));
  }
  const std::vector<std::vector<uint8_t>> sprms = {
      PapxPayload(PapxSpms(1, false, false)),  // O1 cell
      PapxPayload(PapxSpms(1, false, false)),  // O2 paragraph
      PapxPayload(PapxSpms(2, false, false)),  // I1 cell
      PapxPayload(PapxSpms(2, true, true)),    // I2 cell + inner row end
      PapxPayload(PapxSpms(1, false, false)),  // O2 tail paragraph
      PapxPayload(PapxSpms(1, false, false)),  // outer cell 2 end
      PapxPayload(PapxSpms(1, true, false)),   // outer row end
  };

  std::vector<uint8_t> word(3072);
  Put16(word, 0, 0xA5EC);
  Put16(word, 2, 0x00C1);
  Put32(word, 24, kTextFc);
  Put32(word, 28, kTextFc + static_cast<uint32_t>(text.size()));
  Put16(word, 32, 14);
  Put16(word, 62, 22);
  Put32(word, 64 + 3 * 4, static_cast<uint32_t>(text.size()));
  Put16(word, 152, 93);
  Put32(word, 154 + 33 * 8, 0);   // fcClx
  Put32(word, 158 + 33 * 8, 21);  // lcbClx
  Put32(word, 154 + 13 * 8, 48);  // PlcfBtePapx
  Put32(word, 158 + 13 * 8, 12);
  for (size_t i = 0; i < text.size(); ++i)
    word[kTextFc + i] = static_cast<uint8_t>(text[i]);

  std::vector<uint8_t> table(64);
  table[0] = 2;
  Put32(table, 1, 16);
  Put32(table, 5, 0);
  Put32(table, 9, static_cast<uint32_t>(text.size()));
  Put16(table, 13, 0);
  Put32(table, 15, 0x40000000u | (kTextFc * 2));
  Put16(table, 19, 0);
  Put32(table, 48, kTextFc);
  Put32(table, 52, kTextFc + static_cast<uint32_t>(text.size()));
  Put32(table, 56, kPapxPage);

  const size_t base = kPapxPage * 512;
  const size_t count = sprms.size();
  for (size_t i = 0; i < count; ++i) {
    Put32(word, base + 4 * i, i == 0 ? kTextFc : mark_fc[i - 1] + 1);
  }
  Put32(word, base + 4 * count, mark_fc.back() + 1);
  word[base + 511] = static_cast<uint8_t>(count);
  // The rgbx offset byte holds the grpprl offset divided by two.
  size_t data = 4 * (count + 1) + 13 * count + 8;
  if (data & 1)
    ++data;
  for (size_t i = 0; i < count; ++i) {
    word[base + 4 * (count + 1) + 13 * i] = static_cast<uint8_t>(data / 2);
    std::copy(sprms[i].begin(), sprms[i].end(), word.begin() + base + data);
    data += sprms[i].size();
    if (data & 1)
      ++data;
  }

  auto fib = doc::ParseFib(word);
  if (!fib)
    return std::unexpected(fib.error());
  return doc::ParseFormatted(word, table, *fib);
}

std::string Xml(const model::Document& d) {
  auto zip = docx::Write(d);
  if (!zip)
    return {};
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip->data(), zip->size(), 0))
    return {};
  size_t size = 0;
  void* p =
      mz_zip_reader_extract_file_to_heap(&z, "word/document.xml", &size, 0);
  std::string xml;
  if (p) {
    xml.assign(static_cast<const char*>(p), size);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return xml;
}

std::string ParagraphText(const model::Document& d, const model::Block& block) {
  std::string out;
  if (block.index >= d.paragraphs.size())
    return out;
  for (const auto& run : d.paragraphs[block.index].runs)
    out += run.text;
  return out;
}
}  // namespace

TEST(M3NestedTable, DepthTwoTableLivesInTheEnclosingCell) {
  auto d = BuildDocument();
  ASSERT_TRUE(d) << d.error().message;
  // Two tables: the outer one (a body block) and the nested one, which the
  // parser finishes first and therefore stores before its parent.
  ASSERT_EQ(d->tables.size(), 2u);
  ASSERT_EQ(d->blocks.size(), 1u);
  ASSERT_TRUE(d->blocks[0].table);
  const model::Table& outer = d->tables[d->blocks[0].index];
  ASSERT_EQ(outer.rows.size(), 1u);
  ASSERT_EQ(outer.rows[0].size(), 2u);
  EXPECT_EQ(ParagraphText(*d, outer.rows[0][0].blocks.at(0)), "O1");

  // The second cell holds: paragraph "O2", the nested table, paragraph
  // "O2 tail" - in that order.
  const auto& second = outer.rows[0][1].blocks;
  ASSERT_EQ(second.size(), 3u);
  EXPECT_FALSE(second[0].table);
  EXPECT_EQ(ParagraphText(*d, second[0]), "O2");
  ASSERT_TRUE(second[1].table);
  EXPECT_FALSE(second[2].table);
  EXPECT_EQ(ParagraphText(*d, second[2]), "O2 tail");

  // The nested table is a real 1x2 table with its own cells.
  ASSERT_LT(second[1].index, d->tables.size());
  const model::Table& inner = d->tables[second[1].index];
  ASSERT_EQ(inner.rows.size(), 1u);
  ASSERT_EQ(inner.rows[0].size(), 2u);
  EXPECT_EQ(ParagraphText(*d, inner.rows[0][0].blocks.at(0)), "I1");
  EXPECT_EQ(ParagraphText(*d, inner.rows[0][1].blocks.at(0)), "I2");
}

TEST(M3NestedTable, WriterNestsTableElements) {
  auto d = BuildDocument();
  ASSERT_TRUE(d) << d.error().message;
  const std::string xml = Xml(*d);
  ASSERT_FALSE(xml.empty());
  // document.xml must contain <w:tbl> inside <w:tc>.
  const size_t outer_tc = xml.find("<w:tc>");
  ASSERT_NE(outer_tc, std::string::npos);
  const size_t inner_tbl = xml.find("<w:tbl>", outer_tc);
  EXPECT_NE(inner_tbl, std::string::npos);
  EXPECT_NE(xml.find(">I1</w:t>"), std::string::npos);
  EXPECT_NE(xml.find(">I2</w:t>"), std::string::npos);
  EXPECT_EQ(xml.find(">I1</w:t>"), xml.rfind(">I1</w:t>"));
}

TEST(M3NestedTable, DeltaDepthIsAccumulated) {
  // sprmPDtap adjusts the depth that sprmPItap established. Both operands
  // are spra 3 (4-byte signed) and clamp to [0, kMaxTableDepth] through the
  // same ClampTableDepth helper the frame stack uses, so the model field
  // always names the depth of the structure that was actually built.
  model::Paragraph p;
  const std::vector<uint8_t> absolute{2, 0, 0, 0};
  const std::vector<uint8_t> plus_one{1, 0, 0, 0};
  const std::vector<uint8_t> minus_two{0xFE, 0xFF, 0xFF, 0xFF};
  const std::vector<uint8_t> over_deep{0xFF, 0xFF, 0x00, 0x00};  // 65535
  doc::detail::ApplyPara(p, 0x6649, doc::detail::Bytes(absolute));
  EXPECT_EQ(p.table_depth, 2);
  doc::detail::ApplyPara(p, 0x664A, doc::detail::Bytes(plus_one));
  EXPECT_EQ(p.table_depth, 3);
  doc::detail::ApplyPara(p, 0x664A, doc::detail::Bytes(minus_two));
  EXPECT_EQ(p.table_depth, 1);
  // Clamped at zero, like the frame stack's fInTable fallback floor.
  doc::detail::ApplyPara(p, 0x664A, doc::detail::Bytes(minus_two));
  EXPECT_EQ(p.table_depth, 0);
  // Over-deep operands clamp to kMaxTableDepth instead of being dropped.
  doc::detail::ApplyPara(p, 0x6649, doc::detail::Bytes(over_deep));
  EXPECT_EQ(p.table_depth, 8);
  doc::detail::ApplyPara(p, 0x664A, doc::detail::Bytes(plus_one));
  EXPECT_EQ(p.table_depth, 8);
}

TEST(M3NestedTable, DeltaDepthWideArithmetic) {
  model::Paragraph p;
  const std::vector<uint8_t> absolute{3, 0, 0, 0};
  doc::detail::ApplyPara(p, 0x6649, doc::detail::Bytes(absolute));

  const auto apply_delta = [&](const std::vector<uint8_t>& operand) {
    const auto sprm = Prl(0x664A, operand);
    return doc::detail::WalkParagraph(
        doc::detail::Bytes(sprm), doc::detail::Bytes(sprm),
        [&](uint16_t op, doc::detail::Bytes value) {
          doc::detail::ApplyPara(p, op, value);
        });
  };

  EXPECT_TRUE(apply_delta({0xFF, 0xFF, 0xFF, 0x7F}));  // INT32_MAX
  EXPECT_EQ(p.table_depth, 8);
  doc::detail::ApplyPara(p, 0x6649, doc::detail::Bytes(absolute));
  EXPECT_TRUE(apply_delta({0, 0, 0, 0x80}));  // INT32_MIN
  EXPECT_EQ(p.table_depth, 0);
  doc::detail::ApplyPara(p, 0x6649, doc::detail::Bytes(absolute));
  EXPECT_TRUE(apply_delta({0, 0, 0xFF, 0xFF}));  // 0xFFFF0000
  EXPECT_EQ(p.table_depth, 0);
}
