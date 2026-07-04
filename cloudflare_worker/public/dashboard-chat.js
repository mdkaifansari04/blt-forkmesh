// Dashboard ForkMesh room chat integration.
// Keeps the current dashboard UI, but uses the same encrypted room protocol as
// the production chat from forkmesh-today/cloudflare_worker/public/chat.js.

(() => {
  const ROOM_NAME = "general";
  const ROOM_PASSPHRASE = "forkmesh-shared-room-key-v1";
  const CHANNEL = "#general";
  const CHAT_WS_PATH = "/api/repo/mainnode/forkmesh/rooms/general/ws";
  // Mainnode base host for the room WebSocket. Defaults to the origin that
  // served the dashboard, so a self-hosted mainnode talks to itself. Override
  // with window.FORKMESH_RELAY_HOST to target a different relay (see
  // docs/protocol.md, "Self-hosting a mainnode").
  const RELAY_HOST = window.FORKMESH_RELAY_HOST || location.host;
  const MAX_TEXT = 16000;
  const MAX_NAME = 32;
  const MAX_SIDE_MESSAGES = 3;

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
  const seen = new Set();
  const rows = new Map();
  const sideEntries = [];

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

  function displayName() {
    const session = readSession();
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

  function ensureEmptyState() {
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

  function appendFullMessage(kind, who, text, id, senderId, ts) {
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
          <span class="text-[10px] text-muted-foreground/50 font-mono">${escapeHtml(ts || "")}</span>
        </div>
        <p class="text-sm text-muted-foreground leading-relaxed break-words">${escapeHtml(text)}</p>
      </div>`;
    fullLog.append(row);
    fullLog.scrollTop = fullLog.scrollHeight;
    if (id) rows.set(id, { el: row, senderId: senderId || "", textEl: row.querySelector("p") });
  }

  function renderSideMessages() {
    if (!sideLog) return;
    if (!sideEntries.length) {
      sideLog.innerHTML = sideEmptyHtml();
      return;
    }
    sideLog.innerHTML = sideEntries.slice(-MAX_SIDE_MESSAGES).map((message) => `
      <div class="flex items-start gap-2 px-1 py-1 rounded-md hover:bg-secondary/40 transition-colors mt-2">
        <span class="avatar flex h-5 w-5 shrink-0 items-center justify-center rounded-full border border-border bg-secondary font-mono text-[9px] font-semibold text-foreground">${avatarLetter(message.who)}</span>
        <div class="min-w-0 flex-1">
          <span class="text-[10px] font-semibold ${message.kind === "self" ? "text-primary" : "text-foreground"} mr-1.5">${escapeHtml(message.who)}</span>
          <p class="text-xs text-muted-foreground leading-relaxed break-words">${escapeHtml(message.text)}</p>
        </div>
      </div>
    `).join("") + '<div data-chat-bottom></div>';
    sideLog.querySelector("[data-chat-bottom]")?.scrollIntoView({ behavior: "smooth" });
  }

  function appendSideMessage(kind, who, text, id, senderId) {
    sideEntries.push({ kind, who, text, id, senderId });
    renderSideMessages();
  }

  function appendMessage(kind, who, text, id, senderId, ts) {
    appendFullMessage(kind, who, text, id, senderId, ts);
    appendSideMessage(kind, who, text, id, senderId);
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
    const who = String(entry.sender || "peer").slice(0, MAX_NAME);
    const text = entry.fileName ? "📎 " + entry.fileName : entry.text || "";
    if (!text) return;
    const ts = entry.ts ? new Date(entry.ts).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) : "";
    appendMessage(kind, who, text, entry.id, entry.senderId, ts);
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
    const sender = String(plain.sender || "peer").slice(0, MAX_NAME);
    if (type === "chat") {
      renderChatEntry(plain, "peer");
    } else if (type === "history") {
      for (const entry of plain.entries || []) {
        if (entry && (entry.channel || entry.text || entry.fileName)) renderChatEntry(entry, "peer");
      }
    } else if (type === "edit") {
      const rec = rows.get(plain.target);
      if (rec && rec.senderId === plain.senderId && rec.textEl) rec.textEl.textContent = plain.text || "";
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
    return encryptObject(plain).then((envelope) => {
      if (DURABLE_TYPES.has(plain && plain.type)) envelope.persist = true;
      if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(envelope));
    });
  }

  async function connect() {
    if (socket || connecting) return;
    connecting = true;
    setStatus("Connecting...");
    try {
      if (!roomKey) roomKey = await deriveRoomKey();
    } catch (_) {
      connecting = false;
      setStatus("Encryption unavailable");
      return;
    }
    const scheme = location.protocol === "https:" ? "wss:" : "ws:";
    socket = new WebSocket(`${scheme}//${RELAY_HOST}${CHAT_WS_PATH}`);
    socket.addEventListener("open", () => {
      connecting = false;
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

  function sendFrom(inputEl) {
    const text = (inputEl?.value || "").trim();
    if (!text) return;
    if (inputEl) inputEl.value = "";
    runWhenConnected(() => {
      const plain = makePlain("chat", { channel: CHANNEL, text: text.slice(0, MAX_TEXT) });
      send(plain);
      seen.add(plain.id);
      const ts = new Date(plain.ts).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
      appendMessage("self", plain.sender, plain.text, plain.id, plain.senderId, ts);
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

  ensureEmptyState();
  setStatus("Not connected");
  wireInput(fullInput, fullSend);
  wireInput(sideInput, sideSend);
  // Connect right away so the room's message history (replayed by the relay
  // on WebSocket open) is visible without the visitor first focusing an input.
  connect();
})();
