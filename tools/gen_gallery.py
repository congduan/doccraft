#!/usr/bin/env python3
"""Render representative DOC samples through both paths into gallery PNGs.

Requires a built doccraft, LibreOffice, pdftoppm, and Pillow. Each image shows
the first page of the source DOC on the left and the converted DOCX on the
right. Use --check to verify that rerendering reproduces committed images.
"""

import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SAMPLES = (
    ("plain-text", "tests/corpus/corpus/plain-text.doc"),
    ("complex-table", "tests/corpus/corpus/complex-table.doc"),
    ("cjk-text", "tests/corpus/corpus/cjk-text.doc"),
    ("inline-image", "tests/fixtures/inline-image.doc"),
    ("textbox", "tests/fixtures/textbox.doc"),
    ("ole-embedded", "tests/fixtures/ole-embedded.doc"),
    ("fast-saved-o-kurs", "tests/fixtures/fast-saved-o-kurs.doc"),
)


def run(*args: str) -> None:
    subprocess.run(args, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, timeout=180)


def render(source: Path, work: Path, profile: Path, dpi: int) -> Image.Image:
    work.mkdir()
    run("soffice", f"-env:UserInstallation={profile.as_uri()}",
        "--headless", "--convert-to", "pdf", "--outdir", str(work),
        str(source))
    pdf = work / f"{source.stem}.pdf"
    if not pdf.is_file():
        raise RuntimeError(f"LibreOffice did not produce {pdf}")
    png = work / "page.png"
    run("pdftoppm", "-f", "1", "-singlefile", "-r", str(dpi),
        "-png", str(pdf), str(png.with_suffix("")))
    with Image.open(png) as image:
        return image.convert("RGB")


def combine(left: Image.Image, right: Image.Image) -> Image.Image:
    gutter = 16
    canvas = Image.new("RGB", (left.width + right.width + gutter,
                               max(left.height, right.height)), "#e5e5e5")
    canvas.paste(left, (0, 0))
    canvas.paste(right, (left.width + gutter, 0))
    return canvas


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path,
                        default=ROOT / "build/src/cli/doccraft")
    parser.add_argument("--outdir", type=Path, default=ROOT / "docs/gallery")
    parser.add_argument("--dpi", type=int, default=100)
    parser.add_argument("--check", action="store_true",
                        help="compare regenerated PNG bytes with committed files")
    args = parser.parse_args()
    engine = args.engine.resolve()
    outdir = args.outdir.resolve()
    if not engine.is_file():
        parser.error(f"build the converter first: {engine}")
    if args.dpi <= 0:
        parser.error("--dpi must be positive")
    if not args.check:
        outdir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="doccraft-gallery-") as temp:
        tmp = Path(temp)
        for name, relative in SAMPLES:
            source = ROOT / relative
            item = tmp / name
            item.mkdir()
            converted = item / f"{name}.docx"
            run(str(engine), "convert", str(source), "-o", str(converted))
            left = render(source, item / "source", item / "lo-source", args.dpi)
            right = render(converted, item / "converted", item / "lo-converted",
                           args.dpi)
            generated = item / f"{name}.png"
            combine(left, right).save(generated, optimize=True)
            destination = outdir / generated.name
            if args.check:
                if not destination.is_file() or sha256(generated) != sha256(destination):
                    print(f"MISMATCH {destination}")
                    return 1
                print(f"OK {destination.relative_to(ROOT)}")
            else:
                destination.write_bytes(generated.read_bytes())
                print(f"WROTE {destination.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
