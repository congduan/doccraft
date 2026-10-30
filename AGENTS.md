# AGENTS.md

Working manual for AI agents and contributors in this repository. See
[docs/DESIGN.md](docs/DESIGN.md) for the design, [docs/KNOWN-SIMPLIFICATIONS.md](docs/KNOWN-SIMPLIFICATIONS.md)
for deliberate degradations, and [docs/SPRM-COVERAGE.md](docs/SPRM-COVERAGE.md) for the
per-sprm acceptance ledger.

## 1. What this project is

doccraft is a pure C++ `.doc` (Word 97-2003 binary) to `.docx` high-fidelity converter:
single binary, no runtime dependencies. Current progress: M1 (CFB container) and M2
(FIB / piece table / text path) are done; M3 (CHP/PAP/SEP sprms, styles, lists, basic
tables, headers and footers) is being delivered in batches (latest: M3 batch 8,
plus the structure fixes for run merging, cell blocks and per-section headers).

Core invariant: **four layers, one-way data flow, the IR is the only hub.**

```
src/cfb/    layer1  [MS-CFB] container reader (FAT/MiniFAT/DIFAT, directory tree)
src/doc/    layer2  [MS-DOC] parser (FIB, piece table, FKP, sprm, styles, lists, headers, tables)
src/model/  layer3  format-neutral IR (Document/Paragraph/Run/Table/Style/Section), twips
src/docx/   layer4  ECMA-376 transitional writer (miniz for zip, pugixml for XML)
src/cli/            CLI wiring (convert / inspect)
src/api/            embedding C API (buffer in, docx buffer out; no file IO)
src/base/           shared leaf: byte reads (`Bytes`, `U16`, `U32`, `U64`, `S16`), `AppendUtf8`, ANSI code page decoding (`charset.h`)
```

Dependency direction must hold: `cli -> {cfb, doc, docx}`, `docx -> model`,
`doc -> model`, `{cfb, doc} -> base`, `api -> {cfb, doc, docx}`. `src/doc/` must
not include `docx/` or `cfb/` headers; `src/docx/` must not include `doc/`
headers; `src/base/` includes nothing from `cfb/`, `doc/`, `model/` or `docx/`.
Byte-order decoding lives only in `src/base/binary.h` — do not add another
`U16`/`U32` copy in a layer. `src/api/` is the only layer allowed to orchestrate
a whole conversion besides `src/cli/`; keep the two pipelines in step
(`tests/api_cli_parity.py` fails if they diverge).

## 2. Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # first configure fetches googletest (network needed)
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- Toolchain: C++23 (`CMAKE_CXX_STANDARD 23`) with `-Wall -Wextra -Wpedantic -Werror`.
  The README's "C++20 / GCC 12+" line is stale; CMake is authoritative.
- Binary: `build/src/cli/doccraft`.
- Build switches (all default ON): `DOCCRAFT_BUILD_CLI` (OFF builds libraries +
  `src/api` only, the WebAssembly/Android/iOS shape), `DOCCRAFT_BUILD_TESTS`
  (requires the CLI; skip it to avoid the googletest download),
  `DOCCRAFT_WARNINGS_AS_ERRORS` (MSVC warning sets differ; CI's Windows job
  builds with it OFF).
- Test filter: `ctest --test-dir build -R m3_batch7 --output-on-failure`.
- Two ctest entries are Python scripts and need `python3` on PATH:
  `docx_zip_validate` (zip structure) and `corpus_text_consistency` (text agreement).
- Manual smoke test:
  `build/src/cli/doccraft inspect tests/corpus/corpus/plain-text.doc`,
  `build/src/cli/doccraft convert tests/corpus/corpus/plain-text.doc -o /tmp/out.docx`.
- `ctest` must be fully green before committing. Never silence `-Werror` with `-w` or
  `#pragma` (`third_party` is the only exception).

## 3. Code conventions

- Google C++ Style Guide, mechanically enforced by `.clang-format`, `.clang-tidy`
  and `CPPLINT.cfg`: 2-space indent, 80-column limit, `kConstantName`, `.h`/`.cc`,
  Apache-2.0 file header, header guard `<PATH>_<FILE>_H_` relative to the repo root
  (`SRC_DOCX_WRITER_H_`), include order (related header, C system, C++ standard,
  `third_party/`, project). Includes are written relative to the repo root
  (`"docx/writer.h"`, `"third_party/miniz/miniz.h"`).
- Before pushing: `cmake --build build --target format-check` (CI gate);
  `scripts/lint.sh --fix` rewrites, `--cpplint` / `--tidy` add the optional
  passes. `third_party/` is excluded — never reformat vendored code.
- No magic numbers in parsing/writing code. `src/doc/format/sprm_codes.h` holds
  every `kSprm<SpecName>` opcode (one per `docs/SPRM-COVERAGE.md` row);
  `internal.h` holds twip/encoding limits and bit masks. Dense one-to-one
  lookup tables (Ico palette, CP1252 map, LCID tags, Kul underline kinds) may
  keep bare values - they are data, not behaviour.
