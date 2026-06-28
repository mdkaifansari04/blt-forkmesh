---
schema: forkmesh-issue-v1
number: 194
title: Split MainWindow.cpp (34.8k lines) into feature-grouped files for faster rebuilds
status: closed
labels: []
milestone: 
priority: 2
progress: 0
assignees: []
createdAt: 1782345034609
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-194
ts: 1782345034609
attachments: []
sig: ELTuSQOqGZeEJjYugQdHJ5ypJmrpUkCcU8nSEB6DAO2QtYpN-VTOTFfidyTsZed6Cu8We1Fq2p-UTY95iXNwBw
---

## Problem

`qt_client/src/MainWindow.cpp` is **34,803 lines** in a single translation unit
that compiles to a **~26 MB object**, built twice (`forkmesh` and
`forkmesh-window-tests`). Any one-line edit to the `MainWindow` class recompiles
all 600+ methods, which dominates the edit -> build latency in the client.

`MainWindowCoves.cpp` already shows the fix: it defines a slice of `MainWindow`
member functions in its own TU (just `#include "MainWindow.h"`). Generalize that
so the class is spread across feature-grouped `.cpp` files; editing one feature
then only recompiles its small TU.

The blocker is the ~3,000-line anonymous namespace (lines ~143-3281): ~50
file-local helper functions (`runGitCapture` 142 uses, `setOcticon` 137,
`currentThemeIsDark`, `notifyEnabled`, ...), ~25 `k...Setting` `QString`
constants, and ~16 helper classes (delegates, syntax highlighters, custom
widgets). These must be shared before methods can move out. None use `Q_OBJECT`,
so there are no MOC complications, and there is no namespace-scope mutable state.

## Step 1 - extract shared helpers into `MainWindowInternal.h` + `.cpp`

- Header: `inline const QString` for the `k...Setting` constants (C++17 inline
  variables), plain declarations of the free helpers, and full definitions of
  the helper classes/structs/enums at namespace scope (not anonymous, so each
  has a single external definition).
- Cpp: the helper function bodies plus any out-of-line class methods (e.g.
  `CodeLineNumberArea`, the large `forkMeshAvatarPng`/`tintedOcticonPixmap`).
- `MainWindow.cpp` then just `#include "MainWindowInternal.h"` and still builds
  as one TU - a safe checkpoint to compile before moving any methods.

## Step 2 - peel feature method-clusters into `MainWindow<Feature>.cpp`

Each file mirrors `MainWindowCoves.cpp`:

    #include "MainWindow.h"
    #include "MainWindowInternal.h"
    // + feature store headers + the Qt includes it uses

Methods move as verbatim contiguous ranges (cut from `MainWindow.cpp`, paste
into the new file); only the home TU changes. Proposed grouping:

- `MainWindowSession.cpp`   - setup page, account/session, auth/signup/login, heartbeat, admin-verify, catalog
- `MainWindowUpdate.cpp`    - quick-update / rebuild / relaunch / install / uninstall
- `MainWindowServers.cpp`   - servers, relay switcher, favicons
- `MainWindowSolanaProfile.cpp` - solana balances, treasury/donate, wallet notices, leaderboards, node profile
- `MainWindowChat.cpp`      - chat section, messaging/members/channels/DMs/typing/files, flash/log/notify
- `MainWindowIssues.cpp`    - issues build/list/board/labels/milestones/render/compose/edit/bounty/vote/inbox (split in two if needed)
- `MainWindowPulls.cpp`     - pulls build/render/merge/conflicts/links/inbox
- `MainWindowAgents.cpp`    - agents tab/sessions/runners/spend, IDE integration
- `MainWindowRepoDetail.cpp`- repo-detail shell, files/overview/editor/readme, security/insights/scm tabs
- `MainWindowCommits.cpp`   - commits load/show/graph/thread/comments, issue-closures, post-from-commits, search
- `MainWindowBranches.cpp`  - branches/releases/mirror-nodes tabs, repo info/branches/tags
- `MainWindowActions.cpp`   - actions/workflows/runs/notifications/repo-actions tab/variables/push hooks
- `MainWindowRepos.cpp`     - repository mgmt: load/save/mirror/preview/publish/sync/settings/collaborators
- `MainWindowSettings.cpp`  - settings section, avatar, profile change, logout

`MainWindow.cpp` keeps the lean core: ctor, `showEvent`/`closeEvent`/
`changeEvent`, `runDeferredStartup`, `applyTheme`, the app shell
(`buildChatPage`), breadcrumb + nav switchers + relay/node/repo menus,
`showSection`, home section, and the global-search box.

## Step 3 - register sources in CMake

Add `MainWindowInternal.h/.cpp` and every `MainWindow<Feature>.cpp` to the
`FORKMESH_APP_SOURCES` list in `qt_client/CMakeLists.txt` (one list, already
shared by the app and `forkmesh-window-tests`).

## Execution notes

- Incremental and verified: do Step 1, build; then move one feature file at a
  time and rebuild after each so any error localizes to the file just moved.
- Mechanical, not a rewrite - bodies copied verbatim, only includes added.
- Concurrent-edit caution: ForkMesh's issue-agents may auto-edit/commit this
  working tree mid-session; re-read ranges immediately before each cut.

## Verification

1. `cmake --build qt_client/build --target forkmesh` - clean compile + link
   after each extraction.
2. `cmake --build qt_client/build --target check` - builds and runs both CTest
   suites (the window suite drives a real `MainWindow`). Use the `check` target,
   not bare `ctest`, so stale binaries are not run.
3. Confirm the win: `touch` one feature `.cpp`, rebuild, and verify only that TU
   recompiles - not the 34k-line monolith.
