#!/usr/bin/env python3
"""Compile the self-hosted BLT homepage stylesheet.

Replaces the Tailwind CDN script (blocked by CSP in local + production).
Uses Tailwind v3 so ``tailwind.blt-home.config.js`` content scanning works.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "tailwind.blt-home.config.js"
INPUT = ROOT / "tailwind.input.css"
OUTPUT = ROOT / "public" / "blt-home.css"
# Pin v3: the mise-global tailwindcss may be v4, which ignores this config shape.
NPM_TAILWIND = "tailwindcss@3.4.17"


def _npm_tailwind_cmd() -> list[str] | None:
    npm = shutil.which("npm")
    if not npm:
        return None
    return [
        npm,
        "exec",
        "--yes",
        f"--package={NPM_TAILWIND}",
        "--",
        "tailwindcss",
    ]


def _local_tailwind_v3() -> list[str] | None:
    for candidate in (
        shutil.which("tailwindcss"),
        str(Path.home() / ".local/share/mise/installs/ruby/3.4.5/bin/tailwindcss"),
    ):
        if not candidate or not Path(candidate).is_file():
            continue
        try:
            version = subprocess.check_output(
                [candidate, "-h"], text=True, stderr=subprocess.STDOUT
            )
        except (OSError, subprocess.CalledProcessError):
            continue
        if "tailwindcss v3." in version or "\nv3." in version:
            return [candidate]
        # `tailwindcss -h` on v3 prints "tailwindcss v3.x.x" on first line.
        first = version.strip().splitlines()[0] if version.strip() else ""
        if first.startswith("tailwindcss v3."):
            return [candidate]
    return None


def main() -> int:
    if not CONFIG.is_file() or not INPUT.is_file():
        raise SystemExit("missing blt-home Tailwind config or input CSS")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    prefix = _npm_tailwind_cmd() or _local_tailwind_v3()
    if not prefix:
        raise SystemExit(
            "tailwindcss v3 CLI not found; need npm or a v3 tailwindcss binary"
        )
    cmd = prefix + [
        "-c",
        str(CONFIG),
        "-i",
        str(INPUT),
        "-o",
        str(OUTPUT),
        "--minify",
    ]
    subprocess.run(cmd, cwd=ROOT, check=True)
    css = OUTPUT.read_text(encoding="utf-8")
    for required in (".min-h-screen", ".bg-gray-50", ".from-red-500", ".md\\:flex"):
        if required not in css:
            raise SystemExit(f"blt-home.css missing required utility {required}")
    print(f"built blt-home.css ({OUTPUT.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
