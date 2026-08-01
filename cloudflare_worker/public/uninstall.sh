#!/usr/bin/env bash













set -euo pipefail

FORKMESH_HOST="${FORKMESH_HOST:-https://forkmesh.com}"

SRC="${FORKMESH_DIR:-$HOME/.local/share/forkmesh/src}"
BIN_DIR="${FORKMESH_BIN_DIR:-$HOME/.local/bin}"
BIN="$BIN_DIR/forkmesh"

say()  { printf '\033[32m==>\033[0m %s\n' "$1"; }
warn() { printf '\033[33mWarning:\033[0m %s\n' "$1" >&2; }
die()  { printf '\033[31mError:\033[0m %s\n' "$1" >&2; exit 1; }

data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
cache_home="${XDG_CACHE_HOME:-$HOME/.cache}"




dirs=(
  "$config_home/ForkMesh"
  "$data_home/ForkMesh"
  "$cache_home/ForkMesh"
  "$HOME/.forkmesh"
  "$data_home/forkmesh"
)

files=(
  "$BIN"
  "$data_home/applications/forkmesh.desktop"
  "$config_home/autostart/forkmesh.desktop"
  "$data_home/icons/forkmesh.png"
)

printf '\033[31mThis removes ForkMesh and ALL of its data from this computer:\033[0m\n'
printf '  • the forkmesh binary and installed source\n'
printf '  • settings, the node identity key (the account cannot be recovered)\n'
printf '  • every mirrored repository, and all chat history\n'
printf '  • the desktop launcher, icons, and login-autostart entry\n\n'



if [ "${FORKMESH_ASSUME_YES:-0}" != "1" ] && [ "${1:-}" != "--yes" ]; then
  if [ -t 0 ]; then
    printf 'Type DELETE to continue: '
    answer=""; read -r answer || answer=""
    [ "$answer" = "DELETE" ] || die "Uninstall cancelled."
  else
    die "Refusing to uninstall without confirmation. Re-run with --yes (or set FORKMESH_ASSUME_YES=1):  curl -fsSL $FORKMESH_HOST/uninstall.sh | bash -s -- --yes"
  fi
fi












_fm_alive() { pgrep -x forkmesh >/dev/null 2>&1 \
  || pgrep -f -- "$BIN" >/dev/null 2>&1 \
  || pgrep -f -- "$SRC" >/dev/null 2>&1; }
if _fm_alive; then
  pkill -x forkmesh   2>/dev/null || true
  pkill -f -- "$BIN"  2>/dev/null || true
  pkill -f -- "$SRC"  2>/dev/null || true
  for _ in 1 2 3 4 5 6 7 8 9 10; do _fm_alive || break; sleep 0.5; done
  if _fm_alive; then
    pkill -9 -x forkmesh   2>/dev/null || true
    pkill -9 -f -- "$BIN"  2>/dev/null || true
    pkill -9 -f -- "$SRC"  2>/dev/null || true
  fi



  if _fm_alive; then
    if [ "$(id -u 2>/dev/null)" != "0" ] && command -v sudo >/dev/null 2>&1; then
      sudo -n pkill -9 -x forkmesh 2>/dev/null || true
      sudo -n pkill -9 -f -- "$BIN" 2>/dev/null || true
    fi
    if _fm_alive; then
      warn "A ForkMesh daemon is STILL running (it is likely owned by root — re-run this uninstall with sudo). Until it is stopped it will keep reporting an old version to the network."
    else
      say "Stopped the running ForkMesh daemon"
    fi
  else
    say "Stopped the running ForkMesh daemon"
  fi
fi

for d in "${dirs[@]}"; do
  if [ -e "$d" ]; then rm -rf -- "$d" && say "Removed $d"; fi
done
for f in "${files[@]}"; do
  if [ -e "$f" ]; then rm -f -- "$f" && say "Removed $f"; fi
done

find "$data_home/icons/hicolor" -name 'forkmesh.png' -delete 2>/dev/null || true

command -v update-desktop-database >/dev/null 2>&1 \
  && update-desktop-database "$data_home/applications" >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
  && gtk-update-icon-cache -f -t "$data_home/icons/hicolor" >/dev/null 2>&1 || true

say "ForkMesh has been completely removed."
say "Reinstall any time with:  curl -fsSL $FORKMESH_HOST/install.sh | bash"
