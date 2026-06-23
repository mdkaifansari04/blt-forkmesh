#!/usr/bin/env bash
#
# install.sh — register ForkMesh with the desktop (GNOME, KDE, any XDG desktop)
# so it shows the ForkMesh logo in the dock/overview and can be pinned.
#
# This is a USER-level install: it writes only under $HOME (no root, no sudo).
# It does not copy the binary — the .desktop launcher points straight at the
# build output, so a plain `cmake --build build` is picked up next launch and
# the in-app quick-update keeps working.
#
# Re-run it any time; it overwrites its own files and refreshes the icon cache.
#
# Usage:  ./install.sh           # build if needed, then install
#         ./install.sh --uninstall
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BIN="$BUILD_DIR/forkmesh"

# XDG user dirs (honour overrides; fall back to the spec defaults).
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS_DIR="$DATA_HOME/applications"
ICON_BASE="$DATA_HOME/icons/hicolor"
DESKTOP_ID="forkmesh"                 # must match setDesktopFileName() in main.cpp
DESKTOP_FILE="$APPS_DIR/$DESKTOP_ID.desktop"

uninstall() {
    echo "Removing ForkMesh desktop integration..."
    rm -f "$DESKTOP_FILE"
    find "$ICON_BASE" -name "$DESKTOP_ID.png" -delete 2>/dev/null || true
    rm -f "$DATA_HOME/icons/$DESKTOP_ID.png"
    refresh_caches
    echo "Done. (The build directory was left untouched.)"
}

refresh_caches() {
    # Best-effort: these tools may be absent on minimal systems, and a stale
    # cache only delays the icon by a login cycle — never fail the install on it.
    command -v gtk-update-icon-cache >/dev/null 2>&1 \
        && gtk-update-icon-cache -f -t "$ICON_BASE" >/dev/null 2>&1 || true
    command -v update-desktop-database >/dev/null 2>&1 \
        && update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
}

if [[ "${1:-}" == "--uninstall" || "${1:-}" == "-u" ]]; then
    uninstall
    exit 0
fi

# --- 1. Make sure a binary exists -------------------------------------------
if [[ ! -x "$BIN" ]]; then
    echo "No build found at $BIN — building..."
    if ! command -v cmake >/dev/null 2>&1; then
        echo "error: cmake is not installed; cannot build ForkMesh." >&2
        exit 1
    fi
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target forkmesh -j"$(nproc 2>/dev/null || echo 4)"
fi
[[ -x "$BIN" ]] || { echo "error: build did not produce $BIN" >&2; exit 1; }

# --- 2. Install the icon into the hicolor theme -----------------------------
# The icon name "forkmesh" is what the .desktop Icon= key and the Wayland app-id
# resolve against. Prefer ImageMagick to render exact-size buckets (sharp at
# every dock size); otherwise drop the source PNGs into their nearest correct
# bucket, which GNOME will scale.
SRC_1024="$SCRIPT_DIR/resources/icons/forkmesh.png"       # 1024x1024
SRC_256="$SCRIPT_DIR/resources/icons/forkmesh-256.png"    # 256x256
[[ -f "$SRC_1024" ]] || { echo "error: missing $SRC_1024" >&2; exit 1; }

MAGICK=""
command -v magick      >/dev/null 2>&1 && MAGICK="magick"
[[ -z "$MAGICK" ]] && command -v convert >/dev/null 2>&1 && MAGICK="convert"

install_icon() { # <size>
    local size="$1"
    local dir="$ICON_BASE/${size}x${size}/apps"
    mkdir -p "$dir"
    if [[ -n "$MAGICK" ]]; then
        "$MAGICK" "$SRC_1024" -resize "${size}x${size}" "$dir/$DESKTOP_ID.png"
    elif [[ "$size" == "256" && -f "$SRC_256" ]]; then
        cp "$SRC_256" "$dir/$DESKTOP_ID.png"      # exact size, no scaling
    fi
}

if [[ -n "$MAGICK" ]]; then
    for s in 16 32 48 64 128 256 512; do install_icon "$s"; done
else
    echo "note: ImageMagick not found — installing the 256px icon only." >&2
    install_icon 256
    # 1024px source as a generic fallback so something always resolves.
    mkdir -p "$DATA_HOME/icons"
    cp "$SRC_1024" "$DATA_HOME/icons/$DESKTOP_ID.png"
fi

# --- 3. Write the desktop entry ---------------------------------------------
mkdir -p "$APPS_DIR"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=ForkMesh
GenericName=Decentralized Git
Comment=Peer-to-peer Git hosting, chat, issues, and CI
Exec=$BIN
Icon=$DESKTOP_ID
Terminal=false
Categories=Development;RevisionControl;
Keywords=git;fork;mesh;p2p;repository;
StartupNotify=true
StartupWMClass=$DESKTOP_ID
EOF
chmod +x "$DESKTOP_FILE"

# --- 4. Refresh caches so GNOME picks it up without a re-login --------------
refresh_caches

echo "ForkMesh installed for the desktop."
echo "  launcher : $DESKTOP_FILE"
echo "  runs     : $BIN"
echo
echo "Open the Activities/app grid, search \"ForkMesh\", launch it, then"
echo "right-click its dock icon -> \"Pin to Dash\" (GNOME) to keep it there."
