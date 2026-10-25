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

#include "doc/text.h"

#include <string>

#include "doc/format/internal.h"

namespace doccraft::doc {

// The CLX piece table has exactly one reader (doc::detail::ReadChars, shared
// with the formatted path). This is its plain-text projection, used by
// `inspect` and by the corpus text baseline.
std::expected<std::string, Error> ExtractText(std::span<const uint8_t> word,
                                              std::span<const uint8_t> table,
                                              const Fib& fib) {
  auto chars = detail::ReadChars(word, table, fib, fib.ccp_text);
  if (!chars)
    return std::unexpected(chars.error());
  std::string out;
  for (const auto& ch : *chars)
    out += ch.text;
  return out;
}

}  // namespace doccraft::doc
