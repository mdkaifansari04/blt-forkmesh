"""Lazy-loaded, content-free Cache API diagnostics.

Keeping this request-time instrumentation outside the already large Worker
entry module reduces Python startup memory without changing cache behavior.
"""

import json
from urllib.parse import urlparse

from js import Date


VERSION = 1
KV_SNAPSHOT_KEY = "edge-cache-diagnostics-v1"
KV_SNAPSHOT_MINUTES = 10
_STATS = {
    "hits": 0,
    "misses": 0,
    "puts": 0,
    "deletes": 0,
    "errors": 0,
    "routes": {},
    "lastOperationAt": 0,
    "startedAt": 0,
}
_KV_READ_CACHE = {"readAt": 0, "value": None}


def _scope(cache_key):
    try:
        path = str(urlparse(str(cache_key or "")).path or "/").lower()
    except Exception:
        path = "/"
    if "repository-metadata" in path:
        return "repository-metadata"
    if "git-advert" in path or "info/refs" in path:
        return "git-advertisement"
    if path.startswith("/api/world/"):
        return "world"
    if path.startswith("/api/network/"):
        return "network"
    if path.startswith("/api/accounts/"):
        return "accounts-public"
    if path.startswith("/api/repo") or path.startswith("/api/repositories"):
        return "repositories-public"
    return "other-public"


def record(operation, cache_key="", failed=False):
    now = int(Date.now())
    if int(_STATS["startedAt"]) <= 0:
        _STATS["startedAt"] = now
    if failed:
        _STATS["errors"] += 1
    elif operation in _STATS:
        _STATS[operation] += 1
    route = _STATS["routes"].setdefault(
        _scope(cache_key),
        {"hits": 0, "misses": 0, "puts": 0, "deletes": 0, "errors": 0},
    )
    field = "errors" if failed else operation
    if field in route:
        route[field] += 1
    _STATS["lastOperationAt"] = now


def local_snapshot(build_rev, sampled_at=None):
    sampled_at = int(sampled_at or Date.now())
    if int(_STATS["startedAt"]) <= 0:
        _STATS["startedAt"] = sampled_at
    started_at = int(_STATS["startedAt"])
    hits = int(_STATS["hits"])
    misses = int(_STATS["misses"])
    lookups = hits + misses
    return {
        "version": VERSION,
        "sampledAt": sampled_at,
        "buildRev": build_rev,
        "isolateStartedAt": started_at,
        "uptimeMs": max(0, sampled_at - started_at),
        "lookups": lookups,
        "hits": hits,
        "misses": misses,
        "hitRate": round(hits / lookups, 4) if lookups else 0,
        "puts": int(_STATS["puts"]),
        "deletes": int(_STATS["deletes"]),
        "errors": int(_STATS["errors"]),
        "lastOperationAt": int(_STATS["lastOperationAt"]),
        "routes": {
            name: dict(values)
            for name, values in sorted(_STATS["routes"].items())
        },
    }


async def kv_snapshot(env, build_rev, to_js, sampled_at=None):
    namespace = getattr(env, "WORLD_CACHE_META", None)
    if namespace is None:
        return False
    payload = local_snapshot(build_rev, sampled_at)
    try:
        await namespace.put(
            KV_SNAPSHOT_KEY,
            json.dumps(payload, separators=(",", ":")),
            to_js({"expirationTtl": 24 * 60 * 60}),
        )
    except Exception:
        record("puts", "https://forkmesh.internal/kv", failed=True)
        return False
    return True


async def _kv_read(env):
    namespace = getattr(env, "WORLD_CACHE_META", None)
    if namespace is None:
        return None
    now = int(Date.now())
    if now - int(_KV_READ_CACHE["readAt"]) < 60 * 1000:
        return _KV_READ_CACHE["value"]
    try:
        raw = await namespace.get(KV_SNAPSHOT_KEY)
        if raw is None:
            _KV_READ_CACHE.update({"readAt": now, "value": None})
            return None
        payload = json.loads(str(raw))
    except Exception:
        return None
    value = payload if isinstance(payload, dict) else None
    _KV_READ_CACHE.update({"readAt": now, "value": value})
    return value


async def handler(env, request, method_name, json_response, build_rev):
    if method_name(request) != "GET":
        return json_response(
            {"error": "method_not_allowed"},
            status=405,
            cache_control="no-store, max-age=0, must-revalidate",
            extra_headers={"allow": "GET"},
        )
    namespace = getattr(env, "WORLD_CACHE_META", None)
    global_sample = await _kv_read(env)
    return json_response(
        {
            "ok": True,
            "strategy": {
                "payloadLayer": "Cloudflare Cache API",
                "payloadScope": "public read-heavy responses only",
                "invalidation": "event deletes and state-addressed keys",
                "privateResponsesCached": False,
                "kvRole": "content-free global diagnostics snapshot only",
            },
            "cacheApi": {
                "enabled": True,
                "scope": "current edge isolate",
                "live": local_snapshot(build_rev),
            },
            "kv": {
                "binding": "WORLD_CACHE_META",
                "enabled": namespace is not None,
                "namespaceIdExposed": False,
                "keyCountUsed": 1 if namespace is not None else 0,
                "snapshotCadenceMinutes": KV_SNAPSHOT_MINUTES,
                "maximumScheduledWritesPerDay": (
                    24 * 60 // KV_SNAPSHOT_MINUTES if namespace is not None else 0
                ),
                "retentionHours": 24,
                "lastGlobalSample": global_sample,
            },
        },
        cache_control="no-store, max-age=0, must-revalidate",
        extra_headers={"x-content-type-options": "nosniff"},
    )
