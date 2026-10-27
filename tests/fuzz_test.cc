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

// M6 robustness mutation suite. Three surfaces, one invariant: every mutated
// input either converts or returns a std::expected error - never a crash,
// hang or exception. This is a deterministic in-process mutation harness, not
// a continuous fuzzer; see KNOWN-SIMPLIFICATIONS.md "M6 fuzz" for the gap.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "doc/fib.h"
#include "doc/format.h"
#include "doc/format/sprm.h"
#include "doc/format/sprm_codes.h"
#include "docx/writer.h"
#include "model/document.h"
#include "tests/fuzz_util.h"

namespace {
namespace doc = doccraft::doc;
namespace docx = doccraft::docx;
namespace model = doccraft::model;
using doccraft::test::ConvertBytes;
using doccraft::test::ConvertOutcome;
using doccraft::test::MutateRegion;
using doccraft::test::ReadSeed;

using doc::detail::kSprmCFBold;
using doc::detail::kSprmCHps;
using doc::detail::kSprmCIco;
using doc::detail::kSprmPDtap;
using doc::detail::kSprmPFInTable;
using doc::detail::kSprmPItap;
using doc::detail::kSprmPJc;
using doc::detail::kSprmTDefTable;

// sprmPHugePapx / sprmPTableProps: the operand is a 4-byte offset into the
// Data stream of a length-prefixed nested grpprl (WalkParagraph, sprm.h).
constexpr uint16_t kSprmPHugePapx = 0x6646;
constexpr uint16_t kSprmPTableProps = 0x646B;
// sprmTDefTable / sprmTDefTableObsolete carry a U16 cb instead of the usual
// one-byte cb of spra-6 operands.
constexpr uint16_t kSprmTDefTableObsolete = 0xD606;

// PlcPcd.Pcd.fc: bit 30 marks CP1252 ("compressed") text, bits 0-29 hold the
// WordDocument offset.
constexpr uint32_t kFcCompressedBit = 0x40000000u;

void Put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
  b[p] = static_cast<uint8_t>(v);
  b[p + 1] = static_cast<uint8_t>(v >> 8);
}

// Deepest table nesting reachable from one document-level table block.
int StructuralDepth(const model::Document& d, size_t table) {
  int deepest = 1;
  for (const auto& row : d.tables[table].rows)
    for (const auto& cell : row)
      for (const auto& block : cell.blocks)
        if (block.table)
          deepest = std::max(deepest, 1 + StructuralDepth(d, block.index));
  return deepest;
}

int BodyTableDepth(const model::Document& d) {
  int deepest = 0;
  for (const auto& block : d.blocks)
    if (block.table)
      deepest = std::max(deepest, StructuralDepth(d, block.index));
  return deepest;
}

void Put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  Put16(b, p, static_cast<uint16_t>(v));
  Put16(b, p + 2, static_cast<uint16_t>(v >> 16));
}

std::vector<uint8_t> Prl(uint16_t op, std::vector<uint8_t> value) {
  std::vector<uint8_t> result{static_cast<uint8_t>(op),
                              static_cast<uint8_t>(op >> 8)};
  result.insert(result.end(), value.begin(), value.end());
  return result;
}

// A minimal but complete WordDocument/Table/Data triple in the shape the
// m3_batch tests use: one compressed piece, one PAPX and one CHPX FKP page,
// a CLX with one Prc plus the piece table, and two nested grpprls in Data.
struct SyntheticDoc {
  std::vector<uint8_t> word, table, data;
  doc::Fib fib;
  // Byte ranges the directed mutators target (cb byte included, so operand
  // truncation is reachable).
  size_t papx_cb = 0, papx_begin = 0, papx_end = 0;
  size_t chpx_cb = 0, chpx_begin = 0, chpx_end = 0;
  size_t clx_begin = 0, clx_end = 0;
};

