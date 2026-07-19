# ForkMesh marketing Worker

A tiny JS Worker that serves the public marketing pages: `https://forkmesh.com/`
(the landing page), `/pricing`, and the `/blog` index plus every `/blog/<slug>`
post. Every other path (`/api/*`, `/dashboard`, `/login`, `/owner/repo`, and the
pages' own CSS/JS/image subresources, …) is served by the relay Worker in
[`../cloudflare_worker`](../cloudflare_worker), exactly as before.

## How the split works

- This Worker declares the routes `forkmesh.com/`, `forkmesh.com/pricing`,
  `forkmesh.com/blog`, and `forkmesh.com/blog/*` in its `wrangler.toml`.
  Cloudflare ranks route patterns by specificity and routes take precedence over
  a Worker Custom Domain, so only those paths land here; every other
  `forkmesh.com/<anything>` keeps resolving to the relay Worker.
- The documents stay canonical in `../cloudflare_worker/public/` (next to the
  shared CSS/JS/images they reference). The wrangler `[build]` step copies
  `index.html`, `pricing.html`, `blog.html`, and every `blog/<slug>/index.html`
  into `public/` before every deploy — `public/` here is generated and
  gitignored.
- `/pricing` and the blog pages are plain public documents streamed straight
  from the copied assets (clean URLs mapped exactly as the relay's `_redirects`
  do). The `/` request semantics additionally mirror the relay Worker's homepage
  handler, which — along with the pricing/blog routes — remains in place as the
  fallback for workers.dev previews and local dev:
  - `forkmesh_session=1` cookie → `302 /dashboard` with `no-store` (a logout
    must never replay a cached redirect).
  - Otherwise the landing page with `cache-control: no-cache` + `vary: cookie`.
  - Every marketing response carries `x-forkmesh-worker: marketing`, which
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
independent: shipping relay changes does not touch the marketing routes, and
vice versa. When a marketing page itself changes (`index.html`, `pricing.html`,
`blog.html`, or a `blog/<slug>/index.html`), redeploy **this** Worker to pick up
the copy.
