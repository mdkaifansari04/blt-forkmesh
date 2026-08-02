---
schema: forkmesh-pull-v1
number: 47
title: feat: add multiplayer Office meeting rooms
base: api-pr/20260725011143/office-world-chat
head: api-pr/20260725011143/office-meetings-code
status: closed
ts: 1784923300003
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: npsEBrstjRwlGDlxZtsMPinhxDAI5yI927IE3p9gDHuWsy9Kd_FZYHfF2g9SvNNTmPFVzwxNjv7V_C4vyyBbAQ
---

Adds multiplayer Office meeting room code and tests on top of the Office world chat stack. Browser PNG snapshots are split into follow-up asset PRs to keep payloads small.

Summary:
- Adds the Office meeting presence protocol and authorized meeting room flow.
- Shares encrypted chat primitives/room transport between chat and meetings.
- Adds spatial meeting UI, verified chat indicators, and focused Worker/frontend coverage.

Dependencies:
- Apply after: api-pr/20260725011143/chat-channels
- Then apply after: api-pr/20260725011143/office-world-chat

Verification:
- git diff --check api-pr/20260725011143/office-world-chat..api-pr/20260725011143/office-meetings-code
- python3.12 -m py_compile cloudflare_worker/src/entry.py
- uv run --python 3.12 --with pytest python -m pytest -q tests/test_world_office_protocol.py tests/test_world_office_meeting_frontend.py tests/test_chat_shared_modules_frontend.py tests/test_chat_channel_room_access.py
- Integrated branch full Worker pytest: 1913 passed, 2 baseline installer-source failures reproduced on local main.

Note: based on local stack because fetching forkmesh.com/forkmesh/forkmesh returned HTTP 503 during PR prep.
