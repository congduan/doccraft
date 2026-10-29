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

#ifndef TESTS_M20_DOC_UTIL_H_
#define TESTS_M20_DOC_UTIL_H_

// Synthetic Word 97 byte-stream builder shared by the edge-corpus generator
// and the M20 malformed-input regression suite. The layout mirrors the
// hand-assembled fixtures in tests/m3_nested_table_test.cc: a WordDocument
// with the FIB and text runs, a 0Table stream holding the CLX (and optional
// style sheet), and PAPX FKP pages addressed by a PlcfBtePapx.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tests/cfb_test_util.h"

namespace doccraft::test {

inline constexpr uint32_t kDocTextFc = 2048;
inline constexpr uint32_t kDocPageSize = 512;
inline constexpr uint32_t kCompressedBit = 0x40000000u;
inline constexpr uint32_t kFcMask = 0x3FFFFFFFu;

// One piece of the CLX piece table. `byte_offset` is relative to the text
// block (kDocTextFc); compressed pieces consume one byte per CP.
struct Piece {
  uint32_t cp_first = 0;
  uint32_t cp_last = 0;
  uint32_t byte_offset = 0;
  bool compressed = true;
  uint16_t prm = 0;
};

// PAPX sprms applied to the paragraph ending at `cp` (the CP of the 0x0D or
// 0x07 mark itself).
struct PapxEntry {
  uint32_t cp = 0;
  std::vector<uint8_t> sprms;
};

// One STD record. Type 1 = paragraph, 2 = character, 3 = table.
struct StyleSpec {
  std::string name;
  int type = 1;
  int base = -1;
  int next = -1;
  std::vector<uint8_t> paragraph_sprms;
  std::vector<uint8_t> run_sprms;
};

struct DocSpec {
  std::vector<uint8_t> text;
  std::vector<Piece> pieces;  // Empty: one compressed piece over `text`.
  std::vector<PapxEntry> papx;
  std::vector<std::vector<uint8_t>> prcs;  // CLX Prc grpprls.
  std::vector<StyleSpec> styles;
  std::vector<uint8_t> data;
  // End CP of each section (PlcfSed). Empty means the document has the
  // implicit single section only.
  std::vector<uint32_t> section_ends;
  int64_t ccp_override = -1;  // -1 derives the count from the pieces.
  uint32_t fc_min = kDocTextFc;
  uint16_t ident = 0xA5EC;
  uint16_t n_fib = 0x00C1;
  uint16_t lid = 0x0409;
  bool complex = true;
  bool include_clx = true;
  // Added to the Pcdt cbPlcPcd, to build a size/coverage mismatch.
  int32_t pcdt_size_delta = 0;
};

struct DocStreams {
  std::vector<uint8_t> word;
  std::vector<uint8_t> table;
  std::vector<uint8_t> data;
};

inline void Put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
  b[at] = static_cast<uint8_t>(v);
  b[at + 1] = static_cast<uint8_t>(v >> 8);
}

inline void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  b[at] = static_cast<uint8_t>(v);
  b[at + 1] = static_cast<uint8_t>(v >> 8);
  b[at + 2] = static_cast<uint8_t>(v >> 16);
  b[at + 3] = static_cast<uint8_t>(v >> 24);
}

