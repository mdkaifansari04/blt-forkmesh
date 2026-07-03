#!/usr/bin/env python3
"""Account signup/profile endpoint contracts.

These are source-level contract checks because src/entry.py depends on the
Workers Python JS runtime. They keep the product decision explicit: signup is a
simple email/password/name flow, and Solana payout address management lives in
the dashboard profile flow after account creation.
"""
from pathlib import Path

ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
# MainWindow.cpp is split into feature TUs (MainWindow*.cpp); scan them all.
QT_SRC = Path(__file__).resolve().parents[2] / "qt_client" / "src"
QT_TEXT = "\n".join(
    p.read_text(encoding="utf-8") for p in sorted(QT_SRC.glob("MainWindow*.cpp"))
)


def test_worker_exposes_simple_signup_endpoint():
    assert 'url.path == "/api/accounts/signup" and method == "POST"' in ENTRY_TEXT
    assert "async def _account_signup" in ENTRY_TEXT
    assert 'data.get("nodeName", "")' in ENTRY_TEXT
    assert 'data.get("email", "")' in ENTRY_TEXT
    assert 'data.get("password", "")' in ENTRY_TEXT


def test_signup_endpoint_is_not_the_solana_payment_flow():
    signup_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_signup"):
        ENTRY_TEXT.index("async def _account_reserve")
    ]
    assert "donation_address" not in signup_body
    assert "donation_required_lamports" not in signup_body
    assert "SOLANA_RE" not in signup_body
    assert 'rec["status"] = "active"' in signup_body
    assert 'rec["email_verified"] = bool(rec.get("email_verified", False))' in signup_body


def test_worker_exposes_password_authenticated_profile_endpoint_for_wallet_and_verification():
    assert 'url.path == "/api/accounts/profile" and method == "POST"' in ENTRY_TEXT
    assert "async def _account_profile" in ENTRY_TEXT
    profile_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_profile"):
        ENTRY_TEXT.index("async def _login_locked_until")
    ]
    assert 'data.get("password", "")' in profile_body
    assert 'verify_password(password' in profile_body
    assert 'data.get("solana", "")' in profile_body
    assert 'resendVerification' in profile_body
    assert '_send_verification_email' in profile_body


def test_worker_profile_contract_includes_avatar_updates():
    public_payload_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_public_payload"):
        ENTRY_TEXT.index("def _donation_expiry_fields")
    ]
    profile_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_profile"):
        ENTRY_TEXT.index("async def _login_locked_until")
    ]
    heartbeat_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_heartbeat"):
        ENTRY_TEXT.index("async def _account_treasury_address")
    ]
    public_lookup_start = ENTRY_TEXT.index("match = ACCOUNTS_RE.match(url.path)")
    public_lookup_body = ENTRY_TEXT[
        public_lookup_start:
        ENTRY_TEXT.index('return json_response({"error": "not_found"}', public_lookup_start)
    ]

    assert '"avatarPng": rec.get("avatar_png", "")' in public_payload_body
    assert '"avatarUpdatedAt": rec.get("avatar_updated_at", 0)' in public_payload_body
    assert 'data.get("avatarPng", "")' in profile_body
    assert 'rec["avatar_png"] = avatar_png' in profile_body
    assert 'data.get("avatarPng", "")' in heartbeat_body
    assert 'rec["avatar_png"] = avatar_png' in heartbeat_body
    assert '"avatarPng": rec.get("avatar_png", "")' in public_lookup_body


def test_qt_client_publishes_effective_avatar_to_peers():
    # The desktop's avatar reaches peers through the chat backend broadcast
    # (the signed heartbeat carries no avatar; the worker-side avatarPng comes
    # from the dashboard profile flow, pinned above). effectiveAvatar() falls
    # back to a generated face so every node stays identifiable.
    assert "m_backend->setAvatar(effectiveAvatar());" in QT_TEXT

    heartbeat_body = QT_TEXT[
        QT_TEXT.index("void MainWindow::sendNodeHeartbeat()"):
        QT_TEXT.index("void MainWindow::showNodeClaimCode")
    ]
    # The per-minute heartbeat stays lean: name + solana + ts + sig only, no
    # avatar re-upload every beat.
    assert '{"nodeName", name}, {"solana", solana}, {"ts", ts}' in heartbeat_body
    assert "avatarPng" not in heartbeat_body


def test_profile_endpoint_supports_verified_node_rename_and_hard_delete():
    profile_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_profile"):
        ENTRY_TEXT.index("async def _login_locked_until")
    ]
    login_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_login"):
        ENTRY_TEXT.index("def _random_bytes")
    ]

    assert 'data.get("newNodeName", "")' in profile_body
    assert 'data.get("deleteAccount")' in profile_body
    assert 'email_not_verified' in profile_body
    assert 'node_name_taken' in profile_body
    assert '_rename_account_namespace(env, name_bi, rec, new_name)' in profile_body
    assert '_delete_account_namespace(env, name_bi, rec)' in profile_body
    assert '"accountDeleted": True' in profile_body
    assert '"account_disabled"' in login_body


