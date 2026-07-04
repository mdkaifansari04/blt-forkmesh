#!/usr/bin/env bash
# Package the Linux ForkMesh binary as a self-contained AppImage with zsync
# delta-update support (issue #370). Emits:
#   <outdir>/forkmesh-linux-<arch>.AppImage         the installer
#   <outdir>/forkmesh-linux-<arch>.AppImage.zsync   AppImageUpdate control file
#
# Signing/update env (all optional):
#   FORKMESH_GPG_KEY   GPG key id/email — detached-sign the AppImage if set.
#   FORKMESH_HOST      base URL baked into the embedded update-information so
#                      AppImageUpdate knows where to fetch deltas from
#                      (default: https://forkmesh.com).
#
# Degrades gracefully: if linuxdeploy/appimagetool are unavailable, falls back to
# a runnable AppDir tarball so the release still ships *something* downloadable.
# See docs/design/signed-installers.md.
set -euo pipefail

binary="$1"; outdir="${2:-.}"
arch="${FORKMESH_PKG_ARCH:-x86_64}"
version="${FORKMESH_PKG_VERSION:-0.0.0}"
channel="${FORKMESH_PKG_CHANNEL:-latest}"
host="${FORKMESH_HOST:-https://forkmesh.com}"
name="forkmesh-linux-${arch}.AppImage"
out="${outdir%/}/${name}"

log() { echo "[appimage] $*" >&2; }

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
appdir="$workdir/ForkMesh.AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" \
         "$appdir/usr/share/icons/hicolor/256x256/apps"

cp "$binary" "$appdir/usr/bin/forkmesh"
chmod 0755 "$appdir/usr/bin/forkmesh"

cat > "$appdir/forkmesh.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=ForkMesh
Exec=forkmesh
Icon=forkmesh
Categories=Development;
Terminal=false
EOF
cp "$appdir/forkmesh.desktop" "$appdir/usr/share/applications/forkmesh.desktop"

# A 1x1 placeholder icon keeps appimagetool happy when the repo icon is absent.
icon="$appdir/forkmesh.png"
if [ -f "qt_client/resources/forkmesh.png" ]; then
  cp qt_client/resources/forkmesh.png "$icon"
else
  printf '\x89PNG\r\n\x1a\n' > "$icon"  # placeholder; replaced when a real icon exists
fi
cp "$icon" "$appdir/usr/share/icons/hicolor/256x256/apps/forkmesh.png" 2>/dev/null || true

# AppImageUpdate reads this "update-information" from the built AppImage and uses
# it to fetch a zsync binary delta instead of the whole file on each update.
update_info="zsync|${host%/}/${channel}/${name}.zsync"
export UPDATE_INFORMATION="$update_info"

if command -v linuxdeploy >/dev/null 2>&1; then
  # Bundle the Qt runtime + platform plugin so the AppImage is self-contained.
  plugin_args=()
  command -v linuxdeploy-plugin-qt >/dev/null 2>&1 && plugin_args=(--plugin qt)
  log "linuxdeploy: bundling Qt runtime into AppDir"
  linuxdeploy --appdir "$appdir" \
    --desktop-file "$appdir/forkmesh.desktop" \
    --icon-file "$icon" \
    "${plugin_args[@]}" >&2 || log "linuxdeploy reported a non-fatal error; continuing"
fi

if command -v appimagetool >/dev/null 2>&1; then
  log "appimagetool: building $name (update-info: $update_info)"
  ARCH="$arch" appimagetool --updateinformation "$update_info" "$appdir" "$out" >&2
else
  log "appimagetool not found — emitting an AppDir tarball fallback instead"
  out="${outdir%/}/forkmesh-linux-${arch}.AppDir.tar.gz"
  tar -C "$workdir" -czf "$out" ForkMesh.AppDir
fi

# zsync control file for delta updates (only meaningful for a real AppImage).
if command -v zsyncmake >/dev/null 2>&1 && [ "${out##*.}" = "AppImage" ]; then
  log "zsyncmake: writing ${name}.zsync"
  ( cd "$outdir" && zsyncmake -u "$name" "$name" >&2 ) || \
    log "zsyncmake failed; AppImage ships without a delta control file"
fi

# Optional detached GPG signature (Authenticode/Gatekeeper equivalents live in
# the Windows/macOS packagers).
if [ -n "${FORKMESH_GPG_KEY:-}" ] && command -v gpg >/dev/null 2>&1; then
  log "gpg: detached-signing $out with key ${FORKMESH_GPG_KEY}"
  gpg --batch --yes --local-user "$FORKMESH_GPG_KEY" \
      --output "${out}.sig" --detach-sign "$out" >&2 || \
    log "gpg signing failed; shipping unsigned AppImage"
else
  log "no FORKMESH_GPG_KEY (or gpg missing) — shipping UNSIGNED AppImage"
fi

log "done: $out (version $version)"
echo "$out"
