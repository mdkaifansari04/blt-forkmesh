// Browser-side ForkMesh room chat. Reimplements the desktop client's room
// crypto (PBKDF2 + AES-256-GCM) and message envelope so the website can join
// the public encrypted rooms and talk to connected clients. Frames are
// ciphertext on the wire, but the relay derives the default shared passphrase
// and can therefore decrypt them; this is not end-to-end encryption.
//
// Layout: rooms on the left, conversation in the middle (ts-ordered, newest at
// the bottom, avatars + timestamps + reactions), and people on the right.
// Public #general uses its own "world-general" room; authenticated channels
// retain the desktop-compatible "general" room and channel field.

const ROOM_NAME = "general";
const PUBLIC_WORLD_GENERAL_ROOM = "world-general";
// The room key is no longer a public constant. Every client fetches a shared
// passphrase (derived server-side from the relay's DATA_KEY) from
// /api/chat/room-key and feeds it into the same PBKDF2 room-key derivation, so
// clients in a given scope converge on the same AES key. The public World
// #general endpoint is intentionally guest-readable; every other room-key
// request still requires repository-authorized authentication. The relay
// controls DATA_KEY and can derive either key too. Keys are cached by scope.
const AUTHENTICATED_ROOM_KEY_ENDPOINT =
  "/api/chat/room-key?owner=mainnode&repo=forkmesh";
const PUBLIC_WORLD_ROOM_KEY_ENDPOINT =
  AUTHENTICATED_ROOM_KEY_ENDPOINT + "&room=world-general";
const DEFAULT_CHANNELS = ["#general", "#welcome", "#random"];
const AUTHENTICATED_CHAT_WS_PATH =
  "/api/repo/mainnode/forkmesh/rooms/general/ws";
const PUBLIC_WORLD_CHAT_WS_PATH =
  "/api/repo/mainnode/forkmesh/rooms/world-general/ws";
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
// Presence cadence + staleness mirror the desktop node (ServerNode.cpp:
// kPresenceIntervalMs / kPeerStaleMs). The beat doubles as the keep-alive the
// relay's room DO needs — it closes sockets with no frames for 3 minutes, which
// is why the old web client (which sent nothing while idle) kept disconnecting.
const PRESENCE_INTERVAL_MS = 60000;
// Registered-user directory (public, no secrets — see _account_users_directory
// in the Worker). Seeds the people pane so every account shows even before it
// has ever spoken in the room, and a light poll (the endpoint is edge-cached,
// and a signup invalidates that cache) surfaces brand-new signups right away
// with a welcome line in the log.
const USERS_DIRECTORY_ENDPOINT = "/api/accounts/users";
const USERS_DIRECTORY_REFRESH_MS = 60000;
// Counters behind the site-header chat badge; the chat page re-baselines them
// while open so time spent reading here counts as "seen".
const CHAT_ACTIVITY_ENDPOINT = "/api/chat/activity";
const CHAT_ACTIVITY_SEEN_KEY = "forkmesh.chat.activitySeen";
// Only a directory entry created this recently gets the "just joined" welcome
// line — an account merely missing from the previous (top-1000) page isn't news.
const NEW_USER_ANNOUNCE_WINDOW_MS = 10 * 60 * 1000;
const PEER_STALE_MS = 180000;
const GROUP_WINDOW_MS = 5 * 60 * 1000; // same-sender messages collapse under one header
const REACTION_EMOJI = ["👍", "❤️", "😂", "🎉", "👀", "🚀"];
const ACTIVE_CHANNEL_KEY = "forkmesh.chat.channel";

const logEl = document.querySelector("#chat-log");
const nameInput = document.querySelector("#chat-name");
const input = document.querySelector("#chat-input");
const sendBtn = document.querySelector("#chat-send");
const clearBtn = document.querySelector("#chat-clear");
const statusEl = document.querySelector("#chat-status");
const roomsEl = document.querySelector("#chat-rooms");
const peopleEl = document.querySelector("#chat-people");
const peopleTitleEl = document.querySelector("#chat-people-title");
const channelTitleEl = document.querySelector("#chat-channel-title");

const enc = new TextEncoder();
const dec = new TextDecoder();
// A stable per-browser chat id. The relay holds no roster — every participant
// is reconstructed client-side from the senderId on decrypted frames — so a
// fresh random id per page load made each reload/tab of the same person show up
// as a brand-new "ghost" participant (the "lots of jett users" symptom).
// Persisting it collapses reloads/tabs of the same browser into one entry;
// signed-in users still display under their account name via displayName().
const selfId = (() => {
  const STORAGE_KEY = "forkmesh.chat.selfId";
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved) return saved;
  } catch (_) {}
  const fresh =
    (crypto.randomUUID && crypto.randomUUID()) ||
    String(Math.random()).slice(2) + Date.now();
  try { localStorage.setItem(STORAGE_KEY, fresh); } catch (_) {}
  return fresh;
})();

const roomPassphrases = new Map();
const roomKeys = new Map();
let roomKey = null;
let socket = null;
let socketScope = "";
let connecting = false;
let openCallbacks = [];
let cachedUserSession = null;
let reconnectDelayMs = 2000;
let reconnectTimer = null;
const seen = new Set();
// messageId -> message record { id, channel, ts, senderId, sender, text, self,
// el, body, reactionsEl }. `el`/`body` reference the on-screen row while the
// record's channel is the active one, so an edit or a (regular / admin) delete
// can find and update or remove the right row.
const rows = new Map();
// channel -> ts-ordered array of message records (newest last).
const channelMessages = new Map();
// channel -> { unread } for the rooms list badges.
const channelMeta = new Map();
// messageId -> Map(emoji -> Map(reactorId -> reactorName)); same shape as the
// desktop's m_reactions so toggles converge across clients.
const reactions = new Map();
// senderId -> { id, name, kind: "user"|"guest"|"bot"|"node", lastSeenMs } built from
// every decrypted frame; drives the right-hand people pane.
const roster = new Map();
let activeChannel = DEFAULT_CHANNELS[0];
try {
  const saved = localStorage.getItem(ACTIVE_CHANNEL_KEY);
  if (saved && saved.startsWith("#")) activeChannel = saved;
} catch (_) {}
// Rolling per-channel buffer of the most recent decrypted messages, forwarded
// to ForkBot so it can resolve references like "that bug" from the
// conversation. The relay can decrypt the default shared-key room; this buffer
// controls only the narrower context we explicitly send to ForkBot.
const recentContext = new Map(); // channel -> [{sender, text}]
const RECENT_CONTEXT_MAX = 20;
function rememberContext(channel, sender, text) {
  const clean = String(text || "").trim();
  if (!clean) return;
  const list = recentContext.get(channel) || [];
  recentContext.set(channel, list);
  list.push({ sender: String(sender || "").slice(0, MAX_NAME), text: clean });
  if (list.length > RECENT_CONTEXT_MAX) list.shift();
}
const mentionProfileCache = new Map();
let mentionCardEl = null;
let activeMentionAnchor = null;
let mentionHideTimer = null;
let emojiPickerEl = null;
let emojiPickerTarget = null;

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

