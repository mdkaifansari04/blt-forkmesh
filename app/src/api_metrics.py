"""Per-minute API request metrics and the app.forkmesh.com/api landing page.

Every served /api/* response is folded into a per-isolate accumulator and
flushed to D1 in batched upserts (one bounded write per few dozen requests,
not one per request — D1's free-plan write budget is the constraint). The
buckets are minute × normalized route group × status class, which is enough
for the diagnostics the landing page charts: request frequency, error mix,
and per-endpoint latency.

The module also serves the developer surface of that page: an endpoint
catalog reflected from urls.py's route regexes (plus a curated list for the
literal routes entry.py matches inline) and a generated OpenAPI 3 document,
so the landing page can offer a try-it request tester without a separately
maintained API description.

entry.py loads this module lazily on the first /api response (see the 10021
startup-memory budget notes there); it must stay small and import nothing
heavy at module scope.
"""

import re


def _bind_runtime(runtime):
    """Supply the entrypoint primitives used by the metrics implementation."""
    namespace = globals()
    for name, value in runtime.items():
        if not name.startswith("__") and name not in namespace:
            namespace[name] = value


MINUTE_MS = 60_000
RETENTION_MS = 7 * 24 * 3_600_000
# Flush when this many distinct buckets are pending, or whenever a bucket
# from an already-completed minute is still unflushed (so charts lag live
# traffic by at most one flush trigger, and an idle isolate's last minute
# lands with its next request).
FLUSH_PENDING_LIMIT = 15
FLUSH_ROW_LIMIT = 15
# The route grouper is deliberately lossy: bounded label cardinality is what
# keeps the table small and the top-endpoints chart readable. Past the cap,
# new groups fold into "other" for this isolate's lifetime.
GROUP_LIMIT = 200
_API_NAMESPACES = frozenset({
    "accounts", "ai", "badges", "blog", "bot", "chat", "client-errors",
    "bootstrap", "desktop-errors", "federation", "feedback", "forkbot", "genie",
    "install-diag", "install-source", "integrations", "leaderboards",
    "mainnode", "metrics", "mirror-requests", "mirrors", "network",
    "nodes", "notes", "notifications", "openapi.json", "orgs", "outreach",
    "poll", "private-replicas", "referrals", "relay-mesh", "repo",
    "repositories", "repository-imports", "rewards", "room", "security",
    "security-scans", "ssh", "status", "sync", "tasks", "version", "world",
})

_STATIC_GROUPS = frozenset({
    "accounts/availability", "accounts/forgot-password", "accounts/login",
    "accounts/logout", "accounts/profile", "accounts/reset-password",
    "accounts/sessions", "accounts/signup", "accounts/users",
    "bootstrap/readiness", "metrics/endpoints", "metrics/summary", "mirrors/https",
    "network/overview", "openapi.json", "repositories", "status", "sync",
    "version", "world/client", "world/context", "world/inactive",
    "world/online", "world/qa", "world/ticket", "world/visitors",
    "world/wallet", "world/ws",
})

_pending = {}
_groups_seen = set()
_last_prune_minute = 0


def _masked_group(path):
    """The identifier-free label for a path, without registry effects."""
    segments = [s for s in str(path or "").split("/") if s]
    if segments[:1] == ["api"]:
        segments = segments[1:]
    if not segments:
        return "(root)"
    lowered = [segment.lower() for segment in segments[:4]]
    namespace = lowered[0]
    if namespace not in _API_NAMESPACES:
        return "other"
    candidate = "/".join(lowered)
    if candidate in _STATIC_GROUPS:
        return candidate
    return namespace if len(lowered) == 1 else namespace + "/*"


def route_group(path):
    """A bounded, identifier-free label for an /api path."""
    group = _masked_group(path)
    if group in _groups_seen:
        return group
    if len(_groups_seen) >= GROUP_LIMIT:
        return "other"
    _groups_seen.add(group)
    return group


def _status_class(status):
    status = int(status or 0)
    if status >= 500:
        return "5xx"
    if status >= 400:
        return "4xx"
    if status >= 300:
        return "3xx"
    return "2xx"


