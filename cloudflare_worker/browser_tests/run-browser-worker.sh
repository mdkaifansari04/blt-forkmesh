#!/usr/bin/env bash
set -euo pipefail

source ../pywrangler.sh
export FORKMESH_D1_LOCAL=1
export WRANGLER_NPM_SPEC="wrangler@4.42.1"
pywrangler dev \
  --config wrangler.browser-tests.toml \
  --ip 127.0.0.1 \
  --port 4179 \
  --persist-to .wrangler/direct-messages-v3 \
  --local
