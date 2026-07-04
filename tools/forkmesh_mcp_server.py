#!/usr/bin/env python3
"""ForkMesh MCP server — expose the mesh (repos, issues, PRs) as MCP tools.

Roadmap Phase 5 / issue #366. Any agent tooling — not just the built-in Claude
Code / Codex integrations — should be able to work the mesh. This is a
Model Context Protocol server (https://modelcontextprotocol.io/specification)
speaking JSON-RPC 2.0 over the stdio transport (newline-delimited JSON on
stdin/stdout, diagnostics on stderr). stdio is fine for local-first: Claude Code
launches it as a subprocess and auto-discovers it from a `.mcp.json` entry.

Read tools:  list_repos, read_file, search_issues, get_pr_diff
Write tools: create_issue, comment_on_issue, open_pr_from_branch

Write tools sign with the node identity key and write the exact same native
ForkMesh entries the Qt client's IssueStore / PullStore write — no privileged
side door. The signing is reproduced byte-for-byte from:
    qt_client/src/IssueStore.cpp::canonicalString / contentForSigning
    qt_client/src/PullStore.cpp    (see tools/branch_pr_review.py)

Identity key (Ed25519 PEM):
    $XDG_DATA_HOME/ForkMesh/ForkMesh/identity/ed25519.pem

Which repo the tools act on:
  * FORKMESH_REPO       — path to a single repo checkout (default: the git repo
                          containing this script, i.e. the ForkMesh repo).
  * FORKMESH_REPOS_DIR  — a directory of repo checkouts; list_repos enumerates
                          each git repo under it and every tool accepts a
                          `repo` argument naming which one to act on.
"""

import base64
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import uuid
from pathlib import Path

try:
    from cryptography.hazmat.primitives import serialization
except ImportError:  # pragma: no cover - dependency hint
    sys.stderr.write(
        "forkmesh-mcp: missing 'cryptography' (pip install cryptography)\n")
    raise

PROTOCOL_VERSION = "2025-06-18"
SERVER_INFO = {"name": "forkmesh", "version": "0.1.0"}
BASE_BRANCH = "main"

DATA_HOME = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local/share")
KEY_PATH = Path(DATA_HOME) / "ForkMesh/ForkMesh/identity/ed25519.pem"


def log(msg):
    sys.stderr.write(f"forkmesh-mcp: {msg}\n")
    sys.stderr.flush()


# ----------------------------------------------------------------- git helpers
def git(repo, *args, text=True):
    return subprocess.run(
        ["git", "-C", str(repo), *args], capture_output=True,
        text=text, errors="replace" if text else None,
    )


def git_out(repo, *args):
    r = git(repo, *args)
    return r.stdout if r.returncode == 0 else ""


def git_bytes(repo, *args):
    return git(repo, *args, text=False).stdout


def is_git_repo(path):
    p = Path(path)
    return (p / ".git").exists()


# ----------------------------------------------------------------- repo lookup
def default_repo():
    env = os.environ.get("FORKMESH_REPO")
    if env:
        return Path(env).resolve()
    # The git repo this script lives in (tools/ is at the repo root).
    return Path(__file__).resolve().parent.parent


def repos_root():
    env = os.environ.get("FORKMESH_REPOS_DIR")
    return Path(env).resolve() if env else None


def list_repo_paths():
    root = repos_root()
    if root and root.is_dir():
        return sorted(d for d in root.iterdir() if d.is_dir() and is_git_repo(d))
    d = default_repo()
    return [d] if is_git_repo(d) else []


def resolve_repo(name):
    """Map an optional `repo` argument to a checkout path, else the default."""
    if not name:
        d = default_repo()
        if not is_git_repo(d):
            raise ValueError(f"{d} is not a git repository")
        return d
    for p in list_repo_paths():
        if p.name == name or str(p) == name:
            return p
    raise ValueError(f"unknown repo: {name!r}")


# ------------------------------------------------------------------- identity
_key = None
_pub = None


