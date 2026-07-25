// Dashboard ForkMesh room chat integration.
// Keeps the current dashboard UI, but uses the same encrypted room protocol as
// the production chat from forkmesh-today/cloudflare_worker/public/chat.js.

(() => {
  const ROOM_NAME = "general";
  const PUBLIC_WORLD_GENERAL_ROOM = "world-general";
  let roomPassphrase = null;
  const SPACE_CHANNELS = Object.freeze({
    "sky-campus": "#world-sky-campus",
    "space-station": "#world-space-station",
    "code-planet": "#world-code-planet",
    "organization-region": "#world-organization-region",
    "planet-atlas": "#world-planet-atlas",
    neighborhood: "#world-neighborhood",
    broadcast: "#world-broadcast",
    workshop: "#world-workshop",
  });
  const requestedParams = new URLSearchParams(location.search);
  const requestedSpace = requestedParams.get("space") || "";
  const requestedOrganization = String(requestedParams.get("org") || "");
  const requestedWorkshopRepo = String(requestedParams.get("repo") || "");
  const requestedWorkshopRun = String(requestedParams.get("run") || "");
  const scopedWorkshop =
    requestedSpace === "workshop" &&
    /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?\/[A-Za-z0-9._-]{1,100}$/.test(
      requestedWorkshopRepo,
    ) &&
    /^[A-Za-z0-9_-]{16,80}$/.test(requestedWorkshopRun);
  const workshopRepoParts = scopedWorkshop
    ? requestedWorkshopRepo.split("/")
    : ["mainnode", "forkmesh"];
  const ROOM_OWNER = workshopRepoParts[0];
  const ROOM_REPO = workshopRepoParts[1];
  // A workshop uses its repository's own relay-derived passphrase and Durable
  // Object room. Never multiplex a private repo/run channel into the Town
  // Square ciphertext: every registered account can obtain that public room's
  // key. The scoped key endpoint applies the repository ACL before release,
  // and the scoped WebSocket route fails closed before Durable Object access.
  const ACTIVE_SPACE = Object.hasOwn(SPACE_CHANNELS, requestedSpace)
    ? requestedSpace
    : "";
  const PUBLIC_WORLD_GENERAL =
    !ACTIVE_SPACE && !scopedWorkshop && !requestedOrganization;
  const ACTIVE_ROOM = PUBLIC_WORLD_GENERAL
    ? PUBLIC_WORLD_GENERAL_ROOM
    : ROOM_NAME;
  const ROOM_KEY_ENDPOINT =
    `/api/chat/room-key?owner=${encodeURIComponent(ROOM_OWNER)}` +
    `&repo=${encodeURIComponent(ROOM_REPO)}` +
    `&room=${encodeURIComponent(ACTIVE_ROOM)}`;
  const workshopChannelSuffix = scopedWorkshop
    ? `${requestedWorkshopRepo}/${requestedWorkshopRun}`
        .toLowerCase()
        .replace(/[^a-z0-9._/-]+/g, "-")
        .slice(0, 180)
    : "";
  const CHANNEL = scopedWorkshop
    ? `#world-workshop/${workshopChannelSuffix}`
    : ACTIVE_SPACE
      ? SPACE_CHANNELS[ACTIVE_SPACE]
      : "#general";
  const CHANNEL_LABEL = scopedWorkshop
    ? `${requestedWorkshopRepo} · run ${requestedWorkshopRun.slice(0, 12)}`
    : ACTIVE_SPACE
    ? ACTIVE_SPACE
        .split("-")
        .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
        .join(" ")
    : "General";
  const CHAT_WS_PATH =
    `/api/repo/${encodeURIComponent(ROOM_OWNER)}` +
    `/${encodeURIComponent(ROOM_REPO)}/rooms/${encodeURIComponent(ACTIVE_ROOM)}/ws`;
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
  const MAX_ATTACHMENT_BYTES = 1024 * 1024;
  const MAX_ATTACHMENT_NAME = 180;
  const MAX_ATTACHMENT_MIME = 100;
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
  // Stable per-browser chat id, shared with the full chat page (same storage
  // key): the relay keeps no roster, so a fresh random id per load made every
  // reload/tab of the same person a new "ghost" participant. Persisting it
  // collapses them into one identity.
  const selfId = (() => {
    const STORAGE_KEY = "forkmesh.chat.selfId";
    try {
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved) return saved;
    } catch (_) {}
    const fresh =
      (crypto.randomUUID && crypto.randomUUID()) ||
      `${Math.random()}`.slice(2) + Date.now();
    try { localStorage.setItem(STORAGE_KEY, fresh); } catch (_) {}
    return fresh;
  })();

  let roomKey = null;
  let socket = null;
  let connecting = false;
  let openCallbacks = [];
  let cachedUserSession = null;
  const seen = new Set();
  const rows = new Map();
  const sideEntries = [];
  const attachmentControls = [];
  const attachmentUrls = new Set();
  // Rolling buffer of recent decrypted messages, forwarded to ForkBot so it can
  // resolve references like "that bug" from the conversation. The relay can
  // decrypt the default shared-key room; this controls only the narrower
  // context explicitly sent to ForkBot.
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

  function safeAttachmentName(value) {
    const parts = String(value || "").replace(/\\/g, "/").split("/");
    const name = String(parts.pop() || "")
      .replace(/\0/g, "")
      .trim()
      .slice(0, MAX_ATTACHMENT_NAME);
    return name || "file";
  }

  function safeAttachmentMime(value) {
    const mime = String(value || "").trim().toLowerCase();
    return /^[a-z0-9][a-z0-9.+-]*\/[a-z0-9][a-z0-9.+-]*$/.test(mime) &&
      mime.length <= MAX_ATTACHMENT_MIME
      ? mime
      : "application/octet-stream";
  }

  function attachmentFromEntry(entry) {
    if (!entry || !entry.fileName || typeof entry.file !== "string") return null;
    if (!entry.file || entry.file.length > Math.ceil(MAX_ATTACHMENT_BYTES * 4 / 3) + 4) {
      return null;
    }
    try {
      const bytes = b64ToBytes(entry.file);
      if (!bytes.length || bytes.byteLength > MAX_ATTACHMENT_BYTES) return null;
      return {
        fileName: safeAttachmentName(entry.fileName),
        fileMime: safeAttachmentMime(entry.fileMime),
        file: entry.file,
        size: bytes.byteLength,
        objectUrl: "",
      };
    } catch (_) {
      return null;
    }
  }

  function attachmentObjectUrl(attachment) {
    if (attachment.objectUrl) return attachment.objectUrl;
    const bytes = b64ToBytes(attachment.file);
    const url = URL.createObjectURL(new Blob([bytes], { type: attachment.fileMime }));
    attachment.objectUrl = url;
    attachmentUrls.add(url);
    return url;
  }

  function releaseAttachment(attachment) {
    if (!attachment?.objectUrl) return;
    URL.revokeObjectURL(attachment.objectUrl);
    attachmentUrls.delete(attachment.objectUrl);
    attachment.objectUrl = "";
  }

  function formatAttachmentSize(size) {
    const bytes = Math.max(0, Number(size) || 0);
    if (bytes < 1024) return `${bytes} B`;
    return `${(bytes / 1024).toFixed(bytes < 10240 ? 1 : 0)} KiB`;
  }

  window.addEventListener("beforeunload", () => {
    for (const url of attachmentUrls) URL.revokeObjectURL(url);
    attachmentUrls.clear();
  });

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
      const err = new Error(
        PUBLIC_WORLD_GENERAL
          ? "Public World #general key unavailable."
          : "Sign in to join this authenticated chat — room key unavailable."
      );
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
      await crypto.subtle.digest(
        "SHA-256",
        enc.encode("ForkMesh room:" + ACTIVE_ROOM)
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
      const stored = session && typeof session === "object"
        ? {
            ...session,
            sessionToken: (
              location.protocol === "https:" && session.sessionToken
                ? "cookie"
                : session.sessionToken || ""
            ),
          }
        : session;
      localStorage.setItem("forkmesh.session", JSON.stringify(stored));
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

  function canJoinChat() {
    return PUBLIC_WORLD_GENERAL || Boolean(userSession());
  }

  function chatAccountKind() {
    return PUBLIC_WORLD_GENERAL ? "guest" : "user";
  }

  function worldVisitorName(value) {
    const asserted = String(value || "")
      .replace(/^World visitor\s*·\s*/i, "")
      .trim()
      .slice(0, 16) || "guest";
    return `World visitor · ${asserted}`.slice(0, MAX_NAME);
  }

  function displayName() {
    const session = userSession() || readSession();
    const value =
      session?.nodeName ||
      session?.email ||
      `World Guest ${String(selfId).replace(/[^A-Za-z0-9]/g, "").slice(0, 6)}`;
    const name = String(value).trim().slice(0, MAX_NAME) || "World Guest";
    return PUBLIC_WORLD_GENERAL ? worldVisitorName(name) : name;
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
    return `<div class="mt-3 rounded-lg border border-border bg-background px-4 py-3 text-sm text-muted-foreground">Type below to join the encrypted ${escapeHtml(CHANNEL)} collaboration channel.</div>`;
  }

  function sideEmptyHtml() {
    return `<div class="rounded-md border border-border bg-background px-3 py-2 text-xs text-muted-foreground">Type to join ${escapeHtml(CHANNEL)}.</div>`;
  }

  function fullUserOnlyHtml() {
    return `<div class="mt-3 rounded-lg border border-border bg-background px-4 py-3 text-sm text-muted-foreground">Log in as a user to join the authenticated ${escapeHtml(CHANNEL)} channel. Guests can use only public World #general.</div>`;
  }

  function sideUserOnlyHtml() {
    return '<div class="rounded-md border border-border bg-background px-3 py-2 text-xs text-muted-foreground">User login required for this channel. Guests can use only public World #general.</div>';
  }

  function setInputsEnabled(enabled) {
    [fullInput, sideInput, fullSend, sideSend].forEach((el) => {
      if (el) el.disabled = !enabled;
    });
    attachmentControls.forEach((control) => {
      control.button.disabled = !enabled;
      control.input.disabled = !enabled;
    });
  }

  function showUserOnlyState() {
    setStatus("User login required for this channel");
    setInputsEnabled(false);
    if (fullLog) fullLog.innerHTML = fullUserOnlyHtml();
    if (sideLog) sideLog.innerHTML = sideUserOnlyHtml();
  }

  function ensureEmptyState() {
    if (!canJoinChat()) {
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

  function renderAttachment(attachment, compact = false) {
    if (!attachment) return null;
    const objectUrl = attachmentObjectUrl(attachment);
    const wrapper = document.createElement("div");
    wrapper.className = compact ? "mt-1 grid gap-1.5" : "mt-2 grid max-w-md gap-2";
    if (attachment.fileMime.startsWith("image/")) {
      const image = document.createElement("img");
      image.className = "chat-attachment-image";
      image.className += compact
        ? " max-h-28 max-w-full rounded-md border border-border object-contain"
        : " max-h-72 max-w-full rounded-lg border border-border bg-secondary object-contain";
      image.src = objectUrl;
      image.alt = attachment.fileName;
      image.loading = "lazy";
      image.style.minWidth = compact ? "72px" : "96px";
      image.style.minHeight = compact ? "54px" : "72px";
      wrapper.append(image);
    }
    const card = document.createElement("div");
    card.className = "chat-attachment-card";
    card.className += compact
      ? " flex min-w-0 items-center gap-2 rounded-md border border-border bg-secondary/70 px-2 py-1.5"
      : " flex min-w-0 items-center gap-3 rounded-lg border border-border bg-secondary px-3 py-2";
    const info = document.createElement("div");
    info.className = "min-w-0 flex-1";
    const name = document.createElement("div");
    name.className = "truncate text-xs font-semibold text-foreground";
    name.textContent = attachment.fileName;
    name.title = attachment.fileName;
    const meta = document.createElement("div");
    meta.className = "truncate text-[10px] text-muted-foreground";
    meta.textContent = `${attachment.fileMime} - ${formatAttachmentSize(attachment.size)}`;
    info.append(name, meta);
    const link = document.createElement("a");
    link.className = "shrink-0 text-[11px] font-semibold text-primary hover:underline";
    link.href = objectUrl;
    link.download = attachment.fileName;
    link.textContent = "Download";
    link.setAttribute("aria-label", `Download ${attachment.fileName}`);
    card.append(info, link);
    wrapper.append(card);
    return wrapper;
  }

  function appendFullMessage(kind, who, text, id, senderId, tsMs, attachment = null) {
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
    if (textEl) {
      if (text) appendMentionText(textEl, text);
      else textEl.remove();
    }
    const content = row.querySelector(".min-w-0.flex-1");
    const renderedAttachment = renderAttachment(attachment);
    if (content && renderedAttachment) content.append(renderedAttachment);
    fullLog.append(row);
    fullLog.scrollTop = fullLog.scrollHeight;
    if (id) rows.set(id, {
      el: row,
      senderId: senderId || "",
      textEl,
      attachment,
    });
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
      if (textEl) {
        if (message.text) appendMentionText(textEl, message.text);
        else textEl.remove();
      }
      const content = row.querySelector(".min-w-0.flex-1");
      const renderedAttachment = renderAttachment(message.attachment, true);
      if (content && renderedAttachment) content.append(renderedAttachment);
      sideLog.append(row);
    }
    const bottom = document.createElement("div");
    bottom.dataset.chatBottom = "";
    sideLog.append(bottom);
    bottom.scrollIntoView({ behavior: "smooth" });
  }

  function appendSideMessage(kind, who, text, id, senderId, tsMs, attachment = null) {
    // Insert in timestamp order (append is the common case) so the newest
    // message is always the bottom row even when retained history replays
    // after live messages have already landed.
    const entry = {
      kind,
      who,
      text,
      id,
      senderId,
      tsMs: Number(tsMs) || Date.now(),
      attachment,
    };
    let index = sideEntries.length;
    while (index > 0 && Number(sideEntries[index - 1].tsMs) > entry.tsMs) index -= 1;
    sideEntries.splice(index, 0, entry);
    renderSideMessages();
  }

  function appendMessage(kind, who, text, id, senderId, tsMs, attachment = null) {
    appendFullMessage(kind, who, text, id, senderId, tsMs, attachment);
    appendSideMessage(kind, who, text, id, senderId, tsMs, attachment);
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
    if (rec?.attachment) releaseAttachment(rec.attachment);
    rows.delete(id);
    const idx = sideEntries.findIndex((entry) => entry.id === id);
    if (idx >= 0 && sideEntries[idx].attachment !== rec?.attachment) {
      releaseAttachment(sideEntries[idx].attachment);
    }
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
        accountKind: chatAccountKind(),
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

  // Inside the World embed, mirror each live chat line to the parent page so
  // it can float the message above the speaker's avatar and fade it out
  // (world.js handleWorldChatMessage). Same-origin only; history replays are
  // excluded so reconnects do not resurrect old bubbles.
  const WORLD_EMBED_BUBBLES =
    requestedParams.get("worldEmbed") === "1" && window.parent !== window;

  function emitWorldChatBubble(sender, senderId, text) {
    if (!WORLD_EMBED_BUBBLES) return;
    const line = String(text || "").trim().slice(0, 200);
    if (!line) return;
    try {
      window.parent.postMessage(
        {
          type: "forkmesh:world-chat",
          sender: String(sender || "").slice(0, MAX_NAME),
          self: senderId === selfId,
          text: line,
        },
        location.origin
      );
    } catch (_) {}
  }

  function allowedChatAccountKind(value) {
    return value === "user" || (PUBLIC_WORLD_GENERAL && value === "guest");
  }

  function normalizedPublicWorldFrame(entry) {
    if (!PUBLIC_WORLD_GENERAL || !entry || typeof entry !== "object") {
      return entry;
    }
    return {
      ...entry,
      accountKind: "guest",
      sender: worldVisitorName(entry.sender),
    };
  }

  function renderChatEntry(entry, kind, live = false) {
    if (!entry || !allowedChatAccountKind(entry.accountKind)) return;
    entry = normalizedPublicWorldFrame(entry);
    if (!once(entry.id)) return;
    const who = String(entry.sender || "peer").slice(0, MAX_NAME);
    const text = entry.text || "";
    const attachment = attachmentFromEntry(entry);
    if (!text && !attachment) return;
    kind = entry.senderId === selfId ? "self" : kind;
    appendMessage(kind, who, text, entry.id, entry.senderId,
                  Number(entry.ts) || Date.now(), attachment);
    if (live) emitWorldChatBubble(who, entry.senderId, text);
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
    if (type !== "history" && !allowedChatAccountKind(plain.accountKind)) return;
    plain = normalizedPublicWorldFrame(plain);
    const sender = String(plain.sender || "peer").slice(0, MAX_NAME);
    if (type === "chat") {
      if (plain.channel === CHANNEL) renderChatEntry(plain, "peer", true);
    } else if (type === "history") {
      for (const entry of plain.entries || []) {
        if (
          entry &&
          entry.channel === CHANNEL &&
          (entry.channel || entry.text || entry.fileName)
        ) {
          renderChatEntry(entry, "peer");
        }
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
    if (!plain) return;
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
    if (!canJoinChat()) {
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
    if (reconnectTimer || !canJoinChat()) return;
    setStatus("Disconnected · reconnecting…");
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, reconnectDelayMs);
    reconnectDelayMs = Math.min(reconnectDelayMs * 2, 30000);
  }

  async function connect() {
    if (socket || connecting) return;
    if (!canJoinChat()) {
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
      setStatus(
        PUBLIC_WORLD_GENERAL
          ? "Connected · public World #general"
          : "Connected · authenticated shared key"
      );
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
    if (socket && socket.readyState === WebSocket.OPEN && canJoinChat()) {
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
    // The asking client appends directly (not via renderChatEntry), so mirror
    // the reply to the World embed here too — it floats over the ForkBot
    // avatar walking the Town Square.
    emitWorldChatBubble(plain.sender, plain.senderId, plain.text);
  }

  async function maybeAskForkbot(text) {
    // Anyone who can join the room can talk to ForkBot: signed-in users on
    // the dashboard, and guests inside the public World room (the endpoint
    // itself is sessionless).
    if (!canJoinChat()) return;
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
        body: JSON.stringify({
          message: text,
          sender: displayName(),
          room: ACTIVE_SPACE || ROOM_NAME,
          context,
        }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data || !data.botMessage) return;
      runWhenConnected(() => broadcastForkbotMessage(data.botMessage));
    } catch (_) {
      appendSystem("forkbot is unavailable");
    }
  }

  function showAttachmentFeedback(control, message) {
    if (!control?.feedback) return;
    control.feedback.textContent = String(message || "");
    if (message) {
      setTimeout(() => {
        if (control.feedback.textContent === message) {
          control.feedback.textContent = "";
        }
      }, 5000);
    }
  }

  async function sendAttachment(file, control = null) {
    if (!file || !canJoinChat()) {
      if (!canJoinChat()) showUserOnlyState();
      return;
    }
    if (file.size > MAX_ATTACHMENT_BYTES) {
      showAttachmentFeedback(control, "Attachments must be 1 MiB or smaller.");
      return;
    }
    if (!file.size) {
      showAttachmentFeedback(control, "That file is empty.");
      return;
    }
    let buffer;
    try {
      buffer = await file.arrayBuffer();
    } catch (_) {
      showAttachmentFeedback(control, "Could not read that attachment.");
      return;
    }
    const fileName = safeAttachmentName(file.name);
    const fileMime = safeAttachmentMime(file.type);
    const encodedFile = bytesToB64(buffer);
    runWhenConnected(() => {
      const plain = makePlain("chat", {
        channel: CHANNEL,
        fileName,
        fileMime,
        file: encodedFile,
      });
      const attachment = attachmentFromEntry(plain);
      if (!attachment) {
        showAttachmentFeedback(control, "Could not prepare that attachment.");
        return;
      }
      send(plain);
      seen.add(plain.id);
      appendMessage(
        "self",
        plain.sender,
        "",
        plain.id,
        plain.senderId,
        plain.ts,
        attachment,
      );
      showAttachmentFeedback(control, `Shared ${fileName}`);
    });
  }

  function mountAttachmentControl(inputEl) {
    if (!inputEl?.parentElement) return null;
    const bar = inputEl.parentElement;
    const fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.hidden = true;
    fileInput.id = `${inputEl.id}AttachmentInput`;
    fileInput.setAttribute("aria-label", "Choose image or document");
    const button = document.createElement("button");
    button.type = "button";
    button.className = "shrink-0 inline-flex h-7 w-7 items-center justify-center rounded-md border border-border bg-background text-muted-foreground hover:text-foreground disabled:cursor-not-allowed disabled:opacity-50";
    button.setAttribute("aria-label", "Attach image or document");
    button.title = "Attach image or document (up to 1 MiB)";
    button.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21.44 11.05 12.25 20.24a6 6 0 0 1-8.49-8.49l9.19-9.19a4 4 0 0 1 5.66 5.66l-9.2 9.19a2 2 0 0 1-2.83-2.83l8.49-8.48"></path></svg>';
    const feedback = document.createElement("span");
    feedback.className = "sr-only";
    feedback.setAttribute("role", "status");
    feedback.setAttribute("aria-live", "polite");
    const control = { button, input: fileInput, feedback };
    const sendButton = inputEl === fullInput ? fullSend : sideSend;
    bar.insertBefore(fileInput, sendButton || null);
    bar.insertBefore(button, sendButton || null);
    bar.append(feedback);
    button.addEventListener("click", () => fileInput.click());
    fileInput.addEventListener("change", () => {
      const file = fileInput.files && fileInput.files[0];
      fileInput.value = "";
      if (file) sendAttachment(file, control);
    });
    control.button.disabled = !canJoinChat();
    control.input.disabled = !canJoinChat();
    attachmentControls.push(control);
    return control;
  }

  function sendFrom(inputEl) {
    if (!canJoinChat()) {
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
      emitWorldChatBubble(plain.sender, plain.senderId, plain.text);
      maybeAskForkbot(clipped);
    });
  }

  function wireInput(inputEl, sendEl) {
    if (!inputEl || !sendEl) return;
    const attachmentControl = mountAttachmentControl(inputEl);
    sendEl.addEventListener("click", () => sendFrom(inputEl));
    inputEl.addEventListener("paste", (event) => {
      const items = Array.from(event.clipboardData?.items || []);
      const item = items.find((candidate) =>
        candidate.kind === "file" && String(candidate.type || "").startsWith("image/"));
      const file = item ? item.getAsFile() : null;
      if (!file) return;
      event.preventDefault();
      sendAttachment(file, attachmentControl);
    });
    inputEl.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        sendFrom(inputEl);
      }
    });
  }

  function mountPrivateChannelsLink() {
    if (!PUBLIC_WORLD_GENERAL || !fullLog) return;
    const notice = fullLog.previousElementSibling;
    const copy = notice?.querySelector("p");
    if (!copy || copy.querySelector("[data-private-channels-link]")) return;
    const link = document.createElement("a");
    link.href = "/chat";
    link.textContent = "Open private channels";
    link.dataset.privateChannelsLink = "";
    link.className = "ml-1 font-semibold text-primary underline underline-offset-2 hover:text-foreground";
    copy.append(document.createTextNode(" "), link);
  }

  async function initChat() {
    if (ACTIVE_SPACE) {
      document.title = `${CHANNEL_LABEL} collaboration · ForkMesh`;
      document
        .querySelectorAll("[data-dashboard-chat-status]")
        .forEach((element) => {
          element.title = `Dedicated ${CHANNEL} channel inside the encrypted ForkMesh room`;
        });
    }
    await hydrateUserSession();
    ensureEmptyState();
    mountPrivateChannelsLink();
    wireInput(fullInput, fullSend);
    wireInput(sideInput, sideSend);
    // Connect right away so the room's message history (replayed by the relay
    // on WebSocket open) is visible without the visitor first focusing an input.
    if (canJoinChat()) {
      setStatus("Not connected");
      connect();
    }
  }

  initChat();
})();
