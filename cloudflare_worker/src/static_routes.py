"""Static URL ownership for the ForkMesh public site.

Clean marketing/auth routes are served by Workers Static Assets from
``public/_redirects``. Repo shortcuts are Worker-owned so hard-refreshing
``/owner/repo`` and tab/tree/blob deep links can serve the repo-detail page
without relying on a client-side 404 bounce. Direct implementation-file URLs
such as ``/login.html`` are intentionally blocked.

The dashboard is true separate pages (one built document per page, see
``dashboard_shell.PAGES``): the non-home pages are asset-served via
``_redirects`` + ``run_worker_first`` exceptions; the Worker owns ``/dashboard``
itself (it must see the query string to 308 legacy ``?section=`` URLs, which
``_redirects`` cannot match) plus everything unrecognized under ``/dashboard/``.
"""

from urllib.parse import parse_qs, unquote, urlencode


REPO_TAB_ROUTES = frozenset({
    "commits",
    "releases",
    "issues",
    "projects",
    "pulls",
    "discussions",
    "mirrors",
    "agents",
    "insights",
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
    "world",
    "chat",
    "desktop",
    "about",
    "features",
    "homev2",
    "pricing",
    "press",
    "careers",
    "changelog",
    "privacy",
    "terms",
    "status",
    "referrals",
    "leaderboards",
    # Referral share links (/r/<name>) are worker-owned counters.
    "r",
})

# Clean page URL -> the built per-page document that serves it. The Worker
# only consults this for /dashboard itself and as a fallback for deep links;
# the non-home pages are normally asset-served straight from _redirects.
DASHBOARD_PAGE_ASSETS = {
    "/dashboard": "dashboard/index.html",
    "/dashboard/repos": "dashboard/repos/index.html",
    "/dashboard/network": "dashboard/network/index.html",
    "/dashboard/chat": "dashboard/chat/index.html",
    "/dashboard/settings": "dashboard/settings/index.html",
    "/dashboard/profile": "dashboard/profile/index.html",
    "/dashboard/profile/repositories": "dashboard/profile/repositories/index.html",
}

# The repo-detail document, fetched by the Worker for every /owner/repo[...]
# route. Deliberately not a navigable asset path of its own.
DASHBOARD_REPO_ASSET = "dashboard/repo.html"

# Legacy /dashboard?section=X names -> the clean per-page paths. 308s are
# browser-cached permanently, so this mapping is a one-way door — keep it in
# sync with dashboard_shell.PAGES routes.
DASHBOARD_SECTION_PATHS = {
    "home": "/dashboard",
    "repos": "/dashboard/repos",
    "network": "/dashboard/network",
    "chat": "/dashboard/chat",
    "profile": "/dashboard/settings",
    "profile-overview": "/dashboard/profile",
    "profile-repositories": "/dashboard/profile/repositories",
}


def dashboard_section_redirect(path, query):
    """308 target for legacy ``/dashboard?section=X`` URLs, else ``None``.

    Leftover query params are preserved — ``?repo=``, ``?mock=`` and the
    desktop app's ``?link_node=...`` grant params are live features.
    """
    if path != "/dashboard" or not query:
        return None
    params = parse_qs(query, keep_blank_values=True)
    section = (params.get("section") or [""])[0]
    target = DASHBOARD_SECTION_PATHS.get(section)
    if not target:
        return None
    rest = [(key, value) for key, values in params.items() if key != "section" for value in values]
    return target + ("?" + urlencode(rest) if rest else "")


BLOCKED_STATIC_HTML_PATHS = frozenset({
    "/index.html",
    "/world/index.html",
    "/dashboard.html",
    "/dashboard/index.html",
    "/dashboard/repos/index.html",
    "/dashboard/network/index.html",
    "/dashboard/chat/index.html",
    "/dashboard/settings/index.html",
    "/dashboard/profile/index.html",
    "/dashboard/profile/repositories/index.html",
    "/dashboard/repo.html",
    "/blog.html",
    "/login.html",
    "/signup.html",
    "/forgot-password.html",
    "/reset-password.html",
    "/mirror-payouts.html",
    "/referrals.html",
    "/leaderboards.html",
    "/outreach.html",
    "/security-report.html",
    "/docs.html",
    "/network.html",
    "/chat.html",
    "/desktop.html",
    "/about.html",
    "/features.html",
    "/homev2.html",
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
