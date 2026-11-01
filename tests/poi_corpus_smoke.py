#!/usr/bin/env python3
"""Smoke-test the Apache POI real-world corpus: no crashes, sane success rate.

The corpus lives outside the repo (download once, see docs/CI.md). Point
DOCRAFT_CORPUS_DIR at the directory that holds the POI .doc files to enable
this test on a local or self-hosted runner. Skipped cleanly when the directory
is absent so upstream CI without the corpus still passes."""
import os
import subprocess
import sys
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
corpus = Path(os.environ.get(
    "DOCRAFT_CORPUS_DIR", "/home/ubuntu/doccraft-corpus/poi-test-data"))
if not corpus.is_dir():
    print(f"POI corpus not present - skipping (download to {corpus})")
    sys.exit(0)

files = sorted(corpus.glob("*.doc"))
if len(files) < 100:
    print(f"FAIL: corpus shrunk to {len(files)} files, expected >=100")
    sys.exit(1)

ok = 0
crashes = []
for f in files:
    try:
        r = subprocess.run([str(binary), "convert", str(f), "-o", "/tmp/doccraft-poi-smoke.docx"],
                           capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        crashes.append((f.name, "timeout"))
        continue
    if r.returncode < 0 or r.returncode > 100:
        crashes.append((f.name, f"rc={r.returncode}"))
    elif r.returncode == 0:
        ok += 1

if crashes:
    for name, why in crashes[:10]:
        print(f"CRASH {name}: {why}")
    sys.exit(1)
if ok < 100:
    print(f"FAIL: only {ok}/{len(files)} converted, regression against the 102 baseline")
    sys.exit(1)
print(f"POI corpus smoke: {ok}/{len(files)} converted, 0 crashes")
