import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace


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
