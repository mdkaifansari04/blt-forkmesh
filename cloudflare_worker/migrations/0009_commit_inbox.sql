






CREATE TABLE IF NOT EXISTS commit_inbox (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    repo_bi TEXT NOT NULL,
    data TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_commit_inbox_repo ON commit_inbox(repo_bi);
