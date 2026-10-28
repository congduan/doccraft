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
#include <string>
#include <vector>

#include "base/utf8.h"
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

std::expected<model::Image, Error> ReadLegacyPicture(Bytes word,
                                                     uint32_t offset,
                                                     size_t budget) {
  // Word 6/95 stores PICF in WordDocument. Its header omits the Word 97
  // cProps word, and the payload is a direct metafile or bitmap.
  constexpr size_t kLegacyPicfHeaderSize = 66;
  constexpr uint16_t kLegacyBitmapType = 99;
  constexpr uint16_t kWmfAnisotropicType = 8;
  if (offset > word.size() || word.size() - offset < kLegacyPicfHeaderSize)
    return std::unexpected(Error{"Word 6/95 PICF outside WordDocument"});
  Bytes picf = word.subspan(offset);
  const size_t length = U32(picf, 0);
  const size_t header = U16(picf, 4);
  if (header < kLegacyPicfHeaderSize || length <= header ||
      length > picf.size())
    return std::unexpected(Error{"invalid Word 6/95 PICF length"});
  const uint16_t type = U16(picf, 6);
  if (type == kLegacyBitmapType)
    return std::unexpected(Error{"unsupported Word 6/95 bitmap PICF"});
  if (type != kWmfAnisotropicType)
    return std::unexpected(Error{"unsupported Word 6/95 PICF MFPF type"});
  const int goal_width = S16(picf, detail::kPicfGoalOffset);
  const int goal_height = S16(picf, detail::kPicfGoalOffset + 2);
  const int scale_width = U16(picf, detail::kPicfScaleOffset);
  const int scale_height = U16(picf, detail::kPicfScaleOffset + 2);
  if (goal_width <= 0 || goal_height <= 0 || !scale_width || !scale_height)
    return std::unexpected(Error{"invalid Word 6/95 PICF dimensions"});
  Bytes payload = picf.subspan(header, length - header);
  if (payload.size() > detail::kMaxImageSize || payload.size() > budget)
    return std::unexpected(Error{"Word 6/95 image size exceeds limit"});
  if (payload.size() < detail::kWmfHeaderBytes ||
      (U16(payload, 0) != 1 && U16(payload, 0) != 2) ||
      U16(payload, 2) != detail::kWmfHeaderWords ||
      (U16(payload, 4) != detail::kWmfVersion1 &&
       U16(payload, 4) != detail::kWmfVersion3) ||
      static_cast<uint64_t>(U32(payload, 6)) * 2 != payload.size())
    return std::unexpected(Error{"invalid Word 6/95 WMF payload"});
  model::Image image;
  image.format = model::ImageFormat::kWmf;
  image.width = std::clamp(goal_width * scale_width / detail::kPicfScaleUnit, 1,
                           static_cast<int>(detail::kMaxTwips));
  image.height = std::clamp(goal_height * scale_height / detail::kPicfScaleUnit,
                            1, static_cast<int>(detail::kMaxTwips));
  image.bytes.assign(payload.begin(), payload.end());
  return image;
}

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
// Turns a CF_DIB presentation payload into a stand-alone .bmp: a
// BITMAPFILEHEADER followed by the untouched BITMAPINFOHEADER block. The
// palette (bit counts up to 8) and the optional BI_BITFIELDS masks stay in
// place, so only the pixel-data offset has to be computed.
std::expected<std::vector<uint8_t>, Error> PackDib(Bytes dib) {
  if (dib.size() < detail::kBitmapInfoHeaderSize)
    return std::unexpected(Error{"truncated OLE DIB preview"});
  const uint32_t header = U32(dib, detail::kBitmapHeaderSizeOffset);
  const uint16_t planes = U16(dib, detail::kBitmapPlanesOffset);
  const uint16_t bits = U16(dib, detail::kBitmapBitCountOffset);
  const uint32_t compression = U32(dib, detail::kBitmapCompressionOffset);
  const uint32_t colors = U32(dib, detail::kBitmapColorsUsedOffset);
  if (header < detail::kBitmapInfoHeaderSize || header > dib.size() ||
      planes != detail::kBitmapPlanes ||
      (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 &&
       bits != 32) ||
      (compression != detail::kBitmapRgb &&
       compression != detail::kBitmapBitfields))
    return std::unexpected(Error{"unsupported OLE DIB preview format"});
  uint64_t offset = detail::kBitmapFileHeaderSize + header;
  if (compression == detail::kBitmapBitfields)
    offset += detail::kBitmapMasksSize;
  if (bits <= detail::kBitmapPaletteMaxBits) {
    const uint64_t entries = colors ? colors : uint64_t{1} << bits;
    offset += entries * 4;
  }
  if (offset > detail::kBitmapFileHeaderSize + dib.size())
    return std::unexpected(Error{"OLE DIB palette exceeds payload"});
  std::vector<uint8_t> bmp(detail::kBitmapFileHeaderSize, 0);
  const auto put_le = [&bmp](size_t at, uint32_t value, size_t width) {
    for (size_t i = 0; i < width; i++)
      bmp[at + i] = static_cast<uint8_t>(value >> (8 * i));
  };
  bmp[0] = 'B';
  bmp[1] = 'M';
  // The payload was already bounded by kMaxImageSize, so the 32-bit file size
  // and offset fields cannot overflow here.
  put_le(2, static_cast<uint32_t>(detail::kBitmapFileHeaderSize + dib.size()),
         4);
  put_le(10, static_cast<uint32_t>(offset), 4);
  bmp.insert(bmp.end(), dib.begin(), dib.end());
  return bmp;
}
}  // namespace

