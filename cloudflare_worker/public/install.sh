#!/usr/bin/env bash












set -euo pipefail




INSTALLER_VERSION="0.15.0 (2026-07-31)"





FORKMESH_HOST="${FORKMESH_HOST:-https://forkmesh.com}"
FORKMESH_NODE="${FORKMESH_NODE:-}"







FORKMESH_NODE_NAME="${FORKMESH_NODE_NAME:-${FORKMESH_NODE:-}}"








FORKMESH_OWNER="${FORKMESH_OWNER:-}"






FORKMESH_REINSTALL="${FORKMESH_REINSTALL:-0}"







FORKMESH_RESTART="${FORKMESH_RESTART:-0}"






FORKMESH_LINK_CODE="${FORKMESH_LINK_CODE:-}"






FORKMESH_LOCAL_BINARY="${FORKMESH_LOCAL_BINARY:-}"
FORKMESH_LOCAL_OS="${FORKMESH_LOCAL_OS:-}"
FORKMESH_LOCAL_ARCH="${FORKMESH_LOCAL_ARCH:-}"



FORKMESH_EXPECTED_BUILD_COMMIT="${FORKMESH_EXPECTED_BUILD_COMMIT:-}"
FORKMESH_EXPECTED_RELEASE_VERSION="${FORKMESH_EXPECTED_RELEASE_VERSION:-}"






FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256="${FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256:-}"
FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE="${FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE:-}"
[ -n "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" ] ||
  [ ! -f /etc/forkmesh/release-publisher.pem ] ||
  FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE=/etc/forkmesh/release-publisher.pem


FORKMESH_LOCAL_BINARY_SHA256="${FORKMESH_LOCAL_BINARY_SHA256:-}"
FORKMESH_NAME="${FORKMESH_NAME:-forkmesh}"
FORKMESH_INSTALL_SOURCE_URL="${FORKMESH_INSTALL_SOURCE_URL:-${FORKMESH_HOST%/}/api/install-source}"
FORKMESH_DIAG_URL="${FORKMESH_DIAG_URL:-${FORKMESH_HOST%/}/api/install-diag}"
REPO="${FORKMESH_REPO:-}"





FORKMESH_NODES=""
REPO_CANDIDATES=()



CLONE_FAIL_REASON=""



SRC="${FORKMESH_DIR:-$HOME/.local/share/forkmesh/src}"
BIN_DIR="${FORKMESH_BIN_DIR:-$HOME/.local/bin}"
BIN="$BIN_DIR/forkmesh"
PID_FILE="${FORKMESH_PID_FILE:-${XDG_RUNTIME_DIR:-$HOME/.local/share/forkmesh}/forkmesh.pid}"
SYSTEMD_UNIT="/etc/systemd/system/forkmesh-node.service"
SYSTEMD_ENV="/etc/forkmesh/node.env"
SYSTEMD_BIN="/usr/local/bin/forkmesh"
SYSTEMD_INSTALL_MARKER="/etc/forkmesh/installer-managed"
SYSTEMD_STATE_DIR="/var/lib/forkmesh"
SYSTEMD_STATE_MARKER="/var/lib/forkmesh/.forkmesh-managed-service"


PIN_FAILURE=0


INSTALLED_PREBUILT=0



BUILD=""



DEFER_PINNED_REINSTALL=0



MANAGED_MARKER=".forkmesh-managed"
INSTALLER_START_DIR="$(pwd -P 2>/dev/null || pwd)"
INSTALLER_WORKSPACE_ROOT="$(
  git -C "$INSTALLER_START_DIR" rev-parse --show-toplevel 2>/dev/null || true
)"









if [ -z "${FORKMESH_NO_SOURCE_FALLBACK:-}" ]; then
  if [ "$(uname -s 2>/dev/null)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    FORKMESH_NO_SOURCE_FALLBACK=1
  else
    FORKMESH_NO_SOURCE_FALLBACK=0
  fi
fi







cd "$HOME" 2>/dev/null || cd / 2>/dev/null || true

say()  { printf '\033[32m==>\033[0m %s\n' "$1"; }
warn() { printf '\033[33mWarning:\033[0m %s\n' "$1" >&2; }
die()  { printf '\033[31mError:\033[0m %s\n' "$1" >&2; exit 1; }




dbg()  { [ "${FORKMESH_DEBUG:-0}" = "1" ] && printf '\033[2m[debug]\033[0m %s\n' "$1" >&2 || true; }

_sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" 2>/dev/null | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" 2>/dev/null | awk '{print $1}'
  else
    echo ""
  fi
}

_canonical_path() {
  local value="$1" probe suffix="" base physical
  case "$value" in
    /*) probe="$value" ;;
    *) probe="$INSTALLER_START_DIR/$value" ;;
  esac
  case "$probe" in *'
'*) return 1 ;; esac



  while [ ! -e "$probe" ]; do
    base="${probe##*/}"
    [ -n "$base" ] && [ "$base" != "." ] && [ "$base" != ".." ] || return 1
    suffix="/$base$suffix"
    probe="${probe%/*}"
    [ -n "$probe" ] || probe="/"
  done
  [ ! -L "$probe" ] || return 1
  if [ -d "$probe" ]; then
    physical="$(cd -P -- "$probe" 2>/dev/null && pwd -P)" || return 1
  else
    physical="$(cd -P -- "${probe%/*}" 2>/dev/null && pwd -P)" || return 1
    physical="$physical/${probe##*/}"
  fi
  physical="${physical%/}$suffix"
  [ -n "$physical" ] || physical="/"
  printf '%s\n' "$physical"
}

_path_owner_uid() {
  stat -c '%u' "$1" 2>/dev/null || stat -f '%u' "$1" 2>/dev/null || true
}

_marker_is_valid() {
  local dir="$1" marker="$1/$MANAGED_MARKER" uid
  [ -d "$dir" ] && [ ! -L "$dir" ] && [ -f "$marker" ] &&
    [ ! -L "$marker" ] || return 1
  uid="$(_path_owner_uid "$marker")"
  [ "$uid" = "$(id -u)" ] || return 1
  grep -Fqx 'forkmesh-managed-v1' "$marker" &&
    grep -Fqx "uid=$(id -u)" "$marker" &&
    grep -Fqx "path=$dir" "$marker"
}

_write_managed_marker() {
  local dir="$1" marker="$1/$MANAGED_MARKER"
  [ -d "$dir" ] && [ ! -L "$dir" ] || return 1
  umask 077
  {
    printf 'forkmesh-managed-v1\n'
    printf 'uid=%s\n' "$(id -u)"
    printf 'path=%s\n' "$dir"
  } > "$marker"
}

_remove_managed_src() {
  local dir="$1"
  [ -e "$dir" ] || return 0
  _marker_is_valid "$dir" ||
    die "Refusing to recursively remove unmanaged or owner-mismatched source directory: $dir"
  rm -rf -- "$dir"
}

SRC="$(_canonical_path "$SRC")" ||
  die "FORKMESH_DIR is not a canonical, non-symlink path."
CANONICAL_HOME="$(_canonical_path "$HOME")" ||
  die "HOME could not be canonicalized safely."
case "$SRC" in
  /|/bin|/boot|/dev|/etc|/home|/lib|/lib64|/opt|/proc|/root|/run|/sbin|/srv|/sys|/tmp|/usr|/var)
    die "Refusing unsafe FORKMESH_DIR target: $SRC" ;;
esac
[ "$SRC" != "$CANONICAL_HOME" ] ||
  die "FORKMESH_DIR may not be the home directory."
if [ -n "$INSTALLER_WORKSPACE_ROOT" ]; then
  INSTALLER_WORKSPACE_ROOT="$(_canonical_path "$INSTALLER_WORKSPACE_ROOT")" ||
    die "Current workspace could not be canonicalized."
  case "$INSTALLER_WORKSPACE_ROOT/" in
    "$SRC/"*) die "FORKMESH_DIR may not be the current workspace or one of its parents." ;;
  esac
fi



if [ ! -e "$SRC" ]; then
  case "$SRC" in */forkmesh/src) ;; *)
    die "A new FORKMESH_DIR must end in /forkmesh/src." ;;
  esac
elif [ -L "$SRC" ]; then
  die "FORKMESH_DIR may not be a symlink."
elif ! _marker_is_valid "$SRC"; then
  _legacy_default="$(_canonical_path "$HOME/.local/share/forkmesh/src" 2>/dev/null || true)"
  _legacy_marker="$SRC/$MANAGED_MARKER"
  if [ "$SRC" = "$_legacy_default" ] && [ -f "$_legacy_marker" ] &&
     [ ! -L "$_legacy_marker" ] &&
     [ "$(_path_owner_uid "$_legacy_marker")" = "$(id -u)" ]; then
    _write_managed_marker "$SRC" ||
      die "Could not upgrade the legacy ForkMesh install marker."
  else
    die "Existing FORKMESH_DIR is not marked as an owner-matched ForkMesh install: $SRC"
  fi
fi

if [ -n "$FORKMESH_EXPECTED_BUILD_COMMIT" ]; then
  FORKMESH_EXPECTED_BUILD_COMMIT="$(
    printf '%s' "$FORKMESH_EXPECTED_BUILD_COMMIT" | tr 'A-F' 'a-f'
  )"
  if ! printf '%s' "$FORKMESH_EXPECTED_BUILD_COMMIT" |
      grep -Eq '^([0-9a-f]{40}|[0-9a-f]{64})$'; then
    die "FORKMESH_EXPECTED_BUILD_COMMIT must be an exact 40- or 64-hex Git commit."
  fi
  if ! printf '%s' "$FORKMESH_EXPECTED_RELEASE_VERSION" |
      grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+([-.][A-Za-z0-9._+-]+)?$'; then
    die "FORKMESH_EXPECTED_RELEASE_VERSION must be an exact release version."
  fi
fi
for _fm_digest_name in \
    FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256 \
    FORKMESH_LOCAL_BINARY_SHA256; do
  eval "_fm_digest_value=\${${_fm_digest_name}}"
  if [ -n "$_fm_digest_value" ] &&
     ! printf '%s' "$_fm_digest_value" | grep -Eq '^[0-9a-fA-F]{64}$'; then
    die "$_fm_digest_name must be an exact 64-hex SHA-256 digest."
  fi
