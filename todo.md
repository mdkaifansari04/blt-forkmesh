# Forkmesh integration checklist

This file tracks the work requested in this session. Check items off only after
the implementation and its focused verification are complete.

## Current focus

- [ ] Complete the remaining web PR lifecycle: proactive readiness/conflict data, live checks, complete commit history, and an auditable signed update-from-main mirror operation. Conversation, peer review, changed files, and protected owner merge are already present.
- [x] Add a one-line status bar at the bottom of the Qt window carrying the branch switcher, the repository git identity and the running app's on-disk location.
- [x] Make avatar clicks reliably open the user HUD; remove the out-of-scope animation timestamp crash and prefer a nearby visible avatar hit over surrounding geometry.
- [x] Give the Members Circle detailed dirt, one visible pile log per member, and a gently growing bounded campfire.
- [x] Keep the combined aquarium control panel fixed to the lower-right of the tank.
- [x] Show one sanitized recent public-channel message on each user's chest card without exposing private/direct chat.
- [x] Show a green verified-email pin or a red unverified-email X on every signed-in avatar, with guests neutral.
- [x] Add one labeled warp pad per Office floor to the left of the lobby welcome desk, respecting floor access.
- [x] Align image preloads with texture-loader credentials, avoid cross-origin avatar failures with deterministic fallbacks, and treat unavailable Actions summaries as retryable state rather than HTTP 503 noise.
- [x] Make every visible admin database row open a read-only detail page with the complete redacted record listed vertically.
- [x] Show a red email-unverified X for every non-guest member and open the privileged admin user detail action in a new tab.
- [x] Add a live Engineering-room debug control panel with bounded renderer, memory, loop, and interaction counts plus green/orange/red optimization thresholds.
- [x] Put every leaderboard and statistic into one square 5×5 wall, remove the duplicate physical boards and center circle, and keep the complete assembly above terrain.
- [x] Remove the ForkMesh open repo issues billboard and its invisible interaction surface; close the gap by moving the Human TODO board beside the build board.
- [x] Move the Solana treasury QR to the front midpoint of the first node ring and add a Start a node button that opens the desktop download page in a new window.
- [x] Replace the doubled south route with one concrete-brick path terminating at the fire-marked Members Circle sign.
- [x] Replace the doubled north Office route with one width-matched concrete-brick path, bridge, and approach that meet edge-to-edge.
- [x] Make the World foundation and its collision boundary circular around the circular bike lane.
- [x] Make keyboard movement instant, reuse camera/movement frame scratch state, and cadence-bound non-motion proximity/DOM work while preserving full-rate WebGL rendering.
- [x] Move World DEBUG to the lower-left and extend the right chat dock to the bottom; add a pinned dark GitHub Primer multiline composer with channel/repository selectors and working chat, signed-issue, and Engineering-agent actions, move channel security/connection into its header, remove the duplicate public notice, and collapse HUD docks when clicking outside.
- [x] Restyle the compact and expanded World right HUD with GitHub Primer primitives, including selected, hover, focus, and icon-button states.
- [x] Open both Issue and PR tower records in their canonical, same-origin embedded web workbenches instead of the unrelated Repository portals panel.
- [x] Audit the full 2026-07-28 request history against the implementation, tests, QA deck, and build-board source; keep every partial or externally unverifiable item open below.
- [x] Scale repository PR and Issue towers to their complete bounded item counts and move them outside the ActivityPub follower orbit.
- [x] Remove zoom-out relationship lines, rounded connector pads, colored grass ovals, disjoint terrain remnants, and obstructive agent-terminal blocks; retain one fast continuous walkable grass foundation.
- [x] Make the perimeter bike path a true circle and lock mounted bikes to its center groove, with `E` as the explicit mount/dismount control.
- [x] Make World PR and Issue clicks open record-only detail drawers without unrelated Repository portals content.
- [x] Make admin node deletion alias-tolerant and idempotent, purge all related node data, and remove every matching cabinet from the World.
- [x] Add sun/moon sky bodies, an Auto/Day/Night display preference, a collapsed-by-default expandable right HUD rail, and text-free saved-view thumbnails.
- [x] Straighten the World “What we're building” cards, show the complete active task set, and add readable scope/status detail to every card.
- [x] Size the World PR and Issue panels to their visible records, move open counts below the lists, put the newest record at the bottom, and add one-click first-person viewing pads.
- [x] Stop signed-in mobile World pans from clearing or visually refreshing the WebGL scene.
- [x] Restore the cached spawn pose before scene hydration and keep Office travel attached to the avatar.
- [x] Finish the Qt execution-aware stall-log regression build so fast async/worker actions never produce false “not backgrounded” alerts.
- [x] Show follower avatars in the self profile and add a non-blocking ActivityPub composer with selfie capture and auto-filled editable alt text.
- [x] Keep Reef Control visible throughout the lobby and make fish react gently when a visitor approaches.
- [x] Animate verified changed files colliding with their repository ring, with night lightning or daylight file shadows followed by a ten-second sizzle/sparkle fade.
- [x] Show each live repository agent task on its tiny terminal and open that exact engineering-only transcript with prompt and re-prompt controls when clicked.
- [x] Join the World districts into one continuous city landscape with seamless concrete paths.
- [x] Add a visible START HERE progress map from the users area to the centered mirror nodes.
- [x] Make Space jump off the roof, check the user out on exit, slow the descent with a mini parachute that collapses after landing, and allow every chair or bench to seat a visitor.
- [x] Add a connected driveable road, car, beach environment, water, horizon, and beach seating.
- [x] Move the perimeter bike route outside activity areas while retaining two usable bicycles.
- [x] Circle-align the public billboards, retain one leaderboard panel, and remove the obsolete center marker.
- [x] Apply a shared GitHub-like interface system across World panels and controls.
- [x] Add local daylight, stars, chest time, organization-level stars, and optimized local scene textures.
- [x] Finish the canonical World node-delete resolution and cabinet-removal animation regression fix.
- [x] Make the separate World PR and Issue panels single-column while retaining 25 records per independently paginated page.
- [x] Show explicit verified/unverified email state in the World member panel and give `is_admin` viewers a direct, filtered admin user-detail link.
- [x] Split the selected-repository World exhibit into left PR and right Issue panels with large open counts, bottom-only pagination, an angled named commit-activity pedestal, and compact per-repository live agent terminals.
- [x] Replace World object dragging with click selection, a subtle selection highlight, automatic detail-panel opening, admin arrow-key nudging, and `R` rotation.
- [x] Make every visible user avatar clickable, subtly highlight the selected person, and open their privacy-filtered member information in the side panel.
- [x] Fix the `is_admin` World cabinet delete action to target the physical mirror name (not its operator account), report failures, and force-refresh cabinets after deletion.
- [x] Apply the annotated World plan: four broad paved cardinal paths with curved joins; nodes in the center; every repository on the east island and always expanded; billboards on the west island; and the member/campfire circle on its own south island.
- [x] Keep the flagship repository wheel expanded immediately and remove mirror-metadata render delay.
- [x] Restore country flags immediately, add verified-email front pins, and keep privacy choices intact.
- [x] Give every user a stable unique generated face plus a compact account-avatar upload override.
- [x] Superseded: the initial combined repository work list was replaced by independent single-column PR and Issue panels.
- [x] Show the signed-in user's uploaded avatar in the top-right control at the same size as neighboring buttons.
- [x] Swap cabinet Actions/server-info faces and split Claude/Codex work onto opposite side panels.
- [x] Consolidate aquarium feed, opaque/clear backdrop, and light controls into a clickable bottom-right tank panel.
- [x] Represent every public user as a small deterministic fish, with active/recent fish in upper lanes and inactive fish near the bottom.
- [x] Superseded: initial handle-free drag layout control, now replaced by click selection plus keyboard nudge/rotation.
- [x] Keep mirror2 visible as a live green World cabinet when its recent signed endpoint health proves the machine is reachable, while preserving blocked clone/integrity detail.
- [x] Make live mirror cabinets use green for physical node liveness instead of conflating it with per-repository clone eligibility.
- [x] Show the latest commit's relative age beside its hash in both live and catalog-backed Qt Mirror nodes rows.
- [ ] Human TODO: share the live “ForkMesh Forever” post on Reddit, Mastodon, and X, then paste each published permalink into the post’s social-proof fields.
- [x] Add physical Pass / Fail / Unsure tabs to the QA deck with verdict-filtered task pages.
- [x] Let authorized QA reviewers send a reviewed card back to “What we're building”.
- [x] Let authorized QA reviewers file a reviewed card into the `forkmesh/forkmesh` issues list.
- [x] Keep newly provisioned Hosts rows checking until each mirror is reachable, with clear provisioning/online/attention state.
- [x] Show separate Claude Code and Codex installed/missing status beside every saved host.
- [x] Install/probe Claude Code and Codex as the actual `forkmesh-node` service account, including copied device logins.
- [x] Route Claude/Codex organization jobs only to fresh mirrors whose signed catalog advertises that provider.
- [x] Seed every headless mirror with a private service-owned working checkout and preserve it across bootstrap/restarts so installed agents can claim work.
- [x] Show a visible Terms-of-Service moderation flag on repositories that violate ForkMesh policy.
- [x] Initial front-wall elevator camera placement (superseded after live QA).
- [x] Increase the in-elevator button labels and contrast for clear floor selection.
- [x] Follow-up: restore elevator buttons to the side wall and use an upper security-camera view that frames them with the World outside.
- [x] Add tasteful scene-native flowers, trees, and bushes around the Office exterior.
- [x] Count Marketing Office Hours from explicit Office-building punches only, never general World presence.
- [x] Put Marketing desks against the windows with chairs, raise the round table, and seat it clearly.
- [x] Open QA history items as full detail cards with Pass, Fail, Unsure, and Back to Cards actions.
- [x] Add a Marketing initiatives panel in the Marketing room and let web issue pages move issues into it.
- [x] Add a large recent `#general` chat board beside the event list with authors, time, images, and reactions.
- [x] Include organization-owned repositories in Dashboard Top repositories with clear owner labels.
- [x] Restore live recent blog posts in the Dashboard “Latest from the blog” card.
- [x] Add an audited `is_admin`-only World action to permanently delete a named node after typed confirmation.
- [x] Make clicking the ForkMesh logo on Dashboard perform a clean Dashboard reload.
- [x] Keep newly installed mirror6 visible through provisioning in Qt Hosts and the World node cabinets.
- [x] Move Operational alerts into the platform `is_admin` area and retarget alert-management deep links.
- [x] Add a direct “Manage this alert” link to component and scheduled-job alert emails, opening the affected expanded status row.
- [x] Make QA arrows permanently visible and large (red Fail, green Pass, grey Unsure), publish global aggregate stats, and continuously intake newly completed tasks.
- [x] Fix the production `/api/world/deploy-status` response error that made API and Worker status red, then verify live recovery.
- [x] Use the deploy lifecycle singleton as a status semaphore so rollout minutes do not create false Website/API/Worker incidents or emails.
- [x] Show the live World user count and a Join World button between the homepage logo and hamburger on mobile.
- [x] Make the 24-hour QA deck a direct physical grab/swipe board with left Fail, right Pass, down Unsure, test instructions, and private cross-device totals.
- [x] Add “Done → send for QA” to each active build sticky and enqueue its test instructions into the QA deck.
- [x] Keep Share exact view on the right rail, restore the normal right-click menu, and keep Saved Views collapsed with small thumbnails and a +Map control.
- [x] Show admin-only copyable full IP and User-Agent details on guest backs without persisting or broadcasting them to other users.
- [x] Keep repository follower and contributor avatar orbits coplanar with the repository wheel and spaced farther out.
- [x] Consolidate member Info/Fedi into one compact front card with short relative times, Solana wallet QR/copy/balance, and no mode buttons.
- [x] Show every server-authoritative team badge as a stacked list on the avatar's left arm.
- [x] Restore the Marketing task wall to room scale and put create, Marketing-only assign, start/stop, done, and delete controls directly on the physical wall.
- [x] Restore the Fresh Code mirror-push beam and shockwave after a signed catalog commit confirms the push.
- [x] Fix chest Fediverse loading, enlarge the unified card, and use its dark node-style border as the activity indicator.
- [ ] Set up `CLOUDFLARE_OBSERVABILITY_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` so attention emails can include the prior two minutes of logs.
- [x] Deploy an instant World-wide deployment-start indicator, animated activity state, and explicit ready-to-refresh action without auto-refreshing.
- [x] Switch the elevator to an upper-corner first-person view that frames the controls and outside, then restore the prior view on arrival or exit.
- [x] Refresh “What we're building” whenever a player approaches and show its physical updating spinner.
- [x] Verify the ForkMesh X timeline feed and keep the in-world board sourced from the public `@forkmesh` profile without third-party tracking script injection.
- [x] Render recent ForkMesh Twitter/X posts like Mastodon; if public retrieval or credentials fail, show the reason on-board and add a Human TODO.
- [x] Make mirror agent installers exclusively use the saved ForkMesh SSH key when present and clearly distinguish an unreachable host from rejected authentication.
- [x] Finish and test the repository Mastodon-follower and Git-contributor avatar orbits.
- [x] Finish and test Claude/Codex/model assignment controls on World issue cards.
- [x] Finish and test the Qt host buttons that install the official Claude Code and Codex CLIs.
- [x] Finish and test the Marketing wall, member desks, attendance calendar, and sealed reclaimed-wood logo table.
- [x] Add server-authoritative team badges to each user's left arm.
- [x] Let Marketing members submit private social proof-of-work links from their own desk and show those items only to Marketing.
- [x] Run the focused Worker, browser-module, mirror gateway, and Qt tests for this round.
- [x] Deploy this round, verify the production revision/assets, and move each completed sticky to Done.
- [x] Keep Mastodon follower avatars visibly orbiting the selected repository file circle at every normal camera angle.
- [x] Make clicking the Claude or Codex world bot open its complete Engineering-only live status, work, transcript, runtime, and prompt controls.
- [x] Surface stalled mirror-agent diagnostics as actionable notes on the Engineering-only Human TODO board.
- [x] Superseded safely: do not provision Claude on retired `mirror2`; route agent jobs only to signed, provider-ready `mirror6`.
- [x] Show the latest actual safe full referring URL beneath each hostname on the in-world HTTP referrer board.
- [x] Show all available blog-board reach stats: total views, approximate unique views, referrer-site count, referred visits, and network distribution.
- [x] Add an Engineering-only Human TODO board beside the repository work boards, populated from actionable Claude/Codex session signals.
- [x] Review every currently open pull request and record an evidence-backed disposition for each, beyond the automated board status/score.
- [x] Stop signed-in mobile World movement from triggering a native page refresh or reconnect position rollback.
- [x] Move the complete Office elevator shaft into the first bay right of the entrance.
- [x] Move the System Capacity object onto the Infrastructure floor.
- [x] Add an opt-in live display for the viewer's local browser console on the Infrastructure floor.
- [x] Keep local console capture off by default, bounded, redacted, unsaved, and unshared.
- [x] Make “What we're building” stickies draggable to reprioritize the shared todo order.
- [x] Number pending todos by priority, with `1` as the highest priority.
- [x] Add a repo-issues sticky board beside “What we're building”.
- [x] Let authorized members drag an issue sticky onto the todo board to assign it as work.
- [x] Add a compact saved-view button to the World right-side control rail.
- [x] Save a tiny location thumbnail, editable short label, exact position, and camera perspective.
- [x] Restore a saved view with one click, using normal Office check-in/check-out when applicable.
- [x] Show relative “days ago” age on every in-world blog-board post.
- [x] Show each blog post's available cross-network publishing/distribution details on its board card.
- [x] Keep each newly completed and deployed feature on the World bulletin as a crossed-out Done sticky.
- [x] Lowest priority: replace seated repository watchers with follower icons around the file circle.
- [x] Lowest priority: add an issue list matching the World pull-request list.
- [x] Lowest priority: show up to 25 pull requests and fuller issue/PR metadata in both lists.
- [x] Move repository federation controls into an owner-only Settings tab beside Agents.
- [x] Change the About action icon from a gear to a pencil.
- [x] Add an owner-authorized repository deletion danger zone to Settings.
- [x] Let Qt agents work on pull requests while unrelated working-tree changes remain uncommitted.
- [x] Clean up the Qt pull-review file header.
- [x] Show each reviewed file's read-progress ring and percentage.
- [x] Mark files viewed as their diff scrolls through the viewport.
- [x] Keep the left changed-files list highlighted to the diff currently in view.
- [x] Fix the removed-line count shown for pull-request files.
- [x] Move the world sticky-task board into the main open area.
- [x] Place the world sticky-task board directly beside the System Stats display.
- [x] Show only this new round of tasks on the world sticky-task board.
- [x] Apply safe world scene and layout updates live without refreshing.
- [x] Show an explicit refresh button when a world update truly requires a reload.
- [x] Never auto-refresh the world for an available update.
- [x] Reduce first-load world layout shifting and make scene startup smoother.
- [x] Make capacity displays symmetrical with the minute graph on top.
- [x] Stretch minute/hour capacity timelines across the available width.
- [x] Give capacity graph cells a consistent shape and aspect ratio.
- [x] Show every seated member's complete public identity card, including flag.
- [x] Keep joined/first-seen/activity details visible for newly joined seated members.
- [x] Superseded: the initial right-click exact-view action was removed and replaced by the persistent right-rail Share exact view button.
- [x] Restore shared coordinates and camera perspective when opening the link.
- [x] Route new issue submissions into an online repository mirror immediately.
- [x] Replace pending issue placeholders with real mirrored issues after intake.
- [x] Make the repository-header Mirrors button open the full mirror status list.
- [x] Show mirror identity, endpoint, health, integrity, last check, and capabilities.
- [x] Security-review and clean up PR #57, `feat(chat): polish public chat workbench`.
- [x] Merge PR #57 after focused verification, then delete its merged branch/worktree.
- [x] Fix the white-on-white “Assign to taskboard” control contrast.
- [x] Add an organization-admin-only button to delete taskboard tasks.
- [x] Let assignees and managers mark taskboard tasks done while preserving time.
- [x] Include the last two minutes of Cloudflare logs in non-green component-attention emails.
- [x] Keep component-attention email delivery working when log retrieval is unavailable.
- [x] Make every status-history cell use the same height.
- [x] Add an Engineering-team-only Claude Code bot to repository Agents.
- [x] Let a current Engineering team member start its session on an eligible headless mirror.
- [x] Let current Engineering team members revise and send follow-up prompts to that session.
- [x] Fail closed until a Haiku intent check approves each submitted prompt.
- [x] Keep bot authorization, mirror dispatch, session history, and audit data encrypted and restricted to the current Engineering team.
- [x] Give the Claude bot an in-world avatar that walks around using Forkbot's bot-presence pattern.
- [x] Route `@claude` chat mentions into the same organization-scoped, Haiku-gated bot workflow.
- [x] Close stale Office attendance rows so absent members never remain “IN BUILDING”.
- [x] Show each live Office occupant's current floor on the attendance board.
- [x] Add an in-progress indicator and estimate to each active “What we're building” note.
- [x] Restore a safe outdoor zoom and camera angle when walking out of the Office.
- [x] Publish a progress blog post matching the existing ForkMesh cover-art style.
- [x] Raise the in-world ForkMesh blog stand and show four recent posts.
- [x] Add Codex beside Claude as an organization-scoped headless-mirror agent.
- [x] Route authorized `@codex` chat mentions into the agent workflow.
- [x] When Codex completes a tracked task, walk its World avatar to the board and move the note to Done.
- [x] Sync saved World views to the signed-in account so they follow the user across devices.
- [x] Sync player speed and other World preferences to the signed-in account across devices.
- [x] Keep local World settings as an offline fallback and merge them safely after sign-in.
- [x] Show organization agent tasks and running/stopped/merged/attention status on each mirror cabinet side.
- [x] Make the HTTP referrer board taller and show safe clickable referrer URLs grouped under each domain.
- [x] Open ForkMesh repository issues from the World in a full sidebar workbench with the web issue details, comments, and management actions.
- [x] Preserve the visitor's heading when leaving the Office instead of rotating them 180 degrees.
- [x] Add active-account autocomplete to Add member and clearly explain organization role/team access.
- [x] Boot revoked devices and clear ForkMesh session cookies, local caches, and account-scoped browser storage on sign-out.
- [x] Remove the repository-circle Add repo control, move issue/PR boards outside the follower orbit, and show follower avatars as circular orbiting icons.
- [x] Show verified ready/conflict/draft/merged/closed/unavailable status on every World pull-request card and an aggregate board summary.
- [x] Paginate the World issue and pull-request boards at 25 records per page with visible controls and wheel scrolling.
- [x] Update the World “What we're building” board as this round is completed and deployed.
- [x] Add a hashtagged Mastodon toot of at most 500 characters to every blog post, including image alt text in the prefilled share flow.
- [x] Add a prefilled X/Twitter post of at most 250 characters and a full-length Reddit post to every blog post.
- [x] Show a safe prefilled network link beneath each blog social draft.
- [x] Show a prominent World link with the live member count in the shared site header on every page, including every blog post.
- [x] Show total and unique view counts on every blog post.
- [x] Show a privacy-safe HTTP referrer leaderboard on a blog post only when that post has referrer traffic.
- [x] Make the top-right ForkMesh logo in the World an explicit page-refresh control.
- [x] Reveal the live World behind a small centered startup cover and animate real initialization progress so loading feels fast.
- [x] Add a bounded post-deploy grace period before transient repository verification failures become alerts.
- [x] Add a dedicated Worker errors row to the web and in-world system status histories.
- [x] Add a commit-activity chart beneath the in-world repository graph matching the web repository-list visual language.
- [x] Show current average commits per hour for today, this week, and this month on that activity chart.
- [x] Make the World mirror agent-task screen a full interactive prompt and re-prompt session workspace with Qt feature parity.
- [x] Restrict World agent prompt-session access to Engineering team members, including clear access-denied messaging.
- [x] Show all available agent/session detail in the World pull-out screen and report missing Claude/Codex binaries or login state.
- [x] Add a per-PR mergeability score and evidence factors to each in-world pull-request board card.
- [x] Restrict Claude/Codex chat visibility and invocation to Engineering team members in both server responses and World/chat UI.
- [x] Extend the Office glass doors to the full building height.
- [x] Remove the “Walk Right In” doorway sign.
- [x] Add an in-world bulletin board before continuing the remaining tasks.
- [x] Show remaining checklist items as marker-drawn sticky notes on the left.
- [x] Move completed notes to varied positions on the right and mark them with a hand-drawn X.
- [x] Remove the remaining invisible Office doorway collision wall and verify smooth entry/exit.
- [x] Make the walkway, bridge, and Office lobby meet at the same height with no gap.
- [x] Make the sound button a real master on/off control for active playback.
- [x] Verify the Office threshold geometry and sound toggle in the running world UI.
- [x] Move Office entry fully inside the jamb, join the doorway to the lobby collider, and show background access loading.
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

