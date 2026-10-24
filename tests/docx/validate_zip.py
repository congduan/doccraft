#!/usr/bin/env python3
"""Exercise the CLI and validate the miniz OPC package with zipfile."""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree

engine, source = map(Path, sys.argv[1:3])
with tempfile.TemporaryDirectory() as td:
    output = Path(td) / "out.docx"
    subprocess.run([str(engine), "convert", str(source), "-o", str(output)], check=True)
    with zipfile.ZipFile(output) as archive:
        expected = {"[Content_Types].xml", "_rels/.rels", "word/document.xml",
                    "word/styles.xml", "word/numbering.xml",
                    "word/_rels/document.xml.rels"}
        assert set(archive.namelist()) == expected
        assert archive.testzip() is None
        assert all(item.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)
                   for item in archive.infolist())
        for name in expected:
            ElementTree.fromstring(archive.read(name))
print("ZIP/CRC/XML PASS")
