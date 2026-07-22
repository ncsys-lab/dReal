#!/usr/bin/env python3
r"""De-tag a CAPD Doxygen HTML page into clean, readable plain text.

CAPD ships no hand-written prose docs — its API reference is Doxygen HTML under
``../CAPD/docs/html/`` (2000+ pages, ~10-20x larger than their text content once
markup is stripped). This tool renders one page (or a glob of pages) as plain
text so a class's brief, detailed description, and member documentation can be
read/audited without wading through the HTML. It is the surviving, checked-in
replacement for the lost ephemeral ``/tmp/capd_detag.py``.

Dependency-free (Python 3 stdlib ``html.parser``) so the refresh stays hermetic
— no pandoc/lxml/BeautifulSoup install required.

Usage::

    # one class page -> stdout
    python3 scripts/refresh_capd_doxygen.py classcapd_1_1dynset_1_1C0DoubletonSet.html

    # a class by (fuzzy) name -> resolves the mangled Doxygen filename for you
    python3 scripts/refresh_capd_doxygen.py --name C0DoubletonSet

    # bulk: every class page whose name matches, into a directory of .txt files
    python3 scripts/refresh_capd_doxygen.py --name OdeSolver --out /tmp/capd_txt

Paths resolve against the sibling CAPD clone at ``../CAPD`` by default; override
with ``--html-dir``.
"""
from __future__ import annotations

import argparse
import glob
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_HTML_DIR = REPO.parent / "CAPD" / "docs" / "html"

# Tags whose *content* is noise (scripts, styles, nav chrome) — drop entirely.
_DROP_CONTENT = {"script", "style", "head"}
# Tags that should force a line break so structure survives the strip.
_BLOCK = {
    "p", "div", "br", "tr", "li", "h1", "h2", "h3", "h4", "table",
    "dt", "dd", "pre", "hr", "ul", "ol",
}


class _DoxyText(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self._drop_depth = 0

    def handle_starttag(self, tag, attrs):
        if tag in _DROP_CONTENT:
            self._drop_depth += 1
        elif tag in _BLOCK:
            self.parts.append("\n")

    def handle_endtag(self, tag):
        if tag in _DROP_CONTENT and self._drop_depth:
            self._drop_depth -= 1
        elif tag in _BLOCK:
            self.parts.append("\n")

    def handle_data(self, data):
        if self._drop_depth == 0:
            self.parts.append(data)

    def text(self) -> str:
        raw = "".join(self.parts).replace("\xa0", " ")
        # collapse intra-line whitespace, then squeeze blank-line runs.
        lines = [re.sub(r"[ \t]+", " ", ln).strip() for ln in raw.splitlines()]
        out, blank = [], 0
        for ln in lines:
            if ln:
                blank = 0
                out.append(ln)
            else:
                blank += 1
                if blank <= 1:
                    out.append("")
        return "\n".join(out).strip() + "\n"


def detag(html_text: str) -> str:
    parser = _DoxyText()
    parser.feed(html_text)
    return parser.text()


def _resolve(name: str, html_dir: Path) -> list[Path]:
    """Class *name* -> matching Doxygen page files (excludes -members.html)."""
    hits = [
        Path(p) for p in glob.glob(str(html_dir / f"*{name}*.html"))
        if not p.endswith("-members.html")
    ]
    return sorted(hits)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("page", nargs="?", help="Doxygen html filename (relative to --html-dir) or path")
    ap.add_argument("--name", help="resolve the mangled filename(s) for a class name")
    ap.add_argument("--html-dir", type=Path, default=DEFAULT_HTML_DIR)
    ap.add_argument("--out", type=Path, help="write <class>.txt into this dir instead of stdout")
    args = ap.parse_args()

    if args.name:
        pages = _resolve(args.name, args.html_dir)
        if not pages:
            sys.exit(f"no CAPD page matches name {args.name!r} in {args.html_dir}")
    elif args.page:
        p = Path(args.page)
        pages = [p if p.is_absolute() else args.html_dir / p]
    else:
        ap.error("give a page filename or --name")

    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
    for page in pages:
        text = detag(page.read_text(encoding="utf-8", errors="replace"))
        if args.out:
            dest = args.out / (page.stem + ".txt")
            dest.write_text(text, encoding="utf-8")
            print(f"wrote {dest}", file=sys.stderr)
        else:
            if len(pages) > 1:
                print(f"\n===== {page.name} =====")
            sys.stdout.write(text)


if __name__ == "__main__":
    main()