done
FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256="$(
  printf '%s' "$FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256" | tr 'A-F' 'a-f'
)"
FORKMESH_LOCAL_BINARY_SHA256="$(
  printf '%s' "$FORKMESH_LOCAL_BINARY_SHA256" | tr 'A-F' 'a-f'
)"
if [ -n "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" ]; then
  [ -f "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" ] &&
    [ ! -L "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" ] ||
    die "FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE must name a non-symlink regular file."
  _fm_key_uid="$(_path_owner_uid "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE")"
  [ "$_fm_key_uid" = "0" ] || [ "$_fm_key_uid" = "$(id -u)" ] ||
    die "Trusted release public key must be owned by root or the installing user."
  _fm_key_mode="$(
    stat -c '%a' "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" 2>/dev/null ||
      stat -f '%Lp' "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" 2>/dev/null || true
  )"
  printf '%s' "$_fm_key_mode" | grep -Eq '^[0-7]{3,4}$' ||
    die "Could not validate trusted release public-key permissions."
  if (( (8
    die "Trusted release public key may not be group/world writable."
  fi
fi



if [ "${FORKMESH_DEBUG:-0}" = "1" ]; then
  export GIT_CURL_VERBOSE=1
fi


export GIT_TERMINAL_PROMPT=0









RUN_ID="$( (head -c 16 /dev/urandom 2>/dev/null | od -An -tx1 2>/dev/null | tr -d ' \n') || true )"
[ -n "$RUN_ID" ] || RUN_ID="$$-$(date +%s 2>/dev/null || echo 0)"
DIAG_OS="$(uname -s 2>/dev/null || echo unknown)"
DIAG_ARCH="$(uname -m 2>/dev/null || echo unknown)"
DIAG_DISTRO=""
if [ -r /etc/os-release ]; then
  DIAG_DISTRO="$( ( . /etc/os-release 2>/dev/null; printf '%s' "${ID:-}" ) || true )"
fi

DIAG_MISSING=""


CURRENT_STEP="start"

diag() {
  [ "${FORKMESH_NO_DIAG:-0}" = "1" ] && return 0
  command -v curl >/dev/null 2>&1 || return 0
  local step="$1" ok="$2" detail="${3:-}"


  curl -fsS -m 5 -X POST "$FORKMESH_DIAG_URL" \
    -H 'Content-Type: application/json' \
    --data "{\"run\":\"$RUN_ID\",\"step\":\"$step\",\"ok\":$ok,\"os\":\"$DIAG_OS\",\"arch\":\"$DIAG_ARCH\",\"pm\":\"${PM:-}\",\"distro\":\"$DIAG_DISTRO\",\"version\":\"$INSTALLER_VERSION\",\"detail\":\"$detail\"}" \
    >/dev/null 2>&1 </dev/null &
  return 0
}

on_diag_exit() {
  local code=$?
  [ "$code" -ne 0 ] && diag "$CURRENT_STEP" 0 "exit$code"
  return 0
}
trap on_diag_exit EXIT

resolve_install_node() {

  if [ -n "$FORKMESH_NODE" ]; then
    FORKMESH_NODES="$FORKMESH_NODE"
    return 0
  fi
  command -v curl >/dev/null 2>&1 || die "curl is required to find an online ForkMesh mirror."

  local body raw_nodes nodes_blob source_url sep
  sep="?"
  case "$FORKMESH_INSTALL_SOURCE_URL" in
    *\?*) sep="&" ;;
  esac
  source_url="${FORKMESH_INSTALL_SOURCE_URL}${sep}_=$(date +%s)"
  dbg "Querying install source: $source_url"



  if ! body="$(curl -sSL -m 20 -H 'Cache-Control: no-cache' -H 'Pragma: no-cache' "$source_url")"; then
    die "Could not check for an online ForkMesh mirror (timed out after 20s querying $FORKMESH_INSTALL_SOURCE_URL). Please try again shortly."
  fi
  dbg "install-source response: $body"



  nodes_blob="$(printf '%s\n' "$body" | sed -n 's/.*"nodes"[[:space:]]*:[[:space:]]*\[\([^]]*\)\].*/\1/p')"
  if [ -n "$nodes_blob" ]; then
    raw_nodes="$(printf '%s\n' "$nodes_blob" | tr ',' '\n' | sed -n 's/.*"\([^"]*\)".*/\1/p')"
  else
    raw_nodes="$(printf '%s\n' "$body" | sed -n 's/.*"node"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
  fi

  local valid="" n
  for n in $raw_nodes; do
    case "$n" in
      *[!A-Za-z0-9._:-]*|"") continue ;;
    esac
    case " $valid " in *" $n "*) continue ;; esac
    valid="${valid:+$valid }$n"
  done
  [ -n "$valid" ] || die "No online ForkMesh node is currently mirroring '$FORKMESH_NAME'. Start a node that publishes this repository, then try the installer again."
  FORKMESH_NODES="$valid"
  FORKMESH_NODE="${valid%% *}"
}






ensure_mirror_candidates() {
  [ "${#REPO_CANDIDATES[@]}" -gt 0 ] && return 0
  CURRENT_STEP="mirror"
  say "Resolving an online ForkMesh mirror to clone from…"
  resolve_install_node





  REPO_CANDIDATES+=(
    "${FORKMESH_HOST%/}/forkmesh/${FORKMESH_NAME}"
  )
  REPO="${REPO_CANDIDATES[0]}"
  say "Using mirror node: $FORKMESH_NODE"
  case "$FORKMESH_NODES" in
    *" "*) say "  Mirror gateway will fail over across: $FORKMESH_NODES" ;;
  esac
  diag mirror 1
}


















stop_forkmesh_daemons() {
  local pid="" expected="$BIN" live="" owner=""
  if [ "$(id -u)" -eq 0 ] && [ -f "$SYSTEMD_UNIT" ] &&
     grep -Fqx '# Managed-By: ForkMesh installer' "$SYSTEMD_UNIT"; then
    if ! grep -Fqx "ExecStart=$SYSTEMD_BIN" "$SYSTEMD_UNIT"; then
      die "Refusing to stop a modified forkmesh-node.service."
    fi
    systemctl stop forkmesh-node.service ||
      die "Could not stop the managed forkmesh-node.service."
    say "Stopped managed forkmesh-node.service"
    return 0
  fi

  [ -f "$PID_FILE" ] || {
    warn "No managed ForkMesh PID file exists; no process was signalled."
    return 0
  }
  [ ! -L "$PID_FILE" ] || die "Refusing symlink PID file: $PID_FILE"
  IFS= read -r pid < "$PID_FILE" || true
  printf '%s' "$pid" | grep -Eq '^[1-9][0-9]*$' ||
    die "Managed ForkMesh PID file is invalid."
  if ! kill -0 "$pid" 2>/dev/null; then
    rm -f -- "$PID_FILE"
    return 0
  fi
  owner="$(ps -o uid= -p "$pid" 2>/dev/null | tr -d '[:space:]')"
  [ "$owner" = "$(id -u)" ] ||
    die "Refusing to signal PID $pid because it is owned by uid ${owner:-unknown}."
  if [ -e "/proc/$pid/exe" ]; then
    live="$(readlink "/proc/$pid/exe" 2>/dev/null || true)"
    live="${live% (deleted)}"
    expected="$(_canonical_path "$BIN" 2>/dev/null || printf '%s' "$BIN")"
    [ "$live" = "$expected" ] ||
      die "Refusing to signal PID $pid because its executable is $live, not $expected."
  else
    live="$(ps -o command= -p "$pid" 2>/dev/null | awk '{print $1}')"
    case "$live" in "$BIN"|"$BIN.app/Contents/MacOS/ForkMesh") ;; *)
      die "Refusing to signal PID $pid because its command does not match $BIN." ;;
    esac
  fi
  kill "$pid"
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
  done
  if kill -0 "$pid" 2>/dev/null; then
    kill -KILL "$pid"
    sleep 0.1
  fi
  if kill -0 "$pid" 2>/dev/null; then
    die "Managed ForkMesh daemon PID $pid did not stop."
  fi
  rm -f -- "$PID_FILE"
  say "Stopped managed ForkMesh daemon PID $pid"
}

uninstall_forkmesh() {
  local mode="${1:-full}"; shift || true
  local data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
  local config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
  local cache_home="${XDG_CACHE_HOME:-$HOME/.cache}"




  local dirs=(
    "$config_home/ForkMesh"
    "$data_home/ForkMesh"
    "$cache_home/ForkMesh"
    "$HOME/.forkmesh"
  )

  local files=(
    "$BIN"
    "$data_home/applications/forkmesh.desktop"
    "$config_home/autostart/forkmesh.desktop"
    "$data_home/icons/forkmesh.png"
  )

  if [ "$mode" = "reinstall" ]; then
    printf '\033[31mReinstall: removing the existing ForkMesh install and ALL of its data first:\033[0m\n'
  else
    printf '\033[31mThis removes ForkMesh and ALL of its data from this computer:\033[0m\n'
  fi
  printf '  • the forkmesh binary and installed source\n'
  printf '  • settings, the node identity key (the account cannot be recovered)\n'
  printf '  • every mirrored repository, and all chat history\n'
  printf '  • the desktop launcher, icons, and login-autostart entry\n\n'





  if [ "$mode" != "reinstall" ] \
     && [ "${FORKMESH_ASSUME_YES:-0}" != "1" ] && [ "${1:-}" != "--yes" ]; then
    if [ -t 0 ]; then
      printf 'Type DELETE to continue: '
      local answer=""; read -r answer || answer=""
      [ "$answer" = "DELETE" ] || die "Uninstall cancelled."
    else
      die "Refusing to uninstall without confirmation. Re-run with --yes (or set FORKMESH_ASSUME_YES=1) to proceed:  curl -fsSL $FORKMESH_HOST/install.sh | bash -s -- --uninstall --yes"
    fi
  fi













  stop_forkmesh_daemons "installer uninstall"

  if [ "$(id -u)" -eq 0 ] && [ -f "$SYSTEMD_UNIT" ] &&
     grep -Fqx '# Managed-By: ForkMesh installer' "$SYSTEMD_UNIT"; then
    systemctl disable forkmesh-node.service >/dev/null 2>&1 || true
    rm -f -- "$SYSTEMD_UNIT" "$SYSTEMD_ENV"
    systemctl daemon-reload >/dev/null 2>&1 || true
    if [ -f "$SYSTEMD_INSTALL_MARKER" ] &&
       grep -Fqx "binary=$SYSTEMD_BIN" "$SYSTEMD_INSTALL_MARKER"; then
      _managed_hash="$(sed -n 's/^sha256=//p' "$SYSTEMD_INSTALL_MARKER" | head -n1)"
      _current_hash="$(_sha256_file "$SYSTEMD_BIN")"
      if [ -n "$_managed_hash" ] && [ "$_managed_hash" = "$_current_hash" ]; then
        rm -f -- "$SYSTEMD_BIN"
      else
        warn "Managed service binary changed since install; preserving $SYSTEMD_BIN."
      fi
      rm -f -- "$SYSTEMD_INSTALL_MARKER"
    fi
    if [ -f "$SYSTEMD_STATE_MARKER" ] &&
       [ ! -L "$SYSTEMD_STATE_MARKER" ] &&
       [ "$(_path_owner_uid "$SYSTEMD_STATE_MARKER")" = "0" ] &&
       grep -Fqx 'forkmesh-managed-service-v1' "$SYSTEMD_STATE_MARKER" &&
       grep -Fqx "path=$SYSTEMD_STATE_DIR" "$SYSTEMD_STATE_MARKER"; then
      rm -rf -- "$SYSTEMD_STATE_DIR"
    else
      warn "Managed service state marker is missing or invalid; preserving $SYSTEMD_STATE_DIR."
    fi
  fi

  local d f
  _remove_managed_src "$SRC"
  for d in "${dirs[@]}"; do
    if [ -e "$d" ]; then rm -rf -- "$d" && say "Removed $d"; fi
  done


  rmdir -- "$(dirname "$SRC")" 2>/dev/null || true
  for f in "${files[@]}"; do
    if [ -e "$f" ]; then rm -f -- "$f" && say "Removed $f"; fi
  done

  find "$data_home/icons/hicolor" -name 'forkmesh.png' -delete 2>/dev/null || true

  command -v update-desktop-database >/dev/null 2>&1 \
    && update-desktop-database "$data_home/applications" >/dev/null 2>&1 || true
  command -v gtk-update-icon-cache >/dev/null 2>&1 \
    && gtk-update-icon-cache -f -t "$data_home/icons/hicolor" >/dev/null 2>&1 || true
  if [ "$mode" = "reinstall" ]; then
    say "Previous ForkMesh install removed; installing a fresh copy now."
    return 0
  fi
  say "ForkMesh has been completely removed."
  exit 0
}




