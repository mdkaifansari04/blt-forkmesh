---
schema: forkmesh-pull-v1
number: 2
title: feat(public): refresh landing page and brand typography
base: kaif/public-routes-shared-chrome
head: kaif/public-landing-refresh
status: open
ts: 1783173747642
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: POzlc1yvUry5zRjVhLseibPigFgD-naPa73mT9Ctn8_1e-LJ0oCb-PKMMYrfMQFFFtNEmW4SDKo6sIhUIc2CDQ
---

Refreshes the public landing page, visual system, bundled brand fonts, and compressed landing hero video asset.

Test plan:
- uv run --with pytest python -m py_compile src/entry.py src/static_routes.py
- uv run --with pytest python -m pytest tests/test_landing_typography.py tests/test_public_logo.py -q
