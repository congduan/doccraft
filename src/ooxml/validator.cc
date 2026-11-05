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

#include "ooxml/validator.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ooxml/generated/wml_schema_data.h"
#include "third_party/pugixml/pugixml.hpp"

namespace doccraft::ooxml {
namespace {

constexpr char kWordmlNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

constexpr char kRelationshipsNamespace[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

// The AttributeSchema rows of one element slice are sorted by local name, so
// the lookup is a binary search over the slice.
const AttributeSchema* FindAttribute(const ElementSchema* schema,
                                     std::string_view local) {
  std::size_t low = 0;
  std::size_t high = schema->attribute_count;
  while (low < high) {
    const std::size_t mid = low + (high - low) / 2;
    const AttributeSchema& candidate =
        generated::kAttributes[schema->first_attribute + mid];
    const int order = std::string_view(candidate.local).compare(local);
    if (order < 0) {
      low = mid + 1;
    } else if (order > 0) {
      high = mid;
    } else {
      return &candidate;
    }
  }
  return nullptr;
}

// ST_OnOff: the union of xsd:boolean and the two-word spelling the
// transitional schema allows. An empty value is not a member.
bool IsOnOffValue(std::string_view value) {
  return value == "true" || value == "false" || value == "on" ||
         value == "off" || value == "1" || value == "0";
}

// Unbounded xsd:integer lexical shape: optional sign, at least one decimal
// digit, nothing else.
bool IsIntegerShape(std::string_view value) {
  if (value.empty())
    return false;
  const std::size_t digits = value.front() == '+' || value.front() == '-'
                                 ? value.size() - 1
                                 : value.size();
  if (digits == 0)
    return false;
  return std::all_of(value.begin() + (value.size() - digits), value.end(),
                     [](char digit) {
                       return digit >= '0' && digit <= '9';
                     });
}

// Parses a sign-prefixed decimal integer. Returns false on any other shape:
// xsd integer lexical space has no underscores, spaces or exponent form.
bool ParseInteger(std::string_view text, bool signed_type, int64_t* out) {
  if (text.empty())
    return false;
  std::size_t at = 0;
  bool negative = false;
  if (text[0] == '+' || text[0] == '-') {
    negative = text[0] == '-';
    at = 1;
    if (!signed_type && text[0] == '-')
      return false;
  }
  if (at >= text.size())
    return false;
  if (!std::all_of(text.begin() + static_cast<std::ptrdiff_t>(at), text.end(),
                   [](char digit) {
                     return digit >= '0' && digit <= '9';
                   }))
    return false;
  // Leading zeros carry no magnitude, so the range gate counts only the
  // digits after them: "0000000000000000000001" is 1, not 22 digits of
  // overflow. FromInt is exact for the 19-digit magnitudes below; a longer
  // effective input is a range failure, reported the same way as a parse one.
  std::size_t digits = at;
  while (digits + 1 < text.size() && text[digits] == '0')
    ++digits;
  if (text.size() - digits > 19)
    return false;
  uint64_t magnitude = 0;
  for (const char digit : text.substr(at)) {
    if (digit < '0' || digit > '9')
      return false;
    magnitude = magnitude * 10 + static_cast<uint64_t>(digit - '0');
  }
  if (!negative) {
    *out = static_cast<int64_t>(magnitude);
    return magnitude <= static_cast<uint64_t>(INT64_MAX);
  }
  if (magnitude > static_cast<uint64_t>(INT64_MAX) + 1)
    return false;
  *out = magnitude == static_cast<uint64_t>(INT64_MAX) + 1
             ? INT64_MIN
             : -static_cast<int64_t>(magnitude);
  return true;
}

// One lexical check per SDK attribute type. The kind decides which check
// runs; kString, kEnum, kDateTime and kOther accept any lexical form.
bool AttributeValueMatches(const AttributeSchema& attribute,
                           std::string_view value, std::string* why_not) {
  switch (attribute.type) {
    case AttributeType::kString:
    case AttributeType::kEnum:
    case AttributeType::kDateTime:
    case AttributeType::kOther:
      return true;
    case AttributeType::kOnOff:
      if (IsOnOffValue(value))
        return true;
      *why_not = "is not a valid ST_OnOff value (true/false/on/off/1/0)";
      return false;
    case AttributeType::kInteger:
      // xsd:integer is unbounded: only the lexical shape (optional sign
      // followed by decimal digits) gates the value, with no magnitude cap.
      if (IsIntegerShape(value))
        return true;
      *why_not = "is not a valid xsd:integer value";
      return false;
    case AttributeType::kHexBinary: {
      // xsd:hexBinary: an even number of hexadecimal digits.
      if (value.size() % 2 == 0 &&
          std::all_of(value.begin(), value.end(), [](char digit) {
            return (digit >= '0' && digit <= '9') ||
                   (digit >= 'a' && digit <= 'f') ||
                   (digit >= 'A' && digit <= 'F');
          })) {
        return true;
      }
      *why_not = "is not a valid xsd:hexBinary value";
      return false;
    }
    case AttributeType::kBase64Binary: {
      // xsd:base64Binary: after stripping whitespace, the length is a
      // multiple of four, only the base64 alphabet remains, '=' appears
      // only as the final one or two characters, and the six-bit values
      // under any trailing padding are zero (the pad bits must be zero).
      const bool lexical = std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=' ||
               c == ' ' || c == '\t' || c == '\n' || c == '\r';
      });
      const std::string packed = [&] {
        std::string out;
        out.reserve(value.size());
        for (const char c : value) {
          if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            out.push_back(c);
        }
        return out;
      }();
      // W3C lexical rule: whitespace may appear between quanta and is not
      // part of the value, so all of it is stripped above — not just the
      // leading and trailing characters.
      const std::size_t pad_start = packed.find('=');
      const bool has_padding = pad_start != std::string_view::npos;
      // Total length a multiple of four, always; with padding, one or two
      // '=' at the very end — every character after the first '=' is '='.
      const bool padding_shape =
          packed.size() % 4 == 0 &&
          (!has_padding || (packed.size() - pad_start <= 2 &&
                            packed.substr(pad_start).find_first_not_of('=') ==
                                std::string_view::npos));
      // One padding character discards two bits, two discard four: the
      // non-padding characters of the final quantum must carry zeroes in
      // the discarded low bits.
      bool pad_bits_clear = true;
      if (has_padding && packed.size() >= 3) {
        const auto six_bits = [](char c) {
          constexpr char kAlphabet[] =
              "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz012345678"
              "9+/";
          const char* at = std::strchr(kAlphabet, c);
          return at == nullptr ? -1 : static_cast<int>(at - kAlphabet);
        };
        if (packed.size() - pad_start == 2) {
          const int high = six_bits(packed[packed.size() - 3]);
          // Two '=' drop four bits: low nibble of the last data sextet
          // must be zero.
          pad_bits_clear = high >= 0 && (high & 0x0f) == 0;
        } else {
          const int high = six_bits(packed[packed.size() - 3]);
          const int low = six_bits(packed[packed.size() - 2]);
          // One '=' drops two bits: low pair of the combined twelve bits
          // must be zero.
          pad_bits_clear =
              high >= 0 && low >= 0 && ((high << 6 | low) & 0x03) == 0;
        }
      }
      if (lexical && padding_shape && pad_bits_clear)
        return true;
      *why_not = "is not a valid xsd:base64Binary value";
      return false;
    }
    default:
      break;
  }
  static constexpr struct {
    AttributeType type;
    bool signed_type;
    int64_t low;
    int64_t high;
  } kBounds[] = {
      {AttributeType::kInt16, true, INT16_MIN, INT16_MAX},
      {AttributeType::kInt32, true, INT32_MIN, INT32_MAX},
      {AttributeType::kInt64, true, INT64_MIN, INT64_MAX},
      {AttributeType::kUInt16, false, 0, UINT16_MAX},
      {AttributeType::kUInt32, false, 0, UINT32_MAX},
      {AttributeType::kByte, false, 0, UINT8_MAX},
  };
  for (const auto& bound : kBounds) {
    if (bound.type != attribute.type)
      continue;
    int64_t parsed = 0;
    if (ParseInteger(value, bound.signed_type, &parsed) &&
        parsed >= bound.low && parsed <= bound.high) {
      return true;
    }
    *why_not = "is not a valid integer in the attribute's range";
    return false;
  }
  return true;
}

// Positions in the child list; kNoIndex marks a child whose element name is
// absent from the schema table.
using Positions = std::vector<uint16_t>;

void SortUnique(Positions* values) {
  std::sort(values->begin(), values->end());
  values->erase(std::unique(values->begin(), values->end()), values->end());
}

Positions Union(const Positions& left, const Positions& right) {
  Positions merged;
  merged.reserve(left.size() + right.size());
  std::set_union(left.begin(), left.end(), right.begin(), right.end(),
                 std::back_inserter(merged));
  return merged;
}

const ElementSchema* FindElement(std::string_view ns, std::string_view local) {
  std::size_t low = 0;
  std::size_t high = generated::kElementCount;
  while (low < high) {
    const std::size_t mid = low + (high - low) / 2;
    const ElementSchema& candidate = generated::kElements[mid];
    int order = std::string_view(candidate.local).compare(local);
    if (order == 0)
      order = std::string_view(candidate.ns).compare(ns);
    if (order < 0) {
      low = mid + 1;
    } else if (order > 0) {
      high = mid;
    } else {
      return &candidate;
    }
  }
  return nullptr;
}

uint16_t ElementIndex(const ElementSchema* schema) {
  return static_cast<uint16_t>(schema - generated::kElements);
}

// Backtracking matcher over one element's children. memo_ keeps the set of
// reachable positions for every (particle, position) pair, which bounds the
// cost of the deeply nested xs:choice groups found in w:p and w:pPr.
class Matcher {
 public:
  explicit Matcher(const Positions& children) : children_(children) {}