for arg in "$@"; do
  case "$arg" in
    --uninstall|--remove|-u) CURRENT_STEP="uninstall"; trap - EXIT; shift || true
      uninstall_forkmesh full "$@" ;;
    --reinstall) FORKMESH_REINSTALL=1; shift || true ;;
  esac
done
if [ "$FORKMESH_REINSTALL" = "1" ]; then
  if [ -n "$FORKMESH_EXPECTED_BUILD_COMMIT" ]; then
    DEFER_PINNED_REINSTALL=1
    say "Reinstall requested — validating the replacement before clearing the existing node."
  else
    CURRENT_STEP="reinstall"
    say "Reinstall requested — clearing the existing install before reinstalling."
    uninstall_forkmesh reinstall
  fi
fi





printf '\033[32m+-----------------------------------------------+\033[0m\n'
printf '\033[32m|\033[0m  ForkMesh installer  \033[2mv%-24s\033[0m\033[32m|\033[0m\n' "$INSTALLER_VERSION"
printf '\033[32m+-----------------------------------------------+\033[0m\n'
say "Host:   $FORKMESH_HOST"
say "Source: ${SRC}"
say "Target: ${BIN}"
[ -n "$FORKMESH_NODE_NAME" ] && say "Node:   $FORKMESH_NODE_NAME"



[ -n "$FORKMESH_OWNER" ] && say "Owner:  $FORKMESH_OWNER  (this node will be attached to this account)"
if [ "$(id -u)" -eq 0 ]; then
  say "Privileges: running as root (no sudo needed)"
else
  say "Privileges: non-root; will use sudo/doas for package installs"
fi
diag start 1





if [ -z "$REPO" ] && [ -z "$FORKMESH_LOCAL_BINARY" ]; then
  ensure_mirror_candidates
fi



[ "${#REPO_CANDIDATES[@]}" -eq 0 ] && [ -n "$REPO" ] && REPO_CANDIDATES=("$REPO")




SUDO=""
need_sudo() {
  if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
  elif command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  elif command -v doas >/dev/null 2>&1; then
    SUDO="doas"
  else
    return 1
  fi
  return 0
}




PM=""
PM_INSTALL=()
PM_UPDATED=0
detect_pm() {
  if   command -v apt-get >/dev/null 2>&1; then PM="apt";    PM_INSTALL=(apt-get install -y)
  elif command -v dnf     >/dev/null 2>&1; then PM="dnf";    PM_INSTALL=(dnf install -y)
  elif command -v yum     >/dev/null 2>&1; then PM="yum";    PM_INSTALL=(yum install -y)
  elif command -v pacman  >/dev/null 2>&1; then PM="pacman"; PM_INSTALL=(pacman -S --noconfirm --needed)
  elif command -v zypper  >/dev/null 2>&1; then PM="zypper"; PM_INSTALL=(zypper install -y)
  elif command -v apk     >/dev/null 2>&1; then PM="apk";    PM_INSTALL=(apk add)
  elif command -v brew    >/dev/null 2>&1; then PM="brew";   PM_INSTALL=(brew install)
  else return 1
  fi
  return 0
}


pm_refresh() {
  [ "$PM_UPDATED" -eq 1 ] && return 0
  PM_UPDATED=1
  case "$PM" in
    apt)    run_pm apt-get update ;;
    pacman) run_pm pacman -Sy ;;
  esac
}




run_pm() {
  if [ "$PM" = "brew" ]; then
    say "Running: $*"
    "$@"
  else
    say "Running: ${SUDO:+$SUDO }$*"
    $SUDO "$@"
  fi
}


pm_install() {





  if [ "$PM" != "brew" ] && ! need_sudo; then
    die "Installing packages via $PM needs root, but neither sudo nor doas is available. Re-run as root, or install the build dependencies manually and re-run with FORKMESH_NO_INSTALL_DEPS=1."
  fi
  pm_refresh
  run_pm "${PM_INSTALL[@]}" "$@"
}




pkg_for() {
  local what="$1"
  case "$PM:$what" in
    apt:git)        echo git ;;
    apt:age)        echo age ;;
    apt:tar)        echo tar ;;
    apt:cmake)      echo cmake ;;
    apt:compiler)   echo "g++" ;;
    apt:qt)         echo "qt6-base-dev qt6-svg-dev" ;;
    apt:openssl)    echo libssl-dev ;;

    dnf:git|yum:git)            echo git ;;
    dnf:age|yum:age)            echo age ;;
    dnf:tar|yum:tar)            echo tar ;;
    dnf:cmake|yum:cmake)        echo cmake ;;
    dnf:compiler|yum:compiler)  echo "gcc-c++" ;;
    dnf:qt|yum:qt)              echo "qt6-qtbase-devel qt6-qtsvg-devel" ;;
    dnf:openssl|yum:openssl)    echo openssl-devel ;;

    pacman:git)       echo git ;;
    pacman:age)       echo age ;;
    pacman:tar)       echo tar ;;
    pacman:cmake)     echo cmake ;;
    pacman:compiler)  echo gcc ;;
    pacman:qt)        echo "qt6-base qt6-svg" ;;
    pacman:openssl)   echo openssl ;;

    zypper:git)       echo git ;;
    zypper:age)       echo age ;;
    zypper:tar)       echo tar ;;
    zypper:cmake)     echo cmake ;;
    zypper:compiler)  echo "gcc-c++" ;;
    zypper:qt)        echo "qt6-base-devel qt6-svg-devel" ;;
    zypper:openssl)   echo libopenssl-devel ;;

    apk:git)       echo git ;;
    apk:age)       echo age ;;
    apk:tar)       echo tar ;;
    apk:cmake)     echo "cmake make" ;;
    apk:compiler)  echo "g++" ;;
    apk:qt)        echo "qt6-qtbase-dev qt6-qtsvg-dev" ;;
    apk:openssl)   echo "openssl-dev" ;;

    brew:git)       echo git ;;
    brew:age)       echo age ;;
    brew:tar)       echo gnu-tar ;;
    brew:cmake)     echo cmake ;;
    brew:qt)        echo qt ;;
    brew:openssl)   echo "openssl@3" ;;
    brew:compiler)  echo "" ;;
  esac
}



ensure() {
  local what="$1"; shift
  if "$@" >/dev/null 2>&1; then
    say "  $what: already present"
    return 0
  fi

  if [ "${FORKMESH_NO_INSTALL_DEPS:-0}" = "1" ]; then
    die "'$what' is required but not installed (auto-install disabled via FORKMESH_NO_INSTALL_DEPS)."
  fi
  if [ -z "$PM" ]; then
    die "'$what' is required but not installed, and no supported package manager was found to install it."
  fi
  if ! need_sudo && [ "$PM" != "brew" ]; then
    die "'$what' is missing and installing it needs root, but neither sudo nor doas is available."
  fi

  local pkgs; pkgs="$(pkg_for "$what")"
  [ -n "$pkgs" ] || die "'$what' is required but no package candidate is known for '$PM'."

  say "Installing missing prerequisite: $what ($pkgs)"
  DIAG_MISSING="${DIAG_MISSING:+$DIAG_MISSING }$what"

  pm_install $pkgs || die "Failed to install $pkgs via $PM."

  "$@" >/dev/null 2>&1 || die "Installed $pkgs but the check for '$what' still fails. If it is already present in a custom prefix, export PATH/CMAKE_PREFIX_PATH so the check can see it, then re-run."
}


have_compiler() {
  command -v cc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1 \
    || command -v g++ >/dev/null 2>&1 || command -v c++ >/dev/null 2>&1
}





pkg_config_probe() {
  if command -v pkg-config >/dev/null 2>&1; then
    pkg-config "$@"
  elif command -v pkgconf >/dev/null 2>&1; then
    pkgconf "$@"
  else
    return 1
  fi
}









