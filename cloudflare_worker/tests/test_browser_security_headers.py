"""Static browser responses carry a deliberate, enforced security policy."""

from pathlib import Path


HEADERS = (
    Path(__file__).resolve().parents[1] / "public" / "_headers"
).read_text(encoding="utf-8")


def test_global_static_security_headers_are_enforced():
    assert HEADERS.startswith("/*\n")
    for header in (
        "Content-Security-Policy:",
        "Strict-Transport-Security:",
        "Referrer-Policy:",
        "Permissions-Policy:",
        "X-Content-Type-Options: nosniff",
        "X-Frame-Options: DENY",
        "Cross-Origin-Opener-Policy: same-origin",
    ):
        assert header in HEADERS
    assert "Content-Security-Policy-Report-Only" not in HEADERS
    assert "frame-ancestors 'none'" in HEADERS
    assert "object-src 'none'" in HEADERS
    assert "base-uri 'self'" in HEADERS


def test_policy_does_not_grant_sensitive_device_capabilities():
    permissions = next(
        line for line in HEADERS.splitlines()
        if "Permissions-Policy:" in line
    )
    for blocked in (
        "camera=()",
        "geolocation=()",
        "payment=()",
        "usb=()",
        "serial=()",
        "bluetooth=()",
        "browsing-topics=()",
    ):
        assert blocked in permissions
    # Speech is an explicit, same-origin opt-in feature.
    assert "microphone=(self)" in permissions
