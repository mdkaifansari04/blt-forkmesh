#!/usr/bin/env bash
# ForkMesh desktop client uninstaller.
#   curl -fsSL https://forkmesh.com/uninstall.sh | bash
# Removes ForkMesh COMPLETELY so a fresh install.sh run starts from a clean
# slate: the binary, the installer's source checkout, the desktop launcher +
# icons, the login-autostart entry, AND every byte of user data (settings, the
# node identity key, all mirrored repositories, and chat history).
#
# A plain `rm` of the binary leaves the data dirs behind, which is why a
# reinstall can otherwise come back up with an old node name and stale chat.
# This is the same routine install.sh runs for `--uninstall`, packaged as a
# standalone script.
#
# Non-interactive (curl | bash) confirm: pass --yes or set FORKMESH_ASSUME_YES=1.
set -euo pipefail

FORKMESH_HOST="${FORKMESH_HOST:-https://forkmesh.com}"
# Paths must match install.sh so we remove exactly what it created.
SRC="${FORKMESH_DIR:-$HOME/.local/share/forkmesh/src}"
BIN_DIR="${FORKMESH_BIN_DIR:-$HOME/.local/bin}"
BIN="$BIN_DIR/forkmesh"

say()  { printf '\033[32m==>\033[0m %s\n' "$1"; }
warn() { printf '\033[33mWarning:\033[0m %s\n' "$1" >&2; }
die()  { printf '\033[31mError:\033[0m %s\n' "$1" >&2; exit 1; }

data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
cache_home="${XDG_CACHE_HOME:-$HOME/.cache}"

# Directories ForkMesh owns. QSettings org+app are both "ForkMesh", so the
# config/data/cache live under a capitalised "ForkMesh" dir; the installer's
# own checkout lives under the lowercase "forkmesh".
dirs=(
  "$config_home/ForkMesh"          # settings (node name, server, prefs)
  "$data_home/ForkMesh"            # identity key, mirrors, repos, chat, actions
  "$cache_home/ForkMesh"           # caches
  "$HOME/.forkmesh"                # IDE-extension handoff dir
  "$data_home/forkmesh"            # installer source checkout (parent of $SRC)
)
# Loose files: binary, desktop launcher, autostart entry, installed icon.
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

# Honour a non-interactive confirm so `curl | bash` works: pass --yes (or set
# FORKMESH_ASSUME_YES=1). Otherwise prompt when a terminal is attached.
if [ "${FORKMESH_ASSUME_YES:-0}" != "1" ] && [ "${1:-}" != "--yes" ]; then
  if [ -t 0 ]; then
    printf 'Type DELETE to continue: '
    answer=""; read -r answer || answer=""
    [ "$answer" = "DELETE" ] || die "Uninstall cancelled."
  else
    die "Refusing to uninstall without confirmation. Re-run with --yes (or set FORKMESH_ASSUME_YES=1):  curl -fsSL $FORKMESH_HOST/uninstall.sh | bash -s -- --yes"
  fi
fi

# A headless install (e.g. a VPS) runs the binary as a plain background
# process (nohup, no systemd unit). Deleting the binary out from under it just
# unlinks the inode: the running process keeps executing from the deleted
# file and keeps reporting its (now stale) presence/version to the network,
# which is why mirrors could still show an old version after "uninstalling"
# the host. Stop it first.
if pgrep -f -- "$BIN" >/dev/null 2>&1; then
  pkill -f -- "$BIN" 2>/dev/null || true
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    pgrep -f -- "$BIN" >/dev/null 2>&1 || break
    sleep 0.5
  done
  pgrep -f -- "$BIN" >/dev/null 2>&1 && pkill -9 -f -- "$BIN" 2>/dev/null || true
  say "Stopped the running ForkMesh daemon"
fi

for d in "${dirs[@]}"; do
  if [ -e "$d" ]; then rm -rf -- "$d" && say "Removed $d"; fi
done
for f in "${files[@]}"; do
  if [ -e "$f" ]; then rm -f -- "$f" && say "Removed $f"; fi
done
# Every hicolor icon bucket the installer may have written.
find "$data_home/icons/hicolor" -name 'forkmesh.png' -delete 2>/dev/null || true
# Refresh desktop caches so the launcher disappears promptly.
command -v update-desktop-database >/dev/null 2>&1 \
  && update-desktop-database "$data_home/applications" >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
  && gtk-update-icon-cache -f -t "$data_home/icons/hicolor" >/dev/null 2>&1 || true

say "ForkMesh has been completely removed."
say "Reinstall any time with:  curl -fsSL $FORKMESH_HOST/install.sh | bash"
