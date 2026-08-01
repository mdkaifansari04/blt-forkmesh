



CREATE TABLE IF NOT EXISTS repo_media (
    repo_bi TEXT NOT NULL, kind TEXT NOT NULL,
    data TEXT NOT NULL, updated_at INTEGER NOT NULL,
    PRIMARY KEY (repo_bi, kind));
