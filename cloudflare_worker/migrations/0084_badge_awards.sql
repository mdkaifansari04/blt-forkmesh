










CREATE TABLE IF NOT EXISTS badge_awards (
    badge_slug TEXT NOT NULL,
    account_bi TEXT NOT NULL,
    name TEXT NOT NULL,
    granted_by TEXT NOT NULL DEFAULT 'system',
    created_at INTEGER NOT NULL,
    PRIMARY KEY (badge_slug, account_bi));
CREATE INDEX IF NOT EXISTS idx_badge_awards_account ON badge_awards(account_bi);
