import importlib.util
import os
from pathlib import Path
import subprocess


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

    assert deploy_targets.TARGETS == ("app", "world")
    assert "app/wrangler.toml" in manifests["app"]
    assert "app/edge-control/wrangler.toml" in manifests["app"]
    assert "app/edge-control/worker.js" in manifests["app"]
    assert "app/src/entry.py" in manifests["app"]
    assert "app/public/dashboard/repo.html" in manifests["app"]
    assert "world/wrangler.toml" in manifests["world"]
    assert "world/public/world/world.js" in manifests["world"]
    assert "app/src/entry.py" not in manifests["world"]
    assert "world/public/world/world.js" not in manifests["app"]
    assert "app/.env.production" not in deploy_targets.target_inputs("app")
    assert "app/pylock.toml" not in deploy_targets.target_inputs("app")


def test_shared_input_only_invalidates_consumers():
    assert "app/public/api-client.js" in manifest("app")
    assert "app/public/api-client.js" in manifest("world")
    assert "app/public/site-header.js" in manifest("world")
    assert "app/public/dashboard/repo.html" in manifest("app")
    assert "app/public/dashboard/repo.html" not in manifest("world")


def test_app_fingerprint_covers_staged_homepage_dependencies():
    # `/` redirects to the dashboard, so its document is the homepage.
    app = manifest("app")
    assert "app/public/dashboard/index.html" in app
    assert "app/public/_headers" in app
    assert "app/public/install.sh" in app


def test_deploy_state_marks_only_successful_target(tmp_path, monkeypatch):
    state_path = tmp_path / "deploy-targets.json"
    monkeypatch.setattr(deploy_targets, "STATE_PATH", state_path)

    assert deploy_targets.target_status("world")["changed"] is True
    marked = deploy_targets.mark_deployed("world", "abc123", state_path)
    assert marked["revision"] == "abc123"
    assert deploy_targets.target_status("world")["changed"] is False
    assert deploy_targets.target_status("app")["changed"] is True


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
    world = (REPOSITORY / "world/worker.js").read_text(encoding="utf-8")

    assert 'tools/deploy_targets.py fingerprint "$target"' in deploy
    assert '"deployFingerprint"' in entry
    assert "x-forkmesh-deploy-fingerprint" in world
    assert '--var "DEPLOY_FINGERPRINT:${DEPLOY_TARGET_FINGERPRINT}"' in deploy


def test_static_worker_skip_requires_matching_live_fingerprint(tmp_path):
    deploy = (REPOSITORY / "app/deploy.sh").read_text(encoding="utf-8")
    changed = deploy[deploy.index("deploy_target_is_changed() {"):deploy.index(
        "\n}\n\nmark_deploy_target()"
    ) + 2]
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    curl = fake_bin / "curl"
    curl.write_text(
        """#!/usr/bin/env bash
case "${LIVE_PROBE_MODE:-matching}" in
  matching) printf 'HTTP/2 200\\nx-forkmesh-deploy-fingerprint: %s\\n\\n' "$LIVE_FINGERPRINT" ;;
  stale) printf 'HTTP/2 200\\nx-forkmesh-deploy-fingerprint: %064d\\n\\n' 0 ;;
  missing) printf 'HTTP/2 404\\n\\n' ;;
  unreachable) exit 28 ;;
esac
""",
        encoding="utf-8",
    )
    curl.chmod(0o755)
    fingerprint = deploy_targets.fingerprint("world")
    script = changed + "\ndeploy_target_is_changed world; exit $?\n"
    base_env = {
        **os.environ,
        "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
        "LIVE_FINGERPRINT": fingerprint,
        "DEPLOY_VERIFY_WORLD_URL": "https://world.example",
    }

    matching = subprocess.run(
        ["bash", "-c", script],
        cwd=REPOSITORY / "app",
        env=base_env,
        check=False,
    )
    assert matching.returncode == 3

    for mode in ("stale", "missing", "unreachable"):
        attempted = subprocess.run(
            ["bash", "-c", script],
            cwd=REPOSITORY / "app",
            env={**base_env, "LIVE_PROBE_MODE": mode},
            check=False,
        )
        assert attempted.returncode == 0, mode


def test_static_worker_skip_does_not_trust_local_state_without_curl(tmp_path):
    deploy = (REPOSITORY / "app/deploy.sh").read_text(encoding="utf-8")
    changed = deploy[deploy.index("deploy_target_is_changed() {"):deploy.index(
        "\n}\n\nmark_deploy_target()"
    ) + 2]
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    (fake_bin / "python3").symlink_to(Path(os.environ.get("PYTHON", "/usr/bin/python3")))
    result = subprocess.run(
        ["/bin/bash", "-c", changed + "\ndeploy_target_is_changed world; exit $?\n"],
        cwd=REPOSITORY / "app",
        env={
            **os.environ,
            "PATH": str(fake_bin),
            "DEPLOY_VERIFY_WORLD_URL": "https://world.example",
        },
        check=False,
    )
    assert result.returncode == 0
