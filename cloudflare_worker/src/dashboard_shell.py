"""Composition of the per-page dashboard documents from HTML partials.

The dashboard used to be one SPA shell: every ``/dashboard*`` URL served the
same ``dashboard/index.html`` with all views stacked as hidden ``[data-view]``
sections, and dashboard.js swapped them client-side after boot (a visible
flash-then-swap on deep links). It is now a set of true separate pages: one
document per page, each containing the shared chrome (header, sidebar, modals)
plus exactly one view from ``public/dashboard/partials/views/``.

``dashboard/shell.html`` keeps the ``<head>`` and outer body scaffold with

    <!--#include partial="name"-->      pulls in a shared partial
    <!--#include view-->                (inside the "main" partial) pulls in
                                        the page's single view file
    <!--#page token-->                  per-page metadata substitution
                                        (id, title, description, section)

``tools/build_dashboard_assets.py`` composes one document per ``PAGES`` entry
before deploy so Cloudflare can serve them as static assets.

This module is intentionally js-free — no ``js``/``workers`` imports — so both
the Worker (on Cloudflare) and the test suite can import it directly, the same
way they import ``urls.py``. The composition is a pure string substitution, so
it is trivially unit-testable against the on-disk partials.
"""

import hashlib
import re

# Placeholder the shell uses to pull in a partial: <!--#include partial="name"-->
INCLUDE_RE = re.compile(r'<!--#include partial="([a-z0-9-]+)"-->')

# Placeholder inside the "main" partial for the page's single view.
VIEW_INCLUDE_RE = re.compile(r"<!--#include view-->")

# Per-page metadata tokens in the shell: <!--#page id-->, <!--#page title-->, ...
PAGE_TOKEN_RE = re.compile(r"<!--#page ([a-z]+)-->")

# Root-relative first-party <script src="/name.js"> tags, with an optional
# existing ``?v=`` cache-busting query. Used by ``stamp_asset_versions`` to
# rewrite the version to a per-build content hash so a new bundle is never
# served from a stale edge/browser cache (the old static ``?v=public-profiles``
# query never changed, so week-old dashboard.js kept being served until a cache
# happened to expire — a hard refresh masked it).
ASSET_SCRIPT_RE = re.compile(r'(src="/([\w.-]+\.js))(?:\?v=[^"]*)?"')

# The dashboard's pages. Keys are page ids (also the <body data-page> value the
# JS boot dispatch keys off). "view" names the partials/views/<view>.html file;
# "section" is the legacy data-dashboard-section name (CSS hooks + old JS state
# names); "nav" is the sidebar data-nav link to mark active (None = no sidebar
# item); "route" is the public clean URL (None = worker-only, the repo detail
# document is fetched by the Worker for every /owner/repo[...] path); "asset"
# is the built public/-relative file.
PAGES = {
    "home": {
        "view": "home",
        "section": "home",
        "nav": "home",
        "route": "/dashboard",
        "asset": "dashboard/index.html",
        "title": "ForkMesh Dashboard",
        "description": "Manage ForkMesh repositories, mirrors, signed collaboration, and account activity from your dashboard.",
    },
    "repos": {
        "view": "repos",
        "section": "repos",
        "nav": "repos",
        "route": "/dashboard/repos",
        "asset": "dashboard/repos/index.html",
        "title": "Repositories - ForkMesh",
        "description": "Browse repositories mirrored by ForkMesh desktop nodes and open them live from their hosts.",
    },
    "network": {
        "view": "network",
        "section": "network",
        "nav": "network",
        "route": "/dashboard/network",
        "asset": "dashboard/network/index.html",
        "title": "Network - ForkMesh",
        "description": "Relay health, connected nodes, and live network activity on ForkMesh.",
    },
    "chat": {
        "view": "chat",
        "section": "chat",
        "nav": "chat",
        "route": "/dashboard/chat",
        "asset": "dashboard/chat/index.html",
        "title": "Chat - ForkMesh",
        "description": "ForkMesh #general chat uses a relay-derived authenticated shared key; the relay can decrypt messages.",
    },
    "settings": {
        "view": "settings",
        "section": "profile",
        "nav": None,
        "route": "/dashboard/settings",
        "asset": "dashboard/settings/index.html",
        "title": "Settings - ForkMesh",
        "description": "Manage your ForkMesh account: public profile, appearance, notifications, payout, and linked nodes.",
    },
    "profile": {
        "view": "profile-overview",
        "section": "profile-overview",
        "nav": "profile",
        "route": "/dashboard/profile",
        "asset": "dashboard/profile/index.html",
        "title": "Profile - ForkMesh",
        "description": "Your public ForkMesh profile overview and contribution activity.",
    },
    "profile-repositories": {
        "view": "profile-repositories",
        "section": "profile-repositories",
        "nav": "profile",
        "route": "/dashboard/profile/repositories",
        "asset": "dashboard/profile/repositories/index.html",
        "title": "Your repositories - ForkMesh",
        "description": "Repositories on your public ForkMesh profile.",
    },
    "repo": {
        "view": "repo",
        "section": "explore",
        "nav": "repos",
        "route": None,
        "asset": "dashboard/repo.html",
        "title": "Repository - ForkMesh",
        "description": "Inspect a repository mirrored by ForkMesh desktop nodes: files, issues, pulls, and mirrors.",
    },
}


