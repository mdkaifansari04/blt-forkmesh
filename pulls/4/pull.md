---
schema: forkmesh-pull-v1
number: 4
title: feat(public): add launch pricing page
base: kaif/public-landing-refresh
head: kaif/public-pricing-launch
status: open
ts: 1783173747712
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: kivCfBmB2JYN3cvO5LuHNz6E1FUH-3AXe_PrkM0-ZuLs890oUuZD5XakMq9A8IWLZzjSKyclR87s09m-xr5yDw
---

Adds the launch pricing page for Core, Pro, and Enterprise, with Pro free during launch and the future five-dollar plan visible.

Test plan:
- uv run --with pytest python -m py_compile src/entry.py src/static_routes.py
- uv run --with pytest python -m pytest tests/test_pricing_page_frontend.py tests/test_static_route_canonicalization.py -q
