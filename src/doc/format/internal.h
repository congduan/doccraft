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
inline constexpr size_t kMaxTotalImageSize = kMaxDocumentImageBytes;
inline constexpr uint32_t kOleClipboardWmf = 3, kOleClipboardEmf = 14;
// CF_DIB (Windows clipboard format 8) inside an OLE presentation stream: a
// packed BITMAPINFOHEADER block with no file header. OOXML has no packed-DIB
// part, so it is wrapped in a BITMAPFILEHEADER before being written out.
inline constexpr uint32_t kOleClipboardDib = 8;
inline constexpr size_t kBitmapFileHeaderSize = 14;
inline constexpr size_t kBitmapInfoHeaderSize = 40;
inline constexpr size_t kBitmapMasksSize = 12;
inline constexpr uint16_t kBitmapPlanes = 1;
inline constexpr uint8_t kBitmapPaletteMaxBits = 8;
inline constexpr uint32_t kBitmapRgb = 0, kBitmapBitfields = 3;
inline constexpr size_t kBitmapHeaderSizeOffset = 0;
inline constexpr size_t kBitmapPlanesOffset = 12;
inline constexpr size_t kBitmapBitCountOffset = 14;
inline constexpr size_t kBitmapCompressionOffset = 16;
inline constexpr size_t kBitmapColorsUsedOffset = 32;
inline constexpr uint32_t kOleClipboardMarker = 0xFFFFFFFF;
inline constexpr uint32_t kOleClipboardMarkerAlt = 0xFFFFFFFE;
inline constexpr uint32_t kOleHimetricPerInch = 2540;
inline constexpr uint32_t kTwipsPerInch = 1440;
inline constexpr size_t kOlePresentationPrefix = 40;
inline constexpr size_t kOleWmfTrailer = 18;
inline constexpr size_t kWmfHeaderBytes = 18, kEmfHeaderBytes = 88;
inline constexpr uint16_t kWmfHeaderWords = 9;
inline constexpr uint16_t kWmfVersion1 = 0x0100, kWmfVersion3 = 0x0300;
inline constexpr uint32_t kEmfHeaderType = 1;
inline constexpr size_t kEmfSignatureOffset = 40;
inline constexpr uint32_t kEmfSignature = 0x464D4520;

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
inline constexpr uint16_t kClientTextbox = 0xF00D;
inline constexpr uint16_t kTertiaryFopt = 0xF122;
inline constexpr size_t kFopteSize = 6, kFbseDelayOffset = 28;
inline constexpr uint16_t kPropertyIdMask = 0x3FFF;
inline constexpr uint16_t kPropertyComplex = 0x8000, kPib = 0x0104;
inline constexpr uint16_t kGtextUnicode = 192;
// [MS-ODRAW] §2.3.7.2 fillColor, §2.3.7.4 fillType, §2.3.8.1 lineColor,
// §2.3.8.14 lineWidth. Only plain RGB COLORREFs are mapped: an
// OfficeArtCOLORREF whose flag byte is set carries a palette/system/scheme
// index instead.
inline constexpr uint16_t kFillColor = 0x0181, kFillType = 0x0183;
inline constexpr uint16_t kLineColor = 0x01C0, kLineWidth = 0x01CB;
inline constexpr uint32_t kFillTypeSolid = 1;
inline constexpr uint32_t kColorFlagMask = 0xFF000000u;
// OfficeArt geometry is EMU: 635 per twip, so line widths share the twip cap.
inline constexpr uint32_t kEmuPerTwip = 635;
inline constexpr uint32_t kMaxLineWidthEmu = kMaxTwips * kEmuPerTwip;
inline constexpr size_t kFtxbxsSize = 22, kTbkdSize = 6;
inline constexpr size_t kFtxbxsLidOffset = 14;
inline constexpr int kWrapTopBottom = 1, kWrapNone = 3;
inline constexpr int kWrapTight = 4, kWrapThrough = 5;

// Operands that mean "absent"/"automatic" carry a saturated value instead of a
// separate flag.
inline constexpr uint16_t kUndefinedU16 = 0xFFFF;
inline constexpr uint32_t kUndefinedU32 = 0xFFFFFFFF;

