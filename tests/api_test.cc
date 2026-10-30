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

// The embedding API (src/api): buffer in, docx buffer out. This is the entry
// point WebAssembly, Android and iOS use instead of the CLI.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "api/doccraft_api.h"

namespace {

std::vector<uint8_t> ReadFixture(const std::string& relative) {
  std::ifstream file(std::string(DOCCRAFT_SOURCE_DIR) + "/" + relative,
                     std::ios::binary);
  const std::string bytes{std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>()};
  return {bytes.begin(), bytes.end()};
}

// The archive is a zip; part names are stored uncompressed in the local file
// headers, so a plain byte search finds them without inflating anything.
bool ContainsPart(std::string_view archive, std::string_view part) {
  return archive.find(part) != std::string_view::npos;
}

struct Result {
  int status = -1;
  std::vector<uint8_t> bytes;
  std::string error;
};

Result Convert(const std::vector<uint8_t>& input, const char* password) {
  Result result;
  char message[256] = {};
  uint8_t* output = nullptr;
  size_t output_len = 0;
  result.status =
      doccraft_convert_buffer(input.data(), input.size(), password, &output,
                              &output_len, message, sizeof message);
  result.error = message;
  if (output != nullptr) {
    result.bytes.assign(output, output + output_len);
    doccraft_free(output);
  }
  return result;
}

std::string_view View(const std::vector<uint8_t>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace

TEST(ApiTest, ConvertsCorpusDocumentToDocx) {
  const std::vector<uint8_t> input =
      ReadFixture("tests/corpus/corpus/plain-text.doc");
  ASSERT_FALSE(input.empty());

  const Result result = Convert(input, nullptr);
  ASSERT_EQ(result.status, DOCCRAFT_OK) << result.error;
  EXPECT_TRUE(result.error.empty());

  const std::string_view archive = View(result.bytes);
  ASSERT_GE(archive.size(), 2u);
  EXPECT_EQ(archive.substr(0, 2), "PK");
  EXPECT_TRUE(ContainsPart(archive, "word/document.xml"));
  EXPECT_TRUE(ContainsPart(archive, "word/styles.xml"));
}

TEST(ApiTest, DecryptsWithPassword) {
  const std::vector<uint8_t> input =
      ReadFixture("tests/fixtures/encrypted/plain-text-rc4-cryptoapi.doc");
  ASSERT_FALSE(input.empty());

  const Result locked = Convert(input, nullptr);
  EXPECT_EQ(locked.status, DOCCRAFT_ERROR_CONVERSION_FAILED);
  EXPECT_NE(locked.error.find("password"), std::string::npos) << locked.error;
  EXPECT_TRUE(locked.bytes.empty());

  const Result opened = Convert(input, "doccraft-m6");
  ASSERT_EQ(opened.status, DOCCRAFT_OK) << opened.error;
  EXPECT_TRUE(ContainsPart(View(opened.bytes), "word/document.xml"));
}

TEST(ApiTest, KeepsOleObjectPayload) {
  const std::vector<uint8_t> input =
      ReadFixture("tests/fixtures/ole-embedded.doc");
  ASSERT_FALSE(input.empty());

  const Result result = Convert(input, nullptr);
  ASSERT_EQ(result.status, DOCCRAFT_OK) << result.error;
  EXPECT_TRUE(ContainsPart(View(result.bytes), "word/embeddings/"));
}

TEST(ApiTest, ReportsMalformedInput) {
  const std::vector<uint8_t> garbage(64, 0xAB);
  const Result result = Convert(garbage, nullptr);
  EXPECT_EQ(result.status, DOCCRAFT_ERROR_CONVERSION_FAILED);
  EXPECT_FALSE(result.error.empty());
  EXPECT_TRUE(result.bytes.empty());
}

TEST(ApiTest, RejectsMissingArguments) {
  char message[128] = {};
  uint8_t* output = nullptr;
  size_t output_len = 0;
  const uint8_t data[4] = {0xD0, 0xCF, 0x11, 0xE0};

  EXPECT_EQ(doccraft_convert_buffer(nullptr, 4, nullptr, &output, &output_len,
                                    message, sizeof message),
            DOCCRAFT_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(doccraft_convert_buffer(data, 0, nullptr, &output, &output_len,
                                    message, sizeof message),
            DOCCRAFT_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(doccraft_convert_buffer(data, 4, nullptr, nullptr, &output_len,
                                    message, sizeof message),
            DOCCRAFT_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(doccraft_convert_buffer(data, 4, nullptr, &output, nullptr, message,
                                    sizeof message),
            DOCCRAFT_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(std::string(message).empty());
  // The error buffer may be omitted entirely.
  EXPECT_EQ(doccraft_convert_buffer(data, 4, nullptr, &output, &output_len,
                                    nullptr, 0),
            DOCCRAFT_ERROR_CONVERSION_FAILED);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(output_len, 0u);
}

TEST(ApiTest, ReportsCodepageSupport) {
  EXPECT_EQ(doccraft_supports_codepage(1252), 1);
  EXPECT_EQ(doccraft_supports_codepage(437), 0);
}

TEST(ApiTest, ExposesVersion) {
  EXPECT_EQ(doccraft_version(), std::string(DOCCRAFT_VERSION));
}
