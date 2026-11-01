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

// M20 malformed-input regression library. Every case starts from a valid
// synthetic Word 97 image, mutates one byte-level structure, and pins the
// rejection message. The invariant is the same as fuzz_test.cc: no crash, no
// exception, and an error from std::expected where the parser can localize
// the damage.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "doc/format.h"
#include "doc/format/formatted_helpers.h"
#include "doc/format/internal.h"
#include "doc/format/sprm.h"
#include "model/document.h"
#include "tests/fuzz_util.h"
#include "tests/m20_doc_util.h"

namespace {
using doccraft::test::ConvertBytes;
using doccraft::test::DocSpec;
using doccraft::test::DocStreams;
using doccraft::test::Get16;
using doccraft::test::Get32;
using doccraft::test::Prl;
using doccraft::test::Put16;
using doccraft::test::Put32;

constexpr uint16_t kSprmPJc = 0x2401;
constexpr uint16_t kSprmPItap = 0x6649;
constexpr uint16_t kSprmPHugePapx = 0x6646;

DocSpec TextSpec(const std::string& text = "Hi\r") {
  DocSpec spec;
  spec.text.assign(text.begin(), text.end());
  return spec;
}

DocSpec FkpSpec() {
  DocSpec spec = TextSpec();
  spec.papx.push_back({2, Prl(kSprmPJc, {1})});
  return spec;
}

DocSpec ManyMarksSpec() {
  DocSpec spec;
  for (int i = 0; i < 11; ++i) {
    const std::string line = "A\r";
    for (char c : line) {
      spec.text.push_back(static_cast<uint8_t>(c));
      if (c == '\r') {
        spec.papx.push_back(
            {static_cast<uint32_t>(spec.text.size() - 1), Prl(kSprmPJc, {1})});
      }
    }
  }
  return spec;
}

std::vector<uint8_t> Pack(const DocStreams& streams) {
  return doccraft::test::BuildCfb(streams);
}

size_t ClxOffset(const DocStreams& s) {
  return Get32(s.word, 154 + 33 * 8);
}

size_t BteOffset(const DocStreams& s) {
  return Get32(s.word, 154 + 13 * 8);
}

size_t BteSize(const DocStreams& s) {
  return Get32(s.word, 158 + 13 * 8);
}

size_t FkpPages(const DocStreams& s) {
  return (BteSize(s) - 4) / 8;
}

uint32_t FkpPageNumber(const DocStreams& s, size_t index) {
  const size_t pages = FkpPages(s);
  return Get32(s.table, BteOffset(s) + 4 * (pages + 1) + 4 * index);
}

void SetFkpPageNumber(DocStreams* s, size_t index, uint32_t page) {
  const size_t pages = FkpPages(*s);
  Put32(s->table, BteOffset(*s) + 4 * (pages + 1) + 4 * index, page);
}

void ExpectRejected(const std::vector<uint8_t>& file, const char* keyword) {
  const auto outcome = ConvertBytes(file);
  EXPECT_FALSE(outcome.ok) << "expected rejection, converted";
  EXPECT_NE(outcome.error.find(keyword), std::string::npos)
      << "error: " << outcome.error;
}

// ---------------------------------------------------------------------------
// FIB
// ---------------------------------------------------------------------------

TEST(M20Malformed, FibInvalidSignature) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 0, 0x0000);
  ExpectRejected(Pack(s), "invalid WordDocument signature");
}

TEST(M20Malformed, FibVersionTooOld) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 2, 0x00B0);
  ExpectRejected(Pack(s), "unsupported nFib version");
}

TEST(M20Malformed, FibVersionTooNew) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 2, 0x0200);
  ExpectRejected(Pack(s), "unsupported nFib version");
}

TEST(M20Malformed, FibTruncatedBase) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  s.word.resize(20);
  ExpectRejected(Pack(s), "truncated FIB base");
}

