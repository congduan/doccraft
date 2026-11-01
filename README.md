# doccraft

A fast, dependency-free **.doc (Word 97-2003 binary) → .docx** converter in pure
C++23. Clean-room [MS-CFB] and [MS-DOC] parsers, a format-neutral IR, and an
ECMA-376 Transitional OOXML writer — shipped as a single 1.3 MB binary with no
runtime dependencies.

**100 MiB documents convert in 4 seconds.** 680 real-world documents from five
corpora convert with a 92% success rate, zero crashes, and graceful degradation
for the rest.

## Why doccraft

| | doccraft | LibreOffice (ww8) | antiword / wvWare | Online converters |
|---|---|---|---|---|
| Deployment | single 1.3 MB binary | ~1 GB install | abandoned (2005+) | privacy risk, no batch |
| 100 MiB speed | **3.99 s** | 30 s+ | n/a (text only) | slow / size caps |
| Fidelity target | sprm-level ledger | good, unstable | text/HTML only | varies |
| Encryption | RC4 family + XOR | yes | no | varies |
| Test discipline | 547 ctest + 680-doc gate | large but unmeasured | none | none |

doccraft is not a LibreOffice replacement — it is a **converter engine**: smaller,
faster, embeddable (C API), and built around a public per-feature acceptance
ledger instead of accumulated patches.

## What it handles

- Text and formatting: ANSI and UTF-16 pieces; character, paragraph, and
  section properties; styles, numbering, headers, footers, and multiple
  sections.
- Structure: tables, hyperlinks and general fields, bookmarks, comments,
  footnotes, and endnotes.
- Embedded content: inline and floating images, main-story floating text
  boxes, and OLE ObjectPool payloads with field-result previews.
- Languages: Unicode text, RTL and complex-script formatting, and document
  language ID to ANSI code-page mapping. The implementation lists 15 code-page
  decoders; non-CP1252 decoding requires iconv at build time.
- Recovery: final piece-table reconstruction for fast-saved documents,
  legacy RC4 and RC4 CryptoAPI decryption with a password, and warnings for
  selected recoverable formatting or header damage.

Feature states and their boundaries are in [the design checklist](docs/DESIGN.md)
and [known simplifications](docs/KNOWN-SIMPLIFICATIONS.md). The applicable sprm
ledger is tracked in [SPRM coverage](docs/SPRM-COVERAGE.md).

## Platforms

| Platform | Form | Notes |
|---|---|---|
| Linux | CLI binary + library | primary development platform |
| macOS | CLI binary + library | libc iconv, no extra deps |
| Windows | CLI binary + library | Win32 code-page backend; `std::filesystem` |
| Android | library + C API | NDK toolchain; JNI wrapper is consumer-side |
| iOS | library + C API | Xcode toolchain |
| WebAssembly | library + C API | no file system: build with `DOCCRAFT_BUILD_CLI=OFF` |

Embedding uses the C API in
[`src/api/doccraft_api.h`](src/api/doccraft_api.h): buffer in, DOCX buffer out,
no file I/O. See the README section on build switches for the library-only
shape (`DOCCRAFT_BUILD_CLI=OFF`).

## Quality engineering

- **547 ctest cases**, all green before every commit; ASan build runs the full
  suite with leak detection tuned for gtest.
- **680-document real-world gate**: Apache POI, LibreOffice sw/qa, wvWare,
  Aspose, and GOVDOCS1 corpora are converted on every corpus-enabled run
  (92% clean conversions, 0 crashes, rejections individually triaged).
- **Golden gate**: 36 baseline documents compared part-by-part (SHA-256) so
  writer changes cannot silently alter existing output.
- **Pixel comparison**: LibreOffice renders source DOC and converted DOCX to
  PDF and diffs page images against recorded baselines.
- **Fuzzing**: deterministic seed mutation across the full pipeline, targeted
  sprm/FKP/CLX mutation, and all 65536 opcodes dispatched directly; a libFuzzer
  driver is available behind a build flag.
- **Performance gate**: recorded baselines per size tier; regressions over 30%
  are flagged in CI.

## Measured performance

The five committed measurements below come from
[`tests/perf/baseline.json`](tests/perf/baseline.json). The benchmark converts
generated structured DOC files five times per size; wall time is the median,
and peak RSS is the maximum of those runs. These are measurements from that
baseline, not cross-machine guarantees.

| Input DOC | Actual size | Median wall time | Peak RSS |
|---:|---:|---:|---:|
| 1 MiB tier | 1.00 MiB | 0.037 s | 16.6 MiB |
| 5 MiB tier | 5.00 MiB | 0.180 s | 68.3 MiB |
| 20 MiB tier | 20.00 MiB | 0.905 s | 261.8 MiB |
| 50 MiB tier | 50.24 MiB | 1.111 s | 322.2 MiB |
| 100 MiB tier | 99.99 MiB | 3.990 s | 1,287.9 MiB |

Reproduce with `tests/perf/gen_corpus.py` and `tests/perf/run_perf.py` after
building the converter. The 100 MiB result shows that memory use is not yet
bounded; streaming and an explicit memory cap are M6 work.

## Visual fidelity

The [pixel comparison pipeline](tests/pixelcompare/README.md) renders the same
document through LibreOffice twice — the source DOC and doccraft's DOCX — and
diffs the page images. Most fixtures land under the 0.1% bad-pixel threshold;
known gaps and their scope are recorded in
[KNOWN-SIMPLIFICATIONS](docs/KNOWN-SIMPLIFICATIONS.md) rather than hidden.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # first configure fetches googletest
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Build switches (all default ON): `DOCCRAFT_BUILD_CLI` (OFF builds libraries +
`src/api` only — the WebAssembly/Android/iOS shape), `DOCCRAFT_BUILD_TESTS`,
`DOCCRAFT_WARNINGS_AS_ERRORS`. See [AGENTS.md](AGENTS.md) for the full
development manual, layering rules, and code conventions.

## Known limits

- Password-protected documents are decrypted from `--password` for XOR
  obfuscation, legacy RC4/MD5 and RC4 CryptoAPI/SHA-1. Non-ASCII XOR passwords
  and interoperability with a Word-produced encrypted sample are still unverified.
- Basic floating shapes and WordArt become positioned VML placeholders with
  shape type, solid fill, line colour/width and available text. Pattern and
  gradient fills, rotation, curve geometry and shape text wrap may differ, and
  floating image wrap polygons are still rectangular.
- Fast-saved samples can preserve text while changing pagination. OLE objects
  without a field-result image use an EMF/WMF or CF_DIB (bitmap) OlePres preview;
  `\3EPRINT` previews and linked-object semantics can remain blank.
- Word 6/95 files read text, core character/paragraph formatting, styles,
  headers/footers, and direct-stored WMF images; legacy tabs are dropped, full
  legacy table properties are not converted, bitmap pictures are skipped, and
  output layout may degrade.
- Memory is not bounded: very large documents keep the decoded text buffer and
  the CFB input in RAM until conversion completes (100 MiB ≈ 1.3 GiB RSS).
- The DOCX schema validator checks element structure of the 2006 WordprocessingML
  namespace; attribute validity, relationship targets and extension-namespace
  placement are not verified.

The detailed list is in [KNOWN-SIMPLIFICATIONS](docs/KNOWN-SIMPLIFICATIONS.md).

## License and specifications

Licensed under [Apache-2.0](LICENSE). The parser follows
[[MS-CFB]](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/)
and [[MS-DOC]](https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-doc/);
the writer targets [ECMA-376](https://ecma-international.org/publications-and-standards/standards/ecma-376/)
Transitional OOXML.