have_qt6_cmake_package() {
  local root cfg cmake_dir

  for root in ${CMAKE_PREFIX_PATH:+${CMAKE_PREFIX_PATH//[:;]/ }} \
              ${Qt6_DIR:+$Qt6_DIR} \
              /usr /usr/local /opt/homebrew/opt/qt /usr/local/opt/qt; do
    for cfg in "$root"/Qt6Config.cmake \
               "$root"/lib/cmake/Qt6/Qt6Config.cmake \
               "$root"/lib64/cmake/Qt6/Qt6Config.cmake \
               "$root"/lib/*/cmake/Qt6/Qt6Config.cmake; do
      [ -f "$cfg" ] || continue
      cmake_dir="$(dirname "$(dirname "$cfg")")"
      if [ -f "$cmake_dir/Qt6Widgets/Qt6WidgetsConfig.cmake" ] &&
         [ -f "$cmake_dir/Qt6Svg/Qt6SvgConfig.cmake" ]; then
        return 0
      fi
    done
  done
  return 1
}

have_qt6_dev() {


  local mods="Qt6Widgets Qt6Network Qt6Svg Qt6Concurrent"
  if [ "$(uname -s)" = "Linux" ]; then

    mods="$mods Qt6DBus"
  fi

  if pkg_config_probe --exists $mods 2>/dev/null; then
    return 0
  fi





  have_qt6_cmake_package
}

have_openssl_dev() {
  if pkg_config_probe --exists openssl 2>/dev/null; then
    return 0
  fi
  [ -f /usr/include/openssl/ssl.h ] ||
    [ -f /usr/local/include/openssl/ssl.h ]
}

have_age_tools() {
  command -v age >/dev/null 2>&1 &&
    command -v age-keygen >/dev/null 2>&1
}














ensure_qt_runtime() {
  [ "$(uname -s)" = "Linux" ] || return 0

  if command -v ldconfig >/dev/null 2>&1 \
     && ldconfig -p 2>/dev/null | grep -q 'libQt6Widgets\.so\.6'; then
    say "  Qt 6 runtime: already present"
    return 0
  fi
  if [ "${FORKMESH_NO_INSTALL_DEPS:-0}" = "1" ]; then
    warn "Qt 6 runtime libraries appear to be missing and auto-install is disabled (FORKMESH_NO_INSTALL_DEPS); the prebuilt binary may fail to start until they are installed (e.g. apt install qt6-base-dev qt6-svg-dev)."
    return 0
  fi
  if [ -z "$PM" ]; then
    warn "Qt 6 runtime libraries appear to be missing and no supported package manager was found to install them; the prebuilt binary may fail to start (install Qt 6 base + svg runtime, then re-run: $BIN)."
    return 0
  fi
  local qt_pkgs; qt_pkgs="$(pkg_for qt)"
  [ -n "$qt_pkgs" ] || { warn "No Qt 6 runtime package candidate is known for '$PM'."; return 0; }
  say "Installing Qt 6 runtime libraries ($qt_pkgs) for the prebuilt binary"

  pm_install $qt_pkgs || die "Failed to install the Qt 6 runtime ($qt_pkgs) via $PM; the prebuilt binary cannot start without it."
}

if detect_pm; then
  say "Detected package manager: $PM (${PM_INSTALL[*]})"
else
  warn "No supported package manager found; missing tools cannot be auto-installed."
fi

CURRENT_STEP="deps"
say "Checking runtime prerequisites (Git, age, age-keygen, tar)…"
ensure git command -v git
ensure age have_age_tools
ensure tar command -v tar






detect_release_asset() {
  local os arch
  os="$(uname -s 2>/dev/null || echo unknown)"
  arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$os" in
    Linux)                            os="linux" ;;
    Darwin)                           os="macos" ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT)  os="windows" ;;
    *) os="$(printf '%s' "$os" | tr '[:upper:]' '[:lower:]')" ;;
  esac
  case "$arch" in
    x86_64|amd64|x64) arch="x86_64" ;;
    aarch64|arm64)    arch="arm64" ;;
  esac
  ASSET_OS="$os"
  ASSET_ARCH="$arch"
  ASSET_NAME="forkmesh-${os}-${arch}"
  [ "$os" = "windows" ] && ASSET_NAME="${ASSET_NAME}.exe"

  RELEASE_CHANNEL="${FORKMESH_RELEASE:-latest}"
  ASSET_REL_PATH=".forkmesh/releases/${RELEASE_CHANNEL}/${ASSET_NAME}"
}






_sparse_fetch_file() {
  local repo="$1" tmp="$2"; shift 2




  local paths=("$@") mode
  for mode in "--filter=blob:none" ""; do
    rm -rf "$tmp"; mkdir -p "$tmp" || return 1

    if git clone --quiet --depth 1 $mode --no-checkout "$repo" "$tmp" >/dev/null 2>&1 \
        && git -C "$tmp" sparse-checkout set --no-cone "${paths[@]}" >/dev/null 2>&1 \
        && git -C "$tmp" checkout --quiet >/dev/null 2>&1 \
        && [ -s "$tmp/${paths[0]}" ]; then
      return 0
    fi
  done
  return 1
}



_repo_owner_name() {
  local u="${1%.git}"
  RELEASE_REPO_NAME="${u##*/}"; u="${u%/*}"
  RELEASE_REPO_OWNER="${u##*/}"
}








_manifest_repo() {
  [ -f "$1" ] || return 0
  sed -n 's/.*"repo"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1" | head -n 1
}



_manifest_build_commit() {
  [ -f "$1" ] || return 0
  sed -n 's/.*"build_commit"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1" |
    head -n 1 | tr 'A-F' 'a-f'
}

_manifest_tag() {
  [ -f "$1" ] || return 0
  sed -n 's/.*"tag"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1" |
    head -n 1
}

_manifest_checksums_sha256() {
  [ -f "$1" ] || return 0
  sed -n 's/.*"checksums_sha256"[[:space:]]*:[[:space:]]*"\([0-9a-fA-F]*\)".*/\1/p' "$1" |
    head -n 1 | tr 'A-F' 'a-f'
}




_verify_release_metadata() {
  local manifest="$1" signature="$2" sums="$3"
  local manifest_hash sums_hash expected_sums tag build_commit
  manifest_hash="$(_sha256_file "$manifest")"
  if ! printf '%s' "$manifest_hash" | grep -Eq '^[0-9a-f]{64}$'; then
    warn "A working SHA-256 tool is required for every prebuilt install."
    return 1
  fi
  if [ -n "$FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256" ]; then
    if [ "$manifest_hash" != "$FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256" ]; then
      warn "Release manifest does not match the controller-pinned SHA-256."
      return 1
    fi
  elif [ -n "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" ]; then
    if ! command -v openssl >/dev/null 2>&1; then
      warn "OpenSSL is required to verify the trusted release signature."
      return 1
    fi
    if [ ! -f "$signature" ] || [ "$(wc -c < "$signature" | tr -d ' ')" != "64" ] ||
       ! openssl pkeyutl -verify -pubin -rawin \
           -inkey "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" \
           -in "$manifest" -sigfile "$signature" >/dev/null 2>&1; then
      warn "Release manifest signature is missing or invalid."
      return 1
    fi
  else
    warn "No independent release trust anchor is configured; refusing prebuilt bytes."
    return 1
  fi

  grep -Eq '"schema"[[:space:]]*:[[:space:]]*"forkmesh-release-v2"' "$manifest" ||
    { warn "Authenticated release manifest uses an unsupported schema."; return 1; }
  expected_sums="$(_manifest_checksums_sha256 "$manifest")"
  sums_hash="$(_sha256_file "$sums")"
  if ! printf '%s' "$expected_sums" | grep -Eq '^[0-9a-f]{64}$' ||
     [ "$sums_hash" != "$expected_sums" ]; then
    warn "SHASUMS256.txt is not bound to the authenticated release manifest."
    return 1
  fi
  if [ -n "$FORKMESH_EXPECTED_BUILD_COMMIT" ]; then
    build_commit="$(_manifest_build_commit "$manifest")"
    if [ "$build_commit" != "$FORKMESH_EXPECTED_BUILD_COMMIT" ]; then
      warn "Published release does not match required build commit $FORKMESH_EXPECTED_BUILD_COMMIT."
      return 1
    fi
    tag="$(_manifest_tag "$manifest")"
    if [ "$tag" != "v$FORKMESH_EXPECTED_RELEASE_VERSION" ] &&
       [ "$tag" != "$FORKMESH_EXPECTED_RELEASE_VERSION" ]; then
      warn "Published release does not match required version $FORKMESH_EXPECTED_RELEASE_VERSION."
      return 1
    fi
  fi
  return 0
}




_install_binary() {
  local candidate="$1" expected_hash="${2:-}" staged source_hash staged_hash
  INSTALL_BINARY_FAILURE_KIND=""
  mkdir -p "$BIN_DIR" || return 1
  if ! printf '%s' "$expected_hash" | grep -Eq '^[0-9a-f]{64}$'; then
    warn "An authenticated SHA-256 is required before staging a prebuilt binary."
    INSTALL_BINARY_FAILURE_KIND="checksum"
    return 1
  fi
  source_hash="$(_sha256_file "$candidate")"
  if [ "$source_hash" != "$expected_hash" ]; then
    warn "ForkMesh binary failed its source SHA-256 check; leaving the existing node untouched."
    INSTALL_BINARY_FAILURE_KIND="checksum"
    return 1
  fi
  staged="$(mktemp "$BIN_DIR/.forkmesh-install.XXXXXX" 2>/dev/null)" ||
    return 1
  if ! install -m 0755 "$candidate" "$staged" 2>/dev/null; then
    if ! cp "$candidate" "$staged" || ! chmod 0755 "$staged"; then
      rm -f "$staged"
      return 1
    fi
  fi
  staged_hash="$(_sha256_file "$staged")"
  if [ "$staged_hash" != "$expected_hash" ]; then
    warn "Staged ForkMesh binary failed its SHA-256 check; leaving the existing node untouched."
    INSTALL_BINARY_FAILURE_KIND="checksum"
    rm -f "$staged"
    return 1
  fi
  if ! chmod 0755 "$staged"; then
    rm -f "$staged"
    return 1
  fi
  if [ "$DEFER_PINNED_REINSTALL" = "1" ]; then
    CURRENT_STEP="reinstall"
    uninstall_forkmesh reinstall
    DEFER_PINNED_REINSTALL=0
    mkdir -p "$BIN_DIR" || { rm -f "$staged"; return 1; }
  fi
  if ! mv -f "$staged" "$BIN"; then
    rm -f "$staged"
    return 1
  fi
  return 0
}









install_prebuilt_release() {
  command -v git >/dev/null 2>&1 || return 1
  ensure_mirror_candidates
  local tmp repo sums manifest signature canon hash url bin got attempt attempt_url
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/forkmesh-prebuilt.XXXXXX" 2>/dev/null)" || return 1




  if [ -z "$FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256" ] &&
     [ -z "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" ]; then
    FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE="$tmp/release-publisher.pem"
    printf '%s\n' \
      '-----BEGIN PUBLIC KEY-----' \
      'MCowBQYDK2VwAyEAHBYOCW4qnyZkYAnEoqUrYxPiDRszjfJa+xJUeyQPUS0=' \
      '-----END PUBLIC KEY-----' \
      >"$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" || return 1
    chmod 0600 "$FORKMESH_TRUSTED_RELEASE_PUBLIC_KEY_FILE" 2>/dev/null || true
  fi
  sums=".forkmesh/releases/${RELEASE_CHANNEL}/SHASUMS256.txt"
  manifest=".forkmesh/releases/${RELEASE_CHANNEL}/release.json"
  signature=".forkmesh/releases/${RELEASE_CHANNEL}/release.json.sig"
  RELEASE_FRESHNESS_MATCH=0
  RELEASE_CANDIDATE_FAILURE=0
  for repo in "${REPO_CANDIDATES[@]}"; do




    say "Checking $repo for a prebuilt release…"



    if command -v curl >/dev/null 2>&1 &&
       _sparse_fetch_file "$repo" "$tmp" "$sums" "$manifest" "$signature"; then
      if [ ! -s "$tmp/$manifest" ] ||
         ! _verify_release_metadata "$tmp/$manifest" "$tmp/$signature" "$tmp/$sums"; then
        RELEASE_CANDIDATE_FAILURE=1
        continue
      fi
      RELEASE_FRESHNESS_MATCH=1
      hash="$(awk -v n="$ASSET_NAME" '$2==n {print $1; exit}' "$tmp/$sums" 2>/dev/null)"
      if printf '%s' "$hash" | grep -Eq '^[0-9a-f]{64}$'; then



        canon="$(_manifest_repo "$tmp/$manifest")"
        if printf '%s' "$canon" | grep -Eq '^[^/]+/[^/]+$'; then
          RELEASE_REPO_OWNER="${canon%%/*}"
          RELEASE_REPO_NAME="${canon##*/}"
        else
          _repo_owner_name "$repo"
        fi
        url="${FORKMESH_HOST%/}/api/repo/${RELEASE_REPO_OWNER}/${RELEASE_REPO_NAME}/releases/blob/sha256/${hash}"
        bin="$tmp/asset.bin"
        say "Downloading prebuilt ${ASSET_OS}/${ASSET_ARCH} binary ($ASSET_NAME)…"
        for attempt in 1 2; do
          attempt_url="$url"
          if [ "$attempt" = "2" ]; then
            attempt_url="${url}?retry=$(date +%s)"
          fi
          if ! curl -fsSL -H 'Cache-Control: no-cache' "$attempt_url" -o "$bin" 2>/dev/null || [ ! -s "$bin" ]; then
            continue
          fi
          got="$(_sha256_file "$bin")"
          if ! printf '%s' "$got" | grep -Eq '^[0-9a-f]{64}$'; then
            warn "A working SHA-256 tool is required for every prebuilt install."
            RELEASE_CANDIDATE_FAILURE=1
            rm -f "$bin"
            continue
          elif [ -n "$got" ] && [ "$got" != "$hash" ]; then
            warn "Checksum mismatch for $ASSET_NAME (expected $hash, got $got); skipping."
            rm -f "$bin"
            continue
          elif _install_binary "$bin" "$hash"; then
            REPO="$repo"; rm -rf "$tmp"
            say "Installed prebuilt ForkMesh ${ASSET_OS}/${ASSET_ARCH} binary to $BIN"
            return 0
          elif [ -n "${INSTALL_BINARY_FAILURE_KIND:-}" ]; then
            RELEASE_CANDIDATE_FAILURE=1
          fi
        done
      fi
    fi

  done
  rm -rf "$tmp"
  return 1
}







install_local_binary() {
  [ -n "$FORKMESH_LOCAL_BINARY" ] || return 1
  if [ ! -s "$FORKMESH_LOCAL_BINARY" ]; then
    warn "Uploaded binary $FORKMESH_LOCAL_BINARY is missing or empty; falling back to a relay download."
    return 1
  fi
  if [ -n "$FORKMESH_LOCAL_OS" ] && [ "$FORKMESH_LOCAL_OS" != "$ASSET_OS" ]; then
    warn "Uploaded binary targets $FORKMESH_LOCAL_OS but this machine is $ASSET_OS; falling back to a relay download."
    return 1
  fi
  if [ -n "$FORKMESH_LOCAL_ARCH" ] && [ "$FORKMESH_LOCAL_ARCH" != "$ASSET_ARCH" ]; then
    warn "Uploaded binary targets $FORKMESH_LOCAL_ARCH but this machine is $ASSET_ARCH; falling back to a relay download."
    return 1
  fi
  if [ -z "$FORKMESH_LOCAL_BINARY_SHA256" ]; then
    warn "Uploaded binary has no controller-pinned SHA-256; refusing it and falling back to a signed release."
    return 1
  fi
  _install_binary "$FORKMESH_LOCAL_BINARY" "$FORKMESH_LOCAL_BINARY_SHA256" || return 1
  say "Installed the directly-uploaded ForkMesh binary to $BIN"
  return 0
}

detect_release_asset
if [ "${FORKMESH_FROM_SOURCE:-0}" != "1" ]; then
  CURRENT_STEP="prebuilt"
  if install_local_binary; then
    INSTALLED_PREBUILT=1
    ensure_qt_runtime
    diag prebuilt 1 "uploaded"
  elif install_prebuilt_release; then
    INSTALLED_PREBUILT=1
    ensure_qt_runtime
    diag prebuilt 1 "$ASSET_NAME"
  elif [ -n "$FORKMESH_EXPECTED_BUILD_COMMIT" ] &&
       [ "${RELEASE_FRESHNESS_MATCH:-0}" != "1" ]; then
    diag prebuilt 0 "release-commit-mismatch"
    die "No published ForkMesh artifact matches required build commit $FORKMESH_EXPECTED_BUILD_COMMIT. Publish that commit's release before retrying the fleet binary install."
  elif [ -n "$FORKMESH_EXPECTED_BUILD_COMMIT" ] &&
       [ "${RELEASE_CANDIDATE_FAILURE:-0}" = "1" ]; then
    diag prebuilt 0 "release-candidate-provenance"
    die "The published ForkMesh release failed authenticated-manifest or SHA-256 verification. The existing binary and daemon were left untouched."
  elif [ "${RELEASE_CANDIDATE_FAILURE:-0}" = "1" ] &&
       [ "$FORKMESH_NO_SOURCE_FALLBACK" = "1" ]; then
    diag prebuilt 0 "release-authentication"
    die "No prebuilt ForkMesh release passed independent manifest authentication and SHA-256 verification. Configure a trusted publisher key or controller-pinned manifest digest."
  elif [ "$FORKMESH_NO_SOURCE_FALLBACK" = "1" ]; then
    diag prebuilt 0 "$ASSET_NAME"
    die "No prebuilt ForkMesh binary is published for ${ASSET_OS}/${ASSET_ARCH}, and falling back to a source build is disabled (FORKMESH_NO_SOURCE_FALLBACK=1, the default on headless Linux). Publish a prebuilt binary for this platform, or re-run with FORKMESH_NO_SOURCE_FALLBACK=0 to allow a source build."
  else
    say "No prebuilt binary published for ${ASSET_OS}/${ASSET_ARCH}; building from source."
    diag prebuilt 0 "$ASSET_NAME"
  fi
fi



if [ "$INSTALLED_PREBUILT" != "1" ]; then
ensure cmake  command -v cmake



if ! have_compiler; then
  if [ "$(uname -s)" = "Darwin" ]; then
    if [ "${FORKMESH_NO_INSTALL_DEPS:-0}" = "1" ]; then
      die "A C++ compiler is required. Install the Xcode Command Line Tools: xcode-select --install"
    fi
    say "Installing Xcode Command Line Tools (a system dialog may appear)"
    DIAG_MISSING="${DIAG_MISSING:+$DIAG_MISSING }compiler"
    xcode-select --install 2>/dev/null || true
    until have_compiler; do
      say "Waiting for the Command Line Tools install to finish..."
      sleep 10
    done
  else
    ensure compiler have_compiler
  fi
fi






ensure qt have_qt6_dev
ensure openssl have_openssl_dev


diag deps 1 "${DIAG_MISSING:-none}"










owns_src() { _marker_is_valid "$SRC"; }



repo_node() {
  local r="${1%/}"
  r="${r%/*}"
  printf '%s' "${r##*/}"
}





classify_clone_failure() {
  case "$1" in
    *"failed integrity check"*|*"repository failed integrity"*) echo "integrity pin rejected by the relay" ;;
    *"Host timed out"*|*"error: 504"*|*" 504"*)                 echo "mirror endpoint timed out (HTTP 504)" ;;
    *"error: 502"*|*" 502"*)                                    echo "relay gateway error (HTTP 502)" ;;
    *"error: 503"*|*" 503"*)                                    echo "mirror temporarily unavailable (HTTP 503)" ;;
    *"error: 404"*|*"not found"*|*"Repository not found"*)      echo "repository not found on this mirror (HTTP 404)" ;;
    *"error: 401"*|*"Authentication failed"*)                   echo "authentication required (HTTP 401)" ;;
    *"Could not resolve host"*|*"Couldn't resolve"*)            echo "DNS resolution failed" ;;
    *"Connection refused"*|*"Failed to connect"*)               echo "connection refused" ;;
    *"timed out"*|*"timeout"*|*"Operation timed out"*)          echo "network timeout" ;;
    *)                                                          echo "git clone failed" ;;
  esac
}