TEST(M20Malformed, FibTruncatedRgLw) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 62, 0xFFFF);
  ExpectRejected(Pack(s), "truncated FIB cslw");
}

TEST(M20Malformed, FibTruncatedPairs) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 152, 0xFFFF);
  ExpectRejected(Pack(s), "truncated FIB fc/lcb pairs");
}

TEST(M20Malformed, FibShortFcLcbFallsBackToContiguousText) {
  // LibreOffice sw/qa ofz46457-1/ofz53457-1 declare only 15 fc/lcb pairs, so
  // fcClx is absent. The rows that are present stay usable and the text falls
  // back to the contiguous fcMin..fcMac piece instead of rejecting.
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 152, 15);  // cbRgFcLcb too short to reach row 33 (fcClx)
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  EXPECT_EQ(fib->fc_clx, 0u);
  EXPECT_EQ(fib->lcb_clx, 0u);
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_EQ(text, "Hi");
  bool warned = false;
  for (const auto& warning : document->warnings)
    warned |= warning.find("missing CLX Pcdt") != std::string::npos;
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, FibRc4Encrypted) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 10, static_cast<uint16_t>(Get16(s.word, 10) | 0x0100));
  ExpectRejected(Pack(s), "truncated RC4 encryption header");
}

TEST(M20Malformed, FibXorEncrypted) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put16(s.word, 10, static_cast<uint16_t>(Get16(s.word, 10) | 0x8100));
  ExpectRejected(Pack(s), "not an RC4 encrypted document");
}

TEST(M20Malformed, FibMissingClxFallsBackToContiguousText) {
  // With no CLX at all there is no piece table to read. The text still lives
  // contiguously from fcMin to fcMac, so read it as one implicit piece and
  // warn instead of rejecting the document.
  DocSpec spec = TextSpec();
  spec.include_clx = false;
  const DocStreams s = doccraft::test::BuildDocStreams(spec);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_EQ(text, "Hi");
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("missing CLX Pcdt") != std::string::npos &&
              warning.find("contiguous") != std::string::npos;
  }
  EXPECT_TRUE(warned) << (document->warnings.empty()
                              ? ""
                              : document->warnings.front());
}

TEST(M20Malformed, FibFcMinBelowHeaderIsIgnored) {
  // Modern Word 97 text addressing is CLX-authoritative: fcMin is not used
  // when a Pcdt is present, so a bogus value must not break the conversion.
  DocSpec spec = TextSpec();
  spec.fc_min = 4;
  const DocStreams s = doccraft::test::BuildDocStreams(spec);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  // The CLX is authoritative: the covered text must survive the bogus fcMin.
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_NE(text.find("Hi"), std::string::npos) << text;
}

TEST(M20Malformed, FibCcpTextBeyondPieceCoverage) {
  // ccpText claims 100000 characters while the CLX covers three. The reader
  // emits the covered text and records a coverage warning instead of reading
  // past the piece table.
  DocSpec spec = TextSpec();
  spec.ccp_override = 100000;
  spec.complex = false;  // the coverage warning is for non-complex files
  spec.pieces = {{0, 3, 0, true, 0}};
  const DocStreams s = doccraft::test::BuildDocStreams(spec);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("piece coverage short") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

// ---------------------------------------------------------------------------
// CLX / piece table
// ---------------------------------------------------------------------------

TEST(M20Malformed, ClxPieceCpDescending) {
  DocSpec spec;
  spec.text = {'A', 'B', 'C', '\r'};
  spec.pieces = {{1, 3, 1, true, 0}, {0, 1, 0, true, 0}};
  ExpectRejected(Pack(doccraft::test::BuildDocStreams(spec)),
                 "invalid piece CP order");
}

TEST(M20Malformed, ClxPieceCpEqual) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  const size_t clx = ClxOffset(s);
  Put32(s.table, clx + 9, 0);  // second CP equals the first CP
  ExpectRejected(Pack(s), "invalid piece CP order");
}

