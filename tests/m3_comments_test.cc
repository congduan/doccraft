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

// Comments: PlcfandRef anchors (FIB pair 4) plus PlcfandTxt (pair 5) and the
// GrpXstAtnOwners author array (pair 36) become comments.xml with
// w:commentRangeStart/End + w:commentReference in the body.

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

TEST(M3Comments, ReadsBodiesAuthorsAndAnchors) {
  auto d = ParseFixture("comments-basic");
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->comments.size(), 2u);
  EXPECT_EQ(d->comments[0].author, "Alice");
  ASSERT_EQ(d->comments[0].paragraphs.size(), 2u);
  EXPECT_EQ(ParagraphText(d->comments[0].paragraphs[0]), "First comment body.");
  EXPECT_EQ(ParagraphText(d->comments[0].paragraphs[1]),
            "Second comment paragraph.");
  EXPECT_EQ(d->comments[1].author, "Bob");
  ASSERT_EQ(d->comments[1].paragraphs.size(), 1u);
  EXPECT_EQ(ParagraphText(d->comments[1].paragraphs[0]), "Another comment.");

  // The 0x05 marks are gone from the text and replaced by anchor runs.
  ASSERT_EQ(d->paragraphs.size(), 2u);
  EXPECT_EQ(ParagraphText(d->paragraphs[0]),
            "Main text with an annotated phrase and the rest.");
  const auto anchor_of = [](const model::Paragraph& paragraph) {
    int id = -1;
    for (const auto& run : paragraph.runs) {
      if (run.comment_ref >= 0) {
        id = run.comment_ref;
      }
    }
    return id;
  };
  EXPECT_EQ(anchor_of(d->paragraphs[0]), 1);
  EXPECT_EQ(anchor_of(d->paragraphs[1]), 2);
}

TEST(M3Comments, WritesCommentsPartAndAnchors) {
  auto d = ParseFixture("comments-basic");
  ASSERT_TRUE(d) << d.error().message;
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  const std::string document = Part(*zip, "word/document.xml");
  EXPECT_EQ(Count(document, "<w:commentRangeStart"), 2u);
  EXPECT_EQ(Count(document, "<w:commentRangeEnd"), 2u);
  EXPECT_EQ(Count(document, "<w:commentReference"), 2u);
  EXPECT_NE(document.find("<w:commentReference w:id=\"1\"/>"),
            std::string::npos);

  const std::string comments = Part(*zip, "word/comments.xml");
  ASSERT_FALSE(comments.empty());
  EXPECT_NE(comments.find("<w:comment w:id=\"1\" w:author=\"Alice\">"),
            std::string::npos);
  EXPECT_NE(comments.find("<w:comment w:id=\"2\" w:author=\"Bob\">"),
            std::string::npos);
  EXPECT_NE(comments.find(">Second comment paragraph.</w:t>"),
            std::string::npos);

  const std::string types = Part(*zip, "[Content_Types].xml");
  EXPECT_NE(types.find("wordprocessingml.comments+xml"), std::string::npos);
  const std::string rels = Part(*zip, "word/_rels/document.xml.rels");
  EXPECT_NE(rels.find("Target=\"comments.xml\""), std::string::npos);
}

TEST(M3Comments, DocumentWithoutCommentsHasNoPart) {
  model::Document d;
  model::Paragraph p;
  model::Run r;
  r.text = "plain";
  p.runs.push_back(r);
  d.paragraphs.push_back(p);
  auto zip = docx::Write(d);
  ASSERT_TRUE(zip);
  EXPECT_TRUE(Part(*zip, "word/comments.xml").empty());
  EXPECT_EQ(Count(Part(*zip, "word/document.xml"), "commentReference"), 0u);
}
