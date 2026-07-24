---
schema: forkmesh-pull-v1
number: 43
title: issue #476: align product promise, proof path, and measurement
base: main
head: fix/repo-path
status: open
ts: 1784889373393
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: CF9lYPNolVZ2jpVRA5ocXwbUwq0CNnMbWiMWFXxIXN3kyxjh8j70omiLnITMHI72-c2wRy7VihvbUUTUG2bsCA
---

This signed patch PR implements the issue #476 product-promise work from the `fix/repo-path` branch.

Completed:
- Makes the approved product promise the primary copy across homepage, repository profile metadata, signup/onboarding, desktop install, pricing, mirror payouts, and docs surfaces.
- Adds product contract tests that enforce the canonical promise and reject contradictory positioning.
- Adds the public proof path for: collaborate, add a mirror, source host offline, and clone again.
- Marks source-offline and clone-again proof steps as Operator proof required instead of overclaiming production closure.
- Adds docs sections for collaboration routes and mirror failover/operator verification.
- Adds a product-contract evidence map that separates working actions, automated coverage, operator proof required items, and roadmap gaps.
- Adds a five-person cohort comprehension research template with the 80% pass rule.
- Adds privacy-safe product telemetry for allowlisted public interactions only.
- Disables PostHog autocapture, pageview/pageleave capture, and session recording.
- Updates privacy copy for public product telemetry.

Verification run locally:
- git diff --check main..HEAD
- python3.11 -m py_compile src/entry.py tests/test_canonical_product_promise.py tests/test_product_contract_copy.py tests/test_product_contract_evidence.py tests/test_product_promise_research.py tests/test_product_proof_path.py tests/test_product_telemetry.py
- Direct Python 3.11 execution of all new product contract/proof/evidence/research/telemetry test functions.

Source freshness note:
- During PR preparation, `git fetch origin main` force-updated `origin/main` to 28b5b776 with no shared merge-base against this branch in the local checkout.
- To avoid signing an unrelated-history patch, this payload is generated from local `main` 3dd0f642..fix/repo-path 0c03fac8.
- The owner should verify/rebase if their current canonical main differs from local main.

Not claimed complete:
- Live operator failover proof, anonymized cohort results, rollback/recovery evidence, and privacy-reviewed server-side aggregate events remain required before issue #476 is treated as fully closed.