TEST(M20Malformed, ClxPcdtSizeTooLarge) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  const size_t clx = ClxOffset(s);
  Put32(s.table, clx + 1, 20);  // declared 20, actual 16
  ExpectRejected(Pack(s), "invalid PlcPcd size");
}

TEST(M20Malformed, ClxPcdtSizeTooSmall) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  const size_t clx = ClxOffset(s);
  Put32(s.table, clx + 1, 12);  // declared 12, actual 16
  ExpectRejected(Pack(s), "invalid PlcPcd size");
}

TEST(M20Malformed, ClxOutsideTableStreamFallsBackToContiguousText) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put32(s.word, 158 + 33 * 8, 0x1000);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("CLX outside table stream") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, ClxCompressedPieceOverrunsWordIsSkipped) {
  DocSpec spec;
  spec.text = {'A', '\r'};
  spec.pieces = {{0, 2, 0x30000, true, 0}};
  const DocStreams s = doccraft::test::BuildDocStreams(spec);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("piece 0") != std::string::npos &&
              warning.find("outside WordDocument") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, ClxDropsOutOfRangePieceAndKeepsTheRest) {
  // ofz18534-1 shape: five pieces where piece 1 points far outside
  // WordDocument. The other pieces still form the text (CP 256..512 is a
  // hole) and the drop is reported.
  DocSpec spec;
  spec.text = {'O', 'K', '\r'};
  spec.pieces = {
      {0, 2, 0, true, 0}, {2, 4, 0x30000, true, 0}, {4, 5, 2, true, 0}};
  const DocStreams s = doccraft::test::BuildDocStreams(spec);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_EQ(text, "OK");
  ASSERT_FALSE(document->warnings.empty());
  EXPECT_NE(document->warnings.front().find("piece 1"), std::string::npos);
  EXPECT_NE(document->warnings.front().find("outside WordDocument"),
            std::string::npos);
}

TEST(M20Malformed, ClxAllPiecesOutsideWordDocumentWarnsAboutEmptyText) {
  // The degraded piece table drops every piece, but ccpText still claims a
  // non-empty body. Returning an empty document silently would hide the loss.
  DocSpec spec;
  spec.text = {'A', '\r'};
  spec.pieces = {{0, 2, 0x30000, true, 0}};
  const DocStreams s = doccraft::test::BuildDocStreams(spec);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("no text extracted for a non-empty body") !=
                  std::string::npos &&
              warning.find("ccpText = 2") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, ClxMissingPcdtFallsBackToContiguousText) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  s.table[ClxOffset(s)] = 0;
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_EQ(text, "Hi");
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("missing CLX Pcdt") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, ClxFallbackReadsUtf16WhenExtCharIsSet) {
  // The ofz19065 shape: fExtChar is set and the fallback range is 16-bit.
  DocSpec spec;
  spec.text = {'A', 0, 'B', 0, '\r', 0};
  spec.ccp_override = 3;
  spec.include_clx = false;
  DocStreams s = doccraft::test::BuildDocStreams(spec);
  Put16(s.word, 10, static_cast<uint16_t>(Get16(s.word, 10) | 0x1000));
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_EQ(text, "AB");
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("16-bit") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, ClxPrcSizeOverflow) {
  DocSpec spec = TextSpec();
  spec.prcs.push_back({0x35, 0x08, 0x01, 0x00});
  DocStreams s = doccraft::test::BuildDocStreams(spec);
  Put16(s.table, ClxOffset(s) + 1, 0x3FA3);
  ExpectRejected(Pack(s), "invalid CLX Prc size");
}

TEST(M20Malformed, ClxPrmIndexOutsidePrcArray) {
  DocSpec spec = TextSpec();
  spec.pieces = {{0, 3, 0, true, 3}};  // prm bit 0 set, index 1, no Prc
  ExpectRejected(Pack(doccraft::test::BuildDocStreams(spec)),
                 "CLX Prm1 index outside Prc array");
}