// sprm operand structure constants, from [MS-DOC] §2.9 (operand definitions).
// Operands prefixed with a cb byte count the bytes that follow that byte.
inline constexpr size_t kCbPrefixSize = 1;
// BRC80: width, type, unused, flags. BRC: leading COLORREF, then width, type
// and flags; the flag byte packs a five-bit space, then shadow and frame.
inline constexpr size_t kBrc80Size = 4, kBrcSize = 8;
inline constexpr size_t kBrcPrefixedSize = 9;
inline constexpr uint8_t kBrcCb = 8;
inline constexpr size_t kBrcColorSize = 4;
inline constexpr size_t kBrcTypeOffset = 5, kBrcFlagsOffset = 6;
inline constexpr uint8_t kBrcSpaceMask = 31;
inline constexpr uint8_t kBrcShadow = 32, kBrcFrame = 64;
// SHD: fore COLORREF, back COLORREF, then a two-byte pattern; the sprm
// operand prefixes one cb byte. SHD80 packs two five-bit palette indices
// and a six-bit pattern.
inline constexpr size_t kShdSize = 10, kShdPrefixedSize = 11;
inline constexpr uint8_t kShdCb = 10;
inline constexpr size_t kShdForeOffset = 0, kShdBackOffset = 4,
                        kShdPatternOffset = 8;
inline constexpr uint16_t kShd80ColorMask = 31, kShd80PatternMask = 63;
inline constexpr int kShd80BackShift = 5, kShd80PatternShift = 10;
inline constexpr size_t kShd80Size = 2;

// TDefTable operand: the cell count at offset 2 precedes the two-byte cell
// grid; each TC80 cell carries flag fields, a width and four BRC80s.
inline constexpr size_t kTDefCountOffset = 2;
inline constexpr size_t kTDefHeaderSize = 3, kTDefMinSize = 5;
inline constexpr size_t kTDefCellSize = 20, kTDefWidthOffset = 2;
inline constexpr size_t kTDefBordersOffset = 4, kCellSideCount = 4;
inline constexpr size_t kMaxTableCells = 63;
inline constexpr int kTcWidthTypeShift = 9, kTcVertMergeShift = 5;
inline constexpr int kTcTextFlowShift = 2;
inline constexpr uint16_t kTcWidthTypeMask = 7, kTcMergeMask = 3;
inline constexpr uint16_t kTcTextFlowMask = 7;
inline constexpr unsigned kTcMergeStart = 2, kTcMergeContinue = 1;
inline constexpr unsigned kTcVertMergeRestart = 3;
inline constexpr unsigned kTcTextFlowMax = 5;

// Table layout operands in [MS-DOC] §2.9: fts plus a 16-bit width, the six
// table borders, and the two-byte itcFirst/itcLim cell range.
inline constexpr size_t kTableWidthSize = 3, kTableWidthValueOffset = 1;
inline constexpr uint8_t kFtsNil = 0, kFtsPct = 2;
inline constexpr size_t kTableBorderCount = 6;
inline constexpr size_t kTableRangeSize = 2;

