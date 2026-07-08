#!/usr/bin/env python3
"""Build static dashboard assets from source partials/fragments.

The dashboard remains authored as an HTML shell plus partials and ordered JS
fragments, but production must serve the fully composed assets directly from
Cloudflare Static Assets. Building them here keeps hot dashboard requests out
of the Python Worker runtime.
"""

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
PUBLIC = ROOT / "public"

if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import dashboard_bundle  # noqa: E402
import dashboard_shell  # noqa: E402


def _read(rel):
    return (PUBLIC / rel).read_text(encoding="utf-8")


def _write_if_changed(rel, text):
    path = PUBLIC / rel
    old = path.read_text(encoding="utf-8") if path.exists() else None
    if old == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return True


def main():
    shell = dashboard_shell.compose_from_reader(_read)
    script = dashboard_bundle.compose_from_reader(_read)
    changed = []
    for rel, text in (
        ("dashboard/index.html", shell),
        ("dashboard.html", shell),
        ("dashboard.js", script),
    ):
        if _write_if_changed(rel, text):
            changed.append(rel)
    if changed:
        print("Built dashboard assets: " + ", ".join(changed))
    else:
        print("Dashboard assets are up to date.")


if __name__ == "__main__":
    main()
