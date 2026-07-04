// Browser-side ForkMesh room chat. Reimplements the desktop client's room
// crypto (PBKDF2 + AES-256-GCM) and message envelope so the website can join
// the public encrypted "general" room and talk to connected clients. The relay
// only ever sees ciphertext.

const ROOM_NAME = "general";
// Baked-in app key for the passphrase-free shared rooms. MUST stay byte-for-byte
// identical to the desktop client's kAppRoomKey (qt_client RoomCrypto.cpp) and the
// Flutter app's _appRoomKey (room_crypto.dart); the PBKDF2 password is what binds
// every client to the same AES key, so a mismatch silently drops all messages.
const ROOM_PASSPHRASE = "forkmesh-shared-room-key-v1";
const CHANNEL = "#general";
const CHAT_WS_PATH = "/api/repo/mainnode/forkmesh/rooms/general/ws";
// Mainnode base host for the room WebSocket. Defaults to the origin that served
// this page, so a self-hosted mainnode's own site talks to itself with zero
// config. A separately-hosted static site can point at a different relay by
// setting window.FORKMESH_RELAY_HOST (e.g. "relay.example.com") before this
// script loads. See docs/protocol.md ("Self-hosting a mainnode").
const RELAY_HOST = window.FORKMESH_RELAY_HOST || location.host;
const MAX_TEXT = 16000;
const MAX_NAME = 32;

const logEl = document.querySelector("#chat-log");
const nameInput = document.querySelector("#chat-name");
const input = document.querySelector("#chat-input");
const sendBtn = document.querySelector("#chat-send");
const clearBtn = document.querySelector("#chat-clear");
const statusEl = document.querySelector("#chat-status");

const enc = new TextEncoder();
const dec = new TextDecoder();
const selfId =
  (crypto.randomUUID && crypto.randomUUID()) ||
  String(Math.random()).slice(2) + Date.now();

let roomKey = null;
let socket = null;
let connecting = false;
let openCallbacks = [];
const seen = new Set();
// messageId -> { el, senderId } for messages currently on screen, so an edit or
// a (regular / admin) delete can find and update or remove the right row.
const rows = new Map();

// ---- base64 <-> bytes -------------------------------------------------------

function bytesToB64(bytes) {
  const arr = new Uint8Array(bytes);
  let bin = "";
  for (let i = 0; i < arr.length; i += 1) bin += String.fromCharCode(arr[i]);
  return btoa(bin);
}

function b64ToBytes(value) {
  const bin = atob(value);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i += 1) out[i] = bin.charCodeAt(i);
  return out;
}

// The desktop identity encodes keys/signatures as unpadded base64url.
function b64urlToBytes(value) {
  let s = (value || "").replace(/-/g, "+").replace(/_/g, "/");
  while (s.length % 4) s += "=";
  return b64ToBytes(s);
}

// Verify an Ed25519 signature (raw 32-byte key, 64-byte sig) over a UTF-8
// string. Returns false on any unsupported-browser / malformed-input error, so
// a delete we can't authenticate is simply ignored rather than applied.
async function ed25519Verify(pubB64url, sigB64url, dataStr) {
  try {
    const key = await crypto.subtle.importKey(
      "raw",
      b64urlToBytes(pubB64url),
      { name: "Ed25519" },
      false,
      ["verify"]
    );
    return await crypto.subtle.verify(
      { name: "Ed25519" },
      key,
      b64urlToBytes(sigB64url),
      enc.encode(dataStr)
    );
  } catch (error) {
    return false;
  }
}

// ---- room crypto (matches RoomCrypto.cpp) -----------------------------------

async function deriveRoomKey() {
  const saltDigest = new Uint8Array(
    await crypto.subtle.digest("SHA-256", enc.encode("ForkMesh room:" + ROOM_NAME))
  );
  const salt = saltDigest.slice(0, 16);
  const baseKey = await crypto.subtle.importKey(
    "raw",
    enc.encode(ROOM_PASSPHRASE),
    "PBKDF2",
    false,
    ["deriveKey"]
  );
  return crypto.subtle.deriveKey(
    { name: "PBKDF2", salt, iterations: 210000, hash: "SHA-256" },
    baseKey,
    { name: "AES-GCM", length: 256 },
    false,
    ["encrypt", "decrypt"]
  );
}

