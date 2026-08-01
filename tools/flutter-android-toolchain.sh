#!/usr/bin/env bash














set -euo pipefail

readonly TOOLCHAIN_DIR="$HOME/.forkmesh/toolchain"
readonly JAVA_HOME_DIR="$TOOLCHAIN_DIR/jdk"
readonly ANDROID_SDK_ROOT_DIR="$HOME/Android/Sdk"
readonly FLUTTER_DIR="$TOOLCHAIN_DIR/flutter"


if [ ! -x "$JAVA_HOME_DIR/bin/java" ]; then
  echo "Setting up JDK (OpenJDK 21)..." >&2
  mkdir -p "$TOOLCHAIN_DIR" "$TOOLCHAIN_DIR/_jdk_build"
  ( cd "$TOOLCHAIN_DIR/_jdk_build"
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
    mv "$jvm_dir" "$JAVA_HOME_DIR"
  )
  rm -rf "$TOOLCHAIN_DIR/_jdk_build"



  tmpdir=$(mktemp -d)
  awk -v dir="$tmpdir" '/BEGIN CERTIFICATE/{n++; file=sprintf("%s/cert-%03d.pem", dir, n)} {print > file}' /etc/ssl/certs/ca-certificates.crt
  rm -f "$JAVA_HOME_DIR/lib/security/cacerts"
  i=0
  for f in "$tmpdir"/*.pem; do
    i=$((i+1))
    "$JAVA_HOME_DIR/bin/keytool" -importcert -noprompt -trustcacerts -alias "cert$i" -file "$f" \
      -keystore "$JAVA_HOME_DIR/lib/security/cacerts" -storepass changeit >/dev/null 2>&1 || true
  done
  rm -rf "$tmpdir"
  echo "✓ JDK installed" >&2
fi

export JAVA_HOME="$JAVA_HOME_DIR" PATH="$JAVA_HOME_DIR/bin:$PATH"




export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Djavax.net.ssl.trustStorePassword=changeit"


if [ ! -x "$ANDROID_SDK_ROOT_DIR/cmdline-tools/latest/bin/sdkmanager" ]; then
  echo "Setting up Android SDK..." >&2
  mkdir -p "$ANDROID_SDK_ROOT_DIR/cmdline-tools"

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
    exit 1
  fi

  unzip -q "$cli_zip" -d "$ANDROID_SDK_ROOT_DIR/cmdline-tools"
  rm -rf "$ANDROID_SDK_ROOT_DIR/cmdline-tools/latest"
  mv "$ANDROID_SDK_ROOT_DIR/cmdline-tools/cmdline-tools" "$ANDROID_SDK_ROOT_DIR/cmdline-tools/latest"
  rm -f "$cli_zip"
  echo "✓ Android SDK command-line tools installed" >&2
fi

export ANDROID_HOME="$ANDROID_SDK_ROOT_DIR" ANDROID_SDK_ROOT="$ANDROID_SDK_ROOT_DIR"
SDKMANAGER="$ANDROID_SDK_ROOT_DIR/cmdline-tools/latest/bin/sdkmanager"

echo "Accepting Android SDK licenses..." >&2
yes | "$SDKMANAGER" --licenses >/dev/null 2>&1 || true





echo "Installing Android SDK platform-tools..." >&2
"$SDKMANAGER" --install "platform-tools" 2>&1 | grep -v "^Warning:" || true


FLUTTER="$(command -v flutter || true)"
if [ -z "$FLUTTER" ]; then
  for d in "$HOME/flutter/bin" "$HOME/development/flutter/bin" \
           "$HOME/snap/flutter/common/flutter/bin" /opt/flutter/bin \
           "$FLUTTER_DIR/bin"; do
    if [ -x "$d/flutter" ]; then FLUTTER="$d/flutter"; break; fi
  done
fi
if [ -z "$FLUTTER" ]; then
  echo "Installing Flutter (stable channel)..." >&2
  git clone --depth 1 -b stable https://github.com/flutter/flutter.git "$FLUTTER_DIR" >&2
  FLUTTER="$FLUTTER_DIR/bin/flutter"
  echo "✓ Flutter installed" >&2
fi

export PATH="$(dirname "$FLUTTER"):$PATH"
"$FLUTTER" config --no-analytics >/dev/null 2>&1 || true
"$FLUTTER" config --android-sdk "$ANDROID_SDK_ROOT_DIR" >/dev/null

export FLUTTER
