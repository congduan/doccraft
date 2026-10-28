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

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "miniz.h"

#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
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

size_t PartCount(const std::vector<uint8_t>& archive, const char* name) {
  mz_zip_archive zip = {};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
    return 0;
  size_t count = 0;
  for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
    mz_zip_archive_file_stat stat = {};
    if (mz_zip_reader_file_stat(&zip, i, &stat) &&
        std::string(stat.m_filename) == name)
      ++count;
  }
  mz_zip_reader_end(&zip);
  return count;
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

TEST(M6WriterBuffer, HeaderPartCountsTowardTotalLimit) {
  model::Document document;
  model::Paragraph header;
  model::Run run;
  run.text = std::string(4096, 'h');
  header.runs.push_back(std::move(run));
  document.section.headers[1].push_back(std::move(header));

  constexpr size_t kGenerousLimit = 16384;
  auto baseline = docx::detail::WriteWithBufferLimit(document, kGenerousLimit);
  ASSERT_TRUE(baseline) << baseline.error().message;
  mz_zip_archive archive = {};
  ASSERT_TRUE(
      mz_zip_reader_init_mem(&archive, baseline->data(), baseline->size(), 0));
  size_t total = 0;
  size_t core = 0;
  for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&archive);
       ++index) {
    mz_zip_archive_file_stat stat = {};
    ASSERT_TRUE(mz_zip_reader_file_stat(&archive, index, &stat));
    total += stat.m_uncomp_size;
    if (std::string(stat.m_filename) != "word/header1.xml")
      core += stat.m_uncomp_size;
  }
  mz_zip_reader_end(&archive);
  ASSERT_GT(total, core + 1);

  auto zip = docx::detail::WriteWithBufferLimit(document, total - 1);
  ASSERT_FALSE(zip) << "writer returned a ZIP despite oversized total";
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

TEST(M6WriterBuffer, DoubleLimitReportsNamedPartFirst) {
  model::Document document;
  document.paragraphs.push_back({});
  document.paragraphs.back().runs.push_back({});
  document.paragraphs.back().runs.back().text = std::string(2048, 'x');
  constexpr size_t kGenerousLimit = 16384;
  auto baseline = docx::detail::WriteWithBufferLimit(document, kGenerousLimit);
  ASSERT_TRUE(baseline) << baseline.error().message;
  const size_t actual_bytes = Part(*baseline, "word/document.xml").size();
  // Both limits sit one byte below the document part size, so the same
  // AddPart("word/document.xml") call fails the part check and the total
  // check at once. Lock the diagnostic order: the named-part error is the
  // specific one and must win over the anonymous buffer error.
  auto zip = docx::detail::WriteWithBufferLimit(document, actual_bytes - 1,
                                                actual_bytes - 1);
  ASSERT_FALSE(zip) << "writer returned a ZIP despite oversized document";
  EXPECT_EQ(zip.error().message,
            "writer part limit exceeded: word/document.xml (" +
                std::to_string(actual_bytes) + " bytes)");
}

TEST(DocxWriterRelations, HeaderImageAndLinkUseOwnRelationships) {
  model::Document document;
  model::Image image;
  image.bytes = {'p', 'n', 'g'};
  image.width = 96;
  image.height = 96;
  document.images.push_back(image);
  model::Paragraph header;
  model::Run picture;
  picture.image_index = 0;
  header.runs.push_back(picture);
  model::Run link;
  link.text = "site";
  link.hyperlink = "https://example.com/header";
  header.runs.push_back(link);
  document.section.headers[1].push_back(header);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/header1.xml");
  const std::string rels = Part(*zip, "word/_rels/header1.xml.rels");
  const std::string docrels = Part(*zip, "word/_rels/document.xml.rels");
  EXPECT_NE(xml.find("xmlns:wp="), std::string::npos);
  EXPECT_NE(xml.find("xmlns:a="), std::string::npos);
  EXPECT_NE(xml.find("xmlns:pic="), std::string::npos);
  EXPECT_NE(xml.find("r:embed=\"rId1\""), std::string::npos);
  EXPECT_NE(xml.find("<w:hyperlink r:id=\"rId2\""), std::string::npos);
  EXPECT_NE(rels.find("Id=\"rId1\" Type=\"http://schemas."
                      "openxmlformats.org/officeDocument/2006/relationships/"
                      "image\" Target=\"media/image1.png\""),
            std::string::npos);
  EXPECT_NE(rels.find("Id=\"rId2\" Type=\"http://schemas."
                      "openxmlformats.org/officeDocument/2006/relationships/"
                      "hyperlink\" Target=\"https://example.com/header\" "
                      "TargetMode=\"External\""),
            std::string::npos);
  EXPECT_EQ(docrels.find("media/image1.png"), std::string::npos);
  EXPECT_EQ(docrels.find("https://example.com/header"), std::string::npos);
}

