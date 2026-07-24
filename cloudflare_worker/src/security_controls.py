"""Pure validation helpers for ForkMesh security and privacy controls.

The Worker imports this module, but it intentionally has no Workers/JavaScript
dependencies so the security boundary can be unit-tested with normal CPython.
Only generalized, allowlisted incident metadata may reach public/admin APIs.
Raw network identifiers and owner-sealed payload plaintext never belong here.
"""

import base64
from collections import deque
import hashlib
import json
import re
from urllib.parse import unquote


ROLE_VALUES = frozenset({
    "supporting_member",
    "repository_contributor",
    "repository_administrator",
    "mirror_operator",
    "organization_member",
    "organization_administrator",
    "moderator",
    "security_reviewer",
    "platform_administrator",
    "automated_agent",
})

PUBLIC_ROLE_VALUES = frozenset({
    "supporting_member",
    "repository_contributor",
    "repository_administrator",
    "mirror_operator",
    "organization_member",
    "organization_administrator",
    "automated_agent",
})

ROLE_SCOPE_TYPES = {
    "supporting_member": frozenset({"platform"}),
    "repository_contributor": frozenset({"repository"}),
    "repository_administrator": frozenset({"repository"}),
    "mirror_operator": frozenset({"platform", "repository"}),
    "organization_member": frozenset({"organization"}),
    "organization_administrator": frozenset({"organization"}),
    "moderator": frozenset({"platform", "organization", "repository"}),
    "security_reviewer": frozenset({"platform", "repository"}),
    "platform_administrator": frozenset({"platform"}),
    "automated_agent": frozenset({"platform", "repository"}),
}

RESTRICTION_REASONS = frozenset({
    "endpoint_probing",
    "credential_abuse",
    "path_traversal",
    "injection_attempt",
    "excessive_scraping",
    "denial_of_service",
    "exploit_signature",
    "access_control_bypass",
    "other_security_abuse",
})

RESTRICTION_CONFIDENCE = frozenset({"low", "medium", "high"})
RESTRICTION_STATUSES = frozenset({
    "monitoring", "quarantined", "blocked", "expired", "revoked",
})
APPEAL_STATUSES = frozenset({
    "none", "pending", "accepted", "denied", "needs_information",
})

AUTO_RESTRICTION_MAX_MS = 24 * 60 * 60 * 1000
MANUAL_RESTRICTION_MAX_MS = 365 * 24 * 60 * 60 * 1000
DEFAULT_QUARANTINE_MS = 15 * 60 * 1000
MAX_APPEAL_TEXT = 4000
MAX_EVIDENCE_SUMMARY = 500
MAX_OWNER_ENVELOPE_BODY = 1024 * 1024

_INCIDENT_RE = re.compile(r"^[a-f0-9]{32}$")
_TOKEN_RE = re.compile(r"^[a-f0-9]{64}$")
_KEY_ID_RE = re.compile(r"^[A-Za-z0-9_-]{16,128}$")
_B64URL_RE = re.compile(r"^[A-Za-z0-9_-]+$")

_AUDIT_FORBIDDEN_KEYS = (
    "secret", "token", "password", "authorization", "cookie", "private",
    "seed", "body", "content", "email", "ip", "wallet", "signature",
)


def normalize_role(value):
    role = str(value or "").strip().lower().replace("-", "_")
    return role if role in ROLE_VALUES else ""


def normalize_scope_type(value):
    scope = str(value or "platform").strip().lower()
    return scope if scope in ("platform", "organization", "repository") else ""


def role_scope_allowed(role, scope_type):
    role = normalize_role(role)
    scope_type = normalize_scope_type(scope_type)
    return bool(role and scope_type in ROLE_SCOPE_TYPES.get(role, ()))


def public_roles(values):
    roles = []
    for value in values or []:
        role = normalize_role(value)
        if role in PUBLIC_ROLE_VALUES and role not in roles:
            roles.append(role)
    return roles


def normalize_incident_id(value):
    incident = str(value or "").strip().lower()
    return incident if _INCIDENT_RE.fullmatch(incident) else ""


def normalize_subject_token(value):
    token = str(value or "").strip().lower()
    return token if _TOKEN_RE.fullmatch(token) else ""


