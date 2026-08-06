# The four-Worker site split

Since 2026-08, the public site is served by four Cloudflare Workers on the
`forkmesh.com` zone:

| Worker | Config | Owns | How traffic reaches it |
|---|---|---|---|
| `forkmesh-relay` | `wrangler.toml` | The Python application's page/protocol surface: realtime Durable Objects (owner of the classes), git smart-HTTP, ActivityPub (`/ap/*`, `/@handle`), the dashboard, repo pages, auth pages, the homepage `/`, the Worker-built RSS feeds, `.html` canonical 404s, `/health`, the secret admin dashboard, cron — plus the **complete** `public/` asset tree | Custom domains `forkmesh.com` and `www.forkmesh.com` (the catch-all) |
| `forkmesh-api` | `wrangler.api.toml` | The **same Python application**, answering every `/api/*` route (REST + WebSockets; the sockets reach the relay-owned Durable Objects through cross-script bindings) | Zone routes `…/api/*` |
| `forkmesh-www` | `wrangler.www.toml` | Marketing documents: `/pricing`, `/about`, `/features`, `/docs*`, `/blog*`, `/status`, and the other static pages | Exact/prefix zone routes (more specific than the custom domain, so they win) |
| `forkmesh-world` | `wrangler.world.toml` | The `/world` three.js application shell and its module graph `/world/*` | Zone routes `…/world` and `…/world/*` |

## Design invariants

- **The relay is a complete fallback.** `public/` remains the single source of
  truth; `tools/build_split_assets.py` *copies* subsets into `public_www/` and
  `public_world/` (gitignored) at deploy time. Deleting a split Worker deletes
  its routes, and the relay's custom domain resumes serving those paths
  byte-identically. That is the entire rollback procedure:
  `npx wrangler delete --name forkmesh-www` / `--name forkmesh-world`.
- **`/` stays on the relay** because `_serve_homepage` records
  `site_referrers` (feeding `/referrals` and `/api/referrals/sites`) and
  serves `index.html` with `cache-control: no-cache`.
- **RSS stays dynamic.** `forkmesh-www` owns `/blog/*`, and its verbatim copy
  of `_redirects` bounces `/blog/rss.xml` → `/rss.xml` (308). `/rss.xml` is
  deliberately not routed to `forkmesh-www`, so the relay's
  `blog_rss_handler` renders it.
- **`.html` canonical 404s stay on the relay.** The www routes are exact-path
  or `/prefix/*` patterns, so `/pricing.html` etc. still reach the relay's
  `BLOCKED_STATIC_HTML_PATHS` handler and 404.
- **`_headers`/`_redirects` are copied verbatim** into each staged tree
  (rules for paths a Worker never serves are inert), so header behavior
  cannot drift between Workers. The staging step appends one marker header
  per Worker — `x-forkmesh-worker: www|world` — which
  `verify_split_site_workers` in `deploy.sh` uses to prove the routes carved
  the traffic. Relay responses carry no marker.
- **World freshness contract is unchanged**: the World shell and modules keep
  `Cache-Control: no-store` from the copied `_headers`, and
  `verify_public_assets` still hash-compares live `/world/*.js` with the
  checkout after every deploy. `deploy_split_site_workers` runs *before*
  `verify_public_assets` so that check observes the freshly staged copy.
- **Forks never claim our zone.** The route patterns name `forkmesh.com`, so
  `deploy.sh` skips the split deploy unless the manifest's
  `PUBLIC_BASE_URL` still targets the canonical origin
  (`split_site_workers_enabled`). `tools/cloudflare_bootstrap.py` templates
  only `wrangler.toml`, which deliberately contains no `[[routes]]`.
- **Subresources are not duplicated.** Pages served by `forkmesh-www` load
  `/assets/*`, `/styles.css`, `/site-header.js`, `/favicon/*` … and the World
  loads `/assets/world/*`, `/assets/songs/*`, `/assets/video/*` and calls
  `/api/world/*` (including the WebSocket) — all un-routed paths that resolve
  on the relay exactly as before.

- **The api Worker is configuration-locked to the relay.**
  `wrangler.api.toml` must keep `[vars]` value-identical to `wrangler.toml`
  (except `WORKER_ROLE`), bind the same D1/KV/AI, and reach every Durable
  Object class via `script_name = "forkmesh-relay"` — the classes (and their
  migrations) live only in the relay, so a chat room joined through an
  `/api/*` WebSocket is the same instance a relay-served page would reach.
  It has **no cron** (the relay's schedule must not double-run) and receives
  the same secret set on every deploy (`SPLIT_SECRET_WORKER=forkmesh-api
  push_secrets`). `tests/test_split_workers.py` enforces all of this.
- **Build-stamp verification is per Worker**: `/api/version` (now served by
  `forkmesh-api`) echoes `"worker": "api"` and its own `BUILD_REV`;
  `/health` stays relay-routed and echoes `"worker": "relay"` plus the
  relay's `rev`, which is how `verify_split_site_workers` proves both
  deployments landed.

## Operational notes

- The deploy entrypoint is unchanged: `cloudflare_worker/deploy.sh` deploys
  the relay first, verifies it, then deploys both split Workers and verifies
  route ownership end-to-end (`verify_split_site_workers`).
- The Cloudflare API token used for deploys needs the zone-level
  **Workers Routes: Edit** permission on `forkmesh.com` (custom domains alone
  are account-level; routes are zone-level).
- Requests served by the split Workers are pure static-asset responses —
  they never invoke a script and do not count against the Workers free-plan
  request budget.
