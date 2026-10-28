#!/usr/bin/env python3
"""Check that core OPC parts precede discovered document parts."""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

engine, source = map(Path, sys.argv[1:3])
for fixture in ("header-textbox.doc", "inline-image.doc"):
    with tempfile.TemporaryDirectory() as td:
        output = Path(td) / "out.docx"
        subprocess.run([str(engine), "convert", str(source / fixture),
                        "-o", str(output)], check=True)
        with zipfile.ZipFile(output) as archive:
            first = archive.namelist()[0]
            assert first == "[Content_Types].xml", (fixture, first)
        print(f"{fixture}: {first}")