def normalize_reason(value):
    reason = str(value or "").strip().lower().replace("-", "_")
    return reason if reason in RESTRICTION_REASONS else ""


def normalize_confidence(value):
    confidence = str(value or "").strip().lower()
    return confidence if confidence in RESTRICTION_CONFIDENCE else ""


def normalize_appeal_status(value):
    status = str(value or "").strip().lower()
    return status if status in APPEAL_STATUSES else ""


def bounded_restriction_duration(value, automatic=False):
    try:
        duration = int(value)
    except (TypeError, ValueError):
        return 0
    if duration <= 0:
        # Automatic decisions can never become permanent. A zero-duration
        # manual restriction is handled by the caller only after human review.
        return 0
    maximum = (
        AUTO_RESTRICTION_MAX_MS if automatic else MANUAL_RESTRICTION_MAX_MS
    )
    return min(duration, maximum)


def generalized_evidence_summary(value):
    """Return a bounded summary with control characters removed.

    Callers must pass a generalized description, never a raw request path,
    query, form body, address, credential, or source identifier.
    """
    raw = str(value or "")[:MAX_EVIDENCE_SUMMARY * 2]
    clean = "".join(
        ch for ch in raw if ch in ("\n", "\t") or ord(ch) >= 32
    )
    return " ".join(clean.split())[:MAX_EVIDENCE_SUMMARY]


def security_signal_for_path(path, method="GET"):
    """Classify a narrow set of high-signal malicious endpoint probes.

    The returned record never contains the path. Country, browser and OS are
    intentionally absent: none is evidence of malicious intent.
    """
    raw = str(path or "")[:2048]
    decoded = unquote(raw).lower()
    raw_lower = raw.lower()
    method = str(method or "GET").upper()

    traversal = (
        "../" in decoded or "..\\" in decoded or "%2e%2e" in raw.lower()
    )
    if traversal:
        return {
            "rule": "path-traversal-v1",
            "reason": "path_traversal",
            "confidence": "high",
            "summary": "Repeated traversal syntax was requested.",
        }

    # These are executable or administrative exploit targets, not merely a
    # product-specific 404. Keep the rule narrow and require repetition in the
    # Worker before a temporary quarantine is created.
    exploit_markers = (
        "/vendor/phpunit/phpunit/src/util/php/eval-stdin.php",
        "/boaform/admin/formlogin",
        "/hudson/script",
        "/solr/admin/cores",
        "/actuator/gateway/routes",
        "/cgi-bin/.%2e/",
        "${jndi:",
    )
    if any(
        marker in decoded or marker in raw_lower
        for marker in exploit_markers
    ):
        return {
            "rule": "known-exploit-signature-v1",
            "reason": "exploit_signature",
            "confidence": "high",
            "summary": "A known exploit request signature was detected.",
        }

    probe_markers = (
        "/wp-admin", "/wp-login", "/xmlrpc.php", "/phpmyadmin",
        "/.env", "/.git/config", "/vendor/phpunit", "/cgi-bin/",
        "/actuator/env",
    )
    if any(marker in decoded for marker in probe_markers):
        return {
            "rule": "known-endpoint-probe-v1",
            "reason": "endpoint_probing",
            "confidence": "medium",
            "summary": "A known unrelated administration or secret path was probed.",
        }

    injection_markers = (
        "<script", "union select", "' or 1=1", "\" or 1=1",
        "${jndi:", ";drop table",
    )
    if any(marker in decoded for marker in injection_markers):
        return {
            "rule": "injection-signature-v1",
            "reason": "injection_attempt",
            "confidence": "high",
            "summary": "A known injection signature was detected in a path.",
        }

    if method not in ("GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"):
        return {
            "rule": "unexpected-method-v1",
            "reason": "access_control_bypass",
            "confidence": "low",
            "summary": "An unsupported HTTP method was repeatedly attempted.",
        }
    return None


