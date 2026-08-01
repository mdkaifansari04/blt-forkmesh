











ALTER TABLE accounts ADD COLUMN email_bi TEXT;
CREATE INDEX IF NOT EXISTS idx_accounts_email ON accounts(email_bi);
