---
schema: forkmesh-pull-v1
number: 3
title: feat(public): refresh about and network pages
base: kaif/public-landing-refresh
head: kaif/public-about-network-refresh
status: merged
ts: 1783173747679
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: PFsFzNo0og2boToNXPaW5FhQGeDolpFtrhcE22TZlp7DchJqPEbSjG8TvdwyCDHKAkWwF5sLXVU-PURMJOp1Bg
---

Redesigns the About page and replaces the Network page with the command-center operational surface.

Test plan:
- uv run --with pytest python -m py_compile src/entry.py src/static_routes.py
- uv run --with pytest python -m pytest tests/test_about_page_frontend.py tests/test_network_architecture_frontend.py tests/test_static_route_canonicalization.py -q
