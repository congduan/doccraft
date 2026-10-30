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

#ifndef SRC_DOC_FORMAT_SPRM_CODES_H_
#define SRC_DOC_FORMAT_SPRM_CODES_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace doccraft::doc::detail {

// Word 6/95 grpprls use one-byte opcodes. The FKP translator maps this
// supported subset to the Word 97+ sprms below.
inline constexpr uint8_t kLegacySprmPJc = 0x05;
inline constexpr uint8_t kLegacySprmPFKeep = 0x07;
inline constexpr uint8_t kLegacySprmPFKeepFollow = 0x08;
inline constexpr uint8_t kLegacySprmPFPageBreakBefore = 0x09;
inline constexpr uint8_t kLegacySprmPChgTabsPapx = 0x0F;
inline constexpr uint8_t kLegacySprmPChgTabs = 0x17;
inline constexpr uint8_t kLegacySprmPDxaRight = 0x10;
inline constexpr uint8_t kLegacySprmPDxaLeft = 0x11;
inline constexpr uint8_t kLegacySprmPDxaLeft1 = 0x13;
inline constexpr uint8_t kLegacySprmPDyaLine = 0x14;
inline constexpr uint8_t kLegacySprmPDyaBefore = 0x15;
inline constexpr uint8_t kLegacySprmPDyaAfter = 0x16;
inline constexpr uint8_t kLegacySprmPFInTable = 0x18;
inline constexpr uint8_t kLegacySprmPFTtp = 0x19;
inline constexpr uint8_t kLegacySprmPWr = 0x25;
inline constexpr uint8_t kLegacySprmPFNoAutoHyph = 0x2C;
inline constexpr uint8_t kLegacySprmPFWidowControl = 0x33;
// East Asian paragraph flags. [MS-DOC] 2.9.215 (Prm0 isprm) numbers the same
// one-byte space as the Word 6/95 grpprls and names 0x35..0x3A explicitly;
// each operand is a one-byte Bool8.
inline constexpr uint8_t kLegacySprmPFKinsoku = 0x35;
inline constexpr uint8_t kLegacySprmPFWordWrap = 0x36;
inline constexpr uint8_t kLegacySprmPFOverflowPunct = 0x37;
inline constexpr uint8_t kLegacySprmPFTopLinePunct = 0x38;
inline constexpr uint8_t kLegacySprmPFAutoSpaceDE = 0x39;
inline constexpr uint8_t kLegacySprmPFAutoSpaceDN = 0x3A;
// Section properties that identify the header/footer stories of a Word 6/95
// section. sprmSGprfIhdt is consumed by the legacy header reader; the other
// section opcodes are translated onto their Word 97+ counterparts.
inline constexpr uint8_t kLegacySprmSFTitlePage = 0x8F;
inline constexpr uint8_t kLegacySprmSGprfIhdt = 0x99;
inline constexpr uint8_t kLegacySprmScnsPgn = 0x83;
inline constexpr uint8_t kLegacySprmSiHeadingPgn = 0x84;
inline constexpr uint8_t kLegacySprmSDxaColWidth = 0x88;
inline constexpr uint8_t kLegacySprmSDxaColSpacing = 0x89;
inline constexpr uint8_t kLegacySprmSFEvenlySpaced = 0x8A;
inline constexpr uint8_t kLegacySprmSFProtected = 0x8B;
inline constexpr uint8_t kLegacySprmSDmBinFirst = 0x8C;
inline constexpr uint8_t kLegacySprmSDmBinOther = 0x8D;
inline constexpr uint8_t kLegacySprmSBkc = 0x8E;
inline constexpr uint8_t kLegacySprmSCcolumns = 0x90;
inline constexpr uint8_t kLegacySprmSDxaColumns = 0x91;
inline constexpr uint8_t kLegacySprmSNfcPgn = 0x93;
inline constexpr uint8_t kLegacySprmSFPgnRestart = 0x96;
inline constexpr uint8_t kLegacySprmSFEndnote = 0x97;
inline constexpr uint8_t kLegacySprmSLnc = 0x98;
inline constexpr uint8_t kLegacySprmSNLnnMod = 0x9A;
inline constexpr uint8_t kLegacySprmSDxaLnn = 0x9B;
inline constexpr uint8_t kLegacySprmSDyaHdrTop = 0x9C;
inline constexpr uint8_t kLegacySprmSDyaHdrBottom = 0x9D;
inline constexpr uint8_t kLegacySprmSLBetween = 0x9E;
inline constexpr uint8_t kLegacySprmSVjc = 0x9F;
inline constexpr uint8_t kLegacySprmSLnnMin = 0xA0;
inline constexpr uint8_t kLegacySprmSPgnStart = 0xA1;
inline constexpr uint8_t kLegacySprmSBOrientation = 0xA2;
inline constexpr uint8_t kLegacySprmSXaPage = 0xA4;
inline constexpr uint8_t kLegacySprmSYaPage = 0xA5;
inline constexpr uint8_t kLegacySprmSDxaLeft = 0xA6;
inline constexpr uint8_t kLegacySprmSDxaRight = 0xA7;
inline constexpr uint8_t kLegacySprmSDyaTop = 0xA8;
inline constexpr uint8_t kLegacySprmSDyaBottom = 0xA9;
inline constexpr uint8_t kLegacySprmSDzaGutter = 0xAA;
// Word 6/95 table properties. LibreOffice's GetWW6SprmSearcher lists 0xB6,
// 0xB7, 0xB8 and 0xBD as two-byte and 0xB9/0xBA as one-byte operands
// (`tap.jc jc (low order byte is significant)`, `tap.fCantSplit 1 or 0 byte`,
// `tap.fTableHeader 1 or 0 byte`, `tap.dyaRowHeight dya word`). The opcodes
// share the operand layout of their Word 97+ namesakes ([MS-DOC] 2.9.144
// sprmTDxaLeft, 2.9.145 sprmTDxaGapHalf, 2.9.169 sprmTDyaRowHeight).
inline constexpr uint8_t kLegacySprmTJc = 0xB6;
inline constexpr uint8_t kLegacySprmTDxaLeft = 0xB7;
inline constexpr uint8_t kLegacySprmTDxaGapHalf = 0xB8;
inline constexpr uint8_t kLegacySprmTFCantSplit = 0xB9;
inline constexpr uint8_t kLegacySprmTTableHeader = 0xBA;
inline constexpr uint8_t kLegacySprmTDyaRowHeight = 0xBD;
// The compound Word 6/95 table operands. LibreOffice GetWW6SprmSearcher gives
// 0xBB {12, L_FIX}, 0xBC/0xBE {0, L_VAR2} and 0xBF {1, L_VAR}; wvWare
// wvApplysprmTTableBorders reads 0xBB as six two-byte BRC6 values and
// wvApplysprmTDefTable reads 0xBC/0xBE as a two-byte cb, one-byte itcMac,
// itcMac+1 XAS centres and itcMac ten-byte TC6 cells. 0xBF is a one-byte cb
// followed by two-byte SHD palette pairs, the same pairs as SHD80.
inline constexpr uint8_t kLegacySprmTTableBorders = 0xBB;
inline constexpr uint8_t kLegacySprmTDefTable10 = 0xBC;
inline constexpr uint8_t kLegacySprmTDefTable = 0xBE;
inline constexpr uint8_t kLegacySprmTDefTableShd = 0xBF;
inline constexpr uint8_t kLegacySprmCFBold = 0x55;
inline constexpr uint8_t kLegacySprmCFItalic = 0x56;
inline constexpr uint8_t kLegacySprmCFStrike = 0x57;
inline constexpr uint8_t kLegacySprmCFtc = 0x5D;
inline constexpr uint8_t kLegacySprmCKul = 0x5E;
inline constexpr uint8_t kLegacySprmCDxaSpace = 0x60;
inline constexpr uint8_t kLegacySprmCLid = 0x61;
inline constexpr uint8_t kLegacySprmCIco = 0x62;
inline constexpr uint8_t kLegacySprmCHps = 0x63;
inline constexpr uint8_t kLegacySprmCPicLocation = 0x44;

