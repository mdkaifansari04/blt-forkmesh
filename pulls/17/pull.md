---
schema: forkmesh-pull-v1
number: 17
title: Fix Qt client responsive layout
base: main
head: pr/fix-ui-qt-client
status: open
ts: 1783363937227
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: CdM0-nE72U1Cq2oj62N7UWj3vBwXqZTT7YFUModtf9dM3xXG8GHte9hOiQpNGgUcAPeY0OKvOzUmh1bemxccDg
---

Clean Qt-only PR rebuilt from fix/ui-qt-client.

This keeps only the responsive Qt client layout fix and excludes unrelated Worker/static-site, issue metadata, and local PR metadata from the source branch.

Validation:
- git diff --check passed
- cd qt_client && ./run.sh test passed

Scope: qt_client only.
