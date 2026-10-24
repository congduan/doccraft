#ifndef DOCCRAFT_MODEL_DOCUMENT_H_
#define DOCCRAFT_MODEL_DOCUMENT_H_
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace doccraft::model {
struct Border { bool set = false; int type = 0, width = 0, space = 0, color = -1; bool shadow = false, frame = false; };
struct Run {
  std::string text;
  std::string hyperlink;
  bool page_field = false;
  std::string font;
  int size = 0;
  bool bold = false, italic = false, strike = false, double_strike = false;
  bool hidden = false;
  bool bold_set = false, italic_set = false, strike_set = false;
  bool double_strike_set = false, hidden_set = false;
  bool shadow = false, emboss = false, imprint = false, outline = false;
  bool small_caps = false, caps = false;
  bool shadow_set = false, emboss_set = false, imprint_set = false;
  bool outline_set = false, small_caps_set = false, caps_set = false;
  int underline = 0, super_sub = 0, spacing = 0, scale = 0;
  bool underline_set = false, super_sub_set = false, spacing_set = false;
  bool scale_set = false;
  int color = -1, highlight = -1;
  int shade = -1;
  int shade_fore = -1, shade_pattern = 0;
  Border border;
  bool color_rgb = false;
  int symbol_character = -1;
  std::string symbol_font;
  int style = -1;
};
struct TabStop { int position = 0, align = 0, leader = 0; };
struct Paragraph {
  std::vector<Run> runs;
  int style = -1;
  int align = -1, left = 0, right = 0, first = 0;
  bool left_set = false, right_set = false, first_set = false;
  int before = -1, after = -1, line = 0;
  bool line_auto = false;
  bool keep_next = false, keep_lines = false, page_break_before = false;
  bool keep_next_set = false, keep_lines_set = false, page_break_before_set = false;
  int outline = -1, list_id = -1, list_level = 0;
  std::vector<TabStop> tabs;
  std::array<Border,4> borders;
  std::array<Border,3> extra_borders;
  int shade = -1;
  int shade_fore = -1, shade_pattern = 0;
};
struct Style {
  std::string name;
  int type = 0, base = -1, next = -1;
  Paragraph paragraph;
  Run run;
  Run resolved_run;
  Paragraph resolved_paragraph;
  std::vector<uint8_t> table_sprms;
};
struct Section {
  int width = 12240, height = 15840;
  int left = 1134, right = 1134, top = 1134, bottom = 1134;
  int gutter = 0, orientation = 0, break_type = 0;
  bool break_specified = false;
  int header_distance = -1, footer_distance = -1;
  bool first_page_different = false, odd_even_different = false;
  int line_count_by = 0, line_restart = 0, line_start = 0, line_distance = -1;
  int footnote_position = -1, footnote_restart = -1, footnote_start = -1, footnote_format = -1;
  std::array<std::vector<Paragraph>,6> headers;
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
  std::array<Border,6> borders;
  int shade = -1, shade_fore = -1, shade_pattern = 0;
  std::array<int,4> margins{{-1,-1,-1,-1}};
  int vertical_merge = 0, horizontal_merge = 0, text_direction = -1;
  int vertical_align = -1;
  bool no_wrap = false, fit_text = false, hide_mark = false;
};
struct RowProperties {
  std::vector<CellProperties> cells;
  bool cant_split = false, header = false;
  int height = 0;
};
struct Table {
  std::vector<int> columns;
  std::vector<std::vector<Paragraph>> rows;
  std::vector<RowProperties> row_properties;
  std::array<Border,6> borders;
  int width = 5000, width_type = 2;
  bool auto_fit = false;
  int indent = -1, align = -1;
  bool bidi = false;
  int shade = -1, shade_fore = -1, shade_pattern = 0;
};
struct Block {
  bool table = false;
  size_t index = 0;
};
struct Document {
  std::vector<Paragraph> paragraphs;
  std::vector<Style> styles;
  Section section;
  std::vector<std::pair<size_t,Section>> section_breaks;
  std::vector<std::string> fonts;
  std::vector<List> lists;
  std::vector<int> list_overrides;
  std::vector<Table> tables;
  std::vector<Block> blocks;
};
Document FromText(const std::string& text);
}  // namespace doccraft::model
#endif
