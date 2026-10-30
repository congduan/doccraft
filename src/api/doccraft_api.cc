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

#include "api/doccraft_api.h"

#include <cstdlib>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/charset.h"
#include "cfb/cfb_reader.h"
#include "cfb/cfb_writer.h"
#include "doc/encryption.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "doc/text.h"
#include "docx/writer.h"
#include "model/document.h"

#ifndef DOCCRAFT_VERSION
#define DOCCRAFT_VERSION "0.0.0"
#endif

namespace {

namespace base = doccraft::base;
namespace cfb = doccraft::cfb;
namespace doc = doccraft::doc;
namespace docx = doccraft::docx;

void CopyMessage(char* err_buf, size_t err_buf_len, std::string_view message) {
  if (err_buf == nullptr || err_buf_len == 0) {
    return;
  }
  const size_t room = err_buf_len - 1;
  const size_t copy = message.size() < room ? message.size() : room;
  std::memcpy(err_buf, message.data(), copy);
  err_buf[copy] = '\0';
}

// The conversion body of `doccraft convert`, without its command-line
// concerns: no stderr warnings, no "--password ignored" hint, no profile
// timings. Every step is the same core-layer call in the same order, so the
// docx bytes are identical to what the CLI writes for the same input.
std::expected<std::vector<uint8_t>, std::string> ConvertToDocx(
    std::span<const uint8_t> input, std::string_view password) {
  auto opened = cfb::CfbReader::Open(input);
  if (!opened) {
    return std::unexpected(opened.error().message);
  }
  const cfb::CfbReader& container = *opened;

  auto word = container.ReadStream("WordDocument");
  if (!word) {
    return std::unexpected(word.error().message);
  }
  // FibBase.fEncrypted, [MS-DOC] §2.5.1.
  const bool encrypted = word->size() >= 12 && ((*word)[11] & 0x01);
  if (encrypted && password.empty()) {
    return std::unexpected(std::string("document requires a password"));
  }

  auto fib = doc::ParseFib(*word);
  if (fib && fib->legacy && fib->codepage == base::kDefaultCodepage) {
    auto summary = container.ReadStream("\005SummaryInformation");
    if (summary) {
      const uint32_t codepage = doc::ReadSummaryCodepage(*summary);
      if (((codepage >= 1250 && codepage <= 1258) || codepage == 932 ||
           codepage == 936 || codepage == 949 || codepage == 950) &&
          base::SupportsCodepage(codepage)) {
        fib->codepage = codepage;
      }
    }
  }
  if (!fib && !(encrypted && !password.empty())) {
    return std::unexpected(fib.error().message);
  }

  // Word 6/95 keeps its tables in WordDocument even when a repackaged
  // container also carries a Table stream.
  const std::string table_name = fib && fib->legacy     ? "WordDocument"
                                 : fib                  ? fib->table_stream()
                                 : ((*word)[11] & 0x02) ? "1Table"
                                                        : "0Table";
  auto table = container.ReadStream(table_name);
  if (!table && fib && fib->legacy) {
    table = *word;
  }
  if (!table) {
    return std::unexpected(table.error().message);
  }

  auto data = container.ReadStream("Data");
  if (encrypted && !password.empty()) {
    const std::span<uint8_t> data_span =
        data ? std::span<uint8_t>(*data) : std::span<uint8_t>();
    fib = ((*word)[11] & 0x80)
              ? doc::DecryptXor(*word, *table, data_span, password)
              : doc::DecryptRc4(*word, *table, data_span, password);
    if (!fib) {
      return std::unexpected(fib.error().message);
    }
  }

  auto extracted = data ? doc::ParseFormatted(*word, *table, *fib, *data)
                        : doc::ParseFormatted(*word, *table, *fib);
  if (!extracted) {
    return std::unexpected(extracted.error().message);
  }

  size_t image_total = 0;
  for (const auto& image : extracted->images) {
    if (image.bytes.size() > doc::kMaxDocumentImageBytes - image_total) {
      return std::unexpected(std::string("total image size exceeds limit"));
    }
    image_total += image.bytes.size();
  }

  // OLE objects carry their payload outside the IR: repackage the ObjectPool
  // storage. A missing payload degrades to the writer's placeholder, exactly
  // like the CLI's "placeholder emitted" warning path.
  for (auto& object : extracted->ole_objects) {
    auto packaged =
        cfb::RepackageStorage(container, "Root Entry/ObjectPool/_" +
                                             std::to_string(object.storage_id));
    if (!packaged) {
      if (packaged.error().code == cfb::ErrorCode::kStreamNotFound) {
        continue;
      }
      return std::unexpected(packaged.error().message);
    }
    if (object.preview_image_index < 0) {
      const std::string path =
          "Root Entry/ObjectPool/_" + std::to_string(object.storage_id) + "/";
      for (const char prefix : {'\x01', '\x02'}) {
        auto stream = container.ReadStream(path + prefix + "OlePres000");
        if (!stream && stream.error().code != cfb::ErrorCode::kStreamNotFound) {
          return std::unexpected(stream.error().message);
        }
        if (!stream) {
          continue;
        }
        auto image = doc::ReadOlePresentation(
            *stream, doc::kMaxDocumentImageBytes - image_total);
        if (!image) {
          return std::unexpected(image.error().message);
        }
        if (*image) {
          image_total += (**image).bytes.size();
          object.preview_image_index =
              static_cast<int>(extracted->images.size());
          extracted->images.push_back(std::move(**image));
          break;
        }
      }
    }
    object.bytes = std::move(*packaged);
  }

  auto archive = docx::Write(*extracted);
  if (!archive) {
    return std::unexpected(archive.error().message);
  }
  return std::move(*archive);
}

}  // namespace

extern "C" {

int doccraft_convert_buffer(const uint8_t* input, size_t input_len,
                            const char* password, uint8_t** output,
                            size_t* output_len, char* err_buf,
                            size_t err_buf_len) {
  if (output == nullptr || output_len == nullptr) {
    CopyMessage(err_buf, err_buf_len, "output and output_len must not be NULL");
    return DOCCRAFT_ERROR_INVALID_ARGUMENT;
  }
  *output = nullptr;
  *output_len = 0;
  if (input == nullptr || input_len == 0) {
    CopyMessage(err_buf, err_buf_len, "input must not be empty");
    return DOCCRAFT_ERROR_INVALID_ARGUMENT;
  }
  const std::string_view secret = password != nullptr ? password : "";
  auto converted =
      ConvertToDocx(std::span<const uint8_t>(input, input_len), secret);
  if (!converted) {
    CopyMessage(err_buf, err_buf_len, converted.error());
    return DOCCRAFT_ERROR_CONVERSION_FAILED;
  }
  if (converted->empty()) {
    CopyMessage(err_buf, err_buf_len, "writer produced an empty archive");
    return DOCCRAFT_ERROR_CONVERSION_FAILED;
  }
  void* buffer = std::malloc(converted->size());
  if (buffer == nullptr) {
    CopyMessage(err_buf, err_buf_len, "out of memory");
    return DOCCRAFT_ERROR_OUT_OF_MEMORY;
  }
  std::memcpy(buffer, converted->data(), converted->size());
  *output = static_cast<uint8_t*>(buffer);
  *output_len = converted->size();
  return DOCCRAFT_OK;
}

void doccraft_free(void* memory) {
  std::free(memory);
}

int doccraft_supports_codepage(uint32_t codepage) {
  return base::SupportsCodepage(codepage) ? 1 : 0;
}

const char* doccraft_version(void) {
  return DOCCRAFT_VERSION;
}

}  // extern "C"
