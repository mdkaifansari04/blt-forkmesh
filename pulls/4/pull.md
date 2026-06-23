---
schema: forkmesh-pull-v1
number: 4
title: Fix desktop window resizing
base: main
head: fix/screen-size
status: closed
ts: 1782160073930
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: LPlmOSnp5ZW8E9__VdCHgrxncGxl25Ea60EfaIZENwVqBhkoS9EIf2aWcO7eY_wUp38ArR5Jkdb1_ZorTNztAg
---

Summary:
- Wrap tall setup, settings, and repo content in scroll areas so the desktop window can shrink vertically.
- Keep the issue metadata sidebar scrollable instead of forcing the Issues tab height.
- Add an offscreen Qt regression test for resizing the main window to 520px.

Test Plan:
- cd qt_client && ./run.sh test

Author:
- Md Kaif Ansari <amdkaif843@gmail.com>
