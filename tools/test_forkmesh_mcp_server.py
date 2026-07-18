#!/usr/bin/env python3
"""Self-contained test for tools/forkmesh_mcp_server.py (issues #366, #433).

Spins up a throwaway git repo + a fresh Ed25519 identity, drives the server
over its stdio JSON-RPC transport, and checks each tool end-to-end — including
that written issues/milestones/projects/PRs land in the native signed tracker
layout (.forkmesh/issues/{open,closed}/<n>/issue-<n>.json,
.forkmesh/projects/<n>/project-<n>.json) and carry signatures that verify
against the identity's public key (proving the byte-exact reproduction of the
Qt client's IssueStore/ProjectStore signing).

The repo is seeded with an existing open issue, a closed issue, and a project
so number allocation is checked across both status folders and past projects.

Run:  python3 tools/test_forkmesh_mcp_server.py
"""

import base64
import hashlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey, Ed25519PublicKey)

HERE = Path(__file__).resolve().parent
SERVER = HERE / "forkmesh_mcp_server.py"

_spec = importlib.util.spec_from_file_location("srv", SERVER)
srv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(srv)

FAILURES = []

MAX_SAFE_INT = 9007199254740991

# ---- shared drift fixtures ---------------------------------------------------
# One fixture per event type, pinning the exact contentForSigning bytes of
# qt_client/src/IssueStore.cpp and ProjectStore.cpp (and the relay Worker's
# issue_event_content). If the server's port drifts from these rules, the
# comparison below fails before any signature check ever runs.
ISSUE_CONTENT_FIXTURES = [
    ({"type": "open", "title": "T", "body": "B",
      "attachments": ["a.png", "b.png"]}, "T\x00B\x00a.png,b.png"),
    ({"type": "comment", "body": "C", "attachments": []}, "C\x00"),
    ({"type": "edit", "body": "E", "attachments": ["x"]}, "E\x00x"),
    ({"type": "title", "title": "New"}, "New"),
    ({"type": "status", "status": "closed"}, "closed"),
    ({"type": "labels", "labels": ["bug", "ui"]}, "bug,ui"),
    ({"type": "milestone", "milestone": "v1"}, "v1"),
    ({"type": "dates", "startDate": 1784347200000, "endDate": 1784433600000},
     "1784347200000\x001784433600000"),
    ({"type": "priority", "priority": 3}, "3"),
    ({"type": "progress", "progress": 40}, "40"),
    ({"type": "bounty", "bountyUsd": 12.5, "bountyAddress": "addr",
      "bountyStatus": "funded"}, "12.50\x00addr\x00funded"),
    ({"type": "assignees", "assignees": ["a", "b"]}, "a,b"),
    ({"type": "agent", "agentProvider": "claude-code", "agentSessionId": 7,
      "agentStatus": "queued", "agentCreatePr": False},
     "claude-code\x007\x00queued\x00no-pr"),
    ({"type": "agent", "agentProvider": "codex", "agentSessionId": 8,
      "agentStatus": "done", "agentCreatePr": True},
     "codex\x008\x00done\x00pr"),
    ({"type": "delete", "target": "self"}, "self"),
    ({"type": "vote"}, ""),
]

PROJECT_CONTENT_FIXTURES = [
    ({"type": "open", "title": "T", "body": "B"}, "T\x00B"),
    ({"type": "title", "title": "N"}, "N"),
    ({"type": "edit", "body": "E"}, "E"),
    ({"type": "status", "status": "closed"}, "closed"),
    ({"type": "dates", "startDate": 1, "endDate": 2}, "1\x002"),
    ({"type": "milestone", "milestone": "M"}, "M"),
    ({"type": "issues", "issues": [385, 384, 384]}, "384,385"),
    ({"type": "delete", "target": "self"}, "self"),
]


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILURES.append(name)


def rpc(request):
    """Round-trip one JSON-RPC request through serve() with a fresh module state."""
    out = io.StringIO()
    srv.serve(io.StringIO(json.dumps(request) + "\n"), out)
    line = out.getvalue().strip()
    return json.loads(line) if line else None


def unpad(s):
    return s + "=" * (-len(s) % 4)


def _canonical(kind, number, ev, content):
    ch = hashlib.sha256(content.encode()).hexdigest()
    return (f"forkmesh-{kind}-event-v1\n{ev['type']}\n{number}\n"
            f"{ev['author']}\n{ev['ts']}\n{ch}").encode()


