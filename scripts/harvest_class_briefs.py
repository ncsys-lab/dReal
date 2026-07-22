#!/usr/bin/env python3
"""Regenerate the ``index/all-classes.md`` stub index for the CAPD and IBEX
knowledge trees from ground truth.

Two independent harvesters (the class *inventory* differs in source of truth):

* ``--ibex`` walks the fork headers ``../ibex-fork/src/<module>/ibex_*.h``,
  extracts each header's primary-class ``\\brief`` doxygen line, and groups the
  classes by source module. IBEX ships no per-class HTML, so the header IS the
  ground truth.
* ``--capd`` parses CAPD's generated Doxygen ``../CAPD/docs/html/annotated.html``
  (the class index), pulling each class's name, one-line description cell, and
  page link.

Both are idempotent: identical inputs produce byte-identical output. This file
replaces the two lost ephemeral harvesters (``/tmp/ibex_harvest.py`` and the
index half of ``/tmp/capd_detag.py``) so a post-pin-bump refresh is one command.

Dependency-free (Python 3 stdlib only) so the refresh stays hermetic.

Usage::

    python3 scripts/harvest_class_briefs.py --ibex          # rewrite ibex_docs/index/all-classes.md
    python3 scripts/harvest_class_briefs.py --capd          # rewrite capd_docs/index/all-classes.md
    python3 scripts/harvest_class_briefs.py --all
    python3 scripts/harvest_class_briefs.py --capd --check   # print unified diff vs committed, write nothing

Sibling layout assumed (matches the relative links already in the trees)::

    new_dreal/
      dreal4-cmake/          <- this repo (scripts/ lives here)
      ibex-fork/             <- IBEX fork checkout (header ground truth)
      CAPD/                  <- CAPD docs-generation clone (Doxygen html/)
"""
from __future__ import annotations

import argparse
import difflib
import html
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent          # dreal4-cmake/
NEW_DREAL = REPO.parent                                 # new_dreal/
IBEX_FORK = NEW_DREAL / "ibex-fork"
CAPD_CLONE = NEW_DREAL / "CAPD"

IBEX_INDEX = REPO / "ibex_docs" / "index" / "all-classes.md"
CAPD_INDEX = REPO / "capd_docs" / "index" / "all-classes.md"


# --------------------------------------------------------------------------- #
# IBEX: harvest class \brief from fork headers, grouped by source module.
# --------------------------------------------------------------------------- #
IBEX_HEADER = """\
# All IBEX classes — stub index (the breadth catch-all)

Every public header in the fork (`ncsys-lab/ibex-lib@dreal-perf-patches`),
grouped by source module, with the header's own one-line `\\brief` and a link to
the header (the ground truth — IBEX's docs are chapter-based, with no per-class
page). This is the catch-all for any class without a rich node under
[`../classes/`](../classes/) or a narrative in [`../chapters/`](../chapters/).

- **Briefs are harvested verbatim** from each header's `\\brief` doxygen line
  attached to the file's primary class — source-faithful by construction, but
  terse and occasionally imperfect (a few headers carry a misleading first
  brief). When precision matters, open the linked header.
- Modules that dReal binds: `arithmetic`, `function`, `symbolic`, `contractor`,
  `numeric` (linearizers + LP), `system`. Modules dReal ignores by design:
  `solver`, `optim`, `loup`, `strategy` (search), `cell`, `bisector`, `set`,
  `predicate` (separators), `parser`, `combinatorial`. See
  [`../dreal-ibex-usage.md`](../dreal-ibex-usage.md).
- Regenerate with `python3 scripts/harvest_class_briefs.py --ibex`.

<!-- BODY BELOW IS AUTO-HARVESTED -->
"""


def _ibex_brief_for(text: str, cls: str) -> str:
    r"""Best-effort ``\brief`` for the primary class ``cls`` in header ``text``.

    Prefer the ``\brief`` inside the doc-comment block immediately preceding the
    ``class``/``struct <cls>`` declaration; fall back to the first ``\brief`` in
    the file. Returns a single collapsed line (may be empty)."""
    decl = re.search(rf"\b(?:class|struct)\s+{re.escape(cls)}\b", text)
    windows = []
    if decl:
        windows.append(text[max(0, decl.start() - 1200):decl.start()])
    windows.append(text)  # fallback: whole file, first \brief
    for window in windows:
        m = None
        for m in re.finditer(r"[\\@]brief\s+(.*?)(?:[\\@]\w|\*/|\n\s*\*\s*\n)", window, re.S):
            pass  # keep last brief in the preceding window (closest to the class)
        if m:
            brief = re.sub(r"\s*\*\s*", " ", m.group(1))       # drop comment stars
            brief = re.sub(r"\s+", " ", brief).strip()
            return brief
    return ""


