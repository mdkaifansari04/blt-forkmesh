import importlib.util
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
MODULE = REPOSITORY / "app/tools/deploy_targets.py"
SPEC = importlib.util.spec_from_file_location("deploy_targets", MODULE)
deploy_targets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deploy_targets)


def manifest(target):
    return {
        path.relative_to(REPOSITORY).as_posix()
        for path in deploy_targets.target_files(target)
    }


def test_each_worker_has_distinct_inputs():
    manifests = {target: manifest(target) for target in deploy_targets.TARGETS}

    assert deploy_targets.TARGETS == ("app", "world", "www")
    assert "app/wrangler.toml" in manifests["app"]
    assert "app/src/entry.py" in manifests["app"]
    assert "app/public/dashboard/repo.html" in manifests["app"]
    assert "www/wrangler.toml" in manifests["www"]
    assert "www/public/blog.html" in manifests["www"]
    assert "world/wrangler.toml" in manifests["world"]
    assert "world/public/world/world.js" in manifests["world"]
    assert "app/src/entry.py" not in manifests["world"]
    assert "app/src/entry.py" not in manifests["www"]
    assert "world/public/world/world.js" not in manifests["app"]
    assert "app/.env.production" not in deploy_targets.target_inputs("app")
    assert "app/pylock.toml" not in deploy_targets.target_inputs("app")


def test_shared_input_only_invalidates_consumers():
    assert "www/public/api-client.js" in manifest("app")
    assert "www/public/api-client.js" in manifest("www")
    assert "www/public/api-client.js" in manifest("world")
    assert "www/public/home-header-auth.js" in manifest("world")
    assert "www/public/blog/one-app-one-mesh/index.html" not in manifest("app")
    assert "www/public/blog/one-app-one-mesh/index.html" in manifest("www")


def test_app_fingerprint_covers_staged_homepage_dependencies():
    app = manifest("app")
    assert "www/public/home-header-auth.js" in app
    assert "www/public/assets/video/network.mp4" in app


def test_deploy_state_marks_only_successful_target(tmp_path, monkeypatch):
    state_path = tmp_path / "deploy-targets.json"
    monkeypatch.setattr(deploy_targets, "STATE_PATH", state_path)

    assert deploy_targets.target_status("world")["changed"] is True
    marked = deploy_targets.mark_deployed("world", "abc123", state_path)
    assert marked["revision"] == "abc123"
    assert deploy_targets.target_status("world")["changed"] is False
    assert deploy_targets.target_status("www")["changed"] is True


def test_fingerprint_changes_with_input(tmp_path, monkeypatch):
    source = tmp_path / "world.js"
    source.write_text("one", encoding="utf-8")
    monkeypatch.setattr(deploy_targets, "target_files", lambda target: [source])
    monkeypatch.setattr(deploy_targets, "REPOSITORY", tmp_path)

    first = deploy_targets.fingerprint("world")
    source.write_text("two", encoding="utf-8")
    assert deploy_targets.fingerprint("world") != first


def test_app_fingerprint_excludes_local_environment_secrets(tmp_path, monkeypatch):
    app = tmp_path / "app"
    app.mkdir()
    (app / "wrangler.toml").write_text("name = 'test'\n", encoding="utf-8")
    environment = app / ".env.production"
    environment.write_text("DATA_KEY=first-secret\n", encoding="utf-8")
    monkeypatch.setattr(deploy_targets, "REPOSITORY", tmp_path)

    first = deploy_targets.fingerprint("app")
    environment.write_text("DATA_KEY=second-secret\n", encoding="utf-8")

    assert deploy_targets.fingerprint("app") == first


def test_live_worker_fingerprint_is_the_cross_runner_source_of_truth():
    deploy = (REPOSITORY / "app/deploy.sh").read_text(encoding="utf-8")
    entry = (REPOSITORY / "app/src/entry.py").read_text(encoding="utf-8")
    www = (REPOSITORY / "www/worker.js").read_text(encoding="utf-8")
    world = (REPOSITORY / "world/worker.js").read_text(encoding="utf-8")

    assert 'tools/deploy_targets.py fingerprint "$target"' in deploy
    assert '"deployFingerprint"' in entry
    assert "x-forkmesh-deploy-fingerprint" in www
    assert "x-forkmesh-deploy-fingerprint" in world
    assert '--var "DEPLOY_FINGERPRINT:${DEPLOY_TARGET_FINGERPRINT}"' in deploy