def _verify(pub_b64, sig_b64, canonical):
    try:
        Ed25519PublicKey.from_public_bytes(
            base64.urlsafe_b64decode(unpad(pub_b64))).verify(
                base64.urlsafe_b64decode(unpad(sig_b64)), canonical)
        return True
    except Exception:
        return False


def verify_issue_event(pub_b64, number, ev):
    return _verify(pub_b64, ev["sig"],
                   _canonical("issue", number, ev,
                              srv.issue_content_for_signing(ev)))


def verify_project_event(pub_b64, number, ev):
    return _verify(pub_b64, ev["sig"],
                   _canonical("project", number, ev,
                              srv.project_content_for_signing(ev)))


def _safe_int(v):
    return isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= MAX_SAFE_INT


def issue_loads_strict(record, number):
    """Python port of IssueStore's strictIssueObjectValid — the schema the
    desktop's strict loader (and the browser reader) accepts without repair."""
    if record.get("schema") != "forkmesh-issue-v1":
        return False
    if record.get("number") != number:
        return False
    for key in ("createdAt", "startDate", "endDate"):
        if key in record and not _safe_int(record[key]):
            return False
    events = record.get("events")
    if not isinstance(events, list):
        return False
    for ev in events:
        if not isinstance(ev, dict):
            return False
        if not (isinstance(ev.get("type"), str) and ev["type"]):
            return False
        if not (isinstance(ev.get("author"), str) and ev["author"]):
            return False
        if not _safe_int(ev.get("ts")):
            return False
        if not (isinstance(ev.get("sig"), str) and ev["sig"]):
            return False
        atts = ev.get("attachments")
        if atts is not None and not (
                isinstance(atts, list) and all(isinstance(a, str) for a in atts)):
            return False
        if ev["type"] == "open" and not (
                isinstance(ev.get("title"), str) and
                isinstance(ev.get("body"), str) and
                isinstance(ev.get("attachments"), list)):
            return False
    return True


def project_loads_strict(record, number):
    """The shape Project::fromJson / the dashboard reader consume."""
    if record.get("schema") != "forkmesh-project-v1":
        return False
    if record.get("number") != number:
        return False
    if not isinstance(record.get("title"), str):
        return False
    for key in ("createdAt", "updatedAt", "startDate", "endDate"):
        if key in record and not _safe_int(record[key]):
            return False
    issues = record.get("issues")
    if not (isinstance(issues, list) and
            all(isinstance(n, int) for n in issues)):
        return False
    events = record.get("events")
    if not isinstance(events, list) or not events:
        return False
    for ev in events:
        if not (isinstance(ev, dict) and ev.get("type") and ev.get("author")
                and _safe_int(ev.get("ts")) and ev.get("sig")):
            return False
    return True


def seed_issue(repo, number, status, title):
    d = repo / f".forkmesh/issues/{status}/{number}"
    d.mkdir(parents=True)
    rec = {
        "schema": "forkmesh-issue-v1", "number": number, "title": title,
        "status": status, "labels": [], "milestone": "", "startDate": 0,
        "endDate": 0, "priority": 0, "progress": 0, "assignees": [],
        "createdAt": 1, "updatedAt": 1, "author": "seed-key",
        "authorName": "seed", "bountyUsd": 0, "bountyAddress": "",
        "bountyStatus": "", "votes": 0,
        "events": [{"type": "open", "id": f"open-{number}",
                    "author": "seed-key", "authorName": "seed", "ts": 1,
                    "title": title, "body": "seeded", "attachments": [],
                    "sig": "seed-sig"}],
    }
    (d / f"issue-{number}.json").write_text(json.dumps(rec, indent=4) + "\n")


def seed_project(repo, number, title):
    d = repo / f".forkmesh/projects/{number}"
    d.mkdir(parents=True)
    rec = {
        "schema": "forkmesh-project-v1", "number": number, "title": title,
        "body": "seeded", "status": "open", "startDate": 0, "endDate": 0,
        "milestone": "", "issues": [], "createdAt": 1, "updatedAt": 1,
        "author": "seed-key", "authorName": "seed",
        "events": [{"type": "open", "id": f"open-{number}",
                    "author": "seed-key", "authorName": "seed", "ts": 1,
                    "title": title, "body": "seeded", "sig": "seed-sig"}],
    }
    (d / f"project-{number}.json").write_text(json.dumps(rec, indent=4) + "\n")


