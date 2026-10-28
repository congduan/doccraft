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

// Resolves a SttbfRMark index to its author name. `document` null, an empty
// table, or an out-of-range index (already warned about at parse time)
// degrades to the historical placeholder author "doccraft".
const std::string& RevisionAuthor(const model::Document* document, int index);

void RunPr(pugi::xml_node parent, const model::Run& run,
           const model::Document* document);

}  // namespace doccraft::docx

#endif  // SRC_DOCX_WRITER_RUN_H_
