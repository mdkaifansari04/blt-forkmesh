#!/usr/bin/env bash
# ForkMesh desktop client installer.
#   curl -fsSL https://forkmesh.com/install.sh | bash
# Clones the repository, builds the Qt client, and installs it to ~/.local/bin.
# Missing build prerequisites are installed automatically when a supported
# package manager is detected. Set FORKMESH_NO_INSTALL_DEPS=1 to opt out.
set -euo pipefail

# ForkMesh is self-hosted: the same server that serves this script also serves
# the source over git's smart-HTTP protocol at https://<host>/<node>/<repo>.
# Override FORKMESH_HOST when self-hosting. By default, the installer asks the
# mainnode for the currently-online forkmesh host with the most recent uptime.
FORKMESH_HOST="${FORKMESH_HOST:-https://forkmesh.com}"
FORKMESH_NODE="${FORKMESH_NODE:-}"
FORKMESH_NAME="${FORKMESH_NAME:-forkmesh}"
FORKMESH_INSTALL_SOURCE_URL="${FORKMESH_INSTALL_SOURCE_URL:-${FORKMESH_HOST%/}/api/install-source}"
REPO="${FORKMESH_REPO:-}"
SRC="${FORKMESH_DIR:-$HOME/.local/share/forkmesh/src}"
BIN_DIR="${FORKMESH_BIN_DIR:-$HOME/.local/bin}"
BIN="$BIN_DIR/forkmesh"

say()  { printf '\033[32m==>\033[0m %s\n' "$1"; }
warn() { printf '\033[33mWarning:\033[0m %s\n' "$1" >&2; }
die()  { printf '\033[31mError:\033[0m %s\n' "$1" >&2; exit 1; }

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

if [ -z "$REPO" ]; then
  resolve_install_node
  REPO="${FORKMESH_HOST%/}/${FORKMESH_NODE}/${FORKMESH_NAME}"
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
# run as root).
run_pm() {
  if [ "$PM" = "brew" ]; then
    "$@"
  else
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
  # shellcheck disable=SC2086
  pm_install $pkgs || die "Failed to install $pkgs via $PM."

  "$@" >/dev/null 2>&1 || die "Installed $pkgs but '$what' is still unavailable."
}

# A C++ compiler can come from any of cc/clang/g++ (or Xcode CLT on macOS).
have_compiler() {
  command -v cc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1 \
    || command -v g++ >/dev/null 2>&1 || command -v c++ >/dev/null 2>&1
}

detect_pm || warn "No supported package manager found; missing tools cannot be auto-installed."

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

# --- fetch / update ---------------------------------------------------------
mkdir -p "$(dirname "$SRC")"
if [ -d "$SRC/.git" ]; then
  say "Updating existing checkout in $SRC"
  # The stored remote was baked with the node id that was live at the original
  # install. That node may now be offline (its hosted tunnel gone) while a
  # different mirror is online, so pulling from the old URL returns 503. Repoint
  # origin at the freshly resolved live mirror before pulling, and fall back to a
  # clean re-clone if the fast-forward pull still cannot reach a host.
  git -C "$SRC" remote set-url origin "$REPO" 2>/dev/null || true
  if ! git -C "$SRC" pull --ff-only; then
    warn "Could not update from $REPO; re-cloning from the current live mirror."
    rm -rf "$SRC"
    git clone --depth 1 "$REPO" "$SRC"
  fi
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
