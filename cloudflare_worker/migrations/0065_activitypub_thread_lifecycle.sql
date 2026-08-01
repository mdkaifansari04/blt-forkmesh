





ALTER TABLE ap_comments ADD COLUMN parent_remote_id_bi TEXT;
ALTER TABLE ap_comments ADD COLUMN lifecycle TEXT NOT NULL DEFAULT 'active'
    CHECK (lifecycle IN (
        'active', 'edited', 'tombstoned', 'moderated',
        'awaiting-redelivery'
    ));
CREATE INDEX IF NOT EXISTS idx_ap_comments_parent
    ON ap_comments(parent_remote_id_bi);
