# Polling inventory and elimination plan

Goal: no ForkMesh client polls the relay for data. Every "did anything
change?" question becomes a payload-free push over a WebSocket to a Durable
Object, answered by the one signed HTTPS request the client would have made
anyway. The push transport already exists — the per-owner `ForkMeshNodes` DO
behind `GET /api/nodes/events` (desktop/headless nodes) plus the chat, world,
office and note room DOs on the web side — so the work is moving each poller
onto it, not building new infrastructure.

Two rules from the mirror-node conversion (2026-08-06), which is the template
for the rest:

- **No fallback polls.** A "safety net" poll quietly becomes the load. The
  event socket owns liveness instead: reconnect is unconditional with bounded
  backoff, and every successful (re)connect fires one catch-up event that
  drains anything queued while the channel was down.
- **Heartbeats are not polls.** A request whose purpose is to *prove this
  client is alive* (presence heartbeat, endpoint lease renewal) cannot be
  replaced by a server push, only tuned. This plan leaves them in place and
  marks them as such.

## Status: eliminated

| Poller | Was | Now |
| --- | --- | --- |
| Go mirror node intake bridge (`mirror_node/intake.go`) | `GET /api/repo/*/pending` every 5s per node — the busiest endpoint on the relay | Push-only: one `EventSocket` (`mirror_node/nodeevents.go`) to `wss://…/api/nodes/events`; worker starts on push, stops `intakeIdleGrace` after the last one. `intakePollInterval` is parsed but ignored. No fallback poll. |
| Qt headless mirror-bridge worker badges | fleet-wide `/pending` probes through every refresh tick | already skipped when headless (`MainWindowReleases.cpp` `fetchMirrorPendingCounts`) |
| Qt desktop fallback inbox sync (`m_inboxPollTimer`) | `GET /api/sync` every 5m (15m while the event socket was up) | Timer deleted. The `NodeEventSocket` is the only sync trigger: push → debounced sync, plus one catch-up sync per (re)connect and one 20s post-launch pass. |
| Qt desktop `/pending` badge fetch (`fetchMirrorPendingCounts`) | refetched on a 10m TTL through every roster/refresh tick | Fetch-once per repo; the cache entry is invalidated only by a node event push, a local drain, or an explicit repo open (user action). Steady-state ticks reuse the cache with no HTTP. Folding the tallies into the event frame itself (dropping the GET) remains open. |
| Qt desktop office chat backlog (`OfficeChannelMirror`) | `GET /api/chat/channels` + a `/history?since=` per mirrored room every 30s — the client asking "any unread messages?" on a beat | Timer deleted. One channel-list + backlog pass per run seeds each room (and its key); the room's ticketed WebSocket is then held open, so later messages — the ones that light the chat unread badge — arrive as pushes. Reconnect is unconditional with bounded backoff and fires one catch-up history read; the channel list re-lists only on a user action (opening Chat, 10s floor) or an account switch. Live sockets are budgeted to the first 16 rooms; the rest still load on open/send, and the cap is logged. |
| Go mirror node git sync (`daemon.go` `syncLoop`) | upstream fetch + catalog publish every 30s | Push-driven: the relay fans a `commits` event to mirrors when a source publishes a moved public head (catalog publish path, `source == "local-node"` only, so mirror republishes cannot loop); the daemon fetches on that push, on reconnect catch-ups, and after each intake-worker exit. The `syncInterval` ticker survives only for third-party upstreams the relay cannot push for (or with the catalog disabled). The 4m heartbeat republish/lease-renewal cycle reads only local refs — liveness, exempt. |

## Inventory: still polling (relay-facing)

### Qt desktop client

| # | Poller | Interval | Endpoint(s) |
| --- | --- | --- | --- |
| 3 | `m_adminPollTimer` pending-user verification (`MainWindowSetup.cpp:1894`) | 5m (admins only) | pending-users API |
| 4 | `m_chatDirectoryTimer` (`MainWindowMessages.cpp:1018`) | 60s | `GET /api/accounts/users` (edge-cached) |
| 5 | `m_directMirrorRegistrationTimer` (`MainWindowControlNode.cpp:743`) | 5m | mirror registration renewal |
| 6 | `m_noteRefreshTimer` (`MainWindowNotes.cpp:278`) | 3s while a synced note is open | note read API |
| 7 | `m_agentLimitsTimer` (`MainWindowSettings.cpp:1661`) | 60s | agent limits API |
| 8 | `m_homeStatsTimer` / `m_repoChangeBadgeTimer` / `m_controlNodeRefreshTimer` | 60s / 10s / 10s | mostly local git/process reads; audit for hidden HTTP |