def security_signal_for_response(
        path, method, status, authorization_present=False):
    """Classify repeated authentication failures without inspecting secrets.

    The caller invokes this only after a response exists. No request body,
    credential, account name, query string, country, browser, or OS is accepted
    by this interface, so none can influence or enter the resulting evidence.
    """
    path = str(path or "").split("?", 1)[0][:512]
    method = str(method or "GET").upper()
    try:
        status = int(status)
    except (TypeError, ValueError):
        return None
    if status not in (401, 403, 404):
        return None
    credential_paths = frozenset({
        "/api/accounts/login",
        "/api/accounts/admin-session",
        "/api/accounts/reset-password",
    })
    if method == "POST" and path.rstrip("/") in credential_paths:
        return {
            "rule": "credential-stuffing-failures-v1",
            "reason": "credential_abuse",
            "confidence": "low",
            "summary": (
                "Repeated failed credential submissions were observed at an "
                "authentication boundary."
            ),
        }
    sensitive_boundary = bool(
        path.startswith("/api/private-replicas/")
        or path.startswith("/api/security/")
        or path.rstrip("/") == "/api/mirrors/private"
        or path.endswith("/security-scans/ingest")
    )
    signed_request_boundary = bool(
        method == "POST"
        and (
            path.rstrip("/") == "/api/mirrors/private"
            or path.endswith("/security-scans/ingest")
        )
    ) or bool(
        method == "GET"
        and re.fullmatch(r"/api/repo/[^/]+/[^/]+/host", path)
    )
    if sensitive_boundary and (
            authorization_present or signed_request_boundary):
        return {
            "rule": "protected-boundary-bypass-v1",
            "reason": "access_control_bypass",
            "confidence": "low",
            "summary": (
                "Repeated rejected proofs were observed at a protected "
                "authorization boundary."
            ),
        }
    if authorization_present and (
        (path.startswith("/api/") and status in (401, 403))
    ):
        return {
            "rule": "repeated-auth-failures-v1",
            "reason": "credential_abuse",
            "confidence": "low",
            "summary": (
                "Repeated rejected authorization proofs were observed at a "
                "protected boundary."
            ),
        }
    return None


class BoundedTrafficDetector:
    """Per-isolate bounded burst detector for scraping and denial-of-service.

    Only keyed subject tokens and timestamps are retained. Paths, queries,
    user-agent data, country, browser, and operating system are neither stored
    nor accepted. D1 receives a generalized signal only after a high request
    threshold is crossed.
    """

    def __init__(
            self, max_subjects=2048, scrape_window_ms=60_000,
            scrape_requests=90, dos_window_ms=10_000, dos_requests=240):
        self.max_subjects = max(1, min(8192, int(max_subjects)))
        self.scrape_window_ms = max(1000, int(scrape_window_ms))
        self.scrape_requests = max(3, int(scrape_requests))
        self.dos_window_ms = max(1000, int(dos_window_ms))
        self.dos_requests = max(
            self.scrape_requests + 1, int(dos_requests))
        self._subjects = {}

    @property
    def subject_count(self):
        return len(self._subjects)

    @staticmethod
    def _prune(values, cutoff):
        while values and values[0] < cutoff:
            values.popleft()

    def _state(self, subject_token, now):
        state = self._subjects.get(subject_token)
        if state is not None:
            state["last"] = now
            return state
        if len(self._subjects) >= self.max_subjects:
            oldest = min(
                self._subjects,
                key=lambda token: self._subjects[token]["last"],
            )
            self._subjects.pop(oldest, None)
        state = {
            "last": now,
            "all": deque(maxlen=self.dos_requests + 1),
            "reads": deque(maxlen=self.scrape_requests + 1),
        }
        self._subjects[subject_token] = state
        return state

    def observe(self, subject_token, method, now):
        subject_token = normalize_subject_token(subject_token)
        method = str(method or "GET").upper()
        try:
            now = int(now)
        except (TypeError, ValueError):
            return None
        if not subject_token or now <= 0:
            return None
        state = self._state(subject_token, now)
        self._prune(state["all"], now - self.dos_window_ms)
        self._prune(state["reads"], now - self.scrape_window_ms)
        state["all"].append(now)
        if method in ("GET", "HEAD"):
            state["reads"].append(now)
        if len(state["all"]) >= self.dos_requests:
            return {
                "rule": "request-flood-v1",
                "reason": "denial_of_service",
                "confidence": "high",
                "summary": (
                    "A sustained request flood exceeded the bounded service "
                    "threshold."
                ),
            }
        if len(state["reads"]) >= self.scrape_requests:
            return {
                "rule": "excessive-read-automation-v1",
                "reason": "excessive_scraping",
                "confidence": "medium",
                "summary": (
                    "Automated read volume exceeded the bounded scraping "
                    "threshold."
                ),
            }
        return None


