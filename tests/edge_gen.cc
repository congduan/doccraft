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

// Writes the synthetic M20 edge-corpus DOC files. The Python front end
// (tests/corpus/gen_edge_corpus.py) invokes this binary with the output
// directory; expectations live in the Python manifest.

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "tests/m20_doc_util.h"

namespace {
using doccraft::test::DocSpec;
using doccraft::test::Prl;
using doccraft::test::StyleSpec;

constexpr uint16_t kSprmPFInTable = 0x2416;
constexpr uint16_t kSprmPFTtp = 0x2417;
constexpr uint16_t kSprmPFInnerTtp = 0x244C;
constexpr uint16_t kSprmPItap = 0x6649;
constexpr uint16_t kSprmPIstd = 0x4600;
constexpr uint16_t kSprmCFBold = 0x0835;
constexpr uint16_t kSprmCFItalic = 0x0836;

std::vector<uint8_t> TableSprms(int depth, bool row_end, bool inner) {
  std::vector<uint8_t> sprms = Prl(kSprmPFInTable, {1});
  if (row_end) {
    const auto ttp = Prl(kSprmPFTtp, {1});
    sprms.insert(sprms.end(), ttp.begin(), ttp.end());
  }
  if (inner) {
    const auto inner_ttp = Prl(kSprmPFInnerTtp, {1});
    sprms.insert(sprms.end(), inner_ttp.begin(), inner_ttp.end());
  }
  const auto itap = Prl(kSprmPItap, {static_cast<uint8_t>(depth), 0, 0, 0});
  sprms.insert(sprms.end(), itap.begin(), itap.end());
  return sprms;
}

// Appends text and, for every mark it contains, one PAPX entry.
void Append(DocSpec* spec, const std::string& text,
            const std::vector<uint8_t>& sprms) {
  for (char c : text) {
    spec->text.push_back(static_cast<uint8_t>(c));
    if (static_cast<unsigned char>(c) == 0x0D ||
        static_cast<unsigned char>(c) == 0x07) {
      spec->papx.push_back(
          {static_cast<uint32_t>(spec->text.size() - 1), sprms});
    }
  }
}

// A one-cell-per-level nested table. Each level contributes an "L<level>"
// paragraph, a nested table, then a "T<level>" paragraph before its cell and
// row marks.
void NestedTable(int level, int depth, DocSpec* spec) {
  if (level < depth) {
    Append(spec, "L" + std::to_string(level) + "\r",
           TableSprms(level, false, false));
    NestedTable(level + 1, depth, spec);
    Append(spec, "T" + std::to_string(level) + "\r",
           TableSprms(level, false, false));
    Append(spec, "\x07", TableSprms(level, false, false));
    Append(spec, "\x07", TableSprms(level, true, level > 1));
  } else {
    Append(spec, "L" + std::to_string(level) + "\x07",
           TableSprms(level, false, false));
    Append(spec, "\x07", TableSprms(level, true, level > 1));
  }
}

std::vector<uint8_t> Utf16Le(const std::u16string& text) {
  std::vector<uint8_t> out;
  for (char16_t c : text) {
    out.push_back(static_cast<uint8_t>(c));
    out.push_back(static_cast<uint8_t>(c >> 8));
  }
  return out;
}

StyleSpec ParagraphStyle(const std::string& name, int base,
                         std::vector<uint8_t> run_sprms = {}) {
  StyleSpec style;
  style.name = name;
  style.type = 1;
  style.base = base;
  style.run_sprms = std::move(run_sprms);
  return style;
}

std::vector<std::pair<std::string, DocSpec>> Samples() {
  std::vector<std::pair<std::string, DocSpec>> out;
  auto add = [&](const std::string& name, DocSpec spec) {
    out.emplace_back(name, std::move(spec));
  };

  {
    DocSpec spec;
    spec.text = {' '};
    spec.pieces = {{0, 1, 0, true, 0}};
    spec.ccp_override = 0;
    add("empty-document", spec);
  }
  {
    DocSpec spec;
    spec.text = {'A'};
    add("single-char", spec);
  }
  {
    DocSpec spec;
    spec.text = {'A', 'B', '\r', 'C', 'D', '\r'};
    add("two-paragraphs", spec);
  }
  for (int depth : {2, 4, 8, 9}) {
    DocSpec spec;
    NestedTable(1, depth, &spec);
    add("nested-table-d" + std::to_string(depth), spec);
  }
  {
    // One cell well past the 64 KiB piece-size boundary.
    DocSpec spec;
    Append(&spec, std::string(70000, 'X') + "\x07",
           TableSprms(1, false, false));
    Append(&spec, "\x07", TableSprms(1, true, false));
    add("huge-cell-text", spec);
  }
  {
    // One row with 70 cells: more columns than the legacy 64-column limit.
    DocSpec spec;
    for (int i = 1; i <= 70; ++i) {
      Append(&spec, "C" + std::to_string(i) + "\x07",
             TableSprms(1, false, false));
    }
    Append(&spec, "\x07", TableSprms(1, true, false));
    add("many-columns", spec);
  }
  {
    DocSpec spec;
    Append(&spec,
           "A\tB"
           "\x0b"
           "C\r",
           {});
    add("control-whitespace", spec);
  }
  {
    DocSpec spec;
    Append(&spec,
           "A"
           "\x0c"
           "B\r",
           {});
    add("control-page-break", spec);
  }
  {
    DocSpec spec;
    Append(&spec,
           "A"
           "\x0c"
           "B\r",
           {});
    spec.section_ends = {2, 4};
    add("control-section-break", spec);
  }
  {
    DocSpec spec;
    Append(&spec,
           "A"
           "\x13"
           "INSTR"
           "\x14"
           "RESULT"
           "\x15"
           "B\r",
           {});
    add("control-field-chars", spec);
  }
  {
    DocSpec spec;
    Append(&spec,
           "A"
           "\x01"
           "\x02"
           "\x05"
           "\x08"
           "\x1e"
           "\x1f"
           "B\r",
           {});
    add("control-object-marks", spec);
  }
  {
    // CP array [1, 0, 1]: the first piece ends before it starts.
    DocSpec spec;
    spec.text = {'A', 'B', 'C', '\r'};
    spec.pieces = {{1, 3, 1, true, 0}, {0, 1, 0, true, 0}};
    add("piece-cp-descending", spec);
  }
  {
    // The piece points far past the end of WordDocument.
    DocSpec spec;
    spec.text = {'A', '\r'};
    spec.pieces = {{0, 2, 0x30000, true, 0}};
    add("piece-outside-stream", spec);
  }
  {
    // PCD prm selects Prc index 1 while the CLX carries no Prc at all.
    DocSpec spec;
    spec.text = {'A', '\r'};
    spec.pieces = {{0, 2, 0, true, 3}};
    add("piece-prm-index", spec);
  }
  {
    // fcMin points into the FIB while the piece table is well formed.
    DocSpec spec;
    spec.text = {'A', 'B', '\r'};
    spec.fc_min = 512;
    add("piece-fcmin-mismatch", spec);
  }
  {
    // ANSI "AB" | UTF-16LE "中文" | ANSI "CD\r".
    DocSpec spec;
    spec.text = {'A', 'B'};
    const auto cjk = Utf16Le(u"\u4E2D\u6587");
    spec.text.insert(spec.text.end(), cjk.begin(), cjk.end());
    spec.text.insert(spec.text.end(), {'C', 'D', '\r'});
    spec.pieces = {{0, 2, 0, true, 0}, {2, 4, 2, false, 0}, {4, 7, 6, true, 0}};
    add("piece-mixed-encoding", spec);
  }
  {
    // Compressed text holding every CP1252 byte value.
    DocSpec spec;
    for (int i = 0; i < 256; ++i) {
      spec.text.push_back(static_cast<uint8_t>(i));
    }
    add("cp1252-all-256", spec);
  }
  {
    // U+1F600 as a valid surrogate pair, then a plain ASCII character.
    DocSpec spec;
    const auto text = Utf16Le(u"\U0001F600X\r");
    spec.text = text;
    spec.pieces = {{0, 4, 0, false, 0}};
    add("utf16-surrogate-pair", spec);
  }
  {
    // A lone high surrogate, which the decoder replaces with U+FFFD.
    DocSpec spec;
    std::u16string raw;
    raw.push_back(0xD800);
    raw += u"X\r";
    const auto text = Utf16Le(raw);
    spec.text = text;
    spec.pieces = {{0, 3, 0, false, 0}};
    add("utf16-unpaired-surrogate", spec);
  }
  {
    // A -> B -> A based-on cycle.
    DocSpec spec;
    spec.styles.push_back(ParagraphStyle("Normal", -1));
    spec.styles.push_back(ParagraphStyle("CycleA", 2, Prl(kSprmCFBold, {1})));
    spec.styles.push_back(ParagraphStyle("CycleB", 1, Prl(kSprmCFItalic, {1})));
    Append(&spec, "Cycled\r", Prl(kSprmPIstd, {1, 0}));
    add("style-cycle", spec);
  }
  {
    // 20-deep based-on chain, longer than the 16-level resolve guard.
    DocSpec spec;
    spec.styles.push_back(ParagraphStyle("Normal", -1));
    for (int i = 1; i <= 20; ++i) {
      spec.styles.push_back(ParagraphStyle("Deep" + std::to_string(i), i - 1));
    }
    Append(&spec, "Deep\r", Prl(kSprmPIstd, {20, 0}));
    add("style-deep-chain", spec);
  }
  {
    // Five nested fields, each with its own separator and result.
    DocSpec spec;
    std::string text = "N";
    for (int i = 0; i < 5; ++i) {
      text += "\x13INSTR" + std::to_string(i) + "\x14R" + std::to_string(i);
    }
    text += "\x15\x15\x15\x15\x15\r";
    Append(&spec, text, {});
    add("field-nested-5", spec);
  }
  {
    DocSpec spec;
    Append(&spec, "\x13INSTR\x15\r", {});
    add("field-missing-separator", spec);
  }
  {
    DocSpec spec;
    Append(&spec, "\x13INSTR\x14RESULT\r", {});
    add("field-unclosed", spec);
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: edge_gen <output-dir>\n");
    return 2;
  }
  const std::string dir = argv[1];
  for (const auto& sample : Samples()) {
    const auto bytes = doccraft::test::BuildDocFile(sample.second);
    const std::string path = dir + "/" + sample.first + ".doc";
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
      std::fprintf(stderr, "cannot write %s\n", path.c_str());
      return 1;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (written != bytes.size()) {
      std::fprintf(stderr, "short write on %s\n", path.c_str());
      return 1;
    }
    std::printf("%s %zu\n", sample.first.c_str(), bytes.size());
  }
  return 0;
}
