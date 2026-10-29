#!/usr/bin/env python3
"""Convert every synthetic M20 edge sample and assert its expectation.

Each <name>.doc is paired with either <name>.expected.txt (conversion must
succeed and produce that text) or <name>.expected-error (conversion must fail
with that keyword in the diagnostic).

Usage:
  python3 tests/corpus/run_edge_corpus.py <doccraft-binary> [edge-dir]
"""
import difflib
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree

NS = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"


def normalize(text: str) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    # Tab characters are formatting, not text: LO TXT export drops them and
    # so does the expected corpus (control-whitespace expects "AB\nC").
    text = text.replace("\t", "")
    text = re.sub(r"\n{2,}", "\n", text)
    return "\n".join(line.rstrip() for line in text.rstrip().split("\n"))


def docx_text(path: Path) -> str:
    with zipfile.ZipFile(path) as archive:
        root = ElementTree.fromstring(archive.read("word/document.xml"))
    paragraphs = []
    for para in root.iter(NS + "p"):
        parts = []
        for node in para.iter():
            if node.tag == NS + "t":
                parts.append(node.text or "")
            elif node.tag == NS + "tab":
                parts.append("\t")
            elif node.tag == NS + "br":
                parts.append("\n")
        paragraphs.append("".join(parts))
    return "\n".join(paragraphs)


def short(text: str, limit: int = 240) -> str:
    return text if len(text) <= limit else text[:limit] + "..."


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: run_edge_corpus.py <doccraft> [edge-dir]", file=sys.stderr)
        return 2
    engine = Path(sys.argv[1]).resolve()
    edge = Path(sys.argv[2]) if len(sys.argv) > 2 else (
        Path(__file__).resolve().parent / "edge")
    samples = sorted(edge.glob("*.doc"))
    if not samples:
        print(f"no samples in {edge}", file=sys.stderr)
        return 1

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        for source in samples:
            name = source.stem
            out = Path(tmp) / (name + ".docx")
            result = subprocess.run(
                [str(engine), "convert", str(source), "-o", str(out)],
                capture_output=True, text=True)
            error_file = edge / (name + ".expected-error")
            text_file = edge / (name + ".expected.txt")
            if error_file.exists():
                keyword = error_file.read_text(encoding="utf-8").strip()
                if result.returncode == 0:
                    failures.append(f"{name}: expected rejection, converted")
                    print(f"FAIL {name}: expected error '{keyword}'")
                elif keyword not in result.stderr:
                    failures.append(f"{name}: wrong diagnostic")
                    print(f"FAIL {name}: expected '{keyword}' in "
                          f"{short(result.stderr.strip())!r}")
                else:
                    print(f"OK   {name}: rejected with '{keyword}'")
                continue
            if not text_file.exists():
                failures.append(f"{name}: missing expectation")
                print(f"FAIL {name}: no .expected.txt or .expected-error")
                continue
            if result.returncode != 0:
                failures.append(f"{name}: conversion failed")
                print(f"FAIL {name}: {short(result.stderr.strip())!r}")
                continue
            expected = normalize(text_file.read_text(encoding="utf-8"))
            got = normalize(docx_text(out))
            if got != expected:
                failures.append(f"{name}: text mismatch")
                print(f"FAIL {name}: text mismatch")
                diff = difflib.unified_diff(
                    expected.splitlines(), got.splitlines(),
                    fromfile="expected", tofile="doccraft", lineterm="")
                print(short("\n".join(diff)))
            else:
                print(f"OK   {name}: {len(got)} chars")

    print(f"\n{len(samples) - len(failures)}/{len(samples)} edge samples passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