## 2026-07-28 audit additions

- [x] Render recent public X and Reddit posts inside scene-native World frames through the bounded same-origin social-feed snapshot; use Reddit's explicit application User-Agent and avoid injecting third-party tracking scripts into the WebGL page.
- [x] Keep ActivityPub followers in the large outer repository portrait ring, Git contributors in the smaller inner ring, and provide the floating repository Follow control.
- [x] Remove the Office Guide and oversized Marketing Studio banner objects from the Marketing room while retaining the room-scale task controls, desks, attendance calendar, and table.
- [x] Reconcile the later exact-view request: sharing stays on the right rail, Saved Views stays collapsed, and the normal right-click interaction is restored.
- [x] Route Issue and PR tower clicks into their record-specific canonical web workbenches, with narrowly scoped same-origin frame policy and no Repository portals preamble.
- [ ] Complete the remaining web PR parity listed in Current focus; do not expose a fake update-from-main control until an online mirror advertises and signs that mutation.
- [x] Finish the GitHub Primer restyle of the compact and expanded right-side World HUD.
- [ ] Publish role-specific website onboarding paths for QA, developers, and marketing; the Markdown getting-started, Qt, contribution, and independent peer-review guidance exists, but the public docs page does not yet present the role journeys requested today.
- [ ] Verify the live ActivityPub follower inventory against the production backup and restore any missing actors; migration and retention regression coverage are present, but this repository cannot prove that the external backup restore was run.