def load_identity():
    global _key, _pub
    if _key is None:
        if not KEY_PATH.exists():
            raise ValueError(f"identity key not found at {KEY_PATH}")
        _key = serialization.load_pem_private_key(KEY_PATH.read_bytes(), password=None)
        raw = _key.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        _pub = base64.urlsafe_b64encode(raw).decode().rstrip("=")
    return _key, _pub


def b64url(data):
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def author_name(repo):
    return git_out(repo, "config", "user.name").strip() or "forkmesh"


def strip_edge_newlines(text):
    return text.strip("\r\n")


def serialize_list(items):
    return "[" + ", ".join(items) + "]"


# -------------------------------------------------------- issue signing/writing
# Mirrors qt_client/src/IssueStore.cpp exactly.
def issue_content_for_signing(ev):
    t = ev["type"]
    nul = "\x00"
    atts = ",".join(ev.get("attachments", []))
    if t == "open":
        return ev.get("title", "") + nul + ev.get("body", "") + nul + atts
    if t in ("comment", "edit"):
        return ev.get("body", "") + nul + atts
    return ""


def sign_issue_event(number, ev):
    content_hash = hashlib.sha256(
        issue_content_for_signing(ev).encode()).hexdigest()
    canonical = (
        "forkmesh-issue-event-v1\n"
        f"{ev['type']}\n{number}\n{ev['author']}\n{ev['ts']}\n{content_hash}"
    ).encode()
    return b64url(load_identity()[0].sign(canonical))


def issues_dir(repo):
    return Path(repo) / "issues"


def next_issue_number(repo):
    d = issues_dir(repo)
    mx = 0
    if d.exists():
        for entry in d.iterdir():
            if entry.is_dir() and entry.name.isdigit():
                mx = max(mx, int(entry.name))
    return mx + 1


def read_frontmatter(path):
    text = Path(path).read_text(errors="replace")
    m = re.match(r"^---\n(.*?)\n---\n?(.*)$", text, re.S)
    if not m:
        return {}, text
    fields = {}
    for line in m.group(1).splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            fields[k.strip()] = v.strip()
    return fields, m.group(2).lstrip("\n")


def create_issue(repo, title, body, labels, milestone, priority, assignees):
    _, pub = load_identity()
    ts = int(time.time() * 1000)
    body = strip_edge_newlines(body or "")
    number = next_issue_number(repo)
    ev = {
        "type": "open", "id": f"open-{number}", "title": title, "body": body,
        "attachments": [], "author": pub, "ts": ts,
    }
    ev["sig"] = sign_issue_event(number, ev)

    lines = [
        "---",
        "schema: forkmesh-issue-v1",
        f"number: {number}",
        f"title: {title}",
        "status: open",
        f"labels: {serialize_list(labels)}",
        f"milestone: {milestone}",
        f"priority: {max(0, min(99, priority))}",
        "progress: 0",
        f"assignees: {serialize_list(assignees)}",
        f"createdAt: {ts}",
        f"author: {pub}",
        f"authorName: {author_name(repo)}",
        "bountyUsd: 0.00",
        "bountyAddress: ",
        "bountyStatus: ",
        "type: open",
        f"id: {ev['id']}",
        f"ts: {ts}",
        "attachments: []",
        f"sig: {ev['sig']}",
        "---",
        "",
    ]
    idir = issues_dir(repo) / str(number)
    idir.mkdir(parents=True, exist_ok=True)
    (idir / "issue.md").write_text("\n".join(lines) + "\n" + body + "\n")

    _commit(repo, "issues", f"issue #{number}: {title}")
    return number


def comment_on_issue(repo, number, body):
    idir = issues_dir(repo) / str(number)
    if not (idir / "issue.md").exists():
        raise ValueError(f"issue #{number} not found")
    _, pub = load_identity()
    ts = int(time.time() * 1000)
    body = strip_edge_newlines(body or "")
    ev = {
        "type": "comment", "id": str(uuid.uuid4()), "author": pub,
        "authorName": author_name(repo), "ts": ts, "attachments": [], "body": body,
    }
    ev["sig"] = sign_issue_event(number, ev)

    # Event files are named NNNN-<type>.md where NNNN is the 1-based event
    # index (issue.md is the open event at index 1). Append after existing ones.
    existing = sum(1 for f in idir.iterdir()
                   if re.match(r"^\d{4}-.+\.md$", f.name))
    nnnn = existing + 2
    lines = [
        "---",
        "type: comment",
        f"id: {ev['id']}",
        f"author: {pub}",
        f"authorName: {ev['authorName']}",
        f"ts: {ts}",
        "attachments: []",
        f"sig: {ev['sig']}",
        "---",
        "",
    ]
    (idir / f"{nnnn:04d}-comment.md").write_text("\n".join(lines) + "\n" + body + "\n")
    _commit(repo, "issues", f"issue #{number}: comment")
    return nnnn


