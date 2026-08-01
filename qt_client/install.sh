#!/usr/bin/env bash














set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BIN="$BUILD_DIR/forkmesh"





TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then TIMEOUT_BIN="timeout"; fi
bounded() {
    local secs="$1"; shift
    if [[ -n "$TIMEOUT_BIN" ]]; then
        "$TIMEOUT_BIN" "$secs" "$@"
    else
        "$@"
    fi
}


DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS_DIR="$DATA_HOME/applications"
ICON_BASE="$DATA_HOME/icons/hicolor"
DESKTOP_ID="forkmesh"
DESKTOP_FILE="$APPS_DIR/$DESKTOP_ID.desktop"

uninstall() {
    echo "Removing ForkMesh desktop integration..."
    rm -f "$DESKTOP_FILE"
    find "$ICON_BASE" -name "$DESKTOP_ID.png" -delete 2>/dev/null || true
    rm -f "$DATA_HOME/icons/$DESKTOP_ID.png"
    rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/$DESKTOP_ID.desktop"
    refresh_caches





    if [[ "$PURGE" == "1" ]]; then
        echo "Erasing all ForkMesh user data (--purge)…"
        local config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
        local cache_home="${XDG_CACHE_HOME:-$HOME/.cache}"
        local d
        for d in "$config_home/ForkMesh" "$DATA_HOME/ForkMesh" \
                 "$cache_home/ForkMesh" "$HOME/.forkmesh"; do
            if [[ -e "$d" ]]; then rm -rf -- "$d" && echo "  removed $d"; fi
        done
        echo "All user data removed."
    else
        echo "Done. (The build directory and your data were left untouched.)"
        echo "Re-run with --purge to also erase settings, repos, and chat history."
    fi
}

refresh_caches() {




    echo "Refreshing icon cache…"
    command -v gtk-update-icon-cache >/dev/null 2>&1 \
        && bounded 60 gtk-update-icon-cache -f -t "$ICON_BASE" >/dev/null 2>&1 || true
    echo "Refreshing desktop database…"
    command -v update-desktop-database >/dev/null 2>&1 \
        && bounded 30 update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
    echo "Cache refresh done."
}


PURGE=0
DO_UNINSTALL=0
for arg in "$@"; do
    case "$arg" in
        --uninstall|-u) DO_UNINSTALL=1 ;;
        --purge)        PURGE=1 ;;
    esac
done
if [[ "$DO_UNINSTALL" == "1" ]]; then
    uninstall
    exit 0
fi


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






SRC_1024="$SCRIPT_DIR/resources/icons/forkmesh.png"
SRC_256="$SCRIPT_DIR/resources/icons/forkmesh-256.png"
[[ -f "$SRC_1024" ]] || { echo "error: missing $SRC_1024" >&2; exit 1; }

MAGICK=""
command -v magick      >/dev/null 2>&1 && MAGICK="magick"
[[ -z "$MAGICK" ]] && command -v convert >/dev/null 2>&1 && MAGICK="convert"

install_icon() {
    local size="$1"
    local dir="$ICON_BASE/${size}x${size}/apps"
    mkdir -p "$dir"
    if [[ -n "$MAGICK" ]]; then
        bounded 30 "$MAGICK" "$SRC_1024" -resize "${size}x${size}" "$dir/$DESKTOP_ID.png"
    elif [[ "$size" == "256" && -f "$SRC_256" ]]; then
        cp "$SRC_256" "$dir/$DESKTOP_ID.png"
    fi
}

if [[ -n "$MAGICK" ]]; then
    echo "Rendering icon sizes with $MAGICK…"
    for s in 16 32 48 64 128 256 512; do
        echo "  • ${s}x${s}"
        install_icon "$s" || echo "note: failed to render ${s}px icon (continuing)" >&2
    done
else
    echo "note: ImageMagick not found — installing the 256px icon only." >&2
    install_icon 256

    mkdir -p "$DATA_HOME/icons"
    cp "$SRC_1024" "$DATA_HOME/icons/$DESKTOP_ID.png"
fi


echo "Writing desktop launcher → $DESKTOP_FILE"
mkdir -p "$APPS_DIR"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=ForkMesh
GenericName=Decentralized Git
Comment=Peer-to-peer Git hosting, chat, issues, and CI
Exec="$BIN" %u
Icon=$DESKTOP_ID
Terminal=false
Categories=Development;RevisionControl;
MimeType=x-scheme-handler/forkmesh;
Keywords=git;fork;mesh;p2p;repository;
StartupNotify=true
StartupWMClass=$DESKTOP_ID
EOF
chmod +x "$DESKTOP_FILE"


refresh_caches

echo "ForkMesh installed for the desktop."
echo "  launcher : $DESKTOP_FILE"
echo "  runs     : $BIN"
echo
echo "Open the Activities/app grid, search \"ForkMesh\", launch it, then"
echo "right-click its dock icon -> \"Pin to Dash\" (GNOME) to keep it there."
