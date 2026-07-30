// Dashboard ForkMesh room chat integration.
// Keeps the current dashboard UI, but uses the same encrypted room protocol as
// the production chat from forkmesh-today/cloudflare_worker/public/chat.js.

function mountForkMeshDashboardChat() {
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
  const CLAUDE_SENDER_ID = "claude";
  const CODEX_SENDER_ID = "codex";
  // The task board addresses one general bot instead of naming a vendor.
  const ORG_BOT_SENDER_ID = "agent";
  // "codex"/"claude" survive only so a stored selection still routes to the
  // general bot after the per-vendor options were removed.
  const AGENT_ASSIGNEE_VALUES = ["agent", "codex", "claude"];
  const CLAUDE_MENTION_RE = /(?:^|[^A-Za-z0-9_-])@claude\b/i;
  const CODEX_MENTION_RE = /(?:^|[^A-Za-z0-9_-])@codex\b/i;
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
  const REACTION_EMOJI = Object.freeze(["👍", "❤️", "😂", "🎉", "👀", "🚀"]);
  const CHAT_MENTION_RE = /(^|[^A-Za-z0-9_-])@([a-z](?:[a-z0-9-]{0,61}[a-z0-9])?)\b/gi;

  const fullLog = document.querySelector("#fullChatMessages");
  const sideLog = document.querySelector("#sideChatMessages");
  const fullInput = document.querySelector("#fullChatInput");
  const sideInput = document.querySelector("#sideChatInput");
  const fullSend = document.querySelector("#fullChatSend");
  const fullTaskSend = document.querySelector("#fullChatTaskSend");
  const sideSend = document.querySelector("#sideChatSend");
  const fullChannel = document.querySelector("#fullChatChannel");
  const fullRepository = document.querySelector("#fullChatRepo");
  const fullAction = document.querySelector("#fullChatAction");
  const taskRouting = document.querySelector("[data-dashboard-task-routing]");
  const taskDepartment = document.querySelector("[data-dashboard-task-department]");
  const taskTeam = document.querySelector("[data-dashboard-task-team]");
  const taskDestination = document.querySelector("[data-dashboard-task-destination]");
  const taskAssignee = document.querySelector("[data-dashboard-task-assignee]");
  const fullSendLabel = document.querySelector(
    "[data-dashboard-chat-send-label]",
  );
  const fullComposerHint = document.querySelector(
    "[data-dashboard-chat-composer-hint]",
  );
  const fullComposerStatus = document.querySelector(
    "[data-dashboard-chat-composer-status]",
  );
  const contextChannel = document.querySelector(
    "[data-dashboard-chat-context-channel]",
  );
  const contextSource = document.querySelector(
    "[data-dashboard-chat-context-source]",
  );
  const contextAction = document.querySelector(
    "[data-dashboard-chat-context-action]",
  );
  const simpleWorldComposer = Boolean(
    document.querySelector("[data-world-simple-composer]"),
  );
  const chatScrollRail = document.querySelector(
    "[data-dashboard-chat-scroll-rail]",
  );

  if (!fullLog && !sideLog) return false;
  if (fullLog?.dataset.forkmeshChatMounted === "true") return true;
  if (fullLog) fullLog.dataset.forkmeshChatMounted = "true";

  let pendingWorldComposerPrefill = null;
  const seenParentNotifications = new Set();

  function fileFromWorldComposerAttachment(value) {
    if (!value || typeof value !== "object") return null;
    const dataUrl = String(value.dataUrl || "");
    const match = dataUrl.match(
      /^data:(image\/(?:png|jpeg|webp));base64,([A-Za-z0-9+/=]+)$/,
    );
    if (!match || dataUrl.length > 700_000) return null;
    let bytes;
    try {
      const decoded = atob(match[2]);
      bytes = new Uint8Array(decoded.length);
      for (let index = 0; index < decoded.length; index += 1) {
        bytes[index] = decoded.charCodeAt(index);
      }
    } catch (_) {
      return null;
    }
    const suppliedName = safeAttachmentName(value.fileName || "world-screenshot.webp");
    return new File([bytes], suppliedName, {
      type: safeAttachmentMime(value.fileMime || match[1]),
    });
  }

  async function taskAttachmentMetadata(file) {
    const attachment = {
      name: safeAttachmentName(file?.name),
      mime: safeAttachmentMime(file?.type),
      size: Math.max(0, Number(file?.size) || 0),
    };
    if (
      !["image/png", "image/jpeg", "image/webp"].includes(attachment.mime) ||
      !file ||
      attachment.size > 1024 * 1024 ||
      typeof createImageBitmap !== "function"
    ) {
      return attachment;
    }
    try {
      const bitmap = await createImageBitmap(file);
      const side = 48;
      const canvas = document.createElement("canvas");
      canvas.width = side;
      canvas.height = side;
      const context = canvas.getContext("2d");
      const scale = Math.max(side / bitmap.width, side / bitmap.height);
      const width = bitmap.width * scale;
      const height = bitmap.height * scale;
      context.drawImage(
        bitmap,
        (side - width) / 2,
        (side - height) / 2,
        width,
        height,
      );
      bitmap.close?.();
      const thumbnail = canvas.toDataURL("image/webp", 0.72);
      if (thumbnail.length <= 10_000) attachment.thumbnail = thumbnail;
    } catch (_) {}
    return attachment;
  }

  function applyWorldComposerPrefill() {
    const data = pendingWorldComposerPrefill;
    if (!data) return false;
    const input = fullInput || sideInput;
    const control = attachmentControls.find(
      (candidate) => candidate.inputEl === input,
    );
    if (!input || (data.attachment && !control)) return false;
    const altText = String(data.attachment?.altText || "").slice(0, 500);
    input.value = String(data.text || altText || "");
    const file = fileFromWorldComposerAttachment(data.attachment);
    if (file && control) {
      stageDashboardAttachments(control, [file]);
      showAttachmentFeedback(
        control,
        "World screenshot attached. Choose a destination, then send.",
      );
    }
    pendingWorldComposerPrefill = null;
    input.focus();
    const caret = input.value.length;
    input.setSelectionRange?.(caret, caret);
    return true;
  }

  // The World's ForkBot avatar and CHAT bar ask this embed (parent frame,
  // same-origin) to drop starting text into the composer — e.g. "@forkbot "
  // so a visitor can start typing straight away. See world.js openChatTerminal.
  window.addEventListener("message", (event) => {
    if (event.origin !== location.origin) return;
    const data = event.data;
    if (!data) return;
    if (data.type === "forkmesh:chat-notification") {
      const id = String(data.id || "").slice(0, 96);
      if (id && seenParentNotifications.has(id)) return;
      const text = String(data.text || "").replace(/\s+/g, " ").trim().slice(
        0,
        240,
      );
      if (text) {
        if (id) seenParentNotifications.add(id);
        appendSystem(text, false);
      }
      return;
    }
    if (data.type !== "forkmesh:chat-prefill") return;
    pendingWorldComposerPrefill = {
      text: String(data.text || "").slice(0, MAX_TEXT),
      attachment:
        data.attachment && typeof data.attachment === "object"
          ? { ...data.attachment }
          : null,
    };
    applyWorldComposerPrefill();
  });
  if (window.parent !== window) {
    window.parent.postMessage(
      { type: "forkmesh:chat-ready" },
      location.origin,
    );
  } else if (document.querySelector("[data-world-native-chat]")) {
    window.dispatchEvent(
      new CustomEvent("forkmesh:world-chat-native", {
        detail: { type: "forkmesh:chat-ready" },
      }),
    );
  }

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

  // Baseline behind the site-header chat badge, shared with chat.js and
  // site-header.js. Every retained #general line this browser contributes
  // advances it, so talking here never leaves an unread pill on the rest of
  // the site. No baseline yet means the header seeds one silently.
  const CHAT_ACTIVITY_SEEN_KEY = "forkmesh.chat.activitySeen";

  function noteOwnChatActivity() {
    try {
      const raw = localStorage.getItem(CHAT_ACTIVITY_SEEN_KEY);
      if (!raw) return;
      const seenActivity = JSON.parse(raw);
      if (!seenActivity || typeof seenActivity !== "object") return;
      localStorage.setItem(CHAT_ACTIVITY_SEEN_KEY, JSON.stringify({
        ...seenActivity,
        messageCount: (Number(seenActivity.messageCount) || 0) + 1,
        at: Date.now(),
      }));
    } catch (_) {}
  }

  let roomKey = null;
  let socket = null;
  let connecting = false;
  let openCallbacks = [];
  let cachedUserSession = null;
  // Agent conversations are not ordinary room traffic. Access is resolved
  // against the server-authorized Engineering team before history renders or
  // the composer offers Claude/Codex mentions. Fail closed on every error.
  let orgAgentEngineeringAccess = false;
  let orgAgentAccessLoaded = false;
  const seen = new Set();
  const rows = new Map();
  const HISTORY_INITIAL_MESSAGES = 5;
  const HISTORY_BATCH_MESSAGES = 5;
  const historyRowIds = [];
  let historyVisibleCount = HISTORY_INITIAL_MESSAGES;
  let historyIndicator = null;
  let revealingHistory = false;
  let lastFullLogScrollTop = 0;
  let historyTouchStartY = null;
  let historyTouchRevealed = false;
  let historyWheelLatched = false;
  let historyWheelResetTimer = 0;
  let historyReplayTimer = 0;
  let historyReplayEnvelopes = [];
  let inboundFrameQueue = Promise.resolve();
  const DISCORD_REFRESH_MS = 60_000;
  const DISCORD_REFRESH_JITTER_MS = 15_000;
  const DISCORD_MAX_ORGANIZATIONS = 3;
  const DISCORD_MAX_CHANNELS = 5;
  const DISCORD_MAX_INITIAL_MESSAGES = 40;
  let discordRefreshTimer = 0;
  let discordRefreshRunning = false;
  let discordInitialMessagesLoaded = false;
  let discordSources = null;
  let discordBackoffUntil = 0;
  let discordBackoffAttempts = 0;
  // messageId -> Map(emoji -> Map(reactorId -> reactorName)); identical to
  // the full web/Qt protocol shape so reactions converge across every client.
  const reactions = new Map();
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
  const chatAvatarCache = new Map();
  let mentionCardEl = null;
  let activeMentionAnchor = null;
  let mentionHideTimer = null;
  // People seen in this room (live frames and replayed history), keyed by
  // lowercase handle: the relay keeps no roster, so this is what "who is here"
  // means for the composer's @mention list. mentionDirectory holds registered
  // accounts (fetched lazily on the first "@") so people who are offline right
  // now can still be mentioned.
  const mentionPeople = new Map();
  const mentionDirectory = new Map();
  let mentionDirectoryFetchedMs = 0;
  let mentionDirectoryPending = null;
  let mentionSuggestEl = null;
  // null when closed, else { input, items, index, start, end } where start/end
  // bound the "@partial" token in the input's value.
  let mentionSuggest = null;

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

  function discordRequestHeaders() {
    const headers = new Headers({ accept: "application/json" });
    const token = String(userSession()?.sessionToken || "").trim();
    if (token && token !== "cookie") {
      headers.set("authorization", `Bearer ${token}`);
    }
    return headers;
  }

  async function discordJson(path) {
    const response = await fetch(path, {
      headers: discordRequestHeaders(),
      credentials: "same-origin",
      cache: "no-store",
    });
    if (!response.ok) {
      const error = new Error(`Discord source unavailable (${response.status})`);
      error.status = response.status;
      const retryHeader = String(response.headers.get("retry-after") || "");
      const retrySeconds = Number(retryHeader);
      const retryDate = Date.parse(retryHeader);
      const requestedDelay = Number.isFinite(retrySeconds)
        ? Math.max(0, retrySeconds * 1000)
        : Number.isFinite(retryDate)
          ? Math.max(0, retryDate - Date.now())
          : 0;
      // Some edge/provider 429 and 503 responses omit Retry-After. Waiting
      // only for the ordinary one-minute poll in that case repeatedly fans
      // out across every organization and extends the rate limit.
      if (response.status === 429 || response.status === 503) {
        discordBackoffAttempts = Math.min(6, discordBackoffAttempts + 1);
      }
      const fallbackDelay =
        response.status === 429
          ? Math.min(
              30 * 60_000,
              5 * 60_000 * (2 ** (discordBackoffAttempts - 1)),
            )
          : response.status === 503
            ? Math.min(
                10 * 60_000,
                60_000 * (2 ** (discordBackoffAttempts - 1)),
              )
            : 0;
      error.retryAfterMs = Math.min(
        3_600_000,
        Math.max(requestedDelay, fallbackDelay),
      );
      if (error.retryAfterMs) {
        discordBackoffUntil = Math.max(
          discordBackoffUntil,
          Date.now() + error.retryAfterMs,
        );
      }
      throw error;
    }
    return response.json();
  }

  async function discoverDiscordSources() {
    if (Array.isArray(discordSources)) return discordSources;
    const catalog = await discordJson("/api/orgs");
    const organizations = (Array.isArray(catalog?.orgs) ? catalog.orgs : [])
      .slice(0, DISCORD_MAX_ORGANIZATIONS);
    const sources = [];
    // Connector discovery is deliberately sequential. A rate-limit response
    // from the first organization must prevent more doomed requests from being
    // launched at the same provider in the same tick.
    for (const record of organizations) {
      const organization = String(record?.name || "").trim();
      if (!organization) continue;
      try {
        const status = await discordJson(
          `/api/orgs/${encodeURIComponent(organization)}/discord`,
        );
        if (!status?.configured || status?.state !== "configured") continue;
        const channelIds = Array.isArray(status?.connector?.channelIds)
          ? status.connector.channelIds
          : [];
        const channelNames = new Map(
          (Array.isArray(status?.channels) ? status.channels : []).map((channel) => [
            String(channel?.id || ""),
            String(channel?.name || ""),
          ]),
        );
        sources.push(
          ...channelIds
            .slice(0, DISCORD_MAX_CHANNELS)
            .map((channelId) => ({
              organization,
              channelId: String(channelId || ""),
              channelName: channelNames.get(String(channelId || "")) || "",
            }))
            .filter((source) => source.channelId),
        );
      } catch (error) {
        if (error?.status === 429 || error?.status === 503) throw error;
        // One unavailable optional connector must not hide healthy connectors
        // belonging to the same account.
      }
    }
    discordSources = sources;
    return discordSources;
  }

  async function refreshDiscordMessages() {
    if (
      discordRefreshRunning ||
      document.visibilityState === "hidden" ||
      Date.now() < discordBackoffUntil ||
      !userSession()
    ) return;
    discordRefreshRunning = true;
    try {
      const sources = await discoverDiscordSources();
      const results = await Promise.allSettled(sources.map(async (source) => {
        const path =
          `/api/orgs/${encodeURIComponent(source.organization)}` +
          `/discord/messages?channelId=${encodeURIComponent(source.channelId)}`;
        const payload = await discordJson(path);
        const channelName = String(
          payload?.channel?.name || source.channelName || source.channelId,
        ).trim();
        return (Array.isArray(payload?.messages) ? payload.messages : []).map(
          (message) => ({
            ...message,
            organization: source.organization,
            channelName,
          }),
        );
      }));
      const messages = results
        .flatMap((result) => result.status === "fulfilled" ? result.value : [])
        .filter((message) => message?.id && String(message?.content || "").trim())
        .sort((left, right) =>
          Date.parse(left?.createdAt || "") - Date.parse(right?.createdAt || ""));
      const visibleMessages = discordInitialMessagesLoaded
        ? messages
        : messages.slice(-DISCORD_MAX_INITIAL_MESSAGES);
      let appended = 0;
      for (const message of visibleMessages) {
        if (appendDiscordMessage(message)) appended += 1;
      }
      discordBackoffAttempts = 0;
      discordInitialMessagesLoaded = true;
      if (appended) {
        console.info("[ForkMesh chat] Discord messages refreshed", {
          sources: sources.length,
          appended,
        });
      }
    } catch (error) {
      // Discord is optional. Keep the encrypted room usable and retry later.
      console.info("[ForkMesh chat] Discord refresh deferred", {
        reason: String(error?.message || "unavailable").slice(0, 160),
      });
      discordSources = null;
    } finally {
      discordRefreshRunning = false;
    }
  }

  function stopDiscordMessageRefresh() {
    if (!discordRefreshTimer) return;
    clearTimeout(discordRefreshTimer);
    discordRefreshTimer = 0;
  }

  function scheduleDiscordMessageRefresh() {
    stopDiscordMessageRefresh();
    if (document.visibilityState === "hidden" || !userSession()) return;
    const backoff = Math.max(0, discordBackoffUntil - Date.now());
    const jitter = Math.floor(Math.random() * DISCORD_REFRESH_JITTER_MS);
    discordRefreshTimer = window.setTimeout(async () => {
      discordRefreshTimer = 0;
      await refreshDiscordMessages();
      scheduleDiscordMessageRefresh();
    }, Math.max(DISCORD_REFRESH_MS + jitter, backoff));
  }

  function startDiscordMessageRefresh() {
    stopDiscordMessageRefresh();
    if (document.visibilityState === "hidden" || !userSession()) return;
    void refreshDiscordMessages();
    scheduleDiscordMessageRefresh();
  }

  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") {
      stopDiscordMessageRefresh();
      return;
    }
    startDiscordMessageRefresh();
  });

  function canJoinChat() {
    return PUBLIC_WORLD_GENERAL || Boolean(userSession());
  }

  function orgAgentIdentity(sender, senderId = "") {
    const names = [sender, senderId].map((value) =>
      String(value || "").trim().toLowerCase());
    return names.some((name) =>
      name === CLAUDE_SENDER_ID ||
      name === CODEX_SENDER_ID ||
      name === ORG_BOT_SENDER_ID);
  }

  function chatAccountKind() {
    return PUBLIC_WORLD_GENERAL && !userSession() ? "guest" : "user";
  }

  // Everyone in the public World room shows under the plain name they assert.
  // The old "World visitor · jett" prefix read as a second, different person
  // sitting next to the signed-in "jett", so it is stripped from anything that
  // still carries it (session names, replayed history frames).
  function publicWorldName(value) {
    return String(value || "")
      .replace(/^World visitor\s*·\s*/i, "")
      .trim()
      .slice(0, MAX_NAME) || "guest";
  }

  // The World floats each chat line above the speaker's avatar by matching the
  // sender against that avatar's presence name (world.js
  // handleWorldChatMessage). A signed-out visitor stands in the World as
  // "Guest ####" — derived from the same per-tab guest id this same-origin
  // iframe can read — so the embedded chat has to introduce itself under that
  // name or a guest's bubble never finds its avatar. Keep this in step with
  // guestId()/hashSuffix()/accountIdentity() in public/world/world.js.
  const WORLD_GUEST_ID_KEY = "forkmesh.world.guestId.v1";
  function worldGuestPresenceName() {
    if (requestedParams.get("worldEmbed") !== "1") return "";
    let guest = "";
    try {
      guest = String(sessionStorage.getItem(WORLD_GUEST_ID_KEY) || "");
    } catch (_) {
      guest = "";
    }
    if (!guest) return "";
    let hash = 0;
    for (const char of `guest:${guest}`) {
      hash = (Math.imul(hash, 31) + char.charCodeAt(0)) >>> 0;
    }
    return `Guest ${String(hash % 10000).padStart(4, "0")}`;
  }

  function displayName() {
    const session = userSession() || readSession();
    const value =
      session?.nodeName ||
      session?.email ||
      worldGuestPresenceName() ||
      `World Guest ${String(selfId).replace(/[^A-Za-z0-9]/g, "").slice(0, 6)}`;
    const name = String(value).trim().slice(0, MAX_NAME) || "World Guest";
    return PUBLIC_WORLD_GENERAL ? publicWorldName(name) : name;
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

  // ---- @mention autocomplete ---------------------------------------------
  // Typing "@" (plus an optional partial name) in the composer pops a
  // suggestion list; Tab (or Enter / click) accepts the highlighted name,
  // arrows move, Escape dismisses. Mirrors the full chat page composer
  // (chat.js) so both surfaces mention the same way — and so "@forkbot" is
  // reachable without typing it exactly.
  const MENTION_DIRECTORY_ENDPOINT = "/api/accounts/users";
  const MENTION_DIRECTORY_TTL_MS = 60000;
  const MENTION_LIMIT = 8;
  const MENTION_STALE_MS = 180000;
  // Only names the renderer would actually link (CHAT_MENTION_RE) are worth
  // suggesting: guest display names carry spaces and never become mentions.
  const MENTIONABLE_NAME_RE = /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/;

  function rememberMentionPerson(name, tsMs) {
    const key = mentionName(name);
    if (!key || !MENTIONABLE_NAME_RE.test(key)) return;
    const seenMs = Number(tsMs) || 0;
    const prev = mentionPeople.get(key);
    if (prev && prev.lastSeenMs >= seenMs) return;
    mentionPeople.set(key, {
      name: key,
      kind: key === FORKBOT_SENDER_ID ? "bot" : "user",
      lastSeenMs: seenMs,
    });
  }

  // Registered accounts, so someone who has not spoken in this room yet is
  // still mentionable. Fetched on the first "@" only (an idle dashboard tab
  // never pays for it) and refreshed at most once a minute. Resolves true when
  // it added names, so an open popup can re-render with them.
  function refreshMentionDirectory() {
    if (mentionDirectoryPending) return mentionDirectoryPending;
    if (
      mentionDirectoryFetchedMs &&
      Date.now() - mentionDirectoryFetchedMs < MENTION_DIRECTORY_TTL_MS
    ) {
      return Promise.resolve(false);
    }
    mentionDirectoryPending = fetch(MENTION_DIRECTORY_ENDPOINT, {
      headers: { accept: "application/json" },
    })
      .then((response) => (response.ok ? response.json() : null))
      .catch(() => null)
      .then((data) => {
        mentionDirectoryFetchedMs = Date.now();
        mentionDirectoryPending = null;
        const users = data && Array.isArray(data.users) ? data.users : [];
        let added = false;
        for (const user of users) {
          const key = mentionName(user && user.name);
          if (!key || !MENTIONABLE_NAME_RE.test(key)) continue;
          if (mentionDirectory.has(key)) continue;
          mentionDirectory.set(key, { name: key, kind: "user", lastSeenMs: 0 });
          added = true;
        }
        return added;
      });
    return mentionDirectoryPending;
  }

  // The "@partial" token the caret sits inside, or null. A mention starts at an
  // "@" preceded by whitespace/start and uses the charset the renderer links.
  function mentionTokenAtCaret(inputEl) {
    if (!inputEl) return null;
    const caret = inputEl.selectionStart;
    if (caret == null || inputEl.selectionEnd !== caret) return null;
    const before = String(inputEl.value || "").slice(0, caret);
    const match = /(^|\s)@([A-Za-z0-9-]{0,32})$/.exec(before);
    if (!match) return null;
    return {
      start: caret - match[2].length - 1, // include the "@"
      end: caret,
      partial: match[2].toLowerCase(),
    };
  }

  function mentionIsOnline(person) {
    return Date.now() - (person.lastSeenMs || 0) < MENTION_STALE_MS;
  }

  function mentionCandidates(partial) {
    const self = mentionName(displayName());
    const byName = new Map();
    for (const person of mentionDirectory.values()) byName.set(person.name, person);
    // Room presence wins over the directory entry: it carries a lastSeenMs.
    for (const person of mentionPeople.values()) byName.set(person.name, person);
    byName.set(FORKBOT_SENDER_ID, {
      name: FORKBOT_SENDER_ID,
      kind: "bot",
      lastSeenMs: Date.now(),
    });
    if (orgAgentEngineeringAccess) {
      for (const name of [CLAUDE_SENDER_ID, CODEX_SENDER_ID]) {
        byName.set(name, {
          name,
          kind: "bot",
          lastSeenMs: Date.now(),
        });
      }
    } else {
      byName.delete(CLAUDE_SENDER_ID);
      byName.delete(CODEX_SENDER_ID);
    }
    byName.delete(self);
    const matches = [...byName.values()].filter(
      (person) => !partial || person.name.includes(partial));
    matches.sort((a, b) => {
      const aPrefix = partial && a.name.startsWith(partial) ? 0 : 1;
      const bPrefix = partial && b.name.startsWith(partial) ? 0 : 1;
      return (aPrefix - bPrefix) ||
        (mentionIsOnline(b) - mentionIsOnline(a)) ||
        a.name.localeCompare(b.name);
    });
    return matches.slice(0, MENTION_LIMIT);
  }

  function ensureMentionSuggest(inputEl) {
    if (!mentionSuggestEl) {
      mentionSuggestEl = document.createElement("div");
      mentionSuggestEl.className =
        "absolute left-0 right-0 z-30 mb-2 overflow-y-auto rounded-lg border " +
        "border-border bg-background p-1 shadow-lg";
      // bottom/max-height inline: the dashboard ships a pre-compiled Tailwind
      // subset that has no bottom-full / max-h-56 rule to lean on.
      mentionSuggestEl.style.bottom = "100%";
      mentionSuggestEl.style.maxHeight = "224px";
      mentionSuggestEl.hidden = true;
      mentionSuggestEl.setAttribute("role", "listbox");
      mentionSuggestEl.setAttribute("aria-label", "Mention a user");
    }
    // Anchor to the composer pill the input lives in, so the list floats over
    // the transcript instead of pushing the composer around.
    const host = inputEl.parentElement;
    if (host && mentionSuggestEl.parentElement !== host) {
      host.classList.add("relative");
      host.append(mentionSuggestEl);
    }
    return mentionSuggestEl;
  }

  function closeMentionSuggest() {
    mentionSuggest = null;
    if (mentionSuggestEl) mentionSuggestEl.hidden = true;
  }

  function renderMentionSuggest() {
    if (!mentionSuggest) return;
    const box = ensureMentionSuggest(mentionSuggest.input);
    box.textContent = "";
    const hint = document.createElement("div");
    hint.className = "px-2 py-1 text-[10px] text-muted-foreground";
    hint.textContent = "Tab to mention · Esc to dismiss";
    box.append(hint);
    mentionSuggest.items.forEach((person, index) => {
      const active = index === mentionSuggest.index;
      const item = document.createElement("button");
      item.type = "button";
      item.setAttribute("role", "option");
      item.setAttribute("aria-selected", active ? "true" : "false");
      item.className =
        "flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-left text-xs " +
        (active
          ? "bg-secondary text-foreground"
          : "text-muted-foreground hover:bg-secondary/60");
      const avatar = document.createElement("span");
      avatar.className =
        "flex h-5 w-5 shrink-0 items-center justify-center rounded-full border " +
        "border-border bg-secondary font-mono text-[10px] font-semibold text-foreground";
      avatar.textContent = person.name.slice(0, 1).toUpperCase();
      const label = document.createElement("span");
      label.className = "truncate font-semibold";
      label.textContent = "@" + person.name;
      item.append(avatar, label);
      if (mentionIsOnline(person)) {
        const dot = document.createElement("span");
        dot.className = "ml-auto h-1.5 w-1.5 shrink-0 rounded-full bg-primary";
        dot.title = "Active in this room";
        item.append(dot);
      }
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
    const activeItem = box.querySelector('[aria-selected="true"]');
    if (activeItem) activeItem.scrollIntoView({ block: "nearest" });
  }

  function showMentionSuggest(inputEl) {
    const token = mentionTokenAtCaret(inputEl);
    if (!token) {
      closeMentionSuggest();
      return;
    }
    const items = mentionCandidates(token.partial);
    if (!items.length) {
      closeMentionSuggest();
      return;
    }
    const prev = mentionSuggest && mentionSuggest.items[mentionSuggest.index];
    let index = prev ? items.findIndex((person) => person.name === prev.name) : 0;
    if (index < 0) index = 0;
    mentionSuggest = { input: inputEl, items, index, start: token.start, end: token.end };
    renderMentionSuggest();
  }

  function updateMentionSuggest(inputEl) {
    if (!mentionTokenAtCaret(inputEl)) {
      closeMentionSuggest();
      return;
    }
    refreshMentionDirectory().then((added) => {
      if (added && mentionSuggest && mentionSuggest.input === inputEl) {
        showMentionSuggest(inputEl);
      }
    });
    showMentionSuggest(inputEl);
  }

  function acceptMentionSuggest(index) {
    if (!mentionSuggest) return;
    const inputEl = mentionSuggest.input;
    const person = mentionSuggest.items[
      typeof index === "number" ? index : mentionSuggest.index];
    if (!person || !inputEl) return;
    const value = String(inputEl.value || "");
    const inserted = "@" + person.name + " ";
    inputEl.value =
      value.slice(0, mentionSuggest.start) + inserted + value.slice(mentionSuggest.end);
    const caret = mentionSuggest.start + inserted.length;
    closeMentionSuggest();
    inputEl.setSelectionRange(caret, caret);
    inputEl.focus();
  }

  function moveMentionSuggest(step) {
    if (!mentionSuggest) return;
    const count = mentionSuggest.items.length;
    mentionSuggest.index = (mentionSuggest.index + step + count) % count;
    renderMentionSuggest();
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

  function generatedChatFace(handle) {
    const faces = ["🙂", "😄", "😊", "🤓", "🧐", "😎", "😁", "🤠"];
    let hash = 2166136261;
    for (const char of String(handle || "guest").toLowerCase()) {
      hash ^= char.charCodeAt(0);
      hash = Math.imul(hash, 16777619) >>> 0;
    }
    return faces[hash % faces.length];
  }

  function publicChatAvatar(handle) {
    const name = String(handle || "").trim().toLowerCase();
    const session = userSession();
    if (
      session?.nodeName?.toLowerCase() === name &&
      String(session.avatarPng || "").length
    ) {
      return Promise.resolve(String(session.avatarPng));
    }
    if (!/^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(name)) {
      return Promise.resolve("");
    }
    if (chatAvatarCache.has(name)) return chatAvatarCache.get(name);
    const pending = fetch(`/api/accounts/${encodeURIComponent(name)}`, {
      headers: { accept: "application/json" },
    })
      .then((response) => (response.ok ? response.json() : null))
      .then((profile) => {
        const avatar = String(profile?.avatarPng || "");
        return (
          profile?.exists === true &&
          profile?.profilePrivate !== true &&
          avatar.length <= 350_000 &&
          /^[A-Za-z0-9+/=]+$/.test(avatar)
        )
          ? avatar
          : "";
      })
      .catch(() => "");
    chatAvatarCache.set(name, pending);
    return pending;
  }

  function hydrateChatAvatar(avatar, handle) {
    if (!avatar) return;
    const name = String(handle || "guest").trim() || "guest";
    avatar.textContent =
      ["forkbot", "claude", "codex"].includes(name.toLowerCase())
        ? "🤖"
        : generatedChatFace(name);
    avatar.setAttribute("aria-label", `${name} avatar`);
    void publicChatAvatar(name).then((avatarPng) => {
      if (!avatar.isConnected || !avatarPng) return;
      const image = document.createElement("img");
      image.src = `data:image/png;base64,${avatarPng}`;
      image.alt = "";
      image.className = "h-full w-full rounded-full object-cover";
      avatar.replaceChildren(image);
    });
  }

  function fmtChatTime(tsMs) {
    const value = Number(tsMs);
    if (!value) return "";
    return new Date(value).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  }

  let imagePreviewDialog = null;

  function openImagePreview(src, fileName) {
    if (!imagePreviewDialog) {
      imagePreviewDialog = document.createElement("dialog");
      imagePreviewDialog.id = "dashboard-chat-image-preview-dialog";
      imagePreviewDialog.className = "chat-image-preview-dialog max-h-[calc(100vh-1.5rem)] max-w-[calc(100vw-2rem)] rounded-xl border border-border bg-background p-3 shadow-2xl";
      imagePreviewDialog.innerHTML = '<button type="button" class="absolute right-0 top-0 inline-flex h-8 w-8 items-center justify-center rounded-md border border-border bg-background text-xl leading-none text-foreground" aria-label="Close image preview">×</button><img class="chat-image-preview-full max-h-[calc(100vh-1.5rem)] max-w-full object-contain" />';
      imagePreviewDialog.querySelector("button").addEventListener("click", () => imagePreviewDialog.close());
      imagePreviewDialog.addEventListener("click", (event) => {
        if (event.target === imagePreviewDialog) imagePreviewDialog.close();
      });
      document.body.append(imagePreviewDialog);
    }
    const image = imagePreviewDialog.querySelector("img");
    image.src = src;
    image.alt = fileName;
    if (!imagePreviewDialog.open) imagePreviewDialog.showModal();
  }

  function renderAttachment(attachment, compact = false) {
    if (!attachment) return null;
    const objectUrl = attachmentObjectUrl(attachment);
    const wrapper = document.createElement("div");
    wrapper.className = "chat-attachment-wrapper";
    wrapper.className += compact
      ? " chat-attachment-wrapper--compact mt-1 grid gap-1.5"
      : " mt-2 grid max-w-md gap-2";
    if (attachment.fileMime.startsWith("image/")) {
      const preview = document.createElement("div");
      preview.className = "chat-attachment-preview relative w-fit max-w-full";
      const image = document.createElement("img");
      image.className = "chat-attachment-image";
      image.className += compact
        ? " max-h-28 max-w-full rounded-md border border-border object-contain"
        : " max-h-72 max-w-full rounded-lg border border-border bg-secondary object-contain";
      image.src = objectUrl;
      image.alt = attachment.fileName;
      image.loading = "lazy";
      image.style.minWidth = compact
        ? "min(72px, 100%)"
        : "min(96px, 100%)";
      image.style.minHeight = compact ? "54px" : "72px";
      image.tabIndex = 0;
      image.setAttribute("role", "button");
      image.setAttribute("aria-label", `Open ${attachment.fileName} full size`);
      image.addEventListener("click", () => openImagePreview(objectUrl, attachment.fileName));
      image.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          openImagePreview(objectUrl, attachment.fileName);
        }
      });
      const download = document.createElement("a");
      download.href = objectUrl;
      download.download = attachment.fileName;
      download.setAttribute("aria-label", `Download ${attachment.fileName}`);
      download.title = `Download ${attachment.fileName}`;
      download.className = "chat-attachment-download absolute bottom-2 right-2 inline-flex h-7 w-7 items-center justify-center rounded-md border border-border bg-background/90 text-muted-foreground shadow-sm hover:text-foreground";
      download.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3v12m0 0 4-4m-4 4-4-4M5 21h14"></path></svg>';
      preview.append(image, download);
      wrapper.append(preview);
      return wrapper;
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

  function ensureHistoryIndicator() {
    if (!fullLog) return null;
    if (historyIndicator?.isConnected) return historyIndicator;
    historyIndicator = document.createElement("button");
    historyIndicator.type = "button";
    historyIndicator.className = "chat-history-indicator";
    historyIndicator.setAttribute("aria-live", "polite");
    historyIndicator.addEventListener("click", () => revealOlderHistory());
    if (simpleWorldComposer) fullLog.append(historyIndicator);
    else fullLog.prepend(historyIndicator);
    return historyIndicator;
  }

  function currentHistoryRows() {
    const records = historyRowIds
      .map((id) => rows.get(id))
      .filter(Boolean);
    return simpleWorldComposer
      ? records.sort((left, right) => right.tsMs - left.tsMs)
      : records;
  }

  function insertHistoryRow(record) {
    if (!fullLog || !record?.el) return;
    const index = historyRowIds.indexOf(record.id);
    for (let offset = index + 1; offset < historyRowIds.length; offset += 1) {
      const next = rows.get(historyRowIds[offset]);
      if (next?.el?.isConnected) {
        fullLog.insertBefore(record.el, next.el);
        return;
      }
    }
    const firstLive = fullLog.querySelector(
      ".chat-message-row:not([data-chat-history])",
    );
    if (firstLive) fullLog.insertBefore(record.el, firstLive);
    else fullLog.append(record.el);
  }

  function materializeFullMessage(record) {
    if (!fullLog || !record || record.el?.isConnected) return record?.el || null;
    clearEmptyState();
    const row = document.createElement("div");
    row.className =
      `chat-message-row chat-message-row--${record.self ? "self" : "peer"} ` +
      "flex items-start gap-3 group px-2 py-1 transition-colors mt-3";
    if (record.history) row.dataset.chatHistory = "";
    row.innerHTML = `
      <span class="chat-message-avatar avatar flex h-7 w-7 shrink-0 items-center justify-center overflow-hidden rounded-full border text-base ${record.self ? "border-primary/40 bg-primary/10 text-primary" : "border-border bg-secondary text-foreground"}"></span>
      <div class="chat-message-bubble min-w-0 flex-1">
        <div class="mb-0.5 flex items-baseline gap-2">
          <span class="text-xs font-semibold ${record.self ? "text-primary" : "text-foreground"}">${escapeHtml(record.who)}</span>
          <span class="text-[10px] text-muted-foreground/50 font-mono">${escapeHtml(fmtChatTime(record.tsMs))}</span>
        </div>
        <p class="text-sm text-muted-foreground leading-relaxed break-words"></p>
      </div>`;
    const avatarEl = row.querySelector(".chat-message-avatar");
    if (record.external && record.avatarUrl) {
      const image = document.createElement("img");
      image.src = record.avatarUrl;
      image.alt = "";
      image.referrerPolicy = "no-referrer";
      image.className = "h-full w-full rounded-full object-cover";
      avatarEl.replaceChildren(image);
      avatarEl.setAttribute("aria-label", `${record.who} Discord avatar`);
    } else if (record.external) {
      avatarEl.textContent = "D";
      avatarEl.setAttribute("aria-label", `${record.who} Discord avatar`);
    } else {
      hydrateChatAvatar(avatarEl, record.who);
    }
    if (record.sourceLabel) {
      const source = document.createElement("span");
      source.className =
        "chat-message-source rounded-full border border-border px-1.5 py-0.5 " +
        "text-[9px] font-semibold text-muted-foreground";
      source.textContent = record.sourceLabel;
      row.querySelector(".items-baseline")?.append(source);
    }
    const textEl = row.querySelector("p");
    if (textEl) {
      if (record.text) appendMentionText(textEl, record.text);
      else textEl.remove();
    }
    const content = row.querySelector(".min-w-0.flex-1");
    const renderedAttachment = renderAttachment(record.attachment);
    if (content && renderedAttachment) content.append(renderedAttachment);
    const reactionsEl = document.createElement("div");
    reactionsEl.className = "chat-reactions";
    content?.append(reactionsEl);
    record.el = row;
    record.avatarEl = avatarEl;
    record.textEl = textEl?.parentNode ? textEl : null;
    record.contentEl = content;
    record.reactionsEl = reactionsEl;
    if (record.history && !simpleWorldComposer) {
      insertHistoryRow(record);
    } else if (simpleWorldComposer) {
      const next = Array.from(fullLog.querySelectorAll(".chat-message-row"))
        .find((candidate) =>
          Number(candidate.dataset.chatTimestamp || 0) <= record.tsMs);
      row.dataset.chatTimestamp = String(record.tsMs);
      fullLog.insertBefore(row, next || historyIndicator || null);
    } else {
      fullLog.append(row);
    }
    if (record.id && !record.external) {
      content?.append(buildMessageActions(record));
      renderReactions(record.id);
      if (record.editedAt) markEdited(record, record.editedAt);
    }
    return row;
  }

  function renderHistoryWindow({ preserveScroll = false } = {}) {
    if (!fullLog) return;
    const records = currentHistoryRows();
    if (!records.length) {
      historyIndicator?.remove();
      historyIndicator = null;
      ensureEmptyState();
      syncChatScrollThumb();
      return;
    }
    clearEmptyState();
    const previousHeight = fullLog.scrollHeight;
    const visibleCount = Math.min(historyVisibleCount, records.length);
    const hiddenCount = Math.max(0, records.length - visibleCount);
    ensureHistoryIndicator();
    records.forEach((record, index) => {
      const visible = simpleWorldComposer
        ? index < visibleCount
        : index >= hiddenCount;
      if (visible) materializeFullMessage(record);
      if (record.el) record.el.hidden = !visible;
    });
    const indicator = ensureHistoryIndicator();
    indicator.dataset.complete = hiddenCount ? "false" : "true";
    indicator.disabled = hiddenCount <= 0;
    indicator.textContent = hiddenCount
      ? `${simpleWorldComposer ? "↓" : "↑"} ${hiddenCount} earlier message${hiddenCount === 1 ? "" : "s"} · ${simpleWorldComposer ? "open" : "scroll up"} to load ${Math.min(HISTORY_BATCH_MESSAGES, hiddenCount)}`
      : "Beginning of conversation";
    if (preserveScroll) {
      fullLog.scrollTop += Math.max(0, fullLog.scrollHeight - previousHeight);
    } else {
      fullLog.scrollTop = simpleWorldComposer ? 0 : fullLog.scrollHeight;
    }
    lastFullLogScrollTop = fullLog.scrollTop;
    syncChatScrollThumb();
  }

  function revealOlderHistory() {
    if (revealingHistory || !fullLog) return;
    const records = currentHistoryRows();
    if (historyVisibleCount >= records.length) return;
    revealingHistory = true;
    historyVisibleCount = Math.min(
      records.length,
      historyVisibleCount + HISTORY_BATCH_MESSAGES,
    );
    renderHistoryWindow({ preserveScroll: true });
    requestAnimationFrame(() => {
      revealingHistory = false;
    });
  }

  function hasHiddenHistory() {
    return historyVisibleCount < currentHistoryRows().length;
  }

  function scheduleHistoryWheelReset() {
    if (historyWheelResetTimer) clearTimeout(historyWheelResetTimer);
    historyWheelResetTimer = window.setTimeout(() => {
      historyWheelResetTimer = 0;
      historyWheelLatched = false;
    }, 180);
  }

  function appendFullMessage(
    kind,
    who,
    text,
    id,
    senderId,
    tsMs,
    attachment = null,
    deferHistory = false,
    metadata = null,
  ) {
    if (!fullLog) return;
    const self = kind === "self";
    const record = {
      id,
      el: null,
      who,
      tsMs: Number(tsMs) || Date.now(),
      senderId: senderId || "",
      attachment,
      text: text || "",
      self,
      history: Boolean(deferHistory),
      external: Boolean(metadata?.external),
      sourceLabel: String(metadata?.sourceLabel || ""),
      avatarUrl: String(metadata?.avatarUrl || ""),
    };
    if (id) rows.set(id, record);
    if (deferHistory && id) {
      historyRowIds.push(id);
      return;
    }
    materializeFullMessage(record);
    fullLog.scrollTop = simpleWorldComposer ? 0 : fullLog.scrollHeight;
  }

  // ---- edit / delete own messages ----------------------------------------
  // The relay already replays "edit"/"delete" frames (handlePlain below, and
  // the desktop client's kDurableTypes), so the dashboard only needs the
  // author-side controls. Both frames are honoured by peers only when the
  // sender id matches the original message's, so the same guard is applied
  // here before anything is broadcast.

  function messageActionButton(label, onClick, { danger = false, ariaLabel } = {}) {
    const button = document.createElement("button");
    button.type = "button";
    button.className =
      "rounded border border-border bg-background px-1.5 py-0.5 text-[10px] font-semibold " +
      "hover:bg-secondary " +
      (danger ? "text-destructive" : "text-muted-foreground hover:text-foreground");
    button.textContent = label;
    button.setAttribute("aria-label", ariaLabel || `${label} message`);
    button.addEventListener("click", onClick);
    return button;
  }

  function buildMessageActions(record) {
    const actions = document.createElement("div");
    // .chat-message-actions is revealed on row hover / keyboard focus by the
    // dashboard shell stylesheet (Tailwind's group-hover variant is not in the
    // pre-built dashboard/tailwind.css, so the reveal is hand-written CSS).
    actions.className = "chat-message-actions ml-auto flex shrink-0 items-center gap-1";
    actions.append(
      messageActionButton("☺", (event) =>
        showReactionPicker(record, event.currentTarget), {
        ariaLabel: "Add reaction",
      }),
    );
    if (record.self && record.senderId === selfId) {
      if (record.text) {
        actions.append(messageActionButton("Edit", () => beginMessageEdit(record)));
      }
      actions.append(
        messageActionButton("Delete", () => requestMessageDelete(record), { danger: true })
      );
    }
    record.actionsEl = actions;
    return actions;
  }

  let activeReactionPicker = null;

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
      perEmoji.set(
        reactor,
        String(plain.reactorName || plain.sender || "peer").slice(0, MAX_NAME),
      );
    } else {
      perEmoji.delete(reactor);
    }
    if (!perEmoji.size) perMessage.delete(emoji);
    if (!perMessage.size) reactions.delete(target);
    renderReactions(target);
  }

  function renderReactions(messageId) {
    const record = rows.get(messageId);
    if (!record?.reactionsEl) return;
    record.reactionsEl.textContent = "";
    const perMessage = reactions.get(messageId);
    if (!perMessage) return;
    for (const [emoji, reactors] of perMessage) {
      if (!reactors.size) continue;
      const chip = document.createElement("button");
      chip.type = "button";
      chip.className =
        "chat-reaction-chip" + (reactors.has(selfId) ? " is-mine" : "");
      chip.title = [...reactors.values()].join(", ");
      chip.setAttribute(
        "aria-label",
        `${emoji} reaction from ${reactors.size} ${
          reactors.size === 1 ? "person" : "people"
        }`,
      );
      const face = document.createElement("span");
      face.textContent = emoji;
      const count = document.createElement("span");
      count.className = "chat-reaction-count";
      count.textContent = String(reactors.size);
      chip.append(face, count);
      chip.addEventListener("click", () => toggleReaction(messageId, emoji));
      record.reactionsEl.append(chip);
    }
  }

  function toggleReaction(messageId, emoji) {
    if (!canJoinChat()) {
      showUserOnlyState();
      return;
    }
    const mine = Boolean(reactions.get(messageId)?.get(emoji)?.has(selfId));
    const plain = makePlain("reaction", {
      conversation: CHANNEL_LABEL,
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

  function closeReactionPicker({ restoreFocus = false } = {}) {
    if (!activeReactionPicker) return;
    const { element, trigger } = activeReactionPicker;
    element.remove();
    activeReactionPicker = null;
    if (restoreFocus) trigger?.focus();
  }

  function showReactionPicker(record, trigger) {
    if (!record?.id || !record.contentEl) return;
    closeReactionPicker();
    const picker = document.createElement("div");
    picker.className = "chat-reaction-picker";
    picker.setAttribute("role", "toolbar");
    picker.setAttribute("aria-label", "Choose a reaction");
    REACTION_EMOJI.forEach((emoji) => {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = emoji;
      button.setAttribute("aria-label", `React with ${emoji}`);
      button.addEventListener("click", () => {
        toggleReaction(record.id, emoji);
        closeReactionPicker();
      });
      picker.append(button);
    });
    picker.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      event.preventDefault();
      closeReactionPicker({ restoreFocus: true });
    });
    record.contentEl.append(picker);
    activeReactionPicker = { element: picker, trigger };
    picker.querySelector("button")?.focus();
  }

  // An "(edited)" marker so a rewritten message never silently replaces what
  // peers already read (matches the full chat page).
  function markEdited(record, editedAt) {
    if (!record?.textEl) return;
    const stamp = Number(editedAt) || Date.now();
    let marker = record.textEl.querySelector(".chat-edited");
    if (!marker) {
      marker = document.createElement("span");
      marker.className = "chat-edited ml-1 text-[10px] text-muted-foreground/50";
      marker.textContent = "(edited)";
      record.textEl.append(marker);
    }
    marker.title = `Edited ${new Date(stamp).toLocaleString()}`;
  }

  // The action rows carry Tailwind's `flex` utility, which outranks the
  // [hidden] preflight rule in the pre-built stylesheet — toggle display
  // directly rather than the attribute.
  function showElement(el, visible) {
    if (el) el.style.display = visible ? "" : "none";
  }

  function closeMessageEditor(record, { restoreActions = true } = {}) {
    record.editorEl?.remove();
    record.editorEl = null;
    showElement(record.textEl, true);
    if (restoreActions) showElement(record.actionsEl, true);
  }

  function beginMessageEdit(record) {
    if (!record?.textEl || record.senderId !== selfId) return;
    if (record.editorEl) {
      record.editorEl.querySelector("textarea")?.focus();
      return;
    }
    showElement(record.textEl, false);
    showElement(record.actionsEl, false);
    const editor = document.createElement("div");
    editor.className = "chat-inline-editor mt-1 flex flex-col gap-1.5";
    const textarea = document.createElement("textarea");
    textarea.className =
      "w-full resize-y rounded-md border border-border bg-background px-2 py-1 text-sm";
    textarea.rows = 2;
    textarea.maxLength = MAX_TEXT;
    textarea.value = record.text || "";
    textarea.setAttribute("aria-label", "Edit message");
    const controls = document.createElement("div");
    controls.className = "flex items-center gap-2";
    const cancel = messageActionButton(
      "Cancel", () => closeMessageEditor(record), { ariaLabel: "Cancel edit" });
    const save = messageActionButton(
      "Save", () => saveMessageEdit(record, textarea.value), { ariaLabel: "Save edit" });
    textarea.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        event.preventDefault();
        closeMessageEditor(record);
      } else if (event.key === "Enter" && !event.shiftKey) {
        event.preventDefault();
        saveMessageEdit(record, textarea.value);
      }
    });
    controls.append(cancel, save);
    editor.append(textarea, controls);
    record.editorEl = editor;
    (record.contentEl || record.el).append(editor);
    textarea.focus();
    textarea.setSelectionRange(textarea.value.length, textarea.value.length);
  }

  function saveMessageEdit(record, source) {
    if (record.senderId !== selfId) return;
    const text = String(source || "").trim().slice(0, MAX_TEXT);
    if (!text) return;
    const editedAt = Date.now();
    const plain = makePlain("edit", {
      conversation: CHANNEL_LABEL,
      target: record.id,
      text,
      editedAt,
    });
    record.text = text;
    renderMessageText(record.textEl, text);
    markEdited(record, editedAt);
    closeMessageEditor(record);
    const sideEntry = sideEntries.find((entry) => entry.id === record.id);
    if (sideEntry) {
      sideEntry.text = text;
      renderSideMessages();
    }
    seen.add(plain.id);
    runWhenConnected(() => send(plain));
  }

  // Deleting is irreversible for every reader, so ask first — inline, because
  // this chat also runs inside the World's same-origin embed where a blocking
  // window.confirm() would freeze the host frame.
  function requestMessageDelete(record) {
    if (record.senderId !== selfId || !record.actionsEl) return;
    if (record.confirmEl) return;
    showElement(record.actionsEl, false);
    const confirmBar = document.createElement("div");
    confirmBar.className =
      "chat-delete-confirm ml-auto flex shrink-0 items-center gap-1 text-[10px] text-muted-foreground";
    const label = document.createElement("span");
    label.textContent = "Delete?";
    const cancel = messageActionButton("Cancel", () => {
      confirmBar.remove();
      record.confirmEl = null;
      showElement(record.actionsEl, true);
    }, { ariaLabel: "Cancel delete" });
    const confirm = messageActionButton(
      "Delete", () => confirmMessageDelete(record), { danger: true, ariaLabel: "Confirm delete" });
    confirmBar.append(label, cancel, confirm);
    record.confirmEl = confirmBar;
    record.el.append(confirmBar);
    confirm.focus();
  }

  function confirmMessageDelete(record) {
    if (record.senderId !== selfId) return;
    const plain = makePlain("delete", {
      conversation: CHANNEL_LABEL,
      target: record.id,
    });
    record.confirmEl = null;
    seen.add(plain.id);
    removeMessage(record.id);
    runWhenConnected(() => send(plain));
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
        <span class="chat-message-avatar avatar flex h-6 w-6 shrink-0 items-center justify-center overflow-hidden rounded-full border text-sm ${self ? "border-primary/40 bg-primary/10 text-primary" : "border-border bg-secondary text-foreground"}"></span>
        <div class="min-w-0 flex-1">
          <div class="flex items-baseline gap-1.5">
            <span class="text-[11px] font-semibold ${self ? "text-primary" : "text-foreground"}">${escapeHtml(message.who)}</span>
            <span class="text-[9px] text-muted-foreground/50 font-mono">${escapeHtml(fmtChatTime(message.tsMs))}</span>
          </div>
          <p class="text-xs text-muted-foreground leading-relaxed break-words"></p>
        </div>`;
      const avatar = row.querySelector(".chat-message-avatar");
      if (message.external) {
        avatar.textContent = "D";
        avatar.setAttribute("aria-label", "Discord");
      } else {
        hydrateChatAvatar(row.querySelector(".chat-message-avatar"), message.who);
      }
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

  function appendSideMessage(
    kind,
    who,
    text,
    id,
    senderId,
    tsMs,
    attachment = null,
    deferRender = false,
    metadata = null,
  ) {
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
      external: Boolean(metadata?.external),
      sourceLabel: String(metadata?.sourceLabel || ""),
    };
    let index = sideEntries.length;
    while (index > 0 && Number(sideEntries[index - 1].tsMs) > entry.tsMs) index -= 1;
    sideEntries.splice(index, 0, entry);
    if (!deferRender) renderSideMessages();
  }

  function appendMessage(
    kind,
    who,
    text,
    id,
    senderId,
    tsMs,
    attachment = null,
    deferHistory = false,
  ) {
    if (orgAgentIdentity(who, senderId) && !orgAgentEngineeringAccess) return;
    appendFullMessage(
      kind,
      who,
      text,
      id,
      senderId,
      tsMs,
      attachment,
      deferHistory,
    );
    appendSideMessage(
      kind,
      who,
      text,
      id,
      senderId,
      tsMs,
      attachment,
      deferHistory,
    );
    rememberContext(who, text);
  }

  function appendDiscordMessage(message) {
    const organization = String(message?.organization || "").trim();
    const channelId = String(message?.channelId || "").trim();
    const channelName = String(message?.channelName || message?.channelId || "")
      .trim();
    const providerId = String(message?.id || "").trim();
    if (!organization || !channelId || !channelName || !providerId) return false;
    const id = `discord:${organization}:${channelId}:${providerId}`;
    if (rows.has(id)) return false;
    const projectedName = String(message?.author?.name || "").trim();
    const who = projectedName && !/^\d{5,}$/.test(projectedName)
      ? projectedName
      : "Discord member";
    const text = String(message?.content || "").trim();
    if (!text) return false;
    const parsedTime = Date.parse(String(message?.createdAt || ""));
    const tsMs = Number.isFinite(parsedTime) ? parsedTime : Date.now();
    const metadata = {
      external: true,
      sourceLabel: `Discord · ${organization} · #${channelName}`,
      avatarUrl: /^https:\/\/cdn\.discordapp\.com\/(?:avatars\/\d{17,20}\/[A-Za-z0-9_-]{2,128}\.(?:png|webp)(?:\?size=64)?|embed\/avatars\/[0-5]\.png)$/.test(
        String(message?.author?.avatarUrl || ""),
      )
        ? String(message.author.avatarUrl)
        : "",
    };
    appendFullMessage(
      "peer", who, text, id, `discord:${providerId}`, tsMs, null, false, metadata,
    );
    appendSideMessage(
      "peer", who, text, id, `discord:${providerId}`, tsMs, null, false, metadata,
    );
    // Provider text is display-only. It never enters ForkBot's prompt context
    // unless a person explicitly quotes it into a ForkMesh message.
    return true;
  }

  function appendSystem(text, emit = true) {
    if (fullLog) {
      clearEmptyState();
      const row = document.createElement("div");
      row.className = "chat-system-bubble my-2 rounded-md border border-border px-3 py-2 text-xs text-muted-foreground";
      row.textContent = text;
      if (simpleWorldComposer) {
        fullLog.prepend(row);
        fullLog.scrollTop = 0;
      } else {
        fullLog.append(row);
        fullLog.scrollTop = fullLog.scrollHeight;
      }
    }
    if (emit) emitWorldActivity(text, "status");
  }

  function removeMessage(id, { deferRender = false } = {}) {
    const rec = rows.get(id);
    if (activeReactionPicker?.element && rec?.el?.contains(
        activeReactionPicker.element)) {
      closeReactionPicker();
    }
    if (rec?.el?.parentNode) rec.el.parentNode.removeChild(rec.el);
    if (rec?.attachment) releaseAttachment(rec.attachment);
    rows.delete(id);
    const idx = sideEntries.findIndex((entry) => entry.id === id);
    if (idx >= 0 && sideEntries[idx].attachment !== rec?.attachment) {
      releaseAttachment(sideEntries[idx].attachment);
    }
    if (idx >= 0) sideEntries.splice(idx, 1);
    const historyIndex = historyRowIds.indexOf(id);
    if (historyIndex >= 0) historyRowIds.splice(historyIndex, 1);
    reactions.delete(id);
    if (!deferRender) {
      renderSideMessages();
      if (historyIndex >= 0) {
        renderHistoryWindow({ preserveScroll: true });
      }
    }
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
  // (world.js handleWorldChatMessage). Same-origin only. History replays are
  // marked so the parent updates only its collapsed CHAT bar with the most
  // recent line — reconnects never resurrect old bubbles.
  const WORLD_EMBED_BUBBLES =
    requestedParams.get("worldEmbed") === "1" ||
    Boolean(document.querySelector("[data-world-native-chat]"));
  const worldAttachmentPreviewCache = new WeakMap();

  function emitWorldActivity(text, kind = "status") {
    if (!WORLD_EMBED_BUBBLES) return;
    const message = String(text || "").replace(/\s+/g, " ").trim().slice(0, 240);
    if (!message) return;
    try {
      window.parent.postMessage(
        {
          type: "forkmesh:world-activity",
          kind: String(kind || "status").slice(0, 24),
          text: message,
          ts: Date.now(),
        },
        location.origin,
      );
    } catch (_) {}
  }

  function worldAttachmentPreview(attachment) {
    if (
      !attachment ||
      typeof attachment !== "object" ||
      !String(attachment.fileMime || "").startsWith("image/")
    ) {
      return Promise.resolve("");
    }
    if (worldAttachmentPreviewCache.has(attachment)) {
      return worldAttachmentPreviewCache.get(attachment);
    }
    const pending = new Promise((resolve) => {
      const image = new Image();
      image.onload = () => {
        try {
          const scale = Math.min(1, 160 / Math.max(image.width, image.height));
          const canvas = document.createElement("canvas");
          canvas.width = Math.max(1, Math.round(image.width * scale));
          canvas.height = Math.max(1, Math.round(image.height * scale));
          canvas.getContext("2d")?.drawImage(
            image,
            0,
            0,
            canvas.width,
            canvas.height,
          );
          const preview = canvas.toDataURL("image/webp", 0.7);
          resolve(preview.length <= 120_000 ? preview : "");
        } catch (_) {
          resolve("");
        }
      };
      image.onerror = () => resolve("");
      try {
        image.src = attachmentObjectUrl(attachment);
      } catch (_) {
        resolve("");
      }
    });
    worldAttachmentPreviewCache.set(attachment, pending);
    return pending;
  }

  // Authored by the signed-in account, but not necessarily by this browser —
  // another tab, a phone, or the desktop client counts too. Kept apart from
  // `self` (a strict senderId match) because only `self` may raise a bubble
  // over the local avatar; a display name alone is not proof of identity.
  // The World uses this to keep your own lines out of its unread count.
  function isOwnChatLine(sender, senderId) {
    if (senderId === selfId) return true;
    const account = String(userSession()?.nodeName || "").trim().toLowerCase();
    if (!account) return false;
    return String(sender || "").trim().toLowerCase() === account;
  }

  function emitWorldChatBubble(
    sender,
    senderId,
    text,
    history = false,
    meta = {},
  ) {
    if (!WORLD_EMBED_BUBBLES) return;
    if (orgAgentIdentity(sender, senderId) && !orgAgentEngineeringAccess) return;
    const line = String(text || "").trim().slice(0, 200);
    const attachmentName = safeAttachmentName(meta?.attachment?.fileName || "");
    if (!line && !attachmentName) return;
    const payload = {
          type: "forkmesh:world-chat",
          id: String(meta?.id || "").slice(0, 96),
          sender: String(sender || "").slice(0, MAX_NAME),
          self: senderId === selfId,
          own: isOwnChatLine(sender, senderId),
          text: line,
          ts: Number(meta?.ts) || Date.now(),
          attachmentName: attachmentName === "file" ? "" : attachmentName,
          attachmentMime: safeAttachmentMime(
            meta?.attachment?.fileMime || "",
          ).slice(0, MAX_ATTACHMENT_MIME),
          reactionCount: Math.max(
            0,
            Math.min(999, Number(meta?.reactionCount) || 0),
          ),
          history,
        };
    const post = (attachmentPreview = "") => {
      try {
        if (window.parent !== window) {
          window.parent.postMessage(
            { ...payload, attachmentPreview },
            location.origin,
          );
        } else {
          window.dispatchEvent(
            new CustomEvent("forkmesh:world-chat-native", {
              detail: { ...payload, attachmentPreview },
            }),
          );
        }
      } catch (_) {}
    };
    post();
    if (meta?.attachment && !history) {
      void worldAttachmentPreview(meta.attachment).then((preview) => {
        if (preview) post(preview);
      });
    }
  }

  // Replayed entries can arrive out of order. Keep their newest surviving
  // record for the physical World board; mutations are applied before this is
  // emitted, so an edited/deleted latest line never leaks as stale activity.
  let newestHistoryTs = 0;
  function emitWorldChatHistory(entry) {
    if (!WORLD_EMBED_BUBBLES) return;
    const ts = Number(entry.ts) || 0;
    newestHistoryTs = Math.max(newestHistoryTs, ts);
    emitWorldChatBubble(
      entry.sender,
      entry.senderId,
      entry.text,
      true,
      entry,
    );
  }

  function emitNewestWorldChatHistory() {
    const record = currentHistoryRows().reduce((newest, candidate) => {
      if (!newest || Number(candidate.tsMs) >= Number(newest.tsMs)) {
        return candidate;
      }
      return newest;
    }, null);
    if (!record) return;
    const reactionCount = [...(reactions.get(record.id)?.values() || [])]
      .reduce((count, reactors) => count + reactors.size, 0);
    emitWorldChatHistory({
      id: record.id,
      sender: record.who,
      senderId: record.senderId,
      text: record.text,
      ts: record.tsMs,
      attachment: record.attachment,
      reactionCount,
    });
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
      accountKind: entry.accountKind === "user" ? "user" : "guest",
      sender: publicWorldName(entry.sender),
    };
  }

  function renderChatEntry(entry, kind, live = false) {
    if (!entry || !allowedChatAccountKind(entry.accountKind)) return;
    entry = normalizedPublicWorldFrame(entry);
    if (
      orgAgentIdentity(entry.sender, entry.senderId) &&
      !orgAgentEngineeringAccess
    ) {
      return;
    }
    if (!once(entry.id)) return;
    const who = String(entry.sender || "peer").slice(0, MAX_NAME);
    const text = entry.text || "";
    const attachment = attachmentFromEntry(entry);
    if (!text && !attachment) return;
    kind = entry.senderId === selfId ? "self" : kind;
    // Anyone who has spoken here — including in replayed history — becomes a
    // composer mention candidate.
    if (entry.senderId !== selfId) {
      rememberMentionPerson(who, live ? Date.now() : Number(entry.ts) || 0);
    }
    appendMessage(
      kind,
      who,
      text,
      entry.id,
      entry.senderId,
      Number(entry.ts) || Date.now(),
      attachment,
      !live,
    );
    if (live) {
      newestHistoryTs = Math.max(newestHistoryTs, Number(entry.ts) || 0);
      emitWorldChatBubble(who, entry.senderId, text, false, {
        id: entry.id,
        ts: entry.ts,
        attachment,
        reactionCount: entry.reactionCount,
      });
    }
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

  function finishHistoryReplay() {
    renderHistoryWindow();
    renderSideMessages();
    emitNewestWorldChatHistory();
  }

  function handlePlain(plain, historyReplay = false) {
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
    // "hello"/"presence" frames are the only sign of someone who is here but
    // has not typed yet; keep them in the mention list too.
    if (plain.senderId !== selfId) {
      rememberMentionPerson(
        sender,
        historyReplay ? Number(plain.ts) || 0 : Date.now(),
      );
    }
    if (type === "chat") {
      if (plain.channel === CHANNEL) {
        renderChatEntry(plain, "peer", !historyReplay);
      }
    } else if (type === "history") {
      const entries = Array.isArray(plain.entries) ? plain.entries : [];
      for (const entry of entries) {
        if (
          entry &&
          (!entry.type || entry.type === "chat") &&
          entry.channel === CHANNEL &&
          (entry.channel || entry.text || entry.fileName)
        ) {
          renderChatEntry(entry, "peer");
        }
      }
      for (const entry of entries) {
        if (entry?.type && entry.type !== "chat") {
          handlePlain(entry, true);
        }
      }
      if (!historyReplay) finishHistoryReplay();
    } else if (type === "reaction") {
      if (once(plain.id)) applyReaction(plain);
    } else if (type === "edit") {
      const rec = rows.get(plain.target);
      if (rec && rec.senderId === plain.senderId) {
        rec.text = plain.text || "";
        rec.editedAt = plain.editedAt || plain.ts;
        if (rec.textEl) {
          renderMessageText(rec.textEl, rec.text);
          markEdited(rec, rec.editedAt);
        }
        const sideEntry = sideEntries.find((entry) => entry.id === plain.target);
        if (sideEntry) {
          sideEntry.text = plain.text || "";
          if (!historyReplay) renderSideMessages();
        }
      }
    } else if (type === "delete") {
      const rec = rows.get(plain.target);
      if (rec && rec.senderId === plain.senderId) {
        removeMessage(plain.target, { deferRender: historyReplay });
      }
    } else if (type === "admin-delete") {
      verifyAdminDelete(plain).then((ok) => {
        if (!ok) return;
        seen.add(plain.target);
        removeMessage(plain.target);
      });
    } else if (type === "hello" && !plain.to) {
      // Presence is shown in the World itself; do not add join noise to the
      // message timeline or push the composer upward.
    } else if (type === "bye") {
      appendSystem(sender + " left");
    }
  }

  async function flushHistoryReplay() {
    if (historyReplayTimer) {
      clearTimeout(historyReplayTimer);
      historyReplayTimer = 0;
    }
    const envelopes = historyReplayEnvelopes;
    historyReplayEnvelopes = [];
    if (!envelopes.length) {
      finishHistoryReplay();
      return;
    }
    const frames = await Promise.all(envelopes.map(decryptObject));
    const decoded = frames.filter(Boolean);
    // D1 ties frames stored in the same millisecond by opaque cipher hash, so
    // an edit/delete can replay before its original chat. Register every chat
    // record first, then apply mutations in the received order.
    for (const plain of decoded) {
      if (plain.type === "chat") handlePlain(plain, true);
    }
    for (const plain of decoded) {
      if (plain.type !== "chat") handlePlain(plain, true);
    }
    finishHistoryReplay();
  }

  function scheduleHistoryReplayFallback() {
    if (historyReplayTimer) clearTimeout(historyReplayTimer);
    historyReplayTimer = window.setTimeout(() => {
      historyReplayTimer = 0;
      inboundFrameQueue = inboundFrameQueue
        .then(() => flushHistoryReplay())
        .catch(() => {});
    }, 180);
  }

  async function processFrameData(data) {
    if (typeof data !== "string") return;
    let envelope;
    try {
      envelope = JSON.parse(data);
    } catch (_) {
      return;
    }
    if (envelope?.kind === "forkmesh-history-end") {
      await flushHistoryReplay();
      return;
    }
    if (envelope?.historyReplay === true) {
      historyReplayEnvelopes.push(envelope);
      scheduleHistoryReplayFallback();
      return;
    }
    const plain = await decryptObject(envelope);
    if (!plain) return;
    handlePlain(plain);
  }

  async function onFrame(event) {
    const data = event.data;
    const queued = inboundFrameQueue.then(() => processFrameData(data));
    inboundFrameQueue = queued.catch(() => {});
    await queued;
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
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(envelope));
        // You have already read what you just typed, so it must not light the
        // site-header chat badge on every other page. That badge is a delta
        // against a stored baseline, and only retained public-world frames
        // reach the counter behind it (oversized file frames are dropped
        // before retention).
        if (envelope.persist && PUBLIC_WORLD_GENERAL && !plain.file) {
          noteOwnChatActivity();
        }
      }
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

  function makeBotPlain(text, sender = FORKBOT_SENDER_ID) {
    const botName = [FORKBOT_SENDER_ID, CLAUDE_SENDER_ID, CODEX_SENDER_ID]
      .includes(sender) ? sender : FORKBOT_SENDER_ID;
    return makePlain("chat", {
      channel: CHANNEL,
      text: String(text || "").slice(0, MAX_TEXT),
      sender: botName,
      senderId: botName,
      accountKind: "user",
    });
  }

  function broadcastBotMessage(text, sender = FORKBOT_SENDER_ID) {
    const plain = makeBotPlain(text, sender);
    // Claude/Codex prompts and replies are intentionally absent from the
    // shared room. The Engineering-only session endpoint remains the durable
    // transcript; this local line is merely immediate feedback to its author.
    if (!orgAgentIdentity(plain.sender, plain.senderId)) send(plain);
    seen.add(plain.id);
    appendMessage("peer", plain.sender, plain.text, plain.id, plain.senderId, plain.ts);
    // The asking client appends directly (not via renderChatEntry), so mirror
    // the reply to the World embed here too.
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
      runWhenConnected(() => broadcastBotMessage(data.botMessage));
    } catch (_) {
      appendSystem("forkbot is unavailable");
    }
  }

  function orgAgentScope(override = null) {
    const selectedOwner = String(override?.owner || "");
    const selectedRepo = String(override?.repo || override?.name || "");
    if (
      /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(selectedOwner) &&
      /^[A-Za-z0-9._-]{1,100}$/.test(selectedRepo)
    ) {
      return {
        organization: selectedOwner,
        owner: selectedOwner,
        repo: selectedRepo,
      };
    }
    if (scopedWorkshop) {
      return { organization: ROOM_OWNER, owner: ROOM_OWNER, repo: ROOM_REPO };
    }
    const organization = requestedOrganization || "forkmesh";
    return { organization, owner: organization, repo: "forkmesh" };
  }

  async function loadOrgAgentChatAccess() {
    orgAgentEngineeringAccess = false;
    orgAgentAccessLoaded = false;
    const session = userSession();
    if (!session) {
      orgAgentAccessLoaded = true;
      return false;
    }
    const scope = orgAgentScope();
    const endpoint =
      `/api/orgs/${encodeURIComponent(scope.organization)}` +
      `/repos/${encodeURIComponent(scope.repo)}/agent-bots`;
    const token = String(session.sessionToken || "");
    const headers = { accept: "application/json" };
    if (token && token !== "cookie") headers.authorization = `Bearer ${token}`;
    try {
      const response = await fetch(endpoint, {
        cache: "no-store",
        credentials: "same-origin",
        headers,
      });
      const data = await response.json().catch(() => ({}));
      orgAgentEngineeringAccess =
        response.ok && data?.engineeringAccess === true;
    } catch (_) {
      orgAgentEngineeringAccess = false;
    }
    orgAgentAccessLoaded = true;
    return orgAgentEngineeringAccess;
  }

  async function maybeAskOrgAgent(text, selectedScope = null) {
    const provider = CLAUDE_MENTION_RE.test(text || "")
      ? "claude-code"
      : CODEX_MENTION_RE.test(text || "")
        ? "codex"
        : "";
    if (!provider) return false;
    const botName = provider === "codex" ? CODEX_SENDER_ID : CLAUDE_SENDER_ID;
    if (!userSession() || !orgAgentAccessLoaded || !orgAgentEngineeringAccess) {
      appendSystem(`Only Engineering team members can use @${botName}.`);
      return false;
    }
    const prompt = String(text || "")
      .replace(provider === "codex" ? CODEX_MENTION_RE : CLAUDE_MENTION_RE, " ")
      .trim();
    if (!prompt) {
      appendSystem(`Add a task after @${botName}.`);
      return false;
    }
    return queueOrgAgent(provider, prompt, botName, selectedScope);
  }

  // One queue path for every bot request: the mention shortcuts pass a named
  // provider, the task board passes "agent" and lets the Worker pick whichever
  // runtime has an eligible mirror online.
  async function queueOrgAgent(
    provider,
    prompt,
    botName = ORG_BOT_SENDER_ID,
    selectedScope = null,
  ) {
    const session = userSession();
    if (!session || !orgAgentAccessLoaded || !orgAgentEngineeringAccess) {
      appendSystem("Only Engineering team members can start an org bot.");
      return false;
    }
    if (!prompt) return false;
    const scope = orgAgentScope(selectedScope);
    const taskKeyMatch = prompt.match(
      /\[(task:[a-z0-9-]{1,48}|issue:[a-z0-9-]{1,40}\/[a-z0-9._-]{1,60}#[1-9][0-9]{0,8})\]/i,
    );
    const endpoint =
      `/api/orgs/${encodeURIComponent(scope.organization)}` +
      `/repos/${encodeURIComponent(scope.repo)}/agent-bots`;
    const token = String(session.sessionToken || "");
    const headers = {
      "content-type": "application/json",
      accept: "application/json",
    };
    if (token && token !== "cookie") headers.authorization = `Bearer ${token}`;
    try {
      const response = await fetch(endpoint, {
        method: "POST",
        cache: "no-store",
        credentials: "same-origin",
        headers,
        body: JSON.stringify({
          provider,
          prompt: prompt.slice(0, 8000),
          ...(taskKeyMatch ? { taskKey: taskKeyMatch[1].toLowerCase() } : {}),
          ...(token && token !== "cookie" ? { sessionToken: token } : {}),
        }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) {
        throw new Error(String(data.error || `HTTP ${response.status}`));
      }
      const target = String(data.session?.targetNode || "an eligible mirror");
      broadcastBotMessage(
        `Queued on ${target}. Claude Haiku is checking the prompt before I start.`,
        botName,
      );
      return data;
    } catch (error) {
      const reason = error.message === "no_eligible_headless_mirror"
        ? "No eligible headless mirror is online."
        : [
            "engineering_team_required",
            "org_member_required",
            "forbidden",
          ].includes(error.message)
          ? "Only Engineering team members can start this agent."
          : "I could not queue that task.";
      broadcastBotMessage(reason, botName);
      return false;
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

  function renderDashboardDraft(control) {
    if (!control?.queue) return;
    control.queue.textContent = "";
    for (const record of control.draft) {
      const name = safeAttachmentName(record.file.name);
      const row = document.createElement("div");
      row.className = "flex min-w-0 max-w-full items-center gap-2 rounded-md border border-border bg-secondary px-2 py-1.5";
      if (record.previewUrl) {
        const preview = document.createElement("img");
        preview.src = record.previewUrl;
        preview.alt = name;
        preview.className = "h-8 w-8 shrink-0 rounded object-cover";
        row.append(preview);
      }
      const copy = document.createElement("div");
      copy.className = "min-w-0 flex-1";
      const title = document.createElement("div");
      title.className = "truncate text-xs font-semibold text-foreground";
      title.textContent = name;
      const meta = document.createElement("div");
      meta.className = "truncate text-[10px] text-muted-foreground";
      meta.textContent = `${safeAttachmentMime(record.file.type)} - ${formatAttachmentSize(record.file.size)}`;
      copy.append(title, meta);
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "shrink-0 rounded p-1 text-muted-foreground hover:bg-background hover:text-foreground";
      remove.textContent = "×";
      remove.setAttribute("aria-label", `Remove ${name}`);
      remove.addEventListener("click", () => {
        const index = control.draft.indexOf(record);
        if (index < 0) return;
        control.draft.splice(index, 1);
        if (record.previewUrl) URL.revokeObjectURL(record.previewUrl);
        renderDashboardDraft(control);
      });
      row.append(copy, remove);
      control.queue.append(row);
    }
  }

  function canvasBlob(canvas, type, quality) {
    return new Promise((resolve) => canvas.toBlob(resolve, type, quality));
  }

  async function fitDashboardImage(file) {
    if (
      file.size <= MAX_ATTACHMENT_BYTES ||
      !String(file.type || "").startsWith("image/")
    ) {
      return file;
    }
    if (file.size > 24 * 1024 * 1024) return null;
    let bitmap;
    let objectUrl = "";
    try {
      if (typeof createImageBitmap === "function") {
        bitmap = await createImageBitmap(file);
      } else {
        objectUrl = URL.createObjectURL(file);
        bitmap = await new Promise((resolve, reject) => {
          const image = new Image();
          image.onload = () => resolve(image);
          image.onerror = () => reject(new Error("image_decode_failed"));
          image.src = objectUrl;
        });
      }
      const sourceWidth = Math.max(
        1,
        Number(bitmap.width || bitmap.naturalWidth) || 1,
      );
      const sourceHeight = Math.max(
        1,
        Number(bitmap.height || bitmap.naturalHeight) || 1,
      );
      for (const edge of [1600, 1280, 1024, 800, 640, 480]) {
        const scale = Math.min(1, edge / Math.max(sourceWidth, sourceHeight));
        const canvas = document.createElement("canvas");
        canvas.width = Math.max(1, Math.round(sourceWidth * scale));
        canvas.height = Math.max(1, Math.round(sourceHeight * scale));
        const context = canvas.getContext("2d", { alpha: false });
        if (!context) return null;
        context.fillStyle = "#ffffff";
        context.fillRect(0, 0, canvas.width, canvas.height);
        context.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
        for (const quality of [0.86, 0.76, 0.66, 0.54]) {
          const blob = await canvasBlob(canvas, "image/webp", quality);
          if (blob && blob.size <= MAX_ATTACHMENT_BYTES) {
            const stem = safeAttachmentName(file.name)
              .replace(/\.[^.]+$/, "")
              .slice(0, 170) || "image";
            return new File([blob], `${stem}.webp`, {
              type: "image/webp",
              lastModified: Date.now(),
            });
          }
        }
      }
    } catch (_) {
      return null;
    } finally {
      bitmap?.close?.();
      if (objectUrl) URL.revokeObjectURL(objectUrl);
    }
    return null;
  }

  async function stageDashboardAttachments(control, files) {
    if (!control) return;
    const candidates = Array.from(files || []).filter((file) => file instanceof File);
    const remaining = Math.max(0, 4 - control.draft.length);
    if (candidates.length > remaining) showAttachmentFeedback(control, "Share up to 4 files in a message.");
    for (const source of candidates.slice(0, remaining)) {
      const file = await fitDashboardImage(source);
      if (!file || file.size > MAX_ATTACHMENT_BYTES) {
        showAttachmentFeedback(
          control,
          String(source.type || "").startsWith("image/")
            ? "That image could not be resized to fit the 1 MiB limit."
            : "Attachments must be 1 MiB or smaller.",
        );
        continue;
      }
      if (!file.size) {
        showAttachmentFeedback(control, "That file is empty.");
        continue;
      }
      control.draft.push({
        file,
        previewUrl: String(file.type || "").startsWith("image/")
          ? URL.createObjectURL(file)
          : "",
      });
    }
    renderDashboardDraft(control);
  }

  async function sendAttachment(file, control = null) {
    if (!file || !canJoinChat()) {
      if (!canJoinChat()) showUserOnlyState();
      return false;
    }
    if (file.size > MAX_ATTACHMENT_BYTES) {
      showAttachmentFeedback(control, "Attachments must be 1 MiB or smaller.");
      return false;
    }
    if (!file.size) {
      showAttachmentFeedback(control, "That file is empty.");
      return false;
    }
    let buffer;
    try {
      buffer = await file.arrayBuffer();
    } catch (_) {
      showAttachmentFeedback(control, "Could not read that attachment.");
      return false;
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
    return true;
  }

  async function sendDashboardDraft(control) {
    const records = [...(control?.draft || [])];
    if (!records.length) return false;
    let sent = false;
    for (const record of records) sent = (await sendAttachment(record.file, control)) || sent;
    if (sent) {
      for (const record of control.draft) if (record.previewUrl) URL.revokeObjectURL(record.previewUrl);
      control.draft = [];
      renderDashboardDraft(control);
    }
    return sent;
  }

  function mountAttachmentControl(inputEl) {
    if (!inputEl?.parentElement) return null;
    const bar = inputEl.parentElement;
    const fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.multiple = true;
    fileInput.hidden = true;
    fileInput.id = `${inputEl.id}AttachmentInput`;
    fileInput.setAttribute("aria-label", "Choose image, video, or document");
    const button = document.createElement("button");
    button.type = "button";
    button.className = "shrink-0 inline-flex h-7 w-7 items-center justify-center rounded-md border border-border bg-background text-muted-foreground hover:text-foreground disabled:cursor-not-allowed disabled:opacity-50";
    button.setAttribute("aria-label", "Attach image, video, or document");
    button.title = "Attach image, video, or document (up to 1 MiB)";
    button.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21.44 11.05 12.25 20.24a6 6 0 0 1-8.49-8.49l9.19-9.19a4 4 0 0 1 5.66 5.66l-9.2 9.19a2 2 0 0 1-2.83-2.83l8.49-8.48"></path></svg>';
    const feedback = document.createElement("span");
    feedback.className = "sr-only";
    feedback.setAttribute("role", "status");
    feedback.setAttribute("aria-live", "polite");
    const queue = document.createElement("div");
    queue.className = "mt-2 flex flex-wrap gap-2";
    queue.dataset.dashboardChatAttachments = "";
    queue.setAttribute("aria-live", "polite");
    queue.setAttribute("aria-label", "Staged attachments");
    const control = {
      button,
      input: fileInput,
      inputEl,
      feedback,
      queue,
      draft: [],
    };
    bar.parentElement?.insertBefore(queue, bar);
    const sendButton = inputEl === fullInput ? fullSend : sideSend;
    bar.insertBefore(fileInput, sendButton || null);
    bar.insertBefore(button, sendButton || null);
    bar.append(feedback);
    button.addEventListener("click", () => fileInput.click());
    fileInput.addEventListener("change", () => {
      const files = Array.from(fileInput.files || []);
      fileInput.value = "";
      if (files.length) {
        stageDashboardAttachments(control, files);
        if (inputEl !== fullInput || String(fullAction?.value || "chat") === "chat") {
          void sendDashboardDraft(control);
        }
      }
    });
    control.button.disabled = !canJoinChat();
    control.input.disabled = !canJoinChat();
    attachmentControls.push(control);
    applyWorldComposerPrefill();
    return control;
  }

  function selectedComposerRepository() {
    const value = String(fullRepository?.value || "");
    const match = value.match(
      /^([a-z](?:[a-z0-9-]{0,61}[a-z0-9])?)\/([A-Za-z0-9._-]{1,100})$/,
    );
    if (!match) return null;
    const selected = fullRepository?.selectedOptions?.[0];
    const routeOwner = String(selected?.dataset?.routeOwner || match[1]);
    const routeName = String(selected?.dataset?.routeName || match[2]);
    if (
      !/^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(routeOwner) ||
      !/^[A-Za-z0-9._-]{1,100}$/.test(routeName)
    ) {
      return null;
    }
    return {
      owner: routeOwner,
      name: routeName,
      repo: routeName,
      logicalOwner: match[1],
      logicalName: match[2],
      logicalKind: String(selected?.dataset?.logicalKind || "user"),
    };
  }

  function setComposerStatus(message = "", tone = "muted") {
    if (!fullComposerStatus) return;
    fullComposerStatus.textContent = message;
    fullComposerStatus.className =
      tone === "bad"
        ? "text-destructive"
        : tone === "good"
          ? "text-primary"
          : "text-muted-foreground";
  }

  function setFullComposerBusy(busy) {
    for (const control of [
      fullInput,
      fullSend,
      fullTaskSend,
      fullChannel,
      fullRepository,
      fullAction,
      taskDepartment,
      taskTeam,
      taskDestination,
      taskAssignee,
    ]) {
      if (control) control.disabled = Boolean(busy);
    }
  }

  function syncFullComposerAction() {
    if (!fullAction || !fullInput) return;
    const action = fullAction.value;
    if (simpleWorldComposer) {
      if (fullSendLabel) fullSendLabel.textContent = "Chat";
      if (fullComposerHint) {
        fullComposerHint.textContent =
          "Chat posts to #general · Task sends private work to the bot";
      }
      fullInput.placeholder = "Message #general…";
      if (taskRouting) taskRouting.hidden = true;
      syncChatContextBubbles();
      return;
    }
    const presentation = {
      chat: {
        label: "Send",
        hint: "Enter sends · Shift+Enter adds a line",
        placeholder: `Message ${CHANNEL}…`,
      },
      issue: {
        label: "Create issue",
        hint: "First line becomes the title · remaining lines become the description",
        placeholder: "Issue title\\nDescribe the expected behavior and context…",
      },
      task: {
        label: "Create task",
        hint: "First line is the title · route by department, team, destination, and assignee",
        placeholder: "Task title\\nAdd details or QA instructions…",
      },
      agent: {
        label: "Send to bot",
        hint: "Starts a secured Engineering task on an eligible mirror",
        placeholder: "Describe what you want the bot to do…",
      },
    }[action] || {};
    if (fullSendLabel) {
      fullSendLabel.textContent = presentation.label || "Send";
    }
    if (fullComposerHint) fullComposerHint.textContent = presentation.hint || "";
    fullInput.placeholder = presentation.placeholder || "Write a message…";
    if (taskRouting) taskRouting.hidden = action !== "task";
    const repositoryRelevant =
      action !== "chat" &&
      (
        action !== "task" ||
        String(taskDestination?.value || "") === "repository" ||
        AGENT_ASSIGNEE_VALUES.includes(String(taskAssignee?.value || ""))
      );
    fullRepository?.classList.toggle("ring-1", repositoryRelevant);
    fullRepository?.classList.toggle("ring-primary/50", repositoryRelevant);
    syncChatContextBubbles();
    setComposerStatus("");
  }

  function selectedOptionLabel(select, fallback) {
    return String(
      select?.selectedOptions?.[0]?.textContent || fallback,
    ).trim();
  }

  function syncChatContextBubbles() {
    if (contextChannel) {
      contextChannel.textContent = selectedOptionLabel(
        fullChannel,
        CHANNEL_LABEL || "# general",
      );
    }
    if (contextSource) {
      contextSource.textContent = selectedOptionLabel(
        fullRepository,
        "No repository",
      );
    }
    if (contextAction) {
      contextAction.textContent = selectedOptionLabel(fullAction, "Send to chat");
    }
  }

  function syncChatScrollThumb() {
    if (!fullLog || !chatScrollRail) return;
    const available = Math.max(0, fullLog.scrollHeight - fullLog.clientHeight);
    const progress = available > 0
      ? Math.max(0, Math.min(1, fullLog.scrollTop / available))
      : 1;
    chatScrollRail.style.setProperty("--chat-scroll-progress", progress);
    chatScrollRail.toggleAttribute("data-at-start", fullLog.scrollTop <= 1);
    chatScrollRail.toggleAttribute(
      "data-at-end",
      available <= 1 || fullLog.scrollTop >= available - 1,
    );
  }

  async function taskApiRequest(method, path, body = null) {
    const token = String(userSession()?.sessionToken || "");
    const headers = { accept: "application/json" };
    if (body !== null) headers["content-type"] = "application/json";
    if (token && token !== "cookie") headers.authorization = `Bearer ${token}`;
    const response = await fetch(path, {
      method,
      cache: "no-store",
      credentials: "same-origin",
      headers,
      ...(body === null
        ? {}
        : {
            body: JSON.stringify({
              ...body,
              ...(token && token !== "cookie"
                ? { sessionToken: token }
                : {}),
            }),
          }),
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok || payload?.ok === false) {
      throw new Error(String(payload?.error || `HTTP ${response.status}`));
    }
    return payload;
  }

  async function loadTaskRouting() {
    if (!taskAssignee || !taskTeam) return;
    try {
      const payload = await taskApiRequest("GET", "/api/tasks");
      const selfName = String(payload?.actor || displayName()).toLowerCase();
      for (const value of Array.isArray(payload?.members)
        ? payload.members
        : []) {
        const name = String(value || "").trim().toLowerCase();
        if (!/^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(name)) continue;
        const option = document.createElement("option");
        option.value = `user:${name}`;
        option.textContent = name === selfName ? `${name} (you)` : name;
        taskAssignee.append(option);
      }
      if (
        !simpleWorldComposer &&
        selfName &&
        Array.from(taskAssignee.options).some(
          (option) => option.value === `user:${selfName}`,
        )
      ) {
        taskAssignee.value = `user:${selfName}`;
      }
      for (const value of Array.isArray(payload?.teams)
        ? payload.teams
        : []) {
        const team = String(value || "").trim().toLowerCase();
        if (!/^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$/.test(team)) continue;
        const option = document.createElement("option");
        option.value = team;
        option.textContent = team;
        taskTeam.append(option);
      }
    } catch (_) {
      setComposerStatus(
        "Task routing is available only to organization members.",
        "bad",
      );
    }
  }

  async function loadComposerRepositories() {
    if (!fullRepository) return;
    try {
      const response = await fetch("/api/repositories", {
        cache: "no-store",
        credentials: "same-origin",
        headers: { accept: "application/json" },
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error("repository_catalog_unavailable");
      const seenRepositories = new Set();
      const repositories = (Array.isArray(payload?.repositories)
        ? payload.repositories
        : [])
        .flatMap((repository) => {
          const routeOwner = String(repository?.owner || "");
          const name = String(repository?.name || repository?.repo || "");
          const logicalOwners = Array.isArray(repository?.logicalOwners)
            ? repository.logicalOwners
            : (
                ["user", "organization"].includes(
                  String(repository?.ownerKind || ""),
                ) && repository?.logicalOwner
                  ? [{
                      kind: repository.ownerKind,
                      owner: repository.logicalOwner,
                    }]
                  : []
              );
          return logicalOwners.map((identity) => {
            const kind = String(identity?.kind || "").toLowerCase();
            const logicalOwner = String(identity?.owner || "").toLowerCase();
            const value = `${logicalOwner}/${name}`;
            const key = `${kind}:${value}`;
            if (
              !["user", "organization"].includes(kind) ||
              !/^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?\/[A-Za-z0-9._-]{1,100}$/.test(
                value,
              ) ||
              !/^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(routeOwner) ||
              seenRepositories.has(key)
            ) {
              return null;
            }
            seenRepositories.add(key);
            return {
              value,
              label: `${value} · ${
                kind === "organization" ? "Organization" : "User"
              }`,
              kind,
              routeOwner:
                kind === "organization" ? logicalOwner : routeOwner,
              routeName: name,
            };
          });
        })
        .filter(Boolean)
        .sort((left, right) => left.label.localeCompare(right.label));
      const requestedDefault = String(
        document.querySelector("[data-world-native-chat]")
          ?.dataset.worldDefaultRepository || "forkmesh/forkmesh",
      );
      const previous =
        sessionStorage.getItem("forkmesh.worldChat.repository") ||
        requestedDefault;
      for (const repository of repositories.slice(0, 250)) {
        const option = document.createElement("option");
        option.value = repository.value;
        option.textContent = repository.label;
        option.dataset.logicalKind = repository.kind;
        option.dataset.routeOwner = repository.routeOwner;
        option.dataset.routeName = repository.routeName;
        fullRepository.append(option);
      }
      if (repositories.some((repository) => repository.value === previous)) {
        fullRepository.value = previous;
      }
      syncChatContextBubbles();
    } catch (_) {
      setComposerStatus("Repository catalog unavailable", "bad");
    }
  }

  async function runFullComposerAction(inputEl, attachmentControl = null) {
    const action = String(fullAction?.value || "chat");
    const text = String(inputEl?.value || "").trim();
    if (!text) {
      setComposerStatus("Write something first.", "bad");
      inputEl?.focus();
      return;
    }
    const repository = selectedComposerRepository();
    const taskAssigneeValue = String(taskAssignee?.value || "unassigned");
    const taskNeedsRepository =
      action === "task" &&
      (
        String(taskDestination?.value || "") === "repository" ||
        AGENT_ASSIGNEE_VALUES.includes(taskAssigneeValue)
      );
    if (!repository && (action !== "task" || taskNeedsRepository)) {
      setComposerStatus("Choose a repository for this action.", "bad");
      fullRepository?.focus();
      return;
    }
    if (action === "task" && !String(taskTeam?.value || "")) {
      setComposerStatus("Choose the team responsible for this task.", "bad");
      taskTeam?.focus();
      return;
    }
    setFullComposerBusy(true);
    setComposerStatus(
      action === "issue"
        ? "Signing issue…"
        : action === "task"
          ? "Saving private task…"
          : "Assigning agent…",
    );
    try {
      if (action === "task") {
        const lines = text.split(/\r?\n/);
        const title = String(lines.shift() || "").trim().slice(0, 160);
        const details = lines.join("\n").trim().slice(0, 4000);
        if (!title) throw new Error("Task title is required.");
        const botTask = AGENT_ASSIGNEE_VALUES.includes(taskAssigneeValue);
        const userAssignee = taskAssigneeValue.startsWith("user:")
          ? taskAssigneeValue.slice(5)
          : "";
        const destination = botTask
          ? "agent"
          : String(taskDestination?.value || "department");
        const taskAttachments = await Promise.all(
          (attachmentControl?.draft || []).map(({ file }) =>
            taskAttachmentMetadata(file),
          ),
        );
        const created = await taskApiRequest("POST", "/api/tasks", {
          title,
          details,
          department: String(taskDepartment?.value || "general"),
          team: String(taskTeam?.value || ""),
          destination,
          assigneeKind: botTask
            ? "agent"
            : userAssignee
              ? "user"
              : "unassigned",
          assignee: userAssignee,
          repository: repository
            ? `${repository.logicalOwner}/${repository.logicalName}`
            : "",
          attachments: taskAttachments,
          ...(destination === "qa"
            ? {
                howToTest:
                  details ||
                  "Follow the normal user flow and confirm the requested behavior.",
              }
            : {}),
        });
        const task = created?.task || {};
        if (botTask) {
          // The task is already on the board; queueing it on a node is the
          // follow-up, and a failure there leaves the task list authoritative.
          const queued = await queueOrgAgent(
            "agent",
            `[task:${task.id}] ${title}\n\n${details}`.trim(),
            ORG_BOT_SENDER_ID,
            repository,
          );
          if (!queued) {
            throw new Error(
              "The task was saved to the task list, but no bot node accepted it yet.",
            );
          }
          const sessionId = String(queued?.session?.id || "");
          if (sessionId) {
            await taskApiRequest("PATCH", `/api/tasks/${task.id}`, {
              agentSessionId: sessionId,
            });
          }
        }
        appendSystem(
          `Private task “${title}” was sent to ${
            destination === "qa"
              ? "the QA board"
              : destination === "agent"
                ? "the bot queue"
                : destination === "repository"
                  ? `${repository.owner}/${repository.name}`
                  : `${task.department || "general"}`
          }.`,
        );
        if (taskAttachments.length) {
          await sendDashboardDraft(attachmentControl);
        }
        setComposerStatus("Organization task created.", "good");
      } else if (action === "issue") {
        const lines = text.split(/\r?\n/);
        const title = String(lines.shift() || "").trim().slice(0, 200);
        const body = lines.join("\n").trim();
        if (!title) throw new Error("Issue title is required.");
        const submitIssue =
          window.ForkMeshDashboardActions?.submitWebIssue;
        if (typeof submitIssue !== "function") {
          throw new Error("Issue authoring is still loading.");
        }
        await submitIssue(repository, title, body);
        appendSystem(`Issue “${title}” was signed and sent to ${repository.owner}/${repository.name}.`);
        setComposerStatus("Issue sent to the maintainer inbox.", "good");
      } else {
        const lines = text.split(/\r?\n/);
        const title = String(lines.shift() || "").trim().slice(0, 160);
        const details = lines.join("\n").trim().slice(0, 4000);
        const taskAttachments = await Promise.all(
          (attachmentControl?.draft || []).map(({ file }) =>
            taskAttachmentMetadata(file),
          ),
        );
        const created = await taskApiRequest("POST", "/api/tasks", {
          title,
          details,
          department: "engineering",
          team: "",
          destination: "agent",
          assigneeKind: "agent",
          assignee: "",
          repository: `${repository.logicalOwner}/${repository.logicalName}`,
          attachments: taskAttachments,
        });
        const task = created?.task || {};
        const mention = "@bot";
        appendMessage(
          "self",
          displayName(),
          `${mention} ${text}`,
          "",
          "",
          Date.now(),
        );
        const queued = await queueOrgAgent(
          "agent",
          `[task:${task.id}] ${text}`,
          ORG_BOT_SENDER_ID,
          repository,
        );
        if (!queued) {
          throw new Error(
            "The task is in the task list, but no bot node accepted it yet.",
          );
        }
        const sessionId = String(queued?.session?.id || "");
        if (sessionId) {
          await taskApiRequest("PATCH", `/api/tasks/${task.id}`, {
            agentSessionId: sessionId,
          });
        }
        if (attachmentControl?.draft.length) {
          await sendDashboardDraft(attachmentControl);
        }
        appendSystem(`Private task “${title}” was added to the bot queue.`);
        setComposerStatus("Bot task created.", "good");
      }
      inputEl.value = "";
      inputEl.style.height = "";
    } catch (error) {
      setComposerStatus(
        String(error?.message || "The action could not be completed."),
        "bad",
      );
    } finally {
      setFullComposerBusy(false);
      inputEl?.focus();
    }
  }

  function sendFrom(inputEl, attachmentControl) {
    if (
      inputEl === fullInput &&
      fullAction &&
      fullAction.value !== "chat"
    ) {
      void runFullComposerAction(inputEl, attachmentControl);
      return;
    }
    if (!canJoinChat()) {
      showUserOnlyState();
      return;
    }
    const text = (inputEl?.value || "").trim();
    if (!text && !attachmentControl?.draft.length) return;
    closeMentionSuggest();
    if (text) {
      if (inputEl) inputEl.value = "";
      const isOrgAgentPrompt =
        CLAUDE_MENTION_RE.test(text) || CODEX_MENTION_RE.test(text);
      if (isOrgAgentPrompt) {
        if (!orgAgentAccessLoaded || !orgAgentEngineeringAccess) {
          appendSystem("Claude and Codex chat is available only to the Engineering team.");
          return;
        }
        if (attachmentControl?.draft.length) {
          showAttachmentFeedback(
            attachmentControl,
            "Agent chat attachments are not sent to the shared room. Add the relevant path in your prompt.",
          );
        }
        const clipped = text.slice(0, MAX_TEXT);
        const plain = makePlain("chat", { channel: CHANNEL, text: clipped });
        seen.add(plain.id);
        appendMessage("self", plain.sender, plain.text, plain.id, plain.senderId, plain.ts);
        emitWorldChatBubble(plain.sender, plain.senderId, plain.text);
        void maybeAskOrgAgent(clipped);
        return;
      }
      runWhenConnected(() => {
        const clipped = text.slice(0, MAX_TEXT);
        const plain = makePlain("chat", { channel: CHANNEL, text: clipped });
        send(plain);
        seen.add(plain.id);
        appendMessage("self", plain.sender, plain.text, plain.id, plain.senderId, plain.ts);
        emitWorldChatBubble(plain.sender, plain.senderId, plain.text);
        void maybeAskForkbot(clipped);
      });
    }
    void sendDashboardDraft(attachmentControl);
  }

  function wireInput(inputEl, sendEl) {
    if (!inputEl || !sendEl) return;
    const attachmentControl = mountAttachmentControl(inputEl);
    sendEl.addEventListener("click", () => {
      if (simpleWorldComposer && inputEl === fullInput && fullAction) {
        fullAction.value = "chat";
      }
      sendFrom(inputEl, attachmentControl);
    });
    inputEl.addEventListener("paste", (event) => {
      const items = Array.from(event.clipboardData?.items || []);
      const item = items.find((candidate) =>
        candidate.kind === "file" && String(candidate.type || "").startsWith("image/"));
      const file = item ? item.getAsFile() : null;
      if (!file) return;
      event.preventDefault();
      void stageDashboardAttachments(attachmentControl, [file]).then(() => {
        if (
          inputEl !== fullInput ||
          String(fullAction?.value || "chat") === "chat"
        ) {
          return sendDashboardDraft(attachmentControl);
        }
        return null;
      });
    });
    inputEl.addEventListener("keydown", (event) => {
      // While the mention list is open it owns Enter/Tab/arrows, so accepting a
      // name never sends the half-typed message.
      if (mentionSuggest && mentionSuggest.input === inputEl) {
        if (event.key === "ArrowDown") {
          event.preventDefault();
          moveMentionSuggest(1);
          return;
        }
        if (event.key === "ArrowUp") {
          event.preventDefault();
          moveMentionSuggest(-1);
          return;
        }
        if (event.key === "Tab" || event.key === "Enter") {
          event.preventDefault();
          acceptMentionSuggest();
          return;
        }
        if (event.key === "Escape") {
          event.preventDefault();
          closeMentionSuggest();
          return;
        }
      }
      if (event.key === "Enter" && !event.shiftKey) {
        event.preventDefault();
        if (simpleWorldComposer && inputEl === fullInput && fullAction) {
          fullAction.value = "chat";
        }
        sendFrom(inputEl, attachmentControl);
      }
    });
    inputEl.addEventListener("input", () => updateMentionSuggest(inputEl));
    inputEl.addEventListener("input", () => {
      if (inputEl !== fullInput) return;
      inputEl.style.height = "auto";
      inputEl.style.height = `${Math.min(inputEl.scrollHeight, 128)}px`;
    });
    inputEl.addEventListener("click", () => updateMentionSuggest(inputEl));
    inputEl.addEventListener("keyup", (event) => {
      // Caret moves that leave the value alone (no "input" event) can still
      // enter or leave an "@token".
      if (["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) {
        updateMentionSuggest(inputEl);
      }
    });
    inputEl.addEventListener("blur", closeMentionSuggest);
  }

  function wireTaskSend() {
    if (!fullTaskSend || !fullAction || !fullInput) return;
    fullTaskSend.addEventListener("click", () => {
      if (simpleWorldComposer) {
        fullAction.value = "agent";
        if (taskAssignee) taskAssignee.value = "agent";
        void runFullComposerAction(fullInput);
        return;
      }
      if (fullAction.value !== "task") {
        fullAction.value = "task";
        syncFullComposerAction();
        setComposerStatus(
          "Choose a team, then assign this task to a person or an agent.",
        );
        taskDepartment?.focus();
        return;
      }
      void runFullComposerAction(fullInput);
    });
  }

  function mountPrivateChannelsLink() {
    if (!PUBLIC_WORLD_GENERAL || !fullLog) return;
    const header = fullLog.previousElementSibling;
    if (!header || header.querySelector("[data-private-channels-link]")) return;
    const link = document.createElement("a");
    link.href = "/chat";
    link.textContent = "Open private channels";
    link.dataset.privateChannelsLink = "";
    link.className =
      "ml-auto text-xs font-semibold text-primary underline underline-offset-2 " +
      "hover:text-foreground";
    const status = header.querySelector("[data-dashboard-chat-status]")?.parentElement;
    header.insertBefore(link, status || null);
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
    await loadOrgAgentChatAccess();
    ensureEmptyState();
    mountPrivateChannelsLink();
    if (fullChannel) {
      fullChannel.value = ACTIVE_SPACE || "";
      fullChannel.addEventListener("change", () => {
        const destination = new URL(location.href);
        destination.searchParams.set("worldEmbed", "1");
        if (fullChannel.value) {
          destination.searchParams.set("space", fullChannel.value);
        } else {
          destination.searchParams.delete("space");
        }
        destination.searchParams.delete("org");
        destination.searchParams.delete("repo");
        destination.searchParams.delete("run");
        location.assign(`${destination.pathname}${destination.search}`);
      });
    }
    fullRepository?.addEventListener("change", () => {
      try {
        sessionStorage.setItem(
          "forkmesh.worldChat.repository",
          fullRepository.value,
        );
      } catch (_) {}
      syncChatContextBubbles();
    });
    fullAction?.addEventListener("change", syncFullComposerAction);
    taskDestination?.addEventListener("change", syncFullComposerAction);
    taskAssignee?.addEventListener("change", syncFullComposerAction);
    syncFullComposerAction();
    contextChannel?.addEventListener("click", () => fullChannel?.focus());
    contextSource?.addEventListener("click", () => fullRepository?.focus());
    contextAction?.addEventListener("click", () => fullAction?.focus());
    document
      .querySelectorAll("[data-dashboard-chat-scroll]")
      .forEach((button) => {
        button.addEventListener("click", () => {
          if (!fullLog) return;
          const direction = button.dataset.dashboardChatScroll;
          if (
            direction === "up" &&
            fullLog.scrollTop <= 32 &&
            hasHiddenHistory()
          ) {
            revealOlderHistory();
            return;
          }
          fullLog.scrollTo({
            top:
              direction === "up"
                ? Math.max(0, fullLog.scrollTop - fullLog.clientHeight * 0.8)
                : fullLog.scrollHeight,
            behavior: "smooth",
          });
        });
      });
    fullLog?.addEventListener("scroll", () => {
      syncChatScrollThumb();
      const currentTop = fullLog.scrollTop;
      if (
        hasHiddenHistory() &&
        currentTop <= 32 &&
        currentTop < lastFullLogScrollTop - 1 &&
        !historyWheelLatched
      ) {
        historyWheelLatched = true;
        scheduleHistoryWheelReset();
        revealOlderHistory();
      }
      lastFullLogScrollTop = currentTop;
    }, {
      passive: true,
    });
    fullLog?.addEventListener("wheel", (event) => {
      scheduleHistoryWheelReset();
      if (event.deltaY > 0) historyWheelLatched = false;
      if (
        event.deltaY < 0 &&
        hasHiddenHistory() &&
        fullLog.scrollTop <= 32 &&
        !historyWheelLatched
      ) {
        historyWheelLatched = true;
        revealOlderHistory();
      }
    }, { passive: true });
    fullLog?.addEventListener("touchstart", (event) => {
      historyTouchRevealed = false;
      historyTouchStartY =
        fullLog.scrollTop <= 32
          ? Number(event.touches?.[0]?.clientY)
          : null;
    }, { passive: true });
    fullLog?.addEventListener("touchmove", (event) => {
      const currentY = Number(event.touches?.[0]?.clientY);
      if (
        Number.isFinite(historyTouchStartY) &&
        Number.isFinite(currentY) &&
        currentY - historyTouchStartY >= 28 &&
        !historyTouchRevealed &&
        hasHiddenHistory()
      ) {
        historyTouchRevealed = true;
        revealOlderHistory();
      }
    }, { passive: true });
    fullLog?.addEventListener("touchend", () => {
      historyTouchStartY = null;
      historyTouchRevealed = false;
    }, { passive: true });
    if (fullLog && "ResizeObserver" in window) {
      new ResizeObserver(syncChatScrollThumb).observe(fullLog);
    }
    syncChatScrollThumb();
    document
      .querySelectorAll("[data-dashboard-chat-emote]")
      .forEach((button) => {
        button.addEventListener("click", () => {
          const emote = String(button.dataset.dashboardChatEmote || "");
          if (window.parent !== window) {
            window.parent.postMessage(
              { type: "forkmesh:world-emote", emote },
              location.origin,
            );
          } else {
            window.dispatchEvent(
              new CustomEvent("forkmesh:world-emote-native", {
                detail: { emote },
              }),
            );
          }
        });
      });
    void loadComposerRepositories();
    void loadTaskRouting();
    wireInput(fullInput, fullSend);
    wireTaskSend();
    wireInput(sideInput, sideSend);
    // Connect right away so the room's message history (replayed by the relay
    // on WebSocket open) is visible without the visitor first focusing an input.
    if (canJoinChat()) {
      setStatus("Not connected");
      connect();
    }
    startDiscordMessageRefresh();
  }

  initChat();
  return true;
}

window.ForkMeshDashboardChat = Object.freeze({
  mount: mountForkMeshDashboardChat,
});
mountForkMeshDashboardChat();
