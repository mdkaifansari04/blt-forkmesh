#!/usr/bin/env python3
"""BLT signup must not mount the ForkMesh World band."""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
SIGNUP_HTML = (PUBLIC / "signup.html").read_text(encoding="utf-8")


def test_signup_page_does_not_mount_the_world_band():
    assert "data-forkmesh-world" not in SIGNUP_HTML
    assert "site-footer.js" not in SIGNUP_HTML
    assert "Join BLT" in SIGNUP_HTML
