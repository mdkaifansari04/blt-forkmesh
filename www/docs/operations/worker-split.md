# Cloudflare deployment boundaries

ForkMesh has three product Workers plus one small App control Worker. The
control Worker is deployed and fingerprinted with the App target.

| Directory | Script | Official hostnames | Responsibility |
|---|---|---|---|
| `app/` | `forkmesh-relay` | `app.forkmesh.com`; compatibility alias `api.forkmesh.com` | Product UI, API, WebSockets, Git Smart HTTP, ActivityPub, D1, KV, AI, migrations, and Durable Object classes |
| `app/edge-control/` | `forkmesh-edge-control` | Specific health and identity routes on the App and API hosts | Runtime-independent health, build and mirror identity, exact-origin CORS, and the Cron Trigger that kicks the App-owned runner Durable Object |
| `www/` | `forkmesh-www` | `forkmesh.com`, `www.forkmesh.com` | Landing pages, marketing, documentation, blog, and shared browser assets |
| `world/` | `forkmesh-world` | `world.forkmesh.com` | World shell, modules, models, soundtrack, avatars, and World media |

The App retains the `forkmesh-relay` script name so existing Durable Object
namespaces and live room identities survive the reorganization. There is no
separate API Worker. `api.forkmesh.com` is a compatibility custom domain on
the same `forkmesh-relay` script, not another deployment.

## Routing contract

`app.forkmesh.com` owns all operational protocols. `/api` and `/developers`
serve the developer and live-statistics page. `/api/*` remains the API. The App
root serves the dashboard; a self-host sets `SINGLE_WORKER_SITE=true` so its
root serves the complete product entry page instead.

The legacy API hostname serves `/api*`, health, federation/Git protocols, and
WebSocket upgrades directly from the same App Worker. Ordinary GET/HEAD page
loads receive a `308` to the equivalent `app.forkmesh.com` URL. This keeps old
clients live while moving people and search indexes to the canonical App.

The edge-control route layer answers `/health`, `/api/version`, and GET
`/api/mirrors/https` without starting Python. Its method-agnostic route
wildcards forward mirror registration and malformed suffixes exactly once to
`forkmesh-relay`. It also answers CORS preflights that reach those protected
routes. Other App routes retain their existing ownership and behavior.
Mirror identity responses are always `no-store`, including successful reads,
so an intentional trust-root rotation cannot leave clients with a cached key.

`www` redirects exact `/api` to the App developer page. It service-proxies
`/api/*`, Git Smart HTTP, ActivityPub, webhook bodies, and WebSocket upgrades to
`forkmesh-relay`, preserving old same-host clients without duplicating backend
logic. Product routes redirect to App and `/world*` redirects to World.

`world` serves only World assets. Its Worker injects `APP_ORIGIN` into the World
shell before `api-client.js` runs, so HTTP and WebSocket API traffic from both
official and custom World hostnames reaches the configured App.

`www/public/api-client.js` is the sole API-origin implementation. Build staging
copies the same bytes into all three asset roots. It migrates only the retired
official `https://api.forkmesh.com` browser setting to
`https://app.forkmesh.com`; arbitrary remote instances are left unchanged.
Exact `/api` is not rewritten in the browser, which prevents the developer page
from entering a rewrite loop.

## Asset ownership

- `app/public` owns login, signup, dashboard, repository, chat, notes, network,
  status, rewards, accounts, and file icons.
- `www/public` owns official marketing, browser docs, blog, favicons, fonts,
  video, and common site chrome. Authored documentation lives under `www/docs`.
- `world/public` owns World code and World-only media.

`app/tools/build_site_assets.py` creates ignored `app/dist`, `www/dist`, and
`world/dist` trees. App staging includes operational pages, install/uninstall
scripts, shared chrome, and `blog.html` because the backend reads that one index
to generate RSS. It does not ship official blog posts, blog art, or docs.

