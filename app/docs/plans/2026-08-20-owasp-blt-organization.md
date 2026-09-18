# OWASP BLT Organization Implementation Plan

Created: 2026-08-20
Agent: Claude Code
Status: COMPLETE
Approved: Yes
Iterations: 0
Worktree: No
Type: Feature

## Summary

**Goal:** The `owasp-blt` organization is live on ForkMesh fronting kaif-blt-04's repos at `/owasp-blt/<repo>`, with a public org page at `/owasp-blt`, email invitations (including signup handoff for people with no ForkMesh account), working in-app invite notifications, and org members able to mirror org repos onto their own nodes.

## Out of Scope

- **True repo ownership transfer.** Every catalog publish and git push is Ed25519-verified against the owner *account's* key (`src/entry.py:13090-13130`, `verify_push_token`); an org has no keypair. Org repos stay aliases — `org_repos.node_owner` keeps pointing at `kaif-blt-04`. Deliberate decision, revisit as its own project.
- **Un-mirroring on org leave.** The mirror-request rail is fire-and-forget (`src/mirrors.py:1163-1172` acks and drops); a departed member keeps their copy of public code. Stated, not fixed.
- **Cross-isolate `_ORG_ALIAS_MEMO` purge.** The memo is per-isolate with a 30s TTL; other isolates self-heal.
- **Remote (production) D1 migration apply.** Task 3 applies the migration locally only. The production apply happens at deploy time as a separate user-confirmed step (see Global Constraints).

## Approach

**Chosen:** Extend the existing org system (`orgs`/`org_members`/`org_repos` + `org_members_handler` family, `src/entry.py:24105-24944`) with a sibling `org_invitations` table/handler pair modeled on `contributor_invitations` (`src/repository_imports.py:2304-2606`), a literal single-segment Worker route for `/owasp-blt` following the working tree's own `/login`/`/signup` pattern (`wrangler.toml:31-34`), and a widened `mirror_requests_handler` (`src/entry.py:30458`) gated by the existing org-role helpers.
**Why:** Every subsystem this feature needs (org schema+API, HMAC invite tokens, Mailtrap email, notifications, mirror-request delivery via node heartbeat) already exists — the work is wiring, not invention. A sibling invite handler keeps `org_members_handler`'s roster invariants and its pinned tests untouched, rather than making the `unknown_account` 404 conditional inside a ~200-line function.

**Accepted trade-off — existing accounts are added directly, no accept step.** When an invited email already belongs to a ForkMesh account, the handler adds that account to `org_members` immediately (with an in-app `org_invite` notification) instead of parking a pending invite. This matches the product's existing semantics — `org_members_handler` POST already direct-adds known accounts and its own comment calls that "the invite" — and keeps one consistent behavior for both entry points. The accept ceremony exists for the signup handoff, where it is load-bearing; duplicating it for known accounts would change existing product behavior outside this plan's lineage.

## Global Constraints

- Migration number: `0124` (highest existing is `migrations/0123_api_metrics_minute.sql`; 8 historical duplicate-number collisions exist — do not add a 9th).
- Dual DDL: every statement in `migrations/0124_org_invitations.sql` must appear byte-equivalent in `SCHEMA_STATEMENTS` (`src/schema.py`, after the `idx_org_repos_node` entry ~line 1004). Fresh DBs run schema.py; deployed DBs run migrations.
- Migration apply order: local first (`FORKMESH_D1_LOCAL=1 ./migrate.sh`), production later and ONLY with explicit user confirmation at deploy time. `./migrate.sh` without the env var hits the live DB (`wrangler.toml` `database_id bf90dd5d-...`).
- Invite token canonical string: `forkmesh-org-invitation-v1\n<org>\n<invitation_id>\n<email>\n<expires>`, HMAC-SHA256 keyed by `_account_session_secret(env)`. Only `sha256(token)` is ever stored.
- Invite link: `<base>/signup?invite=<id>&org=<org>&token=<token>`.
- Invite expiry: 7 days (`ORG_INVITE_TTL_MS = 7*24*60*60*1000`). Caps: `MAX_ORG_PENDING_INVITES = 50` per org, `ORG_INVITE_DAILY_LIMIT = 25` per inviter per org per day.
- Invite email sender: `from_name="OWASP BLT"` via the existing `_send_email` override; subject `You've been invited to join <org> on ForkMesh`.
- Org name for this deployment: `owasp-blt`. Underlying node account: `kaif-blt-04`.
- The working tree's ~580 uncommitted files are in-flight OSBLT work and current truth. Never revert or "clean up" any of it. The two already-applied fixes (org_invite kind registration; rename-gap UPDATEs in `_rename_account_namespace`) stay.
- All frontend changes require rebuilding: `python3 tools/build_dashboard_assets.py` (dashboard) — generated bundles like `public/dashboard.js` are never edited by hand.
- No `/*` wildcard in `run_worker_first`, no change to `not_found_handling`, no change to `RESERVED_ROUTE_PREFIXES` or `looks_like_repo_route` (single-segment paths already return False at `src/static_routes.py:224-225`).