std::expected<std::optional<model::Image>, Error> ReadOlePresentation(
    Bytes stream, size_t budget) {
  if (stream.size() < detail::kOlePresentationPrefix)
    return std::unexpected(Error{"truncated OLE presentation"});
  const uint32_t marker = U32(stream, 0);
  if (marker != detail::kOleClipboardMarker &&
      marker != detail::kOleClipboardMarkerAlt)
    return std::nullopt;
  const uint32_t format = U32(stream, 4);
  if (format != detail::kOleClipboardWmf &&
      format != detail::kOleClipboardEmf && format != detail::kOleClipboardDib)
    return std::nullopt;
  const uint32_t target_size = U32(stream, 8);
  if (target_size < 4 || target_size > stream.size() - 8 ||
      stream.size() - 8 - target_size < detail::kOlePresentationPrefix - 12)
    return std::unexpected(Error{"invalid OLE target device size"});
  const size_t fields = 8 + target_size;
  const int32_t width = base::S32(stream, fields + 16);
  const int32_t height = base::S32(stream, fields + 20);
  const uint32_t size = U32(stream, fields + 24);
  const size_t start = fields + 28;
  if (width <= 0 || height <= 0 || size == 0 || size > detail::kMaxImageSize ||
      size > stream.size() - start)
    return std::unexpected(
        Error{"invalid OLE presentation dimensions or size"});
  if (size > budget)
    return std::unexpected(Error{"total image size exceeds limit"});
  Bytes payload = stream.subspan(start, size);
  if (format == detail::kOleClipboardEmf) {
    if (payload.size() < detail::kEmfHeaderBytes ||
        U32(payload, 0) != detail::kEmfHeaderType ||
        U32(payload, 4) < detail::kEmfHeaderBytes ||
        U32(payload, 4) > payload.size() ||
        U32(payload, detail::kEmfSignatureOffset) != detail::kEmfSignature)
      return std::unexpected(Error{"invalid OLE EMF preview"});
  } else if (format == detail::kOleClipboardWmf) {
    if (payload.size() < detail::kWmfHeaderBytes ||
        U16(payload, 2) != detail::kWmfHeaderWords ||
        (U16(payload, 4) != detail::kWmfVersion1 &&
         U16(payload, 4) != detail::kWmfVersion3) ||
        stream.size() - start - size < detail::kOleWmfTrailer)
      return std::unexpected(Error{"invalid OLE WMF preview"});
  }
  const int64_t twip_width =
      int64_t{width} * detail::kTwipsPerInch / detail::kOleHimetricPerInch;
  const int64_t twip_height =
      int64_t{height} * detail::kTwipsPerInch / detail::kOleHimetricPerInch;
  if (twip_width <= 0 || twip_height <= 0 || twip_width > detail::kMaxTwips ||
      twip_height > detail::kMaxTwips)
    return std::unexpected(Error{"OLE preview dimensions exceed limit"});
  model::Image image;
  image.width = static_cast<int>(twip_width);
  image.height = static_cast<int>(twip_height);
  if (format == detail::kOleClipboardDib) {
    auto bmp = PackDib(payload);
    if (!bmp)
      return std::unexpected(bmp.error());
    image.format = model::ImageFormat::kBmp;
    image.bytes = std::move(*bmp);
    return image;
  }
  image.format = format == detail::kOleClipboardEmf ? model::ImageFormat::kEmf
                                                    : model::ImageFormat::kWmf;
  image.bytes.assign(payload.begin(), payload.end());
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

// OfficeArtCOLORREF: red, green and blue followed by a flag byte. A set flag
// byte selects a palette, system or scheme color instead of a literal RGB
// value; those are not resolved and leave the colour unset.
bool PlainColor(uint32_t value, int& color) {
  if (value & detail::kColorFlagMask)
    return false;
  color = static_cast<int>(value);
  return true;
}

struct DrawingIndex {
  std::vector<Bytes> blips;
  std::map<uint32_t, uint32_t> pictures;
  struct ShapeInfo {
    uint16_t type = 0;
    std::string text;
    uint32_t textbox_id = 0;
    // OfficeArt fill and line, resolved from the shape's FOPT records.
    int fill_color = -1, line_color = -1;
    int line_width = 0;
    bool solid_fill = false;
  };
  std::map<uint32_t, ShapeInfo> shapes;
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
      uint32_t id = 0, pib = 0, textbox_id = 0;
      uint16_t shape_type = 0;
      std::string shape_text;
      int fill_color = -1, line_color = -1, line_width = 0;
      uint32_t fill_type = 0;
      bool fill_type_set = false;
      Bytes children = r->body;
      while (!children.empty()) {
        if (++index.records > detail::kMaxDrawingRecords)
          return std::unexpected(Error{"drawing record count exceeds limit"});
        auto child = ArtNext(children);
        if (!child)
          return std::unexpected(child.error());
        if (child->type == detail::kFsp) {
          if (child->body.size() != 8 || child->version != 2)
            return std::unexpected(Error{"invalid drawing FSP"});
          id = U32(child->body, 0);
          shape_type = child->instance;
        } else if (child->type == detail::kClientTextbox) {
          // [MS-DOC] 2.9.170: high word is the 1-based FTXBXS index;
          // low word is the zero-based position in the linked chain.
          if (child->body.size() != sizeof(uint32_t) || child->version != 0)
            return std::unexpected(Error{"invalid drawing client textbox"});
          textbox_id = U32(child->body, 0);
        } else if (child->type == detail::kFopt ||
                   child->type == detail::kTertiaryFopt) {
          if (child->instance > child->body.size() / detail::kFopteSize)
            return std::unexpected(Error{"truncated drawing properties"});
          size_t complex_at = child->instance * detail::kFopteSize;
          for (size_t i = 0; i < child->instance; ++i) {
            const size_t at = i * detail::kFopteSize;
            const uint16_t op = U16(child->body, at);
            const uint16_t property = op & detail::kPropertyIdMask;
            if (!(op & detail::kPropertyComplex)) {
              const uint32_t value = U32(child->body, at + 2);
              if (property == detail::kPib) {
                pib = value;
              } else if (property == detail::kFillColor) {
                PlainColor(value, fill_color);
              } else if (property == detail::kFillType) {
                fill_type = value;
                fill_type_set = true;
              } else if (property == detail::kLineColor) {
                PlainColor(value, line_color);
              } else if (property == detail::kLineWidth) {
                line_width =
                    static_cast<int>(std::min(value, detail::kMaxLineWidthEmu));
              }
            }
            if (op & detail::kPropertyComplex) {
              const uint32_t length = U32(child->body, at + 2);
              if (length > child->body.size() - complex_at)
                return std::unexpected(
                    Error{"drawing complex property exceeds record"});
              if ((op & detail::kPropertyIdMask) == detail::kGtextUnicode) {
                if (length % 2)
                  return std::unexpected(Error{"invalid WordArt UTF-16"});
                Bytes text = child->body.subspan(complex_at, length);
                for (size_t j = 0; j < text.size(); j += 2) {
                  const uint16_t code = U16(text, j);
                  if (code == 0)
                    break;
                  if (code >= 0xD800 && code <= 0xDBFF) {
                    if (j + 3 >= text.size())
                      return std::unexpected(Error{"invalid WordArt UTF-16"});
                    const uint16_t low = U16(text, j + 2);
                    if (low < 0xDC00 || low > 0xDFFF)
                      return std::unexpected(Error{"invalid WordArt UTF-16"});
                    base::AppendUtf8(
                        shape_text,
                        0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00));
                    j += 2;
                  } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    return std::unexpected(Error{"invalid WordArt UTF-16"});
                  } else {
                    base::AppendUtf8(shape_text, code);
                  }
                }
              }
              complex_at += length;
            }
          }
        }
      }
      if (pib && !index.pictures.emplace(id, pib).second)
        return std::unexpected(Error{"duplicate drawing shape id"});
      DrawingIndex::ShapeInfo info;
      info.type = shape_type;
      info.text = std::move(shape_text);
      info.textbox_id = textbox_id;
      // A colour without an explicit fill type is a solid fill; patterned,
      // gradient and picture fills are not mapped and leave the shape unfilled.
      info.solid_fill = fill_color >= 0 &&
                        (!fill_type_set || fill_type == detail::kFillTypeSolid);
      info.fill_color = info.solid_fill ? fill_color : -1;
      info.line_color = line_color;
      info.line_width = line_width;
      if (id && !index.shapes.emplace(id, std::move(info)).second)
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

