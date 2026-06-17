#!/usr/bin/env bash
# Applies the ForkMesh D1 migrations. Hooked into wrangler.toml as the [build]
# command, so it runs automatically before every `pywrangler deploy` (and dev).
# Uses D1's native migration tracking (`d1 migrations apply`), so each numbered
# file in migrations/ runs exactly once per database — that lets a migration
# do one-time, non-idempotent things (e.g. dropping a superseded table).
set -euo pipefail
cd "$(dirname "$0")"

DB="${FORKMESH_D1_NAME:-forkmesh}"

# Nothing to do until a real D1 database is wired up: skip (don't fail the
# build) while wrangler.toml still has the placeholder id.
if grep -q "REPLACE_WITH_D1_DATABASE_ID" wrangler.toml; then
  echo "migrate.sh: D1 not configured yet (placeholder database_id) — skipping."
  echo "  Run: uvx --from workers-py pywrangler d1 create $DB, then paste the id."
  exit 0
fi

# Use whichever wrangler is available (this is a Python worker → pywrangler).
if command -v pywrangler >/dev/null 2>&1; then
  WRANGLER=(pywrangler)
elif command -v wrangler >/dev/null 2>&1; then
  WRANGLER=(wrangler)
else
  WRANGLER=(uvx --from workers-py pywrangler)
fi

# Target the deployed (remote) D1 by default; set FORKMESH_D1_LOCAL=1 to apply
# to the local dev database instead.
SCOPE="--remote"
[ "${FORKMESH_D1_LOCAL:-0}" = "1" ] && SCOPE="--local"

echo "migrate.sh: applying D1 migrations to '$DB' ($SCOPE)"
# Non-interactive (build subprocess): wrangler auto-confirms when stdout is not
# a TTY. Each migration is tracked in d1_migrations and applied at most once.
"${WRANGLER[@]}" d1 migrations apply "$DB" "$SCOPE"
echo "migrate.sh: migrations applied."
