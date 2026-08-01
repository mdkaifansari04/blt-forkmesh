#!/usr/bin/env bash






set -euo pipefail
cd "$(dirname "$0")"







build_jobs() {
    local cores=2 ram_kb=0 ram_jobs
    if command -v nproc >/dev/null 2>&1; then
        cores="$(nproc)"
    elif command -v sysctl >/dev/null 2>&1; then
        cores="$(sysctl -n hw.logicalcpu)"
    fi
    if [ -r /proc/meminfo ]; then
        ram_kb="$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)"
    elif command -v sysctl >/dev/null 2>&1; then
        ram_kb="$(($(sysctl -n hw.memsize 2>/dev/null || printf '0') / 1024))"
    fi
    if [ "${ram_kb:-0}" -gt 0 ]; then
        ram_jobs=$((ram_kb / (3 * 1024 * 1024)))
        if [ "$ram_jobs" -lt 1 ]; then
            ram_jobs=1
        fi
        if [ "$ram_jobs" -lt "$cores" ]; then
            cores="$ram_jobs"
        fi
    fi
    printf '%s\n' "$cores"
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


        check_stale_cache
        args=()
        while IFS= read -r -d '' arg; do
            args+=("$arg")
        done < <(cmake_args)
        cmake -B build "${args[@]}" -DFORKMESH_BUILD_TESTS=ON
        cmake --build build --parallel "$(build_jobs)" \
            --target forkmesh-tests forkmesh-codex-tests forkmesh-window-tests
        QT_QPA_PLATFORM=offscreen ./build/forkmesh-tests
        ./build/forkmesh-codex-tests
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
