"""Static contracts for the dormant Qt speech bridge.

The browser entry point is intentionally absent from ForkMesh World.  The
device-local Qt implementation remains available for a future, explicitly
reviewed interface without exposing a launcher or microphone controls today.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public" / "world"
HTML = (PUBLIC / "index.html").read_text(encoding="utf-8")
QT_HEADER = (
    ROOT.parent / "qt_client" / "src" / "WorldSpeechBridge.h"
).read_text(encoding="utf-8")
QT_SOURCE = (
    ROOT.parent / "qt_client" / "src" / "WorldSpeechBridge.cpp"
).read_text(encoding="utf-8")
QT_CHAT = (
    ROOT.parent / "qt_client" / "src" / "MainWindowChat.cpp"
).read_text(encoding="utf-8")
QT_SETTINGS = (
    ROOT.parent / "qt_client" / "src" / "MainWindowSettings.cpp"
).read_text(encoding="utf-8")
DOC = (
    ROOT.parent / "docs" / "security" / "world-local-speech-bridge.md"
).read_text(encoding="utf-8")


def test_world_does_not_load_or_expose_local_speech_controls():
    assert "world-speech.css" not in HTML
    assert "world-speech.js" not in HTML
    assert "Local voice input" not in HTML
    assert not (PUBLIC / "world-speech.js").exists()
    assert not (PUBLIC / "world-speech.css").exists()


def test_qt_bridge_never_relays_microphone_audio():
    assert "none-local-capture-only" in QT_SOURCE
    assert '"audioRelayed"), false' in QT_SOURCE
    assert "/v1/audio" not in QT_SOURCE
    assert "no audio upload or download endpoint" in DOC


def test_capabilities_are_memory_only_and_revocable():
    assert '"/v1/session/revoke"' in QT_SOURCE
    assert "request->target.contains('?')" in QT_SOURCE
    assert "m_pairings.insert(digest(secret)" in QT_SOURCE
    assert "m_sessions.insert(digest(sessionToken)" in QT_SOURCE


def test_exact_origin_loopback_and_replay_guards_are_implemented():
    assert "QHostAddress::LocalHost" in QT_SOURCE
    assert "peerAddress().isLoopback()" in QT_SOURCE
    assert "normalizedOrigin(origin) != origin" in QT_SOURCE
    assert "pairing.origin == origin" in QT_SOURCE
    assert "session.origin == origin" in QT_SOURCE
    assert "constantTimeEqual" in QT_SOURCE
    assert "missing_or_replayed_request_id" in QT_SOURCE
    assert "invalid_or_expired_pairing" in QT_SOURCE
    assert "invalid_or_expired_session" in QT_SOURCE
    assert "Access-Control-Allow-Private-Network: true" in QT_SOURCE


def test_qt_bridge_api_is_capture_implementation_agnostic_and_audited():
    for marker in (
        "captureRequested",
        "stopRequested",
        "cancelRequested",
        "publishPartial",
        "publishFinal",
        "publishError",
        "auditEvent",
        "revokeAll",
    ):
        assert marker in QT_HEADER
    normalized_doc = " ".join(DOC.split())
    assert "transcript contents" in normalized_doc
    assert "without capability values" in normalized_doc


def test_qt_ui_wires_local_capture_status_and_never_puts_secret_in_world_url():
    assert "initializeWorldSpeechBridge();" in (
        ROOT.parent / "qt_client" / "src" / "MainWindow.cpp"
    ).read_text(encoding="utf-8")
    assert "Open World + create code" in QT_SETTINGS
    assert "one-use capability" in QT_SETTINGS
    assert "m_worldSpeechBridge->issuePairing(origin, 120)" in QT_CHAT
    assert "world.setPath(QStringLiteral(\"/world/\"))" in QT_CHAT
    assert "world.setQuery" not in QT_CHAT
    assert "world.setFragment" not in QT_CHAT
    assert "startVoiceCaptureFor(m_worldSpeechDraftEdit" in QT_CHAT
    assert "m_worldSpeechBridge->publishPartial" in QT_CHAT
    assert "m_worldSpeechBridge->publishFinal" in QT_CHAT
    assert "m_worldSpeechBridge->publishError" in QT_CHAT
    assert "cancelWorldVoiceCapture" in QT_CHAT
