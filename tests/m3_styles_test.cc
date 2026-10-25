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

// STSH (style sheet) level tests: inheritance resolution, table style
// defaults and the CNF conditional-formatting blocks.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "miniz.h"

#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/internal.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;
using doc::detail::Bytes;

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t n) {
  b[p] = n & 255;
  b[p + 1] = n >> 8;
}

std::vector<uint8_t> Prl(uint16_t op, const std::vector<uint8_t>& value) {
  std::vector<uint8_t> out{static_cast<uint8_t>(op),
                           static_cast<uint8_t>(op >> 8)};
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

std::vector<uint8_t> Sprms(std::initializer_list<std::vector<uint8_t>> list) {
  std::vector<uint8_t> out;
  for (const auto& item : list)
    out.insert(out.end(), item.begin(), item.end());
  return out;
}

// CNFOperand: cb(1), cnfc(2), grpprl.
std::vector<uint8_t> Cnf(int cnfc, const std::vector<uint8_t>& grpprl) {
  std::vector<uint8_t> out(3 + grpprl.size());
  out[0] = static_cast<uint8_t>(grpprl.size() + 2);
  out[1] = static_cast<uint8_t>(cnfc & 0xFF);
  out[2] = static_cast<uint8_t>((cnfc >> 8) & 0xFF);
  std::copy(grpprl.begin(), grpprl.end(), out.begin() + 3);
  return out;
}

// One STD (style definition) record of the STSH.
struct Std {
  int type = 1;   // sgc: 1 paragraph, 2 character, 3 table, 4 list
  int base = -1;  // stiBase; -1 encodes "no base"
  int next = -1;  // istdNext
  std::string name;
  std::vector<std::vector<uint8_t>> blocks;  // upx blocks in TAPX/PAPX order
};

uint16_t StiField(int value) {
  return static_cast<uint16_t>(value < 0 ? 0x0FFF : value);
}

std::vector<uint8_t> BuildStsh(const std::vector<Std>& styles) {
  std::vector<std::vector<uint8_t>> records;
  for (const auto& style : styles) {
    // cbSTDBaseInFile = 10: two STI words then four reserved bytes.
    std::vector<uint8_t> d(10, 0);
    Put16(d, 2,
          static_cast<uint16_t>(style.type | (StiField(style.base) << 4)));
    Put16(d, 4,
          static_cast<uint16_t>(style.blocks.size() |
                                (StiField(style.next) << 4)));
    // xstzName: character count, UTF-16LE characters, terminating NUL.
    std::vector<uint8_t> name;
    name.push_back(static_cast<uint8_t>(style.name.size()));
    name.push_back(0);
    for (char c : style.name) {
      name.push_back(static_cast<uint8_t>(c));
      name.push_back(0);
    }
    name.push_back(0);
    name.push_back(0);
    d.insert(d.end(), name.begin(), name.end());
    if (d.size() & 1)
      d.push_back(0);
    for (const auto& block : style.blocks) {
      std::vector<uint8_t> len(2);
      Put16(len, 0, static_cast<uint16_t>(block.size()));
      d.insert(d.end(), len.begin(), len.end());
      d.insert(d.end(), block.begin(), block.end());
      if (d.size() & 1)
        d.push_back(0);
    }
    records.push_back(std::move(d));
  }
  // STSHI: cbSTSHI = 18, cstd, cbSTDBaseInFile = 10, then reserved bytes.
  std::vector<uint8_t> out(20, 0);
  Put16(out, 0, 18);
  Put16(out, 2, static_cast<uint16_t>(styles.size()));
  Put16(out, 4, 10);
  for (const auto& record : records) {
    std::vector<uint8_t> len(2);
    Put16(len, 0, static_cast<uint16_t>(record.size()));
    out.insert(out.end(), len.begin(), len.end());
    out.insert(out.end(), record.begin(), record.end());
    if (record.size() & 1)
      out.push_back(0);
  }
  return out;
}

std::vector<model::Style> Read(const std::vector<uint8_t>& stsh) {
  return doc::detail::ReadStyles(
      Bytes(stsh), doc::Fib::Range{0, static_cast<uint32_t>(stsh.size())},
      {"Fixture Font"});
}

std::string Part(const model::Document& d, const char* name) {
  auto zip = docx::Write(d);
  if (!zip)
    return {};
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip->data(), zip->size(), 0))
    return {};
  size_t size = 0;
  void* p = mz_zip_reader_extract_file_to_heap(&z, name, &size, 0);
  std::string xml;
  if (p) {
    xml.assign(static_cast<const char*>(p), size);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return xml;
}
}  // namespace

