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
#include <utility>
#include <vector>

#include "doc/format/internal.h"

namespace doccraft::doc::detail {
namespace {
struct Plc {
  Bytes bytes;
  size_t count = 0;
  size_t stride = 0;
  uint32_t limit = 0;

  uint32_t Cp(size_t i) const {
    return U32(bytes, i * sizeof(uint32_t));
  }
  Bytes Item(size_t i) const {
    return bytes.subspan((count + 1) * sizeof(uint32_t) + i * stride, stride);
  }
};

std::expected<Plc, Error> ReadPlc(Bytes table, Fib::Range range, size_t stride,
                                  uint32_t limit) {
  if (!Valid(table, range) || range.size < sizeof(uint32_t) + stride ||
      (range.size - sizeof(uint32_t)) % (sizeof(uint32_t) + stride))
    return std::unexpected(Error{"invalid textbox PLC size"});
  const size_t count =
      (range.size - sizeof(uint32_t)) / (sizeof(uint32_t) + stride);
  if (count > kMaxDrawingRecords)
    return std::unexpected(Error{"textbox PLC count exceeds limit"});
  Plc plc{table.subspan(range.offset, range.size), count, stride, limit};
  // The last FTXBXS/Tbkd is reusable. Some Word writers leave its final
  // guard CP outside ccpTxbx; no text is read from that range.
  if (plc.Cp(0) != 0 || plc.Cp(count - 1) > limit)
    return std::unexpected(Error{"textbox PLC CP outside story"});
  for (size_t i = 0; i < count; i++) {
    if (plc.Cp(i + 1) <= plc.Cp(i))
      return std::unexpected(Error{"invalid textbox PLC CP order"});
  }
  return plc;
}
}  // namespace

std::expected<std::vector<model::Textbox>, Error> ReadTextboxesFor(
    Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    const std::vector<std::string>& fonts, bool header,
    std::vector<std::string>* warnings) {
  std::vector<model::Textbox> boxes;
  const uint32_t count_chars = header ? fib.ccp_hdr_txbx : fib.ccp_txbx;
  const Fib::Range txt_range = header ? fib.hdr_txbx_txt : fib.txbx_txt;
  const Fib::Range bkd_range = header ? fib.hdr_txbx_bkd : fib.txbx_bkd;
  const Fib::Range spa_range = header ? fib.spa_hdr : fib.spa_main;
  const Fib::Range field_range = header ? fib.fields_hdr_txbx : fib.fields_txbx;
  if (!count_chars) {
    if (txt_range.size || bkd_range.size)
      return std::unexpected(Error{"textbox PLC without textbox story"});
    return boxes;
  }
  if (!txt_range.size || !bkd_range.size)
    return std::unexpected(Error{"missing textbox PLC"});
  auto txt = ReadPlc(table, txt_range, kFtxbxsSize, count_chars);
  if (!txt)
    return std::unexpected(txt.error());
  auto bkd = ReadPlc(table, bkd_range, kTbkdSize, count_chars);
  if (!bkd)
    return std::unexpected(bkd.error());
  const uint64_t base = static_cast<uint64_t>(fib.ccp_text) + fib.ccp_ftn +
                        fib.ccp_hdd + fib.ccp_atn + fib.ccp_edn +
                        (header ? fib.ccp_txbx : 0);
  if (base + count_chars > UINT32_MAX)
    return std::unexpected(Error{"textbox story CP range too large"});
  std::vector<std::vector<uint8_t>> prcs;
  auto chars =
      ReadChars(word, table, fib, static_cast<uint32_t>(base + count_chars),
                static_cast<uint32_t>(base), &prcs);
  if (!chars)
    return std::unexpected(chars.error());
  // The final FTXBXS and Tbkd entries are reusable guards. A linked chain
  // uses successive Tbkd ranges with the same FTXBXS index.
  std::map<size_t, size_t> by_ftxbxs;
  for (size_t i = 0; i + 1 < txt->count; i++) {
    const Bytes item = txt->Item(i);
    if (U16(item, 8) & 1)
      continue;
    if (txt->Cp(i + 1) - txt->Cp(i) <= 1)
      return std::unexpected(Error{"invalid textbox text range"});
    const uint32_t shape = U32(item, kFtxbxsLidOffset);
    if (!shape)
      return std::unexpected(Error{"invalid textbox shape id"});
    if (!by_ftxbxs.emplace(i, boxes.size()).second)
      return std::unexpected(Error{"duplicate textbox index"});
    model::Textbox box;
    box.shape_id = shape;
    boxes.push_back(std::move(box));
  }
  // Validate the textbox story PLC before reading its independent field PLC.
  std::map<uint32_t, uint8_t> field_status;
  if (auto status =
          ReadStoryFieldStatus(table, field_range, base, count_chars)) {
    field_status = std::move(*status);
  } else {
    warnings->push_back(std::string("skipped ") +
                        (header ? "header textbox" : "textbox") +
                        " field PLC: " + status.error().message);
  }
  for (size_t i = 0; i + 1 < bkd->count; i++) {
    const int index = S16(bkd->Item(i), 0);
    if (index < 0 || static_cast<size_t>(index) + 1 >= txt->count)
      return std::unexpected(Error{"textbox break index outside FTXBXS"});
    const size_t f = static_cast<size_t>(index);
    if (!by_ftxbxs.contains(f))
      continue;  // Reusable FTXBXS has no visible story.
    if (bkd->Cp(i) < txt->Cp(f) || bkd->Cp(i + 1) > txt->Cp(f + 1))
      return std::unexpected(Error{"textbox break outside text range"});
    // Only the final range has the paragraph guard. Interior flow breaks can
    // split a run, so include their final character in the first component.
    const uint32_t end =
        bkd->Cp(i + 1) == txt->Cp(f + 1) ? bkd->Cp(i + 1) : bkd->Cp(i + 1) + 1;
    auto paragraphs = ReadStoryParagraphs(*chars, chpx, papx, fonts, prcs, base,
                                          bkd->Cp(i), end, &field_status);
    auto& target = boxes[by_ftxbxs.at(f)].paragraphs;
    for (auto& paragraph : paragraphs)
      target.push_back(std::move(paragraph));
  }
  if (!spa_range.size)
    return std::unexpected(Error{"textbox without shape anchors"});
  if (!Valid(table, spa_range) || spa_range.size < sizeof(uint32_t) ||
      (spa_range.size - sizeof(uint32_t)) % (kSpaSize + sizeof(uint32_t)))
    return std::unexpected(Error{"invalid textbox shape anchor PLC"});
  Bytes spa = table.subspan(spa_range.offset, spa_range.size);
  const size_t count =
      (spa.size() - sizeof(uint32_t)) / (kSpaSize + sizeof(uint32_t));
  if (count > kMaxDrawingRecords)
    return std::unexpected(Error{"textbox anchor count exceeds limit"});
  for (auto& box : boxes) {
    bool found = false;
    for (size_t i = 0; i < count; i++) {
      const Bytes entry =
          spa.subspan((count + 1) * sizeof(uint32_t) + i * kSpaSize, kSpaSize);
      if (U32(entry, 0) != box.shape_id)
        continue;
      if (found)
        return std::unexpected(Error{"duplicate textbox shape anchor"});
      const int64_t x = S32(entry, 4), y = S32(entry, 8);
      const int64_t width = int64_t{S32(entry, 12)} - x;
      const int64_t height = int64_t{S32(entry, 16)} - y;
      if (width <= 0 || width > kMaxTwips || height <= 0 ||
          height > kMaxTwips || x < -kMaxTwips || x > kMaxTwips ||
          y < -kMaxTwips || y > kMaxTwips)
        return std::unexpected(Error{"invalid textbox shape dimensions"});
      box.anchor_cp = U32(spa, i * sizeof(uint32_t));
      if (box.anchor_cp >= (header ? fib.ccp_hdd : fib.ccp_text))
        return std::unexpected(Error{"textbox anchor outside story"});
      box.x = static_cast<int>(x);
      box.y = static_cast<int>(y);
      box.width = static_cast<int>(width);
      box.height = static_cast<int>(height);
      found = true;
    }
    if (!found)
      return std::unexpected(Error{"textbox shape anchor not found"});
  }
  return boxes;
}

std::expected<std::vector<model::Textbox>, Error> ReadTextboxes(
    Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    const std::vector<std::string>& fonts, std::vector<std::string>* warnings) {
  return ReadTextboxesFor(word, table, fib, chpx, papx, fonts, false, warnings);
}

std::expected<std::vector<model::Textbox>, Error> ReadHeaderTextboxes(
    Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    const std::vector<std::string>& fonts, std::vector<std::string>* warnings) {
  return ReadTextboxesFor(word, table, fib, chpx, papx, fonts, true, warnings);
}
}  // namespace doccraft::doc::detail