function roomScopeForChannel(channel = activeChannel) {
  return channel === "#general"
    ? "public-world-general"
    : "authenticated";
}

function canJoinChannel(channel = activeChannel) {
  return roomScopeForChannel(channel) === "public-world-general" ||
    Boolean(userSession());
}

function roomNameForScope(scope) {
  return scope === "public-world-general"
    ? PUBLIC_WORLD_GENERAL_ROOM
    : ROOM_NAME;
}

function roomKeyEndpointForScope(scope) {
  return scope === "public-world-general"
    ? PUBLIC_WORLD_ROOM_KEY_ENDPOINT
    : AUTHENTICATED_ROOM_KEY_ENDPOINT;
}

function roomWebSocketPathForScope(scope) {
  return scope === "public-world-general"
    ? PUBLIC_WORLD_CHAT_WS_PATH
    : AUTHENTICATED_CHAT_WS_PATH;
}

// The public World #general passphrase is intentionally available to guests
// and is isolated in its own DO room. Every other channel keeps the
// authenticated repository-room passphrase.
async function fetchRoomPassphrase(scope = roomScopeForChannel()) {
  if (roomPassphrases.has(scope)) return roomPassphrases.get(scope);
  const session = userSession();
  const token = session && session.sessionToken;
  const headers = { accept: "application/json" };
  if (token) headers.authorization = "Bearer " + token;
  const res = await fetch(roomKeyEndpointForScope(scope), {
    headers,
    cache: "no-store",
  });
  if (!res.ok) {
    const err = new Error(
      scope === "public-world-general"
        ? "Public World #general key unavailable."
        : "Sign in to join this authenticated chat."
    );
    err.code = res.status === 401 || res.status === 403 ? "auth" : "server";
    throw err;
  }
  const data = await res.json().catch(() => ({}));
  if (!data || !data.passphrase) throw new Error("Room key unavailable.");
  const passphrase = String(data.passphrase);
  roomPassphrases.set(scope, passphrase);
  return passphrase;
}

async function deriveRoomKey(scope = roomScopeForChannel()) {
  if (roomKeys.has(scope)) return roomKeys.get(scope);
  const passphrase = await fetchRoomPassphrase(scope);
  const saltDigest = new Uint8Array(
    await crypto.subtle.digest(
      "SHA-256",
      enc.encode("ForkMesh room:" + roomNameForScope(scope))
    )
  );
  const salt = saltDigest.slice(0, 16);
  const baseKey = await crypto.subtle.importKey(
    "raw",
    enc.encode(passphrase),
    "PBKDF2",
    false,
    ["deriveKey"]
  );
  const key = await crypto.subtle.deriveKey(
    { name: "PBKDF2", salt, iterations: 210000, hash: "SHA-256" },
    baseKey,
    { name: "AES-GCM", length: 256 },
    false,
    ["encrypt", "decrypt"]
  );
  roomKeys.set(scope, key);
  return key;
}

