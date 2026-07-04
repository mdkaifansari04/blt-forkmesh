---
schema: forkmesh-issue-v1
number: 362
title: Private repositories as end-to-end encrypted mirrors (hybrid X25519 + ML-KEM)
status: closed
labels: [feature, security]
milestone: v1
priority: 25
progress: 0
assignees: [Claude Code]
createdAt: 1783116818261
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-362
ts: 1783116818261
attachments: []
sig: 9qPcwpQjd3W6abiVj1StQLQ8jdyqKApLCYZtCEedMBH62tpNbA6MnIHmlSQS2FuH2XABKgLbpuMNiX3lXk35Ag
---

**Roadmap Phase 3**, from the original roadmap note. Hosts mirror an opaque blob; the catalog shows a public handle and size only.

- Encrypt pack data client-side before it leaves the owner node; mirrors store ciphertext; collaborators get the content key wrapped to their identity keys. `qt_client/src/CoveCrypto.cpp` / `RoomCrypto.cpp` already have the AES-256-GCM + key-wrap patterns to extend.
- Use a hybrid KEM — X25519 + ML-KEM-768 — so mirrored ciphertext is not harvest-now-decrypt-later bait. FIPS 203 is final and OpenSSL 3.5 ships ML-KEM; we already link OpenSSL.
- Catalog: extend the mirror record with `private: true` + size via the established 4-step recipe (`publishRepository` → `safe_catalog_record` → `build_repo_mirrors_payload` → `loadMirrorNodesPanel`). The worker keeps verifying `catalogSig`/`stateSig` and never sees plaintext.
- v1 scope: whole-mirror encrypted archives (simple and correct); content-defined chunking for incremental sync is a follow-up.

References: FIPS 203 <https://csrc.nist.gov/pubs/fips/203/final>, OpenSSL 3.5 ML-KEM support.
