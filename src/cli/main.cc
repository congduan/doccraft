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

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "cfb/cfb_reader.h"
#include "cfb/cfb_writer.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "doc/text.h"
#include "docx/writer.h"
#include "model/document.h"

namespace {
std::vector<uint8_t> ReadInput(std::ifstream& file) {
  file.seekg(0, std::ios::end);
  const auto end = file.tellg();
  if (end >= 0 && end <= std::numeric_limits<std::streamsize>::max()) {
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    file.read(reinterpret_cast<char*>(bytes.data()), end);
    return bytes;
  }
  file.clear();
  file.seekg(0, std::ios::beg);
  file.clear();
  return {std::istreambuf_iterator<char>(file), {}};
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(
        stderr,
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

    std::vector<uint8_t> bytes = ReadInput(file);
    auto reader = doccraft::cfb::CfbReader::Open(bytes);
    if (!reader) {
      std::fprintf(stderr, "inspect: %s\n", reader.error().message.c_str());
      return 1;
    }

    std::printf("CFB version %u, sector size %u\n", reader->major_version(),
                reader->sector_size());
    auto visited = reader->Visit([](const doccraft::cfb::DirEntry& entry,
                                    std::string_view path, bool) {
      std::printf("%.*s (%" PRIu64 " bytes)\n", static_cast<int>(path.size()),
                  path.data(), static_cast<uint64_t>(entry.size));
      return true;
    });
    if (!visited) {
      std::fprintf(stderr, "inspect: %s\n", visited.error().message.c_str());
      return 1;
    }
    return 0;
  }
  if (cmd == "convert" && argc >= 3) {
    using Clock = std::chrono::steady_clock;
    const auto t_start = Clock::now();
    std::string output = std::string(argv[2]) + "x";
    if (argc == 5 && std::string(argv[3]) == "-o") {
      output = argv[4];
    } else if (argc != 3) {
      std::fprintf(stderr, "convert: usage: convert in.doc [-o out.docx]\n");
      return 2;
    }

    std::ifstream file(argv[2], std::ios::binary);
    if (!file) {
      std::fprintf(stderr, "convert: cannot open input\n");
      return 1;
    }

    std::vector<uint8_t> bytes = ReadInput(file);
    const auto t_read = Clock::now();
    if (!file.eof() && file.fail()) {
      std::fprintf(stderr, "convert: input read failed\n");
      return 1;
    }
    auto cfb = doccraft::cfb::CfbReader::Open(bytes);
    const auto t_cfb = Clock::now();
    if (!cfb) {
      std::fprintf(stderr, "convert: %s\n", cfb.error().message.c_str());
      return 1;
    }
    auto word = cfb->ReadStream("WordDocument");
    if (!word) {
      std::fprintf(stderr, "convert: %s\n", word.error().message.c_str());
      return 1;
    }
    auto fib = doccraft::doc::ParseFib(*word);
    if (!fib) {
      std::fprintf(stderr, "convert: %s\n", fib.error().message.c_str());
      return 1;
    }
    auto table = cfb->ReadStream(fib->table_stream());
    if (!table) {
      std::fprintf(stderr, "convert: %s\n", table.error().message.c_str());
      return 1;
    }
    auto data = cfb->ReadStream("Data");
    const auto t_streams = Clock::now();
    auto extracted =
        data ? doccraft::doc::ParseFormatted(*word, *table, *fib, *data)
             : doccraft::doc::ParseFormatted(*word, *table, *fib);
    if (!extracted) {
      std::fprintf(stderr, "convert: %s\n", extracted.error().message.c_str());
      return 1;
    }
    for (auto& object : extracted->ole_objects) {
      auto packaged = doccraft::cfb::RepackageStorage(
          *cfb, "Root Entry/ObjectPool/_" + std::to_string(object.storage_id));
      if (!packaged) {
        std::fprintf(stderr, "convert: OLE %d: %s\n", object.storage_id,
                     packaged.error().message.c_str());
        return 1;
      }
      object.bytes = std::move(*packaged);
    }
    const auto t_format = Clock::now();
    auto archive = doccraft::docx::Write(*extracted);
    if (!archive) {
      std::fprintf(stderr, "convert: %s\n", archive.error().message.c_str());
      return 1;
    }
    const auto t_writer = Clock::now();

    std::ofstream out(output, std::ios::binary);
    if (!out) {
      std::fprintf(stderr, "convert: cannot open output\n");
      return 1;
    }
    if (std::getenv("DOCCRAFT_PROFILE")) {
      auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
      };
      std::fprintf(stderr,
                   "PROFILE read_ms=%.3f cfb_ms=%.3f streams_ms=%.3f "
                   "format_ms=%.3f writer_ms=%.3f\n",
                   ms(t_start, t_read), ms(t_read, t_cfb), ms(t_cfb, t_streams),
                   ms(t_streams, t_format), ms(t_format, t_writer));
    }
    out.write(reinterpret_cast<const char*>(archive->data()),
              static_cast<std::streamsize>(archive->size()));
    if (!out) {
      std::fprintf(stderr, "convert: output write failed\n");
      return 1;
    }
    return 0;
  }

  std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
  return 2;
}
