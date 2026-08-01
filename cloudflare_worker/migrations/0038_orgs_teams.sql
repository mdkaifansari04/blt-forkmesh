







CREATE TABLE IF NOT EXISTS orgs (
    org_bi TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    data TEXT NOT NULL,
    created_at INTEGER NOT NULL);



CREATE TABLE IF NOT EXISTS org_members (
    org_bi TEXT NOT NULL,
    member_bi TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'member',
    name TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (org_bi, member_bi));
CREATE INDEX IF NOT EXISTS idx_org_members_member ON org_members(member_bi);



CREATE TABLE IF NOT EXISTS org_teams (
    org_bi TEXT NOT NULL,
    team TEXT NOT NULL,
    permission TEXT NOT NULL DEFAULT 'read',
    created_at INTEGER NOT NULL,
    PRIMARY KEY (org_bi, team));

CREATE TABLE IF NOT EXISTS org_team_members (
    org_bi TEXT NOT NULL,
    team TEXT NOT NULL,
    member_bi TEXT NOT NULL,
    name TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (org_bi, team, member_bi));





CREATE TABLE IF NOT EXISTS org_repos (
    org_bi TEXT NOT NULL,
    repo TEXT NOT NULL,
    node_owner TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (org_bi, repo));
CREATE INDEX IF NOT EXISTS idx_org_repos_node ON org_repos(node_owner, repo);
