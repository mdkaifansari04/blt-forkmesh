"""Release manifest + content-addressed blob helpers for the ForkMesh relay.

Pure, stdlib-only helpers for the signed-release flow (issue #304): tag/asset
name validation, the content-addressed blob path layout, the canonical signable
release-manifest body, the release signing message, `SHASUMS256.txt` generation,
and semver ordering for the `latest` alias.

Split out of ``entry.py`` — like ``urls.py`` and ``solana.py`` these depend only
on ``re``/stdlib and hold no runtime state or js imports, so the module loads
standalone (the test suite parses it the same way it parses ``entry.py``). The
one async orchestrator, ``verify_release_manifest``, stays in ``entry.py``
because it reaches back into that module's Ed25519/sha256 crypto helpers; it
imports ``release_manifest_content`` + ``release_signing_message`` from here, so
the dependency stays one-directional (``entry`` imports from here, never the
reverse).
"""

import re

RELEASE_TAG_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$")

SHA256_HEX_RE = re.compile(r"^[0-9a-f]{64}$")
GIT_COMMIT_RE = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")

RELEASE_SEMVER_RE = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:[-.](.+))?$")


def valid_release_tag(value):
    value = (value or "").strip()
    return (bool(value) and ".." not in value and
            bool(RELEASE_TAG_RE.match(value)))


def valid_asset_name(value):


    value = value or ""
    if not (1 <= len(value) <= 255) or value in (".", ".."):
        return False
    if "/" in value or "\\" in value or "\x00" in value:
        return False
    return not any(ord(ch) < 0x20 for ch in value)


def valid_sha256_hex(value):
    return bool(SHA256_HEX_RE.match((value or "").strip().lower()))


def valid_git_commit(value):
    return bool(GIT_COMMIT_RE.match((value or "").strip().lower()))


def cas_blob_relpath(sha256_hex):



    h = (sha256_hex or "").strip().lower()
    if not SHA256_HEX_RE.match(h):
        return None
    return "sha256/%s/%s/data" % (h[:2], h)


def release_asset_line(asset):




    return "\x00".join([
        asset.get("name", "") or "",
        (asset.get("blob_sha256", "") or "").lower(),
        str(int(asset.get("size", 0) or 0)),
        asset.get("content_type", "") or "",
        asset.get("os", "") or "",
        asset.get("arch", "") or "",
        asset.get("label", "") or "",
    ])


def release_manifest_content(manifest):






    repo = manifest.get("repo", "") or ""
    tag = manifest.get("tag", "") or ""
    tag_commit = manifest.get("tag_commit", "") or ""
    build_commit = manifest.get("build_commit", "") or ""
    assets = manifest.get("assets") or []
    asset_lines = sorted(release_asset_line(a) for a in assets)



    header_fields = [repo, tag, tag_commit]
    if build_commit:
        header_fields.append(build_commit)
    header_fields.append(str(len(asset_lines)))
    header = "\x00".join(header_fields)
    return "\n".join([header] + asset_lines)


def release_signing_message(repo, tag, author, ts, content_hash):


    return (
        "forkmesh-release-v1\n" + repo + "\n" + tag + "\n" + author + "\n" +
        str(ts) + "\n" + content_hash
    ).encode()


def generate_shasums(assets):



    lines = []
    for asset in sorted(assets, key=lambda a: a.get("name", "") or ""):
        digest = (asset.get("blob_sha256", "") or "").lower()
        name = asset.get("name", "") or ""
        if SHA256_HEX_RE.match(digest) and name:
            lines.append("%s  %s" % (digest, name))
    return "".join(line + "\n" for line in lines)


def release_semver_key(tag):


    match = RELEASE_SEMVER_RE.match((tag or "").strip())
    if not match:
        return None
    major, minor, patch = (
        int(match.group(1)), int(match.group(2)), int(match.group(3)))
    pre = match.group(4)
    pre_rank = (1,) if pre is None else (0, pre)
    return (major, minor, patch, pre_rank)


def resolve_latest_release(releases):



    best = None
    best_key = None
    for release in releases:
        if release.get("draft"):
            continue
        if (release.get("state", "published") or "published") != "published":
            continue
        if release.get("prerelease"):
            continue
        key = release_semver_key(release.get("tag", ""))
        if key is None:
            continue
        if best_key is None or key > best_key:
            best_key, best = key, release
    return best


def asset_upload_decision(release_state, existing_asset, new_sha256):



    new_sha = (new_sha256 or "").lower()
    if existing_asset is None:
        return "create"
    existing_sha = (existing_asset.get("blob_sha256", "") or "").lower()
    if release_state == "draft":
        return "noop" if existing_sha == new_sha else "replace"
    return "noop" if existing_sha == new_sha else "conflict"
