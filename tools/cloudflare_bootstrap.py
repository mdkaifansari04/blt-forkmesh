#!/usr/bin/env python3
"""Provision an independent ForkMesh relay on Cloudflare.

The bootstrapper deliberately keeps the Cloudflare API token in process memory:
it reads the token from the environment, a terminal prompt, or stdin; passes it
to Wrangler through the child environment; and never writes it to a config or
env file.  The generated Wrangler config contains public resource identifiers
only and is removed in a ``finally`` block.

The operation is idempotent.  An existing D1 database, proxied DNS record, and
Worker route are reused when they already match the requested deployment.
"""

from __future__ import annotations

import argparse
import base64
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any, Callable, Iterable
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
WORKER_DIR = ROOT / "cloudflare_worker"
WRANGLER_TEMPLATE = WORKER_DIR / "wrangler.toml"
API_BASE = "https://api.cloudflare.com/client/v4"
NAME_RE = re.compile(r"^[a-z][a-z0-9-]{0,62}$")
ENV_NAME_RE = re.compile(r"^[A-Z][A-Z0-9_]{0,127}$")
RETRYABLE_HTTP = frozenset({408, 409, 425, 429, 500, 502, 503, 504})
MIRROR_MANIFEST_ASSET = "forkmesh-mirror.json"
MIRROR_MANIFEST_PATH = "/" + MIRROR_MANIFEST_ASSET


class BootstrapError(RuntimeError):
    """A safe-to-display bootstrap failure."""


def _toml_string(value: str) -> str:
    # JSON string syntax is valid TOML basic-string syntax for these values.
    return json.dumps(value, ensure_ascii=False)


def _canonical_json(value: Any) -> str:
    """ForkMesh JSON sort v1: UTF-8, sorted keys, compact separators."""

    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    )


def _decode_base64url(value: str) -> bytes:
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise BootstrapError("identity value is not unpadded base64url")
    try:
        return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))
    except ValueError as exc:
        raise BootstrapError("identity value is not valid base64url") from exc


def validate_ed25519_public_key(value: str) -> str:
    if len(_decode_base64url(value)) != 32:
        raise BootstrapError("mirror public key must contain 32 Ed25519 bytes")
    return value


def validate_ed25519_signature(value: str) -> str:
    if len(_decode_base64url(value)) != 64:
        raise BootstrapError("mirror signature must contain 64 Ed25519 bytes")
    return value


def mirror_manifest_payload(manifest: dict[str, Any]) -> bytes:
    """Return the exact bytes covered by the endpoint signature."""

    unsigned = {key: value for key, value in manifest.items() if key != "signature"}
    return _canonical_json(unsigned).encode("utf-8")


def unsigned_mirror_manifest(
    *,
    hostname: str,
    node_name: str,
    public_key: str,
    generated_at: str | None = None,
) -> dict[str, Any]:
    origin = f"https://{hostname}"
    return {
        "schemaVersion": 1,
        "type": "forkmesh.mirror-endpoint",
        "generatedAt": generated_at
        or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "node": {
            "name": node_name,
            "publicKey": validate_ed25519_public_key(public_key),
        },
        "endpoint": {
            "origin": origin,
            "healthUrl": origin + "/health",
            "manifestUrl": origin + MIRROR_MANIFEST_PATH,
            "repositoryUrlTemplate": origin + "/{owner}/{repository}",
            "transport": "direct-https",
            "mainProxyMode": "masked",
        },
        "dns": {
            "recordName": hostname,
            "proxied": True,
            "workerRoute": f"{hostname}/*",
        },
        "storage": {
            "repositoryBytesInD1": False,
            "repositoryByteOwner": "independent-mirror-host",
            "d1Purpose": [
                "service-discovery",
                "health-metadata",
                "routing-metadata",
                "signed-manifests",
            ],
        },
    }


