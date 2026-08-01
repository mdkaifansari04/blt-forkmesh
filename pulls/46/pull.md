---
schema: forkmesh-pull-v1
number: 46
title: feat: add ForkMesh Office world chat
base: api-pr/20260725011143/chat-channels
head: api-pr/20260725011143/office-world-chat
status: closed
ts: 1784923299924
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: vDpc-6c9YAgeYWQ_4f3NWNhKImZDT5E0u4AhXHeZgjvl9VBdkY0OFdG3ZwF1LJHoNf3ZEExKpF-exmsuZPBNAQ
---

Adds the spatial ForkMesh Office world chat layer on top of the encrypted chat channel foundation.

Summary:
- Adds Office world metadata, landmark, scene/controller wiring, and compact chat embed.
- Updates world UI, CSS, docs/specs, and snapshot coverage for the Office entry points.
- Includes focused Worker/front-end tests for world metadata, Office rendering, and chat embed behavior.

Dependency:
- Apply after the chat channels PR branch: api-pr/20260725011143/chat-channels

Verification:
- git diff --check api-pr/20260725011143/chat-channels..api-pr/20260725011143/office-world-chat
- python3.12 -m py_compile cloudflare_worker/src/entry.py
- uv run --python 3.12 --with pytest python -m pytest -q tests/test_world_backend.py tests/test_world_data_truth.py tests/test_world_frontend.py tests/test_world_office_backend.py tests/test_world_office_frontend.py tests/test_chat_office_embed_frontend.py

Note: based on local stack because fetching forkmesh.com/forkmesh/forkmesh returned HTTP 503 during PR prep.
