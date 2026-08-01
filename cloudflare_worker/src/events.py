"""Signed-event crypto + canonicalization for the ForkMesh relay Worker.

The mesh's issues, discussions, pull requests, PR review comments, commit
comments and release manifests are all authenticated by a raw Ed25519 signature
over a versioned canonical string (``forkmesh-<kind>-v1\n...``). Both halves of
that spine — the WebCrypto verify/sha256 primitives and the per-event content
canonicalization that MUST byte-match the desktop client's ``*Store``
``contentForSigning`` / ``canonicalString`` — are pulled out of the ~11k-line
``entry.py`` into this one module.

Like ``solana.py``/``git_http.py``/``releases.py`` (adhoc #215/#279), the import
is strictly one-directional: this depends only on the stdlib, the Worker
runtime's ``js``/``pyodide`` bridge, ``catalog.clean_string`` and the two pure
release helpers, never on other ``entry`` state — so ``entry`` imports these back
and the AST test suite loads them straight from this file.
"""

import base64

from js import Object
from js import Uint8Array
from js import crypto as js_crypto
from pyodide.ffi import to_js as _to_js

from catalog import clean_string
from releases import (
    release_manifest_content,
    release_signing_message,
    valid_git_commit,
)


ISSUE_EVENT_TYPES = frozenset({
    "open", "comment", "edit", "title", "status", "labels", "milestone",
    "dates", "priority", "progress", "bounty", "assignees", "agent",
    "delete", "vote",
})


def to_js(value):
    return _to_js(value, dict_converter=Object.fromEntries)


def b64url_decode(value):
    value = (value or "").strip()
    value += "=" * (-len(value) % 4)
    return base64.urlsafe_b64decode(value.encode())


async def ed25519_verify(pubkey_b64url, sig_b64url, data_bytes):


    try:
        raw_key = b64url_decode(pubkey_b64url)
        signature = b64url_decode(sig_b64url)
    except Exception:
        return False
    if len(raw_key) != 32 or len(signature) != 64:
        return False
    try:
        key = await js_crypto.subtle.importKey(
            "raw", _to_js(raw_key), to_js({"name": "Ed25519"}), False,
            _to_js(["verify"])
        )
        ok = await js_crypto.subtle.verify(
            to_js({"name": "Ed25519"}), key, _to_js(signature),
            _to_js(data_bytes)
        )
        return bool(ok)
    except Exception:
        return False


async def sha256_hex(text):
    digest = await js_crypto.subtle.digest("SHA-256", _to_js(text.encode()))
    return bytes(Uint8Array.new(digest).to_py()).hex()


def issue_event_content(ev):


    t = ev.get("type", "")
    attachments = ",".join(ev.get("attachments") or [])
    if t == "open":
        return "\x00".join([ev.get("title", ""), ev.get("body", ""), attachments])
    if t in ("comment", "edit"):
        return "\x00".join([ev.get("body", ""), attachments])
    if t == "title":
        return ev.get("title", "")
    if t == "status":
        return ev.get("status", "")
    if t == "labels":
        return ",".join(ev.get("labels") or [])
    if t == "milestone":
        return ev.get("milestone", "") or ""
    if t == "dates":


        parts = []
        for key in ("startDate", "endDate"):
            try:
                parts.append(str(int(ev.get(key, 0) or 0)))
            except (TypeError, ValueError):
                parts.append("0")
        return "\x00".join(parts)
    if t == "priority":
        try:
            return str(int(ev.get("priority", 0)))
        except (TypeError, ValueError):
            return "0"
    if t == "progress":
        try:
            return str(int(ev.get("progress", 0)))
        except (TypeError, ValueError):
            return "0"
    if t == "bounty":
        try:
            amount = "%.2f" % float(ev.get("bountyUsd", 0))
        except (TypeError, ValueError):
            amount = "0.00"
        return "\x00".join([amount, ev.get("bountyAddress", "") or "",
                            ev.get("bountyStatus", "") or ""])
    if t == "assignees":
        return ",".join(ev.get("assignees") or [])
    if t == "agent":
        try:
            session_id = str(int(ev.get("agentSessionId", 0) or 0))
        except (TypeError, ValueError):
            session_id = "0"
        return "\x00".join([
            ev.get("agentProvider", "") or "",
            session_id,
            ev.get("agentStatus", "") or "",
            "pr" if ev.get("agentCreatePr") else "no-pr",
        ])
    if t == "delete":
        return ev.get("target", "")
    if t == "vote":
        return ""
    return ""


async def verify_issue_event(number, ev):
    author = ev.get("author", "")
    signature = ev.get("sig", "")
    event_type = ev.get("type", "")
    if not author or not signature or event_type not in ISSUE_EVENT_TYPES:
        return False
    try:
        event_number = int(number)
    except (TypeError, ValueError):
        return False
    if event_type == "open" and event_number != 0:
        return False
    if event_type != "open" and event_number <= 0:
        return False
    if event_type == "status" and ev.get("status") not in ("open", "closed"):
        return False
    if event_type in ("labels", "assignees"):
        values = ev.get(event_type)
        if not isinstance(values, list) or any(
            not isinstance(value, str) for value in values
        ):
            return False
    if event_type in ("edit", "delete"):
        target = ev.get("target")
        if not isinstance(target, str) or not target:
            return False
    if event_type == "priority":
        try:
            priority = int(ev.get("priority", 0))
        except (TypeError, ValueError):
            return False
        if priority < 0 or priority > 99:
            return False
    if event_type == "progress":
        try:
            progress = int(ev.get("progress", 0))
        except (TypeError, ValueError):
            return False
        if progress < 0 or progress > 100:
            return False
    try:
        ts = int(ev.get("ts", 0))
    except (TypeError, ValueError):
        return False
    content_hash = await sha256_hex(issue_event_content(ev))
    canonical = (
        "forkmesh-issue-event-v1\n" + event_type + "\n" + str(event_number) + "\n" +
        author + "\n" + str(ts) + "\n" + content_hash
    ).encode()
    return await ed25519_verify(author, signature, canonical)


