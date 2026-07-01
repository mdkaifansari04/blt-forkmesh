-- Central donation fund (issue #308).
-- A single worker-custodied Solana wallet that anyone can donate to. A cron
-- sweeps its whole balance out to the currently-online nodes once an hour, so
-- one donation address fans out to every node keeping the network alive. The
-- encrypted blob holds the deposit address, its Ed25519 seed, and the last
-- distribution's timestamp/signature. id is always 1. Main relay only.

CREATE TABLE IF NOT EXISTS central_fund (
  id   INTEGER PRIMARY KEY, -- always 1
  data TEXT NOT NULL        -- AES-GCM encrypted {address, secret, last_distribution_*}
);
