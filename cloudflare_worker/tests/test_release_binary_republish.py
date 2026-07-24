"""Safety contract for deploy.sh's explicit same-version binary refresh."""

import hashlib
import json
from pathlib import Path
import subprocess


DEPLOY = Path(__file__).resolve().parents[1] / "deploy.sh"
PUBLISH = DEPLOY.parents[1] / "tools" / "forkmesh-release-publish.sh"
WORKFLOW = DEPLOY.parents[1] / ".forkmesh" / "release.yml"


def _source() -> str:
    return DEPLOY.read_text(encoding="utf-8")


def _publish_function() -> str:
    source = _source()
    start = source.index("publish_release_binary() {")
    end = source.index("\n}\n\n# Commit release metadata", start)
    return source[start:end]


def test_deploy_script_has_valid_shell_syntax():
    result = subprocess.run(
        ["bash", "-n", str(DEPLOY)],
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr


def test_normal_deploy_skips_but_explicit_command_forces_existing_asset():
    source = _source()
    function = _publish_function()

    assert 'local force="${1:-0}"' in function
    assert 'if [ "$force" != "1" ]; then' in function
    assert "publish_release_binary 0" in source
    assert "republish-release-binary)" in source
    assert "publish_release_binary 1" in source
    assert source.index("republish-release-binary)") < source.index(
        "publish_release_binary 1", source.index("republish-release-binary)")
    )


def test_forced_refresh_is_clean_binary_only_and_served_cas_bound():
    function = _publish_function()

    cas_guard = (
        'if [ -z "${FORKMESH_RELEASE_CAS:-}" ]; then'
    )
    clean_guard = "git -C .. status --porcelain=v1"
    configure = "cmake -S ../qt_client -B ../qt_client/build-release"
    assert cas_guard in function
    assert clean_guard in function
    assert "--untracked-files=all" in function
    assert "could not verify that the release source worktree is clean" in function
    assert function.index(cas_guard) < function.index(configure)
    assert function.index(clean_guard) < function.index(configure)

    assert '-DFORKMESH_BUILD_TESTS=OFF' in function
    assert '-DFORKMESH_VERSION_OVERRIDE="$release_version"' in function
    assert '-DFORKMESH_BUILD_COMMIT_OVERRIDE="$build_commit"' in function
    assert ":(exclude).forkmesh/releases/**" in function
    assert "git -C .. log -1 --format=%H" in function
    assert '"$built" --version' in function
    assert '"$built" --build-commit' in function
    assert '"ForkMesh ${release_version}"' in function
    assert '[ "$reported_commit" != "$build_commit" ]' in function


def test_refresh_publishes_from_repo_root_and_verifies_revision_hash_and_cas():
    function = _publish_function()

    assert "cd .. && tools/forkmesh-release-publish.sh" in function
    assert 'publish_args+=("cloudflare_worker/$asset")' in function
    assert '--tag-commit "$tag_commit"' in function
    assert '--build-commit "$build_commit"' in function
    assert '\\"tag_commit\\": \\"$tag_commit\\"' in function
    assert '\\"build_commit\\": \\"$build_commit\\"' in function
    assert '\\"name\\":\\"$asset\\",\\"blob_sha256\\":\\"$asset_hash\\"' in function
    assert (
        '$cas_dir/sha256/${asset_hash:0:2}/$asset_hash/data'
        in function
    )
    assert "git add ../.forkmesh/releases/latest/SHASUMS256.txt" in function


def test_publisher_records_explicit_binary_source_commit(tmp_path):
    artifact = tmp_path / "forkmesh-linux-x86_64"
    payload = b"\x7fELF commit-bound release test"
    artifact.write_bytes(payload)
    tag_commit = "d4" * 20
    build_commit = "e5" * 20
    result = subprocess.run(
        [
            str(PUBLISH),
            "--channel",
            "latest",
            "--tag",
            "v0.7.0",
            "--tag-commit",
            tag_commit,
            "--build-commit",
            build_commit,
            "--repo",
            "forkmesh/forkmesh",
            "--cas-dir",
            str(tmp_path / "cas"),
            str(artifact),
        ],
        cwd=tmp_path,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    manifest = json.loads(
        (tmp_path / ".forkmesh/releases/latest/release.json").read_text()
    )
    assert manifest["tag_commit"] == tag_commit
    assert manifest["build_commit"] == build_commit
    assert manifest["assets"][0]["blob_sha256"] == hashlib.sha256(payload).hexdigest()


def test_publisher_rejects_unprovable_source_commit(tmp_path):
    artifact = tmp_path / "forkmesh-linux-x86_64"
    artifact.write_bytes(b"\x7fELF invalid provenance")
    result = subprocess.run(
        [
            str(PUBLISH),
            "--tag",
            "v0.7.0",
            "--tag-commit",
            "a1" * 20,
            "--build-commit",
            "not-a-commit",
            "--cas-dir",
            str(tmp_path / "cas"),
            str(artifact),
        ],
        cwd=tmp_path,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode != 0
    assert "--build-commit must identify" in result.stderr
    assert not (tmp_path / ".forkmesh/releases/latest/release.json").exists()


def test_repo_root_build_revision_includes_qt_only_commits(tmp_path):
    repo = tmp_path / "repo"
    worker = repo / "cloudflare_worker"
    qt = repo / "qt_client"
    worker.mkdir(parents=True)
    qt.mkdir()
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.email", "test@example.invalid"],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.name", "ForkMesh Test"],
        check=True,
    )
    (worker / "deploy.sh").write_text("first\n")
    subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
    subprocess.run(
        ["git", "-C", str(repo), "commit", "-qm", "worker"], check=True
    )
    worker_commit = subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True
    ).strip()
    (qt / "CMakeLists.txt").write_text("second\n")
    subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
    subprocess.run(["git", "-C", str(repo), "commit", "-qm", "qt"], check=True)
    qt_commit = subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True
    ).strip()

    wrong_cwd_revision = subprocess.check_output(
        ["git", "-C", str(worker), "log", "-1", "--format=%H", "--", "."],
        text=True,
    ).strip()
    root_revision = subprocess.check_output(
        [
            "git",
            "-C",
            str(repo),
            "log",
            "-1",
            "--format=%H",
            "--",
            ".",
            ":(exclude).forkmesh/releases/**",
        ],
        text=True,
    ).strip()
    assert wrong_cwd_revision == worker_commit
    assert root_revision == qt_commit


