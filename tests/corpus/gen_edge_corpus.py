#!/usr/bin/env python3
"""Refresh the synthetic M20 edge corpus.

The .doc images come from the small C++ writer tests/edge_gen.cc, which reuses
the CfbBuilder container layout the unit tests already trust. This front end
only drives that binary and writes the per-sample expectation next to it:

  <name>.doc             synthetic Word 97 file
  <name>.expected.txt    expected text extracted from doccraft's .docx
  <name>.expected-error  expected rejection; the file holds a message keyword

Usage:
  python3 tests/corpus/gen_edge_corpus.py [build/tests/edge_gen]
"""
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = Path(__file__).parent / "edge"


def nested(depth: int) -> str:
    lines = [f"L{i}" for i in range(1, depth + 1)]
    lines += [f"T{i}" for i in range(depth - 1, 0, -1)]
    return "\n".join(lines)


def cp1252_text() -> str:
    # Bytes 0x00-0x1F become table/field/paragraph structure; the observed
    # text prefix is four newlines. 0x20-0x7F pass through, 0x80-0xFF decode
    # through CP1252 with its five undefined positions passed through raw.
    def char(byte: int) -> str:
        try:
            return bytes([byte]).decode("cp1252")
        except UnicodeDecodeError:
            return chr(byte)

    return "\n\n\n\n" + "".join(char(b) for b in range(0x20, 256))


EXPECTED_TEXT = {
    "empty-document": "",
    "single-char": "A",
    "two-paragraphs": "AB\nCD",
    "nested-table-d2": nested(2),
    "nested-table-d4": nested(4),
    "nested-table-d8": nested(8),
    "nested-table-d9": nested(9),
    "huge-cell-text": "X" * 70000,
    "many-columns": "\n".join(f"C{i}" for i in range(1, 71)),
    "control-whitespace": "AB\nC",
    "control-page-break": "A\nB",
    "control-section-break": "A\nB",
    "control-field-chars": "ARESULTB",
    "control-object-marks": "AB",
    "piece-fcmin-mismatch": "AB",
    "piece-mixed-encoding": "AB\u4e2d\u6587CD",
    "cp1252-all-256": cp1252_text(),
    "utf16-surrogate-pair": "\U0001f600X",
    "utf16-unpaired-surrogate": "\ufffdX",
    "style-cycle": "Cycled",
    "style-deep-chain": "Deep",
    "field-nested-5": "NR0R1R2R3R4",
    "field-missing-separator": "",
    "field-unclosed": "RESULT",
}

EXPECTED_ERROR = {
    "piece-cp-descending": "invalid piece CP order",
    "piece-outside-stream": "piece outside WordDocument",
    "piece-prm-index": "CLX Prm1 index outside Prc array",
}


def main() -> int:
    engine = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build/tests/edge_gen"
    if not engine.exists():
        print(f"edge generator not built: {engine}", file=sys.stderr)
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    for stale in list(OUT.glob("*.expected.txt")) + list(OUT.glob("*.expected-error")):
        stale.unlink()

    produced = subprocess.run(
        [str(engine), str(OUT)], check=True, capture_output=True, text=True
    )
    names = {line.split()[0] for line in produced.stdout.splitlines() if line}
    expected = set(EXPECTED_TEXT) | set(EXPECTED_ERROR)
    if names != expected:
        print(f"generator/sample mismatch: {sorted(names ^ expected)}",
              file=sys.stderr)
        return 1

    for name, text in EXPECTED_TEXT.items():
        (OUT / f"{name}.expected.txt").write_text(text, encoding="utf-8")
    for name, keyword in EXPECTED_ERROR.items():
        (OUT / f"{name}.expected-error").write_text(keyword + "\n",
                                                    encoding="utf-8")

    docs = sorted(OUT.glob("*.doc"))
    print(f"{len(docs)} edge samples and {len(expected)} expectations in {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
