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

const char* ImageExtension(model::ImageFormat format) {
  switch (format) {
    case model::ImageFormat::kPng:
      return "png";
    case model::ImageFormat::kJpeg:
      return "jpg";
    case model::ImageFormat::kWmf:
      return "wmf";
    case model::ImageFormat::kEmf:
      return "emf";
    case model::ImageFormat::kBmp:
      return "bmp";
  }
  return "png";
}

std::expected<void, Error> RegisterImage(int index, WriteContext& ctx) {
  if (!ctx.document || index < 0 ||
      static_cast<size_t>(index) >= ctx.document->images.size())
    return {};
  const model::Image& image = ctx.document->images[index];
  int& image_rid = ctx.image_rids[ctx.source][index];
  if (image_rid == 0) {
    const int number = index + 1;
    std::string name = "media/image" + std::to_string(number) + "." +
                       ImageExtension(image.format);
    image_rid = ctx.NextRid();
    ctx.relations.push_back({ctx.source, image_rid, name});
    if (!ctx.image_parts_added[index]) {
      auto result = ctx.AddPart(
          "word/" + name, std::string(image.bytes.begin(), image.bytes.end()));
      if (!result)
        return std::unexpected(result.error());
      ctx.image_parts_added[index] = true;
    }
  }
  return {};
}

