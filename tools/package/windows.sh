#!/usr/bin/env bash
# Package the Windows ForkMesh binary as an NSIS installer and Authenticode-sign
# it (issue #370). Emits:
#   <outdir>/forkmesh-windows-<arch>-setup.exe
#
# Signing env (all optional — unsigned when absent):
#   On a Windows runner: signtool is used automatically if on PATH.
#   Cross-platform (osslsigncode):
#     FORKMESH_WIN_CERT_PFX        base64 of the .pfx code-signing cert
#     FORKMESH_WIN_CERT_PASSWORD   its password
#     FORKMESH_WIN_TIMESTAMP_URL   RFC-3161 timestamp URL (default: DigiCert)
#
# Updates are handled in-app by WinSparkle, which polls the appcast written by
# generate-appcast.sh. See docs/design/signed-installers.md.
set -euo pipefail

binary="$1"; outdir="${2:-.}"
here="$(cd "$(dirname "$0")" && pwd)"
arch="${FORKMESH_PKG_ARCH:-x86_64}"
version="${FORKMESH_PKG_VERSION:-0.0.0}"
[ "$version" = "" ] && version="0.0.0"
name="forkmesh-windows-${arch}-setup.exe"
out="${outdir%/}/${name}"

log() { echo "[windows] $*" >&2; }

if ! command -v makensis >/dev/null 2>&1; then
  log "makensis not found — cannot build the Windows installer; skipping (bare .exe still published)"
  exit 0
fi

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
cp "$binary" "$workdir/forkmesh.exe"

log "makensis: building $name (version $version)"
makensis \
  -DFORKMESH_VERSION="$version" \
  -DFORKMESH_SRCEXE="$workdir/forkmesh.exe" \
  -DFORKMESH_OUTFILE="$out" \
  "$here/forkmesh.nsi" >&2

# --- Authenticode signing ---------------------------------------------------
if command -v signtool >/dev/null 2>&1 && [ -n "${FORKMESH_WIN_CERT_PFX:-}" ]; then
  pfx="$workdir/cert.pfx"
  printf '%s' "$FORKMESH_WIN_CERT_PFX" | base64 -d > "$pfx"
  ts="${FORKMESH_WIN_TIMESTAMP_URL:-http://timestamp.digicert.com}"
  log "signtool: Authenticode-signing $name"
  signtool sign /f "$pfx" /p "${FORKMESH_WIN_CERT_PASSWORD:-}" \
    /fd sha256 /tr "$ts" /td sha256 "$out" >&2
elif command -v osslsigncode >/dev/null 2>&1 && [ -n "${FORKMESH_WIN_CERT_PFX:-}" ]; then
  pfx="$workdir/cert.pfx"
  printf '%s' "$FORKMESH_WIN_CERT_PFX" | base64 -d > "$pfx"
  ts="${FORKMESH_WIN_TIMESTAMP_URL:-http://timestamp.digicert.com}"
  signed="$workdir/signed.exe"
  log "osslsigncode: Authenticode-signing $name (cross-platform)"
  osslsigncode sign -pkcs12 "$pfx" -pass "${FORKMESH_WIN_CERT_PASSWORD:-}" \
    -h sha256 -ts "$ts" -in "$out" -out "$signed" >&2
  mv "$signed" "$out"
else
  log "no code-signing cert (FORKMESH_WIN_CERT_PFX) or signing tool — shipping UNSIGNED installer"
fi

log "done: $out"
echo "$out"
