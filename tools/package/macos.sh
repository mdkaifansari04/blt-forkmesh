#!/usr/bin/env bash
# Package the macOS ForkMesh client as a codesigned, notarized .dmg (issue #370).
# Emits:
#   <outdir>/forkmesh-macos-<arch>.dmg
#
# The input may be a .app bundle or a bare Mach-O executable (wrapped into a
# minimal .app). Signing/notarization env (all optional — unsigned when absent):
#   FORKMESH_APPLE_DEV_ID       "Developer ID Application: NAME (TEAMID)" identity
#   FORKMESH_APPLE_ID           Apple ID for notarytool
#   FORKMESH_APPLE_TEAM_ID      Apple Developer team id
#   FORKMESH_APPLE_APP_PASSWORD app-specific password for notarytool
#   FORKMESH_SPARKLE_ED_KEY     Ed25519 pub key (SUPublicEDKey) baked into Info.plist
#
# Updates are handled in-app by Sparkle 2, which polls the EdDSA-signed appcast
# written by generate-appcast.sh. See docs/design/signed-installers.md.
set -euo pipefail

src="$1"; outdir="${2:-.}"
arch="${FORKMESH_PKG_ARCH:-arm64}"
version="${FORKMESH_PKG_VERSION:-0.0.0}"
[ "$version" = "" ] && version="0.0.0"
channel="${FORKMESH_PKG_CHANNEL:-latest}"
host="${FORKMESH_HOST:-https://forkmesh.com}"
name="forkmesh-macos-${arch}.dmg"
out="${outdir%/}/${name}"

log() { echo "[macos] $*" >&2; }

if [ "$(uname -s)" != "Darwin" ]; then
  log "not running on macOS — cannot codesign/notarize; skipping (bare binary still published)"
  exit 0
fi

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

# Resolve to a .app bundle, wrapping a bare executable if needed.
if [ -d "$src" ] && [[ "$src" == *.app ]]; then
  app="$workdir/ForkMesh.app"
  cp -R "$src" "$app"
else
  app="$workdir/ForkMesh.app"
  mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
  cp "$src" "$app/Contents/MacOS/ForkMesh"
  chmod 0755 "$app/Contents/MacOS/ForkMesh"
  cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>ForkMesh</string>
  <key>CFBundleExecutable</key><string>ForkMesh</string>
  <key>CFBundleIdentifier</key><string>com.forkmesh.desktop</string>
  <key>CFBundleShortVersionString</key><string>${version}</string>
  <key>CFBundleVersion</key><string>${version}</string>
  <key>SUFeedURL</key><string>${host%/}/${channel}/appcast.xml</string>
  <key>SUPublicEDKey</key><string>${FORKMESH_SPARKLE_ED_KEY:-}</string>
</dict></plist>
EOF
fi

# --- codesign + notarize ----------------------------------------------------
if [ -n "${FORKMESH_APPLE_DEV_ID:-}" ]; then
  log "codesign: hardened-runtime signing with '${FORKMESH_APPLE_DEV_ID}'"
  codesign --force --deep --options runtime --timestamp \
    --sign "$FORKMESH_APPLE_DEV_ID" "$app" >&2
else
  log "no FORKMESH_APPLE_DEV_ID — shipping UNSIGNED .app (Gatekeeper will warn)"
fi

# Build the .dmg from the (signed) app.
if command -v create-dmg >/dev/null 2>&1; then
  create-dmg --volname "ForkMesh ${version}" --app-drop-link 400 120 \
    "$out" "$app" >&2 || hdiutil create -volname "ForkMesh ${version}" \
      -srcfolder "$app" -ov -format UDZO "$out" >&2
else
  hdiutil create -volname "ForkMesh ${version}" -srcfolder "$app" \
    -ov -format UDZO "$out" >&2
fi

if [ -n "${FORKMESH_APPLE_ID:-}" ] && [ -n "${FORKMESH_APPLE_APP_PASSWORD:-}" ] \
   && [ -n "${FORKMESH_APPLE_TEAM_ID:-}" ] && command -v xcrun >/dev/null 2>&1; then
  log "notarytool: submitting $name for notarization"
  xcrun notarytool submit "$out" \
    --apple-id "$FORKMESH_APPLE_ID" \
    --team-id "$FORKMESH_APPLE_TEAM_ID" \
    --password "$FORKMESH_APPLE_APP_PASSWORD" --wait >&2
  log "stapler: attaching notarization ticket"
  xcrun stapler staple "$out" >&2 || log "stapler failed; ticket will be fetched online"
else
  log "notarization credentials incomplete — shipping un-notarized .dmg"
fi

log "done: $out (version $version)"
echo "$out"
