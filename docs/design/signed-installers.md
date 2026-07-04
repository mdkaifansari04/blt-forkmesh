# Signed installers and auto-update (issue #370)

**Status:** plumbing landed; signing certificates are an ops dependency (see §6).

ForkMesh already ships prebuilt release binaries without committing them to git
(issue #304): the release workflow builds `forkmesh-<os>-<arch>[.exe]`, stages
the bytes in the node's content-addressed store (CAS), and commits only small
text metadata under `releases/<channel>/` — `SHASUMS256.txt` + `release.json`.
`install.sh` reads that metadata over the git proxy, downloads the bare binary
from the relay, and verifies its sha256.

A bare binary is a download, but not an *installer*, and not code-signed, so a
first-run desktop user hits SmartScreen / Gatekeeper warnings and has no
first-class update path. This design layers **native, signed installers** and
**in-app auto-update** on top of the existing CAS plumbing without changing it:
the installer is published as *just another asset* in the same `release.json`,
alongside the bare binary that headless/SSH installs keep using.

```
                        release.json  (releases/<channel>/)
                       ┌───────────────────────────────────────────┐
  build binary ──────▶ │ forkmesh-linux-x86_64            (bare)    │ ◀── install.sh, SSH deploy
  package installer ─▶ │ forkmesh-linux-x86_64.AppImage   (signed)  │ ◀── desktop download + AppImageUpdate
                       │ forkmesh-windows-x86_64-setup.exe (signed) │ ◀── desktop download + WinSparkle
                       │ forkmesh-macos-arm64.dmg         (notarized)│ ◀── desktop download + Sparkle 2
                       └───────────────────────────────────────────┘
                                        │
                                        ▼
                         appcast.xml  (generated from release.json,
                         EdDSA-signed) — consumed by Sparkle / WinSparkle;
                         AppImage uses embedded zsync update-info.
```

## 1. Per-platform packaging

All packaging lives under `tools/package/`. Each script takes an already-built
executable and emits a native installer named the way `install.sh` and the
appcast resolve it. Signing is **entirely env-var driven** so the ops team wires
certificates into CI secrets without touching code (§6); every script degrades
gracefully — if the signing material or the packaging toolchain is absent it
emits an *unsigned* artifact and warns, rather than failing the release.

| Platform | Installer            | Update mechanism         | Signing                              |
|----------|----------------------|--------------------------|--------------------------------------|
| Linux    | `.AppImage` + `.zsync` | AppImageUpdate (zsync delta) | detached GPG sig + embedded Ed25519 in appcast |
| Windows  | NSIS `-setup.exe`    | WinSparkle (appcast)     | Authenticode (signtool / osslsigncode) |
| macOS    | notarized `.dmg`     | Sparkle 2 (appcast)      | Developer ID codesign + Apple notarization |

- **`package-release.sh`** — dispatcher. Detects the platform from `--os`, runs
  the matching packager, then hands the artifact to
  `tools/forkmesh-release-publish.sh` so it lands in the CAS + `release.json`
  exactly like the bare binary. Called from `.forkmesh/release.yml`.
- **`appimage.sh`** — bundles the Qt runtime with `linuxdeploy` + the Qt plugin,
  produces a self-contained `.AppImage`, embeds zsync update-information
  (`zsync|<host>/<channel>/forkmesh-linux-<arch>.AppImage.zsync`) so
  AppImageUpdate can fetch binary deltas, and writes the `.zsync` control file.
- **`windows.sh`** — drives `makensis` over `forkmesh.nsi`, then Authenticode-
  signs the result. Works on a Windows runner (`signtool`) or cross-platform via
  `osslsigncode` — both selected by which env/cert is present.
- **`macos.sh`** — assembles `ForkMesh.app`, `codesign`s it with the Developer ID
  identity, staples a notarization ticket (`notarytool`), then packages the
  signed `.dmg`.

## 2. Update manifest: appcast from `release.json`

`generate-appcast.sh` reads `releases/<channel>/release.json` and emits
`releases/<channel>/appcast.xml`. Because the appcast is *derived* from the same
committed manifest the bare-binary path already uses, there is a single source of
truth for "what is the latest release" — the update check and the download page
can never disagree.

Each `<item>` carries the version, the enclosure URL (the relay's content-
addressed blob endpoint), length, and — critically — an **EdDSA signature** of
the installer bytes. ForkMesh already runs Ed25519 everywhere (issue #370 calls
this out as the natural fit), so the appcast signature reuses the node's release
signing key; Sparkle's `SUPublicEDKey` / WinSparkle's public key are derived from
it once (§6). AppImageUpdate does not use the appcast — it trusts the zsync +
sha256 recorded in `release.json` and the detached GPG signature.

## 3. In-app update check

- **macOS / Windows:** Sparkle 2 / WinSparkle poll `appcast.xml` on their own
  schedule, verify the EdDSA signature, and present the standard native "update
  available" UI. Both are configured with the relay URL for the running node's
  channel.
- **Linux:** AppImageUpdate reads the update-information embedded in the running
  AppImage and applies a zsync binary delta.
- **From-source installs** (the current git-checkout auto-update in
  `MainWindow::maybeAutoUpdate`) are unchanged: a node that was `install.sh`'d
  from source keeps rebuilding from a tag. `maybeAutoUpdate` already gates on the
  presence of a local `CMakeLists.txt` checkout, so an installer-based install
  (no checkout) simply defers to the native updater instead. The two paths are
  mutually exclusive by construction.

## 4. The stale-`release.json` pitfall

The appcast and the installer share `release.json`'s failure mode: the bump
commit/tag **must land in the bare mirror before the publish queue runs**, or an
update check reads a `release.json` that points at a tag the mirror can't yet
serve (issue #57 / adhoc #57 + #60). Mitigation is unchanged and inherited for
free because packaging publishes through the same
`tools/forkmesh-release-publish.sh` → harvest → commit → sync-mirror path:

1. The release workflow syncs the version header to the tag and commits it.
2. `forkmesh-release-publish.sh` writes CAS blobs + metadata (now including the
   installer asset and `appcast.xml`).
3. ForkMesh harvests the `releases/<channel>/` metadata, commits, and **syncs the
   served mirror before the asset is advertised** — so an update check never sees
   an appcast whose enclosure the mirror can't stream.

## 5. Verification chain

Nothing is trusted on name alone:

- `install.sh` verifies the downloaded blob against `SHASUMS256.txt` (unchanged).
- AppImageUpdate verifies the zsync-assembled file against the sha256 in
  `release.json`; the detached GPG signature is checked when a key is configured.
- Sparkle / WinSparkle verify the appcast enclosure's EdDSA signature against the
  pinned public key before installing.
- Authenticode / Gatekeeper verify the OS-level code signature at install/launch.

## 6. Ops runbook (not code)

The one-time purchases and secret wiring. None of this is in the repo; all of it
is consumed by the env-var hooks in `tools/package/`.

| Secret (CI)                     | Used by      | How to obtain                                   |
|---------------------------------|--------------|-------------------------------------------------|
| `FORKMESH_APPLE_DEV_ID`         | `macos.sh`   | Apple Developer Program ($99/yr) → "Developer ID Application" cert |
| `FORKMESH_APPLE_ID` / `_TEAM_ID` / `_APP_PASSWORD` | `macos.sh` | Apple ID + app-specific password for `notarytool` |
| `FORKMESH_WIN_CERT_PFX` (base64) / `FORKMESH_WIN_CERT_PASSWORD` | `windows.sh` | Authenticode code-signing cert (OV/EV) from a CA |
| `FORKMESH_WIN_TIMESTAMP_URL`    | `windows.sh` | CA-provided RFC-3161 timestamp URL               |
| `FORKMESH_SPARKLE_ED_KEY`       | `generate-appcast.sh` | Ed25519 private key (base64) — reuse the node release key |
| `FORKMESH_GPG_KEY` (optional)   | `appimage.sh` | GPG key for the detached AppImage signature      |

Publish the corresponding public keys once:
- Sparkle `SUPublicEDKey` in `Info.plist`, WinSparkle public key baked into the
  Windows build — both derived from `FORKMESH_SPARKLE_ED_KEY`.
- Until certificates are purchased, the packagers run **unsigned**: users still
  get real installers and working updates, only without OS-trust badges, and the
  release does not block. Wiring a cert is then a secrets-only change.