std::expected<model::Shape, Error> ReadInlineShape(Bytes data,
                                                   uint32_t offset) {
  if (offset > data.size() || data.size() - offset < detail::kPicfHeaderSize)
    return std::unexpected(Error{"PICF outside Data stream"});
  Bytes picf = data.subspan(offset);
  const size_t length = U32(picf, 0);
  if (length <= detail::kPicfHeaderSize || length > picf.size() ||
      U16(picf, 4) != detail::kPicfHeaderSize)
    return std::unexpected(Error{"invalid PICF header or length"});
  size_t start = detail::kPicfHeaderSize;
  if (U16(picf, 6) == detail::kPicfShapeFile) {
    if (length <= start)
      return std::unexpected(Error{"missing PICF picture name length"});
    start += 1 + picf[start];
    if (start > length)
      return std::unexpected(Error{"PICF picture name exceeds length"});
  }
  DrawingIndex index;
  auto parsed = IndexArt(picf.subspan(start, length - start), index, 0);
  if (!parsed)
    return std::unexpected(parsed.error());
  if (index.shapes.size() != 1 || !index.pictures.empty())
    return std::unexpected(Error{"PICF has no supported inline shape"});
  const int goal_width = S16(picf, detail::kPicfGoalOffset);
  const int goal_height = S16(picf, detail::kPicfGoalOffset + 2);
  const int scale_width = U16(picf, detail::kPicfScaleOffset);
  const int scale_height = U16(picf, detail::kPicfScaleOffset + 2);
  if (goal_width <= 0 || goal_height <= 0 || !scale_width || !scale_height)
    return std::unexpected(Error{"invalid PICF dimensions"});
  model::Shape shape;
  shape.id = index.shapes.begin()->first;
  shape.textbox_id = index.shapes.begin()->second.textbox_id;
  shape.type = index.shapes.begin()->second.type;
  shape.text = index.shapes.begin()->second.text;
  shape.fill_color = index.shapes.begin()->second.fill_color;
  shape.line_color = index.shapes.begin()->second.line_color;
  shape.line_width = index.shapes.begin()->second.line_width;
  shape.width = std::clamp(goal_width * scale_width / detail::kPicfScaleUnit, 1,
                           static_cast<int>(detail::kMaxTwips));
  shape.height = std::clamp(goal_height * scale_height / detail::kPicfScaleUnit,
                            1, static_cast<int>(detail::kMaxTwips));
  shape.inline_shape = true;
  return shape;
}

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
    const uint32_t id = U32(entry, 0);
    const auto picture = index.pictures.find(id);
    const auto shape = index.shapes.find(id);
    if (picture == index.pictures.end() && shape == index.shapes.end())
      continue;
    FloatingPicture item;
    item.cp = cp;
    if (picture != index.pictures.end()) {
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
      item.image = std::move(*image);
    }
    const int64_t left = base::S32(entry, 4), top = base::S32(entry, 8);
    const int64_t width = int64_t{base::S32(entry, 12)} - left;
    const int64_t height = int64_t{base::S32(entry, 16)} - top;
    if (width <= 0 || height <= 0 || width > detail::kMaxTwips ||
        height > detail::kMaxTwips)
      return std::unexpected(Error{"invalid floating picture dimensions"});
    item.image.width = static_cast<int>(width);
    item.image.height = static_cast<int>(height);
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
    if (picture != index.pictures.end()) {
      item.image.anchor = anchor;
    } else {
      model::Shape shape_record;
      shape_record.id = id;
      shape_record.type = shape->second.type;
      shape_record.width = static_cast<int>(width);
      shape_record.height = static_cast<int>(height);
      shape_record.anchor = anchor;
      shape_record.text = shape->second.text;
      shape_record.textbox_id = shape->second.textbox_id;
      shape_record.fill_color = shape->second.fill_color;
      shape_record.line_color = shape->second.line_color;
      shape_record.line_width = shape->second.line_width;
      item.shape = std::move(shape_record);
    }
    result.push_back(std::move(item));
  }
  return result;
}

}  // namespace doccraft::doc
