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

#include "model/document.h"

#include <string>
#include <utility>
namespace doccraft::model {
Document FromText(const std::string& text) {
  Document doc;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      Paragraph p;
      p.runs.emplace_back();
      p.runs.back().text = text.substr(start, i - start);
      doc.paragraphs.push_back(std::move(p));
      start = i + 1;
    }
  }
  if (start < text.size() || doc.paragraphs.empty()) {
    Paragraph p;
    p.runs.emplace_back();
    p.runs.back().text = text.substr(start);
    doc.paragraphs.push_back(std::move(p));
  }
  return doc;
}
}  // namespace doccraft::model
