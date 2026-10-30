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

// Thin file wrapper around doccraft_convert_buffer, used by
// tests/api_cli_parity.py to compare the embedding API with the CLI on the
// same input. Not a shipped tool.
//
// usage: api_cli_fixture <in.doc> <out.docx> [password]

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "api/doccraft_api.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <in.doc> <out.docx> [password]\n", argv[0]);
    return 2;
  }
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) {
    std::fprintf(stderr, "api_cli_fixture: cannot open %s\n", argv[1]);
    return 1;
  }
  const std::string bytes{std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>()};
  const std::vector<uint8_t> buffer(bytes.begin(), bytes.end());

  char message[256] = {};
  uint8_t* output = nullptr;
  size_t output_len = 0;
  const int status = doccraft_convert_buffer(
      buffer.data(), buffer.size(), argc > 3 ? argv[3] : nullptr, &output,
      &output_len, message, sizeof message);
  if (status != DOCCRAFT_OK) {
    std::fprintf(stderr, "api_cli_fixture: %s\n", message);
    return 1;
  }
  std::ofstream out(argv[2], std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "api_cli_fixture: cannot open output\n");
    doccraft_free(output);
    return 1;
  }
  out.write(reinterpret_cast<const char*>(output),
            static_cast<std::streamsize>(output_len));
  const bool ok = static_cast<bool>(out);
  doccraft_free(output);
  if (!ok) {
    std::fprintf(stderr, "api_cli_fixture: output write failed\n");
    return 1;
  }
  return 0;
}
