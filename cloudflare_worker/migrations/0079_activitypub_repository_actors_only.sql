-- Keep the relay's secure-fetch signing material outside the public local
-- actor inventory. The encrypted key blob can move directly without exposing
-- its private component.
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

DELETE FROM ap_followers
WHERE actor_bi IN (
    SELECT actor_bi FROM ap_actors WHERE kind <> 'repo'
);
DELETE FROM ap_objects
WHERE actor_bi IN (
    SELECT actor_bi FROM ap_actors WHERE kind <> 'repo'
);
DELETE FROM ap_actors WHERE kind <> 'repo';