SyntheticDoc MakeSyntheticDoc(const std::string& text = "Fuzz!\r",
                              std::vector<uint8_t> extra_pap = {}) {
  SyntheticDoc d;
  d.word.assign(2048, 0);
  std::copy(text.begin(), text.end(), d.word.begin() + 1024);

  std::vector<uint8_t> pap = Prl(kSprmPJc, {1});
  auto nested = Prl(kSprmPHugePapx, {64, 0, 0, 0});
  pap.insert(pap.end(), nested.begin(), nested.end());
  // 0x8411 = sprmPDxaLeft1 (2-byte operand).
  const auto indent = Prl(0x8411, {0xD0, 0x02});
  pap.insert(pap.end(), indent.begin(), indent.end());
  pap.insert(pap.end(), extra_pap.begin(), extra_pap.end());
  // PAPX grpprls are odd-length: the cb byte counts 16-bit words, so an even
  // grpprl takes one zero alignment byte (Walk's trailing-byte rule). A
  // trailing PRL would not change the parity and cb*2-1-2 would then cut the
  // last byte mid-PRL.
  if ((pap.size() & 1) == 0)
    pap.push_back(0);
  d.word[8] = 20;   // BX offset / 2 -> PAPX at page offset 40
  d.word[511] = 1;  // one FKP run
  Put32(d.word, 0, 1024);
  Put32(d.word, 4, 1024 + text.size());
  d.word[40] = static_cast<uint8_t>((pap.size() + 3) / 2);
  Put16(d.word, 41, 0);  // istd
  std::copy(pap.begin(), pap.end(), d.word.begin() + 43);

  // CHPX: run flags, font size, palette colour and a one-cell TDefTable so
  // the table-definition decoder has a valid operand to mutate.
  std::vector<uint8_t> chp = Prl(kSprmCFBold, {1});
  const auto hps = Prl(kSprmCHps, {24, 0});
  chp.insert(chp.end(), hps.begin(), hps.end());
  const auto ico = Prl(kSprmCIco, {5});
  chp.insert(chp.end(), ico.begin(), ico.end());
  std::vector<uint8_t> def(27, 0);
  Put16(def, 0, static_cast<uint16_t>(def.size() - 1));  // cb = size - 1
  def[2] = 1;                                            // one cell
  Put16(def, 5, 1440);                                   // right boundary
  Put16(def, 9, 1440);                                   // cell width
  const auto table_def = Prl(kSprmTDefTable, def);
  chp.insert(chp.end(), table_def.begin(), table_def.end());
  d.word[512 + 8] = 20;
  d.word[512 + 511] = 1;
  Put32(d.word, 512, 1024);
  Put32(d.word, 516, 1024 + text.size());
  d.word[512 + 40] = static_cast<uint8_t>(chp.size());
  std::copy(chp.begin(), chp.end(), d.word.begin() + 512 + 41);

  d.table.assign(64, 0);
  // CLX: Prc(sprmCFBold on) + Pcdt with one compressed piece; the PCD prm
  // references the Prc, as a fast-saved document would.
  d.table[0] = 1;
  Put16(d.table, 1, 3);
  const auto prc = Prl(kSprmCFBold, {1});
  std::copy(prc.begin(), prc.end(), d.table.begin() + 3);
  d.table[6] = 2;
  Put32(d.table, 7, 16);
  Put32(d.table, 11, 0);
  Put32(d.table, 15, static_cast<uint32_t>(text.size()));
  Put16(d.table, 19, 0);
  Put32(d.table, 21, kFcCompressedBit | 2048);
  Put16(d.table, 25, 1);
  Put32(d.table, 32, 1024);  // PAPX BTE PLC
  Put32(d.table, 36, 1024 + static_cast<uint32_t>(text.size()));
  Put32(d.table, 40, 0);
  Put32(d.table, 48, 1024);  // CHPX BTE PLC
  Put32(d.table, 52, 1024 + static_cast<uint32_t>(text.size()));
  Put32(d.table, 56, 1);

  // Data: two length-prefixed grpprls the nested-sprm operand can point at.
  d.data.assign(256, 0);
  for (const size_t at : {size_t{0}, size_t{64}}) {
    Put16(d.data, at, 16);
    const auto inner = Prl(kSprmPFInTable, {1});
    std::copy(inner.begin(), inner.end(), d.data.begin() + at + 2);
    const auto depth = Prl(kSprmPItap, {1, 0, 0, 0});
    std::copy(depth.begin(), depth.end(), d.data.begin() + at + 5);
  }

  d.fib.ccp_text = static_cast<uint32_t>(text.size());
  d.fib.fc_clx = 0;
  d.fib.lcb_clx = 27;
  d.fib.papx_bte = {32, 12};
  d.fib.chpx_bte = {48, 12};
  d.papx_cb = 40;
  d.papx_begin = 43;
  d.papx_end = 43 + pap.size();
  d.chpx_cb = 512 + 40;
  d.chpx_begin = 512 + 41;
  d.chpx_end = 512 + 41 + chp.size();
  d.clx_begin = 0;
  d.clx_end = 27;
  return d;
}

