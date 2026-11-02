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

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"
#include "tests/textbox_test_util.h"

namespace {
using namespace doccraft;
using namespace doccraft::test::textbox;

TEST(M4Textbox, SyntheticFlowBreaksStayInStoryOrder) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/textbox.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb) << cfb.error().message;
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word) << word.error().message;
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table) << table.error().message;
  // Two text ranges inside the same FTXBXS chain, plus its final guard.
  std::vector<uint8_t> breaks(34);
  Put32(breaks, 0, 0);
  Put32(breaks, 4, 8);
  Put32(breaks, 8, 17);
  Put32(breaks, 12, 18);
  fib->txbx_bkd = {static_cast<uint32_t>(table->size()),
                   static_cast<uint32_t>(breaks.size())};
  table->insert(table->end(), breaks.begin(), breaks.end());
  AddLinkedAnchor(&*table, &*fib);
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 2U);
  ASSERT_EQ(parsed->textboxes[0].paragraphs.size(), 1U);
  ASSERT_EQ(parsed->textboxes[1].paragraphs.size(), 1U);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text +
                parsed->textboxes[1].paragraphs[0].runs[0].text,
            "Box sample line");

  // Duplicate CPs are malformed and must never be used as offsets.
  Put32(*table, fib->txbx_bkd.offset + 4, 0);
  auto invalid = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(invalid) << invalid.error().message;
  EXPECT_TRUE(invalid->textboxes.empty());
  ASSERT_FALSE(invalid->warnings.empty());
  EXPECT_TRUE(std::any_of(invalid->warnings.begin(), invalid->warnings.end(),
                          [](const std::string& warning) {
                            return warning ==
                                   "skipped textbox story PLC at FTXBXS 1";
                          }))
      << ::testing::PrintToString(invalid->warnings);
}

}  // namespace
