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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"
#include "ooxml/validator.h"
#include "third_party/miniz/miniz.h"

namespace {

namespace cfb = doccraft::cfb;
namespace doc = doccraft::doc;
namespace docx = doccraft::docx;
namespace model = doccraft::model;
namespace ooxml = doccraft::ooxml;

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
                              std::istreambuf_iterator<char>());
}

std::optional<model::Document> Convert(const std::string& path) {
  const std::vector<uint8_t> bytes = ReadFile(path);
  if (bytes.empty())
    return std::nullopt;

  auto opened = cfb::CfbReader::Open(bytes);
  if (!opened)
    return std::nullopt;
  cfb::CfbReader reader = std::move(*opened);

  auto word = reader.ReadStream("WordDocument");
  if (!word)
    return std::nullopt;
  auto fib = doc::ParseFib(*word);
  if (!fib)
    return std::nullopt;
  auto table = reader.ReadStream(fib->table_stream());
  if (!table)
    return std::nullopt;

  // Pictures and OLE previews live in the Data stream; without it the parser
  // rejects documents that contain them, exactly like the CLI would.
  auto data = reader.ReadStream("Data");
  auto parsed = data ? doc::ParseFormatted(*word, *table, *fib, *data)
                     : doc::ParseFormatted(*word, *table, *fib);
  if (!parsed)
    return std::nullopt;
  return std::move(*parsed);
}

struct Part {
  std::string name;
  std::string xml;
};

// Only the parts whose root element belongs to WordprocessingML. DrawingML
// theme parts and package parts are not covered by this validator, so they
// must not be counted as "validated".
bool IsWordmlPart(const std::string& name) {
  static const char* const kFixed[] = {
      "word/document.xml",  "word/styles.xml",    "word/numbering.xml",
      "word/settings.xml",  "word/fontTable.xml", "word/webSettings.xml",
      "word/footnotes.xml", "word/endnotes.xml",  "word/comments.xml"};
  for (const char* part : kFixed) {
    if (name == part)
      return true;
  }
  const bool reference =
      name.rfind("word/header", 0) == 0 || name.rfind("word/footer", 0) == 0;
  return reference && name.size() > 4 &&
         name.compare(name.size() - 4, 4, ".xml") == 0;
}

std::vector<Part> WordParts(const std::vector<uint8_t>& zip) {
  std::vector<Part> parts;
  mz_zip_archive archive = {};
  if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) {
    return parts;
  }

  const mz_uint count = mz_zip_reader_get_num_files(&archive);
  for (mz_uint i = 0; i < count; ++i) {
    char name[256] = {};
    if (mz_zip_reader_get_filename(&archive, i, name, sizeof(name)) == 0) {
      continue;
    }
    const std::string part_name(name);
    if (!IsWordmlPart(part_name))
      continue;

    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
    if (data == nullptr)
      continue;
    parts.push_back(
        Part{part_name, std::string(static_cast<const char*>(data), size)});
    mz_free(data);
  }

  mz_zip_reader_end(&archive);
  return parts;
}

std::string Describe(const std::string& file,
                     const std::vector<ooxml::Violation>& violations) {
  std::string text =
      file + ": " + std::to_string(violations.size()) + " violation(s)\n";
  for (const auto& violation : violations) {
    text += "  " + violation.part + " " + violation.path + " -> " +
            violation.message + "\n";
  }
  return text;
}

// Returns the violations of one converted document. `validated_parts` reports
// how many WordprocessingML parts were actually checked, so an empty list can
// never be confused with "nothing was validated".
std::vector<ooxml::Violation> ValidateDocument(const model::Document& document,
                                               size_t* validated_parts) {
  auto zip = docx::Write(document);
  if (!zip) {
    return {ooxml::Violation{"", "", "writer failed: " + zip.error().message}};
  }

  std::vector<ooxml::Violation> all;
  for (const Part& part : WordParts(*zip)) {
    auto violations = ooxml::ValidatePart(part.name, part.xml);
    all.insert(all.end(), violations.begin(), violations.end());
    ++*validated_parts;
  }
  return all;
}

std::vector<std::string> SampleFiles(const std::string& directory) {
  std::vector<std::string> files;
  std::error_code error;
  for (const auto& entry :
       std::filesystem::directory_iterator(directory, error)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".doc") {
      continue;
    }
    files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

void CheckSamples(const std::string& directory, size_t* conversion_failures,
                  size_t* checked) {
  for (const std::string& path : SampleFiles(directory)) {
    const std::string file = std::filesystem::path(path).filename().string();
    auto document = Convert(path);
    if (!document) {
      // The degraded-* fixtures are deliberately damaged inputs; refusing
      // them is the expected outcome, not a schema problem.
      if (file.rfind("degraded-", 0) == 0)
        continue;
      ADD_FAILURE() << file << ": conversion failed";
      ++*conversion_failures;
      continue;
    }

    size_t parts = 0;
    const auto violations = ValidateDocument(*document, &parts);
    EXPECT_GE(parts, 2u) << file << ": no WordprocessingML part was validated";
    EXPECT_TRUE(violations.empty()) << Describe(file, violations);
    ++*checked;
  }
}

TEST(ConverterSchemaTest, SamplesMatchTheWordprocessingMlSchema) {
  const std::string root = DOCCRAFT_SOURCE_DIR;
  size_t failures = 0;
  size_t checked = 0;
  CheckSamples(root + "/tests/corpus/corpus", &failures, &checked);
  CheckSamples(root + "/tests/fixtures", &failures, &checked);

  EXPECT_EQ(failures, 0u) << "sample conversion failed";
  // Guards against an empty or renamed corpus directory silently passing.
  EXPECT_GE(checked, 30u) << "too few samples were validated";
}

}  // namespace
