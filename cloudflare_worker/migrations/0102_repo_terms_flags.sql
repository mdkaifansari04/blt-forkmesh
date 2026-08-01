




CREATE TABLE IF NOT EXISTS repo_terms_flags (
    repo_bi TEXT PRIMARY KEY,
    active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),
    category TEXT NOT NULL CHECK (
        category IN ('spam', 'malware', 'harassment', 'illegal', 'other')
    ),
    data TEXT NOT NULL,
    updated_by_bi TEXT NOT NULL,
    updated_at INTEGER NOT NULL CHECK (updated_at > 0)
);

CREATE INDEX IF NOT EXISTS idx_repo_terms_flags_active
ON repo_terms_flags(active, updated_at DESC);
