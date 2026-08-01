"""D1 table/index DDL for the relay, split out of entry.py.

SCHEMA_STATEMENTS is the ordered list of idempotent CREATE TABLE / CREATE
INDEX statements ensure_schema() runs on first request. It is pure data with
no runtime dependency, so it lives in its own js-free sibling module the
Worker runtime bundles and entry.py re-imports.
"""

SCHEMA_STATEMENTS = [
    # The legacy accounts table (superseded by users/nodes, migration 0025) was
    # fully drained and dropped by migration 0042; make sure lazily-ensured DBs
    # lose it too.
    "DROP TABLE IF EXISTS accounts",
    # email_bi (blind index of the email) lets users log in by email, not just
    # node name (migration 0003). is_admin is an operator-settable flag and
    # username is the public name in plaintext, so an admin can be granted
    # directly in the DB: UPDATE users SET is_admin=1 WHERE username='alice'
    # (migration 0006).
    # ip_bi is the blind index (keyed HMAC) of the transient signup IP; the raw
    # address is not retained in this row or inside the encrypted `data` blob.
    # It lets anti-abuse count accounts sharing a source network identifier
    # without exposing the original address (migration 0015).
    # enable_outreach is an operator-settable flag, like is_admin, that grants a
    # user access to the founders /outreach console without adding them to the
    # outreach_team roster: UPDATE users SET enable_outreach=1 WHERE
    # username='alice' (migration 0040).
    # The UNIQUE email index is partial: one account per email (keyless nodes
    # have no email, so NULL/'' rows are excluded and unconstrained). Enforced
    # for existing DBs by migration 0041, which also removes any pre-existing
    # duplicate emails.
    # A pre-noncustodial deployment may still have donation_secret inside an
    # existing encrypted data blob. Live writers reject such a record until the
    # explicit offline migration scrubs it; new records contain public wallets
    # only.
    """CREATE TABLE IF NOT EXISTS users (
        user_bi TEXT PRIMARY KEY,
        data TEXT NOT NULL,
        email_bi TEXT,
        username TEXT,
        is_admin INTEGER NOT NULL DEFAULT 0,
        ip_bi TEXT,
        enable_outreach INTEGER NOT NULL DEFAULT 0)""",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_users_email ON users(email_bi) "
    "WHERE email_bi IS NOT NULL AND email_bi <> ''",
    "CREATE INDEX IF NOT EXISTS idx_users_ip ON users(ip_bi)",
    """CREATE TABLE IF NOT EXISTS nodes (
        node_bi TEXT PRIMARY KEY,
        user_bi TEXT,
        pubkey TEXT,
        data TEXT NOT NULL,
        name TEXT,
        last_seen INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_nodes_user ON nodes(user_bi)",
    "CREATE INDEX IF NOT EXISTS idx_nodes_pubkey ON nodes(pubkey)",
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
    # The first concrete publishing device becomes the repository authority.
    # Additional devices owned by the same account remain useful mirrors but
    # cannot replace its signed state merely because they cloned the checkout.
    """CREATE TABLE IF NOT EXISTS repo_source_authorities (
        repo_bi TEXT PRIMARY KEY, node_id TEXT NOT NULL,
        machine_name TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL)""",
    """CREATE TABLE IF NOT EXISTS repo_device_mirrors (
        repo_bi TEXT NOT NULL, node_id TEXT NOT NULL, data TEXT NOT NULL,
        updated_at INTEGER NOT NULL, PRIMARY KEY (repo_bi, node_id))""",
    "CREATE INDEX IF NOT EXISTS idx_repo_device_mirrors_repo "
    "ON repo_device_mirrors(repo_bi, updated_at DESC)",
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
        data TEXT NOT NULL, submitter_bi TEXT,
        claimed_by_bi TEXT NOT NULL DEFAULT '',
        claim_expires_at INTEGER NOT NULL DEFAULT 0,
        mirrored_by_bi TEXT NOT NULL DEFAULT '',
        mirrored_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_issue_inbox_repo ON issue_inbox(repo_bi)",
    """CREATE TABLE IF NOT EXISTS pull_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL, submitter_bi TEXT,
        claimed_by_bi TEXT NOT NULL DEFAULT '',
        claim_expires_at INTEGER NOT NULL DEFAULT 0,
        mirrored_by_bi TEXT NOT NULL DEFAULT '',
        mirrored_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_pull_inbox_repo ON pull_inbox(repo_bi)",
    """CREATE TABLE IF NOT EXISTS discussion_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL, submitter_bi TEXT,
        claimed_by_bi TEXT NOT NULL DEFAULT '',
        claim_expires_at INTEGER NOT NULL DEFAULT 0,
        mirrored_by_bi TEXT NOT NULL DEFAULT '',
        mirrored_at INTEGER NOT NULL DEFAULT 0)""",
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
    # Historical issue-bounty compatibility only. Some pre-noncustodial rows
    # contain an encrypted wallet seed until the explicit offline migration
    # exports and scrubs it. The Worker has no creation, signing, payout, or
    # broadcast path; only a public status projection reads these records.
    """CREATE TABLE IF NOT EXISTS issue_bounty (
        bounty_bi TEXT PRIMARY KEY, data TEXT NOT NULL)""",
    # Historical inbuilt bounty-wallet compatibility. Never written by the live
    # Worker; retained until its encrypted keys are exported/reconciled/scrubbed.
    """CREATE TABLE IF NOT EXISTS bounty_wallet (
        wallet_bi TEXT PRIMARY KEY, data TEXT NOT NULL)""",
    # Server-side 5xx / error log surfaced on the admin dashboard. `actor` is
    # the account name the failing request's own session proved (empty for
    # anonymous traffic), so the admin error view can name the affected user.
    """CREATE TABLE IF NOT EXISTS error_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,
        status INTEGER NOT NULL, method TEXT, path TEXT, message TEXT, ray TEXT,
        actor TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_error_log_ts ON error_log(ts)",
    # Inbox drain audit log (adhoc #97): one row each time the owner's node acks
    # (drains) queued inbox submissions, so a "my web-filed issue disappeared but
    # never synced" report can be traced to when/what drained it. Content-free —
    # repo_bi is a blind index and the only payload is a row count.
    """CREATE TABLE IF NOT EXISTS inbox_drain_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,
        repo_bi TEXT NOT NULL, kind TEXT NOT NULL, count INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_inbox_drain_log_ts ON inbox_drain_log(ts)",
    "CREATE INDEX IF NOT EXISTS idx_inbox_drain_log_repo ON inbox_drain_log(repo_bi)",
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
    # Live host presence: lets /api/network/stats report "hosts online" without
    # probing every repo's tunnel Durable Object on every page view. repo_bi is a
    # blind index (no plaintext repo name), ts is refreshed while a host is active
    # and a staleness window self-heals rows left behind by a missed disconnect.
    "CREATE TABLE IF NOT EXISTS host_presence (repo_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_host_presence_ts ON host_presence(ts)",
    # Direct HTTPS mirror endpoints. These rows contain only public routing
    # identity/health data and never repository bytes, wallet material, node
    # private keys, or private repository names. The node identity signature
    # binds node_name + base_url + public_key + issued_at before registration;
    # a fresh signed challenge updates checked_at/healthy.
    """CREATE TABLE IF NOT EXISTS mirror_https_endpoints (
        node_bi TEXT PRIMARY KEY,
        node_name TEXT NOT NULL,
        base_url TEXT NOT NULL,
        public_key TEXT NOT NULL,
        registration_sig TEXT NOT NULL,
        issued_at INTEGER NOT NULL,
        checked_at INTEGER NOT NULL DEFAULT 0,
        latency_ms INTEGER NOT NULL DEFAULT 0,
        region TEXT,
        healthy INTEGER NOT NULL DEFAULT 0,
        integrity TEXT NOT NULL DEFAULT 'unknown',
        abuse_blocked INTEGER NOT NULL DEFAULT 0,
        health_sig TEXT,
        forkmesh_verified_at INTEGER NOT NULL DEFAULT 0,
        forkmesh_refs_sha256 TEXT NOT NULL DEFAULT '',
        forkmesh_operations_sha256 TEXT NOT NULL DEFAULT '',
        forkmesh_operations_json TEXT NOT NULL DEFAULT '[]',
        forkmesh_active INTEGER NOT NULL DEFAULT 0,
        health_message TEXT NOT NULL DEFAULT '',
        updated_at INTEGER NOT NULL)""",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_mirror_https_endpoint_name "
    "ON mirror_https_endpoints(node_name)",
    "CREATE INDEX IF NOT EXISTS idx_mirror_https_endpoint_health "
    "ON mirror_https_endpoints(healthy, checked_at)",
    """CREATE TABLE IF NOT EXISTS edge_route_cursor (
        repo_bi TEXT PRIMARY KEY,
        cursor INTEGER NOT NULL DEFAULT 0,
        updated_at INTEGER NOT NULL)""",
    # Account-authorized pull merges are pinned to one independently operated
    # node. D1 stores only blind indexes, the caller's opaque idempotency key,
    # exact public Git object ids, and a bounded result; repository bytes and
    # node credentials remain outside the Worker. Expiry/indexes support
    # bounded cleanup; triggers are the concurrency-safe final row ceilings.
    """CREATE TABLE IF NOT EXISTS repo_merge_jobs (
        request_id TEXT PRIMARY KEY,
        request_digest TEXT NOT NULL,
        repo_bi TEXT NOT NULL,
        actor_bi TEXT NOT NULL,
        pull_number INTEGER NOT NULL,
        selected_node TEXT NOT NULL,
        status TEXT NOT NULL CHECK (status IN (
            'requested','succeeded','failed')),
        result TEXT NOT NULL DEFAULT '',
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL CHECK (expires_at > created_at))""",
    "CREATE INDEX IF NOT EXISTS idx_repo_merge_jobs_repo "
    "ON repo_merge_jobs(repo_bi, status, updated_at)",
    "CREATE INDEX IF NOT EXISTS idx_repo_merge_jobs_expiry "
    "ON repo_merge_jobs(expires_at)",
    """CREATE TRIGGER IF NOT EXISTS trg_repo_merge_jobs_repo_bound
       BEFORE INSERT ON repo_merge_jobs
       WHEN (SELECT COUNT(*) FROM repo_merge_jobs
              WHERE repo_bi=NEW.repo_bi) >= 256
       BEGIN
         SELECT RAISE(ABORT, 'repo_merge_jobs_repo_limit');
       END""",
    """CREATE TRIGGER IF NOT EXISTS trg_repo_merge_jobs_global_bound
       BEFORE INSERT ON repo_merge_jobs
       WHEN (SELECT COUNT(*) FROM repo_merge_jobs) >= 10000
       BEGIN
         SELECT RAISE(ABORT, 'repo_merge_jobs_global_limit');
       END""",
    # Private replicas use the same registered HTTPS origins, but only after
    # repository authorization. repo_bi/binding_bi are blind indexes and the
    # random opaque id addresses an owner-sealed ciphertext file; no private
    # repository name, plaintext, recipient secret, or decryption key is stored.
    """CREATE TABLE IF NOT EXISTS private_mirror_routes (
        binding_bi TEXT PRIMARY KEY,
        repo_bi TEXT NOT NULL,
        node_bi TEXT NOT NULL,
        opaque_replica_id TEXT NOT NULL,
        replica_sha256 TEXT NOT NULL,
        key_epoch INTEGER NOT NULL,
        owner_sig TEXT NOT NULL,
        issued_at INTEGER NOT NULL,
        active INTEGER NOT NULL DEFAULT 1,
        updated_at INTEGER NOT NULL,
        FOREIGN KEY (node_bi) REFERENCES mirror_https_endpoints(node_bi)
            ON DELETE CASCADE)""",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_private_mirror_route_replica "
    "ON private_mirror_routes(node_bi, opaque_replica_id)",
    "CREATE INDEX IF NOT EXISTS idx_private_mirror_route_access "
    "ON private_mirror_routes(opaque_replica_id, active, key_epoch, repo_bi)",
    "CREATE INDEX IF NOT EXISTS idx_private_mirror_route_repo "
    "ON private_mirror_routes(repo_bi, active, updated_at)",
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
    # Per-node serve tallies, counted at the router. The Worker is the only
    # component that sees every public read it routes to a mirror endpoint —
    # nodes stopped seeing per-request traffic when the per-repository
    # WebSocket transport (RepoHost) was retired — so it owns the Clones /
    # Website counters the Mirror nodes view shows. Names are public catalog
    # identities, already listed on the repository's Mirrors tab.
    """CREATE TABLE IF NOT EXISTS mirror_serve_counters (
        node_name TEXT NOT NULL,
        owner TEXT NOT NULL,
        repo TEXT NOT NULL,
        clones INTEGER NOT NULL DEFAULT 0,
        website INTEGER NOT NULL DEFAULT 0,
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (node_name, owner, repo))""",
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
    # Per-source-IP account-creation throttle: a rolling window count keyed by the
    # blind index of the signup IP, so a single IP can't mass-create accounts /
    # squat node names. ip_bi is the same one-way index stored on the account.
    "CREATE TABLE IF NOT EXISTS signup_rate (ip_bi TEXT PRIMARY KEY, "
    "count INTEGER NOT NULL DEFAULT 0, window_start_ts INTEGER NOT NULL DEFAULT 0)",
    # Installer link-code rendezvous (adhoc #53): install.sh mints a short code
    # the fresh headless node registers with, and the installing user's desktop
    # app offers the same code signed by its key. Whichever side arrives first
    # parks its half here; the second side completes the link (node.owner =
    # user, user.nodes += node) and deletes the row. code_bi is the blind index
    # of "link:<code>"; data is the encrypted {node|user}; rows expire after
    # LINK_CODE_TTL_MS and are pruned as they are looked up.
    "CREATE TABLE IF NOT EXISTS link_codes (code_bi TEXT PRIMARY KEY, "
    "data TEXT NOT NULL, ts INTEGER NOT NULL)",
    # Email-verification fallback queue: accounts land here only when the
    # configured email provider cannot deliver a verification message, so an
    # administrator can review them. data = encrypted {name, email, joinedAt}.
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
    # Administrator-created private chat channels (migration 0069). Channel
    # names and creator labels live only in encrypted data; name_bi enforces
    # uniqueness without exposing the normalized name in plaintext. Every
    # platform administrator has implicit access, while direct user grants are
    # recorded in chat_channel_members.
    """CREATE TABLE IF NOT EXISTS chat_channels (
        channel_id TEXT PRIMARY KEY,
        name_bi TEXT NOT NULL UNIQUE,
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        key_version INTEGER NOT NULL DEFAULT 1 CHECK (key_version >= 1))""",
    """CREATE TABLE IF NOT EXISTS chat_channel_members (
        channel_id TEXT NOT NULL,
        member_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        invited_by_bi TEXT NOT NULL,
        joined_at INTEGER NOT NULL,
        PRIMARY KEY (channel_id, member_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_chat_channel_members_member "
    "ON chat_channel_members(member_bi, channel_id)",
    # Rotate the shared-key namespace atomically whenever a real membership is
    # removed. An idempotent DELETE that matches no row does not fire it.
    """CREATE TRIGGER IF NOT EXISTS trg_chat_channel_member_remove_rotate
        AFTER DELETE ON chat_channel_members
        BEGIN
          UPDATE chat_channels
             SET key_version = key_version + 1,
                 updated_at = CAST(strftime('%s','now') AS INTEGER) * 1000
           WHERE channel_id = OLD.channel_id;
        END""",
    # One-to-one direct messages (migration 0079). The pair blind index keeps
    # one stable conversation per pair without exposing either username.
    # Participant rows, not administrator status, control access.
    """CREATE TABLE IF NOT EXISTS chat_direct_conversations (
        conversation_id TEXT PRIMARY KEY,
        pair_bi TEXT NOT NULL UNIQUE,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        key_version INTEGER NOT NULL DEFAULT 1 CHECK (key_version >= 1),
        message_count INTEGER NOT NULL DEFAULT 0 CHECK (message_count >= 0))""",
    """CREATE TABLE IF NOT EXISTS chat_direct_participants (
        conversation_id TEXT NOT NULL,
        participant_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        joined_at INTEGER NOT NULL,
        last_read_count INTEGER NOT NULL DEFAULT 0
            CHECK (last_read_count >= 0),
        initiated INTEGER NOT NULL DEFAULT 0 CHECK (initiated IN (0, 1)),
        PRIMARY KEY (conversation_id, participant_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_chat_direct_participants_account "
    "ON chat_direct_participants(participant_bi, conversation_id)",
    "CREATE INDEX IF NOT EXISTS idx_chat_direct_participants_creation_rate "
    "ON chat_direct_participants(participant_bi, initiated, joined_at)",
    """CREATE TRIGGER IF NOT EXISTS trg_chat_direct_creation_rate
        BEFORE INSERT ON chat_direct_participants
        WHEN NEW.initiated = 1 AND (
          SELECT COUNT(*)
          FROM chat_direct_participants
          WHERE participant_bi = NEW.participant_bi
            AND initiated = 1
            AND joined_at > NEW.joined_at - 3600000
        ) >= 20
        BEGIN
          SELECT RAISE(ABORT, 'chat_direct_creation_rate_limited');
        END""",
    # --- Relay federation (main relay only) ---------------------------------
    # Allowlist of relays that federate with this (main) relay. A relay is known
    # by its Ed25519 pubkey; only status='approved' relays may custody signups
    # here or submit node attestations for reward verification. Grant directly:
    #   UPDATE relays SET status='approved' WHERE label='...';
    """CREATE TABLE IF NOT EXISTS relays (
        relay_bi TEXT PRIMARY KEY, pubkey TEXT NOT NULL, label TEXT,
        base_url TEXT, status TEXT NOT NULL DEFAULT 'pending',
        registered_at INTEGER, approved_at INTEGER)""",
    # Reward candidates reported by approved federated relays. A wallet and
    # name alone are never sufficient: every live row carries the original
    # node-signed registration + repository-health proof, relay-observed
    # integrity facts, and main-relay observation history. Migration 0057
    # discards the historical wallet-only rows before adding these columns.
    """CREATE TABLE IF NOT EXISTS federated_presence (
        relay_bi TEXT NOT NULL, wallet TEXT NOT NULL, name TEXT,
        operator_id TEXT NOT NULL DEFAULT '',
        device_id TEXT NOT NULL DEFAULT '',
        public_key TEXT NOT NULL DEFAULT '',
        base_url TEXT NOT NULL DEFAULT '',
        registration_sig TEXT NOT NULL DEFAULT '',
        registration_issued_at INTEGER NOT NULL DEFAULT 0,
        health_message TEXT NOT NULL DEFAULT '',
        health_sig TEXT NOT NULL DEFAULT '',
        checked_at INTEGER NOT NULL DEFAULT 0,
        healthy INTEGER NOT NULL DEFAULT 0,
        integrity TEXT NOT NULL DEFAULT 'unknown',
        forkmesh_active INTEGER NOT NULL DEFAULT 0,
        forkmesh_verified_at INTEGER NOT NULL DEFAULT 0,
        forkmesh_refs_sha256 TEXT NOT NULL DEFAULT '',
        forkmesh_operations_sha256 TEXT NOT NULL DEFAULT '',
        abuse_blocked INTEGER NOT NULL DEFAULT 0,
        attestation_id TEXT NOT NULL DEFAULT '',
        first_verified_at INTEGER NOT NULL DEFAULT 0,
        last_verified_at INTEGER NOT NULL DEFAULT 0,
        consecutive_checks INTEGER NOT NULL DEFAULT 0,
        ts INTEGER NOT NULL,
        PRIMARY KEY (relay_bi, wallet))""",
    "CREATE INDEX IF NOT EXISTS idx_federated_presence_ts ON federated_presence(ts)",
    # Historical federated-signup deposit rows. Creation and sweep routes are
    # disabled; existing public status remains readable while any encrypted key
    # awaits explicit offline migration.
    """CREATE TABLE IF NOT EXISTS federated_signup (
        reference TEXT PRIMARY KEY, relay_bi TEXT NOT NULL, data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_federated_signup_relay ON federated_signup(relay_bi)",
    # This relay's own federation identity (federated relays only): a single
    # encrypted row holding the auto-generated Ed25519 keypair used to sign
    # relay->main calls. id is always 1.
    "CREATE TABLE IF NOT EXISTS relay_self (id INTEGER PRIMARY KEY, data TEXT NOT NULL)",
    # Historical central-fund row retained for schema compatibility and offline
    # custody migration. The active pool address comes from public configuration;
    # the Worker never reads this row for signing or distribution.
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
    # address of the asset. ua is the raw User-Agent header, shown as-is in the
    # admin list so operators can spot scripted/bot download traffic. No
    # account/IP is recorded.
    """CREATE TABLE IF NOT EXISTS release_downloads (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        sha256 TEXT NOT NULL, ts INTEGER NOT NULL, ua TEXT)""",
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
    # Daily automated scan artifacts. repo_bi is the keyed blind index of
    # "<owner>/<repo>"; the strict, already-redacted rich + clipboard envelope
    # is still AES-GCM encrypted in data so private repo names, paths and
    # findings do not become plaintext D1 metadata. The ingest handler retains
    # only the newest fixed number of rows per repository.
    """CREATE TABLE IF NOT EXISTS repo_security_scans (
        repo_bi TEXT NOT NULL,
        scan_id TEXT NOT NULL,
        scanned_at INTEGER NOT NULL,
        received_at INTEGER NOT NULL,
        data TEXT NOT NULL,
        PRIMARY KEY (repo_bi, scan_id))""",
    "CREATE INDEX IF NOT EXISTS idx_repo_security_scans_latest "
    "ON repo_security_scans(repo_bi, scanned_at DESC, received_at DESC)",
    # Owner/security-reviewer false-positive triage is an encrypted overlay,
    # leaving the scanner's immutable content-addressed artifact untouched.
    """CREATE TABLE IF NOT EXISTS repo_security_scan_reviews (
        repo_bi TEXT NOT NULL,
        scan_id TEXT NOT NULL,
        finding_id TEXT NOT NULL,
        reviewer_bi TEXT NOT NULL,
        reviewed_at INTEGER NOT NULL,
        data TEXT NOT NULL,
        PRIMARY KEY (repo_bi, scan_id, finding_id))""",
    "CREATE INDEX IF NOT EXISTS idx_repo_security_scan_reviews_scan "
    "ON repo_security_scan_reviews(repo_bi, scan_id, reviewed_at DESC)",
    # Short-lived native mirror-node scan leases. Repository identity and lease
    # capability are keyed blind indexes; commit/runner metadata stays inside
    # the encrypted data blob. A completed row is retained as the per-repository
    # daily idempotency marker and never contains a reusable bearer token.
    """CREATE TABLE IF NOT EXISTS repo_security_scan_leases (
        repo_bi TEXT PRIMARY KEY,
        lease_token_bi TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL,
        acquired_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        next_attempt_at INTEGER NOT NULL DEFAULT 0,
        attempt_count INTEGER NOT NULL DEFAULT 0,
        completed_scan_id TEXT NOT NULL DEFAULT '',
        updated_at INTEGER NOT NULL,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_repo_security_scan_leases_due "
    "ON repo_security_scan_leases(status, expires_at, next_attempt_at)",
    # Anonymous website feedback. One row per like/dislike click from static
    # pages. `ip_hash` is a keyed blind index of the request IP when present,
    # never the raw address; `user_agent` is a legacy column name containing
    # only a broad client category. Bounded by MAX_FEEDBACK in feedback_handler.
    """CREATE TABLE IF NOT EXISTS feedback (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ts INTEGER NOT NULL,
        source TEXT NOT NULL,
        vote TEXT NOT NULL,
        path TEXT NOT NULL,
        message TEXT,
        ip_hash TEXT,
        user_agent TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_feedback_ts ON feedback(ts)",
    "CREATE INDEX IF NOT EXISTS idx_feedback_source_vote ON feedback(source, vote, ts)",
    """CREATE TABLE IF NOT EXISTS profile_follows (
        follower_bi TEXT NOT NULL,
        target_bi TEXT NOT NULL,
        follower_name TEXT NOT NULL,
        target_name TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (follower_bi, target_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_profile_follows_target "
    "ON profile_follows(target_bi, created_at)",
    "CREATE INDEX IF NOT EXISTS idx_profile_follows_follower "
    "ON profile_follows(follower_bi, created_at)",
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
    # Per-minute detail backing the /status page's 60-minute strip. Unlike the
    # daily/hourly rollups above (which only ever store a checks/failures
    # counter, never the individual samples), a minute IS the individual
    # sample, so this is one row per system per minute: ok=1/0 plus the same
    # short failure reason. Short-retention (pruned well under an hour of
    # slack past what the page displays) since nothing else needs history
    # this granular once it has aged out of the visible window.
    """CREATE TABLE IF NOT EXISTS system_status_minute (
        minute_ts INTEGER NOT NULL, system TEXT NOT NULL,
        ok INTEGER NOT NULL DEFAULT 1, reason TEXT,
        PRIMARY KEY (minute_ts, system))""",
    "CREATE INDEX IF NOT EXISTS idx_system_status_minute_ts ON system_status_minute(minute_ts)",
    # At-most-once ownership of each minute's status sample. Two independent
    # schedulers may call record_status_sample for the same minute — the
    # platform Cron Trigger directly (so /status keeps its samples even while
    # Durable Objects are failing) and the ForkMeshCronRunner alarm batch.
    # The daily/hourly rollups are checks-counter increments, so whichever
    # caller INSERTs this minute's row first owns the sample; the loser skips
    # it instead of double-counting the hour. `claim` is a random token the
    # winner reads back to recognize itself (D1's Python client exposes no
    # reliable changes() count). Pruned alongside system_status_minute.
    """CREATE TABLE IF NOT EXISTS system_status_sample_claim (
        minute_ts INTEGER PRIMARY KEY, claim TEXT NOT NULL,
        claimed_at INTEGER NOT NULL)""",
    # Edge repository-render monitor state. One row is enough to deduplicate
    # outage/recovery mail while the normal status tables retain the public
    # minute/hour/day history.
    """CREATE TABLE IF NOT EXISTS repository_monitor_state (
        monitor_id TEXT PRIMARY KEY,
        is_up INTEGER NOT NULL DEFAULT 1,
        changed_at INTEGER NOT NULL,
        outage_started_at INTEGER NOT NULL DEFAULT 0,
        checked_at INTEGER NOT NULL,
        reason TEXT,
        notified_state TEXT NOT NULL DEFAULT '')""",
    # Founders-outreach team: accounts an admin has authorized to send email
    # from the shared founders address via /outreach. `name` is the public
    # account name in plaintext (like users.username) so the roster is listable
    # without decryption; name_bi is the usual blind index of it.
    """CREATE TABLE IF NOT EXISTS outreach_team (
        name_bi TEXT PRIMARY KEY, name TEXT NOT NULL,
        added_by TEXT, added_at INTEGER NOT NULL)""",
    # Audit log of outreach sends. `sender` (public account name) and ts stay
    # plaintext so the per-sender daily cap is one COUNT query; the recipient
    # address + subject are PII, so they live only in the encrypted blob
    # ({sender, to, subject, template, ok}).
    """CREATE TABLE IF NOT EXISTS outreach_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,
        sender TEXT NOT NULL, ok INTEGER NOT NULL DEFAULT 1,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_outreach_log_sender_ts ON outreach_log(sender, ts)",
    # Per-repo issue-number allocator for issues the relay files on a user's
    # behalf (ForkBot). Desktop nodes own the real numbering (nextNumber() =
    # highest issue dir + 1), so a relay-created issue can only be given a
    # *proposed* number the desktop honors when the slot is free. Persisting
    # the allocation here lets ForkBot answer with the number immediately and
    # keeps successive ForkBot issues from proposing the same one; it is
    # re-anchored to each catalog publish's issueMaxNumber so it tracks the
    # desktop's real count over time.
    """CREATE TABLE IF NOT EXISTS issue_seq (
        repo_bi TEXT PRIMARY KEY, next_number INTEGER NOT NULL)""",
    # --- ActivityPub federation (migration 0028) -----------------------------
    # Local fediverse actors: one row per user/repo/instance actor that has
    # actually been looked up from the fediverse (rows are minted lazily on the
    # first WebFinger/actor fetch, so repos nobody follows cost nothing).
    # actor_bi = blind_index("ap-actor:<kind>:<handle>"); handle is "alice" for
    # users, "owner.repo" for repos, "instance" for the service actor. The
    # encrypted data blob holds the RSA-2048 private key (PKCS8); pubkey_pem is
    # plaintext because the actor document publishes it anyway.
    """CREATE TABLE IF NOT EXISTS ap_actors (
        actor_bi TEXT PRIMARY KEY, kind TEXT NOT NULL,
        pubkey_pem TEXT NOT NULL, data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    # The relay's secure-fetch signing key is infrastructure state, not a
    # followable repository actor. Keeping it separate ensures ap_actors is an
    # inventory of official repository actors only.
    """CREATE TABLE IF NOT EXISTS ap_service_keys (
        key_name TEXT PRIMARY KEY, pubkey_pem TEXT NOT NULL,
        data TEXT NOT NULL, created_at INTEGER NOT NULL)""",
    # Remote accounts following a local actor. follower_id/inbox URLs stay
    # plaintext: they are public fediverse identifiers needed for cron
    # delivery fan-out without decrypting every row.
    """CREATE TABLE IF NOT EXISTS ap_followers (
        actor_bi TEXT NOT NULL, follower_id TEXT NOT NULL,
        inbox TEXT NOT NULL, shared_inbox TEXT, follower_handle TEXT,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (actor_bi, follower_id))""",
    "CREATE INDEX IF NOT EXISTS idx_ap_followers_actor ON ap_followers(actor_bi)",
    # Cache of remote actor documents (public data, plaintext like `relays`).
    # avatar_url/summary are the follower's published avatar and bio (migration
    # 0078) — the same public presentation any fediverse client shows, cached so
    # "who follows this repo" can be rendered without re-fetching every actor.
    """CREATE TABLE IF NOT EXISTS ap_remote_actors (
        actor_id TEXT PRIMARY KEY, inbox TEXT, shared_inbox TEXT,
        pubkey_pem TEXT, handle TEXT, display_name TEXT, url TEXT,
        updated_at INTEGER NOT NULL, avatar_url TEXT, summary TEXT)""",
    # Local ActivityPub objects (the Notes we publish), served at /ap/o/<uuid>.
    # context_bi = blind_index("ap-context:<owner>/<repo>#<kind>#<ref>") maps a
    # remote reply's inReplyTo back to its forkmesh thread.
    """CREATE TABLE IF NOT EXISTS ap_objects (
        object_uuid TEXT PRIMARY KEY, actor_bi TEXT NOT NULL,
        context_bi TEXT, data TEXT NOT NULL, published INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_ap_objects_actor ON ap_objects(actor_bi)",
    "CREATE INDEX IF NOT EXISTS idx_ap_objects_context ON ap_objects(context_bi)",
    # Inbound remote replies, kept OUTSIDE the Ed25519-signed event log (they
    # cannot carry forkmesh author signatures) and surfaced to clients as
    # clearly-marked fediverse comments. remote_id_bi dedupes redeliveries;
    # parent_remote_id_bi supports Lemmy-style nesting without exposing the
    # remote URL, and lifecycle preserves edits/tombstones/moderation.
    """CREATE TABLE IF NOT EXISTS ap_comments (
        id INTEGER PRIMARY KEY AUTOINCREMENT, context_bi TEXT NOT NULL,
        remote_id_bi TEXT UNIQUE, parent_remote_id_bi TEXT,
        lifecycle TEXT NOT NULL DEFAULT 'active'
            CHECK (lifecycle IN (
                'active', 'edited', 'tombstoned', 'moderated',
                'awaiting-redelivery'
            )),
        data TEXT NOT NULL, ts INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_ap_comments_context ON ap_comments(context_bi, ts)",
    "CREATE INDEX IF NOT EXISTS idx_ap_comments_parent "
    "ON ap_comments(parent_remote_id_bi)",
    # Inbound repo-actor mentions already processed into an issue-inbox
    # submission (or confidently classified as not-an-issue), keyed by
    # blind_index("ap-mention:<note id>") — dedupes fediverse redeliveries so
    # one Mastodon post can never file the same issue twice (migration 0037).
    """CREATE TABLE IF NOT EXISTS ap_mentions (
        remote_id_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)""",
    # Verified public fediverse feedback shown in World. Remote actor/content,
    # repository names, consent, and errors stay in the encrypted data
    # envelope. A mention is never an issue until an authorized reviewer queues
    # it and the owner node subsequently confirms the materialized issue number
    # (migration 0066).
    """CREATE TABLE IF NOT EXISTS world_fediverse_mentions (
        mention_id TEXT PRIMARY KEY, remote_id_bi TEXT NOT NULL UNIQUE,
        repo_bi TEXT NOT NULL, kind TEXT NOT NULL,
        state TEXT NOT NULL, data TEXT NOT NULL,
        issue_number INTEGER NOT NULL DEFAULT 0,
        create_lease_id TEXT NOT NULL DEFAULT '',
        create_lease_until INTEGER NOT NULL DEFAULT 0,
        followup_state TEXT NOT NULL DEFAULT 'not-requested',
        created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_world_fediverse_mentions_feed "
    "ON world_fediverse_mentions(state, created_at DESC)",
    "CREATE INDEX IF NOT EXISTS idx_world_fediverse_mentions_repo "
    "ON world_fediverse_mentions(repo_bi, created_at DESC)",
    """CREATE TABLE IF NOT EXISTS world_fediverse_mention_moderation (
        record_id TEXT PRIMARY KEY, mention_id TEXT NOT NULL,
        action TEXT NOT NULL, reason TEXT NOT NULL,
        actor_bi TEXT NOT NULL, created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_world_fediverse_mention_moderation "
    "ON world_fediverse_mention_moderation(mention_id, created_at)",
    # Outbound delivery queue (no Cloudflare Queues on the free plan): one row
    # per (activity, destination inbox). A publish inserts rows and best-effort
    # drains a few inline; the cron drains the rest with capped backoff.
    """CREATE TABLE IF NOT EXISTS ap_outbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, inbox TEXT NOT NULL,
        data TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0,
        next_ts INTEGER NOT NULL, created_at INTEGER NOT NULL,
        dedupe_bi TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_ap_outbox_next ON ap_outbox(next_ts)",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_ap_outbox_dedupe "
    "ON ap_outbox(dedupe_bi)",
    # Fediverse operator configuration (admin-managed, migration 0029): k/v
    # settings (currently just enabled=1/0, default on when absent) and the
    # remote-domain blocklist (defederation). Plaintext operational config.
    """CREATE TABLE IF NOT EXISTS ap_settings (
        k TEXT PRIMARY KEY, v TEXT NOT NULL)""",
    """CREATE TABLE IF NOT EXISTS ap_blocked_domains (
        domain TEXT PRIMARY KEY, added_by TEXT, added_at INTEGER NOT NULL)""",
    # Per-repo branding (migration 0030): owner-uploaded logo/banner PNGs shown
    # on the repo's fediverse actor (and anywhere else that wants them).
    # repo_bi = blind_index("owner/repo") (== repositories.key_bi); kind is
    # 'logo' or 'banner'; data = encrypted {png: <base64>}. One row per image
    # so a large banner never pushes the combined row past D1's 2 MB cap, and
    # existence checks (SELECT kind, updated_at) never decrypt the blob.
    """CREATE TABLE IF NOT EXISTS repo_media (
        repo_bi TEXT NOT NULL, kind TEXT NOT NULL,
        data TEXT NOT NULL, updated_at INTEGER NOT NULL,
        PRIMARY KEY (repo_bi, kind))""",
    # Web edits of the repo About card queued for the owner's desktop node
    # (migration 0031): the node drains this via GET /api/sync (same
    # drain-on-read semantics as agent_prompts) and writes the change into the
    # repo's committed .forkmesh/info.json — the single source of truth the
    # desktop app shows. One row per repo; the latest web edit wins.
    """CREATE TABLE IF NOT EXISTS about_inbox (
        repo_bi TEXT PRIMARY KEY, data TEXT NOT NULL,
        queued_at INTEGER NOT NULL)""",
    # Repositories deleted from the website's repo Settings tab (adhoc #91).
    # The owner's node keeps its local mirror and republishes the catalog
    # record on every heartbeat, so a web delete used to reappear within a
    # minute and looked like it had silently failed. This tombstone makes the
    # deletion stick: automatic publishes for the repo are refused with HTTP
    # 410 until it expires, and an explicit user-initiated publish from the
    # desktop ("publishIntent":"user", owner-signed like any other publish)
    # clears the row so the repo can be shared again.
    """CREATE TABLE IF NOT EXISTS repo_deletions (
        repo_bi TEXT PRIMARY KEY, owner_bi TEXT NOT NULL,
        deleted_at INTEGER NOT NULL, expires_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_repo_deletions_expires "
    "ON repo_deletions(expires_at)",
    # Repo stars (migration 0032): which accounts starred which repo. Keyed by
    # blind indexes only (no signing needed - a star is a plain per-account
    # preference, same trust level as profile_follows), so the count and the
    # caller's own starred state are cheap membership checks.
    """CREATE TABLE IF NOT EXISTS repo_stars (
        repo_bi TEXT NOT NULL, account_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (repo_bi, account_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_repo_stars_repo ON repo_stars(repo_bi, created_at)",
    "CREATE INDEX IF NOT EXISTS idx_repo_stars_account ON repo_stars(account_bi, created_at)",
    # "How are we doing?" founder feedback email, sent once per user account
    # ~24h after signup (migration 0033). One row per send is both the
    # once-only guard and the admin's send log (the admin table browser lists
    # every D1 table): `name` is the public node name, nothing else — the
    # recipient email stays only in the encrypted account record.
    """CREATE TABLE IF NOT EXISTS feedback_email_sends (
        account_bi TEXT PRIMARY KEY, name TEXT, sent_at INTEGER NOT NULL)""",
    # Per-repo fediverse switches (migration 0035), owner-managed from the web
    # and Qt repo settings: federate (actor exists at all), broadcastEvents
    # (issues/PRs/releases posted to followers), acceptComments (remote
    # replies ingested as federated comments). repo_bi =
    # blind_index("ap-repo-settings:<owner>/<repo>") with both halves
    # lowercased; data is plaintext JSON of booleans (same operational-config
    # trust level as ap_settings) so the hot unauthenticated AP gates never
    # decrypt. A missing row means "all on" — existing repos keep federating
    # exactly as before.
    """CREATE TABLE IF NOT EXISTS ap_repo_settings (
        repo_bi TEXT PRIMARY KEY, data TEXT NOT NULL,
        updated_at INTEGER NOT NULL)""",
    # Per-repo operational-alert switches (migration 0086), managed by the
    # org admin from the same repo About/settings form as the fediverse
    # switches: statusEmails turns the "[ForkMesh outage]" /
    # "[ForkMesh recovered]" status and cron-watchdog mail on. repo_bi =
    # blind_index("repo-alert-settings:<owner>/<repo>") with both halves
    # lowercased; data is plaintext JSON of booleans (same operational-config
    # trust level as ap_repo_settings). A missing row means "all off" — a
    # deployment mails nobody until an admin opts in.
    """CREATE TABLE IF NOT EXISTS repo_alert_settings (
        repo_bi TEXT PRIMARY KEY, data TEXT NOT NULL,
        updated_at INTEGER NOT NULL)""",
    # Encrypted, bounded automatic-update queues (migration 0046). Human
    # repository/org names and event metadata live only inside `data`;
    # scope_bi is a blind index. next_ts/last_published_at are operational
    # timestamps used by the bounded cron query. Private repositories never
    # receive a row.
    """CREATE TABLE IF NOT EXISTS ap_digest_queues (
        scope_bi TEXT PRIMARY KEY, next_ts INTEGER NOT NULL,
        last_published_at INTEGER NOT NULL DEFAULT 0,
        data TEXT NOT NULL, updated_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_ap_digest_queues_due "
    "ON ap_digest_queues(next_ts, updated_at)",
    # Org owners/admins may independently disable the org-alias digest for
    # linked repositories. The backing repo owner's setting remains an AND
    # gate, so organization administration never grants publication rights.
    """CREATE TABLE IF NOT EXISTS ap_org_digest_settings (
        org_bi TEXT PRIMARY KEY, data TEXT NOT NULL,
        updated_at INTEGER NOT NULL)""",
    # Bounded contribution projections for native profile activity. Every
    # identity and repository join key is a blind index; human-readable labels
    # remain in encrypted data, and raw Git author names/emails are never stored.
    """CREATE TABLE IF NOT EXISTS profile_contribution_receipts (
        generation_bi TEXT PRIMARY KEY,
        snapshot_hash TEXT NOT NULL,
        source_account_bi TEXT NOT NULL,
        source_repo_bi TEXT NOT NULL,
        captured_at INTEGER NOT NULL,
        head TEXT NOT NULL,
        day_rows INTEGER NOT NULL,
        language_rows INTEGER NOT NULL,
        created_at INTEGER NOT NULL)""",
    """CREATE TABLE IF NOT EXISTS profile_contribution_projects (
        source_repo_bi TEXT PRIMARY KEY,
        source_account_bi TEXT NOT NULL,
        owner_user_bi TEXT NOT NULL,
        project_bi TEXT NOT NULL,
        first_public_day TEXT NOT NULL,
        is_public INTEGER NOT NULL DEFAULT 1,
        active_generation_bi TEXT,
        captured_at INTEGER NOT NULL DEFAULT 0,
        verified_from TEXT,
        data TEXT NOT NULL)""",
    """CREATE TABLE IF NOT EXISTS profile_contribution_days (
        generation_bi TEXT NOT NULL,
        subject_user_bi TEXT NOT NULL,
        source_account_bi TEXT NOT NULL,
        source_repo_bi TEXT NOT NULL,
        project_bi TEXT NOT NULL,
        day TEXT NOT NULL,
        commits INTEGER NOT NULL DEFAULT 0,
        issues INTEGER NOT NULL DEFAULT 0,
        pulls INTEGER NOT NULL DEFAULT 0,
        reviews INTEGER NOT NULL DEFAULT 0,
        captured_at INTEGER NOT NULL,
        data TEXT NOT NULL,
        PRIMARY KEY (generation_bi, subject_user_bi, source_repo_bi, day))""",
    """CREATE TABLE IF NOT EXISTS profile_contribution_languages (
        generation_bi TEXT NOT NULL,
        owner_user_bi TEXT NOT NULL,
        source_repo_bi TEXT NOT NULL,
        project_bi TEXT NOT NULL,
        language TEXT NOT NULL,
        bytes INTEGER NOT NULL,
        files INTEGER NOT NULL,
        captured_at INTEGER NOT NULL,
        PRIMARY KEY (generation_bi, owner_user_bi, source_repo_bi, language))""",
    """CREATE INDEX IF NOT EXISTS idx_profile_contribution_projects_owner_public
        ON profile_contribution_projects(owner_user_bi, is_public)""",
    """CREATE INDEX IF NOT EXISTS idx_profile_contribution_projects_project_public
        ON profile_contribution_projects(project_bi, is_public)""",
    """CREATE INDEX IF NOT EXISTS idx_profile_contribution_days_subject_day
        ON profile_contribution_days(subject_user_bi, day)""",
    """CREATE INDEX IF NOT EXISTS idx_profile_contribution_days_repo_generation
        ON profile_contribution_days(source_repo_bi, generation_bi)""",
    """CREATE INDEX IF NOT EXISTS idx_profile_contribution_languages_owner_generation
        ON profile_contribution_languages(owner_user_bi, generation_bi)""",
    # --- Organizations + teams (issue #388, migration 0038) ------------------
    # User-created org namespaces that serve linked repos at /<org>/<repo>
    # instead of the hosting node's name. Org identity and membership rosters
    # are public data (same trust level as users.username / profile_follows /
    # outreach_team), so names and roles stay plaintext for cheap listing;
    # per-org extras (display name, description, creator) live in the encrypted
    # data blob. org_bi = blind_index("org:<name>").
    """CREATE TABLE IF NOT EXISTS orgs (
        org_bi TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    # Who belongs to an org and at what role ('owner' | 'admin' | 'member').
    """CREATE TABLE IF NOT EXISTS org_members (
        org_bi TEXT NOT NULL, member_bi TEXT NOT NULL,
        role TEXT NOT NULL DEFAULT 'member',
        name TEXT NOT NULL, created_at INTEGER NOT NULL,
        PRIMARY KEY (org_bi, member_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_org_members_member ON org_members(member_bi)",
    # Teams: a named subset of org members holding one repository permission
    # ('read' | 'write' | 'maintain' | 'admin') over the org's linked repos.
    """CREATE TABLE IF NOT EXISTS org_teams (
        org_bi TEXT NOT NULL, team TEXT NOT NULL,
        permission TEXT NOT NULL DEFAULT 'read',
        created_at INTEGER NOT NULL,
        PRIMARY KEY (org_bi, team))""",
    """CREATE TABLE IF NOT EXISTS org_team_members (
        org_bi TEXT NOT NULL, team TEXT NOT NULL, member_bi TEXT NOT NULL,
        name TEXT NOT NULL, created_at INTEGER NOT NULL,
        PRIMARY KEY (org_bi, team, member_bi))""",
    # Alias map: which node-hosted repo each /<org>/<repo> URL serves. The
    # reverse index lets the push gate find the orgs a node repo is linked
    # under without scanning.
    """CREATE TABLE IF NOT EXISTS org_repos (
        org_bi TEXT NOT NULL, repo TEXT NOT NULL, node_owner TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (org_bi, repo))""",
    "CREATE INDEX IF NOT EXISTS idx_org_repos_node ON org_repos(node_owner, repo)",
    # Organization-scoped coding bots are intentionally separate from the
    # owner-only repo_agents/agent_prompts E2EE channel. Members see only
    # sessions for an org they currently belong to; every prompt is encrypted
    # at rest and remains pending until the selected mirror reports an exact
    # tool-free Haiku safety verdict.
    """CREATE TABLE IF NOT EXISTS org_agent_sessions (
        session_id TEXT PRIMARY KEY,
        org_bi TEXT NOT NULL,
        repo TEXT NOT NULL,
        target_node TEXT NOT NULL,
        provider TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'security_pending',
        created_by_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        completed_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_org_agent_sessions_scope "
    "ON org_agent_sessions(org_bi, repo, updated_at DESC)",
    """CREATE TABLE IF NOT EXISTS org_agent_jobs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id TEXT NOT NULL,
        org_bi TEXT NOT NULL,
        target_node TEXT NOT NULL,
        repo TEXT NOT NULL,
        provider TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'queued',
        lease_id TEXT NOT NULL DEFAULT '',
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_org_agent_jobs_drain "
    "ON org_agent_jobs(target_node, repo, status, id)",
    # A user may explicitly opt into the Town Square's recently-inactive
    # seating area. The account key is a blind index and the generalized public
    # card is encrypted; exact paths, activity history, IP data, and location
    # are never stored. Rows expire automatically after a bounded window.
    """CREATE TABLE IF NOT EXISTS world_inactive_presence (
        account_bi TEXT PRIMARY KEY,
        data TEXT NOT NULL,
        updated_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_world_inactive_presence_expiry "
    "ON world_inactive_presence(expires_at)",
    # Server-authoritative aggregate time spent in the ForkMesh World
    # (migration 0076). The account key is a blind index; no display name, IP,
    # route, movement history, or client-reported duration is stored. The
    # generation makes a signed ticket continuation single-use even when
    # refreshes race across tabs.
    """CREATE TABLE IF NOT EXISTS world_user_activity (
        account_bi TEXT PRIMARY KEY,
        total_active_ms INTEGER NOT NULL DEFAULT 0
            CHECK (total_active_ms >= 0),
        last_touch_at INTEGER NOT NULL DEFAULT 0
            CHECK (last_touch_at >= 0),
        generation INTEGER NOT NULL DEFAULT 1
            CHECK (generation > 0),
        last_credit_ms INTEGER NOT NULL DEFAULT 0
            CHECK (last_credit_ms >= 0),
        updated_at INTEGER NOT NULL DEFAULT 0
            CHECK (updated_at >= 0))""",
    # Privacy-preserving approximate unique counts for the Arrival Grid
    # (migration 0077). The edge transiently HMACs its observed source address
    # plus a coarse browser/OS/device category, then stores only HyperLogLog
    # register/rank projections. Temporal HMAC context rotates at UTC midnight
    # and is domain-separated from the all-time context. No raw address, raw
    # User-Agent, account, digest, or movement history enters D1. A
    # 1,024-register all-time sketch plus at most 50 hours of 10-minute
    # sketches fixes the maximum footprint.
    """CREATE TABLE IF NOT EXISTS world_visit_unique_hll (
        bucket_start INTEGER NOT NULL,
        register_id INTEGER NOT NULL
            CHECK (register_id >= 0 AND register_id < 1024),
        rank INTEGER NOT NULL CHECK (rank >= 1 AND rank <= 247),
        PRIMARY KEY (bucket_start, register_id)
    ) WITHOUT ROWID""",
    # Content-free relay accounting behind the System Capacity platform
    # (migration 0083). One row per Durable Object binding: how many bytes and
    # frames that class has relayed. No room key, account, peer id, instance
    # id, or message content is stored, and each Durable Object folds its
    # in-memory counters in at most once a minute.
    """CREATE TABLE IF NOT EXISTS durable_object_traffic (
        binding TEXT PRIMARY KEY,
        bytes_in INTEGER NOT NULL DEFAULT 0 CHECK (bytes_in >= 0),
        bytes_out INTEGER NOT NULL DEFAULT 0 CHECK (bytes_out >= 0),
        messages INTEGER NOT NULL DEFAULT 0 CHECK (messages >= 0),
        updated_at INTEGER NOT NULL DEFAULT 0 CHECK (updated_at >= 0))""",
    # Minute aggregates for platform-aborted Durable Object requests
    # (migration 0116). A reconnect storm increments one content-free row
    # instead of writing one error_log row per affected user. /status consumes
    # these counters, preserving incident visibility without flooding the
    # operator's actionable Worker-error queue.
    """CREATE TABLE IF NOT EXISTS durable_object_abort_minute (
        minute_ts INTEGER PRIMARY KEY CHECK (minute_ts >= 0),
        aborts INTEGER NOT NULL DEFAULT 0 CHECK (aborts >= 0),
        duration_aborts INTEGER NOT NULL DEFAULT 0
            CHECK (duration_aborts >= 0),
        updated_at INTEGER NOT NULL DEFAULT 0 CHECK (updated_at >= 0))""",
    # Aggregate-only Town Square arrival odometer for the Arrival Grid plaque
    # (migration 0074). Each accepted world join adds one to a coarse
    # 10-minute UTC bucket; rows never carry a visitor id, country, IP, or
    # session field, so the plaque totals stay outside the world's no-history
    # privacy contract. Buckets older than two days fold into the -1 archive
    # row, keeping the table bounded.
    """CREATE TABLE IF NOT EXISTS world_visit_stats (
        bucket_start INTEGER PRIMARY KEY,
        visits INTEGER NOT NULL DEFAULT 0)""",
    # Public-only, consent-attested social directory (migrations 0052/0061).
    # `data` is not encrypted because the validator permits only fields that
    # are intentionally public; account/follower/private activity data has no
    # storage field here.
    """CREATE TABLE IF NOT EXISTS world_fediverse_instances (
        instance_id TEXT PRIMARY KEY,
        kind TEXT NOT NULL CHECK (
            kind IN ('mastodon', 'lemmy', 'x', 'reddit')),
        host TEXT NOT NULL UNIQUE,
        url TEXT NOT NULL UNIQUE,
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_world_fediverse_kind_updated "
    "ON world_fediverse_instances(kind, updated_at)",
    # Shared-media rooms coordinate bounded external HTTPS provider links.
    # ForkMesh does not store, proxy, autoplay, record, or rebroadcast media.
    """CREATE TABLE IF NOT EXISTS world_media_spaces (
        space_id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        description TEXT NOT NULL DEFAULT '',
        session_type TEXT NOT NULL CHECK (session_type IN (
            'listening-room', 'dj-session', 'video-room', 'watch-party',
            'repository-launch', 'organization-presentation')),
        owner_bi TEXT NOT NULL,
        owner_label TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'active'
            CHECK (status IN ('active', 'archived')),
        playback_state TEXT NOT NULL DEFAULT 'idle'
            CHECK (playback_state IN ('idle', 'stopped')),
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        last_activity_at INTEGER NOT NULL,
        archived_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_world_media_spaces_owner "
    "ON world_media_spaces(owner_bi, status, updated_at)",
    "CREATE INDEX IF NOT EXISTS idx_world_media_spaces_active "
    "ON world_media_spaces(status, last_activity_at)",
    # One server UTC clock coordinates opt-in provider playback. It contains no
    # media bytes and cannot authorize autoplay or rebroadcasting.
    """CREATE TABLE IF NOT EXISTS world_media_playback (
        space_id TEXT PRIMARY KEY,
        item_id TEXT NOT NULL DEFAULT '',
        state TEXT NOT NULL DEFAULT 'idle'
            CHECK (state IN ('idle', 'playing', 'paused', 'stopped')),
        position_ms INTEGER NOT NULL DEFAULT 0
            CHECK (position_ms >= 0 AND position_ms <= 604800000),
        started_at INTEGER NOT NULL DEFAULT 0,
        changed_at INTEGER NOT NULL,
        changed_by_bi TEXT NOT NULL DEFAULT '',
        revision INTEGER NOT NULL DEFAULT 0 CHECK (revision >= 0),
        change_id TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_world_media_playback_changed "
    "ON world_media_playback(changed_at)",
    # Ownership comes from spaces.owner_bi; only explicit moderator grants are
    # rows. Platform roles do not silently inherit room control.
    """CREATE TABLE IF NOT EXISTS world_media_roles (
        role_id TEXT PRIMARY KEY,
        space_id TEXT NOT NULL,
        account_bi TEXT NOT NULL,
        account_label TEXT NOT NULL,
        role TEXT NOT NULL CHECK (role = 'moderator'),
        granted_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        UNIQUE (space_id, account_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_world_media_roles_account "
    "ON world_media_roles(account_bi, space_id)",
    """CREATE TABLE IF NOT EXISTS world_media_items (
        item_id TEXT PRIMARY KEY,
        space_id TEXT NOT NULL,
        position INTEGER NOT NULL,
        status TEXT NOT NULL DEFAULT 'queued'
            CHECK (status IN ('queued', 'stopped', 'removed')),
        data TEXT NOT NULL,
        added_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        removed_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_world_media_items_space "
    "ON world_media_items(space_id, status, position, created_at)",
    "CREATE INDEX IF NOT EXISTS idx_world_media_items_retention "
    "ON world_media_items(status, removed_at)",
    """CREATE TABLE IF NOT EXISTS world_media_schedules (
        schedule_id TEXT PRIMARY KEY,
        space_id TEXT NOT NULL,
        item_id TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL DEFAULT 'scheduled'
            CHECK (status IN ('scheduled', 'cancelled', 'completed')),
        starts_at INTEGER NOT NULL,
        ends_at INTEGER NOT NULL,
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        cancelled_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_world_media_schedules_space "
    "ON world_media_schedules(space_id, status, starts_at)",
    "CREATE INDEX IF NOT EXISTS idx_world_media_schedules_retention "
    "ON world_media_schedules(status, ends_at, cancelled_at)",
    # Collaborative Code Workshops (migration 0055). Repository names,
    # participant labels, reports, and event bodies live only in encrypted
    # `data`; repo_bi is the keyed exact-match scope.
    """CREATE TABLE IF NOT EXISTS world_workshop_sessions (
        session_id TEXT PRIMARY KEY,
        repo_bi TEXT NOT NULL,
        commit_hash TEXT NOT NULL,
        run_id TEXT NOT NULL,
        workshop_type TEXT NOT NULL,
        owner_bi TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'active'
            CHECK (status IN ('active', 'completed', 'archived')),
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        UNIQUE (repo_bi, commit_hash, run_id))""",
    "CREATE INDEX IF NOT EXISTS idx_world_workshop_sessions_scope "
    "ON world_workshop_sessions(repo_bi, commit_hash, updated_at)",
    "CREATE INDEX IF NOT EXISTS idx_world_workshop_sessions_owner "
    "ON world_workshop_sessions(owner_bi, status, updated_at)",
    """CREATE TABLE IF NOT EXISTS world_workshop_participants (
        session_id TEXT NOT NULL,
        account_bi TEXT NOT NULL,
        role TEXT NOT NULL CHECK (role IN ('owner', 'editor', 'viewer')),
        data TEXT NOT NULL,
        added_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (session_id, account_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_world_workshop_participants_account "
    "ON world_workshop_participants(account_bi, updated_at)",
    """CREATE TABLE IF NOT EXISTS world_workshop_results (
        result_id TEXT PRIMARY KEY,
        session_id TEXT NOT NULL,
        run_id TEXT NOT NULL,
        commit_hash TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_world_workshop_results_session "
    "ON world_workshop_results(session_id, created_at)",
    """CREATE TABLE IF NOT EXISTS world_workshop_events (
        event_no INTEGER PRIMARY KEY AUTOINCREMENT,
        event_id TEXT NOT NULL UNIQUE,
        session_id TEXT NOT NULL,
        kind TEXT NOT NULL CHECK (kind IN (
            'session-created', 'result-saved', 'participant-added',
            'participant-removed', 'comment', 'status-updated')),
        actor_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_world_workshop_events_session "
    "ON world_workshop_events(session_id, event_no)",
    # --- Security authorization and audit plane (migration 0043) -------------
    # Explicit least-privilege grants. Guest/registered-user are derived states;
    # this table holds elevated or scoped roles only. scope_bi is a blind index
    # of the organization/repository identifier, never a private repo name.
    """CREATE TABLE IF NOT EXISTS role_grants (
        account_bi TEXT NOT NULL,
        role TEXT NOT NULL,
        scope_type TEXT NOT NULL DEFAULT 'platform',
        scope_bi TEXT NOT NULL DEFAULT '',
        granted_by_bi TEXT,
        granted_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL DEFAULT 0,
        revoked_at INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (account_bi, role, scope_type, scope_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_role_grants_role "
    "ON role_grants(role, scope_type, scope_bi, revoked_at, expires_at)",
    # Append-only, content-free audit trail for sensitive state changes. Targets
    # are blind indexes and details are an allowlisted JSON object with no
    # credentials, bodies, addresses, private repo names, or raw IPs.
    """CREATE TABLE IF NOT EXISTS sensitive_audit_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ts INTEGER NOT NULL,
        actor_bi TEXT,
        actor_label TEXT NOT NULL DEFAULT '',
        action TEXT NOT NULL,
        target_type TEXT NOT NULL DEFAULT '',
        target_token TEXT NOT NULL DEFAULT '',
        outcome TEXT NOT NULL,
        details TEXT NOT NULL DEFAULT '{}')""",
    "CREATE INDEX IF NOT EXISTS idx_sensitive_audit_ts "
    "ON sensitive_audit_log(ts)",
    "CREATE INDEX IF NOT EXISTS idx_sensitive_audit_actor "
    "ON sensitive_audit_log(actor_bi, ts)",
    # Public encryption-key registrations contain public recipient bundles only.
    # Private halves remain on an owner's device. Revocation/rotation never asks
    # the Worker to possess a decryption key.
    """CREATE TABLE IF NOT EXISTS owner_encryption_keys (
        account_bi TEXT NOT NULL,
        key_id TEXT NOT NULL,
        public_bundle TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        revoked_at INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (account_bi, key_id))""",
    """CREATE TABLE IF NOT EXISTS repo_privacy_policy (
        repo_bi TEXT PRIMARY KEY,
        owner_bi TEXT NOT NULL,
        owner_key_id TEXT NOT NULL DEFAULT '',
        require_agent_e2ee INTEGER NOT NULL DEFAULT 1,
        require_mirror_encryption INTEGER NOT NULL DEFAULT 1,
        updated_at INTEGER NOT NULL)""",
    """CREATE TABLE IF NOT EXISTS repo_terms_flags (
        repo_bi TEXT PRIMARY KEY,
        active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),
        category TEXT NOT NULL CHECK (
            category IN ('spam','malware','harassment','illegal','other')),
        data TEXT NOT NULL,
        updated_by_bi TEXT NOT NULL,
        updated_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_repo_terms_flags_active "
    "ON repo_terms_flags(active, updated_at DESC)",
    # Non-custodial on-chain actions. The Worker stores an unsigned transfer
    # plan and later an on-chain signature; a designated local/external signer
    # owns the wallet and signs outside Cloudflare.
    """CREATE TABLE IF NOT EXISTS chain_intents (
        intent_id TEXT PRIMARY KEY,
        kind TEXT NOT NULL,
        source_address TEXT NOT NULL,
        signer_account TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL DEFAULT 'pending_signature',
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL DEFAULT 0,
        tx_signature TEXT NOT NULL DEFAULT '',
        completed_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_chain_intents_status "
    "ON chain_intents(status, created_at)",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_chain_intents_tx_signature "
    "ON chain_intents(tx_signature) WHERE tx_signature<>''",
    # Pending rewards are ledger allocations only: funds remain at the external
    # source wallet until a self-custodial recipient address is supplied.
    """CREATE TABLE IF NOT EXISTS pending_rewards (
        reward_id TEXT PRIMARY KEY,
        recipient_bi TEXT NOT NULL,
        recipient_name TEXT NOT NULL DEFAULT '',
        source_address TEXT NOT NULL,
        amount_lamports INTEGER NOT NULL,
        wallet_address TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL DEFAULT 'pending_wallet',
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        intent_id TEXT NOT NULL DEFAULT '',
        tx_signature TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_pending_rewards_recipient "
    "ON pending_rewards(recipient_bi, status, expires_at)",
    # Server-observed continuity for reward eligibility. A node cannot submit
    # its own uptime counter: the Worker advances this window only while its
    # signed HTTPS endpoint and forkmesh/forkmesh refs attestation are fresh.
    """CREATE TABLE IF NOT EXISTS reward_node_observations (
        node_bi TEXT PRIMARY KEY,
        first_verified_at INTEGER NOT NULL,
        last_verified_at INTEGER NOT NULL,
        consecutive_checks INTEGER NOT NULL DEFAULT 1,
        contribution_units INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_reward_node_observations_last "
    "ON reward_node_observations(last_verified_at)",
    # One auditable randomized schedule/selection record per configured time
    # bucket. All stored entropy and snapshots are public; no wallet secret is
    # involved in scheduling or selection.
    """CREATE TABLE IF NOT EXISTS reward_rounds (
        round_id TEXT PRIMARY KEY,
        status TEXT NOT NULL DEFAULT 'scheduled',
        interval_start INTEGER NOT NULL,
        scheduled_at INTEGER NOT NULL,
        execute_after INTEGER NOT NULL,
        schedule_entropy TEXT NOT NULL,
        snapshot_data TEXT NOT NULL,
        selection_data TEXT NOT NULL DEFAULT '',
        intent_id TEXT NOT NULL DEFAULT '',
        completed_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_reward_rounds_status "
    "ON reward_rounds(status, execute_after)",
    # Voluntary contributors transfer directly from their own wallet to the
    # public pool. These rows contain only public references and finalized
    # signatures, and choose either immediate all-node distribution or the
    # randomized trickle pool.
    """CREATE TABLE IF NOT EXISTS reward_contributions (
        contribution_id TEXT PRIMARY KEY,
        mode TEXT NOT NULL,
        amount_lamports INTEGER NOT NULL,
        reference_address TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'prepared',
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        tx_signature TEXT NOT NULL DEFAULT '',
        source_address TEXT NOT NULL DEFAULT '',
        confirmed_at INTEGER NOT NULL DEFAULT 0,
        distribution_intent_id TEXT NOT NULL DEFAULT '')""",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_reward_contribution_signature "
    "ON reward_contributions(tx_signature) WHERE tx_signature<>''",
    "CREATE INDEX IF NOT EXISTS idx_reward_contributions_status "
    "ON reward_contributions(status, created_at)",
    """CREATE TABLE IF NOT EXISTS reward_contribution_intents (
        contribution_id TEXT NOT NULL,
        intent_id TEXT NOT NULL,
        chunk_index INTEGER NOT NULL,
        chunk_count INTEGER NOT NULL,
        status TEXT NOT NULL DEFAULT 'pending_signature',
        PRIMARY KEY (contribution_id, intent_id))""",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_reward_contribution_intent_id "
    "ON reward_contribution_intents(intent_id)",
    # External provider entries are intentionally separate from `repositories`:
    # an imported metadata record or stub can never be routed as a live mirror.
    # The encrypted data blob carries public/private provider metadata; the
    # plaintext privacy bit lets anonymous queries exclude private rows before
    # any decryption or response construction.
    """CREATE TABLE IF NOT EXISTS repository_imports (
        id TEXT PRIMARY KEY,
        provider TEXT NOT NULL,
        external_id TEXT NOT NULL,
        owner_bi TEXT NOT NULL,
        is_private INTEGER NOT NULL DEFAULT 0,
        status TEXT NOT NULL DEFAULT 'external_repository',
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL)""",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_repository_imports_provider_id "
    "ON repository_imports(provider, external_id)",
    "CREATE INDEX IF NOT EXISTS idx_repository_imports_public "
    "ON repository_imports(is_private, updated_at)",
    "CREATE INDEX IF NOT EXISTS idx_repository_imports_owner "
    "ON repository_imports(owner_bi, updated_at)",
    # Eligible operators may volunteer to mirror an external entry. Public
    # responses expose only a count; the owned node label is visible to the
    # operator and listing moderator, not used as surveillance data.
    """CREATE TABLE IF NOT EXISTS repository_mirror_volunteers (
        repo_id TEXT NOT NULL,
        operator_bi TEXT NOT NULL,
        node_label TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'requested',
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (repo_id, operator_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_repository_mirror_volunteers_status "
    "ON repository_mirror_volunteers(repo_id, status, updated_at)",
    # Contributor invitations store the address and policy basis only inside
    # the encrypted blob. email_bi is a keyed blind index used for cooldowns
    # and opt-outs without making the address readable to a DB operator.
    """CREATE TABLE IF NOT EXISTS contributor_invitations (
        id TEXT PRIMARY KEY,
        repo_id TEXT NOT NULL,
        inviter_bi TEXT NOT NULL,
        email_bi TEXT NOT NULL,
        contributor_bi TEXT NOT NULL,
        provenance_id TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'pending',
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        sent_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_contributor_invitations_repo "
    "ON contributor_invitations(repo_id, created_at)",
    "CREATE INDEX IF NOT EXISTS idx_contributor_invitations_recipient "
    "ON contributor_invitations(email_bi, created_at)",
    # Eligibility is proved before an invitation is reserved. Provider-public
    # evidence, recipient consent, and owner-supplied provenance retain only
    # blind indexes in plaintext columns; human-readable audit context remains
    # inside the encrypted data blob.
    """CREATE TABLE IF NOT EXISTS contributor_invitation_provenance (
        id TEXT PRIMARY KEY,
        repo_id TEXT NOT NULL,
        contributor_bi TEXT NOT NULL,
        email_bi TEXT NOT NULL,
        basis TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        revoked_at INTEGER NOT NULL DEFAULT 0,
        CHECK (basis IN (
            'public_for_invitations','prior_consent','owner_supplied')))""",
    "CREATE INDEX IF NOT EXISTS idx_contributor_invitation_provenance_match "
    "ON contributor_invitation_provenance("
    "repo_id, contributor_bi, email_bi, revoked_at)",
    """CREATE TABLE IF NOT EXISTS contributor_invitation_rate (
        inviter_bi TEXT NOT NULL,
        day_bucket INTEGER NOT NULL,
        repo_id TEXT NOT NULL,
        sent_count INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (inviter_bi, day_bucket, repo_id))""",
    """CREATE TABLE IF NOT EXISTS contributor_invitation_optouts (
        email_bi TEXT PRIMARY KEY,
        opted_out_at INTEGER NOT NULL,
        source_invitation_id TEXT NOT NULL DEFAULT '')""",
    """CREATE TABLE IF NOT EXISTS contributor_invitation_abuse (
        id TEXT PRIMARY KEY,
        invitation_id TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        status TEXT NOT NULL DEFAULT 'open')""",
    "CREATE INDEX IF NOT EXISTS idx_contributor_invitation_abuse_invitation "
    "ON contributor_invitation_abuse(invitation_id, created_at)",
    # Uploaded logos are PNG/JPEG/WebP only (SVG uploads are rejected). Public
    # suggestions remain pending until a provider-verified owner or moderator
    # approves one. The partial unique index guarantees one official mark.
    """CREATE TABLE IF NOT EXISTS repository_logo_suggestions (
        id TEXT PRIMARY KEY,
        repo_id TEXT NOT NULL,
        proposer_bi TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'pending',
        official INTEGER NOT NULL DEFAULT 0,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        reviewed_at INTEGER NOT NULL DEFAULT 0,
        reviewed_by_bi TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_repository_logo_suggestions_repo "
    "ON repository_logo_suggestions(repo_id, status, created_at)",
    "CREATE INDEX IF NOT EXISTS idx_repository_logo_suggestions_proposer "
    "ON repository_logo_suggestions(repo_id, proposer_bi, status, created_at)",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_repository_logo_official "
    "ON repository_logo_suggestions(repo_id) WHERE official=1",
    # SQLite triggers make public-logo and invitation reservations atomic. The
    # matching 0056 migration upgrades databases that already ran migration
    # 0044; these IF NOT EXISTS statements also protect ensure_schema installs.
    """CREATE TRIGGER IF NOT EXISTS trg_logo_pending_proposer_limit
        BEFORE INSERT ON repository_logo_suggestions
        WHEN NEW.status='pending' AND NEW.official=0
         AND (SELECT COUNT(*) FROM repository_logo_suggestions
              WHERE repo_id=NEW.repo_id AND proposer_bi=NEW.proposer_bi
                AND status='pending') >= 5
        BEGIN
            SELECT RAISE(ABORT, 'logo_pending_proposer_limit');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_logo_official_insert
        BEFORE INSERT ON repository_logo_suggestions
        WHEN NEW.official=1
        BEGIN
            DELETE FROM repository_logo_suggestions
            WHERE id IN (
                SELECT id FROM repository_logo_suggestions
                WHERE repo_id=NEW.repo_id AND official=0
                ORDER BY CASE WHEN status IN ('rejected','superseded')
                              THEN 0 ELSE 1 END,
                         created_at ASC
                LIMIT CASE
                    WHEN (
                        SELECT COUNT(*) FROM repository_logo_suggestions
                        WHERE repo_id=NEW.repo_id) >= 50
                    THEN (
                        SELECT COUNT(*) FROM repository_logo_suggestions
                        WHERE repo_id=NEW.repo_id) - 49
                    ELSE 0
                END);
            UPDATE repository_logo_suggestions
            SET official=0,
                status=CASE WHEN status='approved'
                            THEN 'superseded' ELSE status END
            WHERE repo_id=NEW.repo_id AND official=1;
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_logo_official_approval
        BEFORE UPDATE OF official ON repository_logo_suggestions
        WHEN NEW.official=1 AND OLD.official<>1
        BEGIN
            UPDATE repository_logo_suggestions
            SET official=0,
                status=CASE WHEN status='approved'
                            THEN 'superseded' ELSE status END
            WHERE repo_id=NEW.repo_id AND id<>OLD.id AND official=1;
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_logo_community_repository_limit
        BEFORE INSERT ON repository_logo_suggestions
        WHEN NEW.status='pending' AND NEW.official=0
         AND (SELECT COUNT(*) FROM repository_logo_suggestions
              WHERE repo_id=NEW.repo_id) >= 49
        BEGIN
            SELECT RAISE(ABORT, 'logo_community_repository_limit');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_optout_reservation
        BEFORE INSERT ON contributor_invitations
        WHEN NEW.status='pending'
         AND EXISTS (SELECT 1 FROM contributor_invitation_optouts
                     WHERE email_bi=NEW.email_bi)
        BEGIN
            SELECT RAISE(ABORT, 'invitation_recipient_opted_out');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_provenance_reservation
        BEFORE INSERT ON contributor_invitations
        WHEN NEW.status='pending'
         AND NOT EXISTS (
             SELECT 1 FROM contributor_invitation_provenance
             WHERE id=NEW.provenance_id
               AND repo_id=NEW.repo_id
               AND contributor_bi=NEW.contributor_bi
               AND email_bi=NEW.email_bi
               AND revoked_at=0
         )
        BEGIN
            SELECT RAISE(ABORT, 'invitation_provenance_invalid');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_recipient_cooldown
        BEFORE INSERT ON contributor_invitations
        WHEN NEW.status='pending'
         AND EXISTS (
             SELECT 1 FROM contributor_invitations
             WHERE email_bi=NEW.email_bi
               AND status IN ('pending','sent')
               AND created_at > NEW.created_at - 2592000000)
        BEGIN
            SELECT RAISE(ABORT, 'invitation_recipient_cooldown');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_daily_limit
        BEFORE INSERT ON contributor_invitations
        WHEN NEW.status='pending'
         AND COALESCE((
             SELECT sent_count FROM contributor_invitation_rate
             WHERE inviter_bi=NEW.inviter_bi
               AND day_bucket=CAST(NEW.created_at / 86400000 AS INTEGER)
               AND repo_id='*'), 0) >= 20
        BEGIN
            SELECT RAISE(ABORT, 'invitation_daily_limit');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_repository_daily_limit
        BEFORE INSERT ON contributor_invitations
        WHEN NEW.status='pending'
         AND COALESCE((
             SELECT sent_count FROM contributor_invitation_rate
             WHERE inviter_bi=NEW.inviter_bi
               AND day_bucket=CAST(NEW.created_at / 86400000 AS INTEGER)
               AND repo_id=NEW.repo_id), 0) >= 10
        BEGIN
            SELECT RAISE(ABORT, 'invitation_repository_daily_limit');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_repository_history_limit
        BEFORE INSERT ON contributor_invitations
        WHEN NEW.status='pending'
         AND (SELECT COUNT(*) FROM contributor_invitations
              WHERE repo_id=NEW.repo_id) >= 500
        BEGIN
            SELECT RAISE(ABORT, 'invitation_repository_history_limit');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_reserve_rate
        AFTER INSERT ON contributor_invitations
        WHEN NEW.status='pending'
        BEGIN
            INSERT INTO contributor_invitation_rate
                (inviter_bi, day_bucket, repo_id, sent_count)
            VALUES
                (NEW.inviter_bi,
                 CAST(NEW.created_at / 86400000 AS INTEGER), '*', 1)
            ON CONFLICT(inviter_bi,day_bucket,repo_id)
            DO UPDATE SET sent_count=sent_count+1;
            INSERT INTO contributor_invitation_rate
                (inviter_bi, day_bucket, repo_id, sent_count)
            VALUES
                (NEW.inviter_bi,
                 CAST(NEW.created_at / 86400000 AS INTEGER), NEW.repo_id, 1)
            ON CONFLICT(inviter_bi,day_bucket,repo_id)
            DO UPDATE SET sent_count=sent_count+1;
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_invitation_release_rate
        AFTER DELETE ON contributor_invitations
        WHEN OLD.status='pending'
        BEGIN
            UPDATE contributor_invitation_rate
            SET sent_count=MAX(0, sent_count-1)
            WHERE inviter_bi=OLD.inviter_bi
              AND day_bucket=CAST(OLD.created_at / 86400000 AS INTEGER)
              AND repo_id IN ('*', OLD.repo_id);
            DELETE FROM contributor_invitation_rate
            WHERE inviter_bi=OLD.inviter_bi
              AND day_bucket=CAST(OLD.created_at / 86400000 AS INTEGER)
              AND repo_id IN ('*', OLD.repo_id)
              AND sent_count<=0;
        END""",
    # Content-free receipt written by the explicit offline legacy-custody scrub.
    # No address, key, artifact, account/repo name, or RPC credential is stored.
    """CREATE TABLE IF NOT EXISTS legacy_custody_migration_audit (
        migration_id TEXT PRIMARY KEY,
        artifact_sha256 TEXT NOT NULL,
        audit_sha256 TEXT NOT NULL,
        record_count INTEGER NOT NULL,
        address_count INTEGER NOT NULL,
        confirmed_at INTEGER NOT NULL,
        prepared_at INTEGER NOT NULL,
        applied_at INTEGER NOT NULL,
        status TEXT NOT NULL CHECK (status = 'scrubbed'),
        tool_version TEXT NOT NULL)""",
    # Public, key-free reconciliation evidence emitted by the offline scrub.
    # record_id is an opaque hash of the historical row locator; no account or
    # repository name, secret, RPC credential, or transaction bytes are stored.
    """CREATE TABLE IF NOT EXISTS legacy_custody_reconciliation (
        record_id TEXT PRIMARY KEY,
        source_table TEXT NOT NULL,
        public_address TEXT NOT NULL,
        network TEXT NOT NULL,
        balance_lamports INTEGER NOT NULL CHECK (balance_lamports = 0),
        rpc_slot INTEGER NOT NULL,
        reconciled_at INTEGER NOT NULL,
        artifact_sha256 TEXT NOT NULL,
        audit_sha256 TEXT NOT NULL)""",
    # Public UTC announcements are D1-backed rather than hardcoded into the
    # production client. Mutations are platform-admin gated and audited;
    # created_by_bi never appears in public reads.
    """CREATE TABLE IF NOT EXISTS world_events (
        event_id TEXT PRIMARY KEY,
        status TEXT NOT NULL DEFAULT 'scheduled'
            CHECK (status IN ('scheduled', 'cancelled')),
        starts_at INTEGER NOT NULL,
        ends_at INTEGER NOT NULL,
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        cancelled_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_world_events_public "
    "ON world_events(status, ends_at, starts_at)",
    "CREATE INDEX IF NOT EXISTS idx_world_events_retention "
    "ON world_events(status, cancelled_at, ends_at)",
    """CREATE TRIGGER IF NOT EXISTS trg_world_events_record_limit
        BEFORE INSERT ON world_events
        WHEN (SELECT COUNT(*) FROM world_events) >= 500
        BEGIN
            SELECT RAISE(ABORT, 'world_event_catalog_full');
        END""",
    # Private organization marketing work for the in-world Office (migration
    # 0075). Human-readable task copy, assignee labels, and check-in notes are
    # encrypted; plaintext is limited to blind indexes, bounded state, and
    # server-authoritative timer metadata.
    """CREATE TABLE IF NOT EXISTS world_office_marketing_tasks (
        task_id TEXT PRIMARY KEY,
        org_bi TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'idle'
            CHECK (status IN ('idle', 'active')),
        assignee_bi TEXT NOT NULL,
        active_assignee_bi TEXT NOT NULL DEFAULT '',
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        elapsed_ms INTEGER NOT NULL DEFAULT 0 CHECK (elapsed_ms >= 0),
        started_at INTEGER NOT NULL DEFAULT 0 CHECK (started_at >= 0),
        next_checkin_at INTEGER NOT NULL DEFAULT 0
            CHECK (next_checkin_at >= 0),
        completed_at INTEGER NOT NULL DEFAULT 0
            CHECK (completed_at >= 0))""",
    "CREATE INDEX IF NOT EXISTS idx_world_office_marketing_tasks_org "
    "ON world_office_marketing_tasks(org_bi, updated_at DESC)",
    "CREATE INDEX IF NOT EXISTS idx_world_office_marketing_tasks_assignee "
    "ON world_office_marketing_tasks(org_bi, assignee_bi, updated_at DESC)",
    """CREATE UNIQUE INDEX IF NOT EXISTS
        idx_world_office_one_active_per_assignee
        ON world_office_marketing_tasks(org_bi, active_assignee_bi)
        WHERE status = 'active' AND active_assignee_bi <> ''""",
    """CREATE TRIGGER IF NOT EXISTS trg_world_office_marketing_task_limit
        BEFORE INSERT ON world_office_marketing_tasks
        WHEN (
            SELECT COUNT(*) FROM world_office_marketing_tasks
            WHERE org_bi = NEW.org_bi
        ) >= 250
        BEGIN
            SELECT RAISE(
                ABORT, 'world_office_marketing_task_catalog_full');
        END""",
    """CREATE TABLE IF NOT EXISTS world_office_marketing_checkins (
        checkin_id TEXT PRIMARY KEY,
        task_id TEXT NOT NULL,
        org_bi TEXT NOT NULL,
        account_bi TEXT NOT NULL,
        state TEXT NOT NULL CHECK (state IN (
            'going_well', 'blocked', 'needs_help')),
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_world_office_marketing_checkins_task "
    "ON world_office_marketing_checkins(task_id, created_at DESC)",
    """CREATE TRIGGER IF NOT EXISTS trg_world_office_marketing_checkin_limit
        BEFORE INSERT ON world_office_marketing_checkins
        WHEN (SELECT COUNT(*) FROM world_office_marketing_checkins) >= 12500
        BEGIN
            SELECT RAISE(
                ABORT, 'world_office_marketing_checkin_catalog_full');
        END""",
    # Organization-private universal tasks (migration 0105). Department/team
    # and bounded routing state remain queryable inside D1; all human-readable
    # task copy, labels, repository references, and reviewer names stay in
    # encrypted payloads.
    """CREATE TABLE IF NOT EXISTS organization_tasks (
        task_id TEXT PRIMARY KEY,
        org_bi TEXT NOT NULL,
        department TEXT NOT NULL DEFAULT 'general'
            CHECK (length(department) BETWEEN 1 AND 64),
        team TEXT NOT NULL DEFAULT '' CHECK (length(team) <= 64),
        destination TEXT NOT NULL DEFAULT 'department'
            CHECK (destination IN (
                'department','personal','repository','qa','agent')),
        assignee_kind TEXT NOT NULL DEFAULT 'user'
            CHECK (assignee_kind IN (
                'user','unassigned','agent')),
        status TEXT NOT NULL DEFAULT 'idle'
            CHECK (status IN ('idle','active')),
        assignee_bi TEXT NOT NULL DEFAULT '',
        active_assignee_bi TEXT NOT NULL DEFAULT '',
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        elapsed_ms INTEGER NOT NULL DEFAULT 0 CHECK (elapsed_ms >= 0),
        started_at INTEGER NOT NULL DEFAULT 0 CHECK (started_at >= 0),
        next_checkin_at INTEGER NOT NULL DEFAULT 0
            CHECK (next_checkin_at >= 0),
        completed_at INTEGER NOT NULL DEFAULT 0
            CHECK (completed_at >= 0),
        qa_status TEXT NOT NULL DEFAULT 'unknown'
            CHECK (qa_status IN ('unknown','passed','failed')),
        qa_reviewer_bi TEXT NOT NULL DEFAULT '',
        qa_reviewed_at INTEGER NOT NULL DEFAULT 0
            CHECK (qa_reviewed_at >= 0),
        qa_requested_at INTEGER NOT NULL DEFAULT 0
            CHECK (qa_requested_at >= 0),
        priority INTEGER NOT NULL DEFAULT 50
            CHECK (priority BETWEEN 1 AND 99),
        agent_session_id TEXT NOT NULL DEFAULT ''
            CHECK (length(agent_session_id) <= 64))""",
    "CREATE INDEX IF NOT EXISTS idx_organization_tasks_org_updated "
    "ON organization_tasks(org_bi, updated_at DESC)",
    "CREATE INDEX IF NOT EXISTS idx_organization_tasks_scope "
    "ON organization_tasks(org_bi, department, team, updated_at DESC)",
    "CREATE INDEX IF NOT EXISTS idx_organization_tasks_assignee "
    "ON organization_tasks(org_bi, assignee_bi, updated_at DESC)",
    "CREATE INDEX IF NOT EXISTS idx_organization_tasks_qa "
    "ON organization_tasks(org_bi, qa_requested_at, qa_reviewed_at DESC)",
    "CREATE INDEX IF NOT EXISTS idx_organization_tasks_global_priority "
    "ON organization_tasks(org_bi, priority, completed_at, updated_at DESC)",
    "UPDATE organization_tasks SET priority=99 WHERE priority>99",
    """CREATE UNIQUE INDEX IF NOT EXISTS
        idx_organization_tasks_one_active_assignee
        ON organization_tasks(org_bi, active_assignee_bi)
        WHERE status='active' AND active_assignee_bi<>''""",
    """CREATE TRIGGER IF NOT EXISTS trg_organization_task_limit
        BEFORE INSERT ON organization_tasks
        WHEN (
            SELECT COUNT(*) FROM organization_tasks
            WHERE org_bi=NEW.org_bi
        ) >= 2000
        BEGIN
            SELECT RAISE(ABORT, 'organization_task_catalog_full');
        END""",
    """CREATE TABLE IF NOT EXISTS organization_task_attachments (
        attachment_id TEXT PRIMARY KEY,
        task_id TEXT NOT NULL,
        org_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_organization_task_attachments_task "
    "ON organization_task_attachments(org_bi, task_id, created_at)",
    """CREATE TRIGGER IF NOT EXISTS trg_organization_task_attachment_limit
        BEFORE INSERT ON organization_task_attachments
        WHEN (
            SELECT COUNT(*) FROM organization_task_attachments
            WHERE org_bi=NEW.org_bi AND task_id=NEW.task_id
        ) >= 4
        BEGIN
            SELECT RAISE(
                ABORT, 'organization_task_attachment_catalog_full');
        END""",
    """CREATE TABLE IF NOT EXISTS mailtrap_email_sends (
        send_id TEXT PRIMARY KEY,
        account_bi TEXT NOT NULL,
        kind TEXT NOT NULL DEFAULT '',
        sent_at INTEGER NOT NULL,
        accepted INTEGER NOT NULL DEFAULT 0
            CHECK (accepted IN (0, 1)),
        status TEXT NOT NULL DEFAULT '',
        status_at INTEGER NOT NULL DEFAULT 0,
        message_id TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_mailtrap_email_sends_account "
    "ON mailtrap_email_sends(account_bi, sent_at DESC)",
    """CREATE TABLE IF NOT EXISTS mailtrap_webhook_events (
        event_id TEXT PRIMARY KEY,
        received_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_mailtrap_webhook_events_received "
    "ON mailtrap_webhook_events(received_at)",
    """CREATE TABLE IF NOT EXISTS organization_task_checkins (
        checkin_id TEXT PRIMARY KEY,
        task_id TEXT NOT NULL,
        org_bi TEXT NOT NULL,
        account_bi TEXT NOT NULL,
        state TEXT NOT NULL CHECK (state IN (
            'going_well','blocked','needs_help')),
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_organization_task_checkins_task "
    "ON organization_task_checkins(org_bi, task_id, created_at DESC)",
    """CREATE TABLE IF NOT EXISTS organization_task_qa_reviews (
        task_id TEXT NOT NULL,
        org_bi TEXT NOT NULL,
        reviewer_bi TEXT NOT NULL,
        verdict TEXT NOT NULL CHECK (verdict IN (
            'passed','failed','unknown')),
        data TEXT NOT NULL,
        reviewed_at INTEGER NOT NULL CHECK (reviewed_at >= 0),
        PRIMARY KEY (task_id, reviewer_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_organization_task_qa_reviews_task "
    "ON organization_task_qa_reviews(org_bi, task_id, reviewed_at DESC)",
    # Outside collaborators can be placed on a bounded team without becoming
    # organization members. No repository or Office authorization query reads
    # this table; it is used only by explicitly collaborator-aware features
    # such as the private QA deck.
    """CREATE TABLE IF NOT EXISTS org_team_collaborators (
        org_bi TEXT NOT NULL,
        team TEXT NOT NULL,
        account_bi TEXT NOT NULL,
        name TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 64),
        added_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (org_bi,team,account_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_org_team_collaborators_account "
    "ON org_team_collaborators(account_bi, org_bi, team)",
    # Lazy-schema installs also promote legacy Marketing rows. Keeping this
    # copy idempotent covers local databases that do not run migration files.
    """INSERT OR IGNORE INTO organization_tasks (
        task_id,org_bi,department,team,destination,assignee_kind,status,
        assignee_bi,active_assignee_bi,data,created_by_bi,created_at,updated_at,
        elapsed_ms,started_at,next_checkin_at,completed_at)
        SELECT task_id,org_bi,'marketing','marketing','department','user',
        status,assignee_bi,active_assignee_bi,data,created_by_bi,created_at,
        updated_at,elapsed_ms,started_at,next_checkin_at,completed_at
        FROM world_office_marketing_tasks""",
    """INSERT OR IGNORE INTO organization_task_checkins (
        checkin_id,task_id,org_bi,account_bi,state,data,created_at)
        SELECT checkin_id,task_id,org_bi,account_bi,state,data,created_at
        FROM world_office_marketing_checkins""",
    # Shared Office lobby attendance board (migration 0085). Each visit is one
    # row that is opened by an authenticated IN punch and closed in place by
    # the matching OUT punch. The public account name is the only display
    # value; no address, user-agent, session token, or device data is retained.
    """CREATE TABLE IF NOT EXISTS world_office_attendance (
        visit_id TEXT PRIMARY KEY CHECK (
            length(visit_id) = 32
            AND visit_id NOT GLOB '*[^0-9a-f]*'
        ),
        account_bi TEXT NOT NULL CHECK (
            length(account_bi) = 64
            AND account_bi NOT GLOB '*[^0-9a-f]*'
        ),
        account_name TEXT NOT NULL CHECK (
            length(account_name) BETWEEN 1 AND 32
        ),
        in_at INTEGER NOT NULL CHECK (in_at > 0),
        out_at INTEGER CHECK (out_at IS NULL OR out_at >= in_at),
        last_seen_at INTEGER NOT NULL DEFAULT 0 CHECK (last_seen_at >= 0),
        floor_id TEXT NOT NULL DEFAULT '' CHECK (length(floor_id) <= 32),
        visit_scope TEXT NOT NULL DEFAULT 'office'
            CHECK (visit_scope IN ('legacy', 'office')))""",
    """CREATE UNIQUE INDEX IF NOT EXISTS idx_world_office_attendance_open
        ON world_office_attendance(account_bi)
        WHERE out_at IS NULL""",
    """CREATE INDEX IF NOT EXISTS idx_world_office_attendance_recent
        ON world_office_attendance(in_at DESC, visit_id DESC)""",
    """CREATE INDEX IF NOT EXISTS idx_world_office_attendance_live
        ON world_office_attendance(out_at, last_seen_at DESC)""",
    """CREATE INDEX IF NOT EXISTS idx_world_office_attendance_scope
        ON world_office_attendance(visit_scope, account_bi, in_at)""",
    # Marketing proof-of-work links (migration 0096). Public social URLs and
    # labels remain inside encrypted payloads and are disclosed only to a
    # currently authorized Marketing-team member.
    """CREATE TABLE IF NOT EXISTS world_office_marketing_proofs (
        proof_id TEXT PRIMARY KEY CHECK (
            length(proof_id) = 32
            AND proof_id NOT GLOB '*[^0-9a-f]*'
        ),
        org_bi TEXT NOT NULL,
        account_bi TEXT NOT NULL,
        url_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL CHECK (created_at > 0))""",
    """CREATE INDEX IF NOT EXISTS idx_world_office_marketing_proofs_org
        ON world_office_marketing_proofs(
            org_bi, created_at DESC, proof_id DESC)""",
    """CREATE INDEX IF NOT EXISTS idx_world_office_marketing_proofs_member
        ON world_office_marketing_proofs(
            org_bi, account_bi, created_at DESC, proof_id DESC)""",
    """CREATE TRIGGER IF NOT EXISTS
        trg_world_office_marketing_proofs_capacity
        BEFORE INSERT ON world_office_marketing_proofs
        WHEN (SELECT COUNT(*) FROM world_office_marketing_proofs) >= 10000
        BEGIN
            SELECT RAISE(
                ABORT, 'world_office_marketing_proof_catalog_full');
        END""",
    # Evidence-reviewed contextual placements. The accounting table is
    # deliberately isolated from every wallet/reward ledger.
    """CREATE TABLE IF NOT EXISTS community_ad_instance_policy (
        instance_id TEXT PRIMARY KEY,
        enabled INTEGER NOT NULL DEFAULT 0 CHECK (enabled IN (0, 1)),
        contexts TEXT NOT NULL DEFAULT '[]',
        revenue_destination TEXT NOT NULL DEFAULT '',
        revenue_destination_type TEXT NOT NULL DEFAULT ''
            CHECK (revenue_destination_type IN (
                '', 'project-operations', 'instance-operations',
                'community-grants', 'nonprofit'
            )),
        updated_by_bi TEXT NOT NULL,
        updated_at INTEGER NOT NULL)""",
    """CREATE TABLE IF NOT EXISTS community_ad_proposals (
        proposal_id TEXT PRIMARY KEY,
        proposer_bi TEXT NOT NULL,
        status TEXT NOT NULL CHECK (status IN (
            'voting', 'approved', 'rejected', 'expired',
            'suspended', 'appealed'
        )),
        data TEXT NOT NULL,
        opens_at INTEGER NOT NULL,
        closes_at INTEGER NOT NULL,
        review_due_at INTEGER NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_community_ad_proposals_status_review "
    "ON community_ad_proposals(status, review_due_at)",
    """CREATE TABLE IF NOT EXISTS community_ad_votes (
        proposal_id TEXT NOT NULL,
        voter_bi TEXT NOT NULL,
        choice TEXT NOT NULL CHECK (choice IN (
            'approve', 'reject', 'abstain'
        )),
        conflict INTEGER NOT NULL DEFAULT 0 CHECK (conflict IN (0, 1)),
        disclosure TEXT NOT NULL DEFAULT '',
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (proposal_id, voter_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_community_ad_votes_proposal "
    "ON community_ad_votes(proposal_id, choice)",
    """CREATE TABLE IF NOT EXISTS community_ad_moderation (
        record_id TEXT PRIMARY KEY,
        proposal_id TEXT NOT NULL,
        action TEXT NOT NULL CHECK (action IN (
            'note', 'suspend', 'reinstate', 'reject', 'approve',
            'appeal-filed', 'appeal-upheld', 'appeal-denied'
        )),
        reason TEXT NOT NULL,
        evidence_url TEXT NOT NULL DEFAULT '',
        actor_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_community_ad_moderation_proposal "
    "ON community_ad_moderation(proposal_id, created_at)",
    """CREATE TABLE IF NOT EXISTS community_ad_revenue (
        entry_id TEXT PRIMARY KEY,
        instance_id TEXT NOT NULL,
        proposal_id TEXT NOT NULL,
        ledger_class TEXT NOT NULL DEFAULT 'advertising-revenue'
            CHECK (ledger_class = 'advertising-revenue'),
        amount_minor INTEGER NOT NULL CHECK (amount_minor >= 0),
        currency TEXT NOT NULL,
        destination_snapshot TEXT NOT NULL,
        external_reference TEXT NOT NULL,
        recorded_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_community_ad_revenue_instance "
    "ON community_ad_revenue(instance_id, recorded_at)",
    """CREATE TRIGGER IF NOT EXISTS trg_community_ad_proposal_limit
        BEFORE INSERT ON community_ad_proposals
        WHEN (SELECT COUNT(*) FROM community_ad_proposals) >= 2000
        BEGIN
            SELECT RAISE(ABORT, 'community ad proposal limit reached');
        END""",
    # Non-custodial organization succession (migration 0064). Every identity is
    # already a public organization/account identifier; there is no arbitrary
    # encrypted blob and no field capable of carrying credentials, account
    # contents, devices, repository ACLs, private data, or financial material.
    """CREATE TABLE IF NOT EXISTS org_succession_policies (
        org_bi TEXT PRIMARY KEY,
        owner_bi TEXT NOT NULL,
        owner_name TEXT NOT NULL,
        successor_bi TEXT NOT NULL,
        successor_name TEXT NOT NULL,
        inactivity_days INTEGER NOT NULL CHECK (
            inactivity_days BETWEEN 30 AND 730),
        grace_days INTEGER NOT NULL CHECK (grace_days BETWEEN 7 AND 90),
        approval_threshold INTEGER NOT NULL CHECK (
            approval_threshold BETWEEN 2 AND 20),
        owner_last_active_at INTEGER NOT NULL,
        enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0, 1)),
        configured_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_org_succession_warning "
    "ON org_succession_policies(enabled, owner_last_active_at)",
    """CREATE TABLE IF NOT EXISTS org_succession_cases (
        case_id TEXT PRIMARY KEY,
        org_bi TEXT NOT NULL,
        owner_bi TEXT NOT NULL,
        owner_name TEXT NOT NULL,
        successor_bi TEXT NOT NULL,
        successor_name TEXT NOT NULL,
        inactivity_days INTEGER NOT NULL CHECK (
            inactivity_days BETWEEN 30 AND 730),
        grace_days INTEGER NOT NULL CHECK (grace_days BETWEEN 7 AND 90),
        approval_threshold INTEGER NOT NULL CHECK (
            approval_threshold BETWEEN 2 AND 20),
        opened_at INTEGER NOT NULL,
        grace_ends_at INTEGER NOT NULL,
        status TEXT NOT NULL CHECK (
            status IN ('grace', 'cancelled', 'completed', 'superseded')),
        resolved_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_org_succession_one_active_case "
    "ON org_succession_cases(org_bi) WHERE status='grace'",
    "CREATE INDEX IF NOT EXISTS idx_org_succession_cases_history "
    "ON org_succession_cases(org_bi, opened_at)",
    """CREATE TABLE IF NOT EXISTS org_succession_approvals (
        case_id TEXT NOT NULL,
        approver_bi TEXT NOT NULL,
        approved_at INTEGER NOT NULL,
        PRIMARY KEY (case_id, approver_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_org_succession_approvals_case "
    "ON org_succession_approvals(case_id, approved_at)",
    """CREATE TABLE IF NOT EXISTS org_succession_events (
        event_id TEXT PRIMARY KEY,
        org_bi TEXT NOT NULL,
        case_id TEXT NOT NULL DEFAULT '',
        actor_bi TEXT NOT NULL DEFAULT '',
        event_type TEXT NOT NULL CHECK (event_type IN (
            'configured', 'configuration-disabled', 'warning-issued',
            'grace-opened', 'approval-recorded', 'cancelled', 'check-in',
            'completed', 'completion-denied')),
        status TEXT NOT NULL CHECK (status IN (
            'active', 'disabled', 'warning', 'grace', 'cancelled',
            'superseded', 'completed')),
        created_at INTEGER NOT NULL,
        inactivity_days INTEGER NOT NULL DEFAULT 0,
        grace_days INTEGER NOT NULL DEFAULT 0,
        approval_threshold INTEGER NOT NULL DEFAULT 0,
        approval_count INTEGER NOT NULL DEFAULT 0,
        dedupe_key TEXT NOT NULL UNIQUE)""",
    "CREATE INDEX IF NOT EXISTS idx_org_succession_events_history "
    "ON org_succession_events(org_bi, created_at)",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_approval_authorized
        BEFORE INSERT ON org_succession_approvals
        BEGIN
            SELECT RAISE(
                ABORT, 'succession_approver_not_authorized')
            WHERE NOT EXISTS (
                SELECT 1 FROM org_succession_cases c
                JOIN org_members m
                  ON m.org_bi=c.org_bi AND m.member_bi=NEW.approver_bi
                WHERE c.case_id=NEW.case_id
                  AND c.status='grace'
                  AND NEW.approved_at<=c.grace_ends_at
                  AND m.role IN ('owner','admin','member')
                  AND NEW.approver_bi<>c.owner_bi
                  AND NEW.approver_bi<>c.successor_bi
            );
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_approval_no_update
        BEFORE UPDATE ON org_succession_approvals
        BEGIN
            SELECT RAISE(ABORT, 'succession_approvals_append_only');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_approval_no_delete
        BEFORE DELETE ON org_succession_approvals
        BEGIN
            SELECT RAISE(ABORT, 'succession_approvals_append_only');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_event_no_update
        BEFORE UPDATE ON org_succession_events
        BEGIN
            SELECT RAISE(ABORT, 'succession_events_append_only');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_event_no_delete
        BEFORE DELETE ON org_succession_events
        BEGIN
            SELECT RAISE(ABORT, 'succession_events_append_only');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_case_immutable
        BEFORE UPDATE ON org_succession_cases
        WHEN NEW.case_id<>OLD.case_id
          OR NEW.org_bi<>OLD.org_bi
          OR NEW.owner_bi<>OLD.owner_bi
          OR NEW.owner_name<>OLD.owner_name
          OR NEW.successor_bi<>OLD.successor_bi
          OR NEW.successor_name<>OLD.successor_name
          OR NEW.inactivity_days<>OLD.inactivity_days
          OR NEW.grace_days<>OLD.grace_days
          OR NEW.approval_threshold<>OLD.approval_threshold
          OR NEW.opened_at<>OLD.opened_at
          OR NEW.grace_ends_at<>OLD.grace_ends_at
        BEGIN
            SELECT RAISE(ABORT, 'succession_case_snapshot_immutable');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_case_transition
        BEFORE UPDATE OF status ON org_succession_cases
        WHEN NEW.status<>OLD.status
         AND NOT (
            OLD.status='grace'
            AND NEW.status IN ('cancelled','completed','superseded')
         )
        BEGIN
            SELECT RAISE(ABORT, 'invalid_succession_transition');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_case_no_delete
        BEFORE DELETE ON org_succession_cases
        BEGIN
            SELECT RAISE(ABORT, 'succession_cases_append_only');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_completion_guard
        BEFORE UPDATE OF status ON org_succession_cases
        WHEN NEW.status='completed' AND OLD.status='grace'
        BEGIN
            SELECT RAISE(ABORT, 'succession_grace_active')
            WHERE NEW.resolved_at<OLD.grace_ends_at;
            SELECT RAISE(ABORT, 'succession_owner_changed')
            WHERE NOT EXISTS (
                SELECT 1 FROM org_members
                WHERE org_bi=OLD.org_bi
                  AND member_bi=OLD.owner_bi AND role='owner'
            );
            SELECT RAISE(ABORT, 'succession_successor_changed')
            WHERE NOT EXISTS (
                SELECT 1 FROM org_members
                WHERE org_bi=OLD.org_bi
                  AND member_bi=OLD.successor_bi
                  AND role IN ('admin','member')
            );
            SELECT RAISE(
                ABORT, 'succession_approval_threshold_not_met')
            WHERE (
                SELECT COUNT(*) FROM org_succession_approvals a
                JOIN org_members m
                  ON m.org_bi=OLD.org_bi
                 AND m.member_bi=a.approver_bi
                WHERE a.case_id=OLD.case_id
                  AND m.role IN ('owner','admin','member')
                  AND a.approver_bi<>OLD.owner_bi
                  AND a.approver_bi<>OLD.successor_bi
            )<OLD.approval_threshold;
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_role_transfer
        AFTER UPDATE OF status ON org_succession_cases
        WHEN NEW.status='completed' AND OLD.status='grace'
        BEGIN
            UPDATE org_members SET role='admin'
            WHERE org_bi=OLD.org_bi
              AND member_bi=OLD.owner_bi AND role='owner';
            UPDATE org_members SET role='owner'
            WHERE org_bi=OLD.org_bi
              AND member_bi=OLD.successor_bi
              AND role IN ('admin','member');
        END""",
    """CREATE TRIGGER IF NOT EXISTS trg_org_succession_org_delete
        BEFORE DELETE ON orgs
        WHEN EXISTS (
            SELECT 1 FROM org_succession_policies
            WHERE org_bi=OLD.org_bi AND enabled=1
        )
        BEGIN
            INSERT OR IGNORE INTO org_succession_events
                (event_id,org_bi,case_id,actor_bi,event_type,status,
                 created_at,inactivity_days,grace_days,approval_threshold,
                 approval_count,dedupe_key)
            SELECT
                'org-delete:' || p.org_bi || ':' || p.updated_at,
                p.org_bi,
                COALESCE((
                    SELECT case_id FROM org_succession_cases
                    WHERE org_bi=p.org_bi AND status='grace' LIMIT 1
                ), ''),
                '',
                'configuration-disabled',
                'disabled',
                p.updated_at,
                p.inactivity_days,
                p.grace_days,
                p.approval_threshold,
                0,
                'org-delete:' || p.org_bi || ':' || p.updated_at
            FROM org_succession_policies p
            WHERE p.org_bi=OLD.org_bi AND p.enabled=1;
            UPDATE org_succession_cases
            SET status='cancelled',resolved_at=MAX(resolved_at, opened_at)
            WHERE org_bi=OLD.org_bi AND status='grace';
            UPDATE org_succession_policies
            SET enabled=0
            WHERE org_bi=OLD.org_bi AND enabled=1;
        END""",
    # User-managed Git SSH public keys. The reversible public-key line and its
    # private display label live only in the encrypted data envelope; D1 keeps
    # keyed blind indexes for account/key lookup plus the standard public
    # fingerprint and algorithm needed by the owner's settings UI. Revocation
    # is retained instead of deleting the row so a previously revoked key can
    # never silently become trusted again.
    """CREATE TABLE IF NOT EXISTS account_ssh_keys (
        key_id TEXT PRIMARY KEY,
        account_bi TEXT NOT NULL,
        key_bi TEXT NOT NULL UNIQUE,
        key_type TEXT NOT NULL,
        fingerprint TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        last_used_at INTEGER NOT NULL DEFAULT 0,
        revoked_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_account_ssh_keys_account "
    "ON account_ssh_keys(account_bi, revoked_at, created_at)",
    # Revocable account sessions. The browser/desktop receives a random bearer
    # value; D1 keeps only its keyed digest and opaque id. Logout, password
    # reset, account disable/delete and identity-key rotation can therefore
    # revoke immediately without storing a replayable credential. client_ip is
    # the edge-observed sign-in address, returned only to the owner of the
    # account it belongs to so a stolen device can be recognized and revoked.
    """CREATE TABLE IF NOT EXISTS account_sessions (
        session_id TEXT PRIMARY KEY,
        account_bi TEXT NOT NULL,
        token_digest TEXT NOT NULL UNIQUE,
        created_at INTEGER NOT NULL,
        last_seen_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        revoked_at INTEGER NOT NULL DEFAULT 0,
        device_label TEXT NOT NULL DEFAULT '',
        client_ip TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_account_sessions_account "
    "ON account_sessions(account_bi, revoked_at, expires_at)",
    "CREATE INDEX IF NOT EXISTS idx_account_sessions_expiry "
    "ON account_sessions(expires_at, revoked_at)",
    # Explicit, administrator-created World moderation blocks. Subjects are
    # rotating keyed tokens derived transiently at the edge; raw addresses and
    # user-agent strings never enter D1. This table is intentionally separate
    # from the retired automated abuse/quarantine tables and has no signal,
    # scoring, detection, or permanent-ban state.
    """CREATE TABLE IF NOT EXISTS world_manual_blocks (
        block_id TEXT PRIMARY KEY,
        target_type TEXT NOT NULL CHECK (target_type IN ('ip','agent')),
        subject_token TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        revoked_at INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_world_manual_blocks_active "
    "ON world_manual_blocks(target_type, subject_token, expires_at) "
    "WHERE revoked_at=0",
    # The administrator-curated Town Square placement overrides (migrations
    # 0080/0082) were retired with the layout editor by migration 0115; make
    # sure lazily-ensured DBs lose the table too.
    "DROP TABLE IF EXISTS world_object_layout",
    # One public, last-known-good CelesTrak VISUAL OMM snapshot. A scheduled
    # refresh owns all upstream traffic; visitor reads never fetch CelesTrak.
    # This row contains no visitor location, account, session, or wallet data.
    """CREATE TABLE IF NOT EXISTS world_satellite_snapshot (
        snapshot_id INTEGER PRIMARY KEY CHECK (snapshot_id = 1),
        data TEXT NOT NULL CHECK (
            length(data) > 0 AND length(data) <= 524288
        ),
        digest TEXT NOT NULL CHECK (
            length(digest) = 64
            AND digest NOT GLOB '*[^0-9a-f]*'
        ),
        fetched_at INTEGER NOT NULL CHECK (fetched_at > 0),
        source_epoch TEXT NOT NULL,
        last_attempt_at INTEGER NOT NULL CHECK (last_attempt_at > 0),
        last_status INTEGER NOT NULL DEFAULT 200 CHECK (
            last_status >= 0 AND last_status <= 599
        ),
        last_error TEXT NOT NULL DEFAULT ''
            CHECK (length(last_error) <= 240))""",
    # Referral-program counters, one row per referring account. referrer_bi is
    # the account blind index and name is the plaintext public username (the
    # same identity already shown on the profile page and leaderboards). Only
    # aggregate counters are kept — no IP, session, or referred-user identity
    # is ever stored here.
    """CREATE TABLE IF NOT EXISTS referral_stats (
        referrer_bi TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        clicks INTEGER NOT NULL DEFAULT 0,
        signups INTEGER NOT NULL DEFAULT 0,
        last_ts INTEGER NOT NULL DEFAULT 0)""",
    "CREATE INDEX IF NOT EXISTS idx_referral_stats_rank "
    "ON referral_stats(signups DESC, clicks DESC)",
    # Achievement badges (migration 0084): public recognition marks awarded
    # once per (badge, account), either automatically at a real platform event
    # (first 100 signups, an hour in the World, the first successful referral,
    # becoming a mirror operator) or by a platform administrator. account_bi is
    # a blind index; name is the plaintext public username (same trust level as
    # profile_follows / referral_stats.name). granted_by is 'system' for an
    # automatic award or the granting admin's username. Awards are permanent:
    # later losing eligibility does not revoke an already-earned badge.
    """CREATE TABLE IF NOT EXISTS badge_awards (
        badge_slug TEXT NOT NULL,
        account_bi TEXT NOT NULL,
        name TEXT NOT NULL,
        granted_by TEXT NOT NULL DEFAULT 'system',
        created_at INTEGER NOT NULL,
        PRIMARY KEY (badge_slug, account_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_badge_awards_account "
    "ON badge_awards(account_bi)",
    # Inbound website referrals: one aggregate row per referring hostname.
    # last_url is the latest bounded public referring URL, with credentials,
    # fragments, and sensitive query values removed (migration 0095).
    """CREATE TABLE IF NOT EXISTS site_referrers (
        host TEXT PRIMARY KEY,
        visits INTEGER NOT NULL DEFAULT 0,
        first_ts INTEGER NOT NULL DEFAULT 0,
        last_ts INTEGER NOT NULL DEFAULT 0,
        last_url TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_site_referrers_rank "
    "ON site_referrers(visits DESC, last_ts DESC)",
    # Consented World lobby link submissions. URLs/account names live only in
    # encrypted data; public score/range values are bounded (migration 0103).
    """CREATE TABLE IF NOT EXISTS world_lobby_links (
        link_id TEXT PRIMARY KEY,
        account_bi TEXT NOT NULL,
        url_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        score INTEGER NOT NULL CHECK (score BETWEEN 0 AND 100),
        potential_low INTEGER NOT NULL CHECK (potential_low >= 0),
        potential_high INTEGER NOT NULL CHECK (
            potential_high >= potential_low),
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        UNIQUE(account_bi, url_bi))""",
    "CREATE INDEX IF NOT EXISTS idx_world_lobby_links_recent "
    "ON world_lobby_links(created_at DESC, link_id DESC)",
    "CREATE INDEX IF NOT EXISTS idx_world_lobby_links_account "
    "ON world_lobby_links(account_bi, created_at DESC)",
    # Aggregate-only reach for public blog posts (migration 0094). Unique
    # counts use a fixed 64-register HLL per slug; no visitor identifier or
    # digest is stored. Referrers retain only the normalized external host.
    """CREATE TABLE IF NOT EXISTS blog_post_metrics (
        slug TEXT PRIMARY KEY,
        views INTEGER NOT NULL DEFAULT 0 CHECK (views >= 0),
        updated_at INTEGER NOT NULL DEFAULT 0 CHECK (updated_at >= 0))""",
    """CREATE TABLE IF NOT EXISTS blog_post_unique_hll (
        slug TEXT NOT NULL,
        register_id INTEGER NOT NULL
            CHECK (register_id >= 0 AND register_id < 64),
        rank INTEGER NOT NULL CHECK (rank >= 1 AND rank <= 251),
        PRIMARY KEY (slug, register_id)
    ) WITHOUT ROWID""",
    """CREATE TABLE IF NOT EXISTS blog_post_referrers (
        slug TEXT NOT NULL,
        host TEXT NOT NULL,
        visits INTEGER NOT NULL DEFAULT 0 CHECK (visits >= 0),
        last_ts INTEGER NOT NULL DEFAULT 0 CHECK (last_ts >= 0),
        PRIMARY KEY (slug, host)
    ) WITHOUT ROWID""",
    "CREATE INDEX IF NOT EXISTS idx_blog_post_referrers_rank "
    "ON blog_post_referrers(slug, visits DESC, last_ts DESC)",
    """CREATE TABLE IF NOT EXISTS world_build_board_items (
        item_key TEXT PRIMARY KEY,
        kind TEXT NOT NULL CHECK (kind IN ('task','issue')),
        owner TEXT NOT NULL DEFAULT '',
        repo TEXT NOT NULL DEFAULT '',
        issue_number INTEGER NOT NULL DEFAULT 0
            CHECK (issue_number >= 0),
        title TEXT NOT NULL DEFAULT '' CHECK (length(title) <= 160),
        priority INTEGER NOT NULL CHECK (priority >= 1 AND priority <= 64),
        updated_by_bi TEXT NOT NULL,
        updated_at INTEGER NOT NULL,
        completed_at INTEGER NOT NULL DEFAULT 0
            CHECK (completed_at >= 0),
        completed_by_bi TEXT NOT NULL DEFAULT '')""",
    "CREATE INDEX IF NOT EXISTS idx_world_build_board_priority "
    "ON world_build_board_items(priority, item_key)",
    """CREATE TABLE IF NOT EXISTS world_deploy_status (
        singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
        state TEXT NOT NULL
            CHECK (state IN ('idle', 'deploying', 'ready', 'failed')),
        revision TEXT NOT NULL DEFAULT '' CHECK (length(revision) <= 96),
        started_at INTEGER NOT NULL DEFAULT 0 CHECK (started_at >= 0),
        finished_at INTEGER NOT NULL DEFAULT 0 CHECK (finished_at >= 0))""",
    """CREATE TABLE IF NOT EXISTS world_qa_reviews (
        account_bi TEXT NOT NULL,
        item_key TEXT NOT NULL CHECK (length(item_key) BETWEEN 1 AND 80),
        verdict TEXT NOT NULL CHECK (verdict IN ('pass','fail','unsure')),
        reviewed_at INTEGER NOT NULL CHECK (reviewed_at >= 0),
        PRIMARY KEY (account_bi, item_key))""",
    "CREATE INDEX IF NOT EXISTS idx_world_qa_reviews_account_time "
    "ON world_qa_reviews(account_bi, reviewed_at DESC)",
    """CREATE TABLE IF NOT EXISTS world_qa_items (
        item_key TEXT PRIMARY KEY
            CHECK (length(item_key) BETWEEN 1 AND 80),
        title TEXT NOT NULL CHECK (length(title) BETWEEN 1 AND 160),
        how_to_test TEXT NOT NULL
            CHECK (length(how_to_test) BETWEEN 1 AND 720),
        source_key TEXT NOT NULL DEFAULT ''
            CHECK (length(source_key) <= 80),
        added_at INTEGER NOT NULL CHECK (added_at >= 0),
        active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1)))""",
    "CREATE INDEX IF NOT EXISTS idx_world_qa_items_active_time "
    "ON world_qa_items(active, added_at DESC)",
    # Organization bot secrets are shown exactly once. Only their keyed blind
    # index and encrypted metadata remain server-side, making listing and
    # revocation possible without creating a credential-recovery endpoint.
    """CREATE TABLE IF NOT EXISTS org_bot_tokens (
        token_id TEXT PRIMARY KEY,
        org_bi TEXT NOT NULL,
        secret_bi TEXT NOT NULL UNIQUE,
        provider TEXT NOT NULL
            CHECK (provider IN ('codex','claude-code')),
        data TEXT NOT NULL,
        created_by_bi TEXT NOT NULL,
        created_at INTEGER NOT NULL CHECK (created_at >= 0),
        last_used_at INTEGER NOT NULL DEFAULT 0 CHECK (last_used_at >= 0),
        expires_at INTEGER NOT NULL DEFAULT 0 CHECK (expires_at >= 0),
        revoked_at INTEGER NOT NULL DEFAULT 0 CHECK (revoked_at >= 0))""",
    "CREATE INDEX IF NOT EXISTS idx_org_bot_tokens_org "
    "ON org_bot_tokens(org_bi, revoked_at, created_at DESC)",
    # Metadata-only use history: no token secret, body, query string, network
    # address, or user agent is retained. The generic platform admin browser
    # can inspect the full append-only table while org admins receive only a
    # bounded preview through the purpose-built token endpoint.
    """CREATE TABLE IF NOT EXISTS org_bot_token_usage (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        token_id TEXT NOT NULL,
        org_bi TEXT NOT NULL,
        provider TEXT NOT NULL
            CHECK (provider IN ('codex','claude-code')),
        action TEXT NOT NULL CHECK (length(action) BETWEEN 1 AND 120),
        method TEXT NOT NULL
            CHECK (method IN ('GET','POST','PUT','PATCH','DELETE')),
        used_at INTEGER NOT NULL CHECK (used_at >= 0))""",
    "CREATE INDEX IF NOT EXISTS idx_org_bot_token_usage_org_time "
    "ON org_bot_token_usage(org_bi, used_at DESC, id DESC)",
    "CREATE INDEX IF NOT EXISTS idx_org_bot_token_usage_token_time "
    "ON org_bot_token_usage(token_id, used_at DESC, id DESC)",
    # Organization Discord connector policy.  The Worker secret used to call
    # Discord is never persisted here: this encrypted record contains only a
    # selected guild and bounded allowlist of public text-channel ids.
    """CREATE TABLE IF NOT EXISTS organization_discord_connectors (
        org_bi TEXT PRIMARY KEY,
        data TEXT NOT NULL,
        updated_by_bi TEXT NOT NULL,
        updated_at INTEGER NOT NULL CHECK (updated_at >= 0))""",
    "CREATE INDEX IF NOT EXISTS idx_organization_discord_connectors_updated "
    "ON organization_discord_connectors(updated_at DESC)",
    # A single durable, unassigned human setup task per organization.  It
    # deduplicates changing Discord setup states without storing the task text
    # or any credential outside organization_tasks' encrypted payload.
    """CREATE TABLE IF NOT EXISTS organization_discord_setup_tasks (
        org_bi TEXT PRIMARY KEY,
        task_id TEXT NOT NULL UNIQUE,
        created_at INTEGER NOT NULL CHECK (created_at >= 0),
        updated_at INTEGER NOT NULL CHECK (updated_at >= 0))""",
    # A bot's global guild membership is not organization consent. This
    # encrypted OAuth proof binds a single guild to an org before
    # any bot channel/message call is allowed. OAuth access/refresh tokens,
    # authorization codes, and raw callback state are never stored.
    """CREATE TABLE IF NOT EXISTS organization_discord_oauth_grants (
        org_bi TEXT PRIMARY KEY,
        data TEXT NOT NULL,
        verified_by_bi TEXT NOT NULL,
        verified_at INTEGER NOT NULL CHECK (verified_at >= 0),
        updated_at INTEGER NOT NULL CHECK (updated_at >= 0))""",
    "CREATE INDEX IF NOT EXISTS idx_organization_discord_oauth_grants_verified "
    "ON organization_discord_oauth_grants(verified_at DESC)",
    # The raw state and PKCE verifier live only inside this encrypted, ten
    # minute record. Its SHA-256 lookup digest makes the callback one-time
    # without persisting a reusable raw state value. org_bi is metadata only,
    # used to revoke pending attempts when an organization is deleted.
    """CREATE TABLE IF NOT EXISTS organization_discord_oauth_states (
        state_hash TEXT PRIMARY KEY,
        org_bi TEXT NOT NULL,
        data TEXT NOT NULL,
        created_at INTEGER NOT NULL CHECK (created_at >= 0),
        expires_at INTEGER NOT NULL CHECK (expires_at >= 0))""",
    "CREATE INDEX IF NOT EXISTS idx_organization_discord_oauth_states_org "
    "ON organization_discord_oauth_states(org_bi, expires_at)",
    # Single-row bookkeeping for ensure_schema's fast path: the fingerprint of
    # the DDL that has already been applied to this database. A cold isolate
    # reads this one row instead of replaying all ~90 statements above — the
    # full replay only happens when the fingerprint (i.e. this file) changed.
    """CREATE TABLE IF NOT EXISTS schema_meta (
        k TEXT PRIMARY KEY, v TEXT NOT NULL)""",
]
