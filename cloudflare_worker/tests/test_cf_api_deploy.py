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
