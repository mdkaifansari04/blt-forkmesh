#!/usr/bin/env python3
"""Self-contained test for tools/forkmesh_mcp_server.py (issue #366).

Spins up a throwaway git repo + a fresh Ed25519 identity, drives the server
over its stdio JSON-RPC transport, and checks each tool end-to-end — including
that written issues/PRs carry a signature that verifies against the identity's
public key (proving the byte-exact reproduction of the Qt client's signing).

Run:  python3 tools/test_forkmesh_mcp_server.py
"""

import base64
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

    git("init", "-q", "-b", "main")
    git("config", "user.email", "t@t")
    git("config", "user.name", "tester")
    (repo / "README.md").write_text("hello mesh\n")
    git("add", "README.md")
    git("commit", "-qm", "init")

    # initialize handshake
    resp = rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                "params": {"protocolVersion": "2025-06-18"}})
    check("initialize", resp["result"]["serverInfo"]["name"] == "forkmesh")

    # tools/list advertises all 7 tools
    resp = rpc({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
    names = {t["name"] for t in resp["result"]["tools"]}
    check("tools/list has 7 tools", names == {
        "list_repos", "read_file", "search_issues", "create_issue",
        "comment_on_issue", "open_pr_from_branch", "get_pr_diff"})

    def call(name, args):
        r = rpc({"jsonrpc": "2.0", "id": 9, "method": "tools/call",
                 "params": {"name": name, "arguments": args}})
        res = r["result"]
        return res["content"][0]["text"], res.get("isError", False)

    # list_repos
    text, err = call("list_repos", {})
    check("list_repos", not err and json.loads(text)[0]["name"] == "repo")

    # read_file
    text, err = call("read_file", {"path": "README.md"})
    check("read_file", not err and text == "hello mesh\n")
    _, err = call("read_file", {"path": "../escape"})
    check("read_file blocks traversal", err)

    # create_issue -> signed + verifiable
    text, err = call("create_issue", {"title": "First bug", "body": "it broke",
                                       "labels": ["bug"]})
    check("create_issue", not err and "#1" in text)
    fields, body = srv.read_frontmatter(repo / "issues/1/issue.md")
    ev = {"type": "open", "author": fields["author"], "ts": int(fields["ts"]),
          "attachments": [], "title": fields["title"], "body": body.strip("\n")}
    canonical = _canonical(1, ev)
    check("issue signature verifies", _verify(pub_b64, fields["sig"], canonical))
    check("issue committed", "issue #1" in subprocess.run(
        ["git", "-C", str(repo), "log", "-1", "--format=%s"],
        capture_output=True, text=True).stdout)

    # search_issues
    text, err = call("search_issues", {"query": "bug"})
    check("search_issues match", not err and json.loads(text)[0]["number"] == 1)
    text, _ = call("search_issues", {"query": "nonexistent-xyz"})
    check("search_issues no match", json.loads(text) == [])

    # comment_on_issue -> signed + verifiable
    _, err = call("comment_on_issue", {"number": 1, "body": "me too"})
    check("comment_on_issue", not err)
    cfile = repo / "issues/1/0002-comment.md"
    check("comment file written", cfile.exists())
    fields, body = srv.read_frontmatter(cfile)
    ev = {"type": "comment", "author": fields["author"], "ts": int(fields["ts"]),
          "attachments": [], "body": body.strip("\n")}
    check("comment signature verifies",
          _verify(pub_b64, fields["sig"], _canonical(1, ev)))

    # open_pr_from_branch + get_pr_diff
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


def _canonical(number, ev):
    import hashlib
    ch = hashlib.sha256(srv.issue_content_for_signing(ev).encode()).hexdigest()
    return (f"forkmesh-issue-event-v1\n{ev['type']}\n{number}\n"
            f"{ev['author']}\n{ev['ts']}\n{ch}").encode()


def _verify(pub_b64, sig_b64, canonical):
    try:
        Ed25519PublicKey.from_public_bytes(
            base64.urlsafe_b64decode(unpad(pub_b64))).verify(
                base64.urlsafe_b64decode(unpad(sig_b64)), canonical)
        return True
    except Exception:
        return False


if __name__ == "__main__":
    main()
