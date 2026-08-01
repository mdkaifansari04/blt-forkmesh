












CREATE TABLE IF NOT EXISTS release_downloads (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    repo_bi TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    ts INTEGER NOT NULL
);

ALTER TABLE release_downloads ADD COLUMN ua TEXT;
