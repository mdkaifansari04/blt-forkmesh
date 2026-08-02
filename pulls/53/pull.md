---
schema: forkmesh-pull-v1
number: 53
title: feat(chat): add personal direct messages
base: main
head: feat/personal-chat
status: closed
ts: 1785064862930
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: rgCa5kNbH5W-cFbfFheKrvBMv2vWIkEh2GH_NItthxTAxQ6qfsBggceXY_XcejfjSn2O6nZUW_9e__m05m5xCQ
---

Adds participant-only personal direct messages to web chat and preserves the updated channel model.

Summary:
- Adds direct message storage, API routes, room authorization, frontend UI, and tests.
- Documents the personal direct message protocol.
- Rebases onto current main and removes the deprecated private-channel creation UI from this branch.
- Keeps private-channel member management for existing authorized private channels.

Verification:
- python3 -m py_compile cloudflare_worker/src/entry.py
- cd cloudflare_worker && .venv/bin/python -m pytest tests/test_private_chat_channels_frontend.py tests/test_chat_direct_messages_frontend.py tests/test_chat_direct_messages_api.py tests/test_chat_direct_message_room_access.py tests/test_chat_direct_messages_schema.py tests/test_chat_history_byte_budget.py -q
- cd cloudflare_worker && node --check public/chat.js

Note: local cloudflare_worker/wrangler.toml D1 config is intentionally excluded from this PR.
