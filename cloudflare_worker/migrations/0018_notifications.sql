



CREATE TABLE IF NOT EXISTS notifications (
  dedupe_bi    TEXT PRIMARY KEY,
  recipient_bi TEXT NOT NULL,
  ts           INTEGER NOT NULL,
  read_at      INTEGER NOT NULL DEFAULT 0,
  data         TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_notifications_recipient_ts
  ON notifications(recipient_bi, ts);
CREATE INDEX IF NOT EXISTS idx_notifications_unread
  ON notifications(recipient_bi, read_at, ts);
