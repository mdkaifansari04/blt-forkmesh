






CREATE INDEX IF NOT EXISTS idx_private_mirror_route_access
    ON private_mirror_routes(opaque_replica_id, active, key_epoch, repo_bi);
