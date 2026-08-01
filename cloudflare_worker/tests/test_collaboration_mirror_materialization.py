#!/usr/bin/env python3
"""Online mirrors merge collaboration submissions and drain the queue.

Web-submitted issues, pull requests, and discussions no longer wait as
"pending" for the source-of-truth node to come online: any approved online
mirror leases the rows, commits them onto the branch it serves, and its
?mirror=1 acknowledgement deletes them from the inbox outright. The ack also
carries the mirror's signed post-merge refs attestation so the newly served
state joins the clone integrity pin window while the source is offline.
"""

import ast
import asyncio
import re
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


def _load_helpers(names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    assert {node.name for node in selected} == set(names)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    namespace = {
        "parse_qs": parse_qs,
        "urlparse": urlparse,
        "re": re,
        "ISSUE_INBOX_CLAIM_TTL_MS": 300_000,
    }
    namespace.update(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Date:
    @staticmethod
    def now():
        return 1_000_000


class _Request:
    url = "https://forkmesh.test/api/repo/o/r/pulls?ids=7,9"


def test_handlers_offer_mirror_leases_and_drain_on_ack():
    for handler_name, table, topic in (
        ("pulls_handler", "pull_inbox", "pulls"),
        ("discussions_handler", "discussion_inbox", "discussions"),
    ):
        source = _function_source(handler_name)
        assert "_authorized_mirror_issue_signing_key(" in source
        assert "_claim_collaboration_inbox_on_mirror(" in source
        assert table in source
        # The mirror ack is a real drain now, and it records the mirror's
        # post-merge refs attestation so the served state stays pinned.
        assert "_drain_collaboration_inbox_on_mirror(" in source
        assert "_record_mirror_attested_state(" in source
        assert '"retainedForSource": False' in source
        assert '"retainedForSource": True' not in source
        assert f'notify_repo_mirrors(env, owner, repo, "{topic}")' in source

    issues = _function_source("issues_handler")
    assert "_drain_issue_inbox_on_mirror(" in issues
    assert "_record_mirror_attested_state(" in issues
    assert '"retainedForSource": False' in issues
    assert '"retainedForSource": True' not in issues


def test_mirror_claim_is_exact_and_ack_deletes_only_leased_rows():
    calls = []

    async def blind_index(_env, value):
        return "bi:" + value

    async def d1_run(_env, sql, *args):
        calls.append(("run", sql, args))

    async def d1_first(_env, sql, *args):
        calls.append(("first", sql, args))
        return {"c": 2}

    ns = _load_helpers(
        {
            "_drain_ids_from_request",
            "_claim_collaboration_inbox_on_mirror",
            "_drain_collaboration_inbox_on_mirror",
            "_log_inbox_drain",
        },
        extra_globals={
            "Date": _Date,
            "blind_index": blind_index,
            "d1_run": d1_run,
            "d1_first": d1_first,
        },
    )
    claimant = asyncio.run(ns["_claim_collaboration_inbox_on_mirror"](
        object(), "pull_inbox", "repo-bi", "signing-key"))
    assert claimant == "bi:collaboration-inbox-claim:pull_inbox:signing-key"
    assert "mirrored_at=0" in calls[0][1]

    count = asyncio.run(ns["_drain_collaboration_inbox_on_mirror"](
        object(), _Request(), "pull_inbox", "repo-bi", claimant,
        "mirror-node"))
    assert count == 2
    deletes = [c for c in calls if c[1].startswith("DELETE FROM pull_inbox")]
    assert len(deletes) == 1
    # The delete names the exact acknowledged rows and stays scoped to this
    # mirror's lease, so a submission that lands mid-drain survives and a
    # competing claimant can never sweep another mirror's rows.
    assert "id IN (?,?)" in deletes[0][1]
    assert "claimed_by_bi=?" in deletes[0][1]
    assert deletes[0][2] == ("repo-bi", 7, 9, claimant)
    logs = [c for c in calls if c[1].startswith("INSERT INTO inbox_drain_log")]
    assert len(logs) == 1 and logs[0][2][2] == "pulls"


def test_issue_mirror_ack_deletes_only_leased_rows():
    calls = []

    async def d1_run(_env, sql, *args):
        calls.append(("run", sql, args))

    async def d1_first(_env, sql, *args):
        return {"c": 1}

    ns = _load_helpers(
        {
            "_drain_ids_from_request",
            "_drain_issue_inbox_on_mirror",
            "_log_inbox_drain",
        },
        extra_globals={
            "Date": _Date,
            "d1_run": d1_run,
            "d1_first": d1_first,
        },
    )
    count = asyncio.run(ns["_drain_issue_inbox_on_mirror"](
        object(), _Request(), "repo-bi", "claimant-bi", "mirror-node"))
    assert count == 1
    deletes = [c for c in calls if c[1].startswith("DELETE FROM issue_inbox")]
    assert len(deletes) == 1
    assert "id IN (?,?)" in deletes[0][1]
    assert "claimed_by_bi=?" in deletes[0][1]
    logs = [c for c in calls if c[1].startswith("INSERT INTO inbox_drain_log")]
    assert len(logs) == 1 and logs[0][2][2] == "issues"


def _attestation_namespace(runs, verify_result=True):
    async def blind_index(_env, value):
        return "bi:" + value

    async def d1_run(_env, sql, *args):
        runs.append((sql, args))

    async def _owner_signing_pubkeys(_env, node):
        return ["mirror-public-key"] if node == "mirror-node" else []

    seen = {}

    async def ed25519_verify(pub, sig, canonical):
        seen["pub"] = pub
        seen["canonical"] = canonical
        return verify_result and sig == "good-sig"

    def clean_string(value, limit):
        return str(value or "")[:limit]

    ns = _load_helpers(
        {"_record_mirror_attested_state"},
        extra_globals={
            "Date": _Date,
            "LOGIN_MAX_SKEW_MS": 120_000,
            "STATE_PIN_HISTORY": 100,
            "blind_index": blind_index,
            "d1_run": d1_run,
            "_owner_signing_pubkeys": _owner_signing_pubkeys,
            "ed25519_verify": ed25519_verify,
            "clean_string": clean_string,
        },
    )
    return ns, seen


def test_mirror_state_attestation_joins_the_pin_history():
    digest = "ab" * 32

    class Request:
        url = ("https://forkmesh.test/api/repo/o/r/issues?mirror=1&ids=3"
               "&state=%s&stateTs=1000000&stateSig=good-sig" % digest)

    runs = []
    ns, seen = _attestation_namespace(runs)
    ok = asyncio.run(ns["_record_mirror_attested_state"](
        object(), Request(), "o", "r", "mirror-node"))
    assert ok is True
    # The canonical matches what the desktop signs on catalog publishes, bound
    # to the acknowledging mirror node and the exact repo segment.
    assert seen["canonical"] == (
        "forkmesh-repostate-v1\nmirror-node\nr\n%s\n1000000" % digest
    ).encode()
    inserts = [r for r in runs
               if r[0].startswith("INSERT INTO repo_state_history")]
    assert len(inserts) == 1
    assert inserts[0][1] == ("bi:o/r", digest, _Date.now())
    trims = [r for r in runs
             if r[0].startswith("DELETE FROM repo_state_history")]
    assert len(trims) == 1 and trims[0][1][-1] == 100


def test_mirror_state_attestation_rejects_bad_or_stale_signatures():
    digest = "cd" * 32

    class BadSig:
        url = ("https://forkmesh.test/api/repo/o/r/issues?state=%s"
               "&stateTs=1000000&stateSig=forged" % digest)

    class Stale:
        url = ("https://forkmesh.test/api/repo/o/r/issues?state=%s"
               "&stateTs=1&stateSig=good-sig" % digest)

    for request in (BadSig(), Stale()):
        runs = []
        ns, _ = _attestation_namespace(runs)
        ok = asyncio.run(ns["_record_mirror_attested_state"](
            object(), request, "o", "r", "mirror-node"))
        assert ok is False
        assert not runs


def test_qt_mirror_poll_merges_all_three_record_kinds():
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


def test_qt_mirror_acks_attest_served_state_and_keep_owner_only_rows():
    issues = (QT_ROOT / "MainWindowIssues.cpp").read_text(encoding="utf-8")
    pulls = (QT_ROOT / "MainWindowPulls.cpp").read_text(encoding="utf-8")
    discussions = (
        QT_ROOT / "MainWindowDiscussions.cpp").read_text(encoding="utf-8")

    # Every mirror ack carries the fresh signed refs attestation the relay
    # pins, and the helper signs the same repostate canonical publishes use.
    for source in (issues, pulls, discussions):
        assert "appendMirrorStateAttestation(&" in source
    helper = pulls.split(
        "void MainWindow::appendMirrorStateAttestation", 1)[1].split(
            "void MainWindow::scheduleRelaySync()", 1)[0]
    assert "forkmesh-repostate-v1" in helper
    assert "mirrorStateHash(repo.mirrorPath)" in helper

    # Owner-only side effects survive the drain: rows carrying a fediverse
    # mention id or the owner's auto-agent request stay queued for the source
    # node instead of being acknowledged by a mirror.
    assert "validMentionId || (mirrorIntake && meta.wantsAgent)" in issues
