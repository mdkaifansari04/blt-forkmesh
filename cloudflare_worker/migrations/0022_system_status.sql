










CREATE TABLE IF NOT EXISTS system_status_daily (
    day_ts   INTEGER NOT NULL,
    system   TEXT NOT NULL,
    checks   INTEGER NOT NULL DEFAULT 0,
    failures INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (day_ts, system)
);

CREATE INDEX IF NOT EXISTS idx_system_status_daily_day ON system_status_daily(day_ts);
