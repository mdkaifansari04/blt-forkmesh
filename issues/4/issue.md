---
schema: forkmesh-issue-v1
number: 4
title: Signup + account UI
status: closed
labels: [feature]
milestone: 
priority: 0
progress: 100
assignees: []
createdAt: 1781642499082
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: node1
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-4
ts: 1781642499082
attachments: []
sig: DWXd1MYnmiR7_Vwpm0q11znLOWwADhjKnnbfxF0TnlMbrCcrl1vU4yE_cTJ5LJ379zMTSjtPY7B7h8fCMcqJBA
---

The accounts backend works; the surfaces don't exist yet.

New auth model: register through the client, verify an email + a BCH address (replaces passwordless Ed25519-only).

- Website signup page (node-signed, pick a unique node name + email).
- In-node Account UI (Settings): email field + "Register node name" via `ForkMeshIdentity::signData()`, plus a login/status indicator.
- Enforce the `^[a-z][a-z0-9]*$` node-name rule on the client handle.
