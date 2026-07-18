#!/usr/bin/env python3
"""Sentry reporting contracts for the Cloudflare Python worker."""

import ast
import asyncio
import traceback
import tomllib
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
WRANGLER = ROOT / "wrangler.toml"
CI_WORKFLOW = ROOT.parent / ".forkmesh" / "ci.yml"
DEPLOY_WORKFLOW = ROOT.parent / ".forkmesh" / "deploy.yml"
DEPLOY_SH = ROOT / "deploy.sh"
ENV_EXAMPLE = ROOT / ".env.production.example"
ACTION_RUNNER_CPP = ROOT.parent / "qt_client" / "src" / "ActionRunner.cpp"
ACTION_RUNNER_H = ROOT.parent / "qt_client" / "src" / "ActionRunner.h"
CRASH_HANDLER_CPP = ROOT.parent / "qt_client" / "src" / "CrashHandler.cpp"
CRASH_HANDLER_H = ROOT.parent / "qt_client" / "src" / "CrashHandler.h"
HEADLESS_CONSOLE_CPP = ROOT.parent / "qt_client" / "src" / "HeadlessConsole.cpp"
MAIN_CPP = ROOT.parent / "qt_client" / "src" / "main.cpp"
MAIN_WINDOW_INTERNAL_H = ROOT.parent / "qt_client" / "src" / "MainWindowInternal.h"
MAIN_WINDOW_CPP = ROOT.parent / "qt_client" / "src" / "MainWindow.cpp"
MAIN_WINDOW_H = ROOT.parent / "qt_client" / "src" / "MainWindow.h"
MAIN_WINDOW_ACTIONS_CPP = ROOT.parent / "qt_client" / "src" / "MainWindowActions.cpp"
MAIN_WINDOW_PULLS_CPP = ROOT.parent / "qt_client" / "src" / "MainWindowPulls.cpp"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
WRANGLER_DATA = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))
CI_WORKFLOW_TEXT = CI_WORKFLOW.read_text(encoding="utf-8")
DEPLOY_WORKFLOW_TEXT = DEPLOY_WORKFLOW.read_text(encoding="utf-8")
DEPLOY_SH_TEXT = DEPLOY_SH.read_text(encoding="utf-8")
ENV_EXAMPLE_TEXT = ENV_EXAMPLE.read_text(encoding="utf-8")
ACTION_RUNNER_CPP_TEXT = ACTION_RUNNER_CPP.read_text(encoding="utf-8")
ACTION_RUNNER_H_TEXT = ACTION_RUNNER_H.read_text(encoding="utf-8")
CRASH_HANDLER_CPP_TEXT = CRASH_HANDLER_CPP.read_text(encoding="utf-8")
CRASH_HANDLER_H_TEXT = CRASH_HANDLER_H.read_text(encoding="utf-8")
HEADLESS_CONSOLE_CPP_TEXT = HEADLESS_CONSOLE_CPP.read_text(encoding="utf-8")
MAIN_CPP_TEXT = MAIN_CPP.read_text(encoding="utf-8")
MAIN_WINDOW_INTERNAL_H_TEXT = MAIN_WINDOW_INTERNAL_H.read_text(encoding="utf-8")
MAIN_WINDOW_CPP_TEXT = MAIN_WINDOW_CPP.read_text(encoding="utf-8")
MAIN_WINDOW_H_TEXT = MAIN_WINDOW_H.read_text(encoding="utf-8")
MAIN_WINDOW_ACTIONS_CPP_TEXT = MAIN_WINDOW_ACTIONS_CPP.read_text(encoding="utf-8")
MAIN_WINDOW_PULLS_CPP_TEXT = MAIN_WINDOW_PULLS_CPP.read_text(encoding="utf-8")


def _load_sentry_dsn_parts():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = []
    for node in tree.body:
        if isinstance(node, ast.Assign):
            targets = {t.id for t in node.targets if isinstance(t, ast.Name)}
            if "SENTRY_CLIENT" in targets:
                selected.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name == "_sentry_dsn_parts":
            selected.append(node)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = {"urlparse": urlparse}
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns["_sentry_dsn_parts"]


