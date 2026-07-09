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
const FORKBOT_ENDPOINT = "/api/forkbot/chat";
const FORKBOT_SENDER_ID = "forkbot";
const FORKBOT_MENTION_RE = /(?:^|[^A-Za-z0-9_-])@?forkbot\b/i;
// Mainnode base host for the room WebSocket. Defaults to the origin that served
// this page, so a self-hosted mainnode's own site talks to itself with zero
// config. A separately-hosted static site can point at a different relay by
// setting window.FORKMESH_RELAY_HOST (e.g. "relay.example.com") before this
// script loads. See public docs: /docs#self-hosting.
const RELAY_HOST = window.FORKMESH_RELAY_HOST || location.host;
const MAX_TEXT = 16000;
const MAX_NAME = 32;
const CHAT_MENTION_RE = /(^|[^A-Za-z0-9_-])@([a-z](?:[a-z0-9-]{0,61}[a-z0-9])?)\b/gi;

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
let cachedUserSession = null;
const seen = new Set();
// messageId -> { el, senderId } for messages currently on screen, so an edit or
// a (regular / admin) delete can find and update or remove the right row.
const rows = new Map();
// Rolling buffer of the most recent decrypted messages, forwarded to ForkBot so
// it can resolve references like "that bug" from the conversation. The room is
// end-to-end encrypted, so the relay only ever sees what we choose to send here.
const recentContext = [];
const RECENT_CONTEXT_MAX = 20;
function rememberContext(sender, text) {
  const clean = String(text || "").trim();
  if (!clean) return;
  recentContext.push({ sender: String(sender || "").slice(0, MAX_NAME), text: clean });
  if (recentContext.length > RECENT_CONTEXT_MAX) recentContext.shift();
}
const mentionProfileCache = new Map();
let mentionCardEl = null;
let activeMentionAnchor = null;
let mentionHideTimer = null;

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

function readSession() {
  try {
    return JSON.parse(localStorage.getItem("forkmesh.session") || "null");
  } catch (error) {
    return null;
  }
}

function writeSession(session) {
  try {
    localStorage.setItem("forkmesh.session", JSON.stringify(session));
  } catch (error) {
    /* storage disabled */
  }
}

function isUserLikeSession(session) {
  if (!session || !session.nodeName) return false;
  if (session.kind === "user") return true;
  if (session.kind === "node") return false;
  // Legacy browser sessions created before account kind was persisted still
  // carry email + nodeName for user accounts. Accept them, then hydrate below.
  return Boolean(session.email);
}

async function hydrateUserSession() {
  const session = readSession();
  if (isUserLikeSession(session)) {
    cachedUserSession = session;
    if (session.kind === "user") return session;
  } else if (!session || !session.nodeName || session.kind === "node") {
    cachedUserSession = null;
    return null;
  }
  try {
    const res = await fetch("/api/accounts/" + encodeURIComponent(session.nodeName), {
      headers: { accept: "application/json" },
    });
    if (!res.ok) return cachedUserSession;
    const body = await res.json();
    if (!body || body.kind !== "user") {
      cachedUserSession = null;
      return null;
    }
    cachedUserSession = {
      ...session,
      nodeName: body.nodeName || session.nodeName,
      email: body.email || session.email || "",
      status: body.status || session.status || "active",
      pubkey: body.pubkey || session.pubkey || "",
      emailVerified: Boolean(body.emailVerified),
      isAdmin: Boolean(body.isAdmin),
      adminUrl: body.adminUrl || session.adminUrl || "",
      solana: body.solana || session.solana || "",
      hasPayoutAddress: Boolean(body.hasPayoutAddress),
      avatarPng: body.avatarPng || session.avatarPng || "",
      avatarUpdatedAt: Number(body.avatarUpdatedAt) || 0,
      kind: "user",
      owner: body.owner || "",
      nodes: Array.isArray(body.nodes) ? body.nodes : [],
      at: Date.now(),
    };
    writeSession(cachedUserSession);
    return cachedUserSession;
  } catch (error) {
    return cachedUserSession;
  }
}

function userSession() {
  if (isUserLikeSession(cachedUserSession)) return cachedUserSession;
  const session = readSession();
  return isUserLikeSession(session) ? session : null;
}

function displayName() {
  const session = userSession();
  if (session) return String(session.nodeName).trim().slice(0, MAX_NAME);
  const value = (nameInput.value || "").trim();
  return (value || "web-guest").slice(0, MAX_NAME);
}

function setStatus(text) {
  if (statusEl) statusEl.textContent = text;
}

