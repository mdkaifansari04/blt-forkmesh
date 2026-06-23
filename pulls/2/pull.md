---
schema: forkmesh-pull-v1
number: 2
title: Remove repo Star button
base: main
head: issue-160-remove-star-button
status: merged
ts: 1782215685827
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: mainnode
sig: 8LqL-UvPoWiZ386nlHVkW_Bh8hc9r_oP5WjjSU24Lrqbguuj1ShBpdp5U4pzd9Ej4V-fpGX9Yut8jI0iIB-yBg
---

Removes the unused Star action from the Qt repo header while keeping the repo star metadata field for compatibility.

Tests:
- `cmake --build qt_client/build-forkmesh --target check -j2`
- no-mistakes run `01KVT4HV9AR4Z668V1WWESZ2NE`
