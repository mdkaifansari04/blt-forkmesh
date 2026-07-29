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
# Platform-administrator-created private chat channels. Public paths use only
# opaque 128-bit identifiers; human-readable channel names stay encrypted.
CHAT_CHANNELS_RE = re.compile(r"^/api/chat/channels/?$")
CHAT_CHANNEL_MEMBERS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/members/?$")
CHAT_CHANNEL_ROOM_ACCESS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/room-access/?$")
# Retained (still-encrypted) backlog of one channel room. Desktop clients hold
# no WebSocket into these rooms, so they replay the World office's channel chat
# over this read-only poll instead.
CHAT_CHANNEL_HISTORY_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/history/?$")
CHAT_CHANNEL_WS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/ws/?$")
# One-to-one direct messages use opaque conversation identifiers and a
# participant-only authorization policy with no administrator bypass.
CHAT_DIRECT_MESSAGES_RE = re.compile(r"^/api/chat/direct-messages/?$")
CHAT_DIRECT_MESSAGE_USERS_RE = re.compile(
    r"^/api/chat/direct-messages/users/?$")
CHAT_DIRECT_MESSAGE_ROOM_ACCESS_RE = re.compile(
    r"^/api/chat/direct-messages/([0-9a-f]{32})/room-access/?$")
CHAT_DIRECT_MESSAGE_READ_RE = re.compile(
    r"^/api/chat/direct-messages/([0-9a-f]{32})/read/?$")
CHAT_DIRECT_MESSAGE_WS_RE = re.compile(
    r"^/api/chat/direct-messages/([0-9a-f]{32})/ws/?$")
# Issue inbox: signed submissions from people without write access to the repo.
REPO_ISSUES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/issues$")
# Pull-request inbox: signed PR submissions from any node.
REPO_PULLS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/pulls$")
# Authenticated, asynchronous merge of one exact open pull request.  The
# repository alias rewrite runs before this route, so the handler always
# authorizes and dispatches against the canonical backing-node namespace.
REPO_PULL_MERGE_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/pulls/([1-9][0-9]{0,8})/merge$")
# Owner/write-authorized, bounded redacted run summaries from an attested
# mirror Actions executor. No workflow variables or public catalog data use
# this endpoint.
REPO_ACTION_RUNS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/actions/runs$")
# Commit-comment inbox: signed per-commit comments from any node.
REPO_COMMITS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/commits$")
# Discussion inbox: signed discussion open/comment submissions from any node.
REPO_DISCUSSIONS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/discussions$")
# Content-free tallies of inbox items awaiting the owner node's next sync, so
# the website can badge tabs with "N pending" without owner auth (counts only —
# the items themselves stay encrypted and owner-gated).
REPO_PENDING_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/pending$")
# Thread subscriptions (issue #361): a node signs a subscribe/unsubscribe for one
# issue or PR so it gets notified of every reply, not just mentions of it.
REPO_SUBSCRIBE_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/subscribe$")
# Frozen issue-bounty compatibility route. New wallet/create/payout actions fail
# closed; status reads expose migration-only historical metadata without keys.
REPO_BOUNTY_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/bounty$")
# Private-repo collaborator ACL (issue #9): owner-signed grant/revoke/list of the
# accounts a private repo is shared with.
REPO_SHARES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/shares$")
# Authenticated daily security-scan ingest and visibility-gated read models.
# Public readers receive the compact redacted clipboard only; rich history is
# owner-authorized and private repositories otherwise remain indistinguishable
# from missing repositories.
REPO_SECURITY_SCANS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/security-scans/"
    r"(lease|ingest|latest|history|triage)$")
# Public mirror health for a logical repo group.
REPO_MIRRORS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/mirrors$")
# Catalog-facing About details editable from the dashboard by the source owner.
REPO_ABOUT_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/about$")
REPO_LOGO_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/logo$")
REPO_LOGO_SUGGESTIONS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/logo-suggestions$")
# Agent-session sync (adhoc #182): desktop node push/drain of Claude Code agent
# sessions for a repo (signed the same way as issue-inbox drain), the
# owner-authorized retrieval of that same ciphertext list, and owner-sealed
# prompts consumed only by the local recipient key.
REPO_AGENTS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents$")
REPO_AGENTS_LIST_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents/list$")
REPO_AGENTS_ACK_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents/ack$")
REPO_AGENTS_PROMPT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/agents/([^/]+)/prompt$")
# One agent session's owner-sealed snapshot (including its bounded transcript
# tail). An authorized client can retrieve the ciphertext, but only the owner
# device holding the hybrid private key can decrypt it.
REPO_AGENTS_TRANSCRIPT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/agents/([^/]+)/transcript$")
# Organization-member coding bots. The organization routes retain the public
# org alias for membership checks; mirror jobs use the selected node namespace
# and owner signatures.
ORG_AGENT_BOTS_RE = re.compile(
    r"^/api/orgs/([^/]+)/repos/([^/]+)/agent-bots$")
