










DROP TABLE IF EXISTS accounts;
DROP TABLE IF EXISTS repositories;
DROP TABLE IF EXISTS issue_inbox;
DROP TABLE IF EXISTS pull_inbox;



CREATE TABLE IF NOT EXISTS accounts (
  name_bi TEXT PRIMARY KEY,
  data    TEXT NOT NULL
);


CREATE TABLE IF NOT EXISTS repositories (
  key_bi   TEXT PRIMARY KEY,
  owner_bi TEXT NOT NULL,
  data     TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_repos_owner ON repositories(owner_bi);


CREATE TABLE IF NOT EXISTS issue_inbox (
  id      INTEGER PRIMARY KEY AUTOINCREMENT,
  repo_bi TEXT NOT NULL,
  data    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_issue_inbox_repo ON issue_inbox(repo_bi);


CREATE TABLE IF NOT EXISTS pull_inbox (
  id      INTEGER PRIMARY KEY AUTOINCREMENT,
  repo_bi TEXT NOT NULL,
  data    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_pull_inbox_repo ON pull_inbox(repo_bi);
