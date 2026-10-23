// Copyright 2026 The doccraft Authors. All Rights Reserved.
// Implementation of synthetic CFB builder (reference: [MS-CFB]).
// Codex M1 will flesh out edge cases (DIFAT extension, mini-stream); the
// initial version covers the single-FAT, regular-stream happy path so the
// TDD loop can start.

#include "cfb_test_util.h"

namespace doccraft::cfb::test {

namespace {
constexpr uint32_t kSectorSize = 512;
constexpr uint32_t kFreeSector = 0xFFFFFFFF;
constexpr uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr uint32_t kFatSector = 0xFFFFFFFD;
constexpr uint32_t kDifatSector = 0xFFFFFFFC;
}  // namespace

std::vector<uint8_t> MakePayload(size_t size, uint8_t fill) {
  return std::vector<uint8_t>(size, fill);
}

CfbBuilder& CfbBuilder::AddStream(const std::string& name,
                                  std::vector<uint8_t> data) {
  streams_.push_back({name, std::move(data)});
  return *this;
}

std::vector<uint8_t> CfbBuilder::Build() {
  // Layout: [header][dir sectors][mini fat][fat sectors][stream data...]
  // Full layout lands with M1; stub returns a valid-looking empty container
  // so tests can at least assert signature rejection paths.
  std::vector<uint8_t> out(kSectorSize, 0);
  const uint8_t sig[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
  for (int i = 0; i < 8; ++i) out[i] = sig[i];
  auto* u16 = reinterpret_cast<uint16_t*>(out.data() + 0x1E);
  *u16 = 0x0009;  // sector shift 2^9 = 512
  auto* u16b = reinterpret_cast<uint16_t*>(out.data() + 0x20);
  *u16b = 0x0006;  // mini sector shift 2^6 = 64
  // TODO(M1): full builder — directory entries, FAT chains, mini stream.
  return out;
}

}  // namespace doccraft::cfb::test
