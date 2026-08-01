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

import dashboard_bundle
import dashboard_shell
import build_worker_footprint


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


def site_page_assets():
    """Authored (non-dashboard) HTML documents, ``public/``-relative."""
    return dashboard_shell.stamped_site_pages(
        p.relative_to(PUBLIC).as_posix() for p in PUBLIC.rglob("*.html"))


def main():
    dashboard_js = dashboard_bundle.compose_from_reader(_read)




    versions = dashboard_shell.asset_versions(
        _read, {"dashboard.js": dashboard_js})
    outputs = {
        meta["asset"]: dashboard_shell.stamp_asset_versions(
            dashboard_shell.compose_page_from_reader(_read, page_id), versions)
        for page_id, meta in dashboard_shell.PAGES.items()
    }


    for rel in site_page_assets():
        outputs[rel] = dashboard_shell.stamp_asset_versions(_read(rel), versions)
    outputs["dashboard.js"] = dashboard_js
    changed = [rel for rel, text in sorted(outputs.items()) if _write_if_changed(rel, text)]
    if build_worker_footprint.build():
        changed.append("world/worker-footprint.js")


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
