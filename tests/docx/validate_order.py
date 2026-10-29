#!/usr/bin/env python3
"""Check that core OPC parts precede discovered document parts."""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

engine, source = map(Path, sys.argv[1:3])
core_prefix = [
    "[Content_Types].xml",
    "_rels/.rels",
    "word/_rels/document.xml.rels",
    "word/document.xml",
    "word/styles.xml",
    "word/numbering.xml",
]
for fixture in ("header-textbox.doc", "inline-image.doc"):
    with tempfile.TemporaryDirectory() as td:
        output = Path(td) / "out.docx"
        subprocess.run([str(engine), "convert", str(source / fixture),
                        "-o", str(output)], check=True)
        with zipfile.ZipFile(output) as archive:
            prefix = archive.namelist()[:len(core_prefix)]
            assert prefix == core_prefix, (fixture, prefix)
        print(f"{fixture}: {prefix}")
