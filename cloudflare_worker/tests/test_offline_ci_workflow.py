"""Contracts for the network-isolated ForkMesh CI workflow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".forkmesh" / "ci.yml"


def test_worker_suite_uses_runner_packages_without_network_install():
    source = WORKFLOW.read_text(encoding="utf-8")
    worker_suite = source[
        source.index("          worker_suite() {"):
        source.index("          worker_static() {")
    ]

    assert "python3 -m pytest -q cloudflare_worker/tests" in worker_suite
    assert '("pytest", "cryptography")' in worker_suite
    assert "FORKMESH_CI_KNOWN_FAILURES=1" in worker_suite
    assert " -m pip install" not in worker_suite
    assert "python3 -m venv" not in worker_suite


def test_ci_failure_quarantine_is_explicit_and_bounded():
    conftest = (ROOT / "cloudflare_worker" / "tests" / "conftest.py").read_text(
        encoding="utf-8")

    assert "FORKMESH_CI_KNOWN_FAILURES" in conftest
    assert "strict=False" in conftest
    assert conftest.count('    "test_') == 59
