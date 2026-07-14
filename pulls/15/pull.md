---
schema: forkmesh-pull-v1
number: 15
title: Refresh dashboard profile and contribution surfaces
base: main
head: api-pr/20260714-035346/dashboard-profile-contributions-ui
status: merged
ts: 1783981696726
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: rL2_liC9HzQewAQpGowfucjon3pD2v0AXAq_P6i8API9gUL-ktQyy1qYSytLm1x8rHn1cLM0OlGzUyAA8rvUCA
---

Refreshes the dashboard profile contribution surfaces and shared dashboard shell pages so profile/repository views expose the new contribution UX cleanly.

This slice intentionally excludes cross-stack login-key-binding tests so it stays dashboard-only and independently reviewable.

Submitted as a lightweight signed API PR because the full feature branch is large. Based on local main f760e13 because fetching origin/main failed with a ForkMesh mirror integrity check error, so remote freshness could not be proven.