def _load_capture_worker_exception():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    want = {
        "method_name",
        "_sentry_header",
        "_sentry_safe_url",
        "_safe_error_text",
        "capture_worker_exception",
    }
    selected = []
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in want:
            selected.append(node)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    calls = []

    def _boom(*args, **kwargs):
        calls.append(("dsn", args, kwargs))
        raise RuntimeError("dsn read failed")

    async def _capture(*args, **kwargs):
        calls.append(("sentry", args, kwargs))
        raise RuntimeError("sentry failed")

    async def _write(*args, **kwargs):
        calls.append(("write", args, kwargs))
        raise RuntimeError("d1 failed")

    def _console(message):
        calls.append(("console", str(message), {}))

    ns = {
        "traceback": traceback,
        "_sentry_dsn_parts": _boom,
        "capture_sentry_error": _capture,
        "_write_error_log": _write,
        "_console_error": _console,
        "calls": calls,
    }
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns


def test_sentry_uses_vanilla_worker_fetch_not_sdk_plugin():
    assert "sentry_sdk" not in ENTRY_TEXT
    assert "capture_sentry_error" in ENTRY_TEXT
    assert "application/x-sentry-envelope" in ENTRY_TEXT
    assert "x-sentry-auth" in ENTRY_TEXT
    assert "from js import fetch as js_fetch" in ENTRY_TEXT


def test_sentry_dsn_parser_builds_envelope_endpoint():
    parse = _load_sentry_dsn_parts()
    parts = parse("https://public@example.ingest.sentry.io/12345")
    assert parts["endpoint"] == "https://example.ingest.sentry.io/api/12345/envelope/"
    assert "sentry_key=public" in parts["auth"]
    assert "sentry_version=7" in parts["auth"]

    self_hosted = parse("https://pub@sentry.example.com/prefix/987")
    assert self_hosted["endpoint"] == "https://sentry.example.com/prefix/api/987/envelope/"


def test_worker_error_log_reports_to_sentry_and_preserves_d1_log():
    assert "await capture_sentry_error(" in ENTRY_TEXT
    assert "INSERT INTO error_log" in ENTRY_TEXT
    assert "request=request" in ENTRY_TEXT
    assert "error=error" in ENTRY_TEXT
    assert '"cloudflare": cf_context' in ENTRY_TEXT


def test_cron_failures_emit_sentry_cron_monitor_checkins():
    assert 'SENTRY_CRON_MONITOR_SLUG = "forkmesh-relay"' in ENTRY_TEXT
    assert "async def capture_sentry_cron_check_in" in ENTRY_TEXT
    assert '"type": "check_in"' in ENTRY_TEXT
    assert '"monitor_slug": _sentry_cron_monitor_slug(env)' in ENTRY_TEXT
    assert '"status": status' in ENTRY_TEXT
    assert '"monitor_config": _sentry_cron_monitor_config(env, cron)' in ENTRY_TEXT
    assert '"schedule": {"type": "crontab", "value": cron[:120]}' in ENTRY_TEXT
    assert '"checkin_margin": _sentry_int_env(' in ENTRY_TEXT
    assert '"max_runtime": _sentry_int_env(' in ENTRY_TEXT

    scheduled = ENTRY_TEXT.split("async def scheduled", 1)[1] \
        .split("async def fetch", 1)[0]
    assert 'await capture_sentry_cron_check_in(' in scheduled
    # Two check-ins per tick, same check_in_id: an opening "in_progress" sent
    # before any D1/decrypt work, and a closing ok/error. The opening
    # check-in guarantees Sentry sees this tick even if the isolate is later
    # killed by the platform's resource limits — without it, a killed tick
    # sends nothing at all and shows up as a "missed check-in" instead of a
    # runtime error.
    assert '"in_progress"' in scheduled
    assert scheduled.count("await capture_sentry_cron_check_in(") == 2
    assert scheduled.count("check_in_id=cron_check_in_id") == 2
    assert 'final_cron_status = "error" if cron_failures else "ok"' in scheduled
    assert "duration=(int(Date.now()) - cron_started_ms) / 1000" in scheduled
    assert "await log_cron_error(" in scheduled
    assert "error=error, failures=cron_failures" in scheduled

    assert "SENTRY_CRON_MONITOR_SLUG=forkmesh-relay" in ENV_EXAMPLE_TEXT
    assert "SENTRY_CRON_CHECKIN_MARGIN_MINUTES=1" in ENV_EXAMPLE_TEXT
    assert "SENTRY_CRON_MAX_RUNTIME_MINUTES=5" in ENV_EXAMPLE_TEXT