async function encryptObject(obj) {
  const nonce = crypto.getRandomValues(new Uint8Array(12));
  const combined = new Uint8Array(
    await crypto.subtle.encrypt(
      { name: "AES-GCM", iv: nonce, tagLength: 128 },
      roomKey,
      enc.encode(JSON.stringify(obj))
    )
  );
  // The desktop stores ciphertext and the 16-byte GCM tag separately.
  const body = combined.slice(0, combined.length - 16);
  const tag = combined.slice(combined.length - 16);
  return {
    kind: "cipher",
    v: 1,
    nonce: bytesToB64(nonce),
    tag: bytesToB64(tag),
    body: bytesToB64(body),
  };
}

async function decryptObject(envelope) {
  if (!envelope || envelope.kind !== "cipher") return null;
  try {
    const nonce = b64ToBytes(envelope.nonce);
    const body = b64ToBytes(envelope.body);
    const tag = b64ToBytes(envelope.tag);
    const combined = new Uint8Array(body.length + tag.length);
    combined.set(body, 0);
    combined.set(tag, body.length);
    const plain = await crypto.subtle.decrypt(
      { name: "AES-GCM", iv: nonce, tagLength: 128 },
      roomKey,
      combined
    );
    return JSON.parse(dec.decode(plain));
  } catch (error) {
    return null;
  }
}

// ---- UI ---------------------------------------------------------------------

function displayName() {
  const value = (nameInput.value || "").trim();
  return (value || "web-guest").slice(0, MAX_NAME);
}

function setStatus(text) {
  if (statusEl) statusEl.textContent = text;
}

function clearEmpty() {
  const empty = logEl.querySelector(".chat-empty");
  if (empty) empty.remove();
}

function appendMessage(kind, who, text, id, senderId) {
  clearEmpty();
  const row = document.createElement("div");
  row.className = `chat-msg chat-msg-${kind}`;
  const author = document.createElement("span");
  author.className = "chat-author";
  author.textContent = who;
  const body = document.createElement("span");
  body.className = "chat-text";
  body.textContent = text;
  row.append(author, body);
  logEl.append(row);
  logEl.scrollTop = logEl.scrollHeight;
  if (id) {
    rows.set(id, { el: row, senderId: senderId || "", body });
  }
}

// Drop a message row from the screen (a deletion leaves no tombstone, matching
// the desktop client). Keep its id in `seen` so a late duplicate can't reappear.
function removeMessage(id) {
  const rec = rows.get(id);
  if (!rec) return;
  if (rec.el && rec.el.parentNode) rec.el.parentNode.removeChild(rec.el);
  rows.delete(id);
}

// Wipe the on-screen transcript and restore the empty placeholder. This is a
// local view-only clear: it doesn't delete anything on the relay or for other
// clients. Cleared ids stay in `seen` so a replayed history can't bring them
// back, while genuinely new incoming messages still appear.
function clearChat() {
  rows.clear();
  logEl.textContent = "";
  const empty = document.createElement("div");
  empty.className = "chat-empty";
  empty.textContent = "Type below to join the room.";
  logEl.append(empty);
}

function appendSystem(text) {
  clearEmpty();
  const row = document.createElement("div");
  row.className = "chat-system";
  row.textContent = text;
  logEl.append(row);
  logEl.scrollTop = logEl.scrollHeight;
}

// ---- protocol ---------------------------------------------------------------

function makePlain(type, extra) {
  return Object.assign(
    {
      type,
      id:
        (crypto.randomUUID && crypto.randomUUID()) ||
        String(Math.random()).slice(2) + Date.now(),
      senderId: selfId,
      sender: displayName(),
      ts: Date.now(),
    },
    extra || {}
  );
}

function once(id) {
  if (!id || seen.has(id)) return false;
  seen.add(id);
  return true;
}

function renderChatEntry(entry, kind) {
  if (!once(entry.id)) return;
  const who = (entry.sender || "peer").slice(0, MAX_NAME);
  const text = entry.fileName
    ? "📎 " + entry.fileName
    : entry.text || "";
  if (text) appendMessage(kind, who, text, entry.id, entry.senderId);
}