## 2026-07-28 current emergency and follow-up

- [x] Fix the deployed aerial-marker `MEMBER_CIRCLE_CENTER_Z` exception that
  aborted the animation loop before movement, and make optional LOD marker
  initialization fail closed without interrupting input.
- [x] Restore the complete transparent Office cutaway at every camera distance:
  glass, floor slabs, walls, lighting, and furniture remain visible outdoors,
  while entered visitors render only their active floor.
- [x] Keep every World section visible at every zoom level, including
  repository rings and live layers, the complete public-board circle,
  organizations, fediverse displays, landscaping, and plaza fixtures; aerial
  optimization now reduces lighting cost without removing content.
- [x] Center the SOL sign and deterministically reflow live node cabinets into
  complete, evenly spaced rings after every join, refresh, or deletion.
- [x] Remove the round Town path plaza and overlapping edge/path slabs; join
  the four routes with one concrete-brick junction and terminate district
  paths exactly where their matching promenade begins.
- [x] Return Office floors and ceilings to solid finishes and use batched grids
  of small ceiling fixtures with restrained local lighting.
- [x] Add a Primer-style top-toolbar Dashboard control beside the avatar that
  opens the operations console in a safe new tab.
- [x] Restore the SOL treasury QR/sign at the exact center of the node rings,
  including its Start a node download action.