def public_restriction(record, now=None):
    """Project a restriction to the non-sensitive administrative/world shape."""
    record = record if isinstance(record, dict) else {}
    detected = _safe_int(record.get("detectedAt"))
    expires = _safe_int(record.get("expiresAt"))
    if now is not None and expires and expires <= int(now):
        status = "expired"
    else:
        status = str(record.get("status") or "quarantined")
        if status not in RESTRICTION_STATUSES:
            status = "quarantined"
    return {
        "incidentId": normalize_incident_id(record.get("incidentId")),
        "reason": normalize_reason(record.get("reason")) or "other_security_abuse",
        "rule": _short_label(record.get("rule"), 80),
        "detectedAt": detected,
        "durationMs": max(0, _safe_int(record.get("durationMs"))),
        "expiresAt": expires,
        "confidence": normalize_confidence(record.get("confidence")) or "low",
        "automatic": bool(record.get("automatic")),
        "reviewed": bool(record.get("reviewed")),
        "appealStatus": (
            normalize_appeal_status(record.get("appealStatus")) or "none"
        ),
        "status": status,
        "countryCode": _country(record.get("countryCode")),
        "clientCategory": _short_label(record.get("clientCategory"), 40),
    }


def sanitize_audit_details(details):
    """Allow only short scalar metadata and drop secret-bearing key names."""
    if not isinstance(details, dict):
        return {}
    out = {}
    for key, value in list(details.items())[:20]:
        clean_key = re.sub(r"[^A-Za-z0-9_.-]", "", str(key or ""))[:48]
        lowered = clean_key.lower()
        if not clean_key or any(word in lowered for word in _AUDIT_FORBIDDEN_KEYS):
            continue
        if isinstance(value, bool):
            out[clean_key] = value
        elif isinstance(value, int):
            out[clean_key] = value
        elif isinstance(value, float):
            if value == value and value not in (float("inf"), float("-inf")):
                out[clean_key] = value
        elif isinstance(value, str):
            out[clean_key] = generalized_evidence_summary(value)[:160]
    return out


def validate_owner_public_bundle(value):
    """Validate a MirrorCrypto public-only recipient bundle.

    Only X25519 and ML-KEM public keys are accepted. The returned key id is
    derived from those exact bytes, so a caller cannot alias one public bundle
    under another recipient id.
    """
    if not isinstance(value, dict) or value.get("v") != 1:
        return None
    x25519 = str(value.get("x25519") or "").strip()
    mlkem768 = str(value.get("mlkem768") or "").strip()
    if not _valid_b64url(x25519, 32, 32):
        return None
    if not _valid_b64url(mlkem768, 1184, 1184):
        return None
    raw_x = _decode_b64url(x25519)
    raw_m = _decode_b64url(mlkem768)
    kid = base64.urlsafe_b64encode(
        hashlib.sha256(raw_x + raw_m).digest()
    ).decode().rstrip("=")
    return {
        "v": 1,
        "x25519": x25519,
        "mlkem768": mlkem768,
        "kid": kid,
    }


