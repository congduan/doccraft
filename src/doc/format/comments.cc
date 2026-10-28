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

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "doc/format/sprm.h"

namespace doccraft::doc::detail {
namespace {

// PlcfandRef: (n + 1) CPs followed by n ATRDPre10 records of 30 bytes
// ([MS-DOC] §2.8.7). The author index sits at byte 20 of the record; it is
// validated against the author array, so a misread falls back to the initials.
constexpr size_t kAtrdSize = 30;
constexpr size_t kAuthorIndexOffset = 20;
constexpr size_t kInitialsCchSize = 2;
constexpr size_t kInitialsCapacity = 9;
constexpr size_t kUtf16CodeUnitSize = 2;
static_assert(kInitialsCchSize + kInitialsCapacity * kUtf16CodeUnitSize ==
              kAuthorIndexOffset);
constexpr size_t kBookmarkTagOffset = 26;
constexpr size_t kAtnbeSize = 10;

struct CommentAnchor {
  uint32_t cp = 0;
  std::string initials;
  int author = -1;
  uint32_t bookmark_tag = UINT32_MAX;
};

std::expected<std::vector<CommentAnchor>, Error> ReadAnchors(
    Bytes table, Fib::Range r, std::vector<std::string>* warnings = nullptr) {
  std::vector<CommentAnchor> out;
  if (!r.size) {
    return out;
  }
  if (!Valid(table, r) || r.size < 4 + kAtrdSize || (r.size - 4) % 34) {
    return std::unexpected(Error{"invalid comment anchor PLC"});
  }
  Bytes plc = table.subspan(r.offset, r.size);
  const size_t count = (r.size - 4) / 34;
  uint32_t previous = 0;
  for (size_t i = 0; i < count; i++) {
    CommentAnchor anchor;
    anchor.cp = U32(plc, 4 * i);
    if (i && anchor.cp < previous)
      return std::unexpected(Error{"invalid comment anchor PLC CP order"});
    previous = anchor.cp;
    const size_t atrd = 4 * (count + 1) + kAtrdSize * i;
    const size_t cch = U16(plc, atrd);
    if (cch > kInitialsCapacity) {
      if (warnings)
        warnings->push_back("skipped invalid comment initials: id " +
                            std::to_string(i + 1));
    } else {
      for (size_t k = 0; k < cch; ++k) {
        AppendUtf8(anchor.initials,
                   U16(plc, atrd + kInitialsCchSize + k * kUtf16CodeUnitSize));
      }
    }
    const uint16_t author = U16(plc, atrd + kAuthorIndexOffset);
    anchor.author = author == 0xFFFF ? -1 : static_cast<int>(author);
    anchor.bookmark_tag = U32(plc, atrd + kBookmarkTagOffset);
    out.push_back(std::move(anchor));
  }
  if (U32(plc, 4 * count) <= previous)
    return std::unexpected(Error{"invalid comment anchor PLC CP order"});
  return out;
}

// GrpXstAtnOwners is an array of Xst: cch (2 bytes) followed by cch UTF-16
// characters. There is no count; the array ends with the buffer.
std::expected<std::vector<std::string>, Error> ReadAuthors(Bytes table,
                                                           Fib::Range r) {
  std::vector<std::string> out;
  if (!r.size) {
    return out;
  }
  if (!Valid(table, r))
    return std::unexpected(Error{"invalid comment owners array"});
  Bytes b = table.subspan(r.offset, r.size);
  size_t p = 0;
  while (p + 2 <= b.size()) {
    const size_t cch = U16(b, p);
    p += 2;
    if (cch == 0 || p + cch * 2 > b.size())
      return std::unexpected(Error{"invalid comment owners array"});
    std::string name;
    for (size_t i = 0; i < cch; i++) {
      AppendUtf8(name, U16(b, p + 2 * i));
    }
    p += cch * 2;
    out.push_back(std::move(name));
  }
  // Some producers leave one padding byte after the last complete Xst.
  return out;
}

std::expected<void, Error> ValidateCommentStory(Bytes table, const Fib& fib) {
  auto anchors = ReadAnchors(table, fib.and_ref);
  if (!anchors)
    return std::unexpected(anchors.error());
  if (anchors->empty())
    return {};
  if (!fib.and_txt.size || !Valid(table, fib.and_txt) || fib.and_txt.size % 4 ||
      fib.and_txt.size < 8)
    return std::unexpected(Error{"invalid comment text PLC"});
  Bytes text = table.subspan(fib.and_txt.offset, fib.and_txt.size);
  const size_t count = text.size() / 4;
  if (count < anchors->size() + 1 || count > anchors->size() + 2)
    return std::unexpected(Error{"comment PLC entry count mismatch"});
  for (size_t i = 1; i < count; ++i) {
    if (U32(text, 4 * i) <= U32(text, 4 * (i - 1)))
      return std::unexpected(Error{"invalid comment text PLC CP order"});
  }
  if (U32(text, 4 * (count - 1)) > fib.ccp_atn ||
      anchors->back().cp >= fib.ccp_text)
    return std::unexpected(Error{"invalid comment PLC CP range"});
  auto authors = ReadAuthors(table, fib.atn_owners);
  if (!authors)
    return std::unexpected(authors.error());
  return {};
}

}  // namespace

std::vector<CommentRange> ReadCommentRanges(
    Bytes table, const Fib& fib, std::vector<std::string>* warnings) {
  std::vector<CommentRange> out;
  if (!fib.atn_bkf.size && !fib.atn_bkl.size)
    return out;
  auto invalid = [&]() {
    if (warnings)
      warnings->push_back(
          "skipped comment range PLC: invalid annotation "
          "bookmark range");
    return std::vector<CommentRange>{};
  };
  if (!Valid(table, fib.atn_bkf) || !Valid(table, fib.atn_bkl) ||
      fib.atn_bkf.size < 12 || (fib.atn_bkf.size - 4) % 8 ||
      fib.atn_bkl.size < 8 || fib.atn_bkl.size % 4)
    return invalid();
  auto anchors = ReadAnchors(table, fib.and_ref);
  if (!anchors)
    return invalid();
  if (anchors->empty())
    return out;
  const Bytes starts = table.subspan(fib.atn_bkf.offset, fib.atn_bkf.size);
  const Bytes ends = table.subspan(fib.atn_bkl.offset, fib.atn_bkl.size);
  const size_t count = (starts.size() - 4) / 8;
  if (ends.size() / 4 - 1 != count)
    return invalid();
  for (size_t i = 0; i < count; ++i) {
    if (U32(starts, 4 * i) > U32(starts, 4 * (i + 1)) ||
        U32(ends, 4 * i) > U32(ends, 4 * (i + 1)))
      return invalid();
  }
  std::map<uint32_t, size_t> tag_to_bookmark;
  if (fib.atn_bkmk.size) {
    if (!Valid(table, fib.atn_bkmk) || fib.atn_bkmk.size < 6)
      return invalid();
    const Bytes names = table.subspan(fib.atn_bkmk.offset, fib.atn_bkmk.size);
    const size_t entries = U16(names, 2), extra = U16(names, 4);
    if (entries != count || extra < kAtnbeSize)
      return invalid();
    size_t p = 6;
    for (size_t i = 0; i < entries; ++i) {
      if (p + 2 + extra > names.size())
        return invalid();
      const size_t chars = U16(names, p);
      if (chars != 0)
        return invalid();
      const uint32_t tag = U32(names, p + 2 + 2);
      if (!tag_to_bookmark.emplace(tag, i).second)
        return invalid();
      p += 2 + extra + 2 * chars;
      if (p > names.size())
        return invalid();
    }
  } else if (count != anchors->size()) {
    if (warnings)
      warnings->push_back(
          "comment range bookmarks not paired; falling back to point "
          "anchors");
    return out;
  }
  for (size_t i = 0; i < anchors->size(); ++i) {
    if ((*anchors)[i].bookmark_tag == UINT32_MAX)
      continue;
    size_t index = i;
    if (!tag_to_bookmark.empty()) {
      auto found = tag_to_bookmark.find((*anchors)[i].bookmark_tag);
      if (found == tag_to_bookmark.end())
        continue;
      index = found->second;
    }
    const size_t record = 4 * (count + 1) + 4 * index;
    const size_t end_index = U16(starts, record);
    if (end_index >= count)
      return invalid();
    const uint32_t first = U32(starts, 4 * index);
    const uint32_t last = U32(ends, 4 * end_index);
    if (first > last || last > fib.ccp_text) {
      if (warnings)
        warnings->push_back("skipped invalid comment range: id " +
                            std::to_string(i + 1));
      continue;
    }
    if (first < last)
      out.push_back({first, last, static_cast<int>(i) + 1});
  }
  return out;
}

std::expected<std::vector<uint32_t>, Error> ReadCommentAnchors(
    Bytes table, const Fib& fib, std::vector<std::string>* warnings) {
  if (auto valid = ValidateCommentStory(table, fib); !valid) {
    if (warnings)
      warnings->push_back("skipped comment story PLC: " +
                          valid.error().message);
    return std::vector<uint32_t>{};
  }
  auto anchors = ReadAnchors(table, fib.and_ref);
  if (!anchors) {
    return std::unexpected(anchors.error());
  }
  std::vector<uint32_t> cps;
  cps.reserve(anchors->size());
  for (const auto& anchor : *anchors) {
    cps.push_back(anchor.cp);
  }
  return cps;
}

std::expected<void, Error> ReadComments(model::Document& doc, Bytes word,
                                        Bytes table, const Fib& fib,
                                        const std::vector<FormatRange>& chpx,
                                        const std::vector<FormatRange>& papx,
                                        const BookmarkMarks& bookmarks) {
  if (auto valid = ValidateCommentStory(table, fib); !valid)
    return {};
  auto anchors = ReadAnchors(table, fib.and_ref, &doc.warnings);
  if (!anchors) {
    return std::unexpected(anchors.error());
  }
  if (anchors->empty()) {
    return {};
  }
  if (!fib.and_txt.size || !Valid(table, fib.and_txt) || fib.and_txt.size % 4 ||
      fib.and_txt.size < 8) {
    return std::unexpected(Error{"invalid comment text PLC"});
  }
  Bytes text = table.subspan(fib.and_txt.offset, fib.and_txt.size);
  const size_t text_cps = text.size() / 4;
  // The last entry is a guard CP, so the usable range count is one less; the
  // anchor PLC remains authoritative when the two disagree.
  const size_t count = std::min(anchors->size(), text_cps - 1);
  const std::vector<std::string> authors = *ReadAuthors(table, fib.atn_owners);
  const uint64_t base =
      static_cast<uint64_t>(fib.ccp_text) + fib.ccp_ftn + fib.ccp_hdd;
  if (base + fib.ccp_atn > UINT32_MAX) {
    return std::unexpected(Error{"annotation CP range too large"});
  }
  std::vector<std::vector<uint8_t>> prcs;
  auto chars =
      ReadChars(word, table, fib, static_cast<uint32_t>(base + fib.ccp_atn),
                static_cast<uint32_t>(base), &prcs);
  if (!chars) {
    return std::unexpected(chars.error());
  }
  // PlcfFldAtn CPs are relative to the comment region; a malformed PLC drops
  // only the flags, never the comment bodies.
  std::map<uint32_t, uint8_t> field_status;
  if (auto status =
          ReadStoryFieldStatus(table, fib.fields_atn, base, fib.ccp_atn)) {
    field_status = std::move(*status);
  } else {
    doc.warnings.push_back("skipped comment field PLC: " +
                           status.error().message);
  }
  std::vector<std::pair<uint64_t, uint64_t>> ranges;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t first = U32(text, 4 * i);
    const uint32_t last = U32(text, 4 * (i + 1));
    if (last > first && last <= fib.ccp_atn)
      ranges.emplace_back(base + first, base + last);
  }
  const BookmarkMarks story_bookmarks =
      FilterStoryBookmarks(bookmarks, ranges, "comment", &doc.warnings);
  for (size_t i = 0; i < count; i++) {
    const uint32_t first = U32(text, 4 * i);
    const uint32_t last = U32(text, 4 * (i + 1));
    model::Comment comment;
    if ((*anchors)[i].author >= 0 &&
        static_cast<size_t>((*anchors)[i].author) < authors.size()) {
      comment.author = authors[(*anchors)[i].author];
    } else {
      comment.author = (*anchors)[i].initials;
    }
    comment.initials = (*anchors)[i].initials;
    if (last > first && last <= fib.ccp_atn) {
      comment.paragraphs =
          ReadStoryParagraphs(*chars, chpx, papx, doc.fonts, prcs, base, first,
                              last, &field_status, &story_bookmarks);
    }
    doc.comments.push_back(std::move(comment));
  }
  return {};
}
}  // namespace doccraft::doc::detail
