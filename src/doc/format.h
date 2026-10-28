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

#ifndef SRC_DOC_FORMAT_H_
#define SRC_DOC_FORMAT_H_
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "doc/fib.h"
#include "model/document.h"
namespace doccraft::doc {
inline constexpr size_t kMaxDocumentImageBytes = 256 * 1024 * 1024;
std::expected<model::Document, Error> ParseFormatted(
    std::span<const uint8_t> word, std::span<const uint8_t> table,
    const Fib& fib, std::span<const uint8_t> data = {});

std::expected<model::Image, Error> ReadInlinePicture(
    std::span<const uint8_t> data, uint32_t offset,
    size_t budget = std::numeric_limits<size_t>::max());

std::expected<model::Image, Error> ReadLegacyPicture(
    std::span<const uint8_t> word, uint32_t offset,
    size_t budget = std::numeric_limits<size_t>::max());

std::expected<std::optional<model::Image>, Error> ReadOlePresentation(
    std::span<const uint8_t> stream,
    size_t budget = std::numeric_limits<size_t>::max());

struct FloatingPicture {
  uint32_t cp = 0;
  model::Image image;
  std::optional<model::Shape> shape;
};
std::expected<std::vector<FloatingPicture>, Error> ReadFloatingPictures(
    std::span<const uint8_t> word, std::span<const uint8_t> table,
    const Fib& fib);
}  // namespace doccraft::doc

#endif  // SRC_DOC_FORMAT_H_
