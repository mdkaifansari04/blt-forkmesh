"""Self-contained Bitcoin Cash wallet primitives for the donation funnel.

The signup flow generates a *fresh full keypair per donation* on the server,
shows the user that address, and — once the donation lands — sweeps the whole
balance to the ForkMesh treasury and forgets the key. That requires real
on-chain crypto in the Worker, where there is no npm / native secp256k1 module,
so everything here is hand-rolled pure Python:

  * secp256k1 point math + key generation
  * RIPEMD-160 (Pyodide/WebCrypto don't expose it) and hash160
  * CashAddr (BCH base32 + 40-bit polymod checksum) encode/decode
  * deterministic ECDSA (RFC 6979, low-S) + DER encoding
  * a P2PKH "sweep everything to one address" transaction with the BCH
    BIP143 sighash and SIGHASH_ALL|FORKID

It depends only on the standard library (``hashlib``/``hmac``), which is
available under Pyodide, so ``tests/test_bch_wallet.py`` exercises the exact
same code against known-answer vectors. Randomness is *injected* by the caller
(entry.py passes bytes from WebCrypto) so this module stays deterministic and
testable.

CUSTODY NOTE: the sweep path moves real funds. Validate against BCH testnet
before any mainnet balance flows through it.
"""

import hashlib
import hmac

# --- secp256k1 ---------------------------------------------------------------

_P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
_A = 0
_B = 7
_GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
_GY = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
_G = (_GX, _GY)


def _inverse_mod(a, m):
    return pow(a % m, -1, m)


def _point_add(p, q):
    if p is None:
        return q
    if q is None:
        return p
    (x1, y1), (x2, y2) = p, q
    if x1 == x2 and (y1 + y2) % _P == 0:
        return None  # point at infinity
    if x1 == x2 and y1 == y2:
        # doubling
        s = (3 * x1 * x1 + _A) * _inverse_mod(2 * y1, _P) % _P
    else:
        s = (y2 - y1) * _inverse_mod((x2 - x1) % _P, _P) % _P
    x3 = (s * s - x1 - x2) % _P
    y3 = (s * (x1 - x3) - y1) % _P
    return (x3, y3)


def _scalar_mult(k, point):
    result = None
    addend = point
    while k:
        if k & 1:
            result = _point_add(result, addend)
        addend = _point_add(addend, addend)
        k >>= 1
    return result


def privkey_to_pubkey(priv_int):
    """Return the 33-byte compressed SEC public key for a private scalar."""
    if not (1 <= priv_int < _N):
        raise ValueError("private key out of range")
    x, y = _scalar_mult(priv_int, _G)
    prefix = b"\x03" if (y & 1) else b"\x02"
    return prefix + x.to_bytes(32, "big")


def gen_privkey(rand_bytes):
    """Turn 32 caller-supplied random bytes into a valid private scalar.

    ``rand_bytes`` must come from a CSPRNG (the Worker passes
    ``crypto.getRandomValues``). Returns (priv_int, priv_hex).
    """
    if len(rand_bytes) != 32:
        raise ValueError("need 32 random bytes")
    k = int.from_bytes(rand_bytes, "big") % _N
    if k == 0:
        k = 1
    return k, "%064x" % k


# --- hashes ------------------------------------------------------------------


