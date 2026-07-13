// Dashboard ForkMesh room chat integration.
// Keeps the current dashboard UI, but uses the same encrypted room protocol as
// the production chat from forkmesh-today/cloudflare_worker/public/chat.js.

(() => {
  const ROOM_NAME = "general";
  // The room key is fetched from the relay (derived server-side from DATA_KEY)
  // rather than baked in as a public constant; see chat.js for the rationale.
  const ROOM_KEY_ENDPOINT = "/api/chat/room-key";
  let roomPassphrase = null;
  const CHANNEL = "#general";
  const CHAT_WS_PATH = "/api/repo/mainnode/forkmesh/rooms/general/ws";
  const FORKBOT_ENDPOINT = "/api/forkbot/chat";
  const FORKBOT_SENDER_ID = "forkbot";
  const FORKBOT_MENTION_RE = /(?:^|[^A-Za-z0-9_-])@?forkbot\b/i;
  // Mainnode base host for the room WebSocket. Defaults to the origin that
  // served the dashboard, so a self-hosted mainnode talks to itself. Override
  // with window.FORKMESH_RELAY_HOST to target a different relay (see
  // public docs, "Self-hosting and development" (/docs#self-hosting).
  const RELAY_HOST = window.FORKMESH_RELAY_HOST || location.host;
  const MAX_TEXT = 16000;
  const MAX_NAME = 32;
  const MAX_SIDE_MESSAGES = 3;
  const CHAT_MENTION_RE = /(^|[^A-Za-z0-9_-])@([a-z](?:[a-z0-9-]{0,61}[a-z0-9])?)\b/gi;

  const fullLog = document.querySelector("#fullChatMessages");
  const sideLog = document.querySelector("#sideChatMessages");
  const fullInput = document.querySelector("#fullChatInput");
  const sideInput = document.querySelector("#sideChatInput");
  const fullSend = document.querySelector("#fullChatSend");
  const sideSend = document.querySelector("#sideChatSend");

  if (!fullLog && !sideLog) return;

  const enc = new TextEncoder();
  const dec = new TextDecoder();
  const selfId =
    (crypto.randomUUID && crypto.randomUUID()) ||
    `${Math.random()}`.slice(2) + Date.now();

  let roomKey = null;
  let socket = null;
  let connecting = false;
  let openCallbacks = [];
  let cachedUserSession = null;
  const seen = new Set();
  const rows = new Map();
  const sideEntries = [];
  // Rolling buffer of recent decrypted messages, forwarded to ForkBot so it can
  // resolve references like "that bug" from the conversation. The room is E2E
  // encrypted, so the relay only sees what we choose to send here.
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

  function b64urlToBytes(value) {
    let s = (value || "").replace(/-/g, "+").replace(/_/g, "/");
    while (s.length % 4) s += "=";
    return b64ToBytes(s);
  }

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
    } catch (_) {
      return false;
    }
  }

  async function fetchRoomPassphrase() {
    if (roomPassphrase) return roomPassphrase;
    const session = readSession();
    const token = session && session.sessionToken;
    const headers = { accept: "application/json" };
    if (token) headers.authorization = "Bearer " + token;
    const res = await fetch(ROOM_KEY_ENDPOINT, { headers, cache: "no-store" });
    if (!res.ok) {
      const err = new Error("Sign in to join chat — room key unavailable.");
      err.code = res.status === 401 || res.status === 403 ? "auth" : "server";
      throw err;
    }
    const data = await res.json().catch(() => ({}));
    if (!data || !data.passphrase) throw new Error("Room key unavailable.");
    roomPassphrase = String(data.passphrase);
    return roomPassphrase;
  }

  async function deriveRoomKey() {
    const passphrase = await fetchRoomPassphrase();
    const saltDigest = new Uint8Array(
      await crypto.subtle.digest("SHA-256", enc.encode("ForkMesh room:" + ROOM_NAME))
    );
    const salt = saltDigest.slice(0, 16);
    const baseKey = await crypto.subtle.importKey(
      "raw",
      enc.encode(passphrase),
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
    } catch (_) {
      return null;
    }
  }

  function readSession() {
    try {
      return JSON.parse(localStorage.getItem("forkmesh.session") || "null");
    } catch (_) {
      return null;
    }
  }

  function writeSession(session) {
    try {
      localStorage.setItem("forkmesh.session", JSON.stringify(session));
    } catch (_) {}
  }

  function isUserLikeSession(session) {
    if (!session || !session.nodeName) return false;
    if (session.kind === "user") return true;
    if (session.kind === "node") return false;
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
    } catch (_) {
      return cachedUserSession;
    }
  }

  function userSession() {
    if (isUserLikeSession(cachedUserSession)) return cachedUserSession;
    const session = readSession();
    return isUserLikeSession(session) ? session : null;
  }

  function displayName() {
    const session = userSession() || readSession();
    const value = session?.nodeName || session?.email || "web-guest";
    return String(value).trim().slice(0, MAX_NAME) || "web-guest";
  }

  function escapeHtml(value) {
    return String(value || "").replace(/[&<>"']/g, (char) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#039;",
    })[char]);
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

  function setStatus(text) {
    document.querySelectorAll("[data-dashboard-chat-status]").forEach((el) => {
      el.textContent = text;
    });
  }

  function fullEmptyHtml() {
    return '<div class="mt-3 rounded-lg border border-border bg-background px-4 py-3 text-sm text-muted-foreground">Type below to join the encrypted #general room.</div>';
  }

  function sideEmptyHtml() {
    return '<div class="rounded-md border border-border bg-background px-3 py-2 text-xs text-muted-foreground">Type to join #general.</div>';
  }

  function fullUserOnlyHtml() {
    return '<div class="mt-3 rounded-lg border border-border bg-background px-4 py-3 text-sm text-muted-foreground">Log in as a user to join the encrypted #general room.</div>';
  }

  function sideUserOnlyHtml() {
    return '<div class="rounded-md border border-border bg-background px-3 py-2 text-xs text-muted-foreground">User login required for chat.</div>';
  }

  function setInputsEnabled(enabled) {
    [fullInput, sideInput, fullSend, sideSend].forEach((el) => {
      if (el) el.disabled = !enabled;
    });
  }

  function showUserOnlyState() {
    setStatus("User login required");
    setInputsEnabled(false);
    if (fullLog) fullLog.innerHTML = fullUserOnlyHtml();
    if (sideLog) sideLog.innerHTML = sideUserOnlyHtml();
  }

  function ensureEmptyState() {
    if (!userSession()) {
      showUserOnlyState();
      return;
    }
    setInputsEnabled(true);
    if (fullLog && !fullLog.children.length) fullLog.innerHTML = fullEmptyHtml();
    if (sideLog && !sideLog.children.length) sideLog.innerHTML = sideEmptyHtml();
  }

  function clearEmptyState() {
    [fullLog, sideLog].forEach((log) => {
      if (!log) return;
      const text = log.textContent || "";
      if (text.includes("Type below to join") || text.includes("Type to join")) {
        log.textContent = "";
      }
    });
  }

  function avatarLetter(handle) {
    return escapeHtml((handle || "?").slice(0, 1).toUpperCase());
  }

  function fmtChatTime(tsMs) {
    const value = Number(tsMs);
    if (!value) return "";
    return new Date(value).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  }

  function appendFullMessage(kind, who, text, id, senderId, tsMs) {
    if (!fullLog) return;
    clearEmptyState();
    const self = kind === "self";
    const row = document.createElement("div");
    row.className = "flex items-start gap-3 group rounded-lg px-2 py-1 hover:bg-secondary/40 transition-colors mt-3";
    row.innerHTML = `
      <span class="avatar flex h-7 w-7 shrink-0 items-center justify-center rounded-full border font-mono text-[11px] font-semibold ${self ? "border-primary/40 bg-primary/10 text-primary" : "border-border bg-secondary text-foreground"}">${avatarLetter(who)}</span>
      <div class="min-w-0 flex-1">
        <div class="mb-0.5 flex items-baseline gap-2">
          <span class="text-xs font-semibold ${self ? "text-primary" : "text-foreground"}">${escapeHtml(who)}</span>
          <span class="text-[10px] text-muted-foreground/50 font-mono">${escapeHtml(fmtChatTime(tsMs))}</span>
        </div>
        <p class="text-sm text-muted-foreground leading-relaxed break-words"></p>
      </div>`;
    const textEl = row.querySelector("p");
    if (textEl) appendMentionText(textEl, text);
    fullLog.append(row);
    fullLog.scrollTop = fullLog.scrollHeight;
    if (id) rows.set(id, { el: row, senderId: senderId || "", textEl });
  }

  // The rail's mini chat mirrors the full view at a smaller scale: avatar +
  // name + time header with the message below, ordered by each message's own
  // timestamp so replayed history and live traffic interleave correctly with
  // the newest at the bottom.
  function renderSideMessages() {
    if (!sideLog) return;
    if (!sideEntries.length) {
      sideLog.innerHTML = sideEmptyHtml();
      return;
    }
    sideLog.textContent = "";
    for (const message of sideEntries.slice(-MAX_SIDE_MESSAGES)) {
      const self = message.kind === "self";
      const row = document.createElement("div");
      row.className = "flex items-start gap-2 px-1 py-1 rounded-md hover:bg-secondary/40 transition-colors mt-2";
      row.innerHTML = `
        <span class="avatar flex h-6 w-6 shrink-0 items-center justify-center rounded-full border font-mono text-[10px] font-semibold ${self ? "border-primary/40 bg-primary/10 text-primary" : "border-border bg-secondary text-foreground"}">${avatarLetter(message.who)}</span>
        <div class="min-w-0 flex-1">
          <div class="flex items-baseline gap-1.5">
            <span class="text-[11px] font-semibold ${self ? "text-primary" : "text-foreground"}">${escapeHtml(message.who)}</span>
            <span class="text-[9px] text-muted-foreground/50 font-mono">${escapeHtml(fmtChatTime(message.tsMs))}</span>
          </div>
          <p class="text-xs text-muted-foreground leading-relaxed break-words"></p>
        </div>`;
      const textEl = row.querySelector("p");
      if (textEl) appendMentionText(textEl, message.text);
      sideLog.append(row);
    }
    const bottom = document.createElement("div");
    bottom.dataset.chatBottom = "";
    sideLog.append(bottom);
    bottom.scrollIntoView({ behavior: "smooth" });
  }

  function appendSideMessage(kind, who, text, id, senderId, tsMs) {
    // Insert in timestamp order (append is the common case) so the newest
    // message is always the bottom row even when retained history replays
    // after live messages have already landed.
    const entry = { kind, who, text, id, senderId, tsMs: Number(tsMs) || Date.now() };
    let index = sideEntries.length;
    while (index > 0 && Number(sideEntries[index - 1].tsMs) > entry.tsMs) index -= 1;
    sideEntries.splice(index, 0, entry);
    renderSideMessages();
  }

  function appendMessage(kind, who, text, id, senderId, tsMs) {
    appendFullMessage(kind, who, text, id, senderId, tsMs);
    appendSideMessage(kind, who, text, id, senderId, tsMs);
    rememberContext(who, text);
  }

  function appendSystem(text) {
    if (fullLog) {
      clearEmptyState();
      const row = document.createElement("div");
      row.className = "my-2 rounded-md border border-border bg-background px-3 py-2 text-xs text-muted-foreground";
      row.textContent = text;
      fullLog.append(row);
      fullLog.scrollTop = fullLog.scrollHeight;
    }
  }

  function removeMessage(id) {
    const rec = rows.get(id);
    if (rec?.el?.parentNode) rec.el.parentNode.removeChild(rec.el);
    rows.delete(id);
    const idx = sideEntries.findIndex((entry) => entry.id === id);
    if (idx >= 0) sideEntries.splice(idx, 1);
    renderSideMessages();
  }

  function makePlain(type, extra) {
    return Object.assign(
      {
        type,
        id:
          (crypto.randomUUID && crypto.randomUUID()) ||
          `${Math.random()}`.slice(2) + Date.now(),
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
    const who = String(entry.sender || "peer").slice(0, MAX_NAME);
    const text = entry.fileName ? "📎 " + entry.fileName : entry.text || "";
    if (!text) return;
    appendMessage(kind, who, text, entry.id, entry.senderId,
                  Number(entry.ts) || Date.now());
  }

  async function verifyAdminDelete(plain) {
    const target = plain.target;
    const adminId = plain.senderId;
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
      const name = String(plain.sender || "").trim().toLowerCase();
      if (!name) return false;
      const res = await fetch("/api/accounts/" + encodeURIComponent(name));
      const rec = await res.json();
      return rec && rec.isAdmin === true && rec.pubkey === adminId;
    } catch (_) {
      return false;
    }
  }

  function handlePlain(plain) {
    const type = plain.type;
    // Mirror-mesh signals ride the same encrypted room as chat: a source node
    // broadcasts "mirror-update" the instant its repo advances from the source
    // of truth, and every mirror node replies "mirror-synced" once it has
    // pulled that commit. Re-broadcast both as a window event so the dashboard's
    // open repo can re-fetch host health live instead of waiting for a manual
    // reload — this is what makes the Mirrors tab show nodes converge instantly.
    // These frames are stamped accountKind "node", so handle them before the
    // user-only guard below (which is meant for chat surfaces).
    if (type === "mirror-update" || type === "mirror-synced") {
      try {
        window.dispatchEvent(new CustomEvent("forkmesh:mirror-signal", {
          detail: {
            kind: type,
            repo: String(plain.repo || "").slice(0, 200),
            commit: String(plain.commit || "").slice(0, 64),
            sender: String(plain.sender || "").slice(0, MAX_NAME),
          },
        }));
      } catch (_) {}
      return;
    }
    if (type !== "history" && plain.accountKind !== "user") return;
    const sender = String(plain.sender || "peer").slice(0, MAX_NAME);
    if (type === "chat") {
      renderChatEntry(plain, "peer");
    } else if (type === "history") {
      for (const entry of plain.entries || []) {
        if (entry && (entry.channel || entry.text || entry.fileName)) renderChatEntry(entry, "peer");
      }
    } else if (type === "edit") {
      const rec = rows.get(plain.target);
      if (rec && rec.senderId === plain.senderId && rec.textEl) {
        renderMessageText(rec.textEl, plain.text || "");
        const sideEntry = sideEntries.find((entry) => entry.id === plain.target);
        if (sideEntry) {
          sideEntry.text = plain.text || "";
          renderSideMessages();
        }
      }
    } else if (type === "delete") {
      const rec = rows.get(plain.target);
      if (rec && rec.senderId === plain.senderId) removeMessage(plain.target);
    } else if (type === "admin-delete") {
      verifyAdminDelete(plain).then((ok) => {
        if (!ok) return;
        seen.add(plain.target);
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
    } catch (_) {
      return;
    }
    const plain = await decryptObject(envelope);
    if (!plain || plain.senderId === selfId) return;
    handlePlain(plain);
  }

  // Durable message types the relay should retain (still encrypted) and replay
  // to clients that join later — the same set the desktop node tags via
  // kDurableTypes (ServerNode::sendEncrypted). Without this flag the relay's
  // _maybe_retain drops the frame, so a message typed on the website is relayed
  // live but never becomes part of the shared history nodes and other web
  // visitors see on connect — leaving the website out of the shared chat.
  const DURABLE_TYPES = new Set(["chat", "edit", "delete", "reaction", "admin-delete"]);

  function send(plain) {
    if (!userSession()) {
      showUserOnlyState();
      return Promise.resolve();
    }
    return encryptObject(plain).then((envelope) => {
      if (DURABLE_TYPES.has(plain && plain.type)) envelope.persist = true;
      if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(envelope));
    });
  }

  // The relay's room DO reaps sockets that send nothing for 3 minutes, and an
  // idle dashboard tab used to send nothing — its chat silently went stale
  // (no new messages) until the user typed. Reconnect with backoff and beat
  // presence at the desktop's 60s cadence to stay live (mirrors chat.js).
  let reconnectDelayMs = 2000;
  let reconnectTimer = null;

  function scheduleReconnect() {
    if (reconnectTimer || !userSession()) return;
    setStatus("Disconnected · reconnecting…");
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, reconnectDelayMs);
    reconnectDelayMs = Math.min(reconnectDelayMs * 2, 30000);
  }

  async function connect() {
    if (socket || connecting) return;
    if (!userSession()) {
      showUserOnlyState();
      return;
    }
    // WebCrypto (crypto.subtle) only exists in a secure context. On plain HTTP
    // — a self-hosted node or LAN IP opened on mobile — it is undefined, so the
    // room key can never derive. Say so plainly instead of the old blanket
    // "Encryption unavailable", which read like a transient glitch.
    if (!window.isSecureContext || !(window.crypto && window.crypto.subtle)) {
      setStatus("Chat needs a secure (HTTPS) connection");
      return;
    }
    connecting = true;
    setInputsEnabled(true);
    setStatus("Connecting...");
    try {
      if (!roomKey) roomKey = await deriveRoomKey();
    } catch (err) {
      connecting = false;
      // An expired/absent session token 401s the room-key fetch; tell the user
      // to sign in again rather than blaming encryption.
      setStatus(err && err.code === "auth" ? "Sign in again to join chat" : "Encryption unavailable");
      return;
    }
    const scheme = location.protocol === "https:" ? "wss:" : "ws:";
    socket = new WebSocket(`${scheme}//${RELAY_HOST}${CHAT_WS_PATH}`);
    socket.addEventListener("open", () => {
      connecting = false;
      reconnectDelayMs = 2000;
      setStatus("Connected · end-to-end encrypted");
      send(makePlain("hello", { channels: [CHANNEL] }));
      const callbacks = openCallbacks;
      openCallbacks = [];
      callbacks.forEach((cb) => cb());
    });
    socket.addEventListener("message", onFrame);
    socket.addEventListener("close", () => {
      socket = null;
      connecting = false;
      scheduleReconnect();
    });
    socket.addEventListener("error", () => {
      if (socket) socket.close();
    });
  }

  setInterval(() => {
    if (socket && socket.readyState === WebSocket.OPEN && userSession()) {
      send(makePlain("presence"));
    }
  }, 60000);

  function runWhenConnected(callback) {
    if (socket && socket.readyState === WebSocket.OPEN) {
      callback();
      return;
    }
    openCallbacks.push(callback);
    connect();
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
    appendMessage("peer", plain.sender, plain.text, plain.id, plain.senderId, plain.ts);
  }

  async function maybeAskForkbot(text) {
    if (!FORKBOT_MENTION_RE.test(text || "")) return;
    // Drop the triggering line (sent separately as `message`) and ForkBot's own
    // replies, and cap the rest so ForkBot sees the lead-up conversation.
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
    } catch (_) {
      appendSystem("forkbot is unavailable");
    }
  }

  function sendFrom(inputEl) {
    if (!userSession()) {
      showUserOnlyState();
      return;
    }
    const text = (inputEl?.value || "").trim();
    if (!text) return;
    if (inputEl) inputEl.value = "";
    runWhenConnected(() => {
      const clipped = text.slice(0, MAX_TEXT);
      const plain = makePlain("chat", { channel: CHANNEL, text: clipped });
      send(plain);
      seen.add(plain.id);
      appendMessage("self", plain.sender, plain.text, plain.id, plain.senderId, plain.ts);
      maybeAskForkbot(clipped);
    });
  }

  function wireInput(inputEl, sendEl) {
    if (!inputEl || !sendEl) return;
    sendEl.addEventListener("click", () => sendFrom(inputEl));
    inputEl.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        sendFrom(inputEl);
      }
    });
  }

  async function initChat() {
    await hydrateUserSession();
    ensureEmptyState();
    wireInput(fullInput, fullSend);
    wireInput(sideInput, sideSend);
    // Connect right away so the room's message history (replayed by the relay
    // on WebSocket open) is visible without the visitor first focusing an input.
    if (userSession()) {
      setStatus("Not connected");
      connect();
    }
  }

  initChat();
})();
