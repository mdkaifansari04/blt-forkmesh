"""Read-only Solana plumbing for the ForkMesh relay.

Self-contained helpers for public-address encoding and a fail-over JSON-RPC
client across keyless public endpoints. Transaction construction, private-key
import, signing, price speculation, and broadcasting deliberately do not exist
in the Worker bundle.

Split out of ``entry.py`` (adhoc #215). These functions depend only on the
stdlib and the Worker runtime's ``js``/``pyodide`` bridge — never on other
``entry`` state — so the import is strictly one-directional (``entry`` imports
from here). Amount formatting and the ``solana:`` pay-URI stay in ``entry``.
"""

import base64
import json

from js import Object
from pyodide.ffi import to_js as _to_js


def to_js(value):
    return _to_js(value, dict_converter=Object.fromEntries)


BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

def _base58_encode(data):
    n = int.from_bytes(data, "big")
    out = ""
    while n:
        n, rem = divmod(n, 58)
        out = BASE58_ALPHABET[rem] + out
    pad = 0
    for b in data:
        if b == 0:
            pad += 1
        else:
            break
    return "1" * pad + (out or "1")


def _b64url_encode(data):
    return base64.urlsafe_b64encode(data).decode().rstrip("=")







_SOLANA_PUBLIC_RPCS = (
    "https://solana-rpc.publicnode.com",
    "https://rpc.ankr.com/solana",
    "https://solana.drpc.org",
    "https://api.mainnet-beta.solana.com",
)
_SOLANA_TEST_RPCS = {
    "devnet": ("https://api.devnet.solana.com",),
    "testnet": ("https://api.testnet.solana.com",),
}


_SOLANA_RPC_PREFERRED = {"url": ""}
SOLANA_RPC_TIMEOUT_MS = 2500



_SOLANA_READ_ONLY_METHODS = frozenset({
    "getAccountInfo",
    "getBalance",
    "getGenesisHash",
    "getLatestBlockhash",


    "getSignaturesForAddress",
    "getSignatureStatuses",
    "getTransaction",
})


def _solana_endpoints(env):
    endpoints = []
    configured = (getattr(env, "SOLANA_RPC_URL", "") or "").replace(",", " ").split()
    for part in configured:
        part = part.strip()
        if part and part not in endpoints:
            endpoints.append(part)
    network = str(getattr(env, "SOLANA_NETWORK", "") or "").strip().lower()
    defaults = _SOLANA_TEST_RPCS.get(network, _SOLANA_PUBLIC_RPCS)
    for default in defaults:
        if default not in endpoints:
            endpoints.append(default)

    preferred = _SOLANA_RPC_PREFERRED["url"]
    if preferred in endpoints:
        endpoints.remove(preferred)
        endpoints.insert(0, preferred)
    return endpoints


async def _solana_rpc(env, method, params):
    if method not in _SOLANA_READ_ONLY_METHODS:
        return None
    from js import AbortSignal
    from js import fetch as js_fetch
    payload = json.dumps(
        {"jsonrpc": "2.0", "id": 1, "method": method, "params": params})
    for endpoint in _solana_endpoints(env):
        try:
            resp = await js_fetch(
                endpoint,
                to_js({
                    "method": "POST",
                    "headers": {
                        "content-type": "application/json",
                        "accept": "application/json",
                    },
                    "body": payload,


                    "signal": AbortSignal.timeout(SOLANA_RPC_TIMEOUT_MS),
                }),
            )
            if not (200 <= int(getattr(resp, "status", 0)) < 300):
                continue
            data = json.loads(await resp.text())
        except Exception:
            continue


        if isinstance(data, dict) and "result" in data:
            _SOLANA_RPC_PREFERRED["url"] = endpoint
            return data
    return None
