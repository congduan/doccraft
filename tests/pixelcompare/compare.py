#!/usr/bin/env python3
"""doccraft 渲染对拍管线（pixel-compare pipeline）

对拍原理：同一 .doc 源，两条渲染路径，逐页像素 diff：
  路径A（基准）：source.doc  --LibreOffice--> PDF --> PNG
  路径B（被测）：source.doc --doccraft--> out.docx --LibreOffice--> PDF --> PNG
  比较：逐页像素差分（RMS + 最大偏差 + 差异像素占比），产出差异热图

用法：
  python3 tests/pixelcompare/compare.py <source.doc> [--engine A_PATH] [--dpi N]
      [--docx PATH]   # 直接给定被测 docx（跳过 doccraft 转换，M2 前用于管线自检）

退出码：0 = 通过（指标在阈值内），1 = 超阈值，2 = 管线错误
阈值默认：页数一致、每页 diff 像素占比 < 0.5%（M2 前仅管线自检用）
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]

def run(cmd: list, **kw) -> subprocess.CompletedProcess:
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=180, **kw)
    if r.returncode != 0:
        print(f"CMD FAILED: {' '.join(map(str, cmd))}\n{r.stderr[:2000]}", file=sys.stderr)
        sys.exit(2)
    return r

def soffice_convert(src: Path, fmt: str, outdir: Path) -> Path:
    run(["soffice", "--headless", "--convert-to", fmt, "--outdir", str(outdir), str(src)])
    out = outdir / (src.stem + "." + fmt.split(":")[0])
    if not out.exists():
        print(f"convert produced nothing: {src} -> {fmt}", file=sys.stderr)
        sys.exit(2)
    return out

def pdf_to_pngs(pdf: Path, outdir: Path, dpi: int) -> list:
    run(["pdftoppm", "-png", "-r", str(dpi), str(pdf), str(outdir / "page")])
    return sorted(outdir.glob("page-*.png"))

def load_gray(p: Path) -> np.ndarray:
    return np.asarray(Image.open(p).convert("L"), dtype=np.int16)

def compare_pages(a_png: Path, b_png: Path, outdir: Path, idx: int) -> dict:
    a, b = load_gray(a_png), load_gray(b_png)
    if a.shape != b.shape:
        # 尺寸不一致是硬失败（版面几何漂移），先裁到公共区统计再报
        h, w = min(a.shape[0], b.shape[0]), min(a.shape[1], b.shape[1])
        a, b = a[:h, :w], b[:h, :w]
        size_mismatch = True
    else:
        size_mismatch = False
    diff = np.abs(a - b)
    total = diff.size
    bad = int((diff > 16).sum())          # 明显差异像素（>6% 灰度）
    metrics = {
        "page": idx,
        "size_mismatch": size_mismatch,
        "rms": float(np.sqrt((diff.astype(np.float64) ** 2).mean())),
        "max": int(diff.max()),
        "bad_px": bad,
        "bad_ratio": bad / total,
    }
    # 差异热图（放大可见）
    heat = np.zeros((*diff.shape, 3), dtype=np.uint8)
    heat[..., 0] = np.clip(diff * 3, 0, 255)
    Image.fromarray(heat).save(outdir / f"diff-{idx:02d}.png")
    return metrics

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path)
    ap.add_argument("--docx", type=Path, default=None,
                    help="被测 docx（缺省走 doccraft convert；M2 前可用 LibreOffice 生成的 docx 自检管线）")
    ap.add_argument("--engine", type=Path, default=REPO / "build" / "cli" / "doccraft")
    ap.add_argument("--dpi", type=int, default=150)
    ap.add_argument("--threshold", type=float, default=0.001,
                    help="单页 bad_ratio 阈值（默认 0.1%%）")
    args = ap.parse_args()

    if not args.source.exists():
        print(f"no such file: {args.source}", file=sys.stderr); return 2

    with tempfile.TemporaryDirectory(prefix="doccraft-pc-") as td:
        tdp = Path(td)
        a_dir, b_dir = tdp / "a", tdp / "b"
        a_dir.mkdir(); b_dir.mkdir()

        # 路径A：源 .doc 直接渲染
        a_pdf = soffice_convert(args.source, "pdf", a_dir)
        a_pngs = pdf_to_pngs(a_pdf, a_dir, args.dpi)

        # 路径B：被测 docx 渲染
        if args.docx:
            b_src = args.docx
        else:
            if not args.engine.exists():
                print(f"doccraft not built: {args.engine}", file=sys.stderr); return 2
            b_src = tdp / (args.source.stem + ".docx")
            run([str(args.engine), "convert", str(args.source), "-o", str(b_src)])
        b_pdf = soffice_convert(b_src, "pdf", b_dir)
        b_pngs = pdf_to_pngs(b_pdf, b_dir, args.dpi)

        if len(a_pngs) != len(b_pngs):
            print(f"FAIL page count: A={len(a_pngs)} B={len(b_pngs)}")
            return 1

        report, all_ok = [], True
        for i, (pa, pb) in enumerate(zip(a_pngs, b_pngs), 1):
            m = compare_pages(pa, pb, Path("build/pixelcompare"), i)
            report.append(m)
            ok = (not m["size_mismatch"]) and m["bad_ratio"] < args.threshold
            all_ok &= ok
            status = "OK " if ok else "FAIL"
            print(f"{status} page {i}: rms={m['rms']:.2f} max={m['max']} "
                  f"bad_px={m['bad_px']} ({m['bad_ratio']*100:.3f}%)"
                  + (" SIZE-MISMATCH" if m["size_mismatch"] else ""))
        Path("build/pixelcompare/report.tsv").write_text(
            "\n".join(f"{m['page']}\t{m['rms']:.4f}\t{m['max']}\t{m['bad_px']}\t{m['bad_ratio']:.6f}" for m in report))
        print(f"\n{'PASS' if all_ok else 'FAIL'} ({len(report)} pages, threshold {args.threshold*100:.1f}%)"
              f" — heatmaps: build/pixelcompare/")
        return 0 if all_ok else 1

if __name__ == "__main__":
    sys.exit(main())
