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

#ifndef SRC_DOCX_XML_UTIL_H_
#define SRC_DOCX_XML_UTIL_H_

#include <cstdint>
#include <cstdio>
#include <string>

#include "model/document.h"
#include "third_party/pugixml/pugixml.hpp"

// Small helpers shared by the WordprocessingML writers (writer.cc and
// writer_run.cc). They were previously duplicated in both translation units.
namespace doccraft::docx::xml {

// COLORREF packing: red in bits 16-23, green in bits 8-15, blue in bits 0-7.
// A saturated top byte marks the "automatic" colour.
inline constexpr uint32_t kColorAutoFlag = 0xFF000000u;
inline constexpr uint32_t kColorChannelMask = 0xFF;
inline constexpr int kColorGreenShift = 8;
inline constexpr int kColorRedShift = 16;

// Low 16 bits of a symbol character code (MS-DOC CSymbolOperand).
inline constexpr uint32_t kLow16Mask = 0xFFFF;

// Built-in styles 0-9 map to Word's fixed style ids.
inline std::string Id(int index) {
  if (index == 0) {
    return "Normal";
  }
  if (index > 0 && index <= 9) {
    return "Heading" + std::to_string(index);
  }
  return "Style" + std::to_string(index);
}

inline void Attr(pugi::xml_node node, const char* name,
                 const std::string& value) {
  node.append_attribute(name).set_value(value.c_str());
}

inline void IntAttr(pugi::xml_node node, const char* name, int value) {
  node.append_attribute(name).set_value(value);
}

inline void One(pugi::xml_node parent, const char* name) {
  parent.append_child(name);
}

inline void Value(pugi::xml_node parent, const char* name, int value) {
  IntAttr(parent.append_child(name), "w:val", value);
}

// Tri-state property: `set` decides whether the element appears at all, and a
// false value is written explicitly as w:val="0".
inline void SpecBool(pugi::xml_node parent, const char* name, bool value,
                     bool set) {
  if (!set) {
    return;
  }
  auto node = parent.append_child(name);
  if (!value) {
    Attr(node, "w:val", "0");
  }
}

inline std::string Color(int rgb) {
  if (rgb < 0 ||
      (static_cast<uint32_t>(rgb) & kColorAutoFlag) == kColorAutoFlag) {
    return "auto";
  }
  char out[7];
  std::snprintf(out, sizeof(out), "%02X%02X%02X", rgb & kColorChannelMask,
                (rgb >> kColorGreenShift) & kColorChannelMask,
                (rgb >> kColorRedShift) & kColorChannelMask);
  return out;
}

inline const char* BorderType(int type) {
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

inline void BorderPr(pugi::xml_node parent, const char* name,
                     const model::Border& border) {
  if (!border.set) {
    return;
  }
  auto side = parent.append_child(name);
  Attr(side, "w:val", BorderType(border.type));
  if (!border.type) {
    return;
  }
  IntAttr(side, "w:sz", border.width < 2 ? 2 : border.width);
  Attr(side, "w:color", Color(border.color));
  IntAttr(side, "w:space", border.space);
  if (border.shadow) {
    Attr(side, "w:shadow", "1");
  }
  if (border.frame) {
    Attr(side, "w:frame", "1");
  }
}

inline void ShadePr(pugi::xml_node parent, int back, int fore, int pattern) {
  if (back < 0 && fore < 0) {
    return;
  }
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

}  // namespace doccraft::docx::xml

#endif  // SRC_DOCX_XML_UTIL_H_