def test_publisher_rejects_tag_commit_that_is_not_the_peeled_tag(tmp_path):
    repo = tmp_path / "repo"
    repo.mkdir()
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.email", "test@example.invalid"],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.name", "ForkMesh Test"],
        check=True,
    )
    artifact = repo / "forkmesh-linux-x86_64"
    artifact.write_bytes(b"\x7fELF tag target verification")
    subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
    subprocess.run(["git", "-C", str(repo), "commit", "-qm", "release"], check=True)
    subprocess.run(
        ["git", "-C", str(repo), "tag", "-a", "v0.7.0", "-m", "release"],
        check=True,
    )
    target = subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True
    ).strip()
    wrong = "f" * 40 if target != "f" * 40 else "e" * 40

    result = subprocess.run(
        [
            str(PUBLISH),
            "--tag",
            "v0.7.0",
            "--tag-commit",
            wrong,
            "--build-commit",
            target,
            "--cas-dir",
            str(tmp_path / "cas"),
            str(artifact),
        ],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode != 0
    assert "does not match the peeled target" in result.stderr
    assert not (repo / ".forkmesh/releases/latest/release.json").exists()

    accepted = subprocess.run(
        [
            str(PUBLISH),
            "--tag",
            "v0.7.0",
            "--build-commit",
            target,
            "--cas-dir",
            str(tmp_path / "cas"),
            str(artifact),
        ],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    assert accepted.returncode == 0, accepted.stderr
    manifest = json.loads(
        (repo / ".forkmesh/releases/latest/release.json").read_text()
    )
    assert manifest["tag_commit"] == target


def test_release_workflow_requires_committed_version_and_verifies_binary():
    workflow = WORKFLOW.read_text(encoding="utf-8")
    assert "Commit the version bump before creating the release tag" in workflow
    assert '"$built" --version' in workflow
    assert '"$built" --build-commit' in workflow
    assert '[ "$reported_version" != "ForkMesh $version" ]' in workflow
    assert '[ "$reported_commit" != "$revision" ]' in workflow
    assert 'release_tag="${FORKMESH_TAG:-v${project_version}}"' in workflow
    assert '"refs/tags/${release_tag}^{commit}"' in workflow
    assert workflow.count('--tag "$release_tag"') == 2
    assert '--tag "${FORKMESH_TAG:-}"' not in workflow
    assert "Sync the version header to the release tag" not in workflow
    assert "sed -i.bak" not in workflow
