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
    dashboard_js = dashboard_bundle.compose_from_reader(_read)
    # Content-hash every cache-busted bundle into its <script> ?v= query so a
    # changed deploy always serves fresh JS (see dashboard_shell). Authored
    # bundles are read off disk; dashboard.js is composed here, so hand it over
    # in-memory rather than re-reading it.
    versions = dashboard_shell.asset_versions(
        _read, {"dashboard.js": dashboard_js})
    outputs = {
        meta["asset"]: dashboard_shell.stamp_asset_versions(
            dashboard_shell.compose_page_from_reader(_read, page_id), versions)
        for page_id, meta in dashboard_shell.PAGES.items()
    }
    outputs["dashboard.js"] = dashboard_js
    changed = [rel for rel, text in sorted(outputs.items()) if _write_if_changed(rel, text)]
    # The pre-split SPA duplicate: /dashboard.html routes are gone, the per-page
    # documents replace it. Drop a stale copy left by older builds.
    legacy = PUBLIC / "dashboard.html"
    if legacy.exists():
        legacy.unlink()
        changed.append("dashboard.html (removed)")
    if changed:
        print("Built dashboard assets: " + ", ".join(changed))
    else:
        print("Dashboard assets are up to date.")


if __name__ == "__main__":
    main()
