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

// Per-section header/footer stories: PlcfHdd holds six per section, so a
// two-section document must end up with one header part per section instead of
// reusing the first section's story everywhere.

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "miniz.h"

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"

namespace {
using namespace doccraft;

std::expected<model::Document, doc::Error> ParseFixture(const char* name) {
  std::ifstream file(
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/" + name + ".doc",
      std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  if (!cfb) {
    return std::unexpected(doc::Error{"cfb open failed"});
  }
  auto word = cfb->ReadStream("WordDocument");
  if (!word) {
    return std::unexpected(doc::Error{"missing WordDocument"});
  }
  auto fib = doc::ParseFib(*word);
  if (!fib) {
    return std::unexpected(fib.error());
  }
  auto table = cfb->ReadStream(fib->table_stream());
  if (!table) {
    return std::unexpected(doc::Error{"missing table stream"});
  }
  return doc::ParseFormatted(*word, *table, *fib);
}

std::string ParagraphText(const model::Paragraph& paragraph) {
  std::string out;
  for (const auto& run : paragraph.runs) {
    out += run.text;
  }
  return out;
}

std::string StoryText(const std::vector<model::Paragraph>& story) {
  std::string out;
  for (const auto& paragraph : story) {
    out += ParagraphText(paragraph);
  }
  return out;
}

std::string Part(const std::vector<uint8_t>& zip_bytes, const char* name) {
  mz_zip_archive z = {};
  if (!mz_zip_reader_init_mem(&z, zip_bytes.data(), zip_bytes.size(), 0)) {
    return {};
  }
  size_t size = 0;
  void* p = mz_zip_reader_extract_file_to_heap(&z, name, &size, 0);
  std::string out;
  if (p) {
    out.assign(static_cast<const char*>(p), size);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return out;
}

size_t Count(const std::string& text, const std::string& needle) {
  size_t hits = 0, at = 0;
  while ((at = text.find(needle, at)) != std::string::npos) {
    ++hits;
    at += needle.size();
  }
  return hits;
}
}  // namespace

TEST(M3Headers, EachSectionKeepsItsOwnStory) {
  auto d = ParseFixture("two-section-headers");
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->section_breaks.size(), 1u);
  const model::Section& first = d->section_breaks[0].second;
  const model::Section& last = d->section;
  EXPECT_EQ(StoryText(first.headers[1]), "HEADER ONE");
  EXPECT_EQ(StoryText(last.headers[1]), "HEADER TWO");
  // The other five stories are empty in this sample.
  EXPECT_TRUE(StoryText(first.headers[0]).empty());
  EXPECT_TRUE(StoryText(last.headers[4]).empty());
}

TEST(M3Headers, WritesOneHeaderPartPerSection) {
  auto d = ParseFixture("two-section-headers");
  ASSERT_TRUE(d) << d.error().message;
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  const std::string document = Part(*zip, "word/document.xml");
  ASSERT_FALSE(document.empty());
  // Two sectPr (the break section and the final one), each with its own
  // reference.
  EXPECT_EQ(Count(document, "<w:sectPr>"), 2u);
  EXPECT_EQ(Count(document, "<w:headerReference"), 2u);
  const std::string header1 = Part(*zip, "word/header1.xml");
  const std::string header2 = Part(*zip, "word/header2.xml");
  EXPECT_NE(header1.find(">HEADER ONE</w:t>"), std::string::npos);
  EXPECT_NE(header2.find(">HEADER TWO</w:t>"), std::string::npos);
  // The package advertises both parts and both relationships.
  const std::string types = Part(*zip, "[Content_Types].xml");
  EXPECT_NE(types.find("/word/header1.xml"), std::string::npos);
  EXPECT_NE(types.find("/word/header2.xml"), std::string::npos);
  const std::string rels = Part(*zip, "word/_rels/document.xml.rels");
  EXPECT_EQ(Count(rels, "/header\""), 2u);
  // References must point at distinct relationship ids.
  EXPECT_EQ(Count(document, "r:id=\"rId3\""), 1u);
  EXPECT_EQ(Count(document, "r:id=\"rId4\""), 1u);
}

TEST(M3Headers, FirstSectionWithoutStoryFallsBackToNothing) {
  // A section without stories must not inherit the previous section's part.
  model::Document d;
  d.section_breaks.emplace_back(0, model::Section{});
  model::Paragraph heading;
  model::Run run;
  run.text = "body";
  heading.runs.push_back(run);
  d.paragraphs.push_back(heading);
  d.blocks.push_back({false, 0});
  model::Paragraph header;
  header.runs.push_back(run);
  d.section.headers[1].push_back(header);
  auto zip = docx::Write(d);
  ASSERT_TRUE(zip);
  const std::string document = Part(*zip, "word/document.xml");
  EXPECT_EQ(Count(document, "<w:headerReference"), 1u);
  EXPECT_EQ(Count(document, "<w:sectPr>"), 2u);
}
