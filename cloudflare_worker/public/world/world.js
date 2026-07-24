import {
  ACTIVITY_OPTIONS,
  AVAILABILITY_OPTIONS,
  LANDMARKS,
  RADIO_STATIONS,
  THEME_OPTIONS,
  TOUR_STEPS,
  WORKSHOP_TYPES,
  WORLD_EMOJI_CATEGORIES,
  WORLD_REGIONS,
  WORLD_STATUS_NOTE_MAX,
  detectClient,
  flagEmoji,
  landmarkById,
  normalizeWorldEmoji,
  normalizeWorldStatus,
  normalizeWorldStatusNote,
  sanitizePresenceText,
} from "./world-data.js";
import { buildLiveMirrorNodes } from "./world-mirror-nodes.js";
import {
  buildPullMergeRequest,
  buildPullFileTree,
  exactPullMergeContext,
  immutableGitOid,
  parsePullFrontMatter,
  parseUnifiedDiff,
  pullViewedStateKey,
  safeDiffPath,
  safePullNumber,
} from "./world-pull-review.js";
import { buildRepositoryGraphEntities } from "./world-repository-graph.js";
import { createWorldScene } from "./world-scene.js";

const THREE_MODULE_URL =
  "https://cdn.jsdelivr.net/npm/three@0.184.0/build/three.module.min.js";
// Kick off the heavy 3D runtime download the moment this module evaluates so it
// streams in parallel with parsing, the initial data fetches, and scene setup
// rather than only starting once bootstrap() reaches its await. bootstrap()
// re-awaits this promise (handling any load failure there); the noop catch just
// keeps a CDN failure from surfacing as an unhandled rejection before then.
const THREE_MODULE = import(THREE_MODULE_URL);
THREE_MODULE.catch(() => {});
const SETTINGS_KEY = "forkmesh.world.settings.v1";
const GUEST_ID_KEY = "forkmesh.world.guestId.v1";
const FIRST_VISIT_KEY = "forkmesh.world.firstVisitAt.v1";
const VISIT_COUNT_KEY = "forkmesh.world.publicVisitCount.v1";
const INTRO_DISMISSED_KEY = "forkmesh.world.introDismissed.v1";
const POSITION_KEY_PREFIX = "forkmesh.world.position.v1.";
const POSITION_MAX_AGE_MS = 30 * 24 * 60 * 60 * 1000;
const POSITION_WRITE_INTERVAL_MS = 1000;
const POSITION_RADIUS = 72;
const POSITION_FLOOR_TOLERANCE = 0.5;
// Mirrors the server's WORLD_ARRIVAL_CLEARANCE: a restored spot this close to
// another visitor is treated as occupied and the fresh server slot wins.
const ARRIVAL_CLEARANCE = 0.9;
const POSITION_FLOORS = Object.freeze({
  "town-square": 0.38,
  east: 0.38,
  central: 0.38,
  west: 0.38,
  "sky-campus": 15.45,
  "space-station": 18.45,
  "code-planet": 15.45,
  "organization-region": 14.45,
  "planet-atlas": 22.45,
});
const SOCKET_RETRY_MAX_MS = 20000;
const SOCKET_STABLE_MS = 5000;
const SOCKET_PEER_GRACE_MS = 8000;
const SOCKET_BUFFER_HIGH_WATER_BYTES = 64 * 1024;
const PRESENCE_PROFILE_DEBOUNCE_MS = 300;
const MOVEMENT_SEND_INTERVAL_MS = 1000;
const PRESENCE_STALE_MS = 22000;
const WORLD_TICKET_REFRESH_MS = 5 * 60 * 1000;
const WORLD_NOTIFICATION_POLL_MS = 30 * 1000;
const MIRROR_STATUS_POLL_MS = 30 * 1000;
const WORLD_MANUAL_BLOCK_DURATION_MS = 60 * 60 * 1000;
const WORLD_SCORE_LOOP_MS = 4 * 60 * 60 * 1000;
const WORLD_LIGHT_LEVEL_MIN = 40;
const WORLD_LIGHT_LEVEL_MAX = 140;
const WORLD_LIGHT_LEVEL_DEFAULT = 100;
// Movement tuning, stored per device as a percentage of the shared defaults.
const WORLD_MOVE_SPEED_MIN = 50;
const WORLD_MOVE_SPEED_MAX = 300;
const WORLD_MOVE_SPEED_DEFAULT = 100;
const WORLD_MOVE_ACCEL_MIN = 25;
// The top slider position is the "instant" sentinel — it maps to infinite
// acceleration so the player reaches top speed the moment a key is pressed.
const WORLD_MOVE_ACCEL_MAX = 1000;
const WORLD_MOVE_ACCEL_DEFAULT = 100;
const WORLD_DIAGNOSTICS_INTERVAL_MS = 1000;
const WORLD_DIAGNOSTICS_COUNTER_MAX = 1_000_000_000;
const WORLD_PULL_MERGE_MAX_REQUESTS = 6;
const WORLD_PULL_MERGE_POLL_MS = 400;
const WORLD_PULL_MERGE_RESPONSE_MAX_BYTES = 16 * 1024;
// The edge router can spend up to 20 seconds on each of two attested mirrors.
// Keep the browser bound just above that failover envelope; shorter 6-12s
// aborts made healthy exact-ref reads fail whenever a one-vCPU node was doing
// integrity maintenance.
const REPOSITORY_METADATA_TIMEOUT_MS = 45 * 1000;
const WORLD_ACCOUNT_NAME_RE =
  /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/;
const FLAGSHIP_REPOSITORY = Object.freeze({
  owner: "forkmesh",
  repo: "forkmesh",
});
const ACCOUNT_STATUS_VALUES = new Set([
  "Guest",
  "Registered",
  "Supporting member",
  "Mirror operator",
  "Organization admin",
  "Verified bot",
]);
const ACCOUNT_STATUS_ICONS = Object.freeze({
  Guest: "○",
  Registered: "✓",
  "Supporting member": "♥",
  "Mirror operator": "◈",
  "Organization admin": "◆",
  "Verified bot": "⌘",
});
const WORLD_SPACE_IDS = new Set([
  "town-square",
  "east",
  "central",
  "west",
  "sky-campus",
  "space-station",
  "code-planet",
  "organization-region",
  "planet-atlas",
]);

function escapeHTML(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

function incrementDiagnosticCounter(value) {
  return Math.min(
    WORLD_DIAGNOSTICS_COUNTER_MAX,
    Math.max(0, Number(value) || 0) + 1,
  );
}

function normalizeBuildDiagnostics(payload) {
  const version = String(payload?.version || "")
    .trim()
    .replace(/[^a-z0-9._+-]/gi, "")
    .slice(0, 32);
  const rawRevision = String(payload?.rev || "").trim();
  const revision = /^[a-f0-9]{7,64}$/i.test(rawRevision)
    ? rawRevision.toLowerCase()
    : "";
  return { version, revision };
}

function readJSON(storage, key, fallback) {
  try {
    const value = JSON.parse(storage.getItem(key) || "null");
    return value && typeof value === "object" ? value : fallback;
  } catch (_) {
    return fallback;
  }
}

function writeJSON(storage, key, value) {
  try {
    storage.setItem(key, JSON.stringify(value));
  } catch (_) {}
}

function positionIdentityToken(value) {
  let first = 0x811c9dc5;
  let second = 0x9e3779b9;
  for (const character of String(value || "")) {
    const code = character.charCodeAt(0);
    first = Math.imul(first ^ code, 0x01000193) >>> 0;
    second = Math.imul(second ^ (code + 0x9d), 0x85ebca6b) >>> 0;
  }
  return `${first.toString(16).padStart(8, "0")}${second
    .toString(16)
    .padStart(8, "0")}`;
}

function positionStorageKey(identityId) {
  return `${POSITION_KEY_PREFIX}${positionIdentityToken(identityId)}`;
}

function normalizedWorldPosition(record, now = Date.now()) {
  if (!record || typeof record !== "object" || Array.isArray(record)) return null;
  const allowedFields = ["heading", "space", "updatedAt", "x", "y", "z"];
  const fields = Object.keys(record).sort();
  if (
    fields.length !== allowedFields.length ||
    fields.some((field, index) => field !== allowedFields[index])
  ) {
    return null;
  }
  const space = String(record.space || "");
  const floor = POSITION_FLOORS[space];
  const x = Number(record.x);
  const y = Number(record.y);
  const z = Number(record.z);
  const heading = Number(record.heading);
  const updatedAt = Number(record.updatedAt);
  if (
    !WORLD_SPACE_IDS.has(space) ||
    !Number.isFinite(x) ||
    !Number.isFinite(y) ||
    !Number.isFinite(z) ||
    !Number.isFinite(heading) ||
    !Number.isSafeInteger(updatedAt) ||
    Math.abs(x) > POSITION_RADIUS ||
    Math.abs(z) > POSITION_RADIUS ||
    Math.abs(y - floor) > POSITION_FLOOR_TOLERANCE ||
    heading < -Math.PI ||
    heading > Math.PI ||
    updatedAt < now - POSITION_MAX_AGE_MS ||
    updatedAt > now + 60 * 1000
  ) {
    return null;
  }
  return { x, y, z, heading, space, updatedAt };
}

function readWorldPosition(storage, key, now = Date.now()) {
  const position = normalizedWorldPosition(readJSON(storage, key, null), now);
  if (!position) {
    try {
      storage.removeItem(key);
    } catch (_) {}
  }
  return position;
}

function firstVisitTimestamp(now = Date.now()) {
  try {
    const stored = Number(localStorage.getItem(FIRST_VISIT_KEY) || 0);
    if (
      Number.isSafeInteger(stored) &&
      stored >= Date.UTC(2020, 0, 1) &&
      stored <= now
    ) {
      return stored;
    }
    localStorage.setItem(FIRST_VISIT_KEY, String(now));
  } catch (_) {}
  return now;
}

function firstVisitAge(timestamp, now = Date.now()) {
  const age = Math.max(0, now - Number(timestamp || now));
  if (age < 10 * 60 * 1000) return "this-session";
  if (age < 24 * 60 * 60 * 1000) return "today";
  if (age < 7 * 24 * 60 * 60 * 1000) return "this-week";
  if (age < 31 * 24 * 60 * 60 * 1000) return "this-month";
  if (age < 366 * 24 * 60 * 60 * 1000) return "this-year";
  return "over-a-year";
}

function sessionVisitCount(increment = false) {
  try {
    const current = Math.max(
      0,
      Math.min(999, Number(sessionStorage.getItem(VISIT_COUNT_KEY)) || 0),
    );
    const next = increment ? Math.min(999, current + 1) : current;
    sessionStorage.setItem(VISIT_COUNT_KEY, String(next));
    return next;
  } catch (_) {
    return increment ? 1 : 0;
  }
}

function introDismissed() {
  try {
    return localStorage.getItem(INTRO_DISMISSED_KEY) === "1";
  } catch (_) {
    return false;
  }
}

function randomId() {
  try {
    return crypto.randomUUID();
  } catch (_) {
    return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
  }
}

function readSession() {
  return readJSON(localStorage, "forkmesh.session", null);
}

function validWorldSession() {
  const session = readSession();
  const nodeName = String(session?.nodeName || "").trim().toLowerCase();
  const sessionToken = String(session?.sessionToken || "").trim();
  return WORLD_ACCOUNT_NAME_RE.test(nodeName) &&
    sessionToken &&
    sessionToken.length <= 2048
    ? { ...session, nodeName, sessionToken }
    : null;
}

function createPullMergeRequestId() {
  try {
    if (typeof crypto.randomUUID === "function") {
      return `world_merge_${crypto.randomUUID().replaceAll("-", "")}`;
    }
    const bytes = new Uint8Array(18);
    crypto.getRandomValues(bytes);
    return `world_merge_${[...bytes]
      .map((value) => value.toString(16).padStart(2, "0"))
      .join("")}`;
  } catch (_) {
    // A merge request never falls back to a predictable Math.random id.
    return "";
  }
}

function storeWorldSession(body) {
  const nodeName = String(body?.nodeName || "").trim().toLowerCase();
  const sessionToken = String(body?.sessionToken || "").trim();
  if (
    !WORLD_ACCOUNT_NAME_RE.test(nodeName) ||
    !sessionToken ||
    sessionToken.length > 2048
  ) {
    return false;
  }
  writeJSON(localStorage, "forkmesh.session", {
    nodeName,
    email: String(body?.email || "").slice(0, 320),
    status: String(body?.status || "").slice(0, 32),
    pubkey: String(body?.pubkey || "").slice(0, 256),
    emailVerified: Boolean(body?.emailVerified),
    isAdmin: Boolean(body?.isAdmin),
    adminUrl: String(body?.adminUrl || "").slice(0, 300),
    solana: String(body?.solana || "").slice(0, 80),
    hasPayoutAddress: Boolean(body?.hasPayoutAddress),
    sessionToken,
    avatarPng: String(body?.avatarPng || "").slice(0, 600),
    avatarUpdatedAt: Number(body?.avatarUpdatedAt) || 0,
    profileBio: String(body?.profileBio || "").slice(0, 500),
    profileAbout: String(
      body?.profileAbout || body?.profileReadme || "",
    ).slice(0, 20_000),
    profileReadme: String(
      body?.profileReadme || body?.profileAbout || "",
    ).slice(0, 20_000),
    profileLocation: String(body?.profileLocation || "").slice(0, 160),
    profileTimezone: String(body?.profileTimezone || "").slice(0, 80),
    profileLinks: Array.isArray(body?.profileLinks)
      ? body.profileLinks.slice(0, 12).map((link) => ({
          label: String(link?.label || "").slice(0, 80),
          url: String(link?.url || "").slice(0, 500),
        }))
      : [],
    profileFollowers: Math.max(0, Number(body?.followers) || 0),
    profileFollowing: Math.max(0, Number(body?.following) || 0),
    profileMirrorCount: Math.max(0, Number(body?.mirrorCount) || 0),
    kind: String(body?.kind || "").slice(0, 32),
    owner: String(body?.owner || "").slice(0, 80),
    nodes: Array.isArray(body?.nodes)
      ? body.nodes
          .slice(0, 64)
          .map((node) => String(node || "").slice(0, 80))
      : [],
    at: Date.now(),
  });
  try {
    document.cookie =
      "forkmesh_session=1; Path=/; Max-Age=2592000; SameSite=Lax" +
      (location.protocol === "https:" ? "; Secure" : "");
  } catch (_) {}
  return true;
}

function guestId() {
  try {
    let id = sessionStorage.getItem(GUEST_ID_KEY);
    if (!id) {
      id = randomId();
      sessionStorage.setItem(GUEST_ID_KEY, id);
    }
    return id;
  } catch (_) {
    return randomId();
  }
}

function accountIdentity(session) {
  const client = detectClient();
  const id = session?.nodeName
    ? `account:${String(session.nodeName).toLowerCase()}`
    : `guest:${guestId()}`;
  const suffix = hashSuffix(id);
  // Login data in localStorage is only a cache and can be edited by the
  // browser owner. A server-issued world ticket upgrades this guest identity
  // after the session has actually been verified.
  const rawName = `Guest ${suffix}`;
  const name = sanitizePresenceText(rawName, `Guest ${suffix}`, 24);

  return {
    id,
    name,
    browser: client.browser,
    os: client.os,
    touch: client.touch,
    countryCode: "",
    flag: "◌",
    accountStatus: "Guest",
    isAdmin: false,
    nodes: [],
    inputActive: false,
    visitCount: 0,
    firstVisitAge: "this-session",
    activityCategory: "exploring-town-square",
  };
}

function hashSuffix(value) {
  let hash = 0;
  for (const char of String(value || "")) hash = (Math.imul(hash, 31) + char.charCodeAt(0)) >>> 0;
  return String(hash % 10000).padStart(4, "0");
}

function defaultSettings() {
  return {
    theme: "world",
    lightLevel: WORLD_LIGHT_LEVEL_DEFAULT,
    moveSpeed: WORLD_MOVE_SPEED_DEFAULT,
    moveAccel: WORLD_MOVE_ACCEL_DEFAULT,
    availability: "online",
    activityCategory: "automatic",
    publicDoor: "knock",
    displayName: "",
    statusEmoji: "",
    statusNote: "",
    privacy: {
      name: true,
      country: true,
      browser: true,
      os: true,
      activity: true,
      inactivity: false,
      localTime: false,
      nodes: true,
    },
    labels: true,
    reducedData: false,
  };
}

function mergeSettings(stored) {
  const defaults = defaultSettings();
  const requestedTheme = String(stored?.theme || defaults.theme);
  const theme = THEME_OPTIONS.some((option) => option.id === requestedTheme)
    ? requestedTheme
    : defaults.theme;
  const publicStatus = normalizeWorldStatus(
    stored?.statusEmoji,
    stored?.statusNote,
  );
  return {
    ...defaults,
    ...(stored || {}),
    theme,
    lightLevel: Math.min(
      WORLD_LIGHT_LEVEL_MAX,
      Math.max(
        WORLD_LIGHT_LEVEL_MIN,
        Number.isFinite(Number(stored?.lightLevel))
          ? Number(stored.lightLevel)
          : WORLD_LIGHT_LEVEL_DEFAULT,
      ),
    ),
    moveSpeed: Math.min(
      WORLD_MOVE_SPEED_MAX,
      Math.max(
        WORLD_MOVE_SPEED_MIN,
        Number.isFinite(Number(stored?.moveSpeed))
          ? Number(stored.moveSpeed)
          : WORLD_MOVE_SPEED_DEFAULT,
      ),
    ),
    moveAccel: Math.min(
      WORLD_MOVE_ACCEL_MAX,
      Math.max(
        WORLD_MOVE_ACCEL_MIN,
        Number.isFinite(Number(stored?.moveAccel))
          ? Number(stored.moveAccel)
          : WORLD_MOVE_ACCEL_DEFAULT,
      ),
    ),
    statusEmoji: publicStatus.emoji,
    statusNote: publicStatus.note,
    privacy: {
      ...defaults.privacy,
      ...(stored?.privacy || {}),
    },
  };
}

function publicIdentity(identity, settings) {
  const chosenName = sanitizePresenceText(
    identity.accountStatus === "Guest" ? settings.displayName : identity.name,
    identity.name,
    24,
  );
  const publicStatus = normalizeWorldStatus(
    settings.statusEmoji,
    settings.statusNote,
  );
  return {
    id: identity.id,
    name: settings.privacy.name ? chosenName : "Private visitor",
    flag: settings.privacy.country ? identity.flag : "◌",
    countryCode:
      settings.privacy.country &&
      /^[A-Z]{2}$/.test(String(identity.countryCode || ""))
        ? String(identity.countryCode)
        : "",
    browser: settings.privacy.browser ? identity.browser : "Hidden",
    os: settings.privacy.os ? identity.os : "Hidden",
    accountStatus: identity.accountStatus,
    isAdmin: identity.isAdmin === true,
    status:
      settings.privacy.activity &&
      (settings.privacy.inactivity || !["inactive", "recent"].includes(settings.availability))
        ? settings.availability
        : "hidden",
    localTime: settings.privacy.localTime
      ? new Intl.DateTimeFormat(undefined, {
          hour: "numeric",
          minute: "2-digit",
        }).format(new Date())
      : "",
    // Only an authenticated, bounded count is represented. Node names never
    // enter public world presence.
    nodes: settings.privacy.nodes
      ? Array.from(
          { length: Math.min(identity.nodes?.length || 0, 6) },
          () => "node",
        )
      : [],
    publicDoor: ["closed", "knock", "open"].includes(settings.publicDoor)
      ? settings.publicDoor
      : "closed",
    activityCategory: settings.privacy.activity
      ? String(identity.activityCategory || "exploring-town-square")
      : "hidden",
    inputActive:
      settings.privacy.activity && identity.inputActive === true,
    visitCount: settings.privacy.activity
      ? Math.max(0, Math.min(999, Number(identity.visitCount) || 0))
      : 0,
    firstVisitAge: settings.privacy.activity
      ? String(identity.firstVisitAge || "this-session")
      : "hidden",
    statusEmoji: publicStatus.emoji,
    statusNote: publicStatus.note,
  };
}

function presenceBrowser(value, visible) {
  if (!visible) return "hidden";
  const normalized = String(value || "").toLowerCase();
  return ["chrome", "edge", "firefox", "safari"].includes(normalized)
    ? normalized
    : "other";
}

function presenceOS(value, visible) {
  if (!visible) return "hidden";
  const normalized = String(value || "").toLowerCase().replace(/\s+/g, "");
  return ["android", "chromeos", "ios", "linux", "macos", "windows"].includes(
    normalized,
  )
    ? normalized
    : "other";
}

function presenceStatus(settings) {
  if (!settings?.privacy?.activity) return "hidden";
  if (
    !settings.privacy.inactivity &&
    ["inactive", "recent"].includes(settings.availability)
  ) {
    return "hidden";
  }
  return {
    online: "available",
    away: "away",
    inactive: "idle",
    recent: "idle",
    "offline-operator": "away",
    returning: "available",
  }[settings.availability] || "exploring";
}

function presenceLocalTime(visible) {
  if (!visible) return "";
  const now = new Date();
  return `${String(now.getHours()).padStart(2, "0")}:${String(
    now.getMinutes(),
  ).padStart(2, "0")}`;
}

function presenceActivity(settings, automatic = "exploring-town-square") {
  if (!settings?.privacy?.activity) return "hidden";
  const chosen =
    settings.activityCategory === "automatic"
      ? automatic
      : settings.activityCategory;
  return ACTIVITY_OPTIONS.some((option) => option.id === chosen)
    ? chosen
    : "exploring-town-square";
}

function boundedPresenceNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.max(-512, Math.min(512, number)) : 0;
}

function boundedYaw(value) {
  const number = Number(value);
  return Number.isFinite(number)
    ? Math.max(-Math.PI, Math.min(Math.PI, number))
    : 0;
}

function presenceLabel(value, hidden, fallback) {
  if (value === "hidden") return hidden;
  if (!value || value === "other") return fallback;
  const labels = {
    chrome: "Chrome",
    edge: "Edge",
    firefox: "Firefox",
    safari: "Safari",
    android: "Android",
    chromeos: "ChromeOS",
    ios: "iOS",
    linux: "Linux",
    macos: "macOS",
    windows: "Windows",
  };
  return labels[value] || fallback;
}

function remotePlayer(peer) {
  if (!peer?.id) return null;
  const status = String(peer.status || "hidden");
  const publicStatus = normalizeWorldStatus(
    peer.statusEmoji,
    peer.statusNote,
  );
  const moderationHandles = {};
  if (peer.moderationHandles && typeof peer.moderationHandles === "object") {
    for (const targetType of ["ip", "agent"]) {
      const handle = String(peer.moderationHandles[targetType] || "");
      if (/^[a-f0-9]{64}$/.test(handle)) {
        moderationHandles[targetType] = handle;
      }
    }
  }
  return {
    id: String(peer.id),
    name: sanitizePresenceText(peer.name, "visitor", 32),
    flag: flagEmoji(peer.countryCode),
    countryCode: /^[A-Z]{2}$/.test(String(peer.countryCode || ""))
      ? String(peer.countryCode)
      : "",
    browser: presenceLabel(peer.browser, "Hidden", "Browser"),
    os: presenceLabel(peer.os, "Hidden", "Device"),
    activity: status === "hidden" ? "online" : status,
    category: ACTIVITY_OPTIONS.some(
      (option) => option.id === String(peer.activityCategory || ""),
    )
      ? String(peer.activityCategory)
      : "hidden",
    publicDoor: ["closed", "knock", "open"].includes(String(peer.publicDoor))
      ? String(peer.publicDoor)
      : "closed",
    accountStatus: ACCOUNT_STATUS_VALUES.has(String(peer.accountStatus || ""))
      ? String(peer.accountStatus)
      : "Guest",
    nodes: Array.from(
      { length: Math.max(0, Math.min(6, Number(peer.nodeCount) || 0)) },
      () => "node",
    ),
    space: WORLD_SPACE_IDS.has(String(peer.space || ""))
      ? String(peer.space)
      : "town-square",
    localTime: /^\d{2}:\d{2}$/.test(String(peer.localTime || ""))
      ? String(peer.localTime)
      : "",
    x: boundedPresenceNumber(peer.x),
    y: boundedPresenceNumber(peer.y),
    z: boundedPresenceNumber(peer.z),
    heading: boundedYaw(peer.yaw),
    inputActive: peer.inputActive === true,
    visitCount: Math.max(0, Math.min(999, Number(peer.visitCount) || 0)),
    firstVisitAge: [
      "this-session",
      "today",
      "this-week",
      "this-month",
      "this-year",
      "over-a-year",
    ].includes(String(peer.firstVisitAge || ""))
      ? String(peer.firstVisitAge)
      : "hidden",
    statusEmoji: publicStatus.emoji,
    statusNote: publicStatus.note,
    moderationHandles,
    updatedAt: Math.max(0, Number(peer.updatedAt) || 0),
  };
}

function compactNumber(value) {
  const number = Number(value) || 0;
  if (number < 1000) return String(number);
  return new Intl.NumberFormat(undefined, {
    notation: "compact",
    maximumFractionDigits: 1,
  }).format(number);
}

function formatMediaPosition(value) {
  const totalSeconds = Math.max(
    0,
    Math.min(7 * 24 * 60 * 60, Math.floor((Number(value) || 0) / 1000)),
  );
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return hours > 0
    ? `${hours}:${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`
    : `${minutes}:${String(seconds).padStart(2, "0")}`;
}

function providerMetadataCopy(item) {
  const metadata = item?.providerMetadata;
  if (
    metadata?.status === "available" &&
    Number(metadata.receivedAt) > 0 &&
    metadata.title
  ) {
    const credit = [metadata.artist, metadata.title].filter(Boolean).join(" — ");
    return `Permitted provider metadata received ${new Date(
      metadata.receivedAt,
    ).toLocaleTimeString()}: ${credit}`;
  }
  return "Current provider track metadata unavailable: ForkMesh did not receive a permitted provider metadata event. The playlist title is user supplied.";
}

function createProceduralWorldSoundtrack(AudioContext, scoreOffsetMs) {
  const context = new AudioContext();
  const master = context.createGain();
  master.gain.setValueAtTime(0.0001, context.currentTime);
  master.gain.exponentialRampToValueAtTime(
    0.055,
    context.currentTime + 1.2,
  );
  master.connect(context.destination);

  const stepSeconds = 60 / 72 / 2;
  const roots = [110, 98, 82.41, 92.5, 73.42, 82.41, 98, 87.31];
  let step = Math.floor(
    (scoreOffsetMs / 1000 / stepSeconds) %
      Math.floor(WORLD_SCORE_LOOP_MS / 1000 / stepSeconds),
  );
  let nextWhen = context.currentTime + 0.08;
  let stopped = false;

  const voice = (frequency, when, duration, level, type, detune = 0) => {
    const oscillator = context.createOscillator();
    const envelope = context.createGain();
    oscillator.type = type;
    oscillator.frequency.setValueAtTime(frequency, when);
    oscillator.detune.setValueAtTime(detune, when);
    envelope.gain.setValueAtTime(0.0001, when);
    envelope.gain.exponentialRampToValueAtTime(level, when + 0.18);
    envelope.gain.exponentialRampToValueAtTime(
      0.0001,
      when + Math.max(0.25, duration),
    );
    oscillator.connect(envelope);
    envelope.connect(master);
    oscillator.start(when);
    oscillator.stop(when + duration + 0.08);
  };

  const schedule = () => {
    const horizon = context.currentTime + 1.6;
    while (!stopped && nextWhen < horizon) {
      const scoreStepCount = Math.floor(
        WORLD_SCORE_LOOP_MS / 1000 / stepSeconds,
      );
      const normalizedStep = ((step % scoreStepCount) + scoreStepCount) %
        scoreStepCount;
      const scoreMs = normalizedStep * stepSeconds * 1000;
      const chapter = Math.floor(scoreMs / (15 * 60 * 1000));
      const bar = Math.floor(normalizedStep / 8);
      const root = roots[(bar + chapter * 3) % roots.length];
      const color = [1, 6 / 5, 3 / 2, 9 / 5][
        (Math.floor(bar / 2) + chapter) % 4
      ];
      if (normalizedStep % 8 === 0) {
        voice(root, nextWhen, stepSeconds * 7.5, 0.025, "sine");
        voice(
          root * color,
          nextWhen + 0.03,
          stepSeconds * 7.2,
          0.012,
          "triangle",
          chapter % 2 ? 4 : -4,
        );
      }
      if (normalizedStep % 2 === 0) {
        voice(
          root * (chapter % 3 === 0 ? 2 : 1),
          nextWhen,
          stepSeconds * 1.45,
          0.009,
          "sine",
        );
      }
      if (
        normalizedStep % 4 === 3 &&
        (chapter % 4 !== 0 || normalizedStep % 16 === 15)
      ) {
        voice(
          880 + (chapter % 5) * 55,
          nextWhen,
          0.11,
          0.0025,
          "triangle",
        );
      }
      step += 1;
      nextWhen += stepSeconds;
    }
  };
  schedule();
  const timer = window.setInterval(schedule, 180);
  return {
    context,
    gain: master,
    timer,
    scoreOffsetMs,
    durationMs: WORLD_SCORE_LOOP_MS,
    license: "ForkMesh Procedural World Score · CC0-1.0",
    stop() {
      if (stopped) return;
      stopped = true;
      window.clearInterval(timer);
      master.gain.cancelScheduledValues(context.currentTime);
      master.gain.setTargetAtTime(0.0001, context.currentTime, 0.03);
    },
  };
}

function safeHTTPURL(value) {
  try {
    const raw = String(value || "").trim();
    if (!raw) return "";
    const url = new URL(raw, location.origin);
    if (!["http:", "https:"].includes(url.protocol)) return "";
    return url.href;
  } catch (_) {
    return "";
  }
}

function safeNotificationURL(value) {
  try {
    const raw = String(value || "").trim();
    if (!raw) return "";
    const url = new URL(raw, location.origin);
    if (url.username || url.password) return "";
    if (url.origin === location.origin) {
      return `${url.pathname}${url.search}${url.hash}`;
    }
    return url.protocol === "https:" ? url.href : "";
  } catch (_) {
    return "";
  }
}

function sanitizeNotificationText(value, fallback = "", maxLength = 500) {
  const text = String(value ?? "")
    .replace(
      /[\u0000-\u001f\u007f-\u009f\u202a-\u202e\u2066-\u2069]/g,
      " ",
    )
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, maxLength);
  return text || fallback;
}

function safePublicHTTPSURL(value) {
  try {
    const raw = String(value || "").trim();
    if (!raw) return "";
    const url = new URL(raw);
    const host = url.hostname.toLowerCase().replace(/\.$/, "");
    if (
      url.protocol !== "https:" ||
      url.username ||
      url.password ||
      !host.includes(".") ||
      ["localhost", "0.0.0.0", "::1"].includes(host) ||
      /^(?:\d{1,3}\.){3}\d{1,3}$/.test(host) ||
      host.endsWith(".local") ||
      host.endsWith(".internal") ||
      host.endsWith(".test") ||
      host.endsWith(".onion")
    ) {
      return "";
    }
    url.hash = "";
    return url.href;
  } catch (_) {
    return "";
  }
}

function normalizeCommunityPlacement(value) {
  if (
    value?.label !== "Community-reviewed placement" ||
    value?.tracking !== "none" ||
    value?.behavioralTargeting !== false ||
    value?.sensitiveTargeting !== false ||
    value?.personalDataUsed !== false
  ) {
    return null;
  }
  const item = Array.isArray(value?.placements) ? value.placements[0] : null;
  if (
    !item ||
    item.label !== "Community-reviewed placement" ||
    item.tracking !== "none"
  ) {
    return null;
  }
  const destination = safePublicHTTPSURL(item.destinationUrl);
  const proposalId = /^[a-f0-9]{32}$/.test(String(item.proposalId || ""))
    ? String(item.proposalId)
    : "";
  if (!destination || !proposalId) return null;
  return {
    proposalId,
    label: "Community-reviewed placement",
    sponsor: sanitizePresenceText(item.sponsor, "Community sponsor", 80),
    copy: sanitizePresenceText(item.copy, "", 180),
    destination,
    whyShown: sanitizePresenceText(
      item.whyShown,
      "Enabled for the Town Square page context.",
      120,
    ),
    tracking: "none",
  };
}

function normalizeFediverseMentions(value) {
  const states = new Set([
    "review",
    "pending",
    "created",
    "linked",
    "failed",
  ]);
  return (Array.isArray(value?.items) ? value.items : [])
    .map((item) => {
      const id = /^[a-f0-9]{32}$/.test(String(item?.id || ""))
        ? String(item.id)
        : "";
      const state = states.has(item?.state) ? item.state : "review";
      const kind = item?.kind === "reply" ? "reply" : "mention";
      const repositoryParts = String(item?.repository || "").split("/");
      const repository =
        repositoryParts.length === 2 &&
        repositoryParts.every((part) =>
          /^[A-Za-z0-9_.-]{1,100}$/.test(part),
        )
          ? repositoryParts.join("/")
          : "public repository";
      return {
        id,
        state,
        kind,
        repository,
        author: sanitizePresenceText(item?.author, "fediverse user", 100),
        authorName: sanitizePresenceText(item?.authorName, "", 100),
        excerpt: sanitizePresenceText(item?.excerpt, "", 320),
        remoteUrl: safePublicHTTPSURL(item?.remoteUrl),
        issueUrl: safePublicHTTPSURL(item?.issueUrl),
        issueNumber:
          state === "created" || state === "linked"
            ? Math.max(0, Number(item?.issueNumber) || 0)
            : 0,
        progress: sanitizePresenceText(item?.progress, "", 180),
        verifiedPublicActivity: item?.verifiedPublicActivity === true,
        automaticIssueCreation: item?.automaticIssueCreation === true,
      };
    })
    .filter(
      (item) =>
        item.id &&
        item.remoteUrl &&
        item.verifiedPublicActivity &&
        item.automaticIssueCreation === false,
    )
    .slice(0, 50);
}

function normalizeMediaRoom(value) {
  const sessionTypes = new Set([
    "listening-room",
    "dj-session",
    "video-room",
    "watch-party",
    "repository-launch",
    "organization-presentation",
  ]);
  const source = value && typeof value === "object" ? value : {};
  const space =
    source.space && typeof source.space === "object" ? source.space : source;
  const items = Array.isArray(source.items)
    ? source.items
        .map((item) => ({
          id: sanitizePresenceText(item?.id, "", 40),
          title: sanitizePresenceText(item?.title, "Untitled media link", 80),
          url: safeHTTPURL(item?.url),
          provider: sanitizePresenceText(item?.provider, "External provider", 50),
          status: item?.status === "stopped" ? "stopped" : "queued",
          providerMetadata:
            item?.providerMetadata?.status === "available" &&
            item?.providerMetadata?.permissionConfirmed === true &&
            Number(item?.providerMetadata?.receivedAt) > 0
              ? {
                  status: "available",
                  title: sanitizePresenceText(
                    item.providerMetadata.title,
                    "Provider track",
                    100,
                  ),
                  artist: sanitizePresenceText(
                    item.providerMetadata.artist,
                    "",
                    100,
                  ),
                  receivedAt: Number(item.providerMetadata.receivedAt),
                }
              : { status: "unavailable", reason: "not_received" },
        }))
        .filter((item) => item.id && item.url?.startsWith("https://"))
        .slice(0, 100)
    : [];
  const schedules = Array.isArray(source.schedules)
    ? source.schedules
        .map((schedule) => ({
          id: sanitizePresenceText(schedule?.id, "", 40),
          title: sanitizePresenceText(schedule?.title, "Scheduled session", 100),
          sessionType: sessionTypes.has(schedule?.sessionType)
            ? schedule.sessionType
            : "listening-room",
          startsAt: Number(schedule?.startsAt) || 0,
          endsAt: Number(schedule?.endsAt) || 0,
          status: schedule?.status === "completed" ? "completed" : "scheduled",
        }))
        .filter((schedule) => schedule.id && schedule.startsAt > 0)
        .slice(0, 50)
    : [];
  const nextSchedule = schedules.find(
    (schedule) => schedule.status === "scheduled" && schedule.endsAt > Date.now(),
  );
  const playbackSource =
    source.playback && typeof source.playback === "object"
      ? source.playback
      : {};
  const playbackState = ["idle", "playing", "paused", "stopped"].includes(
    playbackSource.state,
  )
    ? playbackSource.state
    : "idle";
  const playbackItemId = sanitizePresenceText(
    playbackSource.itemId,
    "",
    40,
  );
  const playbackPosition = Math.max(
    0,
    Math.min(
      7 * 24 * 60 * 60 * 1000,
      Number(playbackSource.positionMs) || 0,
    ),
  );
  return {
    id: sanitizePresenceText(space?.id, "", 40),
    name: sanitizePresenceText(space?.name, "Shared media room", 80),
    description: sanitizePresenceText(space?.description, "", 300),
    sessionType: sessionTypes.has(space?.sessionType)
      ? space.sessionType
      : "listening-room",
    owner: sanitizePresenceText(space?.owner, "", 80),
    viewerRole: ["owner", "moderator"].includes(space?.viewerRole)
      ? space.viewerRole
      : "",
    canModerate: space?.canModerate === true,
    playbackState,
    playback: {
      state: playbackState,
      itemId: playbackItemId,
      positionMs: playbackPosition,
      changedAt: Math.max(0, Number(playbackSource.changedAt) || 0),
      startedAt: Math.max(0, Number(playbackSource.startedAt) || 0),
      revision: Math.max(0, Number(playbackSource.revision) || 0),
      serverTime: Math.max(0, Number(playbackSource.serverTime) || 0),
      observedAt: Date.now(),
      coordinationOnly: playbackSource.coordinationOnly === true,
      requiresLocalPlaybackConsent:
        playbackSource.requiresLocalPlaybackConsent !== false,
    },
    itemCount: Math.max(0, Number(space?.itemCount) || items.length),
    scheduleCount: Math.max(
      0,
      Number(space?.scheduleCount) || schedules.length,
    ),
    scheduledAt: nextSchedule
      ? new Date(nextSchedule.startsAt).toISOString()
      : "",
    items,
    schedules,
    roles: Array.isArray(source.roles)
      ? source.roles
          .map((role) => ({
            id: sanitizePresenceText(role?.id, "", 40),
            account: sanitizePresenceText(role?.account, "", 80),
            role: role?.role === "moderator" ? "moderator" : "",
          }))
          .filter((role) => role.id && role.account && role.role)
          .slice(0, 20)
      : [],
  };
}

function normalizeMediaSpaces(value) {
  return (Array.isArray(value?.spaces) ? value.spaces : [])
    .map((space) => normalizeMediaRoom(space))
    .filter((space) => space.id)
    .slice(0, 50);
}

function normalizeFederatedInstances(value) {
  return (Array.isArray(value?.instances) ? value.instances : [])
    .map((item) => ({
      id: /^[a-f0-9]{24}$/.test(String(item?.id || ""))
        ? String(item.id)
        : "",
      label: sanitizePresenceText(item?.label, "ForkMesh instance", 80),
      origin: safeHTTPURL(item?.origin),
      approved: item?.approved === true,
      health: ["online", "offline", "awaiting_verified_health"].includes(
        item?.health,
      )
        ? item.health
        : "awaiting_verified_health",
      online: item?.online === true,
      healthEvidence: sanitizePresenceText(
        item?.healthEvidence,
        "no-fresh-verified-node-health",
        80,
      ),
    }))
    .filter(
      (item) =>
        item.id &&
        item.approved &&
        item.origin?.startsWith("https://"),
    )
    .slice(0, 100);
}

function mediaProviderForURL(value) {
  const href = safeHTTPURL(value);
  if (!href?.startsWith("https://")) return "";
  const url = new URL(href);
  const host = url.hostname.toLowerCase();
  const matches = (domain) => host === domain || host.endsWith(`.${domain}`);
  if (matches("somafm.com")) return "somafm";
  if (
    matches("youtube.com") ||
    matches("youtu.be") ||
    matches("youtube-nocookie.com")
  ) {
    return "youtube";
  }
  if (matches("vimeo.com")) return "vimeo";
  if (matches("soundcloud.com")) return "soundcloud";
  if (matches("twitch.tv")) return "twitch";
  if (matches("archive.org")) return "internet-archive";
  if (/^\/(?:w\/|videos\/(?:watch|embed)\/)[A-Za-z0-9_-]{4,128}\/?$/.test(url.pathname)) {
    return "peertube";
  }
  return "";
}

function formatBytes(value) {
  const bytes = Math.max(0, Number(value) || 0);
  if (bytes < 1024) return `${Math.round(bytes)} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let scaled = bytes / 1024;
  let index = 0;
  while (scaled >= 1024 && index < units.length - 1) {
    scaled /= 1024;
    index += 1;
  }
  return `${scaled >= 10 ? scaled.toFixed(0) : scaled.toFixed(1)} ${units[index]}`;
}

function fileLanguage(name) {
  const extension = String(name || "").toLowerCase().split(".").pop();
  return {
    js: "JavaScript",
    mjs: "JavaScript",
    ts: "TypeScript",
    tsx: "TypeScript",
    py: "Python",
    cpp: "C++",
    cc: "C++",
    c: "C",
    h: "C/C++",
    rs: "Rust",
    go: "Go",
    dart: "Dart",
    java: "Java",
    kt: "Kotlin",
    rb: "Ruby",
    php: "PHP",
    css: "CSS",
    html: "HTML",
    sql: "SQL",
    md: "Markdown",
  }[extension] || "Other";
}

function repositoryNodeRole(path, type) {
  if (type === "directory") return "directory";
  const value = String(path || "").toLowerCase();
  if (/(^|\/)(package\.json|package-lock\.json|pyproject\.toml|requirements[^/]*|cargo\.toml|go\.mod|cmakelists\.txt)$/.test(value)) {
    return "package";
  }
  if (/(^|\/)(model|models|schema|schemas|migration|migrations|entities)(\/|\.|$)|\.sql$/.test(value)) {
    return "database-model";
  }
  if (/(^|\/)(api|routes|controllers|endpoints)(\/|\.|$)/.test(value)) {
    return "api";
  }
  if (/(^|\/)(service|services|worker|workers)(\/|\.|$)/.test(value)) {
    return "service";
  }
  if (/(^|\/)(test|tests|spec|specs)(\/|\.|$)/.test(value)) {
    return "test";
  }
  return "module";
}

function modificationBand(changeCount, modified) {
  const count = Math.max(0, Number(changeCount) || 0);
  if (count >= 10) return "high";
  if (count >= 3) return "medium";
  if (count >= 1) return "low";
  const instant = new Date(modified || "");
  if (!Number.isNaN(instant.getTime())) return "low";
  return "unknown";
}

function normalizeTreeEntries(payload, parent = "") {
  const entries = Array.isArray(payload?.entries) ? payload.entries : [];
  const analysisCommit = String(
    payload?.analysis?.commit || payload?.commit || "",
  ).toLowerCase();
  const normalized = entries.slice(0, 160).map((entry, index) => {
    const name = sanitizePresenceText(entry?.name, `item-${index + 1}`, 100);
    const type = entry?.type === "tree" ? "directory" : "file";
    const constructedPath = parent ? `${parent}/${name}` : name;
    const reportedPath = String(entry?.path || "")
      .replaceAll("\\", "/")
      .replace(/^\/+/, "");
    const path =
      reportedPath &&
      !reportedPath.split("/").includes("..") &&
      (!parent || reportedPath.startsWith(`${parent}/`))
        ? reportedPath.slice(0, 500)
        : constructedPath;
    const securityValues = new Set([
      "unavailable",
      "no-finding",
      "finding",
      "reviewed",
    ]);
    const rawCoverage = entry?.coverage;
    const coverage =
      rawCoverage === null ||
      rawCoverage === undefined ||
      rawCoverage === ""
        ? Number.NaN
        : Number(rawCoverage);
    const entryAnalysisCommit = String(
      entry?.analysisCommit || analysisCommit,
    ).toLowerCase();
    const analysisMatches =
      /^[0-9a-f]{40,64}$/.test(analysisCommit) &&
      entryAnalysisCommit === analysisCommit;
    const dependencyDepth = analysisMatches
      ? Math.max(0, Math.min(12, Number(entry?.dependencyDepth) || 0))
      : 0;
    const dependencies =
      analysisMatches && Array.isArray(entry?.dependencies)
        ? entry.dependencies
            .map((dependency) =>
              String(dependency || "").replaceAll("\\", "/").slice(0, 500),
            )
            .filter(
              (dependency) =>
                dependency &&
                !dependency.startsWith("/") &&
                !dependency.split("/").includes(".."),
            )
            .slice(0, 80)
        : [];
    const commitMatchedCoverage =
      analysisMatches &&
      Number.isFinite(coverage) &&
      coverage >= 0 &&
      coverage <= 100
        ? coverage
        : null;
    return {
      name,
      path,
      type,
      size: Math.max(0, Number(entry?.size) || 0),
      language: type === "directory" ? "Directory" : fileLanguage(name),
      contributor: sanitizePresenceText(entry?.author, "Unknown", 40),
      modified: String(entry?.date || ""),
      changeCount: Math.max(0, Math.min(20, Number(entry?.changeCount) || 0)),
      frequency: modificationBand(entry?.changeCount, entry?.date),
      role: repositoryNodeRole(path, type),
      dependencyDepth,
      dependencies,
      security: securityValues.has(entry?.security)
        ? entry.security
        : "unavailable",
      coverage: commitMatchedCoverage,
      analysisCommit: entryAnalysisCommit,
    };
  });
  const stems = new Map();
  normalized.forEach((entry) => {
    const stem = entry.name.toLowerCase().replace(/\.[^.]+$/, "");
    stems.set(stem, (stems.get(stem) || 0) + 1);
  });
  return normalized.map((entry) => ({
    ...entry,
    redundantCandidate:
      (stems.get(entry.name.toLowerCase().replace(/\.[^.]+$/, "")) || 0) > 1,
  }));
}

function mergeCommitMatchedSecurity(entries, scanRecord, commit, triage = null) {
  const rich =
    triage &&
    Array.isArray(triage.findings) &&
    /^[0-9a-f]{40,64}$/.test(String(triage.commitHash || "").toLowerCase())
      ? {
          repository: { commit: triage.commitHash },
          findings: triage.findings,
        }
      : scanRecord?.rich;
  const clipboard = scanRecord?.clipboard || {};
  const scanCommit = String(
    rich?.repository?.commit || clipboard?.commitHash || "",
  ).toLowerCase();
  const expectedCommit = String(commit || "").toLowerCase();
  if (
    !/^[0-9a-f]{40,64}$/.test(expectedCommit) ||
    scanCommit !== expectedCommit ||
    !rich ||
    !Array.isArray(rich.findings)
  ) {
    return entries.map((entry) => ({
      ...entry,
      security: "unavailable",
      securityCommit: scanCommit,
    }));
  }
  const stateByPath = new Map();
  rich.findings.slice(0, 500).forEach((finding) => {
    const path = String(finding?.path || "")
      .replaceAll("\\", "/")
      .replace(/^\/+/, "");
    if (!path || path.split("/").includes("..")) return;
    const next =
      finding?.falsePositiveStatus === "dismissed" ? "reviewed" : "finding";
    if (stateByPath.get(path) !== "finding") stateByPath.set(path, next);
  });
  return entries.map((entry) => {
    let state = stateByPath.get(entry.path) || "no-finding";
    if (entry.type === "directory") {
      const prefix = `${entry.path}/`;
      const descendantStates = [...stateByPath.entries()]
        .filter(([path]) => path.startsWith(prefix))
        .map(([, value]) => value);
      if (descendantStates.includes("finding")) state = "finding";
      else if (descendantStates.includes("reviewed")) state = "reviewed";
    }
    return {
      ...entry,
      security: state,
      securityCommit: scanCommit,
    };
  });
}

function decodeWorkshopBlob(blob) {
  if (!blob || blob.ok === false) return "";
  const content = String(blob.content || blob.text || "");
  if (!content) return "";
  if (blob.encoding !== "base64") return content.slice(0, 240000);
  try {
    const bytes = Uint8Array.from(atob(content), (char) => char.charCodeAt(0));
    return new TextDecoder("utf-8", { fatal: false })
      .decode(bytes)
      .slice(0, 240000);
  } catch (_) {
    return "";
  }
}

function workshopTextCandidate(entry) {
  if (entry.type !== "file" || entry.size > 512000) return false;
  return /\.(c|cc|cpp|cs|dart|go|h|hpp|java|js|jsx|kt|md|php|py|rb|rs|sql|toml|ts|tsx|yaml|yml|json)$/i.test(
    entry.path,
  );
}

function workshopRunId() {
  if (crypto.randomUUID) return crypto.randomUUID();
  return Array.from(crypto.getRandomValues(new Uint8Array(16)), (value) =>
    value.toString(16).padStart(2, "0"),
  ).join("");
}

function findDirectedCycles(edges) {
  const graph = new Map();
  edges.forEach(({ from, to }) => {
    graph.set(from, [...(graph.get(from) || []), to]);
  });
  const cycles = [];
  const visiting = new Set();
  const visited = new Set();
  const walk = (node, trail) => {
    if (visiting.has(node)) {
      const start = trail.indexOf(node);
      cycles.push([...trail.slice(Math.max(0, start)), node]);
      return;
    }
    if (visited.has(node)) return;
    visiting.add(node);
    (graph.get(node) || []).forEach((next) => walk(next, [...trail, node]));
    visiting.delete(node);
    visited.add(node);
  };
  [...graph.keys()].forEach((node) => walk(node, []));
  return cycles.slice(0, 12);
}

function analyzeDatabaseModels(files) {
  const models = [];
  const edges = [];
  files.forEach(({ path, text }) => {
    const modelPath = /(model|schema|migration|entity|database|\.sql)/i.test(path);
    if (!modelPath) return;
    const definitions = [
      ...text.matchAll(
        /\b(?:class|interface|type|model|entity)\s+([A-Z][A-Za-z0-9_]*)/g,
      ),
      ...text.matchAll(
        /\bCREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?["`[]?([A-Za-z_][A-Za-z0-9_]*)/gi,
      ),
      ...text.matchAll(
        /\b(?:sequelize\.define|model)\s*\(\s*["'`]([A-Za-z_][A-Za-z0-9_]*)/gi,
      ),
    ];
    definitions.forEach((match) => {
      const name = sanitizePresenceText(match[1], "Model", 80);
      if (!models.some((model) => model.name === name && model.path === path)) {
        models.push({ name, path, fields: new Set(), uses: [] });
      }
    });
  });
  models.forEach((model) => {
    const own = files.find((file) => file.path === model.path)?.text || "";
    const fieldMatches = own.matchAll(
      /^\s*(?:["'`]?)([A-Za-z_][A-Za-z0-9_]*)(?:["'`]?)\s*(?::|=|\b(?:INTEGER|TEXT|VARCHAR|BOOLEAN|DATE|UUID|JSON)\b)/gim,
    );
    for (const match of fieldMatches) model.fields.add(match[1]);
    files.forEach(({ path, text }) => {
      const uses = text.match(
        new RegExp(`\\b${model.name.replace(/[^A-Za-z0-9_]/g, "")}\\b`, "g"),
      );
      const count = Math.max(
        0,
        Number(uses?.length || 0) - (path === model.path ? 1 : 0),
      );
      if (count) model.uses.push({ path, count });
    });
    const relationshipPatterns = [
      /\b(?:ForeignKey|OneToOneField|ManyToManyField|relationship|belongsTo|hasMany|references)\s*\(\s*["'`]?([A-Z][A-Za-z0-9_]*)/g,
      /\bREFERENCES\s+["`[]?([A-Za-z_][A-Za-z0-9_]*)/gi,
    ];
    relationshipPatterns.forEach((pattern) => {
      for (const match of own.matchAll(pattern)) {
        edges.push({
          from: model.name,
          to: sanitizePresenceText(match[1], "RelatedModel", 80),
          path: model.path,
        });
      }
    });
  });
  const concepts = new Map();
  models.forEach((model) => {
    const concept = model.name.toLowerCase().replace(/[_-]|model|entity/g, "");
    concepts.set(concept, [...(concepts.get(concept) || []), model]);
  });
  const duplicateConcepts = [...concepts.values()].filter(
    (group) => group.length > 1,
  );
  const fields = new Map();
  models.forEach((model) => {
    model.fields.forEach((field) => {
      fields.set(field, [...(fields.get(field) || []), model.name]);
    });
  });
  const repeatedFields = [...fields.entries()]
    .filter(([, owners]) => owners.length > 1)
    .slice(0, 20);
  return {
    models: models.slice(0, 80),
    edges: edges.slice(0, 160),
    cycles: findDirectedCycles(edges),
    duplicateConcepts,
    repeatedFields,
  };
}

function analyzeWorkshopSnapshot(type, entries, files, stats = {}) {
  const references = new Set();
  const findings = [];
  const recommendations = [];
  const nodes = [];
  const edges = [];
  const addReference = (path) => {
    if (path) references.add(String(path).slice(0, 180));
  };
  if (type === "Database-model analysis") {
    const report = analyzeDatabaseModels(files);
    report.models.forEach((model) => {
      nodes.push({
        label: model.name,
        kind: "Model",
        detail: `${model.fields.size} fields · ${model.uses.length} use sites`,
        definitionPath: model.path,
        useSites: model.uses
          .slice(0, 20)
          .map((use) => ({ path: use.path, count: use.count })),
      });
      addReference(model.path);
      model.uses.slice(0, 6).forEach((use) => addReference(use.path));
    });
    report.edges.forEach((edge) => {
      edges.push(edge);
      addReference(edge.path);
    });
    report.duplicateConcepts.forEach((group) => {
      findings.push(
        `Potentially duplicated concept: ${group
          .map((model) => model.name)
          .join(", ")}`,
      );
    });
    report.repeatedFields.forEach(([field, owners]) => {
      findings.push(
        `Potentially redundant field “${field}” appears in ${owners.join(", ")}`,
      );
    });
    report.cycles.forEach((cycle) => {
      findings.push(`Possible circular relationship: ${cycle.join(" → ")}`);
    });
    recommendations.push(
      `${report.models.length} candidate model definitions and ${report.edges.length} relationship edges were mapped.`,
      "Verify inferred relationships, repeated concepts, tables, and fields with a maintainer before changing schema.",
      "Use the supporting paths and use-site counts to scope human review.",
    );
  } else if (type === "Dependency mapping") {
    files.forEach(({ path, text }) => {
      const imports = [
        ...text.matchAll(
          /\b(?:import\s+(?:[^"'`]+?\s+from\s+)?|require\s*\(|#include\s*[<"]|use\s+)(["'`<]?)([^"'`>;\n)]+)\1/g,
        ),
      ].slice(0, 80);
      if (imports.length || repositoryNodeRole(path, "file") === "package") {
        nodes.push({
          label: path.split("/").pop(),
          kind: "Dependency source",
          detail: `${imports.length} inferred edges`,
        });
        addReference(path);
      }
      imports.forEach((match) =>
        edges.push({
          from: path,
          to: String(match[2] || "").trim().slice(0, 100),
          path,
        }),
      );
    });
    findings.push(`${edges.length} candidate import or manifest relationships were found.`);
    recommendations.push(
      "Verify inferred imports against lockfiles and build tooling before treating them as resolved dependency edges.",
    );
  } else if (type === "Redundancy detection") {
    const stems = new Map();
    entries.forEach((entry) => {
      const stem = entry.name.toLowerCase().replace(/\.[^.]+$/, "");
      stems.set(stem, [...(stems.get(stem) || []), entry.path]);
    });
    [...stems.entries()]
      .filter(([, paths]) => paths.length > 1)
      .forEach(([stem, paths]) => {
        findings.push(`Potential duplicate “${stem}”: ${paths.join(", ")}`);
        paths.forEach(addReference);
      });
    recommendations.push(
      "Matching names are candidates only; compare responsibilities, exports, and call sites before removing code.",
    );
  } else if (type === "Dead-code detection") {
    const definitions = [];
    files.forEach(({ path, text }) => {
      for (const match of text.matchAll(
        /\b(?:function|class|def|const|let|var|struct)\s+([A-Za-z_][A-Za-z0-9_]*)/g,
      )) {
        definitions.push({ name: match[1], path });
      }
    });
    definitions.forEach((definition) => {
      const uses = files.reduce(
        (total, file) =>
          total +
          (file.text.match(
            new RegExp(`\\b${definition.name}\\b`, "g"),
          )?.length || 0),
        0,
      );
      if (uses <= 1) {
        findings.push(`Possible unreferenced symbol: ${definition.name}`);
        addReference(definition.path);
      }
    });
    recommendations.push(
      "Reference counts cannot prove dead code; confirm dynamic loading, reflection, generated use, and build targets.",
    );
  } else if (type === "Security analysis") {
    const rules = [
      ["credential-like assignment", /\b(password|secret|api[_-]?key)\s*[:=]\s*["'`][^"'`\n]{6,}/i],
      ["dynamic code execution", /\b(eval|exec)\s*\(/],
      ["shell invocation", /\b(system|popen|child_process|QProcess)\b/],
      ["weak digest", /\b(md5|sha1)\b/i],
    ];
    files.forEach(({ path, text }) => {
      rules.forEach(([rule, pattern]) => {
        if (pattern.test(text)) {
          findings.push(`${rule} candidate in ${path}`);
          addReference(path);
        }
      });
    });
    recommendations.push(
      "Public output names only the rule and authorized path; inspect redacted details in the security-review workflow.",
      "Run dependency, secret-detection, and static-analysis tools on the same commit before triage.",
    );
  } else if (type === "Test-coverage analysis") {
    const tests = entries.filter((entry) =>
      /(^|\/)(test|tests|spec|specs)(\/|\.|$)/i.test(entry.path),
    );
    const coverageFiles = entries.filter((entry) => entry.type === "file");
    const sources = coverageFiles.filter((entry) => !tests.includes(entry));
    const coverageRows = coverageFiles.map((entry) => {
      const value =
        typeof entry.coverage === "number" &&
        Number.isFinite(entry.coverage) &&
        entry.coverage >= 0 &&
        entry.coverage <= 100
          ? entry.coverage
          : null;
      const state =
        value === null
          ? "unknown"
          : value >= 80
            ? "covered"
            : value > 0
              ? "partial"
              : "uncovered";
      return { entry, value, state };
    });
    const measured = coverageRows.filter((row) => row.value !== null);
    const stateCounts = {
      covered: coverageRows.filter((row) => row.state === "covered").length,
      partial: coverageRows.filter((row) => row.state === "partial").length,
      uncovered: coverageRows.filter((row) => row.state === "uncovered").length,
      unknown: coverageRows.filter((row) => row.state === "unknown").length,
    };
    tests.forEach((entry) => addReference(entry.path));
    if (!measured.length) {
      findings.push(
        `${tests.length} test paths and ${sources.length} non-test paths were mapped; no line-coverage claim is made without an artifact.`,
      );
      recommendations.push(
        "Upload a commit-matched coverage artifact to populate per-file coverage and uncovered filters.",
      );
    } else {
      measured
        .sort((left, right) => left.value - right.value)
        .slice(0, 40)
        .forEach((row) => {
          addReference(row.entry.path);
          nodes.push({
            label: row.entry.path,
            kind: `Coverage: ${row.state}`,
            detail: `${row.value}% at the analyzed commit`,
          });
        });
      coverageRows
        .filter((row) => row.state === "unknown")
        .slice(0, 12)
        .forEach((row) =>
          nodes.push({
            label: row.entry.path,
            kind: "Coverage: unknown",
            detail: "No commit-matched per-file value",
          }),
        );
      findings.push(
        `${measured.length} of ${coverageFiles.length} mapped files have commit-matched coverage values.`,
        `Artifact-reported file states: ${stateCounts.covered} covered (80%+), ${stateCounts.partial} partial, ${stateCounts.uncovered} uncovered, and ${stateCounts.unknown} unknown.`,
      );
      if (stateCounts.partial || stateCounts.uncovered) {
        recommendations.push(
          "Review the referenced partial and uncovered files alongside their tests before prioritizing additional coverage.",
        );
      }
      if (stateCounts.unknown) {
        recommendations.push(
          `Publish commit-matched coverage for the ${stateCounts.unknown} unknown file paths before drawing repository-wide conclusions.`,
        );
      }
      recommendations.push(
        "Treat artifact-reported percentages as test-execution evidence for this commit, not proof of behavioral correctness.",
      );
    }
  } else if (type === "Documentation analysis") {
    const docs = entries.filter((entry) =>
      /(readme|docs|contributing|changelog|\.md$)/i.test(entry.path),
    );
    docs.forEach((entry) => addReference(entry.path));
    findings.push(`${docs.length} documentation candidates were mapped.`);
    recommendations.push(
      "Compare documented commands, APIs, and configuration names against current definitions and tests.",
    );
  } else if (type === "Performance analysis") {
    entries
      .filter((entry) => entry.type === "file")
      .sort((a, b) => b.size - a.size)
      .slice(0, 12)
      .forEach((entry) => {
        findings.push(`Large-file investigation candidate: ${entry.path}`);
        addReference(entry.path);
      });
    files.forEach(({ path, text }) => {
      if (/\bfor\b[\s\S]{0,200}\bfor\b/.test(text)) {
        findings.push(`Nested-loop review candidate: ${path}`);
        addReference(path);
      }
    });
    recommendations.push(
      "File size and syntax are not runtime profiles; benchmark representative workloads before optimizing.",
    );
  } else if (type === "License compatibility analysis") {
    entries
      .filter((entry) =>
        /(license|copying|notice|package|lock|pyproject|cargo)/i.test(entry.path),
      )
      .forEach((entry) => addReference(entry.path));
    findings.push(`${references.size} license or dependency declaration paths were found.`);
    recommendations.push(
      "Resolve direct and transitive licenses against project policy; this recommendation is not legal advice.",
    );
  } else {
    const directories = entries.filter((entry) => entry.type === "directory");
    directories.forEach((entry) => {
      nodes.push({
        label: entry.path,
        kind: "Boundary",
        detail: repositoryNodeRole(entry.path, entry.type),
      });
      addReference(entry.path);
    });
    const roles = new Map();
    entries.forEach((entry) =>
      roles.set(entry.role, (roles.get(entry.role) || 0) + 1),
    );
    findings.push(
      `Architecture layers: ${[...roles.entries()]
        .map(([role, count]) => `${role} ${count}`)
        .join(", ")}`,
    );
    recommendations.push(
      "Verify inferred directory boundaries against imports, build targets, deployed services, APIs, and database ownership.",
    );
  }
  if (!nodes.length) {
    entries.slice(0, 18).forEach((entry) =>
      nodes.push({
        label: entry.path,
        kind: entry.role,
        detail: entry.type,
      }),
    );
  }
  return {
    nodes: nodes.slice(0, 80),
    edges: edges.slice(0, 160),
    findings: findings.slice(0, 80),
    recommendations: recommendations.slice(0, 20),
    references: [...references].slice(0, 80),
    modelUseSites:
      type === "Database-model analysis"
        ? nodes.map((node) => ({
            name: node.label,
            definitionPath: node.definitionPath,
            useSites: node.useSites || [],
          }))
        : [],
    contributorCount: Number(
      stats.contributorCount || stats.contributors?.length || 0,
    ),
  };
}

function cleanRepositories(payload) {
  const items =
    payload?.repositories ||
    payload?.items ||
    payload?.repos ||
    payload?.data ||
    [];
  if (!Array.isArray(items)) return [];
  return items
    .filter((repo) => repo && (repo.owner || repo.name))
    .map((repo) => {
      const commit = String(repo.commit || "").trim().toLowerCase();
      const stateHash = String(repo.stateHash || "").trim().toLowerCase();
      const pullCount = Number(repo.pullCount);
      return {
        owner: sanitizePresenceText(repo.owner, "external", 40),
        name: sanitizePresenceText(repo.name, "repository", 60),
        description: String(repo.description || "").slice(0, 180),
        liveHost: Boolean(repo.liveHost ?? repo.cloneOnline ?? repo.online),
        source: String(repo.source || "external"),
        language: String(
          repo.language ||
            repo.primaryLanguage ||
            Object.keys(repo.languages || {})[0] ||
            "",
        ).slice(0, 30),
        isPrivate: Boolean(
          repo.private || repo.isPrivate || repo.visibility === "private",
        ),
        commit: /^[0-9a-f]{40,64}$/.test(commit) ? commit : "",
        stateHash: /^[0-9a-f]{64}$/.test(stateHash) ? stateHash : "",
        rootCommit: immutableGitOid(repo.rootCommit),
        pullCount:
          Number.isSafeInteger(pullCount) &&
          pullCount >= 0 &&
          pullCount <= 10_000_000
            ? pullCount
            : null,
        mirrorCount: Number(repo.mirrorCount || repo.mirrors || 0),
        cloneUrl: String(repo.cloneUrl || "").slice(0, 500),
        updatedAt: Number(repo.updatedAt || repo.lastSync || 0) || 0,
        status: String(repo.status || "").slice(0, 40),
        externalUrl: String(
          repo.externalUrl || repo.sourceUrl || repo.url || "",
        ).slice(0, 500),
        archived: Boolean(repo.archived),
        license: String(repo.license?.name || repo.license || "").slice(0, 80),
        topics: Array.isArray(repo.topics)
          ? repo.topics
              .map((topic) => String(topic).slice(0, 30))
              .slice(0, 12)
          : [],
      };
    })
    .slice(0, 200);
}

function repositoryBlobText(blob) {
  const content = String(blob?.content ?? blob?.text ?? "");
  if (String(blob?.encoding || "").toLowerCase() !== "base64") {
    return content;
  }
  try {
    const bytes = Uint8Array.from(atob(content), (character) =>
      character.charCodeAt(0),
    );
    return new TextDecoder("utf-8", { fatal: false }).decode(bytes);
  } catch (_) {
    return "";
  }
}

function reconcileRepositoryAliases(repositories, mirrorCatalogs) {
  const source = Array.isArray(repositories) ? repositories : [];
  const catalogs = Array.isArray(mirrorCatalogs) ? mirrorCatalogs : [];
  const consumed = new Set();
  const aliases = [];
  catalogs.forEach((catalog) => {
    const owner = sanitizePresenceText(
      catalog?.requestedOwner || catalog?.owner,
      "",
      40,
    );
    const repo = sanitizePresenceText(
      catalog?.requestedRepo || catalog?.repo,
      "",
      60,
    );
    if (!owner || !repo) return;
    const mirrors = Array.isArray(catalog?.mirrors)
      ? catalog.mirrors.slice(0, 100)
      : [];
    const nodes = new Set(
      mirrors
        .map((mirror) =>
          sanitizePresenceText(mirror?.node || mirror?.owner, "", 40).toLowerCase(),
        )
        .filter(Boolean),
    );
    const candidates = source
      .map((record, index) => ({ record, index }))
      .filter(
        ({ record }) =>
          !record.isPrivate &&
          record.name.toLowerCase() === repo.toLowerCase() &&
          nodes.has(record.owner.toLowerCase()),
      );
    if (!candidates.length) return;
    candidates.forEach(({ index }) => consumed.add(index));
    const healthy = mirrors.filter(
      (mirror) =>
        String(mirror?.status || "").toLowerCase() === "online" &&
        mirror?.cloneAvailable === true &&
        String(mirror?.integrity || "").toLowerCase() === "ok" &&
        mirror?.behind !== true,
    );
    const attestedMirrorCommits = new Map();
    healthy.forEach((mirror) => {
      const node = sanitizePresenceText(
        mirror?.node || mirror?.owner,
        "",
        40,
      ).toLowerCase();
      const commit = immutableGitOid(mirror?.commit);
      if (!node || !commit) return;
      if (!attestedMirrorCommits.has(node)) {
        attestedMirrorCommits.set(node, new Set());
      }
      attestedMirrorCommits.get(node).add(commit);
    });
    // A stale/offline repository listing must not erase the immutable pin
    // unanimously reported by the mirrors that can actually serve the clone.
    // Conversely, duplicate or disagreeing eligible reports remain ambiguous
    // and fail closed instead of selecting a majority or freshest timestamp.
    const attestedCandidates = candidates.filter(({ record }) => {
      const reportedCommits = attestedMirrorCommits.get(
        record.owner.toLowerCase(),
      );
      const commit = immutableGitOid(record.commit);
      return (
        commit &&
        reportedCommits?.size === 1 &&
        reportedCommits.has(commit)
      );
    });
    const preferredNode = String(healthy[0]?.node || "").toLowerCase();
    const preferred =
      attestedCandidates.find(
        ({ record }) => record.owner.toLowerCase() === preferredNode,
      )?.record ||
      attestedCandidates
        .map(({ record }) => record)
        .sort((left, right) => right.updatedAt - left.updatedAt)[0] ||
      candidates
        .map(({ record }) => record)
        .sort((left, right) => right.updatedAt - left.updatedAt)[0];
    const commits = new Set(
      attestedCandidates
        .map(({ record }) => immutableGitOid(record.commit))
        .filter(Boolean),
    );
    const stateHashes = new Set(
      attestedCandidates
        .map(({ record }) => String(record.stateHash || "").toLowerCase())
        .filter((value) => /^[0-9a-f]{64}$/.test(value)),
    );
    const reportedPullCounts = mirrors
      .map((mirror) => Number(mirror?.pullCount))
      .filter(
        (value) =>
          Number.isSafeInteger(value) &&
          value >= 0 &&
          value <= 10_000_000,
      );
    const pullCount =
      reportedPullCounts.length === mirrors.length &&
      new Set(reportedPullCounts).size === 1
        ? reportedPullCounts[0]
        : null;
    aliases.push({
      ...preferred,
      owner,
      name: repo,
      servingOwner: preferred.owner,
      servingName: preferred.name,
      source: "organization-alias",
      liveHost: healthy.length > 0,
      mirrorCount: mirrors.length,
      pullCount,
      commit: commits.size === 1 ? [...commits][0] : "",
      stateHash: stateHashes.size === 1 ? [...stateHashes][0] : "",
      mirrorAliases: candidates.map(({ record }) => ({
        owner: record.owner,
        name: record.name,
      })),
    });
  });
  return [
    ...aliases,
    ...source.filter((_record, index) => !consumed.has(index)),
  ].slice(0, 200);
}

function normalizeCommunityEvents(payload, now = Date.now()) {
  const items = Array.isArray(payload?.events) ? payload.events : [];
  return items
    .slice(0, 64)
    .map((item) => {
      const startsAt = String(item?.startsAt || "");
      const endsAt = String(item?.endsAt || "");
      const start = Date.parse(startsAt);
      const end = Date.parse(endsAt);
      if (
        !/Z$/.test(startsAt) ||
        !/Z$/.test(endsAt) ||
        !Number.isFinite(start) ||
        !Number.isFinite(end) ||
        end <= Math.max(start, now)
      ) {
        return null;
      }
      const id = String(item?.id || "").toLowerCase();
      if (!/^[a-f0-9-]{16,64}$/.test(id)) return null;
      return {
        id,
        type: sanitizePresenceText(item?.type, "Community", 32),
        title: sanitizePresenceText(item?.title, "Community event", 160),
        description: sanitizePresenceText(item?.description, "", 500),
        destination: sanitizePresenceText(
          item?.destination,
          "Town Square",
          100,
        ),
        startsAt: new Date(start).toISOString(),
        endsAt: new Date(end).toISOString(),
      };
    })
    .filter(Boolean)
    .sort((left, right) => Date.parse(left.startsAt) - Date.parse(right.startsAt));
}

function normalizeWorldNotifications(payload) {
  const items = Array.isArray(payload?.notifications)
    ? payload.notifications
    : [];
  return items
    .slice(0, 100)
    .map((item) => {
      const id = String(item?.id || "");
      if (!/^[A-Za-z0-9_-]{16,160}$/.test(id)) return null;
      const title = sanitizeNotificationText(item?.title, "Notification", 160);
      if (!title) return null;
      return {
        id,
        kind: sanitizeNotificationText(item?.kind, "Update", 40),
        title,
        body: sanitizeNotificationText(item?.body, "", 500),
        href: safeNotificationURL(item?.href),
        ts: Math.max(0, Number(item?.ts) || 0),
        readAt: Math.max(0, Number(item?.readAt) || 0),
      };
    })
    .filter(Boolean)
    .sort((left, right) => right.ts - left.ts);
}

function liveNodeRecords(network, mirrorCatalogs = []) {
  return buildLiveMirrorNodes(network, mirrorCatalogs);
}

const LOCAL_LIVE_LANDMARKS = new Set([
  "information",
  "neighborhood",
  "broadcast",
  "support",
]);

const LANDMARK_CONSTRUCTION_REASONS = Object.freeze({
  fountain:
    "A configured public Solana reward-pool address has not been verified in this session.",
  repositories:
    "The live repository catalog has not been verified in this session.",
  routing:
    "No healthy, integrity-checked, clone-ready mirror route has been verified in this session.",
  organizations:
    "The organization directory integration has not been verified in this session.",
  fediverse:
    "The Mastodon and Lemmy directory integration has not been verified in this session.",
  security:
    "No completed commit-scoped public security scan has been verified.",
  launchpad:
    "The interactive destination renderer has not finished loading.",
  events:
    "The UTC event service has not been verified in this session.",
  workshops:
    "No live authorized repository is available for a code workshop.",
});

function initialLandmarkCapabilities() {
  return Object.fromEntries(
    LANDMARKS.map((landmark) => [
      landmark.id,
      {
        live: LOCAL_LIVE_LANDMARKS.has(landmark.id),
        reason:
          LANDMARK_CONSTRUCTION_REASONS[landmark.id] ||
          "This integration has not been verified in this session.",
      },
    ]),
  );
}

function hasRepositoryCatalogSchema(payload) {
  return ["repositories", "items", "repos", "data"].some((field) =>
    Array.isArray(payload?.[field]),
  );
}

function hasFediverseDirectorySchema(payload) {
  return (
    Array.isArray(payload?.mastodon) &&
    Array.isArray(payload?.lemmy)
  );
}

function hasCompletedSecurityScan(scan) {
  const status = String(scan?.status || "").trim().toLowerCase();
  const commit = String(scan?.commitHash || scan?.commit || "")
    .trim()
    .toLowerCase();
  const scannedAt = Date.parse(String(scan?.scannedAt || scan?.timestamp || ""));
  return (
    !["", "scan unavailable", "scan failed", "scan outdated"].includes(status) &&
    /^[0-9a-f]{40,64}$/.test(commit) &&
    Number.isFinite(scannedAt)
  );
}

function constructionMarkerHTML(id, capability, className = "") {
  const live = capability?.live === true;
  const reason =
    String(capability?.reason || "").trim() ||
    "This integration has not been verified in this session.";
  return `<span
    class="world-construction-mark ${escapeHTML(className)}"
    data-world-construction-marker="${escapeHTML(id)}"
    role="img"
    aria-label="Under construction: ${escapeHTML(reason)}"
    title="Under construction: ${escapeHTML(reason)}"
    ${live ? "hidden" : ""}
  ><span aria-hidden="true">🚧</span></span>`;
}

function accountBadgeCopy(identity, settings) {
  const availability = AVAILABILITY_OPTIONS.find(
    (option) => option.id === settings.availability,
  )?.label;
  const pieces = [
    `${ACCOUNT_STATUS_ICONS[identity.accountStatus] || "○"} ${
      identity.accountStatus || "Guest"
    }`,
  ];
  if (
    settings.privacy.activity &&
    (settings.privacy.inactivity || !["inactive", "recent"].includes(settings.availability))
  ) {
    pieces.push(availability || "Online");
  }
  if (settings.privacy.localTime) {
    pieces.push(
      new Intl.DateTimeFormat(undefined, {
        hour: "numeric",
        minute: "2-digit",
      }).format(new Date()),
    );
  }
  return pieces.join(" · ");
}

function worldTemplate(identity, settings, mode, landmarkCapabilities) {
  const accountSession = readSession();
  const signedInName =
    accountSession?.sessionToken &&
    WORLD_ACCOUNT_NAME_RE.test(
      String(accountSession.nodeName || "").trim().toLowerCase(),
    )
      ? String(accountSession.nodeName).trim().toLowerCase()
      : "";
  const mapItems = LANDMARKS.map(
    (landmark) => `
      <li>
        <button
          type="button"
          class="world-map-button"
          data-world-landmark="${escapeHTML(landmark.id)}"
          style="--map-color:${escapeHTML(landmark.color)}"
          aria-current="${landmark.id === "information" ? "true" : "false"}"
        >
          <span class="world-map-icon" aria-hidden="true">${escapeHTML(landmark.icon)}</span>
          <span class="world-map-label-copy">
            <span>${escapeHTML(landmark.shortLabel)}</span>
            ${constructionMarkerHTML(
              landmark.id,
              landmarkCapabilities?.[landmark.id],
              "world-construction-mark-map",
            )}
          </span>
          <span class="world-map-distance" data-world-distance="${escapeHTML(landmark.id)}">—</span>
        </button>
      </li>`,
  ).join("");

  const themes = THEME_OPTIONS.map(
    (theme) => `
      <button
        type="button"
        class="world-theme-option"
        data-world-theme="${escapeHTML(theme.id)}"
        aria-pressed="${String(settings.theme === theme.id)}"
      >${escapeHTML(theme.label)}</button>`,
  ).join("");

  const privacyOptions = [
    ["name", "Show chosen display name"],
    ["country", "Show approximate country flag"],
    ["browser", "Show browser badge"],
    ["os", "Show operating-system badge"],
    ["activity", "Share generalized activity"],
    ["inactivity", "Show inactive / recently-active state"],
    ["localTime", "Show local time"],
    ["nodes", "Show mirror-operator belt"],
  ]
    .map(
      ([key, label]) => `
        <label class="world-privacy-option">
          <span>${escapeHTML(label)}</span>
          <input
            type="checkbox"
            data-world-privacy="${escapeHTML(key)}"
            ${settings.privacy[key] ? "checked" : ""}
          />
        </label>`,
    )
    .join("");

  const availabilityOptions = AVAILABILITY_OPTIONS.map(
    (option) => `
      <option value="${escapeHTML(option.id)}" ${
        settings.availability === option.id ? "selected" : ""
      }>${escapeHTML(option.label)}</option>`,
  ).join("");

  const activityOptions = ACTIVITY_OPTIONS.map(
    (option) => `
      <option value="${escapeHTML(option.id)}" ${
        settings.activityCategory === option.id ? "selected" : ""
      }>${escapeHTML(option.label)}</option>`,
  ).join("");
  const publicStatus = normalizeWorldStatus(
    settings.statusEmoji,
    settings.statusNote,
  );
  const emojiCategoryOptions = WORLD_EMOJI_CATEGORIES.map(
    (category, index) => `
      <option value="${escapeHTML(category.id)}" ${index === 0 ? "selected" : ""}>
        ${escapeHTML(category.label)}
      </option>`,
  ).join("");
  const emojiCategoryPanels = WORLD_EMOJI_CATEGORIES.map(
    (category, index) => `
      <div
        class="world-emoji-grid"
        data-world-emoji-category-panel="${escapeHTML(category.id)}"
        ${index === 0 ? "" : "hidden"}
      >
        ${category.emoji
          .map(
            (emoji) => `
              <button
                type="button"
                data-world-status-emoji-choice="${escapeHTML(emoji)}"
                aria-label="Use ${escapeHTML(emoji)} as public status"
                title="Use ${escapeHTML(emoji)}"
              >${escapeHTML(emoji)}</button>`,
          )
          .join("")}
      </div>`,
  ).join("");

  return `
    <div class="fm-world ${mode === "dashboard" ? "world-dashboard-embed" : ""}" data-world-root>
      <section
        class="world-information-anchor"
        id="world-information"
        tabindex="-1"
        aria-label="ForkMesh World information"
      >
        <p><strong>ForkMesh World controls</strong></p>
        <button type="button" data-world-landmark="information">Open the information booth</button>
        <a href="/dashboard">Open the standard operations console</a>
      </section>
      <div class="world-canvas-wrap" data-world-canvas-wrap></div>
      <div class="world-label-layer" data-world-label-layer></div>

      <nav class="world-quick-dock" aria-label="Quick world destinations">
        <button type="button" data-world-landmark="information"><span aria-hidden="true">i</span><span>Start</span></button>
        <button type="button" data-world-landmark="repositories"><span aria-hidden="true">{ }</span><span>Code</span></button>
        <button type="button" data-world-landmark="workshops"><span aria-hidden="true">⌘</span><span>Workshops</span></button>
        <a href="/dashboard/chat" data-world-chat-open><span aria-hidden="true">⌁</span><span>Chat</span></a>
        <button type="button" data-world-landmark="support"><span aria-hidden="true">♥</span><span>Support</span></button>
      </nav>

      <div class="world-loading-screen" data-world-loading aria-live="polite">
        <div class="world-loading-lockup">
          <div class="world-loading-mark" aria-hidden="true"></div>
          <strong>Entering ForkMesh World</strong>
          <span data-world-loading-copy>Mapping the Town Square</span>
        </div>
      </div>

      <div class="world-hud">
        <header class="world-topbar">
          <a class="world-brand brand" href="/" aria-label="ForkMesh World home">
            <img class="brand-mark" src="/assets/logo.png" alt="" aria-hidden="true" />
            <span class="world-brand-name">ForkMesh <small>World</small></span>
            <span class="world-live" data-world-presence-state="connecting">
              <span data-world-presence-copy>Joining world</span>
            </span>
          </a>

          <nav class="world-top-actions" aria-label="World tools">
            <button
              class="world-top-link world-notification-button"
              type="button"
              data-world-landmark="events"
              aria-label="Open World notifications"
            >
              <span aria-hidden="true">◫</span><span>Alerts</span>
              <strong data-world-notification-count aria-hidden="true" hidden>0</strong>
            </button>
            <a class="world-top-link" href="/dashboard/chat" data-world-chat-open title="Open chat inside the World">
              <span aria-hidden="true">⌁</span><span>Chat</span>
            </a>
            <button
              class="world-top-link"
              type="button"
              data-world-sound-toggle
              aria-pressed="false"
              title="Enable World sounds"
            >
              <span aria-hidden="true">♪</span><span data-world-sound-label>Sound</span>
            </button>
            <button
              class="world-top-link"
              type="button"
              data-world-account-open
              title="${
                signedInName
                  ? `Account: ${escapeHTML(signedInName)}`
                  : "Log in or create an account inside the World"
              }"
            >
              <span aria-hidden="true">${signedInName ? "✓" : "○"}</span>
              <span>${signedInName ? "Account" : "Login"}</span>
            </button>
            <a class="world-top-link" href="/dashboard" title="Open operations console">
              <span aria-hidden="true">▦</span><span>Console</span>
            </a>
            <button class="world-icon-button" type="button" data-world-settings-open aria-label="World and privacy settings">⚙</button>
            <button
              class="world-shirt-badge"
              type="button"
              data-world-shirt-badge
              data-world-settings-open
              aria-label="Your public avatar badge — open World and privacy settings"
              title="World and privacy settings"
            >
              <span class="world-shirt-flag" data-world-shirt-flag>${escapeHTML(identity.flag)}</span>
              <span class="world-shirt-account" data-world-shirt-account title="${escapeHTML(
                identity.accountStatus,
              )}">${escapeHTML(
                ACCOUNT_STATUS_ICONS[identity.accountStatus] || "○",
              )}</span>
              <span class="world-shirt-tech" data-world-shirt-tech>${escapeHTML(identity.browser)} · ${escapeHTML(identity.os)}</span>
              <span class="world-shirt-name" data-world-shirt-name>${escapeHTML(identity.name)}</span>
            </button>
          </nav>
        </header>

        <div class="world-left-rail">
          <section
            class="world-arrival-card"
            aria-labelledby="world-arrival-title"
            ${introDismissed() ? "hidden" : ""}
          >
            <button
              class="world-arrival-dismiss"
              type="button"
              data-world-arrival-dismiss
              aria-label="Permanently dismiss this introduction"
              title="Do not show this introduction again"
            >×</button>
            <p class="world-eyebrow">YOU ARE HERE / TOWN SQUARE</p>
            <h1 id="world-arrival-title">Code is a place now.</h1>
            <p>
              Walk the mesh, enter repositories, meet operators, and inspect the
              infrastructure behind every metaphor.
            </p>
            <div class="world-arrival-actions">
              <button class="world-primary-action" type="button" data-world-action="tour">Take the tour</button>
              <button class="world-secondary-action" type="button" data-world-landmark="information">How it works</button>
            </div>
          </section>

          <div class="world-metrics" aria-label="Live ForkMesh metrics">
            <div class="world-metric"><strong data-world-repos>—</strong><span>repositories</span></div>
            <div class="world-metric"><strong data-world-nodes>—</strong><span>live nodes</span></div>
            <div class="world-metric"><strong data-world-players>1</strong><span>in world</span></div>
          </div>
        </div>

        <aside class="world-right-rail" aria-label="World navigation and activity">
          <section class="world-map">
            <div class="world-panel-heading">
              <h2>World map</h2>
              <span data-world-location-code>TS-01</span>
            </div>
            <ul class="world-map-list">${mapItems}</ul>
          </section>
          <section
            class="world-community-placement"
            data-world-community-placement
            aria-label="Community-reviewed contextual placement"
            hidden
          ></section>
          <section
            class="world-fediverse-activity"
            data-world-fediverse-activity
            aria-labelledby="world-fediverse-activity-title"
          >
            <div class="world-panel-heading">
              <h2 id="world-fediverse-activity-title">Verified public feedback</h2>
              <span>MANUAL</span>
            </div>
            <div data-world-fediverse-items>
              <p class="world-rail-empty">No verified public repository feedback is currently listed.</p>
            </div>
          </section>
          <div class="world-activity" aria-live="polite">
            <div class="world-activity-line" data-world-activity>
              Public network activity will appear here in generalized form.
            </div>
          </div>
        </aside>

        <div class="world-controls" aria-label="Movement and camera controls">
          <div class="world-control-keys" aria-hidden="true">
            <span class="world-control-key">W</span>
            <span class="world-control-key">A</span>
            <span class="world-control-key">S</span>
            <span class="world-control-key">D</span>
          </div>
          <div class="world-controls-copy">
            <strong>Move and look around</strong>
            <span>WASD or arrows · drag to rotate · wheel to zoom</span>
          </div>
          <span class="world-location" data-world-location>Town Square</span>
          <span class="world-location world-region-location" data-world-active-region>Central Campus</span>
        </div>

        <div class="world-touch-controls" aria-label="Touch movement controls">
          <button class="world-touch-button" type="button" data-move="forward" aria-label="Move forward">↑</button>
          <button class="world-touch-button" type="button" data-move="left" aria-label="Move left">←</button>
          <button class="world-touch-button" type="button" data-move="back" aria-label="Move back">↓</button>
          <button class="world-touch-button" type="button" data-move="right" aria-label="Move right">→</button>
        </div>

        <details class="world-diagnostics" data-world-diagnostics>
          <summary aria-label="Open local World performance and connection details">
            <span class="world-diagnostics-light" data-world-diagnostics-light data-state="connecting" aria-hidden="true"></span>
            <strong>DEBUG</strong>
            <span data-world-diagnostics-summary>Renderer starting · socket connecting · 1 peer</span>
            <span class="world-diagnostics-toggle" aria-hidden="true">⌃</span>
          </summary>
          <div class="world-diagnostics-details" aria-live="off">
            <p>Local one-second samples only. No diagnostics are transmitted, and no URLs, locations, form contents, or activity history are collected.</p>
            <dl>
              <div><dt>Renderer</dt><dd data-world-diagnostics-renderer>Starting…</dd></div>
              <div><dt>Connection</dt><dd data-world-diagnostics-connection>Connecting…</dd></div>
              <div><dt>Socket frames</dt><dd data-world-diagnostics-traffic>Inbound 0 · outbound 0</dd></div>
              <div><dt>Coalescing</dt><dd data-world-diagnostics-queues>Movement idle · profile idle</dd></div>
              <div><dt>Build</dt><dd data-world-diagnostics-build>Loading current version…</dd></div>
            </dl>
          </div>
        </details>

        <details class="world-diagnostics world-chat-terminal" data-world-chat-terminal>
          <summary aria-label="Open World chat in a terminal panel">
            <span class="world-diagnostics-light" data-state="online" aria-hidden="true"></span>
            <strong>CHAT</strong>
            <span>Chat stays inside ForkMesh World</span>
            <span class="world-diagnostics-toggle" aria-hidden="true">⌃</span>
          </summary>
          <div class="world-chat-terminal-body">
            <iframe
              class="world-chat-terminal-frame"
              data-world-chat-terminal-frame
              title="ForkMesh World chat terminal"
              sandbox="allow-forms allow-same-origin allow-scripts"
              referrerpolicy="same-origin"
            ></iframe>
          </div>
        </details>

        <div class="world-toast" data-world-toast role="status"></div>
        <div class="world-detail-backdrop" data-world-detail-backdrop></div>
        <aside
          class="world-detail"
          data-world-detail
          aria-labelledby="world-detail-title"
          aria-hidden="true"
        ></aside>

        <button
          class="world-chat-backdrop"
          type="button"
          data-world-chat-close
          aria-label="Close World chat"
          tabindex="-1"
        ></button>
        <section
          class="world-chat"
          data-world-chat
          role="dialog"
          aria-modal="true"
          aria-labelledby="world-chat-title"
          aria-hidden="true"
        >
          <header class="world-chat-heading">
            <div>
              <p class="world-eyebrow">LIVE COLLABORATION</p>
              <h2 id="world-chat-title">World chat</h2>
              <span>Chat stays inside ForkMesh World.</span>
            </div>
            <button type="button" data-world-chat-close aria-label="Close World chat">×</button>
          </header>
          <iframe
            class="world-chat-frame"
            data-world-chat-frame
            title="ForkMesh World chat"
            sandbox="allow-forms allow-same-origin allow-scripts"
            referrerpolicy="same-origin"
          ></iframe>
        </section>

        <button
          class="world-account-backdrop"
          type="button"
          data-world-account-close
          aria-label="Close account panel"
          tabindex="-1"
        ></button>
        <section
          class="world-account"
          data-world-account
          role="dialog"
          aria-modal="true"
          aria-labelledby="world-account-title"
          aria-hidden="true"
        >
          <header class="world-account-heading">
            <div>
              <p class="world-eyebrow">FORKMESH IDENTITY</p>
              <h2 id="world-account-title">${
                signedInName ? "Your account" : "Join from the World"
              }</h2>
              <span>Passwords, verification codes, and form contents never enter multiplayer presence.</span>
            </div>
            <button type="button" data-world-account-close aria-label="Close account panel">×</button>
          </header>
          ${
            signedInName
              ? `<div class="world-account-signed-in">
                  <span>Signed in on this device as</span>
                  <strong>${escapeHTML(signedInName)}</strong>
                  <p>The server still verifies your session before granting an account badge, private repository visibility, notifications, or organization permissions.</p>
                  <button type="button" data-world-account-logout>Log out on this device</button>
                </div>`
              : `<div class="world-account-tabs" role="tablist" aria-label="Account action">
                  <button type="button" role="tab" aria-selected="true" data-world-account-mode="login">Log in</button>
                  <button type="button" role="tab" aria-selected="false" data-world-account-mode="signup">Create account</button>
                </div>
                <form class="world-account-form" data-world-login-form data-world-account-view="login">
                  <label>
                    <span>Email</span>
                    <input type="email" name="email" maxlength="320" autocomplete="username" required />
                  </label>
                  <label>
                    <span>Password</span>
                    <input type="password" name="password" maxlength="1024" autocomplete="current-password" required />
                  </label>
                  <label>
                    <span>Authenticator code <small>only if enabled</small></span>
                    <input type="text" name="totp" maxlength="12" inputmode="numeric" autocomplete="one-time-code" />
                  </label>
                  <button type="submit" data-world-account-submit>Log in inside the World</button>
                </form>
                <form class="world-account-form" data-world-signup-form data-world-account-view="signup" hidden>
                  <label>
                    <span>Public username</span>
                    <input type="text" name="nodeName" minlength="1" maxlength="63" autocomplete="username" autocapitalize="none" spellcheck="false" required />
                  </label>
                  <label>
                    <span>Email</span>
                    <input type="email" name="email" maxlength="320" autocomplete="email" required />
                  </label>
                  <label>
                    <span>Password <small>at least 8 characters</small></span>
                    <input type="password" name="password" minlength="8" maxlength="1024" autocomplete="new-password" required />
                  </label>
                  <label class="world-account-consent">
                    <input type="checkbox" name="terms" required />
                    <span>I agree to the ForkMesh <a href="/terms">Terms</a> and <a href="/privacy">Privacy Policy</a>.</span>
                  </label>
                  <button type="submit" data-world-account-submit>Create account inside the World</button>
                </form>
                <div class="world-account-verification" data-world-account-verification hidden>
                  <strong>Check your inbox</strong>
                  <p>ForkMesh created <span data-world-account-created-name></span> and sent a verification message to <span data-world-account-created-email></span>. You may close this panel and keep exploring as a guest.</p>
                </div>
                <p class="world-account-status" data-world-account-status role="status" aria-live="polite"></p>`
          }
          <p class="world-account-privacy">ForkMesh sends these forms only over same-origin HTTPS. Credentials are never placed in URLs, public activity, World sockets, analytics events, or repository logs.</p>
        </section>

        <section class="world-settings" data-world-settings aria-labelledby="world-settings-title" aria-hidden="true">
          <div class="world-settings-heading">
            <div>
              <p class="world-eyebrow">LOCAL CONTROLS</p>
              <h2 id="world-settings-title">Your view, your signal.</h2>
            </div>
            <button class="world-settings-close" type="button" data-world-settings-close aria-label="Close settings">×</button>
          </div>

          <fieldset class="world-setting-group">
            <legend>Personal environment · only changes this device</legend>
            <div class="world-theme-grid">${themes}</div>
            <label class="world-light-control">
              <span>
                <strong>Light level</strong>
                <output data-world-light-level-output>${escapeHTML(
                  settings.lightLevel,
                )}%</output>
              </span>
              <input
                type="range"
                min="${WORLD_LIGHT_LEVEL_MIN}"
                max="${WORLD_LIGHT_LEVEL_MAX}"
                step="5"
                value="${escapeHTML(settings.lightLevel)}"
                data-world-light-level
              />
              <small>Full daylight is the default. This adjustment stays on this device and never changes the shared world.</small>
            </label>
          </fieldset>

          <fieldset class="world-setting-group">
            <legend>Movement · only changes this device</legend>
            <label class="world-light-control">
              <span>
                <strong>Move speed</strong>
                <output data-world-move-speed-output>${escapeHTML(
                  settings.moveSpeed,
                )}%</output>
              </span>
              <input
                type="range"
                min="${WORLD_MOVE_SPEED_MIN}"
                max="${WORLD_MOVE_SPEED_MAX}"
                step="5"
                value="${escapeHTML(settings.moveSpeed)}"
                data-world-move-speed
              />
              <small>Scales how fast your avatar walks and runs. 100% is the default pace.</small>
            </label>
            <label class="world-light-control">
              <span>
                <strong>Acceleration</strong>
                <output data-world-move-accel-output>${escapeHTML(
                  settings.moveAccel >= WORLD_MOVE_ACCEL_MAX
                    ? "∞"
                    : `${settings.moveAccel}%`,
                )}</output>
              </span>
              <input
                type="range"
                min="${WORLD_MOVE_ACCEL_MIN}"
                max="${WORLD_MOVE_ACCEL_MAX}"
                step="25"
                value="${escapeHTML(settings.moveAccel)}"
                data-world-move-accel
              />
              <small>How quickly you reach top speed from rest. Slide all the way up for instant (∞) acceleration.</small>
            </label>
          </fieldset>

          <fieldset class="world-setting-group">
            <legend>Public avatar badge</legend>
            <label class="world-field">
              <span>Display name</span>
              <input
                type="text"
                maxlength="24"
                value="${escapeHTML(settings.displayName || identity.name)}"
                data-world-display-name
              />
            </label>
            <label class="world-field">
              <span>Availability</span>
              <select data-world-availability>${availabilityOptions}</select>
            </label>
            <label class="world-field">
              <span>Generalized public activity</span>
              <select data-world-activity-category>${activityOptions}</select>
            </label>
            <div class="world-status-editor">
              <div class="world-status-fields">
                <label class="world-field">
                  <span>Emoji status</span>
                  <input
                    type="text"
                    inputmode="text"
                    maxlength="48"
                    autocomplete="off"
                    spellcheck="false"
                    value="${escapeHTML(publicStatus.emoji)}"
                    data-world-status-emoji
                    aria-describedby="world-status-privacy-note"
                    placeholder="🧑‍💻"
                  />
                </label>
                <label class="world-field">
                  <span>One-word note <small>optional</small></span>
                  <input
                    type="text"
                    maxlength="${WORLD_STATUS_NOTE_MAX}"
                    autocomplete="off"
                    spellcheck="false"
                    value="${escapeHTML(publicStatus.note)}"
                    data-world-status-note
                    placeholder="coding"
                  />
                </label>
              </div>
              <details class="world-emoji-picker">
                <summary>Choose from the Unicode emoji picker</summary>
                <label class="world-field world-emoji-category">
                  <span>Category</span>
                  <select data-world-emoji-category>${emojiCategoryOptions}</select>
                </label>
                ${emojiCategoryPanels}
              </details>
              <div class="world-status-preview" aria-live="polite">
                <span>Public overhead status</span>
                <strong data-world-status-preview>${
                  publicStatus.emoji
                    ? escapeHTML(
                        [publicStatus.emoji, publicStatus.note]
                          .filter(Boolean)
                          .join(" "),
                      )
                    : "Off"
                }</strong>
                <button type="button" data-world-status-clear ${
                  publicStatus.emoji ? "" : "disabled"
                }>Clear</button>
              </div>
              <small id="world-status-privacy-note">
                Any single Unicode emoji sequence is accepted. The optional
                note must be one word. Only those two bounded values are public;
                no URL, activity detail, or form text is included.
              </small>
            </div>
            <label class="world-field">
              <span>Home / office door</span>
              <select data-world-public-door>
                <option value="knock" ${settings.publicDoor === "knock" ? "selected" : ""}>Visitors knock first</option>
                <option value="open" ${settings.publicDoor === "open" ? "selected" : ""}>Public lobby open</option>
                <option value="closed" ${settings.publicDoor === "closed" ? "selected" : ""}>Closed</option>
              </select>
            </label>
            ${privacyOptions}
          </fieldset>

          <p class="world-setting-note">
            Browser and OS are detected locally. Country comes from a country-only
            edge hint; ForkMesh World does not receive or
            display your raw IP. Movement is coarse, ephemeral, and never includes
            URLs, search terms, form contents, repository names, or wallet data.
          </p>
        </section>

        <section class="world-tour" data-world-tour aria-labelledby="world-tour-title" aria-hidden="true">
          <div class="world-tour-progress" data-world-tour-progress></div>
          <h2 id="world-tour-title" data-world-tour-title>Welcome to ForkMesh</h2>
          <p data-world-tour-copy></p>
          <div class="world-tour-actions">
            <button class="world-tour-skip" type="button" data-world-tour-skip>Leave tour</button>
            <button class="world-tour-next" type="button" data-world-tour-next>Next stop →</button>
          </div>
        </section>
      </div>
    </div>`;
}

class ForkMeshWorld extends HTMLElement {
  constructor() {
    super();
    this.mode = "public";
    this.identity = null;
    this.settings = null;
    this.world = null;
    this.landmarkCapabilities = initialLandmarkCapabilities();
    this.repositories = [];
    this.repositoryCatalogState = "loading";
    this.network = {};
    this.mirrorCatalogs = [];
    this.federatedInstances = [];
    this.communityPlacement = null;
    this.fediverseMentions = [];
    this.activeFediverseMention = null;
    this.organizations = [];
    this.activeOffice = null;
    this.events = [];
    this.eventsState = "loading";
    this.notifications = [];
    this.notificationsState = "loading";
    this.notificationUnread = 0;
    this.notificationAccount = "";
    this.rewardState = {};
    this.pendingRewards = [];
    this.pendingContribution = null;
    this.securityScan = null;
    this.securityHistory = [];
    this.securityRepository = "";
    this.fediverseDirectory = {
      mastodon: [],
      lemmy: [],
      x: [],
      reddit: [],
    };
    this.botDirectory = [];
    this.worldLimits = null;
    this.activeAudio = null;
    this.soundEnabled = false;
    this.soundContext = null;
    this.joinSoundTimes = [];
    this.mediaSpaces = [];
    this.mediaRoom = normalizeMediaRoom(null);
    this.activeRepository = null;
    this.repositoryMapState = "idle";
    this.repositoryMapTarget = "";
    this.repositoryMapSelection = 0;
    this.repositoryManualSelection = "";
    this.repositoryMapLoads = new Map();
    this.repositoryView = "map";
    this.pullReview = null;
    this.pullReviewSelection = 0;
    this.pullViewedFiles = new Map();
    this.pullReviewScrollCleanup = null;
    this.securityTriage = null;
    this.pendingWorkshop = null;
    this.activeWorkshop = null;
    this.savedWorkshopSessions = [];
    this.workshopEventCursor = 0;
    this.remotePlayers = new Map();
    this.localPeers = new Map();
    this.inactivePlayers = [];
    this.memberDirectory = [];
    this.pendingKnocks = new Map();
    this.serverPeerId = "";
    this.worldTicket = "";
    this.worldTicketExpires = 0;
    this.worldTicketTimer = 0;
    this.chatReturnFocus = null;
    this.accountReturnFocus = null;
    const worldQuery = new URLSearchParams(location.search);
    const requestedSpace = worldQuery.get("space") || "";
    const requestedLandmark = worldQuery.get("landmark") || "";
    this.requestedSpaceExplicit = WORLD_SPACE_IDS.has(requestedSpace);
    this.currentSpace = WORLD_SPACE_IDS.has(requestedSpace)
      ? requestedSpace
      : "town-square";
    this.requestedLandmark = LANDMARKS.some(
      (landmark) => landmark.id === requestedLandmark,
    )
      ? requestedLandmark
      : "";
    this.socket = null;
    this.presenceConnecting = false;
    this.socketRetry = 1000;
    this.socketTimer = 0;
    this.socketStableTimer = 0;
    this.socketConnectionAttempts = 0;
    this.socketInboundFrames = 0;
    this.socketOutboundFrames = 0;
    this.socketBackpressureEvents = 0;
    this.movementCoalescedFrames = 0;
    this.profileCoalescedFrames = 0;
    this.diagnosticsTimer = 0;
    this.diagnosticsSampleAt = performance.now();
    this.diagnosticsInboundSample = 0;
    this.diagnosticsOutboundSample = 0;
    this.lastDiagnosticsSnapshot = null;
    this.buildDiagnostics = { version: "", revision: "" };
    this.peerGraceTimer = 0;
    this.peerGraceUntil = 0;
    this.pingTimer = 0;
    this.profilePresenceTimer = 0;
    this.profilePresencePending = false;
    this.movementSendTimer = 0;
    this.pendingMovement = null;
    this.lastMovementSentAt = 0;
    this.positionWriteTimer = 0;
    this.pendingPosition = null;
    this.positionKey = "";
    this.restoredPosition = null;
    this.spawnSelected = false;
    this.rewardTimer = 0;
    this.mirrorTimer = 0;
    this.eventsTimer = 0;
    this.notificationsTimer = 0;
    this.mediaTimer = 0;
    this.seenRewardEvents = new Set();
    this.seenWorldEvents = new Set();
    this.seenNotifications = new Set();
    this.broadcast = null;
    this.broadcastTimer = 0;
    this.activityTimer = 0;
    this.clockTimer = 0;
    this.distanceTimer = 0;
    this.toastTimer = 0;
    this.inactiveSyncTimer = 0;
    this.inputInactiveTimer = 0;
    this.firstVisitAt = firstVisitTimestamp();
    this.publicVisitCount = sessionVisitCount(true);
    this.visitedPlaces = new Set(["town-square"]);
    this.tourIndex = -1;
    this.lastMovement = {
      x: -8.5,
      y: 0.38,
      z: 12.5,
      heading: Math.PI * 0.82,
      activity: "exploring the Town Square",
    };
    this.currentActivityCategory = "exploring-town-square";
    this.destroyed = false;
  }

  connectedCallback() {
    if (this.dataset.worldReady === "true") return;
    this.dataset.worldReady = "true";
    this.mode = this.dataset.worldMode || "public";
    if (this.mode === "public") document.body.classList.add("world-active");
    this.identity = accountIdentity(readSession());
    this.identity.firstVisitAge = firstVisitAge(this.firstVisitAt);
    this.identity.visitCount = this.publicVisitCount;
    this.identity.activityCategory = this.currentActivityCategory;
    this.settings = mergeSettings(readJSON(localStorage, SETTINGS_KEY, null));
    this.positionKey = positionStorageKey(this.identity.id);
    const restoredPosition = readWorldPosition(localStorage, this.positionKey);
    if (
      restoredPosition &&
      (!this.requestedSpaceExplicit ||
        restoredPosition.space === this.currentSpace)
    ) {
      this.restoredPosition = restoredPosition;
      this.currentSpace = restoredPosition.space;
    }
    // Shared rooms, playlists, roles, and schedules are server-authoritative.
    // The only additional device-local state is one bounded position record
    // for this identity. It has no movement history, URLs, or activity labels.
    this.mediaSpaces = [];
    this.mediaRoom = normalizeMediaRoom(null);
    this.innerHTML = worldTemplate(
      this.identity,
      this.settings,
      this.mode,
      this.landmarkCapabilities,
    );
    this.syncViewportHeight();
    window.visualViewport?.addEventListener("resize", this.syncViewportHeight);
    window.addEventListener("orientationchange", this.syncViewportHeight);
    window.addEventListener("storage", this.handleStorage);
    window.addEventListener("pointerdown", this.handlePublicInputActivity, {
      passive: true,
    });
    window.addEventListener("pointermove", this.handlePublicInputActivity, {
      passive: true,
    });
    window.addEventListener("keydown", this.handlePublicInputActivity);
    window.addEventListener("message", this.handleWorldChatMessage);
    this.bindUI();
    this.startClock();
    this.startDiagnostics();
    this.bootstrap();
    this.startWorldTicketRefresh();
  }

  disconnectedCallback() {
    this.destroy();
  }

  $(selector) {
    return this.querySelector(selector);
  }

  $$(selector) {
    return Array.from(this.querySelectorAll(selector));
  }

  handlePublicInputActivity = () => {
    if (this.destroyed || !this.identity) return;
    if (!this.identity.inputActive) {
      this.identity.inputActive = true;
      this.world?.updateIdentity(publicIdentity(this.identity, this.settings));
      this.sendPresence({ type: "presence" });
      this.broadcastLocalPresence();
    }
    window.clearTimeout(this.inputInactiveTimer);
    this.inputInactiveTimer = window.setTimeout(() => {
      if (!this.identity || this.destroyed) return;
      this.identity.inputActive = false;
      this.world?.updateIdentity(publicIdentity(this.identity, this.settings));
      this.sendPresence({ type: "presence" });
      this.broadcastLocalPresence();
    }, 12000);
  };

  // The embedded /dashboard/chat iframe mirrors every live chat line to this
  // page (dashboard-chat.js, emitWorldChatBubble). Float it above the
  // speaker's avatar so nearby visitors see who is talking. Chat identity is
  // separate from presence, so peers are matched by shared display name —
  // best effort only, and unmatched senders simply show no bubble.
  handleWorldChatMessage = (event) => {
    if (this.destroyed || event.origin !== location.origin) return;
    const data = event.data;
    if (!data || data.type !== "forkmesh:world-chat") return;
    const text = String(data.text || "").trim();
    if (!text) return;
    if (data.self === true) {
      this.world?.showChatBubble?.(this.identity?.id, text, true);
      return;
    }
    const senderName = String(data.sender || "")
      .replace(/^World visitor\s*·\s*/i, "")
      .trim()
      .toLowerCase();
    if (!senderName) return;
    for (const [id, peer] of this.remotePlayers) {
      const peerName = String(peer?.name || "").trim().toLowerCase();
      // The public room truncates asserted names to 16 characters, so a
      // truncated sender may only be a prefix of the presence name.
      if (
        peerName === senderName ||
        (senderName.length >= 16 && peerName.startsWith(senderName))
      ) {
        this.world?.showChatBubble?.(id, text);
        return;
      }
    }
  };

  recordPublicVisit(place) {
    const safePlace = String(place || "").toLowerCase().slice(0, 64);
    if (!safePlace || this.visitedPlaces.has(safePlace)) return;
    this.visitedPlaces.add(safePlace);
    this.publicVisitCount = sessionVisitCount(true);
    this.identity.visitCount = this.publicVisitCount;
    this.world?.updateIdentity(publicIdentity(this.identity, this.settings));
  }

  async bootstrap() {
    const loadingCopy = this.$("[data-world-loading-copy]");
    try {
      loadingCopy.textContent = "Contacting the World relay";
      const contextPromise = this.loadContext();
      const dataPromise = this.loadWorldData();
      loadingCopy.textContent = "Building repositories, offices, and portals";
      const THREE = await THREE_MODULE;
      if (this.destroyed) return;
      this.world = createWorldScene({
        THREE,
        container: this.$("[data-world-canvas-wrap]"),
        labelLayer: this.$("[data-world-label-layer]"),
        identity: publicIdentity(this.identity, this.settings),
        reducedMotion: window.matchMedia("(prefers-reduced-motion: reduce)").matches,
        onLandmarkSelect: (id, meta = {}) => {
          if (meta.nodeCabinet) {
            this.openMirrorNodeDetail(meta.nodeCabinet);
            return;
          }
          if (id === "repositories" && meta.graphNode) {
            this.selectRepositoryGraphNode(meta.graphNode);
            return;
          }
          if (id === "broadcast" && meta.mediaSpaceId) {
            this.loadMediaSpace(meta.mediaSpaceId, true);
            return;
          }
          this.openLandmark(id);
        },
        onLocationChange: (label, id) => this.updateLocation(label, id),
        onRegionChange: (region) => this.updateRegion(region),
        onMovement: (movement) => this.handleMovement(movement),
        onModeration: (action) => this.moderateWorldPeer(action),
      });
      this.syncConstructionMarkers();
      this.setLandmarkCapability(
        "launchpad",
        true,
        "The interactive destination renderer is available.",
      );
      this.world.setTheme(this.settings.theme);
      this.world.setLightLevel(this.settings.lightLevel);
      this.world.setMovementTuning?.(this.movementTuning());
      await Promise.allSettled([contextPromise, dataPromise]);
      this.world.updateIdentity(publicIdentity(this.identity, this.settings));
      this.world.updateNetworkNodes(
        liveNodeRecords(this.network, this.mirrorCatalogs),
      );
      this.world.updateFederatedInstances?.(this.federatedInstances);
      this.world.updateBots(this.botDirectory);
      this.world.updateFediverseDirectory(this.fediverseDirectory);
      this.world.updateMediaSpaces?.(this.mediaSpaces, this.mediaRoom);
      this.syncMemberLounge();
      if (this.repositories.length) {
        // The scene and authenticated live catalog are both ready. Populate
        // the repository district from the canonical flagship route without
        // delaying entry into the rest of the World.
        void this.autoLoadFlagshipRepositoryMap();
      } else {
        // The portal's construction geometry is decorative, but an empty or
        // failed live catalog must not leave file icons that look selectable.
        this.world.updateRepositoryGraph?.([], []);
      }
      if (this.restoredPosition) {
        this.world.setSpawn?.(this.restoredPosition);
        this.lastMovement = {
          ...this.lastMovement,
          x: this.restoredPosition.x,
          y: this.restoredPosition.y,
          z: this.restoredPosition.z,
          heading: this.restoredPosition.heading,
          space: this.restoredPosition.space,
        };
        this.spawnSelected = true;
      } else if (this.currentSpace !== "town-square") {
        const traveled = WORLD_REGIONS.some(
          (region) => region.id === this.currentSpace,
        )
          ? this.world.travelToRegion?.(this.currentSpace)
          : this.world.travelToSpace?.(this.currentSpace);
        this.spawnSelected = traveled === true;
      }
      this.connectPresence();
      this.hideLoading();
      this.updateMetrics();
      this.updateDistances();
      this.startActivityTicker();
      this.startRewardPolling();
      this.startMirrorPolling();
      this.startEventPolling();
      this.startNotificationPolling();
      this.startMediaPlaybackPolling();
      this.announceWorldNotifications();
      this.distanceTimer = window.setInterval(() => this.updateDistances(), 1000);
      document.addEventListener("visibilitychange", this.handleVisibility);
      window.addEventListener("pagehide", this.handlePageHide, { once: true });
      const workshopSession = new URLSearchParams(location.search).get(
        "workshopSession",
      );
      if (
        readSession()?.sessionToken &&
        /^[a-f0-9]{32}$/.test(String(workshopSession || ""))
      ) {
        this.openLandmark("workshops");
        this.loadWorkshopSession(workshopSession);
      } else if (this.requestedLandmark) {
        this.openLandmark(this.requestedLandmark);
      }
    } catch (error) {
      console.warn("ForkMesh World could not start WebGL", error);
      this.renderWebGLFallback();
      await Promise.allSettled([this.loadContext(), this.loadWorldData()]);
      this.hideLoading();
      this.updateMetrics();
      this.startEventPolling();
      this.startNotificationPolling();
      this.announceWorldNotifications();
      if (this.requestedLandmark) {
        this.openLandmark(this.requestedLandmark);
      }
    }
  }

  handleVisibility = () => {
    this.world?.setPaused(document.hidden);
    if (document.hidden) {
      try {
        this.socket?.close(1000, "page hidden");
      } catch (_) {}
    } else if (!this.socket) {
      this.refreshWorldTicket();
      this.connectPresence();
      void this.refreshMirrorCatalogs();
    }
  };

  handleStorage = (event) => {
    if (event.key === "forkmesh.session") {
      this.refreshPersonalNotifications(false);
    }
  };

  syncViewportHeight = () => {
    const height = Math.max(
      240,
      Math.round(window.visualViewport?.height || window.innerHeight || 0),
    );
    this.style.setProperty("--world-viewport-height", `${height}px`);
  };

  handlePageHide = () => {
    this.captureWorldPosition(true);
    this.destroy();
  };

  hideLoading() {
    const loading = this.$("[data-world-loading]");
    if (!loading) return;
    // Reveal as soon as the renderer has actually painted a frame — two rAF
    // ticks — instead of a fixed timeout, so we drop the loading curtain the
    // instant the world is on screen without ever flashing a blank canvas.
    const reveal = () => loading.setAttribute("aria-hidden", "true");
    if (typeof window.requestAnimationFrame === "function") {
      window.requestAnimationFrame(() => window.requestAnimationFrame(reveal));
    } else {
      reveal();
    }
  }

  renderWebGLFallback() {
    const wrap = this.$("[data-world-canvas-wrap]");
    if (!wrap) return;
    wrap.innerHTML = `
      <div class="world-webgl-fallback">
        <div>
          <strong>The city is still here.</strong>
          <p>
            This browser could not start the 3D renderer. Use the World Map to
            inspect every district, or open the standard operations console.
          </p>
          <a class="world-primary-action" href="/dashboard">Open operations console</a>
        </div>
      </div>`;
  }

  async fetchJSON(path, options = {}) {
    const controller = new AbortController();
    const timeout = window.setTimeout(() => controller.abort(), options.timeout || 8000);
    const session = readSession();
    const headers = new Headers(options.headers || {});
    if (session?.sessionToken && options.auth !== false) {
      headers.set("Authorization", `Bearer ${session.sessionToken}`);
    }
    try {
      const response = await fetch(path, {
        ...options,
        headers,
        signal: controller.signal,
        credentials: "same-origin",
      });
      if (!response.ok) throw new Error(`${path} returned ${response.status}`);
      return await response.json();
    } finally {
      window.clearTimeout(timeout);
    }
  }

  async postJSON(path, body, options = {}) {
    const controller = new AbortController();
    const timeout = window.setTimeout(
      () => controller.abort(),
      options.timeout || 10000,
    );
    const headers = new Headers({
      accept: "application/json",
      "content-type": "application/json",
      ...(options.headers || {}),
    });
    const session = readSession();
    if (session?.sessionToken && options.auth !== false) {
      headers.set("Authorization", `Bearer ${session.sessionToken}`);
    }
    try {
      const response = await fetch(path, {
        method: options.method || "POST",
        headers,
        credentials: "same-origin",
        cache: "no-store",
        body: JSON.stringify(body || {}),
        signal: controller.signal,
      });
      let payload = {};
      try {
        payload = await response.json();
      } catch (_) {}
      if (!response.ok) {
        throw new Error(
          String(payload?.error || `Request returned ${response.status}`),
        );
      }
      return payload;
    } finally {
      window.clearTimeout(timeout);
    }
  }

  async loadContext() {
    const [contextResult, ticketResult] = await Promise.allSettled([
      this.fetchJSON("/api/world/context", {
        auth: false,
        timeout: 5000,
        cache: "no-store",
      }),
      this.fetchJSON("/api/world/ticket", {
        timeout: 5000,
        cache: "no-store",
      }),
    ]);
    const context =
      contextResult.status === "fulfilled" ? contextResult.value : null;
    const ticket =
      ticketResult.status === "fulfilled" ? ticketResult.value : null;
    if (
      ticket?.authenticated === true &&
      ACCOUNT_STATUS_VALUES.has(String(ticket.accountStatus || "")) &&
      ticket.accountStatus !== "Guest"
    ) {
      this.identity.name = sanitizePresenceText(
        ticket.name,
        this.identity.name,
        24,
      );
      this.identity.accountStatus = String(ticket.accountStatus);
      this.identity.isAdmin = ticket.isAdmin === true;
      this.identity.nodes = Array.from(
        {
          length: Math.max(
            0,
            Math.min(6, Number(ticket.nodeCount) || 0),
          ),
        },
        () => "node",
      );
      this.worldTicket = String(ticket.ticket || "");
      this.worldTicketExpires = Number(ticket.expiresAt || 0);
    } else {
      this.identity.accountStatus = "Guest";
      this.identity.isAdmin = false;
      this.identity.nodes = [];
      this.worldTicket = "";
      this.worldTicketExpires = 0;
    }
    const country = String(context?.country || context?.countryCode || "")
      .trim()
      .toUpperCase()
      .slice(0, 2);
    this.identity.countryCode = /^[A-Z]{2}$/.test(country) ? country : "";
    this.identity.flag = flagEmoji(this.identity.countryCode);
    const worldConnections = Number(context?.worldConnections);
    const worldMessagesPerSecond = Number(context?.worldMessagesPerSecond);
    const chatConnections = Number(context?.chatConnections);
    this.worldLimits =
      Number.isSafeInteger(worldConnections) &&
      worldConnections > 0 &&
      Number.isSafeInteger(worldMessagesPerSecond) &&
      worldMessagesPerSecond > 0 &&
      Number.isSafeInteger(chatConnections) &&
      chatConnections > 0
        ? {
            worldConnections,
            worldMessagesPerSecond,
            chatConnections,
          }
        : null;
    this.updateIdentityUI();
    this.world?.updateIdentity(publicIdentity(this.identity, this.settings));
    this.updateDurableObjectMetrics();
  }

  async loadWorldData() {
    const session = readSession();
    const hasSession = Boolean(session?.sessionToken);
    const [
      networkResult,
      mirrorResult,
      instancesResult,
      reposResult,
      versionResult,
      rewardResult,
      orgResult,
      scanResult,
      directoryResult,
      mediaResult,
      botResult,
      pendingRewardsResult,
      notificationsResult,
      inactiveResult,
      eventsResult,
      placementResult,
      mentionResult,
      membersResult,
    ] =
      await Promise.allSettled([
        this.fetchJSON("/api/network/overview", { auth: false }),
        this.fetchJSON("/api/repo/forkmesh/forkmesh/mirrors", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
        this.fetchJSON("/api/world/instances", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
        this.fetchJSON("/api/repositories"),
        this.fetchJSON("/api/version", { auth: false, timeout: 5000 }),
        this.fetchJSON("/api/accounts/central-fund", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
        this.fetchJSON("/api/world/organizations", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
        this.fetchJSON("/security/latest.json", {
          auth: false,
          timeout: 4000,
          cache: "no-store",
        }),
        this.fetchJSON("/api/world/fediverse", {
          auth: false,
          timeout: 4000,
          cache: "no-store",
        }),
        hasSession
          ? this.fetchJSON("/api/world/media/spaces", {
              timeout: 5000,
              cache: "no-store",
            })
          : Promise.resolve({ spaces: [] }),
        this.fetchJSON("/world/bot-directory.json", {
          auth: false,
          timeout: 4000,
        }),
        hasSession
          ? this.fetchJSON("/api/rewards/pending", {
              timeout: 5000,
              cache: "no-store",
            })
          : Promise.resolve({ rewards: [] }),
        hasSession && session?.nodeName
          ? this.fetchJSON(
              `/api/notifications?node=${encodeURIComponent(
                String(session.nodeName).toLowerCase(),
              )}&limit=100`,
              {
                timeout: 5000,
                cache: "no-store",
              },
            )
          : Promise.resolve({ notifications: [], unread: 0 }),
        this.fetchJSON("/api/world/inactive", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
        this.fetchJSON("/api/world/events", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
        this.fetchJSON(
          "/api/world/community-ads/placements?context=town-square",
          {
            auth: false,
            timeout: 4000,
            cache: "no-store",
          },
        ),
        this.fetchJSON("/api/world/fediverse-mentions", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
        this.fetchJSON("/api/accounts/users", {
          auth: false,
          timeout: 5000,
        }),
      ]);
    this.network = networkResult.status === "fulfilled" ? networkResult.value : {};
    this.mirrorCatalogs =
      mirrorResult.status === "fulfilled"
        ? [
            {
              ...mirrorResult.value,
              requestedOwner: FLAGSHIP_REPOSITORY.owner,
              requestedRepo: FLAGSHIP_REPOSITORY.repo,
            },
          ]
        : [];
    this.federatedInstances =
      instancesResult.status === "fulfilled"
        ? normalizeFederatedInstances(instancesResult.value)
        : [];
    this.communityPlacement =
      placementResult.status === "fulfilled"
        ? normalizeCommunityPlacement(placementResult.value)
        : null;
    this.fediverseMentions =
      mentionResult.status === "fulfilled"
        ? normalizeFediverseMentions(mentionResult.value)
        : [];
    this.renderCommunityPlacement();
    this.renderFediverseActivity();
    if (reposResult.status === "fulfilled") {
      this.repositories = reconcileRepositoryAliases(
        cleanRepositories(reposResult.value),
        this.mirrorCatalogs,
      );
      this.repositoryCatalogState = this.repositories.length ? "ready" : "empty";
    } else {
      this.repositories = [];
      this.repositoryCatalogState = "unavailable";
    }
    if (eventsResult.status === "fulfilled") {
      this.events = normalizeCommunityEvents(eventsResult.value);
      this.eventsState = this.events.length ? "ready" : "empty";
    } else {
      this.events = [];
      this.eventsState = "unavailable";
    }
    this.notificationAccount =
      hasSession && session?.nodeName
        ? String(session.nodeName).toLowerCase()
        : "";
    if (!this.notificationAccount) {
      this.notifications = [];
      this.notificationUnread = 0;
      this.notificationsState = "signed-out";
    } else if (notificationsResult.status === "fulfilled") {
      this.notifications = normalizeWorldNotifications(
        notificationsResult.value,
      );
      this.notificationUnread = Math.max(
        0,
        Number(notificationsResult.value?.unread) || 0,
      );
      this.notificationsState = this.notifications.length ? "ready" : "empty";
    } else {
      this.notifications = [];
      this.notificationUnread = 0;
      this.notificationsState = hasSession ? "unavailable" : "signed-out";
    }
    this.updateNotificationBadge();
    this.buildDiagnostics =
      versionResult.status === "fulfilled"
        ? normalizeBuildDiagnostics(versionResult.value)
        : { version: "", revision: "" };
    this.renderDiagnostics();
    this.rewardState =
      rewardResult.status === "fulfilled" ? rewardResult.value || {} : {};
    this.pendingRewards =
      pendingRewardsResult.status === "fulfilled" &&
      Array.isArray(pendingRewardsResult.value?.rewards)
        ? pendingRewardsResult.value.rewards.slice(0, 100)
        : [];
    this.captureRewardEvents(false);
    this.organizations =
      orgResult.status === "fulfilled" &&
      Array.isArray(orgResult.value?.organizations)
        ? orgResult.value.organizations.slice(0, 50)
        : [];
    if (this.organizations.length) {
      this.organizations = await this.loadOrganizationSpaces(this.organizations);
    }
    this.securityScan =
      scanResult.status === "fulfilled" ? scanResult.value || null : null;
    this.fediverseDirectory =
      directoryResult.status === "fulfilled"
        ? directoryResult.value || {
            mastodon: [],
            lemmy: [],
            x: [],
            reddit: [],
          }
        : { mastodon: [], lemmy: [], x: [], reddit: [] };
    this.mediaSpaces =
      mediaResult.status === "fulfilled"
        ? normalizeMediaSpaces(mediaResult.value)
        : [];
    const selectedMediaSpace =
      this.mediaSpaces.find((space) => space.id === this.mediaRoom.id) ||
      this.mediaSpaces[0];
    if (selectedMediaSpace?.id) {
      try {
        this.mediaRoom = normalizeMediaRoom(
          await this.fetchJSON(
            `/api/world/media/spaces/${encodeURIComponent(
              selectedMediaSpace.id,
            )}`,
            { timeout: 5000, cache: "no-store" },
          ),
        );
      } catch (_) {
        this.mediaRoom = selectedMediaSpace;
      }
    } else {
      this.mediaRoom = normalizeMediaRoom(null);
    }
    this.botDirectory =
      botResult.status === "fulfilled" && Array.isArray(botResult.value?.bots)
        ? botResult.value.bots.slice(0, 24)
        : [];
    this.inactivePlayers =
      inactiveResult.status === "fulfilled" &&
      Array.isArray(inactiveResult.value?.people)
        ? inactiveResult.value.people.slice(0, 64).map((person) => ({
            id: `inactive:${String(person.id || "").slice(0, 24)}`,
            name: sanitizePresenceText(
              person.name,
              "Private contributor",
              32,
            ),
            flag: "◌",
            browser: "Hidden",
            os: "Hidden",
            accountStatus: ACCOUNT_STATUS_VALUES.has(
              String(person.accountStatus || ""),
            )
              ? String(person.accountStatus)
              : "Registered",
            nodes: Array.from(
              {
                length: Math.max(
                  0,
                  Math.min(6, Number(person.nodeCount) || 0),
                ),
              },
              () => "node",
            ),
            activity: "idle",
            availability: String(person.availability || "inactive"),
            lastActive: String(person.lastActive || "Last active recently"),
            publicDoor: "closed",
            space: "town-square",
            x: 0,
            y: 0.38,
            z: 0,
            heading: 0,
            persistedInactive: true,
          }))
        : [];
    // Public chat roster directory (user profiles only) doubles as the Member
    // Lounge population: every public registered account gets a seat, and the
    // roster length feeds the total-members sign at the lounge front.
    this.memberDirectory =
      membersResult.status === "fulfilled" &&
      Array.isArray(membersResult.value?.users)
        ? membersResult.value.users
            .map((user) => ({
              name: sanitizePresenceText(user?.name, "", 32),
              nodes: Array.isArray(user?.nodes) ? user.nodes.slice(0, 6) : [],
            }))
            .filter((user) => user.name)
        : [];
    const liveMirrors = liveNodeRecords(this.network, this.mirrorCatalogs);
    const rewardAddress = String(this.rewardState?.address || "").trim();
    this.landmarkCapabilities.fountain = {
      live:
        rewardResult.status === "fulfilled" &&
        /^[1-9A-HJ-NP-Za-km-z]{32,44}$/.test(rewardAddress),
      reason: LANDMARK_CONSTRUCTION_REASONS.fountain,
    };
    this.landmarkCapabilities.repositories = {
      live:
        reposResult.status === "fulfilled" &&
        hasRepositoryCatalogSchema(reposResult.value),
      reason: LANDMARK_CONSTRUCTION_REASONS.repositories,
    };
    this.landmarkCapabilities.routing = {
      live:
        mirrorResult.status === "fulfilled" &&
        mirrorResult.value?.ok === true &&
        Array.isArray(mirrorResult.value?.mirrors) &&
        liveMirrors.some(
          (node) =>
            node.healthy === true &&
            node.cloneAvailable === true &&
            /^[0-9a-f]{40,64}$/.test(String(node.commit || "")),
        ),
      reason: LANDMARK_CONSTRUCTION_REASONS.routing,
    };
    this.landmarkCapabilities.organizations = {
      live:
        orgResult.status === "fulfilled" &&
        Array.isArray(orgResult.value?.organizations),
      reason: LANDMARK_CONSTRUCTION_REASONS.organizations,
    };
    this.landmarkCapabilities.fediverse = {
      live:
        directoryResult.status === "fulfilled" &&
        hasFediverseDirectorySchema(directoryResult.value),
      reason: LANDMARK_CONSTRUCTION_REASONS.fediverse,
    };
    this.landmarkCapabilities.security = {
      live:
        scanResult.status === "fulfilled" &&
        hasCompletedSecurityScan(this.securityScan),
      reason: LANDMARK_CONSTRUCTION_REASONS.security,
    };
    this.landmarkCapabilities.events = {
      live:
        eventsResult.status === "fulfilled" &&
        Array.isArray(eventsResult.value?.events),
      reason: LANDMARK_CONSTRUCTION_REASONS.events,
    };
    this.landmarkCapabilities.workshops = {
      live:
        this.landmarkCapabilities.repositories.live === true &&
        this.repositories.some((repo) => repo.liveHost || repo.isPrivate),
      reason: LANDMARK_CONSTRUCTION_REASONS.workshops,
    };
    this.syncConstructionMarkers();
    this.world?.updateNetworkNodes(
      liveMirrors,
    );
    this.world?.updateFederatedInstances?.(this.federatedInstances);
    this.world?.updateBots(this.botDirectory);
    this.world?.updateOrganizations(this.organizations);
    this.world?.updateFediverseDirectory(this.fediverseDirectory);
    this.world?.updateMediaSpaces?.(this.mediaSpaces, this.mediaRoom);
    this.renderPeers();
    this.updateMetrics();
  }

  renderCommunityPlacement() {
    const container = this.$("[data-world-community-placement]");
    if (!container) return;
    const placement = this.communityPlacement;
    if (!placement) {
      container.hidden = true;
      container.replaceChildren();
      return;
    }
    container.hidden = false;
    container.innerHTML = `
      <p class="world-community-placement-label">${escapeHTML(
        placement.label,
      )}</p>
      <strong>${escapeHTML(placement.sponsor)}</strong>
      <p>${escapeHTML(placement.copy)}</p>
      <a
        href="${escapeHTML(placement.destination)}"
        target="_blank"
        rel="sponsored noopener noreferrer"
        referrerpolicy="no-referrer"
      >Visit sponsor <span aria-hidden="true">↗</span></a>
      <small>${escapeHTML(
        placement.whyShown,
      )} No behavioral tracking or personal data selected this placement.</small>`;
  }

  renderFediverseActivity() {
    const container = this.$("[data-world-fediverse-items]");
    if (!container) return;
    const items = this.fediverseMentions.slice(0, 3);
    if (!items.length) {
      container.innerHTML = `
        <p class="world-rail-empty">
          No verified public repository feedback is currently listed.
        </p>`;
      return;
    }
    const hasSession = Boolean(readSession()?.sessionToken);
    container.innerHTML = items
      .map((item) => {
        const stateLabel = {
          review: "Manual review",
          pending: "Pending",
          created: "Created",
          linked: "Tracked reply",
          failed: "Retry available",
        }[item.state];
        const reviewable =
          item.kind === "mention" &&
          ["review", "failed"].includes(item.state) &&
          hasSession;
        const destination =
          (item.state === "created" || item.state === "linked") &&
          item.issueUrl
            ? item.issueUrl
            : item.remoteUrl;
        return `
          <article class="world-fediverse-activity-item" data-world-fediverse-item="${escapeHTML(
            item.id,
          )}">
            <div>
              <span>${escapeHTML(stateLabel)}</span>
              <strong>${escapeHTML(item.repository)}</strong>
            </div>
            <p>${escapeHTML(item.excerpt || item.progress)}</p>
            <div>
              ${
                reviewable
                  ? `<button type="button" data-world-fediverse-review="${escapeHTML(
                      item.id,
                    )}">Preview</button>`
                  : ""
              }
              ${
                item.issueNumber > 0
                  ? `<button type="button" data-world-fediverse-thread="${escapeHTML(
                      item.id,
                    )}">Thread</button>`
                  : ""
              }
              <a
                href="${escapeHTML(destination)}"
                target="_blank"
                rel="noopener noreferrer"
                referrerpolicy="no-referrer"
              >${item.issueUrl === destination ? "Open issue" : "Public post"} ↗</a>
            </div>
          </article>`;
      })
      .join("");
  }

  openFediverseDetail(title, summary, content) {
    const detail = this.$("[data-world-detail]");
    const backdrop = this.$("[data-world-detail-backdrop]");
    if (!detail || !backdrop) return;
    detail.style.setProperty("--detail-color", "var(--world-violet)");
    detail.innerHTML = `
      <header class="world-detail-header">
        <div>
          <p class="world-eyebrow">VERIFIED PUBLIC ACTIVITY / MANUAL REVIEW</p>
          <h2 id="world-detail-title">${escapeHTML(title)}</h2>
        </div>
        <button
          class="world-detail-close"
          type="button"
          data-world-detail-close
          aria-label="Close fediverse review"
        >×</button>
      </header>
      <div class="world-detail-scroll">
        <p class="world-detail-summary">${escapeHTML(summary)}</p>
        ${content}
      </div>`;
    detail.dataset.open = "true";
    detail.setAttribute("aria-hidden", "false");
    backdrop.dataset.open = "true";
    window.setTimeout(
      () => detail.querySelector("[data-world-detail-close]")?.focus(),
      100,
    );
  }

  async previewFediverseMention(id) {
    if (!readSession()?.sessionToken) {
      this.toast("Sign in as the repository owner to review this public feedback.");
      return;
    }
    try {
      const preview = await this.postJSON(
        `/api/world/fediverse-mentions/${encodeURIComponent(id)}/preview`,
        {},
      );
      this.activeFediverseMention = {
        id,
        draft: preview.draft || {},
      };
      this.openFediverseDetail(
        "Review public feedback",
        "Nothing is filed automatically. Edit the draft, confirm it explicitly, and it will remain pending until the owner node materializes it.",
        `
          <section class="world-feature-card world-fediverse-review-card">
            <h3>${escapeHTML(preview.repository || "Public repository")}</h3>
            <p>
              From ${escapeHTML(preview.author || "fediverse user")} ·
              <a
                href="${escapeHTML(safePublicHTTPSURL(preview.remoteUrl))}"
                target="_blank"
                rel="noopener noreferrer"
                referrerpolicy="no-referrer"
              >verify public source ↗</a>
            </p>
            <label class="world-field">
              <span>Issue title</span>
              <input
                type="text"
                maxlength="240"
                data-world-fediverse-title
                value="${escapeHTML(preview.draft?.title || "")}"
              />
            </label>
            <label class="world-field">
              <span>Issue body</span>
              <textarea
                rows="9"
                maxlength="8192"
                data-world-fediverse-body
              >${escapeHTML(preview.draft?.body || "")}</textarea>
            </label>
            <label class="world-privacy-option">
              <span>I reviewed this public content and authorize a pending issue submission.</span>
              <input type="checkbox" data-world-fediverse-confirm />
            </label>
            <label class="world-privacy-option">
              <span>After owner-node confirmation, publish one public follow-up with the issue backlink.</span>
              <input type="checkbox" data-world-fediverse-followup />
            </label>
            <div class="world-notice">
              <strong>Pending is not created</strong>
              <span>The World will not call this an issue or announce a backlink until the owner node confirms the committed issue number.</span>
            </div>
            <div class="world-detail-actions">
              <button
                class="world-primary-action"
                type="button"
                data-world-fediverse-create="${escapeHTML(id)}"
              >Queue as pending issue</button>
              <button
                class="world-secondary-action"
                type="button"
                data-world-fediverse-dismiss="${escapeHTML(id)}"
              >Dismiss from feed</button>
            </div>
            <p class="world-panel-footnote" data-world-fediverse-result>
              Follow-up consent is off by default. No automated classification or behavioral profile is used.
            </p>
          </section>`,
      );
    } catch (error) {
      this.toast(
        String(error?.message || "").includes("forbidden")
          ? "Only the repository owner can review this item."
          : "The review preview is unavailable. No issue was queued.",
      );
    }
  }

  async openFediverseThread(id) {
    const item = this.fediverseMentions.find((entry) => entry.id === id);
    const parts = String(item?.repository || "").split("/");
    if (!item || item.issueNumber <= 0 || parts.length !== 2 || parts.some((part) => !part)) {
      this.toast("No public issue thread is available for this activity.");
      return;
    }
    const [owner, repo] = parts;
    this.openFediverseDetail(
      `Issue #${item.issueNumber} fediverse thread`,
      "Loading read-only remote replies. ActivityPub delivery signatures are not ForkMesh native event signatures.",
      '<section class="world-feature-card"><p class="world-empty-state">Loading remote ActivityPub replies…</p></section>',
    );
    try {
      const response = await this.fetchJSON(
        `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(
          repo,
        )}/fedi-comments?kind=issue&number=${encodeURIComponent(
          item.issueNumber,
        )}`,
        { auth: false, timeout: 5000, cache: "no-store" },
      );
      const comments = (
        Array.isArray(response?.comments)
          ? response.comments
          : Array.isArray(response?.items)
            ? response.items
            : []
      )
        .map((comment) => {
          const lifecycle = [
            "active",
            "edited",
            "tombstoned",
            "moderated",
            "awaiting-redelivery",
          ].includes(comment?.lifecycle)
            ? comment.lifecycle
            : comment?.tombstone
              ? "tombstoned"
              : comment?.moderated
                ? "moderated"
                : comment?.edited
                  ? "edited"
                  : "active";
          const provenance =
            comment?.provenance && typeof comment.provenance === "object"
              ? comment.provenance
              : {};
          return {
            id: sanitizePresenceText(
              comment?.remoteId || comment?.id,
              "",
              500,
            ),
            author: sanitizePresenceText(
              comment?.authorName || comment?.author,
              "Remote participant",
              100,
            ),
            body: sanitizePresenceText(comment?.body, "", 4000),
            url: safePublicHTTPSURL(
              comment?.url ||
                comment?.backlink ||
                provenance.backlink ||
                comment?.remoteId,
            ),
            instance: sanitizePresenceText(
              comment?.sourceInstance || provenance.instance,
              "",
              160,
            ),
            software: sanitizePresenceText(
              comment?.sourceSoftware || provenance.software,
              "ActivityPub",
              40,
            ),
            lifecycle,
            depth: Math.max(0, Math.min(8, Number(comment?.depth) || 0)),
            nativeEvent: false,
          };
        })
        .filter((comment) => comment.id || comment.url)
        .slice(0, 200);
      const threadHTML = comments.length
        ? comments
            .map((comment) => {
              const unavailable = [
                "tombstoned",
                "moderated",
                "awaiting-redelivery",
              ].includes(comment.lifecycle);
              const body =
                comment.lifecycle === "tombstoned"
                  ? "Deleted on the remote instance"
                  : unavailable
                    ? "Hidden by remote moderation"
                    : comment.body;
              return `
                <article
                  class="world-feature-card"
                  style="margin-left:${comment.depth * 16}px"
                  data-world-federated-reply
                  data-native-event="false"
                >
                  <h3>${escapeHTML(comment.author)}${
                    comment.lifecycle === "edited"
                      ? " <small>(edited)</small>"
                      : ""
                  }</h3>
                  <p${unavailable ? ' style="font-style:italic"' : ""}>${escapeHTML(
                    body || "Remote reply has no public text.",
                  )}</p>
                  <p class="world-panel-footnote">
                    ${escapeHTML(comment.software)}${
                      comment.instance
                        ? ` · ${escapeHTML(comment.instance)}`
                        : ""
                    } · Remote ActivityPub reply · not a signed ForkMesh native event
                  </p>
                  ${
                    comment.url
                      ? `<a href="${escapeHTML(
                          comment.url,
                        )}" target="_blank" rel="noopener noreferrer" referrerpolicy="no-referrer">Open original ↗</a>`
                      : ""
                  }
                </article>`;
            })
            .join("")
        : '<p class="world-empty-state">No remote ActivityPub replies are attached to this public issue.</p>';
      this.openFediverseDetail(
        `Issue #${item.issueNumber} fediverse thread`,
        `${item.repository} · read-only remote collaboration with explicit provenance and backlinks.`,
        `
          <section class="world-feature-card">
            <div class="world-notice">
              <strong>Separate trust domain</strong>
              <span>These replies are not part of the repository’s Ed25519-signed issue history. Edits, tombstones, moderation, and instance blocks come from the federated projection.</span>
            </div>
            <div class="world-detail-actions">
              ${
                item.issueUrl
                  ? `<a href="${escapeHTML(
                      item.issueUrl,
                    )}" target="_blank" rel="noopener noreferrer">Open native issue ↗</a>`
                  : ""
              }
            </div>
          </section>
          ${threadHTML}`,
      );
    } catch (_) {
      this.openFediverseDetail(
        `Issue #${item.issueNumber} fediverse thread`,
        "The remote-reply projection is currently unavailable.",
        '<section class="world-feature-card"><p class="world-empty-state">No remote content was shown. The signed native issue remains available through its own issue link.</p></section>',
      );
    }
  }

  async createFediverseMentionIssue(id) {
    const result = this.$("[data-world-fediverse-result]");
    const title = this.$("[data-world-fediverse-title]")?.value?.trim() || "";
    const body = this.$("[data-world-fediverse-body]")?.value?.trim() || "";
    const confirmed = Boolean(
      this.$("[data-world-fediverse-confirm]")?.checked,
    );
    if (!confirmed || !title || !body) {
      if (result) {
        result.textContent =
          "Review the title and body, then check the explicit authorization box.";
      }
      return;
    }
    const button = this.$(`[data-world-fediverse-create="${CSS.escape(id)}"]`);
    if (button) button.disabled = true;
    try {
      const response = await this.postJSON(
        `/api/world/fediverse-mentions/${encodeURIComponent(id)}/create`,
        {
          confirm: true,
          title,
          body,
          publishFollowup: Boolean(
            this.$("[data-world-fediverse-followup]")?.checked,
          ),
        },
      );
      if (result) {
        result.textContent =
          response.state === "pending"
            ? "Pending owner-node materialization. No issue-created announcement has been published."
            : "This feedback was already queued; no duplicate was created.";
      }
      await this.refreshFediverseMentions();
      this.toast("Feedback queued as pending; awaiting owner-node confirmation.");
    } catch (_) {
      if (result) {
        result.textContent =
          "The pending inbox could not accept this request. Nothing was called created; retry remains available.";
      }
    } finally {
      if (button) button.disabled = false;
    }
  }

  async moderateFediverseMention(id) {
    const reason = window.prompt(
      "Why should this public activity be dismissed from the World feed?",
      "",
    );
    if (!reason?.trim()) return;
    try {
      await this.postJSON(
        `/api/world/fediverse-mentions/${encodeURIComponent(id)}/moderate`,
        { action: "dismiss", reason: reason.trim() },
      );
      await this.refreshFediverseMentions();
      this.closeLandmark();
      this.toast("Public activity dismissed with an owner audit record.");
    } catch (_) {
      this.toast("The moderation action was not applied.");
    }
  }

  async refreshFediverseMentions() {
    try {
      this.fediverseMentions = normalizeFediverseMentions(
        await this.fetchJSON("/api/world/fediverse-mentions", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        }),
      );
    } catch (_) {
      this.fediverseMentions = [];
    }
    this.renderFediverseActivity();
  }

  async loadOrganizationSpaces(organizations) {
    const spaces = await Promise.all(
      organizations.slice(0, 24).map(async (membership) => {
        const name = sanitizePresenceText(membership.name, "", 50);
        if (!name) return membership;
        const root = `/api/orgs/${encodeURIComponent(name)}`;
        const [profile, members, teams] = await Promise.allSettled([
          this.fetchJSON(root, { timeout: 5000 }),
          this.fetchJSON(`${root}/members`, { timeout: 5000 }),
          this.fetchJSON(`${root}/teams`, { timeout: 5000 }),
        ]);
        return {
          ...membership,
          ...(profile.status === "fulfilled" ? profile.value : {}),
          memberList:
            members.status === "fulfilled" &&
            Array.isArray(members.value?.members)
              ? members.value.members.slice(0, 40)
              : [],
          teamList:
            teams.status === "fulfilled" && Array.isArray(teams.value?.teams)
              ? teams.value.teams.slice(0, 40)
              : [],
        };
      }),
    );
    return spaces.filter((space) => space?.name || space?.org);
  }

  bindUI() {
    const chatTerminal = this.$("[data-world-chat-terminal]");
    chatTerminal?.addEventListener("toggle", () => {
      if (chatTerminal.open) this.loadChatTerminalFrame();
    });
    this.addEventListener("click", (event) => {
      const chatLink = event.target.closest(
        "[data-world-chat-open], a[href^='/dashboard/chat']",
      );
      if (chatLink) {
        event.preventDefault();
        this.openWorldChat(
          chatLink.getAttribute("href") ||
            chatLink.dataset.worldChatOpen ||
            "/dashboard/chat",
          chatLink,
        );
        return;
      }
      if (event.target.closest("[data-world-chat-close]")) {
        this.closeWorldChat();
        return;
      }
      const accountOpen = event.target.closest("[data-world-account-open]");
      if (accountOpen) {
        this.toggleWorldAccount(true, "login", accountOpen);
        return;
      }
      if (event.target.closest("[data-world-account-close]")) {
        this.toggleWorldAccount(false);
        return;
      }
      const accountMode = event.target.closest("[data-world-account-mode]");
      if (accountMode) {
        this.selectWorldAccountMode(accountMode.dataset.worldAccountMode);
        return;
      }
      if (event.target.closest("[data-world-account-logout]")) {
        void this.logoutFromWorld();
        return;
      }
      if (event.target.closest("[data-world-arrival-dismiss]")) {
        try {
          localStorage.setItem(INTRO_DISMISSED_KEY, "1");
        } catch (_) {}
        const card = this.$(".world-arrival-card");
        if (card) card.hidden = true;
        this.toast("Introduction dismissed on this device. Start remains available from the dock.");
        return;
      }
      if (event.target.closest("[data-world-sound-toggle]")) {
        this.toggleWorldSound();
        return;
      }
      const landmarkButton = event.target.closest("[data-world-landmark]");
      if (landmarkButton) {
        const id = landmarkButton.dataset.worldLandmark;
        this.world?.focusLandmark(id);
        this.openLandmark(id);
        return;
      }
      const mirrorNodeButton = event.target.closest("[data-world-mirror-node]");
      if (mirrorNodeButton) {
        const nodeName = String(
          mirrorNodeButton.dataset.worldMirrorNode || "",
        ).toLowerCase();
        const node = liveNodeRecords(
          this.network,
          this.mirrorCatalogs,
        ).find((candidate) => candidate.name.toLowerCase() === nodeName);
        if (node) {
          this.world?.focusNetworkNode?.(node.name);
          this.openMirrorNodeDetail(node);
        }
        return;
      }
      if (event.target.closest("[data-world-action='tour']")) {
        this.startTour();
        return;
      }
      if (event.target.closest("[data-world-settings-open]")) {
        this.toggleSettings(true);
        return;
      }
      if (event.target.closest("[data-world-settings-close]")) {
        this.toggleSettings(false);
        return;
      }
      const emojiChoice = event.target.closest(
        "[data-world-status-emoji-choice]",
      );
      if (emojiChoice) {
        this.commitWorldStatus(
          emojiChoice.dataset.worldStatusEmojiChoice,
          this.$("[data-world-status-note]")?.value,
        );
        return;
      }
      if (event.target.closest("[data-world-status-clear]")) {
        this.commitWorldStatus("", "");
        return;
      }
      const fediverseReview = event.target.closest(
        "[data-world-fediverse-review]",
      );
      if (fediverseReview) {
        this.previewFediverseMention(
          fediverseReview.dataset.worldFediverseReview,
        );
        return;
      }
      const fediverseThread = event.target.closest(
        "[data-world-fediverse-thread]",
      );
      if (fediverseThread) {
        this.openFediverseThread(
          fediverseThread.dataset.worldFediverseThread,
        );
        return;
      }
      const fediverseCreate = event.target.closest(
        "[data-world-fediverse-create]",
      );
      if (fediverseCreate) {
        this.createFediverseMentionIssue(
          fediverseCreate.dataset.worldFediverseCreate,
        );
        return;
      }
      const fediverseDismiss = event.target.closest(
        "[data-world-fediverse-dismiss]",
      );
      if (fediverseDismiss) {
        this.moderateFediverseMention(
          fediverseDismiss.dataset.worldFediverseDismiss,
        );
        return;
      }
      if (event.target.closest("[data-world-detail-close]") || event.target.closest("[data-world-detail-backdrop]")) {
        this.closeLandmark();
        return;
      }
      const themeButton = event.target.closest("[data-world-theme]");
      if (themeButton) {
        this.setTheme(themeButton.dataset.worldTheme);
        return;
      }
      if (event.target.closest("[data-world-contribution-prepare]")) {
        this.prepareRewardContribution();
        return;
      }
      if (event.target.closest("[data-world-contribution-confirm]")) {
        this.confirmRewardContribution();
        return;
      }
      const pendingClaim = event.target.closest("[data-world-pending-claim]");
      if (pendingClaim) {
        this.claimPendingReward(
          pendingClaim.dataset.worldPendingClaim,
          pendingClaim
            .closest("[data-world-pending-reward]")
            ?.querySelector("[data-world-pending-wallet]")?.value,
        );
        return;
      }
      const detailAction = event.target.closest("[data-world-detail-action]");
      if (detailAction) {
        this.handleLandmarkAction(detailAction.dataset.worldDetailAction);
        return;
      }
      if (event.target.closest("[data-world-pull-list]")) {
        this.openRepositoryPullList();
        return;
      }
      const pullOpen = event.target.closest("[data-world-pull-open]");
      if (pullOpen) {
        this.loadRepositoryPullReview(pullOpen.dataset.worldPullNumber);
        return;
      }
      const pullBack = event.target.closest("[data-world-pull-back]");
      if (pullBack) {
        this.repositoryView =
          pullBack.dataset.worldPullBack === "map" ? "map" : "list";
        this.clearPullReviewScrollTracking();
        this.renderRepositoryExplorer();
        return;
      }
      const pullFile = event.target.closest("[data-world-pull-file-path]");
      if (pullFile) {
        this.scrollToPullFile(pullFile.dataset.worldPullFilePath);
        return;
      }
      const pullMerge = event.target.closest("[data-world-pull-merge]");
      if (pullMerge) {
        void this.mergeRepositoryPull();
        return;
      }
      const graphNode = event.target.closest("[data-world-graph-node]");
      if (graphNode) {
        const entity = buildRepositoryGraphEntities(
          this.activeRepository || {},
        ).find((candidate) => candidate.id === graphNode.dataset.worldGraphNode);
        if (entity) this.selectRepositoryGraphNode(entity);
        return;
      }
      const repoMap = event.target.closest("[data-world-repo-map]");
      if (repoMap) {
        const [owner, name] = String(repoMap.dataset.worldRepoMap || "").split("/");
        if (owner && name) {
          this.loadRepositoryMap(owner, name, { automatic: false });
        }
        return;
      }
      const securityScan = event.target.closest("[data-world-security-scan]");
      if (securityScan) {
        const [owner, name] = String(
          securityScan.dataset.worldSecurityScan || "",
        ).split("/");
        if (owner && name) this.loadRepositorySecurity(owner, name);
        return;
      }
      const office = event.target.closest("[data-world-office-member]");
      if (office) {
        this.activeOffice = {
          org: sanitizePresenceText(office.dataset.worldOfficeOrg, "", 50),
          member: sanitizePresenceText(
            office.dataset.worldOfficeMember,
            "",
            50,
          ),
        };
        this.openLandmark("organizations");
        return;
      }
      const triage = event.target.closest("[data-world-security-triage]");
      if (triage) {
        this.updateSecurityFindingStatus(
          triage.dataset.worldSecurityTriage,
          triage.dataset.worldSecurityStatus,
        );
        return;
      }
      const mirrorRequest = event.target.closest("[data-world-mirror-request]");
      if (mirrorRequest) {
        const repo = String(mirrorRequest.dataset.worldMirrorRequest || "");
        const destination = new URL("/dashboard/repos", location.origin);
        destination.searchParams.set("volunteer", repo);
        location.assign(destination.href);
        return;
      }
      const directory = event.target.closest("[data-world-repo-directory]");
      if (directory) {
        const owner = directory.dataset.worldRepoOwner;
        const repo = directory.dataset.worldRepoName;
        const path = directory.dataset.worldRepoDirectory;
        if (owner && repo) this.loadRepositoryDirectory(owner, repo, path || "");
        return;
      }
      if (event.target.closest("[data-world-run-workshop]")) {
        this.runWorkshop();
        return;
      }
      if (event.target.closest("[data-world-workshop-save]")) {
        this.saveWorkshopReport();
        return;
      }
      if (event.target.closest("[data-world-workshop-load]")) {
        this.loadWorkshopSessions();
        return;
      }
      const openWorkshop = event.target.closest("[data-world-workshop-open]");
      if (openWorkshop) {
        this.loadWorkshopSession(openWorkshop.dataset.worldWorkshopOpen);
        return;
      }
      if (event.target.closest("[data-world-workshop-share]")) {
        this.shareWorkshopSession();
        return;
      }
      if (event.target.closest("[data-world-workshop-comment]")) {
        this.addWorkshopComment();
        return;
      }
      if (event.target.closest("[data-world-workshop-events-refresh]")) {
        this.refreshWorkshopEvents();
        return;
      }
      if (event.target.closest("[data-world-events-refresh]")) {
        this.refreshCommunityEvents(true);
        return;
      }
      if (event.target.closest("[data-world-notifications-refresh]")) {
        this.refreshPersonalNotifications(true);
        return;
      }
      if (event.target.closest("[data-world-notifications-read]")) {
        this.markWorldNotificationsRead();
        return;
      }
      const radio = event.target.closest("[data-world-radio]");
      if (radio) {
        this.playRadio(radio.dataset.worldRadio);
        return;
      }
      if (event.target.closest("[data-world-radio-stop]")) {
        this.stopRadio();
        return;
      }
      if (event.target.closest("[data-world-media-add]")) {
        this.addMediaItem();
        return;
      }
      if (event.target.closest("[data-world-media-create]")) {
        this.createMediaSpace();
        return;
      }
      const playMedia = event.target.closest("[data-world-media-play]");
      if (playMedia) {
        this.updateMediaPlayback(
          "playing",
          playMedia.dataset.worldMediaPlay,
        );
        return;
      }
      if (event.target.closest("[data-world-media-pause]")) {
        this.updateMediaPlayback(
          "paused",
          this.mediaRoom.playback?.itemId || "",
        );
        return;
      }
      if (event.target.closest("[data-world-media-stop]")) {
        this.stopMediaRoom();
        return;
      }
      if (event.target.closest("[data-world-media-playback-refresh]")) {
        this.refreshMediaPlayback(true);
        return;
      }
      if (event.target.closest("[data-world-media-moderator-add]")) {
        this.grantMediaModerator();
        return;
      }
      const removeMedia = event.target.closest("[data-world-media-remove]");
      if (removeMedia) {
        this.removeMediaItem(removeMedia.dataset.worldMediaRemove);
        return;
      }
      const cancelSchedule = event.target.closest(
        "[data-world-media-schedule-cancel]",
      );
      if (cancelSchedule) {
        this.cancelMediaSchedule(
          cancelSchedule.dataset.worldMediaScheduleCancel,
        );
        return;
      }
      const removeModerator = event.target.closest(
        "[data-world-media-moderator-remove]",
      );
      if (removeModerator) {
        this.removeMediaModerator(
          removeModerator.dataset.worldMediaModeratorRemove,
        );
        return;
      }
      const travel = event.target.closest("[data-world-travel]");
      if (travel) {
        this.travelTo(travel.dataset.worldTravel);
        return;
      }
      const homeGrant = event.target.closest("[data-world-home-grant]");
      if (homeGrant) {
        const target = homeGrant.dataset.worldHomeGrant;
        this.sendWorldInteraction("home-grant", target);
        this.pendingKnocks.delete(target);
        this.openLandmark("neighborhood");
        this.toast("Front-yard visit granted once. Normal chat and resource permissions are unchanged.");
        return;
      }
      const homeDecline = event.target.closest("[data-world-home-decline]");
      if (homeDecline) {
        const target = homeDecline.dataset.worldHomeDecline;
        this.sendWorldInteraction("home-decline", target);
        this.pendingKnocks.delete(target);
        this.openLandmark("neighborhood");
        this.toast("Visit request declined without exposing a reason.");
        return;
      }
      const enterHome = event.target.closest("[data-world-enter-home]");
      if (enterHome) {
        this.enterNeighborhoodHome(enterHome.dataset.worldEnterHome, false);
        return;
      }
      if (event.target.closest("[data-world-knock]")) {
        const target = event.target.closest("[data-world-knock]")?.dataset
          .worldKnock;
        this.sendWorldInteraction("knock", target);
        this.toast("Knock sent as a consent request. It does not bypass the room’s permissions.");
        return;
      }
      if (event.target.closest("[data-world-tour-skip]")) {
        this.stopTour();
        return;
      }
      if (event.target.closest("[data-world-tour-next]")) {
        this.nextTourStep();
      }
    });

    this.addEventListener("change", (event) => {
      if (event.target.closest("[data-world-repo-filter]")) {
        this.applyRepositoryFilters();
        return;
      }
      const input = event.target.closest("[data-world-privacy]");
      if (input) {
        this.settings.privacy[input.dataset.worldPrivacy] = input.checked;
        this.commitPublicSettings();
        return;
      }
      const availability = event.target.closest("[data-world-availability]");
      if (availability) {
        if (AVAILABILITY_OPTIONS.some((option) => option.id === availability.value)) {
          this.settings.availability = availability.value;
          this.commitPublicSettings();
        }
        return;
      }
      const activity = event.target.closest("[data-world-activity-category]");
      if (activity) {
        if (ACTIVITY_OPTIONS.some((option) => option.id === activity.value)) {
          this.settings.activityCategory = activity.value;
          this.commitPublicSettings();
        }
        return;
      }
      const emojiCategory = event.target.closest(
        "[data-world-emoji-category]",
      );
      if (emojiCategory) {
        this.selectWorldEmojiCategory(emojiCategory.value);
        return;
      }
      const statusEmoji = event.target.closest("[data-world-status-emoji]");
      if (statusEmoji) {
        this.commitWorldStatus(
          statusEmoji.value,
          this.$("[data-world-status-note]")?.value,
        );
        return;
      }
      const statusNote = event.target.closest("[data-world-status-note]");
      if (statusNote) {
        this.commitWorldStatus(
          this.$("[data-world-status-emoji]")?.value,
          statusNote.value,
        );
        return;
      }
      const door = event.target.closest("[data-world-public-door]");
      if (door) {
        if (["knock", "open", "closed"].includes(door.value)) {
          this.settings.publicDoor = door.value;
          this.commitPublicSettings();
        }
        return;
      }
      const mediaSession = event.target.closest("[data-world-media-session]");
      if (mediaSession) {
        this.updateMediaSession(mediaSession.value);
        return;
      }
      const mediaSchedule = event.target.closest("[data-world-media-schedule]");
      if (mediaSchedule) {
        this.scheduleMediaRoom(mediaSchedule.value);
        return;
      }
      const mediaSpace = event.target.closest("[data-world-media-space]");
      if (mediaSpace) {
        this.loadMediaSpace(mediaSpace.value, true);
        return;
      }
      const name = event.target.closest("[data-world-display-name]");
      if (name) {
        this.settings.displayName = sanitizePresenceText(
          name.value,
          this.identity.name,
          24,
        );
        name.value = this.settings.displayName;
        this.commitPublicSettings();
      }
    });

    this.addEventListener("input", (event) => {
      const lightLevel = event.target.closest("[data-world-light-level]");
      if (lightLevel) {
        this.setLightLevel(lightLevel.value);
        return;
      }
      const moveSpeed = event.target.closest("[data-world-move-speed]");
      if (moveSpeed) {
        this.setMoveSpeed(moveSpeed.value);
        return;
      }
      const moveAccel = event.target.closest("[data-world-move-accel]");
      if (moveAccel) {
        this.setMoveAccel(moveAccel.value);
        return;
      }
      if (event.target.closest("[data-world-repo-filter='directory']")) {
        this.applyRepositoryFilters();
      }
    });

    this.$("[data-world-login-form]")?.addEventListener("submit", (event) => {
      event.preventDefault();
      void this.submitWorldLogin(event.currentTarget);
    });
    this.$("[data-world-signup-form]")?.addEventListener("submit", (event) => {
      event.preventDefault();
      void this.submitWorldSignup(event.currentTarget);
    });

    this.$$("[data-move]").forEach((button) => {
      const start = (event) => {
        event.preventDefault();
        button.setPointerCapture?.(event.pointerId);
        this.world?.setControl(button.dataset.move, true);
      };
      const stop = (event) => {
        event.preventDefault();
        this.world?.setControl(button.dataset.move, false);
      };
      button.addEventListener("pointerdown", start);
      button.addEventListener("pointerup", stop);
      button.addEventListener("pointercancel", stop);
      button.addEventListener("pointerleave", stop);
    });

    this.addEventListener("keydown", (event) => {
      if (event.code !== "Escape") return;
      if (this.$("[data-world-chat]")?.dataset.open === "true") {
        this.closeWorldChat();
      } else if (this.$("[data-world-account]")?.dataset.open === "true") {
        this.toggleWorldAccount(false);
      } else if (this.$("[data-world-settings]")?.dataset.open === "true") {
        this.toggleSettings(false);
      } else if (this.$("[data-world-detail]")?.dataset.open === "true") {
        this.closeLandmark();
      } else if (this.tourIndex >= 0) {
        this.stopTour();
      }
    });
  }

  commitPublicSettings() {
    this.saveSettings();
    this.updateIdentityUI();
    this.world?.updateIdentity(publicIdentity(this.identity, this.settings));
    this.sendPresence({ type: "presence" });
    this.broadcastLocalPresence();
    this.syncInactivePresence();
  }

  selectWorldEmojiCategory(categoryId) {
    const selected = WORLD_EMOJI_CATEGORIES.some(
      (category) => category.id === categoryId,
    )
      ? categoryId
      : WORLD_EMOJI_CATEGORIES[0]?.id;
    this.$$("[data-world-emoji-category-panel]").forEach((panel) => {
      panel.hidden = panel.dataset.worldEmojiCategoryPanel !== selected;
    });
  }

  commitWorldStatus(emojiValue, noteValue) {
    const rawEmoji = String(emojiValue || "").trim();
    const rawNote = String(noteValue || "").trim();
    const next = normalizeWorldStatus(rawEmoji, rawNote);
    const emojiInput = this.$("[data-world-status-emoji]");
    const noteInput = this.$("[data-world-status-note]");
    if (rawEmoji && !normalizeWorldEmoji(rawEmoji)) {
      if (emojiInput) emojiInput.value = this.settings.statusEmoji;
      if (noteInput) noteInput.value = this.settings.statusNote;
      this.toast("Choose or paste one valid Unicode emoji.");
      return false;
    }
    if (rawNote && !normalizeWorldStatusNote(rawNote)) {
      if (emojiInput) emojiInput.value = this.settings.statusEmoji;
      if (noteInput) noteInput.value = this.settings.statusNote;
      this.toast(
        `The public note must be one word, up to ${WORLD_STATUS_NOTE_MAX} characters.`,
      );
      return false;
    }
    if (rawNote && !next.emoji) {
      if (emojiInput) emojiInput.value = this.settings.statusEmoji;
      if (noteInput) noteInput.value = this.settings.statusNote;
      this.toast("Choose an emoji before adding a public note.");
      return false;
    }
    const changed =
      next.emoji !== this.settings.statusEmoji ||
      next.note !== this.settings.statusNote;
    this.settings.statusEmoji = next.emoji;
    this.settings.statusNote = next.note;
    if (emojiInput) emojiInput.value = next.emoji;
    if (noteInput) noteInput.value = next.note;
    this.updateWorldStatusUI();
    if (!changed) return false;
    // Profile presence is already coalesced. Status values are deliberately
    // absent from movement frames, so walking cannot repeatedly republish them.
    this.commitPublicSettings();
    return true;
  }

  updateWorldStatusUI() {
    const status = normalizeWorldStatus(
      this.settings?.statusEmoji,
      this.settings?.statusNote,
    );
    const copy = [status.emoji, status.note].filter(Boolean).join(" ");
    const preview = this.$("[data-world-status-preview]");
    const clear = this.$("[data-world-status-clear]");
    if (preview) preview.textContent = copy || "Off";
    if (clear) clear.disabled = !status.emoji;
  }

  syncInactivePresence() {
    window.clearTimeout(this.inactiveSyncTimer);
    this.inactiveSyncTimer = window.setTimeout(async () => {
      const session = readSession();
      if (!session?.sessionToken) return;
      const persistedStatuses = new Set([
        "away",
        "inactive",
        "recent",
        "offline-operator",
        "returning",
      ]);
      const visible =
        this.settings.privacy.inactivity &&
        this.settings.privacy.activity &&
        persistedStatuses.has(this.settings.availability);
      try {
        if (!visible) {
          await this.fetchJSON("/api/world/inactive", {
            method: "DELETE",
            timeout: 5000,
            cache: "no-store",
          });
          this.inactivePlayers = this.inactivePlayers.filter(
            (player) => !player.persistedInactive,
          );
          this.renderPeers();
          return;
        }
        await this.postJSON("/api/world/inactive", {
          status: this.settings.availability,
          shareInactivity: true,
          shareName: Boolean(this.settings.privacy.name),
          shareNodes: Boolean(this.settings.privacy.nodes),
          sessionToken: session.sessionToken,
        });
        const payload = await this.fetchJSON("/api/world/inactive", {
          auth: false,
          timeout: 5000,
          cache: "no-store",
        });
        this.inactivePlayers = Array.isArray(payload?.people)
          ? payload.people.slice(0, 64).map((person) => ({
              id: `inactive:${String(person.id || "").slice(0, 24)}`,
              name: sanitizePresenceText(
                person.name,
                "Private contributor",
                32,
              ),
              flag: "◌",
              browser: "Hidden",
              os: "Hidden",
              accountStatus: ACCOUNT_STATUS_VALUES.has(
                String(person.accountStatus || ""),
              )
                ? String(person.accountStatus)
                : "Registered",
              nodes: Array.from(
                {
                  length: Math.max(
                    0,
                    Math.min(6, Number(person.nodeCount) || 0),
                  ),
                },
                () => "node",
              ),
              activity: "idle",
              availability: String(person.availability || "inactive"),
              lastActive: String(
                person.lastActive || "Last active recently",
              ),
              publicDoor: "closed",
              space: "town-square",
              x: 0,
              y: 0.38,
              z: 0,
              heading: 0,
              persistedInactive: true,
            }))
          : [];
        this.renderPeers();
      } catch (_) {
        // Realtime presence continues even if the optional seating record
        // cannot be updated. Never fabricate a public inactivity state.
      }
    }, 250);
  }

  startClock() {
    // The World no longer shows a clock. This ticker only keeps the opt-in
    // "Show local time" presence badge fresh while that privacy setting is on.
    const render = () => {
      if (this.settings?.privacy?.localTime) this.updateIdentityUI();
    };
    render();
    this.clockTimer = window.setInterval(render, 1000);
  }

  updateIdentityUI() {
    if (!this.identity || !this.settings) return;
    const visible = publicIdentity(this.identity, this.settings);
    const flag = this.$("[data-world-shirt-flag]");
    const account = this.$("[data-world-shirt-account]");
    const tech = this.$("[data-world-shirt-tech]");
    const shirtName = this.$("[data-world-shirt-name]");
    const badge = this.$("[data-world-shirt-badge]");
    if (flag) flag.textContent = visible.flag;
    if (account) {
      account.textContent =
        ACCOUNT_STATUS_ICONS[visible.accountStatus] || "○";
      account.title = visible.accountStatus || "Guest";
    }
    if (tech) tech.textContent = `${visible.browser} · ${visible.os}`;
    if (shirtName) shirtName.textContent = visible.name;
    if (badge) {
      // The badge is the settings entry point; keep the name/status copy that
      // used to sit beside it reachable as its tooltip and accessible name.
      const copy = `${visible.name} · ${accountBadgeCopy(this.identity, this.settings)}`;
      badge.title = `${copy} — World and privacy settings`;
      badge.setAttribute("aria-label", `${copy} — open World and privacy settings`);
    }
    this.updateWorldStatusUI();
  }

  updateMetrics() {
    const stats = this.network?.stats || this.network || {};
    const repoCount = Number(stats.repos || stats.repositories || this.repositories.length);
    const nodes = Number(
      stats.hosts ||
        stats.nodes ||
        liveNodeRecords(this.network, this.mirrorCatalogs).length,
    );
    const reposEl = this.$("[data-world-repos]");
    const nodesEl = this.$("[data-world-nodes]");
    if (reposEl) reposEl.textContent = compactNumber(repoCount);
    if (nodesEl) nodesEl.textContent = compactNumber(nodes);
    this.updatePlayerCount();
    this.updateDurableObjectMetrics();
  }

  updateDurableObjectMetrics() {
    if (!this.world?.updateDurableObjects) return;
    const limits = this.worldLimits;
    if (!limits) {
      this.world.updateDurableObjects([]);
      return;
    }
    const worldSocketOnline =
      this.socket?.readyState === WebSocket.OPEN && Boolean(this.serverPeerId);
    const chatConnected = Number(this.network?.stats?.clients);
    const objects = [];
    if (worldSocketOnline) {
      objects.push({
        id: "forkmesh-world",
        name: "Town Square presence",
        usage: { connections: 1 + this.remotePlayers.size },
        limits: { connections: limits.worldConnections },
      });
    }
    if (Number.isFinite(chatConnected) && chatConnected >= 0) {
      objects.push({
        id: "forkmesh-general-chat",
        name: "#general chat · cached live count",
        usage: { connections: chatConnected },
        limits: { connections: limits.chatConnections },
      });
    }
    this.world.updateDurableObjects({ objects });
  }

  async moderateWorldPeer(action) {
    if (!this.identity?.isAdmin) {
      this.toast("Platform administrator access is required.");
      return;
    }
    const targetType = String(action?.targetType || "").toLowerCase();
    const handle = String(action?.handle || "").toLowerCase();
    if (
      !["ip", "agent"].includes(targetType) ||
      !/^[a-f0-9]{64}$/.test(handle)
    ) {
      this.toast("That temporary moderation handle is no longer available.");
      return;
    }
    const targetLabel =
      targetType === "ip" ? "this rotating IP token" : "this browser agent";
    if (
      !window.confirm(
        `Temporarily block ${targetLabel} from the World for one hour? ` +
          "This is a manual administrative action and will be audited.",
      )
    ) {
      return;
    }
    try {
      const result = await this.postJSON("/api/world/moderation", {
        targetType,
        handle,
        durationMs: WORLD_MANUAL_BLOCK_DURATION_MS,
      });
      const disconnected = Math.max(0, Number(result?.disconnected) || 0);
      this.toast(
        `Temporary ${targetType === "ip" ? "IP-token" : "agent"} block applied` +
          (disconnected
            ? `; ${disconnected} live connection${disconnected === 1 ? "" : "s"} closed.`
            : "."),
      );
    } catch (error) {
      this.toast(`Temporary block was not applied: ${error.message}`);
    }
  }

  rewardEvents() {
    return Array.isArray(this.rewardState?.transactions)
      ? this.rewardState.transactions.slice(0, 40)
      : [];
  }

  captureRewardEvents(animate = true) {
    this.rewardEvents().forEach((event) => {
      const id = String(
        event.transactionSignature || event.signature || event.eventId || event.id || "",
      );
      if (!id) return;
      const isNew = !this.seenRewardEvents.has(id);
      this.seenRewardEvents.add(id);
      const state = String(event.state || event.status || "").toLowerCase();
      if (
        animate &&
        isNew &&
        (event.transactionSignature ||
          event.signature ||
          /(completed|confirmed|finalized|on-chain)/.test(state))
      ) {
        const publicNode = sanitizePresenceText(
          event.node || event.recipientNode || "",
          "",
          40,
        );
        this.world?.playRewardEvent?.(publicNode);
        this.toast(
          "A confirmed community reward event reached an eligible mirror node. The animation is illustrative.",
        );
      }
    });
  }

  async refreshRewardState() {
    const hasSession = Boolean(readSession()?.sessionToken);
    const [pool, pending] = await Promise.allSettled([
      this.fetchJSON("/api/accounts/central-fund", {
        auth: false,
        timeout: 5000,
        cache: "no-store",
      }),
      hasSession
        ? this.fetchJSON("/api/rewards/pending", {
            timeout: 5000,
            cache: "no-store",
          })
        : Promise.resolve({ rewards: [] }),
    ]);
    if (pool.status === "fulfilled") {
      this.rewardState = pool.value || {};
      this.captureRewardEvents(true);
    }
    this.setLandmarkCapability(
      "fountain",
      pool.status === "fulfilled" &&
        /^[1-9A-HJ-NP-Za-km-z]{32,44}$/.test(
          String(pool.value?.address || "").trim(),
        ),
      LANDMARK_CONSTRUCTION_REASONS.fountain,
    );
    if (pending.status === "fulfilled") {
      this.pendingRewards = Array.isArray(pending.value?.rewards)
        ? pending.value.rewards.slice(0, 100)
        : [];
    }
  }

  async prepareRewardContribution() {
    const amountInput = this.$("[data-world-contribution-amount]");
    const modeInput = this.$("[data-world-contribution-mode]");
    const amountSol = Number(amountInput?.value || 0);
    const amountLamports = Math.round(amountSol * 1_000_000_000);
    if (!Number.isSafeInteger(amountLamports) || amountLamports < 10_000) {
      this.toast("Enter at least 0.00001 SOL.");
      return;
    }
    try {
      this.pendingContribution = await this.postJSON(
        "/api/rewards/contributions",
        {
          action: "prepare",
          mode: modeInput?.value || "trickle",
          amountLamports,
        },
        { auth: false },
      );
      this.openLandmark("fountain");
      this.toast(
        "Contribution intent prepared. Review it in your own wallet; ForkMesh never receives your key.",
      );
    } catch (error) {
      this.toast(`Contribution intent unavailable: ${error.message}`);
    }
  }

  async confirmRewardContribution() {
    const contributionId = String(
      this.pendingContribution?.contributionId || "",
    );
    const signature = String(
      this.$("[data-world-contribution-signature]")?.value || "",
    ).trim();
    if (!contributionId || !/^[1-9A-HJ-NP-Za-km-z]{64,120}$/.test(signature)) {
      this.toast("Paste the finalized Solana transaction signature.");
      return;
    }
    try {
      const result = await this.postJSON(
        "/api/rewards/contributions",
        {
          action: "confirm",
          contributionId,
          transactionSignature: signature,
        },
        { auth: false, timeout: 12000 },
      );
      this.pendingContribution = {
        ...this.pendingContribution,
        ...result,
      };
      await this.refreshRewardState();
      this.openLandmark("fountain");
      this.toast(
        result.status === "awaiting_finality"
          ? "Transfer submitted. No distribution occurs until finality is verified."
          : "Finalized direct contribution verified on-chain.",
      );
    } catch (error) {
      this.toast(`Contribution could not be verified: ${error.message}`);
    }
  }

  async claimPendingReward(rewardId, walletAddress) {
    const wallet = String(walletAddress || "").trim();
    if (!/^[1-9A-HJ-NP-Za-km-z]{32,44}$/.test(wallet)) {
      this.toast("Enter a valid public self-custodial Solana address.");
      return;
    }
    try {
      await this.postJSON("/api/rewards/pending", {
        rewardId: String(rewardId || ""),
        walletAddress: wallet,
      });
      await this.refreshRewardState();
      this.openLandmark("fountain");
      this.toast(
        "Public address accepted. The external local signer must still complete and finalize the exact transfer.",
      );
    } catch (error) {
      this.toast(`Pending reward could not be claimed: ${error.message}`);
    }
  }

  startRewardPolling() {
    window.clearInterval(this.rewardTimer);
    window.clearInterval(this.eventsTimer);
    this.rewardTimer = window.setInterval(async () => {
      try {
        await this.refreshRewardState();
        if (
          this.$("[data-world-detail]")?.dataset.open === "true" &&
          this.$("#world-detail-title")?.textContent?.includes("reward")
        ) {
          this.openLandmark("fountain");
        }
      } catch (_) {}
    }, 60000);
  }

  async refreshMirrorCatalogs() {
    const payload = await this.fetchJSON(
      "/api/repo/forkmesh/forkmesh/mirrors",
      {
        auth: false,
        timeout: 5000,
        cache: "no-store",
      },
    );
    this.mirrorCatalogs = [
      {
        ...payload,
        requestedOwner: FLAGSHIP_REPOSITORY.owner,
        requestedRepo: FLAGSHIP_REPOSITORY.repo,
      },
    ];
    const liveMirrors = liveNodeRecords(this.network, this.mirrorCatalogs);
    this.setLandmarkCapability(
      "routing",
      payload?.ok === true &&
        Array.isArray(payload?.mirrors) &&
        liveMirrors.some(
          (node) =>
            node.healthy === true &&
            node.cloneAvailable === true &&
            /^[0-9a-f]{40,64}$/.test(String(node.commit || "")),
        ),
      LANDMARK_CONSTRUCTION_REASONS.routing,
    );
    this.world?.updateNetworkNodes(liveMirrors);
  }

  startMirrorPolling() {
    window.clearInterval(this.mirrorTimer);
    this.mirrorTimer = window.setInterval(() => {
      if (this.destroyed || document.visibilityState !== "visible") return;
      void this.refreshMirrorCatalogs().catch(() => {
        // Preserve the last verified snapshot during a transient HTTPS failure.
      });
    }, MIRROR_STATUS_POLL_MS);
  }

  updatePlayerCount() {
    // BroadcastChannel is an offline/same-device fallback. Once the
    // authoritative World socket is live, counting both maps would show the
    // same browser tab twice.
    const socketOnline =
      this.socket?.readyState === WebSocket.OPEN && Boolean(this.serverPeerId);
    const count =
      1 + this.remotePlayers.size + (socketOnline ? 0 : this.localPeers.size);
    const element = this.$("[data-world-players]");
    if (element) element.textContent = compactNumber(count);
  }

  updateLocation(label, id) {
    const location = this.$("[data-world-location]");
    const code = this.$("[data-world-location-code]");
    if (location) location.textContent = label;
    if (code) code.textContent = id ? id.slice(0, 2).toUpperCase() + "-01" : "TS-01";
    this.$$("[data-world-landmark]").forEach((button) => {
      button.setAttribute(
        "aria-current",
        String(Boolean(id) && button.dataset.worldLandmark === id),
      );
    });
    this.recordPublicVisit(id || "town-square");
    if (this.settings.privacy.activity) {
      this.lastMovement.activity =
        label === "Town Square" ? "exploring the Town Square" : `visiting ${label}`;
    } else {
      this.lastMovement.activity = "online";
    }
    this.currentActivityCategory = {
      repositories: "viewing-repository",
      workshops: "browsing-code-visualization",
      organizations: "visiting-organization",
      information: "reading-documentation",
    }[id] || "exploring-town-square";
    this.identity.activityCategory = this.currentActivityCategory;
    this.sendPresence({ type: "presence" });
    this.broadcastLocalPresence();
  }

  updateRegion(region) {
    const element = this.$("[data-world-active-region]");
    if (element && region) {
      element.textContent = region.label;
    }
  }

  setLandmarkCapability(id, live, reason = "") {
    if (!LANDMARKS.some((landmark) => landmark.id === id)) return;
    this.landmarkCapabilities[id] = {
      live: live === true,
      reason:
        String(reason || "").trim() ||
        LANDMARK_CONSTRUCTION_REASONS[id] ||
        "This integration has not been verified in this session.",
    };
    this.syncConstructionMarkers(id);
  }

  syncConstructionMarkers(id = "") {
    const ids = id ? [id] : LANDMARKS.map((landmark) => landmark.id);
    ids.forEach((landmarkId) => {
      const capability = this.landmarkCapabilities[landmarkId] || {
        live: false,
        reason: "This integration has not been verified in this session.",
      };
      const reason = String(capability.reason || "");
      this.$$(
        `[data-world-construction-marker="${landmarkId}"]`,
      ).forEach((marker) => {
        marker.hidden = capability.live === true;
        marker.setAttribute(
          "aria-label",
          `Under construction: ${reason}`,
        );
        marker.setAttribute("title", `Under construction: ${reason}`);
      });
      this.$$(`[data-world-landmark="${landmarkId}"]`).forEach((button) => {
        button.dataset.worldUnderConstruction = String(
          capability.live !== true,
        );
      });
    });
    this.world?.updateLandmarkConstruction?.(this.landmarkCapabilities);
  }

  updateDistances() {
    const position = this.world?.getPosition?.();
    if (!position) return;
    LANDMARKS.forEach((landmark) => {
      const distance = Math.hypot(
        position.x - landmark.position[0],
        position.z - landmark.position[2],
      );
      const element = this.$(`[data-world-distance="${landmark.id}"]`);
      if (element) element.textContent = distance < 1 ? "here" : `${Math.round(distance)}m`;
    });
  }

  openLandmark(id) {
    const landmark = landmarkById(id);
    const capability = this.landmarkCapabilities[landmark.id] || {
      live: false,
      reason: "This integration has not been verified in this session.",
    };
    const detail = this.$("[data-world-detail]");
    const backdrop = this.$("[data-world-detail-backdrop]");
    if (!detail || !backdrop) return;
    const featurePanel = this.landmarkPanelHTML(landmark.id);
    detail.dataset.openLandmark = landmark.id;
    detail.style.setProperty("--detail-color", landmark.color);
    detail.innerHTML = `
      <header class="world-detail-header">
        <div>
          <p class="world-eyebrow">${escapeHTML(landmark.eyebrow)}</p>
          <h2 id="world-detail-title">${escapeHTML(landmark.label)}</h2>
        </div>
        <button class="world-detail-close" type="button" data-world-detail-close aria-label="Close ${escapeHTML(landmark.label)}">×</button>
      </header>
      <div class="world-detail-scroll">
        <p class="world-detail-summary">${escapeHTML(landmark.summary)}</p>
        <div class="world-status-row">
          <span class="world-status-pill">${escapeHTML(landmark.status)}</span>
          ${constructionMarkerHTML(
            landmark.id,
            capability,
            "world-construction-mark-panel",
          )}
        </div>

        <div class="world-truth-grid">
          <section class="world-truth-block">
            <h3>In the world</h3>
            <p>${escapeHTML(landmark.metaphor)}</p>
          </section>
          <section class="world-truth-block">
            <h3>What is actually happening</h3>
            <p>${escapeHTML(landmark.reality)}</p>
          </section>
        </div>

        <ul class="world-detail-list">
          ${landmark.bullets.map((bullet) => `<li>${escapeHTML(bullet)}</li>`).join("")}
        </ul>

        ${featurePanel}

        <div class="world-detail-actions">
          ${
            landmark.primary?.action
              ? `<button class="world-primary-action" type="button" data-world-detail-action="${escapeHTML(landmark.primary.action)}">${escapeHTML(landmark.primary.label)}</button>`
              : ""
          }
          ${
            landmark.secondary?.href
              ? `<a class="world-secondary-action" href="${escapeHTML(landmark.secondary.href)}">${escapeHTML(landmark.secondary.label)}</a>`
              : ""
          }
        </div>
      </div>`;
    detail.dataset.open = "true";
    detail.setAttribute("aria-hidden", "false");
    backdrop.dataset.open = "true";
    this.updateRepositoryReviewMode();
    if (
      landmark.id === "repositories" &&
      this.repositoryView === "review" &&
      this.pullReview?.state === "ready"
    ) {
      window.requestAnimationFrame(() =>
        this.setupPullReviewScrollTracking(),
      );
    }
    this.$$("[data-world-landmark]").forEach((button) => {
      button.setAttribute(
        "aria-current",
        String(button.dataset.worldLandmark === landmark.id),
      );
    });
    window.setTimeout(() => detail.querySelector("[data-world-detail-close]")?.focus(), 120);
  }

  closeLandmark() {
    const detail = this.$("[data-world-detail]");
    const backdrop = this.$("[data-world-detail-backdrop]");
    if (detail) {
      detail.dataset.open = "false";
      detail.dataset.repositoryReview = "false";
      detail.setAttribute("aria-hidden", "true");
    }
    if (backdrop) backdrop.dataset.open = "false";
    this.clearPullReviewScrollTracking();
    this.world?.clearFocus();
  }

  landmarkPanelHTML(id) {
    const panels = {
      information: () => this.informationPanelHTML(),
      fountain: () => this.rewardPanelHTML(),
      repositories: () => this.repositoryPanelHTML(),
      routing: () => this.routingPanelHTML(),
      organizations: () => this.organizationPanelHTML(),
      fediverse: () => this.fediversePanelHTML(),
      security: () => this.securityPanelHTML(),
      launchpad: () => this.launchpadPanelHTML(),
      events: () => this.eventsPanelHTML(),
      neighborhood: () => this.neighborhoodPanelHTML(),
      workshops: () => this.workshopPanelHTML(),
      broadcast: () => this.broadcastPanelHTML(),
      support: () => this.supportPanelHTML(),
    };
    return panels[id]?.() || "";
  }

  informationPanelHTML() {
    const steps = [
      ["What ForkMesh is", "A multiplayer developer city backed by independently operated Git mirrors."],
      ["Create an account", "Register and verify email in the console; guests can explore immediately."],
      ["Mirror a repository", "Install the desktop control node, choose an authorized repository, and publish signed health."],
      ["Operate a node", "The desktop controls sync, health, logs, permissions, Cloudflare deployment, and local keys."],
      ["Wallets and rewards", "Community members connect only a public self-custodial payout address; never enter its private key or recovery phrase."],
      ["Privacy and encryption", "Country is approximate; presence is optional and generalized; private data follows owner-controlled encryption and authorization."],
    ];
    return `
      <section class="world-feature-card" aria-label="ForkMesh orientation">
        <h3>New contributor route</h3>
        <ol class="world-orientation-list">
          ${steps
            .map(
              ([title, copy]) => `
                <li><strong>${escapeHTML(title)}</strong><span>${escapeHTML(copy)}</span></li>`,
            )
            .join("")}
        </ol>
        <div class="world-notice world-notice-safe">
          <strong>Non-custodial by design</strong>
          <span>User-owned funds and wallet keys remain on the user’s device. Community-pool funds, pending allocations, and completed on-chain transfers are separate states.</span>
        </div>
        <div class="world-notice">
          <strong>One-click Cloudflare setup stays on your computer</strong>
          <span>The hosted World never accepts, proxies, or stores a Cloudflare API token. Open the installed Qt Control Node and enter the scoped session-only token there.</span>
        </div>
        <div class="world-detail-actions">
          <a href="forkmesh://control/cloudflare" data-world-local-qt-link>Open local Qt Cloudflare setup</a>
          <a href="/docs/qt-client/#cloudflare">Desktop setup guide</a>
        </div>
      </section>`;
  }

  routingPanelHTML() {
    const instances = Array.isArray(this.federatedInstances)
      ? this.federatedInstances
      : [];
    const mirrorNodes = liveNodeRecords(this.network, this.mirrorCatalogs);
    return `
      <section class="world-feature-card" aria-label="Live mirror server cabinets">
        <h3>Live mirror server cabinets</h3>
        <div class="world-instance-list" data-world-mirror-node-list>
          ${
            mirrorNodes.length
              ? mirrorNodes
                  .map((node) => {
                    const commit = /^[0-9a-f]{40,64}$/.test(
                      String(node.commit || ""),
                    )
                      ? String(node.commit).slice(0, 12)
                      : "HEAD not reported";
                    const route =
                      node.cloneAvailable === true
                        ? "verified clone route"
                        : String(node.integrity || "") === "rejected"
                          ? "integrity blocked"
                          : "route not verified";
                    return `<article>
                      <span class="world-instance-icon" aria-hidden="true">${
                        node.healthy ? "●" : "◐"
                      }</span>
                      <div>
                        <strong>${escapeHTML(node.name)}</strong>
                        <span>${escapeHTML(
                          [node.platform, node.version ? `v${String(node.version).replace(/^v/i, "")}` : ""]
                            .filter(Boolean)
                            .join(" · ") || "platform/version not reported",
                        )}</span>
                        <p>${escapeHTML(`${commit} · ${route}`)}</p>
                      </div>
                      <button
                        type="button"
                        class="world-secondary-action"
                        data-world-mirror-node="${escapeHTML(node.name)}"
                      >Inspect live server</button>
                    </article>`;
                  })
                  .join("")
              : '<p class="world-empty-state">No live public mirror has a current presence record. ForkMesh does not invent server cabinets.</p>'
          }
        </div>
        <p class="world-panel-footnote">CPU, memory, and disk are optional operator-reported values signed into the public catalog. A signature establishes publisher provenance, not automatic trust. Missing values remain “not shared.”</p>
      </section>
      <section class="world-feature-card" aria-label="Approved ForkMesh relay instances">
        <h3>Approved federated instances</h3>
        <div class="world-instance-list">
          ${
            instances.length
              ? instances
                  .map(
                    (instance) => `<article>
                      <span class="world-instance-icon" aria-hidden="true">${
                        instance.online ? "●" : "○"
                      }</span>
                      <div><strong>${escapeHTML(
                        instance.label,
                      )}</strong><span>${escapeHTML(
                        instance.health.replaceAll("_", " "),
                      )}</span><p>${escapeHTML(
                        instance.online
                          ? "Online is backed by fresh signed node health through an approved relay."
                          : "Listed as approved, but not claimed online without fresh verified health.",
                      )}</p><small>${escapeHTML(
                        instance.healthEvidence.replaceAll("-", " "),
                      )}</small></div>
                      <a href="${escapeHTML(
                        instance.origin,
                      )}" target="_blank" rel="noopener noreferrer">Open public instance</a>
                    </article>`,
                  )
                  .join("")
              : '<p class="world-empty-state">No approved federated instance has a publishable origin yet. ForkMesh does not invent map nodes.</p>'
          }
        </div>
        <p class="world-panel-footnote">This projection includes only an approved public origin, generalized health, and a random public display id. Federation keys, signatures, tokens, wallets, node identities, raw IPs, private repositories, and exact activity are excluded.</p>
      </section>`;
  }

  mirrorNodeTechnicalHTML(node) {
    const known = (value, maximum = Number.MAX_SAFE_INTEGER) => {
      const number = Number(value);
      return Number.isFinite(number) && number >= 0 && number <= maximum
        ? number
        : null;
    };
    const count = (value) => {
      const number = known(value, 1_000_000_000);
      return number === null ? "Not reported" : compactNumber(number);
    };
    const bytes = (value) => {
      const number = known(value, 2 ** 50);
      return number === null ? "Not shared" : formatBytes(number);
    };
    const usage = (usedValue, totalValue) => {
      const used = known(usedValue, 2 ** 50);
      const total = known(totalValue, 2 ** 50);
      if (used === null || total === null || total <= 0 || used > total) {
        return "Not shared";
      }
      return `${formatBytes(used)} / ${formatBytes(total)} · ${(
        (used / total) *
        100
      ).toFixed(1)}%`;
    };
    const date = (value) => {
      const timestamp = known(value);
      if (timestamp === null || timestamp <= 0) return "Not reported";
      const parsed = new Date(timestamp);
      return Number.isNaN(parsed.getTime())
        ? "Not reported"
        : parsed.toLocaleString();
    };
    const syncAge = known(node?.syncAgeMs);
    const syncAgeLabel =
      syncAge === null
        ? "Not reported"
        : syncAge < 60_000
          ? "Less than one minute"
          : `${Math.floor(syncAge / 60_000)} minutes`;
    const cpu = known(node?.cpuPercent, 100);
    const commit = /^[0-9a-f]{40,64}$/.test(String(node?.commit || ""))
      ? String(node.commit)
      : "Not reported";
    const route =
      node?.cloneAvailable === true
        ? "Verified and clone-ready"
        : String(node?.integrity || "") === "rejected"
          ? "Blocked by integrity policy"
          : String(node?.integrity || "") === "healing"
            ? "Online; integrity is being re-verified"
            : "Not currently verified for cloning";
    const repositories = Array.isArray(node?.repositories)
      ? node.repositories.slice(0, 32)
      : [];
    return `
      <section class="world-feature-card" data-world-mirror-node-detail>
        <div class="world-notice ${
          node?.cloneAvailable === true
            ? "world-notice-safe"
            : "world-notice-warning"
        }">
          <strong>${escapeHTML(route)}</strong>
          <span>Online presence, signed repository publication, content integrity, and route eligibility are separate checks. An online node is not automatically trustworthy.</span>
        </div>
        <dl class="world-technical-list">
          <div><dt>Node</dt><dd>${escapeHTML(node?.name || "Not reported")}</dd></div>
          <div><dt>Node id</dt><dd class="world-break">${escapeHTML(
            node?.nodeId || "Not reported",
          )}</dd></div>
          <div><dt>Platform / version</dt><dd>${escapeHTML(
            [node?.platform, node?.version ? `v${String(node.version).replace(/^v/i, "")}` : ""]
              .filter(Boolean)
              .join(" · ") || "Not reported",
          )}</dd></div>
          <div><dt>Git HEAD</dt><dd class="world-break"><code>${escapeHTML(
            commit,
          )}</code></dd></div>
          <div><dt>Branch</dt><dd>${escapeHTML(node?.branch || "Not reported")}</dd></div>
          <div><dt>Integrity</dt><dd>${escapeHTML(
            node?.integrity || "unknown",
          )}${node?.behind === true ? " · behind current state" : ""}</dd></div>
          <div><dt>Last seen</dt><dd>${escapeHTML(date(node?.lastSeen))}</dd></div>
          <div><dt>Last sync</dt><dd>${escapeHTML(
            date(node?.lastSync),
          )}${
            syncAge === null
              ? ""
              : ` · ${escapeHTML(syncAgeLabel)} ago`
          }</dd></div>
          <div><dt>CPU</dt><dd>${escapeHTML(
            cpu === null ? "Not shared" : `${cpu.toFixed(1)}%`,
          )}</dd></div>
          <div><dt>Memory</dt><dd>${escapeHTML(
            usage(node?.memoryUsedBytes, node?.memoryTotalBytes),
          )}</dd></div>
          <div><dt>Disk</dt><dd>${escapeHTML(
            usage(node?.diskUsedBytes, node?.diskTotalBytes),
          )}</dd></div>
          <div><dt>Repository bytes</dt><dd>${escapeHTML(
            bytes(node?.sizeBytes),
          )}</dd></div>
          <div><dt>Commits</dt><dd>${escapeHTML(count(node?.commitCount))}</dd></div>
          <div><dt>Branches</dt><dd>${escapeHTML(count(node?.branchCount))}</dd></div>
          <div><dt>Pull requests</dt><dd>${escapeHTML(
            count(node?.pullCount),
          )}</dd></div>
          <div><dt>Issues</dt><dd>${escapeHTML(count(node?.issueCount))}</dd></div>
          <div><dt>Discussions</dt><dd>${escapeHTML(
            count(node?.discussionCount),
          )}</dd></div>
          <div><dt>Artifacts</dt><dd>${escapeHTML(
            count(node?.artifactCount),
          )}</dd></div>
          <div><dt>Worktrees</dt><dd>${escapeHTML(
            count(node?.worktreeCount),
          )}</dd></div>
          <div><dt>Clones served</dt><dd>${escapeHTML(
            count(node?.clonesServed),
          )}</dd></div>
          <div><dt>Web requests served</dt><dd>${escapeHTML(
            count(node?.websiteServed),
          )}</dd></div>
        </dl>
        <h3>Public mirrored repositories</h3>
        ${
          repositories.length
            ? `<ul class="world-detail-list">${repositories
                .map(
                  (repository) =>
                    `<li><strong>${escapeHTML(
                      repository.owner && repository.name
                        ? `${repository.owner}/${repository.name}`
                        : "Repository identity not reported",
                    )}</strong> · ${escapeHTML(
                      repository.cloneAvailable && repository.integrity === "ok"
                        ? "clone-ready"
                        : repository.integrity || "unverified",
                    )} · ${escapeHTML(bytes(repository.sizeBytes))}</li>`,
                )
                .join("")}</ul>`
            : '<p class="world-empty-state">Repository identity was not reported for this live node.</p>'
        }
        <p class="world-panel-footnote">Resource measurements and repository counters are operator-reported, bounded values signed into the public catalog. They are not independent performance audits and may be stale between publications.</p>
      </section>`;
  }

  openMirrorNodeDetail(node) {
    const detail = this.$("[data-world-detail]");
    const backdrop = this.$("[data-world-detail-backdrop]");
    if (!detail || !backdrop || !node) return;
    detail.dataset.openLandmark = "routing";
    detail.style.setProperty("--detail-color", "#80e8ff");
    detail.innerHTML = `
      <header class="world-detail-header">
        <div>
          <p class="world-eyebrow">MIRROR SERVER / LIVE PUBLIC STATUS</p>
          <h2 id="world-detail-title">${escapeHTML(
            node.name || "Mirror node",
          )}</h2>
        </div>
        <button class="world-detail-close" type="button" data-world-detail-close aria-label="Close mirror server details">×</button>
      </header>
      <div class="world-detail-scroll">
        <p class="world-detail-summary">The readable technical equivalent of this server cabinet’s front display.</p>
        ${this.mirrorNodeTechnicalHTML(node)}
      </div>`;
    detail.dataset.open = "true";
    detail.setAttribute("aria-hidden", "false");
    backdrop.dataset.open = "true";
    window.setTimeout(
      () => detail.querySelector("[data-world-detail-close]")?.focus(),
      120,
    );
  }

  rewardPanelHTML() {
    const address = String(this.rewardState?.address || "");
    const network = String(this.rewardState?.network || "mainnet-beta");
    const balance = Number(
      this.rewardState?.balanceSol ??
        this.rewardState?.balance ??
        this.rewardState?.sol ??
        0,
    );
    const transactions = Array.isArray(this.rewardState?.transactions)
      ? this.rewardState.transactions.slice(0, 5)
      : [];
    const contribution = this.pendingContribution;
    const pending = Array.isArray(this.pendingRewards)
      ? this.pendingRewards.slice(0, 10)
      : [];
    const poolExplorer = String(this.rewardState?.explorerUrl || "");
    const latestRound = this.rewardState?.latestRound || null;
    return `
      <section class="world-feature-card" aria-label="Global Reward Pool status">
        <div class="world-finance-grid">
          <div>
            <span>Public pool balance</span>
            <strong>${address ? `${escapeHTML(String(balance))} SOL` : "Unavailable"}</strong>
          </div>
          <div>
            <span>Solana network</span>
            <strong>${escapeHTML(network)}</strong>
          </div>
          <div>
            <span>Pending allocation window</span>
            <strong>24 hours</strong>
          </div>
        </div>
        <dl class="world-technical-list">
          <div><dt>Public address</dt><dd class="world-break">${
            poolExplorer && address
              ? `<a href="${escapeHTML(poolExplorer)}" target="_blank" rel="noopener noreferrer">${escapeHTML(address)}</a>`
              : escapeHTML(address || "No verified pool address configured")
          }</dd></div>
          <div><dt>User-owned funds</dt><dd>Remain in each user’s self-custodial wallet.</dd></div>
          <div><dt>Community-pool funds</dt><dd>Visible public on-chain state; signing authority stays in the first instance owner’s local Qt client.</dd></div>
          <div><dt>Pending rewards</dt><dd>Ledger allocation only; funds stay at the source and the allocation expires after 24 hours.</dd></div>
          <div><dt>Completed transfers</dt><dd>Shown only after an exact finalized System transfer is independently verified.</dd></div>
          <div><dt>Randomized round</dt><dd>${
            latestRound
              ? `${escapeHTML(latestRound.status || "scheduled")} · ${escapeHTML(
                  latestRound.roundId || "",
                )} · ${Number(latestRound.eligibleCount || 0)} eligible`
              : "No eligible round has been scheduled."
          }</dd></div>
          <div><dt>Selection proof</dt><dd>${
            latestRound?.selection
              ? `${escapeHTML(
                  latestRound.selection.algorithm || "",
                )} selected ${escapeHTML(
                  latestRound.selection.selectedNodeId || "a public node",
                )} from snapshot ${escapeHTML(
                  latestRound.selection.snapshotHash || "",
                )}`
              : "Selection waits for the scheduled time and a finalized public Solana blockhash."
          }</dd></div>
        </dl>
        <h3>Join or fund the community reward program</h3>
        <p class="world-panel-footnote">Joining is a voluntary direct contribution to the configured public pool. It does not purchase ownership, guaranteed rewards, investment returns, or governance control; your own wallet reviews and signs the transfer.</p>
        <div class="world-reward-form">
          <label>
            <span>Amount (SOL)</span>
            <input data-world-contribution-amount type="number" min="0.00001" max="100" step="0.00001" value="0.001" inputmode="decimal" />
          </label>
          <label>
            <span>Distribution choice</span>
            <select data-world-contribution-mode>
              <option value="trickle">Add to randomized trickle pool</option>
              <option value="instant_all_nodes">Instant intent for all eligible nodes</option>
            </select>
          </label>
          <button type="button" data-world-contribution-prepare ${address ? "" : "disabled"}>
            Prepare direct wallet transfer
          </button>
        </div>
        ${
          contribution
            ? `<div class="world-contribution-intent" data-world-contribution-intent>
                <strong>Prepared public intent · ${escapeHTML(
                  contribution.mode || "",
                )}</strong>
                <span>${escapeHTML(
                  contribution.amountSol || "",
                )} SOL → ${escapeHTML(contribution.poolAddress || address)}</span>
                <span>Expires ${escapeHTML(
                  new Date(Number(contribution.expiresAt || 0)).toLocaleString(),
                )}</span>
                ${
                  contribution.uri
                    ? `<a class="world-primary-action" href="${escapeHTML(
                        contribution.uri,
                      )}">Review in self-custodial wallet</a>`
                    : ""
                }
                <label>
                  <span>Finalized transaction signature</span>
                  <input data-world-contribution-signature type="text" autocomplete="off" spellcheck="false" placeholder="Paste public Solana signature" />
                </label>
                <button type="button" data-world-contribution-confirm>Verify finalized transfer</button>
                <small>Never paste a private key, seed, or recovery phrase. A reference-bound direct transfer is required; visual particles do not prove payment.</small>
              </div>`
            : ""
        }
        <h3>Your pending allocations</h3>
        <div class="world-pending-rewards">
          ${
            pending.length
              ? pending
                  .map((item) => {
                    const expiresAt = Number(item.expiresAt || 0);
                    const remaining = Math.max(0, expiresAt - Date.now());
                    const remainingHours = Math.ceil(
                      remaining / (60 * 60 * 1000),
                    );
                    const claimable =
                      String(item.status || "") === "pending_wallet" &&
                      remaining > 0;
                    return `<article data-world-pending-reward>
                      <strong>${escapeHTML(
                        item.amountLamports
                          ? `${Number(item.amountLamports) / 1_000_000_000} SOL`
                          : "Community allocation",
                      )}</strong>
                      <span>Status: ${escapeHTML(item.status || "pending")}</span>
                      <span>Expires ${escapeHTML(
                        new Date(expiresAt).toLocaleString(),
                      )} (${remainingHours}h remaining)</span>
                      ${
                        claimable
                          ? `<label><span>Public self-custodial wallet</span><input data-world-pending-wallet type="text" autocomplete="off" spellcheck="false" placeholder="Solana public address" /></label>
                             <button type="button" data-world-pending-claim="${escapeHTML(
                               item.rewardId || "",
                             )}">Use this public address</button>`
                          : ""
                      }
                    </article>`;
                  })
                  .join("")
              : `<p class="world-empty-state">${
                  readSession()?.sessionToken
                    ? "No pending walletless allocations."
                    : "Sign in to view private pending-allocation status."
                }</p>`
          }
        </div>
        <h3>Recent verified reward events</h3>
        <div class="world-event-list">
          ${
            transactions.length
              ? transactions
                  .map(
                    (item) => `
                      <div>
                        <strong>${escapeHTML(
                          item.type || "Completed on-chain transfer",
                        )}</strong>
                        <span>${escapeHTML(
                          [
                            item.recipientNode,
                            item.amountSol ? `${item.amountSol} SOL` : "",
                          ]
                            .filter(Boolean)
                            .join(" · ") || "Verified event",
                        )}</span>
                        ${
                          item.explorerUrl
                            ? `<a href="${escapeHTML(
                                item.explorerUrl,
                              )}" target="_blank" rel="noopener noreferrer">View finalized transaction</a>`
                            : ""
                        }
                      </div>`,
                  )
                  .join("")
              : `<p class="world-empty-state">No verified on-chain reward events were returned. Fountain particles remain illustrative.</p>`
          }
        </div>
        <div class="world-notice world-notice-warning">
          <strong>ForkMesh is non-custodial</strong>
          <span>ForkMesh does not hold or control your funds. Never enter a private key or recovery phrase. Community rewards are voluntary incentives, not investments or guaranteed returns.</span>
        </div>
      </section>`;
  }

  repositoryMapStatusHTML() {
    if (this.repositoryMapState === "ready" && this.activeRepository) {
      return this.repositoryExplorerHTML(this.activeRepository);
    }
    if (this.repositoryMapState === "loading") {
      return `<p class="world-empty-state">Loading ${escapeHTML(
        this.repositoryMapTarget || "the selected repository",
      )} from commit-pinned authorized HTTPS endpoints…</p>`;
    }
    if (this.repositoryMapState === "unavailable") {
      return `
        <div class="world-notice world-notice-warning" data-world-repository-map-state="unavailable">
          <strong>Repository map unavailable</strong>
          <span>The live catalog or commit-pinned repository data could not be verified. ForkMesh did not substitute sample files, guessed entries, or stale analysis.</span>
        </div>`;
    }
    return `<p class="world-empty-state">${
      this.repositories.length
        ? "Choose an available repository to build its authorized, size-aware file map."
        : "A live, authorized repository is required before a three-dimensional map can run."
    }</p>`;
  }

  renderRepositoryMapStatus() {
    const explorer = this.$("[data-world-repo-explorer]");
    if (explorer) explorer.innerHTML = this.repositoryMapStatusHTML();
    this.updateRepositoryReviewMode();
    if (this.repositoryView === "review" && this.pullReview?.state === "ready") {
      window.requestAnimationFrame(() =>
        this.setupPullReviewScrollTracking(),
      );
    }
  }

  repositoryPanelHTML() {
    const repos = this.repositories.slice(0, 8);
    const catalogEmpty = this.repositoryCatalogState === "empty";
    const catalogUnavailable = this.repositoryCatalogState === "unavailable";
    return `
      <div class="world-repositories-panel" aria-label="Repository portals">
        ${
          repos.length
            ? repos
                .map((repo) => {
            const path = `/${encodeURIComponent(repo.owner)}/${encodeURIComponent(repo.name)}`;
            const state = repo.isPrivate
              ? "Private to you"
              : repo.liveHost
                ? repo.source === "remote-clone"
                  ? "Mirrored"
                  : "Live"
                : repo.source === "external"
                  ? "Stub only"
                  : "Unavailable";
            const meta = [
              repo.language,
              repo.mirrorCount ? `${repo.mirrorCount} mirrors` : "",
              Number.isSafeInteger(repo.pullCount)
                ? `${compactNumber(repo.pullCount)} pull requests`
                : "",
            ]
              .filter(Boolean)
              .join(" · ");
            const external = safeHTTPURL(repo.externalUrl);
            return `
              <div class="world-repo-row">
                <span>
                  <strong>${escapeHTML(repo.owner)}/${escapeHTML(repo.name)}</strong>
                  <span>${escapeHTML(meta || repo.description || "Repository portal")}</span>
                </span>
                <span class="world-repo-state">${escapeHTML(state)}</span>
                <span class="world-repo-actions">
                  ${
                    repo.liveHost || repo.isPrivate
                      ? `<button type="button" data-world-repo-map="${escapeHTML(
                          `${repo.owner}/${repo.name}`,
                        )}">Explore 3D map</button>`
                      : `<button type="button" data-world-mirror-request="${escapeHTML(
                          `${repo.owner}/${repo.name}`,
                        )}">Volunteer to mirror</button>`
                  }
                  <button type="button" data-world-security-scan="${escapeHTML(
                    `${repo.owner}/${repo.name}`,
                  )}">Security clipboard</button>
                  <a href="${escapeHTML(external || path)}">${
                    external ? "Original source" : "Repository"
                  }</a>
                </span>
              </div>`;
                })
                .join("")
            : `<div class="world-notice ${
                catalogUnavailable
                  ? "world-notice-warning"
                  : ""
              }" data-world-repository-catalog-state="${escapeHTML(
                this.repositoryCatalogState,
              )}">
                <strong>${
                  catalogUnavailable
                    ? "Repository catalog unavailable"
                    : catalogEmpty
                      ? "No authorized repositories listed"
                      : "Repository catalog loading"
                }</strong>
                <span>${
                  catalogUnavailable
                    ? "The live catalog request failed. ForkMesh does not substitute demo repositories or imply that a mirror is online."
                    : catalogEmpty
                      ? "The catalog returned no public or account-authorized repositories. No repository can be opened or analyzed from this panel."
                      : "Waiting for the live repository catalog."
                }</span>
              </div>`
        }
        <div data-world-repo-explorer>
          ${this.repositoryMapStatusHTML()}
        </div>
      </div>`;
  }

  organizationPanelHTML() {
    const orgs = this.organizations.length
      ? this.organizations
      : [{ name: "Public lobby", role: "guest", placeholder: true }];
    return `
      <section class="world-feature-card" aria-label="Organization spaces">
        <div class="world-building-grid">
          ${orgs
            .map((org) => {
              const name = sanitizePresenceText(
                org.name || org.org,
                "organization",
                50,
              );
              const displayName = sanitizePresenceText(
                org.displayName,
                name,
                80,
              );
              const role = sanitizePresenceText(
                org.viewerRole || org.role,
                "guest",
                24,
              );
              const logoURL = safeHTTPURL(org.logoUrl);
              const worldAccess =
                org.worldAccess && typeof org.worldAccess === "object"
                  ? org.worldAccess
                  : {
                      lobby: "public",
                      floors: "restricted",
                      offices: "restricted",
                    };
              const capabilities =
                org.worldCapabilities &&
                typeof org.worldCapabilities === "object"
                  ? org.worldCapabilities
                  : {
                      lobby: true,
                      floors: role !== "guest",
                      offices: role !== "guest",
                    };
              const repos = Array.isArray(org.repos) ? org.repos.slice(0, 12) : [];
              const teams = Array.isArray(org.teamList)
                ? org.teamList.slice(0, 12)
                : [];
              const members = Array.isArray(org.memberList)
                ? org.memberList.slice(0, 12)
                : [];
              const office = members.find(
                (member) =>
                  String(member.name || "").toLowerCase() ===
                  String(this.identity.name || "").toLowerCase(),
              );
              const lobbyURL = `/dashboard/chat?org=${encodeURIComponent(name)}`;
              const settingsURL = `/dashboard/settings/organizations?org=${encodeURIComponent(
                name,
              )}`;
              const selectedOffice =
                this.activeOffice?.org === name
                  ? members.find(
                      (member) =>
                        String(member.name || "").toLowerCase() ===
                        String(this.activeOffice.member || "").toLowerCase(),
                    )
                  : null;
              const selectedPresence = selectedOffice
                ? [...this.remotePlayers.values()].find(
                    (player) =>
                      String(player.name || "").toLowerCase() ===
                      String(selectedOffice.name || "").toLowerCase(),
                  )
                : null;
              return `
                <article class="world-building-card">
                  <span class="world-building-logo">${
                    logoURL
                      ? `<img src="${escapeHTML(
                          logoURL,
                        )}" alt="${escapeHTML(
                          displayName,
                        )} logo" loading="lazy" referrerpolicy="no-referrer" />`
                      : `<span aria-hidden="true">${escapeHTML(
                          displayName.slice(0, 2).toUpperCase(),
                        )}</span>`
                  }</span>
                  <div><strong>${escapeHTML(displayName)}</strong><span>${escapeHTML(
                    org.placeholder ? "Public preview" : `${role} access`,
                  )}</span><p>${escapeHTML(
                    String(org.description || "Organization collaboration space").slice(
                      0,
                      180,
                    ),
                  )}</p></div>
                  <ul class="world-org-floors">
                    <li><span>Lobby</span><em>${escapeHTML(
                      `${worldAccess.lobby || "public"} · ${
                        capabilities.lobby ? "enterable" : "restricted"
                      }`,
                    )}</em></li>
                    ${teams
                      .map(
                        (team) =>
                          `<li><span>${escapeHTML(
                            sanitizePresenceText(team.team, "Team", 50),
                          )} team floor</span><em>${escapeHTML(
                            sanitizePresenceText(
                              team.permission,
                              "role checked",
                              24,
                            ),
                          )} · ${Number(team.members || 0)} members</em></li>`,
                      )
                      .join("")}
                    ${repos
                      .map(
                        (repo) =>
                          `<li><span>Repository bed · ${escapeHTML(
                            sanitizePresenceText(repo.repo, "repository", 60),
                          )}</span><em>Node ${escapeHTML(
                            sanitizePresenceText(repo.node, "linked", 50),
                          )}</em></li>`,
                      )
                      .join("")}
                    <li><span>Floor visibility</span><em>${escapeHTML(
                      `${worldAccess.floors || "restricted"} · ${
                        capabilities.floors
                          ? "repository beds visible"
                          : "repository beds hidden"
                      }`,
                    )}</em></li>
                  </ul>
                  <section class="world-office-card">
                    <strong>${office ? `${escapeHTML(office.name)}’s office` : "Personal offices"}</strong>
                    <span>${escapeHTML(
                      capabilities.offices
                        ? office
                          ? "Member workspace available"
                          : "Authorized office directory"
                        : `Offices are ${worldAccess.offices || "restricted"}`,
                    )}</span>
                    <small>Repositories: ${repos.length} · activity: user-controlled · achievements: profile-controlled</small>
                    ${
                      capabilities.offices && members.length
                        ? `<div class="world-detail-actions">${members
                            .map(
                              (member) =>
                                `<button type="button" data-world-office-org="${escapeHTML(
                                  name,
                                )}" data-world-office-member="${escapeHTML(
                                  sanitizePresenceText(
                                    member.name,
                                    "member",
                                    50,
                                  ),
                                )}">Open ${escapeHTML(
                                  sanitizePresenceText(
                                    member.name,
                                    "member",
                                    50,
                                  ),
                                )}’s office</button>`,
                            )
                            .join("")}</div>`
                        : ""
                    }
                  </section>
                  ${
                    selectedOffice
                      ? `<section class="world-office-card" data-world-active-office>
                          <strong>${escapeHTML(
                            sanitizePresenceText(
                              selectedOffice.name,
                              "Member",
                              50,
                            ),
                          )} · ${escapeHTML(displayName)}</strong>
                          <span>Organization role: ${escapeHTML(
                            sanitizePresenceText(
                              selectedOffice.role,
                              "member",
                              24,
                            ),
                          )}${
                            Number(selectedOffice.since) > 0
                              ? ` · member since ${escapeHTML(
                                  new Date(
                                    Number(selectedOffice.since),
                                  ).toLocaleDateString(),
                                )}`
                              : ""
                          }</span>
                          <small>Current work: ${escapeHTML(
                            selectedPresence?.activity &&
                              selectedPresence.activity !== "hidden"
                              ? selectedPresence.activity
                              : "Not publicly shared",
                          )}</small>
                          <ul class="world-org-floors">${
                            repos.length
                              ? repos
                                  .map(
                                    (repo) =>
                                      `<li><span>${escapeHTML(
                                        sanitizePresenceText(
                                          repo.repo,
                                          "repository",
                                          60,
                                        ),
                                      )}</span><em>Authorized organization repository floor</em></li>`,
                                  )
                                  .join("")
                              : "<li><span>No visible repository floor</span><em>Access remains role-gated</em></li>"
                          }</ul>
                          <div class="world-detail-actions">
                            <a href="/@${encodeURIComponent(
                              sanitizePresenceText(
                                selectedOffice.name,
                                "member",
                                50,
                              ),
                            )}">Open public profile and achievements</a>
                            <a href="${escapeHTML(
                              lobbyURL,
                            )}">Collaborate in organization lobby</a>
                          </div>
                        </section>`
                      : ""
                  }
                  <div class="world-repo-actions">
                    ${
                      capabilities.lobby
                        ? `<a href="${escapeHTML(lobbyURL)}">Enter lobby</a>`
                        : `<button type="button" disabled>Lobby restricted</button>`
                    }
                    <a href="/${encodeURIComponent(name)}">Watch projects</a>
                    <a href="/dashboard/settings/activitypub?org=${encodeURIComponent(
                      name,
                    )}">Follow on Fediverse</a>
                    ${
                      org.placeholder
                        ? ""
                        : `<a href="${escapeHTML(settingsURL)}">Manage access</a>`
                    }
                  </div>
                </article>`;
            })
            .join("")}
        </div>
        <p class="world-panel-footnote">An organization role never grants access outside that organization. Mirror operators cannot read private repository plaintext merely because they host encrypted bytes.</p>
      </section>`;
  }

  fediversePanelHTML() {
    const centers = [
      {
        name: "Mastodon",
        copy: "Instances, languages, topics, registration, and explicitly attested public follower connections.",
        url: "https://mastodon.social/@forkmesh",
      },
      {
        name: "Lemmy",
        copy: "Instances, communities, topics, public activity, and moderation or registration state.",
        url: "",
      },
      {
        name: "Twitter / X",
        copy: "ForkMesh’s public profile link only; no private relationship graph is imported.",
        url: "https://x.com/forkmesh",
      },
      {
        name: "Reddit",
        copy: "ForkMesh’s public profile link only; private browsing and subscriptions stay private.",
        url: "https://www.reddit.com/user/forkmesh",
      },
    ];
    const directoryEntries = [
      ...(Array.isArray(this.fediverseDirectory?.mastodon)
        ? this.fediverseDirectory.mastodon.map((item) => ({
            ...item,
            network: "Mastodon",
          }))
        : []),
      ...(Array.isArray(this.fediverseDirectory?.lemmy)
        ? this.fediverseDirectory.lemmy.map((item) => ({
            ...item,
            network: "Lemmy",
          }))
        : []),
      ...(Array.isArray(this.fediverseDirectory?.x)
        ? this.fediverseDirectory.x.map((item) => ({
            ...item,
            network: "X",
          }))
        : []),
      ...(Array.isArray(this.fediverseDirectory?.reddit)
        ? this.fediverseDirectory.reddit.map((item) => ({
            ...item,
            network: "Reddit",
          }))
        : []),
    ].slice(0, 40);
    return `
      <section class="world-feature-card" aria-label="Social and Fediverse centers">
        <div class="world-social-grid">
          ${centers
            .map(
              ({ name, copy, url }) => `
                <article><span aria-hidden="true">${escapeHTML(name.slice(0, 1))}</span><strong>${escapeHTML(
                  name,
                )}</strong><p>${escapeHTML(copy)}</p>${
                  url
                    ? `<a href="${escapeHTML(
                        url,
                      )}" target="_blank" rel="noopener noreferrer">Open public center</a>`
                    : "<small>Participating instances appear below when an approved directory record exists.</small>"
                }</article>`,
            )
            .join("")}
        </div>
        <h3>Participating public directory records</h3>
        <div class="world-instance-list">
          ${
            directoryEntries.length
              ? directoryEntries
                  .map((instance) => {
                    const url = safeHTTPURL(instance.url);
                    const iconURL = safeHTTPURL(instance.icon);
                    const name = sanitizePresenceText(
                      instance.name,
                      "Public instance",
                      80,
                    );
                    const details = [
                      ...(Array.isArray(instance.languages)
                        ? instance.languages.slice(0, 4)
                        : []),
                      ...(Array.isArray(instance.topics)
                        ? instance.topics.slice(0, 4)
                        : []),
                      instance.registration || "",
                      instance.moderation || "",
                      instance.publicActivity || "",
                    ]
                      .filter(Boolean)
                      .map((value) => String(value).slice(0, 40))
                      .join(" · ");
                    return `
                      <article>
                        <span class="world-instance-icon">${
                          iconURL
                            ? `<img src="${escapeHTML(
                                iconURL,
                              )}" alt="" referrerpolicy="no-referrer" />`
                            : escapeHTML(name.slice(0, 1).toUpperCase())
                        }</span>
                        <div><strong>${escapeHTML(name)}</strong><span>${escapeHTML(
                          instance.network,
                        )}</span><p>${escapeHTML(
                          String(instance.description || "").slice(0, 180),
                        )}</p><small>${escapeHTML(details)}</small>
                        ${
                          Array.isArray(instance.communities) &&
                          instance.communities.length
                            ? `<small>Communities: ${escapeHTML(
                                instance.communities
                                  .slice(0, 6)
                                  .map((item) =>
                                    String(item.name || item).slice(0, 40),
                                  )
                                  .join(", "),
                              )}</small>`
                            : ""
                        }
                        ${
                          Array.isArray(instance.relationships) &&
                          instance.relationships.length
                            ? `<small>Public instance links: ${escapeHTML(
                                instance.relationships
                                  .slice(0, 6)
                                  .map((item) =>
                                    String(item.name || item).slice(0, 40),
                                  )
                                  .join(", "),
                              )}</small>`
                            : ""
                        }
                        ${
                          Array.isArray(instance.approvedSubscriptions) &&
                          instance.approvedSubscriptions.length
                            ? `<small>User-approved subscriptions: ${escapeHTML(
                                instance.approvedSubscriptions
                                  .filter(
                                    (item) =>
                                      item?.consent === true &&
                                      item?.public === true,
                                  )
                                  .slice(0, 6)
                                  .map((item) =>
                                    String(item.name || item.community || "").slice(
                                      0,
                                      40,
                                    ),
                                  )
                                  .filter(Boolean)
                                  .join(", "),
                              )}</small>`
                            : ""
                        }
                        <div class="world-follower-orbit" aria-label="User-approved public connections">
                          ${
                            Array.isArray(
                              instance.network === "Mastodon"
                                ? instance.consentedFollowers
                                : instance.consentedProfiles,
                            )
                              ? (instance.network === "Mastodon"
                                  ? instance.consentedFollowers
                                  : instance.consentedProfiles
                                )
                                  .filter(
                                    (follower) =>
                                      follower?.consent === true &&
                                      follower?.public === true &&
                                      follower?.consentEvidence
                                        ?.oauthVerifiedByForkMesh === false,
                                  )
                                  .slice(0, 12)
                                  .map((follower) => {
                                    const avatar = safeHTTPURL(follower.avatar);
                                    const handle = String(
                                      follower.handle || "Public follower",
                                    ).slice(0, 80);
                                    const source = sanitizePresenceText(
                                      follower?.consentEvidence?.source,
                                      "operator attestation",
                                      40,
                                    );
                                    return `<span title="${escapeHTML(
                                      `${handle} · ${source} · not OAuth-verified by ForkMesh`,
                                    )}">${
                                      avatar
                                        ? `<img src="${escapeHTML(
                                            avatar,
                                          )}" alt="${escapeHTML(
                                            handle,
                                          )}" referrerpolicy="no-referrer" />`
                                        : escapeHTML(handle.slice(0, 1))
                                    }</span>`;
                                  })
                                  .join("")
                              : ""
                          }
                        </div></div>
                        ${
                          url
                            ? `<a href="${escapeHTML(
                                url,
                              )}" target="_blank" rel="noopener noreferrer">Visit public instance</a>`
                            : ""
                        }
                      </article>`;
                  })
                  .join("")
              : `<p class="world-empty-state">No participating service has an approved public directory record yet.</p>`
          }
        </div>
        <div class="world-notice world-notice-safe">
          <strong>Consent boundary</strong>
          <span>Directory records are operator-ingested public/consent attestations, labeled with their consent source and review time; ForkMesh does not claim to independently own or OAuth-verify remote accounts. Follower faces appear only for records marked public, public-account-visible, and user-consented. Private followers, private accounts, search terms, and browsing history are excluded.</span>
        </div>
        <h3>Known automated agents</h3>
        <div class="world-instance-list world-bot-list">
          ${
            this.botDirectory.length
              ? this.botDirectory
                  .map((bot) => {
                    const verified = bot.verified === true;
                    return `
                      <article>
                        <span class="world-instance-icon" aria-hidden="true">⌘</span>
                        <div>
                          <strong>${escapeHTML(
                            sanitizePresenceText(bot.name, "Automated agent", 80),
                          )}</strong>
                          <span>${escapeHTML(
                            sanitizePresenceText(bot.type, "Bot", 60),
                          )} · ${verified ? "Verified" : "Unverified"}</span>
                          <p>${escapeHTML(
                            String(bot.generalActivity || "General public automation").slice(
                              0,
                              180,
                            ),
                          )}</p>
                          <small>${escapeHTML(
                            String(
                              bot.publicResourceCategory ||
                                "No public resource category reported",
                            ).slice(0, 120),
                          )}</small>
                        </div>
                      </article>`;
                  })
                  .join("")
              : `<p class="world-empty-state">No deployed bot directory entries are available.</p>`
          }
        </div>
        <dl class="world-technical-list">
          <div><dt>Digest cadence</dt><dd>At most one meaningful automated post per repository or organization each day.</dd></div>
          <div><dt>Empty / duplicate updates</dt><dd>Suppressed before delivery.</dd></div>
          <div><dt>Owner controls</dt><dd>Preview, disable, and per-repository or per-organization settings.</dd></div>
          <div><dt>Attribution</dt><dd>Automated posts identify themselves and link to the relevant public update.</dd></div>
        </dl>
      </section>`;
  }

  securityPanelHTML() {
    const scan = this.securityScan || {};
    const findings = scan.findings || scan.summary || {};
    const state = scan.status || "Scan unavailable";
    const categories =
      scan.categories && typeof scan.categories === "object"
        ? JSON.stringify(scan.categories)
        : scan.categories || "See the authorized report for redacted finding detail.";
    const history = Array.isArray(this.securityHistory)
      ? this.securityHistory
      : [];
    const selectedRepository = this.securityRepository || "No repository selected";
    const triage = this.securityTriage || {};
    const reviewCounts =
      triage.falsePositiveStatus || scan.falsePositiveStatus || {};
    return `
      <section class="world-feature-card world-clipboard" aria-label="Latest security scan clipboard">
        <div class="world-clipboard-head">
          <div><span>REPOSITORY / STATUS</span><strong>${escapeHTML(
            selectedRepository,
          )} · ${escapeHTML(state)}</strong></div>
          <div><span>SCANNED COMMIT</span><strong>${escapeHTML(
            scan.commit || scan.commitHash || "Not reported",
          )}</strong></div>
        </div>
        <dl class="world-technical-list">
          <div><dt>Date and time</dt><dd>${escapeHTML(scan.scannedAt || scan.timestamp || "Not reported")}</dd></div>
          <div><dt>Scanner / model</dt><dd>${escapeHTML(
            [scan.scanner, scan.model, scan.modelVersion].filter(Boolean).join(" · ") ||
              "Not reported",
          )}</dd></div>
          <div><dt>Policy</dt><dd>${escapeHTML(scan.policy || scan.policyVersion || "Public redacted policy not reported")}</dd></div>
          <div><dt>Duration</dt><dd>${escapeHTML(scan.duration || scan.durationMs || "Not reported")}</dd></div>
          <div><dt>Included</dt><dd>${escapeHTML(
            Array.isArray(scan.included) ? scan.included.join(", ") : scan.included || "Not reported",
          )}</dd></div>
          <div><dt>Excluded</dt><dd>${escapeHTML(
            Array.isArray(scan.excluded) ? scan.excluded.join(", ") : scan.excluded || "Not reported",
          )}</dd></div>
          <div><dt>Severity totals</dt><dd>${escapeHTML(
            typeof findings === "object" ? JSON.stringify(findings) : findings || "Not reported",
          )}</dd></div>
          <div><dt>Dependency / secrets / static analysis</dt><dd>${escapeHTML(
            categories,
          )}</dd></div>
          <div><dt>False positives / human review</dt><dd>${escapeHTML(
            scan.reviewStatus || "Not reviewed",
          )}</dd></div>
          <div><dt>Finding review states</dt><dd>${escapeHTML(
            `Unreviewed ${Number(reviewCounts.unreviewed || 0)} · Confirmed ${Number(
              reviewCounts.confirmed || 0,
            )} · Dismissed ${Number(reviewCounts.dismissed || 0)}`,
          )}</dd></div>
          <div><dt>Recommended actions</dt><dd>${escapeHTML(
            Array.isArray(scan.recommendations)
              ? scan.recommendations.join("; ")
              : scan.recommendations || "Run or inspect the latest authorized scan.",
          )}</dd></div>
          <div><dt>Scope limitations</dt><dd>${escapeHTML(
            Array.isArray(scan.scopeLimitations)
              ? scan.scopeLimitations.join("; ") || "None reported"
              : scan.scopeLimitations || "Not reported",
          )}</dd></div>
        </dl>
        <h3>Scan and review record</h3>
        <ol class="world-orientation-list" data-world-security-history>
          ${
            history.length
              ? history
                  .slice(0, 10)
                  .map((record) => {
                    const clipboard = record?.clipboard || {};
                    return `<li><strong>${escapeHTML(
                      clipboard.status || "Scan status unavailable",
                    )}</strong><span>${escapeHTML(
                      clipboard.scannedAt || record?.receivedAt || "Time unavailable",
                    )} · commit ${escapeHTML(
                      clipboard.commitHash || "unknown",
                    )} · ${escapeHTML(
                      clipboard.reviewStatus || "No review action recorded",
                    )}</span></li>`;
                  })
                  .join("")
              : `<li><strong>No scan history returned</strong><span>Select a catalog repository. Missing and unauthorized private repositories fail closed without revealing whether a scan exists.</span></li>`
          }
        </ol>
        ${
          triage.canReview === true && Array.isArray(triage.findings)
            ? `<section class="world-security-triage" aria-label="Authorized finding review">
                <h3>Owner / security-reviewer triage</h3>
                <p class="world-panel-footnote">${escapeHTML(
                  triage.reviewRole || "Authorized reviewer",
                )} · scan ${escapeHTML(triage.scanId || "")} · commit ${escapeHTML(
                  triage.commitHash || "",
                )}</p>
                <div class="world-event-list">${triage.findings
                  .slice(0, 100)
                  .map(
                    (finding) => `<article>
                      <div><strong>${escapeHTML(
                        `${finding.severity || "unknown"} · ${
                          finding.ruleId || finding.category || "finding"
                        }`,
                      )}</strong><p><code>${escapeHTML(
                        finding.path || "redacted path",
                      )}${finding.line ? `:${escapeHTML(finding.line)}` : ""}</code> · ${escapeHTML(
                        finding.summary || "",
                      )}</p><small>Current: ${escapeHTML(
                        finding.falsePositiveStatus || "unreviewed",
                      )}</small></div>
                      <div class="world-detail-actions">
                        ${["unreviewed", "confirmed", "dismissed"]
                          .map(
                            (status) =>
                              `<button type="button" data-world-security-triage="${escapeHTML(
                                finding.id,
                              )}" data-world-security-status="${status}" ${
                                finding.falsePositiveStatus === status
                                  ? "disabled"
                                  : ""
                              }>${status}</button>`,
                          )
                          .join("")}
                      </div>
                    </article>`,
                  )
                  .join("")}</div>
              </section>`
            : ""
        }
        <div class="world-notice world-notice-warning">
          <strong>Scope-limited recommendation</strong>
          <span>Automated scans may miss vulnerabilities. Results apply only to the named commit; a clean scan is not a guarantee, and human review may still be required. Secrets, source, sensitive prompts, and exploit detail are redacted publicly.</span>
        </div>
        <div class="world-detail-actions">
          ${
            this.securityRepository
              ? `<a class="world-primary-action" href="/${escapeHTML(
                  this.securityRepository
                    .split("/")
                    .map((part) => encodeURIComponent(part))
                    .join("/"),
                )}">Open authorized repository review</a>`
              : `<button class="world-primary-action" type="button" data-world-detail-action="repositories">Choose a repository</button>`
          }
          <a class="world-secondary-action" href="/security-report">Report a security issue privately</a>
        </div>
      </section>`;
  }

  launchpadPanelHTML() {
    return `
      <section class="world-feature-card" aria-label="World destinations">
        <div class="world-region-grid">
          ${WORLD_REGIONS.map(
            (region) => `
              <article>
                <span>${escapeHTML(region.phase)}</span>
                <strong>${escapeHTML(region.label)}</strong>
                <p>Walking here never changes your lighting. Events use the same UTC schedule everywhere.</p>
                <button type="button" data-world-travel="${escapeHTML(
                  region.id,
                )}">Teleport to campus</button>
              </article>`,
          ).join("")}
          <article><span>Achievement space</span><strong>Sky campus</strong><p>Collaboration room with standard permission checks.</p><button type="button" data-world-travel="sky-campus">Take the launch elevator</button></article>
          <article><span>Repository world</span><strong>Code planet</strong><p>Opens the selected repository map and workshop tools.</p><button type="button" data-world-travel="code-planet">Enter repository portal</button></article>
          <article><span>Organization region</span><strong>Garden campus</strong><p>Organization-owned lobbies, offices, and project beds.</p><button type="button" data-world-travel="organization-region">Enter organization portal</button></article>
          <article><span>Community planets</span><strong>Planet atlas</strong><p>Achievement, event, and community-owned destinations with UTC schedules.</p><button type="button" data-world-travel="planet-atlas">Open planet atlas</button></article>
          <article><span>Community space</span><strong>Space station</strong><p>Scheduled presentation, chat, and moderated media room.</p><button type="button" data-world-travel="space-station">Board shuttle</button></article>
        </div>
        <p class="world-panel-footnote">Each portal moves your live avatar into the shared 3D destination. Signed-in collaborators can use its dedicated authenticated shared-key channel. The relay derives the default key and can read messages: <a href="/dashboard/chat?space=sky-campus">Sky campus</a> · <a href="/dashboard/chat?space=space-station">Space station</a> · <a href="/dashboard/chat?space=code-planet">Code planet</a> · <a href="/dashboard/chat?space=organization-region">Garden campus</a> · <a href="/dashboard/chat?space=planet-atlas">Planet atlas</a>.</p>
      </section>`;
  }

  setCurrentSpace(space) {
    this.currentSpace = WORLD_SPACE_IDS.has(space) ? space : "town-square";
    const url = new URL(location.href);
    if (this.currentSpace === "town-square") {
      url.searchParams.delete("space");
    } else {
      url.searchParams.set("space", this.currentSpace);
    }
    history.replaceState(history.state, "", `${url.pathname}${url.search}${url.hash}`);
    this.sendPresence({ type: "presence" });
  }

  travelTo(destination) {
    if (WORLD_REGIONS.some((region) => region.id === destination)) {
      this.setCurrentSpace(destination);
      if (this.world?.travelToRegion(destination)) {
        const region = WORLD_REGIONS.find((item) => item.id === destination);
        this.toast(
          `Arrived in ${region.label}. Your local light level is unchanged; shared events stay UTC-synchronized.`,
        );
        this.closeLandmark();
      }
      return;
    }
    const destinationPanels = {
      "sky-campus": "workshops",
      "space-station": "broadcast",
      "code-planet": "repositories",
      "organization-region": "organizations",
      "planet-atlas": "events",
    };
    const panel = destinationPanels[destination];
    if (panel && this.world?.travelToSpace?.(destination)) {
      this.setCurrentSpace(destination);
      this.openLandmark(panel);
      this.toast(
        `Arrived in ${destination.replaceAll("-", " ")}. Your avatar, realtime presence, tools, and dedicated encrypted collaboration channel now share this destination.`,
      );
    }
  }

  eventsPanelHTML() {
    const events = this.events.slice(0, 12);
    const notifications = this.notifications.slice(0, 20);
    const session = readSession();
    const formatter = new Intl.DateTimeFormat(undefined, {
      dateStyle: "medium",
      timeStyle: "short",
    });
    return `
      <div data-world-events-panel-content>
      <section class="world-feature-card" aria-label="Account notifications">
        <div class="world-panel-heading">
          <h3>Your notifications</h3>
          <span>${escapeHTML(
            session?.sessionToken
              ? `${this.notificationUnread} unread`
              : "Sign in to receive",
          )}</span>
        </div>
        <div class="world-event-list world-notification-list">
          ${
            notifications.length
              ? notifications
                  .map((item) => {
                    const instant = new Date(item.ts);
                    return `
                      <article data-world-notification-id="${escapeHTML(
                        item.id,
                      )}" data-unread="${String(!item.readAt)}">
                        <span>${escapeHTML(item.kind || "Update")}</span>
                        ${
                          item.readAt
                            ? ""
                            : '<em class="world-notification-unread">Unread</em>'
                        }
                        <strong>${escapeHTML(item.title)}</strong>
                        ${
                          item.body
                            ? `<p>${escapeHTML(item.body)}</p>`
                            : ""
                        }
                        <time datetime="${escapeHTML(
                          Number.isNaN(instant.getTime())
                            ? ""
                            : instant.toISOString(),
                        )}">${escapeHTML(
                          Number.isNaN(instant.getTime())
                            ? "Recently"
                            : formatter.format(instant),
                        )}</time>
                        ${
                          item.href
                            ? `<a href="${escapeHTML(
                                item.href,
                              )}" rel="noopener noreferrer">Open context</a>`
                            : ""
                        }
                      </article>`;
                  })
                  .join("")
              : `<p class="world-empty-state" data-world-notifications-state="${escapeHTML(
                  this.notificationsState,
                )}">${
                  this.notificationsState === "signed-out"
                    ? "Sign in to receive your private ForkMesh notification inbox inside the World."
                    : this.notificationsState === "unavailable"
                      ? "Your notification inbox is temporarily unavailable."
                      : this.notificationsState === "loading"
                        ? "Loading your notification inbox…"
                        : "No account notifications yet."
                }</p>`
          }
        </div>
        <div class="world-detail-actions">
          <button type="button" data-world-notifications-refresh>Refresh notifications</button>
          ${
            session?.sessionToken && this.notificationUnread
              ? `<button type="button" data-world-notifications-read>Mark all read</button>`
              : ""
          }
        </div>
        <p class="world-panel-footnote">This private inbox is fetched only with your signed-in account session. It is never included in multiplayer presence.</p>
      </section>
      <section class="world-feature-card" aria-label="UTC community events">
        <div class="world-panel-heading">
          <h3>Global World announcements</h3>
          <span>${
            this.eventsState === "unavailable"
              ? "PUBLIC · LAST KNOWN · UTC"
              : "PUBLIC · UTC"
          }</span>
        </div>
        <div class="world-event-list">
          ${
            events.length
              ? events.map((item) => {
              const instant = new Date(item.startsAt);
              const ends = new Date(item.endsAt);
              return `
                <article>
                  <span>${escapeHTML(item.type || "Event")}</span>
                  <strong>${escapeHTML(item.title)}</strong>
                  ${
                    item.description
                      ? `<p>${escapeHTML(item.description)}</p>`
                      : ""
                  }
                  <time datetime="${escapeHTML(item.startsAt)}">${escapeHTML(
                    Number.isNaN(instant.getTime())
                      ? item.startsAt
                      : formatter.format(instant),
                  )}</time>
                  <em>${escapeHTML(item.destination || "Town Square")}</em>
                  <small>Ends ${escapeHTML(
                    Number.isNaN(ends.getTime())
                      ? item.endsAt
                      : formatter.format(ends),
                  )}</small>
                </article>`;
                }).join("")
              : `<p class="world-empty-state" data-world-events-state="${escapeHTML(
                  this.eventsState,
                )}">${
                  this.eventsState === "unavailable"
                    ? "The live event service is unavailable. No seeded or demo announcement is being presented as scheduled."
                    : this.eventsState === "loading"
                      ? "Loading live UTC event announcements…"
                      : "No unexpired community events are currently scheduled."
                }</p>`
          }
        </div>
        <button type="button" data-world-events-refresh>Refresh live events</button>
        <p class="world-panel-footnote">Event instants are stored as UTC ISO-8601 values; the dates above are formatted in this device’s selected time zone.</p>
      </section>
      </div>`;
  }

  async refreshCommunityEvents(render = false) {
    try {
      const payload = await this.fetchJSON("/api/world/events", {
        auth: false,
        timeout: 5000,
        cache: "no-store",
      });
      this.events = normalizeCommunityEvents(payload);
      this.eventsState = this.events.length ? "ready" : "empty";
      this.setLandmarkCapability(
        "events",
        Array.isArray(payload?.events),
        LANDMARK_CONSTRUCTION_REASONS.events,
      );
    } catch (_) {
      this.eventsState = "unavailable";
      this.setLandmarkCapability(
        "events",
        false,
        LANDMARK_CONSTRUCTION_REASONS.events,
      );
    }
    this.updateNotificationBadge();
    this.announceWorldNotifications();
    if (render || this.isEventsPanelOpen()) this.refreshOpenEventsPanel();
  }

  startEventPolling() {
    window.clearInterval(this.eventsTimer);
    this.eventsTimer = window.setInterval(() => {
      this.refreshCommunityEvents(this.isEventsPanelOpen());
    }, 60000);
  }

  isEventsPanelOpen() {
    const detail = this.$("[data-world-detail]");
    return (
      detail?.dataset.open === "true" &&
      detail.dataset.openLandmark === "events"
    );
  }

  refreshOpenEventsPanel() {
    if (!this.isEventsPanelOpen()) return;
    const panel = this.$("[data-world-events-panel-content]");
    if (panel) panel.outerHTML = this.eventsPanelHTML();
  }

  updateNotificationBadge() {
    const badge = this.$("[data-world-notification-count]");
    if (!badge) return;
    const currentEvents = this.events.filter((item) => {
      const start = Date.parse(item.startsAt);
      const end = Date.parse(item.endsAt);
      const now = Date.now();
      return Number.isFinite(start) && Number.isFinite(end) && start <= now && end > now;
    }).length;
    const count = Math.max(
      0,
      Math.min(999, Number(this.notificationUnread || 0) + currentEvents),
    );
    badge.textContent = count > 99 ? "99+" : String(count);
    badge.hidden = count === 0;
    const button = badge.closest("[data-world-landmark='events']");
    if (button) {
      button.setAttribute(
        "aria-label",
        count
          ? `Open World notifications, ${count} active`
          : "Open World notifications",
      );
    }
  }

  announceWorldNotifications() {
    const now = Date.now();
    const globalEvents = this.events.filter(
      (item) =>
        Date.parse(item.startsAt) <= now &&
        Date.parse(item.endsAt) > now &&
        !this.seenWorldEvents.has(item.id),
    );
    const personalNotifications = this.notifications.filter(
      (item) => !item.readAt && !this.seenNotifications.has(item.id),
    );
    globalEvents.forEach((item) => this.seenWorldEvents.add(item.id));
    personalNotifications.forEach((item) =>
      this.seenNotifications.add(item.id),
    );
    const announcements = [];
    if (globalEvents.length) {
      announcements.push(
        `World announcement: ${globalEvents[0].title}${
          globalEvents.length > 1 ? ` (+${globalEvents.length - 1})` : ""
        }`,
      );
    }
    if (personalNotifications.length) {
      announcements.push(
        `New notification: ${personalNotifications[0].title}${
          personalNotifications.length > 1
            ? ` (+${personalNotifications.length - 1})`
            : ""
        }`,
      );
    }
    if (announcements.length) this.toast(announcements.join(" · "));
  }

  async refreshPersonalNotifications(render = false) {
    const session = readSession();
    if (!session?.sessionToken || !session?.nodeName) {
      this.notifications = [];
      this.notificationUnread = 0;
      this.notificationsState = "signed-out";
      this.notificationAccount = "";
      this.seenNotifications.clear();
      this.updateNotificationBadge();
      if (render || this.isEventsPanelOpen()) this.refreshOpenEventsPanel();
      return;
    }
    const account = String(session.nodeName).toLowerCase();
    if (account !== this.notificationAccount) {
      this.notifications = [];
      this.notificationUnread = 0;
      this.notificationAccount = account;
      this.seenNotifications.clear();
    }
    try {
      const payload = await this.fetchJSON(
        `/api/notifications?node=${encodeURIComponent(
          account,
        )}&limit=100`,
        { timeout: 5000, cache: "no-store" },
      );
      this.notifications = normalizeWorldNotifications(payload);
      this.notificationUnread = Math.max(0, Number(payload?.unread) || 0);
      this.notificationsState = this.notifications.length ? "ready" : "empty";
    } catch (_) {
      this.notifications = [];
      this.notificationUnread = 0;
      this.notificationsState = "unavailable";
    }
    this.updateNotificationBadge();
    this.announceWorldNotifications();
    if (render || this.isEventsPanelOpen()) this.refreshOpenEventsPanel();
  }

  async markWorldNotificationsRead() {
    const session = readSession();
    if (!session?.sessionToken || !session?.nodeName) return;
    try {
      await this.postJSON("/api/notifications", {
        node: String(session.nodeName).toLowerCase(),
        all: true,
      });
      this.notifications = this.notifications.map((item) => ({
        ...item,
        readAt: item.readAt || Date.now(),
      }));
      this.notificationUnread = 0;
      this.notificationsState = this.notifications.length ? "ready" : "empty";
      this.updateNotificationBadge();
      this.refreshOpenEventsPanel();
      this.toast("World notifications marked read.");
    } catch (_) {
      this.toast("World notifications could not be marked read.");
    }
  }

  startNotificationPolling() {
    window.clearInterval(this.notificationsTimer);
    this.notificationsTimer = window.setInterval(() => {
      if (!document.hidden) this.refreshPersonalNotifications(false);
    }, WORLD_NOTIFICATION_POLL_MS);
  }

  neighborhoodPanelHTML() {
    const availability =
      AVAILABILITY_OPTIONS.find((option) => option.id === this.settings.availability)
        ?.label || "Online";
    const hidden =
      !this.settings.privacy.activity ||
      (!this.settings.privacy.inactivity &&
        ["inactive", "recent"].includes(this.settings.availability));
    const publicNeighbors = [
      ...this.remotePlayers.values(),
      ...this.inactivePlayers,
    ];
    return `
      <section class="world-feature-card" aria-label="Contributor neighborhood controls">
        <div class="world-home-card">
          <span class="world-home-door" data-state="${escapeHTML(
            this.settings.publicDoor,
          )}" aria-hidden="true"></span>
          <div>
            <strong>${escapeHTML(publicIdentity(this.identity, this.settings).name)}’s space</strong>
            <span>${hidden ? "Availability hidden" : escapeHTML(availability)} · ${
              this.settings.publicDoor === "open"
                ? "public lobby open"
                : this.settings.publicDoor === "closed"
                  ? "door closed"
                  : "knock first"
            }</span>
          </div>
        </div>
        ${
          this.pendingKnocks.size
            ? `<h3>Knocks waiting for your consent</h3>
              <div class="world-neighbor-list">
                ${[...this.pendingKnocks.values()]
                  .slice(0, 8)
                  .map(
                    (visitor) => `<article>
                      <span aria-hidden="true">✦</span>
                      <div><strong>${escapeHTML(
                        visitor.name,
                      )}</strong><small>One-use visual visit request; no chat or private-data access.</small></div>
                      <button type="button" data-world-home-grant="${escapeHTML(
                        visitor.id,
                      )}">Open front yard</button>
                      <button type="button" data-world-home-decline="${escapeHTML(
                        visitor.id,
                      )}">Decline</button>
                    </article>`,
                  )
                  .join("")}
              </div>`
            : ""
        }
        <h3>Public neighborhood</h3>
        <div class="world-neighbor-list">
          ${
            publicNeighbors.length
              ? publicNeighbors
                  .slice(0, 16)
                  .map((player) => {
                    const door = ["closed", "knock", "open"].includes(
                      player.publicDoor,
                    )
                      ? player.publicDoor
                      : "closed";
                    return `
                      <article>
                        <span aria-hidden="true">⌂</span>
                        <div><strong>${escapeHTML(
                          player.name,
                        )}’s house</strong><small>${escapeHTML(
                          player.persistedInactive
                            ? `${player.lastActive || "Last active recently"} · seating status chosen by user`
                            : door === "open"
                            ? "Public lobby unlocked"
                            : door === "knock"
                              ? "Knock before entering"
                              : "Door closed",
                        )}</small></div>
                        ${
                          door === "knock"
                            ? `<button type="button" data-world-knock="${escapeHTML(
                                player.id,
                              )}">Knock</button>`
                            : door === "open"
                              ? `<button type="button" data-world-enter-home="${escapeHTML(
                                  player.id,
                                )}">Enter front yard</button>`
                              : `<button type="button" disabled>Private</button>`
                        }
                      </article>`;
                  })
                  .join("")
              : `<p class="world-empty-state">No other public homes are online. Empty houses reveal nothing about offline users.</p>`
          }
        </div>
        <p class="world-panel-footnote">The seating area indicates a chosen away state without penalties or rankings. Hiding inactivity produces no public inactivity record.</p>
      </section>`;
  }

  sendWorldInteraction(kind, target = "") {
    if (!["knock", "home-grant", "home-decline"].includes(kind) || !target) {
      return;
    }
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      this.toast("Realtime is offline; use the public lobby link instead.");
      return;
    }
    try {
      this.socket.send(
        JSON.stringify({
          type: "interaction",
          kind,
          target: String(target).slice(0, 32),
        }),
      );
    } catch (_) {}
  }

  enterNeighborhoodHome(ownerId, granted) {
    const owner = this.remotePlayers.get(String(ownerId || ""));
    if (!owner || (!granted && owner.publicDoor !== "open")) {
      this.toast("That front yard is no longer open.");
      return;
    }
    if (!this.world?.visitNeighborhoodHome?.(String(ownerId || ""))) {
      this.toast("That home is not currently present in the live neighborhood.");
      return;
    }
    this.closeLandmark();
    this.toast(
      granted
        ? `${owner.name || "The owner"} accepted your knock. You entered their visual front yard; normal collaboration permissions still apply.`
        : `Entered ${owner.name || "the owner"}’s public front yard. Normal collaboration permissions still apply.`,
    );
  }

  workshopPanelHTML() {
    const repos = this.repositories
      .filter((repo) => repo.liveHost || repo.isPrivate)
      .slice(0, 50);
    const runnable = repos.length > 0;
    return `
      <section class="world-feature-card" aria-label="Code workshop controls">
        <div class="world-workshop-form">
          <label><span>Authorized repository</span><select data-world-workshop-repo ${
            runnable ? "" : "disabled"
          }>
            ${
              runnable
                ? repos
                    .map(
                      (repo) =>
                        `<option value="${escapeHTML(`${repo.owner}/${repo.name}`)}">${escapeHTML(
                          `${repo.owner}/${repo.name}`,
                        )}</option>`,
                    )
                    .join("")
                : `<option value="">No live authorized repository available</option>`
            }
          </select></label>
          <label><span>Workshop</span><select data-world-workshop-type>
            ${WORKSHOP_TYPES.map(
              (type) => `<option value="${escapeHTML(type)}">${escapeHTML(type)}</option>`,
            ).join("")}
          </select></label>
          <button type="button" class="world-primary-action" data-world-run-workshop ${
            runnable ? "" : "disabled"
          }>Run local inspection</button>
          <button type="button" data-world-workshop-load ${
            readSession()?.sessionToken ? "" : "disabled"
          }>Load saved workshops</button>
        </div>
        <div data-world-workshop-results>
          <p class="world-empty-state">${
            runnable
              ? "Choose a repository and scope. The first pass uses one commit-pinned authorized snapshot. Sign in to save an encrypted report, invite explicit participants, and coordinate updates."
              : this.repositoryCatalogState === "unavailable"
                ? "The live repository catalog is unavailable. ForkMesh will not run a workshop against sample or guessed repository data."
                : "No live public or account-authorized repository is available for a workshop."
          }</p>
        </div>
      </section>`;
  }

  broadcastPanelHTML() {
    const sessionLabels = {
      "listening-room": "Shared listening room",
      "dj-session": "DJ session",
      "video-room": "Video-sharing room",
      "watch-party": "Watch party",
      "repository-launch": "Repository launch event",
      "organization-presentation": "Organization presentation",
    };
    const authenticated = Boolean(readSession()?.sessionToken);
    const hasRoom = Boolean(this.mediaRoom.id);
    const canModerate = this.mediaRoom.canModerate === true;
    const isOwner = this.mediaRoom.viewerRole === "owner";
    const playback = this.mediaRoom.playback || {
      state: "idle",
      itemId: "",
      positionMs: 0,
      revision: 0,
    };
    const playbackItem = this.mediaRoom.items.find(
      (item) => item.id === playback.itemId,
    );
    const playbackPosition = this.mediaPlaybackPosition();
    const localSchedule = this.mediaRoom.scheduledAt
      ? new Date(this.mediaRoom.scheduledAt)
      : null;
    const scheduleValue =
      localSchedule && !Number.isNaN(localSchedule.getTime())
        ? new Date(
            localSchedule.getTime() -
              localSchedule.getTimezoneOffset() * 60 * 1000,
          )
            .toISOString()
            .slice(0, 16)
        : "";
    return `
      <section class="world-feature-card" aria-label="Opt-in media controls">
        <div class="world-media-list">
          ${RADIO_STATIONS.map(
            (station) => `
              <article>
                <div><span>${escapeHTML(station.provider)}</span><strong>${escapeHTML(
                  station.name,
                )}</strong><p>${escapeHTML(station.description)}</p></div>
                <button type="button" data-world-radio="${escapeHTML(
                  station.id,
                )}">${
                  station.playMode === "external"
                    ? "Open official player"
                    : "Play with consent"
                }</button>
                <a href="${escapeHTML(station.homepageUrl)}" target="_blank" rel="noopener noreferrer">${
                  station.playMode === "external"
                    ? "Provider page"
                    : "License and method"
                }</a>
              </article>`,
          ).join("")}
        </div>
        <div class="world-media-now" data-world-media-now>
          <span>Nothing is playing. Audio never starts automatically.</span>
          <button type="button" data-world-radio-stop disabled>Mute / stop</button>
        </div>
        <section class="world-media-room" aria-label="Community media room">
          <h3>Server-authoritative rooms and playlists</h3>
          ${
            authenticated
              ? `<div class="world-workshop-form">
                  <label><span>Shared room</span><select data-world-media-space>
                    ${
                      this.mediaSpaces.length
                        ? this.mediaSpaces
                            .map(
                              (space) =>
                                `<option value="${escapeHTML(space.id)}" ${
                                  space.id === this.mediaRoom.id ? "selected" : ""
                                }>${escapeHTML(space.name)} · ${escapeHTML(
                                  space.viewerRole || "participant",
                                )}</option>`,
                            )
                            .join("")
                        : `<option value="">No active rooms yet</option>`
                    }
                  </select></label>
                  <label><span>Create a room</span><input type="text" maxlength="80" placeholder="Community listening room" data-world-media-space-name /></label>
                  <button type="button" data-world-media-create>Create room</button>
                </div>`
              : `<div class="world-notice"><strong>Account required</strong><span>Sign in to load shared rooms, playlists, schedules, and moderation roles. No room state is stored in this browser.</span></div>`
          }
          ${
            hasRoom
              ? `<p><strong>${escapeHTML(this.mediaRoom.name)}</strong> · owned by ${escapeHTML(
                  this.mediaRoom.owner || "a registered contributor",
                )} · your role: ${escapeHTML(
                  this.mediaRoom.viewerRole || "participant",
                )}</p>`
              : `<p class="world-empty-state">Create or select an authenticated shared room. ForkMesh stores coordination metadata, not media.</p>`
          }
          <div class="world-notice world-notice-safe" data-world-shared-playback>
            <strong>Shared clock · ${escapeHTML(playback.state)}</strong>
            <span data-world-shared-playback-copy>${
              playbackItem
                ? `${escapeHTML(playbackItem.title)} at ${escapeHTML(
                    formatMediaPosition(playbackPosition),
                  )}. Open the official provider yourself, then seek to this shared position.`
                : "No provider item is selected. The shared clock never starts media on a participant’s device."
            }</span>
            <span data-world-shared-provider-metadata>${escapeHTML(
              playbackItem
                ? providerMetadataCopy(playbackItem)
                : "Current provider track metadata unavailable: no provider item is selected.",
            )}</span>
            <span>Revision ${escapeHTML(playback.revision)} · coordination metadata only</span>
            <div class="world-detail-actions">
              <button type="button" data-world-media-playback-refresh ${
                hasRoom ? "" : "disabled"
              }>Refresh shared clock</button>
              <button type="button" data-world-media-pause ${
                canModerate && playback.state === "playing" ? "" : "disabled"
              }>Pause shared clock</button>
              <button type="button" data-world-media-stop ${
                canModerate && !["idle", "stopped"].includes(playback.state)
                  ? ""
                  : "disabled"
              }>Stop shared clock</button>
            </div>
          </div>
          <div class="world-workshop-form">
            <label><span>Session type</span><select data-world-media-session ${
              canModerate ? "" : "disabled"
            }>
              ${Object.entries(sessionLabels)
                .map(
                  ([value, label]) =>
                    `<option value="${escapeHTML(value)}" ${
                      this.mediaRoom.sessionType === value ? "selected" : ""
                    }>${escapeHTML(label)}</option>`,
                )
                .join("")}
            </select></label>
            <label><span>Schedule in your time zone</span><input type="datetime-local" value="${escapeHTML(
              scheduleValue,
            )}" data-world-media-schedule ${canModerate ? "" : "disabled"} /></label>
          </div>
          <div class="world-media-add">
            <input type="text" maxlength="80" placeholder="Playlist item title" data-world-media-title />
            <input type="url" maxlength="500" placeholder="Official HTTPS provider or media page" data-world-media-url />
            <button type="button" data-world-media-add ${
              canModerate ? "" : "disabled"
            }>Add provider link</button>
            <button type="button" data-world-media-stop ${
              canModerate ? "" : "disabled"
            }>Moderator stop</button>
          </div>
          <div class="world-media-queue">
            ${
              this.mediaRoom.items.length
                ? this.mediaRoom.items
                    .map(
                      (item) => `
                        <article>
                          <div><strong>${escapeHTML(item.title)}</strong><small>User playlist label · ${escapeHTML(
                            item.provider,
                          )} · external provider playback${
                            item.id === playback.itemId
                              ? ` · shared ${escapeHTML(playback.state)} at ${escapeHTML(
                                  formatMediaPosition(playbackPosition),
                                )}`
                              : ""
                          }</small><small>${escapeHTML(
                            providerMetadataCopy(item),
                          )}</small></div>
                          <a href="${escapeHTML(
                            item.url,
                          )}" target="_blank" rel="noopener noreferrer">Open official page</a>
                          ${
                            canModerate
                              ? `<button type="button" data-world-media-play="${escapeHTML(
                                  item.id,
                                )}">${
                                  item.id === playback.itemId &&
                                  playback.state === "paused"
                                    ? "Resume shared clock"
                                    : "Start shared clock"
                                }</button><button type="button" data-world-media-remove="${escapeHTML(
                                  item.id,
                                )}" aria-label="Moderator remove ${escapeHTML(
                                  item.title,
                                )}">Remove</button>`
                              : ""
                          }
                        </article>`,
                    )
                    .join("")
                : `<p class="world-empty-state">The user-created playlist is empty. Add official provider links; ForkMesh stores no copied media.</p>`
            }
          </div>
          <div class="world-media-queue" aria-label="Scheduled broadcasts">
            ${
              this.mediaRoom.schedules.length
                ? this.mediaRoom.schedules
                    .map(
                      (schedule) => `
                        <article>
                          <div><strong>${escapeHTML(
                            schedule.title,
                          )}</strong><small>${escapeHTML(
                            new Date(schedule.startsAt).toLocaleString(),
                          )} · ${escapeHTML(schedule.status)}</small></div>
                          ${
                            canModerate && schedule.status === "scheduled"
                              ? `<button type="button" data-world-media-schedule-cancel="${escapeHTML(
                                  schedule.id,
                                )}">Cancel schedule</button>`
                              : ""
                          }
                        </article>`,
                    )
                    .join("")
                : `<p class="world-empty-state">No shared broadcast is scheduled.</p>`
            }
          </div>
          ${
            isOwner
              ? `<div class="world-media-add" aria-label="Room moderators">
                  <input type="text" maxlength="40" placeholder="Registered username" data-world-media-moderator />
                  <button type="button" data-world-media-moderator-add>Add moderator</button>
                </div>
                <div class="world-media-queue">
                  ${
                    this.mediaRoom.roles.length
                      ? this.mediaRoom.roles
                          .map(
                            (role) => `<article><div><strong>${escapeHTML(
                              role.account,
                            )}</strong><small>Explicit room moderator</small></div><button type="button" data-world-media-moderator-remove="${escapeHTML(
                              role.id,
                            )}">Remove role</button></article>`,
                          )
                          .join("")
                      : `<p class="world-empty-state">Only the owner can moderate until an explicit moderator role is granted.</p>`
                  }
                </div>`
              : ""
          }
          <div class="world-detail-actions">
            <a class="world-primary-action" href="/dashboard/chat?space=${escapeHTML(
              this.mediaRoom.id || "broadcast",
            )}">Open moderated shared room</a>
          </div>
          <p class="world-panel-footnote">${
            this.mediaRoom.scheduledAt
              ? `Scheduled UTC: ${escapeHTML(
                  this.mediaRoom.scheduledAt,
                )} · shown locally above.`
              : "No broadcast is scheduled."
          } The server-authoritative UTC clock synchronizes selection, play,
          pause, stop, and position with optimistic revisions. Playback remains individual and opt-in; ForkMesh never sends an autoplay command or rebroadcasts media.</p>
        </section>
        <div class="world-notice world-notice-safe">
          <strong>Licensed-source boundary</strong>
          <span>Every add confirms provider terms and no rebroadcast. The API accepts supported HTTPS provider pages only, never raw stream/file URLs; playback remains opt-in and provider-controlled. Owners and explicitly granted moderators can add, remove, schedule, or stop items.</span>
        </div>
      </section>`;
  }

  supportPanelHTML() {
    const options = [
      {
        label: "Recurring project support",
        destination: "Patreon · published ForkMesh creator page",
        purpose:
          "Ongoing development, infrastructure, documentation, accessibility, and community operations.",
        href: "https://www.patreon.com/16434219/join",
        action: "Open Patreon",
      },
      {
        label: "Direct project sponsorship",
        destination: "ForkMesh founders · direct contact",
        purpose:
          "Discuss a disclosed one-time sponsorship purpose and destination before any transfer.",
        href: "mailto:founders@forkmesh.com?subject=Direct%20ForkMesh%20project%20support",
        action: "Contact founders",
      },
      {
        label: "Community membership",
        destination: "ForkMesh account and public community",
        purpose:
          "Join discussions, operate a mirror, improve code, report issues, or attend events without purchasing access.",
        href: "/signup",
        action: "Join the community",
      },
    ];
    return `
      <section class="world-feature-card" aria-label="Transparent ForkMesh project support">
        <div class="world-building-grid">
          ${options
            .map(
              (option) => `<article>
                <span>${escapeHTML(option.label)}</span>
                <strong>${escapeHTML(option.destination)}</strong>
                <p>${escapeHTML(option.purpose)}</p>
                <a href="${escapeHTML(option.href)}" ${
                  option.href.startsWith("https://")
                    ? 'target="_blank" rel="noopener noreferrer"'
                    : ""
                }>${escapeHTML(option.action)}</a>
              </article>`,
            )
            .join("")}
        </div>
        <div class="world-notice world-notice-warning">
          <strong>Voluntary project support · no financial return</strong>
          <span>ForkMesh does not custody a supporter’s wallet keys or user funds. Support is voluntary, is separate from the Global Reward Pool, does not guarantee a reward or financial return, and does not buy governance dominance, node selection, security approval, or investment rights.</span>
        </div>
      </section>`;
  }

  mediaMutationError(error, fallback) {
    const detail = String(error?.message || "")
      .replace(/_/g, " ")
      .slice(0, 120);
    this.toast(detail ? `${fallback}: ${detail}.` : fallback);
  }

  syncMediaWorld(render = true) {
    this.world?.updateMediaSpaces?.(this.mediaSpaces, this.mediaRoom);
    if (render) this.openLandmark("broadcast");
  }

  async refreshMediaSpaces(preferredId = "", render = true) {
    if (!readSession()?.sessionToken) {
      this.mediaSpaces = [];
      this.mediaRoom = normalizeMediaRoom(null);
      this.syncMediaWorld(render);
      return;
    }
    const listing = await this.fetchJSON("/api/world/media/spaces", {
      timeout: 5000,
      cache: "no-store",
    });
    this.mediaSpaces = normalizeMediaSpaces(listing);
    const target =
      this.mediaSpaces.find((space) => space.id === preferredId) ||
      this.mediaSpaces.find((space) => space.id === this.mediaRoom.id) ||
      this.mediaSpaces[0];
    if (target?.id) {
      await this.loadMediaSpace(target.id, render);
    } else {
      this.mediaRoom = normalizeMediaRoom(null);
      this.syncMediaWorld(render);
    }
  }

  async loadMediaSpace(spaceId, render = true) {
    const id = sanitizePresenceText(spaceId, "", 40);
    if (!id) return;
    try {
      const detail = await this.fetchJSON(
        `/api/world/media/spaces/${encodeURIComponent(id)}`,
        { timeout: 5000, cache: "no-store" },
      );
      this.mediaRoom = normalizeMediaRoom(detail);
      this.syncMediaWorld(render);
    } catch (error) {
      this.mediaMutationError(error, "Could not load the shared media room");
    }
  }

  mediaPlaybackPosition() {
    const playback = this.mediaRoom.playback || {};
    const base = Math.max(0, Number(playback.positionMs) || 0);
    if (playback.state !== "playing") return base;
    return Math.min(
      7 * 24 * 60 * 60 * 1000,
      base + Math.max(0, Date.now() - (Number(playback.observedAt) || Date.now())),
    );
  }

  renderMediaPlaybackStatus() {
    const container = this.$("[data-world-shared-playback]");
    const copy = this.$("[data-world-shared-playback-copy]");
    if (!container || !copy) return;
    const playback = this.mediaRoom.playback || {};
    const item = this.mediaRoom.items.find(
      (candidate) => candidate.id === playback.itemId,
    );
    const heading = container.querySelector("strong");
    const metadata = this.$("[data-world-shared-provider-metadata]");
    if (heading) heading.textContent = `Shared clock · ${playback.state || "idle"}`;
    copy.textContent = item
      ? `${item.title} at ${formatMediaPosition(
          this.mediaPlaybackPosition(),
        )}. Open the official provider yourself, then seek to this shared position.`
      : "No provider item is selected. The shared clock never starts media on a participant’s device.";
    if (metadata) {
      metadata.textContent = item
        ? providerMetadataCopy(item)
        : "Current provider track metadata unavailable: no provider item is selected.";
    }
  }

  async refreshMediaPlayback(render = false) {
    const id = sanitizePresenceText(this.mediaRoom.id, "", 40);
    if (!id || !readSession()?.sessionToken) return;
    try {
      const result = await this.fetchJSON(
        `/api/world/media/spaces/${encodeURIComponent(id)}/playback`,
        { timeout: 5000, cache: "no-store" },
      );
      const normalized = normalizeMediaRoom({
        ...this.mediaRoom,
        playback: result?.playback,
        items: this.mediaRoom.items,
        schedules: this.mediaRoom.schedules,
        roles: this.mediaRoom.roles,
      });
      this.mediaRoom = normalized;
      this.world?.updateMediaSpaces?.(this.mediaSpaces, this.mediaRoom);
      if (render) {
        this.openLandmark("broadcast");
      } else {
        this.renderMediaPlaybackStatus();
      }
    } catch (error) {
      if (render) {
        this.mediaMutationError(error, "Could not refresh the shared clock");
      }
    }
  }

  startMediaPlaybackPolling() {
    window.clearInterval(this.mediaTimer);
    this.mediaTimer = window.setInterval(() => {
      if (!document.hidden && this.mediaRoom.id) {
        this.refreshMediaPlayback(false);
      }
    }, 5000);
  }

  async createMediaSpace() {
    if (!readSession()?.sessionToken) {
      this.toast("Sign in before creating a shared media room.");
      return;
    }
    const name = sanitizePresenceText(
      this.$("[data-world-media-space-name]")?.value,
      "",
      80,
    );
    if (!name) {
      this.toast("Give the shared room a name.");
      return;
    }
    try {
      const created = await this.postJSON("/api/world/media/spaces", {
        name,
        description:
          "Community-coordinated external provider playback. No media is stored or rebroadcast.",
        sessionType: "listening-room",
      });
      const id = sanitizePresenceText(created?.space?.id, "", 40);
      await this.refreshMediaSpaces(id, true);
      this.toast("Shared room created. You are its owner.");
    } catch (error) {
      this.mediaMutationError(error, "Could not create the room");
    }
  }

  async updateMediaSession(sessionType) {
    if (!this.mediaRoom.id || !this.mediaRoom.canModerate) return;
    try {
      const detail = await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(this.mediaRoom.id)}`,
        { sessionType },
        { method: "PATCH" },
      );
      this.mediaRoom = normalizeMediaRoom(detail);
      this.syncMediaWorld(true);
      this.toast("Shared room type updated.");
    } catch (error) {
      this.mediaMutationError(error, "Could not update the room");
    }
  }

  async addMediaItem() {
    const title = sanitizePresenceText(
      this.$("[data-world-media-title]")?.value,
      "Untitled media link",
      80,
    );
    const url = safeHTTPURL(this.$("[data-world-media-url]")?.value);
    if (!url || !url.startsWith("https://")) {
      this.toast("Add an HTTPS page from an authorized or official media provider.");
      return;
    }
    if (!this.mediaRoom.id || !this.mediaRoom.canModerate) {
      this.toast("Only the room owner or an explicit moderator can add links.");
      return;
    }
    const provider = mediaProviderForURL(url);
    if (!provider) {
      this.toast(
        "Use a supported SomaFM, YouTube, Vimeo, SoundCloud, Twitch, Internet Archive, or PeerTube provider page.",
      );
      return;
    }
    try {
      await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(
          this.mediaRoom.id,
        )}/items`,
        {
          title,
          url,
          provider,
          termsConfirmed: true,
          noRebroadcast: true,
          autoplay: false,
        },
      );
      await this.loadMediaSpace(this.mediaRoom.id, true);
      this.toast(
        "Provider link added to the shared playlist. It will not autoplay or rebroadcast.",
      );
    } catch (error) {
      this.mediaMutationError(error, "Could not add the provider link");
    }
  }

  async removeMediaItem(id) {
    if (!this.mediaRoom.id || !this.mediaRoom.canModerate) return;
    try {
      await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(
          this.mediaRoom.id,
        )}/items/${encodeURIComponent(id)}`,
        {},
        { method: "DELETE" },
      );
      await this.loadMediaSpace(this.mediaRoom.id, true);
      this.toast("Playlist item removed by an authorized room moderator.");
    } catch (error) {
      this.mediaMutationError(error, "Could not remove the playlist item");
    }
  }

  async updateMediaPlayback(state, itemId = "") {
    if (!this.mediaRoom.id || !this.mediaRoom.canModerate) {
      this.toast("Only the room owner or an explicit moderator can change the shared clock.");
      return;
    }
    const current = this.mediaRoom.playback || {};
    const selectedId = sanitizePresenceText(itemId, "", 40);
    const sameItem = selectedId && selectedId === current.itemId;
    const positionMs =
      state === "stopped"
        ? 0
        : sameItem
          ? this.mediaPlaybackPosition()
          : 0;
    try {
      const result = await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(
          this.mediaRoom.id,
        )}/playback`,
        {
          state,
          itemId: state === "stopped" ? "" : selectedId,
          positionMs: Math.round(positionMs),
          expectedRevision: Math.max(0, Number(current.revision) || 0),
        },
        { method: "PATCH" },
      );
      this.mediaRoom = normalizeMediaRoom({
        ...this.mediaRoom,
        playback: result?.playback,
        items: this.mediaRoom.items,
        schedules: this.mediaRoom.schedules,
        roles: this.mediaRoom.roles,
      });
      this.syncMediaWorld(true);
      this.toast(
        `${state[0].toUpperCase()}${state.slice(
          1,
        )} shared clock saved. Each participant still controls local provider playback.`,
      );
    } catch (error) {
      await this.refreshMediaPlayback(true);
      this.mediaMutationError(
        error,
        "Could not update the shared clock; the latest revision was loaded",
      );
    }
  }

  async stopMediaRoom() {
    if (!this.mediaRoom.id || !this.mediaRoom.canModerate) return;
    this.stopRadio();
    await this.updateMediaPlayback("stopped");
  }

  async scheduleMediaRoom(localValue) {
    if (!this.mediaRoom.id || !this.mediaRoom.canModerate) return;
    const instant = new Date(localValue);
    if (!localValue || Number.isNaN(instant.getTime())) {
      this.toast("Choose a valid future date and time.");
      return;
    }
    try {
      await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(
          this.mediaRoom.id,
        )}/schedules`,
        {
          title: `${this.mediaRoom.name} scheduled session`,
          sessionType: this.mediaRoom.sessionType,
          startsAt: instant.getTime(),
          endsAt: instant.getTime() + 60 * 60 * 1000,
        },
      );
      await this.loadMediaSpace(this.mediaRoom.id, true);
      this.toast("Shared schedule stored in UTC and displayed in local time.");
    } catch (error) {
      this.mediaMutationError(error, "Could not schedule the shared room");
    }
  }

  async cancelMediaSchedule(id) {
    if (!this.mediaRoom.id || !this.mediaRoom.canModerate) return;
    try {
      await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(
          this.mediaRoom.id,
        )}/schedules/${encodeURIComponent(id)}`,
        {},
        { method: "DELETE" },
      );
      await this.loadMediaSpace(this.mediaRoom.id, true);
      this.toast("Shared schedule cancelled.");
    } catch (error) {
      this.mediaMutationError(error, "Could not cancel the schedule");
    }
  }

  async grantMediaModerator() {
    if (this.mediaRoom.viewerRole !== "owner") return;
    const account = sanitizePresenceText(
      this.$("[data-world-media-moderator]")?.value,
      "",
      40,
    ).toLowerCase();
    if (!account) {
      this.toast("Enter a registered username.");
      return;
    }
    try {
      await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(
          this.mediaRoom.id,
        )}/roles`,
        { account },
      );
      await this.loadMediaSpace(this.mediaRoom.id, true);
      this.toast("Explicit room moderator role granted.");
    } catch (error) {
      this.mediaMutationError(error, "Could not grant the moderator role");
    }
  }

  async removeMediaModerator(id) {
    if (this.mediaRoom.viewerRole !== "owner") return;
    try {
      await this.postJSON(
        `/api/world/media/spaces/${encodeURIComponent(
          this.mediaRoom.id,
        )}/roles/${encodeURIComponent(id)}`,
        {},
        { method: "DELETE" },
      );
      await this.loadMediaSpace(this.mediaRoom.id, true);
      this.toast("Room moderator role removed.");
    } catch (error) {
      this.mediaMutationError(error, "Could not remove the moderator role");
    }
  }

  async resolveRepositoryPullMetadataCommit(base) {
    const branches = await this.fetchJSON(`${base}/branches`, {
      timeout: REPOSITORY_METADATA_TIMEOUT_MS,
      cache: "no-store",
    });
    const branch = (Array.isArray(branches?.branches) ? branches.branches : [])
      .map((candidate) => ({
        name: String(candidate?.name || candidate?.ref || "")
          .replace(/^refs\/heads\//, "")
          .replace(/^refs\/remotes\/origin\//, ""),
        commit: immutableGitOid(
          candidate?.commit || candidate?.hash || candidate?.sha,
        ),
      }))
      .find(
        (candidate) =>
          candidate.name === "forkmesh/pulls" && candidate.commit,
      );
    if (!branch) throw new Error("pull metadata branch unavailable");
    return branch.commit;
  }

  async loadRepositoryPullRecords(base) {
    const pullMetadataCommit =
      await this.resolveRepositoryPullMetadataCommit(base);
    const tree = await this.fetchJSON(
      `${base}/tree?path=pulls&ref=${encodeURIComponent(
        pullMetadataCommit,
      )}`,
      { timeout: REPOSITORY_METADATA_TIMEOUT_MS, cache: "no-store" },
    );
    if (
      tree?.ok === false ||
      immutableGitOid(tree?.commit) !== pullMetadataCommit
    ) {
      throw new Error("pull metadata commit mismatch");
    }
    const numbered = (Array.isArray(tree?.entries) ? tree.entries : [])
      .filter(
        (entry) =>
          ["tree", "directory"].includes(String(entry?.type || "")) &&
          safePullNumber(entry?.name),
      )
      .map((entry) => safePullNumber(entry.name))
      .sort((left, right) => right - left);
    const selected = numbered.slice(0, 60);
    const paths = selected.map((number) => `pulls/${number}/pull.md`);
    let blobs = {};
    if (paths.length) {
      const query = new URLSearchParams();
      paths.forEach((path) => query.append("path", path));
      query.set("ref", pullMetadataCommit);
      try {
        const result = await this.fetchJSON(`${base}/blobs?${query}`, {
          timeout: REPOSITORY_METADATA_TIMEOUT_MS,
          cache: "no-store",
        });
        if (immutableGitOid(result?.commit) !== pullMetadataCommit) {
          throw new Error("pull metadata batch commit mismatch");
        }
        blobs =
          result?.blobs && typeof result.blobs === "object"
            ? result.blobs
            : {};
      } catch (error) {
        throw new Error(
          error?.message || "pull metadata batch is unavailable",
        );
      }
    }
    let pullMetadataUnavailableCount = 0;
    const pulls = selected.map((number) => {
      const path = `pulls/${number}`;
      const metadataBlob = blobs[`${path}/pull.md`];
      const metadataText =
        metadataBlob && metadataBlob?.ok !== false
          ? repositoryBlobText(metadataBlob)
          : "";
      if (!metadataText.trim()) {
        pullMetadataUnavailableCount += 1;
        return {
          number,
          path,
          state: "unknown",
          title: "Metadata unavailable",
          author: "Unknown",
          base: "",
          head: "",
          createdAt: 0,
          body: "",
          creationBaseOid: "",
          creationHeadOid: "",
          metadataAvailable: false,
        };
      }
      const parsed = parsePullFrontMatter(metadataText, number);
      return {
        number,
        path,
        state: parsed.status,
        title: parsed.title,
        author: parsed.author,
        base: parsed.base,
        head: parsed.head,
        createdAt: parsed.createdAt,
        body: parsed.body,
        creationBaseOid: parsed.creationBaseOid,
        creationHeadOid: parsed.creationHeadOid,
        metadataAvailable: true,
      };
    });
    return {
      pullMetadataCommit,
      pullsAvailable: true,
      pullCount: numbered.length,
      pullCountExact: tree?.truncated !== true,
      pullTreeTruncated: tree?.truncated === true,
      pullMetadataUnavailableCount,
      pulls,
    };
  }

  async loadRepositoryEntityRecords(base, commit, options = {}) {
    const locations = [
      { path: ".forkmesh/issues", state: "" },
      { path: ".forkmesh/issues/open", state: "open" },
      { path: ".forkmesh/issues/closed", state: "closed" },
    ];
    // Pulls have their own immutable metadata commit and are required for a
    // truthful review surface. Resolve that short chain before lower-priority
    // issue-layout probes can occupy every connection on a small mirror.
    const pullResult =
      options.pullResult?.status === "fulfilled" ||
      options.pullResult?.status === "rejected"
        ? options.pullResult
        : options.privateRepository === true
          ? {
              status: "rejected",
              reason: new Error("private pull metadata is not publicly probed"),
            }
          : await this.loadRepositoryPullRecords(base).then(
              (value) => ({ status: "fulfilled", value }),
              (reason) => ({ status: "rejected", reason }),
            );
    const issueResults = [];
    const issueConcurrency = 2;
    for (let offset = 0; offset < locations.length; offset += issueConcurrency) {
      const batch = locations.slice(offset, offset + issueConcurrency);
      issueResults.push(
        ...(await Promise.allSettled(
          batch.map(({ path }) =>
            this.fetchJSON(
              `${base}/tree?path=${encodeURIComponent(
                path,
              )}&ref=${encodeURIComponent(commit)}`,
              {
                // Issue layout is optional context. Never let its three legacy
                // probes hold an otherwise complete PR review for the full
                // two-mirror failover envelope.
                timeout: 6000,
                cache: "no-store",
              },
            ),
          ),
        )),
      );
    }
    const issues = new Map();
    let issuesMatched = false;
    issueResults.forEach((result, index) => {
      if (result.status !== "fulfilled" || result.value?.ok === false) return;
      const payload = result.value || {};
      const payloadCommit = String(
        payload.commit || payload.analysis?.commit || "",
      ).toLowerCase();
      if (payloadCommit !== commit) return;
      issuesMatched = true;
      const location = locations[index];
      const entries = Array.isArray(payload.entries)
        ? payload.entries.slice(0, 80)
        : [];
      entries.forEach((entry) => {
        if (!["tree", "directory"].includes(entry?.type)) return;
        const number = Number(entry.name);
        if (!Number.isSafeInteger(number) || number < 1 || number > 10_000_000) {
          return;
        }
        const record = {
          number,
          path: `${location.path}/${number}`,
          state: location.state,
        };
        const previous = issues.get(number);
        if (!previous || location.state) issues.set(number, record);
      });
    });
    const pullRecords =
      pullResult.status === "fulfilled"
        ? pullResult.value
        : {
            pullMetadataCommit: "",
            pullsAvailable: false,
            pullCount: 0,
            pullCountExact: false,
            pullTreeTruncated: false,
            pullMetadataUnavailableCount: 0,
            pulls: [],
          };
    return {
      commit,
      repositoryCommit: commit,
      available: issuesMatched || pullRecords.pullsAvailable,
      issues: [...issues.values()]
        .sort((left, right) => right.number - left.number)
        .slice(0, 40),
      ...pullRecords,
    };
  }

  flagshipCatalogCommits() {
    const commits = new Set();
    this.repositories.forEach((record) => {
      const source = String(record?.source || "").toLowerCase();
      if (
        String(record?.owner || "").toLowerCase() ===
          FLAGSHIP_REPOSITORY.owner &&
        String(record?.name || "").toLowerCase() === FLAGSHIP_REPOSITORY.repo &&
        !record?.isPrivate &&
        !record?.archived &&
        ["local-node", "remote-clone", "organization-alias"].includes(source) &&
        /^[0-9a-f]{40,64}$/.test(String(record?.commit || "")) &&
        /^[0-9a-f]{64}$/.test(String(record?.stateHash || ""))
      ) {
        commits.add(String(record.commit).toLowerCase());
      }
    });
    return commits;
  }

  async autoLoadFlagshipRepositoryMap() {
    if (
      this.destroyed ||
      !this.world ||
      this.repositoryCatalogState !== "ready" ||
      this.repositoryManualSelection ||
      this.activeRepository
    ) {
      return false;
    }
    const catalogCommits = this.flagshipCatalogCommits();
    if (!catalogCommits.size) {
      this.repositoryMapState = "unavailable";
      this.repositoryMapTarget =
        `${FLAGSHIP_REPOSITORY.owner}/${FLAGSHIP_REPOSITORY.repo}`;
      this.world.updateRepositoryGraph?.([], []);
      this.renderRepositoryMapStatus();
      return false;
    }
    // Remove construction-only file shapes before the live request begins.
    // The scene is repopulated only after every required response is pinned to
    // one catalog-attested commit.
    this.world.updateRepositoryGraph?.([], []);
    return this.loadRepositoryMap(
      FLAGSHIP_REPOSITORY.owner,
      FLAGSHIP_REPOSITORY.repo,
      {
        automatic: true,
        expectedCommits: catalogCommits,
        requireComplete: true,
      },
    );
  }

  async fetchRepositoryMapSnapshot(owner, repo) {
    const safeOwner = sanitizePresenceText(owner, "", 40);
    const safeRepo = sanitizePresenceText(repo, "", 60);
    const base = `/api/repo/${encodeURIComponent(safeOwner)}/${encodeURIComponent(
      safeRepo,
    )}`;
    const tree = await this.fetchJSON(`${base}/tree?path=`, {
      timeout: REPOSITORY_METADATA_TIMEOUT_MS,
      cache: "no-store",
    });
    if (!tree || tree?.ok === false) {
      throw new Error("repository tree unavailable");
    }
    const commit = String(
      tree.commit ||
        tree.analysis?.commit ||
        tree.latestCommit?.commit ||
        tree.latestCommit?.hash ||
      "",
    ).toLowerCase();
    if (!/^[0-9a-f]{40,64}$/.test(commit)) {
      throw new Error("repository tree commit unavailable");
    }
    const ref = `?ref=${encodeURIComponent(commit)}`;
    const catalogRecord = this.repositories.find(
      (record) =>
        record.owner.toLowerCase() === safeOwner.toLowerCase() &&
        record.name.toLowerCase() === safeRepo.toLowerCase(),
    );
    // Resolve the short immutable PR chain before sizes/stats and issue scans
    // can contend for a one-vCPU mirror. This result is then injected into the
    // entity loader, so the browser never repeats branches/tree/blobs.
    const pullResult =
      catalogRecord?.isPrivate === true
        ? {
            status: "rejected",
            reason: new Error("private pull metadata is not publicly probed"),
          }
        : await this.loadRepositoryPullRecords(base).then(
            (value) => ({ status: "fulfilled", value }),
            (reason) => ({ status: "rejected", reason }),
          );
    const [sizeResult, statsResult, mirrorsResult, entityRecordsResult] =
      await Promise.allSettled([
        this.fetchJSON(`${base}/sizes${ref}`, {
          timeout: REPOSITORY_METADATA_TIMEOUT_MS,
          cache: "no-store",
        }),
        this.fetchJSON(`${base}/stats${ref}`, {
          timeout: REPOSITORY_METADATA_TIMEOUT_MS,
          cache: "no-store",
        }),
        this.fetchJSON(`${base}/mirrors`, { auth: false }),
        this.loadRepositoryEntityRecords(base, commit, {
          privateRepository: catalogRecord?.isPrivate === true,
          pullResult,
        }),
      ]);
    const sizes =
      sizeResult.status === "fulfilled" &&
      sizeResult.value?.ok !== false &&
      String(sizeResult.value?.commit || "").toLowerCase() === commit
        ? sizeResult.value
        : null;
    const stats =
      statsResult.status === "fulfilled" &&
      statsResult.value?.ok !== false &&
      String(statsResult.value?.commit || "").toLowerCase() === commit
        ? statsResult.value
        : null;
    const entityRecords =
      entityRecordsResult.status === "fulfilled" &&
      entityRecordsResult.value?.available === true &&
      String(entityRecordsResult.value?.commit || "").toLowerCase() === commit
        ? entityRecordsResult.value
        : null;
    const mirrorPayload =
      mirrorsResult.status === "fulfilled" ? mirrorsResult.value : {};
    const mirrorPullCounts = (
      Array.isArray(mirrorPayload?.mirrors) ? mirrorPayload.mirrors : []
    )
      .map((mirror) => Number(mirror?.pullCount))
      .filter(
        (value) =>
          Number.isSafeInteger(value) &&
          value >= 0 &&
          value <= 10_000_000,
      );
    const unanimousMirrorPullCount =
      mirrorPullCounts.length > 0 &&
      mirrorPullCounts.length ===
        (Array.isArray(mirrorPayload?.mirrors)
          ? mirrorPayload.mirrors.length
          : 0) &&
      new Set(mirrorPullCounts).size === 1
        ? mirrorPullCounts[0]
        : null;
    const catalogPullCount = this.repositories.find(
      (record) =>
        record.owner.toLowerCase() === safeOwner.toLowerCase() &&
        record.name.toLowerCase() === safeRepo.toLowerCase(),
    )?.pullCount;
    const pullCount =
      entityRecords?.pullCountExact === true
        ? entityRecords.pullCount
        : unanimousMirrorPullCount ?? catalogPullCount ?? null;
    const pullCountSource =
      entityRecords?.pullCountExact === true
        ? "metadata-tree"
        : pullCount !== null
          ? "signed-mirror-report"
          : "unavailable";
    const snapshot = {
      owner: safeOwner,
      repo: safeRepo,
      path: "",
      commit,
      analysis: tree.analysis || {},
      entries: normalizeTreeEntries(tree),
      counts: {
        ...(tree?.counts || {}),
        pulls: pullCount,
      },
      pullCount,
      pullCountSource,
      isPrivate: catalogRecord?.isPrivate === true,
      sizes: sizes || {},
      stats: stats || {},
      mirrors: mirrorPayload,
      entityRecords: entityRecords || {
        commit,
        repositoryCommit: commit,
        available: false,
        pullMetadataCommit: "",
        pullsAvailable: false,
        pullCount: 0,
        pullCountExact: false,
        pullTreeTruncated: false,
        pullMetadataUnavailableCount: 0,
        issues: [],
        pulls: [],
      },
    };
    return {
      snapshot,
      complete:
        snapshot.entries.length > 0 &&
        Boolean(sizes) &&
        Boolean(stats) &&
        Boolean(entityRecords),
    };
  }

  async loadRepositoryMap(owner, repo, options = {}) {
    const safeOwner = sanitizePresenceText(owner, "", 40);
    const safeRepo = sanitizePresenceText(repo, "", 60);
    if (!safeOwner || !safeRepo) return false;
    const key = `${safeOwner.toLowerCase()}/${safeRepo.toLowerCase()}`;
    const automatic = options.automatic === true;
    if (automatic && (this.repositoryManualSelection || this.activeRepository)) {
      return false;
    }
    if (!automatic) this.repositoryManualSelection = key;
    if (
      this.repositoryMapState === "ready" &&
      this.activeRepository?.owner.toLowerCase() === safeOwner.toLowerCase() &&
      this.activeRepository?.repo.toLowerCase() === safeRepo.toLowerCase()
    ) {
      this.repositoryView = "map";
      this.pullReview = null;
      this.pullReviewSelection += 1;
      this.clearPullReviewScrollTracking();
      this.renderRepositoryMapStatus();
      return true;
    }

    this.repositoryView = "map";
    this.pullReview = null;
    this.pullReviewSelection += 1;
    this.clearPullReviewScrollTracking();
    const selection = ++this.repositoryMapSelection;
    this.repositoryMapState = "loading";
    this.repositoryMapTarget = `${safeOwner}/${safeRepo}`;
    this.renderRepositoryMapStatus();

    let request = this.repositoryMapLoads.get(key);
    if (!request) {
      request = this.fetchRepositoryMapSnapshot(safeOwner, safeRepo);
      this.repositoryMapLoads.set(key, request);
      request.then(
        () => {
          if (this.repositoryMapLoads.get(key) === request) {
            this.repositoryMapLoads.delete(key);
          }
        },
        () => {
          if (this.repositoryMapLoads.get(key) === request) {
            this.repositoryMapLoads.delete(key);
          }
        },
      );
    }

    let result;
    try {
      result = await request;
    } catch (_) {
      if (selection !== this.repositoryMapSelection || this.destroyed) {
        return false;
      }
      this.repositoryMapState = "unavailable";
      if (!this.activeRepository) this.world?.updateRepositoryGraph?.([], []);
      this.renderRepositoryMapStatus();
      return false;
    }
    if (selection !== this.repositoryMapSelection || this.destroyed) {
      return false;
    }

    const expectedCommits =
      options.expectedCommits instanceof Set
        ? options.expectedCommits
        : new Set();
    if (
      (options.requireComplete === true && !result.complete) ||
      (expectedCommits.size &&
        !expectedCommits.has(String(result.snapshot.commit).toLowerCase()))
    ) {
      this.repositoryMapState = "unavailable";
      if (!this.activeRepository) this.world?.updateRepositoryGraph?.([], []);
      this.renderRepositoryMapStatus();
      return false;
    }

    this.activeRepository = result.snapshot;
    this.repositoryMapState = "ready";
    this.renderRepositoryMapStatus();
    this.world?.updateRepositoryGraph?.(
      this.activeRepository.entries,
      buildRepositoryGraphEntities(this.activeRepository),
    );
    void this.loadRepositorySecurity(safeOwner, safeRepo, false);
    return true;
  }

  async loadRepositorySecurity(owner, repo, openPanel = true) {
    const safeOwner = sanitizePresenceText(owner, "", 40);
    const safeRepo = sanitizePresenceText(repo, "", 60);
    if (!safeOwner || !safeRepo) return;
    this.securityRepository = `${safeOwner}/${safeRepo}`;
    this.securityScan = {
      status: "Scan unavailable",
      commitHash: "Not reported",
      limitations: [
        "The latest redacted clipboard has not been returned.",
        "Missing and unauthorized private repositories use the same response.",
        "A clean automated scan would not guarantee security.",
      ],
    };
    this.securityScanRecord = null;
    this.securityTriage = null;
    this.securityHistory = [];
    if (openPanel) this.openLandmark("security");
    const base = `/api/repo/${encodeURIComponent(
      safeOwner,
    )}/${encodeURIComponent(safeRepo)}/security-scans`;
    let latestResult;
    try {
      latestResult = await this.fetchJSON(`${base}/latest?detail=rich`, {
        timeout: 7000,
        cache: "no-store",
      });
    } catch (_) {
      try {
        latestResult = await this.fetchJSON(`${base}/latest`, {
          timeout: 7000,
          cache: "no-store",
        });
      } catch (_) {
        latestResult = null;
      }
    }
    const [historyResult, triageResult] = await Promise.allSettled([
      this.fetchJSON(`${base}/history?limit=10`, {
        timeout: 7000,
        cache: "no-store",
      }),
      this.fetchJSON(`${base}/triage`, {
        timeout: 7000,
        cache: "no-store",
      }),
    ]);
    if (latestResult) {
      this.securityScan =
        latestResult?.latest?.clipboard || this.securityScan;
      this.securityScanRecord = latestResult?.latest || null;
    }
    if (
      historyResult.status === "fulfilled" &&
      Array.isArray(historyResult.value?.history)
    ) {
      this.securityHistory = historyResult.value.history.slice(0, 10);
    }
    if (triageResult.status === "fulfilled") {
      this.securityTriage = triageResult.value || null;
    }
    if (
      this.activeRepository?.owner === safeOwner &&
      this.activeRepository?.repo === safeRepo
    ) {
      this.activeRepository.entries = mergeCommitMatchedSecurity(
        this.activeRepository.entries,
        this.securityScanRecord,
        this.activeRepository.commit,
        this.securityTriage,
      );
      this.renderRepositoryExplorer();
      this.world?.updateRepositoryGraph?.(
        this.activeRepository.entries,
        buildRepositoryGraphEntities(this.activeRepository),
      );
    }
    if (openPanel) this.openLandmark("security");
  }

  async updateSecurityFindingStatus(findingId, status) {
    const [owner, repo] = String(this.securityRepository || "").split("/");
    const scanId = String(this.securityTriage?.scanId || "");
    if (
      !owner ||
      !repo ||
      !scanId ||
      !/^[0-9a-f]{16}$/.test(String(findingId || "")) ||
      !["unreviewed", "confirmed", "dismissed"].includes(status)
    ) {
      return;
    }
    try {
      await this.postJSON(
        `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(
          repo,
        )}/security-scans/triage`,
        { scanId, findingId, status },
        { method: "PATCH", timeout: 10000 },
      );
      await this.loadRepositorySecurity(owner, repo, true);
      this.toast(`Finding marked ${status}.`);
    } catch (error) {
      this.toast(`Finding review could not be updated: ${error.message}`);
    }
  }

  async loadRepositoryDirectory(owner, repo, path) {
    const explorer = this.$("[data-world-repo-explorer]");
    if (!explorer) return;
    const normalizedPath = String(path || "")
      .split("/")
      .filter(Boolean)
      .slice(0, 24)
      .map((part) => sanitizePresenceText(part, "", 100))
      .filter(Boolean)
      .join("/");
    explorer.innerHTML = `<p class="world-empty-state">Opening ${escapeHTML(
      normalizedPath || "repository root",
    )}…</p>`;
    try {
      const payload = await this.fetchJSON(
        `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(
          repo,
        )}/tree?path=${encodeURIComponent(normalizedPath)}&ref=${encodeURIComponent(
          this.activeRepository?.commit || "",
        )}`,
      );
      if (payload?.ok === false) throw new Error("tree unavailable");
      const payloadCommit = String(
        payload.commit || payload.analysis?.commit || "",
      ).toLowerCase();
      if (payloadCommit !== this.activeRepository?.commit) {
        throw new Error("commit mismatch");
      }
      this.activeRepository = {
        ...(this.activeRepository || {}),
        owner,
        repo,
        path: normalizedPath,
        entries: mergeCommitMatchedSecurity(
          normalizeTreeEntries(payload, normalizedPath),
          this.securityScanRecord,
          payloadCommit,
          this.securityTriage,
        ),
      };
      this.renderRepositoryExplorer();
      this.world?.updateRepositoryGraph?.(
        this.activeRepository.entries,
        buildRepositoryGraphEntities(this.activeRepository),
      );
    } catch (_) {
      explorer.innerHTML = `
        <div class="world-notice world-notice-warning">
          <strong>Directory unavailable</strong>
          <span>The node could not return this authorized tree. No fallback attempts reveal private repository existence.</span>
        </div>`;
    }
  }

  repositoryPullRecords(active = this.activeRepository) {
    const records =
      active?.entityRecords && typeof active.entityRecords === "object"
        ? active.entityRecords
        : {};
    const metadataCommit = immutableGitOid(records.pullMetadataCommit);
    if (
      active?.isPrivate === true ||
      records.pullsAvailable !== true ||
      !metadataCommit ||
      !Array.isArray(records.pulls)
    ) {
      return [];
    }
    return records.pulls
      .map((record) => {
        const number = safePullNumber(record?.number);
        if (!number) return null;
        const state = ["open", "closed", "merged", "unknown"].includes(
          String(record?.state || "").toLowerCase(),
        )
          ? String(record.state).toLowerCase()
          : "unknown";
        return {
          ...record,
          number,
          state,
          title: sanitizePresenceText(
            record?.title,
            `Pull request #${number}`,
            240,
          ),
          author: sanitizePresenceText(record?.author, "Unknown", 100),
          base: sanitizePresenceText(record?.base, "main", 160),
          head: sanitizePresenceText(record?.head, "", 160),
          metadataAvailable: record?.metadataAvailable !== false,
        };
      })
      .filter(Boolean)
      .sort((left, right) => right.number - left.number)
      .slice(0, 60);
  }

  repositoryPullCount(active = this.activeRepository) {
    const candidates = [
      active?.pullCount,
      active?.counts?.pulls,
      active?.entityRecords?.pullCountExact === true
        ? active.entityRecords.pullCount
        : null,
    ];
    for (const candidate of candidates) {
      const value = Number(candidate);
      if (
        Number.isSafeInteger(value) &&
        value >= 0 &&
        value <= 10_000_000
      ) {
        return value;
      }
    }
    return null;
  }

  repositoryPullCountLabel(active = this.activeRepository) {
    const count = this.repositoryPullCount(active);
    if (count === null) return "Pull-request count unavailable";
    const source =
      active?.pullCountSource === "metadata-tree"
        ? "exact metadata tree"
        : active?.pullCountSource === "signed-mirror-report"
          ? "matching signed mirror reports"
          : "authorized repository metadata";
    return `${compactNumber(count)} pull request${
      count === 1 ? "" : "s"
    } · ${source}`;
  }

  repositoryPullListHTML(active) {
    const records = this.repositoryPullRecords(active);
    const metadataCommit = immutableGitOid(
      active?.entityRecords?.pullMetadataCommit,
    );
    const available =
      active?.isPrivate !== true &&
      active?.entityRecords?.pullsAvailable === true &&
      Boolean(metadataCommit);
    const count = this.repositoryPullCount(active);
    return `
      <section class="world-pull-list-panel" aria-labelledby="world-pull-list-title">
        <header class="world-pull-panel-heading">
          <button type="button" data-world-pull-back="map">← Code map</button>
          <div>
            <span>IN-WORLD REVIEW</span>
            <h3 id="world-pull-list-title">${escapeHTML(
              active.owner,
            )}/${escapeHTML(active.repo)} pull requests</h3>
            <small>${escapeHTML(this.repositoryPullCountLabel(active))}</small>
          </div>
        </header>
        ${
          available
            ? `<p class="world-pull-pin">Record index and files are read from immutable <code>${escapeHTML(
                metadataCommit,
              )}</code>. ForkMesh never substitutes <code>main</code> or an unpinned ref.</p>`
            : ""
        }
        <div class="world-pull-records" data-world-pull-state="${
          available ? (records.length ? "ready" : "empty") : "unavailable"
        }">
          ${
            available && records.length
              ? records
                  .map((record) => {
                    const rawTime = Number(record.createdAt || 0);
                    const timestamp =
                      rawTime > 0 && rawTime < 1_000_000_000_000
                        ? rawTime * 1000
                        : rawTime;
                    const date = Number.isFinite(timestamp) && timestamp > 0
                      ? new Date(timestamp).toLocaleDateString()
                      : "";
                    const content = `
                      <span>
                        <strong>#${record.number} · ${escapeHTML(
                          record.title,
                        )}</strong>
                        <small>${escapeHTML(
                          [
                            record.author,
                            record.base && record.head
                              ? `${record.base} ← ${record.head}`
                              : "",
                            date,
                          ]
                            .filter(Boolean)
                            .join(" · "),
                        )}</small>
                      </span>
                      <em data-pull-state="${escapeHTML(
                        record.state,
                      )}">${escapeHTML(record.state)}</em>
                    `;
                    return record.metadataAvailable
                      ? `<button type="button" data-world-pull-open data-world-pull-number="${record.number}">${content}</button>`
                      : `<div class="world-pull-record-unavailable" aria-label="Pull request #${record.number} metadata unavailable">${content}</div>`;
                  })
                  .join("")
              : available
                ? `<p class="world-empty-state">The pinned metadata tree contains no pull-request records.</p>`
                : `<div class="world-notice world-notice-warning">
                    <strong>Pull-request records unavailable</strong>
                    <span>${
                      active?.isPrivate === true
                        ? "Private repositories are not probed through the public pull-metadata branch. Missing and unauthorized repositories remain indistinguishable."
                        : `The exact forkmesh/pulls commit could not be verified${
                            count === null
                              ? ""
                              : `, although ${compactNumber(
                                  count,
                                )} was reported`
                          }. No main-branch, guessed-ref, or repository-name probe was attempted.`
                    }</span>
                  </div>`
          }
        </div>
        ${
          active?.entityRecords?.pullTreeTruncated === true
            ? `<p class="world-panel-footnote">The mirror bounded this metadata listing. Only returned records are shown; ForkMesh does not invent the missing entries.</p>`
            : records.length >= 60
              ? `<p class="world-panel-footnote">Showing the newest 60 records from the pinned metadata tree.</p>`
              : Number(
                    active?.entityRecords?.pullMetadataUnavailableCount || 0,
                  ) > 0
                ? `<p class="world-panel-footnote">${compactNumber(
                    active.entityRecords.pullMetadataUnavailableCount,
                  )} pull-request record${
                    active.entityRecords.pullMetadataUnavailableCount === 1
                      ? " is"
                      : "s are"
                  } listed by the tree but ${
                    active.entityRecords.pullMetadataUnavailableCount === 1
                      ? "its metadata is"
                      : "their metadata are"
                  } unavailable. ${
                    active.entityRecords.pullMetadataUnavailableCount === 1
                      ? "It is"
                      : "They are"
                  } not labeled open.</p>`
              : ""
        }
      </section>`;
  }

  pullViewedSet(active, review) {
    const key = pullViewedStateKey(
      active?.owner,
      active?.repo,
      review?.metadataCommit,
      review?.number,
    );
    if (!key) return new Set();
    if (!this.pullViewedFiles.has(key)) {
      this.pullViewedFiles.set(key, new Set());
    }
    return this.pullViewedFiles.get(key);
  }

  pullFileTreeHTML(nodes, viewed, depth = 0) {
    if (!Array.isArray(nodes) || !nodes.length) return "";
    return `<ul>${nodes
      .map((node) => {
        if (node.kind === "directory") {
          return `<li class="world-pull-tree-directory">
            <span style="--pull-tree-depth:${Math.min(depth, 12)}">▾ ${escapeHTML(
              node.name,
            )}</span>
            ${this.pullFileTreeHTML(node.children, viewed, depth + 1)}
          </li>`;
        }
        const isViewed = viewed.has(node.path);
        return `<li>
          <button type="button"
            style="--pull-tree-depth:${Math.min(depth, 12)}"
            data-world-pull-file-path="${escapeHTML(node.path)}"
            data-viewed="${isViewed}"
            aria-label="${isViewed ? "Viewed" : "Review"} ${escapeHTML(
              node.path,
            )}">
            <span aria-hidden="true">${escapeHTML(
              node.status === "added"
                ? "+"
                : node.status === "deleted"
                  ? "−"
                  : node.status === "renamed"
                    ? "↪"
                    : "◇",
            )}</span>
            <strong>${escapeHTML(node.name)}</strong>
            <small>+${compactNumber(node.additions)} −${compactNumber(
              node.deletions,
            )}</small>
            <em data-world-pull-viewed aria-label="${
              isViewed ? "Viewed" : "Not viewed"
            }">${isViewed ? "✓" : "○"}</em>
          </button>
        </li>`;
      })
      .join("")}</ul>`;
  }

  pullDiffRowsHTML(file) {
    if (file.binary) {
      return `<p class="world-pull-binary">Binary change · content is not rendered in the World.</p>`;
    }
    if (!file.rows.length) {
      return `<p class="world-empty-state">No textual diff rows were returned for this file.</p>`;
    }
    return `<div class="world-pull-diff-table" role="table" aria-label="${escapeHTML(
      file.path,
    )} unified diff">${file.rows
      .map((row) => {
        if (row.type === "hunk") {
          return `<div class="world-pull-diff-row is-hunk" role="row">
            <span role="cell"></span><span role="cell"></span><code role="cell">${escapeHTML(
              row.text,
            )}</code>
          </div>`;
        }
        const marker =
          row.type === "add"
            ? "+"
            : row.type === "delete"
              ? "−"
              : row.type === "meta"
                ? "\\"
                : " ";
        return `<div class="world-pull-diff-row is-${escapeHTML(
          row.type,
        )}" role="row">
          <span role="cell">${row.oldLine ?? ""}</span>
          <span role="cell">${row.newLine ?? ""}</span>
          <code role="cell"><b aria-hidden="true">${marker}</b>${escapeHTML(
            row.text,
          )}</code>
        </div>`;
      })
      .join("")}</div>`;
  }

  repositoryPullMergeHTML(active, review) {
    const merge = review?.merge || { state: "idle" };
    const state = String(merge.state || "idle");
    const context = exactPullMergeContext(active, review);
    const session = validWorldSession();
    const terminalCopy = {
      merged: {
        title: "Merged and published",
        body: "The selected mirror confirmed both the merge and publication. The repository map is being refreshed from the published commit.",
      },
      conflict: {
        title: "Merge conflict",
        body: "The exact reviewed commits do not merge cleanly. No branch or pull-metadata ref was changed.",
      },
      stale: {
        title: "Review is stale",
        body: "The base, head, or pull-metadata commit changed. ForkMesh refused to substitute a newer ref or merge different code.",
      },
      forbidden: {
        title: "Merge not authorized",
        body: "This signed-in account is not authorized as the repository owner, node owner, or an organization writer.",
      },
      unauthenticated: {
        title: "Session no longer valid",
        body: "Sign in again before requesting a merge. No bearer token is placed in the URL or page content.",
      },
      pending: {
        title: "Merge still processing",
        body: "Bounded automatic polling ended. Checking again reuses the same idempotency request and cannot create a second merge job.",
      },
      failed: {
        title: "Merge status unavailable",
        body: "ForkMesh could not verify a terminal result. Checking again safely reuses the same idempotency request.",
      },
    };
    if (terminalCopy[state]) {
      const details = terminalCopy[state];
      const retry =
        session &&
        context &&
        ["pending", "failed"].includes(state) &&
        buildPullMergeRequest(context, merge.requestId);
      return `<section class="world-pull-merge-panel" data-world-pull-merge-state="${escapeHTML(
        state,
      )}" aria-live="${state === "merged" ? "polite" : "assertive"}" aria-atomic="true" role="${
        ["conflict", "stale", "forbidden", "unauthenticated", "failed"].includes(
          state,
        )
          ? "alert"
          : "status"
      }">
        <div>
          <span>PROTECTED MERGE</span>
          <strong>${escapeHTML(details.title)}</strong>
          <small id="world-pull-merge-status">${escapeHTML(details.body)}</small>
        </div>
        ${
          retry
            ? `<button type="button" data-world-pull-merge aria-describedby="world-pull-merge-status">Check merge status</button>`
            : ""
        }
      </section>`;
    }
    if (!session) {
      return `<p class="world-panel-footnote">Sign in to request a protected in-World merge. Review remains available without opening another tab.</p>`;
    }
    if (!context) {
      return `<div class="world-notice world-notice-warning" data-world-pull-merge-state="unavailable">
        <strong>Commit-checked merge unavailable</strong>
        <span>The open pull request must expose matching, immutable base, head, and pull-metadata object IDs before a merge control can appear.</span>
      </div>`;
    }
    const processing = state === "processing";
    return `<section class="world-pull-merge-panel" data-world-pull-merge-state="${processing ? "processing" : "ready"}" aria-live="polite" aria-atomic="true" aria-busy="${processing}" role="status">
      <div>
        <span>PROTECTED MERGE</span>
        <strong>${processing ? "Merge requested" : "Exact commits verified"}</strong>
        <small id="world-pull-merge-status">${
          processing
            ? "The selected mirror is checking and publishing this idempotent request."
            : "The request will be pinned to the reviewed base, head, and pull-metadata commits. Authorization is enforced by the protected endpoint."
        }</small>
      </div>
      <button type="button" data-world-pull-merge aria-describedby="world-pull-merge-status" ${
        processing ? "disabled" : ""
      }>${processing ? "Merging and publishing…" : `Merge pull request #${context.number}`}</button>
    </section>`;
  }

  repositoryPullReviewHTML(active) {
    const review = this.pullReview || { state: "unavailable" };
    const back = `
      <header class="world-pull-panel-heading">
        <button type="button" data-world-pull-back="list">← Pull requests</button>
        <div>
          <span>IN-WORLD REVIEW</span>
          <h3 id="world-pull-review-title">Pull request #${escapeHTML(
            safePullNumber(review.number) || "",
          )}</h3>
        </div>
      </header>`;
    if (review.state === "loading") {
      return `<section class="world-pull-review" aria-labelledby="world-pull-review-title">
        ${back}
        <p class="world-empty-state" role="status">Loading the metadata record and patch from exact commit ${escapeHTML(
          String(review.metadataCommit || "").slice(0, 12),
        )}…</p>
      </section>`;
    }
    if (review.state !== "ready") {
      return `<section class="world-pull-review" aria-labelledby="world-pull-review-title">
        ${back}
        <div class="world-notice world-notice-warning" data-world-pull-detail data-world-pull-state="unavailable">
          <strong>Pull-request review unavailable</strong>
          <span>${escapeHTML(
            review.message ||
              "The exact metadata record and patch could not be verified. No alternate branch or external page was opened.",
          )}</span>
        </div>
      </section>`;
    }
    const viewed = this.pullViewedSet(active, review);
    const files = Array.isArray(review.diff?.files)
      ? review.diff.files
      : [];
    const fileTree = buildPullFileTree(files);
    const metadata = review.metadata || {};
    return `
      <section class="world-pull-review" aria-labelledby="world-pull-review-title" data-world-pull-detail data-world-pull-review-key="${escapeHTML(
        pullViewedStateKey(
          active.owner,
          active.repo,
          review.metadataCommit,
          review.number,
        ),
      )}">
        ${back}
        <div class="world-pull-review-summary">
          <div>
            <strong>${escapeHTML(
              metadata.title || `Pull request #${review.number}`,
            )}</strong>
            <span>${escapeHTML(
              [
                metadata.status || "unknown",
                metadata.author,
                metadata.base && metadata.head
                  ? `${metadata.base} ← ${metadata.head}`
                  : "",
              ]
                .filter(Boolean)
                .join(" · "),
            )}</span>
          </div>
          <div>
            <span><strong>${compactNumber(files.length)}</strong> files</span>
            <span><strong>+${compactNumber(
              review.diff?.additions || 0,
            )}</strong> additions</span>
            <span><strong>−${compactNumber(
              review.diff?.deletions || 0,
            )}</strong> deletions</span>
            <span data-world-pull-viewed-summary>${compactNumber(
              viewed.size,
            )} of ${compactNumber(files.length)} files viewed</span>
          </div>
        </div>
        ${
          metadata.body
            ? `<p class="world-pull-description">${escapeHTML(
                metadata.body,
              )}</p>`
            : ""
        }
        <p class="world-pull-pin">Metadata and committed patch pinned to <code>${escapeHTML(
          review.metadataCommit,
        )}</code>${
          review.patchSource === "immutable-compare"
            ? "; the patch was reconstructed only from the two immutable creation OIDs"
            : ""
        }. Viewed checks live only in memory for this page.</p>
        ${
          review.diff?.truncated
            ? `<div class="world-notice world-notice-warning"><strong>Diff bounded for safe display</strong><span>The World rendered a compact subset. Review the full signed change with a trusted Git client before deciding.</span></div>`
            : ""
        }
        ${
          files.length
            ? `<div class="world-pull-review-layout">
                <nav class="world-pull-file-tree" aria-label="Changed files">
                  ${this.pullFileTreeHTML(fileTree, viewed)}
                </nav>
                <div class="world-pull-diff" data-world-pull-diff tabindex="0" aria-label="Unified code diff">
                  ${files
                    .map(
                      (file) => `
                        <article class="world-pull-diff-file"
                          id="${escapeHTML(file.id)}"
                          tabindex="-1"
                          data-world-pull-diff-file
                          data-world-pull-file="${escapeHTML(file.path)}"
                          data-viewed="${viewed.has(file.path)}">
                          <header>
                            <strong>${escapeHTML(file.path)}</strong>
                            <span>${escapeHTML(file.status)} · +${compactNumber(
                              file.additions,
                            )} −${compactNumber(file.deletions)}</span>
                          </header>
                          ${this.pullDiffRowsHTML(file)}
                          <span class="world-pull-file-end" data-world-pull-file-end="${escapeHTML(
                            file.path,
                          )}" aria-hidden="true"></span>
                        </article>`,
                    )
                    .join("")}
                </div>
              </div>`
            : `<div class="world-notice">
                <strong>No textual patch is committed for this pull request</strong>
                <span>The World will not invent a diff or silently read a moving branch. Use a trusted Git client to inspect branch-backed changes.</span>
              </div>`
        }
        ${this.repositoryPullMergeHTML(active, review)}
      </section>`;
  }

  updateRepositoryReviewMode() {
    const detail = this.$("[data-world-detail]");
    if (!detail) return;
    detail.dataset.repositoryReview = String(
      detail.dataset.openLandmark === "repositories" &&
        this.repositoryView === "review",
    );
  }

  clearPullReviewScrollTracking() {
    if (typeof this.pullReviewScrollCleanup === "function") {
      this.pullReviewScrollCleanup();
    }
    this.pullReviewScrollCleanup = null;
  }

  setupPullReviewScrollTracking() {
    this.clearPullReviewScrollTracking();
    const scroller = this.$("[data-world-pull-diff]");
    if (!scroller || this.pullReview?.state !== "ready") return;
    const onScroll = () => this.markVisiblePullFilesViewed();
    scroller.addEventListener("scroll", onScroll, { passive: true });
    this.pullReviewScrollCleanup = () =>
      scroller.removeEventListener("scroll", onScroll);
  }

  markVisiblePullFilesViewed() {
    const scroller = this.$("[data-world-pull-diff]");
    if (!scroller) return;
    const rootBounds = scroller.getBoundingClientRect();
    scroller.querySelectorAll("[data-world-pull-file-end]").forEach((marker) => {
      const bounds = marker.getBoundingClientRect();
      if (
        bounds.top >= rootBounds.top &&
        bounds.top <= rootBounds.bottom
      ) {
        this.markPullFileViewed(marker.dataset.worldPullFileEnd);
      }
    });
  }

  markPullFileViewed(path) {
    const safePath = safeDiffPath(path);
    if (!safePath || this.pullReview?.state !== "ready") return;
    const viewed = this.pullViewedSet(this.activeRepository, this.pullReview);
    if (viewed.has(safePath)) return;
    viewed.add(safePath);
    this.$$("[data-world-pull-file-path]").forEach((button) => {
      if (button.dataset.worldPullFilePath !== safePath) return;
      button.dataset.viewed = "true";
      button.setAttribute("aria-label", `Viewed ${safePath}`);
      const icon = button.querySelector("[data-world-pull-viewed]");
      if (icon) {
        icon.textContent = "✓";
        icon.setAttribute("aria-label", "Viewed");
      }
    });
    this.$$("[data-world-pull-diff-file]").forEach((section) => {
      if (section.dataset.worldPullFile === safePath) {
        section.dataset.viewed = "true";
      }
    });
    const summary = this.$("[data-world-pull-viewed-summary]");
    const total = this.pullReview.diff?.files?.length || 0;
    if (summary) {
      summary.textContent = `${compactNumber(viewed.size)} of ${compactNumber(
        total,
      )} files viewed`;
    }
  }

  scrollToPullFile(path) {
    const safePath = safeDiffPath(path);
    if (!safePath) return;
    const section = this.$$("[data-world-pull-diff-file]").find(
      (candidate) => candidate.dataset.worldPullFile === safePath,
    );
    if (!section) return;
    section.scrollIntoView({ behavior: "smooth", block: "start" });
    section.focus({ preventScroll: true });
    this.markPullFileViewed(safePath);
  }

  async requestRepositoryPullMerge(path, body, sessionToken) {
    const controller = new AbortController();
    const timeout = window.setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch(path, {
        method: "POST",
        headers: {
          accept: "application/json",
          authorization: `Bearer ${sessionToken}`,
          "content-type": "application/json",
        },
        credentials: "same-origin",
        cache: "no-store",
        body: JSON.stringify(body),
        signal: controller.signal,
      });
      const announced = Number(response.headers.get("content-length") || 0);
      if (
        Number.isFinite(announced) &&
        announced > WORLD_PULL_MERGE_RESPONSE_MAX_BYTES
      ) {
        throw new Error("merge response too large");
      }
      const raw = await response.text();
      const size =
        typeof TextEncoder === "function"
          ? new TextEncoder().encode(raw).byteLength
          : raw.length;
      if (size > WORLD_PULL_MERGE_RESPONSE_MAX_BYTES) {
        throw new Error("merge response too large");
      }
      let payload = {};
      try {
        payload = JSON.parse(raw);
      } catch (_) {}
      return {
        httpStatus: response.status,
        payload:
          payload && typeof payload === "object" && !Array.isArray(payload)
            ? payload
            : {},
      };
    } finally {
      window.clearTimeout(timeout);
    }
  }

  classifyRepositoryPullMergeResponse(response, request) {
    const httpStatus = Number(response?.httpStatus) || 0;
    const payload =
      response?.payload && typeof response.payload === "object"
        ? response.payload
        : {};
    const error = String(payload.error || "");
    if (httpStatus === 401 || error === "invalid_session") {
      return { state: "unauthenticated" };
    }
    if (httpStatus === 403 || error === "forbidden") {
      return { state: "forbidden" };
    }
    if (error === "merge_conflict") return { state: "conflict" };
    if (
      [
        "stale_base",
        "stale_head",
        "stale_pull_metadata",
        "pull_not_found",
        "pull_not_open",
        "unsupported_pull",
      ].includes(error)
    ) {
      return { state: "stale" };
    }
    if (
      httpStatus === 202 &&
      payload.ok === true &&
      payload.status === "processing" &&
      payload.requestId === request.requestId
    ) {
      return { state: "processing" };
    }
    const baseAfter = immutableGitOid(payload.baseAfter);
    const pullsAfter = immutableGitOid(payload.pullsAfter);
    if (
      httpStatus === 200 &&
      payload.ok === true &&
      payload.status === "merged" &&
      payload.published === true &&
      payload.requestId === request.requestId &&
      immutableGitOid(payload.baseBefore) === request.expectedBaseOid &&
      immutableGitOid(payload.head) === request.expectedHeadOid &&
      immutableGitOid(payload.pullsBefore) === request.expectedPullsOid &&
      baseAfter &&
      pullsAfter &&
      baseAfter.length === request.expectedBaseOid.length &&
      pullsAfter.length === request.expectedPullsOid.length
    ) {
      return {
        state: "merged",
        published: true,
        baseAfter,
        pullsAfter,
      };
    }
    return { state: "failed" };
  }

  async reloadRepositoryAfterPublishedPullMerge(active, review, mergeResult) {
    if (
      mergeResult?.state !== "merged" ||
      mergeResult?.published !== true ||
      !immutableGitOid(mergeResult.baseAfter)
    ) {
      return false;
    }
    let result;
    try {
      result = await this.fetchRepositoryMapSnapshot(active.owner, active.repo);
    } catch (_) {
      return false;
    }
    if (
      this.destroyed ||
      this.pullReview !== review ||
      this.activeRepository?.owner !== active.owner ||
      this.activeRepository?.repo !== active.repo ||
      immutableGitOid(result?.snapshot?.commit) !== mergeResult.baseAfter
    ) {
      return false;
    }
    this.activeRepository = result.snapshot;
    this.repositoryMapState = "ready";
    this.renderRepositoryMapStatus();
    this.world?.updateRepositoryGraph?.(
      this.activeRepository.entries,
      buildRepositoryGraphEntities(this.activeRepository),
    );
    void this.loadRepositorySecurity(active.owner, active.repo, false);
    return true;
  }

  async mergeRepositoryPull() {
    const active = this.activeRepository;
    const review = this.pullReview;
    const context = exactPullMergeContext(active, review);
    const session = validWorldSession();
    if (!active || !review || !context || !session) return;
    if (
      ["processing", "merged", "conflict", "stale", "forbidden", "unauthenticated"].includes(
        String(review.merge?.state || ""),
      )
    ) {
      return;
    }
    const existingRequest = buildPullMergeRequest(
      context,
      review.merge?.requestId,
    );
    const requestId =
      existingRequest?.requestId || createPullMergeRequestId();
    const request = buildPullMergeRequest(context, requestId);
    if (!request) {
      review.merge = { state: "failed", requestId: "" };
      this.renderRepositoryExplorer();
      return;
    }
    const endpoint = `/api/repo/${encodeURIComponent(
      active.owner,
    )}/${encodeURIComponent(active.repo)}/pulls/${context.number}/merge`;
    const selection = this.pullReviewSelection;
    const stillCurrent = () =>
      !this.destroyed &&
      selection === this.pullReviewSelection &&
      this.pullReview === review;
    review.merge = { state: "processing", requestId };
    this.renderRepositoryExplorer();

    for (let attempt = 0; attempt < WORLD_PULL_MERGE_MAX_REQUESTS; attempt += 1) {
      if (!stillCurrent()) return;
      if (validWorldSession()?.sessionToken !== session.sessionToken) {
        review.merge = { state: "unauthenticated", requestId };
        this.renderRepositoryExplorer();
        return;
      }
      let response;
      try {
        response = await this.requestRepositoryPullMerge(
          endpoint,
          request,
          session.sessionToken,
        );
      } catch (_) {
        if (!stillCurrent()) return;
        review.merge = { state: "failed", requestId };
        this.renderRepositoryExplorer();
        return;
      }
      if (!stillCurrent()) return;
      const result = this.classifyRepositoryPullMergeResponse(response, request);
      if (result.state === "processing") {
        if (attempt + 1 >= WORLD_PULL_MERGE_MAX_REQUESTS) {
          review.merge = { state: "pending", requestId };
          this.renderRepositoryExplorer();
          return;
        }
        await new Promise((resolve) =>
          window.setTimeout(resolve, WORLD_PULL_MERGE_POLL_MS),
        );
        continue;
      }
      review.merge = { ...result, requestId };
      this.renderRepositoryExplorer();
      if (result.state === "merged" && result.published === true) {
        this.toast("Pull request merged and published.");
        await this.reloadRepositoryAfterPublishedPullMerge(
          active,
          review,
          result,
        );
      }
      return;
    }
  }

  openRepositoryPullList() {
    if (!this.activeRepository) return;
    this.repositoryView = "list";
    this.clearPullReviewScrollTracking();
    if (
      this.$("[data-world-detail]")?.dataset.openLandmark !== "repositories"
    ) {
      this.openLandmark("repositories");
      return;
    }
    this.renderRepositoryExplorer();
  }

  async fetchRepositoryPullReview(active, record, metadataCommit) {
    if (active?.isPrivate === true) {
      throw new Error(
        "Private repositories are not probed through the public pull-metadata branch.",
      );
    }
    const number = safePullNumber(record?.number);
    if (!number || !immutableGitOid(metadataCommit)) {
      throw new Error("The exact pull metadata commit is unavailable.");
    }
    const base = `/api/repo/${encodeURIComponent(
      active.owner,
    )}/${encodeURIComponent(active.repo)}`;
    const metadataPath = `pulls/${number}/pull.md`;
    const patchPath = `pulls/${number}/changes.patch`;
    const query = new URLSearchParams();
    query.append("path", metadataPath);
    query.append("path", patchPath);
    query.set("ref", metadataCommit);
    const payload = await this.fetchJSON(`${base}/blobs?${query.toString()}`, {
      timeout: 12000,
      cache: "no-store",
    });
    const responseCommit = immutableGitOid(payload?.commit);
    if (responseCommit !== metadataCommit) {
      throw new Error("The mirror returned a different metadata commit.");
    }
    const blobs =
      payload?.blobs && typeof payload.blobs === "object"
        ? payload.blobs
        : {};
    const metadataBlob = blobs[metadataPath];
    if (!metadataBlob || metadataBlob?.ok === false) {
      throw new Error("The pinned pull-request record is unavailable.");
    }
    const metadataText = repositoryBlobText(metadataBlob);
    if (!metadataText.trim()) {
      throw new Error("The pinned pull-request metadata is empty.");
    }
    const metadata = parsePullFrontMatter(metadataText, number);
    if (metadata.number !== number) {
      throw new Error("The pinned pull-request record does not match its path.");
    }
    let patch = repositoryBlobText(blobs[patchPath]);
    let patchSource = "metadata-patch";
    if (
      !patch.trim() &&
      metadata.creationBaseOid &&
      metadata.creationHeadOid
    ) {
      try {
        const compare = await this.fetchJSON(
          `${base}/compare?base=${encodeURIComponent(
            metadata.creationBaseOid,
          )}&head=${encodeURIComponent(metadata.creationHeadOid)}`,
          { timeout: 12000, cache: "no-store" },
        );
        if (
          immutableGitOid(compare?.baseOid) === metadata.creationBaseOid &&
          immutableGitOid(compare?.headOid) === metadata.creationHeadOid
        ) {
          patch = String(compare?.patch || "");
          patchSource = "immutable-compare";
        }
      } catch (_) {}
    }
    return {
      state: "ready",
      number,
      metadataCommit,
      metadata,
      patchSource,
      diff: parseUnifiedDiff(patch, {
        maxCharacters: 1_200_000,
        maxFiles: 120,
        maxRows: 6_000,
      }),
    };
  }

  async loadRepositoryPullReview(value) {
    const active = this.activeRepository;
    const number = safePullNumber(value);
    const metadataCommit = immutableGitOid(
      active?.entityRecords?.pullMetadataCommit,
    );
    const record = this.repositoryPullRecords(active).find(
      (candidate) => candidate.number === number,
    );
    this.repositoryView = "review";
    this.clearPullReviewScrollTracking();
    const selection = ++this.pullReviewSelection;
    if (
      !active ||
      !number ||
      !metadataCommit ||
      !record ||
      record.metadataAvailable === false
    ) {
      this.pullReview = {
        state: "unavailable",
        number,
        metadataCommit,
        message:
          "That pull request is not present in the exact pinned metadata index. No alternate ref was queried.",
      };
      if (
        this.$("[data-world-detail]")?.dataset.openLandmark !== "repositories"
      ) {
        this.openLandmark("repositories");
      } else {
        this.renderRepositoryExplorer();
      }
      return;
    }
    this.pullReview = {
      state: "loading",
      number,
      metadataCommit,
    };
    if (
      this.$("[data-world-detail]")?.dataset.openLandmark !== "repositories"
    ) {
      this.openLandmark("repositories");
    } else {
      this.renderRepositoryExplorer();
    }
    try {
      const review = await this.fetchRepositoryPullReview(
        active,
        record,
        metadataCommit,
      );
      if (
        selection !== this.pullReviewSelection ||
        active !== this.activeRepository ||
        this.destroyed
      ) {
        return;
      }
      this.pullReview = review;
    } catch (error) {
      if (
        selection !== this.pullReviewSelection ||
        active !== this.activeRepository ||
        this.destroyed
      ) {
        return;
      }
      this.pullReview = {
        state: "unavailable",
        number,
        metadataCommit,
        message:
          error?.message ||
          "The exact metadata record and patch could not be verified.",
      };
    }
    this.renderRepositoryExplorer();
  }

  repositoryExplorerHTML(active) {
    if (this.repositoryView === "list") {
      return this.repositoryPullListHTML(active);
    }
    if (this.repositoryView === "review") {
      return this.repositoryPullReviewHTML(active);
    }
    const languages = [...new Set(active.entries.map((entry) => entry.language))]
      .filter(Boolean)
      .sort();
    const contributors = [
      ...new Set(active.entries.map((entry) => entry.contributor)),
    ]
      .filter((name) => name && name !== "Unknown")
      .sort();
    const maxSize = Math.max(1, ...active.entries.map((entry) => entry.size));
    const parent = active.path.split("/").slice(0, -1).join("/");
    const contributorCount = Number(
      active.stats?.contributorCount || active.stats?.contributors?.length || 0,
    );
    const fileCount = Number(
      active.stats?.fileCount || active.sizes?.fileCount || active.entries.length,
    );
    const totalSize = Number(active.sizes?.size || 0);
    const mirrorItems = Array.isArray(active.mirrors?.mirrors)
      ? active.mirrors.mirrors
      : [];
    const graphEntities = buildRepositoryGraphEntities(active);
    const entityNodes = [
      ...graphEntities.map((entity) => ({
        kind:
          entity.kind === "contributor"
            ? "Contributor"
            : entity.kind === "issue-collection"
              ? "Issues"
              : entity.kind === "issue"
                ? "Issue"
                : entity.kind === "pull-request-collection"
                  ? "Pull requests"
                  : "Pull request",
        label: entity.label,
        value: entity.detail,
        graphNode: entity,
      })),
      {
        kind: "Dependencies",
        label: "Manifest candidates",
        value: compactNumber(
          active.entries.filter((entry) => entry.role === "package").length,
        ),
      },
      {
        kind: "Database models",
        label: "Model candidates",
        value: compactNumber(
          active.entries.filter((entry) => entry.role === "database-model")
            .length,
        ),
      },
      {
        kind: "Services / APIs",
        label: "Boundary candidates",
        value: compactNumber(
          active.entries.filter((entry) =>
            ["service", "api"].includes(entry.role),
          ).length,
        ),
      },
    ];
    return `
      <section class="world-repo-explorer" aria-label="Three-dimensional repository file graph">
        <header>
          <div>
            <span>AUTHORIZED CODE MAP</span>
            <strong>${escapeHTML(active.owner)}/${escapeHTML(active.repo)}${
              active.path ? ` / ${escapeHTML(active.path)}` : ""
            }</strong>
            <small>commit ${escapeHTML(String(active.commit || "").slice(0, 12))}</small>
          </div>
          ${
            active.path
              ? `<button type="button" data-world-repo-directory="${escapeHTML(
                  parent,
                )}" data-world-repo-owner="${escapeHTML(
                  active.owner,
                )}" data-world-repo-name="${escapeHTML(active.repo)}">← ${
                  parent ? "Parent" : "Root"
                }</button>`
              : ""
          }
        </header>
        <div class="world-repo-summary">
          <span><strong>${compactNumber(fileCount)}</strong> tracked files</span>
          <span><strong>${formatBytes(totalSize)}</strong> mapped size</span>
          <span><strong>${compactNumber(contributorCount)}</strong> contributors</span>
          <span><strong>${compactNumber(mirrorItems.length)}</strong> reported mirrors</span>
          <button type="button" data-world-pull-list>
            <strong>${
              this.repositoryPullCount(active) === null
                ? "—"
                : compactNumber(this.repositoryPullCount(active))
            }</strong>
            pull requests
          </button>
        </div>
        <div class="world-repo-entities" aria-label="Repository entity layers">
          ${entityNodes
            .map(
              (entity) =>
                entity.graphNode
                  ? `
                <button type="button" data-entity-kind="${escapeHTML(
                  entity.kind,
                )}" data-world-graph-node="${escapeHTML(entity.graphNode.id)}">
                  <small>${escapeHTML(entity.kind)}</small>
                  <strong>${escapeHTML(entity.label)}</strong>
                  <em>${escapeHTML(entity.value)}</em>
                </button>`
                  : `
                <span data-entity-kind="${escapeHTML(entity.kind)}">
                  <small>${escapeHTML(entity.kind)}</small>
                  <strong>${escapeHTML(entity.label)}</strong>
                  <em>${escapeHTML(entity.value)}</em>
                </span>`,
            )
            .join("")}
        </div>
        <div class="world-empty-state" data-world-graph-selection>
          Select a file, contributor, issue, or pull-request node in the 3D scene for its commit-matched relationship details.
        </div>
        <details class="world-filter-panel">
          <summary>Graph filters</summary>
          <div class="world-filter-grid">
            <label><span>Language</span><select data-world-repo-filter="language"><option value="">All</option>${languages
              .map(
                (language) =>
                  `<option value="${escapeHTML(language)}">${escapeHTML(
                    language,
                  )}</option>`,
              )
              .join("")}</select></label>
            <label><span>File type</span><select data-world-repo-filter="type"><option value="">Files + directories</option><option value="file">Files</option><option value="directory">Directories</option></select></label>
            <label><span>Contributor</span><select data-world-repo-filter="contributor"><option value="">All public commit authors</option>${contributors
              .map(
                (name) =>
                  `<option value="${escapeHTML(name)}">${escapeHTML(name)}</option>`,
              )
              .join("")}</select></label>
            <label><span>Directory</span><input data-world-repo-filter="directory" type="search" placeholder="Path contains…" /></label>
            <label><span>Minimum size</span><select data-world-repo-filter="size"><option value="0">Any</option><option value="1024">1 KB</option><option value="10240">10 KB</option><option value="102400">100 KB</option></select></label>
            <label><span>Modification frequency</span><select data-world-repo-filter="frequency"><option value="">All frequencies</option><option value="high">High (10+ recent changes)</option><option value="medium">Medium (3–9)</option><option value="low">Low (1–2)</option><option value="unknown">Unknown</option></select></label>
            <label><span>Dependency depth</span><select data-world-repo-filter="dependency"><option value="">Any depth</option><option value="0">0 / not mapped</option><option value="1">1</option><option value="2">2</option><option value="3">3+</option></select></label>
            <label><span>Security findings</span><select data-world-repo-filter="security"><option value="">All commit-matched states</option><option value="unavailable">Unavailable / commit mismatch</option><option value="no-finding">No reported finding</option><option value="finding">Reported finding</option><option value="reviewed">Dismissed after review</option></select></label>
            <label><span>Test coverage</span><select data-world-repo-filter="coverage"><option value="">All coverage states</option><option value="covered">Covered (80%+)</option><option value="partial">Partial</option><option value="uncovered">Uncovered</option><option value="unknown">Unknown</option></select></label>
          </div>
        </details>
        <div class="world-code-map" data-world-code-map>
          ${active.entries
            .map((entry, index) => {
              const scale =
                entry.type === "directory"
                  ? 1.18
                  : 0.72 + Math.sqrt(entry.size / maxSize) * 0.72;
              const style = `--node-scale:${scale.toFixed(2)};--node-depth:${
                index % 5
              };--node-angle:${(index * 47) % 360}deg`;
              const filter = [
                `data-filter-language="${escapeHTML(entry.language)}"`,
                `data-filter-type="${escapeHTML(entry.type)}"`,
                `data-filter-contributor="${escapeHTML(entry.contributor)}"`,
                `data-filter-directory="${escapeHTML(entry.path)}"`,
                `data-filter-size="${escapeHTML(entry.size)}"`,
                `data-filter-security="${escapeHTML(entry.security)}"`,
                `data-filter-frequency="${escapeHTML(entry.frequency)}"`,
                `data-filter-dependency="${escapeHTML(entry.dependencyDepth)}"`,
                `data-filter-coverage="${
                  entry.coverage === null
                    ? "unknown"
                    : entry.coverage >= 80
                      ? "covered"
                      : entry.coverage > 0
                        ? "partial"
                        : "uncovered"
                }"`,
              ].join(" ");
              const classes = [
                "world-code-node",
                `world-code-node--${entry.role}`,
                entry.frequency === "high" ? "is-active" : "",
                entry.redundantCandidate ? "is-redundant" : "",
                entry.security === "finding" ? "is-sensitive" : "",
              ]
                .filter(Boolean)
                .join(" ");
              if (entry.type === "directory") {
                return `<button class="${classes}" type="button" style="${style}" ${filter} data-world-repo-directory="${escapeHTML(
                  entry.path,
                )}" data-world-repo-owner="${escapeHTML(
                  active.owner,
                )}" data-world-repo-name="${escapeHTML(active.repo)}">
                  <span aria-hidden="true">▰</span><strong>${escapeHTML(
                    entry.name,
                  )}</strong><small>Directory layer · ${escapeHTML(
                    entry.frequency,
                  )} activity</small>
                </button>`;
              }
              const href = `/${encodeURIComponent(active.owner)}/${encodeURIComponent(
                active.repo,
              )}/blob/${entry.path
                .split("/")
                .map(encodeURIComponent)
                .join("/")}`;
              return `<a class="${classes}" style="${style}" ${filter} href="${escapeHTML(
                href,
              )}">
                <span aria-hidden="true">◇</span><strong>${escapeHTML(
                  entry.name,
                )}</strong><small>${escapeHTML(formatBytes(entry.size))} · ${escapeHTML(
                  entry.language,
                )} · ${escapeHTML(entry.role)} · ${escapeHTML(
                  entry.frequency,
                )}</small>
              </a>`;
            })
            .join("")}
        </div>
        <p class="world-panel-footnote">Node scale reflects blob size. Dependency edges and depth come from bounded imports resolved against commit ${escapeHTML(
          String(active.commit || "").slice(0, 12),
        )}; coverage comes only from artifacts committed at that revision. File-level security state appears only when an owner-authorized scan names that exact commit. Public mirror health and signed state remain separate from trust.</p>
      </section>`;
  }

  renderRepositoryExplorer() {
    const explorer = this.$("[data-world-repo-explorer]");
    if (!explorer || !this.activeRepository) return;
    explorer.innerHTML = this.repositoryExplorerHTML(this.activeRepository);
    this.updateRepositoryReviewMode();
    if (this.repositoryView === "review" && this.pullReview?.state === "ready") {
      window.requestAnimationFrame(() =>
        this.setupPullReviewScrollTracking(),
      );
    }
  }

  selectRepositoryGraphNode(candidate) {
    const entity = buildRepositoryGraphEntities(
      this.activeRepository || {},
    ).find((item) => item.id === String(candidate?.id || ""));
    if (!entity) return;
    if (
      entity.kind === "pull-request-collection" ||
      entity.kind === "pull-request"
    ) {
      if (
        this.$("[data-world-detail]")?.dataset.openLandmark !== "repositories"
      ) {
        this.openLandmark("repositories");
      }
      if (entity.kind === "pull-request-collection") {
        this.openRepositoryPullList();
      } else {
        this.loadRepositoryPullReview(entity.number);
      }
      return;
    }
    const selection = this.$("[data-world-graph-selection]");
    if (selection) {
      selection.innerHTML = `
        <strong>${escapeHTML(entity.label)}</strong>
        <span>${escapeHTML(entity.detail)}</span>
        <small>${compactNumber(entity.targetPaths?.length || 0)} commit-matched relationship${
          entity.targetPaths?.length === 1 ? "" : "s"
        }</small>
        ${
          entity.href
            ? `<a href="${escapeHTML(entity.href)}">Open ${escapeHTML(
                entity.label,
              )}</a>`
            : ""
        }`;
    }
    this.$$("[data-world-graph-node]").forEach((button) => {
      button.setAttribute(
        "aria-selected",
        String(button.dataset.worldGraphNode === entity.id),
      );
    });
    this.toast(`${entity.label}: ${entity.detail}`);
  }

  applyRepositoryFilters() {
    const map = this.$("[data-world-code-map]");
    if (!map) return;
    const filters = {};
    this.$$("[data-world-repo-filter]").forEach((control) => {
      if (!control.disabled) filters[control.dataset.worldRepoFilter] = control.value;
    });
    map.querySelectorAll(".world-code-node").forEach((node) => {
      const language = node.dataset.filterLanguage || "";
      const type = node.dataset.filterType || "";
      const contributor = node.dataset.filterContributor || "";
      const directory = (node.dataset.filterDirectory || "").toLowerCase();
      const size = Number(node.dataset.filterSize || 0);
      const security = node.dataset.filterSecurity || "";
      const frequency = node.dataset.filterFrequency || "unknown";
      const dependency = Number(node.dataset.filterDependency || 0);
      const coverage = node.dataset.filterCoverage || "unknown";
      node.hidden = Boolean(
        (filters.language && filters.language !== language) ||
          (filters.type && filters.type !== type) ||
          (filters.contributor && filters.contributor !== contributor) ||
          (filters.directory &&
            !directory.includes(String(filters.directory).toLowerCase())) ||
          (filters.size && size < Number(filters.size)) ||
          (filters.security && filters.security !== security) ||
          (filters.frequency && filters.frequency !== frequency) ||
          (filters.dependency &&
            (filters.dependency === "3"
              ? dependency < 3
              : dependency !== Number(filters.dependency))) ||
          (filters.coverage && filters.coverage !== coverage),
      );
    });
  }

  async collectWorkshopSnapshot(owner, repo) {
    const base = `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(
      repo,
    )}`;
    const queue = [""];
    const visited = new Set();
    const entries = [];
    let commit = "";
    while (queue.length && visited.size < 28 && entries.length < 520) {
      const path = queue.shift();
      if (visited.has(path)) continue;
      visited.add(path);
      const query = new URLSearchParams({ path });
      if (commit) query.set("ref", commit);
      const payload = await this.fetchJSON(
        `${base}/tree?${query.toString()}`,
        { timeout: 12000 },
      );
      if (payload?.ok === false) throw new Error("tree unavailable");
      const resolved = String(
        payload?.commit ||
          payload?.analysis?.commit ||
          payload?.latestCommit?.commit ||
          payload?.latestCommit?.hash ||
          "",
      ).toLowerCase();
      if (!/^[0-9a-f]{40,64}$/.test(resolved)) {
        throw new Error("tree commit unavailable");
      }
      if (commit && resolved !== commit) throw new Error("tree commit changed");
      commit = resolved;
      const children = normalizeTreeEntries(payload, path);
      entries.push(...children);
      children
        .filter((entry) => entry.type === "directory")
        .slice(0, 24)
        .forEach((entry) => {
          if (!visited.has(entry.path)) queue.push(entry.path);
        });
    }
    const candidates = entries.filter(workshopTextCandidate).slice(0, 72);
    const files = [];
    for (let offset = 0; offset < candidates.length; offset += 16) {
      const batch = candidates.slice(offset, offset + 16);
      const query = new URLSearchParams();
      batch.forEach((entry) => query.append("path", entry.path));
      query.set("ref", commit);
      const payload = await this.fetchJSON(`${base}/blobs?${query.toString()}`, {
        timeout: 18000,
      });
      const blobs = payload?.blobs || {};
      batch.forEach((entry) => {
        const text = decodeWorkshopBlob(blobs[entry.path]);
        if (text) files.push({ path: entry.path, text });
      });
    }
    let stats = {};
    try {
      stats = await this.fetchJSON(
        `${base}/stats?ref=${encodeURIComponent(commit)}`,
        { timeout: 12000, cache: "no-store" },
      );
      if (String(stats?.commit || "").toLowerCase() !== commit) stats = {};
    } catch (_) {}
    return {
      commit,
      entries: entries.slice(0, 520),
      files,
      stats,
    };
  }

  workshopReportHTML(owner, repo, type, report, context = {}) {
    const session = context.session || null;
    const repositoryRecord = this.repositories.find(
      (candidate) =>
        candidate.owner.toLowerCase() === String(owner).toLowerCase() &&
        candidate.name.toLowerCase() === String(repo).toLowerCase(),
    );
    // Browser WebSocket handshakes cannot attach the dashboard's Bearer header.
    // Keep private or unresolved workshops on the persisted participant ACL
    // event stream until a header-safe private-room handshake is available.
    // Public workshops can use the repository-scoped room below.
    const liveWorkshopChatAvailable = Boolean(
      repositoryRecord && !repositoryRecord.isPrivate,
    );
    const commit = String(context.commit || session?.commit || "");
    const runId = String(context.runId || session?.runId || "");
    const resultId = String(
      context.resultId || session?.results?.[0]?.id || "",
    );
    const chatParams = new URLSearchParams({
      space: "workshop",
      repo: `${owner}/${repo}`,
      run: runId,
    });
    if (session?.id) chatParams.set("workshopSession", session.id);
    const agentParams = new URLSearchParams({
      workshopSession: String(session?.id || ""),
      workshopResult: resultId,
      run: runId,
      commit,
    });
    return `
      <section class="world-workshop-results">
        <span class="world-status-pill">Recommendation · ${escapeHTML(type)}</span>
        <h3>${escapeHTML(owner)}/${escapeHTML(repo)}</h3>
        <p class="world-panel-footnote">Commit <code>${escapeHTML(
          commit || "not attested",
        )}</code> · run <code>${escapeHTML(runId || "not saved")}</code></p>
        <div class="world-model-graph" aria-label="Candidate visual relationship graph">
          ${
            report.nodes.length
              ? report.nodes
                  .slice(0, 24)
                  .map(
                    (node, index) =>
                      `<span style="--graph-index:${index}" title="${escapeHTML(
                        node.detail || "",
                      )}"><strong>${escapeHTML(
                        node.label,
                      )}</strong><small>${escapeHTML(node.kind)}</small></span>`,
                  )
                  .join("")
              : `<span>No candidate nodes matched this bounded local pass.</span>`
          }
        </div>
        ${
          Array.isArray(report.modelUseSites) && report.modelUseSites.length
            ? `<details open data-world-model-use-sites>
                <summary>Model definitions and their own use sites (${report.modelUseSites.length})</summary>
                <div class="world-model-use-sites">${report.modelUseSites
                  .map(
                    (model) => `<article>
                      <strong>${escapeHTML(model.name)}</strong>
                      <span>Definition: <code>${escapeHTML(
                        model.definitionPath || "Unknown",
                      )}</code></span>
                      ${
                        model.useSites?.length
                          ? `<ul>${model.useSites
                              .map(
                                (use) =>
                                  `<li><code>${escapeHTML(
                                    use.path,
                                  )}</code> · ${escapeHTML(
                                    use.count,
                                  )} reference${Number(use.count) === 1 ? "" : "s"}</li>`,
                              )
                              .join("")}</ul>`
                          : "<small>No use beyond the definition occurrence was found in this bounded pass.</small>"
                      }
                    </article>`,
                  )
                  .join("")}</div>
              </details>`
            : ""
        }
        ${
          report.edges.length
            ? `<details open><summary>Relationship edges (${report.edges.length})</summary>
                <ul class="world-file-reference-list">${report.edges
                  .slice(0, 40)
                  .map(
                    (edge) =>
                      `<li>${escapeHTML(edge.from)} → ${escapeHTML(
                        edge.to,
                      )}</li>`,
                  )
                  .join("")}</ul></details>`
            : ""
        }
        <h3>Findings to verify</h3>
        <ul class="world-detail-list">${
          report.findings.length
            ? report.findings
                .map((finding) => `<li>${escapeHTML(finding)}</li>`)
                .join("")
            : "<li>No candidate finding matched the bounded analysis.</li>"
        }</ul>
        <h3>Recommendations</h3>
        <ul class="world-detail-list">${report.recommendations
          .map((item) => `<li>${escapeHTML(item)}</li>`)
          .join("")}</ul>
        <details>
          <summary>Supporting file references (${report.references.length})</summary>
          <ul class="world-file-reference-list">${
            report.references.length
              ? report.references
                  .map((path) => `<li><code>${escapeHTML(path)}</code></li>`)
                  .join("")
              : "<li>None in this bounded pass.</li>"
          }</ul>
        </details>
        <section class="world-workshop-session" data-world-workshop-session>
          <div class="world-detail-actions">
            <button type="button" data-world-workshop-save ${
              readSession()?.sessionToken && commit ? "" : "disabled"
            }>${session?.id ? "Save a new result version" : "Save encrypted workshop report"}</button>
            <button type="button" data-world-workshop-load ${
              readSession()?.sessionToken ? "" : "disabled"
            }>Load saved reports</button>
          </div>
          ${
            session?.id
              ? `<dl class="world-technical-list">
                  <div><dt>Session ID</dt><dd><code>${escapeHTML(
                    session.id,
                  )}</code></dd></div>
                  <div><dt>Result ID</dt><dd><code>${escapeHTML(
                    resultId || "No result",
                  )}</code></dd></div>
                  <div><dt>Participant link</dt><dd><a href="/world/?workshopSession=${encodeURIComponent(
                    session.id,
                  )}">Open this authorized session</a></dd></div>
                  <div><dt>Your role</dt><dd>${escapeHTML(
                    session.viewerRole || "participant",
                  )}</dd></div>
                  <div><dt>Participants</dt><dd>${escapeHTML(
                    (session.participants || [])
                      .map((participant) => `${participant.account} (${participant.role})`)
                      .join(", ") || "Owner only",
                  )}</dd></div>
                </dl>
                ${
                  session.viewerRole === "owner"
                    ? `<div class="world-workshop-form">
                        <label><span>Share with account</span><input data-world-workshop-share-account autocomplete="off" maxlength="63" placeholder="account-name" /></label>
                        <label><span>Role</span><select data-world-workshop-share-role><option value="viewer">Viewer</option><option value="editor">Editor</option></select></label>
                        <button type="button" data-world-workshop-share>Add participant</button>
                      </div>`
                    : ""
                }
                <div class="world-workshop-form">
                  <label><span>Participant update</span><input data-world-workshop-comment-input maxlength="2000" placeholder="Add a bounded collaboration note…" /></label>
                  <button type="button" data-world-workshop-comment>Add update</button>
                  <button type="button" data-world-workshop-events-refresh>Refresh updates</button>
                </div>
                <ol class="world-orientation-list" data-world-workshop-events>${
                  (session.events || []).length
                    ? session.events
                        .slice(-20)
                        .map(
                          (event) =>
                            `<li><strong>${escapeHTML(
                              event.actor || event.kind,
                            )}</strong><span>${escapeHTML(
                              event.data?.message ||
                                event.data?.status ||
                                event.data?.resultId ||
                                event.kind,
                            )}</span></li>`,
                        )
                        .join("")
                    : "<li><strong>No participant updates yet</strong><span>Result saves and explicit comments appear here.</span></li>"
                }</ol>`
              : `<p class="world-empty-state">This result exists only in this browser tab until an authenticated participant saves it. Saved rows encrypt repository identity, paths, report content, participant labels, and updates.</p>`
          }
        </section>
        <div class="world-detail-actions">
          ${
            liveWorkshopChatAvailable
              ? `<a class="world-primary-action" href="/dashboard/chat?${escapeHTML(
                  chatParams.toString(),
                )}">Open live encrypted collaboration for this run</a>`
              : `<span class="world-status-pill">Private/unresolved workshop · use the authorized participant updates above; live browser chat fails closed.</span>`
          }
          ${
            session?.id && resultId
              ? `<a class="world-secondary-action" href="/${encodeURIComponent(
                  owner,
                )}/${encodeURIComponent(repo)}/agents?${escapeHTML(
                  agentParams.toString(),
                )}">Continue with an owner-authorized agent for this result</a>`
              : ""
          }
        </div>
        <p class="world-panel-footnote">Analysis ran in this browser against same-origin, authorized repository data pinned to the displayed commit. It did not send private code to an external model. Findings are recommendations, not guaranteed facts. Collaboration events use incremental cursors. Public workshop live chat uses a repository-scoped encrypted dashboard socket; private and unresolved workshops stay on the authorized persisted event stream.</p>
      </section>`;
  }

  async saveWorkshopReport() {
    const pending = this.pendingWorkshop;
    const results = this.$("[data-world-workshop-results]");
    if (!pending || !results || !readSession()?.sessionToken) {
      this.toast("Sign in and run a commit-pinned workshop before saving.");
      return;
    }
    try {
      let response;
      if (this.activeWorkshop?.id === pending.sessionId) {
        response = await this.postJSON(
          `/api/world/workshops/${encodeURIComponent(
            this.activeWorkshop.id,
          )}/results`,
          { report: pending.report },
          { timeout: 15000 },
        );
        const refreshed = await this.fetchJSON(
          `/api/world/workshops/${encodeURIComponent(this.activeWorkshop.id)}`,
          { timeout: 10000, cache: "no-store" },
        );
        this.activeWorkshop = refreshed.session;
        pending.resultId = response.resultId;
      } else {
        response = await this.postJSON(
          "/api/world/workshops",
          {
            repository: `${pending.owner}/${pending.repo}`,
            commit: pending.commit,
            runId: pending.runId,
            workshopType: pending.type,
            report: pending.report,
          },
          { timeout: 15000 },
        );
        this.activeWorkshop = response.session;
        pending.sessionId = response.session?.id;
        pending.resultId = response.resultId;
      }
      this.workshopEventCursor = Number(
        this.activeWorkshop?.eventCursor || 0,
      );
      results.innerHTML = this.workshopReportHTML(
        pending.owner,
        pending.repo,
        pending.type,
        pending.report,
        {
          commit: pending.commit,
          runId: pending.runId,
          resultId: pending.resultId,
          session: this.activeWorkshop,
        },
      );
      this.toast(`Workshop result saved as ${pending.resultId}.`);
    } catch (error) {
      this.toast(`Workshop could not be saved: ${error.message}`);
    }
  }

  async loadWorkshopSessions() {
    const repoControl = this.$("[data-world-workshop-repo]");
    const results = this.$("[data-world-workshop-results]");
    const repository = String(repoControl?.value || "");
    if (!results || !repository || !readSession()?.sessionToken) {
      this.toast("Sign in to load workshops shared with your account.");
      return;
    }
    results.innerHTML = `<p class="world-empty-state">Loading your encrypted workshop sessions…</p>`;
    try {
      const query = new URLSearchParams({ repository });
      const payload = await this.fetchJSON(
        `/api/world/workshops?${query.toString()}`,
        { timeout: 10000, cache: "no-store" },
      );
      this.savedWorkshopSessions = Array.isArray(payload?.sessions)
        ? payload.sessions
        : [];
      results.innerHTML = this.savedWorkshopSessions.length
        ? `<section class="world-workshop-results"><h3>Saved workshops</h3><div class="world-event-list">${this.savedWorkshopSessions
            .map(
              (session) => `<article>
                <div><strong>${escapeHTML(
                  session.workshopType,
                )}</strong><p>commit ${escapeHTML(
                  String(session.commit || "").slice(0, 12),
                )} · run ${escapeHTML(session.runId)} · ${escapeHTML(
                  session.viewerRole,
                )}</p></div>
                <button type="button" data-world-workshop-open="${escapeHTML(
                  session.id,
                )}">Open saved result</button>
              </article>`,
            )
            .join("")}</div></section>`
        : `<p class="world-empty-state">No saved workshop for this repository is shared with your account.</p>`;
    } catch (error) {
      results.innerHTML = `<p class="world-empty-state">Saved workshops are unavailable: ${escapeHTML(
        error.message,
      )}</p>`;
    }
  }

  async loadWorkshopSession(sessionId) {
    if (!/^[a-f0-9]{32}$/.test(String(sessionId || ""))) return;
    const results = this.$("[data-world-workshop-results]");
    if (!results) return;
    results.innerHTML = `<p class="world-empty-state">Decrypting the authorized workshop report…</p>`;
    try {
      const payload = await this.fetchJSON(
        `/api/world/workshops/${encodeURIComponent(sessionId)}`,
        { timeout: 10000, cache: "no-store" },
      );
      const session = payload.session;
      const result = session?.results?.[0];
      if (!session || !result?.report) throw new Error("result unavailable");
      const [owner, repo] = String(session.repository || "").split("/");
      this.activeWorkshop = session;
      this.workshopEventCursor = Number(session.eventCursor || 0);
      this.pendingWorkshop = {
        owner,
        repo,
        type: session.workshopType,
        report: result.report,
        commit: session.commit,
        runId: session.runId,
        sessionId: session.id,
        resultId: result.id,
      };
      results.innerHTML = this.workshopReportHTML(
        owner,
        repo,
        session.workshopType,
        result.report,
        {
          commit: session.commit,
          runId: session.runId,
          resultId: result.id,
          session,
        },
      );
    } catch (error) {
      results.innerHTML = `<p class="world-empty-state">Workshop unavailable or not shared with this account: ${escapeHTML(
        error.message,
      )}</p>`;
    }
  }

  async shareWorkshopSession() {
    const sessionId = this.activeWorkshop?.id;
    const account = String(
      this.$("[data-world-workshop-share-account]")?.value || "",
    ).trim();
    const role =
      this.$("[data-world-workshop-share-role]")?.value || "viewer";
    if (!sessionId || !account) return;
    try {
      await this.postJSON(
        `/api/world/workshops/${encodeURIComponent(sessionId)}/participants`,
        { account, role },
      );
      await this.loadWorkshopSession(sessionId);
      this.toast(`Workshop shared with ${account}.`);
    } catch (error) {
      this.toast(`Workshop could not be shared: ${error.message}`);
    }
  }

  async addWorkshopComment() {
    const sessionId = this.activeWorkshop?.id;
    const input = this.$("[data-world-workshop-comment-input]");
    const message = String(input?.value || "").trim();
    if (!sessionId || !message) return;
    try {
      await this.postJSON(
        `/api/world/workshops/${encodeURIComponent(sessionId)}/events`,
        { kind: "comment", message },
      );
      if (input) input.value = "";
      await this.refreshWorkshopEvents();
    } catch (error) {
      this.toast(`Workshop update could not be added: ${error.message}`);
    }
  }

  async refreshWorkshopEvents() {
    const sessionId = this.activeWorkshop?.id;
    if (!sessionId) return;
    try {
      const payload = await this.fetchJSON(
        `/api/world/workshops/${encodeURIComponent(
          sessionId,
        )}/events?after=${encodeURIComponent(this.workshopEventCursor)}`,
        { timeout: 8000, cache: "no-store" },
      );
      this.workshopEventCursor = Number(
        payload.cursor || this.workshopEventCursor,
      );
      if (Array.isArray(payload.events) && payload.events.length) {
        this.activeWorkshop.events = [
          ...(this.activeWorkshop.events || []),
          ...payload.events,
        ].slice(-25);
      }
      const pending = this.pendingWorkshop;
      const results = this.$("[data-world-workshop-results]");
      if (pending && results) {
        results.innerHTML = this.workshopReportHTML(
          pending.owner,
          pending.repo,
          pending.type,
          pending.report,
          {
            commit: pending.commit,
            runId: pending.runId,
            resultId: pending.resultId,
            session: this.activeWorkshop,
          },
        );
      }
    } catch (error) {
      this.toast(`Workshop updates unavailable: ${error.message}`);
    }
  }

  async runWorkshop() {
    {
      const repoControl = this.$("[data-world-workshop-repo]");
      const typeControl = this.$("[data-world-workshop-type]");
      const results = this.$("[data-world-workshop-results]");
      const [owner, repo] = String(repoControl?.value || "").split("/");
      if (repoControl && typeControl && results && owner && repo) {
        results.innerHTML = `<p class="world-empty-state">Walking the authorized tree and reading a bounded set of same-origin source files locally…</p>`;
        try {
          const snapshot = await this.collectWorkshopSnapshot(owner, repo);
          const report = analyzeWorkshopSnapshot(
            typeControl.value,
            snapshot.entries,
            snapshot.files,
            snapshot.stats,
          );
          this.activeWorkshop = null;
          this.workshopEventCursor = 0;
          this.pendingWorkshop = {
            owner,
            repo,
            type: typeControl.value,
            report,
            commit: snapshot.commit,
            runId: workshopRunId(),
            sessionId: "",
            resultId: "",
          };
          results.innerHTML = this.workshopReportHTML(
            owner,
            repo,
            typeControl.value,
            report,
            {
              commit: snapshot.commit,
              runId: this.pendingWorkshop.runId,
            },
          );
          return;
        } catch (_) {
          results.innerHTML = `<p class="world-empty-state">Deep local analysis was unavailable; falling back to authorized root metadata.</p>`;
        }
      }
    }
    const repoControl = this.$("[data-world-workshop-repo]");
    const typeControl = this.$("[data-world-workshop-type]");
    const results = this.$("[data-world-workshop-results]");
    if (!repoControl || !typeControl || !results) return;
    const [owner, repo] = String(repoControl.value || "").split("/");
    if (!owner || !repo) {
      results.innerHTML = `<p class="world-empty-state">No authorized online repository is available.</p>`;
      return;
    }
    results.innerHTML = `<p class="world-empty-state">Reading authorized repository metadata locally…</p>`;
    let entries = [];
    try {
      const payload = await this.fetchJSON(
        `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(
          repo,
        )}/tree?path=`,
      );
      entries = normalizeTreeEntries(payload);
    } catch (_) {
      results.innerHTML = `<div class="world-notice world-notice-warning"><strong>Workshop unavailable</strong><span>The selected node is offline or authorization is required. No private repository probe or external-model request was made.</span></div>`;
      return;
    }
    const type = typeControl.value;
    const names = entries.map((entry) => entry.path);
    const lowerNames = names.map((name) => name.toLowerCase());
    const candidates = [];
    const recommendations = [];
    if (type === "Database-model analysis") {
      entries.forEach((entry) => {
        if (/(model|schema|migration|entity|database|\\.sql)/i.test(entry.name)) {
          candidates.push(entry.path);
        }
      });
      recommendations.push(
        "Inspect candidate model definitions and migrations for relationship declarations.",
        "Build the relationship graph from authorized source references before treating possible cycles or duplicate concepts as findings.",
        "Compare table and field names across the listed paths for potentially redundant concepts.",
      );
    } else if (type === "Dependency mapping") {
      entries.forEach((entry) => {
        if (
          /(^|\/)(package-lock\.json|package\.json|pyproject\.toml|requirements.*|cargo\.toml|go\.mod|cmakelists\.txt)$/i.test(
            entry.path,
          )
        ) {
          candidates.push(entry.path);
        }
      });
      recommendations.push(
        "Parse the selected manifests in an authorized agent session to map direct and transitive dependencies.",
        "Keep inferred relationships separate from verified lockfile edges.",
      );
    } else if (type === "Redundancy detection") {
      const stems = new Map();
      entries.forEach((entry) => {
        const stem = entry.name.toLowerCase().replace(/\.[^.]+$/, "");
        stems.set(stem, [...(stems.get(stem) || []), entry.path]);
      });
      stems.forEach((paths) => {
        if (paths.length > 1) candidates.push(...paths);
      });
      recommendations.push(
        "Matching names are only candidates; compare responsibilities and call sites before removing anything.",
      );
    } else if (type === "Dead-code detection") {
      candidates.push(...names.filter((name) => /(deprecated|legacy|unused|old)/i.test(name)));
      recommendations.push(
        "Tree metadata cannot prove dead code. Run reference and build analysis before taking action.",
      );
    } else if (type === "Security analysis") {
      candidates.push(
        ...names.filter((name) =>
          /(^|\/)(auth|crypto|security|secret|wallet|permission|\.env)/i.test(name),
        ),
      );
      recommendations.push(
        "Review the latest commit with dependency, secret-detection, and static-analysis scanners.",
        "Keep exploit detail and secrets out of public workshop output.",
      );
    } else if (type === "Test-coverage analysis") {
      candidates.push(...names.filter((name) => /(^|\/)(test|tests|spec|coverage)/i.test(name)));
      const testCount = lowerNames.filter((name) => /(test|spec)/.test(name)).length;
      const coverageRows = entries
        .filter((entry) => entry.type === "file")
        .map((entry) => ({
          entry,
          value:
            typeof entry.coverage === "number" &&
            Number.isFinite(entry.coverage) &&
            entry.coverage >= 0 &&
            entry.coverage <= 100
              ? entry.coverage
              : null,
        }));
      const measured = coverageRows.filter((row) => row.value !== null);
      if (measured.length) {
        const covered = measured.filter((row) => row.value >= 80).length;
        const partial = measured.filter(
          (row) => row.value > 0 && row.value < 80,
        ).length;
        const uncovered = measured.filter((row) => row.value === 0).length;
        const unknown = coverageRows.length - measured.length;
        candidates.push(
          ...measured
            .filter((row) => row.value < 80)
            .map((row) => row.entry.path),
        );
        recommendations.push(
          `Commit-matched artifact states at this tree depth: ${covered} covered (80%+), ${partial} partial, ${uncovered} uncovered, and ${unknown} unknown.`,
          "Treat artifact-reported percentages as test-execution evidence for this commit, not proof of behavioral correctness.",
        );
      } else {
        recommendations.push(
          `${testCount} root-level test-related entries were identified; load a commit-matched coverage artifact before drawing coverage conclusions.`,
        );
      }
    } else if (type === "Documentation analysis") {
      candidates.push(...names.filter((name) => /(readme|docs|contributing|changelog|\\.md$)/i.test(name)));
      recommendations.push(
        "Compare public APIs and configuration surfaces with the documentation paths listed below.",
      );
    } else if (type === "Performance analysis") {
      candidates.push(
        ...entries
          .filter((entry) => entry.type === "file")
          .sort((a, b) => b.size - a.size)
          .slice(0, 10)
          .map((entry) => entry.path),
      );
      recommendations.push(
        "Large blobs are investigation candidates, not proof of a runtime bottleneck; profile before optimizing.",
      );
    } else if (type === "License compatibility analysis") {
      candidates.push(...names.filter((name) => /(license|copying|notice|package|lock)/i.test(name)));
      recommendations.push(
        "Resolve declared and transitive package licenses against the project policy; this is not legal advice.",
      );
    } else {
      candidates.push(...entries.filter((entry) => entry.type === "directory").map((entry) => entry.path));
      recommendations.push(
        "Treat top-level directories as architectural boundaries to verify against imports, build targets, services, and APIs.",
      );
    }
    const uniqueCandidates = [...new Set(candidates)].slice(0, 24);
    results.innerHTML = `
      <section class="world-workshop-results">
        <span class="world-status-pill">Recommendation · ${escapeHTML(type)}</span>
        <h3>${escapeHTML(owner)}/${escapeHTML(repo)}</h3>
        <div class="world-model-graph" aria-label="Candidate relationship graph">
          ${
            uniqueCandidates.length
              ? uniqueCandidates
                  .slice(0, 12)
                  .map(
                    (path, index) =>
                      `<span style="--graph-index:${index}">${escapeHTML(path)}</span>`,
                  )
                  .join("")
              : `<span>No root-level candidates matched this metadata-only pass.</span>`
          }
        </div>
        <ul class="world-detail-list">${recommendations
          .map((item) => `<li>${escapeHTML(item)}</li>`)
          .join("")}</ul>
        <details>
          <summary>Supporting file references</summary>
          <ul class="world-file-reference-list">${
            uniqueCandidates.length
              ? uniqueCandidates.map((path) => `<li><code>${escapeHTML(path)}</code></li>`).join("")
              : "<li>None at the selected tree depth.</li>"
          }</ul>
        </details>
        <p class="world-panel-footnote">This metadata pass is a recommendation, not a guaranteed fact. A deeper owner-authorized agent can find definitions, use sites, relationships, possible circular dependencies, duplicated concepts, and redundant fields.</p>
      </section>`;
  }

  async playRadio(stationId) {
    const station = RADIO_STATIONS.find((item) => item.id === stationId);
    const now = this.$("[data-world-media-now]");
    if (!station || !now) return;
    this.stopRadio(false);
    if (station.playMode === "external") {
      const provider = window.open(
        station.homepageUrl,
        "_blank",
        "noopener,noreferrer",
      );
      now.innerHTML = `
        <span><strong>${escapeHTML(station.name)}</strong> · official ${escapeHTML(
          station.provider,
        )} player<small>Current provider track metadata is unavailable because ForkMesh did not receive a permitted provider metadata event. ForkMesh does not embed, restream, record, or control the provider’s audio.</small></span>
        <button type="button" data-world-radio-stop disabled>Controlled by provider</button>`;
      this.toast(
        provider
          ? `Opened ${station.name} on the provider’s site.`
          : "The browser blocked the provider window; use the provider-page link.",
      );
      return;
    }
    if (!this.soundEnabled) {
      now.innerHTML = `
        <span><strong>Sound is off</strong><small>Use the Sound button in the World toolbar first. Audio never starts automatically.</small></span>
        <button type="button" data-world-radio-stop disabled>Mute / stop</button>`;
      this.toast("Enable World sounds from the toolbar before starting local audio.");
      return;
    }
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    if (!AudioContext) {
      now.innerHTML = `<span>Local audio synthesis is unavailable in this browser.</span><button type="button" data-world-radio-stop disabled>Mute / stop</button>`;
      return;
    }
    const scoreOffsetMs =
      ((Date.now() % WORLD_SCORE_LOOP_MS) + WORLD_SCORE_LOOP_MS) %
      WORLD_SCORE_LOOP_MS;
    const soundtrack = createProceduralWorldSoundtrack(
      AudioContext,
      scoreOffsetMs,
    );
    this.activeAudio = soundtrack;
    now.innerHTML = `
      <span><strong>${escapeHTML(station.name)}</strong> · ${escapeHTML(
        station.provider,
      )}<small data-world-track>Original four-hour procedural downtempo score · Local score offset ${escapeHTML(
        formatMediaPosition(scoreOffsetMs),
      )} · loops independently of the UTC display · CC0-1.0 · local playback only.</small></span>
      <button type="button" data-world-radio-stop>Mute / stop</button>`;
    try {
      await soundtrack.context.resume();
      this.toast(`${station.name} is playing on this device only.`);
    } catch (_) {
      soundtrack.stop();
      await soundtrack.context.close().catch(() => {});
      this.activeAudio = null;
      now.innerHTML = `
        <span>Playback was blocked or the provider stream is unavailable.</span>
        <button type="button" data-world-radio-stop disabled>Mute / stop</button>`;
    }
  }

  stopRadio(render = true) {
    try {
      this.activeAudio?.stop?.();
      this.activeAudio?.context?.close?.();
    } catch (_) {}
    this.activeAudio = null;
    if (!render) return;
    const now = this.$("[data-world-media-now]");
    if (now) {
      now.innerHTML = `
        <span>Nothing is playing. Audio never starts automatically.</span>
        <button type="button" data-world-radio-stop disabled>Mute / stop</button>`;
    }
  }

  handleLandmarkAction(action) {
    if (action === "tour") {
      this.closeLandmark();
      this.startTour();
      return;
    }
    if (action === "repositories") {
      this.toast(
        this.repositories.length
          ? `${this.repositories.length} authorized repository portals are mapped. Choose one in this panel.`
          : this.repositoryCatalogState === "unavailable"
            ? "The live catalog is unavailable. No substitute portal is shown or treated as mirrored."
            : "The live catalog contains no public or account-authorized repository portal.",
      );
      return;
    }
    const messages = {
      reward:
        "Contributions are direct wallet-to-public-pool transfers. Reward plans are signed only in the instance owner’s local Qt client and shown as complete only after on-chain finality.",
      organizations:
        "Every floor and office delegates to the same organization and repository role checks as the console.",
      fediverse:
        "Only public and explicitly consented social relationships belong in this district.",
      security:
        "The clipboard shows only the latest redacted artifact; private evidence stays restricted.",
      launchpad:
        "Destinations retain normal permissions while events remain synchronized through UTC.",
      events:
        "Event records remain usable over HTTPS even when multiplayer presence is offline.",
      neighborhood:
        "Availability, inactivity, and door state are under your local privacy controls.",
      workshops:
        "Choose a repository and analysis scope in the workshop panel.",
      broadcast:
        "Audio starts only after your explicit play action and stays local to this device.",
      support:
        "Project support is voluntary, separate from node rewards, and never promises returns or governance dominance.",
    };
    this.toast(messages[action] || "This district is being connected to its technical backend.");
  }

  setWorldAccountStatus(message, state = "") {
    const status = this.$("[data-world-account-status]");
    if (!status) return;
    status.textContent = String(message || "");
    status.dataset.state = ["error", "success"].includes(state) ? state : "";
  }

  selectWorldAccountMode(mode) {
    const selected = mode === "signup" ? "signup" : "login";
    const tabs = this.$(".world-account-tabs");
    if (tabs) tabs.hidden = false;
    this.$$("[data-world-account-mode]").forEach((button) => {
      button.setAttribute(
        "aria-selected",
        String(button.dataset.worldAccountMode === selected),
      );
    });
    this.$$("[data-world-account-view]").forEach((view) => {
      view.hidden = view.dataset.worldAccountView !== selected;
    });
    this.$("[data-world-account-verification]")?.setAttribute("hidden", "");
    this.setWorldAccountStatus("");
    window.setTimeout(
      () =>
        this.$(
          `[data-world-account-view="${selected}"]:not([hidden]) input`,
        )?.focus(),
      0,
    );
  }

  toggleWorldAccount(open, mode = "login", returnFocus = null) {
    const panel = this.$("[data-world-account]");
    const backdrop = this.$(".world-account-backdrop");
    if (!panel || !backdrop) return;
    if (open) {
      this.accountReturnFocus =
        returnFocus instanceof HTMLElement ? returnFocus : null;
      this.closeWorldChat();
      this.closeLandmark();
      this.toggleSettings(false);
      if (this.tourIndex >= 0) this.stopTour();
      this.selectWorldAccountMode(mode);
    } else {
      this.$$(
        "[data-world-login-form] input[type='password'], " +
          "[data-world-signup-form] input[type='password'], " +
          "[data-world-login-form] input[name='totp']",
      ).forEach((input) => {
        input.value = "";
      });
      this.setWorldAccountStatus("");
    }
    panel.dataset.open = String(open);
    panel.setAttribute("aria-hidden", String(!open));
    backdrop.dataset.open = String(open);
    if (open) {
      window.setTimeout(
        () =>
          panel.querySelector(
            "input:not([hidden]), [data-world-account-close]",
          )?.focus(),
        80,
      );
    } else {
      const focus = this.accountReturnFocus;
      this.accountReturnFocus = null;
      window.setTimeout(() => focus?.focus?.(), 0);
    }
  }

  async submitWorldLogin(form) {
    if (!(form instanceof HTMLFormElement)) return;
    const email = String(form.elements.email?.value || "").trim();
    const password = String(form.elements.password?.value || "");
    const totp = String(form.elements.totp?.value || "").trim();
    if (!/.+@.+\..+/.test(email) || !password) {
      this.setWorldAccountStatus(
        "Enter a valid email address and password.",
        "error",
      );
      return;
    }
    const submit = form.querySelector("[data-world-account-submit]");
    if (submit) {
      submit.disabled = true;
      submit.textContent = "Logging in…";
    }
    this.setWorldAccountStatus("Verifying this account…");
    try {
      const body = await this.postJSON(
        "/api/accounts/login",
        { email, password, totp },
        { auth: false, timeout: 12_000 },
      );
      if (!storeWorldSession(body)) {
        throw new Error("invalid_session");
      }
      form.elements.password.value = "";
      form.elements.totp.value = "";
      this.captureWorldPosition(true);
      this.setWorldAccountStatus(
        `Logged in as ${String(body.nodeName || "").toLowerCase()}. Reloading the same World position…`,
        "success",
      );
      window.setTimeout(() => location.reload(), 350);
    } catch (error) {
      const code = String(error?.message || "");
      const messages = {
        invalid_credentials: "Incorrect email or password.",
        bad_totp: "Enter the current authenticator code.",
        too_many_attempts:
          "Too many failed attempts. Wait a few minutes and try again.",
        account_disabled: "This account has been disabled.",
      };
      this.setWorldAccountStatus(
        messages[code] || "Could not log in. Please try again.",
        "error",
      );
      form.elements.password.value = "";
      form.elements.totp.value = "";
      form.elements.password.focus();
      if (submit) {
        submit.disabled = false;
        submit.textContent = "Log in inside the World";
      }
    }
  }

  async submitWorldSignup(form) {
    if (!(form instanceof HTMLFormElement)) return;
    const nodeName = String(form.elements.nodeName?.value || "")
      .trim()
      .toLowerCase();
    const email = String(form.elements.email?.value || "").trim();
    const password = String(form.elements.password?.value || "");
    const terms = Boolean(form.elements.terms?.checked);
    form.elements.nodeName.value = nodeName;
    if (!WORLD_ACCOUNT_NAME_RE.test(nodeName)) {
      this.setWorldAccountStatus(
        "Use lowercase letters, numbers, and hyphens; start with a letter and end with a letter or number.",
        "error",
      );
      return;
    }
    if (!/.+@.+\..+/.test(email)) {
      this.setWorldAccountStatus("Enter a valid email address.", "error");
      return;
    }
    if (password.length < 8) {
      this.setWorldAccountStatus(
        "Password must contain at least 8 characters.",
        "error",
      );
      return;
    }
    if (!terms) {
      this.setWorldAccountStatus(
        "Accept the Terms and Privacy Policy to continue.",
        "error",
      );
      return;
    }
    const submit = form.querySelector("[data-world-account-submit]");
    if (submit) {
      submit.disabled = true;
      submit.textContent = "Creating…";
    }
    this.setWorldAccountStatus("Creating the account securely…");
    try {
      await this.postJSON(
        "/api/accounts/signup",
        { nodeName, email, password },
        { auth: false, timeout: 12_000 },
      );
      form.elements.password.value = "";
      this.$$("[data-world-account-view]").forEach((view) => {
        view.hidden = true;
      });
      const tabs = this.$(".world-account-tabs");
      if (tabs) tabs.hidden = true;
      const verification = this.$("[data-world-account-verification]");
      if (verification) verification.hidden = false;
      const createdName = this.$("[data-world-account-created-name]");
      const createdEmail = this.$("[data-world-account-created-email]");
      if (createdName) createdName.textContent = nodeName;
      if (createdEmail) createdEmail.textContent = email;
      this.setWorldAccountStatus(
        "Account created. Verification is required before account permissions appear in the World.",
        "success",
      );
    } catch (error) {
      const code = String(error?.message || "");
      const messages = {
        node_name_taken: "That username is already taken.",
        email_taken: "That email is already registered.",
        password_too_short:
          "Password must contain at least 8 characters.",
      };
      this.setWorldAccountStatus(
        messages[code] || "Could not create the account. Please try again.",
        "error",
      );
      form.elements.password.value = "";
      form.elements.password.focus();
      if (submit) {
        submit.disabled = false;
        submit.textContent = "Create account inside the World";
      }
    }
  }

  async logoutFromWorld() {
    const session = readSession();
    try {
      await fetch("/api/accounts/logout", {
        method: "POST",
        headers: session?.sessionToken
          ? { Authorization: `Bearer ${session.sessionToken}` }
          : {},
        credentials: "same-origin",
        cache: "no-store",
      });
    } catch (_) {
      // Local logout must still complete if the network is unavailable.
    }
    try {
      localStorage.removeItem("forkmesh.session");
      document.cookie =
        "forkmesh_session=; Path=/; Max-Age=0; SameSite=Lax" +
        (location.protocol === "https:" ? "; Secure" : "");
    } catch (_) {}
    this.captureWorldPosition(true);
    location.reload();
  }

  toggleSettings(open) {
    const panel = this.$("[data-world-settings]");
    if (!panel) return;
    panel.dataset.open = String(open);
    panel.setAttribute("aria-hidden", String(!open));
    if (open) window.setTimeout(() => panel.querySelector("button")?.focus(), 80);
  }

  openWorldChat(href = "/dashboard/chat", returnFocus = null) {
    let destination;
    try {
      destination = new URL(String(href || "/dashboard/chat"), location.origin);
    } catch (_) {
      return;
    }
    if (
      destination.origin !== location.origin ||
      !["/dashboard/chat", "/dashboard/chat/"].includes(destination.pathname)
    ) {
      return;
    }
    destination.searchParams.set("worldEmbed", "1");
    const panel = this.$("[data-world-chat]");
    const backdrop = this.$(".world-chat-backdrop");
    const frame = this.$("[data-world-chat-frame]");
    if (!panel || !backdrop || !frame) return;
    const frameURL = `${destination.pathname}${destination.search}`;
    if (frame.dataset.worldChatUrl !== frameURL) {
      frame.dataset.worldChatUrl = frameURL;
      frame.src = frameURL;
    }
    this.chatReturnFocus =
      returnFocus instanceof HTMLElement ? returnFocus : null;
    this.closeLandmark();
    this.toggleSettings(false);
    if (this.tourIndex >= 0) this.stopTour();
    panel.dataset.open = "true";
    panel.setAttribute("aria-hidden", "false");
    backdrop.dataset.open = "true";
    window.setTimeout(
      () => panel.querySelector("[data-world-chat-close]")?.focus(),
      80,
    );
  }

  loadChatTerminalFrame() {
    const frame = this.$("[data-world-chat-terminal-frame]");
    if (!frame || frame.dataset.worldChatUrl) return;
    const frameURL = "/dashboard/chat?worldEmbed=1";
    frame.dataset.worldChatUrl = frameURL;
    frame.src = frameURL;
  }

  closeWorldChat() {
    const panel = this.$("[data-world-chat]");
    const backdrop = this.$(".world-chat-backdrop");
    if (panel) {
      panel.dataset.open = "false";
      panel.setAttribute("aria-hidden", "true");
    }
    if (backdrop) backdrop.dataset.open = "false";
    const returnFocus = this.chatReturnFocus;
    this.chatReturnFocus = null;
    window.setTimeout(() => returnFocus?.focus?.(), 0);
  }

  setTheme(theme) {
    if (!THEME_OPTIONS.some((option) => option.id === theme)) return;
    this.settings.theme = theme;
    this.saveSettings();
    this.world?.setTheme(theme);
    this.$$("[data-world-theme]").forEach((button) => {
      button.setAttribute("aria-pressed", String(button.dataset.worldTheme === theme));
    });
    const label = THEME_OPTIONS.find((option) => option.id === theme)?.label || theme;
    this.toast(`${label} is local to this device and never changes shared presence.`);
  }

  setLightLevel(value) {
    const numeric = Number(value);
    const next = Math.min(
      WORLD_LIGHT_LEVEL_MAX,
      Math.max(
        WORLD_LIGHT_LEVEL_MIN,
        Number.isFinite(numeric) ? numeric : WORLD_LIGHT_LEVEL_DEFAULT,
      ),
    );
    this.settings.lightLevel = next;
    this.saveSettings();
    this.world?.setLightLevel(next);
    const input = this.$("[data-world-light-level]");
    const output = this.$("[data-world-light-level-output]");
    if (input && Number(input.value) !== next) input.value = String(next);
    if (output) output.textContent = `${next}%`;
    return next;
  }

  movementTuning() {
    return {
      speed: this.settings.moveSpeed / 100,
      // The max slider position means "instant" — hand the scene Infinity so it
      // snaps to top speed with no ramp.
      acceleration:
        this.settings.moveAccel >= WORLD_MOVE_ACCEL_MAX
          ? Infinity
          : this.settings.moveAccel / 100,
    };
  }

  setMoveSpeed(value) {
    const numeric = Number(value);
    const next = Math.min(
      WORLD_MOVE_SPEED_MAX,
      Math.max(
        WORLD_MOVE_SPEED_MIN,
        Number.isFinite(numeric) ? numeric : WORLD_MOVE_SPEED_DEFAULT,
      ),
    );
    this.settings.moveSpeed = next;
    this.saveSettings();
    this.world?.setMovementTuning?.(this.movementTuning());
    const input = this.$("[data-world-move-speed]");
    const output = this.$("[data-world-move-speed-output]");
    if (input && Number(input.value) !== next) input.value = String(next);
    if (output) output.textContent = `${next}%`;
    return next;
  }

  setMoveAccel(value) {
    const numeric = Number(value);
    const next = Math.min(
      WORLD_MOVE_ACCEL_MAX,
      Math.max(
        WORLD_MOVE_ACCEL_MIN,
        Number.isFinite(numeric) ? numeric : WORLD_MOVE_ACCEL_DEFAULT,
      ),
    );
    this.settings.moveAccel = next;
    this.saveSettings();
    this.world?.setMovementTuning?.(this.movementTuning());
    const input = this.$("[data-world-move-accel]");
    const output = this.$("[data-world-move-accel-output]");
    if (input && Number(input.value) !== next) input.value = String(next);
    if (output) {
      output.textContent = next >= WORLD_MOVE_ACCEL_MAX ? "∞" : `${next}%`;
    }
    return next;
  }

  async toggleWorldSound() {
    const button = this.$("[data-world-sound-toggle]");
    const label = this.$("[data-world-sound-label]");
    if (this.soundEnabled) {
      this.soundEnabled = false;
      const context = this.soundContext;
      this.soundContext = null;
      await context?.close?.().catch(() => {});
      button?.setAttribute("aria-pressed", "false");
      if (button) button.title = "Enable World sounds";
      if (label) label.textContent = "Sound";
      this.toast("World sounds muted.");
      return;
    }
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    if (!AudioContext) {
      this.toast("World sounds are unavailable in this browser.");
      return;
    }
    try {
      // This is the only path that creates the shared cue context, and it runs
      // directly from the user's sound-button click.
      this.soundContext = new AudioContext();
      await this.soundContext.resume();
      this.soundEnabled = true;
      button?.setAttribute("aria-pressed", "true");
      if (button) button.title = "Mute World sounds";
      if (label) label.textContent = "Sound on";
      this.playCountryJoinSound("FM", true);
      this.toast("World sounds enabled. Join cues use short local tones.");
    } catch (_) {
      this.soundEnabled = false;
      this.soundContext = null;
      this.toast("World sounds could not be enabled.");
    }
  }

  playCountryJoinSound(countryCode, force = false) {
    const context = this.soundContext;
    if (!this.soundEnabled || !context || context.state === "closed") return;
    const now = Date.now();
    this.joinSoundTimes = this.joinSoundTimes.filter(
      (timestamp) => now - timestamp < 10000,
    );
    if (!force && this.joinSoundTimes.length >= 3) return;
    this.joinSoundTimes.push(now);
    const code = /^[A-Z]{2}$/.test(String(countryCode || ""))
      ? String(countryCode)
      : "XX";
    const seed = code.charCodeAt(0) * 37 + code.charCodeAt(1) * 17;
    const scale = [0, 2, 3, 5, 7, 9, 10];
    const first = 220 * 2 ** (scale[seed % scale.length] / 12);
    const second = 220 * 2 ** (scale[(seed >> 3) % scale.length] / 12);
    [first, second].forEach((frequency, index) => {
      const start = context.currentTime + index * 0.09;
      const oscillator = context.createOscillator();
      const gain = context.createGain();
      oscillator.type = "sine";
      oscillator.frequency.setValueAtTime(frequency, start);
      gain.gain.setValueAtTime(0.0001, start);
      gain.gain.exponentialRampToValueAtTime(0.035, start + 0.018);
      gain.gain.exponentialRampToValueAtTime(0.0001, start + 0.11);
      oscillator.connect(gain);
      gain.connect(context.destination);
      oscillator.start(start);
      oscillator.stop(start + 0.12);
    });
  }

  saveSettings() {
    writeJSON(localStorage, SETTINGS_KEY, this.settings);
  }

  toast(message) {
    const element = this.$("[data-world-toast]");
    if (!element) return;
    window.clearTimeout(this.toastTimer);
    element.textContent = message;
    element.dataset.open = "true";
    this.toastTimer = window.setTimeout(() => {
      element.dataset.open = "false";
    }, 4200);
  }

  startTour() {
    this.closeLandmark();
    this.tourIndex = 0;
    const panel = this.$("[data-world-tour]");
    if (panel) {
      panel.dataset.open = "true";
      panel.setAttribute("aria-hidden", "false");
    }
    this.renderTourStep();
  }

  nextTourStep() {
    if (this.tourIndex < 0) return;
    if (this.tourIndex >= TOUR_STEPS.length - 1) {
      this.stopTour();
      this.toast("Tour complete. The whole city is open to you.");
      return;
    }
    this.tourIndex += 1;
    this.renderTourStep();
  }

  renderTourStep() {
    const step = TOUR_STEPS[this.tourIndex];
    if (!step) return;
    this.world?.focusLandmark(step.landmark);
    const title = this.$("[data-world-tour-title]");
    const copy = this.$("[data-world-tour-copy]");
    const progress = this.$("[data-world-tour-progress]");
    const next = this.$("[data-world-tour-next]");
    if (title) title.textContent = step.title;
    if (copy) copy.textContent = step.copy;
    if (progress) {
      progress.innerHTML = TOUR_STEPS.map(
        (_, index) =>
          `<span data-complete="${String(index <= this.tourIndex)}"></span>`,
      ).join("");
    }
    if (next) {
      next.textContent =
        this.tourIndex === TOUR_STEPS.length - 1 ? "Finish tour ✓" : "Next stop →";
    }
  }

  stopTour() {
    this.tourIndex = -1;
    const panel = this.$("[data-world-tour]");
    if (panel) {
      panel.dataset.open = "false";
      panel.setAttribute("aria-hidden", "true");
    }
    this.world?.clearFocus();
  }

  startDiagnostics() {
    window.clearInterval(this.diagnosticsTimer);
    this.renderDiagnostics();
    this.diagnosticsTimer = window.setInterval(
      () => this.renderDiagnostics(),
      WORLD_DIAGNOSTICS_INTERVAL_MS,
    );
  }

  collectDiagnostics(now = performance.now()) {
    const sampleNow = Number.isFinite(Number(now))
      ? Number(now)
      : performance.now();
    const elapsedMs = Math.max(1, sampleNow - this.diagnosticsSampleAt);
    const inboundFrames = Math.max(0, Number(this.socketInboundFrames) || 0);
    const outboundFrames = Math.max(0, Number(this.socketOutboundFrames) || 0);
    const inboundRate = Math.min(
      10_000,
      (Math.max(0, inboundFrames - this.diagnosticsInboundSample) * 1000) /
        elapsedMs,
    );
    const outboundRate = Math.min(
      10_000,
      (Math.max(0, outboundFrames - this.diagnosticsOutboundSample) * 1000) /
        elapsedMs,
    );
    this.diagnosticsSampleAt = sampleNow;
    this.diagnosticsInboundSample = inboundFrames;
    this.diagnosticsOutboundSample = outboundFrames;

    const readyState = Number(this.socket?.readyState);
    let socketState = "offline";
    if (this.presenceConnecting || readyState === 0) {
      socketState = "connecting";
    } else if (readyState === 1) {
      socketState = this.serverPeerId ? "online" : "handshaking";
    } else if (readyState === 2) {
      socketState = "closing";
    } else if (this.socketTimer) {
      socketState = "reconnecting";
    }
    const socketOnline = readyState === 1 && Boolean(this.serverPeerId);
    const peerCount =
      1 +
      this.remotePlayers.size +
      (socketOnline ? 0 : this.localPeers.size);
    const scene = this.world?.getDiagnostics?.(sampleNow) || null;
    const snapshot = {
      renderer: scene
        ? {
            fps: Math.max(0, Math.min(1000, Number(scene.fps) || 0)),
            frameTimeMs: Math.max(
              0,
              Math.min(60_000, Number(scene.frameTimeMs) || 0),
            ),
            calls: Math.max(
              0,
              Math.min(10_000_000, Number(scene.rendererCalls) || 0),
            ),
            triangles: Math.max(
              0,
              Math.min(1_000_000_000, Number(scene.rendererTriangles) || 0),
            ),
            paused: scene.paused === true,
          }
        : null,
      connection: {
        state: socketState,
        peers: Math.max(1, Math.min(10_000, peerCount)),
        reconnects: Math.max(
          0,
          Math.min(
            WORLD_DIAGNOSTICS_COUNTER_MAX,
            this.socketConnectionAttempts - 1,
          ),
        ),
        bufferedBytes: Math.max(
          0,
          Math.min(
            64 * 1024 * 1024,
            Number(this.socket?.bufferedAmount) || 0,
          ),
        ),
      },
      traffic: {
        inboundFrames,
        outboundFrames,
        inboundRate,
        outboundRate,
      },
      queues: {
        movement:
          this.pendingMovement && this.movementSendTimer
            ? "coalescing"
            : this.pendingMovement
              ? "queued"
              : "idle",
        profile:
          this.profilePresencePending && this.profilePresenceTimer
            ? "coalescing"
            : this.profilePresencePending
              ? "queued"
              : "idle",
        movementCoalesced: Math.max(
          0,
          Math.min(
            WORLD_DIAGNOSTICS_COUNTER_MAX,
            Number(this.movementCoalescedFrames) || 0,
          ),
        ),
        profileCoalesced: Math.max(
          0,
          Math.min(
            WORLD_DIAGNOSTICS_COUNTER_MAX,
            Number(this.profileCoalescedFrames) || 0,
          ),
        ),
        backpressureEvents: Math.max(
          0,
          Math.min(
            WORLD_DIAGNOSTICS_COUNTER_MAX,
            Number(this.socketBackpressureEvents) || 0,
          ),
        ),
      },
      build: { ...this.buildDiagnostics },
    };
    this.lastDiagnosticsSnapshot = snapshot;
    return snapshot;
  }

  renderDiagnostics() {
    const root = this.$("[data-world-diagnostics]");
    if (!root) return;
    const snapshot = this.collectDiagnostics();
    const { renderer, connection, traffic, queues, build } = snapshot;
    const formatRate = (value) =>
      `${Math.max(0, Number(value) || 0).toFixed(1)}/s`;
    const rendererSummary = renderer
      ? renderer.paused
        ? "renderer paused"
        : `${renderer.fps.toFixed(0)} FPS · ${renderer.frameTimeMs.toFixed(1)} ms`
      : "renderer unavailable";
    const version = build.version
      ? `${/^v/i.test(build.version) ? "" : "v"}${build.version}`
      : "build pending";
    const summary = this.$("[data-world-diagnostics-summary]");
    if (summary) {
      summary.textContent = `${rendererSummary} · socket ${connection.state} · ${connection.peers} ${connection.peers === 1 ? "peer" : "peers"} · ${version}`;
    }
    const light = this.$("[data-world-diagnostics-light]");
    if (light) {
      light.dataset.state =
        connection.state === "online"
          ? "online"
          : ["connecting", "handshaking", "reconnecting"].includes(
                connection.state,
              )
            ? "connecting"
            : "offline";
    }
    const rendererDetail = this.$("[data-world-diagnostics-renderer]");
    if (rendererDetail) {
      rendererDetail.textContent = renderer
        ? `${renderer.paused ? "Paused" : `${renderer.fps.toFixed(1)} FPS · ${renderer.frameTimeMs.toFixed(1)} ms/frame`} · ${Math.round(renderer.calls).toLocaleString()} calls · ${Math.round(renderer.triangles).toLocaleString()} triangles`
        : "WebGL renderer unavailable";
    }
    const connectionDetail = this.$(
      "[data-world-diagnostics-connection]",
    );
    if (connectionDetail) {
      connectionDetail.textContent = `${connection.state} · ${connection.peers} ${connection.peers === 1 ? "peer" : "peers"} · ${connection.reconnects} reconnect attempts · ${Math.round(connection.bufferedBytes).toLocaleString()} buffered bytes`;
    }
    const trafficDetail = this.$("[data-world-diagnostics-traffic]");
    if (trafficDetail) {
      trafficDetail.textContent = `Inbound ${Math.round(traffic.inboundFrames).toLocaleString()} (${formatRate(traffic.inboundRate)}) · outbound ${Math.round(traffic.outboundFrames).toLocaleString()} (${formatRate(traffic.outboundRate)})`;
    }
    const queueDetail = this.$("[data-world-diagnostics-queues]");
    if (queueDetail) {
      queueDetail.textContent = `Movement ${queues.movement} (${queues.movementCoalesced} coalesced) · profile ${queues.profile} (${queues.profileCoalesced} coalesced) · ${queues.backpressureEvents} backpressure events`;
    }
    const buildDetail = this.$("[data-world-diagnostics-build]");
    if (buildDetail) {
      buildDetail.textContent = build.version
        ? `${version}${build.revision ? ` · ${build.revision.slice(0, 12)}` : " · revision unavailable"}`
        : "Version endpoint unavailable";
    }
  }

  startActivityTicker() {
    const messages = () => {
      const nodes = liveNodeRecords(this.network, this.mirrorCatalogs);
      const repos = this.repositories;
      const output = [
        nodes.length
          ? `${nodes[0].name} is online as a mirror operator.`
          : "The world degrades gracefully while live node data is unavailable.",
        repos.find((repo) => repo.liveHost)
          ? `${repos.find((repo) => repo.liveHost).owner}/${repos.find((repo) => repo.liveHost).name} has a healthy public route.`
          : "Repository portals distinguish live mirrors from stubs and unavailable hosts.",
        "Activity is generalized; private URLs, searches, forms, and history stay out of the world.",
        "Visual reward particles are illustrative and do not represent a guaranteed transfer.",
        this.remotePlayers.size
          ? `${this.remotePlayers.size} other ${this.remotePlayers.size === 1 ? "visitor is" : "visitors are"} moving through the world.`
          : "Presence uses a tiny ephemeral channel with a static-world fallback.",
        ...[...this.remotePlayers.values()]
          .filter((player) => player.category && player.category !== "hidden")
          .slice(0, 2)
          .map((player) => {
            const label =
              ACTIVITY_OPTIONS.find((option) => option.id === player.category)
                ?.label || "Exploring ForkMesh";
            return `${player.name} is ${label.toLowerCase()}.`;
          }),
      ].filter(Boolean);
      return output;
    };
    let index = 0;
    const render = () => {
      const list = messages();
      const element = this.$("[data-world-activity]");
      if (element && list.length) element.textContent = list[index % list.length];
      index += 1;
    };
    render();
    this.activityTimer = window.setInterval(render, 6500);
  }

  handleMovement(movement) {
    const space = WORLD_SPACE_IDS.has(String(movement?.space || ""))
      ? String(movement.space)
      : this.currentSpace;
    this.lastMovement = {
      ...movement,
      space,
      activity: this.settings.privacy.activity ? movement.activity : "online",
    };
    this.spawnSelected = true;
    this.rememberWorldPosition(
      this.lastMovement,
      movement?.moving === false,
    );
    this.queueMovementPresence({
      ...this.lastMovement,
      moving: movement?.moving !== false,
    });
    this.broadcastLocalPresence();
  }

  rememberWorldPosition(position, flush = false) {
    if (!this.positionKey) return;
    const record = normalizedWorldPosition(
      {
        x: position?.x,
        y: position?.y,
        z: position?.z,
        heading: position?.heading ?? position?.yaw,
        space: WORLD_SPACE_IDS.has(String(position?.space || ""))
          ? String(position.space)
          : this.currentSpace,
        updatedAt: Date.now(),
      },
      Date.now(),
    );
    if (!record) return;
    this.pendingPosition = record;
    if (flush) {
      this.flushWorldPosition();
      return;
    }
    if (!this.positionWriteTimer) {
      this.positionWriteTimer = window.setTimeout(
        () => this.flushWorldPosition(),
        POSITION_WRITE_INTERVAL_MS,
      );
    }
  }

  captureWorldPosition(flush = false) {
    const position = this.world?.getPosition?.();
    if (position) this.rememberWorldPosition(position, flush);
  }

  flushWorldPosition() {
    window.clearTimeout(this.positionWriteTimer);
    this.positionWriteTimer = 0;
    if (!this.pendingPosition || !this.positionKey) return;
    const { x, y, z, heading, space, updatedAt } = this.pendingPosition;
    writeJSON(localStorage, this.positionKey, {
      x,
      y,
      z,
      heading,
      space,
      updatedAt,
    });
    this.pendingPosition = null;
  }

  queueMovementPresence(movement) {
    if (this.pendingMovement) {
      this.movementCoalescedFrames = incrementDiagnosticCounter(
        this.movementCoalescedFrames,
      );
    }
    this.pendingMovement = {
      ...movement,
      moving: movement?.moving !== false,
    };
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) return;
    if (!this.pendingMovement.moving) {
      window.clearTimeout(this.movementSendTimer);
      this.movementSendTimer = 0;
      this.flushMovementPresence();
      return;
    }
    const elapsed = performance.now() - this.lastMovementSentAt;
    if (elapsed >= MOVEMENT_SEND_INTERVAL_MS) {
      this.flushMovementPresence();
      return;
    }
    if (!this.movementSendTimer) {
      this.movementSendTimer = window.setTimeout(
        () => this.flushMovementPresence(),
        Math.max(0, MOVEMENT_SEND_INTERVAL_MS - elapsed),
      );
    }
  }

  flushMovementPresence() {
    window.clearTimeout(this.movementSendTimer);
    this.movementSendTimer = 0;
    if (!this.pendingMovement) return;
    const movement = this.pendingMovement;
    if (
      this.sendPresenceNow({
        type: "move",
        ...movement,
      })
    ) {
      this.pendingMovement = null;
      this.lastMovementSentAt = performance.now();
    } else if (
      this.socket?.readyState === WebSocket.OPEN &&
      !this.destroyed
    ) {
      this.movementSendTimer = window.setTimeout(
        () => this.flushMovementPresence(),
        MOVEMENT_SEND_INTERVAL_MS,
      );
    }
  }

  async refreshWorldTicket() {
    if (this.destroyed || document.hidden) return;
    if (!readSession()?.sessionToken) {
      this.worldTicket = "";
      this.worldTicketExpires = 0;
      return;
    }
    try {
      const ticket = await this.fetchJSON("/api/world/ticket", {
        timeout: 5000,
        cache: "no-store",
      });
      if (
        ticket?.authenticated === true &&
        ACCOUNT_STATUS_VALUES.has(String(ticket.accountStatus || "")) &&
        ticket.accountStatus !== "Guest"
      ) {
        this.worldTicket = String(ticket.ticket || "");
        this.worldTicketExpires = Number(ticket.expiresAt || 0);
        this.identity.isAdmin = ticket.isAdmin === true;
        return;
      }
      this.worldTicket = "";
      this.worldTicketExpires = 0;
      if (this.identity) this.identity.isAdmin = false;
    } catch (_) {
      // Keep a still-valid ticket for reconnect; clear only an expired one.
      if (this.worldTicketExpires <= Date.now()) {
        this.worldTicket = "";
        this.worldTicketExpires = 0;
      }
    }
  }

  startWorldTicketRefresh() {
    window.clearInterval(this.worldTicketTimer);
    this.worldTicketTimer = window.setInterval(() => {
      if (!document.hidden && readSession()?.sessionToken) {
        this.refreshWorldTicket();
      }
    }, WORLD_TICKET_REFRESH_MS);
  }

  schedulePresenceReconnect() {
    if (this.destroyed || document.hidden) return;
    window.clearTimeout(this.socketTimer);
    const jitter = 0.75 + Math.random() * 0.5;
    const delay = Math.max(250, Math.round(this.socketRetry * jitter));
    this.socketTimer = window.setTimeout(() => {
      this.socketTimer = 0;
      this.connectPresence();
    }, delay);
    this.socketRetry = Math.min(
      SOCKET_RETRY_MAX_MS,
      Math.round(this.socketRetry * 1.8),
    );
  }

  startPeerReconnectGrace() {
    window.clearTimeout(this.peerGraceTimer);
    if (!this.remotePlayers.size) {
      this.peerGraceUntil = 0;
      this.renderPeers();
      return;
    }
    this.peerGraceUntil = Date.now() + SOCKET_PEER_GRACE_MS;
    this.renderPeers();
    this.peerGraceTimer = window.setTimeout(() => {
      this.peerGraceTimer = 0;
      this.peerGraceUntil = 0;
      if (this.socket?.readyState !== WebSocket.OPEN) {
        this.remotePlayers.clear();
        this.renderPeers();
      }
    }, SOCKET_PEER_GRACE_MS);
  }

  async connectPresence() {
    if (
      this.destroyed ||
      document.hidden ||
      this.presenceConnecting ||
      this.socket?.readyState === WebSocket.OPEN
    ) {
      return;
    }
    this.presenceConnecting = true;
    this.setupBroadcastChannel();
    const protocol = location.protocol === "https:" ? "wss:" : "ws:";
    if (
      readSession()?.sessionToken &&
      (!this.worldTicket || this.worldTicketExpires <= Date.now() + 5000)
    ) {
      await this.refreshWorldTicket();
    }
    if (this.destroyed || document.hidden) {
      this.presenceConnecting = false;
      return;
    }
    const socketURL = new URL(
      `${protocol}//${location.host}/api/world/ws`,
    );
    if (this.worldTicket) socketURL.searchParams.set("ticket", this.worldTicket);
    let socket;
    this.socketConnectionAttempts = incrementDiagnosticCounter(
      this.socketConnectionAttempts,
    );
    try {
      socket = new WebSocket(socketURL.href);
    } catch (_) {
      this.presenceConnecting = false;
      this.setPresenceState("offline", "Local world");
      this.schedulePresenceReconnect();
      return;
    }
    this.socket = socket;
    this.setPresenceState("connecting", "Joining world");
    socket.addEventListener("open", () => {
      if (this.destroyed || this.socket !== socket) {
        try {
          socket.close(1000, "world closed");
        } catch (_) {}
        return;
      }
      this.presenceConnecting = false;
      this.setPresenceState("online", "World online");
      window.clearTimeout(this.socketStableTimer);
      this.socketStableTimer = window.setTimeout(() => {
        if (this.socket === socket && socket.readyState === WebSocket.OPEN) {
          this.socketRetry = 1000;
        }
      }, SOCKET_STABLE_MS);
      window.clearTimeout(this.profilePresenceTimer);
      this.profilePresenceTimer = 0;
      this.profilePresencePending = false;
      this.sendPresenceNow({ type: "presence" });
      window.clearInterval(this.pingTimer);
      this.pingTimer = window.setInterval(
        () => this.sendPresence({ type: "ping" }),
        20000,
      );
    });
    socket.addEventListener("message", (event) => {
      this.socketInboundFrames = incrementDiagnosticCounter(
        this.socketInboundFrames,
      );
      let message = null;
      try {
        message = JSON.parse(event.data);
      } catch (_) {
        return;
      }
      this.receivePresence(message);
    });
    socket.addEventListener("close", () => {
      if (this.socket !== socket) return;
      this.presenceConnecting = false;
      this.socket = null;
      window.clearTimeout(this.socketStableTimer);
      this.socketStableTimer = 0;
      window.clearInterval(this.pingTimer);
      this.serverPeerId = "";
      window.clearTimeout(this.movementSendTimer);
      this.movementSendTimer = 0;
      this.pendingMovement = null;
      if (this.destroyed) return;
      this.setPresenceState("offline", "Local world");
      this.startPeerReconnectGrace();
      this.schedulePresenceReconnect();
    });
    socket.addEventListener("error", () => {
      try {
        socket.close();
      } catch (_) {}
    });
  }

  setPresenceState(state, copy) {
    const element = this.$("[data-world-presence-state]");
    const text = this.$("[data-world-presence-copy]");
    if (element) element.dataset.state = state;
    if (text) text.textContent = copy;
  }

  sendPresence(message) {
    if (message?.type !== "presence") {
      return this.sendPresenceNow(message);
    }
    if (this.profilePresencePending) {
      this.profileCoalescedFrames = incrementDiagnosticCounter(
        this.profileCoalescedFrames,
      );
    }
    this.profilePresencePending = true;
    window.clearTimeout(this.profilePresenceTimer);
    this.profilePresenceTimer = window.setTimeout(
      () => this.flushProfilePresence(),
      PRESENCE_PROFILE_DEBOUNCE_MS,
    );
    return false;
  }

  flushProfilePresence() {
    window.clearTimeout(this.profilePresenceTimer);
    this.profilePresenceTimer = 0;
    if (!this.profilePresencePending) return;
    if (this.sendPresenceNow({ type: "presence" })) {
      this.profilePresencePending = false;
    } else if (
      this.socket?.readyState === WebSocket.OPEN &&
      !this.destroyed
    ) {
      this.profilePresenceTimer = window.setTimeout(
        () => this.flushProfilePresence(),
        PRESENCE_PROFILE_DEBOUNCE_MS,
      );
    }
  }

  sendPresenceNow(message) {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) return false;
    if (
      ["presence", "move", "ping"].includes(String(message?.type || "")) &&
      Number(this.socket.bufferedAmount || 0) > SOCKET_BUFFER_HIGH_WATER_BYTES
    ) {
      this.socketBackpressureEvents = incrementDiagnosticCounter(
        this.socketBackpressureEvents,
      );
      return false;
    }
    let safe;
    if (message.type === "presence") {
      this.identity.firstVisitAge = firstVisitAge(this.firstVisitAt);
      this.identity.visitCount = this.publicVisitCount;
      this.identity.activityCategory = presenceActivity(
        this.settings,
        this.currentActivityCategory,
      );
      const publicStatus = normalizeWorldStatus(
        this.settings.statusEmoji,
        this.settings.statusNote,
      );
      safe = {
        type: "presence",
        name: sanitizePresenceText(this.identity.name, "visitor", 24),
        shareName: Boolean(this.settings.privacy.name),
        shareCountry: Boolean(this.settings.privacy.country),
        shareNodes: Boolean(this.settings.privacy.nodes),
        browser: presenceBrowser(this.identity.browser, this.settings.privacy.browser),
        os: presenceOS(this.identity.os, this.settings.privacy.os),
        status: presenceStatus(this.settings),
        localTime: presenceLocalTime(this.settings.privacy.localTime),
        activityCategory: presenceActivity(
          this.settings,
          this.currentActivityCategory,
        ),
        inputActive:
          Boolean(this.settings.privacy.activity) &&
          this.identity.inputActive === true,
        visitCount: this.settings.privacy.activity
          ? Math.max(0, Math.min(999, Number(this.publicVisitCount) || 0))
          : 0,
        firstVisitAge: this.settings.privacy.activity
          ? this.identity.firstVisitAge
          : "hidden",
        publicDoor: ["closed", "knock", "open"].includes(
          this.settings.publicDoor,
        )
          ? this.settings.publicDoor
          : "closed",
        space: WORLD_SPACE_IDS.has(this.currentSpace)
          ? this.currentSpace
          : "town-square",
        statusEmoji: publicStatus.emoji,
        statusNote: publicStatus.note,
      };
    } else if (message.type === "move") {
      safe = {
        type: "move",
        x: boundedPresenceNumber(message.x),
        y: boundedPresenceNumber(message.y),
        z: boundedPresenceNumber(message.z),
        yaw: boundedYaw(message.yaw ?? message.heading),
        moving: Boolean(message.moving),
      };
    } else if (message.type === "ping") {
      safe = { type: "ping" };
    } else {
      return false;
    }
    try {
      this.socket.send(JSON.stringify(safe));
      this.socketOutboundFrames = incrementDiagnosticCounter(
        this.socketOutboundFrames,
      );
      return true;
    } catch (_) {
      return false;
    }
  }

  receivePresence(message) {
    if (!message || typeof message !== "object") return;
    if (message.type === "welcome" && Array.isArray(message.peers)) {
      this.serverPeerId = String(message.id || "");
      const ownPresence = remotePlayer(message.self);
      // A restored spot may have been handed out as an arrival cell while
      // this browser was away. If another visitor is standing there, fall
      // back to the fresh open cell the server just assigned.
      const ownSpace = String(this.lastMovement?.space || this.currentSpace);
      const spawnBlocked =
        this.spawnSelected &&
        ownSpace === "town-square" &&
        message.peers.some((peer) => {
          const player = remotePlayer(peer);
          return (
            player &&
            player.id !== this.serverPeerId &&
            player.space === ownSpace &&
            Math.hypot(
              player.x - Number(this.lastMovement?.x || 0),
              player.z - Number(this.lastMovement?.z || 0),
            ) < ARRIVAL_CLEARANCE
          );
        });
      if (
        ownPresence?.id === this.serverPeerId &&
        (!this.spawnSelected || spawnBlocked)
      ) {
        this.currentSpace = ownPresence.space;
        this.lastMovement = {
          ...this.lastMovement,
          x: ownPresence.x,
          y: ownPresence.y,
          z: ownPresence.z,
          heading: ownPresence.heading,
          space: ownPresence.space,
        };
        this.world?.setSpawn?.({
          x: ownPresence.x,
          y: ownPresence.y,
          z: ownPresence.z,
          heading: ownPresence.heading,
          space: ownPresence.space,
        });
        this.spawnSelected = true;
        this.rememberWorldPosition(this.lastMovement, true);
      }
      window.clearTimeout(this.peerGraceTimer);
      this.peerGraceTimer = 0;
      this.peerGraceUntil = 0;
      this.remotePlayers.clear();
      message.peers.forEach((peer) => {
        const player = remotePlayer(peer);
        if (player?.id && player.id !== this.serverPeerId) {
          this.remotePlayers.set(player.id, player);
        }
      });
      // Publishing starts only after the server has assigned this connection's
      // unique row/column arrival slot.
      window.clearTimeout(this.movementSendTimer);
      this.movementSendTimer = 0;
      this.pendingMovement = null;
      this.sendPresenceNow({
        type: "move",
        ...this.lastMovement,
        moving: false,
      });
      this.lastMovementSentAt = performance.now();
    } else if (["presence", "join"].includes(message.type) && message.peer?.id) {
      const player = remotePlayer(message.peer);
      if (player?.id && player.id !== this.serverPeerId) {
        const isNewJoin =
          message.type === "join" && !this.remotePlayers.has(player.id);
        const current = this.remotePlayers.get(player.id) || {};
        this.remotePlayers.set(player.id, { ...current, ...player });
        if (isNewJoin) this.playCountryJoinSound(player.countryCode);
      }
    } else if (message.type === "move" && message.id) {
      const id = String(message.id);
      if (id !== this.serverPeerId) {
        const current = this.remotePlayers.get(id) || remotePlayer({ id });
        this.remotePlayers.set(id, {
          ...current,
          x: boundedPresenceNumber(message.x),
          z: boundedPresenceNumber(message.z),
          heading: boundedYaw(message.yaw),
        });
      }
    } else if (message.type === "leave") {
      const departed = String(message.id || "");
      this.remotePlayers.delete(departed);
      this.pendingKnocks.delete(departed);
    } else if (
      message.type === "interaction" &&
      message.kind === "knock" &&
      message.from
    ) {
      const visitorId = String(message.from);
      const visitor = this.remotePlayers.get(visitorId);
      this.pendingKnocks.set(visitorId, {
        id: visitorId,
        name: visitor?.name || "A visitor",
      });
      this.toast(
        `${visitor?.name || "A visitor"} knocked. Open Neighborhood to accept or decline; no permission changes happen automatically.`,
      );
      if (
        this.$("[data-world-detail]")?.dataset.open === "true" &&
        this.$("#world-detail-title")?.textContent?.includes("Neighborhood")
      ) {
        this.openLandmark("neighborhood");
      }
    } else if (
      message.type === "interaction" &&
      message.kind === "home-grant" &&
      message.from
    ) {
      this.enterNeighborhoodHome(String(message.from), true);
    } else if (
      message.type === "interaction" &&
      message.kind === "home-decline" &&
      message.from
    ) {
      const owner = this.remotePlayers.get(String(message.from));
      this.toast(
        `${owner?.name || "The owner"} declined the visual visit request. No reason or private status was shared.`,
      );
    } else if (
      message.type === "interaction" &&
      message.kind === "emote" &&
      ["wave", "idea", "celebrate"].includes(message.emote) &&
      message.from
    ) {
      this.world?.playEmote?.(String(message.from), message.emote);
    }
    this.renderPeers();
  }

  setupBroadcastChannel() {
    if (this.broadcast || !("BroadcastChannel" in window)) return;
    try {
      this.broadcast = new BroadcastChannel("forkmesh-world-presence-v1");
      this.broadcast.addEventListener("message", (event) => {
        const message = event.data;
        if (!message?.id || message.id === this.identity.id) return;
        if (message.type === "leave") {
          this.localPeers.delete(message.id);
        } else {
          this.localPeers.set(message.id, {
            ...message,
            seenAt: Date.now(),
          });
        }
        this.pruneLocalPeers();
      });
      this.broadcastLocalPresence();
      this.broadcastTimer = window.setInterval(() => {
        this.broadcastLocalPresence();
        this.pruneLocalPeers();
      }, 5000);
    } catch (_) {
      this.broadcast = null;
    }
  }

  broadcastLocalPresence() {
    if (!this.broadcast) return;
    try {
      this.broadcast.postMessage({
        type: "presence",
        ...publicIdentity(this.identity, this.settings),
        ...this.lastMovement,
        // The same-device fallback follows the exact same privacy choice as
        // network presence. In particular, the initial movement object must
        // never reintroduce an activity label after activity sharing is off.
        activity: this.settings.privacy.activity
          ? sanitizePresenceText(this.lastMovement.activity, "online", 64)
          : "online",
      });
    } catch (_) {}
  }

  pruneLocalPeers() {
    const cutoff = Date.now() - PRESENCE_STALE_MS;
    this.localPeers.forEach((peer, id) => {
      if (Number(peer.seenAt || 0) < cutoff) this.localPeers.delete(id);
    });
    this.renderPeers();
  }

  renderPeers() {
    const combined = new Map(
      this.inactivePlayers.map((player) => [player.id, player]),
    );
    this.remotePlayers.forEach((player, id) => combined.set(id, player));
    const socketOnline =
      this.socket?.readyState === WebSocket.OPEN && Boolean(this.serverPeerId);
    const reconnectGrace =
      !socketOnline &&
      Boolean(this.peerGraceTimer) &&
      this.peerGraceUntil > 0 &&
      this.remotePlayers.size > 0;
    if (!socketOnline && !reconnectGrace) {
      this.localPeers.forEach((peer, id) => {
        combined.set(`local:${id}`, {
          ...peer,
          id: `local:${id}`,
          activity: peer.status === "hidden" ? "online" : peer.status,
        });
      });
    }
    this.world?.setRemotePlayers([...combined.values()]);
    this.syncMemberLounge();
    this.updatePlayerCount();
    this.updateDurableObjectMetrics();
  }

  syncMemberLounge() {
    if (!this.world?.updateMemberLounge) return;
    // Seat every public registered account in the Member Lounge, except the
    // ones already rendered as live or opted-in idle avatars — those keep
    // their richer presence avatar instead of a duplicate directory figure.
    const present = new Set([
      String(this.identity?.name || "").trim().toLowerCase(),
    ]);
    this.remotePlayers.forEach((player) =>
      present.add(String(player?.name || "").trim().toLowerCase()),
    );
    this.inactivePlayers.forEach((player) =>
      present.add(String(player?.name || "").trim().toLowerCase()),
    );
    this.world.updateMemberLounge(
      this.memberDirectory.filter(
        (member) => !present.has(member.name.toLowerCase()),
      ),
      this.memberDirectory.length,
    );
  }

  destroy() {
    if (this.destroyed) return;
    if (this.spawnSelected) this.captureWorldPosition(true);
    this.destroyed = true;
    this.clearPullReviewScrollTracking();
    document.removeEventListener("visibilitychange", this.handleVisibility);
    window.visualViewport?.removeEventListener(
      "resize",
      this.syncViewportHeight,
    );
    window.removeEventListener("orientationchange", this.syncViewportHeight);
    window.removeEventListener("storage", this.handleStorage);
    window.removeEventListener("pointerdown", this.handlePublicInputActivity);
    window.removeEventListener("pointermove", this.handlePublicInputActivity);
    window.removeEventListener("keydown", this.handlePublicInputActivity);
    window.removeEventListener("message", this.handleWorldChatMessage);
    window.clearTimeout(this.socketTimer);
    window.clearTimeout(this.socketStableTimer);
    window.clearTimeout(this.peerGraceTimer);
    window.clearTimeout(this.profilePresenceTimer);
    window.clearTimeout(this.movementSendTimer);
    window.clearTimeout(this.positionWriteTimer);
    window.clearTimeout(this.toastTimer);
    window.clearTimeout(this.inactiveSyncTimer);
    window.clearTimeout(this.inputInactiveTimer);
    window.clearInterval(this.activityTimer);
    window.clearInterval(this.clockTimer);
    window.clearInterval(this.distanceTimer);
    window.clearInterval(this.pingTimer);
    window.clearInterval(this.rewardTimer);
    window.clearInterval(this.mirrorTimer);
    window.clearInterval(this.eventsTimer);
    window.clearInterval(this.notificationsTimer);
    window.clearInterval(this.mediaTimer);
    window.clearInterval(this.broadcastTimer);
    window.clearInterval(this.worldTicketTimer);
    window.clearInterval(this.diagnosticsTimer);
    this.peerGraceTimer = 0;
    this.profilePresenceTimer = 0;
    this.movementSendTimer = 0;
    this.positionWriteTimer = 0;
    try {
      this.broadcast?.postMessage({ type: "leave", id: this.identity?.id });
      this.broadcast?.close();
    } catch (_) {}
    try {
      this.socket?.close(1000, "page closed");
    } catch (_) {}
    this.stopRadio(false);
    this.soundEnabled = false;
    const soundContext = this.soundContext;
    this.soundContext = null;
    soundContext?.close?.().catch(() => {});
    this.world?.dispose();
    this.world = null;
    if (this.mode === "public") document.body.classList.remove("world-active");
  }
}

if (!customElements.get("forkmesh-world")) {
  customElements.define("forkmesh-world", ForkMeshWorld);
}