def test_public_account_lookup_reports_email_verified_status():
    # issue #320: the dashboard's periodic self-profile poll (refreshPublicProfile)
    # reuses this same public GET lookup, so it must reflect a verification that
    # happened in another tab or "verify your email" never clears.
    lookup_body = ENTRY_TEXT[
        ENTRY_TEXT.index("match = ACCOUNTS_RE.match(url.path)"):
        ENTRY_TEXT.index("# --- Repository submission inboxes")
    ]
    assert '"emailVerified": bool(rec.get("email_verified"))' in lookup_body
    assert '"email":' not in lookup_body


def test_hard_delete_removes_account_identity_and_owned_namespace_state():
    assert "async def _delete_account_namespace" in ENTRY_TEXT
    assert "async def _delete_repo_namespace" in ENTRY_TEXT
    delete_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _delete_bounties_namespace"):
        ENTRY_TEXT.index("async def _account_public_payload")
    ]

    for required in (
        "SELECT key_bi, data FROM repositories WHERE owner_bi=?",
        "DELETE FROM repo_shares WHERE repo_bi=?",
        "DELETE FROM repo_shares WHERE grantee_bi=?",
        "DELETE FROM issue_inbox WHERE repo_bi=?",
        "DELETE FROM pull_inbox WHERE repo_bi=?",
        "DELETE FROM commit_inbox WHERE repo_bi=?",
        "DELETE FROM discussion_inbox WHERE repo_bi=?",
        "DELETE FROM host_presence WHERE repo_bi=?",
        "DELETE FROM clone_rr WHERE repo_bi=?",
        "DELETE FROM repo_first_hosted WHERE repo_bi=?",
        "DELETE FROM issue_bounty WHERE bounty_bi=?",
        "DELETE FROM chat_history WHERE room_key LIKE ?",
        "DELETE FROM funds_received WHERE scope='project' AND key=?",
        "DELETE FROM catalog_rate WHERE owner_bi=?",
        "DELETE FROM account_presence WHERE name_bi=?",
        "DELETE FROM pending_verifications WHERE name_bi=?",
        "DELETE FROM notifications WHERE recipient_bi=?",
        "DELETE FROM login_attempts WHERE id_bi=?",
        "DELETE FROM accounts WHERE name_bi=?",
        "edge_cache_delete(CATALOG_CACHE_KEY)",
    ):
        assert required in delete_body


def test_namespace_rename_moves_account_repo_and_repo_scoped_state():
    assert "async def _rename_account_namespace" in ENTRY_TEXT
    assert "async def _move_repo_namespace" in ENTRY_TEXT
    assert "async def _save_account_full" in ENTRY_TEXT

    save_full_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _save_account_full"):
        ENTRY_TEXT.index("async def _move_repo_shares")
    ]
    assert "INSERT INTO accounts" in save_full_body
    shares_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _move_repo_shares"):
        ENTRY_TEXT.index("async def _move_bounties_namespace")
    ]
    assert "SELECT grantee_bi, data, ts FROM repo_shares WHERE repo_bi=?" in shares_body
    bounty_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _move_bounties_namespace"):
        ENTRY_TEXT.index("async def _move_chat_history_namespace")
    ]
    assert "SELECT bounty_bi, data FROM issue_bounty" in bounty_body
    chat_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _move_chat_history_namespace"):
        ENTRY_TEXT.index("async def _move_repo_namespace")
    ]
    assert "SELECT room_key, msg_id, ts, body FROM chat_history WHERE room_key LIKE ?" in chat_body

    rename_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _rename_account_namespace"):
        ENTRY_TEXT.index("async def _account_profile")
    ]

    for required in (
        "SELECT data, email_bi, ip_bi, is_admin FROM accounts WHERE name_bi=?",
        "DELETE FROM accounts WHERE name_bi=?",
        "UPDATE account_presence SET name_bi=? WHERE name_bi=?",
        "UPDATE pending_verifications SET name_bi=? WHERE name_bi=?",
        "UPDATE notifications SET recipient_bi=? WHERE recipient_bi=?",
        "UPDATE catalog_rate SET owner_bi=? WHERE owner_bi=?",
        "edge_cache_delete(CATALOG_CACHE_KEY)",
    ):
        assert required in rename_body

    repo_move_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _move_repo_namespace"):
        ENTRY_TEXT.index("async def _rename_account_namespace")
    ]

    for required in (
        "SELECT key_bi, data, is_private FROM repositories WHERE owner_bi=?",
        'rec["owner"] = new_owner',
        "UPDATE issue_inbox SET repo_bi=? WHERE repo_bi=?",
        "UPDATE pull_inbox SET repo_bi=? WHERE repo_bi=?",
        "UPDATE commit_inbox SET repo_bi=? WHERE repo_bi=?",
        "UPDATE discussion_inbox SET repo_bi=? WHERE repo_bi=?",
        "UPDATE host_presence SET repo_bi=? WHERE repo_bi=?",
        "UPDATE clone_rr SET repo_bi=? WHERE repo_bi=?",
        "UPDATE repo_first_hosted SET repo_bi=? WHERE repo_bi=?",
        "UPDATE funds_received SET key=?, name=? WHERE scope='project' AND key=?",
    ):
        assert required in repo_move_body


