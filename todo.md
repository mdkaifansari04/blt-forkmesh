# Forkmesh integration checklist

This file tracks the work requested in this session. Check items off only after
the implementation and its focused verification are complete.

## Current focus

- [x] Add an in-world bulletin board before continuing the remaining tasks.
- [x] Show remaining checklist items as marker-drawn sticky notes on the left.
- [x] Move completed notes to varied positions on the right and mark them with a hand-drawn X.
- [x] Remove the remaining invisible Office doorway collision wall and verify smooth entry/exit.
- [x] Make the walkway, bridge, and Office lobby meet at the same height with no gap.
- [x] Make the sound button a real master on/off control for active playback.
- [x] Verify the Office threshold geometry and sound toggle in the running world UI.
- [x] Route pending web issues to an eligible online repository mirror.
- [x] Prevent duplicate issue intake when multiple mirrors are online.
- [x] Sync mirror-created issues back to the repository source of truth.
- [x] Give pull-request details GitHub-style section tabs.
- [x] Show changed files in a left-side file list.
- [x] Let reviewers mark individual files as viewed and persist that state.
- [x] Let a reviewer approve a pull request.
- [x] Let an organization owner merge an approved pull request into an eligible mirror.
- [x] Queue the mirror merge for later synchronization to the source of truth.
- [x] Keep mirror merge owner-only until group permissions are available.
- [x] Add focused authorization, concurrency, persistence, and UI tests.

## Requested follow-up work

- [x] Allow all users to enter the building.
- [x] Keep the full Marketing tasks board blank unless someone is in the room.
- [x] Stop the mobile world view from refreshing while the user moves.
- [x] Show the quick map on mobile.
- [x] Return a safe degraded response instead of `503` for unavailable satellites.
- [x] Point the dashboard globe link to `/world`.
- [x] Allow organization admins to assign user groups, including for offline users.
- [x] Put each seated user's group control on their back.
- [x] Finish reviewing and integrating the remaining session branches from the supplied list.
- [x] Delete merged session branches and worktrees after verification.
- [x] Restore the preserved pre-existing user test changes from the preserved stash.

## Screenshot branch integration

The code from these screenshot sessions has been integrated into `main`.
Deleting their source branches/worktrees remains a separate unchecked cleanup
task above so they stay recoverable until final verification.

- [x] Session 445 — ForkMesh recover setting.
- [x] Session 444 — prevent falling through the floor.
- [x] Session 436 — use the local ForkMesh favicon/header asset.
- [x] Session 432 — Office building character/entry behavior.
- [x] Session 425 — background agent-session deletion cleanup.
- [x] Session 421 — remove the large-diff hidden-for-speed behavior.
- [x] Session 420 — release/status display cleanup.
- [x] Session 418 — same-user login identity handling.
- [x] Session 417 — same-user login identity handling.
- [x] Session 416 — pull-request conflict/stall handling.
- [x] Session 414 — camera capture framing.
- [x] Session 413 — ForkMesh perimeter/portal retry handling.
- [x] Session 412 — mirror World Office channel chats into the desktop app.
- [x] Session 401 — leave first-person mode when zooming out.
- [x] Session 398 — variable-based chat theme/cache-buster handling.
- [x] Session 396 — node-install/provisioning behavior.
- [x] Session 386 — website referral leaderboard.
- [x] Add a separate HTTP `Referer` leaderboard beside the existing referral leaderboard.
- [x] Review, clean up, and merge mdkaifan's direct-chat pull request.
- [x] Remove the direct-chat PR branch/worktree after its merge is verified.

## Final verification

- [ ] Run focused worker API tests.
- [ ] Run focused Qt pull-request and issue-sync tests.
- [ ] Run the relevant build/static checks.
- [ ] Confirm the worktree contains only intentional changes.