def _ripemd160(message):
    """Pure-Python RIPEMD-160; WebCrypto/Pyodide hashlib may not expose it."""
    try:
        h = hashlib.new("ripemd160")
        h.update(message)
        return h.digest()
    except Exception:
        pass

    # Reference implementation (ISO/IEC 10118-3).
    def rol(x, n):
        return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF

    def f(j, x, y, z):
        if j < 16:
            return x ^ y ^ z
        if j < 32:
            return (x & y) | (~x & z)
        if j < 48:
            return (x | ~y & 0xFFFFFFFF) ^ z
        if j < 64:
            return (x & z) | (y & ~z & 0xFFFFFFFF)
        return x ^ (y | ~z & 0xFFFFFFFF)

    KL = [0x00000000, 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xA953FD4E]
    KR = [0x50A28BE6, 0x5C4DD124, 0x6D703EF3, 0x7A6D76E9, 0x00000000]
    RL = [
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
        3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12,
        1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2,
        4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13,
    ]
    RR = [
        5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12,
        6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
        15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13,
        8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14,
        12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11,
    ]
    SL = [
        11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8,
        7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
        11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5,
        11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12,
        9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6,
    ]
    SR = [
        8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6,
        9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
        9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5,
        15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8,
        8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11,
    ]

    h0, h1, h2, h3, h4 = (0x67452301, 0xEFCDAB89, 0x98BADCFE,
                          0x10325476, 0xC3D2E1F0)
    msg = bytearray(message)
    length = (8 * len(message)) & 0xFFFFFFFFFFFFFFFF
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += length.to_bytes(8, "little")

    for off in range(0, len(msg), 64):
        chunk = msg[off:off + 64]
        x = [int.from_bytes(chunk[i:i + 4], "little") for i in range(0, 64, 4)]
        al, bl, cl, dl, el = h0, h1, h2, h3, h4
        ar, br, cr, dr, er = h0, h1, h2, h3, h4
        for j in range(80):
            t = (al + f(j, bl, cl, dl) + x[RL[j]] + KL[j // 16]) & 0xFFFFFFFF
            t = (rol(t, SL[j]) + el) & 0xFFFFFFFF
            al, bl, cl, dl, el = el, t, bl, rol(cl, 10), dl
            t = (ar + f(79 - j, br, cr, dr) + x[RR[j]] + KR[j // 16]) & 0xFFFFFFFF
            t = (rol(t, SR[j]) + er) & 0xFFFFFFFF
            ar, br, cr, dr, er = er, t, br, rol(cr, 10), dr
        t = (h1 + cl + dr) & 0xFFFFFFFF
        h1 = (h2 + dl + er) & 0xFFFFFFFF
        h2 = (h3 + el + ar) & 0xFFFFFFFF
        h3 = (h4 + al + br) & 0xFFFFFFFF
        h4 = (h0 + bl + cr) & 0xFFFFFFFF
        h0 = t
    return b"".join(v.to_bytes(4, "little") for v in (h0, h1, h2, h3, h4))


def sha256(b):
    return hashlib.sha256(b).digest()


def hash256(b):
    return sha256(sha256(b))


def hash160(b):
    return _ripemd160(sha256(b))


# --- CashAddr ----------------------------------------------------------------

_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
_CHARMAP = {c: i for i, c in enumerate(_CHARSET)}
_DEFAULT_PREFIX = "bitcoincash"


def _polymod(values):
    c = 1
    for d in values:
        c0 = c >> 35
        c = ((c & 0x07FFFFFFFF) << 5) ^ d
        if c0 & 0x01:
            c ^= 0x98F2BC8E61
        if c0 & 0x02:
            c ^= 0x79B76D99E2
        if c0 & 0x04:
            c ^= 0xF33E5FB3C4
        if c0 & 0x08:
            c ^= 0xAE2EABE2A8
        if c0 & 0x10:
            c ^= 0x1E4F43E470
    return c ^ 1


def _prefix_expand(prefix):
    return [ord(x) & 0x1F for x in prefix]


def _convertbits(data, frombits, tobits, pad=True):
    acc = 0
    bits = 0
    out = []
    maxv = (1 << tobits) - 1
    for value in data:
        acc = (acc << frombits) | value
        bits += frombits
        while bits >= tobits:
            bits -= tobits
            out.append((acc >> bits) & maxv)
    if pad and bits:
        out.append((acc << (tobits - bits)) & maxv)
    elif not pad and (bits >= frombits or ((acc << (tobits - bits)) & maxv)):
        return None
    return out


def cashaddr_encode(payload_bytes, version_byte=0x00, prefix=_DEFAULT_PREFIX):
    """Encode a 20-byte hash160 as a CashAddr (version byte 0x00 = P2KH)."""
    payload = bytes([version_byte]) + payload_bytes
    data = _convertbits(payload, 8, 5)
    checksum_input = _prefix_expand(prefix) + [0] + data + [0] * 8
    polymod = _polymod(checksum_input)
    checksum = [(polymod >> 5 * (7 - i)) & 0x1F for i in range(8)]
    body = "".join(_CHARSET[d] for d in data + checksum)
    return prefix + ":" + body


def cashaddr_decode(addr, prefix=_DEFAULT_PREFIX):
    """Return (version_byte, hash160_bytes) or raise ValueError."""
    addr = addr.strip().lower()
    if ":" in addr:
        got_prefix, body = addr.split(":", 1)
    else:
        got_prefix, body = prefix, addr
    if got_prefix != prefix:
        raise ValueError("unexpected cashaddr prefix")
    try:
        data = [_CHARMAP[c] for c in body]
    except KeyError:
        raise ValueError("invalid cashaddr character")
    if _polymod(_prefix_expand(got_prefix) + [0] + data) != 0:
        raise ValueError("bad cashaddr checksum")
    payload = _convertbits(data[:-8], 5, 8, pad=False)
    if not payload:
        raise ValueError("invalid cashaddr payload")
    return payload[0], bytes(payload[1:])


def pubkey_to_cashaddr(pubkey_bytes, prefix=_DEFAULT_PREFIX):
    return cashaddr_encode(hash160(pubkey_bytes), 0x00, prefix)


def cashaddr_to_hash160(addr, prefix=_DEFAULT_PREFIX):
    version, h = cashaddr_decode(addr, prefix)
    if version != 0x00 or len(h) != 20:
        raise ValueError("only P2KH addresses are supported")
    return h


# --- ECDSA (RFC 6979 deterministic, low-S) -----------------------------------


def _rfc6979_k(priv_int, msg_hash):
    # RFC 6979 §3.2 with HMAC-SHA256 over the 32-byte message hash.
    x = priv_int.to_bytes(32, "big")
    h1 = msg_hash
    v = b"\x01" * 32
    k = b"\x00" * 32
    k = hmac.new(k, v + b"\x00" + x + h1, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    k = hmac.new(k, v + b"\x01" + x + h1, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    while True:
        v = hmac.new(k, v, hashlib.sha256).digest()
        cand = int.from_bytes(v, "big")
        if 1 <= cand < _N:
            return cand
        k = hmac.new(k, v + b"\x00", hashlib.sha256).digest()
        v = hmac.new(k, v, hashlib.sha256).digest()


def ecdsa_sign(priv_int, msg_hash, low_s=True):
    """Deterministic ECDSA over secp256k1. Returns (r, s)."""
    while True:
        k = _rfc6979_k(priv_int, msg_hash)
        x, _ = _scalar_mult(k, _G)
        r = x % _N
        if r == 0:
            continue
        z = int.from_bytes(msg_hash, "big")
        s = (_inverse_mod(k, _N) * (z + r * priv_int)) % _N
        if s == 0:
            continue
        if low_s and s > _N // 2:
            s = _N - s
        return r, s


def der_encode_sig(r, s):
    def enc_int(v):
        b = v.to_bytes((v.bit_length() + 7) // 8 or 1, "big")
        if b[0] & 0x80:
            b = b"\x00" + b
        return b"\x02" + bytes([len(b)]) + b

    body = enc_int(r) + enc_int(s)
    return b"\x30" + bytes([len(body)]) + body


# --- Transaction (P2PKH, sweep everything to one address) --------------------


def _varint(n):
    if n < 0xFD:
        return bytes([n])
    if n <= 0xFFFF:
        return b"\xfd" + n.to_bytes(2, "little")
    if n <= 0xFFFFFFFF:
        return b"\xfe" + n.to_bytes(4, "little")
    return b"\xff" + n.to_bytes(8, "little")


def _p2pkh_script(hash160_bytes):
    # OP_DUP OP_HASH160 <20> .. OP_EQUALVERIFY OP_CHECKSIG
    return (b"\x76\xa9\x14" + hash160_bytes + b"\x88\xac")


SIGHASH_ALL_FORKID = 0x41  # SIGHASH_ALL | SIGHASH_FORKID (BCH)


def build_tx(priv_int, utxos, outputs, prefix=_DEFAULT_PREFIX):
    """Build a signed raw tx spending every utxo into ``outputs``.

    ``utxos``   : list of {"txid": hex, "vout": int, "value": sats}.
    ``outputs`` : list of (cashaddr, value_sats). The fee is implicit
                  (total_in - sum(outputs)); the caller sizes it.
    All inputs are assumed P2PKH for the wallet's own key. Uses the BCH BIP143
    sighash (SIGHASH_ALL|FORKID). Returns (raw_hex, total_in, total_out).
    Raises ValueError if the outputs are empty or exceed the inputs.
    """
    if not utxos:
        raise ValueError("no utxos to spend")
    if not outputs:
        raise ValueError("no outputs")
    pubkey = privkey_to_pubkey(priv_int)
    own_script = _p2pkh_script(hash160(pubkey))

    total_in = sum(int(u["value"]) for u in utxos)
    total_out = sum(int(v) for _, v in outputs)
    if total_out <= 0:
        raise ValueError("outputs must be positive")
    if total_out > total_in:
        raise ValueError("inputs do not cover the outputs")

    version = (1).to_bytes(4, "little")
    locktime = (0).to_bytes(4, "little")
    sequence = (0xFFFFFFFF).to_bytes(4, "little")

    prevouts = b""
    sequences = b""
    inputs = []
    for u in utxos:
        txid_le = bytes.fromhex(u["txid"])[::-1]
        vout = int(u["vout"]).to_bytes(4, "little")
        prevouts += txid_le + vout
        sequences += sequence
        inputs.append((txid_le, vout, int(u["value"])))

    out_bytes = b""
    for addr, value in outputs:
        script = _p2pkh_script(cashaddr_to_hash160(addr, prefix))
        out_bytes += int(value).to_bytes(8, "little") + _varint(len(script)) + script

    hash_prevouts = hash256(prevouts)
    hash_sequence = hash256(sequences)
    hash_outputs = hash256(out_bytes)

    sig_scripts = []
    for txid_le, vout, value in inputs:
        # BIP143 preimage (reused by BCH's FORKID sighash).
        preimage = (
            version
            + hash_prevouts
            + hash_sequence
            + txid_le + vout
            + _varint(len(own_script)) + own_script
            + value.to_bytes(8, "little")
            + sequence
            + hash_outputs
            + locktime
            + SIGHASH_ALL_FORKID.to_bytes(4, "little")
        )
        sighash = hash256(preimage)
        r, s = ecdsa_sign(priv_int, sighash)
        sig = der_encode_sig(r, s) + bytes([SIGHASH_ALL_FORKID])
        script_sig = (bytes([len(sig)]) + sig +
                      bytes([len(pubkey)]) + pubkey)
        sig_scripts.append(script_sig)

    raw = version + _varint(len(inputs))
    for (txid_le, vout, _), script_sig in zip(inputs, sig_scripts):
        raw += txid_le + vout + _varint(len(script_sig)) + script_sig + sequence
    raw += _varint(len(outputs)) + out_bytes + locktime
    return raw.hex(), total_in, total_out


def build_sweep_tx(priv_int, utxos, dest_addr, fee_sats, prefix=_DEFAULT_PREFIX):
    """Sweep every utxo to a single ``dest_addr`` (value = total_in - fee)."""
    total_in = sum(int(u["value"]) for u in utxos)
    send_value = total_in - int(fee_sats)
    if send_value <= 0:
        raise ValueError("inputs do not cover the fee")
    raw_hex, _, _ = build_tx(priv_int, utxos, [(dest_addr, send_value)], prefix)
    return raw_hex, total_in, send_value