// Word 6/95 one-byte sprm operand widths. Transcribed row for row from
// LibreOffice sw/source/filter/ww8/ww8scan.cxx (GetWW6SprmSearcher), whose
// opcodes come from sw/source/filter/ww8/sprmids.hxx namespace v6. 0x35..0x3A
// are absent there; their widths come from [MS-DOC] 2.9.215 (Prm0), which
// numbers the same one-byte space. The table lets the FKP translator step over
// opcodes whose semantics it does not map: a known width never turns an
// operand byte into a bogus opcode.
//
// kFix: `size` operand bytes. kVar: one cb byte, `size` extra fixed bytes,
// then cb operand bytes. kVar2: a 16-bit count of (operand bytes + 1).
// For spra = 6 operands [MS-DOC] 2.2.5.1 makes the cb byte the count of the
// operand bytes that follow it, so the operand occupies cb + 1 bytes in total;
// LibreOffice's L_VAR `nLen` is not an extra fixed width, and sprmTDefTableShd
// (0xBF) therefore carries no extra byte here.
// Only unmapped opcodes are consumed through this table; the mapped subset is
// translated by NormalizeLegacySprms, which keeps the measured width of
// sprmCPicLocation (0x44, four bytes) even though LibreOffice marks it L_VAR.
enum class LegacySprmWidth : uint8_t { kFix, kVar, kVar2 };

struct LegacySprmSpec {
  uint8_t code;
  uint8_t size;
  LegacySprmWidth width;
};

