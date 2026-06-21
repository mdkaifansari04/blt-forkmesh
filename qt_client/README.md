# ForkMesh Qt Client

The ForkMesh desktop node is a Qt 6 application for preserving repositories
and communicating with peers.

## Features

- Generates a local Ed25519 ForkMesh identity
- Stores one profile/account name and Solana donation address
- Mirrors repositories locally as bare Git repositories
- Publishes selected local mirrors to the ForkMesh web catalog
- Creates per-repository chat channels
- Joins encrypted mainnode WebSocket relay rooms
- Supports messages, files, reactions, avatars, typing state, and history sync

## Build

On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake git qt6-base-dev libssl-dev \
  libxkbcommon-dev libxkbcommon-x11-dev openssl
```

```sh
cd qt_client
./run.sh
```

On macOS with Homebrew:

```sh
brew install cmake qt openssl@3
```

```sh
cd qt_client
./run.sh
```

## Tests

```sh
cd qt_client
./run.sh test
```

The smoke test covers Ed25519 identity generation/signing and room crypto.

## Relay Mode

Start the worker locally:

```sh
cd ../cloudflare_worker
uvx --from workers-py pywrangler dev
```

The client starts without setup input and connects to the ForkMesh mainnode:

```text
wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws
```

For local relay testing, use this server URL instead:

```text
ws://127.0.0.1:8787/api/repo/mainnode/forkmesh/rooms/general/ws
```

Room name and passphrase are used to derive the local AES-256-GCM key. The
relay only sees ciphertext.

## Repository Mirroring

Click `+ Add`, choose a local Git repository or enter a remote clone URL, and
ForkMesh will run:

```sh
git clone --mirror <local-path-or-url> <app-data>/mirrors/<owner>-<repo>.git
```

Later syncs run:

```sh
git -C <mirror> fetch --prune
```

Each mirror also gets a repository chat channel so nodes can discuss project
health, releases, and mirror status.

If `Show this mirror on forkmesh.com` is checked, the client publishes signed
metadata to `/api/repositories`. The local source path remains private; only
owner, name, description, channel, sync time, maintainer key, signature, and
any public clone URL you provide are sent to the mainnode.

## Network Details

- Relay WebSocket path: `/api/repo/{owner}/{repo}/rooms/{room}/ws`
- Repository catalog path: `/api/repositories`
- Compatibility WebSocket path: `/api/room/{room}/ws`
- The client encrypts room traffic before sending it to the mainnode.


- File browsing/clone are served on demand from a connected host, with nothing
  stored on the relay. The website caches browsed data in localStorage; the
  desktop client can keep temporary preview mirrors in the Settings-configured
  preview cache before you mirror or fork. Keeps relay lean / free-plan-hostable.
