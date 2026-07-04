"""Test helper: compose the dashboard behaviour script from its JS fragments.

``public/dashboard.js`` is now split into ordered fragment files under
``public/dashboard/js/``; the Worker concatenates them back into a single
``/dashboard.js`` response at request time (see ``src/dashboard_bundle.py``).
Frontend contract tests that assert on the shipped dashboard script should read
the *assembled* bundle — exactly what a browser receives — via
``assembled_dashboard_js()``.
"""

import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
_SRC = _ROOT / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

import dashboard_bundle  # noqa: E402

PUBLIC = _ROOT / "public"


def assembled_dashboard_js() -> str:
    """The fully composed dashboard script (all public/dashboard/js fragments)."""
    return dashboard_bundle.compose_from_reader(
        lambda rel: (PUBLIC / rel).read_text(encoding="utf-8"))