async function encryptObject(obj, key = roomKey) {
  const nonce = crypto.getRandomValues(new Uint8Array(12));
  const combined = new Uint8Array(
    await crypto.subtle.encrypt(
      { name: "AES-GCM", iv: nonce, tagLength: 128 },
        key,
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

async function decryptObject(envelope, key = roomKey) {
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
        key,
      combined
    );
    return JSON.parse(dec.decode(plain));
  } catch (error) {
    return null;
  }
}

// ---- session ----------------------------------------------------------------

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

function worldVisitorName(value) {
  const asserted = String(value || "")
    .replace(/^World visitor\s*·\s*/i, "")
    .trim()
    .slice(0, 16) || "guest";
  return `World visitor · ${asserted}`.slice(0, MAX_NAME);
}

function displayName() {
  const session = userSession();
  let value;
  if (session) {
    value = String(session.nodeName).trim().slice(0, MAX_NAME);
  } else {
    value = (nameInput.value || "").trim();
    value = (value || "web-guest").slice(0, MAX_NAME);
  }
  return roomScopeForChannel() === "public-world-general"
    ? worldVisitorName(value)
    : value;
}

function setStatus(text) {
  if (statusEl) statusEl.textContent = text;
}

function lockChatForNonUser() {
  setStatus("User login required for this channel");
  [input, sendBtn, nameInput].forEach((el) => {
    if (el) el.disabled = true;
  });
  if (logEl) {
    const empty = logEl.querySelector(".chat-empty");
    if (empty) {
      empty.textContent = (
        "Log in as a user to join this channel. " +
        "Guests can participate only in public World #general."
      );
    }
  }
}

function unlockChatForUser() {
  [input, sendBtn].forEach((el) => {
    if (el) el.disabled = false;
  });
  const session = userSession();
  if (nameInput) {
    if (session) {
      nameInput.value = session.nodeName || "";
      nameInput.disabled = true;
    } else {
      nameInput.value =
        `World Guest ${String(selfId).replace(/[^A-Za-z0-9]/g, "").slice(0, 6)}`;
      nameInput.disabled = true;
    }
  }
}

function clearEmpty() {
  const empty = logEl.querySelector(".chat-empty");
  if (empty) empty.remove();
}

// ---- avatars ------------------------------------------------------------------

const AVATAR_COLORS = [
  "#4f7ddb", "#8250df", "#bf3989", "#cf222e",
  "#bc4c00", "#4d7c0f", "#1f883d", "#0f766e",
];

function avatarColor(name) {
  const key = String(name || "").toLowerCase();
  let hash = 0;
  for (let i = 0; i < key.length; i += 1) hash = (hash * 31 + key.charCodeAt(i)) >>> 0;
  return AVATAR_COLORS[hash % AVATAR_COLORS.length];
}

// Letter avatar immediately; asynchronously swapped for the account's uploaded
// avatarPng when the profile has one (user accounts only — fetchMentionProfile
// resolves null for nodes, which keep the letter).
function makeAvatar(name, kind) {
  const el = document.createElement("div");
  el.className = "chat-avatar";
  const display = String(name || "?").trim() || "?";
  el.style.background = avatarColor(display);
  el.textContent = kind === "bot" ? "🤖" : display.charAt(0).toUpperCase();
  if (kind !== "bot") {
    fetchMentionProfile(display).then((profile) => {
      const png = profile && profile.avatarPng;
      if (!png) return;
      const img = document.createElement("img");
      img.alt = "";
      img.src = png.startsWith("data:") ? png : "data:image/png;base64," + png;
      el.textContent = "";
      el.style.background = "transparent";
      el.append(img);
    });
  }
  return el;
}

function fmtTime(ts) {
  const date = new Date(Number(ts) || Date.now());
  return date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

// ---- mentions -----------------------------------------------------------------

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

// ---- @mention autocomplete -----------------------------------------------------
// Typing "@" (plus an optional partial name) in the composer pops a suggestion
// list built from the roster; Tab (or Enter / click) accepts the highlighted
// name, arrows move, Escape dismisses.

let mentionSuggestEl = null;
// null when closed, else { items, index, start, end } where start/end bound the
// "@partial" token in the input's value.
let mentionSuggest = null;

function ensureMentionSuggest() {
  if (mentionSuggestEl) return mentionSuggestEl;
  mentionSuggestEl = document.createElement("div");
  mentionSuggestEl.className = "chat-mention-suggest";
  mentionSuggestEl.hidden = true;
  const bar = input.closest(".chat-input-bar") || document.body;
  bar.append(mentionSuggestEl);
  return mentionSuggestEl;
}

function closeMentionSuggest() {
  mentionSuggest = null;
  if (mentionSuggestEl) mentionSuggestEl.hidden = true;
}

// The "@partial" token the caret is currently inside, or null. A mention starts
// at "@" preceded by whitespace/start and uses the same charset the renderer
// links (CHAT_MENTION_RE): letters, digits, hyphens.
function mentionTokenAtCaret() {
  const caret = input.selectionStart;
  if (caret == null || input.selectionEnd !== caret) return null;
  const before = input.value.slice(0, caret);
  const match = /(^|\s)@([A-Za-z0-9-]{0,32})$/.exec(before);
  if (!match) return null;
  const start = caret - match[2].length - 1; // include the "@"
  return { start, end: caret, partial: match[2].toLowerCase() };
}

function mentionCandidates(partial) {
  const self = mentionName(displayName());
  const byName = new Map();
  for (const person of roster.values()) {
    const key = mentionName(person.name);
    if (!key || key === self) continue;
    const prev = byName.get(key);
    if (!prev || (personIsOnline(person) && !personIsOnline(prev))) {
      byName.set(key, person);
    }
  }
  if (!byName.has(FORKBOT_SENDER_ID)) {
    byName.set(FORKBOT_SENDER_ID, {
      id: FORKBOT_SENDER_ID, name: "forkbot", kind: "bot", lastSeenMs: Date.now(),
    });
  }
  const matches = [...byName.values()].filter((person) => {
    const key = mentionName(person.name);
    return !partial || key.startsWith(partial) || key.includes(partial);
  });
  matches.sort((a, b) => {
    const ak = mentionName(a.name);
    const bk = mentionName(b.name);
    const aPrefix = partial && ak.startsWith(partial) ? 0 : 1;
    const bPrefix = partial && bk.startsWith(partial) ? 0 : 1;
    return (aPrefix - bPrefix) ||
      (personIsOnline(b) - personIsOnline(a)) ||
      ak.localeCompare(bk);
  });
  return matches.slice(0, 8);
}

function renderMentionSuggest() {
  const box = ensureMentionSuggest();
  box.textContent = "";
  const hint = document.createElement("div");
  hint.className = "chat-mention-suggest-hint";
  hint.textContent = "Tab to mention · Esc to dismiss";
  box.append(hint);
  mentionSuggest.items.forEach((person, index) => {
    const item = document.createElement("button");
    item.type = "button";
    item.className = "chat-mention-suggest-item" +
      (index === mentionSuggest.index ? " is-active" : "");
    item.append(makeAvatar(person.name, person.kind));
    const label = document.createElement("span");
    label.className = "chat-mention-suggest-name";
    label.textContent = person.name;
    const dot = document.createElement("span");
    dot.className = "chat-presence-dot" + (personIsOnline(person) ? " is-online" : "");
    item.append(label, dot);
    // mousedown, not click: click fires after the input's blur would have
    // closed the popup.
    item.addEventListener("mousedown", (event) => {
      event.preventDefault();
      acceptMentionSuggest(index);
    });
    item.addEventListener("mouseenter", () => {
      mentionSuggest.index = index;
      renderMentionSuggest();
    });
    box.append(item);
  });
  box.hidden = false;
  const active = box.querySelector(".is-active");
  if (active) active.scrollIntoView({ block: "nearest" });
}

function updateMentionSuggest() {
  const token = mentionTokenAtCaret();
  if (!token) {
    closeMentionSuggest();
    return;
  }
  const items = mentionCandidates(token.partial);
  if (!items.length) {
    closeMentionSuggest();
    return;
  }
  const prevName = mentionSuggest &&
    mentionSuggest.items[mentionSuggest.index] &&
    mentionName(mentionSuggest.items[mentionSuggest.index].name);
  let index = prevName
    ? items.findIndex((person) => mentionName(person.name) === prevName)
    : 0;
  if (index < 0) index = 0;
  mentionSuggest = { items, index, start: token.start, end: token.end };
  renderMentionSuggest();
}

function acceptMentionSuggest(index) {
  if (!mentionSuggest) return;
  const person = mentionSuggest.items[
    typeof index === "number" ? index : mentionSuggest.index];
  if (!person) return;
  const name = mentionName(person.name);
  const value = input.value;
  const inserted = "@" + name + " ";
  input.value = value.slice(0, mentionSuggest.start) + inserted +
    value.slice(mentionSuggest.end);
  const caret = mentionSuggest.start + inserted.length;
  input.setSelectionRange(caret, caret);
  closeMentionSuggest();
  input.focus();
}

function moveMentionSuggest(step) {
  if (!mentionSuggest) return;
  const count = mentionSuggest.items.length;
  mentionSuggest.index = (mentionSuggest.index + step + count) % count;
  renderMentionSuggest();
}

// ---- rooms (left pane) --------------------------------------------------------

function normalizeChannel(name) {
  const value = String(name || "").trim();
  if (!value || value.length > 40) return "";
  return value.startsWith("#") ? value : "#" + value;
}

function ensureChannel(name) {
  const channel = normalizeChannel(name);
  if (!channel) return "";
  if (!channelMeta.has(channel)) {
    channelMeta.set(channel, { unread: 0 });
    if (!channelMessages.has(channel)) channelMessages.set(channel, []);
    renderRooms();
  }
  return channel;
}

function bumpUnread(channel) {
  const meta = channelMeta.get(channel);
  if (!meta) return;
  meta.unread += 1;
  renderRooms();
}

function renderRooms() {
  if (!roomsEl) return;
  roomsEl.textContent = "";
  const names = [...channelMeta.keys()].sort((a, b) => {
    const ai = DEFAULT_CHANNELS.indexOf(a);
    const bi = DEFAULT_CHANNELS.indexOf(b);
    if (ai >= 0 || bi >= 0) return (ai < 0 ? 99 : ai) - (bi < 0 ? 99 : bi);
    return a.localeCompare(b);
  });
  for (const name of names) {
    const meta = channelMeta.get(name);
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "chat-room" + (name === activeChannel ? " is-active" : "");
    const label = document.createElement("span");
    label.className = "chat-room-name";
    label.textContent = name;
    btn.append(label);
    if (meta.unread > 0 && name !== activeChannel) {
      const badge = document.createElement("span");
      badge.className = "chat-room-unread";
      badge.textContent = meta.unread > 99 ? "99+" : String(meta.unread);
      btn.append(badge);
    }
    btn.addEventListener("click", () => setActiveChannel(name));
    roomsEl.append(btn);
  }
}

function setActiveChannel(name) {
  const normalized = normalizeChannel(name);
  if (!normalized) return;
  if (!userSession() && normalized !== "#general") {
    setStatus("Guests can participate only in public World #general");
    return;
  }
  const channel = ensureChannel(normalized);
  if (channel === activeChannel) return;
  const previousScope = roomScopeForChannel(activeChannel);
  activeChannel = channel;
  try {
    localStorage.setItem(ACTIVE_CHANNEL_KEY, channel);
  } catch (_) {}
  const meta = channelMeta.get(channel);
  if (meta) meta.unread = 0;
  if (channelTitleEl) channelTitleEl.textContent = channel;
  if (input) input.placeholder = `Message ${channel}…`;
  renderRooms();
  renderActiveChannel();
  if (previousScope !== roomScopeForChannel(activeChannel)) {
    switchChatRoom();
  }
}

// ---- people (right pane) -------------------------------------------------------

// Coalesce roster paints: a history replay on join delivers hundreds of frames
// in a burst, and each one touches the roster.
let peopleRenderTimer = null;
function schedulePeopleRender() {
  if (peopleRenderTimer) return;
  peopleRenderTimer = setTimeout(() => {
    peopleRenderTimer = null;
    renderPeople();
  }, 100);
}

function noteRoster(plain) {
  const id = plain && plain.senderId;
  if (!id) return;
  const name = String(plain.sender || "").trim().slice(0, MAX_NAME) || "peer";
  const kind = plain.accountKind === "user"
    ? (id === FORKBOT_SENDER_ID ? "bot" : "user")
    : plain.accountKind === "guest"
      ? "guest"
      : "node";
  // Use the frame's own timestamp (bounded by now): the relay replays retained
  // frames to a joining client, and a days-old replayed message must not paint
  // its author online. Never move lastSeen backwards either — a replayed old
  // frame (or old bye) can't demote a peer we've heard from more recently.
  const frameTs = Math.min(Number(plain.ts) || Date.now(), Date.now());
  const prev = roster.get(id);
  const prevSeen = prev ? prev.lastSeenMs : 0;
  let lastSeenMs;
  if (plain.type === "bye") {
    lastSeenMs = prevSeen > frameTs ? prevSeen : 0;
  } else {
    lastSeenMs = Math.max(prevSeen, frameTs);
  }
  roster.set(id, { id, name, kind, lastSeenMs });
  schedulePeopleRender();
}

function noteSelfRoster() {
  roster.set(selfId, {
    id: selfId,
    name: displayName(),
    // Public-room frames cannot prove a session identity to peers, even when
    // this browser happens to be signed in, so the local roster uses the same
    // explicitly unverified guest treatment as received public frames.
    kind:
      roomScopeForChannel() === "public-world-general"
        ? "guest"
        : "user",
    lastSeenMs: Date.now(),
  });
  schedulePeopleRender();
}

function personIsOnline(person) {
  return person.lastSeenMs > 0 && Date.now() - person.lastSeenMs <= PEER_STALE_MS;
}

// ---- registered-user directory ------------------------------------------------
// The room roster only knows senders it has decrypted frames from, so a user
// who signed up on the website but never opened chat was invisible here. Merge
// in the public account directory: every registered user gets a (offline)
// roster entry, and a signup that appears between polls is announced in the
// log so the room can welcome them right away.

const directoryKnown = new Set(); // lowercased account names already merged
let directorySeeded = false;

async function refreshUsersDirectory() {
  let users;
  try {
    const res = await fetch(USERS_DIRECTORY_ENDPOINT, {
      headers: { accept: "application/json" },
    });
    if (!res.ok) return;
    const data = await res.json().catch(() => null);
    users = data && Array.isArray(data.users) ? data.users : [];
  } catch (_) {
    return;
  }
  for (const user of users) {
    const name = String(user.name || "").trim().toLowerCase().slice(0, MAX_NAME);
    if (!name) continue;
    const fresh = !directoryKnown.has(name);
    directoryKnown.add(name);
    // Namespaced id so this offline placeholder never collides with a live
    // senderId entry for the same account; renderPeople dedupes the pair by
    // name, keeping the freshest sighting (i.e. the live one).
    const id = "account:" + name;
    const prev = roster.get(id);
    roster.set(id, {
      id,
      name,
      kind: "user",
      lastSeenMs: prev ? prev.lastSeenMs : 0,
    });
    if (fresh && directorySeeded &&
        Date.now() - Number(user.createdAt || 0) < NEW_USER_ANNOUNCE_WINDOW_MS) {
      appendSystem("🎉 " + name + " just joined ForkMesh — say hi!");
    }
  }
  directorySeeded = true;
  schedulePeopleRender();
}

// Record the current activity counters as "seen" so the chat icon in the site
// header (site-header.js reads the same key) shows no badge for what's on
// screen right now.
async function markChatActivitySeen() {
  try {
    const res = await fetch(CHAT_ACTIVITY_ENDPOINT, {
      headers: { accept: "application/json" },
    });
    if (!res.ok) return;
    const data = await res.json().catch(() => null);
    if (!data || !data.ok) return;
    localStorage.setItem(CHAT_ACTIVITY_SEEN_KEY, JSON.stringify({
      messageCount: Number(data.messageCount) || 0,
      userCount: Number(data.userCount) || 0,
      at: Date.now(),
    }));
  } catch (_) {}
}

function renderPeople() {
  if (!peopleEl) return;
  peopleEl.textContent = "";
  // Collapse multiple entries for the same person into one row: a person can
  // surface under several ids (history-replayed old senderIds, or the same
  // account from other tabs/devices) that all carry the same display name.
  // Dedupe WITHIN a section (users, nodes) by lowercased name — never across
  // kinds — keeping the freshest sighting so online state wins; fall back to
  // the id when a name is somehow missing.
  const dedupePeople = (list) => {
    const byIdentity = new Map();
    for (const person of list) {
      const key = (String(person.name || "").trim().toLowerCase() || person.id);
      const prev = byIdentity.get(key);
      if (!prev || person.lastSeenMs > prev.lastSeenMs) byIdentity.set(key, person);
    }
    return [...byIdentity.values()];
  };
  const bySection = { user: [], guest: [], node: [] };
  for (const person of roster.values()) {
    const section = person.kind === "node"
      ? "node"
      : person.kind === "guest"
        ? "guest"
        : "user";
    bySection[section].push(person);
  }
  bySection.user = dedupePeople(bySection.user);
  bySection.guest = dedupePeople(bySection.guest);
  bySection.node = dedupePeople(bySection.node);
  const sortPeople = (list) =>
    list.sort((a, b) =>
      (personIsOnline(b) - personIsOnline(a)) ||
      a.name.localeCompare(b.name));
  const onlineCount =
    [...bySection.user, ...bySection.guest, ...bySection.node]
      .filter(personIsOnline).length;
  if (peopleTitleEl) {
    peopleTitleEl.textContent = `People — ${onlineCount} online`;
  }
  const renderSection = (title, list) => {
    if (!list.length) return;
    const head = document.createElement("div");
    head.className = "chat-people-section";
    head.textContent = title;
    peopleEl.append(head);
    for (const person of sortPeople(list)) {
      // Each row links to the person's profile at /@username on this relay
      // (relative URL — a self-hosted relay links to its own pages). ForkBot
      // isn't an account, so its row stays a plain div.
      const hasProfile = person.kind === "user";
      const row = document.createElement(hasProfile ? "a" : "div");
      const online = personIsOnline(person);
      row.className = "chat-person" + (online ? "" : " is-offline");
      row.title = person.name + (online ? " · online" : " · offline");
      if (hasProfile) row.href = mentionProfilePath(person.name);
      row.append(makeAvatar(person.name, person.kind));
      const label = document.createElement("span");
      label.className = "chat-person-name";
      label.textContent = person.name;
      const dot = document.createElement("span");
      dot.className = "chat-presence-dot" + (online ? " is-online" : "");
      row.append(label, dot);
      peopleEl.append(row);
    }
  };
  renderSection("Users", bySection.user);
  renderSection("World guests", bySection.guest);
  renderSection("Nodes", bySection.node);
}

// ---- reactions ------------------------------------------------------------------

function reactionKey(plain) {
  return String(plain.reactorId || plain.senderId || "");
}

function applyReaction(plain) {
  const target = String(plain.target || "");
  const emoji = String(plain.emoji || "").slice(0, 8);
  const reactor = reactionKey(plain);
  if (!target || !emoji || !reactor) return;
  const perMessage = reactions.get(target) || new Map();
  reactions.set(target, perMessage);
  const perEmoji = perMessage.get(emoji) || new Map();
  perMessage.set(emoji, perEmoji);
  if (plain.added) {
    perEmoji.set(reactor, String(plain.reactorName || plain.sender || "peer"));
  } else {
    perEmoji.delete(reactor);
  }
  if (!perEmoji.size) perMessage.delete(emoji);
  renderReactions(target);
}

function renderReactions(messageId) {
  const rec = rows.get(messageId);
  if (!rec || !rec.reactionsEl) return;
  rec.reactionsEl.textContent = "";
  const perMessage = reactions.get(messageId);
  if (!perMessage) return;
  for (const [emoji, reactors] of perMessage) {
    if (!reactors.size) continue;
    const chip = document.createElement("button");
    chip.type = "button";
    chip.className = "chat-reaction-chip" + (reactors.has(selfId) ? " is-mine" : "");
    chip.title = [...reactors.values()].join(", ");
    const face = document.createElement("span");
    face.textContent = emoji;
    const count = document.createElement("span");
    count.className = "chat-reaction-count";
    count.textContent = String(reactors.size);
    chip.append(face, count);
    chip.addEventListener("click", () => toggleReaction(messageId, emoji));
    rec.reactionsEl.append(chip);
  }
}

function toggleReaction(messageId, emoji) {
  if (!canJoinChannel()) {
    lockChatForNonUser();
    return;
  }
  const rec = rows.get(messageId);
  const mine = Boolean(reactions.get(messageId)?.get(emoji)?.has(selfId));
  const plain = makePlain("reaction", {
    conversation: (rec && rec.channel) || activeChannel,
    target: messageId,
    emoji,
    reactorId: selfId,
    reactorName: displayName(),
    added: !mine,
  });
  seen.add(plain.id);
  applyReaction(plain);
  runWhenConnected(() => send(plain));
}

function ensureEmojiPicker() {
  if (emojiPickerEl) return emojiPickerEl;
  emojiPickerEl = document.createElement("div");
  emojiPickerEl.className = "chat-emoji-picker";
  emojiPickerEl.hidden = true;
  for (const emoji of REACTION_EMOJI) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = emoji;
    btn.addEventListener("click", () => {
      if (emojiPickerTarget) toggleReaction(emojiPickerTarget, emoji);
      hideEmojiPicker();
    });
    emojiPickerEl.append(btn);
  }
  document.body.append(emojiPickerEl);
  document.addEventListener("click", (event) => {
    if (!emojiPickerEl.hidden && !emojiPickerEl.contains(event.target)) hideEmojiPicker();
  }, true);
  return emojiPickerEl;
}

function hideEmojiPicker() {
  if (emojiPickerEl) emojiPickerEl.hidden = true;
  emojiPickerTarget = null;
}

function showEmojiPicker(anchor, messageId) {
  const picker = ensureEmojiPicker();
  emojiPickerTarget = messageId;
  picker.hidden = false;
  const rect = anchor.getBoundingClientRect();
  const margin = 8;
  const width = picker.offsetWidth || 220;
  const x = Math.max(margin, Math.min(rect.right - width, window.innerWidth - width - margin));
  const y = Math.max(margin, rect.top - picker.offsetHeight - 6);
  picker.style.left = `${x + window.scrollX}px`;
  picker.style.top = `${y + window.scrollY}px`;
}

// ---- message log (middle pane) --------------------------------------------------

function logNearBottom() {
  return logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 60;
}

function scrollLogToBottom() {
  logEl.scrollTop = logEl.scrollHeight;
}

// Render one message record into the log. `prev` is the record already above
// it; consecutive same-sender messages within GROUP_WINDOW_MS collapse under a
// single avatar + name/time header, Discord-style.
function buildRow(record, prev) {
  const grouped = Boolean(
    prev && !prev.system && prev.senderId === record.senderId &&
    Number(record.ts) - Number(prev.ts) < GROUP_WINDOW_MS);
  const row = document.createElement("div");
  const isBot = record.senderId === FORKBOT_SENDER_ID;
  row.className = "chat-msg" +
    (grouped ? " is-continuation" : " is-group-start") +
    (record.self ? " is-self" : "") +
    (isBot ? " is-bot" : "");
  row.dataset.id = record.id;
  // Clicking the avatar or the author name opens the sender's profile at
  // /@username on THIS relay (relative URL, so a self-hosted relay links to
  // its own profile pages). ForkBot isn't an account, so it stays plain.
  const avatar = makeAvatar(record.sender, isBot ? "bot" : "user");
  if (isBot) {
    row.append(avatar);
  } else {
    const avatarLink = document.createElement("a");
    avatarLink.className = "chat-avatar-link";
    avatarLink.href = mentionProfilePath(record.sender);
    avatarLink.append(avatar);
    row.append(avatarLink);
  }
  const main = document.createElement("div");
  main.className = "chat-msg-main";
  if (!grouped) {
    const head = document.createElement("div");
    head.className = "chat-msg-head";
    const author = document.createElement(isBot ? "span" : "a");
    author.className = "chat-author";
    author.textContent = record.sender;
    if (!isBot) {
      author.href = mentionProfilePath(record.sender);
      author.addEventListener("mouseenter", () => showMentionCard(author, record.sender));
      author.addEventListener("focus", () => showMentionCard(author, record.sender));
      author.addEventListener("mouseleave", hideMentionCardSoon);
      author.addEventListener("blur", hideMentionCardSoon);
    }
    const time = document.createElement("span");
    time.className = "chat-time";
    time.textContent = fmtTime(record.ts);
    time.title = new Date(Number(record.ts) || Date.now()).toLocaleString();
    head.append(author, time);
    main.append(head);
  }
  const body = document.createElement("span");
  body.className = "chat-text";
  appendMentionText(body, record.text);
  main.append(body);
  const reactionsEl = document.createElement("div");
  reactionsEl.className = "chat-reactions";
  main.append(reactionsEl);
  row.append(main);
  const reactBtn = document.createElement("button");
  reactBtn.type = "button";
  reactBtn.className = "chat-react-btn";
  reactBtn.textContent = "☺+";
  reactBtn.title = "Add reaction";
  reactBtn.addEventListener("click", (event) => {
    event.stopPropagation();
    showEmojiPicker(reactBtn, record.id);
  });
  row.append(reactBtn);
  record.el = row;
  record.body = body;
  record.reactionsEl = reactionsEl;
  return row;
}

function renderActiveChannel() {
  logEl.textContent = "";
  const list = channelMessages.get(activeChannel) || [];
  if (!list.length) {
    const empty = document.createElement("div");
    empty.className = "chat-empty";
    empty.textContent = canJoinChannel()
      ? `No messages in ${activeChannel} yet. Say hi!`
      : (
          "Log in as a user to join this channel. " +
          "Guests can participate only in public World #general."
        );
    logEl.append(empty);
    return;
  }
  let prev = null;
  for (const record of list) {
    logEl.append(buildRow(record, prev));
    renderReactions(record.id);
    prev = record;
  }
  scrollLogToBottom();
}

// Insert a message record in ts order (newest at the bottom). Appends in the
// common case; an out-of-order arrival (history replay racing live messages)
// rebuilds the visible channel so ordering and grouping stay correct.
function insertMessage(record) {
  const channel = record.channel;
  const list = channelMessages.get(channel) || [];
  channelMessages.set(channel, list);
  let index = list.length;
  while (index > 0 && Number(list[index - 1].ts) > Number(record.ts)) index -= 1;
  list.splice(index, 0, record);
  rows.set(record.id, record);
  if (channel === activeChannel) {
    const pinned = logNearBottom();
    if (index === list.length - 1) {
      clearEmpty();
      logEl.append(buildRow(record, list.length > 1 ? list[index - 1] : null));
      renderReactions(record.id);
      if (pinned || record.self) scrollLogToBottom();
    } else {
      renderActiveChannel();
    }
  } else if (!record.self) {
    bumpUnread(channel);
  }
}

function appendMessage(kind, who, text, id, senderId, ts, channel) {
  const record = {
    id: id || String(Math.random()).slice(2) + Date.now(),
    channel: ensureChannel(channel) || activeChannel,
    ts: Number(ts) || Date.now(),
    senderId: senderId || "",
    sender: who,
    text,
    self: kind === "self",
  };
  insertMessage(record);
  rememberContext(record.channel, who, text);
}

// Drop a message row from the screen (a deletion leaves no tombstone, matching
// the desktop client). Keep its id in `seen` so a late duplicate can't reappear.
function removeMessage(id) {
  const rec = rows.get(id);
  if (!rec) return;
  rows.delete(id);
  reactions.delete(id);
  const list = channelMessages.get(rec.channel) || [];
  const index = list.indexOf(rec);
  if (index >= 0) list.splice(index, 1);
  // Re-render so grouping headers stay correct around the gap.
  if (rec.channel === activeChannel) renderActiveChannel();
}

// Wipe the active room's local transcript and restore the empty placeholder.
// This is a local view-only clear: it doesn't delete anything on the relay or
// for other clients. Cleared ids stay in `seen` so a replayed history can't
// bring them back, while genuinely new incoming messages still appear.
function clearChat() {
  const list = channelMessages.get(activeChannel) || [];
  for (const record of list) {
    rows.delete(record.id);
    reactions.delete(record.id);
  }
  channelMessages.set(activeChannel, []);
  renderActiveChannel();
}

function appendSystem(text) {
  clearEmpty();
  const row = document.createElement("div");
  row.className = "chat-system";
  row.textContent = text;
  logEl.append(row);
  if (logNearBottom()) scrollLogToBottom();
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
      accountKind:
        roomScopeForChannel() === "public-world-general" ? "guest" : "user",
      ts: Date.now(),
    },
    extra || {}
  );
}

