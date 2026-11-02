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

#ifndef TESTS_TEXTBOX_TEST_UTIL_H_
#define TESTS_TEXTBOX_TEST_UTIL_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "miniz.h"

#include "base/binary.h"
#include "doc/fib.h"
#include "doc/format/internal.h"

// Helpers shared by the textbox/bookmark/field suites, which patch
// WordDocument/Table streams in place and inspect the written package.
namespace doccraft::test::textbox {

inline void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; i++)
    bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}

inline void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
  bytes[at] = static_cast<uint8_t>(value);
  bytes[at + 1] = static_cast<uint8_t>(value >> 8);
}

inline std::string Part(const std::vector<uint8_t>& archive, const char* name) {
  mz_zip_archive zip = {};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
    return {};
  size_t size = 0;
  void* bytes = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
  std::string content;
  if (bytes) {
    content.assign(static_cast<const char*>(bytes), size);
    mz_free(bytes);
  }
  mz_zip_reader_end(&zip);
  return content;
}

// Turns the single FTXBXS anchor into one visible box with two linked ranges
// that start at the same CP, so the parser emits two textboxes sharing a
// story.
inline void AddLinkedAnchor(std::vector<uint8_t>* table, doc::Fib* fib) {
  const auto old = fib->spa_main;
  const uint32_t cp = base::U32(*table, old.offset);
  constexpr size_t kSpa = doc::detail::kSpaSize;
  std::vector<uint8_t> anchors(12 + 2 * kSpa);
  Put32(anchors, 0, cp);
  Put32(anchors, 4, cp + 1);
  Put32(anchors, 8, fib->ccp_text);
  std::copy_n(table->begin() + old.offset + 8, kSpa, anchors.begin() + 12);
  std::copy_n(table->begin() + old.offset + 8, kSpa,
              anchors.begin() + 12 + kSpa);
  Put32(anchors, 12 + kSpa, 500);
  fib->spa_main = {static_cast<uint32_t>(table->size()),
                   static_cast<uint32_t>(anchors.size())};
  table->insert(table->end(), anchors.begin(), anchors.end());
}

}  // namespace doccraft::test::textbox

#endif  // TESTS_TEXTBOX_TEST_UTIL_H_
