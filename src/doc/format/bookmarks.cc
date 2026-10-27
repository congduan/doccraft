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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "doc/format/sprm.h"

namespace doccraft::doc::detail {
namespace {

// SttbfBkmk is an extended STTB ([MS-DOC] §2.9.266): fExtend(2), cData(2),
// cbExtra(2), then per entry cchData(2) + cbExtra bytes + UTF-16 data.
std::vector<std::string> ReadBookmarkNames(Bytes table, Fib::Range r) {
  std::vector<std::string> out;
  if (!r.size || !Valid(table, r) || r.size < 6) {
    return out;
  }
  Bytes b = table.subspan(r.offset, r.size);
  const uint16_t count = U16(b, 2), extra = U16(b, 4);
  size_t p = 6;
  for (size_t i = 0; i < count; i++) {
    if (p + 2 + extra > b.size()) {
      break;
    }
    const size_t cch = U16(b, p);
    p += 2 + extra;
    if (p + cch * 2 > b.size()) {
      break;
    }
    std::string name;
    for (size_t k = 0; k < cch; k++) {
      AppendUtf8(name, U16(b, p + 2 * k));
    }
    p += cch * 2;
    out.push_back(std::move(name));
  }
  return out;
}

}  // namespace

std::expected<BookmarkMarks, Error> ReadBookmarks(Bytes table, const Fib& fib) {
  BookmarkMarks out;
  if (!fib.bkf.size) {
    return out;
  }
  // PlcfBkf: (n + 1) CPs then n four-byte FBKF records; ibkl is the
  // first two bytes, followed by a two-byte BKC.
  if (!Valid(table, fib.bkf) || fib.bkf.size < 8 || (fib.bkf.size - 4) % 8) {
    return std::unexpected(Error{"invalid bookmark start PLC"});
  }
  const size_t count = (fib.bkf.size - 4) / 8;
  if (!fib.bkl.size) {
    return out;
  }
  // PlcfBkl is CPs only.
  if (!Valid(table, fib.bkl) || fib.bkl.size < 8 || (fib.bkl.size % 4)) {
    return std::unexpected(Error{"invalid bookmark end PLC"});
  }
  Bytes start = table.subspan(fib.bkf.offset, fib.bkf.size);
  Bytes end = table.subspan(fib.bkl.offset, fib.bkl.size);
  const size_t end_count = end.size() / 4 - 1;
  const std::vector<std::string> names =
      ReadBookmarkNames(table, fib.sttb_bkmk);
  for (size_t i = 0; i < count; i++) {
    const size_t bkf_at = 4 * (count + 1) + 4 * i;
    const uint16_t ibkl = U16(start, bkf_at);
    if (ibkl >= end_count) {
      continue;
    }
    const int id = static_cast<int>(i);
    const std::string name =
        i < names.size() ? names[i] : "Bookmark" + std::to_string(i + 1);
    const uint32_t begin = U32(start, 4 * i);
    const uint32_t finish = U32(end, 4 * ibkl);
    out.marks[begin].push_back({id, name, true});
    out.marks[finish].push_back({id, name, false});
  }
  return out;
}

BookmarkMarks FilterStoryBookmarks(
    const BookmarkMarks& bookmarks,
    const std::vector<std::pair<uint64_t, uint64_t>>& ranges, const char* story,
    std::vector<std::string>* warnings, bool last_cp_is_guard) {
  std::map<int, uint32_t> starts;
  std::map<int, uint32_t> ends;
  for (const auto& [cp, marks] : bookmarks.marks) {
    for (const auto& mark : marks) {
      if (mark.start)
        starts.emplace(mark.id, cp);
      else
        ends.emplace(mark.id, cp);
    }
  }
  std::set<int> valid_ids;
  bool invalid = false;
  for (const auto& [id, begin] : starts) {
    for (const auto& [first, last] : ranges) {
      if (last <= first || begin < first ||
          begin >= last - (last_cp_is_guard ? 1 : 0))
        continue;
      auto end = ends.find(id);
      if (end != ends.end() && end->second >= begin && end->second < last) {
        valid_ids.insert(id);
      } else {
        invalid = true;
      }
      break;
    }
  }
  if (invalid && warnings)
    warnings->push_back(std::string("skipped ") + story +
                        " bookmark PLC: invalid range");
  BookmarkMarks out;
  for (const auto& [cp, marks] : bookmarks.marks) {
    for (const auto& mark : marks) {
      if (valid_ids.contains(mark.id))
        out.marks[cp].push_back(mark);
    }
  }
  return out;
}
}  // namespace doccraft::doc::detail