- [x] Deploy and verify the durable Office/SOL emergency production revision
  `2d3602646909`.
- [ ] Verify the new aerial landmark LOD in production and confirm that zooming
  out no longer produces red frame-time/draw-call diagnostics.
- [ ] Finish end-to-end organization-admin bot-token QA: one-time secret,
  default scoped permissions, local-computer binding, expiry, audit, and
  immediate revocation.
- [ ] Verify platform `is_admin` web-to-Qt agent dispatch against an owned,
  currently running desktop node; retain provider leases and the fail-closed
  tool-free Haiku preflight.
- [ ] Add the signed-in owner's private desktop-only CPU, RAM, and disk card to
  the World HUD. Never include headless-node telemetry or broadcast these
  owner-only values in presence.
- [ ] Complete embedded World chat QA: pinned multiline prompt, consistent
  Primer contrast/fonts, unclipped Send, team-task creation, repository-issue
  creation, and organization/user repository aliases only.
- [ ] Complete the remaining web PR lifecycle parity item in Current focus.
- [ ] Publish the role-specific QA/developer/marketing onboarding page.
- [ ] Verify and, if needed, restore the production ActivityPub follower backup.

The remaining implementation work stays in **Current focus**. The Cloudflare
observability credentials and “ForkMesh Forever” social-proof publishing remain
the two canonical unchecked human actions above; they are not duplicated here
so their completion state cannot drift.

