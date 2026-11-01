#!/usr/bin/env python3
"""Convert one DOC five times and compare the median with baseline.json.

The comparison is advisory: a slow result prints a GitHub warning annotation
and a step-summary row but exits 0. A conversion failure exits 1.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("document", type=Path)
    parser.add_argument("--baseline", type=Path, default=None)
    parser.add_argument("--size", default="5")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--threshold", type=float, default=0.30)
    args = parser.parse_args()

    times = []
    with tempfile.TemporaryDirectory(prefix="doccraft-perf-") as td:
        out = Path(td) / "out.docx"
        for _ in range(args.runs):
            start = time.perf_counter()
            result = subprocess.run(
                [str(args.binary), "convert", str(args.document), "-o", str(out)],
                capture_output=True, text=True, timeout=600)
            times.append(time.perf_counter() - start)
            if result.returncode != 0:
                print(f"FAIL: conversion returned {result.returncode}: "
                      f"{result.stderr.strip()}", file=sys.stderr)
                return 1
            out.unlink()

    median = statistics.median(times)
    baseline = None
    if args.baseline and args.baseline.is_file():
        data = json.loads(args.baseline.read_text())
        baseline = data.get(args.size, {}).get("wall_seconds")
    row = f"| {args.document.name} | {median:.3f} s |"
    print(f"median: {median:.3f} s over {args.runs} runs")
    if baseline:
        change = median / baseline - 1
        verdict = f"{change * 100:+.1f}%"
        print(f"baseline: {baseline:.3f} s ({verdict})")
        if change > args.threshold:
            verdict += " WARNING"
            message = (f"performance smoke: {args.document.name} is "
                       f"{change * 100:.1f}% slower than baseline "
                       f"({median:.3f} s vs {baseline:.3f} s)")
            print(f"::warning::{message}")
        row += f" {baseline:.3f} s | {verdict} |"
    else:
        row += " n/a | n/a |"
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write("| Sample | Median | Baseline | Change |\n")
            handle.write("|---|---:|---:|---:|\n")
            handle.write(row + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
