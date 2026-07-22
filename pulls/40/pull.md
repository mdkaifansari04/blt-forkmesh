---
schema: forkmesh-pull-v1
number: 40
title: Update homepage hero copy and local D1 dev migrations
base: main
head: fix/hero-title
status: open
ts: 1784743043655
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: Ojp0iF7QQkqD-ChLfV8fxYdOPOwyH-DJXPjIhzb_dPcK3_w8uKKLdJjPTR_3vgPDjH7PGTiTCeNlVoTF1s4VCg
---

This signed patch PR was generated from the rebased `fix/hero-title` branch.

Includes:
- Updates the public homepage hero title/copy for team chat, AI agents, Git hosting, bounties, and desktop-node mirroring.
- Fixes dashboard account/name display copy across dashboard pages.
- Adds local D1 migration handling for Wrangler dev plus focused regression tests.

Verification run locally:
- git diff --check origin/main..HEAD
- python3.11 -m py_compile src/entry.py tests/test_dashboard_landing_migration.py tests/test_migrate_script.py
- bash -n migrate.sh
- Direct Python execution of the changed landing and migrate-script assertion tests.

Note: pytest was not installed in this checkout, so the changed pytest-style assertions were executed directly without installing dependencies.
