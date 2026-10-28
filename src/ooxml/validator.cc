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
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ooxml/generated/wml_schema_data.h"
#include "third_party/pugixml/pugixml.hpp"

namespace doccraft::ooxml {
namespace {

constexpr char kWordmlNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

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
    for (pugi::xml_node child : node.children()) {
      if (child.type() == pugi::node_element)
        Walk(child, &frame, stack);
    }
    PopNamespaces(pushed, stack);
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
    const std::string qualified(node.name());
    const std::string prefix = PrefixPart(qualified);
    const std::string declaration = "xmlns:" + prefix;
    // The stack is not available here, so re-resolve from the node upwards.
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

  void Report(const Frame* frame, const std::string& message) {
    out_->push_back(Violation{std::string(part_), BuildPath(frame), message});
  }

  static void PopNamespaces(std::size_t count, NamespaceStack* stack) {
    for (std::size_t i = 0; i < count; ++i)
      stack->pop_back();
  }

  std::string_view part_;
  std::vector<Violation>* out_;
};

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

bool IsValid(std::string_view xml) {
  return ValidatePart(std::string_view(), xml).empty();
}

}  // namespace doccraft::ooxml
