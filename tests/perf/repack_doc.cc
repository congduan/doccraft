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

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "cfb/cfb_reader.h"
#include "tests/cfb_test_util.h"

int main(int argc, char** argv) {
  if (argc != 4) {
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    return 1;
  }
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  auto reader = doccraft::cfb::CfbReader::Open(bytes);
  if (!reader) {
    return 1;
  }
  doccraft::cfb::test::CfbBuilder builder;
  for (const char* name : {"WordDocument", "1Table"}) {
    auto stream = reader->ReadStream(name);
    if (!stream) {
      return 1;
    }
    builder.AddStream(name, std::move(*stream));
  }
  // A deterministic large binary Data stream models the storage footprint of
  // embedded objects while retaining the source's chapters, tables and notes.
  size_t payload_size = 0;
  const std::string size_text = argv[3];
  auto [end, error] = std::from_chars(
      size_text.data(), size_text.data() + size_text.size(), payload_size);
  if (error != std::errc() || end != size_text.data() + size_text.size()) {
    return 2;
  }
  std::vector<uint8_t> payload(payload_size);
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(i * 37u + (i >> 8));
  }
  builder.AddStream("Data", std::move(payload));
  auto output = builder.Build();
  std::ofstream out(argv[2], std::ios::binary);
  if (!out) {
    return 1;
  }
  out.write(reinterpret_cast<const char*>(output.data()),
            static_cast<std::streamsize>(output.size()));
  return out ? 0 : 1;
}
