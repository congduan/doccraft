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

#include "base/binary.h"
#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "tests/cfb_test_util.h"

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
  auto fib = doccraft::doc::ParseFib(*word);
  if (!fib)
    return 5;
  auto table = cfb->ReadStream(fib->table_stream());
  if (!table || fib->papx_bte.size < 12 ||
      fib->papx_bte.offset > table->size() ||
      fib->papx_bte.size > table->size() - fib->papx_bte.offset)
    return 6;
  const size_t pages = (fib->papx_bte.size - 4) / 8;
  const size_t pnoffset = fib->papx_bte.offset + 4 * (pages + 1);
  const size_t page = doccraft::base::U32(*table, pnoffset) & 0x3FFFFF;
  if (page >= word->size() / 512)
    return 7;
  (*word)[page * 512 + 511] = 255;  // Invalid PAPX FKP run count.
  doccraft::cfb::test::CfbBuilder builder;
  builder.AddStream("WordDocument", std::move(*word));
  builder.AddStream(fib->table_stream(), std::move(*table));
  auto data = cfb->ReadStream("Data");
  if (data)
    builder.AddStream("Data", std::move(*data));
  const auto output = builder.Build();
  std::ofstream out(argv[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(output.data()), output.size());
  return out ? 0 : 8;
}