// True when the synthetic document survives ParseFormatted and the docx
// writer. Failures are fine; crashes are not.
bool Converts(const SyntheticDoc& d, std::string* error = nullptr) {
  auto document = doc::ParseFormatted(d.word, d.table, d.fib, d.data);
  if (!document) {
    if (error)
      *error = document.error().message;
    return false;
  }
  auto zip = docx::Write(*document);
  if (!zip) {
    if (error)
      *error = "writer: " + zip.error().message;
    return false;
  }
  return true;
}

struct Tally {
  size_t rounds = 0, success = 0;
  std::map<std::string, size_t> stages;
  std::string Report() const {
    size_t errors = 0;
    std::string breakdown;
    for (const auto& [stage, n] : stages) {
      errors += n;
      breakdown += stage + "=" + std::to_string(n) + " ";
    }
    return "rounds=" + std::to_string(rounds) +
           " ok=" + std::to_string(success) + " err=" + std::to_string(errors) +
           " " + breakdown;
  }
};

// ---------------------------------------------------------------------------
// 1. Whole-pipeline seed mutation: representative corpus/fixture documents,
//    deterministic multi-byte mutations, the full convert pipeline per round.
// ---------------------------------------------------------------------------
#if defined(__SANITIZE_ADDRESS__)
#define DOCRAFT_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define DOCRAFT_TEST_ASAN 1
#endif
#endif

// Knobs of the seed-mutation wall-clock valve (registered in
// KNOWN-SIMPLIFICATIONS.md "M6 变异加固"): once the budget elapses, each seed
// stops at the next 16-round checkpoint instead of running to its full round
// count, so a slow machine finishes fewer rounds rather than hitting the
// ctest timeout. Every seed still runs its first kSeedMutationMinRounds
// rounds. ASan slows the pipeline ~8x, so its budget is scaled down instead
// of the coverage. To re-balance on slow CI, lower kSeedMutationRoundsAsan
// or raise kSeedMutationBudget.
#ifdef DOCRAFT_TEST_ASAN
constexpr size_t kSeedMutationRoundsAsan = 300;
#endif
constexpr size_t kSeedMutationRounds = 2000;
constexpr size_t kSeedMutationMinRounds = 64;
constexpr auto kSeedMutationBudget = std::chrono::seconds(8);

TEST(Fuzz, FullPipelineSeedMutationNeverCrashes) {
  const struct Seed {
    const char* path;
    const char* password;
  } seeds[] = {
      {"tests/corpus/corpus/plain-text.doc", ""},
      {"tests/corpus/corpus/complex-table.doc", ""},
      {"tests/fixtures/fast-saved-o-kurs.doc", ""},
      {"tests/fixtures/inline-image.doc", ""},
      {"tests/fixtures/encrypted/plain-text-rc4-cryptoapi.doc", "doccraft-m6"},
  };
#ifdef DOCRAFT_TEST_ASAN
  constexpr size_t kRoundsPerSeed = kSeedMutationRoundsAsan;
#else
  constexpr size_t kRoundsPerSeed = kSeedMutationRounds;
#endif
  const auto deadline = std::chrono::steady_clock::now() + kSeedMutationBudget;
  std::mt19937 rng(0x5EED1);
  for (const Seed& seed : seeds) {
    SCOPED_TRACE(seed.path);
    const std::vector<uint8_t> base =
        ReadSeed(std::string(DOCCRAFT_SOURCE_DIR) + "/" + seed.path);
    ASSERT_FALSE(base.empty());
    ASSERT_TRUE(ConvertBytes(base, seed.password).ok)
        << "unmutated seed must convert";

    Tally tally;
    for (; tally.rounds < kRoundsPerSeed; ++tally.rounds) {
      if (tally.rounds >= kSeedMutationMinRounds && (tally.rounds & 15) == 0 &&
          std::chrono::steady_clock::now() > deadline)
        break;
      std::vector<uint8_t> bytes = base;
      MutateRegion(rng, bytes, 0, bytes.size());
      ConvertOutcome outcome = ConvertBytes(bytes, seed.password);
      if (outcome.ok)
        ++tally.success;
      else
        ++tally.stages[outcome.stage];
    }
    EXPECT_GT(tally.rounds, 0u);
    std::printf("[ fuzz ] pipeline %s %s\n", seed.path, tally.Report().c_str());
  }
}

