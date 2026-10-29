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
#include <string>
#include <vector>

#include "doc/format/sprm.h"

namespace doccraft::doc::detail {
namespace {

// Word 6/95 footnote/endnote text PLCs may end up to two CPs past the story
// length: those CPs are the final story's paragraph marks, like PlcfHdd
// (LibreOffice reads the raw PLC without clamping). Word 97+ stays exact.
constexpr uint32_t kLegacyNoteTextGuard = 2;

// PlcffndRef / PlcfendRef: (n + 1) CPs followed by n 2-byte FRD entries.
std::expected<std::vector<uint32_t>, Error> ReadReferenceCps(Bytes table,
                                                             Fib::Range r) {
  std::vector<uint32_t> cps;
  if (!r.size) {
    return cps;
  }
  if (!Valid(table, r) || r.size < 8 || (r.size - 4) % 6) {
    return std::unexpected(Error{"invalid note reference PLC"});
  }
  Bytes plc = table.subspan(r.offset, r.size);
  const size_t count = (r.size - 4) / 6;
  for (size_t i = 0; i <= count; i++) {
    cps.push_back(U32(plc, 4 * i));
    if (i && cps[i] <= cps[i - 1])
      return std::unexpected(Error{"invalid note reference PLC CP order"});
  }
  return cps;
}

// PlcffndTxt / PlcfendTxt: one CP per note plus a trailing guard.
std::expected<std::vector<uint32_t>, Error> ReadTextCps(Bytes table,
                                                        Fib::Range r) {
  std::vector<uint32_t> cps;
  if (!r.size) {
    return cps;
  }
  if (!Valid(table, r) || r.size < 8 || (r.size % 4)) {
    return std::unexpected(Error{"invalid note text PLC"});
  }
  Bytes plc = table.subspan(r.offset, r.size);
  for (size_t i = 0; i < r.size / 4; i++) {
    cps.push_back(U32(plc, 4 * i));
    if (i && cps[i] <= cps[i - 1])
      return std::unexpected(Error{"invalid note text PLC CP order"});
  }
  return cps;
}

}  // namespace

std::expected<NoteReferenceSet, Error> ReadNoteReferences(
    Bytes word, Bytes table, const Fib& fib,
    std::vector<std::string>* warnings) {
  NoteReferenceSet out;
  // Word 6/95 keeps both note PLCs in WordDocument; Word 97+ uses the table
  // stream.
  Bytes source = fib.legacy ? word : table;
  const uint32_t text_guard = fib.legacy ? kLegacyNoteTextGuard : 0;
  auto read = [&](Fib::Range reference, Fib::Range text, uint32_t length,
                  const char* name, std::vector<uint32_t>* target) {
    auto refs = ReadReferenceCps(source, reference);
    auto texts = ReadTextCps(source, text);
    std::string error;
    if (!refs)
      error = refs.error().message;
    else if (!texts)
      error = texts.error().message;
    else if ((refs->empty() && !texts->empty()) ||
             (!refs->empty() && (texts->size() < refs->size() ||
                                 texts->size() > refs->size() + 1)))
      error = "note PLC entry count mismatch";
    else if (refs->size() > 1 && (*refs)[refs->size() - 2] >= fib.ccp_text)
      error = "invalid note reference PLC CP range";
    else if (!texts->empty() && texts->back() > length + text_guard)
      error = "invalid note text PLC CP range";
    if (!error.empty()) {
      if (!warnings)
        return;
      warnings->push_back(std::string("skipped ") + name +
                          " story PLC: " + error);
      return;
    }
    *target = std::move(*refs);
  };
  read(fib.fnd_ref, fib.fnd_txt, fib.ccp_ftn, "footnote", &out.footnote_cps);
  read(fib.end_ref, fib.end_txt, fib.ccp_edn, "endnote", &out.endnote_cps);
  return out;
}

std::expected<void, Error> ReadNotes(model::Document& doc, Bytes word,
                                     Bytes table, const Fib& fib,
                                     const std::vector<FormatRange>& chpx,
                                     const std::vector<FormatRange>& papx,
                                     const BookmarkMarks& bookmarks) {
  // Word 6/95 keeps the note PLCs and their field PLCs in WordDocument; Word
  // 97+ uses the table stream.
  Bytes source = fib.legacy ? word : table;
  const uint32_t text_guard = fib.legacy ? kLegacyNoteTextGuard : 0;
  auto footnotes = ReadTextCps(source, fib.fnd_txt);
  auto endnotes = ReadTextCps(source, fib.end_txt);
  // The reference PLC is authoritative for how many notes exist: some
  // producers append an extra guard CP to the text PLC.
  auto refs = ReadNoteReferences(word, table, fib, nullptr);
  if (!refs) {
    return std::unexpected(refs.error());
  }
  auto count = [](size_t references, size_t text_cps) {
    const size_t by_text = text_cps > 1 ? text_cps - 1 : 0;
    return std::min(references, by_text);
  };
  auto references = [](const std::vector<uint32_t>& cps) {
    return cps.empty() ? size_t{0} : cps.size() - 1;
  };
  const size_t footnote_count =
      count(references(refs->footnote_cps), footnotes ? footnotes->size() : 0);
  const size_t endnote_count =
      count(references(refs->endnote_cps), endnotes ? endnotes->size() : 0);
  if (footnote_count == 0 && endnote_count == 0) {
    return {};
  }
  // [MS-DOC] §2.4.1 text plane: main, footnotes, headers, annotations,
  // endnotes, text boxes. Each note range is relative to its own story.
  const uint64_t footnote_base = fib.ccp_text;
  const uint64_t endnote_base = static_cast<uint64_t>(fib.ccp_text) +
                                fib.ccp_ftn + fib.ccp_hdd + fib.ccp_atn;
  const uint64_t limit = endnote_base + fib.ccp_edn;
  std::vector<std::vector<uint8_t>> prcs;
  auto chars = ReadChars(word, table, fib, static_cast<uint32_t>(limit),
                         fib.ccp_text, &prcs);
  if (!chars) {
    return std::unexpected(chars.error());
  }
  auto append = [&](const std::vector<uint32_t>& text_cps, uint64_t base,
                    uint32_t story_length, size_t count, bool endnote,
                    const std::map<uint32_t, uint8_t>& field_status) {
    // Word 6/95 text PLCs may end up to two CPs past the story (the final
    // story's paragraph marks). Clamp such a range to the story instead of
    // dropping the note, otherwise the reference in the main story keeps
    // pointing at a missing footnotes.xml entry. Word 97+ keeps exact CPs:
    // an over-long range is skipped, never clamped.
    auto resolve_range = [&](uint32_t first, uint32_t last, uint32_t* end) {
      if (last > story_length) {
        if (text_guard == 0)
          return false;
        last = story_length;
        if (last <= first)
          return false;
      }
      if (last < first)
        return false;
      *end = last;
      return true;
    };
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    for (size_t i = 0; i < count; ++i) {
      uint32_t last = 0;
      if (!resolve_range(text_cps[i], text_cps[i + 1], &last) ||
          last == text_cps[i])
        continue;
      ranges.emplace_back(base + text_cps[i], base + last);
    }
    const BookmarkMarks story_bookmarks = FilterStoryBookmarks(
        bookmarks, ranges, endnote ? "endnote" : "footnote", &doc.warnings);
    for (size_t i = 0; i < count; i++) {
      const uint32_t first = text_cps[i];
      uint32_t last = 0;
      if (!resolve_range(first, text_cps[i + 1], &last)) {
        continue;
      }
      model::Note note;
      note.endnote = endnote;
      note.paragraphs =
          ReadStoryParagraphs(*chars, chpx, papx, doc.fonts, prcs, base, first,
                              last, &field_status, &story_bookmarks);
      doc.notes.push_back(std::move(note));
    }
  };
  // PlcfFldFtn / PlcfFldEdn CPs are relative to their own note region; a
  // malformed PLC drops only the flags, never the note bodies.
  auto read_status = [&](Fib::Range r, uint64_t base, uint32_t story_length,
                         const char* what) {
    std::map<uint32_t, uint8_t> status;
    if (auto parsed_status =
            ReadStoryFieldStatus(source, r, base, story_length)) {
      status = std::move(*parsed_status);
    } else {
      doc.warnings.push_back(std::string("skipped ") + what +
                             " field PLC: " + parsed_status.error().message);
    }
    return status;
  };
  const auto footnote_status =
      read_status(fib.fields_ftn, footnote_base, fib.ccp_ftn, "footnote");
  const auto endnote_status =
      read_status(fib.fields_edn, endnote_base, fib.ccp_edn, "endnote");
  if (footnote_count != 0) {
    append(*footnotes, footnote_base, fib.ccp_ftn, footnote_count, false,
           footnote_status);
  }
  if (endnote_count != 0) {
    append(*endnotes, endnote_base, fib.ccp_edn, endnote_count, true,
           endnote_status);
  }
  return {};
}
}  // namespace doccraft::doc::detail
