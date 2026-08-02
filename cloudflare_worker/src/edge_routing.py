"""Pure policy for masked HTTPS routing to independently operated mirrors.

The public Worker remains the URL origin.  It selects an attested, recently
healthy endpoint and streams the endpoint's response in place; repository
bytes are never placed in D1 and the selected hostname is never returned to
the browser.

Cryptographic signature verification and network I/O stay in ``entry.py``.
This module validates records and constructs the canonical messages those
runtime operations verify.
"""

import hashlib
import json
import ipaddress
import re
from urllib.parse import quote, urlencode, urlparse, urlunparse


# Endpoint registration is a coarse availability lease.  Hosts renew it every
# four minutes and the bounded renewal unit may spend up to five minutes
# validating a large encrypted mirror or waiting on the control plane.  Keep
# that healthy path inside this ten-minute lease.  Repository traffic is still
# fail-closed: entry.py independently requires a fresh node-signed repository
# proof and caches it for at most 60 seconds before every routed operation.
ENDPOINT_STALE_MS = 10 * 60 * 1000
MAX_FAILOVER_ATTEMPTS = 4
NODE_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
REPO_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
OPAQUE_REPLICA_RE = re.compile(r"^[0-9a-f]{64}$")
PUBLIC_OPERATIONS = frozenset({
    "git-info-refs", "git-upload-pack", "tree", "blobs", "blob", "raw", "history",
    "commit", "compare", "branches", "search", "stats", "sizes", "release-blob",
    "merge-pull", "actions-status",
})


def cloudflare_proxied_dns_answers(payloads, cidr_documents):
    """Return True only when DoH resolved exclusively to Cloudflare ranges.

    ``payloads`` are Cloudflare DNS-over-HTTPS JSON responses for A/AAAA.
    ``cidr_documents`` are the independently fetched official Cloudflare
    IPv4/IPv6 range documents. CNAME records are ignored; at least one address
    record is required and every returned address must be within a published
    Cloudflare network. Malformed input fails closed.
    """
    networks = []
    try:
        for document in cidr_documents:
            for token in str(document or "").split():
                networks.append(ipaddress.ip_network(token, strict=True))
    except ValueError:
        return False
    if not networks:
        return False
    addresses = []
    for payload in payloads:
        if not isinstance(payload, dict):
            return False
        try:
            status = int(payload.get("Status"))
        except (TypeError, ValueError):
            return False
        if status != 0:
            return False
        answers = payload.get("Answer", [])
        if not isinstance(answers, list):
            return False
        for answer in answers:
            if not isinstance(answer, dict):
                return False
            try:
                record_type = int(answer.get("type"))
            except (TypeError, ValueError):
                continue
            if record_type not in (1, 28):
                continue
            try:
                addresses.append(ipaddress.ip_address(
                    str(answer.get("data") or "").strip()))
            except ValueError:
                return False
    if not addresses:
        return False
    return all(
        any(
            address.version == network.version and address in network
            for network in networks
        )
        for address in addresses
    )


def _public_hostname(hostname):
    hostname = str(hostname or "").strip().lower().rstrip(".")
    if not hostname or hostname == "localhost" or hostname.endswith(".localhost"):
        return False
    if hostname.endswith((".local", ".internal", ".home", ".lan")):
        return False
    try:
        address = ipaddress.ip_address(hostname.strip("[]"))
    except ValueError:
        # A DNS name needs at least one dot. Cloudflare-managed mirror names
        # satisfy this and it avoids accidentally targeting an internal label.
        return "." in hostname
    return bool(address.is_global)


def normalize_base_url(value):
    """Return a safe public HTTPS endpoint base URL or an empty string."""
    try:
        parsed = urlparse(str(value or "").strip())
    except Exception:
        return ""
    if parsed.scheme != "https" or not parsed.hostname:
        return ""
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        return ""
    if parsed.port not in (None, 443) or not _public_hostname(parsed.hostname):
        return ""
    path = "/" + "/".join(
        quote(part, safe="-._~") for part in parsed.path.split("/") if part
    )
    path = "" if path == "/" else path.rstrip("/")
    return urlunparse(("https", parsed.hostname.lower(), path, "", "", ""))


def registration_message(node, base_url, public_key, issued_at):
    """Canonical record a node identity key signs before registration."""
    node = str(node or "").strip().lower()
    base_url = normalize_base_url(base_url)
    public_key = str(public_key or "").strip()
    try:
        issued_at = int(issued_at)
    except (TypeError, ValueError):
        return ""
    if (
        not NODE_RE.fullmatch(node)
        or not base_url
        or not public_key
        or issued_at <= 0
    ):
        return ""
    return "\n".join((
        "forkmesh-https-endpoint-v1",
        node,
        base_url,
        public_key,
        str(issued_at),
    ))


