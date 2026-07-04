"""URL route patterns for the ForkMesh relay Worker.

This module holds every compiled path regex the Worker's router (`Default._route`
in ``entry.py``) matches requests against, plus the git smart-HTTP endpoint
patterns. Keeping the route table in one small module makes the request surface
easy to scan without paging through the 11k-line worker, and lets the Worker and
tests import a single source of truth for "which path shape maps to which
handler".

Only pure ``re``-compiled patterns live here — no runtime state, no js/workers
imports — so this module loads standalone (the test suite parses it the same way
it parses ``entry.py``).
"""

import re

# Each room exposes a WebSocket (/ws) and a read-only live client count
# (/clients); the Durable Object picks behavior from the upgrade header.
ROOM_RE = re.compile(r"^/api/room/([^/]+)/(?:ws|clients)$")
REPO_ROOM_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/rooms/([^/]+)/(?:ws|clients)$")
# Issue inbox: signed submissions from people without write access to the repo.
REPO_ISSUES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/issues$")
# Pull-request inbox: signed PR submissions from any node.
REPO_PULLS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/pulls$")
# Commit-comment inbox: signed per-commit comments from any node.
REPO_COMMITS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/commits$")
# Discussion inbox: signed discussion open/comment submissions from any node.
REPO_DISCUSSIONS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/discussions$")
# Thread subscriptions (issue #361): a node signs a subscribe/unsubscribe for one
# issue or PR so it gets notified of every reply, not just mentions of it.
REPO_SUBSCRIBE_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/subscribe$")
# Issue bounty escrow: mint a per-bounty Solana deposit address, confirm funding,
# and split it 90/10 to the PR author + treasury when the issue's PR merges.
REPO_BOUNTY_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/bounty$")
# Private-repo collaborator ACL (issue #9): owner-signed grant/revoke/list of the
# accounts a private repo is shared with.
REPO_SHARES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/shares$")
# Public mirror health for a logical repo group.
REPO_MIRRORS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/mirrors$")
# Agent-session sync (adhoc #182): desktop node push/drain of Claude Code agent
# sessions for a repo (signed the same way as issue-inbox drain), the
# website's password-gated read of that same list, and a queued text prompt
# the owner sends from the website to one running agent.
REPO_AGENTS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents$")
REPO_AGENTS_LIST_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents/list$")
REPO_AGENTS_PROMPT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/agents/([^/]+)/prompt$")
# One agent session's live transcript (adhoc #259): the desktop pushes a bounded
# tail of each session's run log with the sessions snapshot; the website's agent
# detail page polls this to render (and keep live) the transcript.
REPO_AGENTS_TRANSCRIPT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/agents/([^/]+)/transcript$")
# Live tunnel: desktop clients connect to /host; the website pulls /tree and
# /blob, which the worker forwards to the best-connected host.
REPO_HOST_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history|commit|branches|search)$")
# Release asset download (issue #304): the bytes live in the node's
# content-addressed store (never in git), streamed back over the host tunnel.
# Stable, content-addressed URL — immutable, so it caches forever at the edge.
RELEASE_BLOB_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/releases/blob/sha256/([0-9a-f]{64})$")
# Per-artifact download counts for a repo's releases (issue: Releases tab).
REPO_RELEASE_DOWNLOADS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/releases/downloads$")
# Git smart-HTTP clone endpoints: git clone https://host/<node>/<repo>
GIT_INFO_RE = re.compile(r"^/([^/]+)/([^/]+)/info/refs$")
GIT_PACK_RE = re.compile(r"^/([^/]+)/([^/]+)/git-upload-pack$")
# git push endpoint (issue #358): receive-pack over the same relay tunnel, gated
# by an owner-key-signed HTTP Basic token (see verify_push_token).
GIT_RECEIVE_RE = re.compile(r"^/([^/]+)/([^/]+)/git-receive-pack$")
# Account API: reserve/finalize/login and GET /api/accounts/{name} are all
# single-segment, so this one pattern gates the whole accounts_handler dispatch.
ACCOUNTS_RE = re.compile(r"^/api/accounts/([^/]+)$")