TEST(M20Malformed, ClxPieceOverCoversWord) {
  DocSpec spec;
  spec.text = {'A', '\r'};
  spec.ccp_override = 10000;
  spec.pieces = {{0, 10000, 0, true, 0}};
  ExpectRejected(Pack(doccraft::test::BuildDocStreams(spec)),
                 "piece table over-covers WordDocument");
}

// ---------------------------------------------------------------------------
// r50 document-layer degradation: one damaged structure loses its own
// formatting/attachment, never the body.
// ---------------------------------------------------------------------------

TEST(M20Malformed, SectionSeparatorOutsideWordDocumentUsesDefaults) {
  // ofz18414-1 shape: the WordDocument prefix was salvaged, so the SEPX that
  // sat past the readable end is gone. The section stays.
  DocSpec spec = TextSpec();
  spec.section_ends = {3};
  DocStreams s = doccraft::test::BuildDocStreams(spec);
  const size_t sed = Get32(s.word, 154 + 6 * 8);
  Put32(s.table, sed + 4 * 2 + 2, 0x1000);  // fcSepx past WordDocument
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("SEPX for section 0") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, CorruptMainFieldPlcWarnsAndKeepsBody) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  const size_t at = doccraft::test::AlignUp(s.table.size(), 4);
  s.table.resize(at, 0);
  Put32(s.table, at, 0);           // first CP
  Put32(s.table, at + 4, 100000);  // far past ccpText
  s.table.push_back(0);            // one 2-byte grffldEnd entry
  s.table.push_back(0);
  Put32(s.word, 154 + 16 * 8, static_cast<uint32_t>(at));
  Put32(s.word, 158 + 16 * 8, 10);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_EQ(text, "Hi");
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find("skipped main field PLC") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, UnreadableHugePapxKeepsTableRow) {
  doccraft::model::Table table;
  std::vector<doccraft::model::Cell> row(1);
  doccraft::model::Paragraph para;
  doccraft::doc::detail::FormatRange pap;
  pap.first = 0;
  pap.last = 10;
  pap.sprms = Prl(0x6646, {0, 0, 0, 0});        // nested PAPX at Data offset 0
  const std::vector<uint8_t> data{0xFF, 0xFF};  // length 65535, no payload
  std::vector<std::string> warnings;
  auto done = doccraft::doc::FinishTableRow(
      table, row, para, &pap, doccraft::doc::detail::Bytes(data), &warnings);
  ASSERT_TRUE(done) << done.error().message;
  EXPECT_EQ(table.rows.size(), 1u);
  bool warned = false;
  for (const auto& warning : warnings) {
    warned |=
        warning.find("table row properties unreadable") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

// A TAPX that becomes unreadable halfway must not keep the sprms applied
// before the bad pointer: the row falls back to defaults as one unit.
TEST(M20Malformed, UnreadableHugePapxDoesNotKeepPartialTableProperties) {
  doccraft::model::Table table;
  table.columns = {1000};
  std::vector<doccraft::model::Cell> row(1);
  doccraft::model::Paragraph para;
  doccraft::doc::detail::FormatRange pap;
  pap.first = 0;
  pap.last = 10;
  // TDefTable: cb 26, one 4000-twip column and one zeroed TC80 cell.
  std::vector<uint8_t> def(27, 0);
  def[0] = 26;
  def[2] = 1;
  def[5] = 0xA0;
  def[6] = 0x0F;
  pap.sprms = Prl(0xD608, def);
  const auto huge = Prl(0x6646, {0, 0, 0, 0});
  pap.sprms.insert(pap.sprms.end(), huge.begin(), huge.end());
  const std::vector<uint8_t> data{0xFF, 0xFF};  // huge PAPX has no payload
  std::vector<std::string> warnings;
  auto done = doccraft::doc::FinishTableRow(
      table, row, para, &pap, doccraft::doc::detail::Bytes(data), &warnings);
  ASSERT_TRUE(done) << done.error().message;
  EXPECT_EQ(table.columns, std::vector<int>({1000}));
  ASSERT_EQ(table.row_properties.size(), 1u);
  EXPECT_EQ(table.row_properties[0].columns, std::vector<int>({1000}));
}

// An invalid TAPX operand keeps its rejection priority even when a later
// sprmPHugePapx pointer is unreadable. Hoping the row "defaults to usable"
// would silently accept a malformed operand.
TEST(M20Malformed, InvalidTapxOperandOutranksUnreadableHugePapx) {
  doccraft::model::Table table;
  std::vector<doccraft::model::Cell> row(1);
  doccraft::model::Paragraph para;
  doccraft::doc::detail::FormatRange pap;
  pap.first = 0;
  pap.last = 10;
  // TDefTable claims 255 cells but the operand cannot hold them.
  std::vector<uint8_t> bad(10, 0);
  bad[0] = 9;
  bad[2] = 255;
  pap.sprms = Prl(0xD608, bad);
  const auto huge = Prl(0x6646, {0, 0, 0, 0});
  pap.sprms.insert(pap.sprms.end(), huge.begin(), huge.end());
  const std::vector<uint8_t> data{0xFF, 0xFF};
  std::vector<std::string> warnings;
  auto done = doccraft::doc::FinishTableRow(
      table, row, para, &pap, doccraft::doc::detail::Bytes(data), &warnings);
  ASSERT_FALSE(done);
  EXPECT_NE(done.error().message.find("invalid TAPX operand"),
            std::string::npos)
      << done.error().message;
}

TEST(M20Malformed, CellWidthRangeOutsideRowIsIgnored) {
  doccraft::model::Table table;
  doccraft::model::RowProperties row;
  row.cells.resize(6);
  // ofz19065: itcFirst 65, itcLim 3, fts 0, dxa 0.
  const std::vector<uint8_t> v{5, 0x41, 3, 0, 0, 0};
  std::vector<std::string> warnings;
  EXPECT_TRUE(doccraft::doc::detail::ApplyTable(
      table, row, 0xD635, doccraft::doc::detail::Bytes(v), &warnings));
  EXPECT_EQ(row.cells[0].width_type, 3);
  bool warned = false;
  for (const auto& warning : warnings) {
    warned |= warning.find("sprmTCellWidth") != std::string::npos &&
              warning.find("65..3") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

TEST(M20Malformed, BteRangePastWordDocumentIsDropped) {
  std::vector<uint8_t> word(1024, 0);
  std::vector<uint8_t> table(12, 0);
  Put32(table, 0, 0);       // first FC
  Put32(table, 4, 100000);  // last FC past the 1024-byte stream
  Put32(table, 8, 0);       // page 0
  std::vector<std::string> warnings;
  auto ranges = doccraft::doc::detail::ReadFkps(word, table, {0, 12}, true,
                                                &warnings, false);
  ASSERT_TRUE(ranges) << ranges.error().message;
  EXPECT_TRUE(ranges->empty());
  bool warned = false;
  for (const auto& warning : warnings) {
    warned |=
        warning.find("FC range outside WordDocument") != std::string::npos;
  }
  EXPECT_TRUE(warned);
}

// ---------------------------------------------------------------------------
// FKP
// ---------------------------------------------------------------------------
// Structural FKP damage below the sprm stream is deliberately degraded: the
// page is skipped with a warning and the body survives (KNOWN-SIMPLIFICATIONS
// "degraded FKP"). These cases pin that policy instead of an error.
void ExpectDegradedFkp(const DocStreams& s, const char* keyword) {
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  bool warned = false;
  for (const auto& warning : document->warnings) {
    warned |= warning.find(keyword) != std::string::npos;
  }
  EXPECT_TRUE(warned) << "missing warning containing: " << keyword;
}

TEST(M20Malformed, FkpFcDescendingSkipsPage) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  const size_t page = static_cast<size_t>(FkpPageNumber(s, 0)) * 512;
  Put32(s.word, page, 0xFFFFFFF0u);
  ExpectDegradedFkp(s, "invalid FKP FC order");
}

TEST(M20Malformed, FkpRunCountTooLargeSkipsPage) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  const size_t page = static_cast<size_t>(FkpPageNumber(s, 0)) * 512;
  s.word[page + 511] = 30;
  ExpectDegradedFkp(s, "invalid FKP run count");
}

TEST(M20Malformed, FkpRunCountZeroSkipsPage) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  const size_t page = static_cast<size_t>(FkpPageNumber(s, 0)) * 512;
  s.word[page + 511] = 0;
  ExpectDegradedFkp(s, "invalid FKP run count");
}

TEST(M20Malformed, FkpPapxLengthOverflowsPageSkipsPage) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  const size_t page = static_cast<size_t>(FkpPageNumber(s, 0)) * 512;
  const size_t offset = 2 * s.word[page + 8];
  s.word[page + offset] = 0xFF;
  ExpectDegradedFkp(s, "invalid PAPX size");
}