  Positions Match(uint16_t particle, uint16_t position) {
    const uint64_t key = (static_cast<uint64_t>(particle) << 32) |
                         static_cast<uint64_t>(position);
    const auto cached = memo_.find(key);
    if (cached != memo_.end())
      return cached->second;

    const ParticleNode& node = generated::kParticles[particle];
    const int min_occurs = node.min_occurs;
    const int limit = node.max_occurs == kUnbounded
                          ? static_cast<int>(children_.size()) + 1
                          : static_cast<int>(node.max_occurs);

    Positions result;
    Positions frontier{position};
    if (min_occurs == 0)
      result.push_back(position);

    for (int count = 1; count <= limit; ++count) {
      Positions next;
      for (uint16_t at : frontier) {
        const Positions once = MatchOnce(particle, at);
        next.insert(next.end(), once.begin(), once.end());
      }
      SortUnique(&next);
      if (next.empty())
        break;
      if (count >= min_occurs)
        result = Union(result, next);
      frontier = std::move(next);
    }

    SortUnique(&result);
    memo_.emplace(key, result);
    return result;
  }

 private:
  Positions MatchOnce(uint16_t particle, uint16_t position) {
    const ParticleNode& node = generated::kParticles[particle];
    switch (node.kind) {
      case ParticleKind::kElement:
        if (position < children_.size() && node.element != kNoIndex &&
            children_[position] == node.element) {
          return {static_cast<uint16_t>(position + 1)};
        }
        return {};
      case ParticleKind::kAny:
        if (position < children_.size()) {
          return {static_cast<uint16_t>(position + 1)};
        }
        return {};
      case ParticleKind::kChoice: {
        Positions result;
        for (uint16_t i = 0; i < node.slot_count; ++i) {
          const uint16_t child = generated::kChildSlots[node.first_slot + i];
          result = Union(result, Match(child, position));
        }
        return result;
      }
      case ParticleKind::kSequence:
      case ParticleKind::kGroup:
      case ParticleKind::kAll: {
        // xs:all is rare here (three particles in WordprocessingML) and its
        // members are optional, so sequence order is an acceptable
        // approximation; tracked in docs/KNOWN-SIMPLIFICATIONS.md.
        Positions current{position};
        for (uint16_t i = 0; i < node.slot_count; ++i) {
          const uint16_t child = generated::kChildSlots[node.first_slot + i];
          Positions next;
          for (uint16_t at : current) {
            next = Union(next, Match(child, at));
          }
          current = std::move(next);
          if (current.empty())
            break;
        }
        return current;
      }
    }
    return {};
  }

