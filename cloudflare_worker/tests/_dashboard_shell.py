"""Test helper: compose the per-page dashboard documents from HTML partials.

``public/dashboard/shell.html`` is the authored scaffold with ``<!--#include
partial="name"-->`` / ``<!--#page token-->`` placeholders and one view file per
page under ``public/dashboard/partials/views/``. ``tools/build_dashboard_assets.py``
stitches one document per ``dashboard_shell.PAGES`` entry into the static files
browsers receive. Frontend contract tests can read a single page via
``assembled_dashboard_page(page_id)`` or every page concatenated via
``assembled_dashboard()`` (for "marker exists somewhere in the dashboard"
assertions).
"""

import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
_SRC = _ROOT / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

import dashboard_bundle
import dashboard_shell

PUBLIC = _ROOT / "public"


def _read(rel: str) -> str:
    return (PUBLIC / rel).read_text(encoding="utf-8")


def _asset_versions() -> dict:


    return dashboard_shell.asset_versions(
        _read, {"dashboard.js": dashboard_bundle.compose_from_reader(_read)})


def assembled_dashboard_page(page_id: str) -> str:
    """One fully composed page document (shell + chrome partials + its view)."""
    return dashboard_shell.stamp_asset_versions(
        dashboard_shell.compose_page_from_reader(_read, page_id),
        _asset_versions())


def assembled_dashboard() -> str:
    """Every page document concatenated, in PAGES order.

    Kept for the many contract tests that only assert a marker exists somewhere
    in the dashboard chrome or views.
    """
    return "".join(assembled_dashboard_page(page) for page in dashboard_shell.PAGES)
