-- Email invitations to an organization. An org owner/admin invites an address;
-- the recipient follows a single-use signed link and is joined to org_members
-- at the stored role. Unlike orgs/org_members - public roster data,
-- deliberately plaintext (see 0038_orgs_teams.sql) - an invitee's address is
-- PII belonging to someone who may never accept, so this follows the
-- contributor_invitations shape (0044): a blind index for lookup, dedupe and
-- rate limiting, everything else inside the encrypted blob. Only
-- sha256(token) is retained in the blob; the raw token exists in the
-- delivered email and nowhere else.
CREATE TABLE IF NOT EXISTS org_invitations (
    id TEXT PRIMARY KEY,
    org_bi TEXT NOT NULL,
    email_bi TEXT NOT NULL,
    inviter_bi TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'member',
    status TEXT NOT NULL DEFAULT 'pending',
    data TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    sent_at INTEGER NOT NULL DEFAULT 0,
    accepted_at INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_org_invitations_org
    ON org_invitations(org_bi, status, created_at);
CREATE INDEX IF NOT EXISTS idx_org_invitations_email
    ON org_invitations(email_bi, created_at);
-- One outstanding invite per (org, address): the concurrency boundary, the
-- same job contributor_invitations delegates to its provenance trigger.
-- History rows (accepted/revoked/expired) never block a fresh invite.
CREATE UNIQUE INDEX IF NOT EXISTS idx_org_invitations_pending
    ON org_invitations(org_bi, email_bi) WHERE status='pending';