// An admin-delete frame is signed by the admin's identity key over a canonical
// string (must match the desktop's adminDeleteCanonical byte-for-byte). We only
// remove the message after verifying that signature AND confirming the signer's
// account is flagged admin on the relay — otherwise any room member could forge
// one (chat frames are otherwise unsigned).
async function verifyAdminDelete(plain) {
  const target = plain.target;
  const adminId = plain.senderId; // desktop node id == identity pubkey (b64url)
  const sig = plain.sig;
  if (!target || !sig || !adminId) return false;
  const canonical =
    "forkmesh-admin-delete-v1\n" +
    (plain.conversation || "") +
    "\n" +
    target +
    "\n" +
    adminId +
    "\n" +
    String(plain.ts);
  if (!(await ed25519Verify(adminId, sig, canonical))) return false;
  try {
    const name = (plain.sender || "").trim().toLowerCase();
    if (!name) return false;
    const res = await fetch("/api/accounts/" + encodeURIComponent(name));
    const rec = await res.json();
    return rec && rec.isAdmin === true && rec.pubkey === adminId;
  } catch (error) {
    return false;
  }
}

function handlePlain(plain) {
  const type = plain.type;
  const sender = (plain.sender || "peer").slice(0, MAX_NAME);
  if (type === "chat") {
    renderChatEntry(plain, "peer");
  } else if (type === "history") {
    for (const entry of plain.entries || []) {
      if (entry && (entry.channel || entry.text || entry.fileName)) {
        renderChatEntry(entry, "peer");
      }
    }
  } else if (type === "edit") {
    // The author edited their own message; only honour it from that author.
    const rec = rows.get(plain.target);
    if (rec && rec.senderId === plain.senderId && rec.body) {
      rec.body.textContent = plain.text || "";
    }
  } else if (type === "delete") {
    // A plain delete is only valid from the message's own author.
    const rec = rows.get(plain.target);
    if (rec && rec.senderId === plain.senderId) removeMessage(plain.target);
  } else if (type === "admin-delete") {
    // Moderation: remove any message once the admin signature checks out.
    verifyAdminDelete(plain).then((ok) => {
      if (!ok) return;
      seen.add(plain.target); // also suppress a copy that arrives after the delete
      removeMessage(plain.target);
    });
  } else if (type === "hello" && !plain.to) {
    appendSystem(sender + " joined");
  } else if (type === "bye") {
    appendSystem(sender + " left");
  }
}

async function onFrame(event) {
  if (typeof event.data !== "string") return;
  let envelope;
  try {
    envelope = JSON.parse(event.data);
  } catch (error) {
    return;
  }
  const plain = await decryptObject(envelope);
  if (!plain || plain.senderId === selfId) return;
  handlePlain(plain);
}

function send(plain) {
  return encryptObject(plain).then((envelope) => {
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(envelope));
    }
  });
}

async function connect() {
  if (socket || connecting) return;
  connecting = true;
  setStatus("Connecting…");
  try {
    if (!roomKey) roomKey = await deriveRoomKey();
  } catch (error) {
    connecting = false;
    setStatus("Encryption unavailable in this browser");
    return;
  }
  const scheme = location.protocol === "https:" ? "wss:" : "ws:";
  socket = new WebSocket(`${scheme}//${RELAY_HOST}${CHAT_WS_PATH}`);

  socket.addEventListener("open", () => {
    connecting = false;
    setStatus("Connected · end-to-end encrypted");
    // Announce ourselves so clients add us to their roster and replay history.
    send(makePlain("hello", { channels: [CHANNEL] }));
    const callbacks = openCallbacks;
    openCallbacks = [];
    callbacks.forEach((cb) => cb());
  });
  socket.addEventListener("message", onFrame);
  socket.addEventListener("close", () => {
    socket = null;
    connecting = false;
    setStatus("Disconnected · send to rejoin");
  });
  socket.addEventListener("error", () => {
    if (socket) socket.close();
  });
}

function runWhenConnected(callback) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    callback();
    return;
  }
  openCallbacks.push(callback);
  connect();
}

function sendCurrentMessage() {
  const text = input.value.trim();
  if (!text) return;
  input.value = "";
  runWhenConnected(() => {
    const plain = makePlain("chat", { channel: CHANNEL, text: text.slice(0, MAX_TEXT) });
    send(plain);
    seen.add(plain.id); // we render it here; ignore the echo if one comes back
    appendMessage("self", plain.sender, text.slice(0, MAX_TEXT), plain.id, plain.senderId);
  });
}

if (logEl && input && sendBtn) {
  // Connect right away so the room's message history (replayed by the relay
  // on WebSocket open) is visible to anyone who loads the page, not just
  // people who start typing.
  connect();
  sendBtn.addEventListener("click", sendCurrentMessage);
  if (clearBtn) clearBtn.addEventListener("click", clearChat);
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      sendCurrentMessage();
    }
  });
}
