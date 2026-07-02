# Releases

This directory holds **release metadata only**. Release binaries are **not**
committed to git (issue #304; see
[`docs/design/release-binary-publishing.md`](../docs/design/release-binary-publishing.md)).
The actual binary bytes live in a per-node content-addressed store (the CAS),
served on demand from a hosting node over the relay — the same way the repo
itself is served — and verified by sha256 on download.

> Historical note: releases used to commit the prebuilt binary directly under
> `releases/<channel>/forkmesh-<os>-<arch>`. The installer still understands that
> layout as a fallback, but new releases should use the metadata-only model below.

## Layout

```
releases/<channel>/
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
  --channel latest --tag v1.2.3 --repo <owner>/forkmesh \
  --cas-dir <mirror>/forkmesh-releases \
  forkmesh-linux-x86_64
```

This hashes each binary, copies the bytes into the CAS (`--cas-dir`, which must
be the directory the serving node reads from — set `FORKMESH_RELEASE_CAS` to it),
and writes `releases/<channel>/SHASUMS256.txt` + `release.json`. Commit **only
that metadata** and publish it — the asset goes live immediately. Run it once on a
node of each OS to publish all three platform builds.

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
