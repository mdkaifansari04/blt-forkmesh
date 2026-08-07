#!/usr/bin/env python3
"""Stage the Worker's Python with comments and docstrings removed.

Cloudflare validates a Python Worker inside a fixed memory allowance that
holds entry.py's executed global scope AND the source of every attached
module. That allowance is what fails deploys with error 10021, and it is
also why moving code between files in src/ does not help: the bytes are
still attached. 16% of src/ is comments and docstrings — text the runtime
never needs but the validator still pays for.

This writes src_build/, a byte-reduced copy that the Worker actually
ships. src/ stays the source of truth: every comment in this codebase
survives in git, in review, and in the tests (which read src/).

**Line numbers are preserved exactly.** Comments are blanked in place
rather than deleted, and a removed docstring leaves the same number of
lines behind, so a production traceback still points at the right line of
src/. Getting that wrong would trade a memory win for permanently
misleading error reports.

    tools/build_worker_python.py [--check]

--check reports the savings without writing.
"""

from __future__ import annotations

import ast
import io
import shutil
import sys
import tokenize
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src"
STAGED = ROOT / "src_build"


def _blank_comments(source: str) -> str:
    """Drop comment text, keeping every line and its code intact."""
    lines = source.splitlines(keepends=True)
    cuts: dict[int, int] = {}
    for token in tokenize.generate_tokens(io.StringIO(source).readline):
        if token.type == tokenize.COMMENT:
            row, col = token.start
            # A line can hold only one comment start; keep the earliest.
            cuts[row] = min(cuts.get(row, col), col)
    for row, col in cuts.items():
        line = lines[row - 1]
        ending = "\r\n" if line.endswith("\r\n") else (
            "\n" if line.endswith("\n") else "")
        head = line[:col].rstrip()
        # A whole-line comment collapses to an empty line, not to a line of
        # trailing spaces.
        lines[row - 1] = (head + ending) if head else ending
    return "".join(lines)


def _strip_docstrings(source: str) -> str:
    """Remove docstrings, replacing each with as many blank lines."""
    tree = ast.parse(source)
    lines = source.splitlines(keepends=True)
    removals: list[tuple[int, int, bool]] = []
    for node in ast.walk(tree):
        if not isinstance(
            node, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef,
                   ast.ClassDef)
        ):
            continue
        body = getattr(node, "body", [])
        if not body:
            continue
        first = body[0]
        if not (
            isinstance(first, ast.Expr)
            and isinstance(first.value, ast.Constant)
            and isinstance(first.value.value, str)
        ):
            continue
        # A body that is ONLY a docstring still needs a statement.
        removals.append((first.lineno, first.end_lineno, len(body) == 1))
    for start, end, needs_pass in sorted(removals, reverse=True):
        indent = len(lines[start - 1]) - len(lines[start - 1].lstrip())
        replacement = (" " * indent + "pass\n") if needs_pass else "\n"
        lines[start - 1:end] = [replacement] + ["\n"] * (end - start)
    return "".join(lines)


def main(argv: list[str]) -> int:
    check_only = "--check" in argv[1:]
    before = after = 0
    staged_files: list[tuple[Path, bytes]] = []

    for path in sorted(SOURCE.rglob("*")):
        if path.is_dir() or path.name.startswith("."):
            continue
        relative = path.relative_to(SOURCE)
        # Byte-for-byte staging (below) would otherwise attach every .pyc
        # pytest left in src/__pycache__ — 3.7MB of duplicate modules the
        # runtime never imports, straight onto the 10021 startup budget.
        if "__pycache__" in relative.parts:
            continue
        raw = path.read_bytes()
        before += len(raw)
        if path.suffix != ".py":
            # Non-Python payloads (vendored binaries, data files) ship
            # byte-for-byte.
            staged_files.append((relative, raw))
            after += len(raw)
            continue
        source = raw.decode("utf-8")
        reduced = _strip_docstrings(_blank_comments(source))
        # Refuse to ship anything whose meaning could have shifted: the
        # stripped copy must parse to the same tree as the original once
        # docstrings are discarded from both.
        if ast.dump(ast.parse(reduced)) != ast.dump(
            ast.parse(_strip_docstrings(source))
        ):
            print("ERROR: %s changed meaning when stripped" % relative,
                  file=sys.stderr)
            return 1
        if len(reduced.splitlines()) != len(source.splitlines()):
            print("ERROR: %s changed line count" % relative, file=sys.stderr)
            return 1
        staged_files.append((relative, reduced.encode("utf-8")))
        after += len(reduced.encode("utf-8"))

    saved = before - after
    print("worker python: %d -> %d bytes (saved %d, %.1f%%)"
          % (before, after, saved, 100.0 * saved / max(1, before)))
    if check_only:
        return 0

    if STAGED.exists():
        shutil.rmtree(STAGED)
    for relative, payload in staged_files:
        target = STAGED / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    print("staged %d files in %s" % (len(staged_files),
                                     STAGED.relative_to(ROOT)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
