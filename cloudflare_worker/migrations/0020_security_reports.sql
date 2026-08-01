





CREATE TABLE IF NOT EXISTS security_reports (
  id   INTEGER PRIMARY KEY AUTOINCREMENT,
  ts   INTEGER NOT NULL,
  data TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_security_reports_ts ON security_reports(ts);
