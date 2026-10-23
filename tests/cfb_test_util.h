// Copyright 2026 The doccraft Authors. All Rights Reserved.
// Test utility: synthesizes valid CFB containers for golden/unit tests.
#ifndef DOCCRAFT_TESTS_CFB_TEST_UTIL_H_
#define DOCCRAFT_TESTS_CFB_TEST_UTIL_H_

#include <cstdint>
#include <string>
#include <vector>

namespace doccraft::cfb::test {

// Builder for CFB v3 (512-byte sectors) and v4 (4096-byte sectors).
// Slash-separated stream names create nested storages. Streams below 4096
// bytes are stored in the root mini stream.
class CfbBuilder {
 public:
  CfbBuilder& AddStream(const std::string& name, std::vector<uint8_t> data);
  CfbBuilder& SetSectorSize(uint32_t sector_size);
  std::vector<uint8_t> Build();

 private:
  struct StreamSpec {
    std::string name;
    std::vector<uint8_t> data;
  };
  std::vector<StreamSpec> streams_;
  uint32_t sector_size_ = 512;
};

// A few fixed well-known payloads used by golden assertions.
std::vector<uint8_t> MakePayload(size_t size, uint8_t fill);

}  // namespace doccraft::cfb::test

#endif  // DOCCRAFT_TESTS_CFB_TEST_UTIL_H_
