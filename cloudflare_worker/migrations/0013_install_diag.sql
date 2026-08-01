










CREATE TABLE IF NOT EXISTS install_diag (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts INTEGER NOT NULL,
    run TEXT NOT NULL,
    step TEXT NOT NULL,
    ok INTEGER NOT NULL,
    os TEXT,
    arch TEXT,
    pm TEXT,
    distro TEXT,
    version TEXT,
    detail TEXT
);

CREATE INDEX IF NOT EXISTS idx_install_diag_ts ON install_diag(ts);
CREATE INDEX IF NOT EXISTS idx_install_diag_run ON install_diag(run);
