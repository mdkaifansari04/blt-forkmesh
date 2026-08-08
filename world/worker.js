const APP_PATHS = new Set([
  "/chat",
  "/dashboard",
  "/forgot-password",
  "/leaderboards",
  "/login",
  "/mirror-payouts",
  "/network",
  "/referrals",
  "/reset-password",
  "/signup",
  "/status",
]);

const WWW_PATHS = new Set([
  "/about",
  "/blog",
  "/careers",
  "/changelog",
  "/desktop",
  "/docs",
  "/features",
  "/outreach",
  "/press",
  "/pricing",
  "/privacy",
  "/security-report",
  "/terms",
]);

function redirect(origin, url, pathname = url.pathname, status = 308) {
  const destination = new URL(origin);
  destination.pathname = pathname;
  destination.search = url.search;
  return Response.redirect(destination, status);
}

async function asset(request, env, pathname) {
  const url = new URL(request.url);
  const assetPath = pathname || url.pathname;
  url.pathname = assetPath;
  const response = await env.ASSETS.fetch(new Request(url, request));
  const headers = new Headers(response.headers);
  headers.set("x-forkmesh-worker", "world");
  headers.set("x-forkmesh-deploy-fingerprint", String(env.DEPLOY_FINGERPRINT || ""));
  const result = new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers,
  });
  if (assetPath === "/world/index.html" && response.ok) {
    const origin = String(env.APP_ORIGIN || "").replace(/[&<>"']/g, (character) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;",
    })[character]);
    return new HTMLRewriter().on("head", {
      element(element) {
        element.prepend(`<meta name="forkmesh-api-origin" content="${origin}">`, { html: true });
      },
    }).transform(result);
  }
  return result;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    if (path === "/api" || path.startsWith("/api/")) {
      return redirect(env.APP_ORIGIN, url);
    }
    if (path === "/" || path === "/world" || path === "/world/") {
      return asset(request, env, "/world/index.html");
    }
    if (path === "/dashboard" || path.startsWith("/dashboard/") ||
        path.startsWith("/notes/") || APP_PATHS.has(path)) {
      return redirect(env.APP_ORIGIN, url);
    }
    if (WWW_PATHS.has(path) || path.startsWith("/blog/") || path.startsWith("/docs/")) {
      return redirect(env.WWW_ORIGIN, url);
    }
    return asset(request, env);
  },
};
