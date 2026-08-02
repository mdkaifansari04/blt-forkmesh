"""Contracts for the network-isolated ForkMesh CI workflow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".forkmesh" / "ci.yml"
DEPLOY_WORKFLOW = ROOT / ".forkmesh" / "deploy.yml"
CRITICAL_RUNNER = ROOT / "tools" / "run_critical_tests.py"
BROWSER_PACKAGE = ROOT / "cloudflare_worker" / "browser_tests" / "package.json"
BROWSER_CONFIG = ROOT / "cloudflare_worker" / "browser_tests" / "playwright.config.js"
BROWSER_SERVER = ROOT / "cloudflare_worker" / "browser_tests" / "run-browser-worker.sh"


def test_critical_suites_use_runner_packages_without_network_install():
    source = WORKFLOW.read_text(encoding="utf-8")

    assert "python3 tools/run_critical_tests.py worker" in source
    assert "python3 tools/run_critical_tests.py world" in source
    assert "python3 tools/run_critical_tests.py qt" in source
    assert '("pytest", "cryptography")' in source
    assert "FORKMESH_CI_KNOWN_FAILURES=1" not in source
    assert 'export TMPDIR="$ci_tmp"' in source
    assert 'ci_tmp="$PWD/.forkmesh-ci-tmp"' in source
    assert " -m pip install" not in source
    assert "python3 -m venv" not in source


def test_deploy_reuses_ci_instead_of_installing_pytest():
    source = DEPLOY_WORKFLOW.read_text(encoding="utf-8")

    assert "needs: [.forkmesh/ci.yml]" in source
    assert "Run Cloudflare Worker tests" not in source
    assert " -m pip install" not in source
    assert "python3 -m venv" not in source


def test_critical_suites_have_isolated_workflow_steps():
    source = WORKFLOW.read_text(encoding="utf-8")

    assert "- name: Critical Cloudflare Worker contracts" in source
    assert "- name: Critical virtual World contracts" in source
    assert "- name: Critical Qt client contracts" in source
    assert "Test suites (in parallel)" not in source
    assert "heartbeat()" not in source


def test_critical_suites_enforce_sub_minute_deadlines():
    runner = CRITICAL_RUNNER.read_text(encoding="utf-8")
    package = BROWSER_PACKAGE.read_text(encoding="utf-8")
    config = BROWSER_CONFIG.read_text(encoding="utf-8")
    server = BROWSER_SERVER.read_text(encoding="utf-8")

    assert "TEST_BUDGET_SECONDS = 55" in runner
    assert 'playwright test --grep @critical' in package
    assert "globalTimeout: 55_000" in config
    assert "timeout: 45_000" in config
    assert "python3 -m http.server" in server
    assert "pywrangler" not in server


def test_ci_failure_quarantine_is_explicit_and_bounded():
    conftest = (ROOT / "cloudflare_worker" / "tests" / "conftest.py").read_text(
        encoding="utf-8")

    assert "FORKMESH_CI_KNOWN_FAILURES" in conftest
    assert "strict=False" in conftest
    assert conftest.count('    "test_') == 54
