#!/usr/bin/env bash
# Shared pywrangler bootstrap for deploy.sh and migrate.sh
#
# Prefer uvx when available: it self-fetches workers-py without a project-local
# install. On hosts without uv/uvx, install workers-py into a private venv under
# cloudflare_worker/.pywrangler so deploys do not depend on global Python tools.

PYWRANGLER_VENV="${PYWRANGLER_VENV:-.pywrangler}"
PYWRANGLER_BIN="$PYWRANGLER_VENV/bin/pywrangler"
PYWRANGLER_UVX="$PYWRANGLER_VENV/bin/uvx"
PYWRANGLER_UV="$PYWRANGLER_VENV/bin/uv"
WORKERS_PY_SPEC="${WORKERS_PY_SPEC:-workers-py<1.14.0}"
WRANGLER_NPM_SPEC="${WRANGLER_NPM_SPEC:-wrangler@4.42.1}"

_pywrangler_ensure_venv_path() {
    case ":$PATH:" in
        *":$PYWRANGLER_VENV/bin:"*) ;;
        *) PATH="$PYWRANGLER_VENV/bin:$PATH" ;;
    esac
}

_pywrangler_install_npx_wrapper() {
    mkdir -p "$PYWRANGLER_VENV/bin"
    cat > "$PYWRANGLER_VENV/bin/npx" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ge 2 ] && [ "$1" = "--yes" ] && [ "$2" = "wrangler" ]; then
    shift 2
    if ! command -v npm >/dev/null 2>&1; then
        echo "error: npm is required because pywrangler delegates deploys to Wrangler." >&2
        exit 1
    fi
    exec npm exec --yes --package "${WRANGLER_NPM_SPEC:-wrangler@4.42.1}" -- wrangler "$@"
fi

self_dir="$(cd "$(dirname "$0")" && pwd)"
old_path="$PATH"
new_path=""
IFS=':' read -r -a path_parts <<< "$old_path"
for path_part in "${path_parts[@]}"; do
    [ "$path_part" = "$self_dir" ] && continue
    if [ -z "$new_path" ]; then
        new_path="$path_part"
    else
        new_path="$new_path:$path_part"
    fi
done
PATH="$new_path"
real_npx="$(command -v npx || true)"
PATH="$old_path"
if [ -z "$real_npx" ]; then
    echo "error: npx not found on PATH." >&2
    exit 1
fi
exec "$real_npx" "$@"
EOF
    chmod 0755 "$PYWRANGLER_VENV/bin/npx"
}

pywrangler() {
    _pywrangler_ensure_venv_path
    _pywrangler_install_npx_wrapper
    local pywrangler_path
    pywrangler_path="$(type -P pywrangler || true)"
    if [ -n "$pywrangler_path" ]; then
        "$pywrangler_path" "$@"
        return
    fi

    if [ "${FORKMESH_NO_NODE_PACKAGES:-0}" = "1" ]; then
        echo "error: pywrangler is not installed, and this Cloudflare pipeline is configured" >&2
        echo "       with FORKMESH_NO_NODE_PACKAGES=1." >&2
        echo "       Not auto-installing workers-py here: current pywrangler deploy" >&2
        echo "       proxies to 'npx wrangler', which would require Node/npm packages." >&2
        echo "       Install/provide a pywrangler executable in the runner image, or run" >&2
        echo "       without FORKMESH_NO_NODE_PACKAGES if a Node-backed pywrangler is OK." >&2
        return 1
    fi

    if command -v uvx >/dev/null 2>&1; then
        uvx --from "$WORKERS_PY_SPEC" pywrangler "$@"
        return
    fi
    if [ -x "$PYWRANGLER_UVX" ]; then
        "$PYWRANGLER_UVX" --from "$WORKERS_PY_SPEC" pywrangler "$@"
        return
    fi
    if command -v uv >/dev/null 2>&1; then
        uv tool run --from "$WORKERS_PY_SPEC" pywrangler "$@"
        return
    fi
    if [ -x "$PYWRANGLER_UV" ]; then
        "$PYWRANGLER_UV" tool run --from "$WORKERS_PY_SPEC" pywrangler "$@"
        return
    fi
    if [ ! -x "$PYWRANGLER_BIN" ]; then
        install_pywrangler || return
    fi
    if [ -x "$PYWRANGLER_UVX" ]; then
        "$PYWRANGLER_UVX" --from "$WORKERS_PY_SPEC" pywrangler "$@"
        return
    fi
    if [ -x "$PYWRANGLER_UV" ]; then
        "$PYWRANGLER_UV" tool run --from "$WORKERS_PY_SPEC" pywrangler "$@"
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
    "$PYWRANGLER_VENV/bin/python" -m pip install --upgrade "$WORKERS_PY_SPEC"
    # Newer workers-py binaries can require uv tooling at runtime. Install a
    # project-local uv copy so we can keep the deploy path self-contained when
    # the host only has python3.
    if [ ! -x "$PYWRANGLER_UVX" ] || [ ! -x "$PYWRANGLER_UV" ]; then
        "$PYWRANGLER_VENV/bin/python" -m pip install --upgrade uv >/dev/null
    fi

    # Keep the venv bin on PATH so any worker-installed entrypoint that shells
    # out to uv/uvx can find the project-local copy.
    _pywrangler_ensure_venv_path
    _pywrangler_install_npx_wrapper

    if [ ! -x "$PYWRANGLER_BIN" ]; then
        echo "error: workers-py installed, but $PYWRANGLER_BIN was not created." >&2
        return 1
    fi
}