inline constexpr LegacySprmSpec kLegacySprmSpecs[] = {
    {0x00, 0, LegacySprmWidth::kFix},   // default sprm (skipped)
    {0x02, 2, LegacySprmWidth::kFix},   // sprmPIstd
    {0x03, 3, LegacySprmWidth::kVar},   // sprmPIstdPermute
    {0x04, 1, LegacySprmWidth::kFix},   // sprmPIncLv1
    {0x05, 1, LegacySprmWidth::kFix},   // sprmPJc
    {0x06, 1, LegacySprmWidth::kFix},   // sprmPFSideBySide
    {0x07, 1, LegacySprmWidth::kFix},   // sprmPFKeep
    {0x08, 1, LegacySprmWidth::kFix},   // sprmPFKeepFollow
    {0x09, 1, LegacySprmWidth::kFix},   // sprmPPageBreakBefore
    {0x0A, 1, LegacySprmWidth::kFix},   // sprmPBrcl
    {0x0B, 1, LegacySprmWidth::kFix},   // sprmPBrcp
    {0x0C, 0, LegacySprmWidth::kVar},   // sprmPAnld
    {0x0D, 1, LegacySprmWidth::kFix},   // sprmPNLvlAnm
    {0x0E, 1, LegacySprmWidth::kFix},   // sprmPFNoLineNumb
    {0x0F, 0, LegacySprmWidth::kVar},   // sprmPChgTabsPapx
    {0x10, 2, LegacySprmWidth::kFix},   // sprmPDxaRight
    {0x11, 2, LegacySprmWidth::kFix},   // sprmPDxaLeft
    {0x12, 2, LegacySprmWidth::kFix},   // sprmPNest
    {0x13, 2, LegacySprmWidth::kFix},   // sprmPDxaLeft1
    {0x14, 4, LegacySprmWidth::kFix},   // sprmPDyaLine
    {0x15, 2, LegacySprmWidth::kFix},   // sprmPDyaBefore
    {0x16, 2, LegacySprmWidth::kFix},   // sprmPDyaAfter
    {0x17, 0, LegacySprmWidth::kVar},   // sprmPChgTabs
    {0x18, 1, LegacySprmWidth::kFix},   // sprmPFInTable
    {0x19, 1, LegacySprmWidth::kFix},   // sprmPTtp
    {0x1A, 2, LegacySprmWidth::kFix},   // sprmPDxaAbs
    {0x1B, 2, LegacySprmWidth::kFix},   // sprmPDyaAbs
    {0x1C, 2, LegacySprmWidth::kFix},   // sprmPDxaWidth
    {0x1D, 1, LegacySprmWidth::kFix},   // sprmPPc
    {0x1E, 2, LegacySprmWidth::kFix},   // sprmPBrcTop10
    {0x1F, 2, LegacySprmWidth::kFix},   // sprmPBrcLeft10
    {0x20, 2, LegacySprmWidth::kFix},   // sprmPBrcBottom10
    {0x21, 2, LegacySprmWidth::kFix},   // sprmPBrcRight10
    {0x22, 2, LegacySprmWidth::kFix},   // sprmPBrcBetween10
    {0x23, 2, LegacySprmWidth::kFix},   // sprmPBrcBar10
    {0x24, 2, LegacySprmWidth::kFix},   // sprmPFromText10
    {0x25, 1, LegacySprmWidth::kFix},   // sprmPWr
    {0x26, 2, LegacySprmWidth::kFix},   // sprmPBrcTop
    {0x27, 2, LegacySprmWidth::kFix},   // sprmPBrcLeft
    {0x28, 2, LegacySprmWidth::kFix},   // sprmPBrcBottom
    {0x29, 2, LegacySprmWidth::kFix},   // sprmPBrcRight
    {0x2A, 2, LegacySprmWidth::kFix},   // sprmPBrcBetween
    {0x2B, 2, LegacySprmWidth::kFix},   // sprmPBrcBar
    {0x2C, 1, LegacySprmWidth::kFix},   // sprmPFNoAutoHyph
    {0x2D, 2, LegacySprmWidth::kFix},   // sprmPWHeightAbs
    {0x2E, 2, LegacySprmWidth::kFix},   // sprmPDcs
    {0x2F, 2, LegacySprmWidth::kFix},   // sprmPShd
    {0x30, 2, LegacySprmWidth::kFix},   // sprmPDyaFromText
    {0x31, 2, LegacySprmWidth::kFix},   // sprmPDxaFromText
    {0x32, 1, LegacySprmWidth::kFix},   // sprmPFLocked
    {0x33, 1, LegacySprmWidth::kFix},   // sprmPFWidowControl
    {0x34, 0, LegacySprmWidth::kFix},   // sprmPRuler
    {0x35, 1, LegacySprmWidth::kFix},   // sprmPFKinsoku
    {0x36, 1, LegacySprmWidth::kFix},   // sprmPFWordWrap
    {0x37, 1, LegacySprmWidth::kFix},   // sprmPFOverflowPunct
    {0x38, 1, LegacySprmWidth::kFix},   // sprmPFTopLinePunct
    {0x39, 1, LegacySprmWidth::kFix},   // sprmPFAutoSpaceDE
    {0x3A, 1, LegacySprmWidth::kFix},   // sprmPFAutoSpaceDN
    {0x40, 0, LegacySprmWidth::kVar},   // unknown RTL property
    {0x41, 1, LegacySprmWidth::kFix},   // sprmCFStrikeRM
    {0x42, 1, LegacySprmWidth::kFix},   // sprmCFRMark
    {0x43, 1, LegacySprmWidth::kFix},   // sprmCFFldVanish
    {0x44, 0, LegacySprmWidth::kVar},   // sprmCPicLocation
    {0x45, 2, LegacySprmWidth::kFix},   // sprmCIbstRMark
    {0x46, 4, LegacySprmWidth::kFix},   // sprmCDttmRMark
    {0x47, 1, LegacySprmWidth::kFix},   // sprmCFData
    {0x48, 2, LegacySprmWidth::kFix},   // sprmCRMReason
    {0x49, 3, LegacySprmWidth::kFix},   // sprmCChse
    {0x4A, 0, LegacySprmWidth::kVar},   // sprmCSymbol
    {0x4B, 1, LegacySprmWidth::kFix},   // sprmCFOle2
    {0x4D, 0, LegacySprmWidth::kVar},   // unknown
    {0x4F, 0, LegacySprmWidth::kVar},   // unknown
    {0x50, 2, LegacySprmWidth::kFix},   // sprmCIstd
    {0x51, 0, LegacySprmWidth::kVar},   // sprmCIstdPermute
    {0x52, 0, LegacySprmWidth::kVar},   // sprmCDefault
    {0x53, 0, LegacySprmWidth::kFix},   // sprmCPlain
    {0x55, 1, LegacySprmWidth::kFix},   // sprmCFBold
    {0x56, 1, LegacySprmWidth::kFix},   // sprmCFItalic
    {0x57, 1, LegacySprmWidth::kFix},   // sprmCFStrike
    {0x58, 1, LegacySprmWidth::kFix},   // sprmCFOutline
    {0x59, 1, LegacySprmWidth::kFix},   // sprmCFShadow
    {0x5A, 1, LegacySprmWidth::kFix},   // sprmCFSmallCaps
    {0x5B, 1, LegacySprmWidth::kFix},   // sprmCFCaps
    {0x5C, 1, LegacySprmWidth::kFix},   // sprmCFVanish
    {0x5D, 2, LegacySprmWidth::kFix},   // sprmCFtc
    {0x5E, 1, LegacySprmWidth::kFix},   // sprmCKul
    {0x5F, 3, LegacySprmWidth::kFix},   // sprmCSizePos
    {0x60, 2, LegacySprmWidth::kFix},   // sprmCDxaSpace
    {0x61, 2, LegacySprmWidth::kFix},   // sprmCLid
    {0x62, 1, LegacySprmWidth::kFix},   // sprmCIco
    {0x63, 2, LegacySprmWidth::kFix},   // sprmCHps
    {0x64, 1, LegacySprmWidth::kFix},   // sprmCHpsInc
    {0x65, 2, LegacySprmWidth::kFix},   // sprmCHpsPos
    {0x66, 1, LegacySprmWidth::kFix},   // sprmCHpsPosAdj
    {0x67, 0, LegacySprmWidth::kVar},   // sprmCMajority
    {0x68, 1, LegacySprmWidth::kFix},   // sprmCIss
    {0x69, 0, LegacySprmWidth::kVar},   // sprmCHpsNew50
    {0x6A, 0, LegacySprmWidth::kVar},   // sprmCHpsInc1
    {0x6B, 2, LegacySprmWidth::kFix},   // sprmCHpsKern
    {0x6C, 0, LegacySprmWidth::kVar},   // sprmCMajority50
    {0x6D, 2, LegacySprmWidth::kFix},   // sprmCHpsMul
    {0x6E, 2, LegacySprmWidth::kFix},   // sprmCCondHyhen
    {0x6F, 0, LegacySprmWidth::kVar},   // unknown
    {0x70, 0, LegacySprmWidth::kVar},   // unknown
    {0x71, 0, LegacySprmWidth::kVar},   // unknown
    {0x72, 0, LegacySprmWidth::kVar},   // unknown
    {0x73, 0, LegacySprmWidth::kVar},   // unknown
    {0x74, 0, LegacySprmWidth::kVar},   // unknown
    {0x75, 1, LegacySprmWidth::kFix},   // sprmCFSpec
    {0x76, 1, LegacySprmWidth::kFix},   // sprmCFObj
    {0x77, 1, LegacySprmWidth::kFix},   // sprmPicBrcl
    {0x78, 12, LegacySprmWidth::kVar},  // sprmPicScale
    {0x79, 2, LegacySprmWidth::kFix},   // sprmPicBrcTop
    {0x7A, 2, LegacySprmWidth::kFix},   // sprmPicBrcLeft
    {0x7B, 2, LegacySprmWidth::kFix},   // sprmPicBrcBottom
    {0x7C, 2, LegacySprmWidth::kFix},   // sprmPicBrcRight
    {0x83, 1, LegacySprmWidth::kFix},   // sprmSScnsPgn
    {0x84, 1, LegacySprmWidth::kFix},   // sprmSiHeadingPgn
    {0x85, 0, LegacySprmWidth::kVar},   // sprmSOlstAnm
    {0x88, 3, LegacySprmWidth::kFix},   // sprmSDxaColWidth
    {0x89, 3, LegacySprmWidth::kFix},   // sprmSDxaColSpacing
    {0x8A, 1, LegacySprmWidth::kFix},   // sprmSFEvenlySpaced
    {0x8B, 1, LegacySprmWidth::kFix},   // sprmSFProtected
    {0x8C, 2, LegacySprmWidth::kFix},   // sprmSDmBinFirst
    {0x8D, 2, LegacySprmWidth::kFix},   // sprmSDmBinOther
    {0x8E, 1, LegacySprmWidth::kFix},   // sprmSBkc
    {0x8F, 1, LegacySprmWidth::kFix},   // sprmSFTitlePage
    {0x90, 2, LegacySprmWidth::kFix},   // sprmSCcolumns
    {0x91, 2, LegacySprmWidth::kFix},   // sprmSDxaColumns
    {0x92, 1, LegacySprmWidth::kFix},   // sprmSFAutoPgn
    {0x93, 1, LegacySprmWidth::kFix},   // sprmSNfcPgn
    {0x94, 2, LegacySprmWidth::kFix},   // sprmSDyaPgn
    {0x95, 2, LegacySprmWidth::kFix},   // sprmSDxaPgn
    {0x96, 1, LegacySprmWidth::kFix},   // sprmSFPgnRestart
    {0x97, 1, LegacySprmWidth::kFix},   // sprmSFEndnote
    {0x98, 1, LegacySprmWidth::kFix},   // sprmSLnc
    {0x99, 1, LegacySprmWidth::kFix},   // sprmSGprfIhdt
    {0x9A, 2, LegacySprmWidth::kFix},   // sprmSNLnnMod
    {0x9B, 2, LegacySprmWidth::kFix},   // sprmSDxaLnn
    {0x9C, 2, LegacySprmWidth::kFix},   // sprmSDyaHdrTop
    {0x9D, 2, LegacySprmWidth::kFix},   // sprmSDyaHdrBottom
    {0x9E, 1, LegacySprmWidth::kFix},   // sprmSLBetween
    {0x9F, 1, LegacySprmWidth::kFix},   // sprmSVjc
    {0xA0, 2, LegacySprmWidth::kFix},   // sprmSLnnMin
    {0xA1, 2, LegacySprmWidth::kFix},   // sprmSPgnStart
    {0xA2, 1, LegacySprmWidth::kFix},   // sprmSBOrientation
    {0xA3, 0, LegacySprmWidth::kFix},   // sprmSBCustomize
    {0xA4, 2, LegacySprmWidth::kFix},   // sprmSXaPage
    {0xA5, 2, LegacySprmWidth::kFix},   // sprmSYaPage
    {0xA6, 2, LegacySprmWidth::kFix},   // sprmSDxaLeft
    {0xA7, 2, LegacySprmWidth::kFix},   // sprmSDxaRight
    {0xA8, 2, LegacySprmWidth::kFix},   // sprmSDyaTop
    {0xA9, 2, LegacySprmWidth::kFix},   // sprmSDyaBottom
    {0xAA, 2, LegacySprmWidth::kFix},   // sprmSDzaGutter
    {0xAB, 2, LegacySprmWidth::kFix},   // sprmSDMPaperReq
    {0xB3, 0, LegacySprmWidth::kVar},   // unknown RTL property
    {0xB5, 0, LegacySprmWidth::kVar},   // unknown RTL property
    {0xB6, 2, LegacySprmWidth::kFix},   // sprmTJc
    {0xB7, 2, LegacySprmWidth::kFix},   // sprmTDxaLeft
    {0xB8, 2, LegacySprmWidth::kFix},   // sprmTDxaGapHalf
    {0xB9, 1, LegacySprmWidth::kFix},   // sprmTFCantSplit
    {0xBA, 1, LegacySprmWidth::kFix},   // sprmTTableHeader
    // Complex table operands. The widths stay the same now that the mapper
    // consumes them: 0xBB is six two-byte BRC6 borders, 0xBC/0xBE are a
    // two-byte cb + itcMac + XAS centres + ten-byte TC6 cells, and 0xBF is a
    // cb + two-byte SHD pairs. See ScanLegacySprms for the row/runtime
    // reconstruction into the modern operands.
    {0xBB, 12, LegacySprmWidth::kFix},  // sprmTTableBorders
    {0xBC, 0, LegacySprmWidth::kVar2},  // sprmTDefTable10
    {0xBD, 2, LegacySprmWidth::kFix},   // sprmTDyaRowHeight
    {0xBE, 0, LegacySprmWidth::kVar2},  // sprmTDefTable
    // spra = 6 ([MS-DOC] 2.2.5.1): the cb byte counts the operand bytes that
    // follow it, so the operand is cb + 1 bytes and there is no extra fixed
    // byte. Bug50955.doc ends a PAPX grpprl with `BF 04 01 05 01 05` (cb = 4
    // plus four shading bytes), which LibreOffice's extra `1` (L_VAR nLen)
    // would overrun by one byte. The SHD pairs are already the SHD80 layout,
    // so the operand is forwarded to sprmTDefTableShd80 unchanged.
    {0xBF, 0, LegacySprmWidth::kVar},  // sprmTDefTableShd
    {0xC0, 4, LegacySprmWidth::kFix},  // sprmTTlp
    {0xC1, 5, LegacySprmWidth::kFix},  // sprmTSetBrc
    {0xC2, 4, LegacySprmWidth::kFix},  // sprmTInsert
    {0xC3, 2, LegacySprmWidth::kFix},  // sprmTDelete
    {0xC4, 4, LegacySprmWidth::kFix},  // sprmTDxaCol
    {0xC5, 2, LegacySprmWidth::kFix},  // sprmTMerge
    {0xC6, 2, LegacySprmWidth::kFix},  // sprmTSplit
    {0xC7, 5, LegacySprmWidth::kFix},  // sprmTSetBrc10
    {0xC8, 4, LegacySprmWidth::kFix},  // sprmTSetShd
    {0xCF, 0, LegacySprmWidth::kVar},  // unknown RTL property
};