def harvest_ibex() -> str:
    modules: dict[str, list[tuple[str, str]]] = {}
    for hdr in sorted(IBEX_FORK.glob("src/*/ibex_*.h")):
        module = hdr.parent.name
        cls = hdr.stem[len("ibex_"):]
        text = hdr.read_text(encoding="utf-8", errors="replace")
        brief = _ibex_brief_for(text, cls)
        rel = f"../../../ibex-fork/src/{module}/{hdr.name}"
        modules.setdefault(module, []).append((cls, brief, rel))
    total = sum(len(v) for v in modules.values())
    out = [IBEX_HEADER, "", f"<!-- harvested {total} headers across {len(modules)} modules -->", ""]
    for module in sorted(modules):
        rows = sorted(modules[module], key=lambda r: r[0].lower())
        out.append(f"### `{module}/` ({len(rows)})")
        out.append("")
        out.append("| Class / header | One-line brief (from header) |")
        out.append("|---|---|")
        for cls, brief, rel in rows:
            out.append(f"| [`{cls}`]({rel}) | {brief} |")
        out.append("")
    return "\n".join(out).rstrip() + "\n"


# --------------------------------------------------------------------------- #
# CAPD: harvest class name / desc / page from Doxygen annotated.html.
# --------------------------------------------------------------------------- #
CAPD_HEADER = """\
# CAPD — all classes (auto-harvested stub index)

One line per class from CAPD's Doxygen `annotated.html`. Links are relative to the
CAPD docs html dir (`../../../CAPD/docs/html/`). For rich summaries of the
performance-relevant classes see `../classes/`; for concepts see `../concepts/`.
Regenerate with `python3 scripts/harvest_class_briefs.py --capd`.

| Class | Brief | Page |
|---|---|---|
"""

CAPD_ROW = re.compile(
    r'<a class="el" href="((?:class|struct)[^"]+\.html)"[^>]*>([^<]+)</a>'
    r'</td><td class="desc">(.*?)</td>',
    re.S,
)


def _clean_desc(raw: str) -> str:
    txt = re.sub(r"<[^>]+>", "", raw)          # strip tags
    txt = html.unescape(txt).replace("\xa0", " ")
    return re.sub(r"\s+", " ", txt).strip()


def harvest_capd() -> str:
    annotated = (CAPD_CLONE / "docs" / "html" / "annotated.html").read_text(
        encoding="utf-8", errors="replace"
    )
    rows = []
    for href, name, desc in CAPD_ROW.findall(annotated):
        rows.append((name.strip(), _clean_desc(desc), href))
    rows.sort(key=lambda r: (r[0].lower(), r[2]))
    out = [CAPD_HEADER.rstrip("\n")]
    for name, brief, href in rows:
        out.append(f"| `{name}` | {brief} | [html](../../../CAPD/docs/html/{href}) |")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------- #
def _emit(kind: str, content: str, dest: Path, check: bool) -> None:
    if check:
        old = dest.read_text(encoding="utf-8") if dest.exists() else ""
        diff = list(difflib.unified_diff(
            old.splitlines(True), content.splitlines(True),
            fromfile=f"{dest} (committed)", tofile=f"{dest} (regenerated)",
        ))
        if diff:
            sys.stdout.writelines(diff)
            print(f"\n[{kind}] {len(diff)} diff lines vs committed.", file=sys.stderr)
        else:
            print(f"[{kind}] byte-identical to committed.", file=sys.stderr)
    else:
        dest.write_text(content, encoding="utf-8")
        print(f"[{kind}] wrote {dest} ({content.count(chr(10))} lines).", file=sys.stderr)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ibex", action="store_true")
    ap.add_argument("--capd", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--check", action="store_true", help="diff vs committed, write nothing")
    args = ap.parse_args()
    if not (args.ibex or args.capd or args.all):
        ap.error("pick --ibex, --capd, or --all")
    if args.ibex or args.all:
        _emit("ibex", harvest_ibex(), IBEX_INDEX, args.check)
    if args.capd or args.all:
        _emit("capd", harvest_capd(), CAPD_INDEX, args.check)


if __name__ == "__main__":
    main()
