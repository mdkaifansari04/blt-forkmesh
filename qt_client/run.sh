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

# A CMakeCache.txt records the absolute source and binary dirs it was generated
# in. When a checkout is copied/merged from another machine (or moved), those
# paths no longer match and cmake aborts. Detect that and wipe the cache.
check_stale_cache() {
    local cache="build/CMakeCache.txt"
    [ -f "$cache" ] || return 0

    local cached_src cached_bin
    cached_src="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")"
    cached_bin="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$cache")"

    if { [ -n "$cached_src" ] && [ "$cached_src" != "$PWD" ]; } ||
       { [ -n "$cached_bin" ] && [ "$cached_bin" != "$PWD/build" ]; }; then
        echo "Stale build cache (generated in ${cached_src:-$cached_bin}); cleaning." >&2
        rm -rf build
    fi
}

build() {
    check_stale_cache
    local args=()
    while IFS= read -r -d '' arg; do
        args+=("$arg")
    done < <(cmake_args)
    cmake -B build "${args[@]}"
    # An optional target ($1) builds just that (e.g. the tests) independently of
    # the app; with no argument the default target (the app) is built.
    if [ -n "${1:-}" ]; then
        cmake --build build --parallel "$(build_jobs)" --target "$1"
    else
        cmake --build build --parallel "$(build_jobs)"
    fi
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
        # Build and run both test suites, independently of the app. Force the test
        # option on so a cached FORKMESH_BUILD_TESTS=OFF can't hide the targets.
        check_stale_cache
        args=()
        while IFS= read -r -d '' arg; do
            args+=("$arg")
        done < <(cmake_args)
        cmake -B build "${args[@]}" -DFORKMESH_BUILD_TESTS=ON
        cmake --build build --parallel "$(build_jobs)" \
            --target forkmesh-tests forkmesh-window-tests
        QT_QPA_PLATFORM=offscreen ./build/forkmesh-tests
        QT_QPA_PLATFORM=offscreen exec ./build/forkmesh-window-tests
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
