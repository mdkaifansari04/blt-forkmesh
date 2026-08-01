



CREATE TABLE IF NOT EXISTS organization_discord_connectors (
  org_bi          TEXT PRIMARY KEY,
  data            TEXT NOT NULL,
  updated_by_bi   TEXT NOT NULL,
  updated_at      INTEGER NOT NULL CHECK (updated_at >= 0)
);

CREATE INDEX IF NOT EXISTS idx_organization_discord_connectors_updated
  ON organization_discord_connectors(updated_at DESC);




CREATE TABLE IF NOT EXISTS organization_discord_setup_tasks (
  org_bi          TEXT PRIMARY KEY,
  task_id         TEXT NOT NULL UNIQUE,
  created_at      INTEGER NOT NULL CHECK (created_at >= 0),
  updated_at      INTEGER NOT NULL CHECK (updated_at >= 0)
);




CREATE TABLE IF NOT EXISTS organization_discord_oauth_grants (
  org_bi          TEXT PRIMARY KEY,
  data            TEXT NOT NULL,
  verified_by_bi  TEXT NOT NULL,
  verified_at     INTEGER NOT NULL CHECK (verified_at >= 0),
  updated_at      INTEGER NOT NULL CHECK (updated_at >= 0)
);

CREATE INDEX IF NOT EXISTS idx_organization_discord_oauth_grants_verified
  ON organization_discord_oauth_grants(verified_at DESC);





CREATE TABLE IF NOT EXISTS organization_discord_oauth_states (
  state_hash      TEXT PRIMARY KEY,
  org_bi          TEXT NOT NULL,
  data            TEXT NOT NULL,
  created_at      INTEGER NOT NULL CHECK (created_at >= 0),
  expires_at      INTEGER NOT NULL CHECK (expires_at >= 0)
);

CREATE INDEX IF NOT EXISTS idx_organization_discord_oauth_states_org
  ON organization_discord_oauth_states(org_bi, expires_at);
