#!/usr/bin/env python3
"""Validate WMF/EMF media parts produced from the corpus by rendering them
with the wmf-emf-renderer JS package (congduan/WmfEmfViewer). Skipped when
the corpus or the renderer checkout is absent."""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
corpus = Path("/home/ubuntu/doccraft-corpus")
renderer = Path(os.environ.get("WMF_RENDERER_DIR", "/tmp/WmfEmfViewer"))
node = "/usr/bin/node" if Path("/usr/bin/node").exists() else "node"
if not corpus.is_dir() or not renderer.is_dir():
    print("corpus or wmf-emf-renderer not present - skipping")
    sys.exit(0)

samples = []
workdir = Path(tempfile.mkdtemp())  # lives until after the node invocation
for doc in sorted(corpus.rglob("*.doc"))[:200]:
    out = workdir / "o.docx"
    r = subprocess.run([str(binary), "convert", str(doc), "-o", str(out)],
                       capture_output=True, timeout=60)
    if r.returncode != 0 or not out.exists():
        continue
    try:
        import zipfile
        with zipfile.ZipFile(out) as z:
            for info in z.infolist():
                if info.filename.endswith((".wmf", ".emf")):
                    p = workdir / f"{doc.stem}_{len(samples)}_{Path(info.filename).name}"
                    p.write_bytes(z.read(info.filename))
                    samples.append(str(p))
    except Exception:
        pass
    out.unlink()
    if len(samples) >= 40:
        break

if not samples:
    print("no WMF/EMF media parts found - skipping")
    sys.exit(0)

manifest = workdir / "metafiles.json"
manifest.write_text(json.dumps(samples[:40]))
check = Path(__file__).resolve().parent / "wmf_emf_render_check.js"
r = subprocess.run([node, str(check), str(manifest)], capture_output=True, text=True)
print(r.stdout.strip())
if r.returncode != 0:
    sys.stderr.write(r.stderr)
    sys.exit(1)
