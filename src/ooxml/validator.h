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

// One part of an OPC package. `name` is the ZIP entry path (for example
// "word/document.xml") and `content` is its bytes. The caller keeps `content`
// alive for the duration of ValidatePackage().
struct PackagePart {
  std::string name;
  std::string_view content;
};

// Validates a WordprocessingML part against the schema tables generated from
// DocumentFormat.OpenXml metadata: element order, occurrence bounds, unknown
// elements, unexpected content, unknown WML-namespace attributes and the
// lexical form of typed attribute values. Enum members, required attributes
// and DateTime formats are out of scope; part/relationship references are
// checked by ValidatePackage() instead (see docs/KNOWN-SIMPLIFICATIONS.md).
//
// `part_name` is only used to label violations.
std::vector<Violation> ValidatePart(std::string_view part_name,
                                    std::string_view xml);

// Validates the relationship graph of a package:
//   * every r:id / r:embed reference in an XML content part resolves to a
//     Relationship Id defined by that part's .rels part (the scan covers
//     every .xml part, not only WordprocessingML-rooted ones);
//   * Relationship Ids are unique within one .rels part;
//   * every internal Relationship Target names a part that exists in the
//     package. Resolution follows RFC 3986 §5.2.2: drop the fragment, drop
//     the query, percent-decode, then merge with the owner part's directory
//     and normalize "." / ".." (a leading "/" starts at the package root).
//     Only literal delimiters are cut, so "%3F" and "%23" stay part-name
//     characters; an empty relative path inherits the owner part's full
//     path, so "#anchor" names the part that owns the .rels file. A path
//     that climbs past the package root is a violation: that is a project
//     tightening, not OPC CombinePartUri behaviour, which folds such a ".."
//     back inside the package. TargetMode="External" is exempt; an empty
//     Target is always a violation.
// Relationship Type / target-kind agreement, the schema of .rels parts,
// external URL syntax and [Content_Types].xml consistency are out of scope.
std::vector<Violation> ValidatePackage(const std::vector<PackagePart>& parts);

// True when ValidatePart() reports nothing.
bool IsValid(std::string_view xml);

}  // namespace doccraft::ooxml

#endif  // SRC_OOXML_VALIDATOR_H_