TEST(M3Styles, ParagraphInheritanceChain) {
  Std parent;
  parent.name = "Parent";
  // PAPX blocks carry a 2-byte istd prefix before their sprms.
  parent.blocks.push_back(Sprms({Prl(0x2403, {1}), Prl(0x840F, {0xE8, 0x03})}));
  parent.blocks.back().insert(parent.blocks.back().begin(), {0, 0});
  parent.blocks.push_back(Sprms({Prl(0x0835, {1})}));
  Std child;
  child.name = "Child";
  child.base = 0;
  child.blocks.push_back(Sprms({Prl(0x2405, {1})}));
  child.blocks.back().insert(child.blocks.back().begin(), {0, 0});
  child.blocks.push_back(Sprms({Prl(0x0836, {1})}));

  auto styles = Read(BuildStsh({parent, child}));
  ASSERT_EQ(styles.size(), 2u);
  EXPECT_EQ(styles[0].resolved_paragraph.align, 1);
  const auto& resolved = styles[1].resolved_paragraph;
  EXPECT_EQ(resolved.align, 1);  // inherited from the base style
  EXPECT_TRUE(resolved.left_set);
  EXPECT_EQ(resolved.left, 1000);
  EXPECT_TRUE(resolved.keep_lines_set);
  EXPECT_TRUE(resolved.keep_lines);
  EXPECT_TRUE(styles[1].resolved_run.bold);
  EXPECT_TRUE(styles[1].resolved_run.italic);
}

TEST(M3Styles, TableStyleDefaultsAndConditionals) {
  const std::vector<uint8_t> brc{8, 0, 0, 255, 0, 8, 3, 2, 0};
  const std::vector<uint8_t> shd{10, 0, 0, 255, 0, 255, 255, 0, 0, 8, 0};
  Std normal;
  normal.name = "Normal";
  Std grid;
  grid.type = 3;
  grid.name = "Grid";
  grid.blocks.push_back(Sprms({
      Prl(0x740A, {0xFF, 0xFF, 0xE0, 0x00}),  // TLP: first/last row + first col
      Prl(0x3488, {2}),                       // sprmTCHorzBands
      Prl(0x3489, {1}),                       // sprmTCVertBands
      Prl(0xD63E, {6, 0, 1, 15, 3, 108, 0}),  // sprmTCellPaddingStyle
      Prl(0xD66A, Cnf(0x0040, Sprms({Prl(0xD47F, brc), Prl(0xD687, shd)}))),
  }));
  grid.blocks.push_back({0, 0});                     // PAPX (istd only)
  grid.blocks.push_back(Sprms({Prl(0x0836, {1})}));  // CHPX: italic
  auto styles = Read(BuildStsh({normal, grid}));
  ASSERT_EQ(styles.size(), 2u);
  const auto& style = styles[1];
  EXPECT_EQ(style.type, 3);
  EXPECT_EQ(style.name, "Grid");
  EXPECT_FALSE(style.table_sprms.empty());
  EXPECT_EQ(style.row_band, 2);
  EXPECT_EQ(style.col_band, 1);
  EXPECT_EQ(style.cell_margins[0], 108);
  ASSERT_EQ(style.conditionals.size(), 1u);
  EXPECT_EQ(style.conditionals[0].type, 0x0040);
  EXPECT_TRUE(style.conditionals[0].borders[0].set);
  EXPECT_EQ(style.conditionals[0].shade, 0x00FFFF);
  EXPECT_TRUE(style.run.italic);

  model::Document d;
  d.styles = styles;
  std::string xml = Part(d, "word/styles.xml");
  EXPECT_NE(xml.find("<w:tblStyleRowBandSize w:val=\"2\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:tblStyleColBandSize w:val=\"1\"/>"),
            std::string::npos);
  EXPECT_NE(xml.find("<w:tblStylePr w:type=\"band1Horz\">"), std::string::npos);
  EXPECT_NE(xml.find("w:fill=\"FFFF00\""), std::string::npos);
  EXPECT_NE(xml.find("<w:top w:val=\"double\""), std::string::npos);
}

TEST(M3Styles, SelfReferencingBaseTerminates) {
  Std self;
  self.name = "Loop";
  self.base = 0;
  self.blocks.push_back({0, 0});
  self.blocks.push_back(Sprms({Prl(0x0835, {1})}));
  auto styles = Read(BuildStsh({self}));
  ASSERT_EQ(styles.size(), 1u);
  EXPECT_TRUE(styles[0].resolved_run.bold);
}