Heartbeats (keep): `m_heartbeatTimer` 60s reward-eligibility presence
(`MainWindowSetup.cpp:1751`), `m_relayLatencyTimer` 60s diagnostics ping.

### Website — dashboard

| # | Poller | Interval | Endpoint(s) |
| --- | --- | --- | --- |
| 9 | pending-badge re-poll while a repo shows pending items (`06-repo-content.js`, `pendingInboxRefreshes`) | 10m | `GET /api/repo/*/pending` |
| 10 | `repoMirrorPollTimer` (`07-repo-compose-branch.js:985`) | 5m while the Mirrors tab is visible | repo mirrors API |
| 11 | session validity check (`02-helpers.js:165`, also `site-header.js:246`) | 20s | session validate (only a 401 acts) |

### Website — /world

All in `cloudflare_worker/public/world/world.js` (constants near the top);
each is document-visible-only but still a steady per-tab drumbeat:

| # | Poller | Interval |
| --- | --- | --- |
| 12 | `WORLD_NOTIFICATION_POLL_MS` personal notifications | 60s |
| 13 | `WORLD_EVENT_POLL_MS` community events | 3m |
| 14 | `MIRROR_STATUS_POLL_MS` mirror catalogs | 5m |
| 15 | `MIRROR_ACTIONS_POLL_MS` action runs | 20s |
| 16 | `WORLD_QA_POLL_MS` / `WORLD_BUILD_BOARD_POLL_MS` / `WORLD_STATUS_POLL_MS` | 15s / 60s / 60s |
| 17 | `ADMIN_ERROR_POLL_MS` admin error feed | 15s |
| 18 | `REPOSITORY_IMPORT_POLL_MS`, `WORLD_DEPLOY_STATUS_POLL_MS`, `WORLD_ELEMENT_DEPOSIT_POLL_MS`, media/social timers | 2m / 2.5s / 4s / var |

Chat pages (`chat.js`, `dashboard-chat.js`) are already WebSocket-driven; the
remaining 30s `renderPeople` tick is a local re-render, not a fetch.

### Server side

| # | Poller | Notes |
| --- | --- | --- |
| 19 | Relay cron `* * * * *` (`wrangler.toml`) | scheduled work, not client polling; audit which jobs could be event-triggered from the write path instead |

Already event-driven: the SSH post-receive refresh path
(`packaging/systemd/forkmesh-mirror-refresh.path` is an inotify path unit;
`ssh-refresh-notify` pushes).

## Elimination plan

**Phase 1 — desktop client goes push-only (mirrors of the intake pattern).**
Done for the two big ones (see the eliminated table): the `m_inboxPollTimer`
fallback is deleted and `fetchMirrorPendingCounts` is fetch-once with
push/drain/open invalidation. Remaining: include the per-topic tally in the
event frame itself — the relay already knows the counts at write time
(`notify_repo_host` fires on every submission) and the frame stays tiny and
content-free — so the desktop's `/pending` GET disappears entirely. #3–#7
each become a topic on the same socket (`admin-pending`, `directory`,
`notes`, `agent-limits`); the relay's write paths already call
`notify_repo_host` hooks where most of these change.

**Phase 2 — mirror node git sync.** Done (see the eliminated table): the
catalog publish path fans a `commits` event to mirrors on a moved source
head, the daemon's `syncRequests` channel is subscribed to the shared
`EventSocket`, and the fetch ticker only survives for third-party upstreams
the relay cannot push for. The 4m endpoint lease renewal stays — it is a
liveness proof.

**Phase 3 — website.** The dashboard and world already hold sockets on several
pages (chat, world, office, notes). Add a read-only, unauthenticated
"repo events" channel — one DO per repo, fed by the same write-path notify
calls — and move #9, #10, #13–#16 onto it. Personal notifications (#12) belong
on an authenticated per-account socket (the world already opens one for
presence; piggyback). Session validity (#11) needs no timer at all: check on
`focus`/`visibilitychange` (both hooks already exist) and on any 401 from a
real request. Fast operational views (#15, #17, deploy status) move to the
socket last, since they are visible-tab-only and cheap.

**Phase 4 — relay cron audit.** For each minute-cron job, ask "which write
made this necessary?" and trigger it there (or via a DO alarm set by the write
path). Cron stays only for true time-based work (reward intervals, expiries).

Order matters: phase 1 removes the largest authenticated-poll population,
phase 2 the steadiest background load, phase 3 the long tail of anonymous
per-tab traffic. After each phase, the API traffic chart — the same one that
caught `/pending` — is the acceptance test: the eliminated endpoint's request
group should drop to ~zero, not merely shrink.
