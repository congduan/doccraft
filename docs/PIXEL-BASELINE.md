# Pixel comparison baseline (2026-10-27)

The results in [pixel-baseline.tsv](pixel-baseline.tsv) cover every committed
`.doc` in `tests/corpus/corpus/` and `tests/fixtures/` at commit `64e9a99`:
**33 samples, 28 PASS, 4 FAIL, 1 RENDER_ERROR**, plus the four M20 corpus
samples measured below. They were measured with
LibreOffice 24.2.7.2 and Poppler 24.02.0 at 150 DPI, using the repository's
default 0.1% per-page bad-pixel threshold. The input DOC and doccraft's output
DOCX were independently rendered by LibreOffice, then compared page by page.

To repeat the measurement after building the converter:

```bash
for sample in tests/corpus/corpus/*.doc tests/fixtures/*.doc; do
  python3 tests/pixelcompare/compare.py "$sample"
done
```

`RENDER_ERROR` means the comparator could not get a PDF from LibreOffice for
`comments-basic.doc` after doccraft conversion; a repeat produced the same
result. `fast-saved-o-kurs.doc` has a page-count mismatch; `floating-image.doc`,
`floating-tight.doc`, and `ole-embedded.doc` exceeded the pixel threshold.
`footnote-endnote.doc` used to pass only through a checked-in `.allow` page-count
exception (A=1, B=2). The writer now emits the note reference mark, its tab stop
and the note text styles, so both renders produce one page, the `.allow` file is
gone, and an equivalent 150 DPI re-measurement finds 100 bad pixels (0.005%) —
the same level as LibreOffice's own `.doc`→`.docx` import asymmetry. A PASS here
means passing this specific LibreOffice comparison, not identical behavior in
Microsoft Word or complete support for all forms of the feature.

An earlier 25/32 summary was supplied for release planning, but no per-sample
record for that run is committed. This baseline uses the current committed
sample set and records every result.

## M20 corpus expansion (2026-10-29)

Four MS-DOC-feature samples were added to `tests/corpus/corpus/` from
`gen_corpus.py`: `sections-continuous` (three continuous sections),
`nested-tables` (5-column outer table with a nested 2x2 table),
`page-fields` (PAGE and SEQ fields plus a hyperlink) and
`comments-bookmarks` (a ranged comment and a bookmark over the same text).
Each was measured with the default 150 DPI / 0.1% threshold:

| sample | compared pages | bad_ratio | result |
|---|---|---|---|
| `sections-continuous.doc` | 2 | 0.000% | PASS |
| `nested-tables.doc` | 1 | 0.305% | FAIL |
| `page-fields.doc` | 1 | 0.000% | PASS |
| `comments-bookmarks.doc` | 1 | 0.000% | PASS |

`nested-tables.doc` is a known layout gap, not a text-content gap: doccraft
emits two extra empty paragraphs for the row-end marks inside the nested cell
(8 paragraphs where LibreOffice has 6 in cell B1), so the outer table's second
row starts two lines lower. `corpus_text_consistency` remains at 100% for all
12 samples. The behavior is recorded here and in REPORT.md rather than fixed
in the M20 test-only batch.