clean_clone() {
  local tmp="$SRC.new.$$"
  local repo out rc reason node total="${#REPO_CANDIDATES[@]}" idx=0
  CLONE_FAIL_REASON=""
  for repo in "${REPO_CANDIDATES[@]}"; do
    idx=$((idx + 1))
    node="$(repo_node "$repo")"
    rm -rf -- "$tmp"
    if [ "$total" -gt 1 ]; then
      say "Cloning $repo  (mirror $idx of $total)"
    else
      say "Cloning $repo"
    fi


    out="$(git clone --depth 1 "$repo" "$tmp" 2>&1)"; rc=$?
    printf '%s\n' "$out"
    if [ "$rc" -eq 0 ]; then
      if [ -e "$SRC" ]; then
        _remove_managed_src "$SRC"
      fi
      mv "$tmp" "$SRC"
      _write_managed_marker "$SRC" ||
        die "Could not write the managed-install marker in $SRC."
      REPO="$repo"
      return 0
    fi
    rm -rf -- "$tmp"
    reason="$(classify_clone_failure "$out")"
    CLONE_FAIL_REASON="$reason"



    case "$out" in
      *"failed integrity check"*|*"repository failed integrity"*) PIN_FAILURE=1; return 1 ;;
    esac
    warn "Mirror '$node' could not be cloned: $reason."
    [ "$idx" -lt "$total" ] && say "  Falling back to the next online mirror…"
  done
  return 1
}

fetch_source() {
  mkdir -p "$(dirname "$SRC")" || return 1
  if [ -d "$SRC/.git" ]; then
    if ! owns_src; then
      warn "$SRC is an existing checkout not created by this installer; replacing it with a fresh clone."
      clean_clone || return 1
      return 0
    fi
    say "Updating existing checkout in $SRC"




    git -C "$SRC" remote set-url origin "$REPO" 2>/dev/null || true
    if ! git -C "$SRC" pull --ff-only; then
      warn "Could not update from $REPO; re-cloning from the current live mirror."
      clean_clone || return 1
    fi
  else
    clean_clone || return 1
  fi



  if [ ! -f "$SRC/qt_client/CMakeLists.txt" ]; then
    warn "Checkout in $SRC is incomplete (no qt_client/CMakeLists.txt); re-cloning."
    clean_clone || return 1
  fi
  [ -f "$SRC/qt_client/CMakeLists.txt" ] || return 1
}

build_client() {
  local jobs build_dir
  jobs="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )"
  build_dir="$SRC/qt_client/build"
  local cmake_args=(-S "$SRC/qt_client" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DFORKMESH_BUILD_TESTS=OFF)
  if command -v brew >/dev/null 2>&1; then
    local qt_prefix ssl_prefix
    qt_prefix="$(brew --prefix qt 2>/dev/null || true)"
    ssl_prefix="$(brew --prefix openssl@3 2>/dev/null || true)"
    [ -n "$qt_prefix" ] && cmake_args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
    [ -n "$ssl_prefix" ] && cmake_args+=("-DOPENSSL_ROOT_DIR=$ssl_prefix")
  fi
  say "Configuring"
  cmake "${cmake_args[@]}" || return 1
  say "Building (this can take a few minutes)"
  cmake --build "$build_dir" -j"$jobs" || return 1
  BUILD="$build_dir"
}

