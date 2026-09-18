# owasp-blt organization — E2E evidence log

Evidence artifact for Tasks 1 and 9 of `docs/plans/2026-08-20-owasp-blt-organization.md`.
Records commands actually run and their real output. Anything not verified is
stated as not verified rather than omitted.

## Production deployment — 2026-08-20 — DONE

Target confirmed before deploying: `app/wrangler.toml` carries no `account_id`, so
the deploy uses `CLOUDFLARE_ACCOUNT_ID` from `.env` (`1cbdfdcf…`), which is the same
account `wrangler whoami` reports and where `wrangler deployments list --name
forkmesh-relay` resolves. Its workers.dev subdomain is `owaspblt`, hence
`forkmesh-relay.owaspblt.workers.dev`. The `forkmesh.com` origins that appear in
`./deploy.sh dry-run app` come from `edge-control/wrangler.toml`, a separate
companion worker — not from `app/`.

```
$ pywrangler d1 migrations apply forkmesh --remote
  0123_api_metrics_minute.sql  ✅      (already pending before this work)
  0124_org_invitations.sql     ✅
  Executed 5 commands

$ pywrangler d1 execute forkmesh --remote --command "PRAGMA table_info(org_invitations)"
  id, org_bi, email_bi, inviter_bi, role, status, data,
  created_at, expires_at, sent_at, accepted_at          (11 columns)

$ pywrangler deploy
  Uploaded forkmesh-relay (45.41 sec)
  Deployed forkmesh-relay triggers (5.43 sec)
  https://forkmesh-relay.owaspblt.workers.dev
  Current Version ID: 50071950-cfb2-434c-9fee-b69a0ccad4eb
```

Secrets were NOT pushed. `pywrangler deploy` does not touch them, so the live
`MAILTRAP_API_TOKEN`, `MIRROR_ROUTER_*`, `DATA_KEY` and `ADMIN_PATH` are intact.
`.env.production` was restored byte-identical to its pre-run backup (`diff` clean).

## Task 1 (create org, link repos) — NOT DONE

Blocked on an authenticated `kaifblt04` session. The browser reported
`NO SESSION` on `forkmesh-relay.owaspblt.workers.dev` throughout; an earlier
attempt also hit a Cloudflare 1027 rate limit (free-plan cap) before the origin
recovered.

```
$ curl -s -o /dev/null -w '%{http_code}' https://forkmesh-relay.owaspblt.workers.dev/owasp-blt
404
```

That 404 is `_serve_org_page_response` correctly reporting a missing org — the
route resolves through `_org_row` first. It is NOT evidence of a missing route,
but it also is not proof the route works; creating the org is the test that
distinguishes them.

Remaining: log in → Settings → Organizations → create `owasp-blt` → link the
`kaifblt04/*` repos → PATCH `worldAccess.floors = "public"` → confirm
`/owasp-blt` returns 200 and `git ls-remote .../owasp-blt/<repo>` lists refs.

## Task 9 (production verification pass) — NOT DONE

Depends on Task 1. Also outstanding: one real email invite delivered through
production Mailtrap and accepted end to end. Every invite exercised so far ran
against a local Mailtrap-shaped sink on `127.0.0.1:9925`, so real delivery is
unproven.

## Local verification that DID pass

Full detail in the plan's `## Verification Evidence (local)` section. Summary:
the invite → signup → membership flow was driven in a real browser against a
local worker; the previously-dead `org_invite` notification was observed firing
(`GET /api/notifications?node=kaif-blt-04` returned
`org_invite | alice-e2e accepted your invitation to owasp-blt`); replaying a
consumed invite returned `409 invitation_used`; revoke worked from the settings
UI; `/owasp-blt` rendered in the OSBLT theme.
