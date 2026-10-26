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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <vector>

#include "doc/format.h"
#include "doc/format/internal.h"
#include "third_party/miniz/miniz.h"

namespace doccraft::doc {
namespace {
using detail::Bytes;
using detail::S16;
using detail::U16;
using detail::U32;

std::expected<model::Image, Error> DecodeBlip(Bytes body, uint16_t type,
                                              uint16_t instance,
                                              size_t budget) {
  model::Image image;
  size_t uid_count = 1;
  switch (type) {
    case detail::kBlipPng:
      image.format = model::ImageFormat::kPng;
      if (instance != detail::kBlipPngOneUid &&
          instance != detail::kBlipPngTwoUids)
        return std::unexpected(Error{"invalid PNG BLIP instance"});
      uid_count = instance == detail::kBlipPngTwoUids ? 2 : 1;
      break;
    case detail::kBlipJpeg:
    case detail::kBlipJpegAlt:
      image.format = model::ImageFormat::kJpeg;
      if (instance != detail::kBlipJpegRgbOneUid &&
          instance != detail::kBlipJpegRgbTwoUids &&
          instance != detail::kBlipJpegCmykOneUid &&
          instance != detail::kBlipJpegCmykTwoUids)
        return std::unexpected(Error{"invalid JPEG BLIP instance"});
      uid_count = (instance == detail::kBlipJpegRgbTwoUids ||
                   instance == detail::kBlipJpegCmykTwoUids)
                      ? 2
                      : 1;
      break;
    case detail::kBlipWmf:
      image.format = model::ImageFormat::kWmf;
      if (instance != detail::kBlipWmfOneUid &&
          instance != detail::kBlipWmfTwoUids)
        return std::unexpected(Error{"invalid WMF BLIP instance"});
      uid_count = instance == detail::kBlipWmfTwoUids ? 2 : 1;
      break;
    case detail::kBlipEmf:
      image.format = model::ImageFormat::kEmf;
      if (instance != detail::kBlipEmfOneUid &&
          instance != detail::kBlipEmfTwoUids)
        return std::unexpected(Error{"invalid EMF BLIP instance"});
      uid_count = instance == detail::kBlipEmfTwoUids ? 2 : 1;
      break;
    default:
      return std::unexpected(Error{"unsupported BLIP type"});
  }
  const size_t uid_bytes = uid_count * detail::kBlipUidSize;
  if (body.size() < uid_bytes)
    return std::unexpected(Error{"truncated BLIP UID"});
  if (image.format == model::ImageFormat::kPng ||
      image.format == model::ImageFormat::kJpeg) {
    const size_t prefix = uid_bytes + detail::kBlipTagSize;
    if (body.size() <= prefix)
      return std::unexpected(Error{"empty raster BLIP"});
    if (body.size() - prefix > detail::kMaxImageSize)
      return std::unexpected(Error{"image size exceeds limit"});
    if (body.size() - prefix > budget)
      return std::unexpected(Error{"total image size exceeds limit"});
    image.bytes.assign(body.begin() + prefix, body.end());
    return image;
  }
  const size_t prefix = uid_bytes + detail::kMetafileHeaderSize;
  if (body.size() < prefix)
    return std::unexpected(Error{"truncated metafile BLIP"});
  Bytes header = body.subspan(uid_bytes, detail::kMetafileHeaderSize);
  const uint32_t original_size = U32(header, 0);
  const uint32_t stored_size = U32(header, 28);
  if (original_size == 0 || original_size > detail::kMaxImageSize ||
      stored_size > body.size() - prefix)
    return std::unexpected(Error{"invalid metafile BLIP size"});
  if (original_size > budget)
    return std::unexpected(Error{"total image size exceeds limit"});
  Bytes payload = body.subspan(prefix, stored_size);
  const uint8_t compression = header[detail::kMetafileCompressionOffset];
  if (compression == detail::kMetafileUncompressed) {
    if (stored_size != original_size)
      return std::unexpected(Error{"metafile size mismatch"});
    image.bytes.assign(payload.begin(), payload.end());
  } else if (compression == detail::kMetafileDeflate) {
    image.bytes.resize(original_size);
    mz_ulong decoded = original_size;
    if (mz_uncompress(image.bytes.data(), &decoded, payload.data(),
                      payload.size()) != MZ_OK ||
        decoded != original_size)
      return std::unexpected(Error{"metafile inflate failed"});
  } else {
    return std::unexpected(Error{"unsupported metafile compression"});
  }
  return image;
}

std::expected<std::optional<model::Image>, Error> ScanEscher(Bytes records,
                                                             int depth,
                                                             size_t budget) {
  if (depth > detail::kEscherMaxDepth)
    return std::unexpected(Error{"Escher nesting limit exceeded"});
  size_t pos = 0;
  while (pos < records.size()) {
    if (records.size() - pos < detail::kEscherHeaderSize)
      return std::unexpected(Error{"truncated Escher record header"});
    const uint16_t header = U16(records, pos);
    const uint16_t version = header & detail::kEscherVersionMask;
    const uint16_t instance = header >> detail::kEscherInstanceShift;
    const uint16_t type = U16(records, pos + 2);
    const uint32_t length = U32(records, pos + 4);
    pos += detail::kEscherHeaderSize;
    if (length > records.size() - pos)
      return std::unexpected(Error{"Escher record exceeds PICF"});
    Bytes body = records.subspan(pos, length);
    if (type == detail::kBlipPng || type == detail::kBlipJpeg ||
        type == detail::kBlipJpegAlt || type == detail::kBlipWmf ||
        type == detail::kBlipEmf) {
      if (version != 0)
        return std::unexpected(Error{"invalid BLIP record version"});
      auto image = DecodeBlip(body, type, instance, budget);
      if (!image)
        return std::unexpected(image.error());
      return std::move(*image);
    }
    if (version == detail::kEscherContainerVersion) {
      auto child = ScanEscher(body, depth + 1, budget);
      if (!child)
        return std::unexpected(child.error());
      if (*child)
        return child;
    } else if (type == detail::kEscherFbse) {
      if (body.size() < detail::kFbseFixedSize)
        return std::unexpected(Error{"truncated Escher FBSE"});
      const size_t prefix =
          detail::kFbseFixedSize + body[detail::kFbseNameLengthOffset];
      if (prefix > body.size())
        return std::unexpected(Error{"invalid FBSE name length"});
      if (prefix < body.size()) {
        auto child = ScanEscher(body.subspan(prefix), depth + 1, budget);
        if (!child)
          return std::unexpected(child.error());
        if (*child)
          return child;
      }
    }
    pos += length;
  }
  return std::nullopt;
}
}  // namespace

std::expected<model::Image, Error> ReadInlinePicture(Bytes data,
                                                     uint32_t offset,
                                                     size_t budget) {
  if (offset > data.size() || data.size() - offset < detail::kPicfHeaderSize)
    return std::unexpected(Error{"PICF outside Data stream"});
  Bytes picf = data.subspan(offset);
  const uint32_t length = U32(picf, 0);
  if (length < detail::kPicfHeaderSize || length > picf.size() ||
      U16(picf, 4) != detail::kPicfHeaderSize)
    return std::unexpected(Error{"invalid PICF header or length"});
  const int goal_width = S16(picf, detail::kPicfGoalOffset);
  const int goal_height = S16(picf, detail::kPicfGoalOffset + 2);
  const int scale_width = U16(picf, detail::kPicfScaleOffset);
  const int scale_height = U16(picf, detail::kPicfScaleOffset + 2);
  if (goal_width <= 0 || goal_height <= 0 || scale_width == 0 ||
      scale_height == 0)
    return std::unexpected(Error{"invalid PICF dimensions"});
  size_t record_start = detail::kPicfHeaderSize;
  if (U16(picf, 6) == detail::kPicfShapeFile) {
    if (length <= record_start)
      return std::unexpected(Error{"missing PICF picture name length"});
    record_start += 1 + picf[record_start];
    if (record_start > length)
      return std::unexpected(Error{"PICF picture name exceeds length"});
  }
  auto found =
      ScanEscher(picf.subspan(record_start, length - record_start), 0, budget);
  if (!found)
    return std::unexpected(found.error());
  if (!*found)
    return std::unexpected(Error{"PICF has no supported embedded BLIP"});
  model::Image image = std::move(**found);
  image.width = std::clamp(goal_width * scale_width / detail::kPicfScaleUnit, 1,
                           static_cast<int>(detail::kMaxTwips));
  image.height = std::clamp(goal_height * scale_height / detail::kPicfScaleUnit,
                            1, static_cast<int>(detail::kMaxTwips));
  return image;
}
namespace {
struct ArtRecord {
  uint16_t type = 0, instance = 0, version = 0;
  Bytes body;
};

std::expected<ArtRecord, Error> ArtNext(Bytes& bytes) {
  if (bytes.size() < detail::kEscherHeaderSize)
    return std::unexpected(Error{"truncated drawing record"});
  const auto size = U32(bytes, 4);
  if (size > bytes.size() - detail::kEscherHeaderSize)
    return std::unexpected(Error{"drawing record exceeds stream"});
  const auto header = U16(bytes, 0);
  ArtRecord record{
      U16(bytes, 2),
      static_cast<uint16_t>(header >> detail::kEscherInstanceShift),
      static_cast<uint16_t>(header & detail::kEscherVersionMask),
      bytes.subspan(detail::kEscherHeaderSize, size)};
  bytes = bytes.subspan(detail::kEscherHeaderSize + size);
  return record;
}

struct DrawingIndex {
  std::vector<Bytes> blips;
  std::map<uint32_t, uint32_t> pictures;
  size_t records = 0;
};

std::expected<void, Error> IndexArt(Bytes bytes, DrawingIndex& index, int depth,
                                    bool bstore = false) {
  if (depth > detail::kEscherMaxDepth)
    return std::unexpected(Error{"Escher nesting limit exceeded"});
  while (!bytes.empty()) {
    if (++index.records > detail::kMaxDrawingRecords)
      return std::unexpected(Error{"drawing record count exceeds limit"});
    auto r = ArtNext(bytes);
    if (!r)
      return std::unexpected(r.error());
    if (bstore && r->type == detail::kEscherFbse) {
      if (r->body.size() < detail::kFbseFixedSize)
        return std::unexpected(Error{"truncated drawing FBSE"});
      index.blips.push_back(r->body);
    } else if (r->type == detail::kSpContainer) {
      uint32_t id = 0, pib = 0;
      Bytes children = r->body;
      while (!children.empty()) {
        if (++index.records > detail::kMaxDrawingRecords)
          return std::unexpected(Error{"drawing record count exceeds limit"});
        auto child = ArtNext(children);
        if (!child)
          return std::unexpected(child.error());
        if (child->type == detail::kFsp) {
          if (child->body.size() != 8)
            return std::unexpected(Error{"invalid drawing FSP"});
          id = U32(child->body, 0);
        } else if (child->type == detail::kFopt ||
                   child->type == detail::kTertiaryFopt) {
          if (child->instance > child->body.size() / detail::kFopteSize)
            return std::unexpected(Error{"truncated drawing properties"});
          for (size_t i = 0; i < child->instance; ++i) {
            const size_t at = i * detail::kFopteSize;
            const uint16_t op = U16(child->body, at);
            if ((op & detail::kPropertyIdMask) == detail::kPib &&
                !(op & detail::kPropertyComplex))
              pib = U32(child->body, at + 2);
          }
        }
      }
      if (pib && !index.pictures.emplace(id, pib).second)
        return std::unexpected(Error{"duplicate drawing shape id"});
    } else if (r->version == detail::kEscherContainerVersion) {
      auto nested = IndexArt(r->body, index, depth + 1,
                             r->type == detail::kBStoreContainer);
      if (!nested)
        return nested;
    }
  }
  return {};
}

std::expected<Bytes, Error> DrawingRange(Bytes table, Fib::Range range) {
  if (range.offset > table.size() || range.size > table.size() - range.offset)
    return std::unexpected(Error{"drawing range outside table stream"});
  return table.subspan(range.offset, range.size);
}
}  // namespace

std::expected<std::vector<FloatingPicture>, Error> ReadFloatingPictures(
    Bytes word, Bytes table, const Fib& fib) {
  std::vector<FloatingPicture> result;
  if (!fib.spa_main.size)
    return result;
  auto spa = DrawingRange(table, fib.spa_main);
  auto art = DrawingRange(table, fib.drawing_group);
  if (!spa)
    return std::unexpected(spa.error());
  if (!art)
    return std::unexpected(art.error());
  if (spa->size() < sizeof(uint32_t) ||
      (spa->size() - sizeof(uint32_t)) % (detail::kSpaSize + sizeof(uint32_t)))
    return std::unexpected(Error{"invalid PlcfSpa size"});
  const size_t count =
      (spa->size() - sizeof(uint32_t)) / (detail::kSpaSize + sizeof(uint32_t));
  if (count > detail::kMaxDrawingRecords)
    return std::unexpected(Error{"drawing anchor count exceeds limit"});
  DrawingIndex index;
  Bytes content = *art;
  auto group = ArtNext(content);
  if (!group)
    return std::unexpected(group.error());
  if (group->type != detail::kDggContainer)
    return std::unexpected(Error{"missing drawing group container"});
  auto indexed = IndexArt(group->body, index, 0);
  if (!indexed)
    return std::unexpected(indexed.error());
  while (!content.empty()) {
    const auto label = content.front();
    content = content.subspan(1);
    auto drawing = ArtNext(content);
    if (!drawing)
      return std::unexpected(drawing.error());
    if (label > 1 || drawing->type != detail::kDgContainer)
      return std::unexpected(Error{"invalid Word drawing container"});
    if (label == 0) {
      indexed = IndexArt(drawing->body, index, 0);
      if (!indexed)
        return std::unexpected(indexed.error());
    }
  }
  size_t budget = detail::kMaxTotalImageSize;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t cp = U32(*spa, i * sizeof(uint32_t));
    if (cp >= fib.ccp_text || cp >= U32(*spa, (i + 1) * sizeof(uint32_t)))
      return std::unexpected(Error{"invalid drawing anchor CP"});
    Bytes entry =
        spa->subspan((count + 1) * sizeof(uint32_t) + i * detail::kSpaSize,
                     detail::kSpaSize);
    const auto picture = index.pictures.find(U32(entry, 0));
    // Non-picture shapes/text boxes are deferred (KNOWN-SIMPLIFICATIONS).
    if (picture == index.pictures.end())
      continue;
    if (picture->second == 0 || picture->second > index.blips.size())
      return std::unexpected(Error{"drawing picture index outside BStore"});
    Bytes fbse = index.blips[picture->second - 1];
    const size_t prefix =
        detail::kFbseFixedSize + fbse[detail::kFbseNameLengthOffset];
    if (prefix > fbse.size())
      return std::unexpected(Error{"invalid FBSE name length"});
    Bytes blip = fbse.subspan(prefix);
    if (blip.empty()) {
      const auto delay = U32(fbse, detail::kFbseDelayOffset);
      if (delay > word.size())
        return std::unexpected(Error{"delayed BLIP outside WordDocument"});
      blip = word.subspan(delay);
    }
    auto record = ArtNext(blip);
    if (!record)
      return std::unexpected(record.error());
    if (record->version != 0)
      return std::unexpected(Error{"invalid BLIP record version"});
    auto image =
        DecodeBlip(record->body, record->type, record->instance, budget);
    if (!image)
      return std::unexpected(image.error());
    budget -= image->bytes.size();
    const int64_t left = base::S32(entry, 4), top = base::S32(entry, 8);
    const int64_t width = int64_t{base::S32(entry, 12)} - left;
    const int64_t height = int64_t{base::S32(entry, 16)} - top;
    if (width <= 0 || height <= 0 || width > detail::kMaxTwips ||
        height > detail::kMaxTwips)
      return std::unexpected(Error{"invalid floating picture dimensions"});
    image->width = static_cast<int>(width);
    image->height = static_cast<int>(height);
    const auto flags = U16(entry, detail::kSpaFlagsOffset);
    model::ImageAnchor anchor;
    anchor.x = static_cast<int>(left);
    anchor.y = static_cast<int>(top);
    anchor.horizontal =
        (flags >> detail::kSpaHorizontalShift) & detail::kSpaOriginMask;
    anchor.vertical =
        (flags >> detail::kSpaVerticalShift) & detail::kSpaOriginMask;
    const auto wrap = (flags >> detail::kSpaWrapShift) & detail::kSpaWrapMask;
    anchor.wrap_side = (flags >> detail::kSpaSideShift) & detail::kSpaWrapMask;
    if (anchor.horizontal > 2 || anchor.vertical > 2 ||
        wrap > detail::kWrapThrough)
      return std::unexpected(Error{"invalid floating picture anchor"});
    if (wrap == detail::kWrapNone || wrap == detail::kWrapTopBottom)
      anchor.wrap_side = 0;
    if (anchor.wrap_side > 3)
      return std::unexpected(Error{"invalid floating picture wrap side"});
    switch (wrap) {
      case detail::kWrapNone:
        anchor.wrap = model::ImageWrap::kNone;
        break;
      case detail::kWrapTopBottom:
        anchor.wrap = model::ImageWrap::kTopAndBottom;
        break;
      case detail::kWrapTight:
        anchor.wrap = model::ImageWrap::kTight;
        break;
      case detail::kWrapThrough:
        anchor.wrap = model::ImageWrap::kThrough;
        break;
      default:
        anchor.wrap = model::ImageWrap::kSquare;
    }
    anchor.behind_text =
        wrap == detail::kWrapNone && (flags & detail::kSpaBehind);
    anchor.locked = flags & detail::kSpaLocked;
    image->anchor = anchor;
    result.push_back({cp, std::move(*image)});
  }
  return result;
}

}  // namespace doccraft::doc
