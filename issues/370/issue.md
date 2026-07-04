---
schema: forkmesh-issue-v1
number: 370
title: [build infra + ops: needs signing certs] Signed installers and auto-update for Windows/macOS/Linux
status: closed
labels: [infra]
milestone: Phase 2
priority: 60
progress: 0
assignees: [Claude Code]
createdAt: 1783116818269
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-370
ts: 1783116818269
attachments: []
sig: bCvKzOIjLc0kP6m63iZSdo7Qv26gxnFZKrbicdvLtaOGZv-r4FHdWidO-3BPsXTR844Y84k3aak0uFL8i4OpBg
---

**Roadmap Phase 7 (something people choose).** The app must be a download, not a CMake invocation.

The release plumbing already exists — content-addressed blobs (`node release-blob` op, worker `/releases/blob/<sha256>` route, sha256-verified `install.sh`, `tools/forkmesh-release-publish.sh`). Extend it per-platform:

- Linux: AppImage + zsync delta updates (<https://appimage.org>).
- Windows: NSIS or MSIX installer + WinSparkle for updates.
- macOS: notarized dmg + Sparkle 2 (<https://sparkle-project.org>) — Sparkle appcasts are EdDSA-signed, a natural fit since we already run Ed25519 everywhere.
- In-app update check against `release.json`; mind the stale-release.json pitfall — the bump commit/tag must land in the bare mirror before the publish queue runs.

Ops (not code): purchasing the Apple Developer ID and Windows code-signing certificates and wiring them into CI secrets.