def partial_path(name):
    """public/-relative path to a partial by its include name."""
    return "dashboard/partials/%s.html" % name


def view_path(name):
    """public/-relative path to a per-page view partial."""
    return "dashboard/partials/views/%s.html" % name


def included_partials(shell):
    """Ordered, de-duplicated list of partial names the shell pulls in."""
    seen = []
    for name in INCLUDE_RE.findall(shell):
        if name not in seen:
            seen.append(name)
    return seen


def assemble_shell(shell, partials):
    """Substitute each ``<!--#include-->`` with ``partials[name]``.

    ``partials`` maps a partial name to its raw HTML. A missing name raises
    ``KeyError`` rather than silently leaving the placeholder in the page, so a
    composition bug surfaces loudly instead of shipping a broken shell.
    """
    return INCLUDE_RE.sub(lambda m: partials[m.group(1)], shell)


def mark_active_nav(html, page_id):
    """Bake the active sidebar link for ``page_id`` into the composed page.

    Zero JS and zero flash: the right link carries ``aria-current="page"`` and
    active text color at parse time. Pages without a sidebar item (settings)
    are returned unchanged.
    """
    nav = PAGES[page_id].get("nav")
    if not nav:
        return html
    tag_re = re.compile(r'<a data-nav="%s"[^>]*>' % re.escape(nav))

    def _activate(match):
        tag = match.group(0)
        tag = tag.replace("data-nav-link", 'data-nav-link aria-current="page"', 1)
        return tag.replace("text-muted-foreground", "text-foreground", 1)

    return tag_re.sub(_activate, html)


def content_version(text):
    """Short, deterministic content fingerprint for a client bundle.

    Only the bytes matter (not the deploy time), so an unchanged bundle keeps
    the same ``?v=`` and warm caches survive redeploys; any change rotates it.
    """
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]


# Single source of truth for cache-busting: the first-party client bundles that
# receive a content-hash ``?v=`` query. ``dashboard.js`` is composed in memory
# at build time; the rest are authored files served straight from ``public/``.
# Adding a bundle is one edit here (plus its ``<script>`` tag) — the build tool,
# the stamper, and the frontend tests all derive from this list.
CACHE_BUSTED_BUNDLES = ("dashboard.js", "dashboard-chat.js")


def asset_versions(read, composed=None):
    """Content-hash ``?v=`` map for every :data:`CACHE_BUSTED_BUNDLES` entry.

    ``read(public_relative_path) -> str`` loads authored bundles off disk;
    ``composed`` optionally supplies the text of bundles the caller built in
    memory (e.g. ``dashboard.js``) so they are hashed without a disk round-trip.
    Keyed by served filename. Shared by the build tool and the frontend tests so
    the ``?v=`` stamped into the built documents is reproduced identically.
    """
    composed = composed or {}
    return {
        name: content_version(composed[name] if name in composed else read(name))
        for name in CACHE_BUSTED_BUNDLES
    }


def stamp_asset_versions(html, versions):
    """Rewrite first-party ``<script src="/x.js">`` tags to carry ``?v=<hash>``.

    ``versions`` maps a bundle filename (``dashboard.js``) to a per-build
    content hash. Only listed assets are touched; any existing ``?v=`` query is
    replaced, and a version is added where none was present. Third-party scripts
    (posthog, tailwind, lucide) and unlisted assets pass through unchanged.

    This is the deploy-time cache-buster: because the query string tracks the
    bundle's content, every changed deploy yields a fresh URL, so no edge or
    browser cache can serve the previous build's JS against the new HTML.
    """
    def _stamp(match):
        base, name = match.group(1), match.group(2)
        version = versions.get(name)
        if not version:
            return match.group(0)
        return '%s?v=%s"' % (base, version)

    return ASSET_SCRIPT_RE.sub(_stamp, html)


def compose_page_from_reader(read, page_id):
    """Assemble one page document using ``read(public_relative_path) -> str``.

    Shared by the build tool and the test suite (``read`` loads files off
    disk). Reads the shell, substitutes the page tokens, pulls in each shared
    partial, injects the page's single view, and marks the active sidebar link.
    A bad page id or token raises ``KeyError`` loudly.
    """
    meta = PAGES[page_id]
    tokens = {
        "id": page_id,
        "title": meta["title"],
        "description": meta["description"],
        "section": meta["section"],
    }
    shell = PAGE_TOKEN_RE.sub(lambda m: tokens[m.group(1)], read("dashboard/shell.html"))
    partials = {name: read(partial_path(name)) for name in included_partials(shell)}
    partials["main"] = VIEW_INCLUDE_RE.sub(
        lambda m: read(view_path(meta["view"])), partials["main"])
    return mark_active_nav(assemble_shell(shell, partials), page_id)
