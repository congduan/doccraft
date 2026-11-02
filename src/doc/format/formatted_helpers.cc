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
using detail::FrameLegacySepx;
using detail::kSprmTDefTable;
using detail::kSprmTDefTableObsolete;
using detail::kUndefinedU32;
using detail::LegacySepxCorrections;
using detail::LegacySepxSkip;
using detail::LegacySepxUnmappedWarning;
using detail::ParsedSections;
using detail::U16;
using detail::U32;
using detail::Valid;
using detail::Walk;
using detail::WalkLegacySepx;
using detail::WalkParagraph;

namespace {

struct LegacySectionSprm {
  uint8_t code;
  uint16_t modern;
};

// One-byte Word 6/95 section opcodes and the Word 97+ section sprms they map
// onto. Opcodes the width table knows but this converter has no modern handler
// for are skipped by WalkLegacySepx; ApplySection validates every operand.
constexpr LegacySectionSprm kLegacySectionSprms[] = {
    {detail::kLegacySprmScnsPgn, detail::kSprmScnsPgn},
    {detail::kLegacySprmSiHeadingPgn, detail::kSprmSiHeadingPgn},
    {detail::kLegacySprmSDxaColWidth, detail::kSprmSDxaColWidth},
    {detail::kLegacySprmSDxaColSpacing, detail::kSprmSDxaColSpacing},
    {detail::kLegacySprmSFEvenlySpaced, detail::kSprmSFEvenlySpaced},
    {detail::kLegacySprmSFProtected, detail::kSprmSFProtected},
    {detail::kLegacySprmSDmBinFirst, detail::kSprmSDmBinFirst},
    {detail::kLegacySprmSDmBinOther, detail::kSprmSDmBinOther},
    {detail::kLegacySprmSBkc, detail::kSprmSBkc},
    {detail::kLegacySprmSFTitlePage, detail::kSprmSFTitlePage},
    {detail::kLegacySprmSCcolumns, detail::kSprmSCcolumns},
    {detail::kLegacySprmSDxaColumns, detail::kSprmSDxaColumns},
    {detail::kLegacySprmSNfcPgn, detail::kSprmSNfcPgn},
    {detail::kLegacySprmSFPgnRestart, detail::kSprmSFPgnRestart},
    {detail::kLegacySprmSFEndnote, detail::kSprmSFEndnote},
    {detail::kLegacySprmSLnc, detail::kSprmSLnc},
    {detail::kLegacySprmSNLnnMod, detail::kSprmSNLnnMod},
    {detail::kLegacySprmSDxaLnn, detail::kSprmSDxaLnn},
    {detail::kLegacySprmSDyaHdrTop, detail::kSprmSDyaHdrTop},
    {detail::kLegacySprmSDyaHdrBottom, detail::kSprmSDyaHdrBottom},
    {detail::kLegacySprmSLBetween, detail::kSprmSLBetween},
    {detail::kLegacySprmSVjc, detail::kSprmSVjc},
    {detail::kLegacySprmSLnnMin, detail::kSprmSLnnMin},
    {detail::kLegacySprmSPgnStart, detail::kSprmSPgnStart97},
    {detail::kLegacySprmSBOrientation, detail::kSprmSBOrientation},
    {detail::kLegacySprmSXaPage, detail::kSprmSXaPage},
    {detail::kLegacySprmSYaPage, detail::kSprmSYaPage},
    {detail::kLegacySprmSDxaLeft, detail::kSprmSDxaLeft},
    {detail::kLegacySprmSDxaRight, detail::kSprmSDxaRight},
    {detail::kLegacySprmSDyaTop, detail::kSprmSDyaTop},
    {detail::kLegacySprmSDyaBottom, detail::kSprmSDyaBottom},
    {detail::kLegacySprmSDzaGutter, detail::kSprmSDzaGutter},
};

bool ApplyLegacySectionSprm(model::Section& section, uint8_t code, Bytes v) {
  for (const auto& entry : kLegacySectionSprms)
    if (entry.code == code)
      return ApplySection(section, entry.modern, v);
  return true;
}

}  // namespace

