"""D1 table/index DDL for the relay, split out of entry.py.

SCHEMA_STATEMENTS is the ordered list of idempotent CREATE TABLE / CREATE
INDEX statements ensure_schema() runs on first request. It is pure data with
no runtime dependency, so it lives in its own js-free sibling module the
Worker runtime bundles and entry.py re-imports.
"""

SCHEMA_STATEMENTS = [
    # email_bi (blind index of the email) lets users log in by email, not just
    # node name (migration 0003). is_admin is an operator-settable flag and name
    # is the public node name in plaintext, so an admin can be granted directly
    # in the DB: UPDATE accounts SET is_admin=1 WHERE name='alice' (migration 0006).
    # ip_bi is the blind index (keyed HMAC) of the signup IP — never the IP itself,
    # which lives only inside the encrypted `data` blob. It lets anti-abuse count
    # how many accounts share a source IP for uniqueness without storing or
    # exposing a reversible address (migration 0015).
    "CREATE TABLE IF NOT EXISTS accounts (name_bi TEXT PRIMARY KEY, data TEXT NOT NULL, "
    "email_bi TEXT, name TEXT, is_admin INTEGER NOT NULL DEFAULT 0, ip_bi TEXT)",
    "CREATE INDEX IF NOT EXISTS idx_accounts_email ON accounts(email_bi)",
    "CREATE INDEX IF NOT EXISTS idx_accounts_ip ON accounts(ip_bi)",
    """CREATE TABLE IF NOT EXISTS account_devices (
        device_bi TEXT PRIMARY KEY,
        account_bi TEXT NOT NULL,
        pubkey TEXT NOT NULL,
        kind TEXT NOT NULL DEFAULT 'desktop_node',
        label TEXT,
        capabilities TEXT NOT NULL DEFAULT '',
        enabled INTEGER NOT NULL DEFAULT 1,
        created_at INTEGER NOT NULL,
        last_seen INTEGER NOT NULL DEFAULT 0,
        revoked_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_account_devices_account ON account_devices(account_bi)",
    "CREATE INDEX IF NOT EXISTS idx_account_devices_pubkey ON account_devices(pubkey)",
    """CREATE TABLE IF NOT EXISTS repositories (
        key_bi TEXT PRIMARY KEY, owner_bi TEXT NOT NULL, data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_repos_owner ON repositories(owner_bi)",
    # Per-repo collaborator ACL (issue #9): which grantee accounts an owner has
    # shared a private repo with. repo_bi = blind_index("<owner>/<repo>") (the
    # same key as repositories.key_bi); grantee_bi = blind_index(grantee account
    # name). `data` is the encrypted {grantee, owner, repo, ts} so the owner's UI
    # can list grantee names back. A grantee with a row here additionally sees the
    # repo in their authenticated catalog and may clone it with THEIR OWN key.
    """CREATE TABLE IF NOT EXISTS repo_shares (
        repo_bi TEXT NOT NULL, grantee_bi TEXT NOT NULL,
        data TEXT NOT NULL, ts INTEGER NOT NULL,
        PRIMARY KEY (repo_bi, grantee_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_repo_shares_grantee ON repo_shares(grantee_bi)",
    # Catalog write throttle: last write time per owner (blind index). Plaintext
    # timestamp only — no user content.
    "CREATE TABLE IF NOT EXISTS catalog_rate (owner_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)",
    """CREATE TABLE IF NOT EXISTS issue_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_issue_inbox_repo ON issue_inbox(repo_bi)",
    """CREATE TABLE IF NOT EXISTS pull_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_pull_inbox_repo ON pull_inbox(repo_bi)",
    """CREATE TABLE IF NOT EXISTS commit_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_commit_inbox_repo ON commit_inbox(repo_bi)",
    """CREATE TABLE IF NOT EXISTS discussion_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL, submitter_bi TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_discussion_inbox_repo ON discussion_inbox(repo_bi)",
    # Agent-session sync (website "Agents" tab, adhoc #182): the desktop app
    # pushes a full-replace snapshot of its running/finished Claude Code agent
    # sessions for a repo (one row per session, keyed by the desktop's local
    # session id) so the owner can see them on the website.
    """CREATE TABLE IF NOT EXISTS repo_agents (
        repo_bi TEXT NOT NULL, agent_id TEXT NOT NULL,
        data TEXT NOT NULL, updated_at INTEGER NOT NULL,
        PRIMARY KEY (repo_bi, agent_id))""",
    "CREATE INDEX IF NOT EXISTS idx_repo_agents_repo ON repo_agents(repo_bi)",
    # Prompts the website owner queues for a running agent; the desktop drains
    # (selects + deletes) this table the same way it drains issue_inbox.
    """CREATE TABLE IF NOT EXISTS agent_prompts (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        agent_id TEXT NOT NULL, data TEXT NOT NULL, queued_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_agent_prompts_repo ON agent_prompts(repo_bi)",
    # Issue bounty escrow: one row per (repo, issue number). data is the encrypted
    # record holding the deposit address, its Ed25519 seed, the required amount,
    # and payout state. bounty_bi = blind_index("<owner>/<repo>#<number>").
    """CREATE TABLE IF NOT EXISTS issue_bounty (
        bounty_bi TEXT PRIMARY KEY, data TEXT NOT NULL)""",
    # Inbuilt per-owner bounty wallet (issue #347): one row per owner. data is the
    # encrypted custody deposit key the owner pre-funds; per-PR bounties in
    # "wallet" mode are paid by debiting it. wallet_bi = blind_index("bounty-wallet:<owner>").
    """CREATE TABLE IF NOT EXISTS bounty_wallet (
        wallet_bi TEXT PRIMARY KEY, data TEXT NOT NULL)""",
    # Server-side 5xx / error log surfaced on the admin dashboard.
    """CREATE TABLE IF NOT EXISTS error_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,
        status INTEGER NOT NULL, method TEXT, path TEXT, message TEXT, ray TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_error_log_ts ON error_log(ts)",
    # Anonymous installer diagnostics surfaced on the admin dashboard. One row per
    # reported install step (start/mirror/deps/fetch/build/install/launch/done).
    # `run` is a random id the installer mints per run — it is NOT tied to any
    # account, email, or IP; nothing user-identifying is stored here (plaintext
    # operational diagnostics only, like error_log).
    """CREATE TABLE IF NOT EXISTS install_diag (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,
        run TEXT NOT NULL, step TEXT NOT NULL, ok INTEGER NOT NULL,
        os TEXT, arch TEXT, pm TEXT, distro TEXT, version TEXT, detail TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_install_diag_ts ON install_diag(ts)",
    "CREATE INDEX IF NOT EXISTS idx_install_diag_run ON install_diag(run)",
    # Opt-in crash/stall telemetry from desktop nodes (issue #354). One row per
    # reported event. `node` is a client-computed one-way hash of the node's
    # public key (an anonymized grouping key, NOT the key or any account); no
    # email/IP is stored. `summary` is the scrubbed crash/stall text — repo names
    # and filesystem paths are removed client-side before it is ever sent. Purely
    # operational, like error_log / install_diag.
    """CREATE TABLE IF NOT EXISTS telemetry (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,
        node TEXT NOT NULL, kind TEXT NOT NULL, version TEXT, os TEXT,
        summary TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_telemetry_ts ON telemetry(ts)",
    # Live host presence: lets /api/network/stats report "hosts online" without
    # probing every repo's tunnel Durable Object on every page view. repo_bi is a
    # blind index (no plaintext repo name), ts is refreshed while a host is active
    # and a staleness window self-heals rows left behind by a missed disconnect.
    "CREATE TABLE IF NOT EXISTS host_presence (repo_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_host_presence_ts ON host_presence(ts)",
    # Per-repo round-robin cursor for clone fallbacks. When a repo's named host is
    # offline, a clone is redirected to one of its online mirrors; this counter is
    # bumped on each redirect so the picks rotate across every mirror instead of
    # all piling onto the single freshest one. repo_bi is the same blind index
    # host_presence uses; n is a monotonically increasing rotation cursor.
    "CREATE TABLE IF NOT EXISTS clone_rr (repo_bi TEXT PRIMARY KEY, n INTEGER NOT NULL DEFAULT 0)",
    # Which mirror is currently serving a downed repo's clones IN PLACE (same
    # URL, no redirect). git clones are two requests (info/refs then the
    # upload-pack POST) that must reach the SAME node, so the pick is pinned
    # here for a short window instead of rotating per request; rotation happens
    # when the pin expires. The owner name is public catalog data.
    "CREATE TABLE IF NOT EXISTS clone_sticky ("
    "repo_bi TEXT PRIMARY KEY, owner TEXT NOT NULL, ts INTEGER NOT NULL)",
    # Recent owner-attested repo-state pins (sha256 of the canonical heads+tags
    # advertisement), appended on every catalog publish by a working-copy holder
    # ("local-node"). Mirrors are integrity-checked against the SOURCE's pins —
    # current plus this short history — instead of their own self-attested hash,
    # so a tampered mirror can't just publish a matching pin for its forged refs,
    # while an honest mirror that lags the source by a few publishes still clones
    # (see clone_state_pins). Pruned to the newest STATE_PIN_HISTORY per repo.
    "CREATE TABLE IF NOT EXISTS repo_state_history ("
    "key_bi TEXT NOT NULL, state_hash TEXT NOT NULL, ts INTEGER NOT NULL, "
    "PRIMARY KEY (key_bi, state_hash))",
    # Account-level presence: a node heartbeats here while it is online so it can
    # be included in the reward split. Keyed by the account blind index.
    "CREATE TABLE IF NOT EXISTS account_presence (name_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_account_presence_ts ON account_presence(ts)",
    # Login throttle: failed-attempt counter + lockout per login identifier (blind
    # index). No plaintext credential is stored — only the HMAC of the identifier.
    "CREATE TABLE IF NOT EXISTS login_attempts (id_bi TEXT PRIMARY KEY, "
    "fails INTEGER NOT NULL DEFAULT 0, first_fail_ts INTEGER NOT NULL DEFAULT 0, "
    "locked_until INTEGER NOT NULL DEFAULT 0)",
    # Installer link-code rendezvous (adhoc #53): install.sh mints a short code
    # the fresh headless node registers with, and the installing user's desktop
    # app offers the same code signed by its key. Whichever side arrives first
    # parks its half here; the second side completes the link (node.owner =
    # user, user.nodes += node) and deletes the row. code_bi is the blind index
    # of "link:<code>"; data is the encrypted {node|user}; rows expire after
    # LINK_CODE_TTL_MS and are pruned as they are looked up.
    "CREATE TABLE IF NOT EXISTS link_codes (code_bi TEXT PRIMARY KEY, "
    "data TEXT NOT NULL, ts INTEGER NOT NULL)",
    # Email-verification queue: newly finalized accounts land here until an admin
    # manually verifies them (placeholder until a real email service like SES is
    # wired up). data = encrypted {name, email, joinedAt}.
    "CREATE TABLE IF NOT EXISTS pending_verifications (name_bi TEXT PRIMARY KEY, data TEXT NOT NULL)",
    # Hourly online-activity samples for the /network/ graph. A per-minute cron
    # adds the current online-node count into the current hour's bucket, so
    # node_minutes is "node-minutes online" that hour (one node online all hour
    # = 60). hour_ts is the epoch-ms start of the hour.
    "CREATE TABLE IF NOT EXISTS online_hourly (hour_ts INTEGER PRIMARY KEY, node_minutes INTEGER NOT NULL DEFAULT 0)",
    """CREATE TABLE IF NOT EXISTS online_hourly_nodes (
        hour_ts INTEGER NOT NULL, node_key TEXT NOT NULL, label TEXT NOT NULL,
        node_minutes INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (hour_ts, node_key))""",
    "CREATE INDEX IF NOT EXISTS idx_online_hourly_nodes_key ON online_hourly_nodes(node_key)",
    # Retained chat history: the relay keeps the last few days of *encrypted*
    # durable messages per room so a node joining later sees some history even
    # when no peer is online to replay it. body is the opaque encrypted envelope
    # exactly as relayed; the server never sees plaintext. room_key is the same
    # key used to address the room Durable Object.
    """CREATE TABLE IF NOT EXISTS chat_history (
        room_key TEXT NOT NULL, msg_id TEXT NOT NULL, ts INTEGER NOT NULL,
        body TEXT NOT NULL, PRIMARY KEY (room_key, msg_id))""",
    "CREATE INDEX IF NOT EXISTS idx_chat_history_room_ts ON chat_history(room_key, ts)",
    # --- Relay federation (main relay only) ---------------------------------
    # Allowlist of relays that federate with this (main) relay. A relay is known
    # by its Ed25519 pubkey; only status='approved' relays may custody signups
    # here or have their reported nodes count toward disbursement. Grant directly:
    #   UPDATE relays SET status='approved' WHERE label='...';
    """CREATE TABLE IF NOT EXISTS relays (
        relay_bi TEXT PRIMARY KEY, pubkey TEXT NOT NULL, label TEXT,
        base_url TEXT, status TEXT NOT NULL DEFAULT 'pending',
        registered_at INTEGER, approved_at INTEGER)""",
    # Online node payout wallets reported by federated relays (ts = last report),
    # unioned into the disbursement split alongside this relay's own nodes.
    """CREATE TABLE IF NOT EXISTS federated_presence (
        relay_bi TEXT NOT NULL, wallet TEXT NOT NULL, name TEXT, ts INTEGER NOT NULL,
        PRIMARY KEY (relay_bi, wallet))""",
    "CREATE INDEX IF NOT EXISTS idx_federated_presence_ts ON federated_presence(ts)",
    # Signups proxied here from a federated relay: this relay mints + custodies the
    # deposit wallet and sweeps it. Encrypted blob reuses the donation_* field
    # names so _sweep_confirmed_donation operates on it unchanged.
    """CREATE TABLE IF NOT EXISTS federated_signup (
        reference TEXT PRIMARY KEY, relay_bi TEXT NOT NULL, data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_federated_signup_relay ON federated_signup(relay_bi)",
    # This relay's own federation identity (federated relays only): a single
    # encrypted row holding the auto-generated Ed25519 keypair used to sign
    # relay->main calls. id is always 1.
    "CREATE TABLE IF NOT EXISTS relay_self (id INTEGER PRIMARY KEY, data TEXT NOT NULL)",
    # Central donation fund (issue #308): a single worker-custodied Solana wallet
    # anyone can donate to, swept out to online nodes on an hourly cron. The
    # encrypted blob holds the deposit address, its Ed25519 seed, and the last
    # distribution's timestamp/signature. id is always 1. Main relay only.
    "CREATE TABLE IF NOT EXISTS central_fund (id INTEGER PRIMARY KEY, data TEXT NOT NULL)",
    # --- Leaderboard backing data (issue #11) -------------------------------
    # First time each repo's tunnel was ever seen live, so the "longest hosted"
    # board can rank by age. repo_bi is the same blind index host_presence uses
    # (== repositories.key_bi for published repos); written once and never
    # updated, so it survives the host going offline and coming back.
    "CREATE TABLE IF NOT EXISTS repo_first_hosted (repo_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)",
    # Cumulative contributor activity for the "contributor activity" board. Keyed
    # by a blind index of the (public) author name; the plaintext display name is
    # kept alongside since issue/PR authorship is already public. The inbox tables
    # get drained on merge, so the running tally lives here instead.
    """CREATE TABLE IF NOT EXISTS contributor_activity (
        author_bi TEXT PRIMARY KEY, name TEXT NOT NULL,
        issues INTEGER NOT NULL DEFAULT 0, pulls INTEGER NOT NULL DEFAULT 0,
        commits INTEGER NOT NULL DEFAULT 0, total INTEGER NOT NULL DEFAULT 0,
        last_ts INTEGER)""",
    "CREATE INDEX IF NOT EXISTS idx_contributor_activity_total ON contributor_activity(total)",
    # Cumulative funds (lamports) actually disbursed to each recipient, for the
    # "funds received" boards. scope is 'mainnode' (donation-sweep node split),
    # 'contributor' (bounty payee), or 'project' (the owner/repo a paid bounty
    # belonged to). key is the recipient's Solana address (mainnode/contributor)
    # or "owner/repo" (project). Treasury transfers are never recorded here.
    """CREATE TABLE IF NOT EXISTS funds_received (
        scope TEXT NOT NULL, key TEXT NOT NULL, name TEXT,
        lamports INTEGER NOT NULL DEFAULT 0, last_ts INTEGER,
        PRIMARY KEY (scope, key))""",
    """CREATE TABLE IF NOT EXISTS notifications (
        dedupe_bi TEXT PRIMARY KEY,
        recipient_bi TEXT NOT NULL,
        ts INTEGER NOT NULL,
        read_at INTEGER NOT NULL DEFAULT 0,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_notifications_recipient_ts ON notifications(recipient_bi, ts)",
    "CREATE INDEX IF NOT EXISTS idx_notifications_unread ON notifications(recipient_bi, read_at, ts)",
    # Thread subscriptions (issue #361): who follows a given issue/PR. thread_bi is
    # a blind index of "thread:<owner>/<repo>:<source>:<number>"; the encrypted
    # data holds the subscriber's (public) node name plus a `muted` flag so an
    # explicit unsubscribe survives the auto-subscribe that fires when someone
    # comments. Enqueue reads the name back to fan a reply out to every follower.
    """CREATE TABLE IF NOT EXISTS thread_subscriptions (
        thread_bi TEXT NOT NULL,
        subscriber_bi TEXT NOT NULL,
        ts INTEGER NOT NULL,
        data TEXT NOT NULL,
        PRIMARY KEY (thread_bi, subscriber_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_thread_subscriptions_thread ON thread_subscriptions(thread_bi)",
    # One row per completed release-asset download (served by _release_blob), so
    # the Releases tab can show a per-artifact download count and the admin
    # dashboard has a plain event log of them (same treatment as install_diag).
    # repo_bi is the blind index of "<owner>/<repo>"; sha256 is the content
    # address of the asset. No account/IP is recorded.
    """CREATE TABLE IF NOT EXISTS release_downloads (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        sha256 TEXT NOT NULL, ts INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_release_downloads_repo_sha "
    "ON release_downloads(repo_bi, sha256)",
    # Private vulnerability reports submitted via /api/security/report. The body
    # is AES-GCM encrypted at rest (DATA_KEY); only the operator can read the
    # plaintext. No IP or identifying information is stored beyond what the
    # reporter voluntarily provides (contact field). Bounded by MAX_SECURITY_REPORTS
    # so the unauthenticated endpoint can't grow D1 without limit.
    """CREATE TABLE IF NOT EXISTS security_reports (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ts INTEGER NOT NULL,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_security_reports_ts ON security_reports(ts)",
    # Public /status page (30-day history per system). A per-minute cron folds
    # one health check per system into today's UTC-day bucket; checks/failures
    # let the page compute an uptime percentage per day without storing every
    # individual sample. day_ts is the epoch-ms start of the UTC day.
    """CREATE TABLE IF NOT EXISTS system_status_daily (
        day_ts INTEGER NOT NULL, system TEXT NOT NULL,
        checks INTEGER NOT NULL DEFAULT 0, failures INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (day_ts, system))""",
    "CREATE INDEX IF NOT EXISTS idx_system_status_daily_day ON system_status_daily(day_ts)",
    # Hourly breakdown backing the per-day sliver bars on /status. `reason` is a
    # short human-readable note on the most recent failing check that hour (e.g.
    # which path/host check failed), so hovering a degraded/down hour explains
    # why instead of just showing a color.
    """CREATE TABLE IF NOT EXISTS system_status_hourly (
        hour_ts INTEGER NOT NULL, system TEXT NOT NULL,
        checks INTEGER NOT NULL DEFAULT 0, failures INTEGER NOT NULL DEFAULT 0,
        reason TEXT,
        PRIMARY KEY (hour_ts, system))""",
    "CREATE INDEX IF NOT EXISTS idx_system_status_hourly_hour ON system_status_hourly(hour_ts)",
]

