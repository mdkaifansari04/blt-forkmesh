#!/usr/bin/env bash
# ForkMesh desktop client installer.
#   curl -fsSL https://forkmesh.com/install.sh | bash
# Clones the repository, builds the Qt client, and installs it to ~/.local/bin.
# Missing build prerequisites are installed automatically when a supported
# package manager is detected. Set FORKMESH_NO_INSTALL_DEPS=1 to opt out.
set -euo pipefail

# Installer script version. Bump on every change to install.sh so a user can
# confirm — from the banner printed at startup — that they are running the
# freshly deployed script and not a cached/older copy from the CDN edge.
INSTALLER_VERSION="0.8.0 (2026-06-23)"

# ForkMesh is self-hosted: the same server that serves this script also serves
# the source over git's smart-HTTP protocol at https://<host>/<node>/<repo>.
# Override FORKMESH_HOST when self-hosting. By default, the installer asks the
# mainnode for the currently-online forkmesh host with the most recent uptime.
FORKMESH_HOST="${FORKMESH_HOST:-https://forkmesh.com}"
FORKMESH_NODE="${FORKMESH_NODE:-}"
FORKMESH_NAME="${FORKMESH_NAME:-forkmesh}"
FORKMESH_INSTALL_SOURCE_URL="${FORKMESH_INSTALL_SOURCE_URL:-${FORKMESH_HOST%/}/api/install-source}"
FORKMESH_DIAG_URL="${FORKMESH_DIAG_URL:-${FORKMESH_HOST%/}/api/install-diag}"
REPO="${FORKMESH_REPO:-}"
SRC="${FORKMESH_DIR:-$HOME/.local/share/forkmesh/src}"
BIN_DIR="${FORKMESH_BIN_DIR:-$HOME/.local/bin}"
BIN="$BIN_DIR/forkmesh"

say()  { printf '\033[32m==>\033[0m %s\n' "$1"; }
warn() { printf '\033[33mWarning:\033[0m %s\n' "$1" >&2; }
die()  { printf '\033[31mError:\033[0m %s\n' "$1" >&2; exit 1; }

# --- anonymous diagnostics --------------------------------------------------
# Report each install step to the mainnode so operators can see, in aggregate,
# where installs succeed or fail (find-a-mirror, prerequisites, clone, build,
# install, first launch). This is ANONYMOUS: RUN_ID is a fresh random id minted
# for this run only — it is never tied to your account, email, or IP address,
# and the server does not record the requesting IP. Only coarse platform facts
# (OS, CPU arch, package manager, distro id, installer version) are sent. Opt
# out entirely with FORKMESH_NO_DIAG=1.
RUN_ID="$( (head -c 16 /dev/urandom 2>/dev/null | od -An -tx1 2>/dev/null | tr -d ' \n') || true )"
[ -n "$RUN_ID" ] || RUN_ID="$$-$(date +%s 2>/dev/null || echo 0)"
DIAG_OS="$(uname -s 2>/dev/null || echo unknown)"
DIAG_ARCH="$(uname -m 2>/dev/null || echo unknown)"
DIAG_DISTRO=""
if [ -r /etc/os-release ]; then
  DIAG_DISTRO="$( ( . /etc/os-release 2>/dev/null; printf '%s' "${ID:-}" ) || true )"
fi
# Logical prerequisites we had to install (empty = the machine already had them).
DIAG_MISSING=""
# The install phase currently in progress; the EXIT trap reports it as failed if
# the script dies, so the funnel shows exactly where an install dropped off.
CURRENT_STEP="start"

diag() {
  [ "${FORKMESH_NO_DIAG:-0}" = "1" ] && return 0
  command -v curl >/dev/null 2>&1 || return 0
  local step="$1" ok="$2" detail="${3:-}"
  # Fire-and-forget in the background with a short timeout: diagnostics must
  # never slow down or fail the install, so every error is swallowed.
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
  [ -n "$FORKMESH_NODE" ] && return 0
  command -v curl >/dev/null 2>&1 || die "curl is required to find an online ForkMesh mirror."

  local body node source_url sep
  sep="?"
  case "$FORKMESH_INSTALL_SOURCE_URL" in
    *\?*) sep="&" ;;
  esac
  source_url="${FORKMESH_INSTALL_SOURCE_URL}${sep}_=$(date +%s)"
  if ! body="$(curl -sSL -H 'Cache-Control: no-cache' -H 'Pragma: no-cache' "$source_url")"; then
    die "Could not check for an online ForkMesh mirror. Please try again shortly."
  fi
  node="$(printf '%s\n' "$body" | sed -n 's/.*"node"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
  case "$node" in
    *[!A-Za-z0-9._:-]*|"")
      die "No online ForkMesh node is currently mirroring '$FORKMESH_NAME'. Start a node that publishes this repository, then try the installer again."
      ;;
  esac
  FORKMESH_NODE="$node"
}

