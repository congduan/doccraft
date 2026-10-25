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
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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
  std::string text;
  uint32_t cp, fc;
  bool newline;
};

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
                                                  uint32_t limit);

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
bool SameRunFormatting(const model::Run& prior, const model::Run& r);

}  // namespace doccraft::doc

#endif  // SRC_DOC_FORMAT_INTERNAL_H_