TEST(M3Styles, OutOfRangeBaseIsIgnored) {
  Std orphan;
  orphan.name = "Orphan";
  orphan.base = 40;
  orphan.blocks.push_back({0, 0});
  orphan.blocks.push_back(Sprms({Prl(0x0836, {1})}));
  auto styles = Read(BuildStsh({orphan}));
  ASSERT_EQ(styles.size(), 1u);
  EXPECT_TRUE(styles[0].resolved_run.italic);
  EXPECT_FALSE(styles[0].resolved_run.bold);
}

TEST(M3Styles, InheritedRunDropsDirectFormatting) {
  Std normal;
  normal.name = "Normal";
  Std body;
  body.name = "Body";
  body.base = 0;
  body.blocks.push_back({0, 0});
  body.blocks.push_back(
      Sprms({Prl(0x0835, {1}), Prl(0x4A43, {24, 0}), Prl(0x2A3E, {1})}));
  auto styles = Read(BuildStsh({normal, body}));
  ASSERT_EQ(styles.size(), 2u);
  model::Document d;
  d.styles = styles;
  model::Run inherited = doc::detail::InheritedRun(d, 1);
  // The value survives so a later ToggleOperand can flip it...
  EXPECT_TRUE(inherited.bold);
  // ...but the "direct formatting" flags are cleared: the style carries it.
  EXPECT_FALSE(inherited.bold_set);
  EXPECT_EQ(inherited.size, 0);
  EXPECT_FALSE(inherited.underline_set);
  EXPECT_TRUE(inherited.font.empty());
}

TEST(M3Styles, TruncatedStyleSheetIsRejected) {
  EXPECT_TRUE(Read({}).empty());
  std::vector<uint8_t> bad(20, 0);
  Put16(bad, 0, 18);
  Put16(bad, 2, 2);  // claims two styles, provides none
  EXPECT_TRUE(Read(bad).empty());
  std::vector<uint8_t> wrong_base(20, 0);
  Put16(wrong_base, 0, 18);
  Put16(wrong_base, 2, 1);
  Put16(wrong_base, 4, 12);  // cbSTDBaseInFile must be 10 or 18
  EXPECT_TRUE(Read(wrong_base).empty());
}

TEST(M3Styles, CharacterAndListStyleRouting) {
  Std character;
  character.type = 2;
  character.name = "Emphasis";
  character.blocks.push_back(Sprms({Prl(0x2A3E, {3})}));  // underline: double
  Std numbering;
  numbering.type = 4;
  numbering.name = "ListBullet";
  numbering.blocks.push_back(Sprms({Prl(0x2403, {2})}));  // right aligned
  numbering.blocks.back().insert(numbering.blocks.back().begin(), {0, 0});
  auto styles = Read(BuildStsh({character, numbering}));
  ASSERT_EQ(styles.size(), 2u);
  EXPECT_EQ(styles[0].type, 2);
  EXPECT_TRUE(styles[0].run.underline_set);
  EXPECT_EQ(styles[0].run.underline, 3);
  EXPECT_EQ(styles[1].type, 4);
  EXPECT_EQ(styles[1].paragraph.align, 2);
}

TEST(M3Styles, FontTableDecoding) {
  // SttbfFfn: count(2), reserved(2), then one record per font.
  std::vector<uint8_t> fonts(4, 0);
  Put16(fonts, 0, 2);
  auto record = [&](const std::string& name) {
    std::vector<uint8_t> r(47, 0);
    r[0] = 46;  // cbFfnM1: record size minus the length byte
    size_t x = 40;
    for (char c : name) {
      r[x++] = static_cast<uint8_t>(c);
      r[x++] = 0;
    }
    return r;
  };
  for (const auto& r : {record("FF"), record("GG")})
    fonts.insert(fonts.end(), r.begin(), r.end());
  auto names = doc::detail::ReadFonts(
      Bytes(fonts), doc::Fib::Range{0, static_cast<uint32_t>(fonts.size())});
  ASSERT_EQ(names.size(), 2u);
  EXPECT_EQ(names[0], "FF");
  EXPECT_EQ(names[1], "GG");
  // A record shorter than the fixed 40-byte prefix stops the enumeration.
  std::vector<uint8_t> truncated(4 + 8, 0);
  Put16(truncated, 0, 1);
  truncated[4] = 8;  // cbFfnM1 < 40
  EXPECT_TRUE(doc::detail::ReadFonts(
                  Bytes(truncated),
                  doc::Fib::Range{0, static_cast<uint32_t>(truncated.size())})
                  .empty());
}
