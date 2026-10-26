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

#ifndef SRC_MODEL_DOCUMENT_H_
#define SRC_MODEL_DOCUMENT_H_
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace doccraft::model {

enum class ImageFormat { kPng, kJpeg, kWmf, kEmf };

enum class ImageWrap { kNone, kSquare, kTight, kThrough, kTopAndBottom };
// Same margin/page/text origin semantics as floating tables. Text means the
// column horizontally and the anchor paragraph vertically.
struct ImageAnchor {
  int horizontal = 0, vertical = 0;
  int x = 0, y = 0;
  ImageWrap wrap = ImageWrap::kSquare;
  int wrap_side = 0;  // Both, left, right, largest.
  bool behind_text = false, locked = false;
};

struct Image {
  ImageFormat format = ImageFormat::kPng;
  std::vector<uint8_t> bytes;
  int width = 0, height = 0;  // Display size in twips.
  std::optional<ImageAnchor> anchor;
};

// Run::field_char markers, matching w:fldChar/@w:fldCharType.
inline constexpr int kFieldCharBegin = 0;
inline constexpr int kFieldCharSeparate = 1;
inline constexpr int kFieldCharEnd = 2;

struct Border {
  bool set = false;
  int type = 0, width = 0, space = 0, color = -1;
  bool shadow = false, frame = false;

  bool operator==(const Border&) const = default;
};

struct ColumnSpec {
  int index = 0, width = -1, space = -1;
};

struct Run {
  std::string text;
  std::string hyperlink;
  bool page_field = false;
  std::string font;
  std::string font_hansi, font_east_asia, font_cs;
  std::string lang, lang_east_asia, lang_cs;
  int font_hint = -1;
  int underline_color = -1, text_effect = -1;
  bool no_proof = false, rtl = false, complex_script = false;
  bool web_hidden = false, spec_vanish = false, snap_to_grid = false;
  bool field_hidden = false, field_hidden_set = false;
  int color_cs = -1;
  bool no_proof_set = false, rtl_set = false, complex_script_set = false;
  bool web_hidden_set = false, spec_vanish_set = false,
       snap_to_grid_set = false;
  int size = 0;
  int size_cs = 0, kern = -1, position = 0;
  bool position_set = false;
  bool bold_cs = false, italic_cs = false;
  bool bold_cs_set = false, italic_cs_set = false;
  bool bold = false, italic = false, strike = false, double_strike = false;
  bool hidden = false;
  bool bold_set = false, italic_set = false, strike_set = false;
  bool double_strike_set = false, hidden_set = false;
  bool shadow = false, emboss = false, imprint = false, outline = false;
  bool small_caps = false, caps = false;
  bool shadow_set = false, emboss_set = false, imprint_set = false;
  bool outline_set = false, small_caps_set = false, caps_set = false;
  int underline = 0, super_sub = 0, spacing = 0, scale = 0;
  int emphasis = -1;
  bool underline_set = false, super_sub_set = false, spacing_set = false;
  bool scale_set = false;
  int color = -1, highlight = -1;
  int shade = -1;
  int shade_fore = -1, shade_pattern = 0;
  Border border;
  bool color_rgb = false;
  int symbol_character = -1;
  std::string symbol_font;
  // Complex field markers: 0 = begin, 1 = separator, 2 = end (w:fldChar).
  int field_char = -1;
  // Manual break carried by this run: 1 = page (w:br/@w:type="page"),
  // 2 = column. 0 means the run holds no break.
  int break_type = 0;
  // Field instruction text, written as w:instrText between begin and separator.
  std::string instr_text;
  int object_offset = -1;
  int image_index = -1;  // Index into Document::images for U+0001.
  bool binary_data = false, ole_object = false, embedded_object = false;
  bool revision_insert = false, revision_delete = false,
       property_revision = false;
  // Footnote/endnote reference: id within its own note class, as written to
  // w:footnoteReference / w:endnoteReference. -1 = not a reference.
  int note_ref = -1;
  bool note_endnote = false;
  // Comment anchor: index into Document::comments + 1 (w:commentReference id).
  int comment_ref = -1;
  // Bookmark marker: w:bookmarkStart / w:bookmarkEnd with this id and name.
  int bookmark_id = -1;
  bool bookmark_start = false;
  std::string bookmark_name;
  // sprmCFitText: width in twips a run is squeezed or stretched into.
  int fit_text = 0, fit_text_id = 0;
  // sprmCFELayout: East Asian layout unit (w:eastAsianLayout).
  bool ea_layout_set = false, ea_vert = false, ea_combine = false,
       ea_vert_compress = false;
  int ea_brackets = 0, ea_id = 0;
  int style = -1;

  // Defaulted equality is what run merging compares: every field participates,
  // so a newly added property can never be silently dropped from the merge.
  bool operator==(const Run&) const = default;
};

struct TabStop {
  int position = 0, align = 0, leader = 0;
};

struct Paragraph {
  std::vector<Run> runs;
  int style = -1;
  int align = -1, left = 0, right = 0, first = 0;
  bool left_set = false, right_set = false, first_set = false;
  int before = -1, after = -1, line = 0;
  bool line_auto = false;
  bool widow_control = true, widow_control_set = false;
  bool bidi = false, bidi_set = false;
  bool suppress_hyphenation = false, suppress_hyphenation_set = false;
  bool before_auto = false, after_auto = false, contextual_spacing = false;
  bool before_auto_set = false, after_auto_set = false,
       contextual_spacing_set = false;
  bool mirror_indents = false, mirror_indents_set = false;
  bool snap_to_grid = false, snap_to_grid_set = false;
  bool adjust_right = false, adjust_right_set = false;
  bool no_line_number = false, no_line_number_set = false;
  bool word_wrap = false, word_wrap_set = false;
  bool kinsoku = false, kinsoku_set = false;
  bool overflow_punct = false, overflow_punct_set = false;
  bool top_line_punct = false, top_line_punct_set = false;
  bool auto_space_de = false, auto_space_de_set = false;
  bool auto_space_dn = false, auto_space_dn_set = false;
  bool keep_next = false, keep_lines = false, page_break_before = false;
  bool keep_next_set = false, keep_lines_set = false,
       page_break_before_set = false;
  int outline = -1, list_id = -1, list_level = 0;
  int list_level_inc = 0;
  // sprmPItap/sprmPDtap: absolute table depth (0 = not in a table).
  int table_depth = 0;
  int nest = 0;
  bool nest_set = false;
  int frame_wrap = -1, frame_anchor = -1;
  int frame_x = -1, frame_y = -1, frame_width = -1, frame_height = -1;
  bool frame_height_exact = false;
  int frame_h_space = -1, frame_v_space = -1;
  bool frame_lock = false, frame_lock_set = false;
  // sprmPDcs: 1 = drop cap in text, 2 = drop cap in margin.
  int drop_cap = -1, drop_cap_lines = 0;
  // sprmPWAlignFont: 0 top, 1 center, 2 baseline, 3 bottom, 4 auto.
  int text_alignment = -1;
  // sprmPFrameTextFlow: 1 = vertical, 2 = bottom-to-top.
  int text_flow = -1;
  // Character-unit indents (hundredths) and line-unit spacing.
  int right_chars = 0, left_chars = 0, first_line_chars = 0;
  bool right_chars_set = false, left_chars_set = false,
       first_line_chars_set = false;
  int before_lines = 0, after_lines = 0;
  bool before_lines_set = false, after_lines_set = false;
  std::vector<TabStop> tabs;
  std::array<Border, 4> borders;
  std::array<Border, 3> extra_borders;
  int shade = -1;
  int shade_fore = -1, shade_pattern = 0;
};

// Conditional formatting of a table style (sprmTCnf/sprmPCnf/sprmCCnf), emitted
// as a w:tblStylePr block keyed by the CNFOperand.cnfc condition.
struct ConditionalProperties {
  int type = 0;  // cnfc bit value: 1 first row ... 0x0800 bottom left cell
  Paragraph paragraph;
  Run run;
  std::array<Border, 8>
      borders;  // top/left/bottom/right/insideH/insideV/diagonals
  int shade = -1, shade_fore = -1, shade_pattern = 0;
  std::array<int, 4> margins{{-1, -1, -1, -1}};
  int vertical_align = -1;
  bool no_wrap = false, no_wrap_set = false;
};

struct Style {
  std::string name;
  int type = 0, base = -1, next = -1;
  Paragraph paragraph;
  Run run;
  Run resolved_run;
  Paragraph resolved_paragraph;
  std::vector<uint8_t> table_sprms;
  // Table-style properties (tblPr of w:style) and per-condition blocks.
  int row_band = 0, col_band = 0;
  std::array<int, 4> cell_margins{{-1, -1, -1, -1}};
  std::vector<ConditionalProperties> conditionals;
};

struct Section {
  int width = 12240, height = 15840;
  int left = 1134, right = 1134, top = 1134, bottom = 1134;
  int gutter = 0, orientation = 0, break_type = 0;
  bool break_specified = false;
  int header_distance = -1, footer_distance = -1;
  bool first_page_different = false, odd_even_different = false;
  int columns = 1, column_space = -1;
  bool columns_even = true;
  bool columns_separator = false, bidi = false, rtl_gutter = false;
  int vertical_justify = -1;
  int page_start = -1, page_format = -1;
  bool page_restart = false;
  int grid_line_pitch = -1, grid_mode = -1, text_flow = -1;
  int line_count_by = 0, line_restart = 0, line_start = 0, line_distance = -1;
  int footnote_position = -1, footnote_restart = -1, footnote_start = -1,
      footnote_format = -1;
  int endnote_restart = -1, endnote_start = -1, endnote_format = -1;
  bool endnote_at_section_end = false;
  int paper_first = -1, paper_other = -1;
  // sprmSFProtected: true when the section opts out of form protection.
  bool form_unprotected = false, form_unprotected_set = false;
  // sprmSPgbProp: page border scope and measurement origin.
  int page_border_apply_to = -1, page_border_offset_from = -1;
  std::vector<ColumnSpec> column_specs;
  int chapter_separator = -1, chapter_heading = -1, grid_char_space = -1;
  std::array<Border, 4> page_borders;
  std::array<std::vector<Paragraph>, 6> headers;
};

struct ListLevel {
  int format = 0, start = 1, left = 0, first = 0, suffix = 0;
  int style = -1;
  std::string text, font;
};

struct List {
  int id = 0;
  std::vector<ListLevel> levels;
};

struct CellProperties {
  int width = -1, width_type = 3;
  std::array<Border, 6> borders;
  int shade = -1, shade_fore = -1, shade_pattern = 0;
  std::array<int, 4> margins{{-1, -1, -1, -1}};
  int vertical_merge = 0, horizontal_merge = 0, text_direction = -1;
  int vertical_align = -1;
  bool no_wrap = false, fit_text = false, hide_mark = false;
};

struct Block {
  bool table = false;
  size_t index = 0;
};

// A comment: author metadata plus the note body.
struct Comment {
  std::string author;
  std::string initials;
  std::vector<Paragraph> paragraphs;
};

// A footnote or endnote: the note body plus its identity.
struct Note {
  bool endnote = false;
  std::vector<Paragraph> paragraphs;
};

// One table cell: an ordered block sequence, like the document body. Blocks
// index Document::paragraphs / Document::tables, so nested tables are just
// tables that happen to be reachable from a cell.
struct Cell {
  std::vector<Block> blocks;
};

struct RowProperties {
  std::vector<CellProperties> cells;
  std::vector<int> columns;
  bool cant_split = false, header = false;
  int height = 0;
};

struct Table {
  std::vector<int> columns;
  std::vector<std::vector<Cell>> rows;
  std::vector<RowProperties> row_properties;
  std::array<Border, 6> borders;
  int style = -1;
  int tblp_x = -1, tblp_y = -1;
  std::array<int, 4> from_text{{-1, -1, -1, -1}};
  // sprmTPc: 0 margin, 1 page, 2 text (3 = not absolutely positioned).
  int anchor_v = -1, anchor_h = -1;
  // sprmTTlp: bit mask for w:tblLook (-1 = not specified).
  int look = -1;
  // sprmTCHorzBands / sprmTCVertBands.
  int row_band = 0, col_band = 0;
  std::array<int, 4> cell_margins{{-1, -1, -1, -1}};
  int width = 5000, width_type = 2;
  bool auto_fit = false;
  int indent = -1, align = -1;
  int cell_spacing = -1, left_origin = -1, gap_half = 0;
  bool bidi = false, keep_next = false, no_overlap = false;
  int shade = -1, shade_fore = -1, shade_pattern = 0;
};

struct Document {
  std::vector<Image> images;
  std::vector<Paragraph> paragraphs;
  std::vector<Style> styles;
  Section section;
  std::vector<std::pair<size_t, Section>> section_breaks;
  std::vector<std::string> fonts;
  std::vector<List> lists;
  std::vector<Note> notes;
  std::vector<Comment> comments;
  std::vector<int> list_overrides;
  std::vector<Table> tables;
  std::vector<Block> blocks;
};

Document FromText(const std::string& text);
}  // namespace doccraft::model

#endif  // SRC_MODEL_DOCUMENT_H_