function lockChatForNonUser() {
  setStatus("User login required");
  [input, sendBtn, nameInput].forEach((el) => {
    if (el) el.disabled = true;
  });
  if (logEl) {
    const empty = logEl.querySelector(".chat-empty");
    if (empty) empty.textContent = "Log in as a user to join the room.";
  }
}

function unlockChatForUser() {
  [input, sendBtn].forEach((el) => {
    if (el) el.disabled = false;
  });
  const session = userSession();
  if (nameInput && session) {
    nameInput.value = session.nodeName || "";
    nameInput.disabled = true;
  }
}

function clearEmpty() {
  const empty = logEl.querySelector(".chat-empty");
  if (empty) empty.remove();
}

function mentionName(value) {
  return String(value || "").trim().toLowerCase();
}

function mentionProfilePath(name) {
  const key = mentionName(name);
  return key ? "/@" + encodeURIComponent(key) : "#";
}

async function fetchMentionProfile(name) {
  const key = mentionName(name);
  if (!key) return null;
  if (mentionProfileCache.has(key)) return mentionProfileCache.get(key);
  const pending = fetch("/api/accounts/" + encodeURIComponent(key), {
    headers: { accept: "application/json" },
  }).then(async (response) => {
    if (!response.ok) return null;
    const profile = await response.json().catch(() => null);
    if (!profile || profile.exists === false || profile.kind !== "user") return null;
    return profile;
  }).catch(() => null);
  mentionProfileCache.set(key, pending);
  return pending;
}

function mentionSummary(profile) {
  if (!profile) return "User profile";
  const bio = String(profile.profileBio || "").trim();
  if (bio) return bio.length > 120 ? bio.slice(0, 117) + "..." : bio;
  const nodes = Array.isArray(profile.nodes)
    ? profile.nodes.map((node) => String(node || "").trim()).filter(Boolean)
    : [];
  if (nodes.length) return "Nodes: " + nodes.slice(0, 3).join(", ");
  return profile.status ? String(profile.status) : "User profile";
}

function ensureMentionCard() {
  if (mentionCardEl) return mentionCardEl;
  mentionCardEl = document.createElement("a");
  mentionCardEl.className = "chat-mention-card";
  mentionCardEl.hidden = true;
  mentionCardEl.addEventListener("mouseenter", () => {
    if (mentionHideTimer) clearTimeout(mentionHideTimer);
  });
  mentionCardEl.addEventListener("mouseleave", hideMentionCardSoon);
  document.body.append(mentionCardEl);
  return mentionCardEl;
}

function renderMentionCard(card, name, profile) {
  const display = String(profile?.name || profile?.nodeName || name || "").trim() || name;
  card.textContent = "";
  card.href = mentionProfilePath(name);
  const title = document.createElement("strong");
  title.textContent = "@" + display;
  const summary = document.createElement("span");
  summary.textContent = mentionSummary(profile);
  const action = document.createElement("small");
  action.textContent = "Click to open full profile";
  card.append(title, summary, action);
}

function positionMentionCard(anchor, card) {
  const rect = anchor.getBoundingClientRect();
  const margin = 12;
  const width = card.offsetWidth || 240;
  const x = Math.max(margin, Math.min(rect.left, window.innerWidth - width - margin));
  const y = Math.min(rect.bottom + 8, window.innerHeight - card.offsetHeight - margin);
  card.style.left = `${x + window.scrollX}px`;
  card.style.top = `${Math.max(margin, y) + window.scrollY}px`;
}

function hideMentionCardSoon() {
  if (mentionHideTimer) clearTimeout(mentionHideTimer);
  mentionHideTimer = setTimeout(() => {
    if (mentionCardEl) mentionCardEl.hidden = true;
    activeMentionAnchor = null;
  }, 140);
}

async function showMentionCard(anchor, name) {
  const key = mentionName(name);
  if (!key) return;
  if (mentionHideTimer) clearTimeout(mentionHideTimer);
  activeMentionAnchor = anchor;
  const card = ensureMentionCard();
  renderMentionCard(card, key, null);
  card.hidden = false;
  positionMentionCard(anchor, card);
  const profile = await fetchMentionProfile(key);
  if (activeMentionAnchor !== anchor) return;
  renderMentionCard(card, key, profile);
  positionMentionCard(anchor, card);
}