std::expected<void, Error> WriteImage(pugi::xml_node run, int index,
                                      WriteContext& ctx) {
  if (!ctx.document || index < 0 ||
      static_cast<size_t>(index) >= ctx.document->images.size())
    return {};
  auto result = RegisterImage(index, ctx);
  if (!result)
    return std::unexpected(result.error());
  const model::Image& image = ctx.document->images[index];
  constexpr int64_t kEmuPerTwip = 635;
  const int64_t width = static_cast<int64_t>(image.width) * kEmuPerTwip;
  const int64_t height = static_cast<int64_t>(image.height) * kEmuPerTwip;
  auto inline_node =
      run.append_child("w:drawing")
          .append_child(image.anchor ? "wp:anchor" : "wp:inline");
  Attr(inline_node, "distT", "0");
  Attr(inline_node, "distB", "0");
  Attr(inline_node, "distL", "0");
  Attr(inline_node, "distR", "0");
  if (image.anchor) {
    const auto& anchor = *image.anchor;
    Attr(inline_node, "simplePos", "0");
    IntAttr(inline_node, "relativeHeight", index + 1);
    Attr(inline_node, "behindDoc", anchor.behind_text ? "1" : "0");
    Attr(inline_node, "locked", anchor.locked ? "1" : "0");
    Attr(inline_node, "layoutInCell", "1");
    Attr(inline_node, "allowOverlap", "1");
    auto simple = inline_node.append_child("wp:simplePos");
    Attr(simple, "x", "0");
    Attr(simple, "y", "0");
    const char* horizontal[] = {"margin", "page", "column"};
    const char* vertical[] = {"margin", "page", "paragraph"};
    auto h = inline_node.append_child("wp:positionH");
    Attr(h, "relativeFrom", horizontal[std::clamp(anchor.horizontal, 0, 2)]);
    h.append_child("wp:posOffset")
        .text()
        .set(std::to_string(int64_t{anchor.x} * kEmuPerTwip).c_str());
    auto v = inline_node.append_child("wp:positionV");
    Attr(v, "relativeFrom", vertical[std::clamp(anchor.vertical, 0, 2)]);
    v.append_child("wp:posOffset")
        .text()
        .set(std::to_string(int64_t{anchor.y} * kEmuPerTwip).c_str());
  }
  auto extent = inline_node.append_child("wp:extent");
  Attr(extent, "cx", std::to_string(width));
  Attr(extent, "cy", std::to_string(height));
  auto effect = inline_node.append_child("wp:effectExtent");
  Attr(effect, "l", "0");
  Attr(effect, "t", "0");
  Attr(effect, "r", "0");
  Attr(effect, "b", "0");
  if (image.anchor) {
    const auto& anchor = *image.anchor;
    const char* name = "wp:wrapSquare";
    switch (anchor.wrap) {
      case model::ImageWrap::kNone:
        name = "wp:wrapNone";
        break;
      case model::ImageWrap::kSquare:
        break;
      case model::ImageWrap::kTight:
        name = "wp:wrapTight";
        break;
      case model::ImageWrap::kThrough:
        name = "wp:wrapThrough";
        break;
      case model::ImageWrap::kTopAndBottom:
        name = "wp:wrapTopAndBottom";
        break;
    }
    auto wrap = inline_node.append_child(name);
    if (anchor.wrap != model::ImageWrap::kNone &&
        anchor.wrap != model::ImageWrap::kTopAndBottom) {
      const char* sides[] = {"bothSides", "left", "right", "largest"};
      Attr(wrap, "wrapText", sides[std::clamp(anchor.wrap_side, 0, 3)]);
    }
    if (anchor.wrap == model::ImageWrap::kTight ||
        anchor.wrap == model::ImageWrap::kThrough) {
      // Rectangular contour fallback; custom OfficeArt wrap polygons deferred.
      constexpr int kWrapCoordinateExtent = 21600;
      auto polygon = wrap.append_child("wp:wrapPolygon");
      Attr(polygon, "edited", "0");
      auto start = polygon.append_child("wp:start");
      IntAttr(start, "x", 0);
      IntAttr(start, "y", 0);
      for (const auto& [x, y] :
           {std::pair{0, kWrapCoordinateExtent},
            std::pair{kWrapCoordinateExtent, kWrapCoordinateExtent},
            std::pair{kWrapCoordinateExtent, 0}, std::pair{0, 0}}) {
        auto point = polygon.append_child("wp:lineTo");
        IntAttr(point, "x", x);
        IntAttr(point, "y", y);
      }
    }
  }
  auto doc_pr = inline_node.append_child("wp:docPr");
  IntAttr(doc_pr, "id", index + 1);
  Attr(doc_pr, "name", "Picture " + std::to_string(index + 1));
  inline_node.append_child("wp:cNvGraphicFramePr");
  auto graphic = inline_node.append_child("a:graphic");
  auto graphic_data = graphic.append_child("a:graphicData");
  Attr(graphic_data, "uri",
       "http://schemas.openxmlformats.org/drawingml/2006/picture");
  auto pic = graphic_data.append_child("pic:pic");
  auto nv = pic.append_child("pic:nvPicPr");
  auto nv_pr = nv.append_child("pic:cNvPr");
  IntAttr(nv_pr, "id", index + 1);
  Attr(nv_pr, "name", "image" + std::to_string(index + 1));
  nv.append_child("pic:cNvPicPr");
  auto fill = pic.append_child("pic:blipFill");
  Attr(fill.append_child("a:blip"), "r:embed",
       "rId" + std::to_string(ctx.image_rids[ctx.source][index]));
  auto stretch = fill.append_child("a:stretch");
  stretch.append_child("a:fillRect");
  auto sp_pr = pic.append_child("pic:spPr");
  auto xfrm = sp_pr.append_child("a:xfrm");
  auto off = xfrm.append_child("a:off");
  Attr(off, "x", "0");
  Attr(off, "y", "0");
  auto ext = xfrm.append_child("a:ext");
  Attr(ext, "cx", std::to_string(width));
  Attr(ext, "cy", std::to_string(height));
  Attr(sp_pr.append_child("a:prstGeom"), "prst", "rect");
  return {};
}

