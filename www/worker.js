const APP_PATHS = new Set([
  "/chat",
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

const WWW_PREFIXES = ["/assets/", "/favicon/", "/blog/", "/docs/"];

function redirect(origin, url, pathname = url.pathname, status = 308) {
  const destination = new URL(origin);
  destination.pathname = pathname;
  destination.search = url.search;
  return Response.redirect(destination, status);
}

function proxyToApp(request, env) {
  const url = new URL(request.url);
  const destination = new URL(env.APP_ORIGIN);
  destination.pathname = url.pathname;
  destination.search = url.search;
  return env.APP.fetch(new Request(destination, request));
}

function isGitProtocol(pathname) {
  return /^\/[^/]+\/[^/]+(?:\.git)?\/(?:info\/refs|git-upload-pack|git-receive-pack|info\/lfs\/)/.test(pathname);
}

function isBackendProtocol(pathname) {
  return pathname.startsWith("/.well-known/") || pathname.startsWith("/nodeinfo/") ||
    pathname.startsWith("/ap/") || pathname.startsWith("/@") ||
    pathname.startsWith("/%40") || pathname === "/rss.xml" ||
    pathname === "/feed.xml" || pathname === "/blog/rss.xml" ||
    pathname === "/blog/feed.xml" || pathname === "/health" ||
    pathname === "/mcp" || pathname.startsWith("/mcp/");
}

async function asset(request, env) {
  const response = await env.ASSETS.fetch(request);
  const headers = new Headers(response.headers);
  headers.set("x-forkmesh-worker", "www");
  headers.set("x-forkmesh-deploy-fingerprint", String(env.DEPLOY_FINGERPRINT || ""));
  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers,
  });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    if (path === "/api") {
      return redirect(env.APP_ORIGIN, url);
    }
    if (path.startsWith("/api/")) {
      return proxyToApp(request, env);
    }
    if (isGitProtocol(path) || isBackendProtocol(path)) {
      return proxyToApp(request, env);
    }
    if (path === "/world" || path.startsWith("/world/")) {
      return redirect(env.WORLD_ORIGIN, url, path === "/world" ? "/" : path);
    }
    if (path === "/dashboard" || path.startsWith("/dashboard/") ||
        path.startsWith("/notes/") || APP_PATHS.has(path)) {
      return redirect(env.APP_ORIGIN, url);
    }
    if (request.method !== "GET" && request.method !== "HEAD") {
      return proxyToApp(request, env);
    }
    if (/^\/[^/]+\/[^/]+(?:\/.*)?$/.test(path) &&
        !WWW_PREFIXES.some((prefix) => path.startsWith(prefix))) {
      return redirect(env.APP_ORIGIN, url);
    }
    return asset(request, env);
  },
};
