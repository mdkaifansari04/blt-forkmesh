---
schema: forkmesh-issue-v1
number: 1
title: Anti-spoofing / security hardening
status: open
labels: [security]
milestone: MVP launch
priority: 1
assignees: []
createdAt: 1781642499079
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: node1
type: open
id: open-1
ts: 1781642499079
attachments: []
sig: lCu_LMXYyEXsn4R9ZGeWNFkMDOaD8pUaQmieCFvH4Kmd_tca6xbVfu1H56RBz7EikDGGqY62RlMmHqY5KBwFDw
---

Anyone can currently host or publish under any node name (signatures are carried but never verified).

- Require a node-key-signed token on `/host` + catalog/files publish, verified against the registered account pubkey via `ed25519_verify`.
- Rate-limit messages, host requests, and catalog POSTs.
- Lower the 96 MB room-frame cap (amplification) and add catalog anti-spam.