inline uint16_t Get16(const std::vector<uint8_t>& b, size_t at) {
  return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

inline uint32_t Get32(const std::vector<uint8_t>& b, size_t at) {
  return static_cast<uint32_t>(b[at]) |
         (static_cast<uint32_t>(b[at + 1]) << 8) |
         (static_cast<uint32_t>(b[at + 2]) << 16) |
         (static_cast<uint32_t>(b[at + 3]) << 24);
}

inline size_t AlignUp(size_t value, size_t unit) {
  return (value + unit - 1) / unit * unit;
}

// A single PRL: opcode followed by operand bytes.
inline std::vector<uint8_t> Prl(uint16_t op, std::vector<uint8_t> value) {
  std::vector<uint8_t> out{static_cast<uint8_t>(op),
                           static_cast<uint8_t>(op >> 8)};
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

// PAPX payload: one length byte, a two-byte istd, then the grpprl. The length
// counts 16-bit words, so an even body gets a trailing alignment byte.
inline std::vector<uint8_t> PapxPayload(const std::vector<uint8_t>& sprms) {
  std::vector<uint8_t> body{0, 0};
  body.insert(body.end(), sprms.begin(), sprms.end());
  if ((body.size() & 1) == 0) {
    body.push_back(0);
  }
  std::vector<uint8_t> out;
  out.push_back(static_cast<uint8_t>((body.size() + 1) / 2));
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

struct CpLayout {
  uint32_t fc = 0;
  uint8_t width = 1;  // WordDocument bytes consumed by this CP.
};

inline std::vector<Piece> EffectivePieces(const DocSpec& spec,
                                          uint32_t ccp_text) {
  if (!spec.pieces.empty()) {
    return spec.pieces;
  }
  Piece piece;
  piece.cp_first = 0;
  piece.cp_last = ccp_text;
  piece.byte_offset = 0;
  piece.compressed = true;
  return {piece};
}

inline uint32_t EffectiveCcp(const DocSpec& spec) {
  if (spec.ccp_override >= 0) {
    return static_cast<uint32_t>(spec.ccp_override);
  }
  if (!spec.pieces.empty()) {
    return spec.pieces.back().cp_last;
  }
  return static_cast<uint32_t>(spec.text.size());
}

// CP -> (FC, byte width) for every character position.
inline std::vector<CpLayout> Layout(const DocSpec& spec, uint32_t ccp_text) {
  std::vector<CpLayout> layout(ccp_text);
  const auto pieces = EffectivePieces(spec, ccp_text);
  for (const auto& piece : pieces) {
    const uint32_t width = piece.compressed ? 1 : 2;
    for (uint32_t cp = piece.cp_first; cp < piece.cp_last && cp < ccp_text;
         ++cp) {
      layout[cp].fc =
          kDocTextFc + piece.byte_offset + (cp - piece.cp_first) * width;
      layout[cp].width = static_cast<uint8_t>(width);
    }
  }
  return layout;
}

// Builds the CLX: optional Prc array followed by the Pcdt.
inline std::vector<uint8_t> BuildClx(const DocSpec& spec, uint32_t ccp_text) {
  const auto pieces = EffectivePieces(spec, ccp_text);
  std::vector<uint8_t> clx;
  for (const auto& prc : spec.prcs) {
    clx.push_back(1);
    clx.push_back(static_cast<uint8_t>(prc.size()));
    clx.push_back(static_cast<uint8_t>(prc.size() >> 8));
    clx.insert(clx.end(), prc.begin(), prc.end());
  }
  clx.push_back(2);
  const uint32_t nbytes =
      static_cast<uint32_t>(4 * (pieces.size() + 1) + 8 * pieces.size());
  clx.push_back(static_cast<uint8_t>(nbytes));
  clx.push_back(static_cast<uint8_t>(nbytes >> 8));
  clx.push_back(static_cast<uint8_t>(nbytes >> 16));
  clx.push_back(static_cast<uint8_t>(nbytes >> 24));
  for (const auto& piece : pieces) {
    const uint32_t cp = piece.cp_first;
    clx.push_back(static_cast<uint8_t>(cp));
    clx.push_back(static_cast<uint8_t>(cp >> 8));
    clx.push_back(static_cast<uint8_t>(cp >> 16));
    clx.push_back(static_cast<uint8_t>(cp >> 24));
  }
  const uint32_t last_cp = pieces.empty() ? ccp_text : pieces.back().cp_last;
  clx.push_back(static_cast<uint8_t>(last_cp));
  clx.push_back(static_cast<uint8_t>(last_cp >> 8));
  clx.push_back(static_cast<uint8_t>(last_cp >> 16));
  clx.push_back(static_cast<uint8_t>(last_cp >> 24));
  for (const auto& piece : pieces) {
    clx.push_back(0);  // PCD flags
    clx.push_back(0);
    const uint32_t fc = kDocTextFc + piece.byte_offset;
    uint32_t value = piece.compressed ? (kCompressedBit | (fc * 2)) : fc;
    clx.push_back(static_cast<uint8_t>(value));
    clx.push_back(static_cast<uint8_t>(value >> 8));
    clx.push_back(static_cast<uint8_t>(value >> 16));
    clx.push_back(static_cast<uint8_t>(value >> 24));
    clx.push_back(static_cast<uint8_t>(piece.prm));
    clx.push_back(static_cast<uint8_t>(piece.prm >> 8));
  }
  if (spec.pcdt_size_delta != 0) {
    // Patch the declared cbPlcPcd after the optional Prc prefix.
    size_t at = 0;
    for (const auto& prc : spec.prcs) {
      at += 3 + prc.size();
    }
    const uint32_t declared = static_cast<uint32_t>(
        static_cast<int64_t>(nbytes) + spec.pcdt_size_delta);
    clx[at + 1] = static_cast<uint8_t>(declared);
    clx[at + 2] = static_cast<uint8_t>(declared >> 8);
    clx[at + 3] = static_cast<uint8_t>(declared >> 16);
    clx[at + 4] = static_cast<uint8_t>(declared >> 24);
  }
  return clx;
}

// Encodes a UTF-8 string as UTF-16LE code units (BMP only; the generated
// style names are ASCII).
inline std::u16string Utf16(const std::string& text) {
  std::u16string out;
  for (char c : text) {
    out.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
  }
  return out;
}

// A minimal STSH: 18-byte STSHI header, then one STD record per style.
inline std::vector<uint8_t> BuildStsh(const std::vector<StyleSpec>& styles) {
  std::vector<uint8_t> out(20, 0);
  Put16(out, 0, 18);  // cbStshi
  Put16(out, 2, static_cast<uint16_t>(styles.size()));
  Put16(out, 4, 10);  // cbSTDBaseInFile
  for (const auto& style : styles) {
    std::vector<uint8_t> body(10, 0);
    const unsigned base = style.base < 0 ? 0x0FFF : style.base;
    Put16(body, 2, static_cast<uint16_t>(style.type | (base << 4)));
    const unsigned upx = style.type == 1 ? 2u : (style.type == 2 ? 1u : 0u);
    const unsigned next = style.next < 0 ? 0x0FFF : style.next;
    Put16(body, 4, static_cast<uint16_t>((next << 4) | upx));
    const std::u16string name = Utf16(style.name);
    body.resize(12, 0);
    Put16(body, 10, static_cast<uint16_t>(name.size()));
    for (char16_t c : name) {
      body.push_back(static_cast<uint8_t>(c));
      body.push_back(static_cast<uint8_t>(c >> 8));
    }
    body.push_back(0);
    body.push_back(0);
    auto append_upx = [&](const std::vector<uint8_t>& payload) {
      Put16(body, body.size(), static_cast<uint16_t>(payload.size()));
      body.insert(body.end(), payload.begin(), payload.end());
      if (body.size() & 1) {
        body.push_back(0);
      }
    };
    if (style.type == 1) {
      std::vector<uint8_t> papx{0, 0};
      papx.insert(papx.end(), style.paragraph_sprms.begin(),
                  style.paragraph_sprms.end());
      append_upx(papx);
      append_upx(style.run_sprms);
    } else if (style.type == 2) {
      append_upx(style.run_sprms);
    }
    out.push_back(static_cast<uint8_t>(body.size()));
    out.push_back(static_cast<uint8_t>(body.size() >> 8));
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() & 1) {
      out.push_back(0);
    }
  }
  return out;
}

inline uint32_t CharAt(const DocSpec& spec, const std::vector<CpLayout>& layout,
                       uint32_t cp) {
  if (cp >= layout.size()) {
    return 0;
  }
  const size_t at = layout[cp].fc - kDocTextFc;
  if (layout[cp].width == 1) {
    return at < spec.text.size() ? spec.text[at] : 0;
  }
  if (at + 1 >= spec.text.size()) {
    return 0;
  }
  return static_cast<uint32_t>(spec.text[at] | (spec.text[at + 1] << 8));
}

struct FkpRun {
  uint32_t first = 0;
  uint32_t last = 0;
  std::vector<uint8_t> papx;
};

// Writes one PAPX FKP page. `runs` must hold at most 10 entries so rgfc +
// rgbx + payload stay below the 511-byte limit.
inline std::vector<uint8_t> BuildFkpPage(const std::vector<FkpRun>& runs) {
  std::vector<uint8_t> page(kDocPageSize, 0);
  const size_t n = runs.size();
  for (size_t i = 0; i < n; ++i) {
    Put32(page, 4 * i, runs[i].first);
  }
  Put32(page, 4 * n, runs[n - 1].last);
  size_t data = 4 * (n + 1) + 13 * n;
  if (data & 1) {
    ++data;
  }
  for (size_t i = 0; i < n; ++i) {
    page[4 * (n + 1) + 13 * i] = static_cast<uint8_t>(data / 2);
    std::copy(runs[i].papx.begin(), runs[i].papx.end(),
              page.begin() + static_cast<ptrdiff_t>(data));
    data += runs[i].papx.size();
    if (data & 1) {
      ++data;
    }
  }
  page[511] = static_cast<uint8_t>(n);
  return page;
}

inline DocStreams BuildDocStreams(const DocSpec& spec) {
  const uint32_t ccp = EffectiveCcp(spec);
  const auto layout = Layout(spec, ccp);

  std::vector<uint32_t> marks;
  for (uint32_t cp = 0; cp < ccp; ++cp) {
    const uint32_t c = CharAt(spec, layout, cp);
    if (c == 0x0D || c == 0x07) {
      marks.push_back(cp);
    }
  }
  std::vector<FkpRun> runs;
  uint32_t previous_end = layout.empty() ? spec.fc_min : layout.front().fc;
  for (uint32_t cp : marks) {
    FkpRun run;
    run.first = previous_end;
    run.last = layout[cp].fc + layout[cp].width;
    for (const auto& entry : spec.papx) {
      if (entry.cp == cp) {
        run.papx = PapxPayload(entry.sprms);
        break;
      }
    }
    runs.push_back(run);
    previous_end = run.last;
  }
  const bool has_papx = !spec.papx.empty() && !runs.empty();

  const uint32_t text_end =
      kDocTextFc + static_cast<uint32_t>(spec.text.size());
  const size_t fkp_base =
      AlignUp(std::max<uint32_t>(text_end, kDocTextFc), kDocPageSize);
  const size_t page_count = has_papx ? (runs.size() + 9) / 10 : 0;
  const size_t word_size = fkp_base + page_count * kDocPageSize;
  std::vector<uint8_t> word(word_size, 0);
  std::copy(spec.text.begin(), spec.text.end(),
            word.begin() + static_cast<ptrdiff_t>(kDocTextFc));

  std::vector<uint32_t> page_fcs;
  if (has_papx) {
    for (size_t page = 0; page < page_count; ++page) {
      const size_t begin = page * 10;
      const size_t count = std::min<size_t>(10, runs.size() - begin);
      std::vector<FkpRun> chunk(
          runs.begin() + static_cast<ptrdiff_t>(begin),
          runs.begin() + static_cast<ptrdiff_t>(begin + count));
      const auto bytes = BuildFkpPage(chunk);
      const size_t page_number = fkp_base / kDocPageSize + page;
      std::copy(
          bytes.begin(), bytes.end(),
          word.begin() + static_cast<ptrdiff_t>(page_number * kDocPageSize));
      page_fcs.push_back(chunk.front().first);
    }
    page_fcs.push_back(runs.back().last);
  }

  // FIB base, RgW/RgLw counts and the fixed FibRgFcLcb97 pair count.
  Put16(word, 0, spec.ident);
  Put16(word, 2, spec.n_fib);
  Put16(word, 6, spec.lid);
  Put16(word, 10, spec.complex ? 0x0004 : 0);
  Put32(word, 24, spec.fc_min);
  Put32(word, 28, text_end);
  Put16(word, 32, 14);
  Put16(word, 62, 22);
  Put32(word, 76, ccp);
  Put16(word, 152, 93);

  std::vector<uint8_t> table;
  const std::vector<uint8_t> clx = BuildClx(spec, ccp);
  size_t bte_offset = 0, bte_size = 0;
  if (spec.include_clx) {
    table.insert(table.end(), clx.begin(), clx.end());
  }
  if (has_papx) {
    bte_offset = AlignUp(table.size(), 4);
    table.resize(bte_offset, 0);
    bte_size = 4 * page_fcs.size() + 4 * page_count;
    for (uint32_t fc : page_fcs) {
      table.push_back(static_cast<uint8_t>(fc));
      table.push_back(static_cast<uint8_t>(fc >> 8));
      table.push_back(static_cast<uint8_t>(fc >> 16));
      table.push_back(static_cast<uint8_t>(fc >> 24));
    }
    for (size_t page = 0; page < page_count; ++page) {
      const uint32_t page_number =
          static_cast<uint32_t>(fkp_base / kDocPageSize + page);
      table.push_back(static_cast<uint8_t>(page_number));
      table.push_back(static_cast<uint8_t>(page_number >> 8));
      table.push_back(static_cast<uint8_t>(page_number >> 16));
      table.push_back(static_cast<uint8_t>(page_number >> 24));
    }
  }
  size_t stsh_offset = 0, stsh_size = 0;
  if (!spec.styles.empty()) {
    stsh_offset = AlignUp(table.size(), 4);
    table.resize(stsh_offset, 0);
    const auto stsh = BuildStsh(spec.styles);
    stsh_size = stsh.size();
    table.insert(table.end(), stsh.begin(), stsh.end());
  }
  size_t sed_offset = 0, sed_size = 0;
  if (!spec.section_ends.empty()) {
    sed_offset = AlignUp(table.size(), 4);
    table.resize(sed_offset, 0);
    const size_t n = spec.section_ends.size();
    sed_size = 4 * (n + 1) + 12 * n;
    table.push_back(0);
    table.push_back(0);
    table.push_back(0);
    table.push_back(0);
    for (uint32_t cp : spec.section_ends) {
      for (int b = 0; b < 4; ++b) {
        table.push_back(static_cast<uint8_t>(cp >> (8 * b)));
      }
    }
    // One SED per section: fn (2), fcSepx (4, undefined), fnMpr (2),
    // fcMpr (4, undefined).
    for (size_t i = 0; i < n; ++i) {
      for (int b = 0; b < 12; ++b) {
        table.push_back(0xFF);
      }
    }
  }

  auto set_pair = [&](size_t index, size_t offset, size_t size) {
    Put32(word, 154 + index * 8, static_cast<uint32_t>(offset));
    Put32(word, 158 + index * 8, static_cast<uint32_t>(size));
  };
  set_pair(0, stsh_offset, stsh_size);
  set_pair(6, sed_offset, sed_size);
  set_pair(13, bte_offset, bte_size);
  if (spec.include_clx) {
    set_pair(33, 0, clx.size());
  }
  return {word, table, spec.data};
}

inline std::vector<uint8_t> BuildCfb(const DocStreams& streams) {
  cfb::test::CfbBuilder builder;
  builder.AddStream("WordDocument", streams.word);
  builder.AddStream("0Table", streams.table);
  if (!streams.data.empty()) {
    builder.AddStream("Data", streams.data);
  }
  return builder.Build();
}

inline std::vector<uint8_t> BuildDocFile(const DocSpec& spec) {
  return BuildCfb(BuildDocStreams(spec));
}

}  // namespace doccraft::test

#endif  // TESTS_M20_DOC_UTIL_H_
