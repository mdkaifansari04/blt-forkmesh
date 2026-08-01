






CREATE TABLE IF NOT EXISTS online_hourly (
  hour_ts      INTEGER PRIMARY KEY,
  node_minutes INTEGER NOT NULL DEFAULT 0
);
