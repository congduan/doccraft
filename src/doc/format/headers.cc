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
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
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

// Word 6/95 PlcfHdd stores only the stories a section actually owns. The bit
// order below is the order the stories appear in the PLC (LibreOffice
// ww8scan.hxx nsHdFtFlags / WW8PLCF_HdFt::GetTextPos).
constexpr uint8_t kLegacyStoryFlags[kStoryCount] = {0x01, 0x02, 0x04,
                                                    0x08, 0x10, 0x20};
// The final CP of a Word 6/95 PlcfHdd may sit up to two CPs past ccpHdd: those
// CPs are the last story's paragraph marks, outside the counted text.
constexpr uint32_t kLegacyHddGuard = 2;
// Without a usable section table the story bits fall back to the even/odd
// header and footer stories, so their PlcHdd entries are still consumed.
constexpr uint8_t kLegacyDefaultStoryFlags = 0x0F;
// [MS-DOC] §2.9.210: cbGrpprl is signed and at most 0x3FA2.
constexpr size_t kMaxSepxSize = 0x3FA2;

struct LegacyStoryFlags {
  uint8_t stories = 0;
  bool title_page = false;
};

// Applies the one-byte Word 6/95 sprm table to one SEPX and returns the
// section's grpfIhdt and title-page flags.
std::expected<LegacyStoryFlags, Error> ReadLegacySepx(Bytes raw) {
  LegacyStoryFlags flags;
  for (size_t p = 0; p < raw.size();) {
    const uint8_t code = raw[p++];
    if (code == 0)
      continue;  // Word 6 separator padding.
    const LegacySprmSpec* spec = FindLegacySprmSpec(code);
    if (!spec)
      return std::unexpected(Error{"unsupported Word 6/95 SEPX sprm"});
    size_t size = 0;
    switch (spec->width) {
      case LegacySprmWidth::kFix:
        size = spec->size;
        break;
      case LegacySprmWidth::kVar:
        if (p >= raw.size())
          return std::unexpected(Error{"truncated Word 6/95 SEPX"});
        size = static_cast<size_t>(raw[p]) + 1 + spec->size;
        break;
      case LegacySprmWidth::kVar2: {
        if (raw.size() - p < 2)
          return std::unexpected(Error{"truncated Word 6/95 SEPX"});
        const size_t count = U16(raw, p);
        if (!count)
          return std::unexpected(Error{"truncated Word 6/95 SEPX"});
        size = count + 1;
        break;
      }
    }
    if (size > raw.size() - p)
      return std::unexpected(Error{"truncated Word 6/95 SEPX"});
    if (code == kLegacySprmSGprfIhdt && size) {
      flags.stories = raw[p];
    } else if (code == kLegacySprmSFTitlePage && size) {
      flags.title_page = raw[p] != 0;
    }
    p += size;
  }
  return flags;
}

// Reads the section story flags in document order. A damaged or missing
// Plcfsed yields no stories; the caller turns that into a warning.
std::expected<std::vector<LegacyStoryFlags>, Error> ReadLegacyStoryFlags(
    Bytes word, const Fib& fib, size_t sections,
    std::vector<std::string>* warnings) {
  std::vector<LegacyStoryFlags> out(sections);
  if (!sections)
    return out;
  if (!fib.sed.size) {
    // A missing section table would otherwise drop every header and footer
    // without a trace; fall back to the even/odd stories and say so once.
    if (fib.ccp_hdd && warnings)
      warnings->push_back(
          "legacy section table unavailable; header/footer story bits "
          "default to odd+even");
    for (LegacyStoryFlags& flags : out)
      flags.stories = kLegacyDefaultStoryFlags;
    return out;
  }
  if (!Valid(word, fib.sed) || fib.sed.size < 20 || (fib.sed.size - 4) % 16)
    return std::unexpected(Error{"invalid Word 6/95 SED PLC"});
  Bytes plc = word.subspan(fib.sed.offset, fib.sed.size);
  const size_t count = (plc.size() - 4) / 16;
  if (count != sections)
    return std::unexpected(Error{"header section count mismatch"});
  for (size_t i = 0; i < count; ++i) {
    const size_t pos = 4 * (count + 1) + 12 * i;
    uint32_t fc = U32(plc, pos + 2);
    if (fc == kUndefinedU32)
      continue;
    // A few Word 6 writers store fcSepx one byte past the length prefix; when
    // that frame is impossible, retry the preceding byte before giving up.
    auto frame = [&](uint32_t at) -> std::expected<Bytes, Error> {
      if (at > word.size() || word.size() - at < 2)
        return std::unexpected(Error{"Word 6/95 SEPX outside WordDocument"});
      const size_t len = U16(word, at);
      if (len > kMaxSepxSize || len > word.size() - at - 2)
        return std::unexpected(Error{"truncated Word 6/95 SEPX"});
      return word.subspan(at + 2, len);
    };
    auto raw = frame(fc);
    if (!raw && fc > 0) {
      auto shifted = frame(fc - 1);
      if (shifted) {
        raw = shifted;
        if (warnings)
          warnings->push_back(
              "corrected Word 6/95 SEPX offset: stored one byte too high");
      }
    }
    if (!raw)
      return std::unexpected(raw.error());
    auto flags = ReadLegacySepx(*raw);
    if (!flags)
      return std::unexpected(flags.error());
    out[i] = *flags;
  }
  return out;
}

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
    // Fld.fldch has a 5-bit character code; its high three bits are reserved.
    const uint8_t kind = plc[fld_offset + 2 * i] & 0x1F;
    // A real entry at the story's ccp is invalid, even though a guard may
    // point there.
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
  // The guard may equal the last real CP.
  if (U32(plc, 4 * count) < previous)
    return std::unexpected(Error{"invalid field PLC guard"});
  return status;
}