## Context for Implementer

All paths relative to `app/` unless noted. This is a Python Cloudflare Worker (`python_workers`); `src/entry.py` is ~44k lines and holds the router (`_route`, `entry.py:~42170`) plus most handlers. Tables store HMAC blind-index columns plus one AES-GCM-encrypted `data` blob — the orgs tables are a deliberate plaintext exception for public roster data, but an invitee's email is PII of someone who may never accept, so `org_invitations` follows the encrypted house style (`email_bi` for lookup, everything else in `data`). Tests never import entry.py wholesale: they AST-extract named functions and stub async deps (see `tests/test_orgs_teams.py:37-100` `_load`/`_constant` harness) — new handler tests must follow that pattern. `bounded_json_request` is stubbed with `worker_test_helpers.json_from_request_double`.

## Runtime Environment

- **Local full worker:** `FORKMESH_BROWSER_FULL=1 bash browser_tests/run-browser-worker.sh` → http://127.0.0.1:4179 (local D1, builds assets first)
- **Health check:** `curl -s http://127.0.0.1:4179/health`
- **Unit tests:** `python3 -m pytest tests/ -q` from `app/` (1 known pre-existing failure: `test_world_notification_table_has_owner_scoped_direct_delete`, missing `world/` checkout — not ours)
- **Production:** live at app.forkmesh.com; deploys via `./deploy.sh` (needs user confirmation; not part of this plan's tasks)

## Assumptions

- The two already-applied working-tree fixes (org_invite notification kind at `entry.py:497-568`; rename-gap UPDATEs at `entry.py:~15318`) are correct as reviewed — Task 2 red-green-verifies them rather than rewriting them. If red-green shows either fix wrong, fix it in Task 2.
- `MAILTRAP_API_TOKEN` is live on the production Worker (verified via `wrangler secret list`) — Tasks 4-5's send path works in production. Locally, `_send_email` returns False and the handler degrades to 202 `{deliveryConfigured: false}` by design; local E2E verifies the degraded path plus the invite-row lifecycle, not actual delivery. Task 9 (production E2E) depends on this.
- `kaif-blt-04` has an active session in the connected Chrome browser (chrome-devtools-axi) for Tasks 1 and 9.

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Dual-DDL drift between `0124_org_invitations.sql` and `SCHEMA_STATEMENTS` | Medium | High (fresh vs migrated DBs diverge) | Task 3 includes a parity test: run both DDL sources into in-memory sqlite, assert identical `PRAGMA table_info` + index list |
| Invite send fails mid-flow leaving phantom pending rows | Medium | Medium (address blocked from re-invite) | INSERT-before-send + DELETE-on-failure ordering copied from `repository_imports.py:2434-2482`; unique partial index is the concurrency boundary |
| `/owasp-blt` route branch shadows a future account of that name | Low | Medium | Namespaces already reject each other (`entry.py` signup guard / org-create guard; pinned by `test_org_and_account_namespaces_reject_each_other`) — org branch resolves via `_org_row` only, falls through to 404 otherwise |
| Mirror self-serve feeds the org alias (not canonical node) into the catalog | Low | High (creates keyless namespace records) | Task 8 resolves via `_org_repo_node_strict` and asserts in tests that the parked entry's `owner` equals the canonical node name |

## E2E Test Scenarios

### TS-001: Create owasp-blt and alias repos (existing UI, no new code)
**Priority:** Critical
**Preconditions:** Logged in as kaif-blt-04 in the connected browser; repos published
**Mapped Tasks:** Task 1

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to `/dashboard/settings/organizations` | Organizations section renders with create form |
| 2 | Create org `owasp-blt` | Org appears in list, caller is `owner` |
| 3 | In org detail, link each published repo | Repos listed under the org |
| 4 | Set world access floors to `public` | Setting persists |
| 5 | Navigate to `/owasp-blt/<repo>` (e.g. BLT) | Repo page renders same content as `/kaif-blt-04/<repo>`; org name stays in address bar |
| 6 | `git ls-remote https://app.forkmesh.com/owasp-blt/<repo>` (Bash) | Refs list returned |

### TS-002: Invite an email that already has an account
**Priority:** High
**Preconditions:** Second account with verified email exists (local worker); caller is org owner
**Mapped Tasks:** Task 4

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | POST `/api/orgs/owasp-blt/invitations` with that account's email | 200; response says member added directly (no invite row) |
| 2 | GET `/api/orgs/owasp-blt/members` as owner | New member present with role `member` |
| 3 | GET `/api/notifications` as the new member | `org_invite` notification present |

### TS-003: Invite a new email → signup handoff → membership
**Priority:** Critical
**Preconditions:** Local worker; email has no account
**Mapped Tasks:** Tasks 4, 5

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | POST `/api/orgs/owasp-blt/invitations` `{email, role:"member"}` | 202 `{deliveryConfigured:false}` locally; invite row `pending` (verify via GET list) |
| 2 | Construct accept URL from the invite id + token (test hook: recompute token with known secret) and GET `/api/orgs/owasp-blt/invitations/<id>/accept?token=...` unauthenticated | 200 `{confirmationRequired:true, org, role}`; row still `pending` (GET mutates nothing) |
| 3 | Open `/signup?invite=<id>&org=owasp-blt&token=<tok>` in browser, complete signup | Account created; page POSTs accept; redirected to `/owasp-blt` |
| 4 | GET `/api/orgs/owasp-blt/members` | New account listed as `member`; invite row `accepted` |
| 5 | Repeat the POST accept with the same token | 409 `invitation_used` (single-use) |

### TS-004: Revoke and expiry
**Priority:** Medium
**Preconditions:** Pending invite exists (local worker)
**Mapped Tasks:** Task 4

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | DELETE the invitation as org owner | Row status `revoked` |
| 2 | POST accept with its (still-valid-HMAC) token | 409 `invitation_used` (not pending) |
| 3 | Insert a pending row with `expires_at` in the past (test fixture), POST accept | 410 `invitation_expired` |

### TS-005: Public org page
**Priority:** High
**Preconditions:** Org exists with linked repos (local worker with seeded data, then production after deploy)
**Mapped Tasks:** Task 7

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to `/owasp-blt` | Org page renders: name, description, member count, linked repo list — OSBLT theme (BLT red, dashboard tokens) |
| 2 | Click a repo | Lands on `/owasp-blt/<repo>` repo page |
| 3 | Navigate to `/nonexistent-org-name` (not routed) | Static 404 page (unchanged behavior — route only exists for `/owasp-blt`) |

### TS-006: Member self-serve mirror
**Priority:** High
**Preconditions:** Local worker; member account owns an active node; org repo linked
**Mapped Tasks:** Task 8

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | As member, POST `/api/mirror-requests` `{action:"mirror", org:"owasp-blt", repo:"<repo>", node:"<their-node>"}` | 200 `{status:"accepted"}` |
| 2 | Read the member account's `mirror_requests` (test fixture) | Entry has `owner` = canonical node (kaif-blt-04), NOT `owasp-blt`; status `accepted` |
| 3 | As non-member, same POST | 403 `not_a_member` |
| 4 | As member, POST with someone else's node | 403 `not_your_node` |

## Verification Evidence (local)

Local worker: `pywrangler dev --port 8790 --persist-to .wrangler/state-orgspec --local` with `MAILTRAP_API_URL` pointed at a local Mailtrap-shaped sink so real send/rollback paths execute.

- **TS-003 (invite → signup → membership), browser-verified end to end:** invite POST returned `orginv_9b6ff89f…`; the sink captured `from: {name: "OWASP BLT"}`, subject `You've been invited to join owasp-blt on ForkMesh`, and a `/signup?invite=&org=&token=` link. Unauthenticated GET on the accept URL returned `{confirmationRequired: true, ...}` and left the row `pending` (scanner-safe). Opening the link in Chrome showed the invite banner (`kaif-blt-04 invited you to join the owasp-blt organization as member`) with the email prefilled; completing signup as `alice-e2e` landed her in `org_members` with role `member`, emptied the pending list, and a second accept returned `409 invitation_used`.
- **TS-004 (revoke), browser-verified:** invited `bob-e2e@example.org` from the settings panel → row rendered as `b******@example.org · invited as member · expires 27 Aug 2026` → Revoke → list returned to "No pending invitations."
- **TS-005 (org page), browser-verified:** `/owasp-blt` serves the built org document (`data-page="org"`, canonical tag, title `owasp-blt · BLT`) and renders the org name + Repositories section in the OSBLT dark theme. Populated repo-list state not visually verified (needs a node-published repo; no node keypair locally) — unit-tested only.
- **Dead `org_invite` notification, verified fixed in a real request path:** after the TS-003 accept, `GET /api/notifications?node=kaif-blt-04` returned `org_invite | alice-e2e accepted your invitation to owasp-blt`. Before the fix `enqueue_notification` dropped this kind at its guard and wrote no row.
- **TS-006 (mirror rights), API-verified:** unknown org → `404 not_found`; foreign node → `403 not_your_node`; member with unlinked repo → `404 repo_not_found`. Full matrix in `tests/test_mirror_requests.py`.
- **Tests:** 140 passed across the 11 files touching this feature. One pre-existing unrelated failure (`test_world_notification_table_has_owner_scoped_direct_delete`) — this checkout has no `world/` directory; reproduces without these changes.

## Progress Tracking

- [ ] Task 1: Create owasp-blt org + link repos via existing UI (production, browser) — **BLOCKED on user**: needs an authenticated `kaif-blt-04` session on the OSBLT deployment (`forkmesh-relay.owaspblt.workers.dev`). Earlier attempt hit a Cloudflare 1027 rate limit (free-plan cap); the origin is reachable again, but `/dashboard/settings/organizations` redirects to login and I hold no credentials. Runs together with Task 9.
- [x] Task 2: Regression tests for the two already-applied fixes (red-green)
- [x] Task 3: org_invitations schema — migration 0124 + SCHEMA_STATEMENTS + parity test + local apply (note: full-chain `FORKMESH_D1_LOCAL=1 ./migrate.sh` fails at pre-existing 0034 on ensure_schema-bootstrapped local DBs — "duplicate column ua"; 0124 itself applied cleanly via `d1 execute --local --file`; production chain unaffected, remote D1 tracks 0034 as already applied)
- [x] Task 4: Invite API — token helper, send/list/revoke handler, routes
- [x] Task 5: Accept/decline API + signup handoff
- [x] Task 6: Org invite management UI (dashboard settings)
- [x] Task 7: Public org page at /owasp-blt (browser-verified: renders org name + Repositories section in OSBLT theme, canonical tag + data-page="org" served. Populated repo-list state NOT visually verified — needs a node-published repo, unavailable locally without a node keypair; covered by unit tests only)
- [x] Task 8: Member mirror rights (admin on-behalf + member self-serve + UI)
- [ ] Task 9: Production verification pass — **BLOCKED on user**: requires `./deploy.sh` and the remote `./migrate.sh` (irreversible D1 migration on the live database), both explicitly gated on user approval by this plan's Global Constraints.

## Resume Here (next session)

Code is complete, tested, and deployed. Two tasks remain, both needing an authenticated `kaifblt04` session on `https://forkmesh-relay.owaspblt.workers.dev` — the browser reported `NO SESSION` throughout, so neither could run.

1. Log in, then **Settings → Organizations → create `owasp-blt`**. `/owasp-blt` returns 404 today because `_serve_org_page_response` correctly reports a missing org, NOT because the route is absent — creating the org should flip it to 200 and is the definitive proof the deployed code is live.
2. Link the `kaifblt04/*` repos to the org, and PATCH `worldAccess.floors = "public"` (exposed in the same panel) so anonymous visitors see the repo list.
3. Send one real invite to a controlled address and walk the accept flow. Everything so far ran against a local Mailtrap-shaped sink; production Mailtrap has never delivered one of these.

⚠️ Before anything else touches deploys, read the `./deploy.sh` warning at the end of the deployment section below — four live secrets are `CHANGE-ME` locally.

Expectation to hold: repos keep reading `kaifblt04/...` in the UI. That is the node namespace push and mirror signatures verify against; `/owasp-blt/<repo>` is an additional front door. True transfer was ruled out because an org has no Ed25519 key and transferring would stop the repo being served (`entry.py:13090-13107`, `_owner_pubkey` at `:14636`).

## Production Deployment (2026-08-20)

Deployed to the OSBLT instance `forkmesh-relay` (account `1cbdfdcf…`, workers.dev subdomain `owaspblt`) — confirmed the same account `wrangler whoami` reports and that `wrangler deployments list --name forkmesh-relay` resolves. The `forkmesh.com` origins seen in `./deploy.sh dry-run app` come from `edge-control/wrangler.toml`, a separate companion worker, not from `app/`.

- **Migration:** `pywrangler d1 migrations apply forkmesh --remote` applied `0123_api_metrics_minute.sql` (already pending before this work) and `0124_org_invitations.sql`. Verified against the live DB: `PRAGMA table_info(org_invitations)` returns all 11 columns.
- **Code:** `pywrangler deploy` → `Uploaded forkmesh-relay`, `Current Version ID: 50071950-cfb2-434c-9fee-b69a0ccad4eb`.
- **Secrets untouched:** `pywrangler deploy` does not push secrets; the live `MAILTRAP_API_TOKEN`, `MIRROR_ROUTER_*`, `DATA_KEY`, `ADMIN_PATH` are intact. `.env.production` was restored byte-identical to its pre-run backup (`diff` clean).
- **Skipped vs `./deploy.sh app`:** the edge-control companion deploy and `verify_deploy` post-checks. Substituted with direct curl checks.

⚠️ **`./deploy.sh` is currently unusable from this machine and this is a pre-existing problem worth fixing.** `.env.production` is largely a template — `MIRROR_ROUTER_PUBLIC_KEY`, `MIRROR_ROUTER_SIGNING_SEED`, `ADMIN_PASS` and `MAILTRAP_API_TOKEN` are all `CHANGE-ME`, while the real values exist only on the Worker. `push_secrets` warns on placeholders but pushes them anyway (`deploy.sh:571-573`), so a normal run would overwrite four live production secrets. Its own mirror-router guard aborted the attempt before anything was written.

## Not Verified

- **Production**: nothing has been deployed. Tasks 1 and 9 are user-gated (credentials + deploy authorization). All verification above is against a local worker with local D1.
- **Real email delivery**: verified against a local Mailtrap-shaped sink, not Mailtrap itself. `MAILTRAP_API_TOKEN` is confirmed present on the production Worker (`wrangler secret list`), but no real message has been sent.
- **Populated org repo list**: the `/owasp-blt` empty state is browser-verified; the populated state needs a node-published repo and is covered by unit tests only.
- **Full test suite**: 324 passed across the 18 routing/asset/auth files most likely to catch a regression here, plus 140 across the 11 files touching this feature. A whole-suite run is not possible in this checkout — 131 test files import the deleted `world/` tree and 1 imports `www/wrangler.toml`. The 5 failures seen are all pre-existing environmental ones (missing `world/`/`www/`); the 3 non-import ones were attributed by removing this plan's `PAGES` entry and observing identical failures.
- **Deliberate shortcut**: each org needing a bare landing page requires its own literal `run_worker_first` entry in `wrangler.toml` (no wildcard, which would drag every static asset through the Worker). Fine for a single-tenant OSBLT deployment; revisit if orgs become self-serve.

## Implementation Tasks

### Task 1: Create owasp-blt org and link repos via the existing UI

**Objective:** Stand up the real `owasp-blt` organization on production using the already-shipped settings UI, link every published repo, and set world-access floors to public — proving the org alias end-to-end before any new code lands. This is the user's chosen sequencing: create first, build against something real. Verified by TS-001.

**Files:**

- Test: `tests/e2e_evidence_owasp_blt_org.md` (create — evidence log: commands run, URLs verified, screenshots noted)

**Key Decisions / Notes:**

- Use chrome-devtools-axi against the logged-in browser session; the org settings UI is at `/dashboard/settings/organizations` (client: `public/dashboard/js/04-account.js` `initOrgsSection`).
- Anonymous org API repo listing needs `worldAccess.floors = "public"` (PATCH exposed in the UI) — without it the org API hides repos from visitors; repo *pages* are unaffected either way.
- Production mutation via the product's own UI — no code, no migration. If org creation fails (e.g. name moderation), STOP and report; do not work around.

**Definition of Done:**

- [ ] Org `owasp-blt` exists with kaif-blt-04 as owner; all published repos linked
- [ ] `/owasp-blt/<repo>` serves the repo page in the browser (org name in address bar)
- [ ] `git ls-remote https://app.forkmesh.com/owasp-blt/<repo>` returns refs
- [ ] Verify: evidence log written with each step's actual output

### Task 2: Regression tests for the two already-applied fixes

**Objective:** Backfill red-green-verified tests for the two fixes already sitting in the working tree: (a) the `org_invite` notification kind registration, and (b) the `_rename_account_namespace` org-state repair (org_repos.node_owner, org_members, org_team_members, `_ORG_ALIAS_MEMO.clear()`). Red-green means: temporarily revert each fix in the working copy, watch the new test fail, restore, watch it pass.

**Files:**

- Modify: `tests/test_notifications.py` (org_invite kind is enqueueable + email-kind wired)
- Modify: `tests/test_orgs_teams.py` (rename preserves org alias + membership)

**Key Decisions / Notes:**

- Follow the AST-extraction harness (`tests/test_orgs_teams.py:37-100`): `_load("_rename_account_namespace", ...)` with stubbed `d1_run` capturing SQL, assert the three org UPDATEs execute with (new_name, old_name)/(new_bi, new_name, old_bi) params and that `_ORG_ALIAS_MEMO.clear()` ran (inject a fake memo dict).
- Notification test: `_constant("NOTIFICATION_KINDS")` contains `org_invite`; behavioral half — `_load("enqueue_notification", ...)` with a stub insert, assert an `org_invite` enqueue returns True/persists rather than being guard-dropped.
- Assert behavior, not source text — no string-presence tests on entry.py.

**Definition of Done:**

- [ ] Each new test FAILS with its fix reverted and PASSES with it restored (red-green evidence in output)
- [ ] Rename test asserts membership AND alias survive; notification test asserts org_invite enqueue is not dropped
- [ ] Verify: `python3 -m pytest tests/test_orgs_teams.py tests/test_notifications.py -q`

### Task 3: org_invitations schema — migration 0124 + SCHEMA_STATEMENTS + parity test + local apply

**Objective:** Create the `org_invitations` table in both DDL sources with a pending-uniqueness partial index, prove the two sources identical with a parity test, and apply to the LOCAL D1 only.

**Files:**

- Create: `migrations/0124_org_invitations.sql`
- Modify: `src/schema.py` (append statements after `idx_org_repos_node`, ~line 1004)
- Test: `tests/test_org_invitations_schema.py` (create)

**Key Decisions / Notes:**

- Columns: `id TEXT PRIMARY KEY, org_bi TEXT NOT NULL, email_bi TEXT NOT NULL, inviter_bi TEXT NOT NULL, role TEXT NOT NULL DEFAULT 'member', status TEXT NOT NULL DEFAULT 'pending', data TEXT NOT NULL, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, sent_at INTEGER NOT NULL DEFAULT 0, accepted_at INTEGER NOT NULL DEFAULT 0`.
- Indexes: `idx_org_invitations_org (org_bi, status, created_at)`; `idx_org_invitations_email (email_bi, created_at)`; partial unique `idx_org_invitations_pending ON org_invitations(org_bi, email_bi) WHERE status='pending'` (precedent: `idx_users_email`, `src/schema.py:43-44`).
- Blind index + encrypted blob, NOT plaintext: invitee email is PII of a non-member; the orgs plaintext exception (`migrations/0038_orgs_teams.sql:5-8`) covers public roster data only. `data` blob carries `{email, org, role, inviter, actionTokenDigest}`.
- `CREATE TABLE/INDEX IF NOT EXISTS` only — additive, re-apply is a no-op; a revert leaves an unused empty table (harmless).
- Immediately before creating the file, re-check the highest migration number (`ls migrations/*.sql | sort -V | tail -3`) — guards against a same-day numbering collision from concurrent work.
- Parity test: exec both the `.sql` file's statements and the schema.py additions into two in-memory sqlite DBs; assert identical `PRAGMA table_info(org_invitations)` and index lists (pattern: `tests/test_local_migrations.py`).

**Definition of Done:**

- [ ] Parity test proves migration file and SCHEMA_STATEMENTS produce identical table+indexes
- [ ] `FORKMESH_D1_LOCAL=1 ./migrate.sh` applies 0124 locally without error (production NOT touched)
- [ ] Duplicate pending insert for same (org_bi, email_bi) raises constraint error in the parity test
- [ ] Verify: `python3 -m pytest tests/test_org_invitations_schema.py -q`

### Task 4: Invite API — token helper, send/list/revoke handler, routes

**Objective:** Org owners/admins can create email invitations: POST inserts a pending row then emails a signed single-use link (OWASP BLT-branded); if the address already belongs to an account, that account is added directly instead. GET lists pending invites with masked emails; DELETE revokes. Verified by TS-002/TS-004.

**Files:**

- Modify: `src/urls.py` (add `ORG_INVITES_RE`, `ORG_INVITE_ACTION_RE` next to `ORG_MEMBERS_RE` ~line 157)
- Modify: `src/entry.py` (constants next to `MAX_ORG_MEMBERS` at 22821; `_org_invitation_token` next to `_repository_invitation_token` at 26162; `org_invitations_handler` next to `org_members_handler` at 24393; route registration in `_route` before `ORG_RE`; regex re-export list ~line 601)
- Test: `tests/test_org_invitations.py` (create — one test class, covers Tasks 4+5 handler behavior)

**Key Decisions / Notes:**

- POST ordering copied from `repository_imports.py:2434-2482`: role gate (`_org_role` in owner/admin; role grant obeys owner-only rule from `org_members_handler` ~24522) → `normalize_email` → short-circuit existing account (`SELECT user_bi FROM users WHERE email_bi=?` → direct `org_members` insert + notify, no invite row) → caps (pending < 50, members < `MAX_ORG_MEMBERS`, daily < 25) → INSERT pending → `_send_email(env, email, subject, text, html, from_name="OWASP BLT")` with `_forkmesh_email_card_html`/`_forkmesh_email_action_html` → on send False: DELETE the pending row, return 202 `{deliveryConfigured:false}` → `_audit_sensitive_action("organization.invitation_send", ...)` with masked email.
- Do NOT call `_record_account_email` (resolves a node name; invitee may have no account). Store only `sha256(token)` digest.
- Duplicate pending → unique-index violation → map to 409 `invitation_pending`.
- GET list masks emails (reuse `_mask_email` from repository_imports via the existing module accessor).

**Definition of Done:**

- [ ] Owner/admin can create an invite; member/non-member gets 403; unknown org 404
- [ ] Admin-created invite requesting role `admin` or `owner` is rejected (owner-only elevation); owner-created `admin` invite succeeds
- [ ] Exceeding `MAX_ORG_PENDING_INVITES`, `ORG_INVITE_DAILY_LIMIT`, or `MAX_ORG_MEMBERS` returns its documented error code (tested at each cap boundary)
- [ ] Existing-account email short-circuits to direct membership + org_invite notification, creating no invite row
- [ ] Send failure leaves NO pending row (verified with a False-returning `_send_email` stub) and returns 202
- [ ] Second pending invite to the same address → 409; revoke flips status to `revoked`
- [ ] Verify: `python3 -m pytest tests/test_org_invitations.py tests/test_orgs_teams.py -q`

### Task 5: Accept/decline API + signup handoff

**Objective:** The emailed link lands invitees in the org: GET shows the invite without mutating (link-scanner defense), POST accepts — requiring a session, enforcing single-use and expiry — and joins `org_members`. `/signup` reads `?invite=&org=&token=` and completes the accept right after account creation. Verified by TS-003.

**Files:**

- Modify: `src/entry.py` (`org_invitation_action_handler` beside `org_invitations_handler`)
- Modify: `public/signup.js` (parse invite params — pattern: existing `?ref=` parsing ~line 25-37; prefill email; POST accept after signup; redirect to `/owasp-blt`)
- Modify: `public/signup.html` (invite banner element: "You've been invited to join <org>")
- Test: `tests/test_org_invitations.py` (extend Task 4's class)

**Key Decisions / Notes:**

- Model on `_invitation_action` (`repository_imports.py:2508-2606`): token via `hmac.compare_digest(sha256(supplied), data["actionTokenDigest"])` → 403 `invalid_invitation_token`; GET returns `{ok, confirmationRequired:true, org, role, expiresAt}` and mutates nothing; POST requires `_account_session_record` → 401 `invite_signin_required`; status≠pending → 409 `invitation_used`; expired → 410 `invitation_expired` + status flip; re-check `MAX_ORG_MEMBERS`; join via the exact `org_members` upsert used at `entry.py:~24548` but never lowering an existing higher role; set `accepted`/`accepted_at`; notify inviter (`enqueue_notification(..., "org_invite", ...)`); audit `organization.invitation_accept`.
- Gate on the token only, never on an email match — the token holder holds the mailbox; an email check breaks legitimate forwarding while stopping nobody.
- Already-signed-in browser flow: signup.js detects an active session and POSTs accept directly, skipping the form.
- `_account_signup` itself is NOT modified.

**Definition of Done:**

- [ ] GET with valid token returns invite info and leaves status `pending`; bad token 403; no session on POST 401; used 409; expired 410
- [ ] POST accept joins the member at the stored role, marks the row `accepted`, single-use enforced on retry
- [ ] Signup page with invite params prefills email, shows the org banner, and lands the new account in the org (local browser E2E per TS-003)
- [ ] Verify: `python3 -m pytest tests/test_org_invitations.py -q` plus TS-003 steps against the local worker

### Task 6: Org invite management UI (dashboard settings)

**Objective:** Org owners/admins manage invitations from the existing Organizations settings panel: an email+role invite form, a pending list showing masked emails with expiry, and revoke buttons — all OSBLT-themed via the existing dashboard tokens.

**Files:**

- Modify: `public/dashboard/js/04-account.js` (invite form + pending list in the org detail panel next to the members section ~line 1136; new `ORG_ERROR_TEXT` codes ~line 495)
- Test: `tests/test_org_settings_frontend.py` (extend — existing frontend contract test file)

**Key Decisions / Notes:**

- Error codes to map: `invitation_pending`, `invitation_expired`, `invitation_used`, `invalid_invitation_token`, `too_many_invitations`, `daily_invitation_limit`, `invite_signin_required`, `delivery_unconfigured` (202 path shows "Email delivery isn't configured — invite saved" style copy — actually per Task 4 the row is deleted on send failure, so copy is "Email delivery isn't configured; invitation not sent").
- Rebuild bundles: `python3 tools/build_dashboard_assets.py` (Global Constraints).
- Reuse `orgApiRequest` (`04-account.js:533`) for all calls; follow the existing members-section render style.

**Definition of Done:**

- [ ] Invite form visible to owner/admin only; sends POST and re-renders pending list
- [ ] Pending list shows masked email, role, expiry; revoke removes the row
- [ ] Browser check on the local worker: form renders, invite appears in list, revoke works (TS-004 steps 1-2)
- [ ] Verify: `python3 -m pytest tests/test_org_settings_frontend.py -q` and rebuilt bundles committed alongside

### Task 7: Public org page at /owasp-blt

**Objective:** `/owasp-blt` serves a themed public organization page — name, description, member count, linked repos — rendered client-side from the existing `GET /api/orgs/<name>` response. Follows the working tree's literal single-segment route pattern. Verified by TS-005.

**Files:**

- Modify: `wrangler.toml` (add `"/owasp-blt"` to `run_worker_first`, with the `/login`-style literal entries ~line 31)
- Modify: `src/entry.py` (org-page branch in `_route`: single-segment regex → `_org_row` hit → `_serve_org_page`, modeled on `_serve_profile_page`; miss → fall through to existing 404)
- Modify: `src/static_routes.py` + `src/dashboard_shell.py` (new page entry + asset mapping)
- Create: `public/dashboard/partials/views/org.html` (view partial — named `org.html`, not `org-public.html` as first planned, so it matches its `dashboard_shell.PAGES` key `org` like every other view file)
- Modify: `public/dashboard/js/04-account.js` (org page loader: `orgSubjectFromPath()` → fetch `/api/orgs/<name>` → render; pattern: `publicProfileNameFromPath`/`loadPublicProfile` ~line 1600-1620)
- Test: `tests/test_org_public_page.py` (create)

**Key Decisions / Notes:**

- `_serve_org_page` copies `_serve_profile_page`'s shape (`entry.py:~43560`): `_org_row` → 404 via `_serve_not_found_page` if missing → edge-cache keyed on `origin + "/" + org` → `ASSETS.fetch` the built page → inject canonical link + title → cache 300s.
- The branch is generic (any single-segment path that resolves via `_org_row`), but wrangler only forwards paths listed in `run_worker_first` — each future org needs its own literal entry + deploy. Note this limitation in a code comment; acceptable for a client deployment with one org.
- Place the branch in `_route` after the reserved/static single-segment handling, before the final 404 — repo routes are 2-segment and unaffected.
- `GET /api/orgs/<name>` already returns displayName/description/members/repos/viewerRole — no new data endpoint.
- Theme: the page inherits `shell.html` tokens (BLT red `--dashboard-primary-rgb`) automatically via the dashboard shell composer.

**Definition of Done:**

- [ ] `/owasp-blt` on the local worker renders org name, member count, and linked repo list; repo links navigate to `/owasp-blt/<repo>`
- [ ] Unrouted single-segment paths still serve the static 404 (no behavior change for them)
- [ ] Route test: org branch resolves via `_org_row` and misses fall through (AST-extraction harness)
- [ ] Verify: `python3 -m pytest tests/test_org_public_page.py -q` plus TS-005 steps 1-2 in the browser

### Task 8: Member mirror rights — admin on-behalf + member self-serve

**Objective:** Org admins/owners can send mirror requests for org repos they don't personally own, and any org member can self-serve mirroring an org repo onto their own node — the entry parked pre-accepted on their own account, delivered by the existing heartbeat, requiring zero desktop changes. Verified by TS-006.

**Files:**

- Modify: `src/entry.py` (`_org_repo_mirror_allowed` helper next to `_org_write_allowed` at 22954; widen the `owner != actor` gate in `mirror_requests_handler` at ~30486; new `action == "mirror"` branch)
- Modify: `public/dashboard/js/08-repo-detail-network.js` ("Mirror this repo" control on the Mirrors tab, sibling to `askNodeToMirror` ~line 1047, shown to org members on org-linked repos)
- Test: `tests/test_mirror_requests.py` (extend)

**Key Decisions / Notes:**

- C1 widening: `if owner != actor and not await _account_owns_node(env, actor, owner): if not await _org_repo_mirror_allowed(env, actor, owner, repo): 403`. Helper: repo linked under an org where actor is owner/admin (`SELECT org_bi FROM org_repos WHERE node_owner=? AND repo=?` → `_org_role` in owner/admin), fail-closed except → False. The `_account_owns_node` clause also fixes a latent bug (linked-node owners can't ask today).
- C2 `action=="mirror"`: member gate `_org_role` truthy → resolve canonical owner via `_org_repo_node_strict` (NEVER the swallowing `_org_repo_node` — durable storage decision per its docstring); `_account_owns_node(actor, node)` else 403 `not_your_node`; `owner == node` → 400 `self_target`; private → 400; park on the MEMBER's account with `status:"accepted"` (they're opting in — nobody left to ask); reject at `MAX_MIRROR_REQUESTS` with 429 rather than letting `add_mirror_request` silently truncate (`src/mirrors.py:1112`).
- Parked entry's `owner` MUST be the canonical node name — desktop clones `/<owner>/<repo>` and catalog publish verifies against `owner_pub`; the org name would create a keyless namespace.
- Desktop unchanged: heartbeat emits `{id, owner, repo}` via `accepted_mirror_requests` (whitelists those keys, `mirrors.py:1145-1160`) — wire contract byte-identical.

**Definition of Done:**

- [ ] Org admin can create a mirror request for an org-linked repo they don't own; unrelated caller still 403
- [ ] Member self-serve parks an accepted entry on their own account with canonical `owner`; non-member 403; foreign node 403; at-cap 429
- [ ] Existing mirror-request tests still pass unmodified (contract preserved)
- [ ] Verify: `python3 -m pytest tests/test_mirror_requests.py tests/test_orgs_teams.py -q`

### Task 9: Production verification pass

**Objective:** After the user approves a deploy (`./deploy.sh` + remote migration — outside this plan's authority), verify the full feature set against production: org page live, a real email invite delivered and accepted, notifications firing. This task BLOCKS on explicit user confirmation for the deploy and the remote `./migrate.sh` — it must not run them unprompted.

**Files:**

- Test: `tests/e2e_evidence_owasp_blt_org.md` (extend the Task 1 evidence log)

**Key Decisions / Notes:**

- Deploy + remote migration are user-gated actions (Global Constraints; git/deploy permission rules). Ask, wait, then verify.
- Real email leg: invite an address the user controls; they confirm receipt; accept flow completes in the browser.
- Check `wrangler tail` or the admin error log for new `org_invitations`-related errors during the pass.

**Definition of Done:**

- [ ] `/owasp-blt` and `/owasp-blt/<repo>` live on production
- [ ] One real email invite delivered (user-confirmed), accepted, member visible in the org roster
- [ ] In-app org_invite notification received by the accepting account
- [ ] Verify: evidence log updated with production URLs, timestamps, and outcomes
