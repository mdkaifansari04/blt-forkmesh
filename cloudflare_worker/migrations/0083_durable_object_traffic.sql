




CREATE TABLE IF NOT EXISTS durable_object_traffic (
    binding TEXT PRIMARY KEY,
    bytes_in INTEGER NOT NULL DEFAULT 0 CHECK (bytes_in >= 0),
    bytes_out INTEGER NOT NULL DEFAULT 0 CHECK (bytes_out >= 0),
    messages INTEGER NOT NULL DEFAULT 0 CHECK (messages >= 0),
    updated_at INTEGER NOT NULL DEFAULT 0 CHECK (updated_at >= 0));
