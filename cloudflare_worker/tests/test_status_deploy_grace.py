"""Deployment handoff grace for flagship repository health alerts."""

import ast
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
ENTRY_PATH = ROOT / "src/entry.py"
ENTRY = ENTRY_PATH.read_text(encoding="utf-8")
DEPLOY = (ROOT / "deploy.sh").read_text(encoding="utf-8")


def _node(name):
    for node in ast.parse(ENTRY, filename=str(ENTRY_PATH)).body:
        if isinstance(node, ast.Assign):
            if any(
                isinstance(target, ast.Name) and target.id == name
                for target in node.targets
            ):
                return node
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.name == name:
                return node
    raise AssertionError(name)


def test_deploy_stamps_an_epoch_and_grace_is_bounded_to_five_minutes():
    assert 'DEPLOYED_AT_MS="$(date -u +%s)000"' in DEPLOY
    assert '--var "DEPLOYED_AT_MS:${DEPLOYED_AT_MS}"' in DEPLOY
    namespace = {}
    module = ast.fix_missing_locations(ast.Module(
        body=[
            _node("STATUS_DEPLOY_GRACE_MS"),
            _node("_deployment_status_grace_active"),
        ],
        type_ignores=[],
    ))
    exec(compile(module, str(ENTRY_PATH), "exec"), namespace)
    grace = namespace["STATUS_DEPLOY_GRACE_MS"]
    active = namespace["_deployment_status_grace_active"]
    assert grace == 5 * 60 * 1000
    env = SimpleNamespace(DEPLOYED_AT_MS=1_000_000)
    assert active(env, 1_000_000)
    assert active(env, 1_000_000 + grace - 1)
    assert not active(env, 1_000_000 + grace)
    assert not active(SimpleNamespace(), 1_000_000)


def test_flagship_probe_and_mail_transition_both_apply_the_grace():
    sample = ENTRY[
        ENTRY.index("async def record_status_sample"):
        ENTRY.index("\n\nasync def status_history")
    ]
    assert sample.count("_deployment_status_grace_active(env, now)") == 2
    transition = ENTRY[
        ENTRY.index("async def _record_status_monitor_transitions"):
        ENTRY.index("\ndef _status_expected_checks_for_hour")
    ]
    assert 'system_id == "flagship_repository"' in transition
    assert "int(now) - int(outage_started_at)" in transition
    assert "< STATUS_DEPLOY_GRACE_MS" in transition
    assert 'prior_notified != "down"' in transition
    assert 'notified = "up"' in transition


def test_shared_deploy_semaphore_covers_rollout_and_post_ready_grace():
    namespace = {}
    module = ast.fix_missing_locations(ast.Module(
        body=[
            _node("STATUS_DEPLOY_GRACE_MS"),
            _node("STATUS_DEPLOY_MAX_MS"),
        ],
        type_ignores=[],
    ))
    exec(compile(module, str(ENTRY_PATH), "exec"), namespace)
    assert namespace["STATUS_DEPLOY_GRACE_MS"] == 5 * 60 * 1000
    assert namespace["STATUS_DEPLOY_MAX_MS"] == 30 * 60 * 1000

    semaphore = ENTRY[
        ENTRY.index("async def _status_deploy_semaphore_active"):
        ENTRY.index("\n\nasync def _record_status_deploy_sample")
    ]
    assert "SELECT state,started_at,finished_at" in semaphore
    assert 'state == "deploying"' in semaphore
    assert "< STATUS_DEPLOY_MAX_MS" in semaphore
    assert 'state == "ready"' in semaphore
    assert "< STATUS_DEPLOY_GRACE_MS" in semaphore

    sample = ENTRY[
        ENTRY.index("async def record_status_sample"):
        ENTRY.index("\n\nasync def status_history")
    ]
    assert "_status_deploy_semaphore_active(env, now)" in sample


def test_deploy_announces_before_migrations_and_retries_after_schema_setup():
    production = DEPLOY[DEPLOY.index('case "${1:-deploy}" in'):]
    first_signal = production.index(
        'if signal_world_deploy deploying "$BUILD_REV"; then')
    migration = production.index("./migrate.sh", first_signal)
    retry_signal = production.index(
        'if signal_world_deploy deploying "$BUILD_REV"; then',
        first_signal + 1,
    )
    assert first_signal < migration < retry_signal
    assert "Deploy alert semaphore: state=$state" in DEPLOY
    assert "deploy alert semaphore is unavailable" in production
