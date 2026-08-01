















ALTER TABLE accounts ADD COLUMN enable_outreach INTEGER NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN enable_outreach INTEGER NOT NULL DEFAULT 0;
