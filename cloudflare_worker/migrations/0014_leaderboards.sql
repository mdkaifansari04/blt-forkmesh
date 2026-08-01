












CREATE TABLE IF NOT EXISTS repo_first_hosted (
    repo_bi TEXT PRIMARY KEY,
    ts INTEGER NOT NULL
);




CREATE TABLE IF NOT EXISTS contributor_activity (
    author_bi TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    issues INTEGER NOT NULL DEFAULT 0,
    pulls INTEGER NOT NULL DEFAULT 0,
    commits INTEGER NOT NULL DEFAULT 0,
    total INTEGER NOT NULL DEFAULT 0,
    last_ts INTEGER
);
CREATE INDEX IF NOT EXISTS idx_contributor_activity_total ON contributor_activity(total);






CREATE TABLE IF NOT EXISTS funds_received (
    scope TEXT NOT NULL,
    key TEXT NOT NULL,
    name TEXT,
    lamports INTEGER NOT NULL DEFAULT 0,
    last_ts INTEGER,
    PRIMARY KEY (scope, key)
);