def health_challenge(node, nonce, issued_at):
    node = str(node or "").strip().lower()
    nonce = str(nonce or "").strip()
    try:
        issued_at = int(issued_at)
    except (TypeError, ValueError):
        return ""
    if not NODE_RE.fullmatch(node) or len(nonce) < 16 or issued_at <= 0:
        return ""
    return "\n".join((
        "forkmesh-https-health-v1", node, nonce[:128], str(issued_at),
    ))


def repository_health_challenge(
        node, nonce, issued_at, owner, repository, available, integrity,
        refs_sha256, operations_sha256):
    """Canonical signed availability proof for one explicitly named repo."""
    base = health_challenge(node, nonce, issued_at)
    owner = str(owner or "").strip().lower()
    repository = str(repository or "").strip()
    integrity = str(integrity or "").strip().lower()
    refs_sha256 = str(refs_sha256 or "").strip().lower()
    operations_sha256 = str(operations_sha256 or "").strip().lower()
    if (
        not base
        or not NODE_RE.fullmatch(owner)
        or not REPO_RE.fullmatch(repository)
        or integrity not in ("ok", "unavailable")
        or not re.fullmatch(r"[0-9a-f]{64}", refs_sha256)
        or not re.fullmatch(r"[0-9a-f]{64}", operations_sha256)
        or (bool(available) != (integrity == "ok"))
    ):
        return ""
    return "\n".join((
        "forkmesh-https-health-repository-v1",
        str(node).strip().lower(),
        str(nonce).strip(),
        str(int(issued_at)),
        owner,
        repository,
        "1" if available else "0",
        integrity,
        refs_sha256,
        operations_sha256,
    ))


def operations_sha256(operations):
    """Digest the gateway's sorted, unique public operation projection."""
    if not isinstance(operations, (list, tuple)):
        return ""
    normalized = []
    for operation in operations:
        operation = str(operation or "").strip()
        if operation not in PUBLIC_OPERATIONS or operation in normalized:
            return ""
        normalized.append(operation)
    if normalized != sorted(normalized):
        return ""
    return hashlib.sha256("\n".join(normalized).encode("utf-8")).hexdigest()