printf '\033[32m╭───────────────────────────────────────────────╮\033[0m\n'
printf '\033[32m│\033[0m  ForkMesh installer  \033[2mv%-24s\033[0m\033[32m│\033[0m\n' "$INSTALLER_VERSION"
printf '\033[32m╰───────────────────────────────────────────────╯\033[0m\n'
say "Host:   $FORKMESH_HOST"
say "Source: ${SRC}"
say "Target: ${BIN}"
if [ "$(id -u)" -eq 0 ]; then
  say "Privileges: running as root (no sudo needed)"
else
  say "Privileges: non-root; will use sudo/doas for package installs"
fi
diag start 1

if [ -z "$REPO" ]; then
  CURRENT_STEP="mirror"
  say "Resolving an online ForkMesh mirror to clone from…"
  resolve_install_node
  REPO="${FORKMESH_HOST%/}/${FORKMESH_NODE}/${FORKMESH_NAME}"
  say "Using mirror node: $FORKMESH_NODE"
  diag mirror 1
fi

# --- privilege escalation ---------------------------------------------------
# Resolve how to run a package manager that needs root. Empty when we are
# already root; otherwise prefer sudo, then doas.
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

# --- package manager detection ----------------------------------------------
# Sets PM to the detected manager and PM_INSTALL to the command (as an array)
# that installs packages non-interactively.
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

# Refresh package indexes once, for managers that need it before install.
pm_refresh() {
  [ "$PM_UPDATED" -eq 1 ] && return 0
  PM_UPDATED=1
  case "$PM" in
    apt)    run_pm apt-get update ;;
    pacman) run_pm pacman -Sy ;;
  esac
}

# Run a package-manager command with escalation when required (brew must not
# run as root). Echoes the exact command (including any sudo/doas prefix) so the
# install log shows precisely what is being executed and with what privileges.
run_pm() {
  if [ "$PM" = "brew" ]; then
    say "Running: $*"
    "$@"
  else
    say "Running: ${SUDO:+$SUDO }$*"
    $SUDO "$@"
  fi
}

# Install one or more packages by name.
pm_install() {
  # Resolve privilege escalation before touching the package manager. Every
  # install path funnels through here, so this guarantees $SUDO is set even when
  # the caller didn't go through ensure() (e.g. the Qt/OpenSSL block below, which
  # runs unconditionally). Without this, apt-get runs unprivileged and fails with
  # "Could not open lock file ... Permission denied" on a fresh machine.
  if [ "$PM" != "brew" ] && ! need_sudo; then
    die "Installing packages via $PM needs root, but neither sudo nor doas is available. Re-run as root, or install the build dependencies manually and re-run with FORKMESH_NO_INSTALL_DEPS=1."
  fi
  pm_refresh
  run_pm "${PM_INSTALL[@]}" "$@"
}

# --- prerequisites ----------------------------------------------------------
# Map a logical prerequisite to the package providing it for the detected PM.
# Echoes the package name, or nothing when there is no candidate to install.
pkg_for() {
  local what="$1"
  case "$PM:$what" in
    apt:git)        echo git ;;
    apt:cmake)      echo cmake ;;
    apt:compiler)   echo "g++" ;;
    apt:qt)         echo "qt6-base-dev qt6-svg-dev" ;;
    apt:openssl)    echo libssl-dev ;;

    dnf:git|yum:git)            echo git ;;
    dnf:cmake|yum:cmake)        echo cmake ;;
    dnf:compiler|yum:compiler)  echo "gcc-c++" ;;
    dnf:qt|yum:qt)              echo "qt6-qtbase-devel qt6-qtsvg-devel" ;;
    dnf:openssl|yum:openssl)    echo openssl-devel ;;

    pacman:git)       echo git ;;
    pacman:cmake)     echo cmake ;;
    pacman:compiler)  echo gcc ;;
    pacman:qt)        echo "qt6-base qt6-svg" ;;
    pacman:openssl)   echo openssl ;;

    zypper:git)       echo git ;;
    zypper:cmake)     echo cmake ;;
    zypper:compiler)  echo "gcc-c++" ;;
    zypper:qt)        echo "qt6-base-devel qt6-svg-devel" ;;
    zypper:openssl)   echo libopenssl-devel ;;

    apk:git)       echo git ;;
    apk:cmake)     echo "cmake make" ;;
    apk:compiler)  echo "g++" ;;
    apk:qt)        echo "qt6-qtbase-dev qt6-qtsvg-dev" ;;
    apk:openssl)   echo "openssl-dev" ;;

    brew:git)       echo git ;;
    brew:cmake)     echo cmake ;;
    brew:qt)        echo qt ;;
    brew:openssl)   echo "openssl@3" ;;
    brew:compiler)  echo "" ;;  # provided by Xcode CLT, handled separately
  esac
}