DISCUSSION_CATEGORIES = {
    "Announcements", "Ideas", "Q&A", "Show and tell", "Maintainer notes",
}
DISCUSSION_CATEGORY_MAP = {
    category.lower(): category for category in DISCUSSION_CATEGORIES
}


def normalized_discussion_category(value):
    return DISCUSSION_CATEGORY_MAP.get(clean_string(value, 80).strip().lower(), "")


def discussion_event_content(ev):

    t = ev.get("type", "")
    if t == "open":
        return "\x00".join([
            ev.get("title", ""), ev.get("body", ""), ev.get("category", "")
        ])
    if t == "comment":
        return ev.get("body", "")
    return ""


async def verify_discussion_event(number, ev):




    author = ev.get("author", "")
    signature = ev.get("sig", "")
    event_type = ev.get("type", "")
    if not author or not signature or event_type not in ("open", "comment"):
        return False
    if event_type == "open" and not normalized_discussion_category(
            ev.get("category", "")):
        return False
    try:
        ts = int(ev.get("ts", 0))
    except (TypeError, ValueError):
        return False
    content_hash = await sha256_hex(discussion_event_content(ev))
    canonical = (
        "forkmesh-discussion-event-v1\n" + event_type + "\n" + str(int(number)) +
        "\n" + author + "\n" + str(ts) + "\n" + content_hash
    ).encode()
    return await ed25519_verify(author, signature, canonical)


async def verify_pull_event(pr):






    author = pr.get("author", "")
    signature = pr.get("sig", "")
    if not author or not signature:
        return False
    try:
        ts = int(pr.get("ts", 0))
    except (TypeError, ValueError):
        return False
    fields = [pr.get("title", ""), pr.get("base", ""), pr.get("head", ""),
              pr.get("patch", "")]
    commits = pr.get("commits", "")




    contents = ["\x00".join(fields + [commits])]
    if not commits:
        contents.append("\x00".join(fields))
    for content in contents:
        content_hash = await sha256_hex(content)
        canonical = (
            "forkmesh-pull-event-v1\n" + author + "\n" + str(ts) + "\n" + content_hash
        ).encode()
        if await ed25519_verify(author, signature, canonical):
            return True
    return False


def pull_comment_content(ev):

    t = ev.get("type", "")
    def int_field(name):
        try:
            return str(int(ev.get(name, 0)))
        except (TypeError, ValueError):
            return "0"
    if t == "comment":
        return ev.get("body", "")
    if t == "review":
        return "\x00".join([ev.get("state", ""), ev.get("body", "")])
    if t == "line-comment":
        return "\x00".join([
            ev.get("path", ""), ev.get("side", ""), int_field("line"),
            ev.get("body", ""),
        ])
    if t == "thread-comment":
        return "\x00".join([
            ev.get("threadId", ""), ev.get("path", ""), ev.get("side", ""),
            int_field("lineStart"), int_field("lineEnd"), ev.get("body", ""),
            ev.get("suggestionPatch", ""),
        ])
    if t == "thread-reply":
        return "\x00".join([
            ev.get("threadId", ""), ev.get("parentId", ""),
            ev.get("body", ""),
        ])
    if t == "thread-state":
        return "\x00".join([
            ev.get("threadId", ""), ev.get("state", ""), ev.get("body", ""),
        ])
    if t == "suggestion-state":
        return "\x00".join([
            ev.get("threadId", ""), ev.get("state", ""),
            ev.get("appliedCommit", ""), ev.get("body", ""),
        ])
    return ""


async def verify_pull_comment_event(number, ev):


    author = ev.get("author", "")
    signature = ev.get("sig", "")
    event_type = ev.get("type", "")
    allowed_types = (
        "comment", "review", "line-comment", "thread-comment", "thread-reply",
        "thread-state", "suggestion-state",
    )
    if not author or not signature or event_type not in allowed_types:
        return False
    try:
        ts = int(ev.get("ts", 0))
    except (TypeError, ValueError):
        return False
    content_hash = await sha256_hex(pull_comment_content(ev))
    canonical = (
        "forkmesh-pull-comment-v1\n" + event_type + "\n" + str(int(number)) + "\n" +
        author + "\n" + str(ts) + "\n" + content_hash
    ).encode()
    return await ed25519_verify(author, signature, canonical)














async def verify_release_manifest(manifest):



    author = manifest.get("created_by", "") or ""
    signature = manifest.get("sig", "") or ""
    repo = manifest.get("repo", "") or ""
    tag = manifest.get("tag", "") or ""
    if not author or not signature or not repo or not tag:
        return False
    build_commit = manifest.get("build_commit", "") or ""
    if build_commit and not valid_git_commit(build_commit):
        return False
    try:
        ts = int(manifest.get("published_at", 0))
    except (TypeError, ValueError):
        return False
    content_hash = await sha256_hex(release_manifest_content(manifest))
    canonical = release_signing_message(repo, tag, author, ts, content_hash)
    return await ed25519_verify(author, signature, canonical)