  const Positions& children_;
  std::unordered_map<uint64_t, Positions> memo_;
};

struct Frame {
  pugi::xml_node node;
  const Frame* parent;
};

std::string LocalPart(std::string_view qualified) {
  const std::size_t colon = qualified.find(':');
  return std::string(colon == std::string_view::npos
                         ? qualified
                         : qualified.substr(colon + 1));
}

std::string PrefixPart(std::string_view qualified) {
  const std::size_t colon = qualified.find(':');
  return colon == std::string_view::npos
             ? std::string()
             : std::string(qualified.substr(0, colon));
}

using NamespaceStack = std::vector<std::pair<std::string, std::string>>;

std::size_t PushNamespaces(pugi::xml_node node, NamespaceStack* stack) {
  std::size_t pushed = 0;
  for (pugi::xml_attribute attribute : node.attributes()) {
    const std::string_view name(attribute.name());
    if (name == "xmlns") {
      stack->emplace_back(std::string(), attribute.value());
      ++pushed;
    } else if (name.substr(0, 6) == "xmlns:") {
      stack->emplace_back(std::string(name.substr(6)), attribute.value());
      ++pushed;
    }
  }
  return pushed;
}

bool ResolveNamespace(const std::string& prefix, const NamespaceStack& stack,
                      std::string* out) {
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    if (it->first == prefix) {
      *out = it->second;
      return true;
    }
  }
  return false;
}

