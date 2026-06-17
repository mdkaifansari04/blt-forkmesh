#!/usr/bin/env bash
# Applies the ForkMesh D1 migrations. Hooked into wrangler.toml as the [build]
# command, so it runs automatically before every `pywrangler deploy` (and dev).
# Each file in migrations/ is applied in filename order; they are idempotent
# (CREATE ... IF NOT EXISTS), so re-running on every deploy is safe.
set -euo pipefail
cd "$(dirname "$0")"

DB="${FORKMESH_D1_NAME:-forkmesh}"
MIGRATIONS_DIR="migrations"

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

shopt -s nullglob
migrations=("$MIGRATIONS_DIR"/*.sql)
if [ ${#migrations[@]} -eq 0 ]; then
  echo "migrate.sh: no migrations found in $MIGRATIONS_DIR/ — nothing to do."
  exit 0
fi

for file in "${migrations[@]}"; do  # glob expands in sorted (numeric) order
  echo "migrate.sh: applying $file to D1 '$DB' ($SCOPE)"
  "${WRANGLER[@]}" d1 execute "$DB" --file "$file" "$SCOPE"
done
echo "migrate.sh: ${#migrations[@]} migration(s) applied."
