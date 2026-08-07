#!/usr/bin/env python3
"""
Run the prose-lint skill over the comments in C++ sources.

prose_lint.py reads markdown. C++ comments are not markdown, and two of the
differences matter enough that feeding it a source file directly produces a
useless report:

1. The linter applies its sentence rules PER PHYSICAL LINE. A comment sentence
   wrapped across three 76-column lines is seen as three short fragments, so
   LEN001 - the rule worth running this for - never fires at all.
2. Comment blocks are surrounded by code, and code is full of semicolons,
   braces and identifiers that trip the punctuation and length rules.

So this extracts the comment prose, unwraps each paragraph onto one line, and
writes a SHADOW file: same line count as the source, paragraph text sitting at
the line where the paragraph starts, everything else blank. Line numbers then
pass through the linter untouched and every finding points at a real source
line, with no mapping table to get wrong.

Non-prose inside comments is dropped before linting - indented code samples,
rule-off dividers, tables and ASCII diagrams - because they are not writing and
their punctuation is not a writing fault.

    tools/comment_lint.py src/*.cpp src/*.h
    tools/comment_lint.py src/game.cpp --json
    tools/comment_lint.py src/game.cpp --emit-shadow /tmp/shadow

Exit code is the linter's: 0 all files at or under threshold, 1 any over.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Files whose comments are emitted by a generator. Editing them here would be
# overwritten on the next run of the generator - fix tools/gen_*.py instead.
GENERATED = {"instructions.cpp", "credits.cpp", "cutscene.cpp", "ending.cpp",
             "cp437_font.cpp", "wave_text.cpp"}

LINTER_CANDIDATES = [
    Path.home() / ".claude/skills/prose-lint/scripts/prose_lint.py",
    Path(".claude/skills/prose-lint/scripts/prose_lint.py"),
]

# Comment lines that are not prose.
DIVIDER_RE = re.compile(r"^[-=*_+~#]{3,}$")
PRAGMA_RE = re.compile(r"^(clang-format|NOLINT|IWYU|cppcheck|@\w+$)")
INDENTED_CODE_RE = re.compile(r"^ {4,}\S")


def find_linter(override: str | None) -> Path:
    if override:
        p = Path(override)
        if not p.is_file():
            sys.exit(f"comment_lint: no linter at {p}")
        return p
    for p in LINTER_CANDIDATES:
        if p.is_file():
            return p
    sys.exit("comment_lint: prose_lint.py not found; pass --linter PATH")


def extract_comments(text: str) -> list[tuple[int, str, bool]]:
    """Return [(lineno, comment_text, own_line)] for every comment in a C++ file.

    Walks the file as characters rather than matching // with a regex, so a
    marker inside a string literal - "http://" is the one that actually occurs -
    is not mistaken for a comment.

    own_line is True when nothing but the comment is on that line; a trailing
    comment after code is annotation and is never joined to its neighbours.
    """
    out: list[tuple[int, str, bool]] = []
    i, n = 0, len(text)
    lineno = 1
    line_had_code = False
    in_block = False
    block_started_own_line = False

    def line_start_is_bare(pos: int) -> bool:
        """True when only whitespace precedes pos on its line."""
        j = pos - 1
        while j >= 0 and text[j] != "\n":
            if not text[j].isspace():
                return False
            j -= 1
        return True

    while i < n:
        c = text[i]

        if c == "\n":
            lineno += 1
            line_had_code = False
            i += 1
            continue

        if in_block:
            end = text.find("*/", i)
            stop = end if end != -1 else n
            chunk = text[i:stop]
            for k, raw in enumerate(chunk.split("\n")):
                body = raw
                if k > 0:
                    # strip the ' * ' gutter
                    body = re.sub(r"^\s*\* ?", "", body)
                out.append((lineno + k, body, block_started_own_line))
            lineno += chunk.count("\n")
            i = stop + 2 if end != -1 else n
            in_block = False
            continue

        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == "\n":       # unterminated; resync
                    break
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            line_had_code = True
            continue

        if c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                end = text.find("\n", i)
                end = n if end == -1 else end
                out.append((lineno, text[i + 2:end], not line_had_code))
                i = end
                continue
            if text[i + 1] == "*":
                block_started_own_line = line_start_is_bare(i)
                in_block = True
                i += 2
                continue

        if not c.isspace():
            line_had_code = True
        i += 1

    return out


def is_prose(line: str) -> bool:
    """False for comment content that is not writing."""
    s = line.strip()
    if not s:
        return False
    if DIVIDER_RE.match(s) or PRAGMA_RE.match(s):
        return False
    if s.startswith("|"):                      # table row
        return False
    if INDENTED_CODE_RE.match(line):           # code sample inside a comment
        return False
    letters = sum(ch.isalpha() for ch in s)
    if letters / len(s) < 0.4:                 # ASCII art, hex dump, address list
        return False
    if letters < 3:
        return False
    return True


def build_shadow(text: str) -> tuple[list[str], int]:
    """Return (shadow_lines, paragraph_count) for one source file."""
    nlines = len(text.splitlines())
    shadow = [""] * max(nlines, 1)
    comments = extract_comments(text)

    by_line = {}
    for lineno, body, own in comments:
        # A block comment can put several entries on one line; keep the first.
        by_line.setdefault(lineno, (body, own))

    para: list[str] = []
    para_start = 0
    paras = 0

    def flush():
        nonlocal para, para_start, paras
        if para:
            joined = " ".join(p.strip() for p in para).strip()
            if joined:
                shadow[para_start - 1] = joined
                paras += 1
        para = []

    for lineno in range(1, nlines + 1):
        entry = by_line.get(lineno)
        if entry is None:
            flush()
            continue
        body, own = entry
        if not own:
            # trailing annotation: its own paragraph, never joined
            flush()
            if is_prose(body):
                shadow[lineno - 1] = body.strip()
                paras += 1
            continue
        if not is_prose(body):
            flush()
            continue
        if not para:
            para_start = lineno
        para.append(body)
    flush()

    return shadow, paras


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Lint the prose in C++ comments with the prose-lint skill.")
    ap.add_argument("paths", nargs="+", help="C++ sources")
    ap.add_argument("-m", "--mode", default="standard",
                    choices=["strict", "standard", "chat"])
    ap.add_argument("-t", "--threshold", default="2.0")
    ap.add_argument("--ignore", default="")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--linter", help="path to prose_lint.py")
    ap.add_argument("--emit-shadow", metavar="DIR",
                    help="write the extracted prose and stop, for inspection")
    ap.add_argument("--include-generated", action="store_true",
                    help="also lint files listed as generator output")
    args = ap.parse_args()

    linter = find_linter(args.linter)

    sources = []
    for p in args.paths:
        path = Path(p)
        if not path.is_file():
            print(f"skip: {path} is not a file", file=sys.stderr)
            continue
        if path.name in GENERATED and not args.include_generated:
            print(f"skip: {path} is generated - fix tools/gen_*.py instead",
                  file=sys.stderr)
            continue
        sources.append(path)
    if not sources:
        return 2

    outdir = Path(args.emit_shadow) if args.emit_shadow else Path(tempfile.mkdtemp())
    outdir.mkdir(parents=True, exist_ok=True)

    shadow_to_src: dict[str, Path] = {}
    empty: list[Path] = []
    for src in sources:
        text = src.read_text(encoding="utf-8", errors="replace")
        shadow, paras = build_shadow(text)
        if paras == 0:
            empty.append(src)
            continue
        # flat name, so two dirs with the same basename cannot collide
        flat = str(src).replace(os.sep, "__") + ".txt"
        dest = outdir / flat
        dest.write_text("\n".join(shadow) + "\n", encoding="utf-8")
        shadow_to_src[str(dest)] = src

    if args.emit_shadow:
        for dest, src in sorted(shadow_to_src.items(), key=lambda kv: str(kv[1])):
            print(f"{src} -> {dest}")
        return 0

    if not shadow_to_src:
        print("no comment prose found")
        return 0

    cmd = [sys.executable, str(linter), "--json", "--mode", args.mode,
           "--threshold", str(args.threshold)]
    if args.ignore:
        cmd += ["--ignore", args.ignore]
    cmd += sorted(shadow_to_src)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode == 2:
        sys.stderr.write(proc.stderr)
        return 2

    payload = json.loads(proc.stdout)
    for f in payload["files"]:
        f["path"] = str(shadow_to_src[f["path"]])
    payload["files"].sort(key=lambda f: -f["score"])

    if args.json:
        print(json.dumps(payload, indent=2))
        return proc.returncode

    total_words = sum(f["words"] for f in payload["files"])
    total_find = sum(len(f["findings"]) for f in payload["files"])
    for f in payload["files"]:
        flag = "PASS" if f["pass"] else "FAIL"
        print(f"{f['path']:<26} {f['words']:>6} words  "
              f"{len(f['findings']):>4} findings  score {f['score']:>5.2f}  {flag}")
        if args.quiet:
            continue
        for fi in f["findings"]:
            print(f"    {f['path']}:{fi['line']}  {fi['rule']}  {fi['message']}")
            print(f"        {fi['excerpt']}")
    rules: dict[str, int] = {}
    for f in payload["files"]:
        for fi in f["findings"]:
            rules[fi["rule"]] = rules.get(fi["rule"], 0) + 1
    print(f"\n{len(payload['files'])} files, {total_words} words of comment prose, "
          f"{total_find} findings")
    print("  " + "  ".join(f"{r}:{n}" for r, n in sorted(rules.items())))
    if empty:
        print(f"  ({len(empty)} file(s) with no comment prose)")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
