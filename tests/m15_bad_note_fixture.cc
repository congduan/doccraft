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
#include <utility>
#include <vector>

#include "cfb/cfb_reader.h"
#include "tests/cfb_test_util.h"

namespace {
uint16_t U16(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         static_cast<uint16_t>(bytes[offset + 1]) << 8;
}

void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3)
    return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = doccraft::cfb::CfbReader::Open(bytes);
  if (!cfb)
    return 3;
  auto word = cfb->ReadStream("WordDocument");
  if (!word)
    return 4;
  const size_t lw_count = 34 + static_cast<size_t>(U16(*word, 32)) * 2;
  const size_t pair_count =
      lw_count + 2 + static_cast<size_t>(U16(*word, lw_count)) * 4;
  const size_t pairs = pair_count + 2;
  // FIB pair 2 is PlcffndRef. Keep its length while moving its offset far
  // past the table stream, so the endnote and body remain independently valid.
  Put32(*word, pairs + 2 * 8, 0x7fffffff);
  doccraft::cfb::test::CfbBuilder builder;
  builder.AddStream("WordDocument", std::move(*word));
  for (const char* name : {"0Table", "1Table", "Data"}) {
    auto stream = cfb->ReadStream(name);
    if (stream)
      builder.AddStream(name, std::move(*stream));
  }
  const auto output = builder.Build();
  std::ofstream out(argv[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(output.data()), output.size());
  return out ? 0 : 5;
}
