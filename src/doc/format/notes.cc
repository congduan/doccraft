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
    Bytes table, const Fib& fib, std::vector<std::string>* warnings) {
  NoteReferenceSet out;
  auto read = [&](Fib::Range reference, Fib::Range text, uint32_t length,
                  const char* name, std::vector<uint32_t>* target) {
    auto refs = ReadReferenceCps(table, reference);
    auto texts = ReadTextCps(table, text);
    std::string error;
    if (!refs)
      error = refs.error().message;
    else if (!texts)
      error = texts.error().message;
    else if (!refs->empty() &&
             (texts->size() < refs->size() || texts->size() > refs->size() + 1))
      error = "note PLC entry count mismatch";
    else if (refs->size() > 1 && (*refs)[refs->size() - 2] >= fib.ccp_text)
      error = "invalid note reference PLC CP range";
    else if (!texts->empty() && texts->back() > length)
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
                                     const std::vector<FormatRange>& papx) {
  auto footnotes = ReadTextCps(table, fib.fnd_txt);
  auto endnotes = ReadTextCps(table, fib.end_txt);
  // The reference PLC is authoritative for how many notes exist: some
  // producers append an extra guard CP to the text PLC.
  auto refs = ReadNoteReferences(table, fib, nullptr);
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
    for (size_t i = 0; i < count; i++) {
      const uint32_t first = text_cps[i];
      const uint32_t last = text_cps[i + 1];
      if (last < first || last > story_length) {
        continue;
      }
      model::Note note;
      note.endnote = endnote;
      note.paragraphs = ReadStoryParagraphs(*chars, chpx, papx, doc.fonts, prcs,
                                            base, first, last, &field_status);
      doc.notes.push_back(std::move(note));
    }
  };
  // PlcfFldFtn / PlcfFldEdn CPs are relative to their own note region; a
  // malformed PLC drops only the flags, never the note bodies.
  auto read_status = [&](Fib::Range r, uint64_t base, uint32_t story_length,
                         const char* what) {
    std::map<uint32_t, uint8_t> status;
    if (auto parsed_status =
            ReadStoryFieldStatus(table, r, base, story_length)) {
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
  append(*footnotes, footnote_base, fib.ccp_ftn, footnote_count, false,
         footnote_status);
  append(*endnotes, endnote_base, fib.ccp_edn, endnote_count, true,
         endnote_status);
  return {};
}
}  // namespace doccraft::doc::detail
