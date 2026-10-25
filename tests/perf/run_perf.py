#!/usr/bin/env python3
"""Benchmark DOC conversion at four sizes; flag median wall regressions."""
import argparse
import json
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path(__file__).with_name("baseline.json")


def one_run(engine: Path, source: Path, out: Path) -> tuple[float, int]:
    rss_file = out.with_suffix(".rss")
    start = time.perf_counter()
    result = subprocess.run(
        ["/usr/bin/time", "-f", "%M", "-o", str(rss_file),
         str(engine), "convert", str(source), "-o", str(out)],
        capture_output=True, text=True, timeout=600)
    elapsed = time.perf_counter() - start
    if result.returncode:
        raise RuntimeError(f"{source.name}: {result.stderr.strip()}")
    rss_kb = int(rss_file.read_text().strip())
    rss_file.unlink()
    out.unlink()
    return elapsed, rss_kb


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, default=ROOT / "build" / "perf")
    ap.add_argument("--engine", type=Path,
                    default=ROOT / "build" / "src" / "cli" / "doccraft")
    ap.add_argument("--baseline", type=Path, default=BASELINE)
    ap.add_argument("--write-baseline", action="store_true")
    ap.add_argument("--sizes", type=int, nargs="+", default=[1, 5, 20, 50])
    args = ap.parse_args()
    prior = json.loads(args.baseline.read_text()) if args.baseline.exists() else {}
    rows = dict(prior)
    failed = False
    print("| Input | Size MiB | Median wall s | Peak RSS MiB | Baseline s | Change |")
    print("|---|---:|---:|---:|---:|---:|")
    with tempfile.TemporaryDirectory(prefix="doccraft-perf-") as tmp:
        for size in args.sizes:
            source = args.corpus / f"structured-{size}mb.doc"
            if not source.exists():
                raise FileNotFoundError(f"{source}: run gen_corpus.py first")
            times, rss = [], []
            try:
                for i in range(5):
                    t, m = one_run(args.engine, source,
                                   Path(tmp) / f"out-{size}-{i}.docx")
                    times.append(t)
                    rss.append(m)
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                rows[str(size)] = {"size_bytes": source.stat().st_size,
                                   "error": str(error)}
                print(f"| {source.name} | {source.stat().st_size / 2**20:.2f} | "
                      f"FAIL | - | - | {error} |", flush=True)
                failed = True
                if args.write_baseline:
                    args.baseline.write_text(json.dumps(rows, indent=2) + "\n")
                continue
            median = statistics.median(times)
            peak = max(rss)
            rows[str(size)] = {"size_bytes": source.stat().st_size,
                               "wall_seconds": round(median, 6),
                               "peak_rss_kb": peak}
            baseline = prior.get(str(size), {}).get("wall_seconds")
            change = (median / baseline - 1) if baseline else None
            if change is not None and change > 0.20 and not args.write_baseline:
                failed = True
            baseline_text = f"{baseline:.3f}" if baseline else "-"
            change_text = (f"{change * 100:+.1f}%" if change is not None
                           else "-")
            if change is not None and change > 0.20:
                change_text += " REGRESSION"
            print(f"| {source.name} | {source.stat().st_size / 2**20:.2f} | "
                  f"{median:.3f} | {peak / 1024:.1f} | "
                  f"{baseline_text} | {change_text} |", flush=True)
            if args.write_baseline:
                args.baseline.write_text(json.dumps(rows, indent=2) + "\n")
    if args.write_baseline:
        args.baseline.write_text(json.dumps(rows, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