std::expected<void, Error> WriteOle(pugi::xml_node run, int index,
                                    WriteContext& ctx) {
  if (!ctx.document || index < 0 ||
      static_cast<size_t>(index) >= ctx.document->ole_objects.size())
    return {};
  const auto& object = ctx.document->ole_objects[index];
  if (object.bytes.empty()) {
    run.append_child("w:t").text().set("[Embedded object]");
    return {};
  }
  int& ole_rid = ctx.ole_rids[ctx.source][index];
  if (ole_rid == 0) {
    const std::string name =
        "embeddings/oleObject" + std::to_string(index + 1) + ".bin";
    ole_rid = ctx.NextRid();
    ctx.relations.push_back({ctx.source, ole_rid, name});
    if (!ctx.ole_parts_added[index]) {
      auto result = ctx.AddPart(
          "word/" + name, std::string(object.bytes.begin(), object.bytes.end()));
      if (!result)
        return std::unexpected(result.error());
      ctx.ole_parts_added[index] = true;
    }
  }
  int width = 1440, height = 1440;
  if (object.preview_image_index >= 0 &&
      static_cast<size_t>(object.preview_image_index) <
          ctx.document->images.size()) {
    const auto& image = ctx.document->images[object.preview_image_index];
    width = image.width;
    height = image.height;
    auto result = RegisterImage(object.preview_image_index, ctx);
    if (!result)
      return std::unexpected(result.error());
  }
  auto container = run.append_child("w:object");
  IntAttr(container, "w:dxaOrig", width);
  IntAttr(container, "w:dyaOrig", height);
  const std::string shape_id = "ole_shape_" + std::to_string(index + 1);
  auto shape = container.append_child("v:shape");
  Attr(shape, "id", shape_id);
  Attr(shape, "style",
       "width:" + std::to_string(width / 20.0) +
           "pt;height:" + std::to_string(height / 20.0) + "pt");
  if (object.preview_image_index >= 0 &&
      static_cast<size_t>(object.preview_image_index) <
          ctx.image_rids[ctx.source].size()) {
    Attr(shape.append_child("v:imagedata"), "r:id",
         "rId" + std::to_string(
                     ctx.image_rids[ctx.source][object.preview_image_index]));
  }
  auto ole = container.append_child("o:OLEObject");
  Attr(ole, "Type", "Embed");
  Attr(ole, "ProgID", object.program_id);
  Attr(ole, "ShapeID", shape_id);
  Attr(ole, "DrawAspect", "Content");
  Attr(ole, "ObjectID", "_" + std::to_string(object.storage_id));
  Attr(ole, "r:id", "rId" + std::to_string(ctx.ole_rids[ctx.source][index]));
  return {};
}

std::expected<void, Error> WriteTextbox(pugi::xml_node run, int index,
                                        WriteContext& ctx) {
  if (!ctx.document || index < 0 ||
      static_cast<size_t>(index) >= ctx.document->textboxes.size())
    return {};
  const auto& box = ctx.document->textboxes[index];
  auto rect = run.append_child("w:pict").append_child("v:rect");
  Attr(rect, "id", "Textbox" + std::to_string(index + 1));
  if (box.shape_type >= model::kShapeWordArtFirst &&
      box.shape_type <= model::kShapeWordArtLast)
    Attr(rect, "alt", "shape: WordArt");
  Attr(rect, "stroked", "f");
  Attr(rect, "filled", "f");
  auto points = [](int twips) {
    return std::to_string(twips / 20.0) + "pt";
  };
  Attr(rect, "style",
       (box.floating ? "position:absolute;left:" + points(box.x) +
                           ";top:" + points(box.y) + ";"
                     : std::string()) +
           "width:" + points(box.width) + ";height:" + points(box.height));
  auto content = rect.append_child("v:textbox").append_child("w:txbxContent");
  for (const auto& paragraph : box.paragraphs) {
    auto result = WriteParagraph(content, paragraph, nullptr, ctx);
    if (!result)
      return std::unexpected(result.error());
  }
  return {};
}

