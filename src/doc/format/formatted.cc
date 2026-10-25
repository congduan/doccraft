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
#include "doc/format/sprm_codes.h"

namespace doccraft::doc {
using detail::AppendUtf8;
using detail::ApplyPara;
using detail::ApplyRun;
using detail::ApplySection;
using detail::ApplyTable;
using detail::Bytes;
using detail::Char;
using detail::Find;
using detail::FormatRange;
using detail::InheritedRun;
using detail::kSprmCPlain;
using detail::kSprmPFInTable;
using detail::kSprmPFTtp;
using detail::kSprmTDefTable;
using detail::kSprmTDefTableObsolete;
using detail::kUndefinedU32;
using detail::ReadChars;
using detail::ReadFkps;
using detail::ReadFonts;
using detail::ReadLists;
using detail::ReadStyles;
using detail::S16;
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

// One open complex field ([MS-DOC] §2.5.1 \x13 instruction \x14 result \x15).
enum class FieldMode {
  kPending,
  kHyperlink,  // rewritten as w:hyperlink, no fldChar remains
  kPage,       // rewritten as w:fldSimple
  kGeneric,    // w:fldChar begin/instrText/separate ... end
};

struct FieldFrame {
  std::string instruction;
  bool in_instruction = true;
  FieldMode mode = FieldMode::kPending;
  std::string hyperlink;
  bool page = false;
};

// Two characters merge into one run only when every formatting field matches.
// Symbol and field-marker runs never merge: they carry their own font or emit a
// dedicated element. The comparison itself is Run's defaulted operator== so it
// stays exhaustive as the model grows.
bool SameRunFormatting(const model::Run& prior, const model::Run& r) {
  if (r.symbol_character >= 0 || r.field_char >= 0 || !r.instr_text.empty()) {
    return false;
  }
  if (prior.field_char >= 0 || !prior.instr_text.empty()) {
    return false;
  }
  model::Run left = prior;
  model::Run right = r;
  left.text.clear();
  right.text.clear();
  return left == right;
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

std::expected<model::Document, Error> ParseFormatted(Bytes word, Bytes table,
                                                     const Fib& fib,
                                                     Bytes data) {
  auto chars = ReadChars(word, table, fib, fib.ccp_text);
  if (!chars)
    return std::unexpected(chars.error());
  auto chpx = ReadFkps(word, table, fib.chpx_bte, false);
  if (!chpx)
    return std::unexpected(chpx.error());
  auto papx = ReadFkps(word, table, fib.papx_bte, true);
  if (!papx)
    return std::unexpected(papx.error());
  model::Document doc;
  doc.fonts = ReadFonts(table, fib.fonts);
  doc.styles = ReadStyles(table, fib.stsh, doc.fonts);
  ReadLists(doc, table, fib);
  auto sections_result = ReadSections(word, table, fib);
  if (!sections_result)
    return std::unexpected(sections_result.error());
  std::vector<std::pair<uint32_t, model::Section>> sections =
      std::move(*sections_result);
  if (!sections.empty())
    doc.section = sections.back().second;
  model::Paragraph para;
  model::Cell cell;
  model::Table current_table;
  std::vector<model::Cell> row;
  bool in_table = false;
  std::vector<FieldFrame> fields;
  bool page_field = false;
  std::string hyperlink;
  size_t next_section = 0;
  auto add_paragraph = [&](model::Paragraph value) {
    doc.blocks.push_back({false, doc.paragraphs.size()});
    doc.paragraphs.push_back(std::move(value));
  };
  auto add_table = [&]() {
    if (!in_table)
      return;
    if (!row.empty()) {
      current_table.rows.push_back(std::move(row));
      current_table.row_properties.emplace_back();
      row.clear();
    }
    doc.blocks.push_back({true, doc.tables.size()});
    doc.tables.push_back(std::move(current_table));
    current_table = {};
    in_table = false;
  };
  for (size_t i = 0; i < chars->size(); i++) {
    const Char& ch = (*chars)[i];
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
      para.runs.push_back(std::move(marker));
    };
    auto emit_instruction = [&](const std::string& text) {
      if (text.empty())
        return;
      model::Run marker;
      marker.instr_text = text;
      para.runs.push_back(std::move(marker));
    };
    if (ch.text == "\x13") {
      fields.push_back({});
      continue;
    }
    if (!fields.empty() && fields.back().in_instruction && ch.text == "\x14") {
      FieldFrame& frame = fields.back();
      frame.in_instruction = false;
      ApplyFieldInstruction(frame.instruction, &frame.hyperlink, &frame.page);
      // Only a top-level HYPERLINK/PAGE field keeps its dedicated mapping;
      // nested fields and everything else become generic complex fields.
      if (fields.size() == 1 && !frame.hyperlink.empty())
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
    if (!fields.empty() && ch.text == "\x15") {
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
      fields.back().instruction += ch.text;
      continue;
    }
    const auto* pap = Find(*papx, ch.fc);
    bool table_flag = false, row_end = false;
    if (pap) {
      WalkParagraph(pap->sprms, data, [&](uint16_t op, Bytes v) {
        if (op == kSprmPFInTable)
          table_flag = v[0] != 0;
        if (op == kSprmPFTtp)
          row_end = v[0] != 0;
      });
    }
    // The first in-table paragraph starts the table: its cell mark is still
    // ahead, so waiting for \x07 would drop the cell's first paragraph.
    if (table_flag && !in_table && ch.text != "\x07") {
      in_table = true;
      current_table = {};
    }
    if (in_table && !table_flag && !row_end && ch.text != "\x07")
      add_table();
    if (ch.text == "\x07") {
      if (!in_table) {
        in_table = true;
      }
      if (row_end) {
        // A cell that never saw its own mark still has to be kept.
        if (!cell.paragraphs.empty()) {
          row.push_back(std::move(cell));
          cell = {};
        }
        auto finished = FinishTableRow(current_table, row, para, pap, data);
        if (!finished) {
          return std::unexpected(finished.error());
        }
      } else {
        ApplyParagraphSprms(para, pap, data);
        cell.paragraphs.push_back(std::move(para));
        row.push_back(std::move(cell));
        cell = {};
        para = {};
      }
      continue;
    }
    if (ch.newline) {
      ApplyParagraphSprms(para, pap, data);
      if (in_table)
        cell.paragraphs.push_back(std::move(para));
      else
        add_paragraph(std::move(para));
      para = {};
      continue;
    }
    if (ch.text.size() == 1 && ch.text[0] == '\x0c') {
      if (next_section + 1 < sections.size() &&
          ch.cp + 1 == sections[next_section].first) {
        ApplyParagraphSprms(para, pap, data);
        add_paragraph(std::move(para));
        para = {};
        doc.section_breaks.emplace_back(doc.paragraphs.size() - 1,
                                        sections[next_section].second);
        next_section++;
      } else {
        // Not a section boundary: a manual page break inside the paragraph.
        model::Run page_break;
        page_break.break_type = 1;
        para.runs.push_back(std::move(page_break));
      }
      continue;
    }
    auto baseline = [&]() {
      model::Run value = pap ? InheritedRun(doc, pap->style) : model::Run{};
      value.hyperlink = hyperlink;
      value.page_field = page_field;
      return value;
    };
    model::Run r = baseline();
    if (const auto* x = Find(*chpx, ch.fc)) {
      Walk(x->sprms, [&](uint16_t op, Bytes v) {
        // sprmCPlain discards direct character formatting accumulated so far.
        if (op == kSprmCPlain) {
          r = baseline();
        } else {
          ApplyRun(r, op, v, doc.fonts);
        }
      });
    }
    if (!para.runs.empty()) {
      auto& back = para.runs.back();
      model::Run prior = back;
      prior.text.clear();
      if (SameRunFormatting(prior, r)) {
        back.text += ch.text;
        continue;
      }
    }
    r.text = ch.text;
    para.runs.push_back(std::move(r));
  }
  add_table();
  if (!para.runs.empty() || doc.blocks.empty())
    add_paragraph(std::move(para));
  auto headers = ReadHeaders(doc, word, table, fib, *chpx, *papx);
  if (!headers)
    return std::unexpected(headers.error());
  return doc;
}
}  // namespace doccraft::doc
