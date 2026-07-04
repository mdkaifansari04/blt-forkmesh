"""Solana on-chain plumbing for the ForkMesh relay.

Self-contained helpers for the signup-deposit / bounty custody flow: base58 and
base64url codecs, a fail-over JSON-RPC client across keyless public endpoints,
minimal transfer-transaction assembly + Ed25519 signing via WebCrypto, and the
on-chain Pyth SOL/USD price read (with an HTTP fallback).

Split out of ``entry.py`` (adhoc #215). These functions depend only on the
stdlib and the Worker runtime's ``js``/``pyodide`` bridge — never on other
``entry`` state — so the import is strictly one-directional (``entry`` imports
from here). Amount formatting and the ``solana:`` pay-URI stay in ``entry``
because they reference the lamport/pricing constants that live there.
"""

import base64
import json
import struct

from js import Object
from js import Uint8Array
from js import crypto as js_crypto
from pyodide.ffi import to_js as _to_js


def to_js(value):
    return _to_js(value, dict_converter=Object.fromEntries)


# Signup payments land in unique per-account deposit wallets; the worker sweeps
# confirmed deposits to the treasury and currently-online node payout addresses.
BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
BASE58_INDEX = {ch: i for i, ch in enumerate(BASE58_ALPHABET)}
SOLANA_SYSTEM_PROGRAM = "11111111111111111111111111111111"

# Pyth SOL/USD price account on Solana mainnet (read on-chain via our own RPC so
# the price doesn't depend on a third-party HTTP price API). Overridable via env.
PYTH_SOL_USD_ACCOUNT_DEFAULT = "H6ARHf6YXhGYeQfUzQNGk6rDNnLBQKrenN712K4AQJEG"


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


def _base58_decode(value):
    n = 0
    for ch in value:
        if ch not in BASE58_INDEX:
            return b""
        n = n * 58 + BASE58_INDEX[ch]
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big") if n else b""
    pad = 0
    for ch in value:
        if ch == "1":
            pad += 1
        else:
            break
    return b"\x00" * pad + raw


def _b64url_encode(data):
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def _shortvec(n):
    out = bytearray()
    while True:
        elem = n & 0x7F
        n >>= 7
        if n:
            elem |= 0x80
        out.append(elem)
        if not n:
            return bytes(out)


# Reliability: the canonical public RPC (api.mainnet-beta.solana.com) rate-limits
# / blocks datacenter (Cloudflare) egress, which silently broke getBalance and
# left signups stuck "checking". We fail over across several keyless public
# endpoints, and an operator can prepend their OWN node (or a keyed provider) via
# SOLANA_RPC_URL (space/comma separated) so no third party is required at all.
_SOLANA_PUBLIC_RPCS = (
    "https://solana-rpc.publicnode.com",
    "https://rpc.ankr.com/solana",
    "https://solana.drpc.org",
    "https://api.mainnet-beta.solana.com",
)
# Remember the endpoint that last answered so we hit it first instead of
# re-walking dead hosts on every poll.
_SOLANA_RPC_PREFERRED = {"url": ""}


def _solana_endpoints(env):
    endpoints = []
    configured = (getattr(env, "SOLANA_RPC_URL", "") or "").replace(",", " ").split()
    for part in configured:
        part = part.strip()
        if part and part not in endpoints:
            endpoints.append(part)
    for default in _SOLANA_PUBLIC_RPCS:
        if default not in endpoints:
            endpoints.append(default)
    # Try the last-good endpoint first.
    preferred = _SOLANA_RPC_PREFERRED["url"]
    if preferred in endpoints:
        endpoints.remove(preferred)
        endpoints.insert(0, preferred)
    return endpoints


async def _solana_rpc(env, method, params):
    from js import fetch as js_fetch
    payload = json.dumps(
        {"jsonrpc": "2.0", "id": 1, "method": method, "params": params})
    for endpoint in _solana_endpoints(env):
        try:
            resp = await js_fetch(
                endpoint,
                to_js({
                    "method": "POST",
                    "headers": {"content-type": "application/json",
                                "accept": "application/json"},
                    "body": payload,
                }),
            )
            if not (200 <= int(getattr(resp, "status", 0)) < 300):
                continue
            data = json.loads(await resp.text())
        except Exception:
            continue
        # A well-formed JSON-RPC reply carries "result"; anything else (including
        # a rate-limit error object) means try the next endpoint.
        if isinstance(data, dict) and "result" in data:
            _SOLANA_RPC_PREFERRED["url"] = endpoint
            return data
    return None


