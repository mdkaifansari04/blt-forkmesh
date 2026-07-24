# Releases

This directory holds **release metadata only**. Release binaries are **not**
committed to git (issue #304; see
[`docs/design/release-binary-publishing.md`](../../docs/design/release-binary-publishing.md)).
The actual binary bytes live in a per-node content-addressed store (the CAS),
served on demand from a hosting node over the relay — the same way the repo
itself is served — and verified by sha256 on download.

> Historical note: releases used to commit the prebuilt binary directly under
> `.forkmesh/releases/<channel>/forkmesh-<os>-<arch>`. The installer still understands that
> layout as a fallback, but new releases should use the metadata-only model below.

## Layout

```
.forkmesh/releases/<channel>/
  SHASUMS256.txt   # "<sha256>  <asset-name>" per asset (sha256sum -c compatible)
  release.json     # manifest: repo, tag, tag_commit, channel, assets[]
```

- `<channel>` — `latest` by default. The installer reads `FORKMESH_RELEASE` to
  pick a different channel; the publisher reads `RELEASE_CHANNEL`.
- Asset names follow `forkmesh-<os>-<arch>[.exe]` (os: `linux`/`macos`/`windows`,
  arch: `x86_64`/`arm64`) — the exact scheme `install.sh` resolves from `uname`.

The binary bytes for each asset live in the serving node's CAS, addressed by the
same sha256 recorded in `SHASUMS256.txt`:

```
<mirror>/forkmesh-releases/sha256/<aa>/<full-hash>/data     # gitignored, never committed
```

## Publishing

Don't add binaries here by hand. Cut a release by triggering the **Attach desktop
build to release** action (`.forkmesh/release.yml`) from the Actions tab, or run
the publisher directly:

```sh
tools/forkmesh-release-publish.sh \
  --channel latest --tag v1.2.3 --repo forkmesh/forkmesh \
  --cas-dir <mirror>/forkmesh-releases \
  forkmesh-linux-x86_64
```

This hashes each binary, copies the bytes into the CAS (`--cas-dir`, which must
be the directory the serving node reads from — set `FORKMESH_RELEASE_CAS` to it),
and writes `.forkmesh/releases/<channel>/SHASUMS256.txt` + `release.json`. Commit **only
that metadata** and publish it — the asset goes live immediately. Run it once on a
node of each OS to publish all three platform builds.

### Refreshing a same-version binary

Do not delete a checksum line or copy a local development executable into the
CAS. When fixes must ship under the current app version, first commit the exact
source to publish, then run the explicit refresh from a clean worktree:

```sh
FORKMESH_RELEASE_CAS=/absolute/path/to/the/served/forkmesh-releases \
  cloudflare_worker/deploy.sh republish-release-binary
```

The command refuses a dirty tracked worktree or an unspecified served CAS. It
rebuilds the current platform in Release mode, pins the version from
`qt_client/CMakeLists.txt`, verifies the executable's `--version`, replaces the
content-addressed bytes, and checks that `release.json`, `SHASUMS256.txt`, the
CAS hash, and the source commit all agree. It then commits the small release
metadata update. Push that commit and let mirror catalogs refresh before using
the desktop client's fleet binary-install action.

## Installing

The installer autodetects the platform, reads `SHASUMS256.txt` over the git
proxy, downloads the matching binary from the relay's content-addressed release
endpoint, and verifies its sha256 before installing:

```sh
curl -fsSL https://forkmesh.com/install.sh | bash
```

Force a from-source build instead with `FORKMESH_FROM_SOURCE=1`.

If no prebuilt binary is published for the platform, the installer falls back
to a source build by default. Set `FORKMESH_NO_SOURCE_FALLBACK=1` to disable
that fallback and fail instead — this is already the default on headless
Linux (no `DISPLAY`/`WAYLAND_DISPLAY`), since an unattended mirror-node deploy
should always use a verified binary rather than pulling in a build toolchain.
Headful Linux and macOS still default to allowing the fallback.

## Download endpoint

```
GET /api/repo/<owner>/<repo>/releases/blob/sha256/<sha256>
```

Stable, content-addressed, and immutable — cached forever at the edge. The worker
forwards it to a node holding the blob, which streams the bytes back.
