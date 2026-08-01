


CREATE TABLE IF NOT EXISTS account_devices (
  device_bi TEXT PRIMARY KEY,
  account_bi TEXT NOT NULL,
  pubkey TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT 'desktop_node',
  label TEXT,
  capabilities TEXT NOT NULL DEFAULT '',
  enabled INTEGER NOT NULL DEFAULT 1,
  created_at INTEGER NOT NULL,
  last_seen INTEGER NOT NULL DEFAULT 0,
  revoked_at INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_account_devices_account ON account_devices(account_bi);
CREATE INDEX IF NOT EXISTS idx_account_devices_pubkey ON account_devices(pubkey);
