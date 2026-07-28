import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
from urllib import request as urlrequest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "hosted_repository_sync.py"
SPEC = importlib.util.spec_from_file_location(
    "forkmesh_hosted_repository_sync_test",
    SCRIPT,
)
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)


def _import(name, suffix):
    return {
        "id": "ext_" + suffix * 24,
        "provider": "codeberg",
        "isPrivate": False,
        "status": "external_repository",
        "name": name,
        "originalUrl": "https://codeberg.org/m33/" + name,
        "createdAt": 1785000000000,
        "metadata": {"defaultBranch": "main", "description": name},
    }


def test_prepare_keeps_healthy_imports_when_one_provider_clone_fails(
    tmp_path, monkeypatch
):
    gateway = tmp_path / "gateway"
    gateway.mkdir()
    config = SimpleNamespace(
        node_owner="mirror2",
        gateway_config_path=gateway / "mirror-gateway.json",
    )
    monkeypatch.setattr(sync, "SOURCE_ROOT", tmp_path / "imports")
    monkeypatch.setattr(
        sync,
        "_fetch_imports",
        lambda: [_import("healthy", "a"), _import("unavailable", "b")],
    )

    def prepare_repository(_config, item):
        if item["name"] == "unavailable":
            raise sync.SyncError("hosted repository operation failed")
        return True

    monkeypatch.setattr(sync, "_prepare_repository", prepare_repository)
    monkeypatch.setattr(sync, "_refs_hash", lambda *_args: "c" * 64)

    result = sync.prepare(config)

    assert result == {
        "ok": True,
        "changed": True,
        "repositoryCount": 1,
        "removedCount": 0,
        "failedCount": 1,
    }
    sidecar = json.loads(
        (gateway / sync.SIDECAR_FILE).read_text(encoding="utf-8")
    )
    assert [item["name"] for item in sidecar["repositories"]] == ["healthy"]


def test_delete_catalog_identifies_the_headless_sync_client(monkeypatch):
    config = SimpleNamespace(
        worker_origin="https://forkmesh.com",
    )
    monkeypatch.setattr(
        sync.refresh,
        "_helper_call",
        lambda *_args: {"signature": "signed-delete"},
    )
    requests = []

    class Response:
        def __enter__(self):
            return self

        def __exit__(self, *_args):
            return None

        def read(self, _limit):
            return b'{"ok":true,"deleted":true}'

    def open_request(request, timeout):
        assert isinstance(request, urlrequest.Request)
        assert timeout == 30
        requests.append(request)
        return Response()

    monkeypatch.setattr(sync.urlrequest, "urlopen", open_request)

    sync._delete_catalog(config, "mirror2", "example")

    assert len(requests) == 1
    request = requests[0]
    assert request.method == "DELETE"
    assert request.get_header("Accept") == "application/json"
    assert request.get_header("User-agent") == "ForkMesh-hosted-import-sync/1.0"
    assert "owner=mirror2" in request.full_url
    assert "name=example" in request.full_url


def test_register_persists_catalog_deletion_progress(tmp_path, monkeypatch):
    gateway = tmp_path / "gateway"
    gateway.mkdir()
    config = SimpleNamespace(
        gateway_config_path=gateway / "mirror-gateway.json",
        node_owner="mirror2",
    )
    (gateway / sync.SIDECAR_FILE).write_text(
        '{"repositories":[]}\n',
        encoding="utf-8",
    )
    (gateway / sync.SIDECAR_FILE).chmod(0o600)
    pending_path = gateway / sync.PENDING_FILE
    pending_path.write_text(
        json.dumps(
            {
                "repositories": [
                    {"owner": "mirror2", "name": "first"},
                    {"owner": "mirror2", "name": "second"},
                    {"owner": "mirror2", "name": "third"},
                ]
            }
        ),
        encoding="utf-8",
    )
    pending_path.chmod(0o600)
    monkeypatch.setattr(sync.refresh, "_load_public_identity", lambda _config: {})
    deleted = []

    def delete_catalog(_config, _owner, name):
        deleted.append(name)
        if name == "second":
            raise sync.SyncError("hosted repository catalog deletion failed")

    monkeypatch.setattr(sync, "_delete_catalog", delete_catalog)

    try:
        sync.register(config)
    except sync.SyncError:
        pass
    else:
        raise AssertionError("the simulated second deletion must fail")

    assert deleted == ["first", "second"]
    pending = json.loads(pending_path.read_text(encoding="utf-8"))
    assert [item["name"] for item in pending["repositories"]] == [
        "second",
        "third",
    ]