// ---------------------------------------------------------------------------
// 2. Directed sprm/FKP mutation: opcode bits, cb length bytes, truncated
//    operands and nested (Data-stream) grpprl references.
// ---------------------------------------------------------------------------
void BombardOpcodes(std::mt19937& rng, std::vector<uint8_t>& bytes,
                    size_t begin, size_t end) {
  if (begin + 2 > end)
    return;
  std::uniform_int_distribution<size_t> at(begin, end - 2);
  const size_t count = 1 + rng() % 16;
  for (size_t n = 0; n < count; ++n) {
    const size_t p = at(rng);
    bytes[p] = static_cast<uint8_t>(rng());
    bytes[p + 1] = static_cast<uint8_t>(rng());
    // Force every spra (bits 13-15), including the variable-length one.
    if (rng() % 2)
      bytes[p + 1] =
          static_cast<uint8_t>((bytes[p + 1] & 0x1F) | ((rng() % 8) << 5));
  }
}

void InjectNestedSprm(std::mt19937& rng, std::vector<uint8_t>& bytes,
                      size_t begin, size_t end) {
  if (begin + 6 > end)
    return;
  std::uniform_int_distribution<size_t> at(begin, end - 6);
  const size_t p = at(rng);
  const uint16_t op = rng() % 2 ? kSprmPHugePapx : kSprmPTableProps;
  bytes[p] = static_cast<uint8_t>(op);
  bytes[p + 1] = static_cast<uint8_t>(op >> 8);
  Put32(bytes, p + 2, rng());
}

TEST(Fuzz, DirectedSprmAndFkpMutationNeverCrashes) {
  const SyntheticDoc base = MakeSyntheticDoc();
  std::string error;
  ASSERT_TRUE(Converts(base, &error)) << error;
  constexpr size_t kRounds = 4000;
  std::mt19937 rng(0x5EED2);
  Tally tally;
  for (; tally.rounds < kRounds; ++tally.rounds) {
    SyntheticDoc d = base;
    switch (rng() % 4) {
      case 0:
        MutateRegion(rng, d.word, d.papx_cb, d.papx_end);
        break;
      case 1:
        MutateRegion(rng, d.word, d.chpx_cb, d.chpx_end);
        break;
      case 2:
        BombardOpcodes(rng, d.word, d.papx_begin, d.papx_end);
        BombardOpcodes(rng, d.word, d.chpx_begin, d.chpx_end);
        break;
      default:
        InjectNestedSprm(rng, d.word, d.papx_begin, d.papx_end);
        MutateRegion(rng, d.data, 0, d.data.size());
        break;
    }
    auto document = doc::ParseFormatted(d.word, d.table, d.fib, d.data);
    if (!document) {
      ++tally.stages["parse"];
      continue;
    }
    if (docx::Write(*document).has_value())
      ++tally.success;
    else
      ++tally.stages["writer"];
  }
  EXPECT_EQ(tally.rounds, kRounds);
  std::printf("[ fuzz ] sprm %s\n", tally.Report().c_str());
}

