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

// Bookmarks: SttbfBkmk names (FIB pair 21), PlcfBkf starts (pair 22) and
// PlcfBkl ends (pair 23) become w:bookmarkStart / w:bookmarkEnd markers at
// their CPs in the main story.

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
}  // namespace

TEST(M3Bookmarks, ReadsNamesAndPositions) {
  auto d = ParseFixture("bookmarks-basic");
  ASSERT_TRUE(d) << d.error().message;
  ASSERT_EQ(d->paragraphs.size(), 2u);
  // The bookmark text stays intact; markers are separate runs.
  EXPECT_EQ(ParagraphText(d->paragraphs[0]), "Intro marked words tail.");
  EXPECT_EQ(ParagraphText(d->paragraphs[1]), "Point paragraph.");

  // MarkRange wraps "marked words": start before it, end after it.
  const auto& first = d->paragraphs[0].runs;
  int start_at = -1, end_at = -1, text_at = -1;
  for (size_t i = 0; i < first.size(); i++) {
    if (first[i].bookmark_id == 0 && first[i].bookmark_start) {
      start_at = static_cast<int>(i);
    }
    if (first[i].bookmark_id == 0 && !first[i].bookmark_start) {
      end_at = static_cast<int>(i);
    }
    if (first[i].text == "marked words") {
      text_at = static_cast<int>(i);
    }
  }
  EXPECT_NE(start_at, -1);
  EXPECT_NE(end_at, -1);
  ASSERT_NE(text_at, -1);
  EXPECT_LT(start_at, text_at);
  EXPECT_LT(text_at, end_at);
  EXPECT_EQ(first[start_at].bookmark_name, "MarkRange");

  // MarkPoint is zero-length: start and end adjacent, same id.
  int point_start = -1, point_end = -1;
  for (size_t i = 0; i < d->paragraphs[1].runs.size(); i++) {
    const auto& run = d->paragraphs[1].runs[i];
    if (run.bookmark_id == 1 && run.bookmark_start) {
      point_start = static_cast<int>(i);
    }
    if (run.bookmark_id == 1 && !run.bookmark_start) {
      point_end = static_cast<int>(i);
    }
  }
  EXPECT_NE(point_start, -1);
  EXPECT_EQ(point_end, point_start + 1);
  EXPECT_EQ(d->paragraphs[1].runs[point_start].bookmark_name, "MarkPoint");
}

TEST(M3Bookmarks, WritesBookmarkElements) {
  auto d = ParseFixture("bookmarks-basic");
  ASSERT_TRUE(d) << d.error().message;
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  const std::string document = Part(*zip, "word/document.xml");
  EXPECT_NE(document.find("<w:bookmarkStart w:id=\"0\" w:name=\"MarkRange\"/>"),
            std::string::npos);
  EXPECT_NE(document.find("<w:bookmarkEnd w:id=\"0\"/>"), std::string::npos);
  EXPECT_NE(document.find("<w:bookmarkStart w:id=\"1\" w:name=\"MarkPoint\"/>"),
            std::string::npos);
  // The wrapped text sits between the range markers.
  const size_t start = document.find("bookmarkStart w:id=\"0\"");
  const size_t text = document.find(">marked words<");
  const size_t end = document.find("bookmarkEnd w:id=\"0\"");
  ASSERT_LT(start, text);
  ASSERT_LT(text, end);
  // No bookmark part is created: bookmarks live in the body.
  EXPECT_TRUE(Part(*zip, "word/bookmarks.xml").empty());
}

TEST(M3Bookmarks, DocumentWithoutBookmarksStaysClean) {
  model::Document d;
  model::Paragraph p;
  model::Run r;
  r.text = "plain";
  p.runs.push_back(r);
  d.paragraphs.push_back(p);
  auto zip = docx::Write(d);
  ASSERT_TRUE(zip);
  EXPECT_EQ(Part(*zip, "word/document.xml").find("bookmark"),
            std::string::npos);
}