ORG_AGENT_BOT_RE = re.compile(
    r"^/api/orgs/([^/]+)/repos/([^/]+)/agent-bots/([^/]+)$")
REPO_ORG_AGENT_JOBS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/org-agent-jobs$")
REPO_ORG_AGENT_JOB_RESULT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/org-agent-jobs/([1-9][0-9]{0,18})/result$")
# Owner-only encryption policy and recipient-key registration for private
# repository/agent data. The relay stores public bundles and opaque envelopes,
# never recipient private keys.
REPO_PRIVACY_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/privacy$")
# Authorized download of one owner-sealed private mirror archive. Client-facing
# private transport is deliberately identity-free: platform request logs see
# only a random 256-bit ciphertext locator, never an owner or repository name.
# Authorization is carried in the non-forwarded HTTP Authorization header.
PRIVATE_REPLICA_ACCESS_RE = re.compile(
    r"^/api/private-replicas/([0-9a-f]{64})$")
# `/host` is a control-only presence/update WebSocket. The other legacy path
# shapes remain parseable for compatibility, but Default routes authorized
# reads through direct HTTPS; the former repository tunnel is retired.
REPO_HOST_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history|commit|compare|branches|search|stats|sizes)$")
# Stable content-addressed release URL. Default streams it from an attested
# direct-HTTPS endpoint; the host control socket rejects this path.
RELEASE_BLOB_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/releases/blob/sha256/([0-9a-f]{64})$")
# Per-artifact download counts for a repo's releases (issue: Releases tab).
REPO_RELEASE_DOWNLOADS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/releases/downloads$")
# Git smart-HTTP clone endpoints: git clone https://host/<node>/<repo>
GIT_INFO_RE = re.compile(r"^/([^/]+)/([^/]+)/info/refs$")
GIT_PACK_RE = re.compile(r"^/([^/]+)/([^/]+)/git-upload-pack$")
# Reserved receive-pack path. It fails closed until a direct-HTTPS write
# protocol is available and never falls back to the control socket.
GIT_RECEIVE_RE = re.compile(r"^/([^/]+)/([^/]+)/git-receive-pack$")
# --- Organizations + teams (issue #388) ---------------------------------------
# Orgs are user-created namespaces that serve linked repos at /<org>/<repo>
# instead of the hosting node's name. POST /api/orgs creates one (session
# auth); the per-org members/teams/repos collections manage the roster, the
# permission teams, and the repo alias map.
ORGS_RE = re.compile(r"^/api/orgs$")
ORG_RE = re.compile(r"^/api/orgs/([^/]+)$")
ORG_MEMBERS_RE = re.compile(r"^/api/orgs/([^/]+)/members$")
ORG_TEAMS_RE = re.compile(r"^/api/orgs/([^/]+)/teams$")
ORG_TEAM_MEMBERS_RE = re.compile(r"^/api/orgs/([^/]+)/teams/([^/]+)/members$")
ORG_REPOS_RE = re.compile(r"^/api/orgs/([^/]+)/repos$")
ORG_BOT_TOKENS_RE = re.compile(r"^/api/orgs/([^/]+)/bot-tokens$")
BOT_SESSION_RE = re.compile(r"^/api/bot/session$")
# Organization-only, non-custodial succession. The optional action is parsed by
# the isolated API module; the general ORG_RE cannot swallow this subresource.
ORG_SUCCESSION_RE = re.compile(
    r"^/api/orgs/([^/]+)/succession(?:/([^/]+))?$")