// ---------------------------------------------------------------------------
// 3. CLX / piece-table mutation: fc extremes, the compressed bit, CP order,
//    piece counts against ccpText, and the FIB window onto the CLX.
// ---------------------------------------------------------------------------
// Regression for the first bug the pipeline mutation round found: an
// sprmPItap operand of 0xFFFF built a 65535-level TableFrame stack whose
// folded nesting chain overflowed the writer's recursion (stack overflow /
// SIGSEGV). Depth policy is unified across both layers: the frame stack and
// the ApplyPara model field clamp through the same ClampTableDepth helper,
// so the built structure and the recorded paragraph depth must agree.
TEST(Fuzz, SprmItapDepthOperandIsClamped) {
  std::vector<uint8_t> extra = Prl(kSprmPFInTable, {1});
  // sprmPItap is spra 3: a full 4-byte operand, 0x0000FFFF = 65535.
  const auto itap = Prl(kSprmPItap, {0xFF, 0xFF, 0x00, 0x00});
  extra.insert(extra.end(), itap.begin(), itap.end());
  const SyntheticDoc d = MakeSyntheticDoc(std::string("A\x07"
                                                      "B\r",
                                                      4),
                                          extra);
  std::string error;
  EXPECT_TRUE(Converts(d, &error)) << error;

  auto document = doc::ParseFormatted(d.word, d.table, d.fib, d.data);
  ASSERT_TRUE(document);
  // Clamped to kMaxTableDepth, not dropped: the document still nests, but
  // never deeper than 8.
  EXPECT_EQ(BodyTableDepth(*document), 8);
  for (const auto& para : document->paragraphs)
    EXPECT_EQ(para.table_depth, 8) << "model field must match the structure";
}

// sprmPDtap is also spra 3 (4-byte signed): +7 from depth 3 must clamp the
// sum to 8. The pre-unification parse layer dropped the whole sprm instead,
// recording depth 3 while the frame stack built 8 levels.
TEST(Fuzz, SprmDtapSummedDepthIsClamped) {
  std::vector<uint8_t> extra = Prl(kSprmPFInTable, {1});
  const auto itap = Prl(kSprmPItap, {3, 0, 0, 0});
  const auto dtap = Prl(kSprmPDtap, {7, 0, 0, 0});
  extra.insert(extra.end(), itap.begin(), itap.end());
  extra.insert(extra.end(), dtap.begin(), dtap.end());
  const SyntheticDoc d = MakeSyntheticDoc(std::string("A\x07"
                                                      "B\r",
                                                      4),
                                          extra);
  std::string error;
  EXPECT_TRUE(Converts(d, &error)) << error;

  auto document = doc::ParseFormatted(d.word, d.table, d.fib, d.data);
  ASSERT_TRUE(document);
  EXPECT_EQ(BodyTableDepth(*document), 8);
  for (const auto& para : document->paragraphs)
    EXPECT_EQ(para.table_depth, 8) << "model field must match the structure";
}

TEST(Fuzz, ClxPieceTableMutationNeverCrashes) {
  const SyntheticDoc base = MakeSyntheticDoc();
  std::string error;
  ASSERT_TRUE(Converts(base, &error)) << error;
  constexpr size_t kRounds = 4000;
  std::mt19937 rng(0x5EED3);
  const uint32_t sizes[] = {0, 1, 4, 15, 16, 26, 27, 28, 40, 0x7FFFFFFFu};
  const uint32_t offsets[] = {0, 1, 26, 27, 40, 64, 1000};
  const uint32_t ccps[] = {0, 1, 5, 6, 7, 100, 0x7FFFFFFFu, 0xFFFFFFFFu};
  const uint16_t lids[] = {0x0409, 0x0419, 0x0411, 0xFFFF};
  auto pick32 = [&](const uint32_t* values, size_t count) {
    return rng() % 2 ? values[rng() % count] : static_cast<uint32_t>(rng());
  };
  Tally tally;
  for (; tally.rounds < kRounds; ++tally.rounds) {
    SyntheticDoc d = base;
    switch (rng() % 5) {
      case 0:
        MutateRegion(rng, d.table, d.clx_begin, d.clx_end);
        break;
      case 1:
        // Pcdt only: lcb, CP array and PCDs (fc, compressed bit, prm).
        MutateRegion(rng, d.table, 6, d.clx_end);
        break;
      case 2:
        d.fib.lcb_clx = pick32(sizes, std::size(sizes));
        break;
      case 3:
        d.fib.fc_clx = pick32(offsets, std::size(offsets));
        break;
      default:
        d.fib.ccp_text = pick32(ccps, std::size(ccps));
        d.fib.lid = lids[rng() % std::size(lids)];
        break;
    }
    auto document = doc::ParseFormatted(d.word, d.table, d.fib);
    if (!document) {
      ++tally.stages["parse"];
      continue;
    }
    if (docx::Write(*document).has_value())
      ++tally.success;
    else
      ++tally.stages["writer"];
  }
  EXPECT_EQ(tally.rounds, kRounds);
  std::printf("[ fuzz ] clx %s\n", tally.Report().c_str());
}

