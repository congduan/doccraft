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
  }
  return cps;
}

}  // namespace

std::expected<NoteReferenceSet, Error> ReadNoteReferences(Bytes table,
                                                          const Fib& fib) {
  NoteReferenceSet out;
  auto footnotes = ReadReferenceCps(table, fib.fnd_ref);
  if (!footnotes) {
    return std::unexpected(footnotes.error());
  }
  auto endnotes = ReadReferenceCps(table, fib.end_ref);
  if (!endnotes) {
    return std::unexpected(endnotes.error());
  }
  out.footnote_cps = std::move(*footnotes);
  out.endnote_cps = std::move(*endnotes);
  return out;
}

std::expected<void, Error> ReadNotes(model::Document& doc, Bytes word,
                                     Bytes table, const Fib& fib,
                                     const std::vector<FormatRange>& chpx,
                                     const std::vector<FormatRange>& papx) {
  auto footnotes = ReadTextCps(table, fib.fnd_txt);
  if (!footnotes) {
    return std::unexpected(footnotes.error());
  }
  auto endnotes = ReadTextCps(table, fib.end_txt);
  if (!endnotes) {
    return std::unexpected(endnotes.error());
  }
  // The reference PLC is authoritative for how many notes exist: some
  // producers append an extra guard CP to the text PLC.
  auto refs = ReadNoteReferences(table, fib);
  if (!refs) {
    return std::unexpected(refs.error());
  }
  auto count = [](size_t references, size_t text_cps) {
    const size_t by_text = text_cps > 1 ? text_cps - 1 : 0;
    return references == 0 ? by_text : std::min(references, by_text);
  };
  auto references = [](const std::vector<uint32_t>& cps) {
    return cps.empty() ? size_t{0} : cps.size() - 1;
  };
  const size_t footnote_count =
      count(references(refs->footnote_cps), footnotes->size());
  const size_t endnote_count =
      count(references(refs->endnote_cps), endnotes->size());
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
                    uint32_t story_length, size_t count, bool endnote) {
    for (size_t i = 0; i < count; i++) {
      const uint32_t first = text_cps[i];
      const uint32_t last = text_cps[i + 1];
      if (last < first || last > story_length) {
        continue;
      }
      model::Note note;
      note.endnote = endnote;
      note.paragraphs = ReadStoryParagraphs(*chars, chpx, papx, doc.fonts, prcs,
                                            base, first, last);
      doc.notes.push_back(std::move(note));
    }
  };
  append(*footnotes, footnote_base, fib.ccp_ftn, footnote_count, false);
  append(*endnotes, endnote_base, fib.ccp_edn, endnote_count, true);
  return {};
}
}  // namespace doccraft::doc::detail