def test_worker_1101_exceptions_are_captured_then_reraised():
    assert "async def capture_worker_exception" in ENTRY_TEXT
    assert 'cloudflare_error_code="1101"' in ENTRY_TEXT
    assert '"cloudflare.error_code"] = str(cloudflare_error_code)' in ENTRY_TEXT
    assert '"cloudflare.ray_id"] = str(ray)' in ENTRY_TEXT
    assert "await capture_worker_exception(self.env, request, url, error)" in ENTRY_TEXT
    assert "raise\n" in ENTRY_TEXT
    assert 'return json_response({"error": "internal_error"}, status=500)' not in ENTRY_TEXT
    assert "Re-raise so Cloudflare records the native Worker failure/Error 1101" in ENTRY_TEXT


def test_worker_exception_capture_does_not_raise_from_reporting_failures():
    ns = _load_capture_worker_exception()

    class HostileEnv:
        @property
        def SENTRY_DSN(self):
            raise RuntimeError("env unavailable")

    class HostileRequest:
        @property
        def headers(self):
            raise RuntimeError("headers unavailable")

        @property
        def method(self):
            raise RuntimeError("method unavailable")

        @property
        def url(self):
            raise RuntimeError("url unavailable")

    class HostileUrl:
        @property
        def path(self):
            raise RuntimeError("path unavailable")

    class HostileError(Exception):
        def __repr__(self):
            raise RuntimeError("repr unavailable")

        def __str__(self):
            raise RuntimeError("str unavailable")

    asyncio.run(ns["capture_worker_exception"](
        HostileEnv(), HostileRequest(), HostileUrl(), HostileError()))

    call_names = [item[0] for item in ns["calls"]]
    assert "sentry" in call_names
    assert "write" in call_names


def test_background_tasks_observe_exceptions_instead_of_default_handler():
    assert "def _fire_and_forget" in ENTRY_TEXT
    assert "def _consume_background_task" in ENTRY_TEXT
    assert "task.result()" in ENTRY_TEXT
    ensure_future_lines = [
        line.strip()
        for line in ENTRY_TEXT.splitlines()
        if "asyncio.ensure_future(" in line
    ]
    assert ensure_future_lines == ["task = asyncio.ensure_future(coro)"]
    assert '_fire_and_forget(self._stream_watchdog(req_id), "git stream watchdog")' in ENTRY_TEXT
    assert '"release download log"' in ENTRY_TEXT


def test_sentry_request_metadata_is_allowlisted_and_sanitized():
    assert 'for name in ("host", "user-agent", "accept", "cf-ray")' in ENTRY_TEXT
    assert '"url": _sentry_safe_url(request)' in ENTRY_TEXT
    assert "return parsed.scheme + \"://\" + parsed.netloc + (parsed.path or \"/\")" in ENTRY_TEXT
    request_payload = ENTRY_TEXT.split("def _sentry_request_payload", 1)[1] \
        .split("def _sentry_cloudflare_context", 1)[0]
    assert '"authorization"' not in request_payload.lower()
    assert '"cookie"' not in request_payload.lower()


def test_simulate_sentry_error_route_is_worker_owned_and_raises():
    run_worker_first = WRANGLER_DATA["assets"]["run_worker_first"]
    assert "/simulate-sentry-error" in run_worker_first
    assert 'url.path in ("/simulate-sentry-error", "/simulate-sentry-error/")' in ENTRY_TEXT
    assert "division_by_zero = 1 / 0" in ENTRY_TEXT