install_client() {
  local built
  if [ -x "$BUILD/ForkMesh.app/Contents/MacOS/ForkMesh" ]; then
    built="$BUILD/ForkMesh.app/Contents/MacOS/ForkMesh"
  else
    built="$BUILD/forkmesh"
  fi
  [ -x "$built" ] || { warn "Build did not produce an executable at $built."; return 1; }
  mkdir -p "$BIN_DIR" || return 1
  install -m 0755 "$built" "$BIN" 2>/dev/null \
    || { cp "$built" "$BIN" && chmod 0755 "$BIN"; } || return 1
  say "Installed to $BIN"
}



attempt_install() {



  CURRENT_STEP="fetch";   fetch_source   || { diag fetch 0 "${CLONE_FAIL_REASON:-fetch_failed}"; return 1; }; diag fetch 1
  CURRENT_STEP="build";   build_client   || return 1; diag build 1
  CURRENT_STEP="install"; install_client || return 1; diag install 1
}




pin_failure_help() {
  printf '\033[31mError:\033[0m The mirror serving %s failed its integrity check.\n' "$REPO" >&2
  printf '\n' >&2
  printf 'This is NOT a problem with your machine. The relay pins an owner-signed\n' >&2
  printf 'fingerprint of the repository'"'"'s refs and refuses to serve any mirror that\n' >&2
  printf 'does not match it. The currently published pin is stale or mismatched, so\n' >&2
  printf 'every clone is being rejected before any data is sent.\n' >&2
  printf '\n' >&2
  printf 'The repository owner needs to refresh it from the ForkMesh desktop app:\n' >&2
  printf '  open the repo, then click "Reset integrity pin" (it re-signs the refs\n' >&2
  printf '  the node currently serves). A fresh publish from the host node also fixes it.\n' >&2
  printf '\n' >&2
  printf 'Once the pin is reset, re-run:\n' >&2
  printf '  curl -fsSL %s/install.sh | bash\n' "$FORKMESH_HOST" >&2
  exit 1
}

if ! attempt_install; then
  [ "$PIN_FAILURE" = "1" ] && pin_failure_help
  warn "Install failed; retrying once from a clean clone."
  _remove_managed_src "$SRC"
  if ! attempt_install; then
    [ "$PIN_FAILURE" = "1" ] && pin_failure_help


    if [ "${CLONE_FAIL_REASON:-}" ]; then
      warn "Mirrors tried: ${FORKMESH_NODES:-$REPO}"
      warn "Re-run with FORKMESH_DEBUG=1 for the full git/HTTP trace."
      die "Could not fetch the source from any online mirror ($CLONE_FAIL_REASON). All mirrors are unreachable right now — please try again shortly."
    fi
    die "Install failed again after a clean re-clone; see the messages above for the cause."
  fi
fi
fi

case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *) say "Add $BIN_DIR to your PATH, e.g.  export PATH=\"$BIN_DIR:\$PATH\"" ;;
esac








register_desktop_entry() {
  [ "$(uname -s)" = "Linux" ] || return 0
  CURRENT_STEP="desktop"



  if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    say "No display detected; skipping app-menu registration (run:  forkmesh)."
    return 0
  fi
  local script="$SRC/qt_client/install.sh"
  if [ ! -f "$script" ]; then



    if [ "$INSTALLED_PREBUILT" = "1" ]; then
      say "Installed the prebuilt binary; skipping app-menu registration (run:  forkmesh)."
    else
      warn "Desktop integration script not found at $script; skipping menu registration."
    fi
    diag desktop 1 "skipped"
    return 0
  fi
  say "Registering ForkMesh in the application menu (this can take a moment)…"







  local log rc=0
  log="$(mktemp 2>/dev/null || echo "${TMPDIR:-/tmp}/forkmesh-desktop.$$.log")"
  dbg "running desktop helper: bash $script (output log: $log)"
  if command -v timeout >/dev/null 2>&1; then
    timeout 180 bash "$script" >"$log" 2>&1 || rc=$?
  else
    bash "$script" >"$log" 2>&1 || rc=$?
  fi
  dbg "desktop helper exited with status $rc"
  [ "${FORKMESH_DEBUG:-0}" = "1" ] && [ -s "$log" ] && cat "$log" >&2
  if [ "$rc" -eq 0 ]; then
    say "Added to the application menu — search \"ForkMesh\" in Activities/the app grid."
    diag desktop 1
  elif [ "$rc" -eq 124 ]; then
    warn "Desktop menu registration timed out (>180s) and was skipped; ForkMesh still runs via:  forkmesh"
    [ -s "$log" ] && warn "Last helper output: $(tail -n 3 "$log" 2>/dev/null | tr '\n' ' ')"
    warn "Re-run with FORKMESH_DEBUG=1 to see exactly which desktop step hung."
    diag desktop 0 "timeout"
  else
    warn "Could not register the desktop menu entry (exit $rc); ForkMesh still runs via:  forkmesh"
    [ -s "$log" ] && warn "Helper output: $(tail -n 5 "$log" 2>/dev/null | tr '\n' ' ')"
    diag desktop 0 "rc$rc"
  fi
  rm -f "$log" 2>/dev/null || true
}
register_desktop_entry













LAUNCH_MODE=""

launch_root_headless_service() {
  local service_user="forkmesh-node" state_dir="$SYSTEMD_STATE_DIR"
  local binary_hash account home shell
  [ "$(id -u)" -eq 0 ] || return 1
  command -v systemctl >/dev/null 2>&1 &&
    [ -d /run/systemd/system ] ||
    die "A root headless install requires systemd so ForkMesh can run as an unprivileged supervised service. Otherwise rerun the installer as a dedicated non-root user."
  printf '%s' "$FORKMESH_NODE_NAME" | grep -Eq '^[A-Za-z0-9._-]*$' ||
    die "Headless node names may contain only letters, numbers, dot, underscore, and hyphen."
  printf '%s' "$FORKMESH_LINK_CODE" | grep -Eq '^[0-9]{6}$' ||
    die "Headless link code is invalid."
  [ ! -L "$SYSTEMD_UNIT" ] && [ ! -L "$SYSTEMD_ENV" ] &&
    [ ! -L "$SYSTEMD_BIN" ] && [ ! -L "$SYSTEMD_INSTALL_MARKER" ] ||
    die "Refusing a symlink in the managed system-service paths."
  if [ -e "$SYSTEMD_BIN" ] || [ -e "$SYSTEMD_UNIT" ] ||
     [ -e "$SYSTEMD_STATE_DIR" ]; then
    [ -f "$SYSTEMD_INSTALL_MARKER" ] &&
      grep -Fqx 'forkmesh-system-service-v1' "$SYSTEMD_INSTALL_MARKER" &&
      grep -Fqx "binary=$SYSTEMD_BIN" "$SYSTEMD_INSTALL_MARKER" &&
      [ -f "$SYSTEMD_UNIT" ] &&
      grep -Fqx '# Managed-By: ForkMesh installer' "$SYSTEMD_UNIT" &&
      [ -f "$SYSTEMD_STATE_MARKER" ] &&
      [ ! -L "$SYSTEMD_STATE_MARKER" ] &&
      [ "$(_path_owner_uid "$SYSTEMD_STATE_MARKER")" = "0" ] &&
      grep -Fqx 'forkmesh-managed-service-v1' "$SYSTEMD_STATE_MARKER" &&
      grep -Fqx "path=$SYSTEMD_STATE_DIR" "$SYSTEMD_STATE_MARKER" ||
      die "Refusing to overwrite an unmanaged system binary, service, or state directory."
  fi

  if id "$service_user" >/dev/null 2>&1; then
    command -v getent >/dev/null 2>&1 ||
      die "getent is required to validate the existing service account."
    account="$(getent passwd "$service_user")"
    home="$(printf '%s' "$account" | awk -F: '{print $6}')"
    shell="$(printf '%s' "$account" | awk -F: '{print $7}')"
    [ "$(id -u "$service_user")" != "0" ] ||
      die "Refusing to use a root-valued forkmesh-node account."
    [ "$home" = "$state_dir" ] ||
      die "Existing forkmesh-node account has an unexpected home directory."
    case "$shell" in */nologin|*/false) ;; *)
      die "Existing forkmesh-node account does not use a locked login shell." ;;
    esac
  else
    command -v useradd >/dev/null 2>&1 ||
      die "useradd is required to create the unprivileged forkmesh-node service account."
    useradd --system --home-dir "$state_dir" --create-home \
      --shell /usr/sbin/nologin "$service_user"
  fi




  mkdir -p "$state_dir/.local/share/forkmesh" \
    "$state_dir/.config/ForkMesh" "$state_dir/tmp" /etc/forkmesh





  local checkout_root="$state_dir/repositories"
  local flagship_checkout="$checkout_root/forkmesh"
  local settings_file="$state_dir/.config/ForkMesh/ForkMesh.conf"
  local checkout_source="" candidate staged_settings
  mkdir -p "$checkout_root"
  if [ ! -e "$flagship_checkout" ]; then
    ensure_mirror_candidates
    for candidate in "${REPO_CANDIDATES[@]}"; do
      say "Seeding the headless agent checkout from $candidate…"
      if git clone --quiet --branch main --single-branch \
           "$candidate" "$flagship_checkout"; then
        checkout_source="$candidate"
        break
      fi
      rm -rf "$flagship_checkout"
    done
    [ -d "$flagship_checkout/.git" ] ||
      die "ForkMesh was installed, but no verified mirror could seed the headless agent checkout."
  elif [ ! -d "$flagship_checkout/.git" ]; then
    die "Refusing to replace an unmanaged headless checkout path: $flagship_checkout"
  fi
  [ -n "$checkout_source" ] ||
    checkout_source="$(git -C "$flagship_checkout" remote get-url origin 2>/dev/null || true)"
  if [ ! -f "$settings_file" ]; then
    umask 077
    {
      printf '[repositories]\n'
      printf 'items\\1\\actionsEnabled=false\n'
      printf 'items\\1\\cloneUrl=%s\n' "$checkout_source"
      printf 'items\\1\\localPath=%s\n' "$flagship_checkout"
      printf 'items\\1\\name=forkmesh\n'
      printf 'items\\1\\owner=forkmesh\n'
      printf 'items\\1\\publishToNetwork=true\n'
      printf 'items\\size=1\n'
    } >"$settings_file"
  elif grep -Fqx 'items\1\localPath=' "$settings_file"; then
    staged_settings="$state_dir/tmp/ForkMesh.conf.checkout"
    awk -v target="$flagship_checkout" '
      BEGIN {
        slash = sprintf("%c", 92)
        replacement = "items" slash "1" slash "localPath=" target
      }
      $0 == "items" slash "1" slash "localPath=" {
        print replacement
        changed += 1
        next
      }
      { print }
      END { if (changed != 1) exit 42 }
    ' "$settings_file" >"$staged_settings" ||
      die "Could not attach the managed headless checkout to ForkMesh settings."
    mv "$staged_settings" "$settings_file"
  fi
  chown -R "$service_user:$service_user" "$state_dir"
  chmod 0750 "$state_dir"
  chmod 0700 "$checkout_root" "$flagship_checkout"
  chmod 0600 "$settings_file"
  chmod 0700 "$state_dir/.config" "$state_dir/.config/ForkMesh"




  chmod 0700 "$state_dir/tmp"
  {
    printf 'forkmesh-managed-service-v1\n'
    printf 'path=%s\n' "$state_dir"
  } > "$SYSTEMD_STATE_MARKER"
  chown root:root "$SYSTEMD_STATE_MARKER"
  chmod 0600 "$SYSTEMD_STATE_MARKER"

  install -m 0755 "$BIN" "$SYSTEMD_BIN"
  binary_hash="$(_sha256_file "$SYSTEMD_BIN")"
  printf '%s' "$binary_hash" | grep -Eq '^[0-9a-f]{64}$' ||
    die "Could not hash the staged system service binary."
  umask 077
  {
    printf 'FORKMESH_NODE_NAME=%s\n' "$FORKMESH_NODE_NAME"
    printf 'FORKMESH_LINK_CODE=%s\n' "$FORKMESH_LINK_CODE"
    printf 'HOME=%s\n' "$state_dir"
    printf 'XDG_DATA_HOME=%s\n' "$state_dir/.local/share"
    printf 'TMPDIR=%s\n' "$state_dir/tmp"
  } > "$SYSTEMD_ENV"
  {
    printf '# Managed-By: ForkMesh installer\n'
    printf '[Unit]\nDescription=ForkMesh headless node\nAfter=network-online.target\nWants=network-online.target\n\n'
    printf '[Service]\nType=simple\nUser=%s\nGroup=%s\n' "$service_user" "$service_user"
    printf 'EnvironmentFile=%s\nExecStart=%s\n' "$SYSTEMD_ENV" "$SYSTEMD_BIN"
    printf 'Restart=on-failure\nRestartSec=5\nNoNewPrivileges=true\n'
    printf 'PrivateTmp=true\nPrivateDevices=true\nProtectSystem=strict\nProtectHome=true\n'
    printf 'ReadWritePaths=%s\nRestrictSUIDSGID=true\nLockPersonality=true\n' "$state_dir"
    printf 'TasksMax=256\nLimitNOFILE=8192\nTimeoutStopSec=15\n\n'
    printf '[Install]\nWantedBy=multi-user.target\n'
  } > "$SYSTEMD_UNIT"
  {
    printf 'forkmesh-system-service-v1\n'
    printf 'binary=%s\n' "$SYSTEMD_BIN"
    printf 'sha256=%s\n' "$binary_hash"
  } > "$SYSTEMD_INSTALL_MARKER"
  chmod 0600 "$SYSTEMD_ENV" "$SYSTEMD_INSTALL_MARKER"
  chmod 0644 "$SYSTEMD_UNIT"
  systemctl daemon-reload
  systemctl reset-failed forkmesh-node.service >/dev/null 2>&1 || true
  systemctl enable --now forkmesh-node.service




  local stable_tick restarts main_pid
  for stable_tick in 1 2 3 4 5; do
    sleep 1
    systemctl is-active --quiet forkmesh-node.service ||
      die "forkmesh-node.service did not remain active during its startup health check."
    main_pid="$(systemctl show forkmesh-node.service --property=MainPID --value)"
    restarts="$(systemctl show forkmesh-node.service --property=NRestarts --value)"
    printf '%s' "$main_pid" | grep -Eq '^[1-9][0-9]*$' ||
      die "forkmesh-node.service has no live daemon after installation."
    [ "${restarts:-0}" = "0" ] ||
      die "forkmesh-node.service restarted during its startup health check; inspect journalctl -u forkmesh-node.service."
  done
  LAUNCH_MODE="service"
  LOG_PATH="journalctl -u forkmesh-node.service"
  return 0
}