# Ensure a prerequisite is present, installing it if missing and possible.
# Args: <logical name> <test command...>
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
  # shellcheck disable=SC2086
  pm_install $pkgs || die "Failed to install $pkgs via $PM."

  "$@" >/dev/null 2>&1 || die "Installed $pkgs but '$what' is still unavailable."
}

# A C++ compiler can come from any of cc/clang/g++ (or Xcode CLT on macOS).
have_compiler() {
  command -v cc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1 \
    || command -v g++ >/dev/null 2>&1 || command -v c++ >/dev/null 2>&1
}

if detect_pm; then
  say "Detected package manager: $PM (${PM_INSTALL[*]})"
else
  warn "No supported package manager found; missing tools cannot be auto-installed."
fi

CURRENT_STEP="deps"
say "Checking build prerequisites (git, cmake, compiler, Qt 6, OpenSSL)…"
ensure git    command -v git
ensure cmake  command -v cmake

# Compiler: on macOS this means the Xcode Command Line Tools, which brew can't
# install — trigger Apple's installer instead.
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

# Qt 6 (Widgets, Network, Svg) and OpenSSL are required by CMake. Install the
# dev packages up front. This is a hard requirement: if the package manager
# can't provide them the build is guaranteed to fail at configure time with a
# confusing "Could NOT find Qt6Svg" error, so fail here with a clear message
# instead. The qt6 dev metapackages pull in the Svg/SvgWidgets components.
if [ -n "$PM" ] && [ "${FORKMESH_NO_INSTALL_DEPS:-0}" != "1" ]; then
  qt_pkgs="$(pkg_for qt)"
  if [ -n "$qt_pkgs" ]; then
    say "Installing Qt 6 dev libraries ($qt_pkgs)"
    # shellcheck disable=SC2086
    pm_install $qt_pkgs || die "Failed to install Qt 6 dev packages ($qt_pkgs) via $PM."
  fi
  ssl_pkgs="$(pkg_for openssl)"
  if [ -n "$ssl_pkgs" ]; then
    say "Installing OpenSSL dev libraries ($ssl_pkgs)"
    # shellcheck disable=SC2086
    pm_install $ssl_pkgs || die "Failed to install OpenSSL dev packages ($ssl_pkgs) via $PM."
  fi
else
  say "ForkMesh requires Qt 6 (Widgets, Network, Svg) and OpenSSL."
  say "  Debian/Ubuntu: sudo apt install qt6-base-dev qt6-svg-dev libssl-dev cmake g++"
  say "  Fedora:        sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel openssl-devel cmake gcc-c++"
  say "  macOS:         brew install qt openssl@3 cmake"
fi
# detail records which prerequisites had to be installed ("none" = all present),
# so the funnel shows how often a machine already met the requirements.
diag deps 1 "${DIAG_MISSING:-none}"

# --- fetch + build + install (clean-reclone retry on any failure) -----------
# A leftover checkout from an interrupted earlier run can be stale or incomplete
# (e.g. the directory exists but qt_client/CMakeLists.txt is missing), which
# breaks the build in confusing ways. Each phase below returns non-zero instead
# of aborting, so on ANY failure we can wipe the source tree and run the whole
# pipeline once more from a clean clone.

clean_clone() {
  rm -rf "$SRC"
  say "Cloning $REPO"
  git clone --depth 1 "$REPO" "$SRC"
}