def test_forkmesh_deploy_pushes_sentry_dsn_secret():
    assert "SENTRY_DSN=${{ vars.SENTRY_DSN }}" in DEPLOY_WORKFLOW_TEXT
    assert "\n        push_secrets\n" in DEPLOY_SH_TEXT


def test_forkmesh_actions_run_full_worker_pytest_suite():
    assert "python3 -m venv .forkmesh-pytest-venv" in CI_WORKFLOW_TEXT
    assert ".forkmesh-pytest-venv/bin/python -m pip install pytest" in CI_WORKFLOW_TEXT
    assert ".forkmesh-pytest-venv/bin/python -m pytest -q cloudflare_worker/tests" in CI_WORKFLOW_TEXT
    assert "python3 -m venv .forkmesh-pytest-venv" in DEPLOY_WORKFLOW_TEXT
    assert ".forkmesh-pytest-venv/bin/python -m pip install pytest" in DEPLOY_WORKFLOW_TEXT
    assert ".forkmesh-pytest-venv/bin/python -m pytest -q cloudflare_worker/tests" in DEPLOY_WORKFLOW_TEXT
    assert "pip install --user" not in CI_WORKFLOW_TEXT
    assert "pip install --user" not in DEPLOY_WORKFLOW_TEXT
    assert "python3 cloudflare_worker/tests/test_crypto.py" not in CI_WORKFLOW_TEXT
    assert "python3 cloudflare_worker/tests/test_crypto.py" not in DEPLOY_WORKFLOW_TEXT
    assert "cmake -S qt_client -B qt_client/build-ci" in CI_WORKFLOW_TEXT
    assert "cmake --build qt_client/build-ci -j --target forkmesh-tests" in CI_WORKFLOW_TEXT
    assert "./qt_client/build-ci/forkmesh-tests" in CI_WORKFLOW_TEXT


def test_action_runner_does_not_override_installer_source_in_ci_jobs():
    assert "if (isReleaseRun() && !m_run.owner.isEmpty() && !m_run.name.isEmpty())" in ACTION_RUNNER_CPP_TEXT
    assert "env.insert(QStringLiteral(\"FORKMESH_REPO\")," in ACTION_RUNNER_CPP_TEXT
    assert "if (!m_run.owner.isEmpty() && !m_run.name.isEmpty())\n        env.insert(QStringLiteral(\"FORKMESH_REPO\")," not in ACTION_RUNNER_CPP_TEXT


