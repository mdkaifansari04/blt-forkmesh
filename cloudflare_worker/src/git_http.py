"""Git smart-HTTP + repo-blob byte helpers for the ForkMesh relay Worker.

Pure ``stdlib``-only helpers pulled out of the ~11k-line ``entry.py`` so the git
wire-format code (pkt-line framing, ref-advertisement canonicalization, request
content-encoding decoding) and the repo-blob content-type/filename mapping live
in one small, independently-scannable module. Like ``urls.py`` and ``solana.py``
(adhoc #215), this imports no ``js``/``workers`` runtime, so both the Worker (on
Cloudflare) and the test suite load it standalone — the tests AST-extract these
functions the same way they do for ``entry.py``.
"""

import gzip
import io
import re


def pkt_line(payload):
    return ("%04x" % (len(payload) + 4)).encode() + payload


def advertised_refs_canonical(data):
    # Canonical, hashable fingerprint of the served branches + tags, derived from
    # a `git upload-pack --advertise-refs` body (the bytes a host streams back for
    # info/refs, WITHOUT the "# service=" header the worker prepends). Output is
    # "<sha> <refname>" lines for refs/heads/* and refs/tags/* only, sorted, joined
    # by "\n" — byte-for-byte identical to the desktop node's mirrorStateHash()
    # input (git for-each-ref over the same namespaces). HEAD, peeled tags
    # ("...^{}"), and per-line capabilities (after the first NUL) are dropped.
    # Assumes the traditional (protocol v0) advertisement; the host never sets
    # GIT_PROTOCOL=version=2, so refs are always listed inline.
    data = bytes(data or b"")
    refs = []
    i = 0
    n = len(data)
    while i + 4 <= n:
        try:
            length = int(data[i:i + 4], 16)
        except ValueError:
            break
        if length == 0:        # flush-pkt ("0000") — section/stream boundary
            i += 4
            continue
        if length < 4 or i + length > n:
            break              # malformed; stop rather than misread
        line = data[i + 4:i + length].rstrip(b"\n")
        i += length
        nul = line.find(b"\x00")
        if nul != -1:          # strip capabilities advertised on the first ref
            line = line[:nul]
        parts = line.split(b" ", 1)
        if len(parts) != 2:
            continue
        sha = parts[0].decode("ascii", "ignore")
        name = parts[1].decode("utf-8", "ignore")
        if name == "HEAD" or name.endswith("^{}"):
            continue
        if not (name.startswith("refs/heads/") or name.startswith("refs/tags/")):
            continue
        refs.append(sha + " " + name)
    refs.sort()
    return "\n".join(refs)


def decode_git_request_body(data, content_encoding, max_bytes=8 * 1024 * 1024):
    """Return the Git smart-HTTP body after decoding HTTP content encodings."""
    data = bytes(data or b"")
    if len(data) > max_bytes:
        raise ValueError("git request body is too large")

    encodings = [
        item.strip().lower()
        for item in (content_encoding or "").split(",")
        if item.strip()
    ]
    # Content encodings are decoded in reverse application order. Git uses gzip
    # once its upload-pack request crosses http.postBuffer; forwarding those raw
    # bytes makes upload-pack parse the gzip header as a pkt-line and fail with
    # "bad line length character".
    for encoding in reversed(encodings):
        if encoding == "identity":
            continue
        if encoding != "gzip":
            raise ValueError("unsupported git content encoding: " + encoding)
        try:
            with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:
                data = stream.read(max_bytes + 1)
        except (EOFError, OSError) as error:
            raise ValueError("invalid gzip git request body") from error
        if len(data) > max_bytes:
            raise ValueError("git request body is too large")
    return data


REPO_BLOB_CONTENT_TYPES = {
    "3g2": "video/3gpp2",
    "3gp": "video/3gpp",
    "aac": "audio/aac",
    "apng": "image/png",
    "avi": "video/x-msvideo",
    "avif": "image/avif",
    "bmp": "image/bmp",
    "csv": "text/csv; charset=utf-8",
    "flac": "audio/flac",
    "gif": "image/gif",
    "ico": "image/x-icon",
    "jfif": "image/jpeg",
    "jpe": "image/jpeg",
    "jpeg": "image/jpeg",
    "jpg": "image/jpeg",
    "m4a": "audio/mp4",
    "m4v": "video/mp4",
    "mid": "audio/midi",
    "midi": "audio/midi",
    "mkv": "video/x-matroska",
    "mov": "video/quicktime",
    "mp3": "audio/mpeg",
    "mp4": "video/mp4",
    "mpeg": "video/mpeg",
    "mpg": "video/mpeg",
    "oga": "audio/ogg",
    "ogg": "audio/ogg",
    "ogv": "video/ogg",
    "opus": "audio/ogg",
    "pdf": "application/pdf",
    "png": "image/png",
    "svg": "image/svg+xml",
    "tab": "text/tab-separated-values; charset=utf-8",
    "tif": "image/tiff",
    "tiff": "image/tiff",
    "tsv": "text/tab-separated-values; charset=utf-8",
    "wav": "audio/wav",
    "weba": "audio/webm",
    "webm": "video/webm",
    "webp": "image/webp",
}


def repo_blob_content_type(path):
    name = str(path or "").rsplit("/", 1)[-1].lower()
    if "." not in name:
        return "application/octet-stream"
    return REPO_BLOB_CONTENT_TYPES.get(
        name.rsplit(".", 1)[-1], "application/octet-stream")


def repo_blob_filename(path):
    name = str(path or "").rsplit("/", 1)[-1].strip() or "file"
    cleaned = re.sub(r"[^A-Za-z0-9._ -]+", "_", name).strip(" .")
    return (cleaned or "file")[:180]
