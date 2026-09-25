#!/usr/bin/env python3

import json
import os
import sys
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, Request, build_opener


APP = os.environ.get("DEPLOY_VERIFY_URL", "https://app.forkmesh.com").rstrip("/")
WORLD = os.environ.get("DEPLOY_VERIFY_WORLD_URL", "https://world.forkmesh.com").rstrip("/")
LEGACY = os.environ.get(
    "DEPLOY_VERIFY_LEGACY_API_URL", "https://api.forkmesh.com"
).rstrip("/")


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, file_pointer, code, message, headers, url):
        return None


OPENER = build_opener(NoRedirect)


def request(url, *, method="GET", headers=None, body=None):
    request_headers = {
        "User-Agent": "forkmesh-deploy-verify/1.0",
        "Accept": "*/*",
    }
    request_headers.update(headers or {})
    item = Request(url, data=body, method=method, headers=request_headers)
    try:
        with OPENER.open(item, timeout=25) as response:
            return response.status, dict(response.headers.items()), response.read()
    except HTTPError as error:
        return error.code, dict(error.headers.items()), error.read()
    except URLError as error:
        raise RuntimeError(f"{url}: {error.reason}") from error


def header(headers, name):
    wanted = name.lower()
    return next((value for key, value in headers.items() if key.lower() == wanted), "")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    status, _, body = request(APP + "/api/version")
    version = json.loads(body.decode("utf-8"))
    require(status == 200 and version.get("worker") == "app", "App version probe failed")

    status, headers, _ = request(WORLD + "/", method="HEAD")
    require(status == 200, f"world root returned {status}")
    require(header(headers, "x-forkmesh-worker") == "world", "world ownership marker missing")

    status, _, body = request(LEGACY + "/api/version")
    legacy_version = json.loads(body.decode("utf-8"))
    require(status == 200 and legacy_version.get("worker") == "app", "legacy API alias is not App")
    status, headers, _ = request(LEGACY + "/", method="HEAD")
    require(status == 308 and header(headers, "location") == APP + "/", "legacy canonical redirect failed")

    status, headers, _ = request(
        APP + "/api/version",
        method="OPTIONS",
        headers={
            "origin": WORLD,
            "access-control-request-method": "GET",
            "access-control-request-headers": "authorization,content-type",
        },
    )
    require(status == 204, "allowed CORS preflight failed")
    require(header(headers, "access-control-allow-origin") == WORLD, "allowed CORS origin was not reflected")

    _, headers, _ = request(
        APP + "/api/version",
        method="OPTIONS",
        headers={
            "origin": "https://untrusted.invalid",
            "access-control-request-method": "GET",
        },
    )
    require(not header(headers, "access-control-allow-origin"), "untrusted CORS origin was allowed")

    status, headers, _ = request(
        APP + "/api/world/ws",
        headers={"connection": "Upgrade", "upgrade": "websocket"},
    )
    require(status in (400, 401, 403, 426), f"WebSocket boundary returned unexpected {status} via {APP}")
    require(not header(headers, "location"), f"WebSocket boundary redirected via {APP}")

    print("Split deployment verification passed: App/API alias, World, CORS, and WebSocket boundaries.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
