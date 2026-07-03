#!/usr/bin/env bash
# name: Launch Flutter app (Android)
# description: Install everything required and build/run the ForkMesh Flutter app on Android.
set -euo pipefail

readonly TOOLCHAIN_DIR="$HOME/.forkmesh/toolchain"
readonly JAVA_HOME="$TOOLCHAIN_DIR/jdk"
readonly ANDROID_SDK_ROOT="$HOME/Android/Sdk"

# Ensure JDK is installed
if [ ! -x "$JAVA_HOME/bin/java" ]; then
  echo "Setting up JDK (OpenJDK 21)..." >&2
  mkdir -p "$TOOLCHAIN_DIR" "$TOOLCHAIN_DIR/_jdk_build"
  cd "$TOOLCHAIN_DIR/_jdk_build"
  apt-get download openjdk-21-jre-headless openjdk-21-jdk-headless >/dev/null 2>&1
  mkdir -p root
  for deb in *.deb; do dpkg -x "$deb" root; done
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
  mv "$jvm_dir" "$JAVA_HOME"
  rm -rf "$TOOLCHAIN_DIR/_jdk_build"
  # Generate cacerts from system CA bundle
  tmpdir=$(mktemp -d)
  awk -v dir="$tmpdir" '/BEGIN CERTIFICATE/{n++; file=sprintf("%s/cert-%03d.pem", dir, n)} {print > file}' /etc/ssl/certs/ca-certificates.crt
  rm -f "$JAVA_HOME/lib/security/cacerts"
  i=0
  for f in "$tmpdir"/*.pem; do
    i=$((i+1))
    "$JAVA_HOME/bin/keytool" -importcert -noprompt -trustcacerts -alias "cert$i" -file "$f" \
      -keystore "$JAVA_HOME/lib/security/cacerts" -storepass changeit >/dev/null 2>&1 || true
  done
  rm -rf "$tmpdir"
  echo "✓ JDK installed" >&2
fi

export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"

# Ensure Android SDK is installed
if [ ! -d "$ANDROID_SDK_ROOT" ]; then
  echo "Setting up Android SDK..." >&2
  mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"

  # Download with retries and fallback
  cli_zip="/tmp/cmdline-tools.zip"
  rm -f "$cli_zip"
  downloaded=false
  for attempt in 1 2 3; do
    echo "  Downloading Android CLI tools (attempt $attempt)..." >&2
    if timeout 90 curl -fsSL --max-time 60 --retry 1 \
      -o "$cli_zip" \
      "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" 2>/dev/null; then
      if [ -f "$cli_zip" ] && [ -s "$cli_zip" ]; then
        downloaded=true
        break
      fi
    fi
    [ "$attempt" -lt 3 ] && sleep 2
  done

  if [ "$downloaded" = false ]; then
    echo "Failed to download Android SDK from dl.google.com." >&2
    echo "" >&2
    echo "The Android command-line tools are required. Options:" >&2
    echo "  1. Ensure network access to https://dl.google.com/" >&2
    echo "  2. Configure proxy if behind a corporate firewall:" >&2
    echo "     export http_proxy=http://proxy:port" >&2
    echo "     export https_proxy=http://proxy:port" >&2
    echo "  3. Or manually download and extract to: $ANDROID_SDK_ROOT/" >&2
    echo "     https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" >&2
    exit 1
  fi

  unzip -q "$cli_zip" -d "$ANDROID_SDK_ROOT/cmdline-tools"
  mv "$ANDROID_SDK_ROOT/cmdline-tools/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  rm -f "$cli_zip"
  echo "✓ Android SDK downloaded" >&2
fi

# Install required Android SDK components
if [ -x "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" ]; then
  echo "Installing Android SDK components..." >&2
  ANDROID_HOME="$ANDROID_SDK_ROOT"
  export ANDROID_HOME ANDROID_SDK_ROOT

  "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" --install \
    "platform-tools" \
    "platforms;android-36" \
    "build-tools;36.0.0" \
    "emulator" \
    2>&1 | grep -v "^Warning:" || true
  echo "✓ SDK components installed" >&2
fi

export ANDROID_HOME="$ANDROID_SDK_ROOT" ANDROID_SDK_ROOT

# Resolve the flutter binary: PATH first, then the common ~/flutter SDK checkout.
FLUTTER="$(command -v flutter || true)"
if [ -z "$FLUTTER" ] && [ -x "$HOME/flutter/bin/flutter" ]; then
  FLUTTER="$HOME/flutter/bin/flutter"
fi
if [ -z "$FLUTTER" ]; then
  echo "flutter not found: install it on PATH or at ~/flutter" >&2
  exit 1
fi

# The script lives in <repo>/.forkmesh/shortcuts/, so the app is two levels up.
cd "$(dirname "$0")/../../flutter_app"

echo "Resolving dependencies..." >&2
"$FLUTTER" pub get

# Check for connected Android devices/emulators
devices_output="$("$FLUTTER" devices 2>&1)"
if echo "$devices_output" | grep -qi android; then
  echo "Android device/emulator found, launching app..." >&2
  exec "$FLUTTER" run -d android
else
  # No device attached; try to launch an emulator
  echo "No Android device attached. Checking available emulators..." >&2
  emulators="$("$FLUTTER" emulators 2>&1 | grep "^[a-zA-Z]" || true)"

  if [ -z "$emulators" ]; then
    echo "No Android device or emulator detected." >&2
    "$FLUTTER" devices >&2
    echo "" >&2
    echo "To proceed, either:" >&2
    echo "  1. Plug in an Android device" >&2
    echo "  2. Create and launch an emulator via:" >&2
    echo "     flutter emulators" >&2
    exit 1
  fi

  # Launch the first available emulator
  emu_id="$(echo "$emulators" | head -1 | awk '{print $1}')"
  echo "Launching emulator: $emu_id" >&2
  "$FLUTTER" emulators --launch "$emu_id"

  # Wait for emulator to come online
  echo "Waiting for emulator to be ready..." >&2
  for i in {1..60}; do
    if "$FLUTTER" devices 2>&1 | grep -qi "android.*device"; then
      echo "Emulator ready, launching app..." >&2
      exec "$FLUTTER" run -d android
    fi
    sleep 2
  done

  echo "Emulator failed to come online. Try launching manually:" >&2
  echo "  flutter emulators --launch $emu_id" >&2
  exit 1
fi
