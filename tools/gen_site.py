#!/usr/bin/env python3
"""Render the GitHub Pages landing page from README.md.

Keeps the site source-of-truth in the README: headings, tables, lists and
inline code are converted to a small standalone HTML page (no framework),
gallery images are rewritten to the copied site/gallery directory.
"""
import html
import re
import sys
from pathlib import Path

GALLERY_PREFIX = "docs/gallery/"


def inline(text: str) -> str:
    text = html.escape(text, quote=False)
    text = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)",
                  lambda m: f'<img alt="{m.group(1)}" src="{rewrite(m.group(2))}">', text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)",
                  lambda m: f'<a href="{rewrite(m.group(2))}">{m.group(1)}</a>', text)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    return text


def rewrite(link: str) -> str:
    if link.startswith(("http://", "https://", "#")):
        return link
    if link.startswith(GALLERY_PREFIX):
        return "gallery/" + link[len(GALLERY_PREFIX):]
    if link.endswith(".md"):
        name = Path(link).stem
        return {
            "DESIGN": "https://github.com/congduan/doccraft/blob/main/docs/DESIGN.md",
            "KNOWN-SIMPLIFICATIONS": "https://github.com/congduan/doccraft/blob/main/docs/KNOWN-SIMPLIFICATIONS.md",
            "SPRM-COVERAGE": "https://github.com/congduan/doccraft/blob/main/docs/SPRM-COVERAGE.md",
            "PIXEL-BASELINE": "https://github.com/congduan/doccraft/blob/main/docs/PIXEL-BASELINE.md",
            "LICENSE": "https://github.com/congduan/doccraft/blob/main/LICENSE",
        }.get(name, link)
    return link


def render(md: str) -> str:
    out, in_code, code_buf, in_table, table_rows = [], False, [], False, []
    for line in md.splitlines():
        if line.startswith("```"):
            if in_code:
                out.append('<pre><code>' + html.escape("\n".join(code_buf)) + "</code></pre>")
                code_buf = []
            in_code = not in_code
            continue
        if in_code:
            code_buf.append(line)
            continue
        if line.startswith("|"):
            cells = [c.strip() for c in line.strip("|").split("|")]
            if all(re.fullmatch(r":?-+:?", c) for c in cells):
                continue
            table_rows.append(cells)
            in_table = True
            continue
        if in_table:
            if table_rows:
                head, *body = table_rows
                rows = "<tr>" + "".join(f"<th>{inline(c)}</th>" for c in head) + "</tr>"
                rows += "".join(
                    "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in body)
                out.append(f"<table>{rows}</table>")
            table_rows, in_table = [], False
        if line.startswith("!["):
            m = re.match(r"!\[([^\]]*)\]\(([^)]+)\)", line)
            if m:
                out.append(f'<img alt="{html.escape(m.group(1))}" src="{rewrite(m.group(2))}">')
            continue
        if line.startswith("#"):
            level = len(line) - len(line.lstrip("#"))
            out.append(f"<h{min(level, 4)}>" + inline(line.lstrip("#").strip()) + f"</h{min(level, 4)}>")
            continue
        if line.startswith(("- ", "* ")):
            out.append("<li>" + inline(line[2:]) + "</li>")
            continue
        if re.match(r"^\d+\. ", line):
            out.append("<li>" + inline(re.sub(r"^\d+\. ", "", line)) + "</li>")
            continue
        if line.strip():
            out.append("<p>" + inline(line) + "</p>")
    return "\n".join(out)


PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>doccraft — high-fidelity .doc to .docx converter</title>
<style>
body{{font:16px/1.6 -apple-system,Segoe UI,Roboto,sans-serif;max-width:900px;margin:2rem auto;padding:0 1rem;color:#1a1a1a}}
h1{{border-bottom:2px solid #1f6feb;padding-bottom:.3rem}}
h2{{margin-top:2rem}}
table{{border-collapse:collapse;margin:1rem 0;width:100%}}
th,td{{border:1px solid #d0d7de;padding:.4rem .6rem;text-align:left}}
th{{background:#f6f8fa}}
code{{background:#f6f8fa;padding:.1rem .3rem;border-radius:4px;font-size:.9em}}
pre code{{display:block;padding:1rem;overflow-x:auto}}
img{{max-width:100%;border:1px solid #d0d7de;margin:.5rem 0}}
li{{margin:.2rem 0}}
a{{color:#1f6feb}}
</style>
</head>
<body>
{body}
<p><a href="https://github.com/congduan/doccraft">View source on GitHub</a></p>
</body>
</html>
"""


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} README.md out.html", file=sys.stderr)
        return 2
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    dst.parent.mkdir(parents=True, exist_ok=True)
    body = render(src.read_text(encoding="utf-8"))
    dst.write_text(PAGE.format(body=body), encoding="utf-8")
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
