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

#ifndef SRC_DOC_FORMAT_FORMATTED_HELPERS_H_
#define SRC_DOC_FORMAT_FORMATTED_HELPERS_H_

#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "doc/format/internal.h"

namespace doccraft::doc {

// One open complex field ([MS-DOC] §2.5.1 \x13 instruction \x14 result \x15).
enum class FieldMode {
  kPending,
  kHyperlink,  // rewritten as w:hyperlink, no fldChar remains
  kPage,       // rewritten as w:fldSimple
  kGeneric,    // w:fldChar begin/instrText/separate ... end
};

struct FieldFrame {
  std::string instruction;
  bool in_instruction = true;
  FieldMode mode = FieldMode::kPending;
  std::string hyperlink;
  bool page = false;
};

std::expected<std::vector<std::pair<uint32_t, model::Section>>, Error>
ReadSections(detail::Bytes word, detail::Bytes table, const Fib& fib);
void ApplyFieldInstruction(const std::string& instruction,
                           std::string* hyperlink, bool* page_field);
void ApplyParagraphSprms(model::Paragraph& para, const detail::FormatRange* pap,
                         detail::Bytes data);
std::expected<void, Error> FinishTableRow(model::Table& table,
                                          std::vector<model::Cell>& row,
                                          model::Paragraph& para,
                                          const detail::FormatRange* pap,
                                          detail::Bytes data);

}  // namespace doccraft::doc

#endif  // SRC_DOC_FORMAT_FORMATTED_HELPERS_H_
