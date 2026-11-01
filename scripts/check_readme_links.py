#!/usr/bin/env python3
"""Check that relative links and images in README.md exist on disk.

External URLs, mail links and pure anchors are ignored. Fenced code blocks are
removed first so command examples are not mistaken for links.
"""
import argparse
import re
import sys
from pathlib import Path
from urllib.parse import unquote

LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
FENCE = re.compile(r"```.*?```", re.S)


def targets(markdown: str):
    for match in LINK.finditer(FENCE.sub("", markdown)):
        target = match.group(1).strip()
        if target.startswith("<") and ">" in target:
            target = target[1:target.index(">")]
        else:
            target = target.split(maxsplit=1)[0]
        target = unquote(target.split("#", 1)[0])
        if not target or target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        yield target


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    readme = args.root / "README.md"
    if not readme.is_file():
        print(f"FAIL: {readme} does not exist", file=sys.stderr)
        return 1
    missing = sorted({target for target in targets(readme.read_text())
                      if not (args.root / target).exists()})
    if missing:
        for target in missing:
            print(f"missing: {target}", file=sys.stderr)
        print(f"FAIL: {len(missing)} relative link(s) do not exist",
              file=sys.stderr)
        return 1
    print("README relative links: all targets exist")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
