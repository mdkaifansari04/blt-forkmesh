# ForkMesh marketing Worker

A tiny JS Worker that serves exactly one URL: `https://forkmesh.com/` — the
marketing landing page. Every other path (`/api/*`, `/dashboard`, `/login`,
`/owner/repo`, the landing page's own CSS/JS/asset subresources, …) is served
by the relay Worker in [`../cloudflare_worker`](../cloudflare_worker), exactly
as before.

## How the split works

- This Worker declares the exact-match route `forkmesh.com/` in its
  `wrangler.toml`. Cloudflare ranks route patterns by specificity and routes
  take precedence over a Worker Custom Domain, so only the root URL lands here;
  `forkmesh.com/<anything>` keeps resolving to the relay Worker.
- The landing document stays canonical in
  `../cloudflare_worker/public/index.html` (next to the shared CSS/JS it
  references). The wrangler `[build]` step copies it into `public/` before
  every deploy — `public/` here is generated and gitignored.
- The `/` request semantics mirror the relay Worker's homepage handler, which
  remains in place as the fallback for workers.dev previews and local dev:
  - `forkmesh_session=1` cookie → `302 /dashboard` with `no-store` (a logout
    must never replay a cached redirect).
  - Otherwise the landing page with `cache-control: no-cache` + `vary: cookie`.
  - Every landing response carries `x-forkmesh-worker: marketing`, which
    `deploy.sh` uses to verify the route actually took effect.

  `cloudflare_worker/tests/test_marketing_worker.py` pins both sides of this
  contract.

## Deploying

```sh
./deploy.sh          # deploy to production (verifies forkmesh.com/ answers)
./deploy.sh dev      # run locally
./deploy.sh dry-run  # build and validate without uploading
```

Cloudflare credentials (`CLOUDFLARE_ACCOUNT_ID`, `CLOUDFLARE_API_TOKEN`) are
read from the relay Worker's gitignored `../cloudflare_worker/.env.production`
— both Workers live on the same account/zone. Deploys of the two Workers are
independent: shipping relay changes does not touch the landing route, and vice
versa. When the landing page itself changes (`index.html`), redeploy **this**
Worker to pick up the copy.
