#!/usr/bin/env bash













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
install_root="${FORKMESH_PKG_INSTALL_ROOT:-}"
resource_root="${install_root%/}/share/forkmesh"
if [ -z "$install_root" ] || [ ! -f "$resource_root/tools/cloudflare_bootstrap.py" ] \
   || [ ! -f "$resource_root/cloudflare_worker/src/entry.py" ] \
   || [ ! -d "$resource_root/cloudflare_worker/public" ] \
   || [ ! -d "$resource_root/cloudflare_worker/migrations" ]; then
  log "CMake-installed ForkMesh deployment resources are incomplete"
  exit 1
fi
mkdir -p "$workdir/resources"
cp -R "$resource_root" "$workdir/resources/forkmesh"

log "makensis: building $name (version $version)"
makensis \
  -DFORKMESH_VERSION="$version" \
  -DFORKMESH_SRCEXE="$workdir/forkmesh.exe" \
  -DFORKMESH_RESOURCES="$workdir/resources/forkmesh" \
  -DFORKMESH_OUTFILE="$out" \
  "$here/forkmesh.nsi" >&2


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
