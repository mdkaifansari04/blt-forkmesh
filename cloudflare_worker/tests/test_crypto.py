#!/usr/bin/env python3
"""Algorithm tests for the worker's auth/crypto primitives.

src/entry.py implements TOTP, PBKDF2 and the blind index on top of WebCrypto
(pyodide), which can't run outside the Workers runtime. This file is a pure
stdlib *mirror* of the exact same algorithms, checked against published RFC /
known-answer vectors. If these pass, the math in entry.py is correct; what it
does NOT cover is the pyodide<->WebCrypto glue (verify that on a real deploy).

Run: python3 cloudflare_worker/tests/test_crypto.py   (exit 0 = all passed)
"""
import base64
import hashlib
import hmac
import struct
import sys

# --- Mirrors of entry.py -----------------------------------------------------


def b32_decode(secret):
    s = (secret or "").strip().upper().replace(" ", "")
    return base64.b32decode(s + "=" * ((8 - len(s) % 8) % 8))


def hotp(secret_bytes, counter, digits=6):
    mac = hmac.new(secret_bytes, struct.pack(">Q", counter), hashlib.sha1).digest()
    offset = mac[-1] & 0x0F
    code = ((mac[offset] & 0x7F) << 24 | (mac[offset + 1] & 0xFF) << 16 |
            (mac[offset + 2] & 0xFF) << 8 | (mac[offset + 3] & 0xFF)) % (10 ** digits)
    return str(code).zfill(digits)


def totp(secret_b32, unix_time, step=30, digits=6):
    return hotp(b32_decode(secret_b32), unix_time // step, digits)


def pbkdf2_sha256(password, salt, iters, dklen=32):
    return hashlib.pbkdf2_hmac("sha256", password, salt, iters, dklen)


def blind_index(data_key, value):
    key = hashlib.sha256((data_key + ":blind-index").encode()).digest()
    norm = (value or "").strip().lower().encode()
    return hmac.new(key, norm, hashlib.sha256).hexdigest()


# --- Test runner -------------------------------------------------------------

_failures = []


def check(name, got, expected):
    ok = got == expected
    print(("PASS " if ok else "FAIL ") + name)
    if not ok:
        print("      got:      %r" % (got,))
        print("      expected: %r" % (expected,))
        _failures.append(name)


def main():
    # RFC 4226 HOTP vectors — secret = ASCII "12345678901234567890".
    rfc4226_secret = b"12345678901234567890"
    rfc4226 = ["755224", "287082", "359152", "969429", "338314",
               "254676", "287922", "162583", "399871", "520489"]
    for counter, expected in enumerate(rfc4226):
        check("HOTP/RFC4226 count=%d" % counter,
              hotp(rfc4226_secret, counter), expected)

    # RFC 6238 TOTP (SHA-1) — 6-digit truncation of the published 8-digit codes.
    secret_b32 = base64.b32encode(rfc4226_secret).decode()
    rfc6238 = [
        (59, "287082"),
        (1111111109, "081804"),
        (1111111111, "050471"),
        (1234567890, "005924"),
        (2000000000, "279037"),
        (20000000000, "353130"),
    ]
    for t, expected in rfc6238:
        check("TOTP/RFC6238 T=%d" % t, totp(secret_b32, t), expected)

    # PBKDF2-HMAC-SHA256 known-answer vectors (dkLen=32).
    check(
        "PBKDF2-SHA256 password/salt/1",
        pbkdf2_sha256(b"password", b"salt", 1).hex(),
        "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
    )
    check(
        "PBKDF2-SHA256 password/salt/2",
        pbkdf2_sha256(b"password", b"salt", 2).hex(),
        "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
    )
    # Round-trip at the worker's real iteration count is deterministic.
    h1 = pbkdf2_sha256(b"hunter2", b"\x00" * 16, 150000)
    h2 = pbkdf2_sha256(b"hunter2", b"\x00" * 16, 150000)
    check("PBKDF2-SHA256 deterministic (150k)", h1, h2)
    check("PBKDF2-SHA256 rejects wrong password",
          pbkdf2_sha256(b"wrong", b"\x00" * 16, 150000) == h1, False)

    # Blind index: deterministic, case/space-insensitive, distinct, fixed vector.
    k = "test-data-key"
    check("blind_index deterministic", blind_index(k, "Alice"), blind_index(k, "alice"))
    check("blind_index trims/lowercases", blind_index(k, "  ALICE  "), blind_index(k, "alice"))
    check("blind_index distinct values",
          blind_index(k, "alice") != blind_index(k, "bob"), True)
    check("blind_index key-separated",
          blind_index("key-a", "alice") != blind_index("key-b", "alice"), True)
    check(
        "blind_index regression vector",
        blind_index("forkmesh-dev-data-key", "alice/myrepo"),
        hmac.new(
            hashlib.sha256(b"forkmesh-dev-data-key:blind-index").digest(),
            b"alice/myrepo", hashlib.sha256,
        ).hexdigest(),
    )

    print()
    if _failures:
        print("%d test(s) FAILED: %s" % (len(_failures), ", ".join(_failures)))
        return 1
    print("All crypto algorithm tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
