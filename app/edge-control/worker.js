const CANONICAL_ORIGINS = [
  "https://forkmesh.com",
  "https://www.forkmesh.com",
  "https://app.forkmesh.com",
  "https://api.forkmesh.com",
  "https://world.forkmesh.com",
];

const ORIGIN_VARS = [
  "PUBLIC_BASE_URL",
  "API_ORIGIN",
  "APP_ORIGIN",
  "WWW_ORIGIN",
  "WORLD_ORIGIN",
];

const CORS_METHODS = ["GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"];
const DEFAULT_HEADERS = ["accept", "authorization", "content-type"];
const EXPOSE_HEADERS = [
  "etag",
  "link",
  "location",
  "retry-after",
  "x-forkmesh-worker",
  "x-request-id",
  "x-ratelimit-limit",
  "x-ratelimit-remaining",
  "x-ratelimit-reset",
];
const HEADER_TOKEN = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/;
const RUNNER_NAME = "scheduled-runner-v1";
const KICK_URL = "https://forkmesh.internal/cron-runner/kick";
const MIRROR_IDENTITY_PATH = "/api/mirrors/https";

function enabled(value) {
  return ["1", "true", "yes", "on"].includes(String(value || "").trim().toLowerCase());
}

function normalizedOrigin(value) {
  let url;
  try {
    url = new URL(String(value || "").trim());
  } catch {
    return "";
  }
  if (!["http:", "https:"].includes(url.protocol) || !url.hostname ||
      url.username || url.password || !["", "/"].includes(url.pathname) ||
      url.search || url.hash) {
    return "";
  }
  const hostname = url.hostname.toLowerCase().replace(/\.$/, "");
  if (url.protocol === "http:" && !["localhost", "127.0.0.1", "[::1]", "::1"].includes(hostname)) {
    return "";
  }
  const host = hostname.includes(":") && !hostname.startsWith("[") ? `[${hostname}]` : hostname;
  const port = url.port && !(
    (url.protocol === "https:" && url.port === "443") ||
    (url.protocol === "http:" && url.port === "80")
  ) ? `:${url.port}` : "";
  return `${url.protocol}//${host}${port}`;
}

function targetOrigin(request) {
  try {
    return normalizedOrigin(new URL(request.url).origin);
  } catch {
    return "";
  }
}

function allowedOrigins(env, request) {
  const origins = new Set(enabled(env.SINGLE_WORKER_SITE) ? [] : CANONICAL_ORIGINS);
  for (const name of ORIGIN_VARS) {
    const origin = normalizedOrigin(env[name]);
    if (origin) origins.add(origin);
  }
  for (const value of String(env.CORS_ALLOWED_ORIGINS || "").replaceAll("\n", ",").split(",")) {
    const origin = normalizedOrigin(value);
    if (origin) origins.add(origin);
  }
  const target = targetOrigin(request);
  if (target) origins.add(target);
  return origins;
}

function mergeVary(value, ...names) {
  const values = [];
  const seen = new Set();
  for (const item of [...String(value || "").split(","), ...names]) {
    const clean = item.trim();
    const lower = clean.toLowerCase();
    if (clean && !seen.has(lower)) {
      values.push(clean);
      seen.add(lower);
    }
  }
  return values.join(", ");
}

function corsHeaders(env, request, preflight = false) {
  const origin = normalizedOrigin(request.headers.get("origin"));
  if (!origin || !allowedOrigins(env, request).has(origin)) return null;
  const headers = new Headers({
    "access-control-allow-origin": origin,
    "access-control-allow-credentials": "true",
    "access-control-expose-headers": EXPOSE_HEADERS.join(", "),
    vary: "Origin",
  });
  if (!preflight) return headers;
  const requested = [];
  for (const value of String(request.headers.get("access-control-request-headers") || "").split(",")) {
    const clean = value.trim().toLowerCase();
    if (!clean) continue;
    if (!HEADER_TOKEN.test(clean)) return null;
    if (!requested.includes(clean)) requested.push(clean);
  }
  headers.set("access-control-allow-methods", CORS_METHODS.join(", "));
  headers.set("access-control-allow-headers", [...new Set([...DEFAULT_HEADERS, ...requested])].join(", "));
  headers.set("access-control-max-age", "86400");
  headers.set("vary", mergeVary(
    headers.get("vary"),
    "Access-Control-Request-Method",
    "Access-Control-Request-Headers",
  ));
  if (request.headers.get("access-control-request-private-network")?.toLowerCase() === "true") {
    headers.set("access-control-allow-private-network", "true");
  }
  return headers;
}

