







CREATE TABLE IF NOT EXISTS inbox_drain_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts INTEGER NOT NULL,
    repo_bi TEXT NOT NULL,
    kind TEXT NOT NULL,
    count INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_inbox_drain_log_ts ON inbox_drain_log(ts);
CREATE INDEX IF NOT EXISTS idx_inbox_drain_log_repo ON inbox_drain_log(repo_bi);
