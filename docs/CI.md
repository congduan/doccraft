# Continuous integration

Three workflows live in `.github/workflows/`:

| Workflow | Trigger | Jobs |
|---|---|---|
| `ci.yml` | push, pull request, manual | build + full ctest + format-check, ASan, docs-link check; optional corpus job; manual performance smoke |
| `build-matrix.yml` | push, pull request, manual | Linux/GCC, macOS/Clang, Windows/MSVC (`ctest`), library-only build |
| `pages.yml` | push to `main` when README, gallery or site generator changes, manual | build the static site and publish `gh-pages` |

Every job sets `timeout-minutes` and a `concurrency` group so a stuck or
superseded run cannot occupy a runner indefinitely. The groups use
`cancel-in-progress: true` for pull-request and documentation jobs; the
platform matrix keeps `cancel-in-progress: false` so an in-flight platform
result is not discarded by a second push.

## Corpus gate

The 680-document corpus is not in the repository: it mixes Apache POI,
LibreOffice `sw/qa`, wvWare, Aspose and GOVDOCS1 files with different licenses
and a large footprint. `tests/poi_corpus_smoke.py` therefore reads the corpus
from outside the tree and exits 0 with a "skipping" message when the directory
is absent. That keeps ordinary hosted CI green.

The `corpus` job in `ci.yml` is reserved with `if: false` because
`ubuntu-latest` does not have the corpus. To enable it:

1. Make the corpus available to the runner. On a self-hosted runner, check the
   files out or mount them; on a hosted runner, add a download step that
   fetches the POI test-data archive (the corpus is not redistributed here).
2. Set `DOCRAFT_CORPUS_DIR` to the directory that contains the `.doc` files.
   The test appends nothing to this path; it globs `*.doc` directly.
3. Remove the `if: false` line, or replace it with
   `if: ${{ vars.DOCRAFT_CORPUS_DIR != '' }}` and define the variable in the
   repository settings.

The job runs `ctest -R "poi_corpus_smoke|corpus_text_consistency"`. The smoke
test requires at least 100 files, at least 100 successful conversions, and no
crash or timeout. Locally, the same gate is:

```bash
DOCRAFT_CORPUS_DIR=/path/to/poi-test-data \
  python3 tests/poi_corpus_smoke.py build/src/cli/doccraft
```

Without `DOCRAFT_CORPUS_DIR`, the script falls back to the development path
`/home/ubuntu/doccraft-corpus/poi-test-data`; if that is absent it skips.

## AddressSanitizer

The `asan` job configures a Debug build with
`-fsanitize=address -fno-omit-frame-pointer`, links with the same sanitizer and
runs the complete ctest suite with `ASAN_OPTIONS=detect_leaks=0`. Leak
detection is off because the code uses `std::expected` and process-lifetime
allocations; the job's purpose is memory safety (out-of-bounds, use-after-free,
double-free) rather than leak accounting. The deterministic mutation suite
runs inside the same job.

Local equivalent:

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
ASAN_OPTIONS=detect_leaks=0 cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

`ASAN_OPTIONS` is needed for the build step too: `gtest_discover_tests` runs
each test executable once to enumerate cases, and leak detection would turn a
successful enumeration into a build failure. The job-level `env` in `ci.yml`
covers both steps.

## Performance smoke

The `perf-smoke` job is manual (`workflow_dispatch` in `ci.yml`) because
wall-clock numbers on shared runners are noisy. It generates only the 5 MiB
tier, converts it five times, takes the median, and compares it with the
`"5"` entry in `tests/perf/baseline.json`. A result more than 30% slower emits
a `::warning::` annotation and a step-summary line but does not fail the job —
the threshold catches large regressions without turning shared-runner noise
into a red build. The job needs `libreoffice-writer` because
`tests/perf/gen_corpus.py` produces the DOC through LibreOffice.

## Documentation links

The `docs-links` job runs `scripts/check_readme_links.py`, which extracts every
relative link and image target from `README.md`, resolves it against the
repository root and fails when a target does not exist. External `http(s)`,
`mailto:` and pure `#anchor` links are ignored: the check is about the files
this repository promises, not about network reachability.

## Formatting

`ci.yml` installs `clang-format` and builds the `format-check` target. The same
check runs locally with:

```bash
cmake --build build --target format-check
# or, for an in-place rewrite:
scripts/lint.sh --fix
```

`third_party/` and `src/ooxml/generated/` are excluded from formatting; the
first is vendored upstream code, the second is machine-written schema data.
