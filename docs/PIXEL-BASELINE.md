# Pixel comparison baseline (2026-10-27)

The results in [pixel-baseline.tsv](pixel-baseline.tsv) cover every committed
`.doc` in `tests/corpus/corpus/` and `tests/fixtures/` at commit `64e9a99`:
**33 samples, 28 PASS, 4 FAIL, 1 RENDER_ERROR**. They were measured with
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
`footnote-endnote.doc` is counted as PASS by the comparison script because its
checked-in `.allow` file records the known page-count exception. A PASS here
means passing this specific LibreOffice comparison, not identical behavior in
Microsoft Word or complete support for all forms of the feature.

An earlier 25/32 summary was supplied for release planning, but no per-sample
record for that run is committed. This baseline uses the current committed
sample set and records every result.
