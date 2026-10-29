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

TEST(M20Malformed, FibMissingClx) {
  DocSpec spec = TextSpec();
  spec.include_clx = false;
  ExpectRejected(Pack(doccraft::test::BuildDocStreams(spec)),
                 "missing CLX Pcdt");
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

TEST(M20Malformed, ClxOutsideTableStream) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  Put32(s.word, 158 + 33 * 8, 0x1000);
  ExpectRejected(Pack(s), "CLX outside table stream");
}

TEST(M20Malformed, ClxCompressedPieceOverrunsWord) {
  DocSpec spec;
  spec.text = {'A', '\r'};
  spec.pieces = {{0, 2, 0x30000, true, 0}};
  ExpectRejected(Pack(doccraft::test::BuildDocStreams(spec)),
                 "piece outside WordDocument");
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

TEST(M20Malformed, ClxMissingPcdt) {
  DocStreams s = doccraft::test::BuildDocStreams(TextSpec());
  s.table[ClxOffset(s)] = 0;
  ExpectRejected(Pack(s), "missing CLX Pcdt");
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
  const auto outcome =
      ConvertBytes(Pack(doccraft::test::BuildDocStreams(spec)));
  EXPECT_TRUE(outcome.ok) << outcome.error;
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