# Achievement badges: GET /api/badges is the public fixed catalog; GET on the
# per-account resource lists what an account has earned (public), while
# POST/DELETE grant or revoke a badge (platform-administrator session only).
BADGES_RE = re.compile(r"^/api/badges$")
BADGE_ACCOUNT_RE = re.compile(r"^/api/badges/([^/]+)$")
# Organization-admin digest controls and previews for the org's linked public
# repositories. These controls can suppress org-alias digests, but never
# override the backing repository owner's federation switch.
ORG_FEDIVERSE_RE = re.compile(r"^/api/orgs/([^/]+)/fediverse$")
# Repo-scoped API prefix, matched once by the org-alias rewrite so an org's
# /api/repo/<org>/<repo>/... URLs are re-routed to the linked node's repo
# before any of the per-endpoint patterns above run.
REPO_API_PREFIX_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)(?:/.*)?$")
# Account API: reserve/finalize/login and GET /api/accounts/{name} are all
# single-segment, so this one pattern gates the whole accounts_handler dispatch.
ACCOUNTS_RE = re.compile(r"^/api/accounts/([^/]+)$")
# Public native contribution read model for one profile.
ACCOUNT_CONTRIBUTIONS_RE = re.compile(
    r"^/api/accounts/([^/]+)/contributions$"
)
# Follow/unfollow a public profile: /api/accounts/{name}/follow
ACCOUNT_FOLLOW_RE = re.compile(r"^/api/accounts/([^/]+)/follow$")
# Referral short link: /r/{name} counts a click and bounces to /signup.
REFERRAL_LINK_RE = re.compile(r"^/r/([^/]+)$")
# Rendered social-preview card for one share link: the og:image the /r/{name}
# preview page points at, so a Mastodon/Slack unfurl shows that referrer's
# live click + signup counters.
REFERRAL_CARD_RE = re.compile(r"^/api/referrals/([^/]+)/card\.png$")
# --- ActivityPub federation ---------------------------------------------------
# User actor document + its inbox/outbox/followers/following collections.
AP_USER_RE = re.compile(r"^/ap/users/([^/]+)$")
AP_USER_SUB_RE = re.compile(
    r"^/ap/users/([^/]+)/(inbox|outbox|followers|following)$")
# Repository actor (followed as @owner.repo@<domain> from the fediverse).
AP_REPO_RE = re.compile(r"^/ap/repos/([^/]+)/([^/]+)$")
AP_REPO_SUB_RE = re.compile(
    r"^/ap/repos/([^/]+)/([^/]+)/(inbox|outbox|followers|following)$")
# Local ActivityPub object (a published Note), 32-hex uuid.
AP_OBJECT_RE = re.compile(r"^/ap/o/([0-9a-f]{32})$")
# An image embedded in that Note's body, re-served from its stored base64 so
# remote servers (which cannot fetch data: URLs) have a real URL to attach.
AP_OBJECT_MEDIA_RE = re.compile(r"^/ap/o/([0-9a-f]{32})/media/([0-9]+)$")
# Remote fediverse replies attached to a repo thread, readable by clients.
REPO_FEDI_COMMENTS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/fedi-comments$")
# Owner-node push of canonical repo announcements (releases, merged PRs) into
# the fediverse — events the relay never observes through the signed inboxes.
REPO_AP_PUBLISH_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/ap-publish$")
# Repo-owner digest preview. Automatic repository events are accumulated in an
# encrypted, bounded queue and published at most once per 24 hours; this route
# lets the owner inspect the exact public digest text without publishing it.
REPO_AP_DIGEST_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/fediverse-digest$")
# Repo owner's fediverse-post management surface (dashboard): list the repo
# actor's federated posts and delete one (broadcasts a Delete(Tombstone) so it
# disappears from Mastodon). Session/owner-key authed, never public.
REPO_AP_POSTS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/ap-posts$")
# Owner-uploaded repo branding (logo/banner PNG) served publicly — referenced
# by the repo's fediverse actor document as its avatar/header.
REPO_MEDIA_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/media/(logo|banner)\.png$")
# Rendered social-preview info card (repo stats grid): the repo page's
# og:image, so Mastodon/Slack/Twitter unfurls show the repo's details.
REPO_CARD_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/card\.png$")
# Star/unstar a repo: GET returns the public count (+ the caller's own starred
# state when a session is supplied); POST/DELETE toggle it for the logged-in
# account (session-token authenticated, same as ACCOUNT_FOLLOW_RE).
REPO_STAR_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/star$")
