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

// Emits the w:headerReference / w:footerReference of one section and registers
// its part plus relationship, so every section carries its own stories.
bool WriteSectionReferences(pugi::xml_node sec, const model::Section& section,
                            WriteContext& ctx) {
  bool any = false;
  for (size_t i = 0; i < 6; i++) {
    if (section.headers[i].empty()) {
      continue;
    }
    any = true;
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
    ctx.relations.emplace_back(rid, name);
    pugi::xml_document part;
    auto root = part.append_child(is_header ? "w:hdr" : "w:ftr");
    Attr(root, "xmlns:w", kWordmlNamespace);
    Attr(root, "xmlns:r", kRelationshipsNamespace);
    const bool header_has_vml =
        std::any_of(section.headers[i].begin(), section.headers[i].end(),
                    [](const model::Paragraph& paragraph) {
                      return std::any_of(
                          paragraph.runs.begin(), paragraph.runs.end(),
                          [](const model::Run& run) {
                            return run.textbox_index >= 0 ||
                                   run.shape_index >= 0;
                          });
                    });
    if (header_has_vml) {
      Attr(root, "xmlns:v", "urn:schemas-microsoft-com:vml");
    }
    for (const auto& paragraph : section.headers[i]) {
      WriteParagraph(root, paragraph, nullptr, ctx);
    }
    ctx.parts.push_back({name, Save(part)});
  }
  if (section.first_page_different) {
    One(sec, "w:titlePg");
  }
  if (section.odd_even_different) {
    ctx.odd_even = true;
  }
  return any;
}

void WriteNoteParts(const model::Document& document, WriteContext& ctx) {
  bool has_footnotes = false, has_endnotes = false;
  for (const auto& note : document.notes) {
    (note.endnote ? has_endnotes : has_footnotes) = true;
  }
  if (!has_footnotes && !has_endnotes) {
    return;
  }
  auto write_class = [&](bool endnote) {
    const char* part = endnote ? "endnotes.xml" : "footnotes.xml";
    const char* root_name = endnote ? "w:endnotes" : "w:footnotes";
    const char* note_name = endnote ? "w:endnote" : "w:footnote";
    pugi::xml_document doc;
    auto root = doc.append_child(root_name);
    Attr(root, "xmlns:w", kWordmlNamespace);
    Attr(root, "xmlns:r", kRelationshipsNamespace);
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
      for (const auto& paragraph : note.paragraphs) {
        WriteParagraph(node, paragraph, nullptr, ctx);
      }
    }
    const int rid = ctx.next_rid++;
    ctx.relations.emplace_back(rid, part);
    ctx.parts.push_back({part, Save(doc)});
  };
  if (has_footnotes) {
    write_class(false);
  }
  if (has_endnotes) {
    write_class(true);
  }
}


void WriteCommentPart(const model::Document& document, WriteContext& ctx) {
  if (document.comments.empty()) {
    return;
  }
  pugi::xml_document doc;
  auto root = doc.append_child("w:comments");
  Attr(root, "xmlns:w", kWordmlNamespace);
  Attr(root, "xmlns:r", kRelationshipsNamespace);
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
      WriteParagraph(node, paragraph, nullptr, ctx);
    }
  }
  const int rid = ctx.next_rid++;
  ctx.relations.emplace_back(rid, "comments.xml");
  ctx.parts.push_back({"comments.xml", Save(doc)});
}
