#!/usr/bin/env bash
# Publish prebuilt release binaries WITHOUT committing them into git (issue #304;
# see docs/design/release-binary-publishing.md).
#
# For each binary you pass, this:
#   1. computes its sha256 (its content address),
#   2. copies the bytes into the node's content-addressed release store (the CAS,
#      co-located with the served mirror — NEVER committed to git), and
#   3. records the asset in the channel's release metadata:
#        releases/<channel>/SHASUMS256.txt   (sha256sum -c compatible)
#        releases/<channel>/release.json     (manifest: tag, commit, assets)
#
# Commit the releases/<channel>/ metadata changes (small, text) the usual way;
# install.sh reads SHASUMS256.txt over the git proxy, then downloads each binary
# from the relay's content-addressed endpoint and verifies the sha256.
#
# Usage:
#   tools/forkmesh-release-publish.sh [--channel latest] [--tag vX.Y.Z] \
#       [--cas-dir DIR] [--repo owner/repo] BINARY [BINARY ...]
#
#   --channel   Release channel directory under releases/ (default: latest, or
#               $RELEASE_CHANNEL).
#   --tag       Immutable git tag this release is cut from (default: $FORKMESH_TAG
#               or the current `git describe --tags`).
#   --cas-dir   Where to write blob bytes — point this at the serving node's
#               <mirror>/forkmesh-releases directory. Defaults to
#               $FORKMESH_RELEASE_CAS, else .forkmesh/release-blobs (the node that
#               serves the repo must read its CAS from this path).
#   --repo      owner/repo for the manifest (default: $FORKMESH_REPO).
#
# Each BINARY should already be named the way install.sh resolves per platform:
#   forkmesh-<os>-<arch>[.exe]   (os: linux|macos|windows, arch: x86_64|arm64)
set -euo pipefail

channel="${RELEASE_CHANNEL:-latest}"
tag="${FORKMESH_TAG:-}"
cas_dir="${FORKMESH_RELEASE_CAS:-}"
repo="${FORKMESH_REPO:-}"
bins=()

while [ $# -gt 0 ]; do
  case "$1" in
    --channel) channel="$2"; shift 2 ;;
    --tag)     tag="$2"; shift 2 ;;
    --cas-dir) cas_dir="$2"; shift 2 ;;
    --repo)    repo="$2"; shift 2 ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    --) shift; while [ $# -gt 0 ]; do bins+=("$1"); shift; done ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  bins+=("$1"); shift ;;
  esac
done

[ "${#bins[@]}" -gt 0 ] || { echo "Error: no binaries given." >&2; exit 2; }

if [ -z "$tag" ]; then
  tag="$(git describe --tags --exact-match 2>/dev/null || git describe --tags 2>/dev/null || echo "")"
fi
[ -n "$cas_dir" ] || cas_dir=".forkmesh/release-blobs"

tag_commit="$(git rev-parse HEAD 2>/dev/null || echo "")"
meta_dir="releases/${channel}"
mkdir -p "$meta_dir" "$cas_dir"

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  elif command -v shasum  >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
  else echo "Error: need sha256sum or shasum." >&2; exit 1; fi
}

# Derive os/arch from a forkmesh-<os>-<arch>[.exe] asset name (best effort).
asset_os=""; asset_arch=""
detect_os_arch() {
  local n="${1%.exe}"
  asset_arch="${n##*-}"; n="${n%-*}"; asset_os="${n##*-}"
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

  # Content-addressed store: sha256/<aa>/<full>/data. Idempotent — re-publishing
  # identical bytes is a no-op (the blob already exists under its hash).
  blob="${cas_dir}/sha256/${hash:0:2}/${hash}/data"
  mkdir -p "$(dirname "$blob")"
  [ -f "$blob" ] || cp "$bin" "$blob"

  printf '%s  %s\n' "$hash" "$name" >> "${sums_file}.tmp"
  assets_json="${assets_json}${assets_json:+,}$(printf '{"name":"%s","blob_sha256":"%s","size":%s,"os":"%s","arch":"%s"}' \
    "$name" "$hash" "$size" "$asset_os" "$asset_arch")"
  echo "Staged $name ($size bytes) -> ${blob}"
done

# SHASUMS256.txt sorted by filename so the committed manifest is stable.
sort -k2 "${sums_file}.tmp" > "$sums_file"
rm -f "${sums_file}.tmp"

# release.json: the manifest install.sh / the release page can read, and the
# canonical body a publisher would later Ed25519-sign (signing is optional and
# layered on top — see the design doc §5).
{
  printf '{\n'
  printf '  "schema": "forkmesh-release-v1",\n'
  printf '  "repo": "%s",\n' "$repo"
  printf '  "tag": "%s",\n' "$tag"
  printf '  "tag_commit": "%s",\n' "$tag_commit"
  printf '  "channel": "%s",\n' "$channel"
  printf '  "created_at": %s,\n' "$(date +%s)000"
  printf '  "assets": [%s]\n' "$assets_json"
  printf '}\n'
} > "${meta_dir}/release.json"

echo
echo "Wrote ${meta_dir}/SHASUMS256.txt and ${meta_dir}/release.json"
echo "Blobs staged in: ${cas_dir} (NOT committed; served by the node over the relay)"
echo "Next: commit the releases/${channel}/ metadata, then publish it."
