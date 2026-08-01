



CREATE TABLE IF NOT EXISTS site_referrers (
    host TEXT PRIMARY KEY,
    visits INTEGER NOT NULL DEFAULT 0,
    first_ts INTEGER NOT NULL DEFAULT 0,
    last_ts INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_site_referrers_rank
ON site_referrers(visits DESC, last_ts DESC);
