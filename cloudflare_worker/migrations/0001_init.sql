-- ForkMesh D1 schema — initial tables.
-- Applied automatically by ../migrate.sh (hooked into wrangler.toml [build]).
-- The worker also creates these lazily (ensure_schema in src/entry.py).
-- Idempotent: every statement is CREATE ... IF NOT EXISTS, so re-running is safe.
-- Add later changes as new numbered files in this folder (0002_*.sql, ...).

CREATE TABLE IF NOT EXISTS repositories (
  key          TEXT PRIMARY KEY,   -- maintainer:owner/name
  owner        TEXT NOT NULL,
  name         TEXT NOT NULL,
  description  TEXT,
  clone_url    TEXT,
  bch          TEXT,
  channel      TEXT,
  hosted_since TEXT,
  last_sync    TEXT,
  updated_at   TEXT,
  source       TEXT,
  maintainer   TEXT NOT NULL,
  signature    TEXT
);
CREATE INDEX IF NOT EXISTS idx_repos_updated ON repositories(updated_at DESC);

CREATE TABLE IF NOT EXISTS accounts (
  name       TEXT PRIMARY KEY,
  pubkey     TEXT NOT NULL,
  email_enc  TEXT,
  created_at INTEGER
);

CREATE TABLE IF NOT EXISTS issue_inbox (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  owner        TEXT NOT NULL,
  repo         TEXT NOT NULL,
  number       INTEGER,
  title_if_new TEXT,
  event        TEXT NOT NULL,   -- JSON-encoded signed issue event
  submitter    TEXT,
  submitted_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_issue_inbox_repo ON issue_inbox(owner, repo);

CREATE TABLE IF NOT EXISTS pull_inbox (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  owner        TEXT NOT NULL,
  repo         TEXT NOT NULL,
  pull         TEXT NOT NULL,   -- JSON-encoded signed pull request
  submitter    TEXT,
  submitted_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_pull_inbox_repo ON pull_inbox(owner, repo);
