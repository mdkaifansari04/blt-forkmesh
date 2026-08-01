
















ALTER TABLE accounts ADD COLUMN ip_bi TEXT;
CREATE INDEX IF NOT EXISTS idx_accounts_ip ON accounts(ip_bi);
