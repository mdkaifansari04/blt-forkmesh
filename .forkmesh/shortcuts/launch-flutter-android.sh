#!/usr/bin/env bash
# name: Launch Flutter app (Android)
# description: Install everything required and build/run the ForkMesh Flutter app on Android.
set -euo pipefail

readonly TOOLCHAIN_DIR="$HOME/.forkmesh/toolchain"
readonly JAVA_HOME="$TOOLCHAIN_DIR/jdk"
readonly ANDROID_SDK_ROOT="$HOME/Android/Sdk"
readonly AVD_NAME="forkmesh"
readonly AVD_SYSTEM_IMAGE="system-images;android-34;google_apis;x86_64"

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
  # Generate cacerts from the system CA bundle: the bare dpkg-extracted JDK has
  # no trust store (ca-certificates-java normally builds one via a postinst
  # hook we can't run without root).
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
# The cacerts keystore above isn't the JDK's hardcoded default trust store, so
# the JVM needs to be told its password explicitly or every HTTPS connection
# (sdkmanager, and critically Gradle's own SDK/NDK auto-download) fails with
# "the trustAnchors parameter must be non-empty".
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Djavax.net.ssl.trustStorePassword=changeit"

# Ensure Android SDK cmdline-tools are installed
if [ ! -x "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" ]; then
  echo "Setting up Android SDK..." >&2
  mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"

  # curl has been observed to hang indefinitely on some networks even when the
  # host is reachable; wget doesn't, so prefer it for this large download.
  cli_zip="$(mktemp -u /tmp/cmdline-tools.XXXXXX.zip)"
  downloaded=false
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
    echo "Check network access and re-run this shortcut." >&2
    exit 1
  fi

  unzip -q "$cli_zip" -d "$ANDROID_SDK_ROOT/cmdline-tools"
  rm -rf "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  mv "$ANDROID_SDK_ROOT/cmdline-tools/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  rm -f "$cli_zip"
  echo "✓ Android SDK command-line tools installed" >&2
fi

export ANDROID_HOME="$ANDROID_SDK_ROOT" ANDROID_SDK_ROOT
SDKMANAGER="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
AVDMANAGER="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/avdmanager"

echo "Accepting Android SDK licenses..." >&2
yes | "$SDKMANAGER" --licenses >/dev/null 2>&1 || true

echo "Installing Android SDK components..." >&2
"$SDKMANAGER" --install \
  "platform-tools" \
  "platforms;android-36" \
  "build-tools;36.0.0" \
  "emulator" \
  "$AVD_SYSTEM_IMAGE" \
  2>&1 | grep -v "^Warning:" || true
echo "✓ SDK components installed" >&2

# Create a default AVD if no emulator exists yet, so the run below always has
# a device to target without requiring the user to set one up by hand.
if ! "$AVDMANAGER" list avd 2>/dev/null | grep -q "Name: $AVD_NAME"; then
  echo "Creating Android emulator '$AVD_NAME'..." >&2
  echo "no" | "$AVDMANAGER" create avd -n "$AVD_NAME" -k "$AVD_SYSTEM_IMAGE" --force >/dev/null
  echo "✓ Emulator created" >&2
fi

# Resolve the flutter binary: PATH first, then the common ~/flutter SDK checkout.
FLUTTER="$(command -v flutter || true)"
if [ -z "$FLUTTER" ] && [ -x "$HOME/flutter/bin/flutter" ]; then
  FLUTTER="$HOME/flutter/bin/flutter"
fi
if [ -z "$FLUTTER" ]; then
  echo "flutter not found: install it on PATH or at ~/flutter" >&2
  exit 1
fi

"$FLUTTER" config --android-sdk "$ANDROID_SDK_ROOT" >/dev/null

# The script lives in <repo>/.forkmesh/shortcuts/, so the app is two levels up.
cd "$(dirname "$0")/../../flutter_app"

echo "Resolving dependencies..." >&2
"$FLUTTER" pub get

# Find an already-attached Android device/emulator, matching on the
# target-platform column (a bare "-d android" selector no longer resolves
# device ids like "emulator-5554" in current Flutter).
android_device_id() {
  "$FLUTTER" devices 2>/dev/null | awk -F' • ' '$3 ~ /^android/ {print $2; exit}'
}

device_id="$(android_device_id)"

if [ -z "$device_id" ]; then
  echo "No Android device attached. Starting the '$AVD_NAME' emulator..." >&2
  "$FLUTTER" emulators --launch "$AVD_NAME"

  echo "Waiting for the emulator to be ready..." >&2
  for _ in $(seq 1 90); do
    device_id="$(android_device_id)"
    [ -n "$device_id" ] && break
    sleep 2
  done

  if [ -z "$device_id" ]; then
    echo "Emulator failed to come online. Try launching it manually:" >&2
    echo "  flutter emulators --launch $AVD_NAME" >&2
    exit 1
  fi
fi

echo "Launching app on $device_id..." >&2
exec "$FLUTTER" run -d "$device_id"