class MirrorManifestSigner:
    """External Ed25519 signer adapter; private key material stays local."""

    def __init__(
        self,
        command: Iterable[str],
        *,
        runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    ) -> None:
        self.command = list(command)
        if not self.command or any(not part for part in self.command):
            raise BootstrapError(
                "a local manifest signer command is required unless "
                "--skip-mirror-manifest is used"
            )
        self._runner = runner

    def sign(
        self,
        manifest: dict[str, Any],
        *,
        hidden_env_names: Iterable[str] = (),
    ) -> dict[str, Any]:
        payload = mirror_manifest_payload(manifest)
        request = {
            "schemaVersion": 1,
            "type": "forkmesh.mirror-endpoint-signing-request",
            "algorithm": "Ed25519",
            "encoding": "base64url-no-padding",
            "canonicalization": "forkmesh-json-sort-v1",
            "publicKey": manifest["node"]["publicKey"],
            "payloadBase64": base64.urlsafe_b64encode(payload).decode().rstrip("="),
            "payloadSha256": hashlib.sha256(payload).hexdigest(),
        }
        env = os.environ.copy()
        for name in {
            "CLOUDFLARE_API_TOKEN",
            "CF_API_TOKEN",
            "CLOUDFLARE_TOKEN",
            "CF_TOKEN",
            *hidden_env_names,
        }:
            env.pop(name, None)
        try:
            completed = self._runner(
                self.command,
                input=_canonical_json(request) + "\n",
                text=True,
                capture_output=True,
                env=env,
                check=True,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            raise BootstrapError(
                "local mirror manifest signer failed; inspect its local logs"
            ) from exc
        try:
            response = json.loads(completed.stdout)
        except json.JSONDecodeError as exc:
            raise BootstrapError("local mirror manifest signer returned invalid JSON") from exc
        if not isinstance(response, dict):
            raise BootstrapError("local mirror manifest signer returned a non-object")
        if any(
            re.search(r"(?:private|secret|seed|mnemonic|keypair)", str(key), re.I)
            for key in response
        ):
            raise BootstrapError("local signer returned a prohibited private-key field")
        if response.get("publicKey") not in (None, manifest["node"]["publicKey"]):
            raise BootstrapError("local signer used a different node public key")
        signature = validate_ed25519_signature(str(response.get("signature") or ""))
        signed = dict(manifest)
        signed["signature"] = {
            "algorithm": "Ed25519",
            "encoding": "base64url-no-padding",
            "canonicalization": "forkmesh-json-sort-v1",
            "payloadSha256": request["payloadSha256"],
            "value": signature,
        }
        return signed


@contextmanager
def staged_public_assets(
    mirror_manifest: dict[str, Any] | None,
) -> Iterable[Path]:
    """Stage assets without writing the deployment manifest into the checkout."""

    if mirror_manifest is None:
        yield WORKER_DIR / "public"
        return
    with tempfile.TemporaryDirectory(prefix="forkmesh-bootstrap-assets-") as temp:
        destination = Path(temp) / "public"
        shutil.copytree(
            WORKER_DIR / "public",
            destination,
            copy_function=shutil.copy2,
        )
        (destination / MIRROR_MANIFEST_ASSET).write_text(
            json.dumps(mirror_manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        yield destination


def normalize_hostname(value: str) -> str:
    host = value.strip().rstrip(".").lower()
    if not host or "://" in host or "/" in host or "*" in host:
        raise BootstrapError("hostname must be a plain DNS name without a URL path")
    labels = host.split(".")
    if len(labels) < 2:
        raise BootstrapError("hostname must contain a DNS suffix")
    try:
        ascii_host = host.encode("idna").decode("ascii")
    except UnicodeError as exc:
        raise BootstrapError("hostname is not valid IDNA") from exc
    if len(ascii_host) > 253 or any(
        not label
        or len(label) > 63
        or label.startswith("-")
        or label.endswith("-")
        or not re.fullmatch(r"[a-z0-9-]+", label)
        for label in ascii_host.split(".")
    ):
        raise BootstrapError("hostname contains an invalid DNS label")
    return ascii_host


def validate_resource_name(value: str, label: str) -> str:
    cleaned = value.strip().lower()
    if not NAME_RE.fullmatch(cleaned):
        raise BootstrapError(
            f"{label} must start with a letter and contain only lowercase "
            "letters, digits, or hyphens (maximum 63 characters)"
        )
    return cleaned


def render_wrangler_config(
    source: str,
    *,
    worker_name: str,
    database_name: str,
    database_id: str,
    public_base_url: str,
    node_name: str,
    relay_label: str,
    main_relay_url: str,
    assets_directory: str | None = None,
) -> str:
    """Render a temporary production config without mutating wrangler.toml."""

    lines = source.splitlines()
    output: list[str] = []
    section = ""
    production_d1_seen = False
    vars_seen = False
    vars_values = {
        "NODE_NAME": node_name,
        "PUBLIC_BASE_URL": public_base_url,
        "RELAY_LABEL": relay_label,
        "MAIN_RELAY_URL": main_relay_url,
    }
    vars_written: set[str] = set()

    def flush_missing_vars() -> None:
        if section != "vars":
            return
        for key, value in vars_values.items():
            if key not in vars_written:
                output.append(f"{key} = {_toml_string(value)}")
                vars_written.add(key)

    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            flush_missing_vars()
            section = stripped.strip("[]")
            if stripped == "[[d1_databases]]":
                section = "d1_databases"
                if not production_d1_seen:
                    production_d1_seen = True
            elif stripped == "[vars]":
                section = "vars"
                vars_seen = True
            output.append(line)
            continue

        if index == 0 and re.match(r"^\s*name\s*=", line):
            output.append(f"name = {_toml_string(worker_name)}")
            continue
        if section == "build" and re.match(r"^\s*command\s*=", line):
            # Migrations are applied explicitly with this generated config.  The
            # repository's migrate.sh intentionally uses the checked-in config,
            # so invoking it from here could target the original deployment.
            output.append('command = "python3 tools/build_dashboard_assets.py"')
            continue
        if (
            section == "assets"
            and assets_directory
            and re.match(r"^\s*directory\s*=", line)
        ):
            output.append(f"directory = {_toml_string(assets_directory)}")
            continue
        if section == "vars":
            match = re.match(r"^(\s*)([A-Z][A-Z0-9_]*)\s*=", line)
            if match and match.group(2) in vars_values:
                key = match.group(2)
                output.append(f"{match.group(1)}{key} = {_toml_string(vars_values[key])}")
                vars_written.add(key)
                continue
        if section == "d1_databases" and production_d1_seen:
            if re.match(r"^\s*database_name\s*=", line):
                output.append(f'database_name = {_toml_string(database_name)}')
                continue
            if re.match(r"^\s*database_id\s*=", line):
                output.append(f'database_id = {_toml_string(database_id)}')
                continue
        output.append(line)

    flush_missing_vars()
    if not production_d1_seen:
        raise BootstrapError("wrangler template has no production D1 binding")
    if not vars_seen:
        raise BootstrapError("wrangler template has no [vars] section")
    rendered = "\n".join(output) + "\n"
    if "CLOUDFLARE_API_TOKEN" in rendered:
        raise BootstrapError("refusing to render an API token into Wrangler config")
    return rendered


class CloudflareAPI:
    """Small Cloudflare v4 client with bounded retry and token redaction."""

    def __init__(
        self,
        token: str,
        *,
        opener: Callable[..., Any] = urlopen,
        sleeper: Callable[[float], None] = time.sleep,
    ) -> None:
        self._token = token
        self._opener = opener
        self._sleep = sleeper

    def __repr__(self) -> str:  # pragma: no cover - defensive logging guard
        return "CloudflareAPI(token=<redacted>)"

    def request(
        self,
        method: str,
        path: str,
        *,
        query: dict[str, Any] | None = None,
        body: dict[str, Any] | None = None,
        attempts: int = 4,
    ) -> Any:
        suffix = ""
        if query:
            suffix = "?" + urlencode(
                [(key, value) for key, value in query.items() if value is not None]
            )
        url = API_BASE + path + suffix
        payload = None if body is None else json.dumps(body).encode("utf-8")
        headers = {
            "Authorization": f"Bearer {self._token}",
            "Accept": "application/json",
            "User-Agent": "forkmesh-cloudflare-bootstrap/1",
        }
        if payload is not None:
            headers["Content-Type"] = "application/json"

        for attempt in range(attempts):
            request = Request(url, data=payload, method=method, headers=headers)
            try:
                with self._opener(request, timeout=30) as response:
                    raw = response.read()
                    decoded = json.loads(raw.decode("utf-8")) if raw else {}
                    if isinstance(decoded, dict) and decoded.get("success") is False:
                        raise BootstrapError(
                            self._safe_api_error(decoded).replace(
                                self._token, "<redacted>"
                            )
                        )
                    return decoded.get("result") if isinstance(decoded, dict) else decoded
            except HTTPError as exc:
                retryable = exc.code in RETRYABLE_HTTP
                raw = exc.read().decode("utf-8", "replace")
                if retryable and attempt + 1 < attempts:
                    retry_after = exc.headers.get("Retry-After") if exc.headers else None
                    try:
                        delay = min(15.0, max(0.0, float(retry_after)))
                    except (TypeError, ValueError):
                        delay = min(8.0, 0.5 * (2**attempt))
                    self._sleep(delay)
                    continue
                message = f"Cloudflare API returned HTTP {exc.code}"
                try:
                    decoded = json.loads(raw)
                    message += ": " + self._safe_api_error(decoded)
                except (json.JSONDecodeError, TypeError):
                    pass
                raise BootstrapError(message.replace(self._token, "<redacted>")) from exc
            except URLError as exc:
                if attempt + 1 < attempts:
                    self._sleep(min(8.0, 0.5 * (2**attempt)))
                    continue
                raise BootstrapError(f"Cloudflare API connection failed: {exc.reason}") from exc
        raise BootstrapError("Cloudflare API retry budget exhausted")

    @staticmethod
    def _safe_api_error(payload: Any) -> str:
        errors = payload.get("errors", []) if isinstance(payload, dict) else []
        messages = [
            str(item.get("message", "")).strip()
            for item in errors
            if isinstance(item, dict) and item.get("message")
        ]
        return "; ".join(messages) or "request was rejected"

    def verify_token(self) -> None:
        try:
            result = self.request("GET", "/user/tokens/verify")
        except BootstrapError as error:
            # Account-owned API tokens are valid for the account/zone calls
            # this client makes, but the user-scoped verify endpoint rejects
            # them with HTTP 401 "Invalid API Token". Defer to the account
            # resolution that always follows, which still fails closed.
            if "HTTP 401" in str(error):
                return
            raise
        if not isinstance(result, dict) or result.get("status") != "active":
            raise BootstrapError("Cloudflare API token is not active")

    def resolve_account(self, requested_id: str = "") -> dict[str, Any]:
        if requested_id:
            result = self.request("GET", f"/accounts/{requested_id}")
            if not isinstance(result, dict):
                raise BootstrapError("Cloudflare account lookup returned no account")
            return result
        result = self.request("GET", "/accounts", query={"per_page": 50})
        accounts = result if isinstance(result, list) else []
        if len(accounts) != 1:
            raise BootstrapError(
                "token can access multiple or no accounts; pass --account-id explicitly"
            )
        return accounts[0]

    def resolve_zone(self, account_id: str, zone_name: str) -> dict[str, Any]:
        result = self.request(
            "GET",
            "/zones",
            query={"name": zone_name, "account.id": account_id, "per_page": 50},
        )
        zones = [
            zone
            for zone in (result if isinstance(result, list) else [])
            if isinstance(zone, dict) and zone.get("name") == zone_name
        ]
        if len(zones) != 1:
            raise BootstrapError(
                f"expected one accessible Cloudflare zone named {zone_name!r}"
            )
        return zones[0]

    def list_zones(self, account_id: str) -> list[dict[str, Any]]:
        """Return active zones visible to the scoped token.

        Automatic setup is intentionally fail-closed: it proceeds only when one
        account and one active zone are unambiguous. Operators with broader
        tokens can still select an account and zone in the advanced fields.
        """

        result = self.request(
            "GET",
            "/zones",
            query={
                "account.id": account_id,
                "status": "active",
                "per_page": 50,
            },
        )
        return [
            zone
            for zone in (result if isinstance(result, list) else [])
            if isinstance(zone, dict)
            and zone.get("id")
            and zone.get("name")
            and str(zone.get("status") or "active") == "active"
        ]

    def ensure_d1(self, account_id: str, database_name: str) -> tuple[str, bool]:
        result = self.request(
            "GET",
            f"/accounts/{account_id}/d1/database",
            query={"name": database_name, "per_page": 100},
        )
        for database in result if isinstance(result, list) else []:
            if isinstance(database, dict) and database.get("name") == database_name:
                database_id = str(database.get("uuid") or database.get("id") or "")
                if database_id:
                    return database_id, False
        created = self.request(
            "POST",
            f"/accounts/{account_id}/d1/database",
            body={"name": database_name},
        )
        database_id = str(
            (created or {}).get("uuid") or (created or {}).get("id") or ""
        )
        if not database_id:
            raise BootstrapError("Cloudflare created D1 without returning its id")
        return database_id, True

    def ensure_dns(
        self,
        zone_id: str,
        hostname: str,
        *,
        replace: bool,
    ) -> tuple[dict[str, Any], bool]:
        result = self.request(
            "GET",
            f"/zones/{zone_id}/dns_records",
            query={"name": hostname, "per_page": 100},
        )
        records = [item for item in result if isinstance(item, dict)] if isinstance(result, list) else []
        proxied = next((item for item in records if item.get("proxied") is True), None)
        if proxied:
            return proxied, False
        body = {
            "type": "AAAA",
            "name": hostname,
            # A Workers route only needs an orange-cloud record.  100:: is the
            # IPv6 discard-only prefix and never becomes repository storage.
            "content": "100::",
            "ttl": 1,
            "proxied": True,
            "comment": "ForkMesh relay route; origin traffic is handled by Workers",
        }
        if records:
            if not replace:
                raise BootstrapError(
                    f"DNS records already exist for {hostname} but are not proxied; "
                    "review them or rerun with --replace-dns"
                )
            record_id = str(records[0].get("id") or "")
            if not record_id:
                raise BootstrapError("existing DNS record has no id")
            updated = self.request(
                "PUT", f"/zones/{zone_id}/dns_records/{record_id}", body=body
            )
            return updated, True
        created = self.request("POST", f"/zones/{zone_id}/dns_records", body=body)
        return created, True

    def ensure_worker_route(
        self,
        zone_id: str,
        pattern: str,
        worker_name: str,
        *,
        replace: bool,
    ) -> tuple[dict[str, Any], bool]:
        result = self.request("GET", f"/zones/{zone_id}/workers/routes")
        routes = result if isinstance(result, list) else []
        existing = next(
            (
                item
                for item in routes
                if isinstance(item, dict) and item.get("pattern") == pattern
            ),
            None,
        )
        body = {"pattern": pattern, "script": worker_name}
        if existing and existing.get("script") == worker_name:
            return existing, False
        if existing:
            if not replace:
                raise BootstrapError(
                    f"Worker route {pattern} belongs to {existing.get('script')!r}; "
                    "review it or rerun with --replace-route"
                )
            route_id = str(existing.get("id") or "")
            if not route_id:
                raise BootstrapError("existing Worker route has no id")
            updated = self.request(
                "PUT", f"/zones/{zone_id}/workers/routes/{route_id}", body=body
            )
            return updated, True
        created = self.request("POST", f"/zones/{zone_id}/workers/routes", body=body)
        return created, True


class WranglerRunner:
    """Invoke the repository-pinned pywrangler wrapper without a shell token."""

    def __init__(self, worker_dir: Path = WORKER_DIR) -> None:
        self.worker_dir = worker_dir

    def build_assets(self, *, hidden_env_names: Iterable[str] = ()) -> None:
        env = os.environ.copy()
        for name in {
            "CLOUDFLARE_API_TOKEN",
            "CF_API_TOKEN",
            "CLOUDFLARE_TOKEN",
            "CF_TOKEN",
            *hidden_env_names,
        }:
            env.pop(name, None)
        try:
            subprocess.run(
                [sys.executable, "tools/build_dashboard_assets.py"],
                cwd=self.worker_dir,
                env=env,
                check=True,
            )
        except subprocess.CalledProcessError as exc:
            raise BootstrapError(
                f"dashboard asset build failed with exit code {exc.returncode}"
            ) from exc

    def run(
        self,
        arguments: Iterable[str],
        *,
        token: str,
        account_id: str,
        stdin_text: str | None = None,
        hidden_env_names: Iterable[str] = (),
    ) -> None:
        env = os.environ.copy()
        for name in hidden_env_names:
            env.pop(name, None)
        env["CLOUDFLARE_API_TOKEN"] = token
        env["CLOUDFLARE_ACCOUNT_ID"] = account_id
        command = [
            "bash",
            "-c",
            '. ./pywrangler.sh; pywrangler "$@"',
            "forkmesh-bootstrap",
            *list(arguments),
        ]
        try:
            subprocess.run(
                command,
                cwd=self.worker_dir,
                env=env,
                input=stdin_text,
                text=True,
                check=True,
            )
        except subprocess.CalledProcessError as exc:
            raise BootstrapError(
                f"Wrangler command failed with exit code {exc.returncode}"
            ) from exc


@dataclass(frozen=True)
class BootstrapOptions:
    hostname: str
    zone_name: str
    worker_name: str
    database_name: str
    node_name: str
    relay_label: str
    main_relay_url: str
    account_id: str = ""
    replace_dns: bool = False
    replace_route: bool = False
    dry_run: bool = False
    secret_env: tuple[str, ...] = ()
    verify_health: bool = True
    publish_mirror_manifest: bool = True
    mirror_public_key: str = ""
    manifest_signer_command: tuple[str, ...] = ()


def _validate_secret_names(names: Iterable[str]) -> tuple[str, ...]:
    result: list[str] = []
    for raw in names:
        name = raw.strip()
        if not ENV_NAME_RE.fullmatch(name):
            raise BootstrapError(f"invalid secret environment variable name: {raw!r}")
        if name in {"CLOUDFLARE_API_TOKEN", "CLOUDFLARE_ACCOUNT_ID"}:
            raise BootstrapError(f"{name} configures deployment and is not a Worker secret")
        if name not in result:
            result.append(name)
    return tuple(result)


def _health_check(hostname: str, attempts: int = 6) -> None:
    url = f"https://{hostname}/health"
    for attempt in range(attempts):
        try:
            request = Request(
                url,
                headers={
                    "Accept": "application/json",
                    "User-Agent": "forkmesh-cloudflare-bootstrap/1",
                },
            )
            with urlopen(request, timeout=30) as response:
                if 200 <= response.status < 300:
                    return
        except (HTTPError, URLError, TimeoutError):
            pass
        if attempt + 1 < attempts:
            time.sleep(min(10, 1 + attempt * 2))
    raise BootstrapError(f"deployed relay did not pass its health check at {url}")


def _announce_join(hostname: str, attempts: int = 3) -> str:
    """Launch-time join ping (adhoc #97): ask the fresh instance to register.

    POSTing the new instance's own /api/federation/announce makes it send its
    signed registration to its configured main relay right now, so its request
    to join (and the approve prompt in the operator's desktop) appears the
    moment it launches instead of on the first staggered cron. Best effort:
    returns the reported join status ("pending"/"approved") or "" on failure —
    the instance's scheduled task re-registers later either way.
    """
    url = f"https://{hostname}/api/federation/announce"
    for attempt in range(attempts):
        try:
            request = Request(
                url,
                data=b"{}",
                headers={
                    "Accept": "application/json",
                    "Content-Type": "application/json",
                    "User-Agent": "forkmesh-cloudflare-bootstrap/1",
                },
                method="POST",
            )
            with urlopen(request, timeout=30) as response:
                if 200 <= response.status < 300:
                    payload = json.loads(response.read().decode("utf-8") or "{}")
                    return str(payload.get("status") or "pending")
        except (HTTPError, URLError, TimeoutError, ValueError):
            pass
        if attempt + 1 < attempts:
            time.sleep(min(10, 1 + attempt * 2))
    return ""


def bootstrap(
    options: BootstrapOptions,
    *,
    token: str,
    api: CloudflareAPI | None = None,
    runner: WranglerRunner | None = None,
    manifest_signer: MirrorManifestSigner | None = None,
    asset_stager: Callable[
        [dict[str, Any] | None], Any
    ] = staged_public_assets,
    health_check: Callable[[str], None] = _health_check,
    announce: Callable[[str], str] = _announce_join,
    output: Callable[[str], None] = print,
) -> dict[str, Any]:
    """Execute one idempotent relay bootstrap and return a redacted summary."""

    hostname = normalize_hostname(options.hostname)
    zone_name = normalize_hostname(options.zone_name)
    if hostname != zone_name and not hostname.endswith("." + zone_name):
        raise BootstrapError(f"{hostname} is not inside Cloudflare zone {zone_name}")
    worker_name = validate_resource_name(options.worker_name, "worker name")
    database_name = validate_resource_name(options.database_name, "database name")
    node_name = validate_resource_name(options.node_name, "node name")
    secret_names = _validate_secret_names(options.secret_env)
    secret_values: dict[str, str] = {}
    for name in secret_names:
        value = os.environ.get(name, "")
        if not value:
            raise BootstrapError(
                f"--secret-env {name} was requested but {name} is unset or empty"
            )
        secret_values[name] = value
    if not token.strip():
        raise BootstrapError("Cloudflare API token is empty")
    if options.main_relay_url and not options.main_relay_url.startswith("https://"):
        raise BootstrapError("--main-relay-url must use https://")
    relay_label = options.relay_label.strip() or node_name
    if len(relay_label) > 80 or any(ord(character) < 32 for character in relay_label):
        raise BootstrapError("relay label must be at most 80 printable characters")

    api = api or CloudflareAPI(token)
    runner = runner or WranglerRunner()
    api.verify_token()
    account = api.resolve_account(options.account_id)
    account_id = str(account.get("id") or "")
    if not account_id:
        raise BootstrapError("Cloudflare account has no id")
    zone = api.resolve_zone(account_id, zone_name)
    zone_id = str(zone.get("id") or "")
    if not zone_id:
        raise BootstrapError("Cloudflare zone has no id")

    if options.dry_run:
        output(
            f"Dry run: would deploy {worker_name} with D1 {database_name}, "
            f"proxied DNS {hostname}, and route {hostname}/*."
        )
        return {
            "ok": True,
            "dryRun": True,
            "accountId": account_id,
            "zoneId": zone_id,
            "hostname": hostname,
            "workerName": worker_name,
            "databaseName": database_name,
            "secrets": list(secret_names),
            "mirrorManifest": {
                "published": options.publish_mirror_manifest,
                "path": MIRROR_MANIFEST_PATH
                if options.publish_mirror_manifest
                else None,
            },
        }

    mirror_manifest: dict[str, Any] | None = None
    if options.publish_mirror_manifest:
        public_key = validate_ed25519_public_key(options.mirror_public_key)
        signer = manifest_signer or MirrorManifestSigner(
            options.manifest_signer_command
        )
        mirror_manifest = signer.sign(
            unsigned_mirror_manifest(
                hostname=hostname,
                node_name=node_name,
                public_key=public_key,
            ),
            hidden_env_names=secret_names,
        )

    database_id, database_created = api.ensure_d1(account_id, database_name)
    runner.build_assets(hidden_env_names=secret_names)
    with asset_stager(mirror_manifest) as assets_directory:
        template = WRANGLER_TEMPLATE.read_text(encoding="utf-8")
        rendered = render_wrangler_config(
            template,
            worker_name=worker_name,
            database_name=database_name,
            database_id=database_id,
            public_base_url=f"https://{hostname}",
            node_name=node_name,
            relay_label=relay_label,
            main_relay_url=options.main_relay_url.rstrip("/"),
            assets_directory=str(Path(assets_directory).resolve()),
        )

        config_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                prefix=".forkmesh-bootstrap-",
                suffix=".toml",
                dir=WORKER_DIR,
                delete=False,
            ) as handle:
                handle.write(rendered)
                config_path = Path(handle.name)
            config_path.chmod(0o600)
            relative_config = config_path.name

            output(f"Applying ForkMesh migrations to D1 {database_name}...")
            runner.run(
                [
                    "d1",
                    "migrations",
                    "apply",
                    database_name,
                    "--remote",
                    "--config",
                    relative_config,
                    "--env",
                    "",
                ],
                token=token,
                account_id=account_id,
                hidden_env_names=secret_names,
            )
            output(f"Deploying Worker {worker_name}...")
            runner.run(
                ["deploy", "--config", relative_config, "--env", ""],
                token=token,
                account_id=account_id,
                hidden_env_names=secret_names,
            )
            for name, value in secret_values.items():
                output(f"Setting Worker secret {name} from the local environment...")
                runner.run(
                    [
                        "secret",
                        "put",
                        "--config",
                        relative_config,
                        "--env",
                        "",
                        name,
                    ],
                    token=token,
                    account_id=account_id,
                    stdin_text=value + "\n",
                    hidden_env_names=secret_names,
                )
        finally:
            if config_path is not None:
                config_path.unlink(missing_ok=True)

    dns, dns_changed = api.ensure_dns(
        zone_id, hostname, replace=options.replace_dns
    )
    route, route_changed = api.ensure_worker_route(
        zone_id,
        f"{hostname}/*",
        worker_name,
        replace=options.replace_route,
    )
    if options.verify_health:
        health_check(hostname)
    join_status = ""
    if options.main_relay_url.strip():
        # The launched instance pings its main relay as a request to join
        # (adhoc #97); the operator approves it from the desktop's red-dot
        # prompt, which links it into the federation and the World.
        join_status = announce(hostname)
        output(
            f"Join request sent to {options.main_relay_url.rstrip('/')} "
            f"(status: {join_status}). Approve it from the main instance to "
            "link this World into the federation."
            if join_status
            else "Launch join ping did not reach the main relay; the "
                 "instance's scheduled task will register it later."
        )

    summary = {
        "ok": True,
        "dryRun": False,
        "hostname": hostname,
        "url": f"https://{hostname}",
        "accountId": account_id,
        "zoneId": zone_id,
        "workerName": worker_name,
        "databaseName": database_name,
        "databaseId": database_id,
        "databaseCreated": database_created,
        "dnsRecordId": str((dns or {}).get("id") or ""),
        "dnsChanged": dns_changed,
        "routeId": str((route or {}).get("id") or ""),
        "routeChanged": route_changed,
        "mainRelayUrl": options.main_relay_url.rstrip("/"),
        "joinRequest": {
            "announced": bool(join_status),
            "status": join_status,
        },
        "workerSecretsSet": list(secret_names),
        "mirrorManifest": {
            "published": mirror_manifest is not None,
            "path": MIRROR_MANIFEST_PATH if mirror_manifest is not None else None,
            "url": (
                f"https://{hostname}{MIRROR_MANIFEST_PATH}"
                if mirror_manifest is not None
                else None
            ),
            "schema": "docs/mirror-endpoint.schema.json",
            "payloadSha256": (
                mirror_manifest["signature"]["payloadSha256"]
                if mirror_manifest is not None
                else None
            ),
            "repositoryBytesInD1": False,
        },
    }
    output(
        f"ForkMesh relay ready at https://{hostname}. "
        "The Cloudflare API token was not persisted."
    )
    return summary


def _read_token(stdin: Any = sys.stdin) -> str:
    token = os.environ.get("CLOUDFLARE_API_TOKEN", "").strip()
    if token:
        return token
    if stdin.isatty():
        return getpass.getpass("Cloudflare API token (not stored): ").strip()
    return stdin.readline().strip()


def _derived_resource_name(hostname: str) -> str:
    digest = hashlib.sha256(normalize_hostname(hostname).encode("ascii")).hexdigest()
    return "forkmesh-" + digest[:12]


def discover_deployment_defaults(
    *,
    token: str,
    api: CloudflareAPI | None = None,
    account_id: str = "",
    zone_name: str = "",
    hostname: str = "",
) -> dict[str, str]:
    """Resolve an unambiguous account/zone and safe idempotent hostnames.

    This is the token-only desktop path. It never mutates Cloudflare state and
    never returns the token. A token spanning multiple accounts or zones must be
    narrowed by the operator rather than guessing a deployment target.
    """

    if not token.strip():
        raise BootstrapError("Cloudflare API token is empty")
    client = api or CloudflareAPI(token)
    client.verify_token()
    account = client.resolve_account(account_id)
    resolved_account_id = str(account.get("id") or "")
    if not resolved_account_id:
        raise BootstrapError("Cloudflare account has no id")

    requested_zone = normalize_hostname(zone_name) if zone_name.strip() else ""
    requested_hostname = normalize_hostname(hostname) if hostname.strip() else ""
    if requested_zone:
        zone = client.resolve_zone(resolved_account_id, requested_zone)
    else:
        zones = client.list_zones(resolved_account_id)
        if requested_hostname:
            zones = [
                candidate
                for candidate in zones
                if requested_hostname == normalize_hostname(str(candidate["name"]))
                or requested_hostname.endswith(
                    "." + normalize_hostname(str(candidate["name"]))
                )
            ]
        if len(zones) != 1:
            raise BootstrapError(
                "token can access multiple or no active zones; select a "
                "Cloudflare zone in Advanced settings"
            )
        zone = zones[0]

    resolved_zone_name = normalize_hostname(str(zone.get("name") or ""))
    resolved_zone_id = str(zone.get("id") or "")
    if not resolved_zone_id:
        raise BootstrapError("Cloudflare zone has no id")
    resolved_hostname = (
        requested_hostname or normalize_hostname(f"forkmesh.{resolved_zone_name}")
    )
    if (
        resolved_hostname != resolved_zone_name
        and not resolved_hostname.endswith("." + resolved_zone_name)
    ):
        raise BootstrapError(
            f"{resolved_hostname} is not inside Cloudflare zone "
            f"{resolved_zone_name}"
        )
    direct_hostname = normalize_hostname(f"mirror.{resolved_zone_name}")
    if direct_hostname == resolved_hostname:
        direct_hostname = normalize_hostname(f"mirror-node.{resolved_zone_name}")
    node_name = _derived_resource_name(resolved_hostname)
    return {
        "accountId": resolved_account_id,
        "zoneId": resolved_zone_id,
        "zoneName": resolved_zone_name,
        "hostname": resolved_hostname,
        "directMirrorHostname": direct_hostname,
        "nodeName": node_name,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Create/reuse D1, deploy ForkMesh, and attach proxied DNS plus a "
            "Worker route without saving the Cloudflare API token."
        )
    )
    parser.add_argument(
        "--hostname",
        default="",
        help=(
            "public relay hostname; omitted with --auto-configure to derive "
            "forkmesh.<the single accessible zone>"
        ),
    )
    parser.add_argument(
        "--zone",
        dest="zone_name",
        default="",
        help="Cloudflare zone containing the hostname",
    )
    parser.add_argument(
        "--auto-configure",
        action="store_true",
        help=(
            "discover one unambiguous account and active zone from the scoped "
            "token, then derive relay, mirror, D1, and Worker names"
        ),
    )
    parser.add_argument(
        "--account-id", default=os.environ.get("CLOUDFLARE_ACCOUNT_ID", "")
    )
    parser.add_argument(
        "--worker-name",
        default="",
        help="defaults to a stable name derived from the public hostname",
    )
    parser.add_argument(
        "--database-name",
        default="",
        help="defaults to the derived Worker name",
    )
    parser.add_argument(
        "--node-name",
        default="",
        help="defaults to the derived Worker name",
    )
    parser.add_argument("--relay-label", default="")
    parser.add_argument(
        "--main-relay-url",
        default="https://forkmesh.com",
        help="upstream relay mesh URL; pass an empty string for a main relay",
    )
    parser.add_argument("--replace-dns", action="store_true")
    parser.add_argument("--replace-route", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--secret-env",
        action="append",
        default=[],
        metavar="NAME",
        help=(
            "read NAME from the local environment and pipe it to Wrangler secret "
            "put; may be repeated"
        ),
    )
    parser.add_argument(
        "--mirror-public-key",
        default=os.environ.get("FORKMESH_NODE_PUBLIC_KEY", ""),
        help=(
            "base64url Ed25519 node public key; defaults to "
            "FORKMESH_NODE_PUBLIC_KEY"
        ),
    )
    parser.add_argument(
        "--manifest-signer-command",
        default=os.environ.get("FORKMESH_MIRROR_MANIFEST_SIGNER", ""),
        help=(
            "local signer executable and arguments; receives public signing JSON "
            "on stdin and returns a base64url Ed25519 signature"
        ),
    )
    parser.add_argument(
        "--skip-mirror-manifest",
        action="store_true",
        help="deploy a relay only, without advertising it as a trusted HTTPS mirror",
    )
    parser.add_argument("--skip-health-check", action="store_true")
    parser.add_argument(
        "--json-output",
        type=Path,
        help="write a non-secret deployment summary to this path",
    )
    parser.add_argument(
        "--json-stdout",
        action="store_true",
        help=(
            "emit the non-secret result as a FORKMESH_BOOTSTRAP_RESULT line "
            "for the desktop client"
        ),
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    token = _read_token()
    try:
        api = CloudflareAPI(token)
        discovered: dict[str, str] | None = None
        if args.auto_configure or not args.hostname or not args.zone_name:
            if not args.auto_configure and (not args.hostname or not args.zone_name):
                raise BootstrapError(
                    "--hostname and --zone are required unless --auto-configure "
                    "is used"
                )
            discovered = discover_deployment_defaults(
                token=token,
                api=api,
                account_id=args.account_id,
                zone_name=args.zone_name,
                hostname=args.hostname,
            )
            hostname = discovered["hostname"]
            zone_name = discovered["zoneName"]
            account_id = discovered["accountId"]
        else:
            hostname = args.hostname
            zone_name = args.zone_name
            account_id = args.account_id
        derived_name = _derived_resource_name(hostname)
        signer_command = tuple(shlex.split(args.manifest_signer_command))
        options = BootstrapOptions(
            hostname=hostname,
            zone_name=zone_name,
            worker_name=args.worker_name or derived_name,
            database_name=args.database_name or args.worker_name or derived_name,
            node_name=args.node_name or args.worker_name or derived_name,
            relay_label=args.relay_label,
            main_relay_url=args.main_relay_url,
            account_id=account_id,
            replace_dns=args.replace_dns,
            replace_route=args.replace_route,
            dry_run=args.dry_run,
            secret_env=tuple(args.secret_env),
            verify_health=not args.skip_health_check,
            publish_mirror_manifest=not args.skip_mirror_manifest,
            mirror_public_key=args.mirror_public_key,
            manifest_signer_command=signer_command,
        )
        result = bootstrap(options, token=token, api=api)
        result["autoConfigured"] = discovered is not None
        result["zoneName"] = zone_name
        result["nodeName"] = options.node_name
        result["directMirrorHostname"] = (
            discovered["directMirrorHostname"]
            if discovered is not None
            else normalize_hostname(f"mirror.{zone_name}")
        )
        if args.json_output:
            args.json_output.parent.mkdir(parents=True, exist_ok=True)
            args.json_output.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
        if args.json_stdout:
            print(
                "FORKMESH_BOOTSTRAP_RESULT="
                + base64.urlsafe_b64encode(
                    _canonical_json(result).encode("utf-8")
                )
                .decode("ascii")
                .rstrip("=")
            )
        return 0
    except (BootstrapError, OSError, ValueError) as exc:
        safe = str(exc).replace(token, "<redacted>") if token else str(exc)
        print(f"bootstrap failed: {safe}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
