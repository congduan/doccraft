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
#include <string>
#include <utility>
#include <vector>

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "tests/cfb_test_util.h"

namespace {
void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 4)
    return 2;
  const std::string mode = argv[1];
  if (mode != "short-piece" && mode != "missing-pool")
    return 2;
  std::ifstream input(argv[2], std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = doccraft::cfb::CfbReader::Open(bytes);
  if (!cfb)
    return 3;
  auto word = cfb->ReadStream("WordDocument");
  if (!word)
    return 4;
  auto fib = doccraft::doc::ParseFib(*word);
  if (!fib)
    return 5;
  auto table = cfb->ReadStream(fib->table_stream());
  if (!table)
    return 6;
  if (mode == "short-piece") {
    if (fib->complex || fib->ccp_text < 2 || fib->lcb_clx < 21)
      return 7;
    const size_t clx = fib->fc_clx;
    if (clx + fib->lcb_clx > table->size() || (*table)[clx] != 2)
      return 8;
    // One-piece Pcdt: 0x02, length 16, CP[0], CP[end], PCD.
    Put32(*table, clx + 9, fib->ccp_text - 1);
  }
  doccraft::cfb::test::CfbBuilder builder;
  builder.AddStream("WordDocument", std::move(*word));
  builder.AddStream(fib->table_stream(), std::move(*table));
  auto data = cfb->ReadStream("Data");
  if (data)
    builder.AddStream("Data", std::move(*data));
  const auto output = builder.Build();
  std::ofstream out(argv[3], std::ios::binary);
  out.write(reinterpret_cast<const char*>(output.data()), output.size());
  return out ? 0 : 9;
}
