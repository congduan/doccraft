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

void WriteParagraph(pugi::xml_node parent, const model::Paragraph& p,
                    const model::Section* break_section, WriteContext& ctx) {
  auto pn = parent.append_child("w:p");
  ParaPr(pn, p);
  if (break_section) {
    SectionPr(pn.child("w:pPr").append_child("w:sectPr"), *break_section, &ctx);
  }
  for (const auto& r : p.runs) {
    pugi::xml_node owner = pn;
    if (!r.hyperlink.empty()) {
      auto [it, inserted] =
          ctx.hyperlinks.try_emplace(r.hyperlink, ctx.next_rid++);
      (void)inserted;
      owner = pn.append_child("w:hyperlink");
      Attr(owner, "r:id", "rId" + std::to_string(it->second));
    } else if (r.page_field) {
      owner = pn.append_child("w:fldSimple");
      Attr(owner, "w:instr", "PAGE");
    }
    // sprmCFRMarkDel / sprmCFRMarkIns: tracked text lives inside w:del / w:ins.
    pugi::xml_node wrapper = owner;
    bool deleted = r.revision_delete;
    if (deleted || r.revision_insert) {
      wrapper = owner.append_child(deleted ? "w:del" : "w:ins");
      IntAttr(wrapper, "w:id", ++ctx.revision_id);
      Attr(wrapper, "w:author", "doccraft");
    }
    auto rn = wrapper.append_child("w:r");
    RunPr(rn, r);
    if (r.note_ref >= 0) {
      // A note reference is an empty run carrying the reference element.
      IntAttr(rn.append_child(r.note_endnote ? "w:endnoteReference"
                                             : "w:footnoteReference"),
              "w:id", r.note_ref);
    } else if (r.field_char >= 0) {
      const char* kinds[] = {"begin", "separate", "end"};
      Attr(rn.append_child("w:fldChar"), "w:fldCharType",
           r.field_char < 3 ? kinds[r.field_char] : "begin");
    } else if (r.break_type > 0) {
      auto br = rn.append_child("w:br");
      Attr(br, "w:type", r.break_type == 2 ? "column" : "page");
    } else if (!r.instr_text.empty()) {
      auto instr = rn.append_child("w:instrText");
      Attr(instr, "xml:space", "preserve");
      instr.text().set(r.instr_text.c_str());
    } else if (r.symbol_character >= 0 && !r.symbol_font.empty()) {
      auto sym = rn.append_child("w:sym");
      Attr(sym, "w:font", r.symbol_font);
      char code[5];
      std::snprintf(code, sizeof(code), "%04X",
                    r.symbol_character & kLow16Mask);
      Attr(sym, "w:char", code);
    } else {
      Text(rn, r.text, deleted);
    }
  }
}

