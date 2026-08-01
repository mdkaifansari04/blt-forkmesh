

















DELETE FROM accounts
WHERE email_bi IS NOT NULL AND email_bi <> ''
  AND rowid NOT IN (
    SELECT MIN(rowid) FROM accounts
    WHERE email_bi IS NOT NULL AND email_bi <> ''
    GROUP BY email_bi
  );


DELETE FROM users
WHERE email_bi IS NOT NULL AND email_bi <> ''
  AND rowid NOT IN (
    SELECT MIN(rowid) FROM users
    WHERE email_bi IS NOT NULL AND email_bi <> ''
    GROUP BY email_bi
  );



DROP INDEX IF EXISTS idx_accounts_email;
CREATE UNIQUE INDEX idx_accounts_email ON accounts(email_bi)
  WHERE email_bi IS NOT NULL AND email_bi <> '';

DROP INDEX IF EXISTS idx_users_email;
CREATE UNIQUE INDEX idx_users_email ON users(email_bi)
  WHERE email_bi IS NOT NULL AND email_bi <> '';
