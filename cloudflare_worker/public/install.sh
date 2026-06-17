#!/usr/bin/env bash
# ForkMesh desktop client installer.
#   curl -fsSL https://forkmesh.com/install.sh | bash
# Clones the repository, builds the Qt client, and installs it to ~/.local/bin.
set -euo pipefail

REPO="${FORKMESH_REPO:-https://github.com/forkmesh/forkmesh.git}"
SRC="${FORKMESH_DIR:-$HOME/.local/share/forkmesh/src}"
BIN_DIR="${FORKMESH_BIN_DIR:-$HOME/.local/bin}"
BIN="$BIN_DIR/forkmesh"

say() { printf '\033[32m==>\033[0m %s\n' "$1"; }
die() { printf '\033[31mError:\033[0m %s\n' "$1" >&2; exit 1; }

# --- prerequisites ----------------------------------------------------------
for tool in git cmake; do
  command -v "$tool" >/dev/null 2>&1 || die "'$tool' is required but not installed."
done
command -v cc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1 || \
  command -v g++ >/dev/null 2>&1 || die "A C++ compiler is required (install build tools / Xcode CLT)."

say "ForkMesh requires Qt 6 (Widgets, Network, Svg) and OpenSSL."
say "  Debian/Ubuntu: sudo apt install qt6-base-dev qt6-svg-dev libssl-dev cmake g++"
say "  Fedora:        sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel openssl-devel cmake gcc-c++"
say "  macOS:         brew install qt openssl@3 cmake"

# --- fetch / update ---------------------------------------------------------
mkdir -p "$(dirname "$SRC")"
if [ -d "$SRC/.git" ]; then
  say "Updating existing checkout in $SRC"
  git -C "$SRC" pull --ff-only
else
  say "Cloning $REPO"
  git clone --depth 1 "$REPO" "$SRC"
fi

# --- build ------------------------------------------------------------------
JOBS="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )"
BUILD="$SRC/qt_client/build"
say "Configuring"
CMAKE_ARGS=(-S "$SRC/qt_client" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DFORKMESH_BUILD_TESTS=OFF)
if command -v brew >/dev/null 2>&1; then
  qt_prefix="$(brew --prefix qt 2>/dev/null || true)"
  ssl_prefix="$(brew --prefix openssl@3 2>/dev/null || true)"
  [ -n "$qt_prefix" ] && CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
  [ -n "$ssl_prefix" ] && CMAKE_ARGS+=("-DOPENSSL_ROOT_DIR=$ssl_prefix")
fi
cmake "${CMAKE_ARGS[@]}"
say "Building (this can take a few minutes)"
cmake --build "$BUILD" -j"$JOBS"

# --- install ----------------------------------------------------------------
if [ -x "$BUILD/ForkMesh.app/Contents/MacOS/ForkMesh" ]; then
  BUILT="$BUILD/ForkMesh.app/Contents/MacOS/ForkMesh"
else
  BUILT="$BUILD/forkmesh"
fi
[ -x "$BUILT" ] || die "Build succeeded but the executable was not found at $BUILT."

mkdir -p "$BIN_DIR"
install -m 0755 "$BUILT" "$BIN" 2>/dev/null || { cp "$BUILT" "$BIN"; chmod 0755 "$BIN"; }
say "Installed to $BIN"

case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *) say "Add $BIN_DIR to your PATH, e.g.  export PATH=\"$BIN_DIR:\$PATH\"" ;;
esac
say "Done. Launch it with:  forkmesh"
