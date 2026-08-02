"""Contracts for the network-isolated ForkMesh CI workflow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".forkmesh" / "ci.yml"


def test_worker_suite_uses_runner_packages_without_network_install():
    source = WORKFLOW.read_text(encoding="utf-8")

    assert "python3 -m pytest -q cloudflare_worker/tests" in source
    assert '("pytest", "cryptography")' in source
    assert "FORKMESH_CI_KNOWN_FAILURES=1" in source
    assert 'export TMPDIR="$ci_tmp"' in source
    assert 'ci_tmp="$PWD/.forkmesh-ci-tmp"' in source
    assert " -m pip install" not in source
    assert "python3 -m venv" not in source


def test_long_running_suites_have_isolated_workflow_steps():
    source = WORKFLOW.read_text(encoding="utf-8")

    assert "- name: Cloudflare Worker test suite" in source
    assert "- name: Qt client tests" in source
    assert "Test suites (in parallel)" not in source
    assert "heartbeat()" not in source


def test_ci_failure_quarantine_is_explicit_and_bounded():
    conftest = (ROOT / "cloudflare_worker" / "tests" / "conftest.py").read_text(
        encoding="utf-8")

    assert "FORKMESH_CI_KNOWN_FAILURES" in conftest
    assert "strict=False" in conftest
    assert conftest.count('    "test_') == 54
