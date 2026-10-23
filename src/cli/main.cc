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

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cfb/cfb_reader.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "doccraft v0.1.0 — .doc to .docx converter\n"
                 "usage: doccraft <command> [args]\n"
                 "  convert <in.doc> [-o out.docx]   convert document\n"
                 "  inspect  <in.doc>                dump container/FIB info\n");
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "inspect" && argc >= 3) {
    std::ifstream file(argv[2], std::ios::binary);
    if (!file) {
      std::fprintf(stderr, "inspect: cannot open %s\n", argv[2]);
      return 1;
    }
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
    auto reader = doccraft::cfb::CfbReader::Open(bytes);
    if (!reader) {
      std::fprintf(stderr, "inspect: %s\n", reader.error().message.c_str());
      return 1;
    }
    std::printf("CFB version %u, sector size %u\n", reader->major_version(),
                reader->sector_size());
    auto visited = reader->Visit([](const doccraft::cfb::DirEntry& entry,
                                    std::string_view path, bool) {
      std::printf("%.*s (%llu bytes)\n", static_cast<int>(path.size()), path.data(),
                  static_cast<unsigned long long>(entry.size));
      return true;
    });
    if (!visited) {
      std::fprintf(stderr, "inspect: %s\n", visited.error().message.c_str());
      return 1;
    }
    return 0;
  }
  if (cmd == "convert" && argc >= 3) {
    std::fprintf(stderr, "convert: not implemented yet (M2)\n");
    return 1;
  }
  std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
  return 2;
}