TEST(M20Malformed, FkpPapxSprmTruncated) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  const size_t page = static_cast<size_t>(FkpPageNumber(s, 0)) * 512;
  const size_t offset = 2 * s.word[page + 8];
  s.word[page + offset] = 2;  // (2*2-1) = 3 bytes: istd + one opcode byte
  ExpectRejected(Pack(s), "invalid FKP sprm sequence");
}

TEST(M20Malformed, FkpDuplicatePageSkipsPage) {
  DocStreams s = doccraft::test::BuildDocStreams(ManyMarksSpec());
  ASSERT_EQ(FkpPages(s), 2u);
  SetFkpPageNumber(&s, 1, FkpPageNumber(s, 0));
  ExpectDegradedFkp(s, "duplicate FKP page");
}

TEST(M20Malformed, FkpPageOutsideWordDocumentSkipsPage) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  SetFkpPageNumber(&s, 0, 9999);
  ExpectDegradedFkp(s, "FKP page outside WordDocument");
}

TEST(M20Malformed, FkpBteSizeInvalid) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  Put32(s.word, 158 + 13 * 8, 10);
  ExpectRejected(Pack(s), "invalid BTE table");
}

TEST(M20Malformed, FkpBtePageCountExceedsStream) {
  DocStreams s = doccraft::test::BuildDocStreams(FkpSpec());
  const size_t offset = BteOffset(s);
  const uint32_t pages = static_cast<uint32_t>(s.word.size() / 512) + 4;
  const uint32_t size = 4 * (pages + 1) + 4 * pages;
  Put32(s.word, 158 + 13 * 8, size);
  s.table.resize(offset + size, 0);
  ExpectRejected(Pack(s), "BTE page count exceeds WordDocument");
}