// Table cell operations and their cb-prefixed operand layouts, [MS-DOC] §2.9.
inline constexpr size_t kTableInsertSize = 4, kTableInsertWidthOffset = 2;
inline constexpr size_t kTableColumnWidthSize = 4, kTableColumnWidthOffset = 2;
inline constexpr size_t kCellWidthSize = 6, kCellWidthCb = 5;
inline constexpr size_t kCellWidthTypeOffset = 3, kCellWidthValueOffset = 4;
inline constexpr size_t kVertMergeSize = 3, kVertMergeCb = 2;
inline constexpr size_t kVertMergeCellOffset = 1, kVertMergeValueOffset = 2;
inline constexpr size_t kCellTextFlowSize = 4, kCellTextFlowValueOffset = 2;
inline constexpr size_t kCellLayoutSize = 4, kCellLayoutCb = 3;
inline constexpr size_t kCellLayoutValueOffset = 3;
inline constexpr size_t kCellFitTextSize = 3, kCellFitTextValueOffset = 2;
inline constexpr size_t kCellShadingSize = 13, kCellShadingCb = 12;
inline constexpr size_t kCellShadingOffset = 3;
inline constexpr size_t kCellBorderSize = 12, kCellBorder80Size = 8;
inline constexpr size_t kCellBorderSidesOffset = 3, kCellBorderBrcOffset = 4;
inline constexpr uint8_t kCellBorderSidesMask = 63;
inline constexpr size_t kShadingCellGroupSize = 22;
// CNFOperand: cb, cnfc, then the nested grpprl.
inline constexpr size_t kCnfMinSize = 3, kCnfCnfcSize = 2;
// Boolean toggle operands and text flow values, [MS-DOC] §2.9.
inline constexpr uint8_t kToggleKeep = 0x80, kToggleInvert = 0x81;
inline constexpr unsigned kFlowHorizontal = 0, kFlowVertical = 1;
inline constexpr unsigned kFlowReserved = 2, kFlowVerticalBackwards = 3;
inline constexpr unsigned kFlowRotateLeft = 4, kFlowRotateRight = 5;
// Limits and packed fields in paragraph, section and cell sprms, §2.9.
// TBD: three-bit tab kind, then three-bit tab leader.
inline constexpr size_t kTabChangeMinSize = 3, kTabEntrySize = 3;
inline constexpr size_t kMaxTabStops = 64;
inline constexpr uint8_t kTabKindMask = 7, kTabLeaderMask = 7;
inline constexpr int kTabLeaderShift = 3;
inline constexpr uint8_t kEmphasisCount = 5;
inline constexpr uint16_t kMaxTableAlignment = 2;
inline constexpr uint8_t kMaxCellVerticalAlign = 2;
inline constexpr uint8_t kMaxSectionColumnIndex = 43;
inline constexpr uint8_t kMaxSectionVerticalJustify = 3;
inline constexpr uint16_t kMaxGridMode = 3;
inline constexpr uint32_t kMaxPageStart = 2147483646u;
inline constexpr uint8_t kMaxFrameWrap = 5;
inline constexpr uint8_t kFirstHeadingStyle = 1, kLastHeadingStyle = 9;
// sprmPOutLvl: 0-8 are outline levels, 9 is body text.
inline constexpr uint8_t kMaxOutlineLevel = 9;
inline constexpr uint8_t kDropCapNormal = 1, kDropCapMargin = 2;
inline constexpr uint8_t kMaxDropCapLines = 10;
inline constexpr size_t kRevisionAuthorMinSize = 2;
inline constexpr size_t kRevisionPropertyMinSize = 4;

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

// CSSAOperand, shared by sprmTCellPaddingStyle, sprmTCellPadding and the
// row-wide spacing/padding defaults: cb, itcFirst, itcLim, a side mask, fts,
// then a 16-bit width. The row-wide variants fix itcFirst = 0 and
// itcLim = 1; the spacing default additionally requires every side bit set
// (fbrcSidesOnly) and accepts the fts alias 0x13 for ftsDxa.
inline constexpr uint8_t kCssaCb = 6;
inline constexpr size_t kCssaSize = 7;
inline constexpr uint8_t kCssaSidesOffset = 3;
inline constexpr uint8_t kCssaWidthTypeOffset = 4;
inline constexpr size_t kCssaWidthOffset = 5;
inline constexpr uint8_t kFtsDxa = 3;
inline constexpr uint8_t kCssaSidesOnly = 15;
inline constexpr uint8_t kCssaFixedItcFirst = 0, kCssaFixedItcLim = 1;
inline constexpr uint8_t kCellSpacingDxaAlt = 0x13;
inline constexpr int kMaxCellSpacingTwips = 15840;

// sprmPItap: nested tables deeper than this are clamped (Word stops at 4).
inline constexpr int kMaxTableDepth = 8;

// sprmPItap/sprmPDtap operands are attacker-controlled 4-byte integers with
// no spec-defined range handling. Both depth consumers - the ApplyPara model
// field and the formatted.cc frame stack - clamp through this one helper so
// the IR depth and the built structure can never disagree; the frame stack
// additionally falls back to fInTable when the clamped depth is zero.
inline int ClampTableDepth(int64_t depth) {
  return static_cast<int>(std::clamp<int64_t>(depth, 0, kMaxTableDepth));
}

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
  uint16_t prm = 0;
  // One decoded Unicode scalar takes at most four UTF-8 bytes.
  std::array<char, 4> utf8{};
  uint8_t length = 0;
  bool newline = false;

  std::string_view text() const {
    return {utf8.data(), length};
  }
  bool SetText(std::string_view value) {
    if (value.size() > utf8.size())
      return false;
    length = static_cast<uint8_t>(value.size());
    std::copy(value.begin(), value.end(), utf8.begin());
    return true;
  }
};
static_assert(sizeof(Char) == 16);

