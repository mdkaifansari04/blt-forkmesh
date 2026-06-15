#!/usr/bin/env bash
# Build and run ForkMesh locally.
#
#   ./run.sh          build (incremental) and launch the app
#   ./run.sh clean    clear the build cache (removes build/)
#   ./run.sh rebuild  clear the cache, then build and launch
#   ./run.sh test     build and run the headless backend tests
set -euo pipefail
cd "$(dirname "$0")"

build_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.logicalcpu
    else
        printf '2\n'
    fi
}

cmake_args() {
    printf '%s\0' -DCMAKE_BUILD_TYPE=Release
    if [ "$(uname -s)" = "Darwin" ] && command -v brew >/dev/null 2>&1; then
        local qt_prefix openssl_prefix
        qt_prefix="$(brew --prefix qt 2>/dev/null || true)"
        openssl_prefix="$(brew --prefix openssl@3 2>/dev/null || true)"
        [ -n "$qt_prefix" ] && printf '%s\0' "-DCMAKE_PREFIX_PATH=$qt_prefix"
        [ -n "$openssl_prefix" ] && printf '%s\0' "-DOPENSSL_ROOT_DIR=$openssl_prefix"
    fi
}

build() {
    local args=()
    while IFS= read -r -d '' arg; do
        args+=("$arg")
    done < <(cmake_args)
    cmake -B build "${args[@]}"
    cmake --build build --parallel "$(build_jobs)"
}

forkmesh_bin() {
    if [ -x ./build/ForkMesh.app/Contents/MacOS/ForkMesh ]; then
        printf '%s\n' ./build/ForkMesh.app/Contents/MacOS/ForkMesh
    else
        printf '%s\n' ./build/forkmesh
    fi
}

case "${1:-run}" in
    clean)
        rm -rf build
        echo "Build cache cleared."
        ;;
    rebuild)
        rm -rf build
        build
        exec "$(forkmesh_bin)"
        ;;
    test)
        build
        QT_QPA_PLATFORM=offscreen exec ./build/forkmesh-tests
        ;;
    run)
        build
        exec "$(forkmesh_bin)"
        ;;
    *)
        echo "Usage: $0 [run|clean|rebuild|test]" >&2
        exit 2
        ;;
esac