std::expected<void, Error> WriteShape(pugi::xml_node run, int index,
                                      WriteContext& ctx) {
  if (!ctx.document || index < 0 ||
      static_cast<size_t>(index) >= ctx.document->shapes.size())
    return {};
  const auto& shape = ctx.document->shapes[index];
  const char* kind = shape.type == model::kShapeEllipse ? "v:oval"
                     : shape.type == model::kShapeLine  ? "v:line"
                                                        : "v:rect";
  auto node = run.append_child("w:pict").append_child(kind);
  Attr(node, "id", "Shape" + std::to_string(shape.id));
  const char* name = shape.type == model::kShapeRectangle ? "rectangle"
                     : shape.type == model::kShapeEllipse ? "ellipse"
                     : shape.type == model::kShapeLine    ? "line"
                     : shape.type >= model::kShapeWordArtFirst &&
                             shape.type <= model::kShapeWordArtLast
                         ? "WordArt"
                         : "geometry";
  Attr(node, "alt", std::string("shape: ") + name);
  auto points = [](int twips) {
    return std::to_string(twips / 20.0) + "pt";
  };
  Attr(
      node, "style",
      (shape.inline_shape ? std::string()
                          : "position:absolute;left:" + points(shape.anchor.x) +
                                ";top:" + points(shape.anchor.y) + ";") +
          "width:" + points(shape.width) + ";height:" + points(shape.height));
  if (shape.type == model::kShapeLine) {
    Attr(node, "from", "0pt,0pt");
    Attr(node, "to", points(shape.width) + "," + points(shape.height));
  }
  // OfficeArt fill/line become the VML paint children; an absent colour keeps
  // the VML default (no fill, black line).
  constexpr double kEmuPerPoint = 12700.0;
  // VML paints use the CSS form of a colour: a leading # before RRGGBB.
  auto vml_color = [](int color) { return "#" + Color(color); };
  if (shape.fill_color >= 0 && shape.type != model::kShapeLine) {
    Attr(node, "filled", "t");
    Attr(node.append_child("v:fill"), "color", vml_color(shape.fill_color));
  } else {
    Attr(node, "filled", "f");
  }
  if (shape.line_color >= 0) {
    auto stroke = node.append_child("v:stroke");
    Attr(stroke, "color", vml_color(shape.line_color));
    if (shape.line_width > 0) {
      Attr(stroke, "weight",
           std::to_string(shape.line_width / kEmuPerPoint) + "pt");
    }
  }
  const char* wrap = "square";
  switch (shape.anchor.wrap) {
    case model::ImageWrap::kNone:
      wrap = "none";
      break;
    case model::ImageWrap::kTopAndBottom:
      wrap = "topAndBottom";
      break;
    case model::ImageWrap::kTight:
      wrap = "tight";
      break;
    case model::ImageWrap::kThrough:
      wrap = "through";
      break;
    case model::ImageWrap::kSquare:
      break;
  }
  if (!shape.inline_shape)
    Attr(node.append_child("w10:wrap"), "type", wrap);
  if (!shape.text.empty()) {
    auto content = node.append_child("v:textbox").append_child("w:txbxContent");
    model::Paragraph paragraph;
    model::Run text_run;
    text_run.text = shape.text;
    paragraph.runs.push_back(std::move(text_run));
    auto result = WriteParagraph(content, paragraph, nullptr, ctx);
    if (!result)
      return std::unexpected(result.error());
  }
  return {};
}

