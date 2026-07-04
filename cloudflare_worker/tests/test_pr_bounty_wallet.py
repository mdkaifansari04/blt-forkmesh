#!/usr/bin/env python3
"""Per-PR bounty + inbuilt bounty wallet contracts (issue #347).

Source-level contract checks (entry.py depends on the Workers Python JS runtime,
so behaviour is asserted against the source). They pin the product decision:
every merged PR can reward its author with a fixed bounty, funded either per-PR
(a QR) or from a pre-funded inbuilt wallet, and PR bounties are keyed separately
from issue bounties so a shared number never collides.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# SCHEMA_STATEMENTS (D1 DDL) was extracted from entry.py into schema.py;
# concatenate it so the schema source-contract assertions below still resolve.
ENTRY_TEXT = (
    (ROOT / "src" / "entry.py").read_text(encoding="utf-8") + "\n"
    + (ROOT / "src" / "schema.py").read_text(encoding="utf-8"))
QT_SRC = Path(__file__).resolve().parents[2] / "qt_client" / "src"
QT_TEXT = "\n".join(
    p.read_text(encoding="utf-8") for p in sorted(QT_SRC.glob("MainWindow*.cpp"))
)
QT_HEADERS = "\n".join(
    p.read_text(encoding="utf-8") for p in sorted(QT_SRC.glob("MainWindow*.h"))
)


def test_bounty_key_separates_issues_from_pull_requests():
    # _bounty_bi takes a kind so an issue and a PR sharing a number don't collide.
    assert "async def _bounty_bi(env, owner, repo, number, kind=" in ENTRY_TEXT
    body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _bounty_bi"):
        ENTRY_TEXT.index("async def _bounty_wallet_bi")
    ]
    assert '(kind + "-") if kind else ""' in body


def test_worker_has_inbuilt_bounty_wallet():
    assert 'CREATE TABLE IF NOT EXISTS bounty_wallet' in ENTRY_TEXT
    assert "async def _bounty_wallet_bi(env, owner)" in ENTRY_TEXT
    assert 'blind_index(env, "bounty-wallet:" + owner)' in ENTRY_TEXT
    # A dedicated owner-signed "wallet" action mints/returns the deposit address.
    assert 'if action == "wallet":' in ENTRY_TEXT
    assert '"forkmesh-bounty-wallet-v1\\n"' in ENTRY_TEXT


def test_wallet_mode_pays_split_directly_from_wallet():
    # fromWallet debits the pre-funded wallet and pays the author/treasury split
    # in one step, rejecting an underfunded wallet.
    create = ENTRY_TEXT[
        ENTRY_TEXT.index('if action == "create":'):
        ENTRY_TEXT.index("if rec is None:")
    ]
    assert 'if data.get("fromWallet"):' in create
    assert '"insufficient_wallet_balance"' in create
    assert "_solana_send_transfers(" in create
    assert 'BOUNTY_TREASURY_BPS' in create


def test_desktop_rewards_every_merged_pull_request():
    assert "void MainWindow::autoBountyForMergedPull" in QT_TEXT
    # Wired into the merge flow, and keyed to the PR (kind "pr").
    assert "autoBountyForMergedPull(current);" in QT_TEXT
    assert '{"kind", QStringLiteral("pr")}' in QT_TEXT
    # Settings drive it: enable flag, amount, and per-PR vs wallet mode.
    assert "kAutoPrBountyEnabledSetting" in QT_HEADERS
    assert "kAutoPrBountyModeSetting" in QT_HEADERS
    assert "showBountyWalletDialog" in QT_TEXT