launch_forkmesh() {
  case "$(uname -s)" in
    Darwin)
      LAUNCH_MODE="gui"
      if [ -d "$BUILD/ForkMesh.app" ]; then
        open "$BUILD/ForkMesh.app" && return 0
      fi
      open "$BIN" 2>/dev/null && return 0
      ;;
    *)
      if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
        LAUNCH_MODE="gui"
        if command -v setsid >/dev/null 2>&1; then
          setsid "$BIN" >/dev/null 2>&1 < /dev/null &
        else
          nohup "$BIN" >/dev/null 2>&1 < /dev/null &
        fi
        return 0
      fi







      LAUNCH_MODE="daemon"
      local log rand daemon_pid pid_tmp

      case "$FORKMESH_LINK_CODE" in
        [0-9][0-9][0-9][0-9][0-9][0-9]) ;;
        *)
          rand="$(od -An -N4 -tu4 /dev/urandom 2>/dev/null | tr -d '[:space:]')"
          [ -n "$rand" ] || rand=$(( $(date +%s) + $$ ))
          FORKMESH_LINK_CODE="$(printf '%06d' $(( rand % 1000000 )))"
          ;;
      esac
      if [ "$(id -u)" -eq 0 ]; then
        launch_root_headless_service
        return $?
      fi
      log="${XDG_DATA_HOME:-$HOME/.local/share}/forkmesh/node.log"
      mkdir -p "$(dirname "$log")" 2>/dev/null || true
      if command -v setsid >/dev/null 2>&1; then
        FORKMESH_NODE_NAME="$FORKMESH_NODE_NAME" FORKMESH_LINK_CODE="$FORKMESH_LINK_CODE" setsid "$BIN" >"$log" 2>&1 < /dev/null &
      else
        FORKMESH_NODE_NAME="$FORKMESH_NODE_NAME" FORKMESH_LINK_CODE="$FORKMESH_LINK_CODE" nohup "$BIN" >"$log" 2>&1 < /dev/null &
      fi
      daemon_pid=$!
      mkdir -p "$(dirname "$PID_FILE")"
      pid_tmp="$PID_FILE.tmp.$$"
      umask 077
      printf '%s\n' "$daemon_pid" > "$pid_tmp"
      mv -f "$pid_tmp" "$PID_FILE"
      return 0
      ;;
  esac
  return 1
}

















FORKMESH_TUNNEL_HOSTNAME="${FORKMESH_TUNNEL_HOSTNAME:-}"
FORKMESH_TUNNEL_ZONE="${FORKMESH_TUNNEL_ZONE:-}"
FORKMESH_CLOUDFLARE_ACCOUNT_ID="${FORKMESH_CLOUDFLARE_ACCOUNT_ID:-}"
FORKMESH_RELAY_HOSTNAME="${FORKMESH_RELAY_HOSTNAME:-}"







_tunnel_stage_tools() {
  local tools_dir="$1" tmp="" src="" f
  local wanted="cloudflared_install.py cloudflare_tunnel_bootstrap.py mirror_gateway.py"
  for src in "$SRC/tools" "$SYSTEMD_STATE_DIR/repositories/forkmesh/tools" ""; do
    [ -n "$src" ] && [ -f "$src/cloudflare_tunnel_bootstrap.py" ] && break
  done
  if [ -z "$src" ]; then


    [ "${#REPO_CANDIDATES[@]}" -gt 0 ] || return 1
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/forkmesh-tools.XXXXXX" 2>/dev/null)" || return 1
    local repo fetched=1
    for repo in "${REPO_CANDIDATES[@]}"; do

      if _sparse_fetch_file "$repo" "$tmp" \
           tools/cloudflare_tunnel_bootstrap.py \
           tools/cloudflared_install.py \
           tools/mirror_gateway.py; then
        fetched=0
        break
      fi
    done
    if [ "$fetched" -ne 0 ]; then
      rm -rf "$tmp"
      return 1
    fi
    src="$tmp/tools"
  fi
  mkdir -p "$tools_dir" || { [ -n "$tmp" ] && rm -rf "$tmp"; return 1; }
  for f in $wanted; do
    [ -f "$src/$f" ] || continue
    install -m 0644 "$src/$f" "$tools_dir/$f" || { [ -n "$tmp" ] && rm -rf "$tmp"; return 1; }
  done
  [ -n "$tmp" ] && rm -rf "$tmp"
  [ -f "$tools_dir/cloudflare_tunnel_bootstrap.py" ]
}




