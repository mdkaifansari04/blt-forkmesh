"""Static and Worker-rendered HTML carry the same enforced browser policy."""

import ast
from pathlib import Path
import re


HEADERS = (
    Path(__file__).resolve().parents[1] / "public" / "_headers"
).read_text(encoding="utf-8")
ENTRY_PATH = Path(__file__).resolve().parents[1] / "src" / "entry.py"
ENTRY = ENTRY_PATH.read_text(encoding="utf-8")


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


def test_only_embedded_application_documents_allow_same_origin_frames():
    global_rule = _rule("/*")
    assert "frame-ancestors 'none'" in global_rule
    assert "X-Frame-Options: DENY" in global_rule
    assert "https://static.cloudflareinsights.com" in global_rule

    # Chat renders inside World frames; issue and pull details render inside
    # the World workbench; World renders inside the site-footer band. All stay
    # same-origin only.
    for path in (
        "/dashboard/chat*",
        "/chat",
        "/chat.html",
        "/:owner/:repo/issues/:number",
        "/:owner/:repo/pulls/:number",
        "/world",
        "/world/*",
    ):
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

    assert HEADERS.count("frame-ancestors 'self'") == 7
    assert HEADERS.count("X-Frame-Options: SAMEORIGIN") == 7
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


def test_dynamic_app_html_receives_the_same_security_boundary():
    wanted = {
        "_BROWSER_CSP",
        "_BROWSER_SECURITY_HEADERS",
        "_apply_browser_security_headers",
    }
    nodes = []
    for node in ast.parse(ENTRY, filename=str(ENTRY_PATH)).body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id in wanted
            for target in node.targets
        ):
            nodes.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name in wanted:
            nodes.append(node)
    namespace = {"re": re}
    exec(
        compile(
            ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[])),
            str(ENTRY_PATH),
            "exec",
        ),
        namespace,
    )

    class HeaderMap(dict):
        def get(self, name, default=None):
            return super().get(name.lower(), default)

        def set(self, name, value):
            self[name.lower()] = value

    class DynamicResponse:
        def __init__(self):
            self.headers = HeaderMap({"content-type": "text/html; charset=utf-8"})

    ordinary = namespace["_apply_browser_security_headers"](
        DynamicResponse(), "/dashboard"
    )
    assert ordinary.headers["x-frame-options"] == "DENY"
    assert "frame-ancestors 'none'" in ordinary.headers[
        "content-security-policy"
    ]
    for header in (
        "strict-transport-security",
        "referrer-policy",
        "permissions-policy",
        "x-content-type-options",
        "cross-origin-opener-policy",
    ):
        assert ordinary.headers[header]

    embedded = namespace["_apply_browser_security_headers"](
        DynamicResponse(), "/alice/repo/issues/7"
    )
    assert embedded.headers["x-frame-options"] == "SAMEORIGIN"
    assert "frame-ancestors 'self'" in embedded.headers[
        "content-security-policy"
    ]
    assert "_apply_browser_security_headers(" in ENTRY[
        ENTRY.index("async def fetch(self, request):"):
        ENTRY.index("async def _admin(self, request):")
    ]
