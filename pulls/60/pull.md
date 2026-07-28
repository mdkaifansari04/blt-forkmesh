---
schema: forkmesh-pull-v1
number: 60
title: Chat: harden recovery and hydrate actions before rendering
base: api-pr/20260728-office-aquarium
head: api-pr/20260728-chat-reliability-after-aquarium
status: open
ts: 1785253086240
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: K8G-R_Sqa42odSHkYckXWX9w4yapv0ZEZ9k2RjyrBk-Yec74TyOLKgENtkQDHPLAw6w0tPaD2O-mENzhUmyFDw
---

Depends on the Office aquarium PR. Hardens chat recovery and hydration, wires thread reply and delete actions before rendering, and makes the build-board title and Office attendance migrations idempotent and correctly ordered.

Based on local origin/main 35fbb502cfe37e0ec2eaf9cc3c498c0a4f794c16; the remote fetch returned repository not found, so source freshness could not be refreshed. Internal planning/spec documents and local deploy config are intentionally excluded.
