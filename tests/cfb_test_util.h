// Copyright 2026 The doccraft Authors. All Rights Reserved.
// Test utility: synthesizes valid CFB containers for golden/unit tests.
#ifndef DOCCRAFT_TESTS_CFB_TEST_UTIL_H_
#define DOCCRAFT_TESTS_CFB_TEST_UTIL_H_

#include <cstdint>
#include <string>
#include <vector>

namespace doccraft::cfb::test {

// Builder for minimal-but-valid CFB containers (v3, 512-byte sectors).
// Supports: N regular streams (>= 4096 bytes -> regular FAT chain),
// plus the required Root Entry. Mini-stream support is exercised via a
// dedicated small-stream builder flag.
class CfbBuilder {
 public:
  CfbBuilder& AddStream(const std::string& name, std::vector<uint8_t> data);
  // Sets the number of reserved FAT sectors (exercises DIFAT extension when
  // > 109 FAT sectors are needed).
  std::vector<uint8_t> Build();

 private:
  struct StreamSpec {
    std::string name;
    std::vector<uint8_t> data;
  };
  std::vector<StreamSpec> streams_;
};

// A few fixed well-known payloads used by golden assertions.
std::vector<uint8_t> MakePayload(size_t size, uint8_t fill);

}  // namespace doccraft::cfb::test

#endif  // DOCCRAFT_TESTS_CFB_TEST_UTIL_H_
