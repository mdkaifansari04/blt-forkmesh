





CREATE TABLE IF NOT EXISTS thread_subscriptions (
  thread_bi     TEXT NOT NULL,
  subscriber_bi TEXT NOT NULL,
  ts            INTEGER NOT NULL,
  data          TEXT NOT NULL,
  PRIMARY KEY (thread_bi, subscriber_bi)
);

CREATE INDEX IF NOT EXISTS idx_thread_subscriptions_thread
  ON thread_subscriptions(thread_bi);
