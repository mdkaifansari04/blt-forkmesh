









CREATE TABLE IF NOT EXISTS system_status_hourly (
    hour_ts  INTEGER NOT NULL,
    system   TEXT NOT NULL,
    checks   INTEGER NOT NULL DEFAULT 0,
    failures INTEGER NOT NULL DEFAULT 0,
    reason   TEXT,
    PRIMARY KEY (hour_ts, system)
);

CREATE INDEX IF NOT EXISTS idx_system_status_hourly_hour ON system_status_hourly(hour_ts);