constexpr bool LegacySprmSpecsSorted() {
  for (size_t i = 1; i < std::size(kLegacySprmSpecs); ++i)
    if (kLegacySprmSpecs[i - 1].code >= kLegacySprmSpecs[i].code)
      return false;
  return true;
}
static_assert(LegacySprmSpecsSorted());

// Returns the width record for a Word 6/95 one-byte opcode, or nullptr when
// the opcode is outside the table and its operand width is unknowable.
constexpr const LegacySprmSpec* FindLegacySprmSpec(uint8_t code) {
  const LegacySprmSpec* first = std::begin(kLegacySprmSpecs);
  const LegacySprmSpec* last = std::end(kLegacySprmSpecs);
  const LegacySprmSpec* it = std::lower_bound(
      first, last, code, [](const LegacySprmSpec& spec, uint8_t value) {
        return spec.code < value;
      });
  return it != last && it->code == code ? it : nullptr;
}

// sprm opcodes from [MS-DOC] v20260217 §2.6.1-2.6.4. Names match the
// specification text, so searching the MS-DOC PDF for `sprmCFBold` also
// finds this constant. Keep docs/SPRM-COVERAGE.md in sync when adding one.

inline constexpr uint16_t kSprmCFRMarkDel = 0x0800;     // sprmCFRMarkDel
inline constexpr uint16_t kSprmCFRMarkIns = 0x0801;     // sprmCFRMarkIns
inline constexpr uint16_t kSprmCIbstRMark = 0x4804;     // sprmCIbstRMark
inline constexpr uint16_t kSprmCIbstRMarkDel = 0x4863;  // sprmCIbstRMarkDel
inline constexpr uint16_t kSprmCDttmRMark = 0x6805;     // sprmCDttmRMark
inline constexpr uint16_t kSprmCDttmRMarkDel = 0x6864;  // sprmCDttmRMarkDel
inline constexpr uint16_t kSprmCFFldVanish = 0x0802;    // sprmCFFldVanish
inline constexpr uint16_t kSprmCFData = 0x0806;         // sprmCFData
inline constexpr uint16_t kSprmCFOle2 = 0x080A;         // sprmCFOle2
inline constexpr uint16_t kSprmCFWebHidden = 0x0811;    // sprmCFWebHidden
inline constexpr uint16_t kSprmCFSpecVanish = 0x0818;   // sprmCFSpecVanish
inline constexpr uint16_t kSprmCFBold = 0x0835;         // sprmCFBold
inline constexpr uint16_t kSprmCFItalic = 0x0836;       // sprmCFItalic
inline constexpr uint16_t kSprmCFStrike = 0x0837;       // sprmCFStrike
inline constexpr uint16_t kSprmCFOutline = 0x0838;      // sprmCFOutline
inline constexpr uint16_t kSprmCFShadow = 0x0839;       // sprmCFShadow
inline constexpr uint16_t kSprmCFSmallCaps = 0x083A;    // sprmCFSmallCaps
inline constexpr uint16_t kSprmCFCaps = 0x083B;         // sprmCFCaps
inline constexpr uint16_t kSprmCFVanish = 0x083C;       // sprmCFVanish
inline constexpr uint16_t kSprmCFImprint = 0x0854;      // sprmCFImprint
inline constexpr uint16_t kSprmCFObj = 0x0856;          // sprmCFObj
inline constexpr uint16_t kSprmCFEmboss = 0x0858;       // sprmCFEmboss
inline constexpr uint16_t kSprmCFBiDi = 0x085A;         // sprmCFBiDi
inline constexpr uint16_t kSprmCFBoldBi = 0x085C;       // sprmCFBoldBi
inline constexpr uint16_t kSprmCFItalicBi = 0x085D;     // sprmCFItalicBi
inline constexpr uint16_t kSprmCFUsePgsuSettings =
    0x0868;                                         // sprmCFUsePgsuSettings
