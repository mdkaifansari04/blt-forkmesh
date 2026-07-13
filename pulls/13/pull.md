---
schema: forkmesh-pull-v1
number: 13
title: fix: add public light theme coverage to marketing pages
base: main
head: api-pr/20260714000330/public-light-theme
status: open
ts: 1783967695350
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: LQQ4nqqjwhqJK6rV0SjlgL5wgB_VB6Ywi9AD8lZyYS0AXCM9PpcpmKxPCRN02sZPModJSuIUga34qdyfrKYICQ
---

Summary:
- Standardize public navbar chrome and footer/content surfaces across public pages.
- Add light-theme CSS families for auth, mesh, pricing, docs, and feature article pages.
- Share feature article theme styles and preserve dark-mode link colors.
- Add regression coverage for public theme controls and page-family contracts.

Verification:
- cd cloudflare_worker && python3 -m py_compile src/entry.py
- /Users/mdkaifansari04/code/forkmesh-root/formesh/cloudflare_worker/.venv/bin/pytest -q tests/test_public_light_theme.py tests/test_docs_frontend.py tests/test_shared_header_frontend.py tests/test_network_architecture_frontend.py tests/test_press_page_frontend.py tests/test_dashboard_landing_migration.py (120 passed)

Notes:
- Excludes docs/superpowers development plan/spec Markdown files.
- Excludes cloudflare_worker/wrangler.toml.
- Rebuilt from cached origin/main; git fetch origin main failed with ForkMesh mirror integrity check, so live source freshness could not be proven.
