"""Static security and integration contracts for World local dictation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public" / "world"
JS = (PUBLIC / "world-speech.js").read_text(encoding="utf-8")
CSS = (PUBLIC / "world-speech.css").read_text(encoding="utf-8")
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


def test_world_loads_separate_local_speech_assets():
    assert '<link rel="stylesheet" href="/world/world-speech.css"' in HTML
    assert '<script defer src="/world/world-speech.js"></script>' in HTML
    assert ".world-speech-panel" in CSS
    assert "100dvh" in CSS
    assert "safe-area-inset" in CSS


def test_browser_never_captures_or_transports_microphone_audio():
    forbidden = (
        "getUserMedia",
        "MediaRecorder",
        "AudioWorklet",
        "audio/webm",
        "audio/wav",
        "FormData(",
    )
    for marker in forbidden:
        assert marker not in JS
    assert "none-local-capture-only" in QT_SOURCE
    assert '"audioRelayed"), false' in QT_SOURCE
    assert "/v1/audio" not in QT_SOURCE
    assert "no audio upload or download endpoint" in DOC


def test_capabilities_are_header_only_memory_only_and_revocable():
    assert "Authorization" in JS
    assert "Bearer ${state.sessionToken}" in JS
    assert "X-ForkMesh-Request-Id" in JS
    assert "sessionStorage.setItem" not in JS
    assert "localStorage.setItem" not in JS
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


def test_user_selects_destination_and_gets_recording_controls():
    for destination in ("chat", "issue", "note"):
        assert f'data-destination="{destination}"' in JS
        assert f'data-composer="{destination}"' in JS
    assert "Choose where the transcript goes before starting" in JS
    assert "Start local mic" in JS
    assert "Stop & transcribe" in JS
    assert "Cancel" in JS
    assert 'setStatus("Recording locally in Qt…", "recording")' in JS
    assert 'setStatus("Qt is transcribing locally…", "transcribing")' in JS
    assert "never sent automatically" in JS


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
