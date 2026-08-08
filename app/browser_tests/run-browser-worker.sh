#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
if [ "${FORKMESH_BROWSER_PRESTAGED:-0}" != "1" ]; then
  python3 ../tools/build_dashboard_assets.py >/dev/null
  python3 ../tools/build_site_assets.py cutover >/dev/null
fi

if [ "${FORKMESH_BROWSER_FULL:-0}" = "1" ]; then
  source ../pywrangler.sh
  export FORKMESH_D1_LOCAL=1
  pywrangler dev \
    --config wrangler.browser-tests.toml \
    --ip 127.0.0.1 \
    --port 4179 \
    --persist-to .wrangler/browser-tests-v4 \
    --local
  exit $?
fi

cd ../dist
exec python3 -m http.server 4179 --bind 127.0.0.1