struct FormatRange {
  uint32_t first, last;
  std::vector<uint8_t> sprms;
  int style = -1;
  bool damaged = false;
};

inline bool Valid(Bytes b, Fib::Range r) {
  return r.offset <= b.size() && r.size <= b.size() - r.offset;
}

std::expected<std::vector<Char>, Error> ReadChars(
    Bytes word, Bytes table, const Fib& fib, uint32_t limit, uint32_t begin = 0,
    std::vector<std::vector<uint8_t>>* prcs = nullptr,
    std::vector<std::string>* warnings = nullptr);

std::expected<std::vector<FormatRange>, Error> ReadFkps(
    Bytes word, Bytes table, Fib::Range r, bool para,
    std::vector<std::string>* warnings = nullptr, bool legacy = false);

const FormatRange* Find(const std::vector<FormatRange>& a, uint32_t fc);

std::vector<std::string> ReadFonts(Bytes table, Fib::Range r);

// Reads the revision-author string table (SttbfRMark). A zero-sized range
// is a valid empty table; structural violations are errors so the caller
// can fall back to the placeholder author with a warning.
std::expected<std::vector<std::string>, Error> ReadRevisionAuthors(
    Bytes table, Fib::Range range);

// Warns about revision marks whose author index cannot be resolved.
void ValidateRevisionAuthors(const model::Document& doc,
                             std::vector<std::string>* warnings);

std::vector<model::Style> ReadStyles(Bytes table, Fib::Range r,
                                     const std::vector<std::string>& fonts);

// Word 6/95 tables live in WordDocument and use the pre-97 layouts: the FFN
// entries are byte-counted with ANSI names in the document code page, and the
// STD records carry an 8-byte base, a Pascal name and parity-aligned UPXs of
// Word 6 sprms. Both warn and skip on anything they cannot parse.
std::vector<std::string> ReadLegacyFonts(Bytes word, Fib::Range r,
                                         uint32_t codepage,
                                         std::vector<std::string>* warnings);

std::vector<model::Style> ReadLegacyStyles(
    Bytes word, Fib::Range r, const std::vector<std::string>& fonts,
    uint32_t codepage, std::vector<std::string>* warnings);

// Translates a Word 6/95 one-byte-opcode grpprl into the modern sprm stream
// consumed by Walk(); returns false when an opcode is unsupported.
bool NormalizeLegacySprms(Bytes raw, std::vector<uint8_t>& out);

void ReadLists(model::Document& doc, Bytes table, const Fib& fib);

model::Run InheritedRun(const model::Document& doc, int style);

// Parses one story's Plcfld ([MS-DOC]: (n+1) CPs then n 2-byte Fld records)
// into a map keyed by the field-begin CP in absolute document CP
// space (`base` + the PLC's story-relative CP); the value is the Fld.grffld
// byte of the matching end entry, whose bit 4 is fLocked and bit 2 is
// fResultsDirty. `story_length` is the story's ccp. An empty range yields an
// empty map; anything malformed is an error, so non-main callers can degrade
// to a warning while the main story keeps failing hard.
std::expected<std::map<uint32_t, uint8_t>, Error> ReadStoryFieldStatus(
    Bytes table, Fib::Range r, uint64_t base, uint32_t story_length);

// Turns the CP range [first, last) of one story into paragraphs. `base` is the
// absolute CP where the story starts. `last_cp_is_guard` controls whether the
// final CP is a paragraph guard that may hold a bookmark end.
// `field_status` (may be null) carries the story's Plcfld flags keyed by the
// absolute begin CP; the field hierarchy itself always comes from the 0x13/
// 0x14/0x15 characters, so a null map only drops fldLock/dirty.
struct BookmarkMarks;
std::vector<model::Paragraph> ReadStoryParagraphs(
    const std::vector<Char>& chars, const std::vector<FormatRange>& chpx,
    const std::vector<FormatRange>& papx, const std::vector<std::string>& fonts,
    const std::vector<std::vector<uint8_t>>& prcs, uint64_t base,
    uint32_t first, uint32_t last,
    const std::map<uint32_t, uint8_t>* field_status = nullptr,
    const BookmarkMarks* bookmarks = nullptr, bool last_cp_is_guard = true);

