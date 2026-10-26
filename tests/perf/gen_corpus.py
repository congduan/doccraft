#!/usr/bin/env python3
"""Generate repeatable structured DOC performance samples with LibreOffice."""
import argparse
import html
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "build" / "perf"
HEAD = '''<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:automatic-styles><style:page-layout style:name="pm1">
<style:page-layout-properties fo:page-width="8.5in" fo:page-height="11in"
 fo:margin="0.8in"/></style:page-layout></office:automatic-styles>
<office:master-styles><style:master-page style:name="Standard"
 style:page-layout-name="pm1"><style:header><text:p>Performance corpus header</text:p></style:header>
<style:footer><text:p>Performance corpus footer</text:p></style:footer>
</style:master-page></office:master-styles><office:body><office:text>
'''
TAIL = '</office:text></office:body></office:document>\n'
# Long, varied paragraphs make the DOC body large without huge XML node counts.
WORDS = ("Alpha beta gamma delta epsilon zeta eta theta iota kappa. "
         "A structured conversion benchmark includes prose, lists, tables, "
         "notes, and page furniture. 0123456789 ")


def make_fodt(path: Path, repeats: int, words_per_paragraph: int) -> None:
    with path.open("w", encoding="utf-8") as f:
        f.write(HEAD)
        for i in range(repeats):
            if i % 50 == 0:
                f.write(f'<text:h text:outline-level="1">Chapter {i // 50 + 1}</text:h>\n')
            paragraph = html.escape(f"Paragraph {i:06d}: " +
                                    WORDS * words_per_paragraph)
            if i % 50 == 0:
                paragraph += (f'<text:note text:note-class="footnote" text:id="n{i}">'
                              f'<text:note-citation>{i // 50 + 1}</text:note-citation>'
                              f'<text:note-body><text:p>Note for chapter {i // 50 + 1}.'
                              '</text:p></text:note-body></text:note>')
            f.write(f'<text:p>{paragraph}</text:p>\n')
            if i % 50 == 1:
                f.write('<table:table table:name="Grid"><table:table-column '
                        'table:number-columns-repeated="2"/><table:table-row>'
                        f'<table:table-cell><text:p>Row {i}</text:p></table:table-cell>'
                        '<table:table-cell><text:p>Value</text:p></table:table-cell>'
                        '</table:table-row></table:table>\n')
        f.write(TAIL)


def generate(size_mb: int, out: Path) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    if size_mb == 100:
        source = out / "structured-20mb.doc"
        if not source.exists():
            generate(20, out)
        engine = ROOT / "build" / "tests" / "perf_repack_body"
        subprocess.run(["cmake", "--build", str(ROOT / "build"),
                        "--target", "perf_repack_body", "-j2"], check=True)
        target = out / "structured-100mb.doc"
        subprocess.run([str(engine), str(source), str(target)], check=True)
        print(f"{target.name}: {target.stat().st_size / 2**20:.2f} MiB "
              "(five referenced copies of the 20 MiB structured body)",
              flush=True)
        return target
    if size_mb == 50:
        source = out / "structured-20mb.doc"
        if not source.exists():
            generate(20, out)
        engine = ROOT / "build" / "tests" / "perf_repack"
        subprocess.run(["cmake", "--build", str(ROOT / "build"),
                        "--target", "perf_repack", "-j2"], check=True)
        target = out / "structured-50mb.doc"
        payload = 30 * 1024 * 1024
        subprocess.run([str(engine), str(source), str(target), str(payload)],
                       check=True)
        print(f"{target.name}: {target.stat().st_size / 2**20:.2f} MiB "
              "(20 MiB structured body + 30 MiB Data stream)", flush=True)
        return target
    fodt = out / f"structured-{size_mb}mb.fodt"
    doc = fodt.with_suffix(".doc")
    # LibreOffice stores this ASCII-heavy body at roughly twice the FODT size.
    words_per_paragraph = 16
    repeats = max(1, int(size_mb * 1024 * 1024 /
                         (2 * (len(WORDS) * words_per_paragraph + 200))))
    for attempt in range(3):
        make_fodt(fodt, repeats, words_per_paragraph)
        if doc.exists():
            doc.unlink()
        result = subprocess.run(
            ["soffice", "--headless", "--convert-to", "doc:MS Word 97",
             "--outdir", str(out), str(fodt)], capture_output=True, text=True,
            timeout=1800)
        if result.returncode or not doc.exists():
            raise RuntimeError(f"LibreOffice conversion failed: {result.stderr}")
        actual = doc.stat().st_size
        target = size_mb * 1024 * 1024
        print(f"{doc.name}: {actual / 1024 / 1024:.2f} MiB ({repeats} paragraphs)",
              flush=True)
        if 0.9 * target <= actual <= 1.1 * target:
            break
        repeats = max(1, int(repeats * target / actual))
    fodt.unlink()
    return doc


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sizes", type=int, nargs="+", default=[1, 5, 20, 50])
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()
    for size in args.sizes:
        generate(size, args.out)


if __name__ == "__main__":
    main()
