"""Browser attachment protocol and UI contracts for both chat surfaces."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
CHAT = (PUBLIC / "chat.js").read_text(encoding="utf-8")
ATTACHMENTS = (PUBLIC / "chat-attachments.js").read_text(encoding="utf-8")
DASHBOARD_CHAT = (PUBLIC / "dashboard-chat.js").read_text(encoding="utf-8")
HTML = (PUBLIC / "chat.html").read_text(encoding="utf-8")


def test_full_chat_has_accessible_image_and_document_controls():
    assert 'id="chat-attachment-input"' in HTML
    assert 'id="chat-attachment-button"' in HTML
    assert 'aria-label="Attach image or document"' in HTML
    assert 'type="file"' in HTML


def test_full_chat_uses_desktop_compatible_encrypted_attachment_fields():
    assert "const MAX_ATTACHMENT_BYTES = 1024 * 1024" in ATTACHMENTS
    assert 'from "./chat-attachments.js"' in CHAT
    assert "async function sendAttachment(" in CHAT
    assert "function renderAttachment(" in CHAT
    for field in ("fileName", "fileMime", "file"):
        assert field in CHAT
    assert 'input.addEventListener("paste"' in CHAT
    assert "clipboardData" in CHAT
    assert "event.preventDefault()" in CHAT


def test_full_chat_sends_picker_and_pasted_attachment_drafts_immediately():
    attachment_controls = CHAT[
        CHAT.index("if (attachmentBtn && attachmentInput) {"):
        CHAT.index("if (threadAttachmentBtn && threadAttachmentInput) {")
    ]
    paste = CHAT[
        CHAT.index('input.addEventListener("paste"'):
        CHAT.index('threadInput?.addEventListener("paste"')
    ]
    assert "stageAttachments(files);" in attachment_controls
    assert "void sendAttachmentDraft();" in attachment_controls
    assert "stageAttachments([file]);\n    void sendAttachmentDraft();" in paste


def test_full_chat_renders_safe_images_and_downloadable_documents():
    assert 'className = "chat-attachment-image"' in CHAT
    assert 'className = "chat-attachment-card"' in CHAT
    assert 'link.download = attachment.fileName' in CHAT
    assert "URL.createObjectURL" in CHAT
    assert "safeAttachmentName" in CHAT
    assert "export function safeAttachmentName" in ATTACHMENTS


def test_dashboard_chat_has_equivalent_dynamic_attachment_controls():
    assert "const MAX_ATTACHMENT_BYTES = 1024 * 1024" in DASHBOARD_CHAT
    assert "function mountAttachmentControl(" in DASHBOARD_CHAT
    assert "async function sendAttachment(" in DASHBOARD_CHAT
    assert "function renderAttachment(" in DASHBOARD_CHAT
    assert 'inputEl.addEventListener("paste"' in DASHBOARD_CHAT
    assert "clipboardData" in DASHBOARD_CHAT
    assert "fileName" in DASHBOARD_CHAT
    assert "fileMime" in DASHBOARD_CHAT
    assert "file:" in DASHBOARD_CHAT


def test_oversized_attachments_fail_before_encryption_or_socket_send():
    for source in (CHAT, DASHBOARD_CHAT):
        size_check = source.index("file.size > MAX_ATTACHMENT_BYTES")
        encode = source.index("file.arrayBuffer()", size_check)
        assert size_check < encode
        assert "1 MiB" in source[size_check:encode]
