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

std::string Save(const pugi::xml_document& d) {
  std::ostringstream out;
  d.save(out, "", pugi::format_raw, pugi::encoding_utf8);
  return out.str();
}

void StoryNamespaces(pugi::xml_node root) {
  Attr(root, "xmlns:w", kWordmlNamespace);
  Attr(root, "xmlns:r", kRelationshipsNamespace);
  Attr(root, "xmlns:v", "urn:schemas-microsoft-com:vml");
  Attr(root, "xmlns:o", "urn:schemas-microsoft-com:office:office");
  Attr(root, "xmlns:w10", "urn:schemas-microsoft-com:office:word");
  Attr(root, "xmlns:wp",
       "http://schemas.openxmlformats.org/drawingml/2006/"
       "wordprocessingDrawing");
  Attr(root, "xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main");
  Attr(root, "xmlns:pic",
       "http://schemas.openxmlformats.org/drawingml/2006/picture");
}

// Emits the w:headerReference / w:footerReference of one section and registers
// its part plus relationship, so every section carries its own stories.
std::expected<void, Error> WriteSectionReferences(pugi::xml_node sec,
                                                  const model::Section& section,
                                                  WriteContext& ctx) {
  for (size_t i = 0; i < 6; i++) {
    if (section.headers[i].empty()) {
      continue;
    }
    const bool is_header = i == 0 || i == 1 || i == 4;
    const int rid = ctx.next_rid++;
    const std::string name = (is_header ? "header" : "footer") +
                             std::to_string(ctx.next_part++) + ".xml";
    auto ref =
        sec.append_child(is_header ? "w:headerReference" : "w:footerReference");
    Attr(ref, "w:type",
         i == 0 || i == 2   ? "even"
         : i == 1 || i == 3 ? "default"
                            : "first");
    Attr(ref, "r:id", "rId" + std::to_string(rid));
    ctx.relations.push_back({ctx.source, rid, name});
    const std::string previous_source = ctx.source;
    ctx.SetSource(name);
    pugi::xml_document part;
    auto root = part.append_child(is_header ? "w:hdr" : "w:ftr");
    StoryNamespaces(root);
    for (const auto& paragraph : section.headers[i]) {
      auto result = WriteParagraph(root, paragraph, nullptr, ctx);
      if (!result)
        return std::unexpected(result.error());
    }
    auto result = ctx.AddPart("word/" + name, Save(part));
    if (!result)
      return std::unexpected(result.error());
    ctx.SetSource(previous_source);
  }
  // w:titlePg sits after w:vAlign in CT_SectPr, so SectionPr() emits it;
  // only the part/relationship bookkeeping stays here.
  if (section.odd_even_different) {
    ctx.odd_even = true;
  }
  return {};
}

// Case-folded style-name lookup, restricted to one style type. Note bodies use
// Word's built-in "footnote text" / "endnote text" paragraph styles and the
// matching "... characters" character style; a DOC whose note paragraph names
// no style of its own still expects them, so the writer reuses the parsed
// STSH entry by name instead of inventing an unnamed style.
int FindNoteStyle(const model::Document& document, int type,
                  std::string_view name) {
  for (size_t i = 0; i < document.styles.size(); i++) {
    const model::Style& style = document.styles[i];
    if (style.type != type || style.name.size() != name.size())
      continue;
    bool match = true;
    for (size_t j = 0; j < name.size() && match; j++) {
      const char c = style.name[j];
      const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
      match = lower == name[j];
    }
    if (match)
      return static_cast<int>(i);
  }
  return -1;
}

