









CREATE TABLE IF NOT EXISTS repo_shares (
    repo_bi TEXT NOT NULL,
    grantee_bi TEXT NOT NULL,
    data TEXT NOT NULL,
    ts INTEGER NOT NULL,
    PRIMARY KEY (repo_bi, grantee_bi)
);
CREATE INDEX IF NOT EXISTS idx_repo_shares_grantee ON repo_shares(grantee_bi);