// ---------------------------------------------------------------------------
// 4. Direct sprm dispatch: every 16-bit opcode with a Walk-legal operand
//    (the only operand shapes the grpprl walker can produce) applied to a
//    fresh IR object, then written out.
// ---------------------------------------------------------------------------
std::vector<uint8_t> WalkLegalOperand(uint16_t op, std::mt19937& rng) {
  size_t len;
  switch (op >> 13) {
    case 0:
    case 1:
      len = 1;
      break;
    case 2:
    case 4:
    case 5:
      len = 2;
      break;
    case 3:
      len = 4;
      break;
    case 7:
      len = 3;
      break;
    default: {  // spra 6: cb is one byte, U16 for the TDefTable pair
      if (op == kSprmTDefTable || op == kSprmTDefTableObsolete) {
        std::vector<uint8_t> v(1 + rng() % 64, 0);
        for (uint8_t& b : v)
          b = static_cast<uint8_t>(rng());
        Put16(v, 0, static_cast<uint16_t>(v.size() - 1));
        return v;
      }
      len = 1 + rng() % 64;
      break;
    }
  }
  std::vector<uint8_t> v(len, 0);
  for (uint8_t& b : v)
    b = static_cast<uint8_t>(rng());
  return v;
}

TEST(Fuzz, SprmDispatchOnEveryOpcodeNeverCrashes) {
  constexpr uint32_t kExtraRounds = 20000;
  std::mt19937 rng(0x5EED4);
  size_t writes = 0;
  for (uint32_t op = 0; op <= 0xFFFF; ++op) {
    const std::vector<uint8_t> v =
        WalkLegalOperand(static_cast<uint16_t>(op), rng);
    model::Run run;
    run.text = "X";
    model::Paragraph para;
    model::Section section;
    model::Table table;
    model::RowProperties row;
    row.cells.resize(2);
    model::Style style;
    doc::detail::ApplyRun(run, op, v, {"Fixture Font"});
    doc::detail::ApplyPara(para, op, v);
    doc::detail::ApplySection(section, op, v);
    doc::detail::ApplyTable(table, row, op, v);
    doc::detail::ApplyConditional(style, op, v, {"Fixture Font"});
    if (op == 0xFFFF)
      break;
    // Periodically push the accumulated IR through the writer so mutated
    // property values meet the XML writer too.
    if (op % 512 == 511) {
      para.runs.push_back(run);
      model::Document document;
      document.paragraphs.push_back(para);
      document.section = section;
      (void)docx::Write(document);
      ++writes;
    }
  }
  for (uint32_t n = 0; n < kExtraRounds; ++n) {
    // Random (op, operand) pairs beyond the sequential sweep, biased toward
    // table opcodes (sgc 5), where the size-checking burden is highest.
    uint16_t op = static_cast<uint16_t>(rng());
    if (rng() % 2)
      op = static_cast<uint16_t>((op & 0x1FFF) | (5 << 10) |
                                 ((rng() % 8) << 13));
    const std::vector<uint8_t> v = WalkLegalOperand(op, rng);
    model::Table table;
    model::RowProperties row;
    row.cells.resize(2);
    (void)doc::detail::ApplyTable(table, row, op, v);
  }
  EXPECT_GT(writes, 0u);
  std::printf("[ fuzz ] dispatch opcodes=65536 extra=%u\n", kExtraRounds);
}

