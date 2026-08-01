#!/usr/bin/env bash

















































set -euo pipefail

channel="${RELEASE_CHANNEL:-latest}"
tag="${FORKMESH_TAG:-}"
cas_dir="${FORKMESH_RELEASE_CAS:-}"
repo="${FORKMESH_REPO:-forkmesh/forkmesh}"
tag_commit="${FORKMESH_TAG_COMMIT:-}"
build_commit="${FORKMESH_BUILD_COMMIT:-}"
signing_key="${FORKMESH_RELEASE_SIGNING_KEY:-}"
bins=()

while [ $# -gt 0 ]; do
  case "$1" in
    --channel) channel="$2"; shift 2 ;;
    --tag)     tag="$2"; shift 2 ;;
    --tag-commit) tag_commit="$2"; shift 2 ;;
    --build-commit) build_commit="$2"; shift 2 ;;
    --cas-dir) cas_dir="$2"; shift 2 ;;
    --repo)    repo="$2"; shift 2 ;;
    --signing-key) signing_key="$2"; shift 2 ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    --) shift; while [ $# -gt 0 ]; do bins+=("$1"); shift; done ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  bins+=("$1"); shift ;;
  esac
done

[ "${#bins[@]}" -gt 0 ] || { echo "Error: no binaries given." >&2; exit 2; }

if [ -z "$tag" ]; then
  tag="$(
    git describe --tags --exact-match 2>/dev/null ||
      git describe --tags --abbrev=0 2>/dev/null ||
      echo ""
  )"
fi
[ -n "$tag" ] || {
  echo "Error: --tag must name an existing release tag." >&2
  exit 2
}
[ -n "$cas_dir" ] || cas_dir=".forkmesh/release-blobs"

resolved_tag_commit=""
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  resolved_tag_commit="$(
    git rev-parse --verify "refs/tags/${tag}^{commit}" 2>/dev/null || true
  )"
  if [ -z "$resolved_tag_commit" ]; then
    echo "Error: release tag '$tag' does not resolve through refs/tags/ to a commit." >&2
    exit 2
  fi
  resolved_tag_commit="$(
    printf '%s' "$resolved_tag_commit" | tr 'A-F' 'a-f'
  )"
  if [ -n "$tag_commit" ] &&
     [ "$(printf '%s' "$tag_commit" | tr 'A-F' 'a-f')" != "$resolved_tag_commit" ]; then
    echo "Error: --tag-commit does not match the peeled target of refs/tags/$tag." >&2
    exit 2
  fi
  tag_commit="$resolved_tag_commit"
fi
[ -n "$build_commit" ] ||
  build_commit="$(git log -1 --format=%H -- . \
    ':(exclude).forkmesh/releases/**' 2>/dev/null || echo "")"
tag_commit="$(printf '%s' "$tag_commit" | tr 'A-F' 'a-f')"
build_commit="$(printf '%s' "$build_commit" | tr 'A-F' 'a-f')"
if ! printf '%s' "$tag_commit" |
    grep -Eq '^([0-9a-f]{40}|[0-9a-f]{64})$'; then
  echo "Error: --tag-commit must identify the tag's exact 40- or 64-hex Git commit." >&2
  exit 2
fi
if ! printf '%s' "$build_commit" |
    grep -Eq '^([0-9a-f]{40}|[0-9a-f]{64})$'; then
  echo "Error: --build-commit must identify the binary's exact 40- or 64-hex Git commit." >&2
  exit 2
fi
[ -n "$signing_key" ] || {
  echo "Error: --signing-key (or FORKMESH_RELEASE_SIGNING_KEY) is required." >&2
  exit 2
}
[ -f "$signing_key" ] && [ ! -L "$signing_key" ] || {
  echo "Error: release signing key must be a non-symlink regular file." >&2
  exit 2
}
signing_key_uid="$(
  stat -c '%u' "$signing_key" 2>/dev/null ||
    stat -f '%u' "$signing_key" 2>/dev/null || true
)"
[ "$signing_key_uid" = "$(id -u)" ] || {
  echo "Error: release signing key must be owned by the publishing user." >&2
  exit 2
}
signing_key_mode="$(
  stat -c '%a' "$signing_key" 2>/dev/null ||
    stat -f '%Lp' "$signing_key" 2>/dev/null || true
)"
case "$signing_key_mode" in
  *00) ;;
  *) echo "Error: release signing key must not be group/world accessible (mode 0600 or stricter)." >&2
     exit 2 ;;