// Resolves an element's namespace from the declarations in scope, starting at
// the element itself and walking up to the document root.
std::string ElementNamespace(pugi::xml_node node) {
  const std::string qualified(node.name());
  const std::size_t colon = qualified.find(':');
  const std::string prefix =
      colon == std::string::npos ? std::string() : qualified.substr(0, colon);
  const std::string declaration = "xmlns:" + prefix;
  for (pugi::xml_node scope = node; scope; scope = scope.parent()) {
    for (pugi::xml_attribute attribute : scope.attributes()) {
      const std::string name(attribute.name());
      if (prefix.empty() && name == "xmlns")
        return attribute.value();
      if (!prefix.empty() && name == declaration)
        return attribute.value();
    }
  }
  return std::string();
}

// OPC part names have no leading slash in a ZIP, but relationship Targets may
// be written as absolute references ("/word/document.xml").
std::string StripLeadingSlashes(std::string_view name) {
  while (!name.empty() && name.front() == '/')
    name.remove_prefix(1);
  return std::string(name);
}

// Collapses "." and ".." segments and empty separators. Climbing past the
// package root is dropped: the result then simply fails the existence check.
std::string NormalizePartPath(std::string_view path) {
  std::vector<std::string_view> segments;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find('/', start);
    if (end == std::string_view::npos)
      end = path.size();
    const std::string_view segment = path.substr(start, end - start);
    if (segment.empty() || segment == ".") {
      // Empty separators and the current directory are no-ops.
    } else if (segment == "..") {
      if (!segments.empty())
        segments.pop_back();
    } else {
      segments.push_back(segment);
    }
    if (end == path.size())
      break;
    start = end + 1;
  }

  std::string normalized;
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (i != 0)
      normalized.push_back('/');
    normalized.append(segments[i]);
  }
  return normalized;
}

// "word/document.xml" -> "word/_rels/document.xml.rels"; a part without a
// directory gets "_rels/<part>.rels", and the package root (empty name) maps
// to "_rels/.rels".
std::string RelationshipsPartName(std::string_view part) {
  const std::string name = StripLeadingSlashes(part);
  const std::size_t slash = name.rfind('/');
  if (slash == std::string::npos)
    return "_rels/" + name + ".rels";
  return name.substr(0, slash + 1) + "_rels/" + name.substr(slash + 1) +
         ".rels";
}

// The directory that a relationship Target is resolved against: the directory
// of the part that owns the .rels file, not the directory of the .rels file
// itself.
std::string RelationshipsOwnerDirectory(std::string_view rels_part) {
  const std::string name = StripLeadingSlashes(rels_part);
  const std::size_t slash = name.rfind('/');
  if (slash == std::string::npos)
    return std::string();
  std::string directory = name.substr(0, slash + 1);
  constexpr std::string_view kRelsDirectory = "_rels/";
  if (directory.size() >= kRelsDirectory.size() &&
      directory.compare(directory.size() - kRelsDirectory.size(),
                        kRelsDirectory.size(), kRelsDirectory) == 0) {
    directory.resize(directory.size() - kRelsDirectory.size());
  }
  return directory;
}

std::string ResolveRelationshipTarget(std::string_view owner_directory,
                                      std::string_view target) {
  if (!target.empty() && target.front() == '/')
    return NormalizePartPath(target);
  std::string joined(owner_directory);
  joined.append(target);
  return NormalizePartPath(joined);
}

