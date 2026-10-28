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

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "doc/fib.h"
#include "doc/format/internal.h"
#include "model/document.h"

namespace doccraft::doc::detail {

// SttbfRMark ([MS-DOC] 2.9.290) names the authors of revision marks: an
// extended STTB whose fExtend MUST be 0xFFFF, whose cData is 2 bytes, and
// whose cbExtra MUST be 0. The first entry is always "Unknown" (ibst 0, the
// default for a run that carries no sprmCIbstRMark). Comments use a
// different table (GrpXstAtnOwners) and stay on their own path.
std::expected<std::vector<std::string>, Error> ReadRevisionAuthors(
    Bytes table, Fib::Range range) {
  std::vector<std::string> names;
  if (!range.size)
    return names;  // document carries no author table
  if (range.offset > table.size() || range.size > table.size() - range.offset)
    return std::unexpected(Error{"SttbfRMark outside table stream"});
  Bytes b = table.subspan(range.offset, range.size);
  if (b.size() < 6)
    return std::unexpected(Error{"truncated SttbfRMark"});
  const uint16_t extend = U16(b, 0), count = U16(b, 2), extra = U16(b, 4);
  if (extend != 0xFFFF || extra != 0)
    return std::unexpected(Error{"invalid SttbfRMark header"});
  size_t p = 6;
  for (size_t i = 0; i < count; i++) {
    if (p + 2 > b.size())
      return std::unexpected(Error{"truncated SttbfRMark"});
    const size_t chars = U16(b, p);
    p += 2;
    if (chars > (b.size() - p) / 2)
      return std::unexpected(Error{"truncated SttbfRMark"});
    std::string name;
    for (size_t k = 0; k < chars; k++)
      AppendUtf8(name, U16(b, p + 2 * k));
    p += chars * 2;
    names.push_back(std::move(name));
  }
  return names;
}

namespace {

struct RevisionAudit {
  bool any_mark = false;
  std::set<int> bad_indices;
};

// Author indices are only consulted for runs that carry the matching mark,
// so an unset index keeps its spec default 0 silently.
void AuditRun(const model::Run& run, size_t count, RevisionAudit* audit) {
  const bool marks[3] = {run.revision_insert, run.revision_delete,
                         run.property_revision};
  const int indices[3] = {run.revision_author_insert,
                          run.revision_author_delete, run.revision_property};
  for (size_t i = 0; i < 3; i++) {
    audit->any_mark |= marks[i];
    if (marks[i] &&
        (indices[i] < 0 || static_cast<size_t>(indices[i]) >= count))
      audit->bad_indices.insert(indices[i]);
  }
}

void AuditParagraphs(const std::vector<model::Paragraph>& paragraphs,
                     size_t count, RevisionAudit* audit) {
  for (const auto& paragraph : paragraphs) {
    for (const auto& run : paragraph.runs)
      AuditRun(run, count, audit);
  }
}

}  // namespace

// Walks every run container once and reports, as warnings, the two failure
// modes the writer otherwise degrades silently: revision marks without any
// SttbfRMark, and author indices outside the table. The writer maps every
// bad index to the placeholder author, so the warning is the only trace.
void ValidateRevisionAuthors(const model::Document& doc,
                             std::vector<std::string>* warnings) {
  const size_t count = doc.revision_authors.size();
  RevisionAudit audit;
  AuditParagraphs(doc.paragraphs, count, &audit);
  for (const auto& style : doc.styles) {
    AuditRun(style.run, count, &audit);
    for (const auto& conditional : style.conditionals)
      AuditRun(conditional.run, count, &audit);
  }
  for (const auto& break_entry : doc.section_breaks) {
    for (const auto& story : break_entry.second.headers)
      AuditParagraphs(story, count, &audit);
  }
  for (const auto& story : doc.section.headers)
    AuditParagraphs(story, count, &audit);
  for (const auto& note : doc.notes)
    AuditParagraphs(note.paragraphs, count, &audit);
  for (const auto& comment : doc.comments)
    AuditParagraphs(comment.paragraphs, count, &audit);
  for (const auto& box : doc.textboxes)
    AuditParagraphs(box.paragraphs, count, &audit);
  if (audit.any_mark && count == 0)
    warnings->push_back(
        "revision marks without SttbfRMark; revision authors default to "
        "\"doccraft\"");
  if (!audit.bad_indices.empty())
    warnings->push_back(
        "SttbfRMark author index out of range; revision author defaults to "
        "\"doccraft\"");
}

}  // namespace doccraft::doc::detail
