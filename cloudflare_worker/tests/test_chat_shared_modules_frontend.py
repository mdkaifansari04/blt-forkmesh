#!/usr/bin/env python3
"""Executable contracts for shared encrypted chat browser modules."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
CRYPTO = PUBLIC / "chat-crypto.js"
ATTACHMENTS = PUBLIC / "chat-attachments.js"
TRANSPORT = PUBLIC / "chat-room-transport.js"
CHAT = PUBLIC / "chat.js"
HTML = PUBLIC / "chat.html"


def _run_module(script):
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(result.stdout)


def test_shared_chat_modules_exist_and_full_chat_imports_them():
    assert CRYPTO.exists()
    assert ATTACHMENTS.exists()
    assert TRANSPORT.exists()
    chat = CHAT.read_text(encoding="utf-8")
    html = HTML.read_text(encoding="utf-8")
    assert 'from "./chat-crypto.js"' in chat
    assert 'from "./chat-attachments.js"' in chat
    assert 'from "./chat-room-transport.js"' in chat
    assert '<script type="module" src="/chat.js"></script>' in html


def test_room_transport_authorizes_encrypts_replays_and_suspends_cleanly():
    script = f"""
      import {{ createChatRoomTransport }} from {json.dumps(TRANSPORT.as_uri())};
      import {{ derivePassphraseKey, encryptObject }}
        from {json.dumps(CRYPTO.as_uri())};

      class FakeWebSocket {{
        static CONNECTING = 0;
        static OPEN = 1;
        static CLOSING = 2;
        static CLOSED = 3;
        static instances = [];
        constructor(url) {{
          this.url = url;
          this.readyState = FakeWebSocket.CONNECTING;
          this.sent = [];
          this.listeners = new Map();
          FakeWebSocket.instances.push(this);
        }}
        addEventListener(type, callback) {{
          const entries = this.listeners.get(type) || [];
          entries.push(callback);
          this.listeners.set(type, entries);
        }}
        emit(type, event = {{}}) {{
          for (const callback of this.listeners.get(type) || []) callback(event);
        }}
        open() {{
          this.readyState = FakeWebSocket.OPEN;
          this.emit("open");
        }}
        send(value) {{ this.sent.push(value); }}
        close(code = 1000, reason = "") {{
          if (this.readyState === FakeWebSocket.CLOSED) return;
          this.readyState = FakeWebSocket.CLOSED;
          this.emit("close", {{ code, reason }});
        }}
      }}

      const states = [];
      const received = [];
      const timers = [];
      let authorizations = 0;
      const transport = createChatRoomTransport({{
        authorize: async (room) => {{
          authorizations += 1;
          return {{
            scope: room.scope,
            passphrase: "room secret",
            roomName: "channel-alpha",
            webSocketUrl: "/api/chat/channel-alpha/ws?ticket=one",
          }};
        }},
        onPlain: (plain, room) => received.push({{ plain, scope: room.scope }}),
        onState: (state) => states.push(state),
        WebSocketImpl: FakeWebSocket,
        locationLike: {{ protocol: "https:", host: "relay.example" }},
        setTimeoutImpl: (callback, delay) => {{
          timers.push({{ callback, delay }});
          return timers.length;
        }},
        clearTimeoutImpl: () => {{}},
      }});

      await transport.connect({{ scope: "channel-alpha" }});
      const socket = FakeWebSocket.instances[0];
      socket.open();
      await transport.send({{ type: "chat", text: "hello" }}, {{ persist: true }});

      const key = await derivePassphraseKey("room secret", "channel-alpha");
      const incoming = await encryptObject({{ type: "chat", text: "replayed" }}, key);
      socket.emit("message", {{ data: JSON.stringify(incoming) }});
      for (let attempt = 0; attempt < 20 && !received.length; attempt += 1) {{
        await new Promise((resolve) => setTimeout(resolve, 5));
      }}

      transport.suspend();
      const timerCountAfterSuspend = timers.length;
      socket.close(1011, "relay restarted");

      process.stdout.write(JSON.stringify({{
        authorizations,
        states,
        socketUrl: socket.url,
        sentEnvelope: JSON.parse(socket.sent[0]),
        received,
        connected: transport.connected,
        state: transport.state,
        timerCountAfterSuspend,
        timerCountFinal: timers.length,
      }}));
    """
    result = _run_module(script)
    assert result["authorizations"] == 1
    assert result["states"][:3] == ["authorizing", "connecting", "connected"]
    assert result["states"][-1] == "suspended"
    assert result["socketUrl"] == (
        "wss://relay.example/api/chat/channel-alpha/ws?ticket=one"
    )
    assert result["sentEnvelope"]["persist"] is True
    assert result["received"] == [
        {
            "plain": {"type": "chat", "text": "replayed"},
            "scope": "channel-alpha",
        }
    ]
    assert result["connected"] is False
    assert result["state"] == "suspended"
    assert result["timerCountFinal"] == result["timerCountAfterSuspend"]


def test_shared_aes_envelope_round_trips_with_existing_wire_shape():
    script = f"""
      import {{ derivePassphraseKey, encryptObject, decryptObject }}
        from {json.dumps(CRYPTO.as_uri())};
      const key = await derivePassphraseKey("test-passphrase", "world-general");
      const envelope = await encryptObject({{ type: "chat", text: "hello" }}, key);
      const plain = await decryptObject(envelope, key);
      process.stdout.write(JSON.stringify({{
        keys: Object.keys(envelope).sort(),
        kind: envelope.kind,
        version: envelope.v,
        plain,
      }}));
    """
    result = _run_module(script)
    assert result == {
        "keys": ["body", "kind", "nonce", "tag", "v"],
        "kind": "cipher",
        "version": 1,
        "plain": {"type": "chat", "text": "hello"},
    }


def test_meeting_proof_binds_avatar_to_exact_text_and_attachment():
    script = f"""
      import {{ createMeetingBinding, signMeetingProof, verifyMeetingProof }}
        from {json.dumps(CRYPTO.as_uri())};
      const binding = await createMeetingBinding();
      const message = {{
        participantId: "participant-1",
        messageId: "message-1",
        senderId: "sender-1",
        ts: 1700000000000,
        text: "hello",
        attachment: {{
          fileName: "notes.txt",
          fileMime: "text/plain",
          file: btoa("private notes"),
          size: 13,
        }},
      }};
      const proof = await signMeetingProof(binding.privateKey, message);
      const valid = await verifyMeetingProof(binding.publicJwk, proof, message);
      const altered = await verifyMeetingProof(binding.publicJwk, proof, {{
        ...message,
        text: "altered",
      }});
      const alteredFile = await verifyMeetingProof(binding.publicJwk, proof, {{
        ...message,
        attachment: {{ ...message.attachment, file: btoa("changed notes") }},
      }});
      process.stdout.write(JSON.stringify({{
        valid,
        altered,
        alteredFile,
        extractable: binding.privateKey.extractable,
        publicKeys: Object.keys(binding.publicJwk).sort(),
        proofKeys: Object.keys(proof).sort(),
      }}));
    """
    result = _run_module(script)
    assert result == {
        "valid": True,
        "altered": False,
        "alteredFile": False,
        "extractable": False,
        "publicKeys": ["crv", "kty", "x", "y"],
        "proofKeys": ["participantId", "signature", "ts", "v"],
    }


def test_attachment_module_sanitizes_and_digests_bounded_entries():
    script = f"""
      import {{ attachmentFromEntry, normalizeAttachmentForProof }}
        from {json.dumps(ATTACHMENTS.as_uri())};
      const entry = {{
        fileName: "../release-notes.txt",
        fileMime: "TEXT/PLAIN",
        file: btoa("release notes"),
      }};
      const attachment = attachmentFromEntry(entry);
      const normalized = await normalizeAttachmentForProof(attachment);
      process.stdout.write(JSON.stringify({{
        attachment,
        normalized,
        oversized: attachmentFromEntry({{
          ...entry,
          file: "A".repeat(Math.ceil(1024 * 1024 * 4 / 3) + 8),
        }}),
      }}));
    """
    result = _run_module(script)
    assert result["attachment"] == {
        "fileName": "release-notes.txt",
        "fileMime": "text/plain",
        "file": "cmVsZWFzZSBub3Rlcw==",
        "size": 13,
    }
    assert result["normalized"]["fileName"] == "release-notes.txt"
    assert result["normalized"]["fileMime"] == "text/plain"
    assert result["normalized"]["size"] == 13
    assert len(result["normalized"]["digest"]) == 43
    assert result["oversized"] is None
