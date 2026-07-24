#!/usr/bin/env python3
"""Single-Worker World and marketing deployment contracts."""

import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parent
LEGACY_MARKETING = PROJECT / "cloudflare_marketing_worker"
WRANGLER = tomllib.loads(
    (ROOT / "wrangler.toml").read_text(encoding="utf-8"))
DEPLOY = (ROOT / "deploy.sh").read_text(encoding="utf-8")
REDIRECTS = (ROOT / "public" / "_redirects").read_text(encoding="utf-8")
HEADERS = (ROOT / "public" / "_headers").read_text(encoding="utf-8")


def test_one_worker_owns_world_marketing_assets_and_api():
    assert WRANGLER["name"] == "forkmesh-relay"
    assert WRANGLER["assets"]["directory"] == "./public"
    assert "/" in WRANGLER["assets"]["run_worker_first"]
    assert "routes" not in WRANGLER
    assert not LEGACY_MARKETING.exists()


def test_clean_marketing_urls_are_backed_by_canonical_main_assets():
    for asset in ("index.html", "world/index.html", "pricing.html", "blog.html"):
        assert (ROOT / "public" / asset).is_file()
    posts = sorted((ROOT / "public" / "blog").glob("*/index.html"))
    assert posts
    assert "/ /index.html 200" in REDIRECTS
    assert "/pricing /pricing.html 200" in REDIRECTS
    assert "/blog /blog.html 200" in REDIRECTS
    assert "/blog/:slug/ /blog/:slug/index.html 200" in REDIRECTS


def test_main_deploy_verifies_world_pricing_blog_and_post_parity():
    assert "verify_deploy \"$BUILD_REV\"" in DEPLOY
    assert "verify_public_assets" in DEPLOY
    assert "retire_legacy_marketing_worker" in DEPLOY
    assert "verify_marketing_routes" in DEPLOY
    deploy_tail = DEPLOY.split('case "${1:-deploy}" in', 1)[1]
    assert (
        deploy_tail.index("verify_deploy")
        < deploy_tail.index("verify_public_assets")
        < deploy_tail.index("retire_legacy_marketing_worker")
        < deploy_tail.index("verify_marketing_routes")
    )
    assert "pywrangler delete --name forkmesh-marketing --force" in DEPLOY
    for path in (
        '"/|ForkMesh - Local-first source code preservation"',
        '"/pricing|ForkMesh Pricing - Coding Reimagined for Teams"',
        '"/blog|Blog · ForkMesh"',
        '"/blog/introducing-forkmesh/|Introducing ForkMesh"',
    ):
        assert path in DEPLOY


def test_world_refreshes_cannot_reuse_an_older_application_graph():
    assert "/world\n  Cache-Control: no-store, max-age=0, must-revalidate" in HEADERS
    assert "/world/*\n  Cache-Control: no-store, max-age=0, must-revalidate" in HEADERS
    for asset in (
        "world.js",
        "world-data.js",
        "world-scene.js",
        "world-mirror-nodes.js",
        "world-pull-review.js",
        "world-repository-graph.js",
        "world-speech.js",
        "world.css",
        "world-speech.css",
    ):
        assert f"public/world/{asset}" in DEPLOY
    assert "remote_hash" in DEPLOY
    assert "World runtime assets exactly match" in DEPLOY
    assert "World refresh freshness verification failed" in DEPLOY


def test_main_deploy_is_the_only_cloudflare_site_deploy_entrypoint():
    assert "pywrangler deploy --env \"\"" in DEPLOY
    assert "website + relay" in DEPLOY
