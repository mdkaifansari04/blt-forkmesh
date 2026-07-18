// ForkMesh marketing Worker: serves the public marketing pages —
// forkmesh.com/ (the landing page), /pricing, and the /blog index plus every
// blog post — and nothing else. Every other path is routed to the relay Worker
// (../cloudflare_worker) by Cloudflare route precedence — see wrangler.toml.
//
// The / semantics deliberately mirror the relay Worker's homepage handler
// (entry.py _route "/" branch + _serve_homepage), and /pricing + /blog mirror
// its static-asset serving; the relay keeps all of them in place as the
// fallback for workers.dev previews and local dev. Keep the two in sync;
// cloudflare_worker/tests/test_marketing_worker.py pins the contract.

// Same parsing as the relay's _cookie_value: first "name=value" pair wins,
// strict equality with "1". The cookie is a presence hint set by login/signup
// JS (never a credential); the real session token gates privileged APIs.
function cookieValue(request, name) {
  const header = request.headers.get("cookie") || "";
  for (const part of header.split(";")) {
    const trimmed = part.trim();
    const eq = trimmed.indexOf("=");
    if (eq > 0 && trimmed.slice(0, eq) === name) {
      return trimmed.slice(eq + 1);
    }
  }
  return "";
}

// no-cache only forces revalidation (the platform still ETags the asset body);
// a browser-cached logged-out homepage must never mask the login redirect.
const PAGE_HEADERS = {
  "content-type": "text/html; charset=utf-8",
  "cache-control": "no-cache",
  "vary": "cookie",
  "x-forkmesh-worker": "marketing",
};

// /pricing and /blog are plain public pages (no session redirect). They vary on
// nothing, but still revalidate so a rebuilt page can't stay stale, and stamp
// the same attribution header the deploy check and relay contract look for.
const STATIC_PAGE_HEADERS = {
  "content-type": "text/html; charset=utf-8",
  "cache-control": "no-cache",
  "x-forkmesh-worker": "marketing",
};

// Map a clean marketing URL to the asset document that backs it. Returns null
// for anything this Worker does not own. Mirrors the relay's _redirects rules:
// /pricing -> /pricing.html, /blog -> /blog.html, and each blog post directory
// index /blog/<slug>/ -> /blog/<slug>/index.html (html_handling is "none", so
// the index.html must be named explicitly). Slugs are a single [a-z0-9-] segment
// exactly as the published posts are, which also blocks any path traversal.
function marketingAssetPath(pathname) {
  if (pathname === "/pricing") return "/pricing.html";
  if (pathname === "/blog" || pathname === "/blog/") return "/blog.html";
  if (pathname.startsWith("/blog/")) {
    const slug = pathname.slice("/blog/".length).replace(/\/$/, "");
    if (/^[a-z0-9-]+$/.test(slug)) return `/blog/${slug}/index.html`;
  }
  return null;
}

function bounceToRelay(url) {
  // workers.dev / wrangler-dev has no route splitting, and the relay still
  // holds the canonical copy of every page — send unmatched paths there.
  return Response.redirect(
    "https://forkmesh.com" + url.pathname + url.search, 302);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    const assetPath = path === "/" ? null : marketingAssetPath(path);
    // Not a page this Worker owns: bounce to the relay (only reachable off the
    // production routes, e.g. on a workers.dev preview).
    if (path !== "/" && assetPath === null) {
      return bounceToRelay(url);
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("method not allowed", {
        status: 405,
        headers: { allow: "GET, HEAD" },
      });
    }

    // /pricing and the blog: stream the backing document straight through.
    if (assetPath !== null) {
      let asset = null;
      try {
        asset = await env.ASSETS.fetch(new URL(assetPath, url.origin));
      } catch (_err) {
        asset = null;
      }
      if (!asset || !asset.ok) {
        return bounceToRelay(url);
      }
      return new Response(asset.body, {
        status: 200,
        headers: STATIC_PAGE_HEADERS,
      });
    }

    // Root landing page below (path === "/").
    // Logged-in visitors go straight to the dashboard: a server-side 302 keyed
    // off the forkmesh_session presence cookie. no-store so a logout never
    // replays a cached redirect.
    if (cookieValue(request, "forkmesh_session") === "1") {
      return new Response(null, {
        status: 302,
        headers: {
          "location": "/dashboard",
          "cache-control": "no-store, max-age=0, must-revalidate",
          "x-forkmesh-worker": "marketing",
        },
      });
    }

    // Stream the asset body straight through — only the headers are rewritten.
    let asset = null;
    try {
      asset = await env.ASSETS.fetch(new URL("/index.html", url.origin));
    } catch (_err) {
      asset = null;
    }
    if (!asset || !asset.ok) {
      return new Response("<!doctype html><title>ForkMesh</title>", {
        status: 200,
        headers: PAGE_HEADERS,
      });
    }
    return new Response(asset.body, { status: 200, headers: PAGE_HEADERS });
  },
};
