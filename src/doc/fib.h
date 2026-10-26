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
  };
  Range stsh, chpx_bte, papx_bte, sed, fonts, lists, list_overrides, headers;
  // Footnote/endnote reference and text PLCs ([MS-DOC] §2.8.4, §2.8.5).
  Range fnd_ref, fnd_txt, end_ref, end_txt;
  // Comment anchors (PlcfandRef), comment text (PlcfandTxt) and the author
  // name array (GrpXstAtnOwners).
  Range and_ref, and_txt, atn_owners;
  // Bookmark name table (SttbfBkmk) and the start/end PLCs (PlcfBkf/PlcfBkl).
  Range sttb_bkmk, bkf, bkl;
  Range spa_main, drawing_group;
  std::string table_stream() const {
    return which_table ? "1Table" : "0Table";
  }
};

std::expected<Fib, Error> ParseFib(std::span<const uint8_t> word);
}  // namespace doccraft::doc

#endif  // SRC_DOC_FIB_H_