// One story's CP range (relative to the start of its story region) is turned
// into paragraphs; the last paragraph mark may be a guard.
std::vector<model::Paragraph> ReadStoryParagraphs(
    const std::vector<Char>& chars, const std::vector<FormatRange>& chpx,
    const std::vector<FormatRange>& papx, const std::vector<std::string>& fonts,
    const std::vector<std::vector<uint8_t>>& prcs, uint64_t base,
    uint32_t first, uint32_t last,
    const std::map<uint32_t, uint8_t>* field_status,
    const BookmarkMarks* bookmarks, bool last_cp_is_guard) {
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
  // Only the story's final paragraph mark is a guard. At an interior textbox
  // break, the next segment emits marks at this CP before its first character.
  if (bookmarks && last_cp_is_guard) {
    if (auto found = bookmarks->marks.find(base + last - 1);
        found != bookmarks->marks.end()) {
      auto* final_para =
          para.runs.empty() && !out.empty() ? &out.back() : &para;
      for (const auto& mark : found->second) {
        if (mark.start)
          continue;
        model::Run marker;
        marker.bookmark_id = mark.id;
        final_para->runs.push_back(std::move(marker));
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
  uint64_t base = static_cast<uint64_t>(fib.ccp_text) + fib.ccp_ftn;
  uint64_t end = base + fib.ccp_hdd;
  if (end > UINT32_MAX) {
    return std::unexpected(Error{"header CP range too large"});
  }
  // Read the independent field PLC before validating the story PLC. If both
  // are damaged, callers still receive each warning.
  std::map<uint32_t, uint8_t> field_status;
  if (auto status =
          ReadStoryFieldStatus(table, fib.fields_hdr, base, fib.ccp_hdd)) {
    field_status = std::move(*status);
  } else {
    doc.warnings.push_back("skipped header field PLC: " +
                           status.error().message);
  }
  // Word 6/95 keeps PlcfHdd in WordDocument and stores only the stories a
  // section owns; Word 97+ uses the table stream with a six-CP preamble.
  Bytes plc_source = fib.legacy ? word : table;
  const size_t minimum_plc = fib.legacy ? 4 * 2 : 4 * (kStoryCount + 1);
  if (!Valid(plc_source, fib.headers) || fib.headers.size % 4 ||
      fib.headers.size < minimum_plc) {
    return std::unexpected(Error{"invalid header story PLC"});
  }
  std::vector<std::vector<uint8_t>> prcs;
  auto chars = ReadChars(word, table, fib, end, base, &prcs);
  if (!chars) {
    return std::unexpected(chars.error());
  }
  Bytes plc = plc_source.subspan(fib.headers.offset, fib.headers.size);
  const size_t available = plc.size() / 4 - 1;
  if (fib.legacy) {
    auto flags = ReadLegacyStoryFlags(word, fib, sections, &doc.warnings);
    if (!flags)
      return std::unexpected(flags.error());
    size_t index = 0;
    bool facing_pages = false;
    if (fib.dop.size && Valid(word, fib.dop) && fib.dop.size >= 2) {
      const uint16_t dop = U16(word, fib.dop.offset);
      facing_pages = (dop & 0x0001) != 0;
      // DOP.grpfIhdt reserves one leading story per set bit (separators and
      // notices); LibreOffice skips them the same way (WW8PLCF_HdFt).
      index = std::popcount(static_cast<unsigned>(dop >> 8) & 0x3Fu);
    } else {
      doc.warnings.push_back(
          "legacy document properties truncated; header/footer story skip "
          "count defaults to 0");
    }
    for (size_t section = 0; section < sections; ++section) {
      for (size_t story = 0; story < kStoryCount; ++story) {
        const uint8_t bit = kLegacyStoryFlags[story];
        if (!((*flags)[section].stories & bit))
          continue;
        if (index >= available) {
          return std::unexpected(
              Error{"header story PLC is shorter than the section count"});
        }
        const uint32_t first = U32(plc, 4 * index);
        const uint32_t last = U32(plc, 4 * (index + 1));
        ++index;
        if (last < first || last > fib.ccp_hdd + kLegacyHddGuard) {
          return std::unexpected(Error{"header CP range outside story"});
        }
        // Even stories are used only when the document enables facing pages;
        // first-page stories only when the section enables a title page.
        const bool enabled = (story == 0 || story == 2) ? facing_pages
                             : story >= 4 ? (*flags)[section].title_page
                                          : true;
        if (enabled) {
          out[section][story] =
              ReadStoryParagraphs(*chars, chpx, papx, doc.fonts, prcs, base,
                                  first, last, &field_status, nullptr);
        }
      }
    }
    return out;
  }
  std::vector<std::pair<uint64_t, uint64_t>> story_ranges;
  for (size_t section = 0; section < sections; ++section) {
    for (size_t story = 0; story < kStoryCount; ++story) {
      const size_t index = kStoryPreamble + section * kStoryCount + story;
      if (index + 1 > available) {
        return std::unexpected(
            Error{"header story PLC is shorter than the section count"});
      }
      const uint32_t first = U32(plc, 4 * index);
      const uint32_t last = U32(plc, 4 * (index + 1));
      if (last < first || last > fib.ccp_hdd) {
        return std::unexpected(Error{"header CP range outside story"});
      }
      story_ranges.emplace_back(base + first, base + last);
    }
  }
  // PlcfFldHdr CPs share the header region's CP space, so one map serves all
  // sections; each story read only looks up begins inside its own range.
  // Standard bookmark PLCs use absolute document CPs. Keep only pairs whose
  // starts are text CPs and whose ends are in the same story, including its
  // guard CP. An invalid pair does not remove text or other valid bookmarks.
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
  std::set<int> valid_ids;
  for (const auto& [id, cp] : starts) {
    auto found = ends.find(id);
    bool same_story = false;
    if (found != ends.end() && found->second >= cp && found->second < end) {
      // The ordered, disjoint ranges are checked while constructed above
      // (last < first / last > fib.ccp_hdd). The first range ending after cp
      // is the only range that can contain this bookmark start.
      auto range = std::lower_bound(story_ranges.begin(), story_ranges.end(),
                                    cp, [](const auto& story, uint32_t value) {
                                      return story.second <= value;
                                    });
      if (range != story_ranges.end()) {
        const auto [first, last] = *range;
        same_story = last > first && cp >= first && cp < last - 1 &&
                     found->second <= last - 1;
      }
    }
    if (!same_story) {
      invalid_bookmark = true;
    } else {
      valid_ids.insert(id);
    }
  }
  if (invalid_bookmark)
    doc.warnings.push_back("skipped header bookmark PLC: invalid range");
  for (const auto& [cp, marks] : bookmarks.marks) {
    if (cp < base || cp >= end)
      continue;
    for (const auto& mark : marks) {
      if (valid_ids.contains(mark.id))
        header_bookmarks.marks[cp].push_back(mark);
    }
  }
  for (size_t section = 0; section < sections; section++) {
    for (size_t story = 0; story < kStoryCount; story++) {
      const size_t index = kStoryPreamble + section * kStoryCount + story;
      const uint32_t first = U32(plc, 4 * index);
      const uint32_t last = U32(plc, 4 * (index + 1));
      out[section][story] =
          ReadStoryParagraphs(*chars, chpx, papx, doc.fonts, prcs, base, first,
                              last, &field_status, &header_bookmarks);
    }
  }
  return out;
}
}  // namespace doccraft::doc::detail