// ---------------------------------------------------------------------------
// 5. Representative known-corruption assertions: the errors must be
//    distinguishable, not just "some error".
// ---------------------------------------------------------------------------
TEST(Fuzz, KnownCorruptionsReportDistinguishableErrors) {
  const SyntheticDoc base = MakeSyntheticDoc();
  // CLX and PAPX corruption is a hard error; CHPX page corruption degrades
  // to a warning (§6.9), so those cases assert the skip instead.
  auto error_of = [](SyntheticDoc d) {
    auto parsed = doc::ParseFormatted(d.word, d.table, d.fib, d.data);
    EXPECT_FALSE(parsed.has_value());
    return parsed.has_value() ? std::string("(success)")
                              : parsed.error().message;
  };

  SyntheticDoc d = base;
  d.fib.lcb_clx = 1000;
  EXPECT_EQ(error_of(d), "CLX outside table stream");
  d = base;
  d.fib.fc_clx = 1000;
  EXPECT_EQ(error_of(d), "CLX outside table stream");
  d = base;
  Put32(d.table, 11, 6);  // CPs out of order
  Put32(d.table, 15, 0);
  EXPECT_EQ(error_of(d), "invalid piece CP order");
  d = base;
  Put32(d.table, 21, 0x7FFFFFFFu);  // fc far outside WordDocument
  EXPECT_EQ(error_of(d), "piece outside WordDocument");
  d = base;
  Put32(d.table, 15, 5000);  // pieces claim more text than WordDocument has
  d.fib.ccp_text = 5000;
  EXPECT_EQ(error_of(d), "piece table over-covers WordDocument");
  d = base;
  Put16(d.table, 25, 3);  // Prm1 index outside the Prc array
  EXPECT_EQ(error_of(d), "CLX Prm1 index outside Prc array");
  d = base;
  Put32(d.table, 21, 2048);  // compressed bit cleared, UTF-16 at stream end
  EXPECT_EQ(error_of(d), "piece outside WordDocument");

  d = base;
  d.word[d.chpx_cb] = 2;  // opcode left, operand truncated away
  d.word[d.chpx_begin] = 0x01;
  d.word[d.chpx_begin + 1] = 0x60;
  auto degraded = doc::ParseFormatted(d.word, d.table, d.fib, d.data);
  EXPECT_TRUE(degraded.has_value());
  ASSERT_FALSE(degraded->warnings.empty());
  EXPECT_NE(degraded->warnings[0].find("skipped CHPX FKP page 1"),
            std::string::npos);
  EXPECT_NE(degraded->warnings[0].find("invalid FKP sprm sequence"),
            std::string::npos);
  d = base;
  d.word[d.papx_cb] = 255;  // PAPX cb beyond the page
  EXPECT_EQ(error_of(d), "invalid PAPX size");

  auto plain = ReadSeed(std::string(DOCCRAFT_SOURCE_DIR) +
                        "/tests/corpus/corpus/plain-text.doc");
  ASSERT_FALSE(plain.empty());
  plain[0] ^= 0xFF;  // CFB signature destroyed
  ConvertOutcome outcome = ConvertBytes(plain);
  EXPECT_FALSE(outcome.ok);
  EXPECT_EQ(outcome.stage, "cfb");

  auto legacy = ReadSeed(std::string(DOCCRAFT_SOURCE_DIR) +
                         "/tests/fixtures/encrypted/plain-text-rc4-legacy.doc");
  ASSERT_FALSE(legacy.empty());
  ASSERT_TRUE(ConvertBytes(legacy, "doccraft-m6").ok)
      << "unmutated legacy fixture must decrypt";
  outcome = ConvertBytes(legacy, "wrong");
  EXPECT_FALSE(outcome.ok);
  EXPECT_EQ(outcome.error, "incorrect document password");
  outcome = ConvertBytes(legacy);
  EXPECT_FALSE(outcome.ok);
  EXPECT_NE(outcome.error.find("legacy RC4/MD5"), std::string::npos);
  EXPECT_NE(outcome.error.find("requires --password"), std::string::npos);
}

}  // namespace
