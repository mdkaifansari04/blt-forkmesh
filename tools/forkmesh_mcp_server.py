#!/usr/bin/env python3
"""ForkMesh MCP server — expose the mesh (repos, issues, PRs) as MCP tools.

Roadmap Phase 5 / issue #366. Any agent tooling — not just the built-in Claude
Code / Codex integrations — should be able to work the mesh. This is a
Model Context Protocol server (https://modelcontextprotocol.io/specification)
speaking JSON-RPC 2.0 over the stdio transport (newline-delimited JSON on
stdin/stdout, diagnostics on stderr). stdio is fine for local-first: Claude Code
launches it as a subprocess and auto-discovers it from a `.mcp.json` entry.

Read tools:  whoami, list_repos, read_file, search_issues, get_pr_diff
Write tools: create_issue, comment_on_issue, create_milestone, update_milestone,
             create_project, update_project, open_pr_from_branch

Connector token (adhoc #16). The write tools sign as this node, so holding them
is holding the node's identity. The desktop client's Settings -> MCP tab mints a
connector token into
    $XDG_DATA_HOME/ForkMesh/ForkMesh/mcp/connector.json
and puts it in the agent's MCP config as FORKMESH_MCP_TOKEN. When that file
exists the write tools require a matching token; reads stay open so an agent can
still browse without being granted the identity. When it does not exist (the
historical setup: a subprocess you launched yourself) everything is allowed, so
existing .mcp.json entries keep working. Revoking is deleting the file — every
config still holding the old string is demoted to read-only at once.

Write tools sign with the node identity key and write the exact same native
ForkMesh entries the Qt client's IssueStore / ProjectStore / PullStore write —
no privileged side door. Issues are signed-event JSON records at
.forkmesh/issues/{open,closed}/<n>/issue-<n>.json, projects at
.forkmesh/projects/<n>/project-<n>.json, milestone definitions at
.forkmesh/issues/milestones.json. The signing is reproduced byte-for-byte from:
    qt_client/src/IssueStore.cpp::canonicalString / contentForSigning
    qt_client/src/ProjectStore.cpp::canonicalString / contentForSigning
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
import datetime
import hashlib
import hmac
import json
import os
import re
import shutil
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
CONNECTOR_PATH = Path(
    os.environ.get("FORKMESH_MCP_CONNECTOR")
    or Path(DATA_HOME) / "ForkMesh/ForkMesh/mcp/connector.json")

# Tools that sign with — and therefore act as — the node identity.
WRITE_TOOLS = {
    "create_issue", "comment_on_issue", "create_milestone", "update_milestone",
    "create_project", "update_project", "open_pr_from_branch",
}


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


# ------------------------------------------------------------------ connector
def connector_record():
    """The minted connector, or {} when this node has not published one."""
    try:
        record = json.loads(CONNECTOR_PATH.read_text())
    except (OSError, ValueError):
        return {}
    return record if isinstance(record, dict) and record.get("token") else {}


def presented_token():
    return (os.environ.get("FORKMESH_MCP_TOKEN") or "").strip()


def access_level():
    """'open' (no connector), 'write' (token matches), or 'read' (no token)."""
    record = connector_record()
    if not record:
        return "open"
    # Constant-time: a token comparison that leaks length/prefix by timing is
    # exactly the kind of side channel a local agent could grind.
    return "write" if hmac.compare_digest(
        str(record["token"]), presented_token()) else "read"


def require_write_access(name):
    if name not in WRITE_TOOLS or access_level() != "read":
        return
    raise ValueError(
        f"{name} needs the connector token: this node published an MCP "
        "connector, so write tools require a matching FORKMESH_MCP_TOKEN. "
        "Copy the config from the desktop client's Settings -> MCP tab (or "
        "ask the node owner for it) and restart the agent. Read tools "
        "(whoami, list_repos, read_file, search_issues, get_pr_diff) work "
        "without it.")


def author_name(repo):
    return git_out(repo, "config", "user.name").strip() or "forkmesh"


def strip_edge_newlines(text):
    return text.strip("\r\n")


def now_ms():
    return int(time.time() * 1000)


def parse_date_ms(value, field):
    """Parse a start/end/due date to epoch milliseconds.

    Accepts an epoch-ms integer or a YYYY-MM-DD string (local midnight, like
    the desktop's date pickers). None/""/0 mean unset (0).
    """
    if value is None or value == "" or value == 0:
        return 0
    if isinstance(value, bool):
        raise ValueError(f"{field} must be YYYY-MM-DD or epoch milliseconds")
    if isinstance(value, (int, float)):
        ms = int(value)
        if ms != value or ms < 0:
            raise ValueError(
                f"{field} must be a non-negative epoch-milliseconds integer, "
                f"got {value!r}")
        return ms
    text = str(value).strip()
    if text.isdigit():
        return int(text)
    try:
        day = datetime.date.fromisoformat(text)
    except ValueError:
        raise ValueError(
            f"{field} must be YYYY-MM-DD or epoch milliseconds, got {value!r}")
    return int(time.mktime(day.timetuple()) * 1000)


def parse_date_range(start_date, end_date, start_field="start_date",
                     end_field="end_date"):
    start_ms = parse_date_ms(start_date, start_field)
    end_ms = parse_date_ms(end_date, end_field)
    if start_ms and end_ms and end_ms < start_ms:
        raise ValueError(f"{end_field} is before {start_field}")
    return start_ms, end_ms


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


# -------------------------------------------------------- issue signing/writing
# Native signed-event tracker records, mirroring qt_client/src/IssueStore.cpp
# byte-for-byte (contentForSigning / canonicalString; the relay Worker's
# issue_event_content is the same spine).
ISSUES_REL = ".forkmesh/issues"


def issue_content_for_signing(ev):
    t = ev.get("type", "")
    nul = "\x00"
    atts = ",".join(ev.get("attachments") or [])
    if t == "open":
        return ev.get("title", "") + nul + ev.get("body", "") + nul + atts
    if t in ("comment", "edit"):
        return ev.get("body", "") + nul + atts
    if t == "title":
        return ev.get("title", "")
    if t == "status":
        return ev.get("status", "")
    if t == "labels":
        return ",".join(ev.get("labels") or [])
    if t == "milestone":
        return ev.get("milestone", "")
    if t == "dates":
        return str(int(ev.get("startDate", 0))) + nul + str(int(ev.get("endDate", 0)))
    if t == "priority":
        return str(int(ev.get("priority", 0)))
    if t == "progress":
        return str(int(ev.get("progress", 0)))
    if t == "bounty":
        return ("%.2f" % float(ev.get("bountyUsd", 0)) + nul +
                (ev.get("bountyAddress") or "") + nul +
                (ev.get("bountyStatus") or ""))
    if t == "assignees":
        return ",".join(ev.get("assignees") or [])
    if t == "agent":
        return ((ev.get("agentProvider") or "") + nul +
                str(int(ev.get("agentSessionId", 0))) + nul +
                (ev.get("agentStatus") or "") + nul +
                ("pr" if ev.get("agentCreatePr") else "no-pr"))
    if t == "delete":
        return ev.get("target", "")
    return ""


def sign_issue_event(number, ev):
    content_hash = hashlib.sha256(
        issue_content_for_signing(ev).encode()).hexdigest()
    canonical = (
        "forkmesh-issue-event-v1\n"
        f"{ev['type']}\n{number}\n{ev['author']}\n{ev['ts']}\n{content_hash}"
    ).encode()
    return b64url(load_identity()[0].sign(canonical))


def issues_root(repo):
    return Path(repo) / ISSUES_REL


def issue_dir_candidates(repo, number):
    """Repo folders for issue <n>, in the order readers probe them:
    open/<n>, closed/<n>, then the pre-split legacy <n>."""
    root = issues_root(repo)
    return [root / "open" / str(number), root / "closed" / str(number),
            root / str(number)]


def _numeric_dir_numbers(d):
    if not d.is_dir():
        return []
    return [int(e.name) for e in d.iterdir() if e.is_dir() and e.name.isdigit()]


def next_issue_number(repo):
    """Closed and legacy (pre-split) issues still occupy their numbers, so the
    max spans open/, closed/, and the legacy root."""
    root = issues_root(repo)
    mx = 0
    for d in (root / "open", root / "closed", root):
        for n in _numeric_dir_numbers(d):
            mx = max(mx, n)
    return mx + 1


def dump_record(record):
    return json.dumps(record, indent=4, sort_keys=True) + "\n"


def read_issue_record(repo, number):
    for d in issue_dir_candidates(repo, number):
        f = d / f"issue-{number}.json"
        if f.is_file():
            return json.loads(f.read_text(errors="replace")), f
    raise ValueError(f"issue #{number} not found")


def _commit_or_rollback(repo, pathspec, message, created_dir):
    """Commit, or remove the just-created record so no partial state remains."""
    try:
        _commit(repo, pathspec, message)
    except Exception:
        shutil.rmtree(created_dir, ignore_errors=True)
        git(repo, "add", "-A", "--", pathspec)  # drop it from the index too
        raise


def _rewrite_and_commit(repo, path, payload, pathspec, message):
    """Overwrite an existing record and commit; restore the old bytes if the
    commit fails so a broken commit never leaves a half-updated record."""
    old = path.read_bytes()
    path.write_text(payload)
    try:
        _commit(repo, pathspec, message)
    except Exception:
        path.write_bytes(old)
        git(repo, "add", "-A", "--", pathspec)
        raise


def create_issue(repo, title, body, labels, milestone, priority, assignees,
                 start_date=None, end_date=None):
    _, pub = load_identity()
    title = (title or "").strip()
    if not title:
        raise ValueError("title must not be empty")
    start_ms, end_ms = parse_date_range(start_date, end_date)
    body = strip_edge_newlines(body or "")
    name = author_name(repo)

    for _attempt in range(3):
        number = next_issue_number(repo)
        if any(d.exists() for d in issue_dir_candidates(repo, number)):
            continue  # a concurrent writer claimed the number; re-read
        ts = now_ms()
        open_ev = {
            "type": "open", "id": f"open-{number}", "author": pub,
            "authorName": name, "ts": ts, "title": title, "body": body,
            "attachments": [],
        }
        open_ev["sig"] = sign_issue_event(number, open_ev)
        events = [open_ev]
        if start_ms or end_ms:
            dates_ev = {
                "type": "dates", "id": str(uuid.uuid4()), "author": pub,
                "authorName": name, "ts": now_ms(),
                "startDate": start_ms, "endDate": end_ms,
            }
            dates_ev["sig"] = sign_issue_event(number, dates_ev)
            events.append(dates_ev)
        record = {
            "schema": "forkmesh-issue-v1", "number": number, "title": title,
            "status": "open", "labels": list(labels or []),
            "milestone": milestone or "",
            "startDate": start_ms, "endDate": end_ms,
            "priority": max(0, min(99, int(priority or 0))), "progress": 0,
            "assignees": list(assignees or []),
            "createdAt": ts, "updatedAt": events[-1]["ts"],
            "author": pub, "authorName": name,
            "bountyUsd": 0, "bountyAddress": "", "bountyStatus": "",
            "votes": 0, "events": events,
        }
        idir = issues_root(repo) / "open" / str(number)
        idir.mkdir(parents=True, exist_ok=True)
        payload = dump_record(record)
        path = idir / f"issue-{number}.json"
        path.write_text(payload)
        # Re-read before committing: a concurrent writer may have taken the
        # same number (its folder appeared elsewhere, or it overwrote ours).
        if any(d.exists() for d in issue_dir_candidates(repo, number)[1:]):
            shutil.rmtree(idir, ignore_errors=True)
            continue
        if path.read_text() != payload:
            continue  # the competing record won this path; pick a fresh number
        _commit_or_rollback(repo, ISSUES_REL, f"issue #{number}: {title}", idir)
        return number
    raise ValueError(
        "could not allocate a unique issue number (concurrent writers kept "
        "taking it); retry")


def comment_on_issue(repo, number, body):
    record, path = read_issue_record(repo, number)
    _, pub = load_identity()
    ts = now_ms()
    body = strip_edge_newlines(body or "")
    ev = {
        "type": "comment", "id": str(uuid.uuid4()), "author": pub,
        "authorName": author_name(repo), "ts": ts, "attachments": [],
        "body": body,
    }
    ev["sig"] = sign_issue_event(number, ev)
    record.setdefault("events", []).append(ev)
    record["updatedAt"] = max(int(record.get("updatedAt") or 0), ts)
    _rewrite_and_commit(repo, path, dump_record(record), ISSUES_REL,
                        f"issue #{number}: comment")
    return len(record["events"])


def search_issues(repo, query, status=None):
    root = issues_root(repo)
    q = (query or "").lower()
    out = []
    seen = set()
    # The status folder is authoritative for open/closed (the layout encodes
    # each issue's state); the legacy pre-split root falls back to the record.
    for sub, folder in (("open", "open"), ("closed", "closed"), ("", "")):
        d = root / sub if sub else root
        if not d.is_dir():
            continue
        for entry in d.iterdir():
            if not (entry.is_dir() and entry.name.isdigit()):
                continue
            number = int(entry.name)
            if number in seen:
                continue
            f = entry / f"issue-{number}.json"
            if not f.is_file():
                continue
            try:
                record = json.loads(f.read_text(errors="replace"))
            except json.JSONDecodeError:
                continue
            if not isinstance(record, dict):
                continue
            seen.add(number)
            st = folder or record.get("status", "open")
            if status and st != status:
                continue
            title = record.get("title", "")
            body = "\n".join(e.get("body", "")
                             for e in record.get("events", [])
                             if isinstance(e, dict))
            if q and q not in title.lower() and q not in body.lower():
                continue
            out.append({
                "number": number, "title": title, "status": st,
                "labels": record.get("labels", []),
                "author": record.get("authorName") or record.get("author", ""),
            })
    out.sort(key=lambda r: r["number"])
    return out


# ------------------------------------------------------- milestone definitions
# .forkmesh/issues/milestones.json — a plain list of {title, due, status,
# description}, the same file IssueStore::saveMilestones writes.
def milestones_path(repo):
    return issues_root(repo) / "milestones.json"


def load_milestones(repo):
    p = milestones_path(repo)
    if not p.is_file():
        return []
    try:
        data = json.loads(p.read_text(errors="replace"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"{p} is not valid JSON: {exc}")
    return data if isinstance(data, list) else []


def save_milestones(repo, milestones):
    p = milestones_path(repo)
    old = p.read_bytes() if p.exists() else None
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(milestones, indent=4, sort_keys=True) + "\n")
    try:
        _commit(repo, ISSUES_REL, "issues: update milestones")
    except Exception:
        if old is None:
            p.unlink(missing_ok=True)
        else:
            p.write_bytes(old)
        git(repo, "add", "-A", "--", ISSUES_REL)
        raise


def milestone_titles(repo):
    return [m.get("title", "") for m in load_milestones(repo)
            if isinstance(m, dict)]


def create_milestone(repo, title, due=None, description=""):
    title = (title or "").strip()
    if not title:
        raise ValueError("milestone title must not be empty")
    due_ms = parse_date_ms(due, "due")
    milestones = load_milestones(repo)
    if title in milestone_titles(repo):
        raise ValueError(
            f"milestone {title!r} already exists (use update_milestone to "
            "change it)")
    milestones.append({"title": title, "due": due_ms, "status": "open",
                       "description": description or ""})
    save_milestones(repo, milestones)


def update_milestone(repo, title, due=None, status=None, description=None):
    milestones = load_milestones(repo)
    target = next((m for m in milestones
                   if isinstance(m, dict) and m.get("title") == title), None)
    if target is None:
        known = ", ".join(repr(t) for t in milestone_titles(repo)) or "none"
        raise ValueError(f"unknown milestone {title!r} (existing: {known})")
    if due is None and status is None and description is None:
        raise ValueError("nothing to update: pass due, status, or description")
    if status is not None and status not in ("open", "closed"):
        raise ValueError(f"status must be 'open' or 'closed', got {status!r}")
    if due is not None:
        target["due"] = parse_date_ms(due, "due")
    if status is not None:
        target["status"] = status
    if description is not None:
        target["description"] = description
    save_milestones(repo, milestones)


# ------------------------------------------------------ project signing/writing
# Mirrors qt_client/src/ProjectStore.cpp byte-for-byte.
PROJECTS_REL = ".forkmesh/projects"


def projects_root(repo):
    return Path(repo) / PROJECTS_REL


def sorted_issue_numbers(issues):
    return sorted({int(n) for n in issues})


def project_content_for_signing(ev):
    t = ev.get("type", "")
    nul = "\x00"
    if t == "open":
        return ev.get("title", "") + nul + ev.get("body", "")
    if t == "title":
        return ev.get("title", "")
    if t == "edit":
        return ev.get("body", "")
    if t == "status":
        return ev.get("status", "")
    if t == "dates":
        return str(int(ev.get("startDate", 0))) + nul + str(int(ev.get("endDate", 0)))
    if t == "milestone":
        return ev.get("milestone", "")
    if t == "issues":
        # Comma-joined, ascending, no spaces (e.g. "384,385") so every port
        # hashes identical bytes regardless of stored order.
        return ",".join(str(n) for n in sorted_issue_numbers(ev.get("issues") or []))
    if t == "delete":
        return ev.get("target", "")
    return ""


def sign_project_event(number, ev):
    content_hash = hashlib.sha256(
        project_content_for_signing(ev).encode()).hexdigest()
    canonical = (
        "forkmesh-project-event-v1\n"
        f"{ev['type']}\n{number}\n{ev['author']}\n{ev['ts']}\n{content_hash}"
    ).encode()
    return b64url(load_identity()[0].sign(canonical))


def next_project_number(repo):
    return max(_numeric_dir_numbers(projects_root(repo)), default=0) + 1


def read_project_record(repo, number):
    f = projects_root(repo) / str(number) / f"project-{number}.json"
    if not f.is_file():
        raise ValueError(f"project #{number} not found")
    return json.loads(f.read_text(errors="replace")), f


def validate_issue_links(repo, issues):
    numbers = sorted_issue_numbers(issues or [])
    missing = [n for n in numbers
               if not any((d / f"issue-{n}.json").is_file()
                          for d in issue_dir_candidates(repo, n))]
    if missing:
        raise ValueError(
            "unknown issue number(s): "
            + ", ".join(f"#{n}" for n in missing)
            + " — link only issues that exist in this repository")
    return numbers


def validate_milestone_exists(repo, milestone):
    titles = milestone_titles(repo)
    if milestone not in titles:
        known = ", ".join(repr(t) for t in titles) or "none"
        raise ValueError(
            f"unknown milestone {milestone!r}; create it first with "
            f"create_milestone (existing: {known})")


def create_project(repo, title, body, start_date=None, end_date=None,
                   milestone="", issues=None):
    _, pub = load_identity()
    title = (title or "").strip()
    if not title:
        raise ValueError("title must not be empty")
    start_ms, end_ms = parse_date_range(start_date, end_date)
    milestone = milestone or ""
    if milestone:
        validate_milestone_exists(repo, milestone)
    linked = validate_issue_links(repo, issues)
    body = body or ""
    name = author_name(repo)

    for _attempt in range(3):
        number = next_project_number(repo)
        pdir = projects_root(repo) / str(number)
        if pdir.exists():
            continue  # a concurrent writer claimed the number; re-read
        ts = now_ms()
        open_ev = {
            "type": "open", "id": f"open-{number}", "author": pub,
            "authorName": name, "ts": ts, "title": title, "body": body,
        }
        open_ev["sig"] = sign_project_event(number, open_ev)
        events = [open_ev]
        if start_ms or end_ms:
            ev = {"type": "dates", "id": str(uuid.uuid4()), "author": pub,
                  "authorName": name, "ts": now_ms(),
                  "startDate": start_ms, "endDate": end_ms}
            ev["sig"] = sign_project_event(number, ev)
            events.append(ev)
        if milestone:
            ev = {"type": "milestone", "id": str(uuid.uuid4()), "author": pub,
                  "authorName": name, "ts": now_ms(), "milestone": milestone}
            ev["sig"] = sign_project_event(number, ev)
            events.append(ev)
        if linked:
            ev = {"type": "issues", "id": str(uuid.uuid4()), "author": pub,
                  "authorName": name, "ts": now_ms(), "issues": linked}
            ev["sig"] = sign_project_event(number, ev)
            events.append(ev)
        record = {
            "schema": "forkmesh-project-v1", "number": number, "title": title,
            "body": body, "status": "open",
            "startDate": start_ms, "endDate": end_ms,
            "milestone": milestone, "issues": linked,
            "createdAt": ts, "updatedAt": events[-1]["ts"],
            "author": pub, "authorName": name, "events": events,
        }
        pdir.mkdir(parents=True, exist_ok=True)
        payload = dump_record(record)
        path = pdir / f"project-{number}.json"
        path.write_text(payload)
        # Re-read before committing: retry if a concurrent writer overwrote us.
        if path.read_text() != payload:
            continue
        _commit_or_rollback(repo, PROJECTS_REL,
                            f"projects: #{number} {title}", pdir)
        return number
    raise ValueError(
        "could not allocate a unique project number (concurrent writers kept "
        "taking it); retry")


def _append_project_event(repo, number, ev_fields, commit_suffix):
    """Append one signed event to a project record, fold it into the
    top-level metadata (like ProjectStore::recomputeMetadata), and commit."""
    record, path = read_project_record(repo, number)
    _, pub = load_identity()
    ev = {"id": str(uuid.uuid4()), "author": pub,
          "authorName": author_name(repo), "ts": now_ms(), **ev_fields}
    ev["sig"] = sign_project_event(number, ev)
    record.setdefault("events", []).append(ev)
    if ev["type"] == "dates":
        record["startDate"] = ev["startDate"]
        record["endDate"] = ev["endDate"]
    elif ev["type"] == "milestone":
        record["milestone"] = ev["milestone"]
    elif ev["type"] == "issues":
        record["issues"] = ev["issues"]
    record["updatedAt"] = max(int(record.get("updatedAt") or 0), ev["ts"])
    _rewrite_and_commit(repo, path, dump_record(record), PROJECTS_REL,
                        f"projects: #{number} {commit_suffix}")


def update_project(repo, number, start_date=None, end_date=None,
                   milestone=None, issues=None):
    read_project_record(repo, number)  # actionable "not found" before anything
    set_dates = start_date is not None or end_date is not None
    if not (set_dates or milestone is not None or issues is not None):
        raise ValueError(
            "nothing to update: pass start_date/end_date, milestone, or issues")
    # Validate everything up front so a bad field never leaves a partial update.
    if set_dates:
        start_ms, end_ms = parse_date_range(start_date, end_date)
    if milestone:
        validate_milestone_exists(repo, milestone)
    if issues is not None:
        linked = validate_issue_links(repo, issues)
    # One signed event + native commit per field, like the desktop's set* calls.
    if set_dates:
        _append_project_event(repo, number,
                              {"type": "dates", "startDate": start_ms,
                               "endDate": end_ms}, "dates")
    if milestone is not None:
        _append_project_event(repo, number,
                              {"type": "milestone", "milestone": milestone},
                              "milestone")
    if issues is not None:
        _append_project_event(repo, number,
                              {"type": "issues", "issues": linked}, "issues")


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
    add = git(repo, "add", "-A", "--", pathspec)
    if add.returncode != 0:
        raise ValueError(f"git add failed: {add.stderr.strip()}")
    commit = git(repo, "commit", "-m", message, "--", pathspec)
    if commit.returncode != 0:
        err = (commit.stderr or commit.stdout).strip()
        if "nothing to commit" not in err:
            raise ValueError(f"git commit failed: {err}")


# ----------------------------------------------------------------------- tools
def tool_whoami(_args):
    """Who the agent is acting as, and what it is allowed to do.

    The first call an agent should make: it answers "do I have the token?"
    without having to trip over a refused write halfway through a task.
    """
    level = access_level()
    try:
        _, pub = load_identity()
    except ValueError as exc:
        pub = None
        identity_error = str(exc)
    else:
        identity_error = None

    info = {
        "node": pub,
        "access": level,
        "canWrite": level != "read",
        "connector": bool(connector_record()),
        "tokenPresented": bool(presented_token()),
        "repos": [p.name for p in list_repo_paths()],
        "writeTools": sorted(WRITE_TOOLS),
    }
    if identity_error:
        info["identityError"] = identity_error
    if level == "read":
        info["hint"] = ("Set FORKMESH_MCP_TOKEN from the desktop client's "
                        "Settings -> MCP tab to enable the write tools.")
    return json.dumps(info, indent=2)


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
    # Reject traversal (".."), absolute paths, and symlink escapes. An absolute
    # right-hand operand silently discards the repo prefix (Path("/repo") / "/etc/
    # passwd" == Path("/etc/passwd")), so a "\x00".isabs() / resolve-and-contain
    # check is required, not just a ".." component filter.
    if Path(path).is_absolute() or ".." in Path(path).parts:
        raise ValueError("path must stay within the repo")
    ref = args.get("ref")
    if ref:
        r = git(repo, "show", f"{ref}:{path}")
        if r.returncode != 0:
            raise ValueError(r.stderr.strip() or f"{path} not found at {ref}")
        return r.stdout
    repo_root = Path(repo).resolve()
    f = (repo_root / path).resolve()
    if repo_root != f and repo_root not in f.parents:
        raise ValueError("path must stay within the repo")
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
        int(args.get("priority", 0)), args.get("assignees", []),
        args.get("start_date"), args.get("end_date"))
    return f"Created issue #{number} in {Path(repo).name}"


def tool_create_milestone(args):
    repo = resolve_repo(args.get("repo"))
    create_milestone(repo, args["title"], args.get("due"),
                     args.get("description", ""))
    return f"Created milestone {args['title']!r} in {Path(repo).name}"


def tool_update_milestone(args):
    repo = resolve_repo(args.get("repo"))
    update_milestone(repo, args["title"], args.get("due"),
                     args.get("status"), args.get("description"))
    return f"Updated milestone {args['title']!r}"


def tool_create_project(args):
    repo = resolve_repo(args.get("repo"))
    number = create_project(
        repo, args["title"], args.get("body", ""),
        args.get("start_date"), args.get("end_date"),
        args.get("milestone", ""), args.get("issues"))
    return f"Created project #{number} in {Path(repo).name}"


def tool_update_project(args):
    repo = resolve_repo(args.get("repo"))
    update_project(repo, int(args["number"]), args.get("start_date"),
                   args.get("end_date"), args.get("milestone"),
                   args.get("issues"))
    return f"Updated project #{args['number']}"


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

DATE_ARG = {"type": ["string", "integer"],
            "description": "YYYY-MM-DD or epoch milliseconds"}

TOOLS = [
    {
        "name": "whoami",
        "description": "Identity this connector acts as, its access level "
                       "(open/write/read), and the repos it can reach.",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": tool_whoami,
    },
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
                "start_date": DATE_ARG,
                "end_date": DATE_ARG,
                **REPO_ARG,
            },
            "required": ["title"],
        },
        "handler": tool_create_issue,
    },
    {
        "name": "create_milestone",
        "description": "Add a milestone definition to .forkmesh/issues/milestones.json.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {"type": "string"},
                "due": DATE_ARG,
                "description": {"type": "string"},
                **REPO_ARG,
            },
            "required": ["title"],
        },
        "handler": tool_create_milestone,
    },
    {
        "name": "update_milestone",
        "description": "Update an existing milestone's due date, status, or description.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {"type": "string"},
                "due": DATE_ARG,
                "status": {"type": "string", "description": "open or closed"},
                "description": {"type": "string"},
                **REPO_ARG,
            },
            "required": ["title"],
        },
        "handler": tool_update_milestone,
    },
    {
        "name": "create_project",
        "description": "Create a signed project with optional dates, milestone, "
                       "and linked issue numbers.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {"type": "string"},
                "body": {"type": "string"},
                "start_date": DATE_ARG,
                "end_date": DATE_ARG,
                "milestone": {"type": "string"},
                "issues": {"type": "array", "items": {"type": "integer"},
                           "description": "issue numbers to link"},
                **REPO_ARG,
            },
            "required": ["title"],
        },
        "handler": tool_create_project,
    },
    {
        "name": "update_project",
        "description": "Append signed events to a project: set dates, milestone, "
                       "or the linked issue numbers.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "number": {"type": "integer"},
                "start_date": DATE_ARG,
                "end_date": DATE_ARG,
                "milestone": {"type": "string"},
                "issues": {"type": "array", "items": {"type": "integer"}},
                **REPO_ARG,
            },
            "required": ["number"],
        },
        "handler": tool_update_project,
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
        level = access_level()
        # `instructions` is the spec's slot for telling the model how to use the
        # server; say up front whether writes are available so it plans a task
        # it can actually finish.
        instructions = (
            "ForkMesh mesh access. Read: whoami, list_repos, read_file, "
            "search_issues, get_pr_diff. Write (signed as this node): "
            "create_issue, comment_on_issue, create_milestone, "
            "update_milestone, create_project, update_project, "
            "open_pr_from_branch. Work a task by finding it with "
            "search_issues, commenting progress with comment_on_issue, and "
            "landing it with open_pr_from_branch. ")
        instructions += (
            "This connector is read-only: no valid FORKMESH_MCP_TOKEN was "
            "presented, so write tools will refuse."
            if level == "read" else
            "This connector may write.")
        return _result(mid, {
            "protocolVersion": client_ver,
            "capabilities": {"tools": {}},
            "serverInfo": SERVER_INFO,
            "instructions": instructions,
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
            require_write_access(name)
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
    log(f"listening on stdio; identity {KEY_PATH}; access {access_level()}")
    serve(sys.stdin, sys.stdout)


if __name__ == "__main__":
    main()
