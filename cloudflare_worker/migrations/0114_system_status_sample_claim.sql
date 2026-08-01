





CREATE TABLE IF NOT EXISTS system_status_sample_claim (
    minute_ts INTEGER PRIMARY KEY, claim TEXT NOT NULL,
    claimed_at INTEGER NOT NULL);