TEST(DocxWriterRelations, DuplicateBodyUrlReusesRidWithoutGap) {
  model::Document document;
  model::Paragraph paragraph;
  model::Run link;
  link.text = "first";
  link.hyperlink = "https://example.com/shared";
  paragraph.runs.push_back(link);
  link.text = "second";
  paragraph.runs.push_back(link);
  model::Run picture;
  picture.image_index = 0;
  paragraph.runs.push_back(picture);
  document.paragraphs.push_back(paragraph);
  model::Image image;
  image.bytes = {'p', 'n', 'g'};
  image.width = 96;
  image.height = 96;
  document.images.push_back(image);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/document.xml");
  const std::string rels = Part(*zip, "word/_rels/document.xml.rels");
  const std::string link_id = "<w:hyperlink r:id=\"rId3\"";
  ASSERT_NE(xml.find(link_id), std::string::npos);
  EXPECT_NE(xml.find(link_id, xml.find(link_id) + 1), std::string::npos);
  EXPECT_NE(xml.find("r:embed=\"rId4\""), std::string::npos);
  EXPECT_NE(rels.find("Id=\"rId3\""), std::string::npos);
  EXPECT_NE(rels.find("Id=\"rId4\""), std::string::npos);
  EXPECT_EQ(rels.find("Id=\"rId5\""), std::string::npos);
  EXPECT_EQ(rels.find("shared", rels.find("shared") + 1), std::string::npos);
}

TEST(DocxWriterRelations, PlainHeaderHasNoRelationshipsPart) {
  model::Document document;
  model::Paragraph header;
  model::Run text;
  text.text = "header";
  header.runs.push_back(text);
  document.section.headers[1].push_back(header);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_EQ(PartCount(*zip, "word/_rels/header1.xml.rels"), 0);
}

TEST(DocxWriterRelations, BodyAndFooterCanShareImage) {
  model::Document document;
  model::Image image;
  image.bytes = {'p', 'n', 'g'};
  image.width = 96;
  image.height = 96;
  document.images.push_back(image);
  model::Paragraph paragraph;
  model::Run picture;
  picture.image_index = 0;
  paragraph.runs.push_back(picture);
  document.paragraphs.push_back(paragraph);
  document.section.headers[3].push_back(paragraph);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  EXPECT_NE(Part(*zip, "word/document.xml").find("r:embed=\"rId3\""),
            std::string::npos);
  EXPECT_NE(Part(*zip, "word/footer1.xml").find("r:embed=\"rId1\""),
            std::string::npos);
  EXPECT_NE(Part(*zip, "word/_rels/document.xml.rels")
                .find("Id=\"rId3\" Type=\"http://schemas.openxmlformats."
                      "org/officeDocument/2006/relationships/image\" "
                      "Target=\"media/image1.png\""),
            std::string::npos);
  EXPECT_NE(Part(*zip, "word/_rels/footer1.xml.rels")
                .find("Id=\"rId1\" Type=\"http://schemas.openxmlformats."
                      "org/officeDocument/2006/relationships/image\" "
                      "Target=\"media/image1.png\""),
            std::string::npos);
  EXPECT_EQ(PartCount(*zip, "word/media/image1.png"), 1);
}

TEST(DocxWriterRelations, NotesAndCommentsOwnTheirLinks) {
  model::Document document;
  model::Paragraph paragraph;
  model::Run link;
  link.text = "site";
  link.hyperlink = "https://example.com/note";
  paragraph.runs.push_back(link);
  model::Note note;
  note.paragraphs.push_back(paragraph);
  document.notes.push_back(note);
  note.endnote = true;
  document.notes.push_back(note);
  model::Comment comment;
  comment.paragraphs.push_back(paragraph);
  document.comments.push_back(comment);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  for (const char* part : {"footnotes.xml", "endnotes.xml", "comments.xml"}) {
    const std::string xml = Part(*zip, ("word/" + std::string(part)).c_str());
    const std::string rels =
        Part(*zip, ("word/_rels/" + std::string(part) + ".rels").c_str());
    EXPECT_NE(xml.find("<w:hyperlink r:id=\"rId1\""), std::string::npos);
    EXPECT_NE(rels.find("Id=\"rId1\" Type=\"http://schemas."
                        "openxmlformats.org/officeDocument/2006/relationships/"
                        "hyperlink\" Target=\"https://example.com/note\" "
                        "TargetMode=\"External\""),
              std::string::npos);
  }
  EXPECT_EQ(Part(*zip, "word/_rels/document.xml.rels")
                .find("https://example.com/note"),
            std::string::npos);
}