esac
command -v openssl >/dev/null 2>&1 || {
  echo "Error: OpenSSL is required to sign release metadata." >&2
  exit 2
}
meta_dir=".forkmesh/releases/${channel}"
mkdir -p "$meta_dir" "$cas_dir"

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  elif command -v shasum  >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
  else echo "Error: need sha256sum or shasum." >&2; exit 1; fi
}


asset_os=""; asset_arch=""
detect_os_arch() {
  local n="${1%.exe}"
  asset_arch="${n##*-}"; n="${n%-*}"; asset_os="${n##*-}"
}




json_escape() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  s="${s//$'\n'/\\n}"
  s="${s//$'\r'/\\r}"
  s="${s//$'\t'/\\t}"
  printf '%s' "$s"
}

sums_file="${meta_dir}/SHASUMS256.txt"
: > "${sums_file}.tmp"
assets_json=""

for bin in "${bins[@]}"; do
  [ -f "$bin" ] || { echo "Error: not a file: $bin" >&2; exit 1; }
  name="$(basename "$bin")"
  hash="$(sha256_of "$bin")"
  size="$(wc -c < "$bin" | tr -d ' ')"
  detect_os_arch "$name"



  blob="${cas_dir}/sha256/${hash:0:2}/${hash}/data"
  mkdir -p "$(dirname "$blob")"
  [ -f "$blob" ] || cp "$bin" "$blob"

  printf '%s  %s\n' "$hash" "$name" >> "${sums_file}.tmp"
  assets_json="${assets_json}${assets_json:+,}$(printf '{"name":"%s","blob_sha256":"%s","size":%s,"os":"%s","arch":"%s"}' \
    "$(json_escape "$name")" "$hash" "$size" \
    "$(json_escape "$asset_os")" "$(json_escape "$asset_arch")")"
  echo "Staged $name ($size bytes) -> ${blob}"
done


sort -k2 "${sums_file}.tmp" > "$sums_file"
rm -f "${sums_file}.tmp"
sums_hash="$(sha256_of "$sums_file")"




{
  printf '{\n'
  printf '  "schema": "forkmesh-release-v2",\n'
  printf '  "repo": "%s",\n' "$(json_escape "$repo")"
  printf '  "tag": "%s",\n' "$(json_escape "$tag")"
  printf '  "tag_commit": "%s",\n' "$(json_escape "$tag_commit")"
  printf '  "build_commit": "%s",\n' "$(json_escape "$build_commit")"
  printf '  "channel": "%s",\n' "$(json_escape "$channel")"
  printf '  "created_at": %s,\n' "$(date +%s)000"
  printf '  "checksums_sha256": "%s",\n' "$sums_hash"
  printf '  "assets": [%s]\n' "$assets_json"
  printf '}\n'
} > "${meta_dir}/release.json"



signature="${meta_dir}/release.json.sig"
if ! openssl pkeyutl -sign -rawin -inkey "$signing_key" \
     -in "${meta_dir}/release.json" -out "${signature}.tmp" 2>/dev/null; then
  rm -f "${signature}.tmp"
  echo "Error: could not Ed25519-sign release.json with the configured key." >&2
  exit 1
fi
[ "$(wc -c < "${signature}.tmp" | tr -d ' ')" = "64" ] || {
  rm -f "${signature}.tmp"
  echo "Error: release signer did not produce a 64-byte Ed25519 signature." >&2
  exit 1
}
mv -f "${signature}.tmp" "$signature"

echo
echo "Wrote ${meta_dir}/SHASUMS256.txt, release.json, and release.json.sig"
echo "Blobs staged in: ${cas_dir} (NOT committed; served by the node over the relay)"
echo "Next: commit the .forkmesh/releases/${channel}/ metadata, then publish it."