std::string BuildPath(const Frame* frame) {
  if (frame == nullptr)
    return std::string();
  std::string path = BuildPath(frame->parent);
  const std::string local = LocalPart(frame->node.name());
  int index = 1;
  for (pugi::xml_node sibling = frame->node.previous_sibling(); sibling;
       sibling = sibling.previous_sibling()) {
    if (sibling.type() == pugi::node_element &&
        LocalPart(sibling.name()) == local) {
      ++index;
    }
  }
  path += '/';
  path += local;
  path += '[';
  path += std::to_string(index);
  path += ']';
  return path;
}

class PartValidator {
 public:
  PartValidator(std::string_view part, std::vector<Violation>* out)
      : part_(part), out_(out) {}

  void Walk(pugi::xml_node node, const Frame* parent, NamespaceStack* stack) {
    stack_ = stack;
    const std::size_t pushed = PushNamespaces(node, stack);
    const std::string qualified(node.name());
    const std::string prefix = PrefixPart(qualified);
    const std::string local = LocalPart(qualified);

    std::string ns;
    const bool has_ns = ResolveNamespace(prefix, *stack, &ns);
    if (!has_ns || ns != kWordmlNamespace) {
      // Markup Compatibility and extension namespaces are outside the
      // WordprocessingML schema; skip them instead of guessing.
      PopNamespaces(pushed, stack);
      return;
    }

    const Frame frame{node, parent};
    const ElementSchema* schema = FindElement(ns, local);
    if (schema == nullptr) {
      // The parent's ordering check already reports unknown children when the
      // position rejects them; only a root element has no parent to do that.
      if (parent == nullptr) {
        Report(&frame, "element <" + local +
                           "> is not defined by the WordprocessingML schema");
      }
      for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_element)
          Walk(child, &frame, stack);
      }
      PopNamespaces(pushed, stack);
      return;
    }

    CheckChildren(node, schema, &frame);
    CheckAttributes(node, schema, &frame);
    for (pugi::xml_node child : node.children()) {
      if (child.type() == pugi::node_element)
        Walk(child, &frame, stack);
    }
    PopNamespaces(pushed, stack);
  }

  // Attributes: unknown WML-namespace names are violations; known ones get
  // the lexical check their SDK type implies. Namespaces are resolved from
  // the walk's stack, which by now holds every declaration in scope.
  void CheckAttributes(pugi::xml_node node, const ElementSchema* schema,
                       const Frame* frame) {
    for (pugi::xml_attribute attribute : node.attributes()) {
      const std::string qualified(attribute.name());
      if (qualified.rfind("xmlns", 0) == 0)
        continue;  // Namespace declarations are not schema attributes.
      const std::string prefix = PrefixPart(qualified);
      const std::string local = LocalPart(qualified);
      // XML namespaces do not apply to unprefixed attributes: a default
      // namespace declaration never pulls them in. Skip them outright.
      if (prefix.empty())
        continue;
      std::string ns;
      if (!ResolveNamespace(prefix, *stack_, &ns) || ns != kWordmlNamespace)
        continue;  // xml: and extension-namespace attributes are out of scope.
      const AttributeSchema* attribute_schema = FindAttribute(schema, local);
      if (attribute_schema == nullptr) {
        Report(frame, "unknown attribute '" + local + "' on element <" +
                          std::string(schema->local) + ">");
        continue;
      }
      std::string_view value(attribute.value());
      std::string why_not;
      if (!AttributeValueMatches(*attribute_schema, value, &why_not)) {
        Report(frame, "attribute '" + local + "' value '" + std::string(value) +
                          "' " + why_not);
      }
    }
  }

 private:
  void CheckChildren(pugi::xml_node node, const ElementSchema* schema,
                     const Frame* frame) {
    Positions children;
    std::vector<pugi::xml_node> nodes;
    for (pugi::xml_node child : node.children()) {
      if (child.type() != pugi::node_element)
        continue;
      const std::string local = LocalPart(child.name());
      const std::string ns = NamespaceOf(child);
      const ElementSchema* child_schema = FindElement(ns, local);
      if (child_schema == nullptr) {
        if (ns == kWordmlNamespace) {
          // Unknown WordprocessingML element: keep it so the ordering check
          // reports it at the right position.
          children.push_back(kNoIndex);
          nodes.push_back(child);
        }
        // Other namespaces are Markup Compatibility extension points, which
        // may legitimately appear anywhere; they take no part in the
        // sequence and their own content is not validated.
        continue;
      }
      children.push_back(ElementIndex(child_schema));
      nodes.push_back(child);
    }

    if (children.size() >= kNoIndex)
      return;  // Defensive: too many children.
    if (schema->particle == kNoIndex)
      return;

    Matcher matcher(children);
    const Positions reachable = matcher.Match(schema->particle, 0);
    const uint16_t total = static_cast<uint16_t>(children.size());
    if (std::binary_search(reachable.begin(), reachable.end(), total))
      return;

    uint16_t furthest = 0;
    for (uint16_t position : reachable) {
      furthest = std::max(furthest, position);
    }

    if (furthest < total) {
      const std::string local = LocalPart(nodes[furthest].name());
      const ElementSchema* offender =
          children[furthest] == kNoIndex
              ? nullptr
              : &generated::kElements[children[furthest]];
      if (offender == nullptr) {
        Report(frame, "element <" + local +
                          "> is not defined by the WordprocessingML schema");
      } else {
        Report(frame, "element <" + local +
                          "> is not allowed here: it breaks the schema "
                          "element order");
      }
    } else {
      Report(frame, "content is incomplete: required child elements missing");
    }
  }

  // Resolves a child's namespace without pushing its declarations; the
  // element walk handles the stack proper, this only feeds ordering checks.
  std::string NamespaceOf(pugi::xml_node node) {
    return ElementNamespace(node);
  }

  void Report(const Frame* frame, const std::string& message) {
    out_->push_back(Violation{std::string(part_), BuildPath(frame), message});
  }

  static void PopNamespaces(std::size_t count, NamespaceStack* stack) {
    for (std::size_t i = 0; i < count; ++i)
      stack->pop_back();
  }

  std::string_view part_;
  NamespaceStack* stack_ = nullptr;
  std::vector<Violation>* out_;
};

