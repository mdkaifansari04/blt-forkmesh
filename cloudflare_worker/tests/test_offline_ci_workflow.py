"""Contracts for the network-isolated ForkMesh CI workflow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".forkmesh" / "ci.yml"
DEPLOY_WORKFLOW = ROOT / ".forkmesh" / "deploy.yml"
RELEASE_WORKFLOW = ROOT / ".forkmesh" / "release.yml"
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


def test_qt_step_builds_every_binary_the_critical_label_selects():
    """ctest reports an unbuilt suite as "***Not Run", so a suite promoted to
    the "critical" label without a matching build target fails CI silently.
    The workflow builds one aggregate target that CMake derives from the same
    place it applies the label, so the two lists cannot drift apart."""
    source = WORKFLOW.read_text(encoding="utf-8")
    cmake = (ROOT / "qt_client" / "CMakeLists.txt").read_text(encoding="utf-8")

    assert "--target critical-tests" in source
    # No enumerating suites in the workflow: that is what drifted before.
    assert "--target forkmesh-" not in source
    assert "add_custom_target(critical-tests" in cmake

    labelled = _cmake_list(cmake, "FORKMESH_CRITICAL_TESTS")
    built = _cmake_list(cmake, "FORKMESH_CRITICAL_TEST_TARGETS")
    assert labelled and built
    for test in labelled:
        # forkmesh-mirror-fleet-window-tests is a flag-selected second run of
        # the forkmesh-window-tests binary; every other name is its own target.
        target = test.replace("mirror-fleet-window", "window")
        assert target in built, f"{test} has no binary in critical-tests"


def _cmake_list(cmake: str, name: str) -> list[str]:
    _, _, rest = cmake.partition(f"set({name}\n")
    body, _, _ = rest.partition(")")
    return body.split()


def test_qt_builds_pin_autogen_pools_to_the_sandbox_budget():
    """CMake's AUTOGEN_PARALLEL default (AUTO) sizes each AUTOMOC driver's
    thread pool from the host's core count, which no step can see from inside
    the Actions cgroup. One such pool per -j slot exhausted the scope's task
    ceiling and the build died on pthread_create's "Resource temporarily
    unavailable" (adhoc #1586). Both Qt builds pin the pools to the same job
    count the compiles are sized from."""
    for workflow in (WORKFLOW, RELEASE_WORKFLOW):
        source = workflow.read_text(encoding="utf-8")
        assert '-DCMAKE_AUTOGEN_PARALLEL="$jobs"' in source, workflow.name
        # Sizing the pools from $jobs only works if $jobs is already computed.
        assert source.index("jobs=") < source.index("-DCMAKE_AUTOGEN_PARALLEL")


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
