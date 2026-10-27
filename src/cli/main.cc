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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sys/resource.h>
#endif

#include "cfb/cfb_reader.h"
#include "cfb/cfb_writer.h"
#include "doc/encryption.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "doc/text.h"
#include "docx/writer.h"
#include "model/document.h"

namespace {
#ifdef __linux__
long PeakRssKb() {
  rusage usage{};
  return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : 0;
}
#else
long PeakRssKb() {
  return 0;
}
#endif

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
        "  convert <in.doc> [-o out.docx] [--password value]   convert "
        "document\n"
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
    const bool profile = std::getenv("DOCCRAFT_PROFILE") != nullptr;
    std::string output = std::string(argv[2]) + "x";
    std::string password;
    bool has_password = false;
    bool has_output = false;
    for (int i = 3; i < argc; i += 2) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "convert: missing option value\n");
        return 2;
      }
      const std::string option = argv[i];
      if (option == "-o" && !has_output) {
        output = argv[i + 1];
        has_output = true;
      } else if (option == "--password" && !has_password) {
        password = argv[i + 1];
        has_password = true;
      } else {
        std::fprintf(stderr,
                     "convert: usage: convert in.doc [-o out.docx] "
                     "[--password value]\n");
        return 2;
      }
    }

    std::ifstream file(argv[2], std::ios::binary);
    if (!file) {
      std::fprintf(stderr, "convert: cannot open input\n");
      return 1;
    }

    std::vector<uint8_t> bytes = ReadInput(file);
    const auto t_read = Clock::now();
    const long rss_read = profile ? PeakRssKb() : 0;
    if (!file.eof() && file.fail()) {
      std::fprintf(stderr, "convert: input read failed\n");
      return 1;
    }
    auto opened = doccraft::cfb::CfbReader::Open(bytes);
    const auto t_cfb = Clock::now();
    const long rss_cfb = profile ? PeakRssKb() : 0;
    if (!opened) {
      std::fprintf(stderr, "convert: %s\n", opened.error().message.c_str());
      return 1;
    }
    std::optional<doccraft::cfb::CfbReader> cfb(std::move(*opened));
    auto word = cfb->ReadStream("WordDocument");
    if (!word) {
      std::fprintf(stderr, "convert: %s\n", word.error().message.c_str());
      return 1;
    }
    const bool encrypted = word->size() >= 12 && ((*word)[11] & 0x01);
    if (has_password && !encrypted) {
      std::fprintf(stderr,
                   "warning: --password ignored: document is not encrypted\n");
    }
    auto fib = doccraft::doc::ParseFib(*word);
    if (encrypted && !has_password && !((*word)[11] & 0x80)) {
      const std::string table_name = ((*word)[11] & 0x02) ? "1Table" : "0Table";
      auto encrypted_table = cfb->ReadStream(table_name);
      if (!encrypted_table) {
        std::fprintf(stderr, "convert: %s\n",
                     encrypted_table.error().message.c_str());
        return 1;
      }
      auto type = doccraft::doc::DetectRc4Type(*word, *encrypted_table);
      if (!type) {
        std::fprintf(stderr, "convert: %s\n", type.error().message.c_str());
      } else {
        std::fprintf(stderr, "convert: %.*s document requires --password\n",
                     static_cast<int>(type->size()), type->data());
      }
      return 1;
    }
    if (!fib && !(encrypted && has_password)) {
      std::fprintf(stderr, "convert: %s\n", fib.error().message.c_str());
      return 1;
    }
    const std::string table_name = fib                    ? fib->table_stream()
                                   : ((*word)[11] & 0x02) ? "1Table"
                                                          : "0Table";
    auto table = cfb->ReadStream(table_name);
    if (!table) {
      std::fprintf(stderr, "convert: %s\n", table.error().message.c_str());
      return 1;
    }
    auto data = cfb->ReadStream("Data");
    if (encrypted && has_password) {
      fib = doccraft::doc::DecryptRc4(
          *word, *table,
          data ? std::span<uint8_t>(*data) : std::span<uint8_t>(), password);
      if (!fib) {
        std::fprintf(stderr, "convert: %s\n", fib.error().message.c_str());
        return 1;
      }
    }
    const auto t_streams = Clock::now();
    const long rss_streams = profile ? PeakRssKb() : 0;
    bool has_object_pool = false;
    auto visited = cfb->Visit([&](const doccraft::cfb::DirEntry&,
                                  std::string_view path, bool storage) {
      if (storage && path == "Root Entry/ObjectPool")
        has_object_pool = true;
      return true;
    });
    if (!visited) {
      std::fprintf(stderr, "convert: %s\n", visited.error().message.c_str());
      return 1;
    }
    if (!has_object_pool) {
      // Stream copies no longer refer to the input container. Keep it only
      // when an embedded OLE object may need repackaging after parsing.
      cfb.reset();
      std::vector<uint8_t>().swap(bytes);
    }
    auto extracted =
        data ? doccraft::doc::ParseFormatted(*word, *table, *fib, *data)
             : doccraft::doc::ParseFormatted(*word, *table, *fib);
    if (!extracted) {
      std::fprintf(stderr, "convert: %s\n", extracted.error().message.c_str());
      return 1;
    }
    for (const auto& warning : extracted->warnings)
      std::fprintf(stderr, "warning: %s\n", warning.c_str());
    for (auto& object : extracted->ole_objects) {
      if (!cfb) {
        std::fprintf(stderr, "convert: OLE %d: OLE storage has no streams\n",
                     object.storage_id);
        return 1;
      }
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
    const long rss_format = profile ? PeakRssKb() : 0;
    auto archive = doccraft::docx::Write(*extracted);
    if (!archive) {
      std::fprintf(stderr, "convert: %s\n", archive.error().message.c_str());
      return 1;
    }
    const auto t_writer = Clock::now();
    const long rss_writer = profile ? PeakRssKb() : 0;

    std::ofstream out(output, std::ios::binary);
    if (!out) {
      std::fprintf(stderr, "convert: cannot open output\n");
      return 1;
    }
    if (profile) {
      auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
      };
      std::fprintf(stderr,
                   "PROFILE read_ms=%.3f cfb_ms=%.3f streams_ms=%.3f "
                   "format_ms=%.3f writer_ms=%.3f "
                   "rss_read_kb=%ld rss_cfb_kb=%ld rss_streams_kb=%ld "
                   "rss_format_kb=%ld rss_writer_kb=%ld\n",
                   ms(t_start, t_read), ms(t_read, t_cfb), ms(t_cfb, t_streams),
                   ms(t_streams, t_format), ms(t_format, t_writer), rss_read,
                   rss_cfb, rss_streams, rss_format, rss_writer);
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
