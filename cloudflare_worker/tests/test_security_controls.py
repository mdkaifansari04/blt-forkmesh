"""Security-boundary tests that run without the Workers runtime."""

import base64
import hashlib
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SPEC = importlib.util.spec_from_file_location(
    "security_controls", SRC / "security_controls.py"
)
security = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(security)


def _b64url(raw):
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def _envelope(**changes):
    value = {
        "kind": "forkmesh.owner-sealed",
        "v": 1,
        "alg": "x25519+mlkem768/aes256gcm",
        "nonce": _b64url(b"n" * 12),
        "tag": _b64url(b"t" * 16),
        "body": _b64url(b"opaque ciphertext"),
        "recipients": [{
            "kid": _b64url(hashlib.sha256(b"owner-key").digest()),
            "x25519": _b64url(b"x" * 32),
            "mlkem768": _b64url(b"m" * 1088),
            "nonce": _b64url(b"w" * 12),
            "tag": _b64url(b"g" * 16),
            "key": _b64url(b"k" * 32),
        }],
    }
    value.update(changes)
    return value


def test_roles_are_explicit_and_scope_limited():
    assert security.normalize_role("security-reviewer") == "security_reviewer"
    assert security.normalize_role("root") == ""
    assert security.role_scope_allowed("repository_administrator", "repository")
    assert not security.role_scope_allowed(
        "repository_administrator", "platform"
    )
    assert security.role_scope_allowed("moderator", "organization")
    assert not security.role_scope_allowed(
        "platform_administrator", "repository"
    )
    # Privileged moderation/reviewer/admin grants never become profile badges.
    assert security.public_roles(
        ["mirror_operator", "moderator", "platform_administrator"]
    ) == ["mirror_operator"]


def test_abuse_classifier_never_returns_request_or_identity_data():
    samples = (
        security.security_signal_for_path("/wp-admin", "GET"),
        security.security_signal_for_path(
            "/vendor/phpunit/phpunit/src/Util/PHP/eval-stdin.php", "GET"),
        security.security_signal_for_path("/a/../../etc/passwd", "GET"),
        security.security_signal_for_path("/?q=UNION%20SELECT", "GET"),
        security.security_signal_for_path("/", "CONNECT"),
    )
    for signal in samples:
        assert signal
        rendered = json.dumps(signal).lower()
        for forbidden in (
            "wp-admin", "passwd", "union select", "country", "browser",
            "operating", "user-agent", "ip", "address",
        ):
            assert forbidden not in rendered
        assert set(signal) == {"rule", "reason", "confidence", "summary"}
    assert security.security_signal_for_path("/ordinary/page", "GET") is None


def test_failed_auth_classifier_is_post_response_narrow_and_identity_free():
    stuffing = security.security_signal_for_response(
        "/api/accounts/login", "POST", 401)
    bypass = security.security_signal_for_response(
        "/api/private-replicas/" + "a" * 64,
        "GET",
        404,
        authorization_present=True,
    )
    signed_bypass = security.security_signal_for_response(
        "/api/mirrors/private",
        "POST",
        404,
        authorization_present=False,
    )
    repeated = security.security_signal_for_response(
        "/api/accounts/profile",
        "POST",
        401,
        authorization_present=True,
    )
    assert stuffing == {
        "rule": "credential-stuffing-failures-v1",
        "reason": "credential_abuse",
        "confidence": "low",
        "summary": (
            "Repeated failed credential submissions were observed at an "
            "authentication boundary."
        ),
    }
    assert bypass["rule"] == "protected-boundary-bypass-v1"
    assert bypass["reason"] == "access_control_bypass"
    assert signed_bypass["rule"] == "protected-boundary-bypass-v1"
    assert signed_bypass["reason"] == "access_control_bypass"
    assert repeated["rule"] == "repeated-auth-failures-v1"
    assert repeated["reason"] == "credential_abuse"
    assert security.security_signal_for_response(
        "/api/accounts/login", "POST", 200) is None
    assert security.security_signal_for_response(
        "/api/public", "GET", 403, authorization_present=False) is None
    for signal in (stuffing, bypass, signed_bypass, repeated):
        rendered = json.dumps(signal).lower()
        for forbidden in (
            "alice", "repository", "country", "browser", "operating",
            "user-agent", "ip", "address", "password", "token",
        ):
            assert forbidden not in rendered


