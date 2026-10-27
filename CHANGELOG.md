# Changelog

## v0.9.0-rc1 — release candidate preparation

- Published measured five-size conversion baselines, a reproducible visual
  gallery, and build/test/format CI gates.
- Documented current format coverage and known limitations. M6 remains open;
  this is a release candidate, not a 1.0 release.

## M5 — images and embedded objects

- Added inline and floating images, main-story floating text boxes, and OLE
  ObjectPool packaging with field-result previews.
- Added selective recovery for damaged CHPX formatting and header stories.

## M4 — document structure

- Added tables, list numbering, fields, bookmarks, comments, footnotes, and
  endnotes, with OOXML parts for their supported main-story forms.

## M3 — formatting

- Added character, paragraph, section, and style parsing and writing, with
  sprm coverage tracked in [the acceptance ledger](docs/SPRM-COVERAGE.md).

## M2 — text path

- Added FIB and piece-table parsing, ANSI/UTF-16 text decoding, and the first
  end-to-end DOC to DOCX conversion path.

## M1 — container

- Added CFB/OLE container reading and the `inspect` CLI command.
