


CREATE TABLE IF NOT EXISTS repo_stars (
    repo_bi TEXT NOT NULL,
    account_bi TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (repo_bi, account_bi)
);

CREATE INDEX IF NOT EXISTS idx_repo_stars_repo
    ON repo_stars(repo_bi, created_at);
CREATE INDEX IF NOT EXISTS idx_repo_stars_account
    ON repo_stars(account_bi, created_at);
