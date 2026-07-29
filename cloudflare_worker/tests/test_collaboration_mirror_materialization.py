#!/usr/bin/env python3
"""Online mirrors materialize every browsable collaboration record."""

import ast
import asyncio
from pathlib import Path
from urllib.parse import parse_qs, urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
QT_ROOT = ROOT.parent / "qt_client" / "src"


def _function_source(name):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    node = next(
        item for item in tree.body
        if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
        and item.name == name
    )
    return ast.get_source_segment(ENTRY_TEXT, node)


def _load_helpers():
    names = {
        "_drain_ids_from_request",
        "_claim_collaboration_inbox_on_mirror",
        "_materialize_collaboration_inbox_on_mirror",
    }
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    assert {node.name for node in selected} == names
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    namespace = {
        "parse_qs": parse_qs,
        "urlparse": urlparse,
        "ISSUE_INBOX_CLAIM_TTL_MS": 300_000,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Date:
    @staticmethod
    def now():
        return 1_000_000


class _Request:
    url = "https://forkmesh.test/api/repo/o/r/pulls?ids=7,9"


def test_pull_and_discussion_handlers_offer_mirror_leases_and_retain_source():
    for handler_name, table, topic in (
        ("pulls_handler", "pull_inbox", "pulls"),
        ("discussions_handler", "discussion_inbox", "discussions"),
    ):
        source = _function_source(handler_name)
        assert "_authorized_mirror_issue_signing_key(" in source
        assert "_claim_collaboration_inbox_on_mirror(" in source
        assert table in source
        assert "_materialize_collaboration_inbox_on_mirror(" in source
        assert '"retainedForSource": True' in source
        assert f'notify_repo_mirrors(env, owner, repo, "{topic}")' in source


def test_mirror_claim_and_materialization_are_exact_and_non_destructive():
    calls = []

    async def blind_index(_env, value):
        return "bi:" + value

    async def d1_run(_env, sql, *args):
        calls.append(("run", sql, args))

    async def d1_first(_env, sql, *args):
        calls.append(("first", sql, args))
        return {"c": 2}

    ns = _load_helpers()
    ns.update({
        "Date": _Date,
        "blind_index": blind_index,
        "d1_run": d1_run,
        "d1_first": d1_first,
    })
    claimant = asyncio.run(ns["_claim_collaboration_inbox_on_mirror"](
        object(), "pull_inbox", "repo-bi", "signing-key"))
    assert claimant == "bi:collaboration-inbox-claim:pull_inbox:signing-key"
    assert "mirrored_at=0" in calls[0][1]

    count = asyncio.run(ns["_materialize_collaboration_inbox_on_mirror"](
        object(), _Request(), "pull_inbox", "repo-bi", claimant,
        "mirror-node"))
    assert count == 2
    update_sql = calls[-1][1]
    assert update_sql.startswith("UPDATE pull_inbox SET mirrored_by_bi=")
    assert "id IN (?,?)" in update_sql
    assert "DELETE" not in update_sql


def test_qt_mirror_poll_materializes_all_three_record_kinds():
    issues = (QT_ROOT / "MainWindowIssues.cpp").read_text(encoding="utf-8")
    pulls = (QT_ROOT / "MainWindowPulls.cpp").read_text(encoding="utf-8")
    discussions = (
        QT_ROOT / "MainWindowDiscussions.cpp").read_text(encoding="utf-8")

    poll = issues.split(
        "void MainWindow::pollMirrorIssueInboxes()", 1)[1].split(
            "void MainWindow::applyIssuesInboxPayload", 1)[0]
    assert "drainIssuesInboxFor(repo" in poll
    assert "drainPullsInboxFor(repo" in poll
    assert "drainDiscussionsInboxFor(repo" in poll
    for source in (pulls, discussions):
        assert 'QStringLiteral("mirror"), QStringLiteral("1")' in source
        assert "/*mirrorIntake=*/true" in source
        assert 'QStringLiteral("ids")' in source
