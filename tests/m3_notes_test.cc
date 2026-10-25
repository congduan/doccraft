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

// Footnotes and endnotes: PlcffndRef/PlcffndTxt (FIB pairs 2/3) and
// PlcfendRef/PlcfendTxt (pairs 46/47) become w:footnoteReference /
// w:endnoteReference with matching footnotes.xml / endnotes.xml parts.

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

TEST(M3Notes, ReadsNoteBodiesAndReferences) {
  auto d = ParseFixture("footnote-endnote");
  ASSERT_TRUE(d) << d.error().message;
  // One footnote, then one endnote.
  ASSERT_EQ(d->notes.size(), 2u);
  EXPECT_FALSE(d->notes[0].endnote);
  EXPECT_FALSE(d->notes[0].paragraphs.empty());
  EXPECT_EQ(ParagraphText(d->notes[0].paragraphs[0]), "Footnote body text.");
  EXPECT_TRUE(d->notes[1].endnote);
  ASSERT_FALSE(d->notes[1].paragraphs.empty());
  EXPECT_EQ(ParagraphText(d->notes[1].paragraphs[0]), "Endnote body text.");

  // The 0x02 characters in the main story became reference runs, and their
  // text no longer leaks into the paragraph.
  ASSERT_EQ(d->paragraphs.size(), 2u);
  EXPECT_EQ(ParagraphText(d->paragraphs[0]),
            "Main paragraph with a footnote and more text.");
  EXPECT_EQ(ParagraphText(d->paragraphs[1]),
            "Second paragraph with an endnote.");
  const auto find_reference = [](const model::Paragraph& paragraph) {
    for (const auto& run : paragraph.runs) {
      if (run.note_ref >= 0) {
        return run;
      }
    }
    return model::Run{};
  };
  const model::Run footnote_ref = find_reference(d->paragraphs[0]);
  EXPECT_EQ(footnote_ref.note_ref, 1);
  EXPECT_FALSE(footnote_ref.note_endnote);
  const model::Run endnote_ref = find_reference(d->paragraphs[1]);
  EXPECT_EQ(endnote_ref.note_ref, 1);
  EXPECT_TRUE(endnote_ref.note_endnote);
}

TEST(M3Notes, WritesFootnoteAndEndnoteParts) {
  auto d = ParseFixture("footnote-endnote");
  ASSERT_TRUE(d) << d.error().message;
  auto zip = docx::Write(*d);
  ASSERT_TRUE(zip);
  const std::string document = Part(*zip, "word/document.xml");
  EXPECT_NE(document.find("<w:footnoteReference w:id=\"1\"/>"),
            std::string::npos);
  EXPECT_NE(document.find("<w:endnoteReference w:id=\"1\"/>"),
            std::string::npos);

  const std::string footnotes = Part(*zip, "word/footnotes.xml");
  ASSERT_FALSE(footnotes.empty());
  EXPECT_NE(footnotes.find("<w:footnote w:type=\"separator\" w:id=\"-1\">"),
            std::string::npos);
  EXPECT_NE(footnotes.find("<w:footnote w:type=\"continuationSeparator\" "
                           "w:id=\"0\">"),
            std::string::npos);
  EXPECT_EQ(Count(footnotes, "<w:footnote w:id="), 1u);
  EXPECT_NE(footnotes.find(">Footnote body text.</w:t>"), std::string::npos);
  EXPECT_EQ(footnotes.find("Endnote body text."), std::string::npos);

  const std::string endnotes = Part(*zip, "word/endnotes.xml");
  ASSERT_FALSE(endnotes.empty());
  EXPECT_EQ(Count(endnotes, "<w:endnote w:id="), 1u);
  EXPECT_NE(endnotes.find(">Endnote body text.</w:t>"), std::string::npos);

  const std::string types = Part(*zip, "[Content_Types].xml");
  EXPECT_NE(types.find("wordprocessingml.footnotes+xml"), std::string::npos);
  EXPECT_NE(types.find("wordprocessingml.endnotes+xml"), std::string::npos);
  const std::string rels = Part(*zip, "word/_rels/document.xml.rels");
  EXPECT_NE(rels.find("Target=\"footnotes.xml\""), std::string::npos);
  EXPECT_NE(rels.find("Target=\"endnotes.xml\""), std::string::npos);
}

TEST(M3Notes, DocumentWithoutNotesHasNoNoteParts) {
  model::Document d;
  model::Paragraph p;
  model::Run r;
  r.text = "plain";
  p.runs.push_back(r);
  d.paragraphs.push_back(p);
  auto zip = docx::Write(d);
  ASSERT_TRUE(zip);
  EXPECT_TRUE(Part(*zip, "word/footnotes.xml").empty());
  EXPECT_TRUE(Part(*zip, "word/endnotes.xml").empty());
  const std::string document = Part(*zip, "word/document.xml");
  EXPECT_EQ(Count(document, "noteReference"), 0u);
}