inline constexpr uint16_t kSprmCFNoProof = 0x0875;  // sprmCFNoProof
inline constexpr uint16_t kSprmCFComplexScripts =
    0x0882;                                            // sprmCFComplexScripts
inline constexpr uint16_t kSprmPFInTable = 0x2416;     // sprmPFInTable
inline constexpr uint16_t kSprmPFTtp = 0x2417;         // sprmPFTtp
inline constexpr uint16_t kSprmPJc80 = 0x2403;         // sprmPJc80
inline constexpr uint16_t kSprmPFKeep = 0x2405;        // sprmPFKeep
inline constexpr uint16_t kSprmPFKeepFollow = 0x2406;  // sprmPFKeepFollow
inline constexpr uint16_t kSprmPFPageBreakBefore =
    0x2407;                                            // sprmPFPageBreakBefore
inline constexpr uint16_t kSprmPFNoLineNumb = 0x240C;  // sprmPFNoLineNumb
inline constexpr uint16_t kSprmPWr = 0x2423;           // sprmPWr
inline constexpr uint16_t kSprmPFNoAutoHyph = 0x242A;  // sprmPFNoAutoHyph
inline constexpr uint16_t kSprmPFLocked = 0x2430;      // sprmPFLocked
inline constexpr uint16_t kSprmPFWidowControl = 0x2431;   // sprmPFWidowControl
inline constexpr uint16_t kSprmPFKinsoku = 0x2433;        // sprmPFKinsoku
inline constexpr uint16_t kSprmPFWordWrap = 0x2434;       // sprmPFWordWrap
inline constexpr uint16_t kSprmPFOverflowPunct = 0x2435;  // sprmPFOverflowPunct
inline constexpr uint16_t kSprmPFTopLinePunct = 0x2436;   // sprmPFTopLinePunct
inline constexpr uint16_t kSprmPFAutoSpaceDE = 0x2437;    // sprmPFAutoSpaceDE
inline constexpr uint16_t kSprmPFAutoSpaceDN = 0x2438;    // sprmPFAutoSpaceDN
inline constexpr uint16_t kSprmPFBiDi = 0x2441;           // sprmPFBiDi
inline constexpr uint16_t kSprmPFUsePgsuSettings =
    0x2447;                                             // sprmPFUsePgsuSettings
inline constexpr uint16_t kSprmPFAdjustRight = 0x2448;  // sprmPFAdjustRight
inline constexpr uint16_t kSprmPFDyaBeforeAuto = 0x245B;  // sprmPFDyaBeforeAuto
inline constexpr uint16_t kSprmPFDyaAfterAuto = 0x245C;   // sprmPFDyaAfterAuto
inline constexpr uint16_t kSprmPJc = 0x2461;              // sprmPJc
inline constexpr uint16_t kSprmPFContextualSpacing =
    0x246D;  // sprmPFContextualSpacing
inline constexpr uint16_t kSprmPFMirrorIndents = 0x2470;  // sprmPFMirrorIndents
inline constexpr uint16_t kSprmPIncLvl = 0x2602;          // sprmPIncLvl
inline constexpr uint16_t kSprmPIlvl = 0x260A;            // sprmPIlvl
inline constexpr uint16_t kSprmPPc = 0x261B;              // sprmPPc
inline constexpr uint16_t kSprmPOutLvl = 0x2640;          // sprmPOutLvl
inline constexpr uint16_t kSprmCSfxText = 0x2859;         // sprmCSfxText
inline constexpr uint16_t kSprmCIdctHint = 0x286F;        // sprmCIdctHint
inline constexpr uint16_t kSprmCHighlight = 0x2A0C;       // sprmCHighlight
// Presence resets direct character formatting to the paragraph style.
inline constexpr uint16_t kSprmCPlain = 0x2A33;          // sprmCPlain
inline constexpr uint16_t kSprmCKcd = 0x2A34;            // sprmCKcd
inline constexpr uint16_t kSprmCKul = 0x2A3E;            // sprmCKul
inline constexpr uint16_t kSprmCIco = 0x2A42;            // sprmCIco
inline constexpr uint16_t kSprmCIss = 0x2A48;            // sprmCIss
inline constexpr uint16_t kSprmCFDStrike = 0x2A53;       // sprmCFDStrike
inline constexpr uint16_t kSprmCFSdtVanish = 0x2A90;     // sprmCFSdtVanish
inline constexpr uint16_t kSprmScnsPgn = 0x3000;         // sprmScnsPgn
inline constexpr uint16_t kSprmSiHeadingPgn = 0x3001;    // sprmSiHeadingPgn
inline constexpr uint16_t kSprmSFEvenlySpaced = 0x3005;  // sprmSFEvenlySpaced
inline constexpr uint16_t kSprmSBkc = 0x3009;            // sprmSBkc
inline constexpr uint16_t kSprmSFTitlePage = 0x300A;     // sprmSFTitlePage
inline constexpr uint16_t kSprmSNfcPgn = 0x300E;         // sprmSNfcPgn
inline constexpr uint16_t kSprmSFPgnRestart = 0x3011;    // sprmSFPgnRestart
inline constexpr uint16_t kSprmSFEndnote = 0x3012;       // sprmSFEndnote
inline constexpr uint16_t kSprmSLnc = 0x3013;            // sprmSLnc
inline constexpr uint16_t kSprmSLBetween = 0x3019;       // sprmSLBetween
inline constexpr uint16_t kSprmSVjc = 0x301A;            // sprmSVjc
inline constexpr uint16_t kSprmSBOrientation = 0x301D;   // sprmSBOrientation
inline constexpr uint16_t kSprmSFpc = 0x303B;            // sprmSFpc
inline constexpr uint16_t kSprmSRncFtn = 0x303C;         // sprmSRncFtn
inline constexpr uint16_t kSprmSRncEdn = 0x303E;         // sprmSRncEdn
inline constexpr uint16_t kSprmSFBiDi = 0x3228;          // sprmSFBiDi
inline constexpr uint16_t kSprmSFRTLGutter = 0x322A;     // sprmSFRTLGutter
inline constexpr uint16_t kSprmTFCantSplit90 = 0x3403;   // sprmTFCantSplit90
inline constexpr uint16_t kSprmTTableHeader = 0x3404;    // sprmTTableHeader
inline constexpr uint16_t kSprmTFNoAllowOverlap =
    0x3465;                                             // sprmTFNoAllowOverlap