def validate_owner_envelope(value, max_body=MAX_OWNER_ENVELOPE_BODY):
    """Validate an opaque client-sealed envelope without decrypting it.

    This is the single-recipient form of Qt MirrorCrypto's working hybrid
    envelope.  The recipient entry carries the ephemeral X25519 public key,
    ML-KEM-768 ciphertext, and AES-GCM-wrapped content key; nonce/tag/body are
    the payload ciphertext.  The server checks only framing and bounds.
    Recipient private keys and plaintext remain entirely client-side.
    """
    if not isinstance(value, dict):
        return None
    if value.get("kind") != "forkmesh.owner-sealed" or value.get("v") != 1:
        return None
    if value.get("alg") != "x25519+mlkem768/aes256gcm":
        return None
    nonce = str(value.get("nonce") or "").strip()
    tag = str(value.get("tag") or "").strip()
    body = str(value.get("body") or "").strip()
    recipients = value.get("recipients")
    if not isinstance(recipients, list) or len(recipients) != 1:
        return None
    recipient = recipients[0]
    if not isinstance(recipient, dict):
        return None
    kid = str(recipient.get("kid") or "").strip()
    ephemeral = str(recipient.get("x25519") or "").strip()
    kem_ciphertext = str(recipient.get("mlkem768") or "").strip()
    wrap_nonce = str(recipient.get("nonce") or "").strip()
    wrap_tag = str(recipient.get("tag") or "").strip()
    wrapped_key = str(recipient.get("key") or "").strip()
    if (not _KEY_ID_RE.fullmatch(kid)
            or not _valid_b64url(ephemeral, 32, 32)
            or not _valid_b64url(kem_ciphertext, 1088, 1088)
            or not _valid_b64url(wrap_nonce, 12, 12)
            or not _valid_b64url(wrap_tag, 16, 16)
            or not _valid_b64url(wrapped_key, 32, 32)):
        return None
    if not _valid_b64url(nonce, 12, 12):
        return None
    if not _valid_b64url(tag, 16, 16):
        return None
    if not body or len(body) > int(max_body * 4 / 3) + 8:
        return None
    if not _B64URL_RE.fullmatch(body):
        return None
    try:
        raw_size = len(_decode_b64url(body))
    except Exception:
        return None
    if raw_size > max_body:
        return None
    return {
        "kind": "forkmesh.owner-sealed",
        "v": 1,
        "alg": "x25519+mlkem768/aes256gcm",
        "nonce": nonce,
        "tag": tag,
        "body": body,
        "recipients": [{
            "kid": kid,
            "x25519": ephemeral,
            "mlkem768": kem_ciphertext,
            "nonce": wrap_nonce,
            "tag": wrap_tag,
            "key": wrapped_key,
        }],
    }


def owner_envelope_key_id(envelope):
    """Return the sole validated recipient id without exposing ciphertext."""
    valid = validate_owner_envelope(envelope)
    if valid is None:
        return ""
    return valid["recipients"][0]["kid"]


def encode_owner_envelope(envelope, routing=None):
    envelope = validate_owner_envelope(envelope)
    if envelope is None:
        return ""
    wrapper = {"envelope": envelope}
    if isinstance(routing, dict):
        safe_routing = {}
        for key in ("agentId", "queuedAt"):
            if key in routing:
                safe_routing[key] = _safe_int(routing.get(key))
        if safe_routing:
            wrapper["routing"] = safe_routing
    raw = json.dumps(wrapper, sort_keys=True, separators=(",", ":")).encode()
    return "owner-sealed-v1:" + base64.urlsafe_b64encode(raw).decode().rstrip("=")


def decode_owner_envelope(value):
    value = str(value or "")
    prefix = "owner-sealed-v1:"
    if not value.startswith(prefix):
        return None
    try:
        raw = _decode_b64url(value[len(prefix):])
        wrapper = json.loads(raw.decode("utf-8"))
    except Exception:
        return None
    if not isinstance(wrapper, dict):
        return None
    envelope = validate_owner_envelope(wrapper.get("envelope"))
    if envelope is None:
        return None
    result = {"envelope": envelope}
    routing = wrapper.get("routing")
    if isinstance(routing, dict):
        result["routing"] = {
            key: _safe_int(routing.get(key))
            for key in ("agentId", "queuedAt") if key in routing
        }
    return result


def _decode_b64url(value):
    value = str(value or "")
    return base64.urlsafe_b64decode(value + "=" * ((4 - len(value) % 4) % 4))


def _valid_b64url(value, minimum, maximum):
    if not value or not _B64URL_RE.fullmatch(value):
        return False
    try:
        size = len(_decode_b64url(value))
    except Exception:
        return False
    return minimum <= size <= maximum


def _safe_int(value):
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def _short_label(value, maximum):
    raw = str(value or "")[:maximum * 2]
    clean = "".join(
        ch for ch in raw if ch.isalnum() or ch in (" ", ".", "_", "-", ":")
    )
    return " ".join(clean.split())[:maximum]


def _country(value):
    value = str(value or "").strip().upper()
    return value if re.fullmatch(r"[A-Z]{2}", value) and value not in ("XX",) else ""
