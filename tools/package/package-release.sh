#!/usr/bin/env bash





























set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
os=""; arch=""; binary=""; outdir="."; install_root=""
channel="${RELEASE_CHANNEL:-latest}"; tag="${FORKMESH_TAG:-}"

while [ $# -gt 0 ]; do
  case "$1" in
    --os)      os="$2"; shift 2 ;;
    --arch)    arch="$2"; shift 2 ;;
    --binary)  binary="$2"; shift 2 ;;
    --channel) channel="$2"; shift 2 ;;
    --tag)     tag="$2"; shift 2 ;;
    --outdir)  outdir="$2"; shift 2 ;;
    --install-root) install_root="$2"; shift 2 ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done


if [ -z "$os" ]; then
  case "$(uname -s)" in
    Linux)  os="linux" ;;
    Darwin) os="macos" ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT) os="windows" ;;
    *) os="$(uname -s | tr '[:upper:]' '[:lower:]')" ;;
  esac
fi
if [ -z "$arch" ]; then
  case "$(uname -m)" in
    x86_64|amd64|x64) arch="x86_64" ;;
    aarch64|arm64)    arch="arm64" ;;
    *) arch="$(uname -m)" ;;
  esac
fi

[ -n "$binary" ] || { echo "Error: --binary is required" >&2; exit 2; }
[ -e "$binary" ] || { echo "Error: no such binary: $binary" >&2; exit 1; }
mkdir -p "$outdir"

export FORKMESH_PKG_OS="$os"
export FORKMESH_PKG_ARCH="$arch"
export FORKMESH_PKG_TAG="$tag"
export FORKMESH_PKG_CHANNEL="$channel"
export FORKMESH_PKG_VERSION="${tag#v}"
export FORKMESH_PKG_INSTALL_ROOT="$install_root"

case "$os" in
  linux)   installer="$(bash "$here/appimage.sh" "$binary" "$outdir")" ;;
  windows) installer="$(bash "$here/windows.sh"  "$binary" "$outdir")" ;;
  macos)   installer="$(bash "$here/macos.sh"    "$binary" "$outdir")" ;;
  *) echo "No packager for os '$os'; skipping installer (bare binary still published)." >&2
     exit 0 ;;
esac

[ -n "$installer" ] && [ -f "$installer" ] || {
  echo "Packager for $os produced no installer; skipping (bare binary still published)." >&2
  exit 0
}

echo "Packaged installer: $installer" >&2



echo "$installer"