inline constexpr uint16_t kSprmTFCantSplit = 0x3466;    // sprmTFCantSplit
inline constexpr uint16_t kSprmTFAutofit = 0x3615;      // sprmTFAutofit
inline constexpr uint16_t kSprmTFKeepFollow = 0x3619;   // sprmTFKeepFollow
inline constexpr uint16_t kSprmPWHeightAbs = 0x442B;    // sprmPWHeightAbs
inline constexpr uint16_t kSprmPShd80 = 0x442D;         // sprmPShd80
inline constexpr uint16_t kSprmPIstd = 0x4600;          // sprmPIstd
inline constexpr uint16_t kSprmPIlfo = 0x460B;          // sprmPIlfo
inline constexpr uint16_t kSprmPNest80 = 0x4610;        // sprmPNest80
inline constexpr uint16_t kSprmPNest = 0x465F;          // sprmPNest
inline constexpr uint16_t kSprmCHpsPos = 0x4845;        // sprmCHpsPos
inline constexpr uint16_t kSprmCHpsKern = 0x484B;       // sprmCHpsKern
inline constexpr uint16_t kSprmCCharScale = 0x4852;     // sprmCCharScale
inline constexpr uint16_t kSprmCLidBi = 0x485F;         // sprmCLidBi
inline constexpr uint16_t kSprmCShd80 = 0x4866;         // sprmCShd80
inline constexpr uint16_t kSprmCRgLid0_80 = 0x486D;     // sprmCRgLid0_80
inline constexpr uint16_t kSprmCRgLid1_80 = 0x486E;     // sprmCRgLid1_80
inline constexpr uint16_t kSprmCRgLid0 = 0x4873;        // sprmCRgLid0
inline constexpr uint16_t kSprmCRgLid1 = 0x4874;        // sprmCRgLid1
inline constexpr uint16_t kSprmCIstd = 0x4A30;          // sprmCIstd
inline constexpr uint16_t kSprmCHps = 0x4A43;           // sprmCHps
inline constexpr uint16_t kSprmCRgFtc0 = 0x4A4F;        // sprmCRgFtc0
inline constexpr uint16_t kSprmCRgFtc1 = 0x4A50;        // sprmCRgFtc1
inline constexpr uint16_t kSprmCRgFtc2 = 0x4A51;        // sprmCRgFtc2
inline constexpr uint16_t kSprmCFtcBi = 0x4A5E;         // sprmCFtcBi
inline constexpr uint16_t kSprmCIcoBi = 0x4A60;         // sprmCIcoBi
inline constexpr uint16_t kSprmCHpsBi = 0x4A61;         // sprmCHpsBi
inline constexpr uint16_t kSprmSDmBinFirst = 0x5007;    // sprmSDmBinFirst
inline constexpr uint16_t kSprmSDmBinOther = 0x5008;    // sprmSDmBinOther
inline constexpr uint16_t kSprmSCcolumns = 0x500B;      // sprmSCcolumns
inline constexpr uint16_t kSprmSNLnnMod = 0x5015;       // sprmSNLnnMod
inline constexpr uint16_t kSprmSLnnMin = 0x501B;        // sprmSLnnMin
inline constexpr uint16_t kSprmSPgnStart97 = 0x501C;    // sprmSPgnStart97
inline constexpr uint16_t kSprmSClm = 0x5032;           // sprmSClm
inline constexpr uint16_t kSprmSTextFlow = 0x5033;      // sprmSTextFlow
inline constexpr uint16_t kSprmSNFtn = 0x503F;          // sprmSNFtn
inline constexpr uint16_t kSprmSNfcFtnRef = 0x5040;     // sprmSNfcFtnRef
inline constexpr uint16_t kSprmSNEdn = 0x5041;          // sprmSNEdn
inline constexpr uint16_t kSprmSNfcEdnRef = 0x5042;     // sprmSNfcEdnRef
inline constexpr uint16_t kSprmTJc90 = 0x5400;          // sprmTJc90
inline constexpr uint16_t kSprmTJc = 0x548A;            // sprmTJc
inline constexpr uint16_t kSprmTFBiDi = 0x560B;         // sprmTFBiDi
inline constexpr uint16_t kSprmTDelete = 0x5622;        // sprmTDelete
inline constexpr uint16_t kSprmTMerge = 0x5624;         // sprmTMerge
inline constexpr uint16_t kSprmTSplit = 0x5625;         // sprmTSplit
inline constexpr uint16_t kSprmTIstd = 0x563A;          // sprmTIstd
inline constexpr uint16_t kSprmTFBiDi90 = 0x5664;       // sprmTFBiDi90
inline constexpr uint16_t kSprmPDyaLine = 0x6412;       // sprmPDyaLine
inline constexpr uint16_t kSprmPBrcTop80 = 0x6424;      // sprmPBrcTop80
inline constexpr uint16_t kSprmPBrcBetween80 = 0x6428;  // sprmPBrcBetween80
inline constexpr uint16_t kSprmPBrcBar80 = 0x6629;      // sprmPBrcBar80
inline constexpr uint16_t kSprmCBrc80 = 0x6865;         // sprmCBrc80
inline constexpr uint16_t kSprmCCv = 0x6870;            // sprmCCv
inline constexpr uint16_t kSprmCCvUl = 0x6877;          // sprmCCvUl
inline constexpr uint16_t kSprmCPicLocation = 0x6A03;   // sprmCPicLocation
inline constexpr uint16_t kSprmCSymbol = 0x6A09;        // sprmCSymbol
inline constexpr uint16_t kSprmSBrcTop80 = 0x702B;      // sprmSBrcTop80
inline constexpr uint16_t kSprmSBrcRight80 = 0x702E;    // sprmSBrcRight80
inline constexpr uint16_t kSprmSDxtCharSpace = 0x7030;  // sprmSDxtCharSpace
inline constexpr uint16_t kSprmSPgnStart = 0x7044;      // sprmSPgnStart
inline constexpr uint16_t kSprmTInsert = 0x7621;        // sprmTInsert
inline constexpr uint16_t kSprmTDxaCol = 0x7623;        // sprmTDxaCol
inline constexpr uint16_t kSprmTTextFlow = 0x7629;      // sprmTTextFlow
inline constexpr uint16_t kSprmPDxaRight80 = 0x840E;    // sprmPDxaRight80
inline constexpr uint16_t kSprmPDxaLeft80 = 0x840F;     // sprmPDxaLeft80
inline constexpr uint16_t kSprmPDxaLeft180 = 0x8411;    // sprmPDxaLeft180
inline constexpr uint16_t kSprmPDxaAbs = 0x8418;        // sprmPDxaAbs
inline constexpr uint16_t kSprmPDyaAbs = 0x8419;        // sprmPDyaAbs
inline constexpr uint16_t kSprmPDxaWidth = 0x841A;      // sprmPDxaWidth
inline constexpr uint16_t kSprmPDyaFromText = 0x842E;   // sprmPDyaFromText
inline constexpr uint16_t kSprmPDxaFromText = 0x842F;   // sprmPDxaFromText
inline constexpr uint16_t kSprmPDxaRight = 0x845D;      // sprmPDxaRight
inline constexpr uint16_t kSprmPDxaLeft = 0x845E;       // sprmPDxaLeft
inline constexpr uint16_t kSprmPDxaLeft1 = 0x8460;      // sprmPDxaLeft1
inline constexpr uint16_t kSprmCDxaSpace = 0x8840;      // sprmCDxaSpace
inline constexpr uint16_t kSprmSDxaColumns = 0x900C;    // sprmSDxaColumns
inline constexpr uint16_t kSprmSDxaLnn = 0x9016;        // sprmSDxaLnn
inline constexpr uint16_t kSprmSDyaTop = 0x9023;        // sprmSDyaTop
inline constexpr uint16_t kSprmSDyaBottom = 0x9024;     // sprmSDyaBottom
inline constexpr uint16_t kSprmSDyaLinePitch = 0x9031;  // sprmSDyaLinePitch
inline constexpr uint16_t kSprmTDyaRowHeight = 0x9407;  // sprmTDyaRowHeight
inline constexpr uint16_t kSprmTDxaAbs = 0x940E;        // sprmTDxaAbs
inline constexpr uint16_t kSprmTDyaAbs = 0x940F;        // sprmTDyaAbs
inline constexpr uint16_t kSprmTDxaFromText = 0x9410;   // sprmTDxaFromText
inline constexpr uint16_t kSprmTDyaFromText = 0x9411;   // sprmTDyaFromText
inline constexpr uint16_t kSprmTDxaFromTextRight =
    0x941E;  // sprmTDxaFromTextRight
