"""Static browser responses carry a deliberate, enforced security policy."""

from pathlib import Path


HEADERS = (
    Path(__file__).resolve().parents[1] / "public" / "_headers"
).read_text(encoding="utf-8")


def _rule(path):
    marker = f"{path}\n"
    start = HEADERS.index(marker)
    lines = []
    for line in HEADERS[start + len(marker):].splitlines():
        if line and not line[0].isspace() and not line.startswith("#"):
            break
        if line.startswith((" ", "\t")):
            lines.append(line.strip())
    return "\n".join(lines)


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


def test_only_chat_and_world_documents_allow_same_origin_frames():
    global_rule = _rule("/*")
    assert "frame-ancestors 'none'" in global_rule
    assert "X-Frame-Options: DENY" in global_rule
    assert "https://static.cloudflareinsights.com" in global_rule

    # Chat renders inside World frames; World renders inside the site-footer
    # band on every page. Both stay same-origin only.
    for path in ("/dashboard/chat*", "/chat", "/chat.html", "/world", "/world/*"):
        rule = _rule(path)
        assert "! Content-Security-Policy" in rule
        assert "frame-ancestors 'self'" in rule
        assert "frame-ancestors 'none'" not in rule
        assert "! X-Frame-Options" in rule
        assert "X-Frame-Options: SAMEORIGIN" in rule
        assert "X-Frame-Options: DENY" not in rule
        assert "https://cdn.jsdelivr.net" in rule
        assert "https://static.cloudflareinsights.com" in rule

    # The World application shell must never be cached across deployments.
    for path in ("/world", "/world/*"):
        assert "Cache-Control: no-store, max-age=0, must-revalidate" in _rule(path)

    assert HEADERS.count("frame-ancestors 'self'") == 5
    assert HEADERS.count("X-Frame-Options: SAMEORIGIN") == 5
    assert "https://cdn.tailwindcss.com" not in HEADERS


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
        "browsing-topics=()",
    ):
        assert blocked in permissions
    # Speech is an explicit, same-origin opt-in feature.
    assert "microphone=(self)" in permissions