// ---------------------------------------------------------------------------
// sprm
// ---------------------------------------------------------------------------

TEST(M20Malformed, SprmOperandTruncated) {
  DocSpec spec = TextSpec();
  spec.papx.push_back({2, Prl(kSprmPItap, {1, 0})});  // spra 3 needs 4 bytes
  ExpectRejected(Pack(doccraft::test::BuildDocStreams(spec)),
                 "invalid FKP sprm sequence");
}

TEST(M20Malformed, SprmKnownOpcodeShortOperand) {
  DocSpec spec = TextSpec();
  spec.papx.push_back({2, {0x49, 0x66}});  // sprmPItap with no operand
  ExpectRejected(Pack(doccraft::test::BuildDocStreams(spec)),
                 "invalid FKP sprm sequence");
}

TEST(M20Malformed, SprmNestedDepthBombSurvives) {
  // sprmPHugePapx points at a Data grpprl that points at itself. WalkParagraph
  // stops at depth 4; the malformed reference is ignored instead of looping.
  DocSpec spec = TextSpec();
  spec.data.assign(8, 0);
  Put16(spec.data, 0, 6);
  const auto inner = Prl(kSprmPHugePapx, {0, 0, 0, 0});
  std::copy(inner.begin(), inner.end(), spec.data.begin() + 2);
  spec.papx.push_back({2, Prl(kSprmPHugePapx, {0, 0, 0, 0})});
  const DocStreams s = doccraft::test::BuildDocStreams(spec);
  auto fib = doccraft::doc::ParseFib(s.word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto document = doccraft::doc::ParseFormatted(s.word, s.table, *fib, s.data);
  ASSERT_TRUE(document) << document.error().message;
  // The self-referencing grpprl must be ignored, not drop the paragraph text.
  std::string text;
  for (const auto& para : document->paragraphs)
    for (const auto& run : para.runs)
      text += run.text;
  EXPECT_NE(text.find("Hi"), std::string::npos) << text;
}

// ---------------------------------------------------------------------------
// Pictures and OLE payloads (byte-level API entry points)
// ---------------------------------------------------------------------------

std::vector<uint8_t> PicfWith(int width, int height) {
  std::vector<uint8_t> data(68, 0);
  Put32(data, 0, static_cast<uint32_t>(data.size()));
  Put16(data, 4, 68);
  Put16(data, 28, static_cast<uint16_t>(width));
  Put16(data, 30, static_cast<uint16_t>(height));
  Put16(data, 32, 1000);
  Put16(data, 34, 1000);
  return data;
}

TEST(M20Malformed, PicturePicfNegativeWidth) {
  const auto data = PicfWith(0xFFF0, 720);
  const auto image = doccraft::doc::ReadInlinePicture(data, 0);
  ASSERT_FALSE(image);
  EXPECT_NE(image.error().message.find("invalid PICF dimensions"),
            std::string::npos)
      << image.error().message;
}

TEST(M20Malformed, PicturePicfNegativeHeight) {
  const auto data = PicfWith(720, 0xFFF0);
  const auto image = doccraft::doc::ReadInlinePicture(data, 0);
  ASSERT_FALSE(image);
  EXPECT_NE(image.error().message.find("invalid PICF dimensions"),
            std::string::npos)
      << image.error().message;
}

TEST(M20Malformed, PictureUnknownBlipType) {
  constexpr size_t kPicf = 68, kEscherHeader = 8, kFbse = 36, kUid = 16,
                   kTag = 1;
  std::vector<uint8_t> data(kPicf + kEscherHeader + kFbse + 8 + kUid + kTag +
                            8);
  Put32(data, 0, static_cast<uint32_t>(data.size()));
  Put16(data, 4, kPicf);
  Put16(data, 28, 1440);
  Put16(data, 30, 720);
  Put16(data, 32, 1000);
  Put16(data, 34, 1000);
  Put16(data, kPicf, 0x0062);  // OfficeArtFBSE
  Put16(data, kPicf + 2, 0xF007);
  Put32(data, kPicf + 4,
        static_cast<uint32_t>(data.size() - kPicf - kEscherHeader));
  const size_t blip = kPicf + kEscherHeader + kFbse;
  Put16(data, blip, 0x6E00);
  Put16(data, blip + 2, 0xF0FF);  // unknown BLIP record type
  Put32(data, blip + 4, static_cast<uint32_t>(kUid + kTag + 8));
  const auto image = doccraft::doc::ReadInlinePicture(data, 0);
  ASSERT_FALSE(image);
  EXPECT_NE(image.error().message.find("PICF has no supported embedded BLIP"),
            std::string::npos)
      << image.error().message;
}

TEST(M20Malformed, OlePresentationStreamTruncated) {
  const std::vector<uint8_t> stream(8, 0);
  const auto image = doccraft::doc::ReadOlePresentation(stream);
  ASSERT_FALSE(image);
  EXPECT_NE(image.error().message.find("truncated OLE presentation"),
            std::string::npos)
      << image.error().message;
}

}  // namespace
