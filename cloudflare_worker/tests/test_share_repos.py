#!/usr/bin/env python3
"""Private-repo collaborator sharing contract checks (stdlib only, issue #9).

The share feature spans the worker and the Qt client and is held together by
Ed25519-signed canonical strings that must match byte-for-byte on both sides
(the same way test_private_repos.py pins the catalog-view token). The flow:

  * Owner grants/revokes a collaborator   -> forkmesh-share-v1 (owner-signed)
  * Owner lists collaborators             -> forkmesh-shares-list-v1 (owner-signed)
  * Grantee browses/clones a shared repo  -> forkmesh-share-view-v1 (grantee-signed)

These tests don't import the Workers-only JS runtime; they assert the wire
contract and the key security guards as source substrings so a one-sided edit
(worker OR client) can't silently break sharing or weaken its access control.
"""

from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
# MainWindow.cpp is split into feature TUs (MainWindow*.cpp); scan them all.
QT_SRC = Path(__file__).resolve().parents[2] / "qt_client" / "src"
QT_HDR = QT_SRC / "MainWindow.h"

# SCHEMA_STATEMENTS (D1 DDL) was extracted from entry.py into schema.py;
# concatenate it so the schema source-contract assertions below still resolve.
SCHEMA = ENTRY.parent / "schema.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n" + SCHEMA.read_text(encoding="utf-8"))
QT_TEXT = "\n".join(
    p.read_text(encoding="utf-8") for p in sorted(QT_SRC.glob("MainWindow*.cpp"))
)
QT_HDR_TEXT = QT_HDR.read_text(encoding="utf-8") if QT_HDR.exists() else ""


# --- forkmesh-share-view-v1: the grantee read/clone token -------------------

def test_share_view_token_canonical_matches_across_worker_and_client():
    # The grantee signs viewer + owner + repo + ts under a distinct prefix; both
    # sides must agree on the leading fields (the trailing repo/name + ts field
    # is on the next wrapped line in each source file).
    common = '"forkmesh-share-view-v1\\n" + viewer + "\\n" + owner + "\\n"'
    assert common in ENTRY_TEXT
    assert common in QT_TEXT


def test_share_view_token_requires_an_active_share_row():
    # A valid grantee signature is NOT sufficient: access is granted only when a
    # repo_shares row exists for (repo, grantee). This is the heart of the ACL —
    # losing it would let any logged-in account clone any private repo.
    assert "return await _repo_shared_with(env, owner, repo, viewer)" in ENTRY_TEXT
    assert ("SELECT 1 AS one FROM repo_shares WHERE repo_bi=? AND grantee_bi=?"
            in ENTRY_TEXT)


def test_git_basic_auth_branches_on_username():
    # Git clone selects the owner vs grantee path by the Basic-auth username, so a
    # grantee token (signed with the grantee's key) is verified against the right
    # key and a forged owner-named token can't take the grantee branch.
    assert "if username and username != owner:" in ENTRY_TEXT
    assert ("return await verify_share_view_token(env, username, owner, repo, ts, sig)"
            in ENTRY_TEXT)


# --- forkmesh-share-v1 / -shares-list-v1: owner-only ACL admin --------------

def test_grant_token_is_owner_signed_and_action_bound():
    # The action ("add"/"remove") is bound into the signature so a grant token
    # can never be replayed to revoke (or vice versa), on both sides.
    assert '"forkmesh-share-v1\\n" + owner + "\\n" + repo + "\\n"' in ENTRY_TEXT
    assert 'action + "\\n" + str(ts)' in ENTRY_TEXT
    assert '"forkmesh-share-v1\\n" + repo.owner + "\\n" + repo.name' in QT_TEXT
    assert 'action + "\\n" + ts' in QT_TEXT


def test_list_token_canonical_matches_across_worker_and_client():
    assert '"forkmesh-shares-list-v1\\n" + owner + "\\n" + repo + "\\n"' in ENTRY_TEXT
    assert '"forkmesh-shares-list-v1\\n" + repo.owner + "\\n" + repo.name' in QT_TEXT


def test_grant_requires_a_real_grantee_account():
    # A token can only ever verify if the grantee is a registered account, so the
    # add path rejects unknown accounts rather than storing a dead share row.
    assert "unknown_account" in ENTRY_TEXT
    assert "if not await _owner_pubkey(env, grantee):" in ENTRY_TEXT


# --- Catalog visibility -----------------------------------------------------

def test_authenticated_catalog_includes_repos_shared_to_the_viewer():
    # A logged-in viewer additionally sees private repos shared WITH them, joined
    # through the repo_shares ACL by their own blind index.
    assert ("SELECT repo_bi FROM repo_shares WHERE grantee_bi = ?" in ENTRY_TEXT)
    # ...and the public-only branch (anonymous callers) stays public-only.
    assert ("SELECT key_bi, data FROM repositories WHERE is_private = 0"
            in ENTRY_TEXT)


def test_shared_repos_are_flagged_for_the_client():
    # The relay marks a shared (not owned) private repo so the client badges it
    # and clones it with the grantee token instead of the owner token.
    assert 'rec["sharedWithMe"]' in ENTRY_TEXT


# --- Schema + storage -------------------------------------------------------

def test_repo_shares_table_is_defined():
    assert "CREATE TABLE IF NOT EXISTS repo_shares" in ENTRY_TEXT
    assert "idx_repo_shares_grantee" in ENTRY_TEXT
    # And a parity D1 migration file exists for the file-based schema path.
    mig = ENTRY.resolve().parents[1] / "migrations" / "0014_repo_shares.sql"
    assert mig.exists()


# --- Client wiring ----------------------------------------------------------

def test_client_declares_collaborator_api():
    for decl in ("sharesApiUrl", "addRepoCollaborator", "removeRepoCollaborator",
                 "refreshRepoCollaborators"):
        assert decl in QT_HDR_TEXT, decl
        assert decl in QT_TEXT, decl
