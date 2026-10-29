#!/usr/bin/env python3
"""Check writer part order, allowing story relationships before document.xml."""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

engine, source = map(Path, sys.argv[1:3])
fixed_prefix = [
    "[Content_Types].xml",
    "_rels/.rels",
    "word/_rels/document.xml.rels",
]
core_parts = ("word/document.xml", "word/styles.xml", "word/numbering.xml")
for fixture in ("header-textbox.doc", "inline-image.doc"):
    with tempfile.TemporaryDirectory() as td:
        output = Path(td) / "out.docx"
        subprocess.run([str(engine), "convert", str(source / fixture),
                        "-o", str(output)], check=True)
        with zipfile.ZipFile(output) as archive:
            parts = archive.namelist()
            assert parts[:len(fixed_prefix)] == fixed_prefix, (fixture, parts)
            core_indices = [parts.index(part) for part in core_parts]
            assert core_indices == sorted(core_indices), (fixture, parts)
            generated = [i for i, part in enumerate(parts)
                         if part.startswith(("word/media/", "word/header",
                                             "word/footer"))]
            assert all(core_indices[-1] < i for i in generated), (fixture, parts)
            if "word/settings.xml" in parts:
                assert parts[-1] == "word/settings.xml", (fixture, parts)
        print(f"{fixture}: {parts}")