def search_issues(repo, query, status=None):
    d = issues_dir(repo)
    out = []
    if not d.exists():
        return out
    q = (query or "").lower()
    for idir in sorted(d.iterdir(),
                       key=lambda p: int(p.name) if p.name.isdigit() else 0):
        md = idir / "issue.md"
        if not (idir.is_dir() and idir.name.isdigit() and md.exists()):
            continue
        fields, body = read_frontmatter(md)
        st = fields.get("status", "open")
        if status and st != status:
            continue
        title = fields.get("title", "")
        if q and q not in title.lower() and q not in body.lower():
            continue
        out.append({
            "number": int(idir.name), "title": title, "status": st,
            "labels": fields.get("labels", "[]"),
            "author": fields.get("authorName", fields.get("author", "")),
        })
    return out


# ---------------------------------------------------------- pull signing/writing
# Mirrors qt_client/src/PullStore.cpp (see tools/branch_pr_review.py).
def sign_pull(title, base, head, patch_bytes, commits_bytes, ts, author):
    content = (title.encode() + b"\x00" + base.encode() + b"\x00" + head.encode()
               + b"\x00" + patch_bytes + b"\x00" + commits_bytes)
    content_hash = hashlib.sha256(content).hexdigest().encode()
    canonical = (b"forkmesh-pull-event-v1\n" + author.encode() + b"\n"
                 + str(ts).encode() + b"\n" + content_hash)
    return b64url(load_identity()[0].sign(canonical))


def pulls_dir(repo):
    return Path(repo) / "pulls"


def next_pull_number(repo):
    d = pulls_dir(repo)
    mx = 0
    if d.exists():
        for entry in d.iterdir():
            if entry.is_dir() and entry.name.isdigit():
                mx = max(mx, int(entry.name))
    return mx + 1


def existing_pr_for_head(repo, head):
    d = pulls_dir(repo)
    if not d.exists():
        return None
    for entry in sorted(d.iterdir()):
        md = entry / "pull.md"
        if not (entry.is_dir() and entry.name.isdigit() and md.exists()):
            continue
        fm = md.read_text(errors="replace")
        h = re.search(r"^head:\s*(.*)$", fm, re.M)
        s = re.search(r"^status:\s*(.*)$", fm, re.M)
        if h and h.group(1).strip() == head and (not s or s.group(1).strip() == "open"):
            return int(entry.name)
    return None


def open_pr_from_branch(repo, branch, title, description, base):
    base = base or BASE_BRANCH
    existing = existing_pr_for_head(repo, branch)
    if existing:
        return {"number": existing, "existing": True}
    patch_bytes = git_bytes(repo, "diff", f"{base}..{branch}")
    commits_bytes = git_bytes(repo, "format-patch", "--stdout", f"{base}..{branch}")
    if not patch_bytes.strip():
        raise ValueError(f"branch {branch!r} has no diff against {base}")

    ts = int(time.time() * 1000)
    _, author = load_identity()
    title = title or branch
    sig = sign_pull(title, base, branch, patch_bytes, commits_bytes, ts, author)
    number = next_pull_number(repo)
    pdir = pulls_dir(repo) / str(number)
    pdir.mkdir(parents=True, exist_ok=True)
    front = "\n".join([
        "---", "schema: forkmesh-pull-v1", f"number: {number}",
        f"title: {title}", f"base: {base}", f"head: {branch}", "status: open",
        f"ts: {ts}", f"author: {author}", f"authorName: {author_name(repo)}",
        f"sig: {sig}", "---", "",
    ])
    (pdir / "pull.md").write_text(front + "\n" + (description or "") + "\n")
    (pdir / "changes.patch").write_bytes(patch_bytes)
    (pdir / "commits.mbox").write_bytes(commits_bytes)
    _commit(repo, f"pulls/{number}", f"pull #{number}: open")
    return {"number": number, "existing": False}