def main():
    tmp = Path(tempfile.mkdtemp(prefix="fmmcp-"))
    repo = tmp / "repo"
    repo.mkdir()

    # fresh identity at the path the server reads
    data_home = tmp / "data"
    key_dir = data_home / "ForkMesh/ForkMesh/identity"
    key_dir.mkdir(parents=True)
    priv = Ed25519PrivateKey.generate()
    (key_dir / "ed25519.pem").write_bytes(priv.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))
    pub_raw = priv.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    pub_b64 = base64.urlsafe_b64encode(pub_raw).decode().rstrip("=")

    os.environ["XDG_DATA_HOME"] = str(data_home)
    os.environ["FORKMESH_REPO"] = str(repo)
    # reset cached identity/paths picked up at import time
    srv.KEY_PATH = key_dir / "ed25519.pem"
    srv._key = srv._pub = None

    def git(*args):
        subprocess.run(["git", "-C", str(repo), *args], check=True,
                       capture_output=True)

    def last_commit():
        return subprocess.run(
            ["git", "-C", str(repo), "log", "-1", "--format=%s"],
            capture_output=True, text=True).stdout.strip()

    git("init", "-q", "-b", "main")
    git("config", "user.email", "t@t")
    git("config", "user.name", "tester")
    (repo / "README.md").write_text("hello mesh\n")
    # Pre-existing native tracker state: an open issue, a closed issue, and a
    # project, so allocation must span both status folders and past projects.
    seed_issue(repo, 1, "open", "Seeded open issue")
    seed_issue(repo, 2, "closed", "Seeded closed issue")
    seed_project(repo, 1, "Seeded project")
    git("add", "-A")
    git("commit", "-qm", "init")

    # canonical signing rules must match the native stores' fixtures exactly
    check("issue contentForSigning matches native fixtures", all(
        srv.issue_content_for_signing(ev) == expected
        for ev, expected in ISSUE_CONTENT_FIXTURES))
    check("project contentForSigning matches native fixtures", all(
        srv.project_content_for_signing(ev) == expected
        for ev, expected in PROJECT_CONTENT_FIXTURES))

    # initialize handshake
    resp = rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                "params": {"protocolVersion": "2025-06-18"}})
    check("initialize", resp["result"]["serverInfo"]["name"] == "forkmesh")

    # tools/list advertises all 11 tools
    resp = rpc({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
    names = {t["name"] for t in resp["result"]["tools"]}
    check("tools/list has 11 tools", names == {
        "list_repos", "read_file", "search_issues", "create_issue",
        "comment_on_issue", "create_milestone", "update_milestone",
        "create_project", "update_project", "open_pr_from_branch",
        "get_pr_diff"})

    def call(name, args):
        r = rpc({"jsonrpc": "2.0", "id": 9, "method": "tools/call",
                 "params": {"name": name, "arguments": args}})
        res = r["result"]
        return res["content"][0]["text"], res.get("isError", False)

    # list_repos
    text, err = call("list_repos", {})
    check("list_repos", not err and json.loads(text)[0]["name"] == "repo")
    check("list_repos counts seeded open issue",
          json.loads(text)[0]["openIssues"] == 1)

    # read_file
    text, err = call("read_file", {"path": "README.md"})
    check("read_file", not err and text == "hello mesh\n")
    _, err = call("read_file", {"path": "../escape"})
    check("read_file blocks traversal", err)
    # Absolute paths must not escape the repo: Path(repo) / "/etc/passwd"
    # collapses to "/etc/passwd", so a ".."-only guard would leak any file
    # (including the node's Ed25519 identity key).
    _, err = call("read_file", {"path": "/etc/passwd"})
    check("read_file blocks absolute path", err)

    # ---- issues -------------------------------------------------------------
    # create_issue allocates past the seeded closed issue and writes the
    # native signed-event record, never the legacy Markdown layout.
    text, err = call("create_issue", {
        "title": "First bug", "body": "it broke", "labels": ["bug"],
        "start_date": "2026-07-20", "end_date": "2026-07-24"})
    check("create_issue spans open+closed numbering", not err and "#3" in text)
    ipath = repo / ".forkmesh/issues/open/3/issue-3.json"
    check("issue record at native path", ipath.is_file())
    check("no legacy markdown layout",
          not (repo / "issues").exists() and
          not list(repo.glob(".forkmesh/issues/**/issue.md")))
    rec = json.loads(ipath.read_text())
    check("issue loads without repair (strict native schema)",
          issue_loads_strict(rec, 3))
    check("issue record fields", rec["title"] == "First bug" and
          rec["status"] == "open" and rec["labels"] == ["bug"] and
          rec["startDate"] > 0 and rec["endDate"] > rec["startDate"])
    check("issue has open + dates events",
          [e["type"] for e in rec["events"]] == ["open", "dates"])
    check("every issue event signature verifies",
          all(verify_issue_event(pub_b64, 3, e) for e in rec["events"]))
    check("issue committed with native message",
          last_commit() == "issue #3: First bug")

    # second dated issue (epoch-ms dates)
    text, err = call("create_issue", {
        "title": "Second bug", "body": "also broke",
        "start_date": 1784433600000, "end_date": 1784520000000})
    check("second dated issue is #4", not err and "#4" in text)
    rec4 = json.loads(
        (repo / ".forkmesh/issues/open/4/issue-4.json").read_text())
    check("second issue verifies + strict", issue_loads_strict(rec4, 4) and
          all(verify_issue_event(pub_b64, 4, e) for e in rec4["events"]))

    # invalid dates fail with no partial record
    _, err = call("create_issue", {"title": "Bad", "start_date": "not-a-date"})
    check("create_issue rejects invalid date", err)
    _, err = call("create_issue", {"title": "Bad", "start_date": "2026-07-24",
                                   "end_date": "2026-07-20"})
    check("create_issue rejects end before start", err)
    check("no partial record after date errors",
          not any(d.exists() for d in srv.issue_dir_candidates(repo, 5)))

    # a stale number read (concurrent writer took it) retries with a fresh one
    real_next = srv.next_issue_number
    calls = {"n": 0}

    def stale_next(r):
        calls["n"] += 1
        return 3 if calls["n"] == 1 else real_next(r)

    srv.next_issue_number = stale_next
    try:
        text, err = call("create_issue", {"title": "After stale read"})
    finally:
        srv.next_issue_number = real_next
    check("stale-number collision retries", not err and "#5" in text)

    # a collision that lands between our write and commit is re-read and the
    # partial record is withdrawn before retrying
    real_sign = srv.sign_issue_event
    raced = {"done": False}

    def racing_sign(number, ev):
        if not raced["done"]:
            raced["done"] = True
            d = repo / f".forkmesh/issues/closed/{number}"
            d.mkdir(parents=True)
            (d / f"issue-{number}.json").write_text(json.dumps({
                "schema": "forkmesh-issue-v1", "number": number,
                "title": "raced", "status": "closed", "events": []}) + "\n")
        return real_sign(number, ev)

    srv.sign_issue_event = racing_sign
    try:
        text, err = call("create_issue", {"title": "After race"})
    finally:
        srv.sign_issue_event = real_sign
    check("post-write collision re-reads before commit",
          not err and "#7" in text and
          not (repo / ".forkmesh/issues/open/6").exists() and
          (repo / ".forkmesh/issues/open/7/issue-7.json").is_file())

    # search_issues reads the native records; folder is authoritative
    text, err = call("search_issues", {"query": "bug"})
    check("search_issues match", not err and
          [r["number"] for r in json.loads(text)] == [3, 4])
    text, _ = call("search_issues", {"query": "", "status": "closed"})
    check("search_issues closed folder",
          2 in [r["number"] for r in json.loads(text)])
    text, _ = call("search_issues", {"query": "nonexistent-xyz"})
    check("search_issues no match", json.loads(text) == [])

    # comment_on_issue appends a signed event to the record
    _, err = call("comment_on_issue", {"number": 3, "body": "me too"})
    check("comment_on_issue", not err)
    rec = json.loads(ipath.read_text())
    ev = rec["events"][-1]
    check("comment appended to record", ev["type"] == "comment" and
          ev["body"] == "me too")
    check("comment signature verifies", verify_issue_event(pub_b64, 3, ev))
    check("comment committed", last_commit() == "issue #3: comment")
    _, err = call("comment_on_issue", {"number": 99, "body": "x"})
    check("comment_on_issue unknown issue", err)

    # ---- milestones ---------------------------------------------------------
    _, err = call("create_milestone", {"title": "Sprint A",
                                       "due": "2026-08-01",
                                       "description": "first sprint"})
    check("create_milestone", not err)
    _, err = call("create_milestone", {"title": "Sprint B"})
    check("second milestone", not err)
    ms = json.loads((repo / ".forkmesh/issues/milestones.json").read_text())
    by_title = {m["title"]: m for m in ms}
    check("milestones.json has both", set(by_title) == {"Sprint A", "Sprint B"})
    check("milestone fields", by_title["Sprint A"]["due"] > 0 and
          by_title["Sprint A"]["status"] == "open" and
          by_title["Sprint A"]["description"] == "first sprint" and
          by_title["Sprint B"]["due"] == 0)
    check("milestones committed", last_commit() == "issues: update milestones")
    text, err = call("create_milestone", {"title": "Sprint A"})
    check("duplicate milestone title rejected", err and "already exists" in text)

    _, err = call("update_milestone", {"title": "Sprint B",
                                       "due": 1785556800000,
                                       "status": "closed"})
    ms = json.loads((repo / ".forkmesh/issues/milestones.json").read_text())
    by_title = {m["title"]: m for m in ms}
    check("update_milestone", not err and
          by_title["Sprint B"]["due"] == 1785556800000 and
          by_title["Sprint B"]["status"] == "closed")
    text, err = call("update_milestone", {"title": "Nope", "due": 1})
    check("update_milestone unknown title", err and "unknown milestone" in text)
    _, err = call("update_milestone", {"title": "Sprint A", "status": "wat"})
    check("update_milestone bad status", err)

    # ---- projects -----------------------------------------------------------
    text, err = call("create_project", {
        "title": "Roadmap Q3", "body": "the plan",
        "start_date": "2026-07-20", "end_date": "2026-08-01",
        "milestone": "Sprint A", "issues": [4, 3, 4]})
    check("create_project past existing project", not err and "#2" in text)
    ppath = repo / ".forkmesh/projects/2/project-2.json"
    check("project record at native path", ppath.is_file())
    prec = json.loads(ppath.read_text())
    check("project loads without repair (native schema)",
          project_loads_strict(prec, 2))
    check("project links sorted deduped issues", prec["issues"] == [3, 4])
    check("project fields", prec["title"] == "Roadmap Q3" and
          prec["milestone"] == "Sprint A" and prec["status"] == "open" and
          prec["startDate"] > 0 and prec["endDate"] > prec["startDate"])
    check("project events open/dates/milestone/issues",
          [e["type"] for e in prec["events"]] ==
          ["open", "dates", "milestone", "issues"])
    check("every project event signature verifies",
          all(verify_project_event(pub_b64, 2, e) for e in prec["events"]))
    check("project committed with native message",
          last_commit() == "projects: #2 Roadmap Q3")

    # errors are actionable and leave no partial record
    text, err = call("create_project", {"title": "Bad", "issues": [999]})
    check("create_project unknown issue link", err and "#999" in text)
    text, err = call("create_project", {"title": "Bad", "milestone": "Nope"})
    check("create_project unknown milestone",
          err and "unknown milestone" in text)
    _, err = call("create_project", {"title": "Bad", "start_date": "junk"})
    check("create_project invalid date", err)
    check("no partial project record after errors",
          not (repo / ".forkmesh/projects/3").exists())

    # update_project appends signed events with native commit messages
    _, err = call("update_project", {"number": 2, "issues": [3],
                                     "milestone": "Sprint B"})
    check("update_project", not err)
    prec = json.loads(ppath.read_text())
    check("update_project folds into metadata", prec["issues"] == [3] and
          prec["milestone"] == "Sprint B")
    check("update_project appended signed events",
          [e["type"] for e in prec["events"][-2:]] == ["milestone", "issues"]
          and all(verify_project_event(pub_b64, 2, e)
                  for e in prec["events"][-2:]))
    check("update_project committed", last_commit() == "projects: #2 issues")
    _, err = call("update_project", {"number": 2,
                                     "start_date": 1, "end_date": 2})
    prec = json.loads(ppath.read_text())
    check("update_project dates", not err and prec["startDate"] == 1 and
          prec["endDate"] == 2 and last_commit() == "projects: #2 dates")
    _, err = call("update_project", {"number": 42, "issues": [3]})
    check("update_project unknown project", err)
    _, err = call("update_project", {"number": 2})
    check("update_project needs a field", err)
    _, err = call("update_project", {"number": 2, "issues": [999]})
    check("update_project unknown issue link", err)

    # ---- pulls (unchanged legacy path) --------------------------------------
    git("checkout", "-q", "-b", "feature")
    (repo / "feature.txt").write_text("new\n")
    git("add", "feature.txt")
    git("commit", "-qm", "add feature")
    git("checkout", "-q", "main")
    text, err = call("open_pr_from_branch", {"branch": "feature",
                                             "title": "Add feature"})
    check("open_pr_from_branch", not err and "#1" in text)
    check("pull.md written", (repo / "pulls/1/pull.md").exists())
    text, err = call("open_pr_from_branch", {"branch": "feature"})
    check("open_pr idempotent", not err and "already open" in text)
    text, err = call("get_pr_diff", {"number": 1})
    check("get_pr_diff", not err and "feature.txt" in text)

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILURE(S): {FAILURES}")
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