function allowedChatAccountKind(value, scope = roomScopeForChannel()) {
  return value === "user" ||
    (scope === "public-world-general" && value === "guest");
}

function normalizedPublicWorldFrame(
  plain,
  scope = roomScopeForChannel(),
) {
  if (scope !== "public-world-general" ||
      !plain || typeof plain !== "object") {
    return plain;
  }
  return {
    ...plain,
    accountKind: "guest",
    sender: worldVisitorName(plain.sender),
  };
}

function frameMatchesScope(plain, scope) {
  if (scope !== "public-world-general") return true;
  if (plain.type === "history") return true;
  if (plain.type === "hello" || plain.type === "presence" ||
      plain.type === "bye") {
    return allowedChatAccountKind(plain.accountKind, scope);
  }
  return plain.channel === "#general" ||
    plain.conversation === "#general";
}

function once(id) {
  if (!id || seen.has(id)) return false;
  seen.add(id);
  return true;
}

function renderChatEntry(entry, kind, scope = roomScopeForChannel()) {
  if (!entry || !allowedChatAccountKind(entry.accountKind, scope)) return;
  entry = normalizedPublicWorldFrame(entry, scope);
  if (scope === "public-world-general" && entry.channel !== "#general") return;
  // A private-room message from a room we weren't invited to is ignored, the
  // same honour-model as the desktop client.
  if (entry.private) return;
  // Historical authors still belong in the people pane (as offline entries —
  // noteRoster derives presence from the entry's own timestamp).
  noteRoster(entry);
  if (!once(entry.id)) return;
  const who = (entry.sender || "peer").slice(0, MAX_NAME);
  const text = entry.fileName
    ? "📎 " + entry.fileName
    : entry.text || "";
  if (text) {
    const renderedKind = entry.senderId === selfId ? "self" : kind;
    appendMessage(
      renderedKind,
      who,
      text,
      entry.id,
      entry.senderId,
      entry.ts,
      entry.channel,
    );
  }
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

function handlePlain(plain, scope = roomScopeForChannel()) {
  const type = plain.type;
  if (!frameMatchesScope(plain, scope)) return;
  if (type !== "history" && !allowedChatAccountKind(plain.accountKind, scope)) {
    return;
  }
  plain = normalizedPublicWorldFrame(plain, scope);
  // Roster + channel discovery run for EVERY decrypted frame — desktop nodes
  // announce themselves (hello/presence) without accountKind, and the people
  // pane should still show them with their online status.
  noteRoster(plain);
  if (type === "hello" || type === "channel") {
    const announced = scope === "public-world-general"
      ? ["#general"]
      : type === "channel"
        ? [plain.name]
        : plain.channels || [];
    for (const name of announced) {
      if (name) ensureChannel(name);
    }
  }
  const sender = (plain.sender || "peer").slice(0, MAX_NAME);
  if (type === "chat") {
    renderChatEntry(plain, "peer", scope);
  } else if (type === "history") {
    for (const entry of plain.entries || []) {
      if (entry && (entry.channel || entry.text || entry.fileName)) {
        renderChatEntry(entry, "peer", scope);
      }
    }
  } else if (type === "reaction") {
    if (once(plain.id)) applyReaction(plain);
  } else if (type === "edit") {
    // The author edited their own message; only honour it from that author.
    const rec = rows.get(plain.target);
    if (rec && rec.senderId === plain.senderId && rec.body) {
      rec.text = plain.text || "";
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

async function onFrame(event, key = roomKey, scope = socketScope) {
  if (typeof event.data !== "string") return;
  let envelope;
  try {
    envelope = JSON.parse(event.data);
  } catch (error) {
    return;
  }
  const plain = await decryptObject(envelope, key);
  if (!plain) return;
  handlePlain(plain, scope);
}

const DURABLE_TYPES = new Set(["chat", "edit", "delete", "reaction", "admin-delete"]);

function send(plain) {
  if (!canJoinChannel()) {
    lockChatForNonUser();
    return Promise.resolve();
  }
  const scope = socketScope || roomScopeForChannel();
  if (
    scope === "public-world-general" &&
    plain?.channel &&
    plain.channel !== "#general"
  ) {
    return Promise.resolve();
  }
  return encryptObject(plain, roomKey).then((envelope) => {
    if (DURABLE_TYPES.has(plain && plain.type)) envelope.persist = true;
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(envelope));
    }
  });
}

function makeForkbotPlain(text) {
  return makePlain("chat", {
    channel: activeChannel,
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
  appendMessage("peer", plain.sender, plain.text, plain.id, plain.senderId, plain.ts, plain.channel);
}

async function maybeAskForkbot(text) {
  if (!userSession()) return;
  if (!FORKBOT_MENTION_RE.test(text || "")) return;
  // The triggering line is the last buffer entry (appendMessage ran just
  // before this) and is sent separately as `message`; drop it, drop ForkBot's
  // own replies, and cap the rest so ForkBot sees the lead-up conversation of
  // the room the mention happened in.
  const context = (recentContext.get(activeChannel) || [])
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

function switchChatRoom() {
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  const previous = socket;
  socket = null;
  socketScope = "";
  roomKey = null;
  connecting = false;
  try {
    previous?.close(1000, "channel changed");
  } catch (_) {}
  if (canJoinChannel()) {
    unlockChatForUser();
    connect();
  } else {
    lockChatForNonUser();
  }
}

function scheduleReconnect() {
  if (reconnectTimer || !canJoinChannel()) return;
  setStatus("Disconnected · reconnecting…");
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, reconnectDelayMs);
  reconnectDelayMs = Math.min(reconnectDelayMs * 2, 30000);
}

async function connect() {
  if (socket || connecting) return;
  const scope = roomScopeForChannel();
  if (!canJoinChannel()) {
    lockChatForNonUser();
    return;
  }
  // WebCrypto (crypto.subtle) only exists in a secure context. Served over
  // plain HTTP — a self-hosted node or LAN IP opened on mobile — it is
  // undefined, so the room key can never derive. Say so plainly.
  if (!window.isSecureContext || !(window.crypto && window.crypto.subtle)) {
    setStatus("Chat needs a secure (HTTPS) connection");
    return;
  }
  connecting = true;
  setStatus("Connecting...");
  let derivedKey;
  try {
    derivedKey = await deriveRoomKey(scope);
  } catch (error) {
    connecting = false;
    // An expired/absent session token 401s the room-key fetch; point the user
    // at re-authenticating instead of blaming the browser's crypto.
    setStatus(error && error.code === "auth"
      ? "Sign in again to join chat"
      : "Encryption unavailable in this browser");
    return;
  }
  if (scope !== roomScopeForChannel()) {
    connecting = false;
    connect();
    return;
  }
  roomKey = derivedKey;
  socketScope = scope;
  const scheme = location.protocol === "https:" ? "wss:" : "ws:";
  const roomSocket = new WebSocket(
    `${scheme}//${RELAY_HOST}${roomWebSocketPathForScope(scope)}`
  );
  socket = roomSocket;

  roomSocket.addEventListener("open", () => {
    if (socket !== roomSocket || scope !== roomScopeForChannel()) {
      roomSocket.close(1000, "channel changed");
      return;
    }
    connecting = false;
    reconnectDelayMs = 2000;
    setStatus(
      scope === "public-world-general"
        ? "Connected · public World #general"
        : "Connected · authenticated shared key"
    );
    // Announce ourselves so clients add us to their roster and replay history.
    send(makePlain("hello", {
      channels: scope === "public-world-general"
        ? ["#general"]
        : [...channelMeta.keys()].filter((channel) => channel !== "#general"),
    }));
    noteSelfRoster();
    const callbacks = openCallbacks;
    openCallbacks = [];
    callbacks.forEach((cb) => cb());
  });
  roomSocket.addEventListener(
    "message",
    (event) => onFrame(event, derivedKey, scope)
  );
  roomSocket.addEventListener("close", () => {
    if (socket !== roomSocket) return;
    socket = null;
    socketScope = "";
    roomKey = null;
    connecting = false;
    scheduleReconnect();
  });
  roomSocket.addEventListener("error", () => {
    if (socket === roomSocket) roomSocket.close();
  });
}

function runWhenConnected(callback) {
  const scope = roomScopeForChannel();
  if (
    socket &&
    socket.readyState === WebSocket.OPEN &&
    socketScope === scope
  ) {
    callback();
    return;
  }
  openCallbacks.push(callback);
  if (socket && socketScope !== scope) {
    switchChatRoom();
    return;
  }
  connect();
}

function sendCurrentMessage() {
  if (!canJoinChannel()) {
    lockChatForNonUser();
    return;
  }
  const text = input.value.trim();
  if (!text) return;
  input.value = "";
  runWhenConnected(() => {
    const clipped = text.slice(0, MAX_TEXT);
    const plain = makePlain("chat", { channel: activeChannel, text: clipped });
    send(plain);
    seen.add(plain.id); // we render it here; ignore the echo if one comes back
    appendMessage("self", plain.sender, clipped, plain.id, plain.senderId, plain.ts, plain.channel);
    maybeAskForkbot(clipped);
  });
}

async function initChat() {
  await hydrateUserSession();
  if (!userSession() && activeChannel !== "#general") {
    activeChannel = "#general";
    try {
      localStorage.setItem(ACTIVE_CHANNEL_KEY, activeChannel);
    } catch (_) {}
  }
  const initialChannels = userSession() ? DEFAULT_CHANNELS : ["#general"];
  for (const channel of initialChannels) ensureChannel(channel);
  if (channelTitleEl) channelTitleEl.textContent = activeChannel;
  if (input) input.placeholder = `Message ${activeChannel}…`;
  renderRooms();
  renderPeople();
  // Fill the people pane with every registered user (and thereafter pick up
  // brand-new signups), and baseline the header badge counters for this visit.
  refreshUsersDirectory();
  markChatActivitySeen();
  setInterval(() => {
    refreshUsersDirectory();
    markChatActivitySeen();
  }, USERS_DIRECTORY_REFRESH_MS);
  // Public World #general connects for everyone. Other channels remain
  // session-gated and use the authenticated repository room.
  if (canJoinChannel()) {
    unlockChatForUser();
    connect();
    renderActiveChannel();
  } else {
    lockChatForNonUser();
  }
  sendBtn.addEventListener("click", sendCurrentMessage);
  if (clearBtn) clearBtn.addEventListener("click", clearChat);
  input.addEventListener("keydown", (event) => {
    // While the @mention popup is open it owns the keyboard: Tab (or Enter)
    // accepts the highlighted name, arrows move, Escape dismisses — only then
    // does Enter fall through to send.
    if (mentionSuggest) {
      if (event.key === "Tab" || event.key === "Enter") {
        event.preventDefault();
        acceptMentionSuggest();
        return;
      }
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        moveMentionSuggest(event.key === "ArrowDown" ? 1 : -1);
        return;
      }
      if (event.key === "Escape") {
        event.preventDefault();
        closeMentionSuggest();
        return;
      }
    }
    if (event.key === "Enter") {
      event.preventDefault();
      sendCurrentMessage();
    }
  });
  // Track the "@partial" token under the caret as it changes — typing, caret
  // moves (arrows/click), and focus loss (delayed so a suggestion mousedown
  // still lands).
  input.addEventListener("input", updateMentionSuggest);
  input.addEventListener("click", updateMentionSuggest);
  input.addEventListener("keyup", (event) => {
    if (["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) {
      updateMentionSuggest();
    }
  });
  input.addEventListener("blur", () => {
    setTimeout(closeMentionSuggest, 120);
  });
  // Presence beat, matching the desktop's cadence: keeps our roster entry
  // fresh for peers AND keeps the room DO from closing the socket as stale
  // (it reaps sockets that send nothing for 3 minutes — the old web client's
  // idle disconnects).
  setInterval(() => {
    if (socket && socket.readyState === WebSocket.OPEN && canJoinChannel()) {
      send(makePlain("presence"));
      noteSelfRoster();
    }
  }, PRESENCE_INTERVAL_MS);
  // Re-evaluate online dots as peers go stale even with no traffic.
  setInterval(renderPeople, 30000);
}

if (logEl && input && sendBtn) {
  initChat();
}
