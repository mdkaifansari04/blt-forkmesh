// ForkMesh marketing Worker: serves forkmesh.com/ (the landing page) and
// nothing else. Every other path is routed to the relay Worker
// (../cloudflare_worker) by Cloudflare route precedence — see wrangler.toml.
//
// The / semantics deliberately mirror the relay Worker's homepage handler
// (entry.py _route "/" branch + _serve_homepage), which stays in place as the
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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Only forkmesh.com/ is routed here in production; any other path means a
    // workers.dev / wrangler-dev hit, where no route splitting exists. Bounce
    // it to the public origin so the relay Worker (which owns every non-root
    // path, including this page's own CSS/JS subresources) can serve it.
    if (url.pathname !== "/") {
      return Response.redirect(
        "https://forkmesh.com" + url.pathname + url.search, 302);
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("method not allowed", {
        status: 405,
        headers: { allow: "GET, HEAD" },
      });
    }

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
