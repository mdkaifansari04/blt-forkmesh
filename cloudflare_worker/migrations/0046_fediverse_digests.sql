




CREATE TABLE IF NOT EXISTS ap_digest_queues (
    scope_bi TEXT PRIMARY KEY,
    next_ts INTEGER NOT NULL,
    last_published_at INTEGER NOT NULL DEFAULT 0,
    data TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_ap_digest_queues_due
    ON ap_digest_queues(next_ts, updated_at);




CREATE TABLE IF NOT EXISTS ap_org_digest_settings (
    org_bi TEXT PRIMARY KEY,
    data TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);




ALTER TABLE ap_outbox ADD COLUMN dedupe_bi TEXT;
CREATE UNIQUE INDEX IF NOT EXISTS idx_ap_outbox_dedupe
    ON ap_outbox(dedupe_bi);
