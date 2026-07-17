"""Catalog-record sanitization helpers (js-free sibling module).

These pure input-validation helpers -- string trimming, path-segment
validation, and the public catalog-record builder -- were split out of the
11k-line entry.py so the sanitization layer is one small, scannable module.
The Worker runtime bundles sibling modules in src/, so entry.py's re-import
resolves both on Cloudflare and in the test suite (which parses this file
the same way it parses entry.py).
"""

import re
from urllib.parse import unquote

MAX_REPO_SEGMENT = 80
ROOM_NAME_RE = re.compile(r"^[A-Za-z0-9._:-]+$")


def safe_segment(value, max_length=MAX_REPO_SEGMENT):
    value = unquote(value).strip()
    if not value or len(value) > max_length:
        return None
    if not ROOM_NAME_RE.match(value):
        return None
    return value


def clean_string(value, max_length=240):
    if not isinstance(value, str):
        return ""
    return value.strip()[:max_length]


def safe_contribution_transport(data):
    """Return bounded optional snapshot fields without adding them to a record."""
    if not isinstance(data, dict):
        return {"present": False, "payload": "", "signature": "", "warning": ""}
    present = "contributionPayload" in data or "contributionSig" in data
    if not present:
        return {"present": False, "payload": "", "signature": "", "warning": ""}
    payload = data.get("contributionPayload")
    signature = data.get("contributionSig")
    if not isinstance(payload, str) or not isinstance(signature, str):
        return {
            "present": True,
            "payload": "",
            "signature": "",
            "warning": "invalid_contribution_transport",
        }
    if len(payload) > 64 * 1024:
        return {
            "present": True,
            "payload": "",
            "signature": "",
            "warning": "contribution_payload_too_large",
        }
    if not payload or not signature or len(signature) > 200:
        return {
            "present": True,
            "payload": "",
            "signature": "",
            "warning": "invalid_contribution_transport",
        }
    return {
        "present": True,
        "payload": payload,
        "signature": signature,
        "warning": "",
    }


def clean_int_series(value, length=52, max_value=1000000):
    if not isinstance(value, list):
        return [0] * length
    series = []
    for item in value[-length:]:
        try:
            number = int(item)
        except (TypeError, ValueError):
            number = 0
        series.append(max(0, min(number, max_value)))
    return ([0] * max(0, length - len(series))) + series


def safe_catalog_record(data):
    if not isinstance(data, dict):
        return None

    owner = safe_segment(data.get("owner", ""))
    name = safe_segment(data.get("name", ""))
    public_key = clean_string(data.get("maintainer", ""), 120)
    if not owner or not name or not public_key:
        return None

    now = clean_string(data.get("updatedAt", ""), 32)
    # Visibility: anything other than the literal "private" is treated as public,
    # so an absent/garbled field can never accidentally hide a repo.
    visibility = "private" if data.get("visibility") == "private" else "public"
    # On-disk mirror size (bytes) the publishing node reports. Clamped to a sane
    # non-negative integer; 0 when absent or unparseable. Drives the size figures
    # and "data hosted" leaderboards on the network page.
    try:
        size_bytes = max(0, min(int(data.get("sizeBytes", 0) or 0), 1 << 50))
    except (TypeError, ValueError):
        size_bytes = 0
    return {
        "owner": owner,
        "name": name,
        "visibility": visibility,
        "sizeBytes": size_bytes,
        "description": clean_string(data.get("description", ""), 240),
        "cloneUrl": clean_string(data.get("cloneUrl", ""), 2048),
        "solana": clean_string(data.get("solana", ""), 64),
        "channel": clean_string(data.get("channel", f"#{owner}-{name}"), 120),
        "hostedSince": clean_string(data.get("hostedSince", ""), 32),
        "lastSync": clean_string(data.get("lastSync", ""), 32),
        "updatedAt": now,
        # Stable identity (first/root commit) shared by every mirror of this repo,
        # so the network page can group mirrors under different owners into one
        # card. Falls back to the repo name on the website when absent.
        "rootCommit": clean_string(data.get("rootCommit", ""), 64),
        "source": clean_string(data.get("source", "local-node"), 40),
        # Node facts mirrored from the publishing node's live advert, so the Mirror
        # nodes view can show latest commit / issues / platform / version / id for a
        # node even while it's offline (adhoc #56). Point-in-time, like sizeBytes.
        "commit": clean_string(data.get("commit", ""), 64),
        "branch": clean_string(data.get("branch", ""), 120),
        "issueCount": clean_string(data.get("issueCount", ""), 12),
        # Highest issue number ever assigned (closed/deleted included), so the
        # relay can propose the desktop's real next number for a ForkBot issue
        # (see _forkbot_next_issue_number). Distinct from issueCount, which is
        # only the open count.
        "issueMaxNumber": clean_string(data.get("issueMaxNumber", ""), 12),
        "commitCount": clean_string(data.get("commitCount", ""), 12),
        "branchCount": clean_string(data.get("branchCount", ""), 12),
        "pullCount": clean_string(data.get("pullCount", ""), 12),
        "discussionCount": clean_string(data.get("discussionCount", ""), 12),
        "activityWeeks": clean_int_series(data.get("activityWeeks"), 52),
        "worktreeCount": clean_string(data.get("worktreeCount", ""), 12),
        "artifactCount": clean_string(data.get("artifactCount", ""), 12),
        "platform": clean_string(data.get("platform", ""), 16),
        "version": clean_string(data.get("version", ""), 32),
        "nodeId": clean_string(data.get("nodeId", ""), 64),
        # How many clones and website (browse/fetch) requests this node has served
        # for the repo. Purely local counters otherwise, mirrored here so the Mirror
        # nodes view can show a node's contribution even while it's offline.
        "clonesServed": clean_string(data.get("clonesServed", ""), 12),
        "websiteServed": clean_string(data.get("websiteServed", ""), 12),
        "maintainer": public_key,
        "signature": clean_string(data.get("signature", ""), 220),
        # Owner-signed fingerprint of the repo's served refs (sha256 over the
        # canonical heads+tags advertisement; see advertised_refs_canonical). The
        # relay pins this and refuses to serve any mirror whose live advertisement
        # doesn't hash to it — so a tampered or rolled-back mirror can't be cloned.
        "stateHash": clean_string(data.get("stateHash", ""), 64),
        "stateSig": clean_string(data.get("stateSig", ""), 220),
    }
