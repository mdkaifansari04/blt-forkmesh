# ForkMesh — todo

1) get pull requests working one node's repo can do a PR to another node's repo, and the file changes can be seen in the client and in the web browser



## Remaining

1. **Email sending from the main node** look into free senders mailgun or others - looking for a better solution to this

2. **Website signup + client account UI (#4 finish).**
   The accounts backend works; the surfaces don't exist yet.
   Left to build:
   - Website signup page that requires a running node (the node signs the
     signup), lets the user pick a unique node name and submit their email.
   - In-node Account UI (Settings): email field + "Register node name" using
     `ForkMeshIdentity::signData()`, plus a login/status indicator.
   - Enforce the `^[a-z][a-z0-9]*$` node-name rule on the client handle field
     (currently only the worker enforces it).

3. **Anti-spoofing / security hardening (#7).**
   A real review found the main hole: **anyone can currently host or publish
   under any node name** — host `/host` connections and catalog/files POSTs do
   not verify ownership (signatures are carried but never checked).
   Left to build:
   - Require a node-key-signed token on `/host` connect and on catalog/files
     publish, verified against the registered account's public key. (The
     verification primitive — `ed25519_verify` — is already in the worker.)
   - Rate-limit messages, host requests, and catalog POSTs.
   - Lower the 96 MB room-frame cap (amplification) and add catalog anti-spam.

4. **Per-second activity chart on the dashboard / home page (#6).**
   The client Home already has `#homeGraph` / `#homeScoreBoard` styles started.
   Left to build: a live per-second chart driven by message/relay activity.

## Notes / decisions in effect

- File browsing and clone are **pure live**: data is served on demand from a
  connected host; nothing is stored on the relay. If no host is online, the
  repo is unavailable (the website caches what you've browsed in localStorage). this allows us to keep the relay lean and can be hosted on most free plans
- Accounts use the node's existing **Ed25519 identity** for auth — no passwords.
- Set a real `ACCOUNTS_KEY` secret in production
  (`pywrangler secret put ACCOUNTS_KEY`) before relying on email encryption.
- **Relaunch the client** to pick up clone-hosting, keepalive, and counters —
  the running instance predates them.
