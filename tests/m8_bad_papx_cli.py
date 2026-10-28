#!/usr/bin/env python3
"""A damaged PAPX page omits its table and reports the skipped region."""

import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


def convert(binary: str, source: Path, target: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [binary, "convert", str(source), "-o", str(target)],
        capture_output=True,
        text=True,
        timeout=30,
    )


def main() -> int:
    generator, converter, source = sys.argv[1:4]
    with tempfile.TemporaryDirectory(prefix="doccraft-papx-") as directory:
        root = Path(directory)
        damaged = root / "damaged.doc"
        made = subprocess.run([generator, source, str(damaged)], timeout=30)
        if made.returncode:
            return made.returncode
        original = convert(converter, Path(source), root / "original.docx")
        changed = convert(converter, damaged, root / "damaged.docx")
        if original.returncode or changed.returncode:
            print(original.stderr + changed.stderr, file=sys.stderr)
            return 1
        if "warning: skipped PAPX FKP page" not in changed.stderr:
            print("missing PAPX warning: " + changed.stderr, file=sys.stderr)
            return 1
        with zipfile.ZipFile(root / "original.docx") as archive:
            baseline = archive.read("word/document.xml")
        with zipfile.ZipFile(root / "damaged.docx") as archive:
            xml = archive.read("word/document.xml")
        if b"<w:tbl>" not in baseline or xml.count(b"<w:tbl>") >= baseline.count(b"<w:tbl>"):
            print("damaged table was not omitted", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