def validate_tunnel_manifest(value, base_url, node, public_key):
    """Validate the secret-free signed-manifest shape.

    Signature verification stays in the Worker runtime.  The returned bytes
    are exactly what the node signs with ``forkmesh-json-sort-v1``.
    """
    if not isinstance(value, dict):
        return None
    base_url = normalize_base_url(base_url)
    node = str(node or "").strip().lower()
    public_key = str(public_key or "").strip()
    signature = value.get("signature")
    endpoint = value.get("endpoint")
    manifest_node = value.get("node")
    dns = value.get("dns")
    edge = value.get("edge")
    storage = value.get("storage")
    parsed = urlparse(base_url)
    top_fields = {
        "schemaVersion", "type", "generatedAt", "node", "endpoint", "dns",
        "edge", "storage", "signature",
    }
    endpoint_fields = {
        "origin", "healthUrl", "manifestUrl", "repositoryUrlTemplate",
        "transport", "mainProxyMode",
    }
    dns_fields = {"recordName", "recordType", "proxied", "target"}
    edge_fields = {"provider", "kind", "tunnelId", "originExposure"}
    storage_fields = {
        "repositoryBytesInD1", "repositoryByteOwner", "d1Purpose",
    }
    d1_purposes = storage.get("d1Purpose") if isinstance(storage, dict) else None
    if (
        not base_url
        or not NODE_RE.fullmatch(node)
        or not re.fullmatch(r"[A-Za-z0-9_-]{43}", public_key)
        or set(value) != top_fields
        or value.get("schemaVersion") != 1
        or value.get("type") != "forkmesh.mirror-endpoint"
        or not re.fullmatch(
            r"\d{4}-\d{2}-\d{2}T[0-9:.+-]+Z?",
            str(value.get("generatedAt") or ""))
        or not isinstance(signature, dict)
        or set(signature) != {
            "algorithm", "encoding", "canonicalization",
            "payloadSha256", "value",
        }
        or signature.get("algorithm") != "Ed25519"
        or signature.get("encoding") != "base64url-no-padding"
        or signature.get("canonicalization") != "forkmesh-json-sort-v1"
        or not re.fullmatch(
            r"[0-9a-f]{64}", str(signature.get("payloadSha256") or ""))
        or not re.fullmatch(
            r"[A-Za-z0-9_-]{86}", str(signature.get("value") or ""))
        or not isinstance(manifest_node, dict)
        or manifest_node != {"name": node, "publicKey": public_key}
        or not isinstance(endpoint, dict)
        or set(endpoint) != endpoint_fields
        or endpoint.get("origin") != base_url
        or endpoint.get("healthUrl") != base_url + "/health"
        or endpoint.get("manifestUrl") != base_url + "/forkmesh-mirror.json"
        or endpoint.get("repositoryUrlTemplate") != (
            base_url
            + "/v1/repositories/{owner}/{repository}/{operation}")
        or endpoint.get("transport") != "direct-https"
        or endpoint.get("mainProxyMode") != "masked"
        or not isinstance(dns, dict)
        or set(dns) != dns_fields
        or dns.get("recordName") != parsed.hostname
        or dns.get("recordType") != "CNAME"
        or dns.get("proxied") is not True
        or not isinstance(edge, dict)
        or set(edge) != edge_fields
        or edge.get("provider") != "cloudflare"
        or edge.get("kind") != "cloudflare-tunnel"
        or edge.get("originExposure") != "loopback-only"
        or not isinstance(storage, dict)
        or set(storage) != storage_fields
        or storage.get("repositoryBytesInD1") is not False
        or storage.get("repositoryByteOwner") != "independent-mirror-host"
        or not isinstance(d1_purposes, list)
        or not d1_purposes
        or len(d1_purposes) != len(set(d1_purposes))
        or not set(d1_purposes).issubset({
            "service-discovery", "health-metadata", "routing-metadata",
            "signed-manifests",
        })
    ):
        return None
    tunnel_id = str(edge.get("tunnelId") or "")
    if (
        not re.fullmatch(r"[A-Za-z0-9-]+", tunnel_id)
        or dns.get("target") != tunnel_id + ".cfargotunnel.com"
    ):
        return None
    unsigned = {key: item for key, item in value.items() if key != "signature"}
    payload = json.dumps(
        unsigned, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    if not hashlib.sha256(payload).hexdigest() == signature["payloadSha256"]:
        return None
    return {
        "payload": payload,
        "signature": signature["value"],
        "payloadSha256": signature["payloadSha256"],
    }


def request_message(node, method, path, body_sha256, request_id, issued_at):
    """Canonical per-request capability signed by the routing Worker."""
    node = str(node or "").strip().lower()
    method = str(method or "").strip().upper()
    path = str(path or "").strip()
    body_sha256 = str(body_sha256 or "").strip().lower()
    request_id = str(request_id or "").strip()
    try:
        issued_at = int(issued_at)
    except (TypeError, ValueError):
        return ""
    if (
        not NODE_RE.fullmatch(node)
        or method not in ("GET", "HEAD", "POST")
        or not path.startswith("/")
        or "\n" in path
        or not re.fullmatch(r"[0-9a-f]{64}", body_sha256)
        or not re.fullmatch(r"[A-Za-z0-9_-]{12,80}", request_id)
        or issued_at <= 0
    ):
        return ""
    return "\n".join((
        "forkmesh-masked-proxy-v1", node, method, path, body_sha256,
        request_id, str(issued_at),
    ))


def private_route_message(
        owner, repository, node, opaque_id, replica_sha256, key_epoch,
        active, issued_at):
    """Canonical owner authorization for one opaque private replica route."""
    owner = str(owner or "").strip().lower()
    repository = str(repository or "").strip()
    node = str(node or "").strip().lower()
    opaque_id = str(opaque_id or "").strip().lower()
    replica_sha256 = str(replica_sha256 or "").strip().lower()
    try:
        key_epoch = int(key_epoch)
        issued_at = int(issued_at)
    except (TypeError, ValueError):
        return ""
    if (
        not NODE_RE.fullmatch(owner)
        or not REPO_RE.fullmatch(repository)
        or not NODE_RE.fullmatch(node)
        or not OPAQUE_REPLICA_RE.fullmatch(opaque_id)
        or not re.fullmatch(r"[0-9a-f]{64}", replica_sha256)
        or key_epoch <= 0
        or issued_at <= 0
        or not isinstance(active, bool)
    ):
        return ""
    return "\n".join((
        "forkmesh-private-route-v1",
        owner,
        repository,
        node,
        opaque_id,
        replica_sha256,
        str(key_epoch),
        "1" if active else "0",
        str(issued_at),
    ))


def normalize_endpoint_record(value):
    """Normalize the public, secret-free endpoint health projection."""
    if not isinstance(value, dict):
        return None
    node = str(value.get("node") or "").strip().lower()
    base_url = normalize_base_url(value.get("baseUrl"))
    public_key = str(value.get("publicKey") or "").strip()
    signature = str(value.get("signature") or "").strip()
    if (
        not NODE_RE.fullmatch(node)
        or not base_url
        or not public_key
        or not signature
    ):
        return None
    try:
        issued_at = int(value.get("issuedAt") or 0)
        checked_at = int(value.get("checkedAt") or 0)
        latency_ms = max(0, min(60_000, int(value.get("latencyMs") or 0)))
    except (TypeError, ValueError):
        return None
    refs_sha256 = str(value.get("refsSha256") or "").strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", refs_sha256):
        refs_sha256 = ""
    return {
        "node": node,
        "baseUrl": base_url,
        "publicKey": public_key,
        "signature": signature,
        "issuedAt": issued_at,
        "checkedAt": checked_at,
        "latencyMs": latency_ms,
        "healthy": bool(value.get("healthy")),
        "integrity": str(value.get("integrity") or "unknown").lower(),
        "region": str(value.get("region") or "").strip().upper()[:8],
        "abuseBlocked": bool(value.get("abuseBlocked")),
        "refsSha256": refs_sha256,
    }


def endpoint_eligible(record, now, allowed_nodes=None):
    record = normalize_endpoint_record(record)
    if not record:
        return False
    try:
        now = int(now)
    except (TypeError, ValueError):
        return False
    if allowed_nodes is not None and record["node"] not in set(allowed_nodes):
        return False
    return bool(
        record["healthy"]
        and not record["abuseBlocked"]
        and record["integrity"] == "ok"
        and record["checkedAt"] > 0
        and 0 <= now - record["checkedAt"] <= ENDPOINT_STALE_MS
    )


def select_endpoints(records, now, preferred_region="", cursor=0):
    """Healthy endpoints ordered for latency/region and deterministic failover."""
    eligible = [
        normalize_endpoint_record(record)
        for record in (records or [])
        if endpoint_eligible(record, now)
    ]
    preferred_region = str(preferred_region or "").strip().upper()
    eligible.sort(key=lambda record: (
        0 if preferred_region and record["region"] == preferred_region else 1,
        record["latencyMs"] or 60_000,
        record["node"],
    ))
    if not eligible:
        return []
    cursor = int(cursor or 0) % len(eligible)
    rotated = eligible[cursor:] + eligible[:cursor]
    return rotated[:MAX_FAILOVER_ATTEMPTS]


def equivalent_current_endpoint_nodes(records, current_nodes, current_pins):
    """Expand current preference to endpoints with identical signed refs."""
    current_nodes = {
        str(node or "").strip().lower() for node in (current_nodes or set())
    }
    current_pins = {
        str(pin or "").strip().lower() for pin in (current_pins or set())
    }
    current_refs = {
        record.get("refsSha256", "")
        for record in (records or [])
        if record.get("refsSha256")
        and (
            record.get("node") in current_nodes
            or record.get("refsSha256") in current_pins
        )
    }
    if not current_refs:
        return current_nodes
    return current_nodes.union({
        record.get("node")
        for record in (records or [])
        if record.get("node") and record.get("refsSha256") in current_refs
    })


def masked_target_url(base_url, owner, repo, operation, query=None):
    """Build an internal endpoint URL; callers must never return it to clients."""
    base_url = normalize_base_url(base_url)
    owner = str(owner or "").strip().lower()
    repo = str(repo or "").strip()
    operation = str(operation or "").strip()
    if (
        not base_url
        or not NODE_RE.fullmatch(owner)
        or not REPO_RE.fullmatch(repo)
        or operation not in PUBLIC_OPERATIONS
    ):
        return ""
    path = "/v1/repositories/%s/%s/%s" % (
        quote(owner, safe="-._~"),
        quote(repo, safe="-._~"),
        quote(operation, safe="-"),
    )
    safe_query = {}
    for key in ("path", "ref", "base", "head", "service", "sha256"):
        if key in (query or {}):
            raw = (query or {}).get(key)
            values = raw if isinstance(raw, (list, tuple)) else [raw]
            clean = []
            for value in values[:60]:
                value = str(value or "")[:500]
                if "\x00" not in value and "\r" not in value and "\n" not in value:
                    clean.append(value)
            if clean:
                safe_query[key] = clean if len(clean) > 1 else clean[0]
    return base_url + path + (
        ("?" + urlencode(safe_query, doseq=True)) if safe_query else "")


def masked_private_replica_url(base_url, opaque_id):
    """Build an internal ciphertext URL without a repository identity."""
    base_url = normalize_base_url(base_url)
    opaque_id = str(opaque_id or "").strip().lower()
    if not base_url or not OPAQUE_REPLICA_RE.fullmatch(opaque_id):
        return ""
    return base_url + "/v1/private-replicas/" + opaque_id


def response_headers(headers):
    """Allowlist endpoint response headers without leaking its identity/cookies."""
    headers = {
        str(key).lower(): str(value)
        for key, value in (headers or {}).items()
    }
    allowed = (
        "content-type", "content-length", "content-range", "accept-ranges",
        "etag", "last-modified", "cache-control", "content-disposition",
    )
    output = {key: headers[key] for key in allowed if key in headers}
    output["x-content-type-options"] = "nosniff"
    return output


def empty_body_sha256():
    return hashlib.sha256(b"").hexdigest()
