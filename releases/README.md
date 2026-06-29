# Release binaries

Prebuilt desktop-client binaries attached to releases live here, so the
installer can download a ready-to-run binary instead of compiling the Qt client
from source. Because they are committed to the repository, the same git mirror
the relay/website already serves carries them — `install.sh` fetches just the
one asset it needs over the website's git proxy from whichever node is online.

## Layout

```
releases/<channel>/forkmesh-<os>-<arch>[.exe]
```

- `<channel>` — `latest` by default. The installer reads `FORKMESH_RELEASE` to
  pick a different channel; the release workflow reads `RELEASE_CHANNEL`.
- `<os>` — `linux`, `macos`, or `windows` (from `uname -s`).
- `<arch>` — `x86_64` or `arm64` (from `uname -m`).
- `.exe` suffix on Windows only.

Examples:

```
releases/latest/forkmesh-linux-x86_64
releases/latest/forkmesh-macos-arm64
releases/latest/forkmesh-windows-x86_64.exe
```

## Publishing

Don't add binaries here by hand. Cut a release by triggering the
**Attach desktop build to release** action (`.forkmesh/release.yml`) from the
Actions tab. It builds the client for the node's own platform and commits the
matching `forkmesh-<os>-<arch>` asset into `releases/<channel>/`. Run it once on
a node of each OS (Linux, macOS, Windows) to attach all three builds, then
publish the commits — the assets go live for download immediately.

## Installing

The installer autodetects the platform and prefers the prebuilt asset:

```sh
curl -fsSL https://forkmesh.com/install.sh | bash
```

Force a from-source build instead with `FORKMESH_FROM_SOURCE=1`.
