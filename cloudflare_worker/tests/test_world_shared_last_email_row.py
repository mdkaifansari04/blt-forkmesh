#!/usr/bin/env python3
"""The badge's last-email row reads on everybody, not just on yourself.

adhoc #21: the chest badge showed "LAST EMAIL 2D AGO · DELIVERED" for the
signed-in player and "LAST EMAIL · NOT SHARED" on every other member, because
the stamp only rode the owner's authenticated /api/accounts/sessions read.
Contracts pinned here:

  * The public directory (/api/accounts/users) carries an hour-bucketed send
    time plus the delivery outcome. The address, the email kind and the send
    count stay behind the authenticated read.
  * The World app forwards both fields, and the scene files them as directory
    facts so a member's walking avatar and their campfire bench figure paint
    the same row.
  * A directory member is never "NOT SHARED": no stamp means never emailed.
    Guests — who own no account record — still are.
"""

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
APP = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")


def _load(name):
    for node in ast.parse(ENTRY).body:
        if isinstance(node, ast.FunctionDef) and node.name == name:
            namespace = {
                "ACCOUNT_EMAIL_STATUS_DELIVERED": "delivered",
                "ACCOUNT_EMAIL_STATUS_FAILED": "failed",
                "WORLD_PUBLIC_LAST_EMAIL_BUCKET_MS": 60 * 60 * 1000,
            }
            exec(ast.get_source_segment(ENTRY, node), namespace)
            return namespace[name]
    raise AssertionError("entry.py must define %s" % name)


def test_public_stamp_is_hour_bucketed_with_the_delivery_outcome():
    public_last_email = _load("_account_public_last_email")
    hour = 60 * 60 * 1000
    assert public_last_email(
        {"last_email_ts": 5 * hour + 1234, "last_email_ok": True}
    ) == {"lastEmailAt": 5 * hour, "lastEmailStatus": "delivered"}
    assert public_last_email(
        {"last_email_ts": 9 * hour, "last_email_ok": False}
    ) == {"lastEmailAt": 9 * hour, "lastEmailStatus": "failed"}


def test_never_emailed_and_broken_records_publish_no_stamp():
    public_last_email = _load("_account_public_last_email")
    empty = {"lastEmailAt": 0, "lastEmailStatus": ""}
    assert public_last_email({}) == empty
    assert public_last_email({"last_email_ts": 0}) == empty
    assert public_last_email({"last_email_ts": "not-a-number"}) == empty
    assert public_last_email(None) == empty


def test_directory_payload_carries_the_stamp_but_not_the_address():
    stamp = _load("_account_public_last_email")(
        {"last_email_ts": 60 * 60 * 1000, "last_email_ok": True})
    assert set(stamp) == {"lastEmailAt", "lastEmailStatus"}
    assert "**_account_public_last_email(rec)," in ENTRY
    # The kind and the running counter stay on the authenticated read.
    helper = ENTRY.split("def _account_public_last_email", 1)[1].split(
        "\ndef ", 1)[0]
    assert '"lastEmailKind"' not in helper
    assert '"emailSendCount"' not in helper


def test_world_app_forwards_both_fields_from_the_directory():
    directory = APP.split("function normalizeMemberDirectory(value)", 1)[1] \
        .split("\nfunction ", 1)[0]
    assert "lastEmailAt: Math.max(0, Number(user?.lastEmailAt) || 0)" in \
        directory
    assert 'String(user?.lastEmailStatus || "")' in directory


def test_scene_paints_the_row_for_members_and_hides_it_for_guests():
    # Directory facts: the walking avatar and the bench figure both read them.
    assert "lastEmailAt: Math.max(0, Number(member?.lastEmailAt) || 0)" in SCENE
    assert "lastEmailAt: Math.max(0, Number(member.lastEmailAt) || 0)" in SCENE
    assert SCENE.count("lastEmailPrivate: false") == 3
    # withMemberFacts returns early for a guest, so the guest badge keeps the
    # "NOT SHARED" line.
    assert '"LAST EMAIL · NOT SHARED"' in SCENE
    assert '"LAST EMAIL NEVER · NO DELIVERY"' in SCENE
