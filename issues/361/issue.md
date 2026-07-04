---
schema: forkmesh-issue-v1
number: 361
title: Notifications: mentions, subscriptions, and an email bridge
status: closed
labels: [feature]
milestone: v1
priority: 24
progress: 0
assignees: [Claude Code]
createdAt: 1783116818260
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-361
ts: 1783116818260
attachments: []
sig: nDYK-2ltat1czJYENJY7Iz0ZXQ_8KCU_4-iKWWBvWpI0-jt4GG_uhWnM3S-VxgsbFjJJYn_afcnO1-ZmMP6uBw
---

**Roadmap Phase 3.** Today nobody learns about a reply unless the app is open on the right tab.

- Subscriptions: signed `subscribe` events per issue/PR; auto-subscribe the author and anyone who comments.
- Mentions: parse `@nodename` in issue/PR comment bodies during the render pass (`MainWindowIssues.cpp`, `MainWindowPulls.cpp`).
- Delivery: a small per-pubkey pending-notification queue on the worker (new DO route in `cloudflare_worker/src/entry.py`) that nodes drain on their existing heartbeat; a notification inbox in the left nav with unread counts.
- Email bridge: digest emails through the verified-email machinery — but open issue #320 says email verification is currently broken, so fix that first; it is a hard dependency. Open issue #346 (email when Claude credits refill) should ride the same delivery rail rather than building its own.
