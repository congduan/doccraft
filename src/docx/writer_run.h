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

#ifndef SRC_DOCX_WRITER_RUN_H_
#define SRC_DOCX_WRITER_RUN_H_
#include "model/document.h"
#include "third_party/pugixml/pugixml.hpp"
namespace doccraft::docx {

// COLORREF packing shared by the writer: red in bits 16-23, green in bits 8-15,
// blue in bits 0-7. A saturated top byte marks the "automatic" colour.
inline constexpr uint32_t kColorAutoFlag = 0xFF000000u;
inline constexpr uint32_t kColorChannelMask = 0xFF;
inline constexpr int kColorGreenShift = 8;
inline constexpr int kColorRedShift = 16;

// Low 16 bits of a symbol character code (MS-DOC CSymbolOperand).
inline constexpr uint32_t kLow16Mask = 0xFFFF;

void RunPr(pugi::xml_node parent, const model::Run& run);

}  // namespace doccraft::docx

#endif  // SRC_DOCX_WRITER_RUN_H_