TEST(DocxWriterRelations, StoryImagesDeclareDrawingNamespaces) {
  model::Document document;
  model::Image image;
  image.bytes = {'p', 'n', 'g'};
  image.width = 96;
  image.height = 96;
  document.images.push_back(image);
  model::Paragraph paragraph;
  model::Run picture;
  picture.image_index = 0;
  paragraph.runs.push_back(picture);
  model::Note note;
  note.paragraphs.push_back(paragraph);
  document.notes.push_back(note);
  note.endnote = true;
  document.notes.push_back(note);
  model::Comment comment;
  comment.paragraphs.push_back(paragraph);
  document.comments.push_back(comment);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  for (const char* part : {"footnotes.xml", "endnotes.xml", "comments.xml"}) {
    const std::string xml = Part(*zip, ("word/" + std::string(part)).c_str());
    const std::string rels =
        Part(*zip, ("word/_rels/" + std::string(part) + ".rels").c_str());
    EXPECT_NE(xml.find("xmlns:wp="), std::string::npos) << part;
    EXPECT_NE(xml.find("xmlns:a="), std::string::npos) << part;
    EXPECT_NE(xml.find("xmlns:pic="), std::string::npos) << part;
    EXPECT_NE(xml.find("r:embed=\"rId1\""), std::string::npos) << part;
    EXPECT_NE(rels.find("Target=\"media/image1.png\""), std::string::npos)
        << part;
  }
}

TEST(DocxWriterRelations, HeaderNestedImageOleAndShapeDeclareNamespaces) {
  model::Document document;
  model::Image image;
  image.bytes = {'p', 'n', 'g'};
  image.width = 96;
  image.height = 96;
  document.images.push_back(image);
  model::Textbox box;
  box.paragraphs.push_back({});
  model::Run picture;
  picture.image_index = 0;
  box.paragraphs.back().runs.push_back(picture);
  document.textboxes.push_back(box);
  model::OleObject object;
  object.bytes = {'o', 'l', 'e'};
  object.program_id = "Package";
  document.ole_objects.push_back(object);
  model::Shape shape;
  shape.type = model::kShapeRectangle;
  shape.width = 1440;
  shape.height = 720;
  document.shapes.push_back(shape);
  model::Paragraph header;
  model::Run anchor;
  anchor.textbox_index = 0;
  header.runs.push_back(anchor);
  model::Run ole;
  ole.ole_index = 0;
  header.runs.push_back(ole);
  model::Run drawing;
  drawing.shape_index = 0;
  header.runs.push_back(drawing);
  document.section.headers[1].push_back(header);
  auto zip = docx::Write(document);
  ASSERT_TRUE(zip) << zip.error().message;
  const std::string xml = Part(*zip, "word/header1.xml");
  const std::string rels = Part(*zip, "word/_rels/header1.xml.rels");
  for (const char* prefix : {"wp", "a", "pic", "v", "o", "w10"})
    EXPECT_NE(xml.find("xmlns:" + std::string(prefix) + "="), std::string::npos)
        << prefix;
  EXPECT_NE(xml.find("<w:txbxContent>"), std::string::npos);
  EXPECT_NE(xml.find("<o:OLEObject"), std::string::npos);
  EXPECT_NE(xml.find("<w10:wrap"), std::string::npos);
  EXPECT_NE(rels.find("Target=\"media/image1.png\""), std::string::npos);
  EXPECT_NE(rels.find("Target=\"embeddings/oleObject1.bin\""),
            std::string::npos);
}

TEST(M4Textbox, LibreOfficeFloatingTextboxKeepsStoryAtAnchor) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/textbox.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb) << cfb.error().message;
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word) << word.error().message;
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table) << table.error().message;
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1U);
  ASSERT_EQ(parsed->textboxes[0].paragraphs.size(), 1U);
  ASSERT_EQ(parsed->textboxes[0].paragraphs[0].runs.size(), 1U);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text, "Box sample line");
  auto written = docx::Write(*parsed);
  ASSERT_TRUE(written) << written.error().message;
  const std::string xml = Part(*written, "word/document.xml");
  EXPECT_NE(xml.find("<w:txbxContent>"), std::string::npos);
  EXPECT_NE(xml.find("Box sample line"), std::string::npos);
  EXPECT_NE(xml.find("<w:pict>"), std::string::npos);
}

TEST(M4Textbox, HeaderTextboxAppearsInHeaderPart) {
  const std::string path =
      std::string(DOCCRAFT_SOURCE_DIR) + "/tests/fixtures/header-textbox.doc";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  auto cfb = cfb::CfbReader::Open(bytes);
  ASSERT_TRUE(cfb) << cfb.error().message;
  auto word = cfb->ReadStream("WordDocument");
  ASSERT_TRUE(word) << word.error().message;
  auto fib = doc::ParseFib(*word);
  ASSERT_TRUE(fib) << fib.error().message;
  auto table = cfb->ReadStream(fib->table_stream());
  ASSERT_TRUE(table) << table.error().message;
  auto parsed = doc::ParseFormatted(*word, *table, *fib);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed->textboxes.size(), 1U);
  EXPECT_EQ(parsed->textboxes[0].paragraphs[0].runs[0].text, "Header box text");
  auto written = docx::Write(*parsed);
  ASSERT_TRUE(written) << written.error().message;
  bool found = false;
  for (int i = 1; i <= 6; ++i) {
    const auto xml =
        Part(*written, ("word/header" + std::to_string(i) + ".xml").c_str());
    found |= xml.find("Header box text") != std::string::npos &&
             xml.find("<w:txbxContent>") != std::string::npos;
  }
  EXPECT_TRUE(found);
}

}  // namespace
