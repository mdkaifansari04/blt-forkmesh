"""Test helper: compose the dashboard SPA shell from its HTML partials.

``public/dashboard/index.html`` is now just the shell scaffold with
``<!--#include partial="name"-->`` placeholders; the Worker stitches the
partials in at request time (see ``src/dashboard_shell.py``). Frontend contract
tests that assert on the rendered dashboard chrome should read the *assembled*
document, which is exactly what a browser receives, via ``assembled_dashboard()``.
"""

import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
_SRC = _ROOT / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

import dashboard_shell  # noqa: E402

PUBLIC = _ROOT / "public"


def assembled_dashboard() -> str:
    """The fully composed dashboard shell (index.html + all its partials)."""
    return dashboard_shell.compose_from_reader(
        lambda rel: (PUBLIC / rel).read_text(encoding="utf-8"))
