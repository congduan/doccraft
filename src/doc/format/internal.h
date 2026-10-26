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

#ifndef SRC_DOC_FORMAT_INTERNAL_H_
#define SRC_DOC_FORMAT_INTERNAL_H_
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/binary.h"
#include "base/utf8.h"
#include "doc/format.h"

namespace doccraft::doc::detail {

// Byte readers and UTF-8 encoding from the shared base layer; [MS-DOC] is
// little-endian and its text plane is UTF-16LE.
using base::AppendUtf8;
using base::Bytes;
using base::S16;
using base::S32;
using base::U16;
using base::U32;

// [MS-DOC] §2.9.1: frame geometry is expressed in twips and capped at 22 inch,
// the largest value Word writes for a page object.
inline constexpr uint16_t kMaxTwips = 31680;

// [MS-DOC] PICF/PICMID and [MS-ODRAW] OfficeArt record layout.
inline constexpr size_t kPicfHeaderSize = 68;
inline constexpr uint16_t kPicfShapeFile = 0x0066;
inline constexpr size_t kPicfGoalOffset = 28;
inline constexpr size_t kPicfScaleOffset = 32;
inline constexpr int kPicfScaleUnit = 1000;
inline constexpr size_t kEscherHeaderSize = 8;
inline constexpr uint16_t kEscherContainerVersion = 15;
inline constexpr uint16_t kEscherVersionMask = 0x000F;
inline constexpr int kEscherInstanceShift = 4;
inline constexpr int kEscherMaxDepth = 8;
inline constexpr uint16_t kEscherFbse = 0xF007;
inline constexpr size_t kFbseFixedSize = 36;
inline constexpr size_t kFbseNameLengthOffset = 33;
inline constexpr uint16_t kBlipEmf = 0xF01A;
inline constexpr uint16_t kBlipWmf = 0xF01B;
inline constexpr uint16_t kBlipJpeg = 0xF01D;
inline constexpr uint16_t kBlipPng = 0xF01E;
inline constexpr uint16_t kBlipJpegAlt = 0xF02A;
inline constexpr uint16_t kBlipPngOneUid = 0x6E0;
inline constexpr uint16_t kBlipPngTwoUids = 0x6E1;
inline constexpr uint16_t kBlipJpegRgbOneUid = 0x46A;
inline constexpr uint16_t kBlipJpegRgbTwoUids = 0x46B;
inline constexpr uint16_t kBlipJpegCmykOneUid = 0x6E2;
inline constexpr uint16_t kBlipJpegCmykTwoUids = 0x6E3;
inline constexpr uint16_t kBlipWmfOneUid = 0x216;
inline constexpr uint16_t kBlipWmfTwoUids = 0x217;
inline constexpr uint16_t kBlipEmfOneUid = 0x3D4;
inline constexpr uint16_t kBlipEmfTwoUids = 0x3D5;
inline constexpr size_t kBlipUidSize = 16;
inline constexpr size_t kBlipTagSize = 1;
inline constexpr size_t kMetafileHeaderSize = 34;
inline constexpr size_t kMetafileCompressionOffset = 32;
inline constexpr uint8_t kMetafileDeflate = 0;
inline constexpr uint8_t kMetafileUncompressed = 0xFE;
inline constexpr size_t kMaxImageSize = 64 * 1024 * 1024;
// Aggregate cap across all images: metafile deflate can amplify ~1000x per
// BLIP, so per-image limits alone do not bound total memory.
inline constexpr size_t kMaxTotalImageSize = 256 * 1024 * 1024;

// [MS-DOC] Spa / [MS-ODRAW] drawing records and property identifiers.
inline constexpr size_t kSpaSize = 26, kSpaFlagsOffset = 20;
inline constexpr int kSpaHorizontalShift = 1, kSpaVerticalShift = 3;
inline constexpr int kSpaWrapShift = 5, kSpaSideShift = 9;
inline constexpr uint16_t kSpaOriginMask = 3, kSpaWrapMask = 15;
inline constexpr uint16_t kSpaBehind = 0x4000, kSpaLocked = 0x8000;
inline constexpr size_t kMaxDrawingRecords = 65536;
inline constexpr uint16_t kDggContainer = 0xF000, kBStoreContainer = 0xF001;
inline constexpr uint16_t kDgContainer = 0xF002, kSpContainer = 0xF004;
inline constexpr uint16_t kFsp = 0xF00A, kFopt = 0xF00B;
inline constexpr uint16_t kTertiaryFopt = 0xF122;
inline constexpr size_t kFopteSize = 6, kFbseDelayOffset = 28;
inline constexpr uint16_t kPropertyIdMask = 0x3FFF;
inline constexpr uint16_t kPropertyComplex = 0x8000, kPib = 0x0104;
inline constexpr int kWrapTopBottom = 1, kWrapNone = 3;
inline constexpr int kWrapTight = 4, kWrapThrough = 5;

// Operands that mean "absent"/"automatic" carry a saturated value instead of a
// separate flag.
inline constexpr uint16_t kUndefinedU16 = 0xFFFF;
inline constexpr uint32_t kUndefinedU32 = 0xFFFFFFFF;

// sprmPWHeightAbs: bit 15 marks an exact height, bits 0-14 hold the height.
inline constexpr uint16_t kFrameHeightExact = 0x8000;
inline constexpr uint16_t kFrameHeightMask = 0x7FFF;

// [MS-DOC] §2.2.5.1 sprm layout: spra in bits 13-15, sgc in bits 10-12.
inline constexpr int kSprmSgcShift = 10;
inline constexpr uint16_t kSprmSgcMask = 7;
inline constexpr unsigned kSgcParagraph = 1;
inline constexpr unsigned kSgcCharacter = 2;
inline constexpr unsigned kSgcTable = 5;

// sprmPDcs (DCS): fdct in bits 0-2, cl (drop cap lines) in bits 3-7.
inline constexpr uint16_t kDcsTypeMask = 7;
inline constexpr int kDcsLinesShift = 3;
inline constexpr uint16_t kDcsLinesMask = 31;

// sprmPWAlignFont values (0 top .. 4 auto).
inline constexpr uint16_t kMaxTextAlignment = 4;

// sprmPFrameTextFlow (FrameTextFlowOperand): fVertical(1), fBackwards(2).
inline constexpr uint16_t kFrameFlowVertical = 0x0001;
inline constexpr uint16_t kFrameFlowBackwards = 0x0002;

// sprmSPgbProp (SPgbPropOperand): pgbApplyTo(3), pgbPageDepth(2),
// pgbOffsetFrom(3), then a reserved byte that MUST be 0.
inline constexpr size_t kPgbPropSize = 2;
inline constexpr uint8_t kPgbApplyToMask = 7;
inline constexpr int kPgbOffsetFromShift = 5;
inline constexpr uint8_t kPgbOffsetFromMask = 7;
inline constexpr uint8_t kPgbApplyToMax = 2;
inline constexpr uint8_t kPgbOffsetFromMax = 1;

// sprmCFELayout (UFEL): fTNY, fWarichu, iWarichuBracket, fTNYCompress.
inline constexpr uint16_t kUfelTny = 0x0001;
inline constexpr uint16_t kUfelWarichu = 0x0002;
inline constexpr int kUfelBracketShift = 8;
inline constexpr uint16_t kUfelBracketMask = 7;
inline constexpr uint16_t kUfelTnyCompress = 0x1000;

// sprmCFitText (CFitTextOperand): cb = 8, dxaFitText(4), FitTextID(4).
inline constexpr uint8_t kFitTextCb = 8;
inline constexpr size_t kFitTextSize = 9;
inline constexpr size_t kFitTextWidthOffset = 1;
inline constexpr size_t kFitTextIdOffset = 5;

// sprmCFELayout (FarEastLayoutOperand): cb = 6, ufel(2), lFELayoutID(4).
inline constexpr uint8_t kFarEastLayoutCb = 6;
inline constexpr size_t kFarEastLayoutSize = 7;
inline constexpr size_t kFarEastLayoutIdOffset = 3;

// sprmTPc (PositionCodeOperand): pcVert in bits 4-5, pcHorz in bits 6-7.
inline constexpr int kPositionVertShift = 4;
inline constexpr int kPositionHorzShift = 6;
inline constexpr uint8_t kPositionAnchorMask = 3;
inline constexpr unsigned kPositionNone = 3;

// sprmTTlp (TLP): grfatl is the second 16-bit field; Fatl bits 5-10 hold the
// first/last row/column and band flags, which match w:tblLook bit positions.
inline constexpr size_t kTlpCb = 4;
inline constexpr size_t kTlpFatlOffset = 2;
inline constexpr uint16_t kFatlLookMask = 0x01E0;
inline constexpr uint16_t kFatlBandRow = 0x0200;
inline constexpr uint16_t kFatlBandCol = 0x0400;
inline constexpr int kLookNoHBand = 0x0200;
inline constexpr int kLookNoVBand = 0x0400;

// sprmTCellPaddingStyle / CSSAOperand: cb = 6, cb sides mask, ftsDxa = 3.
inline constexpr uint8_t kCssaCb = 6;
inline constexpr size_t kCssaSize = 7;
inline constexpr uint8_t kCssaSidesOffset = 3;
inline constexpr uint8_t kCssaWidthTypeOffset = 4;
inline constexpr size_t kCssaWidthOffset = 5;
inline constexpr uint8_t kFtsDxa = 3;
inline constexpr uint8_t kCssaAllSides = 15;

// sprmPItap: nested tables deeper than this are clamped (Word stops at 4).
inline constexpr int kMaxTableDepth = 8;

// sprmTCHorzBands / sprmTCVertBands accept 1-3 rows or columns per band.
inline constexpr uint8_t kMaxBandSize = 3;

// BrcCvOperand: cb(1) then one 4-byte COLORREF per cell.
inline constexpr size_t kBrcCvColorSize = 4;

// [MS-DOC] §2.5.1 text is UTF-16LE; a surrogate pair encodes one code point.
inline constexpr uint32_t kUtf16HighSurrogateMin = 0xD800;
inline constexpr uint32_t kUtf16HighSurrogateMax = 0xDBFF;
inline constexpr uint32_t kUtf16LowSurrogateMin = 0xDC00;
inline constexpr uint32_t kUtf16LowSurrogateMax = 0xDFFF;
inline constexpr uint32_t kUtf16SurrogateBase = 0x10000;

// Code points that must not reach the XML output: the noncharacters U+FFFE /
// U+FFFF and lone surrogates are replaced by U+FFFD.
inline constexpr uint32_t kReplacementCharacter = 0xFFFD;
inline constexpr uint32_t kNonCharacterMin = 0xFFFE;

// PlcPcd.PnFkp carries the FKP page number in its low 22 bits.
inline constexpr uint32_t kPnFkpMask = 0x3FFFFF;

// CP1252 runs (0x80-0x9F) are remapped to Unicode by a 32-entry table.
inline constexpr uint8_t kCp1252First = 0x80;
inline constexpr uint8_t kCp1252Last = 0x9F;

// PlcPcd entry: bit 30 marks CP1252 ("compressed") text, bits 0-29 hold the
// offset inside the WordDocument stream.
inline constexpr uint32_t kFcCompressed = 0x40000000;
inline constexpr uint32_t kFcMask = 0x3FFFFFFF;

struct Char {
  uint32_t cp = 0, fc = 0;
  // iconv can return up to eight bytes for one legacy-codepage character.
  std::array<char, 8> utf8{};
  uint8_t length = 0;
  bool newline = false;

