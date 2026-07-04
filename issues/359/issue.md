---
schema: forkmesh-issue-v1
number: 359
title: Inline PR review comments and review states (human review that holds up)
status: closed
labels: [feature]
milestone: v1
priority: 21
progress: 0
assignees: [Claude Code]
createdAt: 1783116818258
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-359
ts: 1783116818258
attachments: []
sig: 885Ggk7rpGbmffgPjxPARDG7cD0Rdbg6JViDIq2Zd5Ioi_vvNHPZnMIzO87os46hNukgDpCuj33r7Fsyd-SRAg
---

**Roadmap Phase 3.** AI review already anchors suggestions to diff hunks (`suggestionPatch` mini-diffs with verify-and-relocate on apply — `qt_client/src/PullAiReview.cpp`); human reviewers get no equivalent.

- Storage: signed `review-comment` events under `pulls/<N>/` using the same append-only `NNNN-<type>.md` pattern and canonical-string signature scheme documented in `issues/README.md`, with a file + line anchor included in the signed content.
- Rendering: pin comments to diff lines in the review pane (`qt_client/src/PullReviewModel.cpp`, `MainWindowPulls.cpp`), reusing the suggestionPatch relocate-on-drift logic when the PR is updated.
- Review states: `approve` / `request-changes` as signed events; an unresolved request-changes gates the merge button.
- Web: read-only rendering in `cloudflare_worker/public/dashboard.js` can follow as a second step.
