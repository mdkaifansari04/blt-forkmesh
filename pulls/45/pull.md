---
schema: forkmesh-pull-v1
number: 45
title: feat: add encrypted public and private chat channels
base: main
head: api-pr/20260725011143/chat-channels
status: open
ts: 1784923299852
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: _OcV3nGsxR6yxqLBGKOQ5M68KkVtJbjaaYNsUf_8an7_3hNpprzN-xEGlXcX3MiY7RkRuMuuqhPsB_hjOSNOCg
---

Adds the private/public chat channel foundation for ForkMesh dashboard chat.

Summary:
- Adds private chat channel schema and migrations.
- Adds channel membership authorization and room access gates.
- Adds encrypted chat attachments, persistence, and user picker flows.
- Includes focused Worker/front-end tests for channel access, attachments, persistence, migrations, and login binding.

Verification:
- git diff --check main..api-pr/20260725011143/chat-channels
- python3.12 -m py_compile cloudflare_worker/src/entry.py
- uv run --python 3.12 --with pytest python -m pytest -q tests/test_private_chat_channel_schema.py tests/test_chat_channels_api.py tests/test_chat_channel_room_access.py tests/test_chat_attachments_frontend.py tests/test_chat_history_byte_budget.py tests/test_dashboard_chat_persist_frontend.py tests/test_private_chat_channels_frontend.py tests/test_login_key_binding.py tests/test_local_migrations.py

Note: based on local main because fetching forkmesh.com/forkmesh/forkmesh returned HTTP 503 during PR prep.
