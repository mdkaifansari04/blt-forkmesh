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
const MAX_TEXT = 16000;
const MAX_NAME = 32;

const logEl = document.querySelector("#chat-log");
const nameInput = document.querySelector("#chat-name");
const input = document.querySelector("#chat-input");
const sendBtn = document.querySelector("#chat-send");
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

function appendMessage(kind, who, text) {
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
  if (text) appendMessage(kind, who, text);
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
  socket = new WebSocket(`${scheme}//${location.host}${CHAT_WS_PATH}`);

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
    appendMessage("self", plain.sender, text.slice(0, MAX_TEXT));
  });
}

if (logEl && input && sendBtn) {
  // Connect lazily on first intent so passive visitors aren't counted as
  // clients; only people who actually chat join the room.
  input.addEventListener("focus", connect);
  nameInput.addEventListener("focus", connect);
  sendBtn.addEventListener("click", sendCurrentMessage);
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      sendCurrentMessage();
    }
  });
}
