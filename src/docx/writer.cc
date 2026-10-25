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

#include "docx/writer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "docx/writer_run.h"
#include "docx/xml_util.h"
#include "third_party/miniz/miniz.h"
#include "third_party/pugixml/pugixml.hpp"

namespace doccraft::docx {
namespace {
using xml::Attr;
using xml::BorderPr;
using xml::Color;
using xml::Id;
using xml::IntAttr;
using xml::kLow16Mask;
using xml::One;
using xml::ShadePr;
using xml::SpecBool;
using xml::Value;
constexpr char kWordmlNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

// Control characters Word stores inline in run text ([MS-DOC] §2.4.1): cell and
// row marks, manual line break, non-breaking and optional hyphens, tab and the
// lowest printable ASCII code point.
constexpr char kCellMark = 0x07;
constexpr char kLineBreak = 0x0B;
constexpr char kTab = 0x09;
constexpr char kNoBreakHyphen = 0x1E;
constexpr char kOptionalHyphen = 0x1F;
constexpr char kFirstPrintable = 32;

// LVLF.nfc values that do not describe a numeric format.
constexpr int kNumberFormatBullet = 0x17;
constexpr int kNumberFormatNone = 0xFF;

constexpr char kRelationshipsNamespace[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

// [Content_Types].xml: fixed head, then one Override per header/footer part.
constexpr char kContentTypesPrefix[] =
    "<?xml version=\"1.0\"?><Types "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/"
    "vnd.openxmlformats-package.relationships+xml\"/><Default "
    "Extension=\"xml\" ContentType=\"application/xml\"/><Override "
    "PartName=\"/word/document.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/styles.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
    "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>";

constexpr char kSettingsContentType[] =
    "<Override PartName=\"/word/settings.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>";

constexpr char kRootRelationships[] =
    "<?xml version=\"1.0\"?><Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/"
    "officeDocument/2006/relationships/officeDocument\" "
    "Target=\"word/document.xml\"/></Relationships>";

constexpr char kSettingsPart[] =
    "<?xml version=\"1.0\"?><w:settings "
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:evenAndOddHeaders/></w:settings>";

// One part of the resulting OPC package (path inside the ZIP plus payload).
struct Part {
  std::string name;
  std::string data;
};

// State shared by the whole writer pass: hyperlink targets, revision ids and
// the header/footer parts discovered while walking the sections. rId1/rId2 are
// reserved for the styles and numbering parts.
struct WriteContext {
  std::map<std::string, int> hyperlinks;
  int revision_id = 0;
  int next_rid = 3;
  int next_part = 1;
  bool odd_even = false;
  std::vector<std::pair<int, std::string>> relations;  // rId -> part name
  std::vector<Part> parts;
};

std::string Save(const pugi::xml_document& d);

void WriteParagraph(pugi::xml_node parent, const model::Paragraph& p,
                    const model::Section* break_section, WriteContext& ctx);

// Emits footnotes.xml / endnotes.xml when the document has notes, and
// registers the parts and relationships.
void WriteNoteParts(const model::Document& document, WriteContext& ctx);

// Returns false when `section` has no header/footer story to reference.
bool WriteSectionReferences(pugi::xml_node sec, const model::Section& section,
                            WriteContext& ctx);

#include "docx/writer_01_paragraph.inc"
#include "docx/writer_02_section.inc"
#include "docx/writer_03_notes.inc"
#include "docx/writer_04_paragraph_body.inc"
#include "docx/writer_05_table.inc"
#include "docx/writer_06_styles.inc"

std::string BuildContentTypes(const WriteContext& ctx) {
  std::string out = kContentTypesPrefix;
  for (const auto& part : ctx.parts) {
    bool header = part.name.starts_with("header");
    out += "<Override PartName=\"/word/" + part.name +
           "\" "
           "ContentType=\"application/"
           "vnd.openxmlformats-officedocument.wordprocessingml." +
           (header ? std::string("header") : std::string("footer")) +
           "+xml\"/>";
  }
  for (const auto& part : ctx.parts) {
    if (part.name == "footnotes.xml") {
      out +=
          "<Override PartName=\"/word/footnotes.xml\" "
          "ContentType=\"application/"
          "vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/"
          ">";
    } else if (part.name == "endnotes.xml") {
      out +=
          "<Override PartName=\"/word/endnotes.xml\" ContentType=\"application/"
          "vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml\"/>";
    }
  }
  if (ctx.odd_even) {
    out += kSettingsContentType;
  }
  out += "</Types>";
  return out;
}

pugi::xml_document BuildRelationships(const WriteContext& ctx) {
  constexpr const char* kRelBase =
      "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
  pugi::xml_document docrels;
  auto relations = docrels.append_child("Relationships");
  Attr(relations, "xmlns",
       "http://schemas.openxmlformats.org/package/2006/relationships");
  auto style_rel = relations.append_child("Relationship");
  Attr(style_rel, "Id", "rId1");
  Attr(style_rel, "Type", std::string(kRelBase) + "styles");
  Attr(style_rel, "Target", "styles.xml");
  auto number_rel = relations.append_child("Relationship");
  Attr(number_rel, "Id", "rId2");
  Attr(number_rel, "Type", std::string(kRelBase) + "numbering");
  Attr(number_rel, "Target", "numbering.xml");
  const int settings_id = ctx.odd_even ? ctx.next_rid : 0;
  for (const auto& [rid, target] : ctx.relations) {
    const char* type = "footer";
    if (target.starts_with("header")) {
      type = "header";
    } else if (target == "footnotes.xml") {
      type = "footnotes";
    } else if (target == "endnotes.xml") {
      type = "endnotes";
    }
    auto rel = relations.append_child("Relationship");
    Attr(rel, "Id", "rId" + std::to_string(rid));
    Attr(rel, "Type", std::string(kRelBase) + type);
    Attr(rel, "Target", target);
  }
  if (settings_id != 0) {
    auto rel = relations.append_child("Relationship");
    Attr(rel, "Id", "rId" + std::to_string(settings_id));
    Attr(rel, "Type", std::string(kRelBase) + "settings");
    Attr(rel, "Target", "settings.xml");
  }
  for (const auto& [url, id] : ctx.hyperlinks) {
    auto link = relations.append_child("Relationship");
    Attr(link, "Id", "rId" + std::to_string(id));
    Attr(link, "Type", std::string(kRelBase) + "hyperlink");
    Attr(link, "Target", url);
    Attr(link, "TargetMode", "External");
  }
  return docrels;
}

std::expected<std::vector<uint8_t>, Error> PackParts(
    const std::vector<Part>& parts) {
  mz_zip_archive z = {};
  if (!mz_zip_writer_init_heap(&z, 0, 0)) {
    return std::unexpected(Error{"ZIP init failed"});
  }
  bool ok = true;
  for (const auto& part : parts) {
    if (!ok) {
      break;
    }
    ok = mz_zip_writer_add_mem(&z, part.name.c_str(), part.data.data(),
                               part.data.size(), MZ_DEFAULT_LEVEL) != 0;
  }
  void* ptr = nullptr;
  size_t len = 0;
  if (ok) {
    ok = mz_zip_writer_finalize_heap_archive(&z, &ptr, &len) != 0;
  }
  mz_zip_writer_end(&z);
  if (!ok) {
    if (ptr) {
      mz_free(ptr);
    }
    return std::unexpected(Error{"ZIP write failed"});
  }
  auto* bytes = static_cast<uint8_t*>(ptr);
  std::vector<uint8_t> result(bytes, bytes + len);
  mz_free(ptr);
  return result;
}
}  // namespace

uint32_t Crc32(std::string_view bytes) {
  return mz_crc32(0, reinterpret_cast<const unsigned char*>(bytes.data()),
                  bytes.size());
}

std::expected<std::vector<uint8_t>, Error> Write(
    const model::Document& document) {
  using Clock = std::chrono::steady_clock;
  const auto t_start = Clock::now();
  WriteContext ctx;
  pugi::xml_document doc;
  auto root = doc.append_child("w:document");
  Attr(root, "xmlns:w", kWordmlNamespace);
  Attr(root, "xmlns:r", kRelationshipsNamespace);
  auto body = root.append_child("w:body");
  WriteBody(body, document, ctx);

  WriteNoteParts(document, ctx);
  auto sec = body.append_child("w:sectPr");
  SectionPr(sec, document.section, &ctx);

  pugi::xml_document styles = WriteStyles(document);
  pugi::xml_document numbering = WriteNumbering(document);
  pugi::xml_document docrels = BuildRelationships(ctx);

  std::vector<Part> parts;
  parts.push_back({"[Content_Types].xml", BuildContentTypes(ctx)});
  parts.push_back({"_rels/.rels", kRootRelationships});
  parts.push_back({"word/_rels/document.xml.rels", Save(docrels)});
  parts.push_back({"word/document.xml", Save(doc)});
  parts.push_back({"word/styles.xml", Save(styles)});
  parts.push_back({"word/numbering.xml", Save(numbering)});
  for (const auto& part : ctx.parts) {
    parts.push_back({"word/" + part.name, part.data});
  }
  if (ctx.odd_even) {
    parts.push_back({"word/settings.xml", kSettingsPart});
  }
  const auto t_xml = Clock::now();
  auto archive = PackParts(parts);
  if (std::getenv("DOCCRAFT_PROFILE")) {
    auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::fprintf(stderr, "PROFILE xml_ms=%.3f zip_ms=%.3f\n",
                 ms(t_start, t_xml), ms(t_xml, Clock::now()));
  }
  return archive;
}
}  // namespace doccraft::docx
