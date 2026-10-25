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

#ifndef TESTS_CFB_TEST_UTIL_H_
#define TESTS_CFB_TEST_UTIL_H_

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

#endif  // TESTS_CFB_TEST_UTIL_H_
