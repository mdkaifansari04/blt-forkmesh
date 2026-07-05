#!/usr/bin/env bash
# Shared pywrangler bootstrap for deploy.sh and migrate.sh.
#
# Prefer uvx when available: it self-fetches workers-py without a project-local
# install. On hosts without uv/uvx, install workers-py into a private venv under
# cloudflare_worker/.pywrangler so deploys do not depend on global Python tools.

PYWRANGLER_VENV="${PYWRANGLER_VENV:-.pywrangler}"
PYWRANGLER_BIN="$PYWRANGLER_VENV/bin/pywrangler"
PYWRANGLER_UVX="$PYWRANGLER_VENV/bin/uvx"
PYWRANGLER_UV="$PYWRANGLER_VENV/bin/uv"

pywrangler() {
    if command -v uvx >/dev/null 2>&1; then
        uvx --from workers-py pywrangler "$@"
        return
    fi
    if [ -x "$PYWRANGLER_UVX" ]; then
        "$PYWRANGLER_UVX" --from workers-py pywrangler "$@"
        return
    fi
    if command -v uv >/dev/null 2>&1; then
        uv tool run --from workers-py pywrangler "$@"
        return
    fi
    if [ -x "$PYWRANGLER_UV" ]; then
        "$PYWRANGLER_UV" tool run --from workers-py pywrangler "$@"
        return
    fi
    local pywrangler_path
    pywrangler_path="$(type -P pywrangler || true)"
    if [ -n "$pywrangler_path" ]; then
        "$pywrangler_path" "$@"
        return
    fi
    if [ ! -x "$PYWRANGLER_BIN" ]; then
        install_pywrangler || return
    fi
    if [ -x "$PYWRANGLER_UVX" ]; then
        "$PYWRANGLER_UVX" --from workers-py pywrangler "$@"
        return
    fi
    if [ -x "$PYWRANGLER_UV" ]; then
        "$PYWRANGLER_UV" tool run --from workers-py pywrangler "$@"
        return
    fi
    "$PYWRANGLER_BIN" "$@"
}

install_pywrangler() {
    if ! command -v python3 >/dev/null 2>&1; then
        echo "error: need python3 to install pywrangler automatically." >&2
        echo "       Install uv (recommended) or install pywrangler manually:" >&2
        echo "       https://docs.astral.sh/uv/getting-started/" >&2
        return 1
    fi

    echo "pywrangler not found; installing workers-py into $PYWRANGLER_VENV ..." >&2
    python3 -m venv "$PYWRANGLER_VENV"
    "$PYWRANGLER_VENV/bin/python" -m pip install --upgrade pip >/dev/null
    "$PYWRANGLER_VENV/bin/python" -m pip install --upgrade workers-py
    # Newer workers-py binaries can require uv tooling at runtime. Install a
    # project-local uv copy so we can keep the deploy path self-contained when
    # the host only has python3.
    if [ ! -x "$PYWRANGLER_UVX" ] && [ ! -x "$PYWRANGLER_UV" ]; then
        "$PYWRANGLER_VENV/bin/python" -m pip install --upgrade uv >/dev/null
    fi

    if [ ! -x "$PYWRANGLER_BIN" ]; then
        echo "error: workers-py installed, but $PYWRANGLER_BIN was not created." >&2
        return 1
    fi
}
