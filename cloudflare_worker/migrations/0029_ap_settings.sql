


CREATE TABLE IF NOT EXISTS ap_settings (
    k TEXT PRIMARY KEY, v TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS ap_blocked_domains (
    domain TEXT PRIMARY KEY, added_by TEXT, added_at INTEGER NOT NULL);
