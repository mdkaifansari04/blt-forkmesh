---
schema: forkmesh-issue-v1
number: 368
title: Identity key backup, export, and rotation
status: closed
labels: [security, feature]
milestone: Phase 2
priority: 51
progress: 0
assignees: [Claude Code]
createdAt: 1783116818267
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-368
ts: 1783116818267
attachments: []
sig: rZVLIfSLOlB85dZJjCpdKIU3ph8VRIHuBNh8UUBA3SqeoOU4DTPLodt8lpfKrgwE_hKZsTlxT-6WnuYD4PEpAA
---

**Roadmap Phase 6.** Losing the laptop currently means losing the identity: every signature, catalog record, and bounty binding hangs off one non-backed-up `ed25519.pem` created by `qt_client/src/ForkMeshIdentity.cpp` under the app data dir.

- Export/import: passphrase-encrypted keyfile (AES-256-GCM with a memory-hard KDF — reuse the cove KDF path in `CoveCrypto.cpp`), plus a QR export (`QrCode.cpp` already exists) and an optional BIP39-style mnemonic. Settings UI in `MainWindowSettings.cpp`, with a "back up your key" nag on first run until done.
- Rotation: a signed `rotate` record — the old key signs the new pubkey — honored by the mainnode and peers; the worker's account/key binding must accept the successor key (today a pubkey bound elsewhere is a hard failure).

Best-practice reference for the keyfile envelope: age (<https://age-encryption.org>).
