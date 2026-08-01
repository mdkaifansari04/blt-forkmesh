




CREATE TABLE IF NOT EXISTS referral_stats (
    referrer_bi TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    clicks INTEGER NOT NULL DEFAULT 0,
    signups INTEGER NOT NULL DEFAULT 0,
    last_ts INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_referral_stats_rank
ON referral_stats(signups DESC, clicks DESC);
