#!/usr/bin/env bash





set -euo pipefail
cd "$(dirname "$0")"
. ./pywrangler.sh

DB="${FORKMESH_D1_NAME:-forkmesh}"




if grep -RinE \
    'DELETE[[:space:]]+FROM[[:space:]]+ap_followers|DROP[[:space:]]+TABLE([[:space:]]+IF[[:space:]]+EXISTS)?[[:space:]]+ap_followers' \
    migrations; then
  echo "migrate.sh: refusing a migration that deletes the ActivityPub follower graph." >&2
  exit 1
fi



if grep -q "REPLACE_WITH_D1_DATABASE_ID" wrangler.toml; then
  echo "migrate.sh: D1 not configured yet (placeholder database_id) — skipping."
  echo "  Run: uvx --from workers-py pywrangler d1 create $DB, then paste the id."
  exit 0
fi



SCOPE="--remote"
[ "${FORKMESH_D1_LOCAL:-0}" = "1" ] && SCOPE="--local"

echo "migrate.sh: applying D1 migrations to '$DB' ($SCOPE)"







log="$(mktemp)"
trap 'rm -f "$log"' EXIT
set +e
pywrangler d1 migrations apply "$DB" "$SCOPE" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -e

if [ "$status" -ne 0 ]; then





  if grep -qE 'code: (9109|10000)|Invalid access token|Authentication error' "$log"; then
    db_id="$(sed -n 's/^[[:space:]]*database_id[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' wrangler.toml | head -1)"
    {
      echo ""
      echo "migrate.sh: Cloudflare rejected the remote D1 migration as UNAUTHENTICATED."
      echo "  CLOUDFLARE_API_TOKEN (from cloudflare_worker/.env.production) is missing, expired,"
      echo "  lacks the 'D1: Edit' permission, or belongs to a DIFFERENT Cloudflare account than"
      echo "  the one that owns D1 database '$DB'${db_id:+ (id $db_id)}. A wrong-account token is"
      echo "  the classic cause of this failure (the 'wrangler ... (update available)' banner above"
      echo "  is just a version notice, not the error)."
      echo "  Fix: set a CLOUDFLARE_API_TOKEN that (1) has Workers Scripts: Edit AND D1: Edit, and"
      echo "  (2) is for the SAME account as CLOUDFLARE_ACCOUNT_ID — the account that owns the"
      echo "  database_id in wrangler.toml — then redeploy."
      echo "  To migrate the LOCAL dev DB instead (no Cloudflare auth needed): FORKMESH_D1_LOCAL=1 ./migrate.sh"
    } >&2
  fi
  exit "$status"
fi
echo "migrate.sh: migrations applied."
