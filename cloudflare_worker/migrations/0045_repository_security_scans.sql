






CREATE TABLE IF NOT EXISTS repo_security_scans (
    repo_bi TEXT NOT NULL,
    scan_id TEXT NOT NULL,
    scanned_at INTEGER NOT NULL,
    received_at INTEGER NOT NULL,
    data TEXT NOT NULL,
    PRIMARY KEY (repo_bi, scan_id)
);
CREATE INDEX IF NOT EXISTS idx_repo_security_scans_latest
    ON repo_security_scans(repo_bi, scanned_at DESC, received_at DESC);
