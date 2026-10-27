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
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "doc/format/formatted_helpers.h"
#include "doc/format/sprm.h"
#include "doc/format/sprm_codes.h"

namespace doccraft::doc::detail {
namespace {

// Number of header/footer stories each section owns: even/odd/default header
// then even/odd/default footer ([MS-DOC] §2.8.2 PlcfHdd). Word reserves six
// leading CPs before the first section's stories, so section k starts at
// kStoryPreamble + 6 * k.
constexpr size_t kStoryCount = 6;
constexpr size_t kStoryPreamble = 6;

// grffldEnd flags of a field-end record ([MS-DOC] grffldEnd).
constexpr uint8_t kFldResultsDirty = 0x04;
constexpr uint8_t kFldLocked = 0x10;

}  // namespace

std::expected<std::map<uint32_t, uint8_t>, Error> ReadStoryFieldStatus(
    Bytes table, Fib::Range r, uint64_t base, uint32_t story_length) {
  std::map<uint32_t, uint8_t> status;
  if (!r.size) {
    return status;
  }
  if (!Valid(table, r) || r.size < 4 || (r.size - 4) % 6)
    return std::unexpected(Error{"invalid field PLC"});
  Bytes plc = table.subspan(r.offset, r.size);
  const size_t count = (plc.size() - 4) / 6;
  const size_t fld_offset = 4 * (count + 1);
  std::vector<uint32_t> starts;
  uint32_t previous = 0;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t cp = U32(plc, 4 * i);
    // Fld.fldch packs the 5-bit character code with three marker flags.
    const uint8_t kind = plc[fld_offset + 2 * i] & 0x1F;
    if ((i && cp <= previous) || cp >= story_length ||
        (kind != 0x13 && kind != 0x14 && kind != 0x15))
      return std::unexpected(Error{"invalid field PLC entry"});
    previous = cp;
    if (kind == 0x13) {
      starts.push_back(static_cast<uint32_t>(base + cp));
    } else if (kind == 0x15 && !starts.empty()) {
      status.emplace(starts.back(), plc[fld_offset + 2 * i + 1]);
      starts.pop_back();
    }
  }
  if (U32(plc, 4 * count) < previous)
    return std::unexpected(Error{"invalid field PLC guard"});
  return status;
}

