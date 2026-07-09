---
schema: forkmesh-pull-v1
number: 23
title: docs: clarify ForkMesh pillar 1 trust model
base: main
head: feat/feature-addon
status: open
ts: 1783596911208
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: -GkdfHwRVWZ8OFU5sBemxEnqaLInAK_hE71vqtY13eW1WeStSgW846_BlQ6thhUDMGdILzEAULFIZRTx1FRBDQ
---

Implements Pillar 1 clarity/trust positioning for ForkMesh.

Summary:
- rewrites the README pitch and current-state language
- adds Git compatibility and GitHub/GitLab mirror onboarding guidance
- clarifies mainnode/Worker centralization boundaries and non-canonical Git storage
- adds money/Solana experimental status and MVP threat-model copy
- adds public docs sections for compatibility, centralization, money, threat model, comparison, and GitHub refugee flow
- updates homepage cards/FAQ/pricing copy without adding new JS behavior
- adds regression tests for Pillar 1 public/docs copy and /docs route canonicalization

Validation:
- python3 -m py_compile src/entry.py src/static_routes.py tests/test_docs_frontend.py tests/test_static_route_canonicalization.py tests/test_pillar1_public_copy.py tests/test_index_pricing_section.py
- PYTHONDONTWRITEBYTECODE=1 uv run --with pytest python -m pytest -p no:cacheprovider tests/test_pillar1_public_copy.py tests/test_docs_frontend.py tests/test_static_route_canonicalization.py tests/test_favicon_metadata.py tests/test_public_logo.py tests/test_landing_typography.py tests/test_features_route.py tests/test_index_pricing_section.py -q
- 36 passed

Note: no-mistakes was started after commit, surfaced one homepage reward-copy warning, and was then skipped at user request because it was slow. The warning was manually fixed and targeted tests were rerun.
