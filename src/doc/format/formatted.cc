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

namespace doccraft::doc {
using detail::AppendUtf8;
using detail::ApplyRun;
using detail::Bytes;
using detail::Char;
using detail::Find;
using detail::FormatRange;
using detail::InheritedRun;
using detail::kSprmCPlain;
using detail::kSprmPDtap;
using detail::kSprmPFInnerTableCell;
using detail::kSprmPFInnerTtp;
using detail::kSprmPFInTable;
using detail::kSprmPFTtp;
using detail::kSprmPItap;
using detail::ReadBookmarks;
using detail::ReadChars;
using detail::ReadCommentAnchors;
using detail::ReadComments;
using detail::ReadFkps;
using detail::ReadFonts;
using detail::ReadLists;
using detail::ReadNoteReferences;
using detail::ReadNotes;
using detail::ReadStyles;
using detail::S16;
using detail::U16;
using detail::Walk;
using detail::WalkParagraph;

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
  size_t total_image_size = 0;
  auto floating = ReadFloatingPictures(word, table, fib);
  if (!floating)
    return std::unexpected(floating.error());
  for (const auto& picture : *floating)
    total_image_size += picture.image.bytes.size();
  size_t next_picture = 0;
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
  auto note_refs = ReadNoteReferences(table, fib);
  if (!note_refs) {
    return std::unexpected(note_refs.error());
  }
  auto comment_anchors = ReadCommentAnchors(table, fib);
  if (!comment_anchors) {
    return std::unexpected(comment_anchors.error());
  }
  auto bookmarks = ReadBookmarks(table, fib);
  if (!bookmarks) {
    return std::unexpected(bookmarks.error());
  }
  // CP -> comment id (index + 1), matching w:commentReference/@w:id.
  std::map<uint32_t, int> comment_index;
  for (size_t i = 0; i < comment_anchors->size(); i++) {
    comment_index.emplace((*comment_anchors)[i], static_cast<int>(i) + 1);
  }
  // CP -> note index, so a 0x02 in the main story can be resolved to the note
  // whose body was read into Document::notes.
  struct NoteSite {
    int id = 0;
    bool endnote = false;
  };
  std::map<uint32_t, NoteSite> note_index;
  const size_t footnote_refs =
      note_refs->footnote_cps.empty() ? 0 : note_refs->footnote_cps.size() - 1;
  const size_t endnote_refs =
      note_refs->endnote_cps.empty() ? 0 : note_refs->endnote_cps.size() - 1;
  for (size_t i = 0; i < footnote_refs; i++) {
    note_index.emplace(note_refs->footnote_cps[i],
                       NoteSite{static_cast<int>(i) + 1, false});
  }
  for (size_t i = 0; i < endnote_refs; i++) {
    note_index.emplace(note_refs->endnote_cps[i],
                       NoteSite{static_cast<int>(i) + 1, true});
  }
  model::Paragraph para;
  // One open table per nesting level. sprmPItap/sprmPDtap give a paragraph's
  // absolute depth, so the stack height follows the document, not the marks.
  struct TableFrame {
    model::Table table;
    std::vector<model::Cell> row;
    model::Cell cell;
  };
  std::vector<TableFrame> frames;
  std::vector<FieldFrame> fields;
  bool page_field = false;
  std::string hyperlink;
  size_t next_section = 0;
  const FormatRange* last_pap = nullptr;
  const FormatRange* last_chp = nullptr;
  const FormatRange* cached_pap = nullptr;
  const FormatRange* cached_chp = nullptr;
  const FormatRange* table_pap = nullptr;
  bool cached_table_flag = false;
  bool cached_row_end = false;
  int cached_depth = 0;
  std::string last_hyperlink;
  bool last_page_field = false;
  size_t last_run_count = 0;
  auto add_paragraph = [&](model::Paragraph value) {
    doc.blocks.push_back({false, doc.paragraphs.size()});
    doc.paragraphs.push_back(std::move(value));
  };
  auto attach_paragraph = [&](model::Paragraph value) {
    doc.paragraphs.push_back(std::move(value));
    frames.back().cell.blocks.push_back({false, doc.paragraphs.size() - 1});
  };
  // A cell mark always yields a cell, even an empty one (merged cells carry no
  // content); the row-end mark only keeps a cell that still has content.
  auto close_cell = [&](bool keep_empty) {
    TableFrame& frame = frames.back();
    if (keep_empty || !frame.cell.blocks.empty())
      frame.row.push_back(std::move(frame.cell));
    frame.cell = {};
  };
  auto close_row = [&]() {
    TableFrame& frame = frames.back();
    close_cell(false);
    if (!frame.row.empty()) {
      frame.table.rows.push_back(std::move(frame.row));
      frame.table.row_properties.emplace_back();
      frame.row.clear();
    }
  };
  // Pops the innermost table and hands it to its parent: a block of the
  // document body at depth 1, or a block of the enclosing cell below that.
  auto close_table = [&]() {
    if (frames.empty())
      return;
    close_row();
    const size_t index = doc.tables.size();
    doc.tables.push_back(std::move(frames.back().table));
    frames.pop_back();
    if (frames.empty())
      doc.blocks.push_back({true, index});
    else
      frames.back().cell.blocks.push_back({true, index});
  };
  auto set_table_depth = [&](int depth) {
    if (depth < 0)
      return;
    while (static_cast<int>(frames.size()) > depth)
      close_table();
    while (static_cast<int>(frames.size()) < depth)
      frames.emplace_back();
  };
  auto emit_bookmarks = [&](uint32_t cp) {
    auto found = bookmarks->marks.find(cp);
    if (found == bookmarks->marks.end()) {
      return;
    }
    for (const auto& mark : found->second) {
      model::Run run;
      run.bookmark_id = mark.id;
      run.bookmark_name = mark.name;
      run.bookmark_start = mark.start;
      para.runs.push_back(std::move(run));
    }
  };
  for (size_t i = 0; i < chars->size(); i++) {
    const Char& ch = (*chars)[i];
    const auto text = ch.text();
    const char control = text.size() == 1 ? text[0] : '\0';
    emit_bookmarks(ch.cp);
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
    if (control == '\x13') {
      fields.push_back({});
      continue;
    }
    if (!fields.empty() && fields.back().in_instruction && control == '\x14') {
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
      fields.back().instruction += text;
      continue;
    }
    if (next_picture < floating->size() &&
        (*floating)[next_picture].cp == ch.cp) {
      model::Run picture_run;
      picture_run.image_index = static_cast<int>(doc.images.size());
      doc.images.push_back(std::move((*floating)[next_picture++].image));
      para.runs.push_back(std::move(picture_run));
      last_run_count = 0;
    }
    if (control == '\x08')
      continue;
    const auto* pap =
        cached_pap && ch.fc >= cached_pap->first && ch.fc < cached_pap->last
            ? cached_pap
            : Find(*papx, ch.fc);
    cached_pap = pap;
    if (pap != table_pap) {
      cached_table_flag = false;
      cached_row_end = false;
      cached_depth = 0;
      if (pap) {
        WalkParagraph(pap->sprms, data, [&](uint16_t op, Bytes v) {
          if (op == kSprmPFInTable)
            cached_table_flag = v[0] != 0;
          if (op == kSprmPFTtp || op == kSprmPFInnerTtp)
            cached_row_end = v[0] != 0;
          if (op == kSprmPItap)
            cached_depth = U16(v, 0);
          if (op == kSprmPDtap)
            cached_depth = std::max(0, cached_depth + S16(v, 0));
        });
      }
      table_pap = pap;
    }
    // Depth is authoritative when the document carries sprmPItap; otherwise an
    // in-table paragraph means "one level deep", which is what pre-Word-97
    // producers and LibreOffice write.
    if (pap) {
      const int wanted =
          cached_depth > 0 ? cached_depth : (cached_table_flag ? 1 : 0);
      set_table_depth(wanted);
    }
    if (control == '\x07') {
      if (frames.empty())
        set_table_depth(1);
      if (cached_row_end) {
        // The row-end mark is itself a paragraph (the TTP): keep any content
        // that belongs to the last cell before the row closes.
        if (!para.runs.empty()) {
          ApplyParagraphSprms(para, pap, data);
          frames.back().cell.blocks.push_back({false, doc.paragraphs.size()});
          doc.paragraphs.push_back(std::move(para));
          para = {};
        }
        close_cell(false);
        auto finished = FinishTableRow(frames.back().table, frames.back().row,
                                       para, pap, data);
        if (!finished) {
          return std::unexpected(finished.error());
        }
      } else {
        if (!para.runs.empty()) {
          ApplyParagraphSprms(para, pap, data);
          frames.back().cell.blocks.push_back({false, doc.paragraphs.size()});
          doc.paragraphs.push_back(std::move(para));
          para = {};
        }
        close_cell(true);
      }
      continue;
    }
    if (ch.newline) {
      ApplyParagraphSprms(para, pap, data);
      if (frames.empty())
        add_paragraph(std::move(para));
      else
        attach_paragraph(std::move(para));
      para = {};
      continue;
    }
    if (control == '\x0c') {
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
    if (control == '\x05') {
      // Comment reference mark: the comment itself is written to comments.xml.
      auto found = comment_index.find(ch.cp);
      if (found != comment_index.end()) {
        model::Run mark;
        mark.comment_ref = found->second;
        para.runs.push_back(std::move(mark));
      }
      continue;
    }
    if (control == '\x02') {
      auto found = note_index.find(ch.cp);
      if (found != note_index.end()) {
        model::Run note;
        note.note_ref = found->second.id;
        note.note_endnote = found->second.endnote;
        para.runs.push_back(std::move(note));
      }
      continue;
    }
    const auto* chp =
        cached_chp && ch.fc >= cached_chp->first && ch.fc < cached_chp->last
            ? cached_chp
            : Find(*chpx, ch.fc);
    cached_chp = chp;
    if (!text.empty() && control != '\x01' && last_run_count != 0 &&
        para.runs.size() == last_run_count &&
        para.runs.back().image_index < 0 && pap == last_pap &&
        chp == last_chp && hyperlink == last_hyperlink &&
        page_field == last_page_field) {
      para.runs.back().text += text;
      continue;
    }
    auto remember_run = [&]() {
      last_pap = pap;
      last_chp = chp;
      last_hyperlink = hyperlink;
      last_page_field = page_field;
      last_run_count = para.runs.size();
    };
    auto baseline = [&]() {
      model::Run value = pap ? InheritedRun(doc, pap->style) : model::Run{};
      value.hyperlink = hyperlink;
      value.page_field = page_field;
      return value;
    };
    model::Run r = baseline();
    if (chp) {
      Walk(chp->sprms, [&](uint16_t op, Bytes v) {
        // sprmCPlain discards direct character formatting accumulated so far.
        if (op == kSprmCPlain) {
          r = baseline();
        } else {
          ApplyRun(r, op, v, doc.fonts);
        }
      });
    }
    if (control == '\x01' && r.object_offset >= 0 && !r.ole_object &&
        !r.embedded_object && !r.binary_data) {
      auto picture = ReadInlinePicture(
          data, r.object_offset, detail::kMaxTotalImageSize - total_image_size);
      if (!picture)
        return std::unexpected(picture.error());
      if (picture->bytes.size() > detail::kMaxTotalImageSize - total_image_size)
        return std::unexpected(Error{"total image size exceeds limit"});
      total_image_size += picture->bytes.size();
      r.image_index = static_cast<int>(doc.images.size());
      doc.images.push_back(std::move(*picture));
      para.runs.push_back(std::move(r));
      remember_run();
      continue;
    }
    if (!para.runs.empty()) {
      auto& back = para.runs.back();
      if (SameRunFormatting(back, r)) {
        back.text += text;
        remember_run();
        continue;
      }
    }
    r.text = text;
    para.runs.push_back(std::move(r));
    remember_run();
  }
  while (!frames.empty())
    close_table();
  if (!para.runs.empty() || doc.blocks.empty())
    add_paragraph(std::move(para));
  auto notes = ReadNotes(doc, word, table, fib, *chpx, *papx);
  if (!notes) {
    return std::unexpected(notes.error());
  }
  auto comments = ReadComments(doc, word, table, fib, *chpx, *papx);
  if (!comments) {
    return std::unexpected(comments.error());
  }
  // Header/footer stories are per section: PlcfHdd holds six per section, in
  // document order, so section k reads entries 6k..6k+5.
  const size_t section_count =
      doc.section_breaks.empty() ? 1 : doc.section_breaks.size() + 1;
  auto headers =
      ReadHeaders(doc, word, table, fib, *chpx, *papx, section_count);
  if (!headers)
    return std::unexpected(headers.error());
  for (size_t k = 0; k < section_count; k++) {
    detail::SectionHeaders& stories = (*headers)[k];
    model::Section* target = nullptr;
    if (k + 1 == section_count)
      target = &doc.section;
    else if (k < doc.section_breaks.size())
      target = &doc.section_breaks[k].second;
    if (target == nullptr)
      continue;
    target->headers = stories;
    if (!stories[4].empty() || !stories[5].empty())
      target->first_page_different = true;
    if (!stories[0].empty() || !stories[2].empty())
      target->odd_even_different = true;
  }
  return doc;
}
}  // namespace doccraft::doc
