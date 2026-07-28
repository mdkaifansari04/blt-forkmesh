"""Account-scoped World preference and saved-view synchronization contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src/entry.py").read_text(encoding="utf-8")
WORLD = (ROOT / "public/world/world.js").read_text(encoding="utf-8")


def test_world_preferences_require_a_revocable_session_and_same_origin_write():
    assert "async def world_preferences_handler" in ENTRY
    assert "_account_session_record(env, request, data)" in ENTRY
    assert '"invalid_session"' in ENTRY
    assert "_request_same_origin(request)" in ENTRY
    assert '"origin_not_allowed"' in ENTRY
    assert "WORLD_PREFERENCES_MAX_BYTES = 320 * 1024" in ENTRY


def test_preferences_stay_in_the_encrypted_account_record_and_are_bounded():
    assert 'rec["world_preferences"] = stored' in ENTRY
    assert "await _save_account(env, account_bi, rec)" in ENTRY
    assert '"storage": "account-encrypted"' in ENTRY
    assert "WORLD_PREFERENCES_MAX_VIEWS = 4" in ENTRY
    assert "data:image/webp;base64" in ENTRY
    assert "abs(x) > 340" in ENTRY
    assert "settingsUpdatedAt" in ENTRY


def test_stale_devices_merge_instead_of_overwriting_newer_preferences():
    assert 'incoming["settingsUpdatedAt"] >= settings_updated_at' in ENTRY
    assert 'view["updatedAt"] >= previous["updatedAt"]' in ENTRY
    assert "mergeWorldPreferenceSnapshot(payload)" in WORLD
    assert "view.updatedAt > previous.updatedAt" in WORLD
    assert "WORLD_PREFERENCES_SYNC_DELAY_MS = 700" in WORLD


def test_signed_in_world_syncs_views_and_movement_with_offline_fallback():
    assert 'const WORLD_PREFERENCES_ENDPOINT = "/api/world/preferences"' in WORLD
    assert "void this.loadWorldPreferences()" in WORLD
    assert "queueWorldPreferencesSync()" in WORLD
    assert "this.settings.moveSpeed" in WORLD
    assert "this.settings.moveAccel" in WORLD
    assert "this.savedViews.slice(0, SAVED_VIEWS_MAX)" in WORLD
    assert "Local storage remains the offline source" in WORLD
    assert "synced to your account" in WORLD
