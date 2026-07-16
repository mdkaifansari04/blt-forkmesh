#!/bin/sh
set -eu

package_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dart_bin=${DART_BIN:-/opt/homebrew/share/flutter/bin/cache/dart-sdk/bin/dart}
system_git=$(command -v git)
sentinel_dir=/private/tmp/forkmesh-g1-no-git-bin
sentinel_file=/private/tmp/forkmesh-g1-git-sentinel

mkdir -p "$sentinel_dir"
rm -f "$sentinel_file"
cat > "$sentinel_dir/git" <<'EOF'
#!/bin/sh
touch "$FORKMESH_G1_GIT_SENTINEL"
exit 97
EOF
chmod 755 "$sentinel_dir/git"

cd "$package_root"
test -x "$dart_bin"
"$dart_bin" run tool/verify_no_process_fallback.dart
PATH="$sentinel_dir:/usr/bin:/bin" \
FORKMESH_G1_GIT_SENTINEL="$sentinel_file" \
"$dart_bin" run tool/runtime_no_process_probe.dart
test ! -e "$sentinel_file"
FORKMESH_TEST_SYSTEM_GIT="$system_git" \
"$dart_bin" test test integration_test -r expanded
"$dart_bin" format --set-exit-if-changed lib test integration_test tool
"$dart_bin" analyze
printf '%s\n' 'no-process-fallback=PROVED'
