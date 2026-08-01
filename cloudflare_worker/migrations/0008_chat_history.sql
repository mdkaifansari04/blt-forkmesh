






CREATE TABLE IF NOT EXISTS chat_history (
  room_key TEXT NOT NULL,
  msg_id   TEXT NOT NULL,
  ts       INTEGER NOT NULL,
  body     TEXT NOT NULL,
  PRIMARY KEY (room_key, msg_id)
);

CREATE INDEX IF NOT EXISTS idx_chat_history_room_ts ON chat_history(room_key, ts);
