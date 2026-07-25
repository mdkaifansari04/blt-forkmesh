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


def clean_optional_integer(value, max_value):
    """Return a bounded integer metric, or None when it was not shared."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if isinstance(value, float) and (
            value != value or value in (float("inf"), float("-inf"))
            or not value.is_integer()):
        return None
    if value < 0:
        return None
    return min(int(value), max_value)


def clean_optional_usage(used_value, total_value):
    """Normalize a used/total byte pair without manufacturing partial data."""
    maximum = 1 << 50
    used = clean_optional_integer(used_value, maximum)
    total = clean_optional_integer(total_value, maximum)
    if used is None or total is None or total <= 0:
        return None, None
    return min(used, total), total


def clean_logo_metadata(value):
    """Keep only bounded, content-free inputs for native logo generation."""
    value = value if isinstance(value, dict) else {}

    def labels(name, limit, max_length):
        raw = value.get(name)
        if not isinstance(raw, list):
            return []
        output = []
        for item in raw:
            label = clean_string(item, max_length)
            if label:
                output.append(label)
            if len(output) >= limit:
                break
        return output

    languages = {}
    raw_languages = value.get("languages")
    if isinstance(raw_languages, dict):
        for raw_name, raw_bytes in raw_languages.items():
            name = clean_string(raw_name, 80)
            if not name:
                continue
            try:
                byte_count = int(raw_bytes)
            except (TypeError, ValueError):
                byte_count = 0
            languages[name] = max(0, min(byte_count, 1 << 50))
            if len(languages) >= 12:
                break

    return {
        "description": clean_string(value.get("description", ""), 500),
        "languages": languages,
        "topics": labels("topics", 12, 80),
        "fileStructure": labels("fileStructure", 24, 120),
        "frameworks": labels("frameworks", 12, 80),
        "projectCategory": clean_string(
            value.get("projectCategory", ""), 80),
    }


def safe_catalog_record(data):
    if not isinstance(data, dict):
        return None

    owner = safe_segment(data.get("owner", ""))
    name = safe_segment(data.get("name", ""))
    public_key = clean_string(data.get("maintainer", ""), 120)
    if not owner or not name or not public_key:
        return None

    now = clean_string(data.get("updatedAt", ""), 32)
    # Visibility fails closed. Older/malformed publishers that omit the field
    # may hide a public repository until they republish, but they can never make
    # a private repository discoverable by accident.
    visibility = (
        "public" if data.get("visibility") == "public" else "private"
    )
    # On-disk mirror size (bytes) the publishing node reports. Clamped to a sane
    # non-negative integer; 0 when absent or unparseable. Drives the size figures
    # and "data hosted" leaderboards on the network page.
    try:
        size_bytes = max(0, min(int(data.get("sizeBytes", 0) or 0), 1 << 50))
    except (TypeError, ValueError):
        size_bytes = 0
    try:
        key_epoch = max(0, min(int(data.get("keyEpoch", 0) or 0), 1 << 31))
    except (TypeError, ValueError):
        key_epoch = 0
    solana = clean_string(data.get("solana", ""), 64)
    if not re.fullmatch(r"[1-9A-HJ-NP-Za-km-z]{32,44}", solana):
        solana = ""
    mem_used, mem_total = clean_optional_usage(
        data.get("memUsedBytes"), data.get("memTotalBytes"))
    disk_used, disk_total = clean_optional_usage(
        data.get("diskUsedBytes"), data.get("diskTotalBytes"))
    actions_fields = {"actionsEnabled", "actionsState"}.intersection(data)
    if actions_fields and actions_fields != {"actionsEnabled", "actionsState"}:
        return None
    actions_enabled = data.get("actionsEnabled")
    actions_state = data.get("actionsState")
    if actions_fields and (
        not isinstance(actions_enabled, bool)
        or (
            (not actions_enabled and actions_state != "disabled")
            or (actions_enabled and actions_state not in {"enabled", "running"})
        )
    ):
        return None
    record = {
        "owner": owner,
        "name": name,
        "visibility": visibility,
        "mirrorEncryption": (
            "owner-sealed-v1"
            if data.get("mirrorEncryption") == "owner-sealed-v1" else ""),
        "opaqueRepoId": clean_string(data.get("opaqueRepoId", ""), 64).lower(),
        "keyEpoch": key_epoch,
        "encryptedManifestHash": clean_string(
            data.get("encryptedManifestHash", ""), 64).lower(),
        "encryptedManifestSig": clean_string(
            data.get("encryptedManifestSig", ""), 220),
        "sizeBytes": size_bytes,
        "description": clean_string(data.get("description", ""), 240),
        "logoMetadata": clean_logo_metadata(data.get("logoMetadata")),
        "cloneUrl": clean_string(data.get("cloneUrl", ""), 2048),
        "solana": solana,
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
    # Keep this pair absent on legacy records so their catalog-v2 signatures
    # still verify. New reports are a strict, signed capability/status only:
    # arbitrary Actions configuration never reaches the stored public record.
    if actions_fields:
        record["actionsEnabled"] = actions_enabled
        record["actionsState"] = actions_state
    # Keep absent telemetry absent (rather than adding null fields) so a
    # catalog-v2 signature produced by an older, opted-out client continues to
    # verify after this schema extension. Consumers still expose unknown values
    # as null in their response shape.
    cpu_percent = clean_optional_integer(data.get("cpuPercent"), 100)
    if cpu_percent is not None:
        record["cpuPercent"] = cpu_percent
    if mem_total is not None:
        record["memUsedBytes"] = mem_used
        record["memTotalBytes"] = mem_total
    if disk_total is not None:
        record["diskUsedBytes"] = disk_used
        record["diskTotalBytes"] = disk_total
    return record