async def _solana_latest_blockhash(env):
    resp = await _solana_rpc(env, "getLatestBlockhash", [])
    if not isinstance(resp, dict):
        return ""
    try:
        return str(resp["result"]["value"]["blockhash"] or "")
    except Exception:
        return ""


async def _solana_send_transaction(env, tx_bytes):
    encoded = base64.b64encode(tx_bytes).decode()
    resp = await _solana_rpc(
        env, "sendTransaction",
        [encoded, {"encoding": "base64", "skipPreflight": False}],
    )
    if not isinstance(resp, dict):
        return ""
    result = resp.get("result")
    return str(result or "") if result else ""


def _solana_transfer_message(from_addr, transfers, blockhash):
    account_addrs = [from_addr]
    for to_addr, _lamports in transfers:
        if to_addr not in account_addrs:
            account_addrs.append(to_addr)
    if SOLANA_SYSTEM_PROGRAM not in account_addrs:
        account_addrs.append(SOLANA_SYSTEM_PROGRAM)
    program_idx = account_addrs.index(SOLANA_SYSTEM_PROGRAM)
    out = bytearray()
    out += bytes([1, 0, 1])
    out += _shortvec(len(account_addrs))
    for addr in account_addrs:
        raw = _base58_decode(addr)
        if len(raw) != 32:
            return b""
        out += raw
    blockhash_raw = _base58_decode(blockhash)
    if len(blockhash_raw) != 32:
        return b""
    out += blockhash_raw
    out += _shortvec(len(transfers))
    for to_addr, lamports in transfers:
        data = struct.pack("<IQ", 2, int(lamports))
        out += bytes([program_idx])
        out += _shortvec(2) + bytes([0, account_addrs.index(to_addr)])
        out += _shortvec(len(data)) + data
    return bytes(out)


async def _solana_sign_message(from_addr, seed_b64url, message):
    pub = _base58_decode(from_addr)
    if len(pub) != 32 or not seed_b64url or not message:
        return b""
    try:
        jwk = {
            "kty": "OKP", "crv": "Ed25519", "x": _b64url_encode(pub),
            "d": seed_b64url, "ext": True, "key_ops": ["sign"],
        }
        key = await js_crypto.subtle.importKey(
            "jwk", to_js(jwk), to_js({"name": "Ed25519"}), False,
            _to_js(["sign"])
        )
        sig = await js_crypto.subtle.sign(
            to_js({"name": "Ed25519"}), key, _to_js(message))
        return bytes(Uint8Array.new(sig).to_py())
    except Exception:
        return b""


# --- SOL/USD price + dynamic minimum ----------------------------------------
def _parse_pyth_price(raw_bytes):
    # Pyth v2 price account: exponent (i32 LE) at offset 20, aggregate price
    # (i64 LE) at offset 208. price = agg_price * 10**expo. Guarded so a layout
    # mismatch falls through to the bounds check rather than returning garbage.
    try:
        if len(raw_bytes) < 216:
            return 0.0
        expo = int.from_bytes(raw_bytes[20:24], "little", signed=True)
        agg = int.from_bytes(raw_bytes[208:216], "little", signed=True)
        if agg <= 0 or expo < -18 or expo > 0:
            return 0.0
        return agg * (10.0 ** expo)
    except Exception:
        return 0.0


async def _sol_usd_from_pyth(env):
    account = (getattr(env, "PYTH_SOL_USD_ACCOUNT", "") or
               PYTH_SOL_USD_ACCOUNT_DEFAULT).strip()
    resp = await _solana_rpc(
        env, "getAccountInfo", [account, {"encoding": "base64"}])
    if not isinstance(resp, dict):
        return 0.0
    value = (resp.get("result") or {}).get("value") if isinstance(
        resp.get("result"), dict) else None
    data = value.get("data") if isinstance(value, dict) else None
    if not isinstance(data, list) or not data:
        return 0.0
    try:
        raw = base64.b64decode(data[0])
    except Exception:
        return 0.0
    return _parse_pyth_price(raw)


async def _sol_usd_from_http(env):
    # Fallback only: a configurable HTTP price source (default CoinGecko).
    from js import fetch as js_fetch
    url = (getattr(env, "SOL_PRICE_URL", "") or
           "https://api.coingecko.com/api/v3/simple/price"
           "?ids=solana&vs_currencies=usd").strip()
    try:
        resp = await js_fetch(url, to_js({"method": "GET"}))
        if not (200 <= int(getattr(resp, "status", 0)) < 300):
            return 0.0
        body = json.loads(await resp.text())
        return float((body.get("solana") or {}).get("usd") or 0.0)
    except Exception:
        return 0.0
