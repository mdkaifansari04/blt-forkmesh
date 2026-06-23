# ForkMesh — Flutter client (iOS / Android)

A mobile Flutter client for the ForkMesh network. It speaks the **same encrypted
relay protocol** and **same signed inbox format** as the Qt client
(`qt_client/`), so it shares chat rooms, reads the same catalog/issue/PR data,
and submits issues/PRs/votes that the relay accepts — a real peer, not a mock.

> Platforms: **iOS and Android only.** (No desktop/web targets.)
> Status: core read + chat + **signed write paths**. The Qt client is ~35k lines;
> the heavy local-git / agents / wallet subsystems are scoped under "Remaining".

## Run

```bash
cd flutter_app
flutter pub get
flutter run                 # on a connected iOS/Android device or emulator
flutter build apk           # Android (needs Android SDK)
flutter build ipa           # iOS (needs macOS + Xcode)
```

## Architecture

```
lib/
  main.dart                 entry, provider wiring, auto-connect
  theme.dart                GitHub-Primer dark theme (ported from qt Theme.h)
  models/models.dart        Member, ChatMessage, Repository, Issue, PullRequest, NetworkStats
  services/
    room_crypto.dart        AES-256-GCM + PBKDF2-HMAC-SHA256 — byte-compatible with RoomCrypto.cpp
    identity.dart           Ed25519 node identity; base64url pubkey = node id / inbox author
    relay_service.dart      WebSocket relay client (ServerNode equivalent): connect,
                            presence heartbeat + stale-peer reaping, roster, chat
    api_service.dart        worker REST reads: repositories, issues, pulls, commits, network stats
    inbox_service.dart      signed write paths (issues/PRs/votes/comments/reviews)
    settings_service.dart   persisted profile / relay / token settings
  screens/                  home_shell, chat, repos (+ repo detail), notifications, settings
  widgets/connection_dot.dart  avatar + presence dot
```

State: `provider` (ChangeNotifier services). No code generation.

## Interoperability

- **Crypto** ports `RoomCrypto.cpp`: AES-256-GCM (12-byte nonce, 16-byte tag),
  key = PBKDF2-HMAC-SHA256(passphrase|app key, SHA256("ForkMesh room:"+room)[:16],
  210000 rounds, 32 bytes); envelope {kind:"cipher",v:1,nonce,tag,body} (base64).
- **Identity**: Ed25519; the base64url (no padding) public key is the node id and
  the `author` on signed events — matching ForkMeshIdentity and the worker's
  `ed25519_verify` (raw 32-byte key, raw 64-byte sig, base64url).
- **Transport**: WebSocket to the relay (default
  wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws).

### Signed write paths (inbox)

`inbox_service.dart` builds canonical strings byte-identical to
`cloudflare_worker/src/entry.py` and signs them, then POSTs to the repo inbox:

| Action | endpoint | canonical prefix |
|---|---|---|
| New issue (`open`) | `POST /api/repo/{o}/{n}/issues` | `forkmesh-issue-event-v1` |
| Issue comment / vote / status | `POST …/issues` | `forkmesh-issue-event-v1` |
| New PR | `POST …/pulls` (`pull`) | `forkmesh-pull-event-v1` |
| PR comment / review | `POST …/pulls` (`event`) | `forkmesh-pull-comment-v1` |

Content is SHA-256-hex of the NUL-joined, type-specific fields (see
`_issueContent` / `_pullCommentContent`). Submissions land in the relay inbox;
the **repo owner** drains, applies, commits and syncs back (owner-side, still the
Qt client) — so a submitted item shows up after the owner merges it, which the
UI states ("pending the repo owner applying it").

## Implemented

- Encrypted chat on the live room: channels, DMs, roster + presence, optimistic
  send, unread badges, stale-peer reaping.
- Repository catalog + repo detail (About, Commits, Issues, Pull requests) reads.
- **Signed writes**: open issue, comment, vote, open/close; open PR, comment,
  approve / request changes.
- Network stats + connection/activity log. Settings (profile, Solana address,
  relay, GitHub/GitLab tokens). Persistent Ed25519 identity; GitHub-style theme.

## Remaining parity work (roadmap)

1. **Inbox drain/apply (owner side)** — applying submissions to the git mirror +
   sync-back. Needs git; stays on the owner's full client for now.
2. **Repo file browsing** — tree/blob, README render, last-commit + size bars.
3. **Local repos & git** — clone/mirror/host/publish/push (native git or a
   server-side helper on mobile).
4. **Solana wallet** — balance (SOL/USD/INR), deposit-to-verify banner, payouts,
   bounty escrow QR.
5. **Agents (OpenAI/Claude) and Actions (.forkmesh CI)** — shell out locally in
   Qt; mobile needs a different execution model.
6. **Account/auth** — signup/login, TOTP, email verification, heartbeat.
7. System notifications; light-theme toggle (palette ready in theme.dart).

## Notes

- Inbox signing is verified against `cloudflare_worker/src/entry.py`
  (verify_issue_event / verify_pull_event / verify_pull_comment_event). If those
  canonical strings change, update `inbox_service.dart` in lockstep.
