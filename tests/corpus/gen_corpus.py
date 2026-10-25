#!/usr/bin/env python3
"""Generate .doc golden corpus for doccraft M2+ via LibreOffice headless.

Creates minimal .doc files covering the feature checklist, plus matching
.docx references for roundtrip diff testing.

Text baselines (tests/corpus/baseline/*.txt) are a separate step:
    soffice --headless --convert-to txt --outdir <tmp> <sample>.doc
then replace TAB with newline (LibreOffice separates table cells with TAB, the
checked-in baselines use one line per cell) and keep the UTF-8 BOM.
"""
import subprocess
import sys
from pathlib import Path

OUT = Path(__file__).parent / "corpus"
OUT.mkdir(exist_ok=True)

# Each entry: (name, odt/fodt content snippet set via CLI below)
# We generate .fodt (flat XML, easy to author), then convert to .doc and .docx.
SAMPLES = {
    "plain-text": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:body><office:text>
<text:p>Hello doccraft. Plain single paragraph.</text:p>
<text:p>Second paragraph with more text.</text:p>
</office:text></office:body></office:document>""",
    "bold-italic-underline": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:automatic-styles>
<style:style style:name="T1" style:family="text">
<style:text-properties fo:font-weight="bold" fo:font-style="italic"
 style:text-underline-style="solid"/></style:style>
</office:automatic-styles>
<office:body><office:text>
<text:p>Normal <text:span text:style-name="T1">bold italic underline</text:span> end.</text:p>
</office:text></office:body></office:document>""",
    "alignment-indents": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:automatic-styles>
<style:style style:name="P1" style:family="paragraph">
<style:paragraph-properties fo:text-align="center" fo:margin-top="0.2in"
 fo:margin-bottom="0.2in" fo:line-height="150%"/></style:style>
<style:style style:name="P2" style:family="paragraph">
<style:paragraph-properties fo:text-align="justify" fo:margin-left="0.5in"/></style:style>
</office:automatic-styles>
<office:body><office:text>
<text:p text:style-name="P1">Centered paragraph</text:p>
<text:p text:style-name="P2">Justified indented paragraph</text:p>
</office:text></office:body></office:document>""",
    "simple-table": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:body><office:text>
<text:p>Before table</text:p>
<table:table table:name="T1">
<table:table-column table:number-columns-repeated="2"/>
<table:table-row>
<table:table-cell><text:p>A1</text:p></table:table-cell>
<table:table-cell><text:p>B1</text:p></table:table-cell>
</table:table-row>
<table:table-row>
<table:table-cell><text:p>A2</text:p></table:table-cell>
<table:table-cell><text:p>B2</text:p></table:table-cell>
</table:table-row>
</table:table>
<text:p>After table</text:p>
</office:text></office:body></office:document>""",
    "headings-list": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:body><office:text>
<text:h text:outline-level="1">Heading 1</text:h>
<text:p>Intro</text:p>
<text:h text:outline-level="2">Heading 2</text:h>
<text:list><text:list-item><text:p>Item one</text:p></text:list-item>
<text:list-item><text:p>Item two</text:p></text:list-item></text:list>
</office:text></office:body></office:document>""",
    "cjk-text": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:body><office:text>
<text:p>中文段落：文档转换需要保留全部字符。</text:p>
<text:p>日本語の段落です。文字化けしてはいけません。</text:p>
<text:p>Mixed 中文 and Latin text 混排。</text:p>
</office:text></office:body></office:document>""",
    "cyrillic-text": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:body><office:text>
<text:p>Первый абзац на русском языке.</text:p>
<text:p>Второй абзац с числами 123 и символами №.</text:p>
</office:text></office:body></office:document>""",
    "complex-table": """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
<office:body><office:text>
<text:p>Before complex table</text:p>
<table:table table:name="T1">
<table:table-column table:number-columns-repeated="3"/>
<table:table-row>
<table:table-cell table:number-columns-spanned="2" table:number-rows-spanned="1"><text:p>Merged two</text:p></table:table-cell>
<table:table-cell><text:p>C1</text:p></table:table-cell>
</table:table-row>
<table:table-row>
<table:table-cell><text:p>Multi one</text:p><text:p>Multi two</text:p></table:table-cell>
<table:table-cell><text:p>B2</text:p></table:table-cell>
<table:table-cell><text:p>C2</text:p></table:table-cell>
</table:table-row>
</table:table>
<text:p>After complex table</text:p>
</office:text></office:body></office:document>""",
}

def convert(src: Path, fmt: str) -> Path:
    subprocess.run(
        ["soffice", "--headless", "--convert-to", fmt, "--outdir", str(src.parent), str(src)],
        check=True, capture_output=True, timeout=120)
    return src.with_suffix("." + fmt.split(":")[0])

def main() -> int:
    made = []
    for name, fodt in SAMPLES.items():
        src = OUT / f"{name}.fodt"
        src.write_text(fodt, encoding="utf-8")
        doc = convert(src, "doc:MS Word 97")
        docx = convert(src, "docx:MS Word 2007 XML")
        assert doc.exists() and docx.exists(), f"conversion failed for {name}"
        made += [doc.name, docx.name]
        print(f"OK {doc.name} ({doc.stat().st_size}B) + {docx.name} ({docx.stat().st_size}B)")
    print(f"\n{len(made)} files in {OUT}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