// Parses one .rels part, reports duplicate Ids, empty Targets and internal
// Targets that name no part in the package, and returns the defined Ids.
std::unordered_set<std::string> ScanRelationships(
    std::string_view part_name, std::string_view xml,
    const std::unordered_set<std::string>& package_parts,
    std::vector<Violation>* out) {
  std::unordered_set<std::string> ids;
  pugi::xml_document document;
  const pugi::xml_parse_result parsed = document.load_buffer(
      xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
  if (!parsed) {
    out->push_back(Violation{
        std::string(part_name), std::string(),
        "XML is not well-formed: " + std::string(parsed.description())});
    return ids;
  }

  pugi::xml_node root = document.document_element();
  if (!root) {
    out->push_back(Violation{std::string(part_name), std::string(),
                             "XML has no document element"});
    return ids;
  }

  const std::string owner_directory = RelationshipsOwnerDirectory(part_name);
  const Frame root_frame{root, nullptr};
  for (pugi::xml_node child : root.children()) {
    if (child.type() != pugi::node_element ||
        LocalPart(child.name()) != "Relationship") {
      continue;
    }
    const Frame frame{child, &root_frame};
    const std::string path = BuildPath(&frame);
    const std::string id = child.attribute("Id").value();
    const std::string target = child.attribute("Target").value();
    const std::string mode = child.attribute("TargetMode").value();

    if (!ids.insert(id).second) {
      out->push_back(Violation{std::string(part_name), path,
                               "duplicate Relationship Id '" + id + "'"});
    }
    if (target.empty()) {
      out->push_back(
          Violation{std::string(part_name), path,
                    "Relationship '" + id + "' has an empty Target"});
      continue;
    }
    if (mode == "External")
      continue;
    const std::string resolved =
        ResolveRelationshipTarget(owner_directory, target);
    if (!package_parts.contains(resolved)) {
      out->push_back(
          Violation{std::string(part_name), path,
                    "Relationship '" + id + "' Target '" + target +
                        "' does not name a part in the package (resolved to '" +
                        resolved + "')"});
    }
  }
  return ids;
}

// Reports every r:id / r:embed attribute in one WordprocessingML content part
// that the part's .rels file does not define. Attribute matching is by
// (namespace, local name), like the element-side checks; xml: reserved
// attributes and attributes with no prefix are not in a namespace.
void ScanRelationshipReferences(pugi::xml_node node, const Frame* parent,
                                NamespaceStack* stack,
                                std::string_view part_name,
                                const std::string& rels_part,
                                const std::unordered_set<std::string>* ids,
                                bool has_rels, std::vector<Violation>* out) {
  const std::size_t pushed = PushNamespaces(node, stack);
  const Frame frame{node, parent};
  for (pugi::xml_attribute attribute : node.attributes()) {
    const std::string qualified(attribute.name());
    if (qualified.rfind("xmlns", 0) == 0)
      continue;
    const std::string prefix = PrefixPart(qualified);
    if (prefix.empty())
      continue;
    std::string ns;
    if (!ResolveNamespace(prefix, *stack, &ns) ||
        ns != kRelationshipsNamespace) {
      continue;
    }
    const std::string local = LocalPart(qualified);
    if (local != "id" && local != "embed")
      continue;
    const std::string value(attribute.value());
    if (ids != nullptr && ids->contains(value))
      continue;

    std::string message = "attribute '" + qualified + "' value '" + value +
                          "' does not resolve to a Relationship Id";
    message +=
        has_rels ? " in " + rels_part : " (the part has no " + rels_part + ")";
    out->push_back(
        Violation{std::string(part_name), BuildPath(&frame), message});
  }

  for (pugi::xml_node child : node.children()) {
    if (child.type() == pugi::node_element) {
      ScanRelationshipReferences(child, &frame, stack, part_name, rels_part,
                                 ids, has_rels, out);
    }
  }
  for (std::size_t i = 0; i < pushed; ++i)
    stack->pop_back();
}

}  // namespace

