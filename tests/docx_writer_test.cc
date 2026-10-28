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

#include <string>
#include <vector>

#include "miniz.h"

#include "docx/internal.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;

std::string Part(const std::vector<uint8_t>& archive, const char* name) {
  mz_zip_archive zip = {};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
    return {};
  size_t size = 0;
  void* bytes = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
  std::string content;
  if (bytes) {
    content.assign(static_cast<const char*>(bytes), size);
    mz_free(bytes);
  }
  mz_zip_reader_end(&zip);
  return content;
}

TEST(M6WriterBuffer, SerializedPartsLimitReturnsError) {
  model::Document document;
  constexpr size_t kTestLimit = 8192;
  auto baseline = docx::detail::WriteWithBufferLimit(document, kTestLimit);
  ASSERT_TRUE(baseline) << baseline.error().message;
  document.paragraphs.push_back({});
  document.paragraphs.back().runs.push_back({});
  document.paragraphs.back().runs.back().text = std::string(16384, 'x');
  auto zip = docx::detail::WriteWithBufferLimit(document, kTestLimit);
  ASSERT_FALSE(zip);
  EXPECT_EQ(zip.error().message, "writer buffer limit exceeded");
}

TEST(M6WriterBuffer, SinglePartLimitReturnsNamedSize) {
  model::Document document;
  document.paragraphs.push_back({});
  document.paragraphs.back().runs.push_back({});
  document.paragraphs.back().runs.back().text = std::string(2048, 'x');
  constexpr size_t kTotalLimit = 16384;
  constexpr size_t kPartLimit = 1024;
  auto baseline = docx::detail::WriteWithBufferLimit(document, kTotalLimit);
  ASSERT_TRUE(baseline) << baseline.error().message;
  const size_t actual_bytes = Part(*baseline, "word/document.xml").size();
  auto zip =
      docx::detail::WriteWithBufferLimit(document, kTotalLimit, kPartLimit);
  ASSERT_FALSE(zip);
  EXPECT_EQ(zip.error().message,
            "writer part limit exceeded: word/document.xml (" +
                std::to_string(actual_bytes) + " bytes)");
}

TEST(M6WriterBuffer, LastSectionHeaderPartLimitReturnsError) {
  model::Document document;
  model::Paragraph header;
  model::Run run;
  run.text = std::string(4096, 'h');
  header.runs.push_back(std::move(run));
  document.section.headers[1].push_back(std::move(header));
  constexpr size_t kTotalLimit = 16384;
  auto baseline = docx::detail::WriteWithBufferLimit(document, kTotalLimit);
  ASSERT_TRUE(baseline) << baseline.error().message;
  const size_t header_bytes = Part(*baseline, "word/header1.xml").size();
  ASSERT_GT(header_bytes, Part(*baseline, "word/document.xml").size());
  auto zip = docx::detail::WriteWithBufferLimit(document, kTotalLimit,
                                                header_bytes - 1);
  ASSERT_FALSE(zip) << "writer returned a ZIP despite oversized header";
  EXPECT_EQ(zip.error().message,
            "writer part limit exceeded: word/header1.xml (" +
                std::to_string(header_bytes) + " bytes)");
}

TEST(M6WriterBuffer, CommentImagePartLimitReturnsError) {
  model::Document document;
  model::Image image;
  image.bytes.assign(4096, 'i');
  image.width = 96;
  image.height = 96;
  document.images.push_back(std::move(image));
  model::Comment comment;
  model::Run image_run;
  image_run.image_index = 0;
  model::Paragraph body;
  body.runs.push_back(std::move(image_run));
  comment.paragraphs.push_back(std::move(body));
  document.comments.push_back(std::move(comment));
  constexpr size_t kTotalLimit = 16384;
  auto baseline = docx::detail::WriteWithBufferLimit(document, kTotalLimit);
  ASSERT_TRUE(baseline) << baseline.error().message;
  const size_t image_bytes = Part(*baseline, "word/media/image1.png").size();
  auto zip = docx::detail::WriteWithBufferLimit(document, kTotalLimit,
                                                image_bytes - 1);
  ASSERT_FALSE(zip) << "writer returned a ZIP despite oversized comment image";
  EXPECT_EQ(zip.error().message,
            "writer part limit exceeded: word/media/image1.png (" +
                std::to_string(image_bytes) + " bytes)");
}

}  // namespace
