






CREATE TABLE IF NOT EXISTS feedback (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts INTEGER NOT NULL,
    source TEXT NOT NULL,
    vote TEXT NOT NULL,
    path TEXT NOT NULL,
    message TEXT,
    ip_hash TEXT,
    user_agent TEXT
);

CREATE INDEX IF NOT EXISTS idx_feedback_ts ON feedback(ts);
CREATE INDEX IF NOT EXISTS idx_feedback_source_vote ON feedback(source, vote, ts);