std::expected<void, Error> WriteNoteParts(const model::Document& document,
                                          WriteContext& ctx) {
  bool has_footnotes = false, has_endnotes = false;
  for (const auto& note : document.notes) {
    (note.endnote ? has_endnotes : has_footnotes) = true;
  }
  if (!has_footnotes && !has_endnotes) {
    return {};
  }
  auto write_class = [&](bool endnote) -> std::expected<void, Error> {
    const char* part = endnote ? "endnotes.xml" : "footnotes.xml";
    const char* root_name = endnote ? "w:endnotes" : "w:footnotes";
    const char* note_name = endnote ? "w:endnote" : "w:footnote";
    pugi::xml_document doc;
    auto root = doc.append_child(root_name);
    StoryNamespaces(root);
    const std::string previous_source = ctx.source;
    ctx.SetSource(part);
    // Word requires the two separator notes; they carry negative ids.
    struct Separator {
      const char* name;
      int id;
    };
    const Separator separators[] = {{"w:separator", -1},
                                    {"w:continuationSeparator", 0}};
    for (const auto& separator : separators) {
      auto note = root.append_child(note_name);
      Attr(note, "w:type", std::string(separator.name + 2));
      IntAttr(note, "w:id", separator.id);
      auto run = note.append_child("w:p").append_child("w:r");
      run.append_child(separator.name);
    }
    const int text_style = FindNoteStyle(
        document, 1, endnote ? "endnote text" : "footnote text");
    const int mark_style = FindNoteStyle(
        document, 2, endnote ? "endnote characters" : "footnote characters");
    int id = 0;
    for (const auto& note : document.notes) {
      if (note.endnote != endnote) {
        continue;
      }
      id++;
      auto node = root.append_child(note_name);
      IntAttr(node, "w:id", id);
      if (note.paragraphs.empty()) {
        node.append_child("w:p");
      }
      for (size_t i = 0; i < note.paragraphs.size(); i++) {
        model::Paragraph paragraph = note.paragraphs[i];
        if (paragraph.style < 0 && text_style >= 0) {
          paragraph.style = text_style;
        }
        if (i == 0) {
          // Word numbers a note from the mark that opens its body; the DOC text
          // does not carry it, so the writer synthesizes mark + tab.
          model::TabStop stop;
          stop.position = detail::kNoteTextTabTwips;
          paragraph.tabs.push_back(stop);
          model::Run tab;
          tab.text = "\t";
          paragraph.runs.insert(paragraph.runs.begin(), tab);
          model::Run mark;
          mark.note_ref_mark = endnote ? 2 : 1;
          mark.style = mark_style;
          paragraph.runs.insert(paragraph.runs.begin(), mark);
        }
        auto result = WriteParagraph(node, paragraph, nullptr, ctx);
        if (!result)
          return std::unexpected(result.error());
      }
    }
    const int rid = ctx.next_rid++;
    ctx.relations.push_back({previous_source, rid, part});
    auto result = ctx.AddPart("word/" + std::string(part), Save(doc));
    ctx.SetSource(previous_source);
    return result;
  };
  if (has_footnotes) {
    auto result = write_class(false);
    if (!result)
      return std::unexpected(result.error());
  }
  if (has_endnotes) {
    auto result = write_class(true);
    if (!result)
      return std::unexpected(result.error());
  }
  return {};
}

std::expected<void, Error> WriteCommentPart(const model::Document& document,
                                            WriteContext& ctx) {
  if (document.comments.empty()) {
    return {};
  }
  pugi::xml_document doc;
  auto root = doc.append_child("w:comments");
  StoryNamespaces(root);
  const std::string previous_source = ctx.source;
  ctx.SetSource("comments.xml");
  int id = 0;
  for (const auto& comment : document.comments) {
    id++;
    auto node = root.append_child("w:comment");
    IntAttr(node, "w:id", id);
    Attr(node, "w:author",
         comment.author.empty() ? "doccraft" : comment.author);
    if (!comment.initials.empty()) {
      Attr(node, "w:initials", comment.initials);
    }
    if (comment.paragraphs.empty()) {
      node.append_child("w:p");
    }
    for (const auto& paragraph : comment.paragraphs) {
      auto written = WriteParagraph(node, paragraph, nullptr, ctx);
      if (!written)
        return std::unexpected(written.error());
    }
  }
  const int rid = ctx.next_rid++;
  ctx.relations.push_back({previous_source, rid, "comments.xml"});
  auto result = ctx.AddPart("word/comments.xml", Save(doc));
  ctx.SetSource(previous_source);
  return result;
}
