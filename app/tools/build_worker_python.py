#!/usr/bin/env python3
"""Stage compact, semantics-equivalent Python for the Worker runtime.

Cloudflare validates a Python Worker inside a fixed memory allowance that
holds entry.py's executed global scope AND the source of every attached
module. That allowance is what fails deploys with error 10021, and it is
also why moving code between files in src/ does not help: the bytes are
still attached. 16% of src/ is comments and docstrings — text the runtime
never needs but the validator still pays for.

This writes src_build/, a byte-reduced copy that the Worker actually ships.
src/ stays the source of truth for review and tests. The staged modules are
round-tripped through Python's AST after docstrings are removed, which drops
comments and formatting without changing executable structure. Function names
remain present in production tracebacks; line numbers refer to the compact
staged module.

    tools/build_worker_python.py [--check]

--check reports the savings without writing.
"""

from __future__ import annotations

import ast
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src"
STAGED = ROOT / "src_build"


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


def _compact_python(source: str) -> str:
    """Return the smallest stdlib-generated source with the same AST."""
    without_docstrings = _strip_docstrings(source)
    expected = ast.parse(without_docstrings)
    compact = ast.unparse(expected) + "\n"
    if ast.dump(ast.parse(compact)) != ast.dump(expected):
        raise ValueError("AST round-trip changed executable structure")
    return compact


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
        try:
            reduced = _compact_python(source)
        except (SyntaxError, ValueError) as error:
            print("ERROR: %s could not be compacted: %s" % (relative, error),
                  file=sys.stderr)
            return 1
        staged_files.append((relative, reduced.encode("utf-8")))
        after += len(reduced.encode("utf-8"))

    saved = before - after
    print("worker python: %d -> %d bytes (saved %d, %.1f%%)"
          % (before, after, saved, 100.0 * saved / max(1, before)))
    if check_only:
        return 0

    # Wrangler may run this custom build concurrently while a previous
    # src_build tree is still being read; ignore_errors + a short retry avoids
    # fatal "Directory not empty" races that abort the whole build and leave
    # local ASSETS in a broken 500/503 state.
    if STAGED.exists():
        for _attempt in range(5):
            try:
                shutil.rmtree(STAGED, ignore_errors=False)
                break
            except OSError:
                shutil.rmtree(STAGED, ignore_errors=True)
                if not STAGED.exists():
                    break
                time.sleep(0.05)
        if STAGED.exists():
            shutil.rmtree(STAGED, ignore_errors=True)
    for relative, payload in staged_files:
        target = STAGED / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    print("staged %d files in %s" % (len(staged_files),
                                     STAGED.relative_to(ROOT)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
