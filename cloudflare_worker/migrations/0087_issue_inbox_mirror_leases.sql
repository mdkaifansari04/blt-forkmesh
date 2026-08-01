



ALTER TABLE issue_inbox
  ADD COLUMN claimed_by_bi TEXT NOT NULL DEFAULT '';
ALTER TABLE issue_inbox
  ADD COLUMN claim_expires_at INTEGER NOT NULL DEFAULT 0;
CREATE INDEX IF NOT EXISTS idx_issue_inbox_claim
  ON issue_inbox(repo_bi, claim_expires_at, claimed_by_bi);
