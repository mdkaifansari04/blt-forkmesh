#!/usr/bin/env bash
# name: Launch Flutter app (Android)
# description: Build and run the ForkMesh Flutter app on a connected Android device or emulator.
set -euo pipefail

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

"$FLUTTER" pub get

# -d android picks whatever Android device/emulator is attached; fail with the
# device list if none is.
if ! "$FLUTTER" devices | grep -qi android; then
  echo "No Android device or emulator detected:" >&2
  "$FLUTTER" devices >&2
  echo "Start an emulator (flutter emulators --launch <id>) or plug in a device." >&2
  exit 1
fi

exec "$FLUTTER" run -d android
