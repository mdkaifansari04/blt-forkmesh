#!/usr/bin/env python3
"""Deploy the ForkMesh Python Worker through Cloudflare's HTTP API.

This avoids Wrangler/npm entirely. It intentionally uses only the Python
standard library so the ForkMesh deploy runner can bootstrap from python3 plus
the CLOUDFLARE_* variables written by deploy.yml.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import mimetypes
import os
from pathlib import Path
import secrets
import sys
import tomllib
import urllib.error
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parent
API = "https://api.cloudflare.com/client/v4"
MIGRATION_TABLE = "forkmesh_d1_migrations"


class CloudflareError(RuntimeError):
    pass


def load_config() -> dict:
    with (ROOT / "wrangler.toml").open("rb") as fh:
        return tomllib.load(fh)


def api_headers(token: str, content_type: str | None = None) -> dict[str, str]:
    headers = {"Authorization": f"Bearer {token}"}
    if content_type:
        headers["Content-Type"] = content_type
    return headers


def api_request(
    method: str,
    path: str,
    token: str,
    *,
    body: bytes | None = None,
    content_type: str | None = None,
) -> dict:
    req = urllib.request.Request(
        API + path,
        data=body,
        method=method,
        headers=api_headers(token, content_type),
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        try:
            payload = json.loads(raw.decode("utf-8", errors="replace"))
        except Exception:
            payload = raw.decode("utf-8", errors="replace")
        raise CloudflareError(
            f"Cloudflare API {method} {path} failed with HTTP {exc.code}: {payload}"
        ) from exc

    payload = json.loads(raw.decode("utf-8"))
    if not payload.get("success", False):
        raise CloudflareError(
            f"Cloudflare API {method} {path} returned errors: {payload.get('errors')}"
        )
    return payload


def api_json(method: str, path: str, token: str, payload: dict) -> dict:
    return api_request(
        method,
        path,
        token,
        body=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        content_type="application/json",
    )


def d1_query(account_id: str, database_id: str, token: str, sql: str, params=None) -> dict:
    path = f"/accounts/{account_id}/d1/database/{database_id}/query"
    payload = {"sql": sql}
    if params is not None:
        payload["params"] = params
    return api_json("POST", path, token, payload)


def first_d1_database(config: dict) -> dict:
    databases = config.get("d1_databases") or []
    if not databases:
        raise CloudflareError("wrangler.toml has no [[d1_databases]] binding")
    db = databases[0]
    if db.get("database_id") == "REPLACE_WITH_D1_DATABASE_ID":
        raise CloudflareError("D1 database_id is still the placeholder value")
    return db


def split_sql(script: str) -> list[str]:
    statements: list[str] = []
    buf: list[str] = []
    in_single = False
    in_double = False
    in_line_comment = False
    in_block_comment = False
    i = 0
    while i < len(script):
        ch = script[i]
        nxt = script[i + 1] if i + 1 < len(script) else ""
        if in_line_comment:
            buf.append(ch)
            if ch == "\n":
                in_line_comment = False
            i += 1
            continue
        if in_block_comment:
            buf.append(ch)
            if ch == "*" and nxt == "/":
                buf.append(nxt)
                in_block_comment = False
                i += 2
            else:
                i += 1
            continue
        if not in_single and not in_double and ch == "-" and nxt == "-":
            buf.extend([ch, nxt])
            in_line_comment = True
            i += 2
            continue
        if not in_single and not in_double and ch == "/" and nxt == "*":
            buf.extend([ch, nxt])
            in_block_comment = True
            i += 2
            continue
        if ch == "'" and not in_double:
            in_single = not in_single
        elif ch == '"' and not in_single:
            in_double = not in_double
        if ch == ";" and not in_single and not in_double:
            statement = "".join(buf).strip()
            if statement:
                statements.append(statement)
            buf = []
        else:
            buf.append(ch)
        i += 1
    tail = "".join(buf).strip()
    if tail:
        statements.append(tail)
    return statements


def apply_d1_migrations(account_id: str, token: str, config: dict, dry_run: bool) -> None:
    db = first_d1_database(config)
    database_id = db["database_id"]
    migration_dir = ROOT / db.get("migrations_dir", "migrations")
    files = sorted(migration_dir.glob("*.sql"))
    if not files:
        print("cf_api_deploy: no D1 migration files found.")
        return
    if dry_run:
        print(f"cf_api_deploy: would apply D1 migrations from {migration_dir}.")
        return

    d1_query(
        account_id,
        database_id,
        token,
        f"CREATE TABLE IF NOT EXISTS {MIGRATION_TABLE} (name TEXT PRIMARY KEY, applied_at INTEGER NOT NULL)",
    )
    seen_payload = d1_query(
        account_id,
        database_id,
        token,
        f"SELECT name FROM {MIGRATION_TABLE}",
    )
    rows = (seen_payload.get("result") or [{}])[0].get("results") or []
    seen = {row.get("name") for row in rows}
    try:
        wrangler_payload = d1_query(account_id, database_id, token, "SELECT name FROM d1_migrations")
        wrangler_rows = (wrangler_payload.get("result") or [{}])[0].get("results") or []
        seen.update(row.get("name") for row in wrangler_rows)
    except CloudflareError:
        pass
    if not seen:
        schema_payload = d1_query(
            account_id,
            database_id,
            token,
            "SELECT name FROM sqlite_master WHERE type='table' AND name IN ('accounts', 'repositories')",
        )
        schema_rows = (schema_payload.get("result") or [{}])[0].get("results") or []
        if schema_rows and os.environ.get("FORKMESH_API_DEPLOY_FORCE_MIGRATIONS") != "1":
            raise CloudflareError(
                "D1 already has ForkMesh tables but no migration records were found; "
                "refusing to rerun migrations that include destructive setup. "
                "Set FORKMESH_API_DEPLOY_FORCE_MIGRATIONS=1 only if this database is disposable."
            )

    for path in files:
        if path.name in seen:
            continue
        print(f"cf_api_deploy: applying D1 migration {path.name}")
        sql = path.read_text(encoding="utf-8")
        for statement in split_sql(sql):
            d1_query(account_id, database_id, token, statement)
        d1_query(
            account_id,
            database_id,
            token,
            f"INSERT OR IGNORE INTO {MIGRATION_TABLE} (name, applied_at) VALUES (?, unixepoch())",
            [path.name],
        )
    print("cf_api_deploy: D1 migrations applied.")


def asset_hash(path: Path, relative: str) -> str:
    # Cloudflare's direct-upload docs hash base64(file) + extension and trim to
    # 32 hex chars; match that so the upload session accepts payload buckets.
    extension = relative.rsplit(".", 1)[1] if "." in relative else ""
    content = base64.b64encode(path.read_bytes()) + extension.encode("utf-8")
    return hashlib.sha256(content).hexdigest()[:32]


def build_asset_manifest(public_dir: Path) -> tuple[dict, dict[str, Path]]:
    manifest: dict[str, dict[str, int | str]] = {}
    by_hash: dict[str, Path] = {}
    for path in sorted(public_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(public_dir).as_posix()
        manifest_path = "/" + rel
        digest = asset_hash(path, rel)
        manifest[manifest_path] = {"hash": digest, "size": path.stat().st_size}
        by_hash[digest] = path
    if not manifest:
        raise CloudflareError(f"no static assets found in {public_dir}")
    return manifest, by_hash


def upload_assets(account_id: str, script_name: str, token: str) -> str:
    manifest, by_hash = build_asset_manifest(ROOT / "public")
    path = f"/accounts/{account_id}/workers/scripts/{script_name}/assets-upload-session"
    session = api_json("POST", path, token, {"manifest": manifest}).get("result") or {}
    upload_jwt = session.get("jwt")
    buckets = session.get("buckets")
    if upload_jwt is None or buckets is None:
        raise CloudflareError("asset upload session did not return buckets and jwt")

    completion_jwt = upload_jwt
    for index, bucket in enumerate(buckets, start=1):
        payload = {}
        for digest in bucket:
            source = by_hash.get(digest)
            if source is None:
                raise CloudflareError(f"asset upload bucket requested unknown hash {digest}")
            payload[digest] = base64.b64encode(source.read_bytes()).decode("ascii")
        if not payload:
            continue
        print(f"cf_api_deploy: uploading asset bucket {index}/{len(buckets)}")
        fields = [
            (digest, "text/plain", encoded.encode("ascii"), None)
            for digest, encoded in payload.items()
        ]
        body, content_type = multipart_body(fields)
        result = api_request(
            "POST",
            f"/accounts/{account_id}/workers/assets/upload?base64=true",
            upload_jwt,
            body=body,
            content_type=content_type,
        ).get("result") or {}
        completion_jwt = result.get("jwt") or completion_jwt
    print(f"cf_api_deploy: assets ready ({len(manifest)} file(s)).")
    return completion_jwt


def binding_metadata(config: dict, extra_vars: dict[str, str], assets_jwt: str) -> list[dict]:
    bindings: list[dict] = [{"type": "assets", "name": config["assets"]["binding"]}]
    for key, value in (config.get("vars") or {}).items():
        bindings.append({"type": "plain_text", "name": key, "text": str(value)})
    for key, value in extra_vars.items():
        bindings.append({"type": "plain_text", "name": key, "text": value})
    for db in config.get("d1_databases") or []:
        bindings.append({"type": "d1", "name": db["binding"], "id": db["database_id"]})
    for dob in config.get("durable_objects", {}).get("bindings") or []:
        bindings.append(
            {
                "type": "durable_object_namespace",
                "name": dob["name"],
                "class_name": dob["class_name"],
            }
        )
    return bindings


def module_root(config: dict | None = None) -> Path:
    main = (config or load_config()).get("main", "src/entry.py")
    return ROOT / Path(main).parent


def main_module_name(config: dict) -> str:
    return Path(config["main"]).name


def module_parts(config: dict | None = None) -> list[tuple[str, str, bytes]]:
    root = module_root(config)
    parts: list[tuple[str, str, bytes]] = []
    for path in sorted(root.glob("*.py")):
        rel = path.relative_to(root).as_posix()
        parts.append((rel, "text/x-python", path.read_bytes()))
    return parts


def multipart_body(fields: list[tuple[str, str, bytes, str | None]]) -> tuple[bytes, str]:
    boundary = "----forkmesh-" + secrets.token_hex(16)
    chunks: list[bytes] = []
    for name, content_type, data, filename in fields:
        chunks.append(f"--{boundary}\r\n".encode("ascii"))
        if filename is None:
            chunks.append(f'Content-Disposition: form-data; name="{name}"\r\n'.encode("utf-8"))
        else:
            chunks.append(
                f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'.encode(
                    "utf-8"
                )
            )
        chunks.append(f"Content-Type: {content_type}\r\n\r\n".encode("ascii"))
        chunks.append(data)
        chunks.append(b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode("ascii"))
    return b"".join(chunks), f"multipart/form-data; boundary={boundary}"


def deploy_worker(
    account_id: str,
    token: str,
    config: dict,
    extra_vars: dict[str, str],
    dry_run: bool,
) -> None:
    script_name = config["name"]
    if dry_run:
        manifest, _ = build_asset_manifest(ROOT / "public")
        print(f"cf_api_deploy: would upload {len(manifest)} asset(s).")
        print(f"cf_api_deploy: would upload {len(module_parts(config))} Python module part(s).")
        return

    assets_jwt = upload_assets(account_id, script_name, token)
    metadata = {
        "main_module": main_module_name(config),
        "compatibility_date": config["compatibility_date"],
        "compatibility_flags": config.get("compatibility_flags") or [],
        "bindings": binding_metadata(config, extra_vars, assets_jwt),
        "assets": {
            "jwt": assets_jwt,
            "html_handling": config["assets"].get("html_handling", "auto-trailing-slash"),
            "not_found_handling": config["assets"].get("not_found_handling", "none"),
            "run_worker_first": config["assets"].get("run_worker_first", []),
        },
        "observability": config.get("observability") or {},
    }
    migrations = config.get("migrations")
    if migrations and os.environ.get("FORKMESH_API_DEPLOY_DO_MIGRATIONS") == "1":
        metadata["migrations"] = {
            "new_tag": migrations[-1]["tag"],
            "steps": [
                {
                    k: v
                    for k, v in step.items()
                    if k
                    in {
                        "new_classes",
                        "new_sqlite_classes",
                        "deleted_classes",
                        "renamed_classes",
                        "transferred_classes",
                    }
                }
                for step in migrations
            ],
        }

    fields = [("metadata", "application/json", json.dumps(metadata).encode("utf-8"), None)]
    fields.extend((name, content_type, data, name) for name, content_type, data in module_parts(config))
    body, content_type = multipart_body(fields)
    print(f"cf_api_deploy: uploading Worker script {script_name}.")
    api_request(
        "PUT",
        f"/accounts/{account_id}/workers/scripts/{script_name}",
        token,
        body=body,
        content_type=content_type,
    )
    print("cf_api_deploy: Worker uploaded.")


def parse_vars(values: list[str]) -> dict[str, str]:
    parsed = {}
    for item in values:
        if ":" not in item:
            raise SystemExit(f"--var must be KEY:VALUE, got {item!r}")
        key, value = item.split(":", 1)
        parsed[key] = value
    return parsed


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--var", action="append", default=[])
    parser.add_argument("action", choices=["deploy", "migrate"])
    args = parser.parse_args(argv)

    token = os.environ.get("CLOUDFLARE_API_TOKEN")
    account_id = os.environ.get("CLOUDFLARE_ACCOUNT_ID")
    if not token or not account_id:
        raise SystemExit("CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID are required")

    config = load_config()
    extra_vars = parse_vars(args.var)
    if args.action == "migrate":
        apply_d1_migrations(account_id, token, config, args.dry_run)
    else:
        apply_d1_migrations(account_id, token, config, args.dry_run)
        deploy_worker(account_id, token, config, extra_vars, args.dry_run)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except CloudflareError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
