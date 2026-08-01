



ALTER TABLE mirror_https_endpoints
  ADD COLUMN forkmesh_operations_json TEXT NOT NULL DEFAULT '[]';