def test_bounded_traffic_detector_flags_only_sustained_volume_and_caps_memory():
    detector = security.BoundedTrafficDetector(
        max_subjects=2,
        scrape_window_ms=1_000,
        scrape_requests=3,
        dos_window_ms=1_000,
        dos_requests=5,
    )
    first = "a" * 64
    assert detector.observe(first, "GET", 1000) is None
    assert detector.observe(first, "GET", 1001) is None
    scraping = detector.observe(first, "GET", 1002)
    assert scraping["reason"] == "excessive_scraping"
    assert scraping["rule"] == "excessive-read-automation-v1"

    second = "b" * 64
    for instant in range(1100, 1104):
        assert detector.observe(second, "POST", instant) is None
    denial = detector.observe(second, "POST", 1104)
    assert denial["reason"] == "denial_of_service"
    assert denial["rule"] == "request-flood-v1"

    # A third keyed subject evicts the oldest state instead of growing without
    # bound. Invalid/raw identifiers are rejected and never retained.
    assert detector.observe("not-a-token", "GET", 1200) is None
    assert detector.observe("c" * 64, "GET", 1200) is None
    assert detector.subject_count == 2
    for signal in (scraping, denial):
        assert set(signal) == {"rule", "reason", "confidence", "summary"}
        rendered = json.dumps(signal).lower()
        for forbidden in (
            "country", "browser", "operating", "user-agent", "path", "url",
        ):
            assert forbidden not in rendered


def test_worker_enforces_volume_and_observes_failed_auth_without_request_data():
    worker = (SRC / "entry.py").read_text(encoding="utf-8")
    enforce = worker[
        worker.index("async def _security_enforce_request")
        :worker.index("async def _security_observe_response")
    ]
    observe = worker[
        worker.index("async def _security_observe_response")
        :worker.index("async def security_quarantine_handler")
    ]
    fetch = worker[
        worker.index("    async def fetch(self, request):", worker.index(
            "class Default"))
        :worker.index("    async def _admin(self, request):")
    ]
    assert "_SECURITY_TRAFFIC_DETECTOR.observe" in enforce
    assert "_record_security_signal" in enforce
    assert "security_signal_for_response" in observe
    assert "_record_security_signal" in observe
    assert "request.text" not in observe
    assert "request.json" not in observe
    assert "url.query" not in observe
    assert "_security_observe_response" in fetch


def test_audit_detail_sanitizer_is_scalar_allowlist_and_secret_denylist():
    cleaned = security.sanitize_audit_details({
        "status": "approved",
        "retryCount": 2,
        "automatic": True,
        "privateKey": "secret",
        "walletAddress": "public-but-sensitive-target",
        "authorization": "Bearer token",
        "source_ip": "192.0.2.4",
        "requestBody": "form contents",
        "nested": {"must": "not survive"},
        "items": ["must", "not", "survive"],
    })
    assert cleaned == {
        "status": "approved",
        "retryCount": 2,
        "automatic": True,
    }


def test_automatic_restrictions_are_temporal_and_public_shape_is_generalized():
    assert security.bounded_restriction_duration(
        10**15, automatic=True
    ) == security.AUTO_RESTRICTION_MAX_MS
    assert security.bounded_restriction_duration(0, automatic=True) == 0
    record = security.public_restriction({
        "incidentId": "a" * 32,
        "reason": "path_traversal",
        "rule": "path-traversal-v1",
        "detectedAt": 10,
        "durationMs": 20,
        "expiresAt": 30,
        "confidence": "high",
        "automatic": True,
        "appealStatus": "pending",
        "status": "quarantined",
        "countryCode": "US",
        "clientCategory": "browser",
        "rawIp": "192.0.2.4",
        "evidence": "private",
    }, now=31)
    assert record["status"] == "expired"
    assert record["countryCode"] == "US"
    assert set(record) == {
        "incidentId", "reason", "rule", "detectedAt", "durationMs",
        "expiresAt", "confidence", "automatic", "reviewed",
        "appealStatus", "status", "countryCode", "clientCategory",
    }
    assert "192.0.2.4" not in json.dumps(record)


def test_world_quarantine_view_never_decrypts_private_reviewer_evidence():
    worker = (SRC / "entry.py").read_text(encoding="utf-8")
    handler = worker[
        worker.index("async def security_quarantine_handler")
        :worker.index("async def security_appeals_handler")
    ]
    assert 'x-forkmesh-world-view' in handler
    assert "and not world_generalized" in handler
    assert '"reviewer-generalized-world"' in handler
    assert '"allowedActions": ["revoke"]' in handler
    assert '"summary": [{' in handler


def test_owner_public_bundle_contains_no_private_material_and_binds_key_id():
    x25519 = b"x" * 32
    mlkem = b"m" * 1184
    validated = security.validate_owner_public_bundle({
        "v": 1,
        "x25519": _b64url(x25519),
        "mlkem768": _b64url(mlkem),
        # Caller-provided aliases and private fields are ignored.
        "kid": "attacker-alias",
        "privateKey": "never-retained",
    })
    assert validated == {
        "v": 1,
        "x25519": _b64url(x25519),
        "mlkem768": _b64url(mlkem),
        "kid": _b64url(hashlib.sha256(x25519 + mlkem).digest()),
    }
    assert security.validate_owner_public_bundle({
        "v": 1, "x25519": _b64url(b"x" * 31),
        "mlkem768": _b64url(mlkem),
    }) is None