_tunnel_write_control_settings() {
  local conf="$1" relay="$2" mirror="$3" node="$4" rkey="$5"
  local staged="$conf.tunnel.$$"
  mkdir -p "$(dirname "$conf")" || return 1
  [ -f "$conf" ] || : > "$conf"
  awk -v relay="$relay" -v mirror="$mirror" -v node="$node" -v rkey="$rkey" '
    function emit() {
      print "cloudflareHostname=" relay
      print "cloudflareMirrorHostname=" mirror
      print "cloudflareNodeName=" node
      print "directMirrorRouterPublicKey=" rkey
    }
    /^\[control\]$/ { print; emit(); inserted = 1; incontrol = 1; next }
    /^\[/ { incontrol = 0 }
    incontrol && (/^cloudflareHostname=/ || /^cloudflareMirrorHostname=/ ||
                  /^cloudflareNodeName=/ || /^directMirrorRouterPublicKey=/) { next }
    { print }
    END { if (!inserted) { print "[control]"; emit() } }
  ' "$conf" > "$staged" || { rm -f "$staged"; return 1; }
  mv "$staged" "$conf"
}

provision_cloudflare_tunnel() {
  CURRENT_STEP="tunnel"
  local hostname zone relay_host node_name account
  local state_home conf appdata gw_root node_binary tools_dir cfd_dest
  local service_user="forkmesh-node" as_service=0
  hostname="$(printf '%s' "$FORKMESH_TUNNEL_HOSTNAME" | tr 'A-Z' 'a-z')"
  printf '%s' "$hostname" | grep -Eq '^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)+$' ||
    { warn "FORKMESH_TUNNEL_HOSTNAME is not a DNS hostname; skipping tunnel provisioning."; return 1; }
  zone="$(printf '%s' "${FORKMESH_TUNNEL_ZONE:-${hostname#*.}}" | tr 'A-Z' 'a-z')"
  relay_host="${FORKMESH_RELAY_HOSTNAME:-${FORKMESH_HOST#*://}}"
  relay_host="${relay_host%%/*}"
  case "$LAUNCH_MODE" in
    service) as_service=1 ;;
    daemon)  as_service=0 ;;
    *) warn "Tunnel provisioning targets headless nodes; skipping (launch mode: ${LAUNCH_MODE:-none})."; return 1 ;;
  esac
  command -v python3 >/dev/null 2>&1 ||
    { warn "python3 is required for tunnel provisioning; skipping."; return 1; }
  command -v openssl >/dev/null 2>&1 ||
    { warn "openssl is required to derive the node public key; skipping tunnel provisioning."; return 1; }
  if [ "$as_service" -eq 1 ]; then
    command -v runuser >/dev/null 2>&1 ||
      { warn "runuser is required to bootstrap the tunnel as $service_user; skipping."; return 1; }
    state_home="$SYSTEMD_STATE_DIR"
    conf="$state_home/.config/ForkMesh/ForkMesh.conf"

    appdata="$state_home/.local/share/ForkMesh/ForkMesh"
    node_binary="$SYSTEMD_BIN"
    tools_dir="/usr/local/share/forkmesh/tools"
    cfd_dest="/usr/local/bin/cloudflared"
  else
    state_home="$HOME"
    conf="${XDG_CONFIG_HOME:-$HOME/.config}/ForkMesh/ForkMesh.conf"
    appdata="${XDG_DATA_HOME:-$HOME/.local/share}/ForkMesh/ForkMesh"
    node_binary="$BIN"
    tools_dir="$(dirname "$BIN_DIR")/share/forkmesh/tools"
    cfd_dest="$appdata/mirror-gateway/bin/cloudflared"
  fi
  gw_root="$appdata/mirror-gateway"


  node_name="$(printf '%s' "$FORKMESH_NODE_NAME" | tr 'A-Z' 'a-z')"
  printf '%s' "$node_name" | grep -Eq '^[a-z][a-z0-9-]{0,62}$' ||
    node_name="${hostname%%.*}"
  printf '%s' "$node_name" | grep -Eq '^[a-z][a-z0-9-]{0,62}$' ||
    { warn "Could not derive a valid node name for the tunnel; skipping."; return 1; }

  say "Provisioning the direct HTTPS mirror endpoint $hostname (zone $zone)…"

  if ! _tunnel_stage_tools "$tools_dir"; then
    warn "Could not stage the pinned mirror tools into $tools_dir; skipping tunnel provisioning."
    return 1
  fi
  say "  Pinned mirror tools staged in $tools_dir"




  if command -v cloudflared >/dev/null 2>&1; then
    say "  cloudflared: already present ($(command -v cloudflared))"
  else
    if [ "$as_service" -eq 0 ]; then
      mkdir -p "$gw_root/bin" && chmod 0700 "$gw_root" "$gw_root/bin" || true
    fi
    if python3 "$tools_dir/cloudflared_install.py" \
         --destination "$cfd_dest" --json-stdout >/dev/null 2>&1; then
      say "  Installed the SHA-256-pinned cloudflared connector to $cfd_dest"
    else


      warn "Could not install the pinned cloudflared now; the node will retry with its own verified installer."
    fi
  fi



  local router_body router_key
  router_body="$(curl -fsS -m 20 "https://$relay_host/api/mirrors/https" 2>/dev/null || true)"
  router_key="$(printf '%s\n' "$router_body" |
    sed -n 's/.*"routerPublicKey"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
  if ! printf '%s' "$router_key" | grep -Eq '^[A-Za-z0-9_-]{43}$'; then
    warn "Could not discover a valid mirror-router public key from https://$relay_host/api/mirrors/https; skipping tunnel provisioning."
    return 1
  fi



  local identity_pem="$appdata/identity/ed25519.pem" waited=0
  say "  Waiting for the node identity key (up to 90s)…"
  while [ ! -s "$identity_pem" ] && [ "$waited" -lt 90 ]; do
    sleep 2
    waited=$((waited + 2))
  done
  if [ ! -s "$identity_pem" ]; then
    warn "The node identity key never appeared at $identity_pem; skipping tunnel provisioning."
    return 1
  fi


  local node_pubkey
  node_pubkey="$(openssl pkey -in "$identity_pem" -pubout -outform DER 2>/dev/null |
    tail -c 32 | base64 | tr '+/' '-_' | tr -d '=')"
  if ! printf '%s' "$node_pubkey" | grep -Eq '^[A-Za-z0-9_-]{43}$'; then
    warn "Could not derive the node public key from $identity_pem; skipping tunnel provisioning."
    return 1
  fi



  if [ "$as_service" -eq 1 ]; then
    systemctl stop forkmesh-node.service ||
      { warn "Could not stop forkmesh-node.service for tunnel provisioning."; return 1; }
  else
    stop_forkmesh_daemons "tunnel provisioning"
  fi


  _tunnel_restart_node() {
    if [ "$as_service" -eq 1 ]; then
      systemctl start forkmesh-node.service ||
        warn "Could not restart forkmesh-node.service; start it manually."
    else
      launch_forkmesh || warn "Could not relaunch the ForkMesh daemon; start it manually with: $BIN"
    fi
  }

  if ! _tunnel_write_control_settings "$conf" "$relay_host" "$hostname" "$node_name" "$router_key"; then
    warn "Could not write the [control] mirror settings to $conf."
    _tunnel_restart_node
    return 1
  fi
  chmod 0600 "$conf" 2>/dev/null || true
  if [ "$as_service" -eq 1 ]; then
    chown "$service_user:$service_user" "$conf" 2>/dev/null || true
  fi





  if [ "$as_service" -eq 1 ]; then
    say "  Running the pinned Cloudflare Tunnel bootstrap as $service_user…"
  else
    say "  Running the pinned Cloudflare Tunnel bootstrap…"
  fi
  set -- \
    "$tools_dir/cloudflare_tunnel_bootstrap.py" \
    --hostname "$hostname" \
    --zone "$zone" \
    --node-name "$node_name" \
    --origin-host 127.0.0.1 \
    --origin-port 8790 \
    --gateway-config "$gw_root/config.json" \
    --mirror-public-key "$node_pubkey" \
    --manifest-signer-command "'$node_binary' --sign-mirror-manifest" \
    --manifest-output "$gw_root/forkmesh-mirror.json" \
    --tunnel-token-file "$gw_root/connector.token"
  if [ -n "$FORKMESH_CLOUDFLARE_ACCOUNT_ID" ]; then
    set -- "$@" --account-id "$FORKMESH_CLOUDFLARE_ACCOUNT_ID"
  fi
  local bootstrap_rc=0
  if [ "$as_service" -eq 1 ]; then
    CLOUDFLARE_API_TOKEN="$CLOUDFLARE_API_TOKEN" \
    HOME="$state_home" \
    XDG_DATA_HOME="$state_home/.local/share" \
    TMPDIR="$state_home/tmp" \
    PYTHONUNBUFFERED=1 \
      runuser -u "$service_user" -- python3 "$@" </dev/null || bootstrap_rc=$?
  else
    CLOUDFLARE_API_TOKEN="$CLOUDFLARE_API_TOKEN" \
    PYTHONUNBUFFERED=1 \
      python3 "$@" </dev/null || bootstrap_rc=$?
  fi
  if [ "$bootstrap_rc" -ne 0 ]; then
    warn "Cloudflare Tunnel bootstrap failed (exit $bootstrap_rc); the node keeps running without a tunnel."
    _tunnel_restart_node
    return 1
  fi
  if [ ! -s "$gw_root/connector.token" ]; then
    warn "The Tunnel bootstrap wrote no connector token; the node keeps running without a tunnel."
    _tunnel_restart_node
    return 1
  fi

  _tunnel_restart_node
  say "Direct HTTPS mirror endpoint provisioned:"
  say "  Hostname:  https://$hostname  (tunnel + proxied DNS in zone $zone)"
  say "  Node name: $node_name    Relay: $relay_host"
  say "  The restarted node auto-starts its gateway, Tunnel connector, and"
  say "  signed endpoint registration (Settings key control/autoStartMirrorServices)."
  diag tunnel 1
  return 0
}

CURRENT_STEP="launch"
LOG_PATH="${XDG_DATA_HOME:-$HOME/.local/share}/forkmesh/node.log"






if [ "$FORKMESH_RESTART" = "1" ] && [ "${FORKMESH_NO_LAUNCH:-0}" != "1" ]; then
  stop_forkmesh_daemons "restart before relaunch"
fi
if [ "${FORKMESH_NO_LAUNCH:-0}" = "1" ]; then
  say "Done. Launch it with:  forkmesh"
  say "  On a server with no display, forkmesh opens an interactive CLI."
  diag launch 1 "skipped"
elif launch_forkmesh; then
  if [ "$LAUNCH_MODE" = "daemon" ] || [ "$LAUNCH_MODE" = "service" ]; then
    say "Done — ForkMesh is running as a background daemon."
    say "  The node will join the network and appear in the Mirror nodes list shortly."
    if [ "$LAUNCH_MODE" = "service" ]; then
      say "  Runs unprivileged as forkmesh-node under systemd."
      say "  Logs: journalctl -u forkmesh-node.service"
      say "  Stop: systemctl stop forkmesh-node.service"
    else
      say "  Logs: $LOG_PATH    Stop: kill \"\$(cat '$PID_FILE')\""
    fi


    say ""
    say "FORKMESH LINK CODE: $FORKMESH_LINK_CODE"
    if [ -n "$FORKMESH_OWNER" ]; then
      say "  This links the new node to owner \"$FORKMESH_OWNER\"."
    fi
    say "  Enter this code in your ForkMesh desktop app to link the new node"
    say "  to your account (a popup opens during a Hosts-panel install; the"
    say "  code expires 30 minutes after the node registers)."






    if [ "$LAUNCH_MODE" = "daemon" ] && command -v tail >/dev/null 2>&1; then
      say ""
      say "--- Live node output (first few seconds) ---"

      _w=0
      while [ ! -s "$LOG_PATH" ] && [ "$_w" -lt 40 ]; do sleep 0.25; _w=$((_w+1)); done
      tail -n +1 -f "$LOG_PATH" 2>/dev/null &
      _tail_pid=$!
      sleep 15



      kill "$_tail_pid" 2>/dev/null || true
      wait "$_tail_pid" 2>/dev/null || true
      say "--- (live output continues in $LOG_PATH) ---"
    fi
  else
    say "Done — launching ForkMesh now. (Next time, just run:  forkmesh)"
  fi
  diag launch 1 "$LAUNCH_MODE"
else
  say "Done. Launch it with:  forkmesh"
  say "  No display detected — forkmesh opens an interactive CLI here."
  say "  Type 'help' once it starts. Headless nodes should run under a dedicated"
  say "  unprivileged account; this installer configures that automatically when run as root."
  diag launch 1 "manual"
fi




if [ -n "$FORKMESH_TUNNEL_HOSTNAME" ] && [ -n "${CLOUDFLARE_API_TOKEN:-}" ]; then
  provision_cloudflare_tunnel ||
    warn "Automatic tunnel provisioning did not complete; the node is installed and running without a direct HTTPS endpoint."
elif [ -n "$FORKMESH_TUNNEL_HOSTNAME" ]; then
  warn "FORKMESH_TUNNEL_HOSTNAME is set but CLOUDFLARE_API_TOKEN is not; skipping automatic tunnel provisioning."
fi


CURRENT_STEP="done"
diag done 1