inline constexpr uint16_t kSprmTDyaFromTextBottom =
    0x941F;                                           // sprmTDyaFromTextBottom
inline constexpr uint16_t kSprmTDxaLeft = 0x9601;     // sprmTDxaLeft
inline constexpr uint16_t kSprmTDxaGapHalf = 0x9602;  // sprmTDxaGapHalf
inline constexpr uint16_t kSprmPDyaBefore = 0xA413;   // sprmPDyaBefore
inline constexpr uint16_t kSprmPDyaAfter = 0xA414;    // sprmPDyaAfter
inline constexpr uint16_t kSprmSDyaHdrTop = 0xB017;   // sprmSDyaHdrTop
inline constexpr uint16_t kSprmSDyaHdrBottom = 0xB018;    // sprmSDyaHdrBottom
inline constexpr uint16_t kSprmSXaPage = 0xB01F;          // sprmSXaPage
inline constexpr uint16_t kSprmSYaPage = 0xB020;          // sprmSYaPage
inline constexpr uint16_t kSprmSDxaLeft = 0xB021;         // sprmSDxaLeft
inline constexpr uint16_t kSprmSDxaRight = 0xB022;        // sprmSDxaRight
inline constexpr uint16_t kSprmSDzaGutter = 0xB025;       // sprmSDzaGutter
inline constexpr uint16_t kSprmPChgTabsPapx = 0xC60D;     // sprmPChgTabsPapx
inline constexpr uint16_t kSprmPChgTabs = 0xC615;         // sprmPChgTabs
inline constexpr uint16_t kSprmPShd = 0xC64D;             // sprmPShd
inline constexpr uint16_t kSprmPBrcTop = 0xC64E;          // sprmPBrcTop
inline constexpr uint16_t kSprmPBrcBar = 0xC653;          // sprmPBrcBar
inline constexpr uint16_t kSprmCShd = 0xCA71;             // sprmCShd
inline constexpr uint16_t kSprmCBrc = 0xCA72;             // sprmCBrc
inline constexpr uint16_t kSprmCPropRMark = 0xCA89;       // sprmCPropRMark
inline constexpr uint16_t kSprmSBrcTop = 0xD234;          // sprmSBrcTop
inline constexpr uint16_t kSprmSBrcRight = 0xD237;        // sprmSBrcRight
inline constexpr uint16_t kSprmTTableBorders80 = 0xD605;  // sprmTTableBorders80
inline constexpr uint16_t kSprmTDefTable = 0xD608;        // sprmTDefTable
// Obsolete [MS-DOC] §2.6.3 variant of sprmTDefTable that carries BRC10
// cell borders instead of TC80 structures.
inline constexpr uint16_t kSprmTDefTableObsolete = 0xD606;

inline constexpr uint16_t kSprmTDefTableShd80 = 0xD609;   // sprmTDefTableShd80
inline constexpr uint16_t kSprmTDefTableShd3rd = 0xD60C;  // sprmTDefTableShd3rd
inline constexpr uint16_t kSprmTDefTableShd = 0xD612;     // sprmTDefTableShd
inline constexpr uint16_t kSprmTTableBorders = 0xD613;    // sprmTTableBorders
inline constexpr uint16_t kSprmTDefTableShd2nd = 0xD616;  // sprmTDefTableShd2nd
inline constexpr uint16_t kSprmTSetBrc80 = 0xD620;        // sprmTSetBrc80
inline constexpr uint16_t kSprmTVertMerge = 0xD62B;       // sprmTVertMerge
inline constexpr uint16_t kSprmTVertAlign = 0xD62C;       // sprmTVertAlign
inline constexpr uint16_t kSprmTSetShd = 0xD62D;          // sprmTSetShd
inline constexpr uint16_t kSprmTSetShdOdd = 0xD62E;       // sprmTSetShdOdd
inline constexpr uint16_t kSprmTSetBrc = 0xD62F;          // sprmTSetBrc
inline constexpr uint16_t kSprmTCellPadding = 0xD632;     // sprmTCellPadding
inline constexpr uint16_t kSprmTCellSpacingDefault =
    0xD633;  // sprmTCellSpacingDefault
inline constexpr uint16_t kSprmTCellPaddingDefault =
    0xD634;                                          // sprmTCellPaddingDefault
inline constexpr uint16_t kSprmTCellWidth = 0xD635;  // sprmTCellWidth
inline constexpr uint16_t kSprmTFCellNoWrap = 0xD639;     // sprmTFCellNoWrap
inline constexpr uint16_t kSprmTCellFHideMark = 0xD642;   // sprmTCellFHideMark
inline constexpr uint16_t kSprmTSetShdTable = 0xD660;     // sprmTSetShdTable
inline constexpr uint16_t kSprmTDefTableShdRaw = 0xD670;  // sprmTDefTableShdRaw
inline constexpr uint16_t kSprmTDefTableShdRaw2nd =
    0xD671;  // sprmTDefTableShdRaw2nd