function json(payload, { status = 200, cacheControl = "", headers = null } = {}) {
  const responseHeaders = new Headers(headers || undefined);
  responseHeaders.set("content-type", "application/json; charset=utf-8");
  responseHeaders.set("x-forkmesh-worker", "app");
  responseHeaders.set("x-forkmesh-edge-control", "active");
  if (cacheControl) responseHeaders.set("cache-control", cacheControl);
  return new Response(JSON.stringify(payload), { status, headers: responseHeaders });
}

function apiResponse(request, env, payload, options = {}) {
  const headers = corsHeaders(env, request);
  return json(payload, { ...options, headers });
}

function preflight(request, env) {
  const headers = corsHeaders(env, request, true);
  if (!headers) {
    return json({ error: "origin_not_allowed" }, { status: 403, cacheControl: "no-store" });
  }
  const method = String(request.headers.get("access-control-request-method") || "").toUpperCase();
  if (!method || !CORS_METHODS.includes(method)) {
    return json({ error: "method_not_allowed" }, { status: 405, cacheControl: "no-store", headers });
  }
  headers.set("x-forkmesh-worker", "app");
  headers.set("x-forkmesh-edge-control", "active");
  return new Response(null, { status: 204, headers });
}

function withoutHeadBody(request, response) {
  if (request.method !== "HEAD") return response;
  return new Response(null, {
    status: response.status,
    statusText: response.statusText,
    headers: response.headers,
  });
}

function health(request, env) {
  return apiResponse(request, env, {
    ok: true,
    service: "forkmesh-mainnode",
    rev: String(env.BUILD_REV || "dev"),
    worker: String(env.WORKER_ROLE || "app"),
    node: String(env.NODE_NAME || "forkmesh"),
    nodeSolanaAddress: String(env.NODE_SOLANA_ADDRESS || ""),
    websocket: "/api/repo/{owner}/{repo}/rooms/{room}/ws",
    compatWebsocket: "/api/room/{room}/ws",
    capabilities: [
      "encrypted-relay-rooms",
      "repo-scoped-rooms",
      "ephemeral-ciphertext-broadcast",
    ],
    runtime: "javascript-edge-control",
  });
}

function version(request, env) {
  return withoutHeadBody(request, apiResponse(request, env, {
    ok: true,
    rev: String(env.BUILD_REV || "dev"),
    worker: String(env.WORKER_ROLE || "app"),
    version: String(env.APP_VERSION || ""),
    deployFingerprint: String(env.DEPLOY_FINGERPRINT || ""),
    now: Date.now(),
  }));
}

function mirrorIdentity(request, env) {
  const candidate = String(env.MIRROR_ROUTER_PUBLIC_KEY || "").trim();
  const routerPublicKey = /^[A-Za-z0-9_-]{43}$/.test(candidate) ? candidate : "";
  return apiResponse(request, env, {
    ok: Boolean(routerPublicKey),
    protocol: "forkmesh-masked-proxy-v1",
    registration: "forkmesh-https-endpoint-v1",
    routerPublicKey,
    nonCustodial: true,
    repositoryBytesInD1: false,
  }, {
    status: routerPublicKey ? 200 : 503,
    cacheControl: "no-store",
  });
}

async function kickCronRunner(env) {
  const id = env.FORKMESH_CRON_RUNNER.idFromName(RUNNER_NAME);
  const response = await env.FORKMESH_CRON_RUNNER.get(id).fetch(KICK_URL);
  if (!response.ok) {
    throw new Error(`cron runner rejected trigger kick (${response.status})`);
  }
}

export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (request.method === "OPTIONS" && (path === "/api" || path.startsWith("/api/"))) {
      return preflight(request, env);
    }
    if (path === "/health") return withoutHeadBody(request, health(request, env));
    if (path === "/api/version" || path === "/api/version/") return version(request, env);
    if (request.method === "GET" && (path === MIRROR_IDENTITY_PATH || path === `${MIRROR_IDENTITY_PATH}/`)) {
      return mirrorIdentity(request, env);
    }
    if (request.method === "GET" && path.startsWith(`${MIRROR_IDENTITY_PATH}//`) &&
        /^\/+$/u.test(path.slice(MIRROR_IDENTITY_PATH.length))) {
      return apiResponse(request, env, { error: "not_found" }, {
        status: 404,
        cacheControl: "no-store",
      });
    }
    return env.APP.fetch(request);
  },
  async scheduled(_controller, env) {
    await kickCronRunner(env);
  },
};