function appendMentionText(container, text) {
  const value = String(text || "");
  const mentionRe = new RegExp(CHAT_MENTION_RE.source, "gi");
  let cursor = 0;
  let match;
  while ((match = mentionRe.exec(value)) !== null) {
    const prefix = match[1] || "";
    const name = mentionName(match[2]);
    const start = match.index + prefix.length;
    const end = mentionRe.lastIndex;
    if (start > cursor) container.append(document.createTextNode(value.slice(cursor, start)));
    const anchor = document.createElement("a");
    anchor.className = "chat-mention";
    anchor.href = mentionProfilePath(name);
    anchor.dataset.chatMention = name;
    anchor.textContent = value.slice(start, end);
    anchor.addEventListener("mouseenter", () => showMentionCard(anchor, name));
    anchor.addEventListener("focus", () => showMentionCard(anchor, name));
    anchor.addEventListener("mouseleave", hideMentionCardSoon);
    anchor.addEventListener("blur", hideMentionCardSoon);
    container.append(anchor);
    cursor = end;
  }
  if (cursor < value.length) container.append(document.createTextNode(value.slice(cursor)));
}

function renderMessageText(container, text) {
  container.textContent = "";
  appendMentionText(container, text);
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
  appendMentionText(body, text);
  row.append(author, body);
  logEl.append(row);
  logEl.scrollTop = logEl.scrollHeight;
  if (id) {
    rows.set(id, { el: row, senderId: senderId || "", body });
  }
  rememberContext(who, text);
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
  empty.textContent = userSession()
    ? "Type below to join the room."
    : "Log in as a user to join the room.";
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
      accountKind: "user",
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
  if (!entry || entry.accountKind !== "user") return;
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
  if (type !== "history" && plain.accountKind !== "user") return;
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
      renderMessageText(rec.body, plain.text || "");
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

const DURABLE_TYPES = new Set(["chat", "edit", "delete", "reaction", "admin-delete"]);

function send(plain) {
  if (!userSession()) {
    lockChatForNonUser();
    return Promise.resolve();
  }
  return encryptObject(plain).then((envelope) => {
    if (DURABLE_TYPES.has(plain && plain.type)) envelope.persist = true;
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(envelope));
    }
  });
}

function makeForkbotPlain(text) {
  return makePlain("chat", {
    channel: CHANNEL,
    text: String(text || "").slice(0, MAX_TEXT),
    sender: "forkbot",
    senderId: FORKBOT_SENDER_ID,
    accountKind: "user",
  });
}

function broadcastForkbotMessage(text) {
  const plain = makeForkbotPlain(text);
  send(plain);
  seen.add(plain.id);
  appendMessage("peer", plain.sender, plain.text, plain.id, plain.senderId);
}

async function maybeAskForkbot(text) {
  if (!FORKBOT_MENTION_RE.test(text || "")) return;
  // The triggering line is the last buffer entry (appendMessage ran just
  // before this) and is sent separately as `message`; drop it, drop ForkBot's
  // own replies, and cap the rest so ForkBot sees the lead-up conversation.
  const context = recentContext
    .slice(0, -1)
    .filter((m) => m.sender.toLowerCase() !== "forkbot")
    .slice(-12);
  try {
    const response = await fetch(FORKBOT_ENDPOINT, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ message: text, sender: displayName(), room: ROOM_NAME, context }),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || !data || !data.botMessage) return;
    runWhenConnected(() => broadcastForkbotMessage(data.botMessage));
  } catch (error) {
    appendSystem("forkbot is unavailable");
  }
}

async function connect() {
  if (socket || connecting) return;
  if (!userSession()) {
    lockChatForNonUser();
    return;
  }
  connecting = true;
  setStatus("Connecting...");
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
  if (!userSession()) {
    lockChatForNonUser();
    return;
  }
  const text = input.value.trim();
  if (!text) return;
  input.value = "";
  runWhenConnected(() => {
    const clipped = text.slice(0, MAX_TEXT);
    const plain = makePlain("chat", { channel: CHANNEL, text: clipped });
    send(plain);
    seen.add(plain.id); // we render it here; ignore the echo if one comes back
    appendMessage("self", plain.sender, clipped, plain.id, plain.senderId);
    maybeAskForkbot(clipped);
  });
}

async function initChat() {
  await hydrateUserSession();
  // Connect right away for signed-in users so retained room history is visible
  // without first focusing an input.
  if (userSession()) {
    unlockChatForUser();
    connect();
  } else {
    lockChatForNonUser();
  }
  sendBtn.addEventListener("click", sendCurrentMessage);
  if (clearBtn) clearBtn.addEventListener("click", clearChat);
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      sendCurrentMessage();
    }
  });
}

if (logEl && input && sendBtn) {
  initChat();
}
