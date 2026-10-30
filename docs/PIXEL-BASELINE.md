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
`footnote-endnote.doc` was recorded here as passing on one page after the
writer began emitting the note reference mark, its tab stop and the note text
styles. A 2026-10-31 re-measurement with the same LibreOffice 24.2.7.2 reports
A=1, B=2 for doccraft's DOCX **and** for a DOCX that LibreOffice exports from
the same `.doc` itself: the endnote moves to a second page on both `.docx`
paths. The two DOCX renders are pixel-identical on both pages (RMS 0, 0 bad
pixels), and against the source `.doc`, page 1 scores exactly 2988 bad pixels
(0.137%) for each of them. The page-count split is therefore a LibreOffice
`.doc`→`.docx` import asymmetry, not a doccraft regression. No `.allow`
exception is checked in, so `compare.py` still exits 1 on the page-count line
alone. A PASS here means passing this specific LibreOffice comparison, not
identical behavior in Microsoft Word or complete support for all forms of the
feature.

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
| `nested-tables.doc` | 1 | 0.010% | PASS |
| `page-fields.doc` | 1 | 0.000% | PASS |
| `comments-bookmarks.doc` | 1 | 0.000% | PASS |

The original 0.305% measurement predates commit `aa133ee` ("split nested-table
rows and cells on inner CR marks"): that parser treated only BEL as a
structural table mark, so the four inner paragraphs piled into one cell and the
two inner row-end CRs stayed behind as empty paragraphs. `aa133ee` routes CR
marks at depth >= 2 carrying `sprmPFInnerTableCell`/`sprmPFInnerTtp` through the
same cell/row path as BEL. Re-measured at HEAD on 2026-10-31, the sample is a
5-column outer table with a 2x2 inner table, one paragraph per cell, and page 1
scores 208 bad pixels (0.010%): PASS.

The remaining 208 pixels are not a doccraft layout error. Rendering doccraft's
DOCX and LibreOffice's own `nested-tables.docx` side by side is pixel-identical
(RMS 0, 0 bad pixels), and comparing that reference DOCX with the source `.doc`
reproduces exactly 208 bad pixels (0.010%). The geometry is pinned by
`M3NestedTable.CorpusNestedTableKeepsTwoRowsAndFourCells` and
`M3NestedTable.CorpusNestedTableWritesGridAndCellWidths`;
`corpus_text_consistency` remains at 100% for all 12 samples. The M20 row in
`pixel-baseline.tsv` records the re-measured PASS; the earlier FAIL row is
superseded.
