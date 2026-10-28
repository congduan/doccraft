#!/usr/bin/env python3
"""Benchmark DOC conversion against a concurrently built reference commit."""
import argparse
from datetime import datetime, timezone
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path(__file__).with_name("baseline.json")
DEFAULT_BASELINE_REF = "ee2df0e"  # Last verified clean Release comparison.
WALL_REGRESSION = 0.20
RSS_WARNING = 0.30
RUNS = 5


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


def build_reference(ref: str, worktree: Path, googletest: Path) -> Path:
    """Build a detached reference without changing the main checkout or build."""
    subprocess.run(["git", "worktree", "add", "--detach", str(worktree), ref],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    build = worktree / "build"
    env = os.environ.copy()
    for name in ("CFLAGS", "CXXFLAGS", "LDFLAGS"):
        env.pop(name, None)
    try:
        configure = ["cmake", "-S", str(worktree), "-B", str(build),
                     "-DCMAKE_BUILD_TYPE=Release"]
        if googletest.exists():
            configure.append(f"-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST={googletest}")
        subprocess.run(
            configure,
            check=True, capture_output=True, text=True, env=env)
        subprocess.run(["cmake", "--build", str(build), "--target", "doccraft",
                        "-j"], check=True, capture_output=True, text=True,
                       env=env)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(error.stderr.strip() or error.stdout.strip()) from error
    return build / "src" / "cli" / "doccraft"


def comparison(value: float, baseline: float | None, threshold: float,
               label: str) -> tuple[str, bool]:
    if not baseline:
        return "-", False
    change = value / baseline - 1
    return (f"{change * 100:+.1f}%" + (f" {label}" if change > threshold else ""),
            change > threshold)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, default=ROOT / "build" / "perf")
    ap.add_argument("--engine", type=Path,
                    default=ROOT / "build" / "src" / "cli" / "doccraft")
    ap.add_argument("--baseline", type=Path, default=BASELINE)
    ap.add_argument("--baseline-ref", default=DEFAULT_BASELINE_REF,
                    help="reference commit for concurrent comparison")
    ap.add_argument("--write-baseline", action="store_true")
    ap.add_argument("--sizes", type=int, nargs="+",
                    default=[1, 5, 20, 50, 100])
    args = ap.parse_args()
    prior = json.loads(args.baseline.read_text()) if args.baseline.exists() else {}
    rows = dict(prior)
    failed = False
    with tempfile.TemporaryDirectory(prefix="doccraft-perf-") as tmp:
        ref_engine = None
        worktree = Path(tmp) / "reference"
        if not args.write_baseline:
            try:
                ref_engine = build_reference(args.baseline_ref, worktree,
                                             ROOT / "build" / "_deps" / "googletest-src")
                print(f"Reference: {args.baseline_ref} (concurrent Release build)",
                      flush=True)
            except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
                detail = (error.stderr.strip() if isinstance(
                    error, subprocess.CalledProcessError) else str(error))
                print(f"WARNING: reference build failed: {detail}; using JSON baseline",
                      file=sys.stderr, flush=True)
        else:
            rows["_meta"] = {
                "recorded_at_utc": datetime.now(timezone.utc).isoformat(
                    timespec="seconds"),
                "commit": subprocess.check_output(
                    ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "method": "clean Release build; idle machine; five conversions per size; "
                          "median wall seconds and maximum peak RSS KB",
            }
        mode = "recording" if args.write_baseline else (
            "concurrent" if ref_engine else "JSON")
        print(f"Comparison: {mode}", flush=True)
        print("| Input | Size MiB | Median wall s | Peak RSS MiB | Baseline s | Change | Baseline RSS MiB | RSS change |")
        print("|---|---:|---:|---:|---:|---:|---:|---:|")
        try:
            for size in args.sizes:
                source = args.corpus / f"structured-{size}mb.doc"
                if not source.exists():
                    raise FileNotFoundError(f"{source}: run gen_corpus.py first")
                times, rss, ref_times, ref_rss = [], [], [], []
                try:
                    for i in range(RUNS):
                        # Reverse order each round to reduce time drift bias.
                        engines = ([args.engine, ref_engine] if i % 2 == 0 else
                                   [ref_engine, args.engine]) if ref_engine else [args.engine]
                        for engine in engines:
                            t, m = one_run(engine, source, Path(tmp) /
                                           f"out-{size}-{i}-{len(times) + len(ref_times)}.docx")
                            (times if engine == args.engine else ref_times).append(t)
                            (rss if engine == args.engine else ref_rss).append(m)
                except (RuntimeError, subprocess.TimeoutExpired) as error:
                    rows[str(size)] = {"size_bytes": source.stat().st_size,
                                       "error": str(error)}
                    print(f"| {source.name} | {source.stat().st_size / 2**20:.2f} | "
                          f"FAIL | - | - | {error} | - | - |", flush=True)
                    failed = True
                    continue
                median = statistics.median(times)
                peak = max(rss)
                if args.write_baseline:
                    rows[str(size)] = {"size_bytes": source.stat().st_size,
                                       "wall_seconds": round(median, 6),
                                       "peak_rss_kb": peak}
                old = prior.get(str(size), {})
                baseline = (None if args.write_baseline else
                            statistics.median(ref_times) if ref_engine else old.get(
                                "wall_seconds"))
                baseline_rss = (None if args.write_baseline else
                                max(ref_rss) if ref_engine else old.get("peak_rss_kb"))
                wall_text, wall_bad = comparison(median, baseline, WALL_REGRESSION,
                                                 "REGRESSION")
                rss_text, rss_bad = comparison(peak, baseline_rss, RSS_WARNING,
                                               "RSS WARNING")
                if wall_bad and not args.write_baseline:
                    failed = True
                baseline_text = f"{baseline:.3f}" if baseline else "-"
                baseline_rss_text = f"{baseline_rss / 1024:.1f}" if baseline_rss else "-"
                print(f"| {source.name} | {source.stat().st_size / 2**20:.2f} | "
                      f"{median:.3f} | {peak / 1024:.1f} | {baseline_text} | "
                      f"{wall_text} | {baseline_rss_text} | {rss_text} |",
                      flush=True)
                if rss_bad:
                    print(f"WARNING: {source.name}: peak RSS exceeds reference by "
                          f"more than {RSS_WARNING:.0%}", file=sys.stderr,
                          flush=True)
        finally:
            if worktree.exists():
                subprocess.run(["git", "worktree", "remove", "--force",
                                str(worktree)], cwd=ROOT, check=True)
    if args.write_baseline:
        args.baseline.write_text(json.dumps(rows, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