def get_pr_diff(repo, number):
    pdir = pulls_dir(repo) / str(number)
    patch = pdir / "changes.patch"
    if patch.exists():
        return patch.read_text(errors="replace")
    md = pdir / "pull.md"
    if not md.exists():
        raise ValueError(f"pull #{number} not found")
    fm, _ = read_frontmatter(md)
    base, head = fm.get("base", BASE_BRANCH), fm.get("head", "")
    if not head:
        raise ValueError(f"pull #{number} has no head branch")
    return git_out(repo, "diff", f"{base}..{head}")


def _commit(repo, pathspec, message):
    add = git(repo, "add", pathspec)
    if add.returncode != 0:
        raise ValueError(f"git add failed: {add.stderr.strip()}")
    commit = git(repo, "commit", "-m", message, "--", pathspec)
    if commit.returncode != 0:
        err = (commit.stderr or commit.stdout).strip()
        if "nothing to commit" not in err:
            raise ValueError(f"git commit failed: {err}")


# ----------------------------------------------------------------------- tools
def tool_list_repos(_args):
    out = []
    for p in list_repo_paths():
        out.append({
            "name": p.name,
            "path": str(p),
            "rootCommit": git_out(p, "rev-list", "--max-parents=0", "HEAD").strip()
                          .splitlines()[:1],
            "openIssues": len(search_issues(p, "", status="open")),
        })
    for r in out:
        r["rootCommit"] = r["rootCommit"][0] if r["rootCommit"] else ""
    return json.dumps(out, indent=2)


def tool_read_file(args):
    repo = resolve_repo(args.get("repo"))
    path = args["path"]
    if ".." in Path(path).parts:
        raise ValueError("path must stay within the repo")
    ref = args.get("ref")
    if ref:
        r = git(repo, "show", f"{ref}:{path}")
        if r.returncode != 0:
            raise ValueError(r.stderr.strip() or f"{path} not found at {ref}")
        return r.stdout
    f = Path(repo) / path
    if not f.is_file():
        raise ValueError(f"{path} not found")
    return f.read_text(errors="replace")


def tool_search_issues(args):
    repo = resolve_repo(args.get("repo"))
    return json.dumps(
        search_issues(repo, args.get("query", ""), args.get("status")), indent=2)


def tool_create_issue(args):
    repo = resolve_repo(args.get("repo"))
    number = create_issue(
        repo, args["title"], args.get("body", ""),
        args.get("labels", []), args.get("milestone", ""),
        int(args.get("priority", 0)), args.get("assignees", []))
    return f"Created issue #{number} in {Path(repo).name}"


def tool_comment_on_issue(args):
    repo = resolve_repo(args.get("repo"))
    comment_on_issue(repo, int(args["number"]), args["body"])
    return f"Commented on issue #{args['number']}"


def tool_open_pr_from_branch(args):
    repo = resolve_repo(args.get("repo"))
    result = open_pr_from_branch(
        repo, args["branch"], args.get("title", ""),
        args.get("description", ""), args.get("base", ""))
    verb = "already open as" if result["existing"] else "opened"
    return f"PR {verb} #{result['number']}"


def tool_get_pr_diff(args):
    repo = resolve_repo(args.get("repo"))
    return get_pr_diff(repo, int(args["number"]))


REPO_ARG = {"repo": {"type": "string",
                     "description": "repo name (from list_repos); omit for the default"}}