def test_owner_envelope_round_trip_is_opaque_and_routing_is_allowlisted():
    envelope = _envelope()
    assert security.validate_owner_envelope(envelope) == envelope
    assert security.owner_envelope_key_id(envelope) == (
        envelope["recipients"][0]["kid"])
    encoded = security.encode_owner_envelope(
        envelope,
        {
            "agentId": 42,
            "queuedAt": 1234,
            "repo": "must-not-be-stored-in-routing",
            "plaintext": "must-not-survive",
        },
    )
    assert encoded.startswith("owner-sealed-v1:")
    decoded = security.decode_owner_envelope(encoded)
    assert decoded == {
        "envelope": envelope,
        "routing": {"agentId": 42, "queuedAt": 1234},
    }
    assert "plaintext" not in encoded
    assert security.decode_owner_envelope("owner-sealed-v1:not-base64!") is None
    assert security.validate_owner_envelope(
        _envelope(alg="server-readable/aes")
    ) is None
    assert security.validate_owner_envelope(
        _envelope(body=_b64url(b"x" * 17)), max_body=16
    ) is None
    assert security.validate_owner_envelope(
        _envelope(recipients=[])
    ) is None
    two = _envelope()
    two["recipients"].append(dict(two["recipients"][0]))
    assert security.validate_owner_envelope(two) is None
    incomplete = _envelope()
    del incomplete["recipients"][0]["mlkem768"]
    assert security.validate_owner_envelope(incomplete) is None


def test_schema_and_worker_keep_sensitive_planes_out_of_generic_admin():
    schema = (SRC / "schema.py").read_text(encoding="utf-8")
    worker = (SRC / "entry.py").read_text(encoding="utf-8")
    migration = (
        ROOT / "migrations" / "0043_security_control_plane.sql"
    ).read_text(encoding="utf-8")
    for table in (
        "role_grants", "security_signals", "security_restrictions",
        "security_appeals", "sensitive_audit_log", "owner_encryption_keys",
        "repo_privacy_policy", "chain_intents", "pending_rewards",
    ):
        assert f"CREATE TABLE IF NOT EXISTS {table}" in schema
        assert f"CREATE TABLE IF NOT EXISTS {table}" in migration
        assert f'"{table}"' in worker
    assert "allow_admin=False" in worker


def test_worker_has_no_wallet_signing_or_transaction_submission_imports():
    worker = (SRC / "entry.py").read_text(encoding="utf-8")
    solana = (SRC / "solana.py").read_text(encoding="utf-8")
    import_block = worker.split("from solana import (", 1)[1].split(")", 1)[0]
    for forbidden in (
        "_solana_sign_message", "_solana_transfer_message",
        "_solana_send_transaction", "_solana_latest_blockhash", "_shortvec",
    ):
        assert forbidden not in import_block
    for helper in (
        "_solana_send_transfers", "_solana_sign_transfers",
        "_solana_broadcast_raw", "_new_solana_keypair",
    ):
        assert f"def {helper}" not in worker
    for helper in (
        "_solana_send_transaction", "_solana_sign_message",
        "_solana_transfer_message",
    ):
        assert f"def {helper}" not in solana


def test_private_profiles_and_repo_pages_revoke_cached_public_metadata():
    worker = (SRC / "entry.py").read_text(encoding="utf-8")
    lookup_start = worker.index("match = ACCOUNTS_RE.match(url.path)")
    lookup_end = worker.index(
        'return json_response({"error": "not_found"}', lookup_start)
    lookup = worker[lookup_start:lookup_end]
    assert lookup.index("profile_private = bool(rec.get") < lookup.index(
        "cached = await edge_cache_match(lookup_cache_key)")
    assert "await _account_session_record(env, request)" in lookup
    assert '"profilePrivate": True' in lookup
    assert '"nodes": _owned_nodes(rec)' in lookup

    directory = worker[
        worker.index("async def _account_users_directory")
        :worker.index("def _donation_expiry_fields")
    ]
    assert 'or rec.get("profile_private")' in directory

    profile_page = worker[
        worker.index("async def _serve_profile_page")
        :worker.index("async def _serve_repo_page")
    ]
    assert profile_page.index('rec.get("profile_private")') < (
        profile_page.index("cached = await edge_cache_match(cache_key)"))


def test_error_logs_redact_browser_repository_routes_too():
    worker = (SRC / "entry.py").read_text(encoding="utf-8")
    body = worker[
        worker.index("async def _privacy_safe_log_path")
        :worker.index("async def capture_worker_exception")
    ]
    assert "looks_like_repo_route(path)" in body
    assert '"/private-or-unpublished-repository"' in body
