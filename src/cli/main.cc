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
#include <string>

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
    // M1: open CFB and dump directory tree.
    std::fprintf(stderr, "inspect: not implemented yet (M1)\n");
    return 1;
  }
  if (cmd == "convert" && argc >= 3) {
    std::fprintf(stderr, "convert: not implemented yet (M2)\n");
    return 1;
  }
  std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
  return 2;
}
