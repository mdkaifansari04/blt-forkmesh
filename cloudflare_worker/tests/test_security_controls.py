"""Security-boundary tests that run without the Workers runtime."""

import base64
import hashlib
import importlib.util
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

    assert security.public_roles(
        ["mirror_operator", "moderator", "platform_administrator"]
    ) == ["mirror_operator"]


def test_worker_has_no_abuse_quarantine_request_or_response_runtime():
    worker = (SRC / "entry.py").read_text(encoding="utf-8")
    retired_symbols = (
        "_security_enforce_request",
        "_security_observe_response",
        "_record_security_signal",
        "security_signal_for_path",
        "security_signal_for_response",
        "BoundedTrafficDetector",
        "public_restriction",
        "bounded_restriction_duration",
        "normalize_subject_token",
        "normalize_reason",
        "normalize_confidence",
        "normalize_appeal_status",
        "temporarily_quarantined",
        "/api/security/quarantine",
        "/api/security/appeals",
        "security.auto_quarantine",
        "security.account_restrict",
    )
    for removed in retired_symbols:
        assert removed not in worker
        assert not hasattr(security, removed)

    for removed in (
        "RESTRICTION_REASONS",
        "RESTRICTION_CONFIDENCE",
        "RESTRICTION_STATUSES",
        "APPEAL_STATUSES",
        "AUTO_RESTRICTION_MAX_MS",
        "MANUAL_RESTRICTION_MAX_MS",
        "DEFAULT_QUARANTINE_MS",
        "MAX_APPEAL_TEXT",
    ):
        assert not hasattr(security, removed)



    for retained in (
        "signup_rate_check",
        "ROOM_MSG_WINDOW_MS",
        "HOST_RATE_WINDOW_MS",
        "security_roles_handler",
        "security_report_handler",
        "security_audit_handler",
    ):
        assert retained in worker


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


def test_owner_public_bundle_contains_no_private_material_and_binds_key_id():
    x25519 = b"x" * 32
    mlkem = b"m" * 1184
    validated = security.validate_owner_public_bundle({
        "v": 1,
        "x25519": _b64url(x25519),
        "mlkem768": _b64url(mlkem),

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
        "role_grants", "sensitive_audit_log", "owner_encryption_keys",
        "repo_privacy_policy", "chain_intents", "pending_rewards",
    ):
        assert f"CREATE TABLE IF NOT EXISTS {table}" in schema
        assert f"CREATE TABLE IF NOT EXISTS {table}" in migration
        assert f'"{table}"' in worker
    for removed in (
        "security_signals", "security_restrictions", "security_appeals",
    ):
        assert f'"{removed}"' not in worker
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



    assert '"/private-or-unpublished-repository/"' in body
    assert "_privacy_redacted_route_kind(path)" in body
