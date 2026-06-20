#!/usr/bin/env python3
"""Known-answer tests for src/bch_wallet.py.

bch_wallet.py is pure stdlib (hashlib/hmac), so it runs identically here and
under Pyodide. These vectors lock down the secp256k1 math, RIPEMD-160,
CashAddr checksum, and RFC 6979 ECDSA before the sweep path touches real funds.

Run: python3 cloudflare_worker/tests/test_bch_wallet.py   (exit 0 = all passed)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import bch_wallet as w  # noqa: E402

_failures = []


def check(name, got, expected):
    ok = got == expected
    print(("PASS " if ok else "FAIL ") + name)
    if not ok:
        print("      got:      %r" % (got,))
        print("      expected: %r" % (expected,))
        _failures.append(name)


def main():
    # secp256k1: priv=1 -> pubkey is the generator G (compressed, even Y).
    pub1 = w.privkey_to_pubkey(1)
    check("pubkey(priv=1) == compressed G",
          pub1.hex(),
          "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798")

    # hash160 of that compressed pubkey is the famous BIP173 example value.
    check("hash160(compressed G)",
          w.hash160(pub1).hex(),
          "751e76e8199196d454941c45d1b3a323f1433bd6")

    # RIPEMD-160 known-answer ("abc").
    check("ripemd160('abc')",
          w._ripemd160(b"abc").hex(),
          "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc")

    # CashAddr round-trip against a widely-cited valid mainnet address: decode
    # must accept its checksum and re-encoding the payload must reproduce it.
    known = "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a"
    version, h160 = w.cashaddr_decode(known)
    check("cashaddr decode version", version, 0x00)
    check("cashaddr re-encode round-trip", w.cashaddr_encode(h160, version), known)
    check("cashaddr_to_hash160 length", len(w.cashaddr_to_hash160(known)), 20)

    # Deterministic ECDSA (RFC 6979) over secp256k1+SHA-256, message "sample",
    # private key from the canonical secp256k1 test vector. The nonce k is the
    # published RFC 6979 value; r/s match the reference `ecdsa` library output
    # (canonical low-S). secp256k1 was not in the original RFC, so we lock on the
    # widely-used vector + an independent reference implementation.
    priv = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721
    msg_hash = w.sha256(b"sample")
    N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
    check("RFC6979 nonce k",
          "%064x" % w._rfc6979_k(priv, msg_hash),
          "a6e3c57dd01abe90086538398355dd4c3b17aa873382b0f24d6129493d8aad60")
    r, s = w.ecdsa_sign(priv, msg_hash)
    check("ECDSA r",
          "%064x" % r,
          "432310e32cb80eb6503a26ce83cc165c783b870845fb8aad6d970889fcd7a6c8")
    check("ECDSA s (low-S)",
          "%064x" % s,
          "530128b6b81c548874a6305d93ed071ca6e05074d85863d4056ce89b02bfab69")
    check("s is canonical (<= N/2)", s <= N // 2, True)
    r2, s2 = w.ecdsa_sign(priv, msg_hash)
    check("ECDSA deterministic", (r, s), (r2, s2))

    # DER encoding is well-formed.
    der = w.der_encode_sig(r, s)
    check("DER sequence tag", der[0], 0x30)
    check("DER length matches", der[1], len(der) - 2)

    # Sweep tx is deterministic and structurally sound.
    rand = bytes(range(1, 33))
    priv_int, _ = w.gen_privkey(rand)
    utxos = [{"txid": "00" * 32, "vout": 0, "value": 500000}]
    raw1, total_in, sent = w.build_sweep_tx(
        priv_int, utxos, "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
        fee_sats=300)
    raw2, _, _ = w.build_sweep_tx(
        priv_int, utxos, "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
        fee_sats=300)
    check("sweep tx deterministic", raw1, raw2)
    check("sweep total_in", total_in, 500000)
    check("sweep sent = in - fee", sent, 500000 - 300)
    check("sweep tx version prefix", raw1[:8], "01000000")
    try:
        w.build_sweep_tx(priv_int, utxos,
                         "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
                         fee_sats=500000)
        check("sweep rejects fee >= inputs", "no-raise", "raise")
    except ValueError:
        check("sweep rejects fee >= inputs", "raise", "raise")

    # Multi-output payout tx (50% treasury + split) is deterministic, encodes the
    # right number of outputs, and rejects over-spending.
    treasury = "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a"
    node = "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a"
    outs = [(treasury, 249700), (node, 249700)]
    raw3, ti, to = w.build_tx(priv_int, utxos, outs)
    raw4, _, _ = w.build_tx(priv_int, utxos, outs)
    check("payout tx deterministic", raw3, raw4)
    check("payout total_in", ti, 500000)
    check("payout total_out", to, 499400)
    # Output count byte sits right after all inputs; a 1-in tx has the count at a
    # fixed offset, so just assert the raw contains both output scripts (one per
    # payee) by counting the P2PKH script prefix occurrences.
    check("payout has 2 outputs", raw3.count("76a914"), 2)
    try:
        w.build_tx(priv_int, utxos, [(treasury, 400000), (node, 400000)])
        check("payout rejects over-spend", "no-raise", "raise")
    except ValueError:
        check("payout rejects over-spend", "raise", "raise")

    print()
    if _failures:
        print("%d test(s) FAILED: %s" % (len(_failures), ", ".join(_failures)))
        return 1
    print("All bch_wallet tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
