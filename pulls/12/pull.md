---
schema: forkmesh-pull-v1
number: 12
title: fix: bootstrap release download migration for local D1
base: main
head: api-pr/20260714000330/local-migration-bootstrap
status: open
ts: 1783967695243
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: 6G4hz0t9gqMAKwOV-DXI5jtETeyjQrfrj4Xxiyp57_3ro9QjklC01BkxzAiwLWlRFOenab2CoDXxIOz7zGWkAQ
---

Summary:
- Bootstrap release_downloads inside migration 0034 before adding ua so fresh local D1 databases can apply the migration.
- Ignore the local .wrangler cache directory.

Verification:
- cd cloudflare_worker && python3 -m py_compile src/entry.py
- /Users/mdkaifansari04/code/forkmesh-root/formesh/cloudflare_worker/.venv/bin/pytest -q tests/test_local_migrations.py (1 passed)

Notes:
- Excludes cloudflare_worker/wrangler.toml and the local wrangler-command test because the config contains local dev-only values.
- Rebuilt from cached origin/main; git fetch origin main failed with ForkMesh mirror integrity check, so live source freshness could not be proven.