- Namespaces: `doccraft::cfb` / `doccraft::doc` / `doccraft::model` / `doccraft::docx`.
  Internal helpers live in `doc::detail` (see `src/doc/format/internal.h`) or an
  anonymous namespace.
- Errors always travel as `std::expected<T, Error>` (`Error{std::string message}`).
  **No exceptions, no try/catch.** The CLI prints `error().message` to stderr and
  returns non-zero.
- All lengths are twips internally; conversion happens only in `src/docx/`.
- Property structs use the tri-state "value + `*_set` flag" pattern
  (unset / explicitly off / on). Follow it for new sprms instead of magic sentinels.
- New files carry the Apache-2.0 license header (some existing files lack it; treat
  that as legacy, add it for anything new).
- `third_party/` holds vendored sources (miniz, pugixml). Do not modify or upgrade them
  unless the task explicitly asks. Note: DESIGN §2.1 says miniz/pugixml come via
  FetchContent, but they are actually vendored static libraries (see
  `src/docx/CMakeLists.txt`); the code is authoritative.
- Do not add third-party dependencies. If one is truly required, first update DESIGN
  §2.1 and document its license and transitive dependencies.

## 4. Testing conventions (TDD)

- Framework: googletest (`gtest_main`). Each test file becomes its own executable and
  is registered with `gtest_discover_tests`, so new test files must also be added to
  `tests/CMakeLists.txt`.
- When a test reads a sample from the source tree, use
  `target_compile_definitions(... PRIVATE DOCCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")`
  and build paths from `${DOCCRAFT_SOURCE_DIR}/tests/...`.
- Per-layer style:
  - layer1: build containers programmatically with `CfbBuilder` from
    `tests/cfb_test_util.h` (v3/v4 sectors, MiniFAT boundaries, nested storages).
  - layer2/layer4: assemble WordDocument/Table byte streams in the test, inject FKP
    pages and sprms, then assert on IR fields and on `word/document.xml` fragments.
    `tests/m3_batch*_test.cc` are the templates; reuse the `Synthetic()`, `Xml()` and
    `RoundTrip()` helpers for new features.
  - Corpus: `tests/corpus/corpus/*.doc` is generated by `gen_corpus.py` (LibreOffice
    headless) from `.fodt`, paired with `.docx` references and `baseline/*.txt` text
    baselines.
  - Corpus text baselines come from `soffice --convert-to txt`; replace TAB with
    newline before checking one in (table cells use one line each).
  - Feature fixtures: one minimal sample per feature as `tests/fixtures/<feature>.fodt`
    plus the matching `.doc`.
  - Visual comparison: `tests/pixelcompare/compare.py` (LibreOffice + pdftoppm pixel
    diff, `bad_ratio` threshold 0.1%, exit codes 0/1/2) for layout regressions.
- Assert on concrete IR fields or XML elements; "conversion did not fail" is not a test.

## 5. Documentation obligations

A behavior change is not complete until these are updated:

1. `docs/SPRM-COVERAGE.md`: flip the affected row from ☐/◐ to ✅ and put the test name
   (`M3BatchN.TestName`) in the classification column. Legend: ✅ asserted by a sample,
   ◐ partial, ☐ pending, N/A with a stated reason.
2. `docs/KNOWN-SIMPLIFICATIONS.md`: append any deliberate simplification or known gap
   under the current milestone, describing the symptom and where it is deferred.
3. `docs/DESIGN.md` §6 feature checklist: update the status when a feature moves
   ☐ -> ◐/✅.
4. `README.md`: only when CLI usage or build requirements change.

## 6. Standard flow for one feature

1. Locate the target item in `docs/SPRM-COVERAGE.md` or DESIGN §6; confirm the opcode
   and the spec section.
2. Write the failing test first (synthetic byte stream -> IR assertion -> written XML
   assertion).
3. Parse it in `src/doc/format/` (sprm dispatch is centralized in `sprm.cc`), add fields
   to `src/model/document.h`, then map to OOXML in `src/docx/writer*.cc`.
4. Add or update a `tests/fixtures` or corpus sample; run pixelcompare when layout is
   affected.
5. Full `ctest` green, ledger and KNOWN-SIMPLIFICATIONS updated, one focused commit
   (`feat:` / `fix:` / `docs:` / `refactor:`, optionally prefixed with `M3 batch N`).

## 7. Do not

- Do not touch `third_party/`, `build/` or `.git/`; do not commit build output
  (`.gitignore` covers it).
- Do not weaken assertions, delete tests, or downgrade `-Werror` to make a build pass.
- Do not leak docx-specific concepts (`w:` elements, part names) into `model`/`doc`.
- Do not use exceptions for error paths; do not silently swallow parse errors without
  recording the gap in KNOWN-SIMPLIFICATIONS.
- Do not guess sprm semantics from OCR or memory; quote [MS-DOC].