  std::string_view text() const {
    return {utf8.data(), length};
  }
  void SetText(std::string_view value) {
    length = static_cast<uint8_t>(value.size());
    std::copy(value.begin(), value.end(), utf8.begin());
  }
};
static_assert(sizeof(Char) == 20);

struct FormatRange {
  uint32_t first, last;
  std::vector<uint8_t> sprms;
  int style = -1;
};

inline bool Valid(Bytes b, Fib::Range r) {
  return r.offset <= b.size() && r.size <= b.size() - r.offset;
}

std::expected<std::vector<Char>, Error> ReadChars(Bytes word, Bytes table,
                                                  const Fib& fib,
                                                  uint32_t limit,
                                                  uint32_t begin = 0);

std::expected<std::vector<FormatRange>, Error> ReadFkps(Bytes word, Bytes table,
                                                        Fib::Range r,
                                                        bool para);

const FormatRange* Find(const std::vector<FormatRange>& a, uint32_t fc);

std::vector<std::string> ReadFonts(Bytes table, Fib::Range r);

std::vector<model::Style> ReadStyles(Bytes table, Fib::Range r,
                                     const std::vector<std::string>& fonts);

void ReadLists(model::Document& doc, Bytes table, const Fib& fib);

model::Run InheritedRun(const model::Document& doc, int style);

// Turns the CP range [first, last) of one story into paragraphs. `base` is the
// absolute CP where the story starts; the final paragraph mark is a guard.
std::vector<model::Paragraph> ReadStoryParagraphs(
    const std::vector<Char>& chars, const std::vector<FormatRange>& chpx,
    const std::vector<FormatRange>& papx, const std::vector<std::string>& fonts,
    uint64_t base, uint32_t first, uint32_t last);

// Bookmark markers keyed by their CP in the main story.
struct BookmarkMark {
  int id = 0;
  std::string name;
  bool start = false;
};

struct BookmarkMarks {
  std::map<uint32_t, std::vector<BookmarkMark>> marks;
};

std::expected<BookmarkMarks, Error> ReadBookmarks(Bytes table, const Fib& fib);

// Comment anchor CPs inside the main story, in document order. Comment i of
// Document::comments is anchored at anchor[i].
std::expected<std::vector<uint32_t>, Error> ReadCommentAnchors(Bytes table,
                                                               const Fib& fib);

// Fills Document::comments with the comment bodies and their authors.
std::expected<void, Error> ReadComments(model::Document& doc, Bytes word,
                                        Bytes table, const Fib& fib,
                                        const std::vector<FormatRange>& chpx,
                                        const std::vector<FormatRange>& papx);

// Reference sites of footnotes/endnotes inside the main story, in document
// order. Note i of Document::notes is footnote i, then endnote i.
struct NoteReferenceSet {
  std::vector<uint32_t> footnote_cps;
  std::vector<uint32_t> endnote_cps;
};

std::expected<NoteReferenceSet, Error> ReadNoteReferences(Bytes table,
                                                          const Fib& fib);

// Fills Document::notes with the footnote and endnote bodies.
std::expected<void, Error> ReadNotes(model::Document& doc, Bytes word,
                                     Bytes table, const Fib& fib,
                                     const std::vector<FormatRange>& chpx,
                                     const std::vector<FormatRange>& papx);

// The six header/footer stories ([MS-DOC] §2.8.2) of one section:
// even/odd/default header, then even/odd/default footer.
using SectionHeaders = std::array<std::vector<model::Paragraph>, 6>;

std::expected<std::vector<SectionHeaders>, Error> ReadHeaders(
    model::Document& doc, Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    size_t sections);
}  // namespace doccraft::doc::detail

namespace doccraft::doc {

// True when two adjacent characters may share one run: every character
// property matches. Symbol and field-marker runs never merge. Shared by the
// body parser and the header/footer stories.
bool SameRunFormatting(model::Run& prior, model::Run& r);

}  // namespace doccraft::doc

#endif  // SRC_DOC_FORMAT_INTERNAL_H_
