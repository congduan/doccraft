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

// Repeat a valid structured DOC body, its WordDocument bytes, and its FKPs.
// The original auxiliary stories stay after the repeated main story in CLX.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "base/binary.h"
#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "tests/cfb_test_util.h"

namespace {
using doccraft::base::Bytes;
using doccraft::base::U16;
using doccraft::base::U32;
constexpr size_t kSector = 512;
constexpr uint32_t kCompressed = 0x40000000;
constexpr uint32_t kFcMask = 0x3FFFFFFF;
constexpr size_t kFibBase = 34;
constexpr size_t kChpxPair = 12;
constexpr size_t kPapxPair = 13;
constexpr size_t kClxPair = 33;

void Put32(std::vector<uint8_t>* v, size_t p, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    (*v)[p + i] = (value >> (8 * i)) & 255;
}
void Add32(std::vector<uint8_t>* v, uint32_t value) {
  size_t p = v->size();
  v->resize(p + 4);
  Put32(v, p, value);
}
size_t PairStart(Bytes w) {
  size_t lw_count = kFibBase + 2 * U16(w, 32);
  return lw_count + 2 + 4 * U16(w, lw_count) + 2;
}
void SetPair(std::vector<uint8_t>* word, size_t pair_start, size_t index,
             uint32_t offset, uint32_t size) {
  Put32(word, pair_start + 8 * index, offset);
  Put32(word, pair_start + 8 * index + 4, size);
}
std::vector<uint8_t> RepeatBte(std::vector<uint8_t>* word, Bytes table,
                               doccraft::doc::Fib::Range range, size_t stride,
                               int repeats) {
  Bytes old = table.subspan(range.offset, range.size);
  size_t pages = (range.size - 4) / 8;
  std::vector<uint8_t> out;
  out.reserve(4 + 8 * pages * repeats);
  for (int copy = 0; copy < repeats; ++copy) {
    uint32_t shift = static_cast<uint32_t>(stride * copy);
    for (size_t j = 0; j < pages; ++j) {
      Add32(&out, U32(old, 4 * j) + shift);
      size_t pn = U32(old, 4 * (pages + 1) + 4 * j) & 0x3FFFFF;
      size_t page = pn * kSector + stride * copy;
      uint8_t count = (*word)[page + kSector - 1];
      for (size_t run = 0; run <= count; ++run)
        Put32(word, page + 4 * run, U32(*word, page + 4 * run) + shift);
    }
  }
  Add32(&out,
        U32(old, 4 * pages) + static_cast<uint32_t>(stride * (repeats - 1)));
  for (int copy = 0; copy < repeats; ++copy) {
    for (size_t j = 0; j < pages; ++j) {
      uint32_t pn = U32(old, 4 * (pages + 1) + 4 * j);
      Add32(&out, pn + static_cast<uint32_t>(stride * copy / kSector));
    }
  }
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3)
    return 2;
  std::ifstream in(argv[1], std::ios::binary);
  if (!in)
    return 1;
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  auto cfb = doccraft::cfb::CfbReader::Open(bytes);
  if (!cfb)
    return 1;
  auto word_result = cfb->ReadStream("WordDocument");
  if (!word_result)
    return 1;
  auto fib = doccraft::doc::ParseFib(*word_result);
  if (!fib)
    return 1;
  auto table_result = cfb->ReadStream(fib->table_stream());
  if (!table_result)
    return 1;
  auto word = std::move(*word_result);
  auto table = std::move(*table_result);
  constexpr int kRepeats = 5;
  const size_t stride = (word.size() + kSector - 1) / kSector * kSector;
  std::vector<uint8_t> original = word;
  word.resize(stride * kRepeats);
  for (int copy = 1; copy < kRepeats; ++copy)
    std::copy(original.begin(), original.end(), word.begin() + stride * copy);
  Bytes clx = Bytes(table).subspan(fib->fc_clx, fib->lcb_clx);
  size_t p = 0;
  while (p < clx.size() && clx[p] == 1)
    p += 3 + U16(clx, p + 1);
  if (p + 5 > clx.size() || clx[p] != 2)
    return 1;
  uint32_t nbytes = U32(clx, p + 1);
  p += 5;
  if (nbytes != 16 || U32(clx, p) != 0 || U32(clx, p + 4) < fib->ccp_text)
    return 1;
  uint32_t total_cp = U32(clx, p + 4);
  uint32_t raw = U32(clx, p + 10);
  uint32_t first_fc = (raw & kFcMask) / ((raw & kCompressed) ? 2 : 1);
  uint32_t main_bytes = fib->ccp_text * ((raw & kCompressed) ? 1 : 2);
  if (first_fc + main_bytes > original.size())
    return 1;
  auto chpx = RepeatBte(&word, table, fib->chpx_bte, stride, kRepeats);
  auto papx = RepeatBte(&word, table, fib->papx_bte, stride, kRepeats);
  size_t pair_start = PairStart(word);
  Put32(&word, kFibBase + 2 * U16(word, 32) + 2 + 12, fib->ccp_text * kRepeats);
  auto append_pair = [&](size_t index, const std::vector<uint8_t>& value) {
    size_t offset = table.size();
    table.insert(table.end(), value.begin(), value.end());
    SetPair(&word, pair_start, index, offset, value.size());
  };
  append_pair(kChpxPair, chpx);
  append_pair(kPapxPair, papx);
  std::vector<uint8_t> new_clx;
  new_clx.push_back(2);
  Add32(&new_clx, 4 + 12 * (kRepeats + 1));
  for (int copy = 0; copy <= kRepeats; ++copy)
    Add32(&new_clx, fib->ccp_text * copy);
  Add32(&new_clx, fib->ccp_text * kRepeats + total_cp - fib->ccp_text);
  for (int copy = 0; copy < kRepeats; ++copy) {
    new_clx.insert(new_clx.end(), clx.begin() + p + 8, clx.begin() + p + 16);
    Put32(&new_clx, 5 + 4 * (kRepeats + 2) + 8 * copy + 2,
          raw + static_cast<uint32_t>(stride * copy) *
                    ((raw & kCompressed) ? 2 : 1));
  }
  new_clx.insert(new_clx.end(), clx.begin() + p + 8, clx.begin() + p + 16);
  Put32(&new_clx, 5 + 4 * (kRepeats + 2) + 8 * kRepeats + 2,
        raw + main_bytes * ((raw & kCompressed) ? 2 : 1));
  append_pair(kClxPair, new_clx);
  doccraft::cfb::test::CfbBuilder builder;
  builder.AddStream("WordDocument", std::move(word));
  builder.AddStream(fib->table_stream(), std::move(table));
  auto output = builder.Build();
  std::ofstream out(argv[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(output.data()), output.size());
  return out ? 0 : 1;
}
