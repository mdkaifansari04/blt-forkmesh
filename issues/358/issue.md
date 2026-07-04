---
schema: forkmesh-issue-v1
number: 358
title: git push: receive-pack over the relay tunnel with signed-identity auth
status: closed
labels: [feature, security]
milestone: v1
priority: 20
progress: 0
assignees: [Claude Code]
createdAt: 1783116818257
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-358
ts: 1783116818257
attachments: []
sig: XqEILUJV8VRyhHxcu-lIJ3ZApGgVmUVF0rUiNkJB1vsn_e4tp9w9Y-24F1gVjfzXVsB6XRvmWeFCH0jIDMbTBA
---

**Roadmap Phase 3 (complete forge).** The relay serves `git-upload-pack` only (`GIT_PACK_RE`, `cloudflare_worker/src/entry.py` ~124); pushing to forkmesh.com 404s and publishing requires a local sync. This is the single biggest missing forge primitive.

- Worker: add the `git-receive-pack` route and the `?service=git-receive-pack` advertisement on `info/refs`. Stream request/response bodies through the DO tunnel exactly like upload-pack (TransformStream — never buffer a pack in the isolate; buffering a clone pack already caused a site-wide OOM outage once).
- Auth: only the repo owner's Ed25519 key (later: listed collaborator keys). Simplest binding that stays in the existing trust model: HTTP Basic where username = pubkey (base64url) and password = an Ed25519 signature over a short-lived server nonce; verify with the existing `ed25519_verify` in `entry.py`.
- Host side: `qt_client/src/ServerNode.cpp` / `RepoHost.cpp` spawn `git receive-pack` against the bare mirror, then run the normal post-sync publish path — integrity pins **must** be re-attested after an accepted push (see the pin re-attest sweep in `MainWindowActions.cpp`).

References: smart HTTP protocol <https://git-scm.com/docs/http-protocol>, <https://git-scm.com/docs/git-receive-pack>.
