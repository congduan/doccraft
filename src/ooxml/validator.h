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

#ifndef SRC_OOXML_VALIDATOR_H_
#define SRC_OOXML_VALIDATOR_H_

#include <string>
#include <string_view>
#include <vector>

namespace doccraft::ooxml {

// One schema violation, mirroring what OpenXmlValidator reports: the part,
// the element path (local names with 1-based sibling indices) and a short
// reason. Messages are ours, but the set of reported problems is meant to
// agree with the SDK.
struct Violation {
  std::string part;
  std::string path;
  std::string message;
};

// Validates a WordprocessingML part against the schema tables generated from
// DocumentFormat.OpenXml metadata: element order, occurrence bounds, unknown
// elements and unexpected content. Attributes are not checked yet (see
// docs/KNOWN-SIMPLIFICATIONS.md).
//
// `part_name` is only used to label violations.
std::vector<Violation> ValidatePart(std::string_view part_name,
                                    std::string_view xml);

// True when ValidatePart() reports nothing.
bool IsValid(std::string_view xml);

}  // namespace doccraft::ooxml

#endif  // SRC_OOXML_VALIDATOR_H_
