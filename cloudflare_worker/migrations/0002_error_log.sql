





CREATE TABLE IF NOT EXISTS error_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts INTEGER NOT NULL,
    status INTEGER NOT NULL,
    method TEXT,
    path TEXT,
    message TEXT,
    ray TEXT
);

CREATE INDEX IF NOT EXISTS idx_error_log_ts ON error_log(ts);
