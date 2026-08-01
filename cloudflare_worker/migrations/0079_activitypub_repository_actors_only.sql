


CREATE TABLE IF NOT EXISTS ap_service_keys (
    key_name TEXT PRIMARY KEY,
    pubkey_pem TEXT NOT NULL,
    data TEXT NOT NULL,
    created_at INTEGER NOT NULL
);

INSERT INTO ap_service_keys (key_name, pubkey_pem, data, created_at)
SELECT 'instance', pubkey_pem, data, created_at
FROM ap_actors
WHERE kind = 'instance'
ON CONFLICT(key_name) DO NOTHING;