std::expected<void, Error> WriteParagraph(pugi::xml_node parent,
                                          const model::Paragraph& p,
                                          const model::Section* break_section,
                                          WriteContext& ctx) {
  auto pn = parent.append_child("w:p");
  ParaPr(pn, p);
  if (break_section) {
    auto result = SectionPr(pn.child("w:pPr").append_child("w:sectPr"),
                            *break_section, &ctx);
    if (!result)
      return std::unexpected(result.error());
  }
  for (const auto& r : p.runs) {
    pugi::xml_node owner = pn;
    if (!r.hyperlink.empty()) {
      auto& links = ctx.hyperlinks[ctx.source];
      auto it = links.find(r.hyperlink);
      if (it == links.end())
        it = links.emplace(r.hyperlink, ctx.NextRid()).first;
      owner = pn.append_child("w:hyperlink");
      Attr(owner, "r:id", "rId" + std::to_string(it->second));
    } else if (r.page_field) {
      owner = pn.append_child("w:fldSimple");
      Attr(owner, "w:instr", "PAGE");
      if (r.field_locked)
        Attr(owner, "w:fldLock", "1");
      if (r.field_dirty)
        Attr(owner, "w:dirty", "1");
    }
    // sprmCFRMarkDel / sprmCFRMarkIns: tracked text lives inside w:del / w:ins,
    // attributed to the author named by the matching sprmCIbstRMark index.
    pugi::xml_node wrapper = owner;
    bool deleted = r.revision_delete;
    if (deleted || r.revision_insert) {
      wrapper = owner.append_child(deleted ? "w:del" : "w:ins");
      IntAttr(wrapper, "w:id", ++ctx.revision_id);
      Attr(wrapper, "w:author",
           RevisionAuthor(ctx.document, deleted ? r.revision_author_delete
                                                : r.revision_author_insert));
      const std::string date = RevisionDate(deleted ? r.revision_date_delete
                                                    : r.revision_date_insert);
      if (!date.empty())
        Attr(wrapper, "w:date", date);
    }
    if (r.comment_range_end >= 0) {
      IntAttr(pn.append_child("w:commentRangeEnd"), "w:id",
              r.comment_range_end);
      continue;
    }
    if (r.comment_range_start >= 0) {
      IntAttr(pn.append_child("w:commentRangeStart"), "w:id",
              r.comment_range_start);
      continue;
    }
    if (r.comment_ref >= 0 && !r.comment_ref_has_range)
      IntAttr(pn.append_child("w:commentRangeStart"), "w:id", r.comment_ref);
    auto rn = wrapper.append_child("w:r");
    RunPr(rn, r, ctx.document);
    if (r.bookmark_id >= 0) {
      // Bookmarks are paragraph-level markers, not run content.
      auto mark = pn.append_child(r.bookmark_start ? "w:bookmarkStart"
                                                   : "w:bookmarkEnd");
      IntAttr(mark, "w:id", r.bookmark_id);
      if (r.bookmark_start) {
        Attr(mark, "w:name", r.bookmark_name);
      }
      continue;
    }
    if (r.comment_ref >= 0) {
      IntAttr(rn.append_child("w:commentReference"), "w:id", r.comment_ref);
      if (!r.comment_ref_has_range)
        IntAttr(pn.append_child("w:commentRangeEnd"), "w:id", r.comment_ref);
    } else if (r.image_index >= 0) {
      auto result = WriteImage(rn, r.image_index, ctx);
      if (!result)
        return std::unexpected(result.error());
    } else if (r.shape_index >= 0) {
      auto result = WriteShape(rn, r.shape_index, ctx);
      if (!result)
        return std::unexpected(result.error());
    } else if (r.textbox_index >= 0) {
      auto result = WriteTextbox(rn, r.textbox_index, ctx);
      if (!result)
        return std::unexpected(result.error());
    } else if (r.ole_index >= 0) {
      auto result = WriteOle(rn, r.ole_index, ctx);
      if (!result)
        return std::unexpected(result.error());
    } else if (r.note_ref >= 0) {
      // A note reference is an empty run carrying the reference element.
      IntAttr(rn.append_child(r.note_endnote ? "w:endnoteReference"
                                             : "w:footnoteReference"),
              "w:id", r.note_ref);
    } else if (r.note_ref_mark > 0) {
      // The mark at the head of a note body; its w:rStyle comes from RunPr.
      rn.append_child(r.note_ref_mark == 2 ? "w:endnoteRef" : "w:footnoteRef");
    } else if (r.field_char >= 0) {
      const char* kinds[] = {"begin", "separate", "end"};
      auto fld = rn.append_child("w:fldChar");
      Attr(fld, "w:fldCharType",
           r.field_char < 3 ? kinds[r.field_char] : "begin");
      if (r.field_locked)
        Attr(fld, "w:fldLock", "1");
      if (r.field_dirty)
        Attr(fld, "w:dirty", "1");
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
  return {};
}
