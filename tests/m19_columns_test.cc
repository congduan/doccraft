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

// Non-evenly-spaced columns ([MS-DOC] §2.6.4 sprmSFEvenlySpaced /
// sprmSDxaColWidth / sprmSDxaColSpacing): a column that only carries
// sprmSDxaColSpacing keeps its w:col output with a fallback width instead of
// being dropped from w:cols.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "doc/format.h"
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

TEST(M19Columns, SpacingOnlyColumnKeepsWColWithFallbackWidth) {
  model::Section s;
  // sprmSFEvenlySpaced = 0, sprmSCcolumns = 3 columns.
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x3005, Bytes(std::vector<uint8_t>{0})));
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x500B, Bytes(std::vector<uint8_t>{2, 0})));
  // Column 0 has an explicit width, column 1 only a spacing, column 2 has
  // neither sprm.
  ASSERT_TRUE(doc::detail::ApplySection(
      s, 0xF203, Bytes(std::vector<uint8_t>{0, 0x88, 0x13})));  // 5000
  ASSERT_TRUE(doc::detail::ApplySection(
      s, 0xF204, Bytes(std::vector<uint8_t>{1, 0x2C, 0x01})));  // 300
  EXPECT_FALSE(s.columns_even);
  ASSERT_EQ(s.column_specs.size(), 2u);
  EXPECT_EQ(s.column_specs[1].width, -1);
  EXPECT_EQ(s.column_specs[1].space, 300);

  model::Document d;
  d.section = s;
  std::string xml = Xml(d);
  EXPECT_NE(xml.find("w:equalWidth=\"0\""), std::string::npos);
  // The spacing-only column is no longer silently dropped: it keeps its
  // spacing and gains LibreOffice's 1-inch fallback width (ww8par6.cxx).
  // The column with no width or spacing sprm at all keeps the column count
  // stable with the same fallback width.
  EXPECT_NE(xml.find("<w:col w:w=\"5000\"/>"
                     "<w:col w:w=\"1440\" w:space=\"300\"/>"
                     "<w:col w:w=\"1440\"/>"),
            std::string::npos);
}

TEST(M19Columns, EvenlySpacedSectionStillDropsSpacingOnlySpec) {
  model::Section s;
  ASSERT_TRUE(
      doc::detail::ApplySection(s, 0x500B, Bytes(std::vector<uint8_t>{1, 0})));
  ASSERT_TRUE(doc::detail::ApplySection(
      s, 0xF204, Bytes(std::vector<uint8_t>{0, 0x2C, 0x01})));
  EXPECT_TRUE(s.columns_even);
  model::Document d;
  d.section = s;
  std::string xml = Xml(d);
  // sprmSDxaColWidth/Spacing only apply "in case columns are not evenly
  // spaced" ([MS-DOC] §2.6.4), so an evenly spaced section keeps dropping
  // the spec entry and stays equal-width.
  EXPECT_EQ(xml.find("w:equalWidth=\"0\""), std::string::npos);
  EXPECT_EQ(xml.find("<w:col "), std::string::npos);
}
}  // namespace
