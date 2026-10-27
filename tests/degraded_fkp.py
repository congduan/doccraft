#!/usr/bin/env python3
"""A damaged local structure must retain readable body text and warn."""

import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="doccraft-fkp-") as directory:
        output = Path(directory) / "result.docx"
        result = subprocess.run(
            [sys.argv[1], "convert", sys.argv[2], "-o", str(output)],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr)
            return 1
        if sys.argv[3] not in result.stderr:
            print("missing FKP warning: " + result.stderr, file=sys.stderr)
            return 1
        with zipfile.ZipFile(output) as archive:
            xml = archive.read("word/document.xml").decode("utf-8")
        if sys.argv[4] not in xml:
            print("body text was lost", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
