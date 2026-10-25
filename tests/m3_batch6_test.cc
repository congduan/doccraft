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

#include "doc/format/sprm.h"
#include "docx/writer.h"
#include "third_party/miniz/miniz.h"
namespace {
namespace doc = doccraft::doc;
namespace docx = doccraft::docx;
namespace model = doccraft::model;

using doc::detail::Bytes;
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

model::Document WithParagraph(const model::Paragraph& p) {
  model::Document d;
  d.paragraphs.push_back(p);
  return d;
}

TEST(M3Batch6, ParagraphLayoutFlags) {
  struct Case {
    uint16_t op;
    const char* xml;
  };
  const Case cases[] = {
      {0x240C, "<w:suppressLineNumbers/"},
      {0x242A, "<w:suppressAutoHyphens/"},
      {0x2431, "<w:widowControl/"},
      {0x2433, "<w:kinsoku/"},
      {0x2434, "<w:wordWrap/"},
      {0x2435, "<w:overflowPunct/"},
      {0x2436, "<w:topLinePunct/"},
      {0x2437, "<w:autoSpaceDE/"},
      {0x2438, "<w:autoSpaceDN/"},
      {0x2441, "<w:bidi/"},
      {0x2447, "<w:snapToGrid/"},
      {0x2448, "<w:adjustRightInd/"},
      {0x245B, "w:beforeAutospacing=\"1\""},
      {0x245C, "w:afterAutospacing=\"1\""},
      {0x246D, "<w:contextualSpacing/"},
      {0x2470, "<w:mirrorIndents/"},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.op);
    model::Paragraph p;
    const std::vector<uint8_t> v{1};

    doc::detail::ApplyPara(p, c.op, Bytes(v));
    EXPECT_NE(Xml(WithParagraph(p)).find(c.xml), std::string::npos);
  }
  model::Paragraph p;
  const std::vector<uint8_t> no{0};

  doc::detail::ApplyPara(p, 0x2431, Bytes(no));
  EXPECT_NE(Xml(WithParagraph(p)).find("<w:widowControl w:val=\"0\""),
            std::string::npos);
}

TEST(M3Batch6, ExistingParagraphAndCharacterPaths) {
  for (uint16_t op :
       {static_cast<uint16_t>(0x8411), static_cast<uint16_t>(0xC64F),
        static_cast<uint16_t>(0xC650)}) {
    SCOPED_TRACE(op);
    model::Paragraph p;
    const std::vector<uint8_t> v =
        op == 0x8411 ? std::vector<uint8_t>{0xD0, 0x02}
                     : std::vector<uint8_t>{8, 0, 0, 255, 0, 8, 3, 2, 0};
    doc::detail::ApplyPara(p, op, Bytes(v));
    EXPECT_NE(Xml(WithParagraph(p))
                  .find(op == 0x8411   ? "w:firstLine=\"720\""
                        : op == 0xC64F ? "<w:left w:val=\"double\""
                                       : "<w:bottom w:val=\"double\""),
              std::string::npos);
  }
  model::Run r;
  r.text = "X";
  const std::vector<uint8_t> style{1, 0};

  doc::detail::ApplyRun(r, 0x4A30, Bytes(style), {});
  model::Paragraph p;
  p.runs.push_back(r);
  EXPECT_NE(Xml(WithParagraph(p)).find("<w:rStyle w:val=\"Heading1\""),
            std::string::npos);
}

TEST(M3Batch6, ExistingAndNewTablePaths) {
  for (uint16_t op :
       {static_cast<uint16_t>(0x5400), static_cast<uint16_t>(0x9601),
        static_cast<uint16_t>(0x3619), static_cast<uint16_t>(0x3465),
        static_cast<uint16_t>(0xD671), static_cast<uint16_t>(0xD672)}) {
    SCOPED_TRACE(op);
    model::Table t;
    model::RowProperties row;
    row.cells.resize(63);
    std::vector<uint8_t> v;
    if (op == 0x5400)
      v = {1, 0};
    else if (op == 0x9601)
      v = {0xA0, 0x0F};
    else if (op == 0x3619 || op == 0x3465)
      v = {1};
    else
      v = {10, 0, 0, 255, 0, 255, 255, 0, 0, 8, 0};
    ASSERT_TRUE(doc::detail::ApplyTable(t, row, op, Bytes(v)));
    if (op == 0x5400) {
      EXPECT_EQ(t.align, 1);
    }
    if (op == 0x9601) {
      EXPECT_EQ(t.left_origin, 4000);
    }
    if (op == 0x3619) {
      EXPECT_TRUE(t.keep_next);
    }
    if (op == 0x3465) {
      EXPECT_TRUE(t.no_overlap);
    }
    if (op == 0xD671) {
      EXPECT_EQ(row.cells[22].shade, 0x00ffff);
    }
    if (op == 0xD672) {
      EXPECT_EQ(row.cells[44].shade, 0x00ffff);
    }
    if (op == 0x5400 || op == 0x3465) {
      t.rows = {{{}}};
      t.row_properties = {row};
      model::Document d;
      d.tables.push_back(t);
      d.blocks.push_back({true, 0});
      EXPECT_NE(Xml(d).find(op == 0x5400 ? "<w:jc w:val=\"center\""
                                         : "<w:tblOverlap w:val=\"never\""),
                std::string::npos);
    }
  }
}

TEST(M3Batch6, SectionBordersAndNumbering) {
  for (uint16_t op :
       {static_cast<uint16_t>(0x702B), static_cast<uint16_t>(0x702C),
        static_cast<uint16_t>(0x702D), static_cast<uint16_t>(0x702E),
        static_cast<uint16_t>(0xD234), static_cast<uint16_t>(0xD235),
        static_cast<uint16_t>(0xD236), static_cast<uint16_t>(0xD237)}) {
    SCOPED_TRACE(op);
    model::Section s;
    const std::vector<uint8_t> v =
        op < 0xD000 ? std::vector<uint8_t>{8, 3, 6, 2}
                    : std::vector<uint8_t>{8, 0, 0, 255, 0, 8, 3, 2, 0};
    ASSERT_TRUE(doc::detail::ApplySection(s, op, Bytes(v)));
    model::Document d;
    d.section = s;
    const char* side[] = {"w:top", "w:left", "w:bottom", "w:right"};

    EXPECT_NE(Xml(d).find(std::string("<") +
                          side[op < 0xD000 ? op - 0x702B : op - 0xD234] +
                          " w:val=\"double\""),
              std::string::npos);
  }

  struct Case {
    uint16_t op;
    std::vector<uint8_t> v;
    const char* xml;
  };
  const Case cases[] = {
      {0x3000, {1}, "w:chapSep=\"period\""},
      {0x3001, {2}, "w:chapStyle=\"2\""},
      {0x303E, {1}, "<w:endnotePr><w:numRestart w:val=\"eachSect\""},
      {0x5041, {5, 0}, "<w:endnotePr><w:numStart w:val=\"5\""},
      {0x5042, {2, 0}, "<w:endnotePr><w:numFmt w:val=\"lowerRoman\""},
      {0x7030, {10, 0, 0, 0}, "w:charSpace=\"10\""},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.op);
    model::Section s;
    s.grid_mode = 1;
    ASSERT_TRUE(doc::detail::ApplySection(s, c.op, Bytes(c.v)));
    model::Document d;
    d.section = s;
    EXPECT_NE(Xml(d).find(c.xml), std::string::npos);
  }
}
}  // namespace
