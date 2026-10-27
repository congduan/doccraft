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
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "doc/format/formatted_helpers.h"
#include "doc/format/sprm.h"
#include "doc/format/sprm_codes.h"

namespace doccraft::doc {
using detail::AppendUtf8;
using detail::ApplyPara;
using detail::ApplyRun;
using detail::Bytes;
using detail::Char;
using detail::ClampTableDepth;
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
using detail::S32;
using detail::U16;
using detail::U32;
using detail::Valid;
using detail::Walk;
using detail::WalkParagraph;

namespace detail {
// [MS-DOC] §2.9.215: the seven-bit isprm of Prm0 names one sprm. Entries
// absent from the table are reserved or have no supported property mapping.
uint16_t Prm0Opcode(uint8_t isprm) {
  static const auto kOpcodes = [] {
    std::array<uint16_t, 128> result{};
    result[0x00] = detail::kSprmCLbcCRJ;
    result[0x04] = detail::kSprmPIncLvl;
    result[0x05] = detail::kSprmPJc;
    result[0x07] = detail::kSprmPFKeep;
    result[0x08] = detail::kSprmPFKeepFollow;
    result[0x09] = detail::kSprmPFPageBreakBefore;
    result[0x0C] = detail::kSprmPIlvl;
    result[0x0D] = detail::kSprmPFMirrorIndents;
    result[0x0E] = detail::kSprmPFNoLineNumb;
    result[0x0F] = detail::kSprmPTtwo;
    result[0x18] = detail::kSprmPFInTable;
    result[0x19] = detail::kSprmPFTtp;
    result[0x1D] = detail::kSprmPPc;
    result[0x25] = detail::kSprmPWr;
    result[0x2C] = detail::kSprmPFNoAutoHyph;
    result[0x32] = detail::kSprmPFLocked;
    result[0x33] = detail::kSprmPFWidowControl;
    result[0x35] = detail::kSprmPFKinsoku;
    result[0x36] = detail::kSprmPFWordWrap;
    result[0x37] = detail::kSprmPFOverflowPunct;
    result[0x38] = detail::kSprmPFTopLinePunct;
    result[0x39] = detail::kSprmPFAutoSpaceDE;
    result[0x3A] = detail::kSprmPFAutoSpaceDN;
    result[0x41] = detail::kSprmCFRMarkDel;
    result[0x42] = detail::kSprmCFRMarkIns;
    result[0x43] = detail::kSprmCFFldVanish;
    result[0x47] = detail::kSprmCFData;
    result[0x4B] = detail::kSprmCFOle2;
    result[0x4D] = detail::kSprmCHighlight;
    result[0x4E] = detail::kSprmCFEmboss;
    result[0x4F] = detail::kSprmCSfxText;
    result[0x50] = detail::kSprmCFWebHidden;
    result[0x51] = detail::kSprmCFSpecVanish;
    result[0x53] = detail::kSprmCPlain;
    result[0x55] = detail::kSprmCFBold;
    result[0x56] = detail::kSprmCFItalic;
    result[0x57] = detail::kSprmCFStrike;
    result[0x58] = detail::kSprmCFOutline;
    result[0x59] = detail::kSprmCFShadow;
    result[0x5A] = detail::kSprmCFSmallCaps;
    result[0x5B] = detail::kSprmCFCaps;
    result[0x5C] = detail::kSprmCFVanish;
    result[0x5E] = detail::kSprmCKul;
    result[0x62] = detail::kSprmCIco;
    result[0x68] = detail::kSprmCIss;
    result[0x73] = detail::kSprmCFDStrike;
    result[0x74] = detail::kSprmCFImprint;
    result[0x75] = detail::kSprmCFSpec;
    result[0x76] = detail::kSprmCFObj;
    result[0x78] = detail::kSprmPOutLvl;
    result[0x7B] = detail::kSprmCFSdtVanish;
    result[0x7C] = detail::kSprmCNeedFontFixup;
    return result;
  }();
  return kOpcodes[isprm];
}
}  // namespace detail

std::expected<model::Document, Error> ParseFormatted(Bytes word, Bytes table,
                                                     const Fib& fib,
                                                     Bytes data) {
  std::vector<std::vector<uint8_t>> prcs;
  auto chars = ReadChars(word, table, fib, fib.ccp_text, 0, &prcs);
  if (!chars)
    return std::unexpected(chars.error());
  model::Document doc;
  auto chpx = ReadFkps(word, table, fib.chpx_bte, false, &doc.warnings);
  if (!chpx)
    return std::unexpected(chpx.error());
  // PAPX can carry table structure; losing it can change cell boundaries.
  auto papx = ReadFkps(word, table, fib.papx_bte, true);
  if (!papx)
    return std::unexpected(papx.error());
  size_t total_image_size = 0;
  auto floating = ReadFloatingPictures(word, table, fib);
  if (!floating)
    return std::unexpected(floating.error());
  for (const auto& picture : *floating)
    total_image_size += picture.image.bytes.size();
  size_t next_picture = 0;
  doc.fonts = ReadFonts(table, fib.fonts);
  doc.styles = ReadStyles(table, fib.stsh, doc.fonts);
  auto textboxes =
      detail::ReadTextboxes(word, table, fib, *chpx, *papx, doc.fonts);
  if (!textboxes)
    return std::unexpected(textboxes.error());
  doc.textboxes = std::move(*textboxes);
  for (const auto& item : *floating) {
    if (!item.shape)
      continue;
    for (auto& box : doc.textboxes) {
      if (box.shape_id == item.shape->id)
        box.shape_type = item.shape->type;
    }
  }
  std::map<uint32_t, size_t> textbox_anchors;
  for (size_t i = 0; i < doc.textboxes.size(); i++) {
    if (!textbox_anchors.emplace(doc.textboxes[i].anchor_cp, i).second)
      return std::unexpected(Error{"duplicate textbox anchor CP"});
  }
  auto walk_piece_prm = [&](uint16_t prm, auto fn) {
    if (!prm)
      return;
    if (prm & 1) {
      Walk(prcs[prm >> 1], fn);
    } else {
      uint16_t op = detail::Prm0Opcode((prm >> 1) & 0x7F);
      if (op) {
        uint8_t value = static_cast<uint8_t>(prm >> 8);
        fn(op, Bytes(&value, 1));
      }
    }
  };
  auto apply_piece_para = [&](model::Paragraph& paragraph, uint16_t prm) {
    walk_piece_prm(prm, [&](uint16_t op, Bytes v) {
      if ((op >> 10 & 7) == 1)
        ApplyPara(paragraph, op, v);
    });
  };
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
  std::map<uint32_t, uint8_t> field_status;
  if (fib.fields_main.size) {
    if (!Valid(table, fib.fields_main) || fib.fields_main.size < 4 ||
        (fib.fields_main.size - 4) % 6)
      return std::unexpected(Error{"invalid main field PLC"});
    Bytes plc = table.subspan(fib.fields_main.offset, fib.fields_main.size);
    const size_t count = (plc.size() - 4) / 6;
    const size_t fld_offset = 4 * (count + 1);
    std::vector<uint32_t> starts;
    uint32_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
      const uint32_t cp = U32(plc, 4 * i);
      // Fld.fldch packs the 5-bit character code with three marker flags.
      const uint8_t kind = plc[fld_offset + 2 * i] & 0x1F;
      if ((i && cp <= previous) || cp >= fib.ccp_text ||
          (kind != 0x13 && kind != 0x14 && kind != 0x15))
        return std::unexpected(Error{"invalid main field PLC entry"});
      previous = cp;
      if (kind == 0x13) {
        starts.push_back(cp);
      } else if (kind == 0x15 && !starts.empty()) {
        field_status.emplace(starts.back(), plc[fld_offset + 2 * i + 1]);
        starts.pop_back();
      }
    }
    if (U32(plc, 4 * count) < previous)
      return std::unexpected(Error{"invalid main field PLC guard"});
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
  int pending_ole = -1;
  bool warned_field_control = false;
  bool page_field = false;
  std::string hyperlink;
  size_t next_section = 0;
  const FormatRange* last_pap = nullptr;
  const FormatRange* last_chp = nullptr;
  uint16_t last_prm = 0;
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
    if (control == '\x13') {
      fields.push_back({});
      if (auto found = field_status.find(ch.cp); found != field_status.end()) {
        fields.back().locked = (found->second & 0x10) != 0;
        fields.back().dirty = (found->second & 0x04) != 0;
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
      // [MS-DOC] sprmCPicLocation on the field separator is the decimal
      // ObjectPool storage name, not a Data-stream picture offset.
      model::Run object;
      if (const auto* x = Find(*chpx, ch.fc)) {
        Walk(x->sprms, [&](uint16_t op, Bytes v) {
          ApplyRun(object, op, v, doc.fonts);
        });
      }
      walk_piece_prm(ch.prm, [&](uint16_t op, Bytes v) {
        if ((op >> 10 & 7) == 2)
          ApplyRun(object, op, v, doc.fonts);
      });
      if (object.ole_object) {
        if (object.object_offset < 0)
          return std::unexpected(Error{"OLE field has no ObjectPool id"});
        std::istringstream instruction(frame.instruction);
        std::string kind, program_id;
        instruction >> kind >> program_id;
        if (kind != "EMBED" && kind != "LINK" && kind != "CONTROL")
          return std::unexpected(Error{"invalid OLE field instruction"});
        model::OleObject value;
        value.storage_id = object.object_offset;
        value.program_id = std::move(program_id);
        pending_ole = static_cast<int>(doc.ole_objects.size());
        doc.ole_objects.push_back(std::move(value));
        model::Run marker;
        marker.ole_index = pending_ole;
        para.runs.push_back(std::move(marker));
        last_run_count = 0;
      }
      sync_field_state();
      continue;
    }
    if (!fields.empty() && control == '\x15') {
      pending_ole = -1;
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
      // Binary object markers sometimes occur inside fast-saved field code
      // ranges. XML 1.0 cannot represent them, including as numeric refs.
      if (control && static_cast<unsigned char>(control) < 32 &&
          control != '\t' && control != '\n' && control != '\r') {
        if (!warned_field_control) {
          doc.warnings.push_back(
              "ignored non-XML control in field instruction");
          warned_field_control = true;
        }
        continue;
      }
      fields.back().instruction += text;
      continue;
    }
    if (next_picture < floating->size() &&
        (*floating)[next_picture].cp == ch.cp) {
      model::Run picture_run;
      auto& item = (*floating)[next_picture++];
      if (item.shape) {
        const bool textbox = std::any_of(
            doc.textboxes.begin(), doc.textboxes.end(), [&](const auto& box) {
              return box.shape_id == item.shape->id;
            });
        if (!textbox) {
          picture_run.shape_index = static_cast<int>(doc.shapes.size());
          doc.shapes.push_back(std::move(*item.shape));
          para.runs.push_back(std::move(picture_run));
        }
      } else {
        picture_run.image_index = static_cast<int>(doc.images.size());
        doc.images.push_back(std::move(item.image));
        para.runs.push_back(std::move(picture_run));
      }
      last_run_count = 0;
    }
    if (auto box = textbox_anchors.find(ch.cp); box != textbox_anchors.end()) {
      model::Run textbox_run;
      textbox_run.textbox_index = static_cast<int>(box->second);
      para.runs.push_back(std::move(textbox_run));
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
            cached_depth = ClampTableDepth(S32(v, 0));
          if (op == kSprmPDtap)
            cached_depth =
                ClampTableDepth(static_cast<int64_t>(cached_depth) + S32(v, 0));
        });
      }
      table_pap = pap;
    }
    // A fast-saved piece can carry paragraph/table sprms independently of
    // PAPX. Resolve both before interpreting cell and row marks.
    bool in_table = cached_table_flag;
    bool row_end = cached_row_end;
    int depth = cached_depth;
    bool piece_table_set = false;
    walk_piece_prm(ch.prm, [&](uint16_t op, Bytes v) {
      if (op == kSprmPFInTable) {
        in_table = v[0] != 0;
        piece_table_set = true;
      } else if (op == kSprmPFTtp || op == kSprmPFInnerTtp) {
        row_end = v[0] != 0;
        piece_table_set = true;
      } else if (op == kSprmPItap && v.size() >= 4) {
        depth = ClampTableDepth(S32(v, 0));
        piece_table_set = true;
      } else if (op == kSprmPDtap && v.size() >= 4) {
        depth = ClampTableDepth(static_cast<int64_t>(depth) + S32(v, 0));
        piece_table_set = true;
      }
    });
    // Depth is authoritative when present; otherwise fInTable means one
    // level. Character-only piece modifiers do not change table structure.
    // The reads above clamp through ClampTableDepth - the same policy as the
    // ApplyPara model field - because sprmPItap operands are attacker-
    // controlled and an unclamped depth builds a nesting chain the writer
    // cannot traverse. The min here re-asserts the bound in case a future
    // read path bypasses the clamp.
    if (pap || piece_table_set) {
      const int bounded = std::min(depth, detail::kMaxTableDepth);
      const int wanted = bounded > 0 ? bounded : (in_table ? 1 : 0);
      set_table_depth(wanted);
    }
    if (control == '\x07') {
      if (frames.empty())
        set_table_depth(1);
      if (row_end) {
        // The row-end mark is itself a paragraph (the TTP): keep any content
        // that belongs to the last cell before the row closes.
        if (!para.runs.empty()) {
          ApplyParagraphSprms(para, pap, data);
          apply_piece_para(para, ch.prm);
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
          apply_piece_para(para, ch.prm);
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
      apply_piece_para(para, ch.prm);
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
        apply_piece_para(para, ch.prm);
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
        para.runs.back().image_index < 0 && para.runs.back().shape_index < 0 &&
        para.runs.back().textbox_index < 0 && pap == last_pap &&
        chp == last_chp && ch.prm == last_prm && hyperlink == last_hyperlink &&
        page_field == last_page_field) {
      para.runs.back().text += text;
      continue;
    }
    auto remember_run = [&]() {
      last_pap = pap;
      last_chp = chp;
      last_prm = ch.prm;
      last_hyperlink = hyperlink;
      last_page_field = page_field;
      last_run_count = para.runs.size();
    };
    auto baseline = [&]() {
      model::Run value = pap ? InheritedRun(doc, pap->style) : model::Run{};
      value.hyperlink = hyperlink;
      value.page_field = page_field;
      if (page_field && !fields.empty()) {
        value.field_locked = fields.back().locked;
        value.field_dirty = fields.back().dirty;
      }
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
    walk_piece_prm(ch.prm, [&](uint16_t op, Bytes v) {
      if ((op >> 10 & 7) != 2)
        return;
      if (op == kSprmCPlain)
        r = baseline();
      else
        ApplyRun(r, op, v, doc.fonts);
    });
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
      if (pending_ole >= 0) {
        doc.ole_objects[static_cast<size_t>(pending_ole)].preview_image_index =
            r.image_index;
        pending_ole = -1;
        last_run_count = 0;
        continue;
      }
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
  if (!headers) {
    doc.warnings.push_back("skipped header stories: " +
                           headers.error().message);
  } else {
    auto header_boxes =
        detail::ReadHeaderTextboxes(word, table, fib, *chpx, *papx, doc.fonts);
    if (!header_boxes)
      return std::unexpected(header_boxes.error());
    for (auto& box : *header_boxes) {
      bool attached = false;
      for (size_t k = 0; k < section_count && !attached; ++k) {
        for (size_t story = 0; story < 6; ++story) {
          const size_t index = 6 + k * 6 + story;
          const auto plc = table.subspan(fib.headers.offset, fib.headers.size);
          if ((index + 1) * 4 >= plc.size())
            return std::unexpected(Error{"header textbox story PLC too short"});
          const uint32_t first = detail::U32(plc, index * 4);
          const uint32_t last = detail::U32(plc, (index + 1) * 4);
          if (box.anchor_cp < first || box.anchor_cp >= last)
            continue;
          auto& paragraphs = (*headers)[k][story];
          if (paragraphs.empty())
            paragraphs.push_back({});
          model::Run run;
          run.textbox_index = static_cast<int>(doc.textboxes.size());
          paragraphs.front().runs.insert(paragraphs.front().runs.begin(), run);
          doc.textboxes.push_back(std::move(box));
          attached = true;
          break;
        }
      }
      if (!attached)
        return std::unexpected(Error{"header textbox anchor outside headers"});
    }
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
  }
  return doc;
}
}  // namespace doccraft::doc