uint16_t Prm0Opcode(uint8_t isprm);

std::expected<std::vector<model::Textbox>, Error> ReadTextboxes(
    Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    const std::vector<std::string>& fonts,
    const std::vector<FloatingPicture>& drawings,
    const BookmarkMarks& bookmarks, std::vector<std::string>* warnings);
std::expected<std::vector<model::Textbox>, Error> ReadHeaderTextboxes(
    Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    const std::vector<std::string>& fonts, const BookmarkMarks& bookmarks,
    std::vector<std::string>* warnings);

// Standard bookmark markers keyed by absolute CP across document parts.
struct BookmarkMark {
  int id = 0;
  std::string name;
  bool start = false;
};

struct BookmarkMarks {
  std::map<uint32_t, std::vector<BookmarkMark>> marks;
};

std::expected<BookmarkMarks, Error> ReadBookmarks(Bytes table, const Fib& fib);

// Keep complete pairs inside one story range. When the final CP is a guard,
// a bookmark may end there but may not start there. Otherwise the end may
// equal last (the main story's exclusive end CP).
BookmarkMarks FilterStoryBookmarks(
    const BookmarkMarks& bookmarks,
    const std::vector<std::pair<uint64_t, uint64_t>>& ranges, const char* story,
    std::vector<std::string>* warnings, bool last_cp_is_guard = true);

// Comment anchor CPs inside the main story, in document order. Comment i of
// Document::comments is anchored at anchor[i].
std::expected<std::vector<uint32_t>, Error> ReadCommentAnchors(
    Bytes table, const Fib& fib, std::vector<std::string>* warnings);

struct CommentRange {
  uint32_t start = 0, end = 0;
  int id = 0;
};
std::vector<CommentRange> ReadCommentRanges(Bytes table, const Fib& fib,
                                            std::vector<std::string>* warnings);

// Fills Document::comments with the comment bodies and their authors.
std::expected<void, Error> ReadComments(model::Document& doc, Bytes word,
                                        Bytes table, const Fib& fib,
                                        const std::vector<FormatRange>& chpx,
                                        const std::vector<FormatRange>& papx,
                                        const BookmarkMarks& bookmarks);

// Reference sites of footnotes/endnotes inside the main story, in document
// order. Note i of Document::notes is footnote i, then endnote i.
struct NoteReferenceSet {
  std::vector<uint32_t> footnote_cps;
  std::vector<uint32_t> endnote_cps;
};

std::expected<NoteReferenceSet, Error> ReadNoteReferences(
    Bytes table, const Fib& fib, std::vector<std::string>* warnings);

// Fills Document::notes with the footnote and endnote bodies.
std::expected<void, Error> ReadNotes(model::Document& doc, Bytes word,
                                     Bytes table, const Fib& fib,
                                     const std::vector<FormatRange>& chpx,
                                     const std::vector<FormatRange>& papx,
                                     const BookmarkMarks& bookmarks);

// The six header/footer stories ([MS-DOC] §2.8.2) of one section:
// even/odd/default header, then even/odd/default footer.
using SectionHeaders = std::array<std::vector<model::Paragraph>, 6>;

std::expected<std::vector<SectionHeaders>, Error> ReadHeaders(
    model::Document& doc, Bytes word, Bytes table, const Fib& fib,
    const std::vector<FormatRange>& chpx, const std::vector<FormatRange>& papx,
    size_t sections, const BookmarkMarks& bookmarks);
}  // namespace doccraft::doc::detail

namespace doccraft::doc {

// True when two adjacent characters may share one run: every character
// property matches. Symbol and field-marker runs never merge. Shared by the
// body parser and the header/footer stories.
bool SameRunFormatting(model::Run& prior, model::Run& r);

}  // namespace doccraft::doc

#endif  // SRC_DOC_FORMAT_INTERNAL_H_