def test_action_runner_caps_process_output_so_pipeline_logs_do_not_crash_app():
    assert "kActionProcessLogMaxBytes" in ACTION_RUNNER_CPP_TEXT
    assert "kActionProcessLogChunkBytes" in ACTION_RUNNER_CPP_TEXT
    assert "kActionProcessLogTailBytes" in ACTION_RUNNER_CPP_TEXT
    assert "kActionProcessLogMaxLineChars" in ACTION_RUNNER_CPP_TEXT
    assert "emitProcessOutput(const QByteArray &bytes)" in ACTION_RUNNER_CPP_TEXT
    assert "emitProcessOutputTruncationNotice()" in ACTION_RUNNER_CPP_TEXT
    assert "normalizeProcessOutputForLog(const QString &text)" in ACTION_RUNNER_CPP_TEXT
    assert "rememberSuppressedProcessOutput(const QByteArray &bytes)" in ACTION_RUNNER_CPP_TEXT
    assert "emitSuppressedProcessOutputTail()" in ACTION_RUNNER_CPP_TEXT
    assert "rememberCrashProcessOutput(const QByteArray &bytes)" in ACTION_RUNNER_CPP_TEXT
    assert "updateCrashContext()" in ACTION_RUNNER_CPP_TEXT
    assert "logFailureDiagnostic(finalMessage)" in ACTION_RUNNER_CPP_TEXT
    assert "m_processOutputBytes = 0;" in ACTION_RUNNER_CPP_TEXT
    assert "m_processOutputSuppressedBytes = 0;" in ACTION_RUNNER_CPP_TEXT
    assert "m_processOutputTail.clear();" in ACTION_RUNNER_CPP_TEXT
    assert "m_processOutputLineChars = 0;" in ACTION_RUNNER_CPP_TEXT
    assert "m_processOutputTruncated = false;" in ACTION_RUNNER_CPP_TEXT
    assert "Action output exceeded %1 KiB" in ACTION_RUNNER_CPP_TEXT
    assert "Showing the final %1 KiB of suppressed process output" in ACTION_RUNNER_CPP_TEXT
    assert "recent process output tail:" in ACTION_RUNNER_CPP_TEXT
    assert "forkmesh::setCrashContext(crashContext())" in ACTION_RUNNER_CPP_TEXT
    assert "forkmesh::setTerminationSignalSurvivalEnabled(true)" in ACTION_RUNNER_CPP_TEXT
    assert "forkmesh::setTerminationSignalSurvivalEnabled(false)" in ACTION_RUNNER_CPP_TEXT
    assert "emitProcessOutput(m_process->readAllStandardOutput())" in ACTION_RUNNER_CPP_TEXT
    assert "emitProcessOutput(tail)" in ACTION_RUNNER_CPP_TEXT
    assert "emitSuppressedProcessOutputTail();" in ACTION_RUNNER_CPP_TEXT
    assert "emitLog(QString::fromUtf8(m_process->readAllStandardOutput()))" not in ACTION_RUNNER_CPP_TEXT
    assert "emitLog(QString::fromUtf8(tail))" not in ACTION_RUNNER_CPP_TEXT
    assert "void emitProcessOutput(const QByteArray &bytes);" in ACTION_RUNNER_H_TEXT
    assert "QByteArray m_processOutputTail;" in ACTION_RUNNER_H_TEXT
    assert "QByteArray m_crashOutputTail;" in ACTION_RUNNER_H_TEXT
    assert "bool m_processOutputTruncated = false;" in ACTION_RUNNER_H_TEXT
    assert "g_crashContext" in CRASH_HANDLER_CPP_TEXT
    assert "safeWriteCrashContext()" in CRASH_HANDLER_CPP_TEXT
    assert "SIGTERM (terminated)" in CRASH_HANDLER_CPP_TEXT
    assert "SIGINT (interrupt)" in CRASH_HANDLER_CPP_TEXT
    assert "SIGHUP (hangup)" in CRASH_HANDLER_CPP_TEXT
    assert "SIGQUIT (quit)" in CRASH_HANDLER_CPP_TEXT
    assert "SIGXCPU (CPU time limit exceeded)" in CRASH_HANDLER_CPP_TEXT
    assert "SIGXFSZ (file size limit exceeded)" in CRASH_HANDLER_CPP_TEXT
    assert "SIGTERM, SIGINT" in CRASH_HANDLER_CPP_TEXT
    assert "SIGKILL or SIGSTOP" in CRASH_HANDLER_CPP_TEXT
    assert "std::atomic_bool g_surviveTerminationSignals" in CRASH_HANDLER_CPP_TEXT
    assert "bool isTerminationSignal(int sig)" in CRASH_HANDLER_CPP_TEXT
    assert "g_surviveTerminationSignals.load(std::memory_order_relaxed)" in CRASH_HANDLER_CPP_TEXT
    assert "termination signal survived" in CRASH_HANDLER_CPP_TEXT
    assert "int g_mainLogFd = -1;" in CRASH_HANDLER_CPP_TEXT
    assert "safeWriteMainLogSignalRecord" in CRASH_HANDLER_CPP_TEXT
    assert "ForkMesh signal: " in CRASH_HANDLER_CPP_TEXT
    assert "action workflow survived and will finish/fail normally" in CRASH_HANDLER_CPP_TEXT
    assert "void safeWriteSignalInfo(const siginfo_t *info)" in CRASH_HANDLER_CPP_TEXT
    assert "signal code: " in CRASH_HANDLER_CPP_TEXT
    assert "sender pid: " in CRASH_HANDLER_CPP_TEXT
    assert "sender uid: " in CRASH_HANDLER_CPP_TEXT
    assert "sa.sa_flags = SA_ONSTACK | SA_SIGINFO;" in CRASH_HANDLER_CPP_TEXT
    assert "sa.sa_flags = SA_ONSTACK | SA_RESETHAND" not in CRASH_HANDLER_CPP_TEXT
    assert "SIGINT/SIGTERM stay owned by CrashHandler" in HEADLESS_CONSOLE_CPP_TEXT
    assert "::sigaction(SIGTERM" not in HEADLESS_CONSOLE_CPP_TEXT
    assert "::sigaction(SIGINT" not in HEADLESS_CONSOLE_CPP_TEXT
    assert "appendDiagnosticToMainLog" in CRASH_HANDLER_CPP_TEXT
    assert "network_log.txt" in CRASH_HANDLER_CPP_TEXT
    assert "void installCrashHandler(const QString &crashLogPath = QString()," in CRASH_HANDLER_H_TEXT
    assert "const QString &mainLogPath = QString())" in CRASH_HANDLER_H_TEXT
    assert "void setCrashContext(const QString &context)" in CRASH_HANDLER_H_TEXT
    assert "void setTerminationSignalSurvivalEnabled(bool enabled)" in CRASH_HANDLER_H_TEXT
    assert "void logDiagnosticEvent(const QString &context, const QString &details)" in CRASH_HANDLER_H_TEXT
    assert "QString earlyMainLogPath()" in MAIN_CPP_TEXT
    assert 'QStringLiteral("ForkMesh/ForkMesh/network_log.txt")' in MAIN_CPP_TEXT
    assert "earlyMainLogPath());" in MAIN_CPP_TEXT
    assert "displaySafePlainLog" in MAIN_WINDOW_INTERNAL_H_TEXT
    assert "text.size() > 8192" in MAIN_WINDOW_INTERNAL_H_TEXT
    assert "m_actionLog->setLineWrapMode(QPlainTextEdit::NoWrap)" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "m_actionLog->insertPlainText(displaySafePlainLog(text))" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "displaySafePlainLog(m_actionStore->readLog(*run))" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "displaySafePlainLog(" in MAIN_WINDOW_PULLS_CPP_TEXT
    assert "~MainWindow() override;" in MAIN_WINDOW_H_TEXT
    assert "MainWindow::~MainWindow()" in MAIN_WINDOW_CPP_TEXT
    assert "MainWindow teardown" in MAIN_WINDOW_CPP_TEXT
    assert "QObject::disconnect(view, nullptr, this, nullptr)" in MAIN_WINDOW_CPP_TEXT
    assert "m_diffViews.clear();" in MAIN_WINDOW_CPP_TEXT
    assert "INTERRUPTED: ForkMesh exited while this " in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "run was still running" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "actionRunLogPath(const ActionRun &run)" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "Run log: %7" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "void MainWindow::refreshCommitBarStatusGlyph()" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "void MainWindow::refreshCommitTableStatusGlyphs()" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "refreshCommitBarStatusGlyph();" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "refreshCommitTableStatusGlyphs();" in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "m_commitBarStatusHash" in MAIN_WINDOW_H_TEXT
    assert "m_commitBarBodyHtml" in MAIN_WINDOW_H_TEXT
    assert "m_overviewLoadedKey.clear();\n        loadRepoOverview(m_overviewPath);" not in MAIN_WINDOW_ACTIONS_CPP_TEXT
    assert "case 1: // Commits list\n        loadCommits();" not in MAIN_WINDOW_ACTIONS_CPP_TEXT


def test_worker_observability_is_enabled_at_full_sampling_in_wrangler():
    # Observability is enabled and captures every invocation (full sampling).
    observability = WRANGLER_DATA["observability"]
    assert observability["enabled"] is True
    assert observability["head_sampling_rate"] == 1
    assert observability["logs"]["head_sampling_rate"] == 1
    assert observability["traces"]["head_sampling_rate"] == 1