// One story's CP range (relative to the start of its story region) is turned
// into paragraphs; the last paragraph mark is a guard.
std::vector<model::Paragraph> ReadStoryParagraphs(
    const std::vector<Char>& chars, const std::vector<FormatRange>& chpx,
    const std::vector<FormatRange>& papx, const std::vector<std::string>& fonts,
    const std::vector<std::vector<uint8_t>>& prcs, uint64_t base,
    uint32_t first, uint32_t last,
    const std::map<uint32_t, uint8_t>* field_status,
    const BookmarkMarks* bookmarks) {
  std::vector<model::Paragraph> out;
  if (last <= first) {
    return out;
  }
  model::Paragraph para;
  // Field state mirrors the main story: \x13 opens a frame, \x14 closes its
  // instruction, \x15 closes the field. The hierarchy comes from the text, so
  // a story without a usable Plcfld only loses its lock/dirty flags.
  std::vector<FieldFrame> fields;
  std::string hyperlink;
  bool page_field = false;
  auto walk_prm = [&](uint16_t prm, auto fn) {
    if (!prm)
      return;
    if (prm & 1) {
      if ((prm >> 1) < prcs.size())
        Walk(prcs[prm >> 1], fn);
    } else {
      const uint16_t op = Prm0Opcode((prm >> 1) & 0x7F);
      if (op) {
        const uint8_t value = static_cast<uint8_t>(prm >> 8);
        fn(op, Bytes(&value, 1));
      }
    }
  };
  // The hyperlink/page target of a field applies to the runs after its
  // separator; pop restores the enclosing field's target.
  auto sync_field_state = [&]() {
    hyperlink.clear();
    page_field = false;
    for (auto frame = fields.rbegin(); frame != fields.rend(); ++frame) {
      if (frame->in_instruction)
        continue;
      hyperlink = frame->hyperlink;
      page_field = frame->page;
      break;
    }
  };
  auto emit_field_char = [&](int kind) {
    model::Run marker;
    marker.field_char = kind;
    if (!fields.empty()) {
      marker.field_locked = fields.back().locked;
      marker.field_dirty = fields.back().dirty;
    }
    para.runs.push_back(std::move(marker));
  };
  auto emit_instruction = [&](const std::string& text) {
    if (text.empty())
      return;
    model::Run marker;
    marker.instr_text = text;
    para.runs.push_back(std::move(marker));
  };
  auto start = std::lower_bound(chars.begin(), chars.end(), base + first,
                                [](const Char& ch, uint64_t cp) {
                                  return ch.cp < cp;
                                });
  for (auto it = start; it != chars.end() && it->cp < base + last - 1; ++it) {
    const Char& ch = *it;
    if (bookmarks) {
      if (auto found = bookmarks->marks.find(ch.cp);
          found != bookmarks->marks.end()) {
        for (const auto& mark : found->second) {
          model::Run marker;
          marker.bookmark_id = mark.id;
          marker.bookmark_name = mark.name;
          marker.bookmark_start = mark.start;
          para.runs.push_back(std::move(marker));
        }
      }
    }
    const std::string_view text = ch.text();
    const char control = text.size() == 1 ? text[0] : '\0';
    if (control == '\x13') {
      fields.push_back({});
      if (field_status) {
        if (auto found = field_status->find(ch.cp);
            found != field_status->end()) {
          fields.back().locked = (found->second & kFldLocked) != 0;
          fields.back().dirty = (found->second & kFldResultsDirty) != 0;
        }
      }
      continue;
    }
    if (!fields.empty() && fields.back().in_instruction && control == '\x14') {
      FieldFrame& frame = fields.back();
      frame.in_instruction = false;
      ApplyFieldInstruction(frame.instruction, &frame.hyperlink, &frame.page);
      // Only a top-level HYPERLINK/PAGE field keeps its dedicated mapping;
      // nested fields and everything else become generic complex fields.
      if (fields.size() == 1 && !frame.hyperlink.empty() && !frame.locked &&
          !frame.dirty)
        frame.mode = FieldMode::kHyperlink;
      else if (fields.size() == 1 && frame.page)
        frame.mode = FieldMode::kPage;
      else
        frame.mode = FieldMode::kGeneric;
      if (frame.mode == FieldMode::kGeneric) {
        emit_field_char(model::kFieldCharBegin);
        emit_instruction(frame.instruction);
        emit_field_char(model::kFieldCharSeparate);
      }
      sync_field_state();
      continue;
    }
    if (!fields.empty() && control == '\x15') {
      // A field that closes before its separator is malformed: keep its
      // instruction as an unmatched begin/end pair instead of dropping it.
      if (fields.back().mode == FieldMode::kPending) {
        emit_field_char(model::kFieldCharBegin);
        emit_instruction(fields.back().instruction);
      }
      if (fields.back().mode != FieldMode::kHyperlink &&
          fields.back().mode != FieldMode::kPage)
        emit_field_char(model::kFieldCharEnd);
      fields.pop_back();
      sync_field_state();
      continue;
    }
    if (!fields.empty() && fields.back().in_instruction) {
      // Binary object markers can leak into fast-saved field instructions.
      // XML 1.0 cannot represent them, and story readers have no warning
      // channel, so they are dropped as the plain text path always did.
      if (control && static_cast<unsigned char>(control) < 32 &&
          control != '\t' && control != '\n' && control != '\r') {
        continue;
      }
      fields.back().instruction += text;
      continue;
    }
    if (ch.newline) {
      if (const auto* x = Find(papx, ch.fc)) {
        para.style = x->style;
        Walk(x->sprms, [&](uint16_t op, Bytes v) {
          ApplyPara(para, op, v);
        });
      }
      walk_prm(ch.prm, [&](uint16_t op, Bytes v) {
        if ((op >> 10 & 7) == 1)
          ApplyPara(para, op, v);
      });
      out.push_back(std::move(para));
      para = {};
    } else if (text.size() != 1 || static_cast<unsigned char>(text[0]) >= 32) {
      model::Run r;
      if (const auto* x = Find(chpx, ch.fc)) {
        Walk(x->sprms, [&](uint16_t op, Bytes v) {
          ApplyRun(r, op, v, fonts);
        });
      }
      walk_prm(ch.prm, [&](uint16_t op, Bytes v) {
        if ((op >> 10 & 7) != 2)
          return;
        if (op == kSprmCPlain)
          r = {};
        else
          ApplyRun(r, op, v, fonts);
      });
      r.hyperlink = hyperlink;
      r.page_field = page_field;
      if (page_field && !fields.empty()) {
        r.field_locked = fields.back().locked;
        r.field_dirty = fields.back().dirty;
      }
      r.text = text;
      if (!para.runs.empty() && SameRunFormatting(para.runs.back(), r)) {
        para.runs.back().text += text;
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

std::expected<std::vector<SectionHeaders>, Error> ReadHeaders(
    model::Document& doc, Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    size_t sections, const BookmarkMarks& bookmarks) {
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
  std::vector<std::vector<uint8_t>> prcs;
  auto chars = ReadChars(word, table, fib, end, base, &prcs);
  if (!chars) {
    return std::unexpected(chars.error());
  }
  Bytes plc = table.subspan(fib.headers.offset, fib.headers.size);
  const size_t available = plc.size() / 4 - 1;
  // PlcfFldHdr CPs share the header region's CP space, so one map serves all
  // sections; each story read only looks up begins inside its own range.
  std::map<uint32_t, uint8_t> field_status;
  if (auto status =
          ReadStoryFieldStatus(table, fib.fields_hdr, base, fib.ccp_hdd)) {
    field_status = std::move(*status);
  } else {
    doc.warnings.push_back("skipped header field PLC: " +
                           status.error().message);
  }
  // Standard bookmark PLCs use absolute document CPs. Keep only pairs
  // entirely inside the header region; an invalid pair does not remove text.
  BookmarkMarks header_bookmarks;
  std::map<int, uint32_t> starts;
  std::map<int, uint32_t> ends;
  for (const auto& [cp, marks] : bookmarks.marks) {
    for (const auto& mark : marks) {
      if (mark.start && cp >= base && cp < end)
        starts.emplace(mark.id, cp);
      if (!mark.start)
        ends.emplace(mark.id, cp);
    }
  }
  bool invalid_bookmark = false;
  for (const auto& [id, cp] : starts) {
    auto found = ends.find(id);
    if (found == ends.end() || found->second < cp || found->second >= end) {
      invalid_bookmark = true;
      break;
    }
  }
  if (invalid_bookmark) {
    doc.warnings.push_back("skipped header bookmark PLC: invalid range");
  } else {
    for (const auto& [cp, marks] : bookmarks.marks) {
      if (cp < base || cp >= end)
        continue;
      for (const auto& mark : marks) {
        if (starts.contains(mark.id))
          header_bookmarks.marks[cp].push_back(mark);
      }
    }
  }
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
          ReadStoryParagraphs(*chars, chpx, papx, doc.fonts, prcs, base, first,
                              last, &field_status, &header_bookmarks);
    }
  }
  return out;
}
}  // namespace doccraft::doc::detail
