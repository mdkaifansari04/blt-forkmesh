







CREATE TABLE IF NOT EXISTS users (
  user_bi  TEXT PRIMARY KEY,
  data     TEXT NOT NULL,
  email_bi TEXT,
  username TEXT,
  is_admin INTEGER NOT NULL DEFAULT 0,
  ip_bi    TEXT
);
CREATE INDEX IF NOT EXISTS idx_users_email ON users(email_bi);
CREATE INDEX IF NOT EXISTS idx_users_ip ON users(ip_bi);

CREATE TABLE IF NOT EXISTS nodes (
  node_bi   TEXT PRIMARY KEY,
  user_bi   TEXT,
  pubkey    TEXT,
  data      TEXT NOT NULL,
  name      TEXT,
  last_seen INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_nodes_user ON nodes(user_bi);
CREATE INDEX IF NOT EXISTS idx_nodes_pubkey ON nodes(pubkey);
