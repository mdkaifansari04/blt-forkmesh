"""Test helper: compose the dashboard SPA shell from its HTML partials.

``public/dashboard/shell.html`` is the authored scaffold with ``<!--#include
partial="name"-->`` placeholders. ``tools/build_dashboard_assets.py`` stitches
the partials into the static files browsers receive. Frontend contract tests
that assert on rendered dashboard chrome can read that assembled document via
``assembled_dashboard()``.
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
