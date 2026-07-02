---
schema: forkmesh-pull-v1
number: 25
title: Fix Qt client roster alerts and test isolation
base: main
head: fix/qt-client
status: merged
ts: 1782985734466
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: 0W6L9KvLnoTd7i2n2MnG14MKyLvksagznX1g2BASpwj2GE8Ffzwm-y7z0UeJCQDcAhTB5L12Ljv-_O0XV3CoBw
---

Summary:
- avoid self-node entries when deciding whether a roster update is the first peer roster
- keep macOS header helpers inline to avoid duplicate definitions from header inclusion
- isolate Qt window-resize test app data with QStandardPaths test mode and cleanup

Test:
- cd qt_client && ./run.sh test
