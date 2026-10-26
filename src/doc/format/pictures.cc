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
                                              uint16_t instance) {
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
                                                             int depth) {
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
      auto image = DecodeBlip(body, type, instance);
      if (!image)
        return std::unexpected(image.error());
      return std::move(*image);
    }
    if (version == detail::kEscherContainerVersion) {
      auto child = ScanEscher(body, depth + 1);
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
        auto child = ScanEscher(body.subspan(prefix), depth + 1);
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
                                                     uint32_t offset) {
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
  auto found = ScanEscher(picf.subspan(record_start, length - record_start), 0);
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
}  // namespace doccraft::doc
