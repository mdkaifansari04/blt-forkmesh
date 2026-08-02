"""Build-time Qt provenance must fail closed for dirty source trees."""

from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT_TEMPLATE = (
    ROOT / "qt_client/cmake/GenerateForkMeshVersion.cmake.in"
).read_text(encoding="utf-8")
HEADER_TEMPLATE = (
    ROOT / "qt_client/src/ForkMeshVersion.h.in"
).read_text(encoding="utf-8")


def _git(repo, *args):
    return subprocess.check_output(
        ["git", "-C", str(repo), *args], text=True
    ).strip()


def _write_generator(repo, build, *, override=""):
    source = repo / "qt_client"
    generated = (
        SCRIPT_TEMPLATE
        .replace("@CMAKE_CURRENT_SOURCE_DIR@", str(source))
        .replace("@CMAKE_CURRENT_BINARY_DIR@", str(build))
        .replace("@FORKMESH_VERSION@", "0.7.0")
        .replace("@FORKMESH_BUILD_COMMIT_OVERRIDE@", override)
    )
    path = build / "GenerateForkMeshVersion.cmake"
    build.mkdir(parents=True, exist_ok=True)
    path.write_text(generated, encoding="utf-8")
    return path


def _run_generator(repo, build, *, override=""):
    script = _write_generator(repo, build, override=override)
    return subprocess.run(
        ["cmake", "-P", str(script)],
        text=True,
        capture_output=True,
        check=False,
    )


def _reported_commit(build):
    text = (build / "ForkMeshVersion.h").read_text(encoding="utf-8")
    marker = '#define FORKMESH_BUILD_COMMIT "'
    return text.split(marker, 1)[1].split('"', 1)[0]


@pytest.mark.skipif(shutil.which("cmake") is None, reason="cmake unavailable")
def test_build_provenance_tracks_clean_root_and_rejects_dirty_inputs(tmp_path):
    repo = tmp_path / "repo"
    source = repo / "qt_client"
    (source / "src").mkdir(parents=True)
    (source / "cmake").mkdir()
    (repo / "cloudflare_worker").mkdir()
    (source / "src/ForkMeshVersion.h.in").write_text(
        HEADER_TEMPLATE, encoding="utf-8"
    )
    (source / "cmake/GenerateForkMeshVersion.cmake.in").write_text(
        SCRIPT_TEMPLATE, encoding="utf-8"
    )
    (repo / "cloudflare_worker/input.txt").write_text("clean\n")
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.email", "test@example.invalid"],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.name", "ForkMesh Test"],
        check=True,
    )
    subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
    subprocess.run(["git", "-C", str(repo), "commit", "-qm", "clean"], check=True)
    commit = _git(repo, "rev-parse", "HEAD")
    build = tmp_path / "build"

    result = _run_generator(repo, build)
    assert result.returncode == 0, result.stderr
    assert _reported_commit(build) == commit

    # A dirty file outside qt_client still affects packaged artifact resources.
    (repo / "cloudflare_worker/input.txt").write_text("dirty\n")
    result = _run_generator(repo, build)
    assert result.returncode == 0, result.stderr
    assert _reported_commit(build) == "dirty"
    subprocess.run(
        ["git", "-C", str(repo), "checkout", "--", "cloudflare_worker/input.txt"],
        check=True,
    )

    (repo / "cloudflare_worker/untracked.txt").write_text("untracked\n")
    result = _run_generator(repo, build)
    assert result.returncode == 0, result.stderr
    assert _reported_commit(build) == "dirty"
    (repo / "cloudflare_worker/untracked.txt").unlink()

    # Output-only release metadata neither changes code provenance nor creates
    # the impossible artifact -> metadata commit -> artifact cycle.
    metadata = repo / ".forkmesh/releases/latest/release.json"
    metadata.parent.mkdir(parents=True)
    metadata.write_text("{}\n")
    result = _run_generator(repo, build)
    assert result.returncode == 0, result.stderr
    assert _reported_commit(build) == commit


@pytest.mark.skipif(shutil.which("cmake") is None, reason="cmake unavailable")
def test_archive_override_works_but_checkout_override_cannot_lie(tmp_path):
    archive = tmp_path / "archive"
    source = archive / "qt_client/src"
    source.mkdir(parents=True)
    (archive / "qt_client/cmake").mkdir()
    (source / "ForkMeshVersion.h.in").write_text(HEADER_TEMPLATE)
    build = tmp_path / "archive-build"
    commit = "a1" * 20
    result = _run_generator(archive, build, override=commit)
    assert result.returncode == 0, result.stderr
    assert _reported_commit(build) == commit

    repo = tmp_path / "checkout"
    (repo / "qt_client/src").mkdir(parents=True)
    (repo / "qt_client/cmake").mkdir()
    (repo / "qt_client/src/ForkMeshVersion.h.in").write_text(HEADER_TEMPLATE)
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.email", "test@example.invalid"],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(repo), "config", "user.name", "ForkMesh Test"],
        check=True,
    )
    subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
    subprocess.run(["git", "-C", str(repo), "commit", "-qm", "clean"], check=True)
    result = _run_generator(repo, tmp_path / "checkout-build", override="b2" * 20)
    assert result.returncode != 0
    assert "does not match the clean checkout" in result.stderr