`app/wrangler.toml` owns secrets, D1, KV, AI, migrations, and Durable Object
classes. `app/edge-control/wrangler.toml` owns the Cron Trigger and binds the
existing App-owned runner namespace through an external Durable Object
binding. The static manifests contain public origins; `www` also has the App
service binding. Only scheduled ingress moved to JavaScript: the App-owned
CronRunner and its maintenance jobs still execute in the Python/Pyodide
Worker until a later extraction.

## Self-hosted topology

The default self-host has one complete App at
`https://forkmesh.example.com`. Set `PUBLIC_BASE_URL`, `API_ORIGIN`,
`APP_ORIGIN`, `WWW_ORIGIN`, and allowed CORS origins to that same origin, and
set `SINGLE_WORKER_SITE=true`. Official marketing content is not required. The
App's Python API, D1, and Durable Object workload requires Cloudflare Workers
Paid; the free plan's 10 ms CPU ceiling is below normal App request cost.
The bootstrapper deploys a sibling edge-control script and installs only the
three more-specific health and identity routes in front of the broad App
route; every other request continues directly to the self-hosted App.

World is optional. When enabled, deploy it at a hostname such as
`https://forkmesh-world.example.com`, set its `APP_ORIGIN` to the self-hosted
App, and add the World origin to the App CORS allowlist.

## Safe official cutover

Run `cd app && ./deploy.sh deploy`. On the first official split the coordinator
uses this roll-forward order:

1. Migrate D1, deploy `forkmesh-relay`, publish its secrets, and verify the
   backend revision and App fingerprint through unoverlaid `/api/mainnode`.
   Deploy edge control and separately verify its matching revision,
   fingerprint, ownership marker, and exact mirror-router public key, while temporarily
   retaining the App's legacy apex and `www` domains.
2. Deploy and verify World.
3. Deploy and verify `www`; only now do the apex domains move away from the
   already healthy App.
4. Redeploy App with its canonical hostname plus the `api.forkmesh.com`
   compatibility alias, removing only the temporary apex/www routes.
5. Probe `/api/version` through the alias and the alias-root canonical redirect.
   Delete the obsolete `forkmesh-api` script only when both prove that the
   alias is already served by `forkmesh-relay` and explicit retirement is set.

Any failed verification stops the sequence. D1 migrations are applied before
the App upload and must remain forward-compatible with the previous Worker
version so a failed upload leaves the live version usable.

After the first cutover, `./deploy.sh changed` and `./deploy.sh deploy`
fingerprint App, World, and www independently and upload only changed targets.
Use `./deploy.sh app`, `world`, or `www` for one target, `./deploy.sh status` to
inspect fingerprints, and `./deploy.sh dry-run <target>` to build without an
upload. `./deploy.sh post-deploy-verify` performs read-only ownership, legacy
alias, www service-binding POST, CORS allow/deny, and WebSocket boundary probes
against the live three-service topology.

An App target is skipped only when the edge and unoverlaid Python identities
both report the current fingerprint, the same non-development App revision,
and the App worker role, and the edge reports the exact local mirror-router
public key. The unoverlaid backend must report that same key and prove its
private seed by signing and verifying a fixed domain-separated challenge. A
missing, stale, or mismatched backend therefore cannot be hidden by a healthy
edge response; no seed or proof signature is returned.

`./deploy.sh secrets` updates non-router secrets only. It preserves both
mirror-router secrets already on the App Worker and proves that the public
edge identity is unchanged afterward. To change that trust root, put a matched
32-byte base64url Ed25519 public key and signing seed in the ignored
`.env.production`, then run `./deploy.sh rotate-mirror-router`. Normal App
deploys reject a live/local key mismatch. The explicit rotation first makes
the public identity unavailable, atomically publishes the matched Python key
pair, and only then restores the edge route with the new public key; the seed
is never passed to edge control or printed.

## Rollback

Deploy a previously verified revision of the affected target. Never rename
`forkmesh-relay` or recreate Durable Object classes under a new script. Database
migrations are roll-forward, so application rollbacks must continue to accept
the newest applied schema.