async def record_api_request(env, path, status, duration_ms):
    """Fold one served /api response into the minute buckets (never raises)."""
    try:
        now = int(Date.now())
        minute = (now // MINUTE_MS) * MINUTE_MS
        key = (minute, route_group(path), _status_class(status))
        bucket = _pending.get(key)
        duration_ms = max(0, int(duration_ms or 0))
        if bucket is None:
            _pending[key] = [1, duration_ms, duration_ms]
        else:
            bucket[0] += 1
            bucket[1] += duration_ms
            bucket[2] = max(bucket[2], duration_ms)
        stale = any(k[0] < minute for k in _pending)
        if len(_pending) >= FLUSH_PENDING_LIMIT or stale:
            await _flush(env, now)
    except BaseException:
        # Metrics must never break (or slow) serving; a dropped bucket is
        # bounded loss.
        _pending.clear()


async def _flush(env, now):
    batch = dict(_pending)
    _pending.clear()
    if not batch:
        return
    await ensure_schema(env)
    rows = list(batch.items())
    for offset in range(0, len(rows), FLUSH_ROW_LIMIT):
        chunk = rows[offset:offset + FLUSH_ROW_LIMIT]
        placeholders = ",".join(["(?,?,?,?,?,?)"] * len(chunk))
        args = []
        for (minute, group, cls), (count, dur_sum, dur_max) in chunk:
            args.extend([minute, group, cls, count, dur_sum, dur_max])
        await d1_run(
            env,
            "INSERT INTO api_metrics_minute "
            "(minute_ts, route_group, status_class, requests, dur_ms_sum,"
            " dur_ms_max) VALUES " + placeholders + " "
            "ON CONFLICT(minute_ts, route_group, status_class) DO UPDATE SET "
            "requests=requests+excluded.requests, "
            "dur_ms_sum=dur_ms_sum+excluded.dur_ms_sum, "
            "dur_ms_max=MAX(dur_ms_max, excluded.dur_ms_max)",
            *args,
        )
    # Retention rides the flush path once per isolate-hour (minute 7 keeps it
    # off the busy top-of-hour), so no scheduler dependency is added.
    global _last_prune_minute
    minute_of_hour = (now // MINUTE_MS) % 60
    if minute_of_hour == 7 and _last_prune_minute != now // MINUTE_MS:
        _last_prune_minute = now // MINUTE_MS
        await d1_run(
            env,
            "DELETE FROM api_metrics_minute WHERE minute_ts < ?",
            now - RETENTION_MS,
        )


async def metrics_summary_handler(env, request):
    """GET /api/metrics/summary?minutes=N&limit=M — buckets for the landing
    page and the Qt client's Web Requests tab.

    Public and content-free by construction: route groups are masked labels,
    counts and latencies only (the /status page already exposes coarser
    health publicly). `limit` caps the groups list; the default keeps the
    landing page's historical top-40 shape, while the ceiling comfortably
    covers every group route_group() can mint (GROUP_LIMIT plus "other").
    """
    await ensure_schema(env)
    query = parse_qs(urlparse(request.url).query)
    try:
        minutes = int(query.get("minutes", ["60"])[0])
    except Exception:
        minutes = 60
    minutes = max(10, min(minutes, 1440))
    try:
        limit = int(query.get("limit", ["40"])[0])
    except Exception:
        limit = 40
    limit = max(1, min(limit, 500))
    now = int(Date.now())
    since = ((now // MINUTE_MS) * MINUTE_MS) - minutes * MINUTE_MS
    rows = await d1_all(
        env,
        "SELECT minute_ts, route_group, status_class, requests, dur_ms_sum,"
        " dur_ms_max FROM api_metrics_minute WHERE minute_ts >= ?",
        since,
    )
    per_minute = {}
    per_group = {}
    for row in rows or []:
        minute = int(row.get("minute_ts") or 0)
        group = str(row.get("route_group") or "other")
        cls = str(row.get("status_class") or "2xx")
        requests = int(row.get("requests") or 0)
        dur_sum = int(row.get("dur_ms_sum") or 0)
        dur_max = int(row.get("dur_ms_max") or 0)
        m = per_minute.setdefault(
            minute, {"t": minute, "requests": 0, "errors": 0})
        m["requests"] += requests
        if cls == "5xx":
            m["errors"] += requests
        g = per_group.setdefault(group, {
            "group": group, "requests": 0, "errors": 0,
            "dur_ms_sum": 0, "dur_ms_max": 0,
            "classes": {"2xx": 0, "3xx": 0, "4xx": 0, "5xx": 0},
        })
        g["requests"] += requests
        g["dur_ms_sum"] += dur_sum
        g["dur_ms_max"] = max(g["dur_ms_max"], dur_max)
        g["classes"][cls] = g["classes"].get(cls, 0) + requests
        if cls == "5xx":
            g["errors"] += requests
    groups = []
    for g in per_group.values():
        g["avg_ms"] = (
            g["dur_ms_sum"] // g["requests"] if g["requests"] else 0)
        del g["dur_ms_sum"]
        groups.append(g)
    groups.sort(key=lambda g: -g["requests"])
    classes = {"2xx": 0, "3xx": 0, "4xx": 0, "5xx": 0}
    for g in groups:
        for cls, count in g["classes"].items():
            classes[cls] = classes.get(cls, 0) + count
    return json_response({
        "ok": True,
        "now": now,
        "windowMinutes": minutes,
        "worker": str(getattr(env, "WORKER_ROLE", "") or "relay"),
        "rev": str(getattr(env, "BUILD_REV", "") or ""),
        "minutes": sorted(per_minute.values(), key=lambda m: m["t"]),
        "classes": classes,
        "groups": groups[:limit],
    }, cache_control="no-store, max-age=0, must-revalidate")


# Literal routes entry.py matches inline (they have no urls.py regex to
# reflect). Kept deliberately short: the reflected table below carries the
# bulk, and anything unlisted still shows up in the observed traffic groups.
_CURATED_ENDPOINTS = [
    ("GET", "/api/version", "Build stamp + which Worker answered"),
    ("GET", "/api/status", "Public systems status and minute history"),
    ("GET", "/api/metrics/summary", "Traffic buckets behind these charts"),
    ("GET", "/api/metrics/endpoints", "This endpoint catalog"),
    ("GET", "/api/openapi.json", "Generated OpenAPI 3 description"),
    ("GET", "/api/repositories", "Public repository catalog"),
    ("GET", "/api/mirrors/https", "Mirror gateway protocol identity"),
    ("GET", "/api/world/online", "World presence count"),
    ("GET", "/api/world/deploy-status", "World deploy semaphore"),
    ("GET", "/api/accounts/users", "Public account directory"),
    ("GET", "/api/leaderboards", "Node leaderboards"),
    ("GET", "/api/sync", "Signed owner-node drain (owner+ts+sig auth)"),
]


def _template_from_pattern(pattern):
    """A human-usable path template for one urls.py route regex."""
    text = str(pattern or "")
    text = text[1:] if text.startswith("^") else text
    text = text[:-1] if text.endswith("$") else text
    text = text.replace("/?", "")
    # Keep the first alternative of non-capturing (?:a|b) groups.
    text = re.sub(r"\(\?:([^)|]+)(?:\|[^()]*)?\)", r"\1", text)
    counter = {"n": 0}

    def _placeholder(_match):
        counter["n"] += 1
        return "{p%d}" % counter["n"]

    text = re.sub(r"\((?:[^()]|\([^()]*\))*\)", _placeholder, text)
    text = text.replace("\\.", ".").replace("\\-", "-")
    if text.startswith("/api/repo/{p1}/{p2}"):
        text = text.replace("{p1}", "{owner}", 1).replace("{p2}", "{repo}", 1)
    elif text.startswith("/api/orgs/{p1}"):
        text = text.replace("{p1}", "{org}", 1)
    return text


def endpoint_catalog():
    """Every route template: reflected from urls.py plus the curated list."""
    import urls
    items = {}
    for method, template, summary in _CURATED_ENDPOINTS:
        items[template] = {
            "method": method, "template": template, "summary": summary,
            "source": "curated",
        }
    for name in sorted(dir(urls)):
        if not name.endswith("_RE"):
            continue
        pattern = str(getattr(getattr(urls, name), "pattern", "") or "")
        if not pattern.startswith("^/"):
            continue
        template = _template_from_pattern(pattern)
        if template in items:
            continue
        # A bare single wildcard segment ("/{p1}") is a page-route matcher,
        # not an endpoint: it matches every one-segment path (/login, /docs,
        # a public org page) and would render in the catalog as a callable
        # catch-all with a "try" button. Prefixed page routes like /r/{p1}
        # stay - they name a real, specific surface.
        if re.fullmatch(r"/\{[a-z0-9]+\}", template):
            continue
        method = "POST" if (
            "upload-pack" in template or "receive-pack" in template
        ) else "GET"
        items[template] = {
            "method": method,
            "template": template,
            "summary": name[:-3].replace("_", " ").lower(),
            "source": "urls.py",
        }
    catalog = []
    for item in items.values():
        path_only = item["template"].split("?")[0]
        item["group"] = (
            _masked_group(path_only)
            if path_only.startswith("/api/") else None)
        item["params"] = re.findall(r"\{([a-z0-9]+)\}", item["template"])
        catalog.append(item)
    catalog.sort(key=lambda i: i["template"])
    return catalog


async def endpoint_catalog_handler(env, request):
    """GET /api/metrics/endpoints — the catalog the landing page renders."""
    return json_response({
        "ok": True,
        "endpoints": endpoint_catalog(),
    }, cache_control="public, max-age=300")


async def openapi_handler(env, request):
    """GET /api/openapi.json — a generated, best-effort OpenAPI 3 document.

    Paths and parameters are exact (reflected from the route table); methods
    beyond the known POST routes default to GET, and auth/response schemas
    are deliberately not asserted — this exists so standard tooling can
    import the surface, not as a hand-maintained contract.
    """
    paths = {}
    for item in endpoint_catalog():
        template = item["template"].split("?")[0]
        operation = {
            "summary": item["summary"],
            "responses": {"200": {"description": "OK"}},
        }
        if item["params"]:
            operation["parameters"] = [
                {
                    "name": param,
                    "in": "path",
                    "required": True,
                    "schema": {"type": "string"},
                }
                for param in item["params"]
            ]
        paths.setdefault(template, {})[item["method"].lower()] = operation
    return json_response({
        "openapi": "3.0.3",
        "info": {
            "title": "ForkMesh API",
            "version": str(getattr(env, "APP_VERSION", "") or "0"),
            "description": (
                "Generated from the Worker's route table. Methods beyond "
                "the known POST routes default to GET; many routes require "
                "signed or session auth that this document does not model."),
        },
        "servers": [{
            "url": str(
                getattr(env, "API_ORIGIN", "")
                or "https://app.forkmesh.com"
            ).rstrip("/")
        }],
        "paths": paths,
    }, cache_control="public, max-age=300")


def landing_page_response(env):
    """The app /api landing page with live diagnostics for API traffic.

    Self-contained (inline CSS/JS, no CDNs — the page sets its own CSP) and
    dark-committed: it is an operations surface, not a marketing page. The
    charts read GET /api/metrics/summary from this same origin.
    """
    rev = str(getattr(env, "BUILD_REV", "") or "")[:12]
    worker = str(getattr(env, "WORKER_ROLE", "") or "relay")
    html = _LANDING_PAGE_HTML.replace("__REV__", rev).replace(
        "__WORKER__", worker)
    return Response(html, status=200, headers={
        "content-type": "text/html; charset=utf-8",
        "cache-control": "no-store, max-age=0, must-revalidate",
        "content-security-policy": (
            "default-src 'none'; script-src 'unsafe-inline'; "
            "style-src 'unsafe-inline'; connect-src 'self'; "
            "img-src 'self' data:; base-uri 'none'; form-action 'none'"),
        "x-frame-options": "DENY",
        "x-content-type-options": "nosniff",
        "referrer-policy": "strict-origin-when-cross-origin",
    })


# Palette: dataviz reference instance, dark mode — categorical slot 1
# (#3987e5) carries the request series and the single-hue bars; status
# critical (#d03b3b) is reserved for the 5xx series and never reused as a
# category. Validated pair (CVD ΔE 25.7, normal 31.9, both ≥3:1 on #1a1a19).
_LANDING_PAGE_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>ForkMesh API — traffic diagnostics</title>
<style>
  :root {
    color-scheme: dark;
    --surface: #1a1a19; --panel: #232322; --line: #ffffff14;
    --text: #ffffff; --text-2: #c3c2b7; --text-3: #8b8a80;
    --series-req: #3987e5; --status-critical: #d03b3b;
  }
  * { box-sizing: border-box; margin: 0; }
  body { background: var(--surface); color: var(--text);
    font: 14px/1.5 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    padding: 24px; max-width: 1080px; margin: 0 auto; }
  header { display: flex; flex-wrap: wrap; align-items: baseline;
    gap: 12px; margin-bottom: 20px; }
  h1 { font-size: 18px; font-weight: 700; }
  h2 { font-size: 13px; font-weight: 600; color: var(--text-2);
    margin: 26px 0 10px; text-transform: uppercase; letter-spacing: .06em; }
  .meta { color: var(--text-3); font-size: 12px; }
  .filters { display: flex; gap: 8px; margin-left: auto; }
  button { background: var(--panel); color: var(--text-2); cursor: pointer;
    border: 1px solid var(--line); border-radius: 6px; padding: 4px 12px;
    font: inherit; font-size: 12px; }
  button[aria-pressed="true"] { color: var(--text);
    border-color: var(--series-req); }
  .tiles { display: grid; grid-template-columns:
    repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; }
  .tile { background: var(--panel); border-radius: 8px; padding: 12px 14px; }
  .tile b { display: block; font-size: 22px; font-weight: 700; }
  .tile span { color: var(--text-3); font-size: 12px; }
  .tile.crit b { color: var(--status-critical); }
  .panel { background: var(--panel); border-radius: 8px; padding: 14px;
    overflow-x: auto; }
  .legend { display: flex; gap: 16px; font-size: 12px;
    color: var(--text-2); margin-bottom: 6px; }
  .legend i { display: inline-block; width: 14px; height: 3px;
    border-radius: 2px; vertical-align: middle; margin-right: 6px; }
  svg text { font: 11px ui-monospace, monospace; fill: var(--text-3); }
  .bar-label { fill: var(--text-2); }
  .bar-value { fill: var(--text); }
  #tooltip { position: fixed; pointer-events: none; background: #000000e0;
    color: var(--text); padding: 6px 9px; border-radius: 6px; font-size: 12px;
    display: none; border: 1px solid var(--line); z-index: 9; }
  table { border-collapse: collapse; width: 100%; font-size: 12px; }
  th, td { text-align: right; padding: 5px 10px;
    border-bottom: 1px solid var(--line); }
  th:first-child, td:first-child { text-align: left; }
  th { color: var(--text-3); font-weight: 600; }
  a { color: var(--series-req); }
  .hidden { display: none; }
</style>
</head>
<body>
<header>
  <h1>ForkMesh API</h1>
  <span class="meta">served by __WORKER__ · build __REV__ ·
    <a href="/api/metrics/summary?minutes=60">raw summary JSON</a></span>
  <nav class="filters" aria-label="time range">
    <button data-mins="60" aria-pressed="true">1h</button>
    <button data-mins="360" aria-pressed="false">6h</button>
    <button data-mins="1440" aria-pressed="false">24h</button>
    <button id="tableToggle" aria-pressed="false">table</button>
  </nav>
</header>

<div class="tiles" id="tiles"></div>

<h2>Requests per minute</h2>
<div class="panel">
  <div class="legend">
    <span><i style="background:var(--series-req)"></i>requests</span>
    <span><i style="background:var(--status-critical)"></i>5xx errors</span>
  </div>
  <svg id="lineChart" width="1020" height="220" role="img"
    aria-label="Requests and 5xx errors per minute"></svg>
</div>

<h2 id="topHead">Top endpoints</h2>
<div class="panel"><svg id="topChart" width="1020" height="10"></svg></div>

<h2>Slowest endpoints (avg ms)</h2>
<div class="panel"><svg id="slowChart" width="1020" height="10"></svg></div>

<h2 class="hidden" id="tableHead">Endpoint table</h2>
<div class="panel hidden" id="tablePanel"></div>

<h2 id="catalogHead">API endpoints
  <span class="meta" style="text-transform:none;letter-spacing:0">
    — reflected from the route table ·
    <a href="/api/openapi.json">openapi.json</a></span></h2>
<div class="panel">
  <input id="epFilter" placeholder="filter endpoints…" aria-label="filter
    endpoints" style="width:100%;margin-bottom:10px;background:var(--surface);
    border:1px solid var(--line);border-radius:6px;color:var(--text);
    font:inherit;padding:6px 10px">
  <div style="max-height:340px;overflow-y:auto">
    <table id="catalogTable"><thead><tr><th>method</th><th>endpoint</th>
      <th>req/1h</th><th>avg ms</th><th></th></tr></thead>
      <tbody></tbody></table>
  </div>
</div>

<h2 id="testerHead">Request tester</h2>
<div class="panel" id="tester">
  <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px">
    <select id="tMethod" aria-label="method" style="background:var(--surface);
      color:var(--text);border:1px solid var(--line);border-radius:6px;
      font:inherit;padding:6px">
      <option>GET</option><option>POST</option><option>PUT</option>
      <option>DELETE</option><option>HEAD</option>
    </select>
    <input id="tPath" value="/api/version" aria-label="request path"
      style="flex:1;min-width:280px;background:var(--surface);
      border:1px solid var(--line);border-radius:6px;color:var(--text);
      font:inherit;padding:6px 10px">
    <button id="tSend" style="border-color:var(--series-req);
      color:var(--text)">send</button>
  </div>
  <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px">
    <input id="tAuth" placeholder="bearer token (optional)"
      aria-label="bearer token" style="flex:1;min-width:280px;
      background:var(--surface);border:1px solid var(--line);
      border-radius:6px;color:var(--text);font:inherit;padding:6px 10px">
  </div>
  <textarea id="tBody" rows="3" placeholder="request body (non-GET, JSON)"
    class="hidden" style="width:100%;background:var(--surface);
    border:1px solid var(--line);border-radius:6px;color:var(--text);
    font:inherit;padding:6px 10px;margin-bottom:8px"></textarea>
  <div class="meta" id="tStatus">Pick an endpoint above or type a path, then
    send. Requests run same-origin against this Worker with your token only
    in this tab.</div>
  <pre id="tOut" class="hidden" style="background:var(--surface);
    border-radius:6px;padding:10px;max-height:360px;overflow:auto;
    white-space:pre-wrap;font-size:12px;margin-top:8px"></pre>
</div>

<p class="meta" style="margin-top:18px">Auto-refreshes every 60s. Buckets are
one minute × masked route group × status class; identifiers are never
recorded. Retention 7 days.</p>
<div id="tooltip"></div>

<script>
"use strict";
const REQ = getComputedStyle(document.documentElement)
  .getPropertyValue("--series-req").trim() || "#3987e5";
const CRIT = getComputedStyle(document.documentElement)
  .getPropertyValue("--status-critical").trim() || "#d03b3b";
let mins = 60, data = null, timer = null;
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"']/g,
  (c) => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));

async function load() {
  try {
    const r = await fetch("/api/metrics/summary?minutes=" + mins);
    data = await r.json();
    render();
  } catch (e) { /* transient; next refresh retries */ }
}

function tiles() {
  const total = data.minutes.reduce((a, m) => a + m.requests, 0);
  const errors = data.minutes.reduce((a, m) => a + m.errors, 0);
  const durGroups = data.groups.filter((g) => g.requests > 0);
  const avg = durGroups.length ? Math.round(durGroups.reduce(
    (a, g) => a + g.avg_ms * g.requests, 0) / Math.max(1, total)) : 0;
  const rate = total ? (100 * errors / total) : 0;
  $("tiles").innerHTML =
    tile(total.toLocaleString(), "requests / " + label(mins)) +
    tile(errors.toLocaleString(), "5xx responses", errors > 0) +
    tile(rate.toFixed(2) + "%", "error rate", rate >= 1) +
    tile(avg + " ms", "avg latency") +
    tile((data.classes["4xx"] || 0).toLocaleString(), "4xx responses");
}
const tile = (v, l, crit) =>
  `<div class="tile${crit ? " crit" : ""}"><b>${v}</b><span>${l}</span></div>`;
const label = (m) => m >= 1440 ? "24h" : m >= 360 ? "6h" : "1h";

function lineChart() {
  const svg = $("lineChart"), W = 1020, H = 220,
    P = {l: 46, r: 74, t: 10, b: 22};
  const start = data.now - mins * 60000;
  const byT = Object.fromEntries(data.minutes.map((m) => [m.t, m]));
  const pts = [];
  for (let t = Math.ceil(start / 60000) * 60000; t <= data.now; t += 60000) {
    const m = byT[t] || {t, requests: 0, errors: 0};
    pts.push(m);
  }
  const maxY = Math.max(5, ...pts.map((m) => m.requests));
  const x = (t) => P.l + (W - P.l - P.r) * (t - pts[0].t) /
    Math.max(1, pts[pts.length - 1].t - pts[0].t);
  const y = (v) => H - P.b - (H - P.t - P.b) * v / maxY;
  const path = (key) => pts.map((m, i) =>
    (i ? "L" : "M") + x(m.t).toFixed(1) + "," + y(m[key]).toFixed(1)).join("");
  const gridN = 4;
  let s = "";
  for (let i = 0; i <= gridN; i++) {
    const v = Math.round(maxY * i / gridN), gy = y(v);
    s += `<line x1="${P.l}" x2="${W - P.r}" y1="${gy}" y2="${gy}"
      stroke="var(--line)"/><text x="${P.l - 8}" y="${gy + 4}"
      text-anchor="end">${v}</text>`;
  }
  const hours = mins > 120;
  for (let i = 0; i <= 4; i++) {
    const t = pts[0].t + (pts[pts.length - 1].t - pts[0].t) * i / 4;
    const d = new Date(t);
    s += `<text x="${x(t)}" y="${H - 6}" text-anchor="middle">` +
      String(d.getUTCHours()).padStart(2, "0") + ":" +
      String(d.getUTCMinutes()).padStart(2, "0") + "Z</text>";
  }
  s += `<path d="${path("requests")}" fill="none" stroke="${REQ}"
    stroke-width="2"/>`;
  s += `<path d="${path("errors")}" fill="none" stroke="${CRIT}"
    stroke-width="2"/>`;
  const last = pts[pts.length - 1];
  s += `<text x="${W - P.r + 8}" y="${y(last.requests) + 4}"
    style="fill:${REQ}">requests</text>`;
  s += `<text x="${W - P.r + 8}" y="${Math.max(y(last.errors) + 4,
    y(last.requests) + 18)}" style="fill:${CRIT}">5xx</text>`;
  s += `<line id="xhair" y1="${P.t}" y2="${H - P.b}" stroke="var(--text-3)"
    stroke-width="1" visibility="hidden"/>`;
  svg.innerHTML = s;
  svg.onmousemove = (ev) => {
    const box = svg.getBoundingClientRect();
    const mx = (ev.clientX - box.left) * (W / box.width);
    let best = pts[0];
    for (const m of pts) if (Math.abs(x(m.t) - mx) < Math.abs(x(best.t) - mx))
      best = m;
    const hair = $("xhair");
    hair.setAttribute("x1", x(best.t)); hair.setAttribute("x2", x(best.t));
    hair.setAttribute("visibility", "visible");
    const tip = $("tooltip"), d = new Date(best.t);
    tip.style.display = "block";
    tip.style.left = (ev.clientX + 14) + "px";
    tip.style.top = (ev.clientY - 10) + "px";
    tip.innerHTML = String(d.getUTCHours()).padStart(2, "0") + ":" +
      String(d.getUTCMinutes()).padStart(2, "0") + "Z — " +
      best.requests + " req, " + best.errors + " 5xx";
  };
  svg.onmouseleave = () => { $("tooltip").style.display = "none";
    $("xhair").setAttribute("visibility", "hidden"); };
}

function bars(id, rows, valueOf, fmt) {
  const svg = $(id), W = 1020, rowH = 26, P = {l: 320, r: 90};
  svg.setAttribute("height", Math.max(30, rows.length * rowH + 8));
  const maxV = Math.max(1, ...rows.map(valueOf));
  let s = "";
  rows.forEach((g, i) => {
    const v = valueOf(g), bw = Math.max(2, (W - P.l - P.r) * v / maxV);
    const gy = i * rowH + 6;
    s += `<text class="bar-label" x="${P.l - 10}" y="${gy + 14}"
      text-anchor="end">${esc(g.group).slice(0, 40)}</text>`;
    s += `<rect x="${P.l}" y="${gy}" width="${bw}" height="18" rx="4"
      fill="${REQ}"><title>${esc(g.group)} — ${fmt(g)}</title></rect>`;
    if (g.errors > 0) {
      const ew = Math.max(2, bw * g.errors / Math.max(1, g.requests));
      s += `<rect x="${P.l}" y="${gy + 20}" width="${ew}" height="2"
        fill="${CRIT}"/>`;
    }
    s += `<text class="bar-value" x="${P.l + bw + 8}" y="${gy + 14}">` +
      fmt(g) + `</text>`;
  });
  svg.innerHTML = s;
}

function table() {
  const rows = data.groups.map((g) =>
    `<tr><td>${esc(g.group)}</td><td>${g.requests.toLocaleString()}</td>` +
    `<td>${g.classes["2xx"] || 0}</td><td>${g.classes["3xx"] || 0}</td>` +
    `<td>${g.classes["4xx"] || 0}</td><td>${g.classes["5xx"] || 0}</td>` +
    `<td>${g.avg_ms}</td><td>${g.dur_ms_max}</td></tr>`).join("");
  $("tablePanel").innerHTML =
    `<table><thead><tr><th>route group</th><th>requests</th><th>2xx</th>` +
    `<th>3xx</th><th>4xx</th><th>5xx</th><th>avg ms</th><th>max ms</th>` +
    `</tr></thead><tbody>${rows}</tbody></table>`;
}

function render() {
  if (!data || !data.ok) return;
  tiles(); lineChart();
  bars("topChart", data.groups.slice(0, 12),
    (g) => g.requests, (g) => g.requests.toLocaleString());
  bars("slowChart",
    [...data.groups].sort((a, b) => b.avg_ms - a.avg_ms).slice(0, 8),
    (g) => g.avg_ms, (g) => g.avg_ms + " ms");
  table();
}

document.querySelectorAll(".filters button[data-mins]").forEach((b) => {
  b.onclick = () => {
    document.querySelectorAll(".filters button[data-mins]").forEach(
      (o) => o.setAttribute("aria-pressed", o === b ? "true" : "false"));
    mins = Number(b.dataset.mins); load();
  };
});
$("tableToggle").onclick = () => {
  const on = $("tableToggle").getAttribute("aria-pressed") !== "true";
  $("tableToggle").setAttribute("aria-pressed", on ? "true" : "false");
  $("tablePanel").classList.toggle("hidden", !on);
  $("tableHead").classList.toggle("hidden", !on);
};

// --- Endpoint catalog + request tester ---------------------------------
let catalog = [];
async function loadCatalog() {
  try {
    const r = await fetch("/api/metrics/endpoints");
    catalog = (await r.json()).endpoints || [];
    renderCatalog();
  } catch (e) { /* transient */ }
}
function renderCatalog() {
  const needle = $("epFilter").value.trim().toLowerCase();
  const byGroup = Object.fromEntries(
    (data && data.groups || []).map((g) => [g.group, g]));
  const rows = catalog
    .filter((e) => !needle || e.template.toLowerCase().includes(needle) ||
      (e.summary || "").toLowerCase().includes(needle))
    .map((e) => {
      const seen = e.group && byGroup[e.group];
      return `<tr><td style="text-align:left">${esc(e.method)}</td>` +
        `<td style="text-align:left" title="${esc(e.summary || "")}">` +
        `${esc(e.template)}</td>` +
        `<td>${seen ? seen.requests.toLocaleString() : "—"}</td>` +
        `<td>${seen ? seen.avg_ms : "—"}</td>` +
        `<td><button data-try="${esc(e.template)}" ` +
        `data-method="${esc(e.method)}">try</button></td></tr>`;
    }).join("");
  $("catalogTable").querySelector("tbody").innerHTML =
    rows || `<tr><td colspan="5">no matches</td></tr>`;
  $("catalogTable").querySelectorAll("button[data-try]").forEach((b) => {
    b.onclick = () => {
      $("tMethod").value = b.dataset.method;
      $("tPath").value = b.dataset.try.split("?")[0];
      $("tBody").classList.toggle("hidden", b.dataset.method === "GET");
      $("tester").scrollIntoView({ behavior: "smooth", block: "center" });
      $("tPath").focus();
    };
  });
}
$("epFilter").oninput = renderCatalog;
$("tMethod").onchange = () =>
  $("tBody").classList.toggle("hidden", $("tMethod").value === "GET");
$("tSend").onclick = async () => {
  const method = $("tMethod").value;
  let path = $("tPath").value.trim();
  if (!path.startsWith("/")) path = "/" + path;
  const headers = { accept: "application/json" };
  if ($("tAuth").value.trim())
    headers.authorization = "Bearer " + $("tAuth").value.trim();
  const init = { method, headers, cache: "no-store" };
  if (method !== "GET" && method !== "HEAD" && $("tBody").value.trim()) {
    init.body = $("tBody").value;
    headers["content-type"] = "application/json";
  }
  $("tStatus").textContent = "…";
  const started = performance.now();
  try {
    const r = await fetch(path, init);
    const ms = Math.round(performance.now() - started);
    const text = await r.text();
    let body = text;
    try { body = JSON.stringify(JSON.parse(text), null, 2); } catch (e) {}
    const shown = body.length > 20000
      ? body.slice(0, 20000) + "\\n… (" + body.length + " chars)" : body;
    const hdrs = [...r.headers.entries()].map(([k, v]) => k + ": " + v)
      .join("\\n");
    $("tStatus").textContent =
      `HTTP ${r.status} · ${ms} ms · ${text.length} bytes — ` +
      `curl -X ${method} '${location.origin}${path}'`;
    $("tOut").classList.remove("hidden");
    $("tOut").textContent = hdrs + "\\n\\n" + shown;
  } catch (e) {
    $("tStatus").textContent = "request failed: " + e;
  }
};

load();
loadCatalog();
timer = setInterval(load, 60000);
</script>
</body>
</html>
"""
