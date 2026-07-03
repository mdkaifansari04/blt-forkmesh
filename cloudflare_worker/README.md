# ForkMesh Cloudflare Python Relay

This Python Worker serves the ForkMesh public site from `public/` and hosts test
encrypted ForkMesh mainnode rooms at: test

```text
/api/repo/{owner}/{repo}/rooms/{room}/ws
```

The older `/api/room/{room}/ws` path remains as a temporary compatibility
endpoint.

The static site is deployed with Cloudflare Workers Static Assets. API and
health routes run through the Python Worker first, while `/` is served from
`public/index.html`.

Selected desktop mirrors are published to a Durable Object repository catalog
at:

```text
/api/repositories
```

The public website fetches this endpoint and renders the network repository
list. The catalog stores signed metadata only, not local filesystem paths or
Git object data.

The desktop client encrypts every room payload with AES-256-GCM before it
leaves the machine. The Durable Object only sees ciphertext envelopes and
keeps them ephemeral: each text frame is broadcast to currently connected
clients and is not written to storage.

There are no npm, npx, TypeScript, or package-lock dependencies in this worker
project.

## Local Development

```sh
uvx --from workers-py pywrangler dev
```

Then use this server URL in the Qt client:

```text
ws://127.0.0.1:8787/api/repo/mainnode/forkmesh/rooms/general/ws
```

The deployed mainnode URL is:

```text
wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws
```

## Deploy

The website (static assets in `public/`) and the relay Worker ship together in
one deploy:

```sh
./deploy.sh            # deploy to production
./deploy.sh dry-run    # validate without uploading
```

This wraps `uvx --from workers-py pywrangler deploy`. Run it directly if you
prefer.

Set `NODE_NAME` and `NODE_SOLANA_ADDRESS` in `wrangler.toml` or as dashboard
environment variables for the health response.

## Mainnode Binding

The Durable Object binding is `FORKMESH_MAINNODE_ROOM`. Each object is keyed by
repository and room:

```text
repo:{owner}/{repo}:room:{room}
```

The repository catalog binding is `FORKMESH_CATALOG`; it stores the public
catalog records shown on the website.
