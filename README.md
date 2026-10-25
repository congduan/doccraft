# doccraft

High-fidelity `.doc` → `.docx` converter in pure C++20. Self-contained binary,
no Office installation, no third-party runtime dependencies.

```bash
doccraft convert input.doc -o output.docx   # end-to-end, sub-second for 99% of documents
doccraft inspect input.doc                  # dump CFB container / FIB / piece table
```

## Why

Existing converters (LibreOffice, ONLYOFFICE, antiword, wvWare) either carry
heavy runtimes, degrade complex features, or have stopped maintaining the
legacy Word 97-2003 binary path. doccraft is a clean-room implementation of
[MS-CFB] + [MS-DOC] parsing with a format-neutral IR and an ECMA-376
(transitional) writer — engineered feature-by-feature against a public
checklist (see [docs/DESIGN.md](docs/DESIGN.md) §6) with TDD goldens.

## Status

Early development — see [docs/DESIGN.md](docs/DESIGN.md) for the full design
and milestone plan. M1 (CFB/OLE container layer) in progress.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Requirements: C++20 compiler (GCC 12+/Clang 15+), CMake 3.20+.

## Code style

The tree follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html),
enforced by `.clang-format`, `.clang-tidy` and `CPPLINT.cfg`. `third_party/`
is vendored upstream code and excluded from all checks.

```bash
cmake --build build --target format-check   # verify formatting (CI gate)
cmake --build build --target format         # rewrite sources in place
scripts/lint.sh --cpplint                   # optional cpplint pass
scripts/lint.sh --tidy                      # optional clang-tidy pass
```

## License

Apache-2.0
