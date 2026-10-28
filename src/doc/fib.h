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

#ifndef SRC_DOC_FIB_H_
#define SRC_DOC_FIB_H_
#include <cstdint>
#include <expected>
#include <span>
#include <string>
namespace doccraft::doc {
struct Error {
  std::string message;
};

struct Fib {
  uint16_t n_fib = 0;
  bool legacy = false;
  uint32_t fc_min = 0;
  // FibBase.lid selects the ANSI code page of compressed ("8-bit") pieces.
  uint16_t lid = 0;
  uint32_t codepage = 1252;
  bool complex = false;
  bool encrypted = false;
  bool which_table = false;
  uint32_t ccp_text = 0, ccp_ftn = 0, ccp_hdd = 0, ccp_atn = 0;
  uint32_t ccp_edn = 0, ccp_txbx = 0, ccp_hdr_txbx = 0;
  uint32_t fc_clx = 0, lcb_clx = 0;
  struct Range {
    uint32_t offset = 0, size = 0;
    bool operator==(const Range&) const = default;
  };
  Range stsh, chpx_bte, papx_bte, sed, fonts, lists, list_overrides, headers;
  // Footnote/endnote reference and text PLCs ([MS-DOC] §2.8.4, §2.8.5).
  Range fnd_ref, fnd_txt, end_ref, end_txt;
  // Comment anchors (PlcfandRef), comment text (PlcfandTxt) and the author
  // name array (GrpXstAtnOwners).
  Range and_ref, and_txt, atn_owners;
  // Bookmark name table (SttbfBkmk) and the start/end PLCs (PlcfBkf/PlcfBkl).
  Range sttb_bkmk, bkf, bkl;
  // Revision author names (SttbfRMark = FibRgFcLcb97 pair(51), right after
  // fcDggInfo = pair(50); pair(24) is fcCmds), shared by insert/delete/property
  // revision marks. Comments keep their own GrpXstAtnOwners table above.
  Range rmark_authors;
  Range spa_main, spa_hdr, drawing_group, txbx_txt, txbx_bkd;
  Range hdr_txbx_txt, hdr_txbx_bkd;
  // Field character PLCs ([MS-DOC] §2.5.10): main, header, footnote, comment
  // and endnote stories each carry their own Plcfld.
  Range fields_main, fields_hdr, fields_ftn, fields_atn, fields_edn;
  Range fields_txbx, fields_hdr_txbx;
  std::string table_stream() const {
    return which_table ? "1Table" : "0Table";
  }
};

std::expected<Fib, Error> ParseFib(std::span<const uint8_t> word);
uint32_t ReadSummaryCodepage(std::span<const uint8_t> summary);
}  // namespace doccraft::doc

#endif  // SRC_DOC_FIB_H_
