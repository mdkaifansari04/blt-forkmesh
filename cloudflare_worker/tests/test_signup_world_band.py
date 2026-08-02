#!/usr/bin/env python3
"""The signup page ends with the ForkMesh World band (adhoc #397).

Referral links land on /signup?ref=<name>, so that page — which renders no
site footer — mounts the World band on its own via [data-forkmesh-world].
"""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
SIGNUP_HTML = (PUBLIC / "signup.html").read_text(encoding="utf-8")
FOOTER_JS = (PUBLIC / "site-footer.js").read_text(encoding="utf-8")
FOOTER_CSS = (PUBLIC / "site-footer.css").read_text(encoding="utf-8")


def test_signup_page_mounts_the_world_band():
    assert "<div data-forkmesh-world></div>" in SIGNUP_HTML
    assert 'href="/site-footer.css"' in SIGNUP_HTML
    assert 'src="/site-footer.js"' in SIGNUP_HTML
    # The band only, not the whole footer with its link columns.
    assert "data-forkmesh-footer" not in SIGNUP_HTML


def test_footer_script_mounts_standalone_world_bands():
    assert "[data-forkmesh-world]" in FOOTER_JS
    assert "mountWorldBands" in FOOTER_JS
    assert "site-footer-world-standalone" in FOOTER_JS
    assert 'src="/world/"' in FOOTER_JS


def test_standalone_band_carries_the_footer_palette():
    # Outside .forkmesh-footer the --fm-footer-* variables are undefined, so
    # the standalone band declares them (dark) plus a light-theme override.
    assert ".site-footer-world-standalone {" in FOOTER_CSS
    assert "html.light .site-footer-world-standalone {" in FOOTER_CSS
    for var in ("--fm-footer-panel", "--fm-footer-fg", "--fm-footer-muted",
                "--fm-footer-border"):
        assert FOOTER_CSS.count(var + ":") >= 3
