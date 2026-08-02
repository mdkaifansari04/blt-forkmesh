#!/usr/bin/env bash
set -euo pipefail

# Critical browser cases stub their API and WebSocket boundaries in Playwright;
# serve only the checked-in assets they exercise. Worker routing and Durable
# Object behavior are covered by the separate bounded Python contracts.
cd ../public
exec python3 -m http.server 4179 --bind 127.0.0.1
