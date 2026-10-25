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
namespace {

// Number of header/footer stories each section owns: even/odd/default header
// then even/odd/default footer ([MS-DOC] §2.8.2 PlcfHdd). Word reserves six
// leading CPs before the first section's stories, so section k starts at
// kStoryPreamble + 6 * k.
constexpr size_t kStoryCount = 6;
constexpr size_t kStoryPreamble = 6;

// One story's CP range (relative to the start of the header story region) is
// turned into paragraphs; the last paragraph mark is a guard.
std::vector<model::Paragraph> ReadStory(const std::vector<Char>& chars,
                                        const std::vector<FormatRange>& chpx,
                                        const std::vector<FormatRange>& papx,
                                        const std::vector<std::string>& fonts,
                                        uint64_t base, uint32_t first,
                                        uint32_t last) {
  std::vector<model::Paragraph> out;
  if (last <= first) {
    return out;
  }
  model::Paragraph para;
  for (const auto& ch : chars) {
    if (ch.cp < base + first || ch.cp >= base + last - 1) {
      continue;
    }
    if (ch.newline) {
      if (const auto* x = Find(papx, ch.fc)) {
        para.style = x->style;
        Walk(x->sprms, [&](uint16_t op, Bytes v) {
          ApplyPara(para, op, v);
        });
      }
      out.push_back(std::move(para));
      para = {};
    } else if (ch.text.size() != 1 ||
               static_cast<unsigned char>(ch.text[0]) >= 32) {
      model::Run r;
      if (const auto* x = Find(chpx, ch.fc)) {
        Walk(x->sprms, [&](uint16_t op, Bytes v) {
          ApplyRun(r, op, v, fonts);
        });
      }
      r.text = ch.text;
      if (!para.runs.empty() && SameRunFormatting(para.runs.back(), r)) {
        para.runs.back().text += ch.text;
      } else {
        para.runs.push_back(std::move(r));
      }
    }
  }
  if (!para.runs.empty()) {
    out.push_back(std::move(para));
  }
  bool has_text = false;
  for (const auto& paragraph : out) {
    for (const auto& run : paragraph.runs) {
      if (!run.text.empty()) {
        has_text = true;
      }
    }
  }
  if (!has_text) {
    out.clear();
  }
  return out;
}

}  // namespace

std::expected<std::vector<SectionHeaders>, Error> ReadHeaders(
    model::Document& doc, Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    size_t sections) {
  std::vector<SectionHeaders> out(sections);
  if (sections == 0 || !fib.ccp_hdd || !fib.headers.size) {
    return out;
  }
  if (!Valid(table, fib.headers) || fib.headers.size % 4 ||
      fib.headers.size < 4 * (kStoryCount + 1)) {
    return std::unexpected(Error{"invalid header story PLC"});
  }
  uint64_t base = static_cast<uint64_t>(fib.ccp_text) + fib.ccp_ftn;
  uint64_t end = base + fib.ccp_hdd;
  if (end > UINT32_MAX) {
    return std::unexpected(Error{"header CP range too large"});
  }
  auto chars = ReadChars(word, table, fib, end);
  if (!chars) {
    return std::unexpected(chars.error());
  }
  Bytes plc = table.subspan(fib.headers.offset, fib.headers.size);
  const size_t available = plc.size() / 4 - 1;
  for (size_t section = 0; section < sections; section++) {
    for (size_t story = 0; story < kStoryCount; story++) {
      const size_t index = kStoryPreamble + section * kStoryCount + story;
      if (index + 1 > available) {
        return std::unexpected(
            Error{"header story PLC is shorter than the "
                  "section count"});
      }
      const uint32_t first = U32(plc, 4 * index);
      const uint32_t last = U32(plc, 4 * (index + 1));
      if (last < first || last > fib.ccp_hdd) {
        return std::unexpected(Error{"header CP range outside story"});
      }
      out[section][story] =
          ReadStory(*chars, chpx, papx, doc.fonts, base, first, last);
    }
  }
  return out;
}
}  // namespace doccraft::doc::detail
