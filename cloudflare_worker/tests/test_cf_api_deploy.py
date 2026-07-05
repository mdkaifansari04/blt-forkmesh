import base64
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import cf_api_deploy


def test_asset_bucket_upload_uses_multipart_form_data(monkeypatch, tmp_path):
    public = tmp_path / "public"
    public.mkdir()
    asset = public / "file.txt"
    asset.write_text("hello", encoding="utf-8")

    digest = cf_api_deploy.asset_hash(asset, "file.txt")
    calls = []

    def fake_manifest(root):
        assert root == cf_api_deploy.ROOT / "public"
        return {"/file.txt": {"hash": digest, "size": 5}}, {digest: asset}

    def fake_api_json(method, path, token, payload):
        calls.append(("json", method, path, token, payload))
        return {"result": {"jwt": "upload-jwt", "buckets": [[digest]]}}

    def fake_api_request(method, path, token, *, body=None, content_type=None):
        calls.append(("request", method, path, token, body, content_type))
        return {"result": {"jwt": "complete-jwt"}}

    monkeypatch.setattr(cf_api_deploy, "build_asset_manifest", fake_manifest)
    monkeypatch.setattr(cf_api_deploy, "api_json", fake_api_json)
    monkeypatch.setattr(cf_api_deploy, "api_request", fake_api_request)

    assert cf_api_deploy.upload_assets("acct", "script", "api-token") == "complete-jwt"

    upload = calls[1]
    assert upload[0:4] == (
        "request",
        "POST",
        "/accounts/acct/workers/assets/upload?base64=true",
        "upload-jwt",
    )
    body = upload[4]
    content_type = upload[5]
    assert content_type.startswith("multipart/form-data; boundary=")
    assert f'name="{digest}"'.encode("ascii") in body
    assert base64.b64encode(b"hello") in body


def test_worker_upload_has_no_requirements_part():
    parts = cf_api_deploy.module_parts()

    assert parts
    assert all(content_type == "text/x-python" for _, content_type, _ in parts)
    assert all(name != "pylock.toml" for name, _, _ in parts)
    assert all(not name.startswith("src/") for name, _, _ in parts)
    assert "entry.py" in {name for name, _, _ in parts}


def test_worker_upload_omits_durable_object_migrations_by_default(monkeypatch):
    captured = {}

    monkeypatch.delenv("FORKMESH_API_DEPLOY_DO_MIGRATIONS", raising=False)
    monkeypatch.setattr(cf_api_deploy, "upload_assets", lambda *args: "asset-jwt")
    monkeypatch.setattr(cf_api_deploy, "apply_d1_migrations", lambda *args: None)
    monkeypatch.setattr(cf_api_deploy, "module_parts", lambda config=None: [])

    def fake_api_request(method, path, token, *, body=None, content_type=None):
        captured["body"] = body
        captured["content_type"] = content_type
        return {"result": {}}

    monkeypatch.setattr(cf_api_deploy, "api_request", fake_api_request)

    config = {
        "name": "forkmesh-relay",
        "main": "src/entry.py",
        "compatibility_date": "2026-06-14",
        "compatibility_flags": ["python_workers"],
        "assets": {"binding": "ASSETS", "run_worker_first": ["/api/*"]},
        "migrations": [{"tag": "v8", "deleted_classes": ["OldClass"]}],
    }
    cf_api_deploy.deploy_worker("acct", "token", config, {}, dry_run=False)

    assert b'"migrations"' not in captured["body"]
    assert b'"main_module": "entry.py"' in captured["body"]