def test_worker_exposes_password_reset_flow():
    # Both halves of the emailed password-reset flow are routed and implemented.
    assert 'url.path == "/api/accounts/forgot-password" and method == "POST"' in ENTRY_TEXT
    assert 'url.path == "/api/accounts/reset-password" and method == "POST"' in ENTRY_TEXT
    assert "async def _account_forgot_password" in ENTRY_TEXT
    assert "async def _account_reset_password" in ENTRY_TEXT
    assert "async def _password_reset_token" in ENTRY_TEXT
    assert "async def _send_password_reset_email" in ENTRY_TEXT


def test_forgot_password_does_not_leak_account_existence():
    forgot_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_forgot_password"):
        ENTRY_TEXT.index("async def _account_reset_password")
    ]
    # Look up by email OR node name, but always answer {"ok": True} so the
    # endpoint can't enumerate which accounts exist.
    assert 'data.get("identifier", "")' in forgot_body
    assert "_send_password_reset_email" in forgot_body
    assert 'return json_response({"ok": True})' in forgot_body
    assert "invalid_credentials" not in forgot_body
    assert "no_such_account" not in forgot_body


def test_reset_password_validates_token_expiry_and_hashes_new_password():
    reset_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_reset_password"):
        ENTRY_TEXT.index("def _verify_email_page")
    ]
    # Expiry is enforced, the token is recomputed from the stored pass_hash
    # (single-use), a short password is rejected, and the new one is PBKDF2-hashed.
    assert "reset_link_expired" in reset_body
    assert "password_too_short" in reset_body
    assert "invalid_reset_token" in reset_body
    assert "hmac.compare_digest(token, expected)" in reset_body
    assert "hash_password(password)" in reset_body
    assert 'rec["pass_hash"] = phash' in reset_body

    token_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _password_reset_token"):
        ENTRY_TEXT.index("async def _send_password_reset_email")
    ]
    # The token is domain-separated and binds the current pass_hash + expiry.
    assert "forkmesh-password-reset-v1" in token_body
    assert "pass_hash" in token_body


def test_login_page_links_to_password_reset():
    login_html = (Path(__file__).resolve().parents[1] /
                  "public" / "login.html").read_text(encoding="utf-8")
    assert "/forgot-password.html" in login_html
    for asset in ("forgot-password.html", "forgot-password.js",
                  "reset-password.html", "reset-password.js"):
        assert (Path(__file__).resolve().parents[1] / "public" / asset).exists()


if __name__ == "__main__":
    for test in (
        test_worker_exposes_simple_signup_endpoint,
        test_signup_endpoint_is_not_the_solana_payment_flow,
        test_worker_exposes_password_authenticated_profile_endpoint_for_wallet_and_verification,
        test_worker_profile_contract_includes_avatar_updates,
        test_qt_client_publishes_effective_avatar_with_signed_heartbeat,
        test_profile_endpoint_supports_verified_node_rename_and_hard_delete,
        test_hard_delete_removes_account_identity_and_owned_namespace_state,
        test_namespace_rename_moves_account_repo_and_repo_scoped_state,
        test_worker_exposes_password_reset_flow,
        test_forgot_password_does_not_leak_account_existence,
        test_reset_password_validates_token_expiry_and_hashes_new_password,
        test_login_page_links_to_password_reset,
    ):
        test()
        print("PASS", test.__name__)


def test_worker_exposes_password_reset_endpoints():
    assert 'url.path == "/api/accounts/forgot-password" and method == "POST"' in ENTRY_TEXT
    assert 'url.path == "/api/accounts/reset-password" and method == "POST"' in ENTRY_TEXT
    assert "async def _account_forgot_password" in ENTRY_TEXT
    assert "async def _account_reset_password" in ENTRY_TEXT


def test_forgot_password_never_leaks_account_existence():
    body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_forgot_password"):
        ENTRY_TEXT.index("async def _account_reset_password")
    ]
    assert 'return json_response({"ok": True})' in body
    assert "_send_password_reset_email" in body


def test_reset_password_verifies_token_and_rehashes():
    body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_reset_password"):
        ENTRY_TEXT.index("def _verify_email_page")
    ]
    assert "_password_reset_token" in body
    assert "hmac.compare_digest" in body
    assert "hash_password(password)" in body
    assert '"password_too_short"' in body
    assert '"reset_link_expired"' in body


def test_reset_token_bound_to_current_hash_and_expiry():
    body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _password_reset_token"):
        ENTRY_TEXT.index("async def _send_password_reset_email")
    ]
    assert "forkmesh-password-reset-v1" in body
    assert "pass_hash" in body
    assert "expires" in body


def test_login_page_links_to_password_reset():
    login_html = (Path(__file__).resolve().parents[1] /
                  "public" / "login.html").read_text(encoding="utf-8")
    assert "/forgot-password.html" in login_html
