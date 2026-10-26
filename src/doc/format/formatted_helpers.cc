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

#include "doc/format/formatted_helpers.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "doc/format/sprm.h"
#include "doc/format/sprm_codes.h"

namespace doccraft::doc {
using detail::ApplyPara;
using detail::ApplySection;
using detail::ApplyTable;
using detail::Bytes;
using detail::FormatRange;
using detail::kSprmTDefTable;
using detail::kSprmTDefTableObsolete;
using detail::kUndefinedU32;
using detail::U16;
using detail::U32;
using detail::Valid;
using detail::Walk;
using detail::WalkParagraph;

// Reads the section descriptor PLC (SED) and the SEPX of each section.
std::expected<std::vector<std::pair<uint32_t, model::Section>>, Error>
ReadSections(Bytes word, Bytes table, const Fib& fib) {
  std::vector<std::pair<uint32_t, model::Section>> sections;
  if (fib.sed.size) {
    if (!Valid(table, fib.sed) || fib.sed.size < 20 || (fib.sed.size - 4) % 16)
      return std::unexpected(Error{"invalid SED PLC"});
    Bytes b = table.subspan(fib.sed.offset, fib.sed.size);
    size_t n = (b.size() - 4) / 16;
    uint32_t previous = 0;
    for (size_t i = 0; i < n; i++) {
      uint32_t end = U32(b, 4 * (i + 1));
      if (end <= previous)
        return std::unexpected(Error{"invalid section CP order"});
      previous = end;
      size_t pos = 4 * (n + 1) + 12 * i;
      uint32_t fc = U32(b, pos + 2);
      model::Section section;
      if (fc != kUndefinedU32) {
        if (fc > word.size() || word.size() - fc < 2)
          return std::unexpected(Error{"SEPX outside WordDocument"});
        size_t len = U16(word, fc);
        if (len > word.size() - fc - 2)
          return std::unexpected(Error{"truncated SEPX"});
        bool valid = true;
        if (!Walk(word.subspan(fc + 2, len),
                  [&](uint16_t op, Bytes v) {
                    valid &= ApplySection(section, op, v);
                  }) ||
            !valid)
          return std::unexpected(Error{"invalid SEPX sprm"});
      }
      sections.emplace_back(end, std::move(section));
    }
  }
  return sections;
}

// Extracts the HYPERLINK target and detects PAGE fields from a field
// instruction string (the text between \x13 and \x14).
void ApplyFieldInstruction(const std::string& instruction,
                           std::string* hyperlink, bool* page_field) {
  auto tag = instruction.find("HYPERLINK");
  if (tag != std::string::npos) {
    size_t p = tag + 9;
    while (p < instruction.size() && instruction[p] == ' ')
      p++;
    if (p < instruction.size() && instruction[p] == '\"') {
      size_t end = instruction.find('"', p + 1);
      if (end != std::string::npos)
        *hyperlink = instruction.substr(p + 1, end - p - 1);
    } else {
      size_t end = instruction.find(' ', p);
      *hyperlink = instruction.substr(p, end - p);
    }
  }
  *page_field = instruction.find("PAGE") != std::string::npos;
}

// Two characters merge into one run only when every formatting field matches.
// Symbol and field-marker runs never merge: they carry their own font or emit a
// dedicated element. The comparison itself is Run's defaulted operator== so it
// stays exhaustive as the model grows.
bool SameRunFormatting(model::Run& prior, model::Run& r) {
  if (r.symbol_character >= 0 || r.field_char >= 0 || r.image_index >= 0 ||
      !r.instr_text.empty()) {
    return false;
  }
  if (prior.field_char >= 0 || prior.image_index >= 0 ||
      !prior.instr_text.empty()) {
    return false;
  }
  std::string text = std::move(prior.text);
  std::string next_text = std::move(r.text);
  const bool same = prior == r;
  prior.text = std::move(text);
  r.text = std::move(next_text);
  return same;
}

// Applies the paragraph sprms of one PAPX to `para`, honouring its style.
void ApplyParagraphSprms(model::Paragraph& para, const FormatRange* pap,
                         Bytes data) {
  if (!pap) {
    return;
  }
  para.style = pap->style;
  WalkParagraph(pap->sprms, data, [&](uint16_t op, Bytes v) {
    ApplyPara(para, op, v);
  });
}

// Closes a table row: the TAPX operand of the row-end mark carries cell and row
// properties, which must be applied before the row is appended.
std::expected<void, Error> FinishTableRow(model::Table& table,
                                          std::vector<model::Cell>& row,
                                          model::Paragraph& para,
                                          const FormatRange* pap, Bytes data) {
  model::RowProperties properties;
  bool valid = true;
  uint16_t invalid_op = 0;
  if (pap) {
    valid =
        WalkParagraph(pap->sprms, data,
                      [&](uint16_t op, Bytes v) {
                        if (valid && (op == kSprmTDefTable ||
                                      op == kSprmTDefTableObsolete ||
                                      (op >> 10 & 7) == 5)) {
                          if (!(valid = ApplyTable(table, properties, op, v))) {
                            invalid_op = op;
                          }
                        }
                      }) &&
        valid;
  }
  if (!valid) {
    return std::unexpected(
        Error{"invalid TAPX operand " + std::to_string(invalid_op)});
  }
  if (properties.cells.empty()) {
    properties.cells.resize(row.size());
  }
  properties.columns = table.columns;
  table.row_properties.push_back(std::move(properties));
  table.rows.push_back(std::move(row));
  row.clear();
  para = {};
  return {};
}

}  // namespace doccraft::doc