// Reads the section descriptor PLC (SED) and the SEPX of each section.
std::expected<ParsedSections, Error> ReadSections(
    Bytes word, Bytes table, const Fib& fib, std::vector<std::string>* warnings,
    LegacySepxCorrections* corrected) {
  ParsedSections out;
  if (fib.sed.size) {
    // Word 6/95 keeps Plcfsed in WordDocument; Word 97+ uses the table stream.
    Bytes source = fib.legacy ? word : table;
    if (!Valid(source, fib.sed) || fib.sed.size < 20 || (fib.sed.size - 4) % 16)
      return std::unexpected(Error{"invalid SED PLC"});
    Bytes b = source.subspan(fib.sed.offset, fib.sed.size);
    size_t n = (b.size() - 4) / 16;
    out.sed_count = n;
    uint32_t previous = 0;
    for (size_t i = 0; i < n; i++) {
      uint32_t end = U32(b, 4 * (i + 1));
      if (end <= previous) {
        // A truncated Plcfsed often ends with a CP that does not advance
        // (ofz34749-1). Drop only the affected entry so the sections that did
        // parse keep their properties; `previous` stays at the last accepted
        // CP so the surviving boundaries remain strictly increasing.
        if (warnings) {
          warnings->push_back("skipped section " + std::to_string(i) + ": CP " +
                              std::to_string(end) + " does not follow CP " +
                              std::to_string(previous) + "; section dropped");
        }
        continue;
      }
      previous = end;
      if (fib.legacy) {
        size_t pos = 4 * (n + 1) + 12 * i;
        uint32_t fc = U32(b, pos + 2);
        model::Section section;
        if (fc != kUndefinedU32) {
          auto payload = FrameLegacySepx(word, fc, warnings, corrected);
          if (!payload) {
            // A damaged SEPX frame loses only this section's properties
            // (ofz21385-1); the section itself and every later section stay.
            if (warnings) {
              warnings->push_back("skipped section " + std::to_string(i) +
                                  ": " + payload.error().message +
                                  "; default section properties used");
            }
          } else {
            bool valid = true;
            LegacySepxSkip skip;
            auto walked = WalkLegacySepx(
                *payload,
                [&](uint8_t code, Bytes v) {
                  valid &= ApplyLegacySectionSprm(section, code, v);
                },
                &skip);
            if (!walked) {
              if (walked.error().message != detail::kLegacySepxTruncated)
                return std::unexpected(walked.error());
              if (warnings) {
                warnings->push_back("skipped section " + std::to_string(i) +
                                    ": " + walked.error().message +
                                    "; default section properties used");
              }
              section = model::Section{};
            } else if (!valid) {
              return std::unexpected(Error{"invalid SEPX sprm"});
            } else if (skip.skipped && warnings &&
                       (!corrected || corrected->unmapped.insert(fc).second)) {
              warnings->push_back(LegacySepxUnmappedWarning(skip));
            }
          }
        }
        out.sections.emplace_back(end, std::move(section));
        out.sed_indices.push_back(i);
        continue;
      }
      size_t pos = 4 * (n + 1) + 12 * i;
      uint32_t fc = U32(b, pos + 2);
      model::Section section;
      if (fc != kUndefinedU32) {
        if (fc > word.size() || word.size() - fc < 2) {
          // The SEPX lives past the readable WordDocument (common when the
          // stream itself was truncated, as in ofz18414-1). The section still
          // exists; only its properties are unknown.
          if (warnings) {
            warnings->push_back("SEPX for section " + std::to_string(i) +
                                " at fc " + std::to_string(fc) +
                                " lies outside WordDocument (" +
                                std::to_string(word.size()) +
                                " bytes); default section properties used");
          }
          out.sections.emplace_back(end, model::Section{});
          out.sed_indices.push_back(i);
          continue;
        }
        size_t len = U16(word, fc);
        if (len > word.size() - fc - 2) {
          if (warnings) {
            warnings->push_back("SEPX for section " + std::to_string(i) +
                                " at fc " + std::to_string(fc) +
                                " is truncated; default section "
                                "properties used");
          }
          out.sections.emplace_back(end, model::Section{});
          out.sed_indices.push_back(i);
          continue;
        }
        bool valid = true;
        if (!Walk(word.subspan(fc + 2, len),
                  [&](uint16_t op, Bytes v) {
                    valid &= ApplySection(section, op, v);
                  }) ||
            !valid)
          return std::unexpected(Error{"invalid SEPX sprm"});
      }
      out.sections.emplace_back(end, std::move(section));
      out.sed_indices.push_back(i);
    }
  }
  return out;
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
                                          const FormatRange* pap, Bytes data,
                                          std::vector<std::string>* warnings) {
  // Collect first, apply second: a TAPX whose nested sprmPHugePapx pointer is
  // unreadable must not leave the table half-formatted. Operands are only
  // borrowed views into `data`, which outlives both passes.
  std::vector<std::pair<uint16_t, Bytes>> operands;
  bool walked = true;
  if (pap) {
    walked = WalkParagraph(pap->sprms, data, [&](uint16_t op, Bytes v) {
      if (op == kSprmTDefTable || op == kSprmTDefTableObsolete ||
          (op >> 10 & 7) == 5) {
        operands.emplace_back(op, v);
      }
    });
  }
  model::RowProperties properties;
  bool valid = true;
  uint16_t invalid_op = 0;
  const auto apply = [&](model::Table& target, model::RowProperties& props) {
    for (const auto& [op, v] : operands) {
      if (!ApplyTable(target, props, op, v, warnings)) {
        valid = false;
        invalid_op = op;
        return;
      }
    }
  };
  if (!walked) {
    // Confirm the operands that were reachable before the pointer damage. An
    // invalid one keeps its rejection priority over the "defaults used"
    // fallback; valid ones are applied to a scratch copy and discarded.
    if (!operands.empty()) {
      model::Table scratch = table;
      model::RowProperties scratch_properties = properties;
      apply(scratch, scratch_properties);
      if (!valid) {
        return std::unexpected(
            Error{"invalid TAPX operand " + std::to_string(invalid_op)});
      }
    }
    // A nested sprmPHugePapx pointer outside the Data stream makes the row
    // properties unreadable; the cells themselves come from the text and stay
    // usable (LibreOffice sw/qa ofz18414-1).
    if (warnings) {
      warnings->push_back(
          "table row properties unreadable (sprm stream outside its backing "
          "stream); defaults used");
    }
  } else {
    apply(table, properties);
    if (!valid) {
      return std::unexpected(
          Error{"invalid TAPX operand " + std::to_string(invalid_op)});
    }
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