inline constexpr uint16_t kSprmTDefTableShdRaw3rd =
    0xD672;                                       // sprmTDefTableShdRaw3rd
inline constexpr uint16_t kSprmCFSpec = 0x0855;   // sprmCFSpec
inline constexpr uint16_t kSprmCLbcCRJ = 0x2879;  // sprmCLbcCRJ
inline constexpr uint16_t kSprmCNeedFontFixup = 0x2A86;  // sprmCNeedFontFixup
inline constexpr uint16_t kSprmCHresi = 0x484E;          // sprmCHresi
inline constexpr uint16_t kSprmCPbiGrf = 0x4888;         // sprmCPbiGrf
inline constexpr uint16_t kSprmCPbiIBullet = 0x6887;     // sprmCPbiIBullet
inline constexpr uint16_t kSprmCFitText = 0xCA76;        // sprmCFitText
inline constexpr uint16_t kSprmCFELayout = 0xCA78;       // sprmCFELayout
inline constexpr uint16_t kSprmCFMathPr = 0xC81A;        // sprmCFMathPr
inline constexpr uint16_t kSprmCIstdPermute = 0xCA31;    // sprmCIstdPermute
inline constexpr uint16_t kSprmCMajority = 0xCA47;       // sprmCMajority
inline constexpr uint16_t kSprmCCnf = 0xCA85;            // sprmCCnf
inline constexpr uint16_t kSprmPFInnerTableCell =
    0x244B;                                          // sprmPFInnerTableCell
inline constexpr uint16_t kSprmPFInnerTtp = 0x244C;  // sprmPFInnerTtp
inline constexpr uint16_t kSprmPFOpenTch = 0x245A;   // sprmPFOpenTch
inline constexpr uint16_t kSprmPFNoAllowOverlap =
    0x2462;                                              // sprmPFNoAllowOverlap
inline constexpr uint16_t kSprmPTtwo = 0x2471;           // sprmPTtwo
inline constexpr uint16_t kSprmPDcs = 0x442C;            // sprmPDcs
inline constexpr uint16_t kSprmPWAlignFont = 0x4439;     // sprmPWAlignFont
inline constexpr uint16_t kSprmPFrameTextFlow = 0x443A;  // sprmPFrameTextFlow
inline constexpr uint16_t kSprmPDxcRight = 0x4455;       // sprmPDxcRight
inline constexpr uint16_t kSprmPDxcLeft = 0x4456;        // sprmPDxcLeft
inline constexpr uint16_t kSprmPDxcLeft1 = 0x4457;       // sprmPDxcLeft1
inline constexpr uint16_t kSprmPDylBefore = 0x4458;      // sprmPDylBefore
inline constexpr uint16_t kSprmPDylAfter = 0x4459;       // sprmPDylAfter
inline constexpr uint16_t kSprmPIpgp = 0x6465;           // sprmPIpgp
inline constexpr uint16_t kSprmPItap = 0x6649;           // sprmPItap
inline constexpr uint16_t kSprmPDtap = 0x664A;           // sprmPDtap
inline constexpr uint16_t kSprmPIstdPermute = 0xC601;    // sprmPIstdPermute
inline constexpr uint16_t kSprmPCnf = 0xC666;            // sprmPCnf
inline constexpr uint16_t kSprmPTIstdInfo = 0xC66C;      // sprmPTIstdInfo
inline constexpr uint16_t kSprmSFProtected = 0x3006;     // sprmSFProtected
inline constexpr uint16_t kSprmSDmPaperReq = 0x5026;     // sprmSDmPaperReq
inline constexpr uint16_t kSprmSPgbProp = 0x522F;        // sprmSPgbProp
inline constexpr uint16_t kSprmTCellVertAlignStyle =
    0x347C;  // sprmTCellVertAlignStyle
inline constexpr uint16_t kSprmTCellNoWrapStyle =
    0x347D;                                            // sprmTCellNoWrapStyle
inline constexpr uint16_t kSprmTCHorzBands = 0x3488;   // sprmTCHorzBands
inline constexpr uint16_t kSprmTCVertBands = 0x3489;   // sprmTCVertBands
inline constexpr uint16_t kSprmTPc = 0x360D;           // sprmTPc
inline constexpr uint16_t kSprmTTlp = 0x740A;          // sprmTTlp
inline constexpr uint16_t kSprmTIpgp = 0x7469;         // sprmTIpgp
inline constexpr uint16_t kSprmTBrcTopCv = 0xD61A;     // sprmTBrcTopCv
inline constexpr uint16_t kSprmTBrcLeftCv = 0xD61B;    // sprmTBrcLeftCv
inline constexpr uint16_t kSprmTBrcBottomCv = 0xD61C;  // sprmTBrcBottomCv
inline constexpr uint16_t kSprmTBrcRightCv = 0xD61D;   // sprmTBrcRightCv
inline constexpr uint16_t kSprmTCellPaddingStyle =
    0xD63E;                                            // sprmTCellPaddingStyle
inline constexpr uint16_t kSprmTCellBrcType = 0xD662;  // sprmTCellBrcType
inline constexpr uint16_t kSprmTCnf = 0xD66A;          // sprmTCnf
inline constexpr uint16_t kSprmTCellBrcTopStyle =
    0xD47F;  // sprmTCellBrcTopStyle
inline constexpr uint16_t kSprmTCellBrcBottomStyle =
    0xD680;  // sprmTCellBrcBottomStyle
inline constexpr uint16_t kSprmTCellBrcLeftStyle =
    0xD681;  // sprmTCellBrcLeftStyle
inline constexpr uint16_t kSprmTCellBrcRightStyle =
    0xD682;  // sprmTCellBrcRightStyle
inline constexpr uint16_t kSprmTCellBrcInsideHStyle =
    0xD683;  // sprmTCellBrcInsideHStyle
inline constexpr uint16_t kSprmTCellBrcInsideVStyle =
    0xD684;  // sprmTCellBrcInsideVStyle
inline constexpr uint16_t kSprmTCellBrcTL2BRStyle =
    0xD685;  // sprmTCellBrcTL2BRStyle
inline constexpr uint16_t kSprmTCellBrcTR2BLStyle =
    0xD686;  // sprmTCellBrcTR2BLStyle
inline constexpr uint16_t kSprmTCellShdStyle = 0xD687;   // sprmTCellShdStyle
inline constexpr uint16_t kSprmTWidthBefore = 0xF617;    // sprmTWidthBefore
inline constexpr uint16_t kSprmTWidthAfter = 0xF618;     // sprmTWidthAfter
inline constexpr uint16_t kSprmSDxaColWidth = 0xF203;    // sprmSDxaColWidth
inline constexpr uint16_t kSprmSDxaColSpacing = 0xF204;  // sprmSDxaColSpacing
inline constexpr uint16_t kSprmTTableWidth = 0xF614;     // sprmTTableWidth
inline constexpr uint16_t kSprmTFitText = 0xF636;        // sprmTFitText
inline constexpr uint16_t kSprmTWidthIndent = 0xF661;    // sprmTWidthIndent

}  // namespace doccraft::doc::detail

#endif  // SRC_DOC_FORMAT_SPRM_CODES_H_