std::vector<Violation> ValidatePart(std::string_view part_name,
                                    std::string_view xml) {
  std::vector<Violation> violations;
  pugi::xml_document document;
  const pugi::xml_parse_result parsed = document.load_buffer(
      xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
  if (!parsed) {
    violations.push_back(Violation{
        std::string(part_name), std::string(),
        "XML is not well-formed: " + std::string(parsed.description())});
    return violations;
  }

  pugi::xml_node root = document.document_element();
  if (!root) {
    violations.push_back(Violation{std::string(part_name), std::string(),
                                   "XML has no document element"});
    return violations;
  }

  PartValidator validator(part_name, &violations);
  NamespaceStack stack;
  validator.Walk(root, nullptr, &stack);
  return violations;
}

std::vector<Violation> ValidatePackage(const std::vector<PackagePart>& parts) {
  std::vector<Violation> violations;

  std::unordered_set<std::string> package_parts;
  package_parts.reserve(parts.size());
  for (const PackagePart& part : parts)
    package_parts.insert(NormalizePartPath(part.name));

  // Relationships first: the content pass needs the Ids each part defines.
  std::unordered_map<std::string, std::unordered_set<std::string>> ids_by_rels;
  for (const PackagePart& part : parts) {
    const std::string name = NormalizePartPath(part.name);
    if (!name.ends_with(".rels"))
      continue;
    ids_by_rels.emplace(name, ScanRelationships(name, part.content,
                                                package_parts, &violations));
  }

  for (const PackagePart& part : parts) {
    const std::string name = NormalizePartPath(part.name);
    if (name.ends_with(".rels") || !name.ends_with(".xml"))
      continue;

    pugi::xml_document document;
    const pugi::xml_parse_result parsed =
        document.load_buffer(part.content.data(), part.content.size(),
                             pugi::parse_default, pugi::encoding_utf8);
    if (!parsed)
      continue;  // ValidatePart() reports malformed WordprocessingML parts.
    pugi::xml_node root = document.document_element();
    if (!root || ElementNamespace(root) != kWordmlNamespace)
      continue;

    const std::string rels_part = RelationshipsPartName(name);
    const auto found = ids_by_rels.find(rels_part);
    NamespaceStack stack;
    ScanRelationshipReferences(
        root, nullptr, &stack, name, rels_part,
        found == ids_by_rels.end() ? nullptr : &found->second,
        found != ids_by_rels.end(), &violations);
  }

  return violations;
}

bool IsValid(std::string_view xml) {
  return ValidatePart(std::string_view(), xml).empty();
}

}  // namespace doccraft::ooxml
