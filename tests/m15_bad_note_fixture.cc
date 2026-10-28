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

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

#include "base/binary.h"
#include "cfb/cfb_reader.h"
#include "tests/cfb_test_util.h"

namespace {
void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4)
    return 2;
  const std::string_view mode = argc == 4 ? argv[3] : "note";
  if (mode != "note" && mode != "textbox" && mode != "textbox-story")
    return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = doccraft::cfb::CfbReader::Open(bytes);
  if (!cfb)
    return 3;
  auto word = cfb->ReadStream("WordDocument");
  if (!word)
    return 4;
  const size_t lw_count =
      34 + static_cast<size_t>(doccraft::base::U16(*word, 32)) * 2;
  const size_t pair_count =
      lw_count + 2 +
      static_cast<size_t>(doccraft::base::U16(*word, lw_count)) * 4;
  const size_t pairs = pair_count + 2;
  // Pair 2 is PlcffndRef; pair 75 is the body textbox Tbkd PLC.
  constexpr size_t kNotePair = 2;
  constexpr size_t kTextboxBreakPair = 75;
  constexpr uint32_t kInvalidPlcOffset = 0x7fffffff;
  const size_t pair = mode == "note" ? kNotePair : kTextboxBreakPair;
  if (pairs + pair * 8 + 8 > word->size())
    return 5;
  const uint32_t textbox_offset = doccraft::base::U32(*word, pairs + pair * 8);
  const bool use_one_table = doccraft::base::U16(*word, 10) & 0x0200;
  if (mode != "textbox-story") {
    // Keep the PLC length while moving its offset beyond the table stream.
    Put32(*word, pairs + pair * 8, kInvalidPlcOffset);
  }
  doccraft::cfb::test::CfbBuilder builder;
  builder.AddStream("WordDocument", std::move(*word));
  bool story_injected = false;
  for (const char* name : {"0Table", "1Table", "Data"}) {
    auto stream = cfb->ReadStream(name);
    if (stream) {
      if (mode == "textbox-story" &&
          std::string_view(name) == (use_one_table ? "1Table" : "0Table")) {
        if (textbox_offset + 8 > stream->size())
          return 5;
        // Collapse the first Tbkd CP boundary while leaving the PLC intact.
        Put32(*stream, textbox_offset + 4, 0);
        story_injected = true;
      }
      builder.AddStream(name, std::move(*stream));
    }
  }
  if (mode == "textbox-story" && !story_injected)
    return 5;
  const auto output = builder.Build();
  std::ofstream out(argv[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(output.data()), output.size());
  return out ? 0 : 5;
}