fetch_source() {
  mkdir -p "$(dirname "$SRC")" || return 1
  if [ -d "$SRC/.git" ]; then
    say "Updating existing checkout in $SRC"
    # The stored remote was baked with the node id live at the original install.
    # That node may now be offline while a different mirror is online, so pulling
    # the old URL returns 503. Repoint origin at the freshly resolved live mirror
    # before pulling, and fall back to a clean re-clone if the pull still fails.
    git -C "$SRC" remote set-url origin "$REPO" 2>/dev/null || true
    if ! git -C "$SRC" pull --ff-only; then
      warn "Could not update from $REPO; re-cloning from the current live mirror."
      clean_clone || return 1
    fi
  else
    clean_clone || return 1
  fi
  # git pull can say "Already up to date" yet leave a tree missing the Qt sources
  # CMake builds from (stale/partial mirror). Success of the fetch is not proof
  # the build inputs exist, so verify the actual file and re-clone if it is gone.
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
  BUILD="$build_dir"  # used by the install/launch phases below
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

# Fetch -> build -> install, tagging the active phase for diagnostics. Returns
# non-zero (rather than exiting) on the first failure so the caller can retry.
attempt_install() {
  CURRENT_STEP="fetch";   fetch_source   || return 1; diag fetch 1
  CURRENT_STEP="build";   build_client   || return 1; diag build 1
  CURRENT_STEP="install"; install_client || return 1; diag install 1
}

if ! attempt_install; then
  warn "Install failed; removing $SRC and retrying once from a clean clone."
  rm -rf "$SRC"
  attempt_install \
    || die "Install failed again after a clean re-clone; see the messages above for the cause."
fi

case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *) say "Add $BIN_DIR to your PATH, e.g.  export PATH=\"$BIN_DIR:\$PATH\"" ;;
esac

# --- desktop integration (Linux) --------------------------------------------
# Register a .desktop launcher + hicolor icons so ForkMesh appears in the
# GNOME/KDE app menu and dock — not just on the PATH. This reuses the dedicated
# qt_client/install.sh that ships in the cloned source, so the launcher entry
# stays in one place and points at the build output (picking up in-app updates).
# Best-effort and Linux-only: macOS gets its menu entry from the .app bundle,
# and a missing icon-cache tool must never fail the whole install.
register_desktop_entry() {
  [ "$(uname -s)" = "Linux" ] || return 0
  local script="$SRC/qt_client/install.sh"
  if [ ! -f "$script" ]; then
    warn "Desktop integration script not found at $script; skipping menu registration."
    return 0
  fi
  say "Registering ForkMesh in the application menu"
  # Keep the helper's verbose stdout out of the installer log, but let any
  # errors through. It won't rebuild (the binary already exists) and a non-zero
  # exit here is non-fatal — the app still runs from $BIN / the build output.
  if bash "$script" >/dev/null; then
    say "Added to the application menu — search \"ForkMesh\" in Activities/the app grid."
  else
    warn "Could not register the desktop menu entry; ForkMesh still runs via:  forkmesh"
  fi
}
register_desktop_entry

# --- launch -----------------------------------------------------------------
# One-shot install: start ForkMesh automatically so the user lands in the app.
# Detached from this script (which may itself be running under `curl | bash`) so
# it keeps running after the installer exits. Set FORKMESH_NO_LAUNCH=1 to skip
# (e.g. headless build servers). On Linux we only auto-launch when a display is
# present; a headless box gets the manual hint instead.
launch_forkmesh() {
  case "$(uname -s)" in
    Darwin)
      if [ -d "$BUILD/ForkMesh.app" ]; then
        open "$BUILD/ForkMesh.app" && return 0
      fi
      open "$BIN" 2>/dev/null && return 0
      ;;
    *)
      [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] || return 1
      if command -v setsid >/dev/null 2>&1; then
        setsid "$BIN" >/dev/null 2>&1 < /dev/null &
      else
        nohup "$BIN" >/dev/null 2>&1 < /dev/null &
      fi
      return 0
      ;;
  esac
  return 1
}

CURRENT_STEP="launch"
if [ "${FORKMESH_NO_LAUNCH:-0}" = "1" ]; then
  say "Done. Launch it with:  forkmesh"
  diag launch 1 "skipped"
elif launch_forkmesh; then
  say "Done — launching ForkMesh now. (Next time, just run:  forkmesh)"
  diag launch 1 "launched"
else
  say "Done. Launch it with:  forkmesh"
  diag launch 1 "manual"
fi

# Whole install finished successfully; the EXIT trap only fires on failure.
CURRENT_STEP="done"
diag done 1
