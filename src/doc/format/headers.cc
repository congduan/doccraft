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
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "doc/format/sprm.h"

namespace doccraft::doc::detail {
std::expected<void, Error> ReadHeaders(model::Document& doc, Bytes word,
                                       Bytes table, const Fib& fib,
                                       const std::vector<FormatRange>& chpx,
                                       const std::vector<FormatRange>& papx) {
  if (!fib.ccp_hdd || !fib.headers.size)
    return {};
  if (!Valid(table, fib.headers) || fib.headers.size % 4 ||
      fib.headers.size < 14 * 4)
    return std::unexpected(Error{"invalid header story PLC"});
  uint64_t base = static_cast<uint64_t>(fib.ccp_text) + fib.ccp_ftn;
  uint64_t end = base + fib.ccp_hdd;
  if (end > UINT32_MAX)
    return std::unexpected(Error{"header CP range too large"});
  auto chars = ReadChars(word, table, fib, end);
  if (!chars)
    return std::unexpected(chars.error());
  Bytes plc = table.subspan(fib.headers.offset, fib.headers.size);
  for (size_t i = 0; i < 6; i++) {
    uint32_t first = U32(plc, 4 * (6 + i)), last = U32(plc, 4 * (7 + i));
    if (last < first || last > fib.ccp_hdd)
      return std::unexpected(Error{"header CP range outside story"});
    if (first == last)
      continue;
    model::Paragraph para;
    // The last paragraph mark is a guard and is not part of the story.
    for (const auto& ch : *chars) {
      if (ch.cp < base + first || ch.cp >= base + last - 1)
        continue;
      if (ch.newline) {
        if (const auto* x = Find(papx, ch.fc)) {
          para.style = x->style;
          Walk(x->sprms, [&](uint16_t op, Bytes v) {
            ApplyPara(para, op, v);
          });
        }
        doc.section.headers[i].push_back(std::move(para));
        para = {};
      } else if (ch.text.size() != 1 ||
                 static_cast<unsigned char>(ch.text[0]) >= 32) {
        model::Run r;
        if (const auto* x = Find(chpx, ch.fc)) {
          Walk(x->sprms, [&](uint16_t op, Bytes v) {
            ApplyRun(r, op, v, doc.fonts);
          });
        }
        r.text = ch.text;
        para.runs.push_back(std::move(r));
      }
    }
    if (!para.runs.empty())
      doc.section.headers[i].push_back(std::move(para));
    bool has_text = false;
    for (const auto& paragraph : doc.section.headers[i])
      for (const auto& run : paragraph.runs)
        if (!run.text.empty())
          has_text = true;
    if (!has_text)
      doc.section.headers[i].clear();
  }
  doc.section.first_page_different = doc.section.first_page_different ||
                                     !doc.section.headers[4].empty() ||
                                     !doc.section.headers[5].empty();
  doc.section.odd_even_different =
      !doc.section.headers[0].empty() || !doc.section.headers[2].empty();
  return {};
}
}  // namespace doccraft::doc::detail
