#!/usr/bin/env bash
# Ensure a Flutter + Android SDK + JDK toolchain is available on this host,
# installing whatever piece is missing under $HOME/.forkmesh/toolchain (no
# root required). Meant to be SOURCED, not executed, from
# .forkmesh/build-android.yml so the Android build doesn't depend on a CI
# node having Flutter pre-installed — mirrors how cloudflare_worker/
# pywrangler.sh self-installs its own deploy toolchain (workers-py, wrangler)
# instead of assuming the host has it, so deploys "do not depend on global
# tools". JDK/Android-SDK bootstrap logic is duplicated from
# .forkmesh/shortcuts/launch-flutter-android.sh (that script also creates an
# emulator/AVD for interactive `flutter run`, which a headless build doesn't
# need).
#
# Contract for callers. The caller normally runs under `set -e`, so this script
# never calls exit and never returns non-zero for a toolchain it cannot build —
# it reports through variables and lets the caller decide:
#
#   FLUTTER_TOOLCHAIN_READY   1 when a usable toolchain is set up, else 0
#   FLUTTER_TOOLCHAIN_STATUS  ready | offline | failed. "offline" is a policy
#                             fact about the host (no egress, nothing to fix in
#                             the tree) that a caller can treat as a skip;
#                             "failed" is a real breakage worth a red run.
#   FLUTTER_TOOLCHAIN_REASON  why it is not usable (empty when ready)
#   FLUTTER                   path to a working `flutter` (only when ready)
#   JAVA_HOME, ANDROID_HOME, ANDROID_SDK_ROOT, PATH  exported when ready
#
# Every missing piece is a download (apt archives, dl.google.com, github.com),
# and `flutter pub get` plus Gradle's dependency resolution need egress even
# when the toolchain is already installed. ForkMesh Actions run with the network
# namespace unshared unless the node sets FORKMESH_ACTIONS_ALLOW_NETWORK, so on
# a normal node NONE of that can work. The first version of this script found
# that out the hard way: `apt-get download` failed with its output redirected to
# /dev/null, and under the step's `set -e` the whole run died as a bare "exit
# code 100" one line after printing "Setting up JDK (OpenJDK 21)...". Probe for
# egress up front instead, and report a reason the run log can explain.

_fm_toolchain_dir="$HOME/.forkmesh/toolchain"

# Is there any egress? Inside an Actions sandbox with --unshare-net there is no
# route off loopback (and no /etc/resolv.conf), so both probes fail immediately;
# on a normal host at least one succeeds. DNS is tried first because that is
# what every downloader below actually needs, with a raw TCP connect as a second
# opinion for hosts that have egress but lock resolution down.
_fm_have_network() {
  if timeout 5 getent ahosts dl.google.com >/dev/null 2>&1; then
    return 0
  fi
  if timeout 5 bash -c 'exec 3<>/dev/tcp/1.1.1.1/443' >/dev/null 2>&1; then
    return 0
  fi
  return 1
}