## Requested follow-up work

- [x] Redesign the exterior World as a mixed city-and-woodland landscape with stone plazas, paths, varied grass patches, trees, broad continuous land connections, finished edges, and visible dirt/roots underneath.
- [x] Make mirror4 appear automatically in Qt, the World, and the repository mirror catalog after provisioning.
- [x] Make future Vultr mirrors complete first-run setup automatically, including dependencies, state directories, capacity checks, signed catalog publication, and a truthful readiness probe.
- [x] Retire the exact mirror4 and mirror5 Vultr instances and remove their saved Qt Host entries after confirming their identities.
- [x] Verify mirror6 on its 1 GB Vultr plan is linked, signed into the repository catalog, and represented by the World mirror-cabinet data.
- [x] Make Qt host probes find Claude Code and Codex in their standard per-user install directories.
- [x] Require successful Vultr provisioning to wait for the mirror's signed public catalog record instead of stopping at SSH/systemd success.
- [x] Use 1 GB as the minimum Vultr mirror plan for all future provisioning.
- [x] Persist and show each managed host's provider, plan/type, display name, and estimated cost in the Qt Hosts list.
- [x] Restore each node's Clones and Websites metrics and verify that newly provisioned mirrors report both values.
- [x] Queue Claude organization-agent work only on mirrors that have reported the Claude Code binary installed; currently only mirror6 is eligible.
- [x] Restore the World fresh-code effect.
- [x] Add an Executive-team-only Office floor with a strategy room, organization map, decision table, chairs, collision footprint, elevator destination, and attendance label.
- [x] Update the organization-access explanation so Claude/Codex session access is described as Engineering-team-only.
- [x] Verify and enforce that non-Engineering organization members cannot start, review, continue, or view Claude/Codex sessions.
- [x] Show each node cabinet's recent Actions runs, bounded redacted log tails, and running/done/error state on its back with restrained status animations and a full authorized detail list.
- [x] Add an admin-only error-log analytics view that groups equivalent errors and charts their occurrences over the previous 24 hours.
- [x] Move the recent roughly 50-repository bulk-import set back onto its own World island before considering deletion.
- [x] Identify and delete only the 54 repositories imported in that batch after verifying the exact cohort, temporarily isolating it in the World, and preserving a checksum-verified upstream/HEAD recovery archive.
- [x] Move Marketing desks to the front windows, face them outward, correct chair orientation, and use small desktop name plaques.
- [x] Crop and embed the supplied ForkMesh cube logo beneath the epoxy surface of the round Marketing table.
- [x] Fix the member chest Fediverse feed when it reports unavailable.
- [x] Make Add wallet open the signed-in user's profile wallet editor.
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

The code from these screenshot sessions has been integrated into `main`, and
their verified merged source branches/worktrees were removed after the
recoverability checks recorded above.

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

- [x] Run focused worker API tests.
- [x] Run focused Qt pull-request and issue-sync tests.
- [x] Run the relevant build/static checks.
- [x] Confirm the worktree contains only intentional changes.
