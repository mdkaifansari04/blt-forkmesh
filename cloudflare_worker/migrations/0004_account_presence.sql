





CREATE TABLE IF NOT EXISTS account_presence (
  name_bi TEXT PRIMARY KEY,
  ts      INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_account_presence_ts ON account_presence(ts);