TOOLS = [
    {
        "name": "list_repos",
        "description": "List the repositories this ForkMesh node exposes.",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": tool_list_repos,
    },
    {
        "name": "read_file",
        "description": "Read a file from a repo, optionally at a git ref.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "repo-relative path"},
                "ref": {"type": "string", "description": "optional git ref/branch/sha"},
                **REPO_ARG,
            },
            "required": ["path"],
        },
        "handler": tool_read_file,
    },
    {
        "name": "search_issues",
        "description": "Search issues by substring in title/body. Optional status filter.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {"type": "string"},
                "status": {"type": "string", "description": "e.g. open, closed"},
                **REPO_ARG,
            },
        },
        "handler": tool_search_issues,
    },
    {
        "name": "create_issue",
        "description": "Create a new signed issue (signs with the node identity key).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {"type": "string"},
                "body": {"type": "string"},
                "labels": {"type": "array", "items": {"type": "string"}},
                "milestone": {"type": "string"},
                "priority": {"type": "integer"},
                "assignees": {"type": "array", "items": {"type": "string"}},
                **REPO_ARG,
            },
            "required": ["title"],
        },
        "handler": tool_create_issue,
    },
    {
        "name": "comment_on_issue",
        "description": "Add a signed comment to an existing issue.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "number": {"type": "integer"},
                "body": {"type": "string"},
                **REPO_ARG,
            },
            "required": ["number", "body"],
        },
        "handler": tool_comment_on_issue,
    },
    {
        "name": "open_pr_from_branch",
        "description": "Open a signed native ForkMesh PR from a local branch against base.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "branch": {"type": "string", "description": "head branch"},
                "title": {"type": "string"},
                "description": {"type": "string"},
                "base": {"type": "string", "description": "base branch (default main)"},
                **REPO_ARG,
            },
            "required": ["branch"],
        },
        "handler": tool_open_pr_from_branch,
    },
    {
        "name": "get_pr_diff",
        "description": "Return the unified diff for a ForkMesh PR number.",
        "inputSchema": {
            "type": "object",
            "properties": {"number": {"type": "integer"}, **REPO_ARG},
            "required": ["number"],
        },
        "handler": tool_get_pr_diff,
    },
]

TOOLS_BY_NAME = {t["name"]: t for t in TOOLS}


# ------------------------------------------------------------- JSON-RPC / stdio
def tools_list_payload():
    return {"tools": [{k: t[k] for k in ("name", "description", "inputSchema")}
                      for t in TOOLS]}


def handle_request(msg):
    """Return a JSON-RPC response dict, or None for notifications."""
    method = msg.get("method")
    mid = msg.get("id")
    params = msg.get("params") or {}

    if method == "initialize":
        client_ver = params.get("protocolVersion", PROTOCOL_VERSION)
        return _result(mid, {
            "protocolVersion": client_ver,
            "capabilities": {"tools": {}},
            "serverInfo": SERVER_INFO,
        })
    if method in ("notifications/initialized", "initialized"):
        return None
    if method == "ping":
        return _result(mid, {})
    if method == "tools/list":
        return _result(mid, tools_list_payload())
    if method == "tools/call":
        name = params.get("name")
        tool = TOOLS_BY_NAME.get(name)
        if not tool:
            return _error(mid, -32602, f"unknown tool: {name}")
        try:
            text = tool["handler"](params.get("arguments") or {})
        except Exception as exc:  # noqa: BLE001 — surface as a tool error
            log(f"tool {name} failed: {exc}")
            return _result(mid, {
                "content": [{"type": "text", "text": f"Error: {exc}"}],
                "isError": True,
            })
        return _result(mid, {"content": [{"type": "text", "text": text}]})

    if mid is None:
        return None  # unknown notification
    return _error(mid, -32601, f"method not found: {method}")


def _result(mid, result):
    return {"jsonrpc": "2.0", "id": mid, "result": result}


def _error(mid, code, message):
    return {"jsonrpc": "2.0", "id": mid, "error": {"code": code, "message": message}}


def serve(stdin, stdout):
    for line in stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError as exc:
            log(f"bad json: {exc}")
            continue
        response = handle_request(msg)
        if response is not None:
            stdout.write(json.dumps(response) + "\n")
            stdout.flush()


def main():
    log(f"listening on stdio; identity {KEY_PATH}")
    serve(sys.stdin, sys.stdout)


if __name__ == "__main__":
    main()