# Locate a full JDK (javac, not just a JRE — Gradle needs the compiler). A
# system JDK counts: /usr is bind-mounted into the Actions sandbox, so one
# installed on the node is usable there without any download.
_fm_find_jdk() {
  local candidate javac
  for candidate in "${JAVA_HOME:-}" "$_fm_toolchain_dir/jdk"; do
    if [ -n "$candidate" ] && [ -x "$candidate/bin/javac" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  javac="$(command -v javac 2>/dev/null || true)"
  if [ -n "$javac" ]; then
    javac="$(readlink -f "$javac")"
    candidate="$(dirname "$(dirname "$javac")")"
    if [ -x "$candidate/bin/java" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  fi
  return 1
}

_fm_find_flutter() {
  local d
  if [ -n "${FLUTTER:-}" ] && [ -x "${FLUTTER:-}" ]; then
    printf '%s\n' "$FLUTTER"
    return 0
  fi
  d="$(command -v flutter 2>/dev/null || true)"
  if [ -n "$d" ]; then
    printf '%s\n' "$d"
    return 0
  fi
  for d in "$HOME/flutter/bin" "$HOME/development/flutter/bin" \
           "$HOME/snap/flutter/common/flutter/bin" /opt/flutter/bin \
           "$_fm_toolchain_dir/flutter/bin"; do
    if [ -x "$d/flutter" ]; then
      printf '%s\n' "$d/flutter"
      return 0
    fi
  done
  return 1
}

# --- JDK ---------------------------------------------------------------
_fm_install_jdk() {
  local build_dir="$_fm_toolchain_dir/_jdk_build" jvm_dir deb target candidate
  echo "Setting up JDK (OpenJDK 21)..." >&2
  rm -rf "$build_dir"
  mkdir -p "$_fm_toolchain_dir" "$build_dir" || return 1
  (
    cd "$build_dir" || exit 1
    # Failures here are reported, not swallowed: an unreachable archive used to
    # surface only as the step's exit status.
    apt-get download openjdk-21-jre-headless openjdk-21-jdk-headless || exit 1
    mkdir -p root
    for deb in *.deb; do dpkg -x "$deb" root || exit 1; done
    # Absolute symlinks inside the extracted tree point at paths that only exist
    # once the package is really installed; re-point the dangling ones at the
    # extracted copy.
    find root -type l | while read -r link; do
      target="$(readlink "$link")"
      case "$target" in
        /*)
          if [ ! -e "$target" ]; then
            candidate="root$target"
            if [ -e "$candidate" ]; then
              rm -f "$link"
              cp -a "$candidate" "$link"
            fi
          fi
          ;;
      esac
    done
    jvm_dir="$(find root/usr/lib/jvm -maxdepth 1 -mindepth 1 -type d ! -name '.' | head -n1)"
    [ -n "$jvm_dir" ] || { echo "no JDK found in the extracted packages" >&2; exit 1; }
    mv "$jvm_dir" "$_fm_toolchain_dir/jdk"
  ) || { rm -rf "$build_dir"; return 1; }
  rm -rf "$build_dir"

  # Generate cacerts from the system CA bundle: the bare dpkg-extracted JDK has
  # no trust store (ca-certificates-java normally builds one via a postinst
  # hook we can't run without root).
  local tmpdir f i=0
  tmpdir="$(mktemp -d)" || return 1
  awk -v dir="$tmpdir" '/BEGIN CERTIFICATE/{n++; file=sprintf("%s/cert-%03d.pem", dir, n)} {print > file}' \
    /etc/ssl/certs/ca-certificates.crt
  rm -f "$_fm_toolchain_dir/jdk/lib/security/cacerts"
  for f in "$tmpdir"/*.pem; do
    i=$((i+1))
    "$_fm_toolchain_dir/jdk/bin/keytool" -importcert -noprompt -trustcacerts \
      -alias "cert$i" -file "$f" \
      -keystore "$_fm_toolchain_dir/jdk/lib/security/cacerts" \
      -storepass changeit >/dev/null 2>&1 || true
  done
  rm -rf "$tmpdir"
  echo "✓ JDK installed" >&2
  return 0
}

# --- Android SDK cmdline-tools ------------------------------------------
_fm_install_cmdline_tools() {
  local sdk_root="$1" cli_zip attempt downloaded=false
  echo "Setting up Android SDK..." >&2
  mkdir -p "$sdk_root/cmdline-tools" || return 1

  # curl has been observed to hang indefinitely on some networks even when the
  # host is reachable; wget doesn't, so prefer it for this large download.
  cli_zip="$(mktemp -u /tmp/cmdline-tools.XXXXXX.zip)"
  for attempt in 1 2 3; do
    echo "  Downloading Android command-line tools (attempt $attempt)..." >&2
    if wget -q -O "$cli_zip" --tries=1 --timeout=90 \
      "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" \
      && [ -s "$cli_zip" ]; then
      downloaded=true
      break
    fi
    rm -f "$cli_zip"
    sleep 2
  done
  if [ "$downloaded" = false ]; then
    echo "Failed to download the Android command-line tools from dl.google.com." >&2
    return 1
  fi

  unzip -q "$cli_zip" -d "$sdk_root/cmdline-tools" || { rm -f "$cli_zip"; return 1; }
  rm -rf "$sdk_root/cmdline-tools/latest"
  mv "$sdk_root/cmdline-tools/cmdline-tools" "$sdk_root/cmdline-tools/latest" || return 1
  rm -f "$cli_zip"
  echo "✓ Android SDK command-line tools installed" >&2
  return 0
}

# --- Flutter -------------------------------------------------------------
_fm_install_flutter() {
  echo "Installing Flutter (stable channel)..." >&2
  rm -rf "$_fm_toolchain_dir/flutter"
  mkdir -p "$_fm_toolchain_dir" || return 1
  git clone --depth 1 -b stable https://github.com/flutter/flutter.git \
    "$_fm_toolchain_dir/flutter" >&2 || return 1
  echo "✓ Flutter installed" >&2
  return 0
}

_fm_toolchain_setup() {
  local jdk_dir sdk_root flutter_bin missing="" sdkmanager

  jdk_dir="$(_fm_find_jdk || true)"
  flutter_bin="$(_fm_find_flutter || true)"
  sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"

  [ -n "$jdk_dir" ] || missing="$missing a JDK,"
  [ -x "$sdk_root/cmdline-tools/latest/bin/sdkmanager" ] ||
    missing="$missing the Android SDK command-line tools,"
  [ -n "$flutter_bin" ] || missing="$missing Flutter,"
  missing="${missing%,}"

  # Bail out before touching the network when there isn't any. Note this gates
  # a complete toolchain too: `flutter pub get` and Gradle both resolve
  # dependencies over the network, so an offline build cannot succeed either
  # way — better to say so in one line than to fail minutes later inside Gradle.
  if ! _fm_have_network; then
    FLUTTER_TOOLCHAIN_STATUS="offline"
    if [ -n "$missing" ]; then
      FLUTTER_TOOLCHAIN_REASON="this run has no network egress, and these would have to be downloaded first:${missing}"
    else
      FLUTTER_TOOLCHAIN_REASON="this run has no network egress, which \`flutter pub get\` and Gradle dependency resolution both require"
    fi
    return 1
  fi

  if [ -z "$jdk_dir" ]; then
    if ! _fm_install_jdk; then
      FLUTTER_TOOLCHAIN_REASON="the OpenJDK 21 packages could not be downloaded and unpacked"
      return 1
    fi
    jdk_dir="$_fm_toolchain_dir/jdk"
  fi
  export JAVA_HOME="$jdk_dir" PATH="$jdk_dir/bin:$PATH"
  # The cacerts keystore built above isn't the JDK's hardcoded default trust
  # store, so the JVM needs to be told its password explicitly or every HTTPS
  # connection (sdkmanager, and critically Gradle's own SDK/NDK auto-download)
  # fails with "the trustAnchors parameter must be non-empty".
  export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Djavax.net.ssl.trustStorePassword=changeit"

  if [ ! -x "$sdk_root/cmdline-tools/latest/bin/sdkmanager" ]; then
    if ! _fm_install_cmdline_tools "$sdk_root"; then
      FLUTTER_TOOLCHAIN_REASON="the Android SDK command-line tools could not be installed"
      return 1
    fi
  fi
  export ANDROID_HOME="$sdk_root" ANDROID_SDK_ROOT="$sdk_root"
  sdkmanager="$sdk_root/cmdline-tools/latest/bin/sdkmanager"

  echo "Accepting Android SDK licenses..." >&2
  yes | "$sdkmanager" --licenses >/dev/null 2>&1 || true

  # Only platform-tools is installed up front; the Android Gradle Plugin
  # auto-downloads whatever compileSdk/build-tools/NDK version Flutter's own
  # Gradle plugin asks for during `flutter build apk`, now that licenses are
  # accepted and cmdline-tools is on ANDROID_SDK_ROOT.
  echo "Installing Android SDK platform-tools..." >&2
  "$sdkmanager" --install "platform-tools" 2>&1 | grep -v "^Warning:" || true

  if [ -z "$flutter_bin" ]; then
    if ! _fm_install_flutter; then
      FLUTTER_TOOLCHAIN_REASON="the Flutter SDK could not be cloned from github.com"
      return 1
    fi
    flutter_bin="$_fm_toolchain_dir/flutter/bin/flutter"
  fi

  FLUTTER="$flutter_bin"
  export PATH="$(dirname "$FLUTTER"):$PATH"
  export FLUTTER
  "$FLUTTER" config --no-analytics >/dev/null 2>&1 || true
  if ! "$FLUTTER" config --android-sdk "$sdk_root" >/dev/null; then
    FLUTTER_TOOLCHAIN_REASON="\`flutter config\` failed, so the Flutter SDK at $FLUTTER is not usable"
    return 1
  fi
  return 0
}

FLUTTER_TOOLCHAIN_READY=0
FLUTTER_TOOLCHAIN_STATUS="failed"
FLUTTER_TOOLCHAIN_REASON=""
if _fm_toolchain_setup; then
  FLUTTER_TOOLCHAIN_READY=1
  FLUTTER_TOOLCHAIN_STATUS="ready"
  FLUTTER_TOOLCHAIN_REASON=""
elif [ -z "$FLUTTER_TOOLCHAIN_REASON" ]; then
  FLUTTER_TOOLCHAIN_REASON="the Flutter/Android toolchain could not be set up"
fi

unset -f _fm_have_network _fm_find_jdk _fm_find_flutter _fm_install_jdk \
         _fm_install_cmdline_tools _fm_install_flutter _fm_toolchain_setup
unset _fm_toolchain_dir
