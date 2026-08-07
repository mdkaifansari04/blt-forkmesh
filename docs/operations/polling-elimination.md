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
| Unread counts, all three surfaces (`refreshWebAlerts`, `startNotificationPolling`, `refreshDirectMessages`, `markChatActivitySeen`) | desktop ping inbox every 5m off the heartbeat; /world ping digest every 60s; /chat conversation list every 30s and the chat-activity counters every 60s | One read when the surface opens, then push-only. `notify_account_event(env, owner, topic)` fans a payload-free frame to the account's `ForkMeshNodes` DO from the only two writes that can move a count: `enqueue_notification` (`pings`) and `_chat_direct_message_retained` (`direct-messages`, to the participant who did not send it). Browsers join that same per-account socket with a 60s ticket from `GET /api/accounts/event-ticket` (a WebSocket upgrade carries no `Authorization` header) through the shared `public/account-events.js`; the desktop already held it. No fallback poll — one catch-up read per (re)connect. The header chat badge's baseline now advances locally off the room socket (`noteSeenChatActivity`) while chat is open, and is re-read absolutely once more on `visibilitychange`/`pagehide` — two open tabs share one localStorage baseline and would each bump it for the same message, so that read on the way out is the drift correction the 60s timer used to provide. That 30s re-read also discovered lists that changed for somebody else's reason, so a channel invite and a newly-opened conversation push `private-channels` / `direct-messages` too (via `runtime.notify_account`) rather than quietly becoming reload-only. |
| Go mirror node git sync (`daemon.go` `syncLoop`) | upstream fetch + catalog publish every 30s | Push-driven: the relay fans a `commits` event to mirrors when a source publishes a moved public head (catalog publish path, `source == "local-node"` only, so mirror republishes cannot loop); the daemon fetches on that push, on reconnect catch-ups, and after each intake-worker exit. The `syncInterval` ticker survives only for third-party upstreams the relay cannot push for (or with the catalog disabled). The 4m heartbeat republish/lease-renewal cycle reads only local refs — liveness, exempt. |

## Related: startup bursts (not polls, same symptom)

A client that makes one request per *row* instead of one per *topic* produces
the same relay load a poll does, just all at once. The desktop's organization
task board is push/action-driven and reads `GET /api/tasks` exactly once per
launch — but until adhoc #1618 it then published each agent session's live run
state as its own signed `POST /api/tasks/<id>/agent-status`, so a node with a
fleet of sessions opened seventeen writes in the same millisecond and the relay
rate-limited (HTTP 429) most of them, its own account lookup included. The
states now coalesce into one `POST /api/tasks/agent-status` carrying every
changed run. When auditing a client path, count requests per *event*, not just
timers per minute.

## Inventory: still polling (relay-facing)

### Qt desktop client

| # | Poller | Interval | Endpoint(s) |
| --- | --- | --- | --- |
| 3 | `m_adminPollTimer` pending-user verification (`MainWindowSetup.cpp:1894`) | 5m (admins only) | pending-users API |
| 4 | `m_chatDirectoryTimer` (`MainWindowMessages.cpp:1018`) | 60s | `GET /api/accounts/users` (edge-cached) — the registered-user directory, deliberately left standing when the unread counts moved to pushes: an account that has never spoken has no socket frame to announce it, so this one needs a signup push first |
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
| 13 | `WORLD_EVENT_POLL_MS` community events | 3m |
| 14 | `MIRROR_STATUS_POLL_MS` mirror catalogs | 5m |
| 15 | `MIRROR_ACTIONS_POLL_MS` action runs | 20s |
| 16 | `WORLD_QA_POLL_MS` / `WORLD_BUILD_BOARD_POLL_MS` / `WORLD_STATUS_POLL_MS` | 15s / 60s / 60s |
| 17 | `ADMIN_ERROR_POLL_MS` admin error feed | 15s |
| 18 | `REPOSITORY_IMPORT_POLL_MS`, `WORLD_DEPLOY_STATUS_POLL_MS`, `WORLD_ELEMENT_DEPOSIT_POLL_MS`, media/social timers | 2m / 2.5s / 4s / var |

(#12, personal notifications, is eliminated — see the table above.)

Chat pages (`chat.js`, `dashboard-chat.js`) are WebSocket-driven, including
their unread counts. What is left on a timer there is not a count: the 30s
`renderPeople` tick is a local re-render, the 60s `PRESENCE_INTERVAL_MS` beat is
a liveness proof the room DO requires, and the 60s `USERS_DIRECTORY_REFRESH_MS`
tick is the same never-spoken-account problem as desktop #4.

### Server side

| # | Poller | Notes |
| --- | --- | --- |
| 19 | Relay cron `* * * * *` (`wrangler.toml`) | scheduled work, not client polling; audit which jobs could be event-triggered from the write path instead |

Already event-driven: the SSH post-receive refresh path
(`packaging/systemd/forkmesh-mirror-refresh.path` is an inotify path unit;
`ssh-refresh-notify` pushes).

## Elimination plan

**Phase 1 — desktop client goes push-only (mirrors of the intake pattern).**
Done for the big ones (see the eliminated table): the `m_inboxPollTimer`
fallback is deleted, `fetchMirrorPendingCounts` is fetch-once with
push/drain/open invalidation, and `refreshWebAlerts` reads the ping inbox once
per run behind the new `pings` topic. Remaining: include the per-topic tally in the
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
calls — and move #9, #10, #13–#16 onto it. Personal notifications (#12) are
done, and they turned out not to need a new socket at all: the browser joins
the desktop's per-account `ForkMeshNodes` channel with a short-lived ticket
(`public/account-events.js`), which is now the obvious home for any other
account-scoped web poller. Session validity (#11) needs no timer at all: check on
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
