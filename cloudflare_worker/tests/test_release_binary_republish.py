"""Safety contract for deploy.sh's explicit same-version binary refresh."""

from pathlib import Path
import subprocess


DEPLOY = Path(__file__).resolve().parents[1] / "deploy.sh"


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
    clean_guard = "git status --porcelain --untracked-files=no"
    configure = "cmake -S ../qt_client -B ../qt_client/build-release"
    assert cas_guard in function
    assert clean_guard in function
    assert function.index(cas_guard) < function.index(configure)
    assert function.index(clean_guard) < function.index(configure)

    assert '-DFORKMESH_BUILD_TESTS=OFF' in function
    assert '-DFORKMESH_VERSION_OVERRIDE="$release_version"' in function
    assert '"$built" --version' in function
    assert '"ForkMesh ${release_version}"' in function


def test_refresh_publishes_from_repo_root_and_verifies_revision_hash_and_cas():
    function = _publish_function()

    assert "cd .. && tools/forkmesh-release-publish.sh" in function
    assert 'publish_args+=("cloudflare_worker/$asset")' in function
    assert '"tag_commit": "$source_commit"' not in function  # grep includes JSON quoting
    assert '\\"tag_commit\\": \\"$source_commit\\"' in function
    assert '\\"name\\":\\"$asset\\",\\"blob_sha256\\":\\"$asset_hash\\"' in function
    assert (
        '$cas_dir/sha256/${asset_hash:0:2}/$asset_hash/data'
        in function
    )
    assert "git add ../.forkmesh/releases/latest/SHASUMS256.txt" in function
