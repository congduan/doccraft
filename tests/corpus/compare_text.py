#!/usr/bin/env python3
"""Compare all corpus DOC texts against checked-in LibreOffice TXT baselines."""
import difflib
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree

engine = Path(sys.argv[1]).resolve()
repo = Path(__file__).resolve().parents[2]
corpus = repo / "tests/corpus/corpus"
baseline = repo / "tests/corpus/baseline"
ns = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"


def normalize(text):
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    # A row guard and cell end are two separate DOC control codes.
    text = re.sub(r"\n{2,}", "\n", text)
    return "\n".join(line.rstrip() for line in text.rstrip().split("\n"))


def docx_text(path):
    with zipfile.ZipFile(path) as archive:
        root = ElementTree.fromstring(archive.read("word/document.xml"))
    paragraphs = []
    for para in root.iter(ns + "p"):
        parts = []
        for node in para.iter():
            if node.tag == ns + "t":
                parts.append(node.text or "")
            elif node.tag == ns + "br":
                parts.append("\n")
        paragraphs.append("".join(parts))
    return "\n".join(paragraphs)


scores = []
with tempfile.TemporaryDirectory() as td:
    for source in sorted(corpus.glob("*.doc")):
        out = Path(td) / (source.stem + ".docx")
        subprocess.run([str(engine), "convert", str(source), "-o", str(out)], check=True)
        got = normalize(docx_text(out))
        ref = normalize((baseline / (source.stem + ".txt")).read_text(encoding="utf-8-sig"))
        # LO renders list numbering from PAPX, which is outside M2 text runs.
        ref_content = re.sub(r"(?m)^\s*[•]\s+", "", ref)
        ratio = difflib.SequenceMatcher(None, got, ref_content).ratio()
        scores.append(ratio)
        print(f"{source.name}: {ratio:.2%}")
        if ratio < 0.99:
            print("\n".join(difflib.unified_diff(ref_content.splitlines(), got.splitlines(),
                                                   fromfile="baseline", tofile="doccraft")))
print(f"minimum: {min(scores):.2%}")
assert len(scores) == 5 and min(scores) >= 0.99
