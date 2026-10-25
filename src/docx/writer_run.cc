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

#include "docx/writer_run.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "third_party/pugixml/pugixml.hpp"

namespace doccraft::docx {
namespace {
std::string Id(int i) {
  if (i == 0)
    return "Normal";
  if (i > 0 && i <= 9)
    return "Heading" + std::to_string(i);
  return "Style" + std::to_string(i);
}

void Attr(pugi::xml_node n, const char* name, const std::string& value) {
  n.append_attribute(name).set_value(value.c_str());
}

void IntAttr(pugi::xml_node n, const char* name, int value) {
  n.append_attribute(name).set_value(value);
}

void Value(pugi::xml_node parent, const char* name, int value) {
  IntAttr(parent.append_child(name), "w:val", value);
}

void SpecBool(pugi::xml_node parent, const char* name, bool value, bool set) {
  if (set) {
    auto n = parent.append_child(name);
    if (!value)
      Attr(n, "w:val", "0");
  }
}

std::string Color(int rgb) {
  if (rgb < 0 ||
      (static_cast<uint32_t>(rgb) & kColorAutoFlag) == kColorAutoFlag)
    return "auto";
  char out[7];
  std::snprintf(out, sizeof(out), "%02X%02X%02X", rgb & kColorChannelMask,
                (rgb >> kColorGreenShift) & kColorChannelMask,
                (rgb >> kColorRedShift) & kColorChannelMask);
  return out;
}

const char* BorderType(int type) {
  switch (type) {
    case 0:
      return "nil";
    case 1:
      return "single";
    case 2:
      return "thick";
    case 3:
      return "double";
    case 5:
      return "hairline";
    case 6:
      return "dotted";
    case 7:
      return "dashed";
    case 8:
      return "dotDash";
    case 9:
      return "dotDotDash";
    case 10:
      return "triple";
    case 20:
      return "wave";
    case 21:
      return "doubleWave";
    case 22:
      return "dashSmallGap";
    case 24:
      return "threeDEmboss";
    case 25:
      return "threeDEngrave";
    default:
      return "single";
  }
}

void BorderPr(pugi::xml_node parent, const char* name, const model::Border& b) {
  if (!b.set)
    return;
  auto side = parent.append_child(name);
  Attr(side, "w:val", BorderType(b.type));
  if (b.type) {
    IntAttr(side, "w:sz", b.width < 2 ? 2 : b.width);
    Attr(side, "w:color", Color(b.color));
    IntAttr(side, "w:space", b.space);
    if (b.shadow)
      Attr(side, "w:shadow", "1");
    if (b.frame)
      Attr(side, "w:frame", "1");
  }
}

void ShadePr(pugi::xml_node parent, int back, int fore, int pattern) {
  if (back < 0 && fore < 0)
    return;
  static constexpr const char* patterns[] = {"clear",
                                             "solid",
                                             "pct5",
                                             "pct10",
                                             "pct20",
                                             "pct25",
                                             "pct30",
                                             "pct40",
                                             "pct50",
                                             "pct60",
                                             "pct70",
                                             "pct75",
                                             "pct80",
                                             "pct90",
                                             "horzStripe",
                                             "vertStripe",
                                             "reverseDiagStripe",
                                             "diagStripe",
                                             "horzCross",
                                             "diagCross",
                                             "thinHorzStripe",
                                             "thinVertStripe",
                                             "thinReverseDiagStripe",
                                             "thinDiagStripe",
                                             "thinHorzCross",
                                             "thinDiagCross"};
  auto shd = parent.append_child("w:shd");
  Attr(shd, "w:val",
       pattern >= 0 && static_cast<size_t>(pattern) < std::size(patterns)
           ? patterns[pattern]
           : "clear");
  Attr(shd, "w:fill", Color(back));
  Attr(shd, "w:color", Color(fore));
}
}  // namespace
void RunPr(pugi::xml_node parent, const model::Run& r) {
  pugi::xml_node n = parent.append_child("w:rPr");
  if (r.style >= 0)
    Attr(n.append_child("w:rStyle"), "w:val", Id(r.style));
  if (!r.font.empty() || !r.font_hansi.empty() || !r.font_east_asia.empty() ||
      !r.font_cs.empty() || r.font_hint >= 0) {
    auto f = n.append_child("w:rFonts");
    if (!r.font.empty())
      Attr(f, "w:ascii", r.font);
    if (!r.font_hansi.empty())
      Attr(f, "w:hAnsi", r.font_hansi);
    else if (!r.font.empty())
      Attr(f, "w:hAnsi", r.font);
    if (!r.font_east_asia.empty())
      Attr(f, "w:eastAsia", r.font_east_asia);
    if (!r.font_cs.empty())
      Attr(f, "w:cs", r.font_cs);
    if (r.font_hint >= 0)
      Attr(f, "w:hint",
           r.font_hint == 1   ? "eastAsia"
           : r.font_hint == 2 ? "cs"
                              : "default");
  }
  if (!r.lang.empty() || !r.lang_east_asia.empty() || !r.lang_cs.empty()) {
    auto lang = n.append_child("w:lang");
    if (!r.lang.empty())
      Attr(lang, "w:val", r.lang);
    if (!r.lang_east_asia.empty())
      Attr(lang, "w:eastAsia", r.lang_east_asia);
    if (!r.lang_cs.empty())
      Attr(lang, "w:bidi", r.lang_cs);
  }
  if (r.size > 0)
    Value(n, "w:sz", r.size);
  if (r.size_cs > 0)
    Value(n, "w:szCs", r.size_cs);
  SpecBool(n, "w:b", r.bold, r.bold_set);
  SpecBool(n, "w:i", r.italic, r.italic_set);
  SpecBool(n, "w:bCs", r.bold_cs, r.bold_cs_set);
  SpecBool(n, "w:iCs", r.italic_cs, r.italic_cs_set);
  if (r.kern >= 0)
    Value(n, "w:kern", r.kern);
  if (r.position_set)
    Value(n, "w:position", r.position);
  SpecBool(n, "w:strike", r.strike, r.strike_set);
  SpecBool(n, "w:dstrike", r.double_strike, r.double_strike_set);
  bool vanish = r.hidden || r.field_hidden;
  SpecBool(n, "w:vanish", vanish, r.hidden_set || r.field_hidden_set);
  SpecBool(n, "w:webHidden", r.web_hidden, r.web_hidden_set);
  SpecBool(n, "w:specVanish", r.spec_vanish, r.spec_vanish_set);
  SpecBool(n, "w:rtl", r.rtl, r.rtl_set);
  SpecBool(n, "w:cs", r.complex_script, r.complex_script_set);
  SpecBool(n, "w:snapToGrid", r.snap_to_grid, r.snap_to_grid_set);
  SpecBool(n, "w:noProof", r.no_proof, r.no_proof_set);
  SpecBool(n, "w:shadow", r.shadow, r.shadow_set);
  SpecBool(n, "w:emboss", r.emboss, r.emboss_set);
  SpecBool(n, "w:imprint", r.imprint, r.imprint_set);
  SpecBool(n, "w:outline", r.outline, r.outline_set);
  SpecBool(n, "w:smallCaps", r.small_caps, r.small_caps_set);
  SpecBool(n, "w:caps", r.caps, r.caps_set);
  if (r.underline_set) {
    const char* kind = "single";
    switch (r.underline) {
      case 0:
        kind = "none";
        break;
      case 1:
        kind = "single";
        break;
      case 2:
        kind = "words";
        break;
      case 3:
        kind = "double";
        break;
      case 4:
        kind = "dotted";
        break;
      case 6:
        kind = "thick";
        break;
      case 7:
        kind = "dash";
        break;
      case 9:
        kind = "dotDash";
        break;
      case 10:
        kind = "dotDotDash";
        break;
      case 11:
        kind = "wave";
        break;
      case 0x14:
        kind = "dottedHeavy";
        break;
      case 0x17:
        kind = "dashedHeavy";
        break;
      case 0x19:
        kind = "dotDashHeavy";
        break;
      case 0x1A:
        kind = "dotDotDashHeavy";
        break;
      case 0x1B:
        kind = "wavyHeavy";
        break;
      case 0x27:
        kind = "dashLong";
        break;
      case 0x2B:
        kind = "wavyDouble";
        break;
      case 0x37:
        kind = "dashLongHeavy";
        break;
    }
    auto u = n.append_child("w:u");
    Attr(u, "w:val", kind);
    if (r.underline_color >= 0)
      Attr(u, "w:color", Color(r.underline_color));
  }
  if (r.super_sub_set)
    Attr(n.append_child("w:vertAlign"), "w:val",
         r.super_sub == 1   ? "superscript"
         : r.super_sub == 2 ? "subscript"
                            : "baseline");
  if (r.emphasis >= 0) {
    const char* marks[] = {"none", "dot", "comma", "circle", "underDot"};

    Attr(n.append_child("w:em"), "w:val",
         marks[r.emphasis < 5 ? r.emphasis : 0]);
  }
  if (r.spacing_set)
    IntAttr(n.append_child("w:spacing"), "w:val", r.spacing);
  if (r.scale_set)
    IntAttr(n.append_child("w:w"), "w:val", r.scale);
  if (r.text_effect >= 0) {
    const char* effects[] = {"none",    "lights",    "blinkBackground",
                             "sparkle", "antsBlack", "antsRed",
                             "shimmer"};
    if (r.text_effect < 7)
      Attr(n.append_child("w:effect"), "w:val", effects[r.text_effect]);
  }
  if (r.color >= 0) {
    constexpr const char* colors[] = {"auto",   "000000", "0000FF", "00FFFF",
                                      "008000", "FF00FF", "FF0000", "FFFF00",
                                      "FFFFFF", "000080", "008080", "008000",
                                      "800080", "800000", "808000", "808080"};
    Attr(n.append_child("w:color"), "w:val",
         r.color_rgb    ? Color(r.color)
         : r.color < 16 ? colors[r.color]
                        : "auto");
  }
  if (r.highlight >= 0) {
    constexpr const char* colors[] = {
        "none",        "black",    "blue",       "cyan",
        "green",       "magenta",  "red",        "yellow",
        "white",       "darkBlue", "darkCyan",   "darkGreen",
        "darkMagenta", "darkRed",  "darkYellow", "darkGray"};
    Attr(n.append_child("w:highlight"), "w:val",
         r.highlight < 16 ? colors[r.highlight] : "none");
  }

  ShadePr(n, r.shade, r.shade_fore, r.shade_pattern);
  BorderPr(n, "w:bdr", r.border);
  if (r.revision_insert || r.revision_delete || r.property_revision) {
    auto change = n.append_child("w:rPrChange");
    IntAttr(change, "w:id", 0);
    Attr(change, "w:author", "doccraft");
    change.append_child("w:rPr");
  }
}
}  // namespace doccraft::docx
