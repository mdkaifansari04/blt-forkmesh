"""Static URL ownership for the ForkMesh public site.

Clean marketing/auth routes are served by Workers Static Assets from
``public/_redirects``. Repo shortcuts are Worker-owned so hard-refreshing
``/owner/repo`` and tab/tree/blob deep links can serve the dashboard shell
without relying on a client-side 404 bounce. Direct implementation-file URLs
such as ``/login.html`` are intentionally blocked.
"""

from urllib.parse import unquote


REPO_TAB_ROUTES = frozenset({
    "commits",
    "releases",
    "issues",
    "pulls",
    "discussions",
    "mirrors",
    "agents",
})

RESERVED_ROUTE_PREFIXES = frozenset({
    "api",
    "assets",
    "favicon",
    "dashboard",
    "docs",
    "blogs",
    "blog",
    "login",
    "signup",
    "forgot-password",
    "reset-password",
    "mirror-payouts",
    "outreach",
    "security-report",
    "network",
    "chat",
    "desktop",
    "about",
    "features",
    "pricing",
    "press",
    "careers",
    "changelog",
    "privacy",
    "terms",
    "status",
})

BLOCKED_STATIC_HTML_PATHS = frozenset({
    "/index.html",
    "/dashboard.html",
    "/dashboard/index.html",
    "/blog.html",
    "/login.html",
    "/signup.html",
    "/forgot-password.html",
    "/reset-password.html",
    "/mirror-payouts.html",
    "/outreach.html",
    "/security-report.html",
    "/docs.html",
    "/network.html",
    "/chat.html",
    "/desktop.html",
    "/about.html",
    "/features.html",
    "/pricing.html",
    "/press.html",
    "/careers.html",
    "/changelog.html",
    "/privacy.html",
    "/terms.html",
    "/status.html",
})


def path_parts(pathname):
    return [part for part in (pathname or "").split("/") if part]


def looks_like_repo_route(pathname):
    parts = path_parts(pathname)
    if len(parts) < 2:
        return False

    owner = unquote(parts[0]).lower()
    if owner in RESERVED_ROUTE_PREFIXES:
        return False

    if len(parts) == 2:
        return True

    kind = unquote(parts[2])
    return kind in ("tree", "blob") or kind in REPO_TAB_ROUTES
