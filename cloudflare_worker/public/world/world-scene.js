import {
  LANDMARKS,
  OUTFIT_COLOR_OPTIONS,
  WORLD_REGIONS,
  landmarkById,
  normalizeWorldStatus,
} from "./world-data.js";
import { nextOfficeZoneState } from "./world-office.js";
// Side-effect import: ForkMesh's own QR generator publishes globalThis.ForkMeshQR,
// used for the reward-pool treasury address board.
import "../qr.js";

const OUTFIT_COLOR_HEX = Object.fromEntries(
  OUTFIT_COLOR_OPTIONS.map((option) => [option.id, option.color]),
);

const WORLD_RADIUS = 72;
const WORLD_GROUND_RADIUS = 88;
// Base (from-rest) speed. Raised so keyboard movement leaves standstill with
// more pace by default; multiplied by the per-device move-speed control.
const PLAYER_SPEED = 6.4;
const PLAYER_MAX_SPEED = 13;
const PLAYER_ACCELERATION = 5.4;
// Double-clicking the ground sends the avatar to that spot at a dash speed far
// above the walking cap, so crossing the whole square takes a couple of seconds
// without teleporting the avatar out from under the camera.
const PLAYER_DASH_SPEED = 48;
const PLAYER_DASH_ARRIVE_DISTANCE = 0.3;
const CAMERA_OFFSET = [17, 16, 21];
const CAMERA_DISTANCE = Math.hypot(...CAMERA_OFFSET);
// Let players pull all the way back to a map-scale view where the World is a
// small speck. The enlarged far plane keeps that overview visible instead of
// clipping the ground and distant landmarks.
const CAMERA_ZOOM_MIN = 0.06;
const CAMERA_ZOOM_MAX = 28;
const CAMERA_FAR_PLANE = 1200;
const CAMERA_LOOK_SENSITIVITY = 0.0022;
const RENDER_STALL_THRESHOLD_MS = 150;
const RENDER_STALL_LOG_COOLDOWN_MS = 1000;
// Dragging upward lowers the orbit eye beneath the target, which is how this
// camera looks into the sky. Allow the full arc in both directions.
const CAMERA_PITCH_MIN = -Math.PI / 2 + 0.01;
// Stop just shy of vertical so the camera stays numerically stable while a
// player can still look directly into the sky.
const CAMERA_PITCH_MAX = Math.PI / 2 - 0.01;
const FIRST_PERSON_EYE_HEIGHT = 2.2;
const FIRST_PERSON_PITCH_MIN = -Math.PI / 2 + 0.01;
const FIRST_PERSON_PITCH_MAX = Math.PI / 2 - 0.01;
const REPOSITORY_FIRST_PERSON_DISTANCE = 5.5;
const REPOSITORY_FIRST_PERSON_PITCH = -0.08;
const OFFICE_WIDTH = 17;
const OFFICE_DEPTH = 12;
const OFFICE_HEIGHT = 7;
const OFFICE_FRONT_Z = 6;
const OFFICE_DOOR_WIDTH = 2.4;
const OFFICE_AVATAR_RADIUS = 0.46;
const OFFICE_INTERIOR_WALL_LIMIT = 5.46;
const OFFICE_INTERIOR_EXIT_Z = 6.18;
const LIGHT_LEVEL_MIN = 40;
const LIGHT_LEVEL_MAX = 140;
const LIGHT_LEVEL_DEFAULT = 100;
// ForkBot's world presence: a wandering guide anchored to the Town Square.
// Chat bubbles addressed to this peer id float over its avatar, mirroring how
// visitor bubbles work (world.js handleWorldChatMessage).
const FORKBOT_PEER_ID = "forkbot";
const FORKBOT_HOME = Object.freeze([6, 0.38, 12]);
const FORKBOT_WANDER_RADIUS = 14;
const FORKBOT_SPEED = 3.4;
const FORKBOT_GREETING_RANGE = 3.2;
// If a visitor is out of reach (travelled to another space, moderation walls,
// …) the greeting still fires from wherever ForkBot got to.
const FORKBOT_GREETING_TIMEOUT_MS = 12000;
// The public World has one shared ground plane plus three regional labels.
// Deprecated off-world destinations are deliberately not valid spawn spaces.
const WORLD_SPACE_FLOORS = Object.freeze({
  "town-square": 0.38,
  east: 0.38,
  central: 0.38,
  west: 0.38,
});
const MOVEMENT_KEYS = new Set([
  "KeyW",
  "KeyA",
  "KeyS",
  "KeyD",
  "ArrowUp",
  "ArrowDown",
  "ArrowLeft",
  "ArrowRight",
]);
const ACTIVE_LEADERBOARD_POSITION = Object.freeze([-11.5, 0, 25]);
// Beside the active leaderboard, just west of the arrival grid.
const REFERRAL_LEADERBOARD_POSITION = Object.freeze([-13.5, 0, 30]);
const SYSTEM_CAPACITY_PLATFORM_POSITION = Object.freeze([8, 0, -27]);
const SERVER_CABINET_YARD_ORIGIN = Object.freeze([18, 0, 0]);
const ARRIVAL_GRID_BOUNDS = Object.freeze({
  minX: -10.8,
  maxX: 10.8,
  minZ: 14,
  maxZ: 32.4,
});
const TREES_PER_LANDMARK = 3;
const TREE_MIN_SPACING = 3.2;
const TREE_PORTAL_CLEARANCE = 7;
const TREE_LANDMARK_CLEARANCE = Object.freeze({
  fountain: 9,
  repositories: 9,
  organizations: 10,
  fediverse: 9,
  events: 9,
  neighborhood: 9,
  broadcast: 9,
  office: 10,
});
// Shared by the seated pose and the presence frame so other visitors can render
// a bench sitter sitting rather than standing on the plank.
const CAMPFIRE_SEATED_ACTIVITY = "sitting beside the campfire";
const REGISTERED_LOUNGE_STATUSES = new Set([
  "Registered",
  "Supporting member",
  "Mirror operator",
  "Organization admin",
]);
const LOCAL_ENVIRONMENT_OVERLAYS = Object.freeze({
  rain: {
    tint: "#5f7785",
    tintStrength: 0.2,
    lightMultiplier: 0.76,
    exposureMultiplier: 0.88,
    fogMultiplier: 1.22,
  },
  snow: {
    tint: "#dcece9",
    tintStrength: 0.22,
    lightMultiplier: 1.04,
    exposureMultiplier: 1.02,
    fogMultiplier: 1.16,
  },
  winter: {
    tint: "#b9ccca",
    tintStrength: 0.27,
    lightMultiplier: 0.94,
    exposureMultiplier: 0.96,
    fogMultiplier: 1.18,
  },
  cyberpunk: {
    tint: "#35104c",
    tintStrength: 0.34,
    lightMultiplier: 0.9,
    exposureMultiplier: 0.9,
    fogMultiplier: 1.12,
  },
  "low-light": {
    tint: "#07110f",
    tintStrength: 0.18,
    lightMultiplier: 0.64,
    exposureMultiplier: 0.7,
    fogMultiplier: 1.05,
  },
});
const DAYLIGHT_ENVIRONMENT = Object.freeze({
  // Keep the far overview visually continuous with the ground. The former
  // pale sky read as a hazy cover when the whole world was zoomed out.
  background: "#174434",
  fog: "#9bc6b5",
  hemiSky: "#d8fff1",
  hemiGround: "#25493a",
  sun: "#fff0bd",
  sunPower: 3.7,
  hemiPower: 2.1,
  exposure: 1.12,
  fogDensity: 0.0068,
});
const ACCOUNT_STATUS_ICONS = Object.freeze({
  Guest: "○",
  Registered: "✓",
  "Supporting member": "♥",
  "Mirror operator": "◈",
  "Organization admin": "◆",
  "Verified bot": "⌘",
});
const WORLD_MODERATION_HANDLE_PATTERN = /^[a-f0-9]{64}$/;
const REPOSITORY_SIZE_MAP_COLORS = Object.freeze([
  "#3987e5",
  "#199e70",
  "#c98500",
  "#008300",
  "#9085e9",
  "#e66767",
  "#d55181",
  "#d95926",
]);
const REPOSITORY_SIZE_MAP_GRAY = "#8a8a8a";
const REPOSITORY_SIZE_MAP_MAX_RINGS = 4;
const REPOSITORY_SIZE_MAP_MAX_SEGMENTS = 420;
const REPOSITORY_CATALOG_MAX = 200;
const REPOSITORY_EDGE_RADIUS = 68;

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function hashNumber(value) {
  let hash = 2166136261;
  for (const char of String(value || "")) {
    hash ^= char.charCodeAt(0);
    hash = Math.imul(hash, 16777619);
  }
  return Math.abs(hash >>> 0);
}

function deterministicFraction(value) {
  return (hashNumber(value) % 1_000_003) / 1_000_003;
}

function pointInsideBounds(x, z, bounds) {
  return (
    x >= bounds.minX &&
    x <= bounds.maxX &&
    z >= bounds.minZ &&
    z <= bounds.maxZ
  );
}

function isArrivalGridPosition(x, z) {
  return pointInsideBounds(Number(x) || 0, Number(z) || 0, ARRIVAL_GRID_BOUNDS);
}

function arrivalFacingHeading(x, z, heading) {
  return isArrivalGridPosition(x, z) ? Math.PI : heading;
}

function deterministicTreeLayout() {
  const positions = [];
  const cabinetBounds = {
    minX: SERVER_CABINET_YARD_ORIGIN[0] + 6,
    maxX: SERVER_CABINET_YARD_ORIGIN[0] + 34,
    minZ: SERVER_CABINET_YARD_ORIGIN[2] - 14,
    maxZ: SERVER_CABINET_YARD_ORIGIN[2] + 14,
  };
  const durableBounds = {
    minX: SYSTEM_CAPACITY_PLATFORM_POSITION[0] - 10,
    maxX: SYSTEM_CAPACITY_PLATFORM_POSITION[0] + 10,
    minZ: SYSTEM_CAPACITY_PLATFORM_POSITION[2] - 8,
    maxZ: SYSTEM_CAPACITY_PLATFORM_POSITION[2] + 8,
  };
  LANDMARKS.forEach((landmark) => {
    for (let treeIndex = 0; treeIndex < TREES_PER_LANDMARK; treeIndex += 1) {
      const angle =
        deterministicFraction(`tree-angle:${landmark.id}:${treeIndex}`) * Math.PI * 2;
      const radius =
        6.4 + deterministicFraction(`tree-radius:${landmark.id}:${treeIndex}`) * 2.1;
      const x = landmark.position[0] + Math.cos(angle) * radius;
      const z = landmark.position[2] + Math.sin(angle) * radius;
    if (
      pointInsideBounds(x, z, ARRIVAL_GRID_BOUNDS) ||
      pointInsideBounds(x, z, cabinetBounds) ||
      pointInsideBounds(x, z, durableBounds)
    ) {
        continue;
    }
      if (Math.hypot(x - 8, z - 8) < 5.5) continue;
    if (
      positions.some(
        (tree) => Math.hypot(x - tree.x, z - tree.z) < TREE_MIN_SPACING,
      )
    ) {
        continue;
    }
      positions.push({
        x,
        z,
        scale:
          0.7 + deterministicFraction(`tree-scale:${landmark.id}:${treeIndex}`) * 0.38,
        colorIndex: hashNumber(`tree-color:${landmark.id}:${treeIndex}`) % 4,
      });
    }
  });
  return positions;
}

function canvasTexture(THREE, width, height, draw) {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  draw(context, canvas);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.minFilter = THREE.LinearFilter;
  texture.magFilter = THREE.LinearFilter;
  return texture;
}

function roundedRect(context, x, y, width, height, radius) {
  const r = Math.min(radius, width / 2, height / 2);
  context.beginPath();
  context.moveTo(x + r, y);
  context.arcTo(x + width, y, x + width, y + height, r);
  context.arcTo(x + width, y + height, x, y + height, r);
  context.arcTo(x, y + height, x, y, r);
  context.arcTo(x, y, x + width, y, r);
  context.closePath();
}

// Directory figures carry the account's public joined timestamp and coarse
// total active time; live presence identities do not, so the badge falls
// back to the first-seen and activity·visits lines for them.
function joinedAgoLabel(joinedAt, now = Date.now()) {
  const timestamp = Number(joinedAt);
  if (!Number.isFinite(timestamp) || timestamp <= 0) return "";
  const elapsed = Math.max(0, now - timestamp);
  const units = [
    [365 * 24 * 60 * 60 * 1000, "YEAR"],
    [30 * 24 * 60 * 60 * 1000, "MONTH"],
    [7 * 24 * 60 * 60 * 1000, "WEEK"],
    [24 * 60 * 60 * 1000, "DAY"],
    [60 * 60 * 1000, "HOUR"],
    [60 * 1000, "MINUTE"],
  ];
  for (const [size, unit] of units) {
    const count = Math.floor(elapsed / size);
    if (count) return `JOINED ${count} ${unit}${count === 1 ? "" : "S"} AGO`;
  }
  return "JOINED JUST NOW";
}

function badgeActiveDurationLabel(value) {
  const minutes = Math.floor(Math.max(0, Number(value) || 0) / 60000);
  const days = Math.floor(minutes / 1440);
  const hours = Math.floor((minutes % 1440) / 60);
  if (days) return `${days}D ${hours}H`;
  if (hours) return `${hours}H ${minutes % 60}M`;
  return `${minutes % 60}M`;
}

function badgeTexture(THREE, identity, accent = "#9ef7c6") {
  const activityLabels = {
    "browsing-code-visualization": "CODE MAP",
    "exploring-town-square": "TOWN SQUARE",
    "reading-documentation": "DOCUMENTATION",
    "viewing-repository": "REPOSITORY",
    "visiting-organization": "ORGANIZATION",
    hidden: "ACTIVITY HIDDEN",
  };
  const firstSeenLabels = {
    "this-session": "FIRST SEEN THIS SESSION",
    today: "FIRST SEEN TODAY",
    "this-week": "FIRST SEEN THIS WEEK",
    "this-month": "FIRST SEEN THIS MONTH",
    "this-year": "FIRST SEEN THIS YEAR",
    "over-a-year": "FIRST SEEN 1Y+ AGO",
    hidden: "FIRST SEEN HIDDEN",
  };
  const statusColors = {
    online: "#9ef7c6",
    available: "#9ef7c6",
    away: "#f7c96b",
    inactive: "#91a39a",
    recent: "#91a39a",
    returning: "#77d9ff",
    hidden: "#65776f",
  };
  return canvasTexture(THREE, 512, 512, (context) => {
    context.fillStyle = "#0c2019";
    context.fillRect(0, 0, 512, 512);
    context.strokeStyle = accent;
    context.lineWidth = 12;
    context.strokeRect(8, 8, 496, 496);

    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '128px system-ui, "Apple Color Emoji", "Segoe UI Emoji"';
    context.fillStyle = "#ffffff";
    context.fillText(identity.flag || "◌", 256, 104);

    context.font = '700 44px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#ffffff";
    context.fillText(String(identity.name || "guest").slice(0, 15), 256, 216);

    context.font = '700 21px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#9ef7c6";
    context.fillText(
      joinedAgoLabel(identity.joinedAt) ||
        firstSeenLabels[identity.firstVisitAge] ||
        firstSeenLabels.hidden,
      256,
      292,
    );
    const activity =
      activityLabels[identity.activityCategory] || activityLabels.hidden;
    const visits = Math.max(0, Math.min(999, Number(identity.visitCount) || 0));
    context.fillStyle = "#77d9ff";
    context.fillText(
      identity.totalActiveMs != null &&
        Number.isFinite(Number(identity.totalActiveMs))
        ? `ACTIVE ${badgeActiveDurationLabel(identity.totalActiveMs)} IN WORLD`
        : `${activity} · ${visits} PUBLIC URL VISITS`,
      256,
      334,
    );

    context.beginPath();
    context.arc(60, 452, 12, 0, Math.PI * 2);
    context.fillStyle = statusColors[identity.status] || "#9ef7c6";
    context.fill();
    context.textAlign = "left";
    context.font = '700 20px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#b9cfc4";
    const account = String(identity.accountStatus || "Guest")
      .replace(/\s+/g, " ")
      .slice(0, 18);
    context.fillText(
      [
        `${ACCOUNT_STATUS_ICONS[account] || "○"} ${account}`,
        identity.localTime,
      ].filter(Boolean).join(" · "),
      86,
      453,
    );
  });
}

function countryShirtTexture(THREE, identity) {
  const code = /^[A-Z]{2}$/.test(String(identity.countryCode || ""))
    ? String(identity.countryCode)
    : "";
  const flag = code && identity.flag && identity.flag !== "◌"
    ? identity.flag
    : "◌";
  return canvasTexture(THREE, 256, 256, (context) => {
    const seed = hashNumber(code || "neutral");
    const palettes = [
      ["#174f3d", "#9ef7c6"],
      ["#183f62", "#77d9ff"],
      ["#633e23", "#f7c96b"],
      ["#573965", "#d5b6ff"],
      ["#6a3346", "#ff9eb7"],
    ];
    const [base, stripe] = palettes[seed % palettes.length];
    context.fillStyle = code ? "#f7fbf8" : base;
    context.fillRect(0, 0, 256, 256);
    context.fillStyle = stripe;
    context.fillRect(0, 0, 256, 34);
    context.fillRect(0, 222, 256, 34);
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '150px system-ui, "Apple Color Emoji", "Segoe UI Emoji"';
    context.fillStyle = "#ffffff";
    context.fillText(flag, 128, 128);
  });
}

function wordTexture(THREE, title, subtitle, color = "#9ef7c6") {
  return canvasTexture(THREE, 768, 256, (context) => {
    context.clearRect(0, 0, 768, 256);
    roundedRect(context, 4, 4, 760, 248, 12);
    context.fillStyle = "rgba(6,17,14,0.92)";
    context.fill();
    context.strokeStyle = color;
    context.lineWidth = 4;
    context.stroke();
    context.textAlign = "left";
    context.textBaseline = "middle";
    context.fillStyle = "#f1fff6";
    context.font = '700 54px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText(String(title || "").slice(0, 22), 42, 99);
    context.fillStyle = color;
    context.font = '400 25px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(String(subtitle || "").toUpperCase().slice(0, 36), 42, 165);
  });
}

function activeDurationLabel(value) {
  const milliseconds = Number(value);
  if (!Number.isFinite(milliseconds) || milliseconds < 0) {
    return "ACTIVE TIME NOT REPORTED";
  }
  let seconds = Math.floor(milliseconds / 1000);
  const days = Math.floor(seconds / 86400);
  seconds %= 86400;
  const hours = Math.floor(seconds / 3600);
  seconds %= 3600;
  const minutes = Math.floor(seconds / 60);
  seconds %= 60;
  return `${days}d ${hours}h ${minutes}m ${seconds}s`;
}

function rankedActiveLeaderboardMembers(members = []) {
  return (Array.isArray(members) ? members : [])
    .filter(
      (member) =>
        String(member?.name || "").trim() &&
        (member?.activeNow === true ||
          Number(member?.totalActiveMs ?? member?.activeMs ?? 0) > 0),
    )
    .sort((left, right) => {
      const durationDifference =
        Number(right?.totalActiveMs ?? right?.activeMs ?? 0) -
        Number(left?.totalActiveMs ?? left?.activeMs ?? 0);
      if (durationDifference) return durationDifference;
      return String(left?.name || "").localeCompare(
        String(right?.name || ""),
        undefined,
        { sensitivity: "base" },
      );
    })
    .slice(0, 6);
}

function activeLeaderboardTexture(THREE, members = []) {
  const rows = rankedActiveLeaderboardMembers(members);
  return canvasTexture(THREE, 768, 512, (context) => {
    context.clearRect(0, 0, 768, 512);
    roundedRect(context, 4, 4, 760, 504, 16);
    context.fillStyle = "rgba(6,17,14,0.95)";
    context.fill();
    context.strokeStyle = "#77d9ff";
    context.lineWidth = 5;
    context.stroke();
    context.textBaseline = "middle";
    context.fillStyle = "#f1fff6";
    context.font = '700 48px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText("ACTIVE LEADERBOARD", 38, 58);
    context.fillStyle = "#77d9ff";
    context.font = '400 20px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("REGISTERED USER · TOTAL ACTIVE TIME", 38, 102);
    rows.forEach((member, index) => {
      const top = 148 + index * 56;
      const name = String(member.name).trim().slice(0, 24);
      const duration = activeDurationLabel(
        member.totalActiveMs ?? member.activeMs,
      );
      context.fillStyle = "#d9ffea";
      context.font = '700 25px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(`${index + 1}. ${name}`, 38, top);
      context.fillStyle = "#9ef7c6";
      context.font = '400 19px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        `${member.activeNow === true ? "ACTIVE NOW · " : ""}${duration}`.slice(
          0,
          34,
        ),
        38,
        top + 25,
      );
    });
    if (!rows.length) {
      context.fillStyle = "#91a39a";
      context.font = '400 24px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO REGISTERED ACTIVITY REPORTED", 38, 176);
    }
  });
}

function rankedReferralRows(rows = []) {
  return (Array.isArray(rows) ? rows : [])
    .filter(
      (row) =>
        String(row?.name || "").trim() &&
        (Number(row?.clicks) > 0 || Number(row?.signups) > 0),
    )
    .sort((left, right) => {
      const signupDifference =
        (Number(right?.signups) || 0) - (Number(left?.signups) || 0);
      if (signupDifference) return signupDifference;
      const clickDifference =
        (Number(right?.clicks) || 0) - (Number(left?.clicks) || 0);
      if (clickDifference) return clickDifference;
      return String(left?.name || "").localeCompare(
        String(right?.name || ""),
        undefined,
        { sensitivity: "base" },
      );
    })
    .slice(0, 5);
}

function referralLeaderboardTexture(THREE, rows = [], viewerLink = "") {
  const ranked = rankedReferralRows(rows);
  return canvasTexture(THREE, 768, 512, (context) => {
    context.clearRect(0, 0, 768, 512);
    roundedRect(context, 4, 4, 760, 504, 16);
    context.fillStyle = "rgba(6,17,14,0.95)";
    context.fill();
    context.strokeStyle = "#9ef7c6";
    context.lineWidth = 5;
    context.stroke();
    context.textBaseline = "middle";
    context.fillStyle = "#f1fff6";
    context.font = '700 48px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText("REFERRAL LEADERBOARD", 38, 58);
    context.fillStyle = "#9ef7c6";
    context.font = '400 20px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("SHARE YOUR /r/ LINK · CLICKS AND SIGNUPS", 38, 102);
    ranked.forEach((row, index) => {
      const top = 148 + index * 52;
      const name = String(row.name).trim().slice(0, 22);
      context.fillStyle = "#d9ffea";
      context.font = '700 25px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(`${index + 1}. ${name}`, 38, top);
      context.fillStyle = "#77d9ff";
      context.font = '400 20px "ForkMesh Mono", ui-monospace, monospace';
      const clicks = Number(row.clicks) || 0;
      const signups = Number(row.signups) || 0;
      const value = `${signups} SIGNUPS · ${clicks} CLICKS`;
      context.fillText(value, 768 - 38 - context.measureText(value).width, top);
    });
    if (!ranked.length) {
      context.fillStyle = "#91a39a";
      context.font = '400 24px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO REFERRALS COUNTED YET", 38, 200);
    }
    context.strokeStyle = "rgba(158,247,198,0.4)";
    context.lineWidth = 2;
    context.beginPath();
    context.moveTo(38, 418);
    context.lineTo(730, 418);
    context.stroke();
    if (viewerLink) {
      context.fillStyle = "#9ef7c6";
      context.font = '400 19px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("YOUR LINK · TAP THE BOARD TO COPY", 38, 446);
      context.fillStyle = "#d9ffea";
      context.font = '700 23px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(String(viewerLink).slice(0, 52), 38, 480);
    } else {
      context.fillStyle = "#91a39a";
      context.font = '400 21px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("SIGN IN TO GET YOUR OWN REFERRAL LINK", 38, 462);
    }
  });
}

function makeReferralLeaderboardSign(THREE) {
  const sign = new THREE.Group();
  sign.name = "world-referral-leaderboard";
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(4.8, 0.25, 1.35),
    makeMaterial(THREE, "#1a3032", { roughness: 0.8 }),
  );
  base.position.y = 0.13;
  sign.add(base);
  for (const x of [-1.72, 1.72]) {
    const post = new THREE.Mesh(
      new THREE.BoxGeometry(0.16, 3.7, 0.16),
      makeMaterial(THREE, "#234d4a", { metalness: 0.22, roughness: 0.52 }),
    );
    post.position.set(x, 1.85, 0);
    sign.add(post);
  }
  const board = new THREE.Mesh(
    new THREE.BoxGeometry(3.72, 2.72, 0.12),
    makeMaterial(THREE, "#071712", { roughness: 0.55 }),
  );
  board.position.set(0, 2.08, 0.08);
  sign.add(board);
  const face = new THREE.Mesh(
    new THREE.PlaneGeometry(3.52, 2.55),
    new THREE.MeshBasicMaterial({
      map: referralLeaderboardTexture(THREE),
      transparent: true,
    }),
  );
  face.position.set(0, 2.08, 0.151);
  sign.add(face);
  sign.userData.face = face;
  return sign;
}

const OFFICE_MARKETING_TASK_LIMIT = 6;

function boundedOfficeMarketingTaskText(value, maxLength, fallback = "") {
  const normalized = String(value ?? "")
    .replace(/[\u0000-\u001f\u007f]+/g, " ")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, maxLength);
  return normalized || fallback;
}

function normalizeOfficeMarketingTasks(payload = {}) {
  const source =
    payload && typeof payload === "object" && !Array.isArray(payload)
      ? payload
      : {};
  const authorized = source.authorized === true;
  const requestedState = boundedOfficeMarketingTaskText(
    source.state,
    16,
    authorized ? "" : "locked",
  ).toLowerCase();
  const tasks = authorized
    ? (Array.isArray(source.tasks) ? source.tasks : [])
        .filter((task) => task && typeof task === "object" && !Array.isArray(task))
        .slice(0, OFFICE_MARKETING_TASK_LIMIT)
        .map((task) => ({
          title: boundedOfficeMarketingTaskText(
            task.title,
            42,
            "Untitled marketing task",
          ),
          assignee: boundedOfficeMarketingTaskText(
            task.assignee,
            24,
            "Unassigned",
          ),
          status: boundedOfficeMarketingTaskText(
            task.status,
            16,
            "Open",
          ),
          elapsed: boundedOfficeMarketingTaskText(
            task.elapsed,
            18,
            "Not started",
          ),
        }))
    : [];
  const state =
    !authorized || requestedState === "locked"
      ? "locked"
      : requestedState === "loading"
        ? "loading"
        : tasks.length
          ? "ready"
          : "empty";
  return { authorized, state, tasks };
}

function officeMarketingTasksTexture(THREE, payload = {}) {
  const snapshot = normalizeOfficeMarketingTasks(payload);
  return canvasTexture(THREE, 1024, 768, (context) => {
    context.clearRect(0, 0, 1024, 768);
    roundedRect(context, 5, 5, 1014, 758, 18);
    context.fillStyle = "rgba(5,17,14,0.97)";
    context.fill();
    context.strokeStyle = "#9ef7c6";
    context.lineWidth = 10;
    context.stroke();

    context.textAlign = "left";
    context.textBaseline = "middle";
    context.fillStyle = "#f1fff6";
    context.font = '700 54px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText("MARKETING TASKS", 52, 64);
    context.fillStyle = "#9ef7c6";
    context.font = '700 22px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("AUTHORIZED OFFICE VIEW · SELECT BOARD FOR DETAILS", 52, 112);
    context.strokeStyle = "rgba(158,247,198,0.28)";
    context.lineWidth = 2;
    context.beginPath();
    context.moveTo(52, 142);
    context.lineTo(972, 142);
    context.stroke();

    if (snapshot.state !== "ready") {
      const stateCopy = {
        locked: ["LOCKED", "Sign in with task access to view this board."],
        loading: ["LOADING", "Retrieving authorized marketing tasks…"],
        empty: ["NO OPEN TASKS", "The authorized marketing queue is empty."],
      }[snapshot.state] || ["UNAVAILABLE", "Task summaries are unavailable."];
      context.fillStyle =
        snapshot.state === "locked" ? "#f7c96b" : "#9ef7c6";
      context.font = '700 46px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(stateCopy[0], 52, 260);
      context.fillStyle = "#b9cfc4";
      context.font = '400 28px "ForkMesh Favorit", system-ui, sans-serif';
      context.fillText(stateCopy[1], 52, 316, 900);
      return;
    }

    snapshot.tasks.forEach((task, index) => {
      const top = 180 + index * 91;
      context.fillStyle = index % 2
        ? "rgba(255,255,255,0.025)"
        : "rgba(158,247,198,0.045)";
      roundedRect(context, 42, top - 30, 940, 80, 10);
      context.fill();
      context.fillStyle = "#d9ffea";
      context.font = '700 27px "ForkMesh Favorit", system-ui, sans-serif';
      context.fillText(task.title, 62, top - 4, 600);
      context.fillStyle = "#91a39a";
      context.font = '400 19px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        `${task.assignee} · ${task.elapsed}`,
        62,
        top + 27,
        600,
      );
      context.textAlign = "right";
      context.fillStyle = "#9ef7c6";
      context.font = '700 20px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(task.status.toUpperCase(), 954, top + 10, 260);
      context.textAlign = "left";
    });
  });
}

function chatBubbleTexture(THREE, name, text) {
  return canvasTexture(THREE, 768, 256, (context) => {
    context.clearRect(0, 0, 768, 256);
    roundedRect(context, 4, 4, 760, 216, 26);
    context.fillStyle = "rgba(6,17,14,0.94)";
    context.fill();
    context.strokeStyle = "#9ef7c6";
    context.lineWidth = 4;
    context.stroke();
    // Speech-bubble tail pointing down toward the speaker's head.
    context.beginPath();
    context.moveTo(354, 214);
    context.lineTo(384, 250);
    context.lineTo(414, 214);
    context.closePath();
    context.fillStyle = "rgba(6,17,14,0.94)";
    context.fill();
    context.textAlign = "left";
    context.textBaseline = "middle";
    context.fillStyle = "#9ef7c6";
    context.font = '700 26px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(String(name || "visitor").toUpperCase().slice(0, 24), 42, 50);
    context.fillStyle = "#f1fff6";
    context.font = '600 38px "ForkMesh Favorit", system-ui, sans-serif';
    const words = String(text || "").split(/\s+/).filter(Boolean);
    const lines = [""];
    for (const word of words) {
      const current = lines[lines.length - 1];
      const candidate = current ? `${current} ${word}` : word;
      if (!current || context.measureText(candidate).width <= 680) {
        lines[lines.length - 1] = candidate;
      } else if (lines.length < 3) {
        lines.push(word);
      } else {
        lines[2] += "…";
        break;
      }
    }
    lines.forEach((line, index) => {
      context.fillText(line, 42, 102 + index * 50, 680);
    });
  });
}

function moderationControlTexture(THREE, title, subtitle, color) {
  return canvasTexture(THREE, 512, 160, (context) => {
    context.clearRect(0, 0, 512, 160);
    roundedRect(context, 4, 4, 504, 152, 18);
    context.fillStyle = "rgba(35,8,12,0.96)";
    context.fill();
    context.strokeStyle = color;
    context.lineWidth = 8;
    context.stroke();
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillStyle = "#fff5f6";
    context.font = '800 38px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(title, 256, 58);
    context.fillStyle = color;
    context.font = '700 22px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(subtitle, 256, 113);
  });
}

function sanitizedModerationHandles(remote, isAdmin) {
  const peerId = String(remote?.id || "");
  if (
    isAdmin !== true ||
    !peerId ||
    /^(?:inactive|local|node|bot):/.test(peerId) ||
    remote?.persistedInactive === true ||
    remote?.accountStatus === "Verified bot" ||
    !remote?.moderationHandles ||
    typeof remote.moderationHandles !== "object" ||
    Array.isArray(remote.moderationHandles)
  ) {
    return {};
  }
  const handles = {};
  for (const targetType of ["ip", "agent"]) {
    const handle = remote.moderationHandles[targetType];
    if (
      typeof handle === "string" &&
      WORLD_MODERATION_HANDLE_PATTERN.test(handle)
    ) {
      handles[targetType] = handle;
    }
  }
  return handles;
}

function createAvatarModerationControls(THREE, handles) {
  const group = new THREE.Group();
  group.name = "forkmesh-world-moderation-controls";
  const specs = [
    {
      targetType: "ip",
      title: "TEMP BLOCK IP",
      subtitle: "ROTATING IP TOKEN · 1 HOUR",
      color: "#ff9ca4",
      y: 2.43,
    },
    {
      targetType: "agent",
      title: "TEMP BLOCK AGENT",
      subtitle: "BROWSER AGENT · 1 HOUR",
      color: "#ffc279",
      y: 2.02,
    },
  ];
  specs.forEach((spec) => {
    if (!handles[spec.targetType]) return;
    const control = new THREE.Mesh(
      new THREE.PlaneGeometry(1.02, 0.34),
      new THREE.MeshBasicMaterial({
        map: moderationControlTexture(
          THREE,
          spec.title,
          spec.subtitle,
          spec.color,
        ),
        transparent: false,
        depthWrite: true,
      }),
    );
    control.name = `world-moderation-${spec.targetType}-control`;
    // Avatar fronts face -Z; positive Z places this single-sided control on
    // the back, so it cannot be selected through the avatar from the front.
    control.position.set(0, spec.y, 0.321);
    control.userData.worldModerationControl = spec.targetType;
    control.renderOrder = 3;
    group.add(control);
  });
  return group;
}

function makeMaterial(THREE, color, options = {}) {
  const parameters = {
    color,
    roughness: options.roughness ?? 0.68,
    metalness: options.metalness ?? 0.08,
    emissive: options.emissive || 0x000000,
    emissiveIntensity: options.emissiveIntensity ?? 0,
    transparent: Boolean(options.transparent),
    opacity: options.opacity ?? 1,
  };
  // Three.js warns for explicitly supplied `undefined` enum values. Omit the
  // option entirely unless a caller intentionally selected a rendering side.
  if (options.side !== undefined) parameters.side = options.side;
  return new THREE.MeshStandardMaterial(parameters);
}

function setShadows(object, cast = true, receive = true) {
  object.traverse((child) => {
    if (!child.isMesh) return;
    child.castShadow = cast;
    child.receiveShadow = receive;
  });
}

function disposeObject3D(object) {
  const geometries = new Set();
  const materials = new Set();
  const textures = new Set();
  object?.traverse?.((child) => {
    if (child.geometry) geometries.add(child.geometry);
    const childMaterials = Array.isArray(child.material)
      ? child.material
      : [child.material];
    childMaterials.filter(Boolean).forEach((material) => {
      materials.add(material);
      if (material.map) textures.add(material.map);
    });
  });
  textures.forEach((texture) => texture.dispose?.());
  materials.forEach((material) => material.dispose?.());
  geometries.forEach((geometry) => geometry.dispose?.());
}

function removeInteractiveObject(interactive, object) {
  if (!object) return;
  object.traverse?.((child) => {
    let index = interactive.indexOf(child);
    while (index >= 0) {
      interactive.splice(index, 1);
      index = interactive.indexOf(child);
    }
  });
}

function removeGeneratedLayer(parent, layer, interactive) {
  if (!parent || !layer) return;
  removeInteractiveObject(interactive, layer);
  parent.remove(layer);
  disposeObject3D(layer);
}

function objectIsEffectivelyVisible(object) {
  let current = object;
  while (current) {
    if (current.visible === false) return false;
    current = current.parent;
  }
  return true;
}

function makeLabelSprite(THREE, title, subtitle, color) {
  const material = new THREE.SpriteMaterial({
    map: wordTexture(THREE, title, subtitle, color),
    transparent: true,
    depthTest: false,
    depthWrite: false,
  });
  const sprite = new THREE.Sprite(material);
  sprite.scale.set(7.5, 2.5, 1);
  sprite.renderOrder = 10;
  return sprite;
}

// Stable, server-safe layout id built from the only durable name a scene prop
// has: its placard title, or a node's name.
function worldLayoutId(prefix, name) {
  const slug = String(name || "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 60);
  return slug ? prefix + slug : "";
}

function plaqueLayoutId(title) {
  return worldLayoutId("plaque-", title);
}

function makeGroundPlaque(THREE, title, subtitle, color) {
  const plaque = new THREE.Group();
  plaque.name = "forkmesh-section-plaque";
  plaque.userData.plaqueLayoutId = plaqueLayoutId(title);
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(3.9, 0.22, 1.5),
    makeMaterial(THREE, "#233b33", { roughness: 0.82 }),
  );
  base.position.y = 0.11;
  plaque.add(base);
  const slab = new THREE.Mesh(
    new THREE.BoxGeometry(3.6, 1.3, 0.14),
    makeMaterial(THREE, "#101d18", { roughness: 0.55, metalness: 0.12 }),
  );
  slab.position.set(0, 0.74, 0.12);
  // Lean the top back so the face reads from the raised world camera.
  slab.rotation.x = -0.42;
  plaque.add(slab);
  const face = new THREE.Mesh(
    new THREE.PlaneGeometry(3.44, 1.14),
    new THREE.MeshBasicMaterial({
      map: wordTexture(THREE, title, subtitle, color),
      transparent: true,
    }),
  );
  face.position.z = 0.08;
  slab.add(face);
  return plaque;
}

function formatArrivalCount(value) {
  return Math.max(0, Math.min(99999999, Math.round(Number(value) || 0)))
    .toLocaleString("en-US");
}

function arrivalTrendText(current, previous) {
  const now = Math.max(0, Math.round(Number(current) || 0));
  const then = Math.max(0, Math.round(Number(previous) || 0));
  const yesterday = `yesterday ${formatArrivalCount(then)}`.toUpperCase();
  if (then <= 0) {
    return now > 0 ? `▲ UP · ${yesterday}` : `= EVEN · ${yesterday}`;
  }
  const delta = Math.round(((now - then) / then) * 100);
  if (delta > 0) return `▲ +${delta}% · ${yesterday}`;
  if (delta < 0) return `▼ ${delta}% · ${yesterday}`;
  return `= EVEN · ${yesterday}`;
}

function arrivalPlaqueTexture(THREE, stats) {
  const ready = Boolean(stats);
  const lines = [
    {
      label: "UNIQUE VISITORS",
      value: ready ? formatArrivalCount(stats.total) : "—",
      note: ready
        ? "ALL TIME · APPROXIMATE · NO RAW IP STORED"
        : "COUNTING…",
    },
    {
      label: "TODAY",
      value: ready ? formatArrivalCount(stats.today) : "—",
      note: ready
        ? arrivalTrendText(stats.today, stats.yesterdaySameTime)
        : "COUNTING…",
    },
    {
      label: "PAST HOUR",
      value: ready ? formatArrivalCount(stats.pastHour) : "—",
      note: ready
        ? arrivalTrendText(stats.pastHour, stats.pastHourYesterday)
        : "COUNTING…",
    },
  ];
  return canvasTexture(THREE, 768, 512, (context) => {
    context.clearRect(0, 0, 768, 512);
    roundedRect(context, 6, 6, 756, 500, 18);
    context.fillStyle = "rgba(6,17,14,0.94)";
    context.fill();
    context.strokeStyle = "#9ef7c6";
    context.lineWidth = 6;
    context.stroke();
    context.textAlign = "left";
    context.textBaseline = "middle";
    context.fillStyle = "#f1fff6";
    context.font = '700 54px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText("ARRIVAL GRID", 46, 74);
    context.fillStyle = "#9ef7c6";
    context.font = '400 23px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      "10 visitors per row · face the square".toUpperCase(),
      46,
      126,
    );
    context.fillStyle = "rgba(158,247,198,0.35)";
    context.fillRect(46, 158, 676, 3);
    lines.forEach((line, index) => {
      const top = 204 + index * 106;
      context.textAlign = "left";
      context.fillStyle = "#9ef7c6";
      context.font = '700 26px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(line.label, 46, top);
      context.fillStyle = "#77d9ff";
      context.font = '400 20px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(line.note, 46, top + 40);
      context.textAlign = "right";
      context.fillStyle = "#f1fff6";
      context.font = '700 46px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(line.value, 722, top + 14);
    });
  });
}

// The Arrival Grid's sign is a standing plaque at the grid's front edge:
// fresh arrivals spawn facing the square, so the visit counters are the
// first readable landmark, and the raised world camera sees the same face.
function makeArrivalPlaque(THREE) {
  const plaque = new THREE.Group();
  plaque.name = "world-arrival-plaque";
  plaque.userData.plaqueLayoutId = plaqueLayoutId("arrival");
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(4.1, 0.24, 1.6),
    makeMaterial(THREE, "#233b33", { roughness: 0.82 }),
  );
  base.position.y = 0.12;
  plaque.add(base);
  const slab = new THREE.Mesh(
    new THREE.BoxGeometry(3.7, 2.5, 0.16),
    makeMaterial(THREE, "#101d18", { roughness: 0.55, metalness: 0.12 }),
  );
  slab.position.set(0, 1.5, 0.1);
  slab.rotation.x = -0.16;
  plaque.add(slab);
  const face = new THREE.Mesh(
    new THREE.PlaneGeometry(3.54, 2.36),
    new THREE.MeshBasicMaterial({
      map: arrivalPlaqueTexture(THREE, null),
      transparent: true,
    }),
  );
  face.position.z = 0.09;
  slab.add(face);
  plaque.userData.statsFace = face;
  return plaque;
}

function makeActiveLeaderboardSign(THREE) {
  const sign = new THREE.Group();
  sign.name = "world-active-leaderboard";
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(4.8, 0.25, 1.35),
    makeMaterial(THREE, "#1a3032", { roughness: 0.8 }),
  );
  base.position.y = 0.13;
  sign.add(base);
  for (const x of [-1.72, 1.72]) {
    const post = new THREE.Mesh(
      new THREE.BoxGeometry(0.16, 3.7, 0.16),
      makeMaterial(THREE, "#234d4a", { metalness: 0.22, roughness: 0.52 }),
    );
    post.position.set(x, 1.85, 0);
    sign.add(post);
  }
  const board = new THREE.Mesh(
    new THREE.BoxGeometry(3.72, 2.72, 0.12),
    makeMaterial(THREE, "#071712", { roughness: 0.55 }),
  );
  board.position.set(0, 2.08, 0.08);
  sign.add(board);
  const face = new THREE.Mesh(
    new THREE.PlaneGeometry(3.52, 2.55),
    new THREE.MeshBasicMaterial({
      map: activeLeaderboardTexture(THREE),
      transparent: true,
    }),
  );
  face.position.set(0, 2.08, 0.151);
  sign.add(face);
  sign.userData.face = face;
  return sign;
}

// Places a plaque on the ground in front of the section — on the side facing
// the Town Square center, where visitors walk up. `position` is the section's
// world position; sections at the center face the arrival grid instead.
function placeSectionPlaque(group, plaque, position, distance) {
  const x = Array.isArray(position) ? position[0] : 0;
  const z = Array.isArray(position) ? position[2] : 0;
  const length = Math.hypot(x, z);
  const ux = length > 0.001 ? -x / length : 0;
  const uz = length > 0.001 ? -z / length : 1;
  plaque.position.set(ux * distance, 0, uz * distance);
  plaque.rotation.y = Math.atan2(ux, uz);
  group.add(plaque);
  return plaque;
}

function addSectionPlaque(THREE, group, position, title, subtitle, color, distance) {
  return placeSectionPlaque(
    group,
    makeGroundPlaque(THREE, title, subtitle, color),
    position,
    distance,
  );
}

// Worn on the head when a visitor has never stored a world-status emoji.
const AVATAR_DEFAULT_FACE_EMOJI = "🙂";
// Flat yellow of the standard emoji faces. The whole head uses it so the
// wrapped emoji decal and the sphere behind it read as one continuous head.
const AVATAR_EMOJI_SKIN_COLOR = "#ffcc4d";

function avatarFaceTexture(THREE, emoji) {
  return canvasTexture(THREE, 128, 128, (context) => {
    context.clearRect(0, 0, 128, 128);
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '118px system-ui, "Apple Color Emoji", "Segoe UI Emoji"';
    context.fillStyle = "#1d130c";
    context.fillText(emoji, 64, 68);
  });
}

function syncAvatarFace(THREE, avatar) {
  const face = avatar.userData.faceMesh;
  if (!face?.material) return;
  const worn = avatar.userData.statusEmoji || AVATAR_DEFAULT_FACE_EMOJI;
  if (avatar.userData.faceEmojiShown === worn) return;
  face.material.map?.dispose?.();
  face.material.map = avatarFaceTexture(THREE, worn);
  face.material.needsUpdate = true;
  avatar.userData.faceEmojiShown = worn;
}

function syncAvatarStatus(THREE, avatar, identity) {
  if (!avatar?.userData) return;
  const status = normalizeWorldStatus(
    identity?.statusEmoji,
    identity?.statusNote,
  );
  const key = `${status.emoji}\u0000${status.note}`;
  if (avatar.userData.emojiStatusKey === key) return;
  const previous = avatar.userData.emojiStatusSprite;
  if (previous) {
    avatar.remove(previous);
    previous.material?.map?.dispose?.();
    previous.material?.dispose?.();
  }
  avatar.userData.emojiStatusKey = key;
  avatar.userData.statusEmoji = status.emoji;
  avatar.userData.statusNote = status.note;
  avatar.userData.emojiStatusSprite = null;
  // Status remains available to the accessible player label and presence
  // payload, but the large duplicate overhead banner is intentionally not
  // rendered in-world. The last-used emoji is worn on the face instead.
  syncAvatarFace(THREE, avatar);
}

function makeConsentedProfileFace(THREE, follower) {
  const face = new THREE.Mesh(
    new THREE.SphereGeometry(0.24, 18, 14),
    makeMaterial(THREE, "#ff9eb7", {
      emissive: "#7f3150",
      emissiveIntensity: 0.35,
      roughness: 0.7,
    }),
  );
  const rawAvatar = String(follower?.avatar || "").trim();
  try {
    const avatar = new URL(rawAvatar);
    if (
      avatar.protocol === "https:" &&
      !avatar.username &&
      !avatar.password &&
      rawAvatar.length <= 800
    ) {
      const loader = new THREE.TextureLoader();
      loader.setCrossOrigin("anonymous");
      loader.load(
        avatar.href,
        (texture) => {
          texture.colorSpace = THREE.SRGBColorSpace;
          face.material.map = texture;
          face.material.color.set("#ffffff");
          face.material.needsUpdate = true;
        },
        undefined,
        () => {},
      );
    }
  } catch (_) {}
  face.userData.publicProfileHandle = String(
    follower?.handle || "public",
  ).slice(0, 80);
  return face;
}

function syncOperatorBelt(THREE, avatar, nodeCount) {
  const previous = avatar.getObjectByName("forkmesh-operator-belt");
  if (previous) {
    avatar.remove(previous);
    previous.traverse((child) => {
      child.geometry?.dispose?.();
      child.material?.dispose?.();
    });
  }
  const count = Math.max(0, Math.min(6, Number(nodeCount) || 0));
  if (!count) {
    if (avatar.userData) avatar.userData.nodeCount = 0;
    return;
  }
  const beltGroup = new THREE.Group();
  beltGroup.name = "forkmesh-operator-belt";
  const belt = new THREE.Mesh(
    new THREE.BoxGeometry(1.12, 0.18, 0.66),
    makeMaterial(THREE, "#d6a44d", {
      metalness: 0.42,
      roughness: 0.4,
    }),
  );
  belt.position.y = 1.43;
  beltGroup.add(belt);
  for (let index = 0; index < count; index += 1) {
    const light = new THREE.Mesh(
      new THREE.SphereGeometry(0.055, 10, 8),
      makeMaterial(THREE, "#9ef7c6", {
        emissive: "#9ef7c6",
        emissiveIntensity: 1.2,
      }),
    );
    light.position.set(-0.4 + index * 0.16, 1.43, -0.35);
    beltGroup.add(light);
  }
  avatar.add(beltGroup);
  if (avatar.userData) avatar.userData.nodeCount = count;
}

function createAvatar(THREE, identity, options = {}) {
  const seed = hashNumber(identity.id || identity.name);
  const group = new THREE.Group();
  group.name = `avatar:${identity.id || identity.name}`;
  const scale = options.scale || 1;
  const remote = Boolean(options.remote);

  const skin = makeMaterial(THREE, AVATAR_EMOJI_SKIN_COLOR, { roughness: 0.92 });
  const shirt = makeMaterial(THREE, "#ffffff", {
    roughness: 0.8,
  });
  applyOutfit(THREE, shirt, identity);
  shirt.needsUpdate = true;
  const dark = makeMaterial(THREE, "#101d19", { roughness: 0.85 });
  const shoe = makeMaterial(THREE, "#07100e", { roughness: 0.82 });

  const torso = new THREE.Mesh(new THREE.BoxGeometry(1.1, 1.5, 0.62), shirt);
  torso.position.y = 2.15;
  group.add(torso);

  const head = new THREE.Mesh(new THREE.SphereGeometry(0.45, 18, 14), skin);
  head.scale.y = 1.05;
  head.position.y = 3.36;
  group.add(head);

  const hair = new THREE.Mesh(
    new THREE.SphereGeometry(0.47, 16, 10, 0, Math.PI * 2, 0, Math.PI * 0.52),
    dark,
  );
  hair.position.set(0, 3.46, 0.07);
  // Tilted back so the cap clears the forehead and the wrapped emoji face
  // has the whole front of the head to itself.
  hair.rotation.x = 0.42;
  group.add(hair);

  // The face wears the last world-status emoji the visitor set (default
  // smile). Avatar fronts face -Z; the emoji is mapped onto a thin curved
  // shell hugging the head sphere so it wraps the whole face, and
  // syncAvatarFace keeps its texture current.
  const faceMesh = new THREE.Mesh(
    new THREE.SphereGeometry(
      0.462,
      18,
      14,
      Math.PI * 0.08,
      Math.PI * 0.84,
      Math.PI * 0.16,
      Math.PI * 0.68,
    ),
    new THREE.MeshBasicMaterial({ transparent: true }),
  );
  faceMesh.scale.y = 1.05;
  faceMesh.position.y = 3.36;
  faceMesh.rotation.y = Math.PI;
  group.add(faceMesh);

  const limbGeometry = new THREE.BoxGeometry(0.29, 1.25, 0.32);
  const leftArm = new THREE.Mesh(limbGeometry, shirt);
  leftArm.position.set(-0.73, 2.08, 0);
  group.add(leftArm);
  const rightArm = leftArm.clone();
  rightArm.position.x = 0.73;
  group.add(rightArm);

  const bracelet = new THREE.Mesh(
    new THREE.TorusGeometry(0.19, 0.045, 8, 24),
    makeMaterial(THREE, "#9ef7c6", {
      emissive: "#39c783",
      emissiveIntensity: 1.4,
      metalness: 0.42,
      roughness: 0.32,
    }),
  );
  bracelet.name = "mouse-activity-bracelet";
  bracelet.rotation.x = Math.PI / 2;
  bracelet.position.set(0.73, 1.52, 0);
  bracelet.visible = identity.inputActive === true;
  group.add(bracelet);

  const legGeometry = new THREE.BoxGeometry(0.42, 1.25, 0.45);
  const leftLeg = new THREE.Mesh(legGeometry, dark);
  leftLeg.position.set(-0.3, 0.78, 0);
  group.add(leftLeg);
  const rightLeg = leftLeg.clone();
  rightLeg.position.x = 0.3;
  group.add(rightLeg);

  const leftShoe = new THREE.Mesh(new THREE.BoxGeometry(0.46, 0.26, 0.7), shoe);
  leftShoe.position.set(-0.3, 0.15, -0.09);
  group.add(leftShoe);
  const rightShoe = leftShoe.clone();
  rightShoe.position.x = 0.3;
  group.add(rightShoe);

  const badge = new THREE.Mesh(
    new THREE.PlaneGeometry(0.76, 0.76),
    new THREE.MeshBasicMaterial({
      map: badgeTexture(THREE, identity, remote ? "#77d9ff" : "#9ef7c6"),
      transparent: false,
    }),
  );
  badge.scale.set(1, 1.14, 1);
  badge.position.set(0, 2.25, -0.316);
  badge.rotation.y = Math.PI;
  group.add(badge);

  group.scale.setScalar(scale);
  group.userData = {
    id: identity.id,
    name: identity.name,
    leftArm,
    rightArm,
    leftLeg,
    rightLeg,
    badge,
    shirt,
    shirtMeshes: [torso, leftArm, rightArm],
    bracelet,
    phase: (seed % 100) / 10,
    targetPosition: new THREE.Vector3(),
    targetHeading: 0,
    status: identity.status || "exploring",
    nodeCount: 0,
    accountStatus: identity.accountStatus || "Guest",
    inputActive: identity.inputActive === true,
    inactiveSince:
      identity.inputActive === true ? 0 : performance.now(),
    avatarOpacity: 1,
    statusEmoji: "",
    statusNote: "",
    emojiStatusKey: "",
    emojiStatusSprite: null,
    faceMesh,
    faceEmojiShown: "",
  };
  syncOperatorBelt(THREE, group, identity.nodes?.length || 0);
  syncAvatarStatus(THREE, group, identity);
  setShadows(group, true, true);
  return group;
}

function applyOutfit(THREE, shirt, identity) {
  const outfitColor = OUTFIT_COLOR_HEX[identity.outfitColor];
  const previous = shirt.map;
  if (outfitColor) {
    shirt.map = null;
    shirt.color.set(outfitColor);
  } else {
    shirt.map = countryShirtTexture(THREE, identity);
    shirt.color.set("#ffffff");
  }
  shirt.needsUpdate = true;
  if (previous !== shirt.map) previous?.dispose?.();
}

function syncCountryShirt(THREE, avatar, identity) {
  const shirt = avatar?.userData?.shirt;
  if (!shirt) return;
  applyOutfit(THREE, shirt, identity);
}

function syncAvatarActivity(avatar, identity) {
  if (!avatar?.userData) return;
  const active = identity.inputActive === true;
  if (active && !avatar.userData.inputActive) {
    avatar.userData.inactiveSince = 0;
  } else if (!active && avatar.userData.inputActive) {
    avatar.userData.inactiveSince = performance.now();
  }
  avatar.userData.inputActive = active;
  avatar.userData.accountStatus = identity.accountStatus || "Guest";
  if (avatar.userData.bracelet) avatar.userData.bracelet.visible = active;
}

function updateAvatarBadge(THREE, avatar, identity, remote = false) {
  const badge = avatar?.userData?.badge;
  if (!badge?.material) return;
  const old = badge.material.map;
  badge.material.map = badgeTexture(THREE, identity, remote ? "#77d9ff" : "#9ef7c6");
  badge.material.needsUpdate = true;
  old?.dispose?.();
  avatar.userData.name = identity.name;
  syncCountryShirt(THREE, avatar, identity);
  syncAvatarActivity(avatar, identity);
  syncAvatarStatus(THREE, avatar, identity);
}

function animateAvatarActivity(avatar, time, delta, reducedMotion) {
  if (!avatar?.userData) return;
  const bracelet = avatar.userData.bracelet;
  if (bracelet?.visible) {
    bracelet.rotation.z += reducedMotion ? 0 : delta * 3.2;
    bracelet.material.emissiveIntensity = reducedMotion
      ? 1.15
      : 1.1 + (Math.sin(time * 0.008 + avatar.userData.phase) + 1) * 0.55;
  }
  const inactiveFor = avatar.userData.inactiveSince
    ? Math.max(0, time - avatar.userData.inactiveSince)
    : 0;
  const shouldFade =
    avatar.userData.accountStatus === "Guest" &&
    avatar.userData.inputActive !== true &&
    inactiveFor >= 12000;
  const targetOpacity = shouldFade ? 0.36 : 1;
  const blend = 1 - Math.pow(0.006, Math.max(0, delta));
  avatar.userData.avatarOpacity +=
    (targetOpacity - avatar.userData.avatarOpacity) * blend;
  const opacity = avatar.userData.avatarOpacity;
  const materials = new Set();
  avatar.traverse((child) => {
    if (
      !child.isMesh ||
      child === bracelet ||
      child.userData?.worldModerationControl
    ) return;
    const childMaterials = Array.isArray(child.material)
      ? child.material
      : [child.material];
    childMaterials.filter(Boolean).forEach((material) => materials.add(material));
  });
  materials.forEach((material) => {
    if (material.userData.avatarBaseOpacity === undefined) {
      material.userData.avatarBaseOpacity = Number(material.opacity) || 1;
      material.userData.avatarBaseTransparent = Boolean(material.transparent);
    }
    material.opacity = material.userData.avatarBaseOpacity * opacity;
    material.transparent =
      material.userData.avatarBaseTransparent || opacity < 0.995;
    material.needsUpdate = true;
  });
}

function mirrorNodeIsOnline(node) {
  const health = String(node?.health || node?.status || "").toLowerCase();
  return (
    node?.online === true ||
    node?.healthy === true ||
    ["online", "healthy", "available"].includes(health)
  );
}

function mirrorMetric(value, maximum = Number.MAX_SAFE_INTEGER) {
  const number = Number(value);
  return Number.isFinite(number) && number >= 0 && number <= maximum
    ? number
    : null;
}

function compactMirrorCount(value) {
  const number = mirrorMetric(value, 1_000_000_000);
  if (number === null) return "—";
  if (number >= 1_000_000) return `${(number / 1_000_000).toFixed(1)}M`;
  if (number >= 1_000) return `${(number / 1_000).toFixed(1)}K`;
  return String(Math.round(number));
}

function compactMirrorBytes(value) {
  const bytes = mirrorMetric(value, 2 ** 50);
  if (bytes === null) return "—";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let amount = bytes;
  let unit = 0;
  while (amount >= 1024 && unit < units.length - 1) {
    amount /= 1024;
    unit += 1;
  }
  return `${amount >= 10 || unit === 0 ? amount.toFixed(0) : amount.toFixed(1)} ${units[unit]}`;
}

function mirrorRatio(used, total) {
  const safeUsed = mirrorMetric(used, 2 ** 50);
  const safeTotal = mirrorMetric(total, 2 ** 50);
  if (safeUsed === null || safeTotal === null || safeTotal <= 0 || safeUsed > safeTotal) {
    return null;
  }
  return safeUsed / safeTotal;
}

function nodeDataKey(node) {
  return JSON.stringify({
    name: node?.name,
    online: mirrorNodeIsOnline(node),
    healthy: node?.healthy,
    integrity: node?.integrity,
    cloneAvailable: node?.cloneAvailable,
    commit: node?.commit,
    branch: node?.branch,
    lastCommitMessage: node?.lastCommitMessage,
    lastCommitAuthorName: node?.lastCommitAuthorName,
    lastCommitAt: node?.lastCommitAt,
    version: node?.version,
    platform: node?.platform,
    lastSync: node?.lastSync,
    updatedAt: node?.updatedAt,
    sizeBytes: node?.sizeBytes,
    issueCount: node?.issueCount,
    commitCount: node?.commitCount,
    branchCount: node?.branchCount,
    pullCount: node?.pullCount,
    discussionCount: node?.discussionCount,
    worktreeCount: node?.worktreeCount,
    artifactCount: node?.artifactCount,
    clonesServed: node?.clonesServed,
    websiteServed: node?.websiteServed,
    cpuPercent: node?.cpuPercent,
    memoryUsedBytes: node?.memoryUsedBytes,
    memoryTotalBytes: node?.memoryTotalBytes,
    diskUsedBytes: node?.diskUsedBytes,
    diskTotalBytes: node?.diskTotalBytes,
    repositories: node?.repositories,
  });
}

function mirrorCommitAgeLabel(ageMs) {
  const age = Number(ageMs);
  if (!Number.isFinite(age) || age < 0) return "AGE NOT REPORTED";
  const minute = 60 * 1000;
  const hour = 60 * minute;
  const day = 24 * hour;
  const week = 7 * day;
  const month = 30 * day;
  const year = 365 * day;
  if (age < minute) return "just now";
  if (age < hour) return `${Math.floor(age / minute)}m ago`;
  if (age < day) return `${Math.floor(age / hour)}h ago`;
  if (age < week) return `${Math.floor(age / day)}d ago`;
  if (age < month) return `${Math.floor(age / week)}w ago`;
  if (age < year) return `${Math.floor(age / month)}mo ago`;
  return `${Math.floor(age / year)}y ago`;
}

function mirrorCommitSnapshot(node, repo) {
  const nested = node?.lastCommit || node?.commitDetails || repo?.lastCommit || {};
  const message = String(
    node?.lastCommitMessage ||
      node?.commitMessage ||
      node?.commitSubject ||
      repo?.lastCommitMessage ||
      repo?.commitMessage ||
      nested?.message ||
      nested?.subject ||
      "COMMIT SUBJECT NOT REPORTED",
  )
    .replace(/[\u0000-\u001f\u007f]/g, " ")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 80);
  const author = String(
    node?.lastCommitAuthorName ||
      node?.commitAuthorName ||
      repo?.lastCommitAuthorName ||
      repo?.commitAuthorName ||
      nested?.authorName ||
      nested?.author?.name ||
      node?.commitAuthor ||
      "AUTHOR NOT REPORTED",
  )
    .replace(/[\u0000-\u001f\u007f]/g, " ")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 42);
  const rawTimestamp = Number(
    node?.lastCommitAt ||
      node?.commitDate ||
      repo?.lastCommitAt ||
      repo?.commitDate ||
      nested?.committedAt ||
      nested?.date ||
      nested?.author?.date,
  );
  const commitAt = Number.isFinite(rawTimestamp)
    ? rawTimestamp < 100_000_000_000
      ? rawTimestamp * 1000
      : rawTimestamp
    : 0;
  const syncAge = mirrorMetric(node?.syncAgeMs ?? repo?.syncAgeMs, 2 ** 50);
  const ageMs = commitAt > 0
    ? Math.max(0, Date.now() - commitAt)
    : syncAge;
  const initials = author
    .split(/\s+/)
    .filter(Boolean)
    .slice(0, 2)
    .map((part) => part[0])
    .join("")
    .toUpperCase()
    .slice(0, 2) || "?";
  let colorHash = 0;
  for (const character of author) {
    colorHash = (colorHash * 31 + character.charCodeAt(0)) >>> 0;
  }
  const hue = colorHash % 360;
  return {
    message,
    author,
    initials,
    age: mirrorCommitAgeLabel(ageMs),
    avatarColor: `hsl(${hue} 42% 34%)`,
  };
}

function serverPanelTexture(THREE, node) {
  const online = mirrorNodeIsOnline(node);
  const integrity = String(node?.integrity || "unknown").toLowerCase();
  const repo = Array.isArray(node?.repositories) ? node.repositories[0] : null;
  const repositoryLabel =
    repo?.owner && repo?.name
      ? `${String(repo.owner).slice(0, 32)}/${String(repo.name).slice(0, 44)}`
      : "REPOSITORY NOT REPORTED";
  const commitSnapshot = mirrorCommitSnapshot(node, repo);
  const commit = String(node?.commit || repo?.commit || "").toLowerCase();
  const shortCommit = /^[0-9a-f]{40,64}$/.test(commit)
    ? commit.slice(0, 12)
    : "NOT REPORTED";
  const cpu = mirrorMetric(node?.cpuPercent, 100);
  const memory = mirrorRatio(node?.memoryUsedBytes, node?.memoryTotalBytes);
  const disk = mirrorRatio(node?.diskUsedBytes, node?.diskTotalBytes);
  const statusColor =
    online && integrity === "ok"
      ? "#73f0ad"
      : integrity === "rejected" || integrity === "degraded"
        ? "#ff7e88"
        : online
          ? "#f7c96b"
          : "#91a39a";
  const routeLabel = !online
    ? "ROUTE OFFLINE"
    : node?.cloneAvailable === true && integrity === "ok"
      ? "CLONE READY"
      : integrity === "rejected" || integrity === "degraded"
        ? "ROUTE BLOCKED"
        : integrity === "healing"
          ? "ROUTE VERIFYING"
          : "ROUTE UNVERIFIED";
  // 512² keeps the worst-case 64-cabinet texture budget bounded on mobile.
  // Draw in a 1024-unit coordinate system so typography stays easy to tune.
  return canvasTexture(THREE, 512, 512, (context) => {
    context.scale(0.5, 0.5);
    context.fillStyle = "#07110f";
    context.fillRect(0, 0, 1024, 1024);
    context.strokeStyle = "#526c61";
    context.lineWidth = 12;
    roundedRect(context, 10, 10, 1004, 1004, 28);
    context.stroke();

    context.textAlign = "left";
    context.textBaseline = "middle";
    context.font = '800 52px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#f1fff6";
    context.fillText(String(node?.name || "MIRROR").toUpperCase().slice(0, 24), 58, 70);
    context.font = '700 25px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = statusColor;
    context.fillText(
      `${online ? "ONLINE" : "OFFLINE"} · ${integrity.toUpperCase()} · ${routeLabel}`,
      58,
      124,
    );

    context.strokeStyle = "#294339";
    context.lineWidth = 3;
    context.beginPath();
    context.moveTo(58, 158);
    context.lineTo(966, 158);
    context.stroke();

    const drawBar = (label, ratio, value, y, color) => {
      context.font = '700 25px "ForkMesh Mono", ui-monospace, monospace';
      context.fillStyle = "#cce9d8";
      context.fillText(label, 58, y);
      roundedRect(context, 170, y - 19, 520, 38, 9);
      context.fillStyle = "#15271f";
      context.fill();
      if (ratio !== null) {
        roundedRect(context, 170, y - 19, Math.max(12, 520 * ratio), 38, 9);
        context.fillStyle = color;
        context.fill();
      } else {
        context.strokeStyle = "#53665e";
        context.lineWidth = 3;
        for (let x = 178; x < 682; x += 24) {
          context.beginPath();
          context.moveTo(x, y + 16);
          context.lineTo(x + 18, y - 16);
          context.stroke();
        }
      }
      context.textAlign = "right";
      context.fillStyle = ratio === null ? "#91a39a" : "#f1fff6";
      context.fillText(ratio === null ? "NOT SHARED" : value, 966, y);
      context.textAlign = "left";
    };
    drawBar(
      "CPU",
      cpu === null ? null : cpu / 100,
      cpu === null ? "" : `${cpu.toFixed(1)}%`,
      216,
      "#77d9ff",
    );
    drawBar(
      "MEM",
      memory,
      memory === null
        ? ""
        : `${compactMirrorBytes(node?.memoryUsedBytes)} / ${compactMirrorBytes(
            node?.memoryTotalBytes,
          )}`,
      278,
      "#d5b6ff",
    );
    drawBar(
      "DISK",
      disk,
      disk === null
        ? ""
        : `${compactMirrorBytes(node?.diskUsedBytes)} / ${compactMirrorBytes(
            node?.diskTotalBytes,
          )}`,
      340,
      "#f7c96b",
    );

    context.fillStyle = "#10251d";
    roundedRect(context, 42, 386, 940, 170, 14);
    context.fill();
    context.font = '700 25px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#9ef7c6";
    context.fillText(repositoryLabel, 62, 426);
    context.fillStyle = "#f1fff6";
    context.font = '700 30px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(`HEAD ${shortCommit}`, 62, 475);
    context.font = '600 23px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#b4cabd";
    context.fillText(
      `${String(node?.branch || repo?.branch || "branch not reported").slice(
        0,
        30,
      )} · ${String(node?.platform || "platform —").slice(0, 18)} · v${
        String(node?.version || "—").replace(/^v/i, "").slice(0, 20)
      }`,
      62,
      522,
    );

    context.strokeStyle = "#294339";
    context.lineWidth = 3;
    context.beginPath();
    context.moveTo(520, 404);
    context.lineTo(520, 540);
    context.stroke();
    context.font = '700 20px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#8ca99a";
    context.fillText("LAST COMMIT", 548, 414);
    context.font = '700 23px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#f1fff6";
    context.fillText(commitSnapshot.message.slice(0, 31), 548, 454);
    context.fillText(commitSnapshot.message.slice(31, 62), 548, 486);
    context.beginPath();
    context.arc(568, 521, 18, 0, Math.PI * 2);
    context.fillStyle = commitSnapshot.avatarColor;
    context.fill();
    context.font = '800 17px "ForkMesh Mono", ui-monospace, monospace';
    context.textAlign = "center";
    context.fillStyle = "#f1fff6";
    context.fillText(commitSnapshot.initials, 568, 522);
    context.textAlign = "left";
    context.font = '700 20px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#9ef7c6";
    context.fillText(commitSnapshot.author.slice(0, 19), 598, 516);
    context.font = '600 18px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#b4cabd";
    context.fillText(commitSnapshot.age, 598, 540);

    const rows = [
      ["COMMITS", node?.commitCount],
      ["BRANCHES", node?.branchCount],
      ["PULL REQUESTS", node?.pullCount],
      ["ISSUES", node?.issueCount],
      ["DISCUSSIONS", node?.discussionCount],
      ["ARTIFACTS", node?.artifactCount],
      ["WORKTREES", node?.worktreeCount],
      ["CLONES SERVED", node?.clonesServed],
      ["WEB SERVED", node?.websiteServed],
      ["REPO BYTES", compactMirrorBytes(node?.sizeBytes)],
    ];
    context.font = '700 21px "ForkMesh Mono", ui-monospace, monospace';
    rows.forEach(([label, value], index) => {
      const column = index % 2;
      const row = Math.floor(index / 2);
      const x = column === 0 ? 58 : 536;
      const y = 604 + row * 62;
      context.fillStyle = "#8ca99a";
      context.fillText(label, x, y);
      context.textAlign = "right";
      context.fillStyle = "#f1fff6";
      context.fillText(
        typeof value === "string" ? value : compactMirrorCount(value),
        x + 414,
        y,
      );
      context.textAlign = "left";
    });

    context.fillStyle = "#91a39a";
    context.font = '600 19px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      "OPERATOR-REPORTED, SIGNED REPO DATA · UNAVAILABLE VALUES ARE NEVER ESTIMATED",
      58,
      958,
    );
  });
}

function createMirrorServerCabinet(THREE, node, id) {
  const group = new THREE.Group();
  group.name = `mirror-server-cabinet:${id}`;
  group.userData.infrastructureKind = "mirror-node";
  group.userData.nodeId = id;
  const online = mirrorNodeIsOnline(node);
  const body = new THREE.Mesh(
    new THREE.BoxGeometry(2.24, 3.28, 1.42),
    makeMaterial(THREE, online ? "#17221e" : "#252b28", {
      emissive: online ? "#0b2d20" : "#171b19",
      emissiveIntensity: online ? 0.24 : 0.1,
      metalness: 0.72,
      roughness: 0.28,
    }),
  );
  body.position.y = 1.64;
  group.add(body);

  const trimMaterial = makeMaterial(THREE, "#485950", {
    emissive: "#15271f",
    emissiveIntensity: 0.18,
    metalness: 0.88,
    roughness: 0.22,
  });
  for (const x of [-1.04, 1.04]) {
    const rail = new THREE.Mesh(
      new THREE.BoxGeometry(0.075, 3.14, 1.5),
      trimMaterial,
    );
    rail.position.set(x, 1.64, 0);
    group.add(rail);
  }
  for (const x of [-0.84, 0.84]) {
    const foot = new THREE.Mesh(
      new THREE.BoxGeometry(0.28, 0.18, 0.8),
      trimMaterial,
    );
    foot.position.set(x, 0.08, 0);
    group.add(foot);
  }

  const panelGeometry = new THREE.PlaneGeometry(1.9, 2.46);
  const panelMaterial = new THREE.MeshBasicMaterial({
    map: serverPanelTexture(THREE, node),
    toneMapped: false,
  });
  const panel = new THREE.Mesh(panelGeometry, panelMaterial);
  // Keep a real depth gap in front of the 0.71 cabinet face. A near-coplanar
  // display flickers at oblique camera angles on mobile GPUs.
  panel.position.set(0, 1.72, 0.735);
  panel.name = "mirror-server-front-panel";
  panel.userData.nodeCabinet = { ...node };
  group.add(panel);
  // A mirrored service panel on the rear keeps the technical display readable
  // from the third-person camera while the physical front remains oriented
  // toward the routing-station walkway.
  const rearPanel = new THREE.Mesh(panelGeometry, panelMaterial);
  rearPanel.position.set(0, 1.72, -0.735);
  rearPanel.rotation.y = Math.PI;
  rearPanel.name = "mirror-server-rear-panel";
  rearPanel.userData.nodeCabinet = { ...node };
  group.add(rearPanel);

  const integrity = String(node?.integrity || "unknown").toLowerCase();
  const statusColor =
    integrity === "rejected" || integrity === "degraded"
      ? "#ff0000"
      : integrity === "healing" || (online && node?.cloneAvailable !== true)
        ? "#ffcc00"
        : online
          ? "#00cc44"
          : "#71837a";
  // A single beacon lamp sits on the cabinet roof; its color is the status.
  // The lens is an unlit cylinder so the status reads as one flat, solid
  // colour from every camera angle instead of shading into a gradient.
  const statusLight = new THREE.Group();
  const beaconBase = new THREE.Mesh(
    new THREE.CylinderGeometry(0.17, 0.19, 0.08, 20),
    makeMaterial(THREE, "#35463e", {
      metalness: 0.82,
      roughness: 0.3,
    }),
  );
  beaconBase.position.y = 0.04;
  statusLight.add(beaconBase);
  const beaconLens = new THREE.Mesh(
    new THREE.CylinderGeometry(0.15, 0.15, 0.24, 20),
    new THREE.MeshBasicMaterial({ color: statusColor, toneMapped: false }),
  );
  beaconLens.position.y = 0.2;
  statusLight.add(beaconLens);
  const beaconCap = new THREE.Mesh(
    new THREE.CylinderGeometry(0.16, 0.16, 0.05, 20),
    makeMaterial(THREE, "#35463e", {
      metalness: 0.82,
      roughness: 0.3,
    }),
  );
  beaconCap.position.y = 0.345;
  statusLight.add(beaconCap);
  statusLight.position.set(0, 3.29, 0);
  group.add(statusLight);

  const vent = new THREE.Mesh(
    new THREE.BoxGeometry(1.42, 0.26, 0.05),
    makeMaterial(THREE, "#35463e", {
      metalness: 0.76,
      roughness: 0.38,
    }),
  );
  vent.position.set(0, 0.22, 0.75);
  group.add(vent);
  group.userData.signalRing = statusLight;
  group.userData.online = online;
  group.userData.dataKey = nodeDataKey(node);
  group.userData.nodeRecord = { ...node };
  setShadows(group);
  return group;
}

function createAgentRobot(THREE, bot, id) {
  const group = new THREE.Group();
  group.name = `verified-agent-object:${id}`;
  group.userData.infrastructureKind = "automated-agent";
  group.userData.agentId = id;
  group.userData.verified = bot?.verified === true;
  const verified = bot?.verified === true;
  const core = new THREE.Mesh(
    new THREE.OctahedronGeometry(0.62, 0),
    makeMaterial(THREE, verified ? "#d5b6ff" : "#89929b", {
      emissive: verified ? "#6f4da0" : "#32383d",
      emissiveIntensity: verified ? 0.85 : 0.2,
      metalness: 0.42,
      roughness: 0.28,
    }),
  );
  core.position.y = 1.35;
  group.add(core);
  const lens = new THREE.Mesh(
    new THREE.SphereGeometry(0.17, 12, 10),
    makeMaterial(THREE, verified ? "#9ef7c6" : "#91a39a", {
      emissive: verified ? "#39c783" : "#3f4a45",
      emissiveIntensity: verified ? 1.25 : 0.3,
    }),
  );
  lens.position.set(0, 1.36, -0.55);
  group.add(lens);
  for (const side of [-1, 1]) {
    const rotor = new THREE.Mesh(
      new THREE.TorusGeometry(0.34, 0.055, 7, 20),
      makeMaterial(THREE, "#77d9ff", {
        emissive: "#237a99",
        emissiveIntensity: 0.72,
      }),
    );
    rotor.position.set(side * 0.8, 1.35, 0);
    rotor.rotation.x = Math.PI / 2;
    group.add(rotor);
  }
  const label = makeLabelSprite(
    THREE,
    String(bot.name).slice(0, 22),
    `${verified ? "verified" : "unverified"} · ${String(bot.type).slice(0, 20)}`,
    verified ? "#d5b6ff" : "#91a39a",
  );
  label.scale.set(2.7, 0.9, 1);
  label.position.y = 2.7;
  group.add(label);
  group.userData.core = core;
  setShadows(group);
  return group;
}

function createSystemCapacityPlatform(THREE) {
  const district = new THREE.Group();
  district.name = "system-capacity-infrastructure";
  district.userData.infrastructureKind = "system-capacity";
  district.userData.metricsAvailable = false;
  district.position.set(...SYSTEM_CAPACITY_PLATFORM_POSITION);
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(13.5, 0.36, 9.5),
    makeMaterial(THREE, "#102d32", {
      emissive: "#164a55",
      emissiveIntensity: 0.25,
      roughness: 0.62,
    }),
  );
  base.position.y = 0.18;
  district.add(base);
  addSectionPlaque(
    THREE,
    district,
    SYSTEM_CAPACITY_PLATFORM_POSITION,
    "SYSTEM CAPACITY",
    "live services + database rows",
    "#80e8ff",
    9,
  );
  const emptyMarker = new THREE.Mesh(
    new THREE.TorusGeometry(1.2, 0.08, 8, 36),
    makeMaterial(THREE, "#718087", {
      emissive: "#2d3f44",
      emissiveIntensity: 0.2,
      transparent: true,
      opacity: 0.75,
    }),
  );
  emptyMarker.name = "system-capacity-metrics-unavailable";
  emptyMarker.position.y = 1.4;
  emptyMarker.rotation.x = Math.PI / 2;
  district.add(emptyMarker);
  setShadows(district);
  return district;
}

function systemCapacityMetricPairs(record) {
  const usage =
    record?.usage && typeof record.usage === "object" ? record.usage : null;
  const limits =
    record?.limits && typeof record.limits === "object" ? record.limits : null;
  if (!usage || !limits) return [];
  return Object.keys(usage)
    .filter(
      (key) =>
        /^[A-Za-z][A-Za-z0-9_-]{0,31}$/.test(key) &&
        Object.prototype.hasOwnProperty.call(limits, key),
    )
    .map((key) => ({
      key,
      usage: Number(usage[key]),
      limit: Number(limits[key]),
    }))
    .filter(
      (metric) =>
        Number.isFinite(metric.usage) &&
        metric.usage >= 0 &&
        Number.isFinite(metric.limit) &&
        metric.limit > 0,
    )
    .slice(0, 4);
}

function createTree(THREE, x, z, scale = 1, color = "#2f8c5f") {
  const group = new THREE.Group();
  const trunk = new THREE.Mesh(
    new THREE.CylinderGeometry(0.12 * scale, 0.18 * scale, 1.25 * scale, 7),
    makeMaterial(THREE, "#694d34", { roughness: 1 }),
  );
  trunk.position.y = 0.62 * scale;
  group.add(trunk);
  const crown = new THREE.Mesh(
    new THREE.IcosahedronGeometry(0.68 * scale, 1),
    makeMaterial(THREE, color, { roughness: 0.88 }),
  );
  crown.scale.y = 1.35;
  crown.position.y = 1.62 * scale;
  group.add(crown);
  group.position.set(x, 0, z);
  setShadows(group);
  return group;
}

// The reward pool names itself: instead of a pale stone foundation plus a
// ground plaque, the title wraps around the green rim of the basin.
function rewardPoolRimTexture(THREE) {
  const width = 4096;
  const height = 256;
  const repeats = 5;
  return canvasTexture(THREE, width, height, (context) => {
    context.fillStyle = "#1d5240";
    context.fillRect(0, 0, width, height);
    context.fillStyle = "#164236";
    context.fillRect(0, 0, width, 18);
    context.fillRect(0, height - 18, width, 18);
    context.textAlign = "center";
    context.textBaseline = "middle";
    const slot = width / repeats;
    for (let index = 0; index < repeats; index += 1) {
      const centre = slot * (index + 0.5);
      context.fillStyle = "#f7c96b";
      context.font = '800 108px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("GLOBAL REWARD POOL", centre, height / 2 + 4);
      context.fillStyle = "#9ef7c6";
      context.font = '700 96px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("◎", centre + slot / 2, height / 2 + 4);
    }
  });
}

// Paints a QR matrix (from /qr.js) into `size` square pixels at (x, y).
function drawQrModules(context, text, x, y, size) {
  const encoder = globalThis.ForkMeshQR;
  if (!encoder?.generate || !text) return false;
  let matrix = null;
  try {
    matrix = encoder.generate(text);
  } catch {
    return false;
  }
  if (!matrix?.size) return false;
  const quiet = 4;
  const total = matrix.size + quiet * 2;
  const scale = size / total;
  context.fillStyle = "#ffffff";
  context.fillRect(x, y, size, size);
  context.fillStyle = "#07120e";
  for (let row = 0; row < matrix.size; row += 1) {
    for (let column = 0; column < matrix.size; column += 1) {
      if (!matrix.modules[row][column]) continue;
      context.fillRect(
        x + (column + quiet) * scale,
        y + (row + quiet) * scale,
        Math.ceil(scale),
        Math.ceil(scale),
      );
    }
  }
  return true;
}

function rewardTreasuryState(state = {}) {
  const address = String(state?.address || "").trim();
  const valid = /^[1-9A-HJ-NP-Za-km-z]{32,44}$/.test(address);
  const rawBalance = Number(
    state?.balanceSol ?? state?.balance ?? state?.sol ?? Number.NaN,
  );
  return {
    address: valid ? address : "",
    network: String(state?.network || "").slice(0, 24),
    balance:
      valid && Number.isFinite(rawBalance)
        ? `${rawBalance.toLocaleString("en-US", {
            minimumFractionDigits: 0,
            maximumFractionDigits: 6,
          })} SOL`
        : "",
  };
}

function rewardTreasuryTexture(THREE, treasury) {
  return canvasTexture(THREE, 512, 640, (context) => {
    context.clearRect(0, 0, 512, 640);
    roundedRect(context, 8, 8, 496, 624, 26);
    context.fillStyle = "rgba(7, 20, 16, 0.95)";
    context.fill();
    context.strokeStyle = "#f7c96b";
    context.lineWidth = 6;
    context.stroke();
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillStyle = "#9ef7c6";
    context.font = '700 30px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      `TREASURY · ${(treasury.network || "mainnet-beta").toUpperCase()}`,
      256,
      56,
    );
    const drawn = drawQrModules(context, treasury.address, 106, 92, 300);
    if (!drawn) {
      context.fillStyle = "rgba(158, 247, 198, 0.14)";
      context.fillRect(106, 92, 300, 300);
      context.fillStyle = "#d9ffea";
      context.font = '700 26px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO VERIFIED", 256, 226);
      context.fillText("POOL ADDRESS", 256, 262);
    }
    context.fillStyle = "#f7c96b";
    context.font = '800 54px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText(treasury.balance || "BALANCE UNAVAILABLE", 256, 448);
    context.fillStyle = "#d9ffea";
    context.font = '400 22px "ForkMesh Mono", ui-monospace, monospace';
    const address = treasury.address || "—";
    context.fillText(address.slice(0, 22), 256, 512);
    context.fillText(address.slice(22) || " ", 256, 542);
    context.fillStyle = "#9ef7c6";
    context.font = '400 18px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("PUBLIC ON-CHAIN BALANCE · EXTERNAL SIGNER", 256, 592);
  });
}

// A double-sided board at the basin edge showing the treasury address as a
// scannable QR code with the live public balance beneath it.
function createRewardTreasurySign(THREE) {
  const sign = new THREE.Group();
  sign.name = "reward-treasury-sign";
  const post = new THREE.Mesh(
    new THREE.BoxGeometry(0.28, 1.5, 0.28),
    makeMaterial(THREE, "#123a2c", { roughness: 0.82 }),
  );
  post.position.y = 0.75;
  sign.add(post);
  const board = new THREE.Mesh(
    new THREE.BoxGeometry(2.1, 2.6, 0.16),
    makeMaterial(THREE, "#0c1f19", { roughness: 0.56, metalness: 0.12 }),
  );
  board.position.y = 2.55;
  sign.add(board);
  const faces = [];
  for (const facing of [1, -1]) {
    const face = new THREE.Mesh(
      new THREE.PlaneGeometry(1.94, 2.42),
      new THREE.MeshBasicMaterial({
        map: rewardTreasuryTexture(THREE, rewardTreasuryState()),
        transparent: true,
        toneMapped: false,
      }),
    );
    face.position.z = facing * 0.085;
    face.rotation.y = facing > 0 ? 0 : Math.PI;
    board.add(face);
    faces.push(face);
  }
  sign.userData.treasuryFaces = faces;
  sign.userData.treasurySignature = "";
  return sign;
}

function applyRewardTreasury(THREE, sign, state) {
  const faces = sign?.userData?.treasuryFaces;
  if (!Array.isArray(faces) || !faces.length) return;
  const treasury = rewardTreasuryState(state);
  const signature = `${treasury.address}|${treasury.balance}|${treasury.network}`;
  if (sign.userData.treasurySignature === signature) return;
  sign.userData.treasurySignature = signature;
  faces.forEach((face) => {
    face.material.map?.dispose?.();
    face.material.map = rewardTreasuryTexture(THREE, treasury);
    face.material.needsUpdate = true;
  });
}

function createFountain(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const darkStone = makeMaterial(THREE, "#1d3b31", { roughness: 0.74 });
  const water = makeMaterial(THREE, "#48d9b1", {
    roughness: 0.2,
    metalness: 0.08,
    transparent: true,
    opacity: 0.72,
    emissive: "#20ae83",
    emissiveIntensity: 0.38,
  });
  const sunMat = makeMaterial(THREE, "#ffd66e", {
    roughness: 0.32,
    emissive: "#ffae3f",
    emissiveIntensity: 1.6,
  });

  const rimSide = new THREE.MeshStandardMaterial({
    map: rewardPoolRimTexture(THREE),
    roughness: 0.66,
    metalness: 0.06,
  });
  const rimCap = makeMaterial(THREE, "#1d5240", { roughness: 0.7 });
  const foundation = new THREE.Mesh(
    new THREE.CylinderGeometry(4.9, 5.25, 0.62, 64),
    [rimSide, rimCap, rimCap],
  );
  foundation.position.y = 0.31;
  group.add(foundation);
  const pool = new THREE.Mesh(new THREE.CylinderGeometry(4.4, 4.4, 0.16, 48), water);
  pool.position.y = 0.56;
  group.add(pool);
  const island = new THREE.Mesh(new THREE.CylinderGeometry(1.25, 1.65, 1.1, 28), darkStone);
  island.position.y = 1.05;
  group.add(island);
  const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.18, 0.34, 3.4, 16), sunMat);
  stem.position.y = 3.05;
  group.add(stem);
  const sun = new THREE.Mesh(new THREE.IcosahedronGeometry(1.15, 2), sunMat);
  sun.position.y = 5.15;
  sun.userData.baseY = sun.position.y;
  group.add(sun);

  for (const radius of [1.75, 2.65, 3.55]) {
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(radius, 0.04, 8, 64),
      makeMaterial(THREE, "#a7ffe1", {
        emissive: "#6dffd0",
        emissiveIntensity: 0.9,
      }),
    );
    ring.rotation.x = Math.PI / 2;
    ring.position.y = 0.68;
    group.add(ring);
  }

  const particleCount = 190;
  const particlePositions = new Float32Array(particleCount * 3);
  const particleSeeds = [];
  for (let index = 0; index < particleCount; index += 1) {
    const angle = Math.random() * Math.PI * 2;
    const radius = 0.35 + Math.random() * 3.7;
    const height = 0.65 + Math.random() * 4.4;
    particlePositions[index * 3] = Math.cos(angle) * radius;
    particlePositions[index * 3 + 1] = height;
    particlePositions[index * 3 + 2] = Math.sin(angle) * radius;
    particleSeeds.push({ angle, radius, speed: 0.25 + Math.random() * 0.6, height });
  }
  const particleGeometry = new THREE.BufferGeometry();
  particleGeometry.setAttribute("position", new THREE.BufferAttribute(particlePositions, 3));
  const particles = new THREE.Points(
    particleGeometry,
    new THREE.PointsMaterial({
      color: "#ffe39a",
      size: 0.12,
      transparent: true,
      opacity: 0.86,
      sizeAttenuation: true,
    }),
  );
  group.add(particles);

  const light = new THREE.PointLight("#ffd66e", 5.5, 24, 1.7);
  light.position.y = 5.3;
  group.add(light);

  const treasurySign = createRewardTreasurySign(THREE);
  placeSectionPlaque(group, treasurySign, position, 6.4);
  group.userData.treasurySign = treasurySign;

  group.position.set(...position);
  group.userData.landmark = "fountain";
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "fountain";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    sun.rotation.y = time * 0.00045;
    sun.rotation.x = Math.sin(time * 0.0004) * 0.12;
    sun.position.y = sun.userData.baseY + Math.sin(time * 0.0012) * 0.16;
    const positions = particles.geometry.attributes.position.array;
    for (let index = 0; index < particleCount; index += 1) {
      const seed = particleSeeds[index];
      const phase = time * 0.00035 * seed.speed;
      const pulse = 0.78 + Math.sin(phase * 2.3 + index) * 0.18;
      positions[index * 3] = Math.cos(seed.angle + phase) * seed.radius * pulse;
      positions[index * 3 + 1] =
        0.75 + ((seed.height + time * 0.00065 * seed.speed) % 4.5);
      positions[index * 3 + 2] = Math.sin(seed.angle + phase) * seed.radius * pulse;
    }
    particles.geometry.attributes.position.needsUpdate = true;
  });
  return group;
}

function compactSceneBytes(value) {
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

function repositorySizeLabelSprite(THREE, title, subtitle, color) {
  const texture = canvasTexture(THREE, 512, 160, (context) => {
    context.clearRect(0, 0, 512, 160);
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.shadowColor = "rgba(0, 0, 0, 0.86)";
    context.shadowBlur = 9;
    context.shadowOffsetY = 2;
    context.font = '700 34px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#ffffff";
    context.fillText(String(title || "").slice(0, 20), 256, 61);
    context.font = '700 23px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = color || "#d9ffea";
    context.fillText(String(subtitle || "").slice(0, 24), 256, 111);
  });
  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({
      map: texture,
      transparent: true,
      depthTest: true,
      depthWrite: false,
      toneMapped: false,
    }),
  );
  sprite.renderOrder = 12;
  return sprite;
}

function repositoryStarPlaneTexture(THREE, count, starred = false) {
  // A single, fixed upright plane avoids the chunky extruded-star silhouette
  // at close range. Its transparent texture preserves the actual star shape
  // while keeping the complete repo_stars value readable at its centre.
  return canvasTexture(THREE, 512, 512, (context) => {
    context.clearRect(0, 0, 512, 512);
    const outerRadius = 224;
    const innerRadius = 104;
    context.beginPath();
    for (let point = 0; point < 10; point += 1) {
      const angle = -Math.PI / 2 + point * (Math.PI / 5);
      const radius = point % 2 === 0 ? outerRadius : innerRadius;
      const x = 256 + Math.cos(angle) * radius;
      const y = 256 + Math.sin(angle) * radius;
      if (point === 0) context.moveTo(x, y);
      else context.lineTo(x, y);
    }
    context.closePath();
    context.fillStyle = starred ? "#f7c96b" : "#9ef7c6";
    context.fill();
    context.lineWidth = 14;
    context.strokeStyle = starred ? "#b87418" : "#20764f";
    context.stroke();
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillStyle = "#07120e";
    const value = Number.isSafeInteger(count)
      ? count.toLocaleString("en-US")
      : "—";
    let size = 168;
    do {
      context.font = `900 ${size}px "ForkMesh Mono", ui-monospace, monospace`;
      size -= 8;
    } while (size > 40 && context.measureText(value).width > 230);
    context.fillText(value, 256, 265);
  });
}

function repositoryStonePlaqueTexture(THREE, repositoryName) {
  return canvasTexture(THREE, 1024, 220, (context) => {
    context.fillStyle = "#89928b";
    context.fillRect(0, 0, 1024, 220);
    context.strokeStyle = "#c9d4c9";
    context.lineWidth = 14;
    context.strokeRect(8, 8, 1008, 204);
    context.fillStyle = "#102019";
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '800 76px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(String(repositoryName || "repository").slice(0, 42), 512, 112);
  });
}

const REPOSITORY_RECORD_STATE_COLORS = {
  open: "#9ef7c6",
  closed: "#8fa39a",
  merged: "#d5b6ff",
};

function repositoryIssuePageTexture(THREE, issue, expanded, repositoryName) {
  const width = expanded ? 512 : 256;
  const height = expanded ? 672 : 336;
  return canvasTexture(THREE, width, height, (context) => {
    context.fillStyle = "#f6f1e2";
    context.fillRect(0, 0, width, height);
    context.strokeStyle = "#c8bfa4";
    context.lineWidth = expanded ? 6 : 4;
    context.strokeRect(2, 2, width - 4, height - 4);
    const accent =
      REPOSITORY_RECORD_STATE_COLORS[issue.state] || "#f7c96b";
    context.fillStyle = accent;
    context.fillRect(0, 0, width, expanded ? 88 : 52);
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillStyle = "#17251d";
    context.font = `900 ${expanded ? 52 : 30}px "ForkMesh Mono", ui-monospace, monospace`;
    context.fillText(`ISSUE #${issue.number}`, width / 2, expanded ? 46 : 27);
    context.fillStyle = "#3d4a41";
    context.font = `700 ${expanded ? 34 : 24}px "ForkMesh Mono", ui-monospace, monospace`;
    context.fillText(
      (issue.state || "recorded").toUpperCase(),
      width / 2,
      expanded ? 140 : 88,
    );
    // Ruled page lines keep the sheet reading as a document, not a button.
    context.strokeStyle = "rgba(84, 96, 88, 0.35)";
    context.lineWidth = expanded ? 3 : 2;
    const firstRule = expanded ? 200 : 120;
    const ruleGap = expanded ? 46 : 30;
    for (let y = firstRule; y < height - (expanded ? 130 : 36); y += ruleGap) {
      context.beginPath();
      context.moveTo(width * 0.12, y);
      context.lineTo(width * 0.88, y);
      context.stroke();
    }
    if (expanded) {
      context.fillStyle = "#3d4a41";
      context.font = '700 26px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(String(repositoryName || "").slice(0, 30), 256, 580);
      context.fillStyle = "#1f6b46";
      context.font = '800 28px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("CLICK AGAIN TO OPEN", 256, 622);
    }
  });
}

function repositoryPullCardTexture(THREE, pull) {
  return canvasTexture(THREE, 1024, 232, (context) => {
    context.fillStyle = "#171429";
    context.fillRect(0, 0, 1024, 232);
    const accent =
      REPOSITORY_RECORD_STATE_COLORS[pull.state] || "#d5b6ff";
    context.fillStyle = accent;
    context.fillRect(0, 0, 18, 232);
    context.textAlign = "left";
    context.textBaseline = "middle";
    context.fillStyle = "#d5b6ff";
    context.font = '900 62px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(`#${pull.number}`, 52, 66);
    context.fillStyle = accent;
    context.font = '800 40px "ForkMesh Mono", ui-monospace, monospace';
    context.textAlign = "right";
    context.fillText((pull.state || "recorded").toUpperCase(), 984, 66);
    context.textAlign = "left";
    context.fillStyle = "#f1edff";
    context.font = '700 44px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(String(pull.title || "").slice(0, 40), 52, 164);
  });
}

const REPOSITORY_FOLLOWER_ACCENTS = [
  "#d5b6ff",
  "#8c8dff",
  "#ff9eb7",
  "#77d9ff",
  "#9ef7c6",
  "#f7c96b",
];
const REPOSITORY_FOLLOWER_SKINS = [
  "#d59a70",
  "#9a6043",
  "#f0bd91",
  "#704832",
  "#c98255",
];
const REPOSITORY_FOLLOWERS_VISIBLE = 8;

function repositoryFollowerSeed(follower) {
  return hashNumber(
    `${follower?.handle || ""}|${follower?.profileUrl || ""}`,
  );
}

function repositoryFollowerInitials(follower) {
  const source = String(follower?.name || follower?.handle || "")
    .replace(/^@+/, "")
    .trim();
  const parts = source.split(/[\s._@\-/]+/u).filter(Boolean);
  const initials = parts
    .slice(0, 2)
    .map((part) => [...part][0] || "")
    .join("");
  return (initials || "?").toLocaleUpperCase().slice(0, 2);
}

function repositoryFollowerFollowedLabel(followedAt) {
  const stamp = Number(followedAt);
  if (!Number.isSafeInteger(stamp) || stamp <= 0) return "";
  const date = new Date(stamp);
  if (Number.isNaN(date.getTime())) return "";
  return `FOLLOWING SINCE ${date.toISOString().slice(0, 10)}`;
}

function loadRepositoryFollowerAvatar(url, onLoad) {
  // The follower's published avatar, fetched anonymously (no cookies, no
  // referrer) and only used once it decodes. A server without CORS headers
  // simply fails here and the card keeps its generated initials plate.
  if (!url) return;
  const image = new Image();
  image.crossOrigin = "anonymous";
  image.referrerPolicy = "no-referrer";
  image.decoding = "async";
  image.onload = () => {
    if (image.naturalWidth > 0 && image.naturalHeight > 0) onLoad(image);
  };
  image.onerror = () => {};
  image.src = url;
}

function drawRepositoryFollowerAvatar(
  context,
  follower,
  accent,
  image,
  x,
  y,
  size,
) {
  const radius = size / 2;
  context.save();
  context.beginPath();
  context.arc(x + radius, y + radius, radius, 0, Math.PI * 2);
  context.closePath();
  context.clip();
  if (image) {
    // Cover-crop the remote avatar into the circle without distorting it.
    const scale = size / Math.min(image.naturalWidth, image.naturalHeight);
    const width = image.naturalWidth * scale;
    const height = image.naturalHeight * scale;
    context.drawImage(
      image,
      x + (size - width) / 2,
      y + (size - height) / 2,
      width,
      height,
    );
  } else {
    context.fillStyle = "#0b1c19";
    context.fillRect(x, y, size, size);
    context.fillStyle = accent;
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = `800 ${Math.round(size * 0.42)}px "ForkMesh Mono", ui-monospace, monospace`;
    context.fillText(
      repositoryFollowerInitials(follower),
      x + radius,
      y + radius + size * 0.02,
    );
  }
  context.restore();
  context.beginPath();
  context.arc(x + radius, y + radius, radius - 2, 0, Math.PI * 2);
  context.strokeStyle = accent;
  context.lineWidth = 5;
  context.stroke();
}

function repositoryFollowerCardTexture(THREE, follower, accent, image = null) {
  // One follower, with everything the fediverse publishes about them: avatar,
  // display name, full @handle, home instance, bio, and when the follow landed.
  return canvasTexture(THREE, 560, 320, (context) => {
    context.clearRect(0, 0, 560, 320);
    context.fillStyle = "rgba(7, 18, 15, 0.9)";
    roundedRect(context, 6, 6, 548, 308, 26);
    context.fill();
    context.strokeStyle = accent;
    context.lineWidth = 4;
    context.stroke();
    drawRepositoryFollowerAvatar(context, follower, accent, image, 30, 34, 108);
    context.textAlign = "left";
    context.textBaseline = "middle";
    const name = String(follower?.name || "").trim();
    const handle = String(follower?.handle || "").trim();
    context.font = '800 40px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#f1fff6";
    context.fillText((name || handle || "fediverse account").slice(0, 20), 158, 66);
    context.font = '700 27px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = accent;
    context.fillText((handle || follower?.instance || "").slice(0, 28), 158, 106);
    context.font = '700 22px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#9fb8ad";
    const meta = [
      String(follower?.instance || "").slice(0, 26),
      repositoryFollowerFollowedLabel(follower?.followedAt),
    ]
      .filter(Boolean)
      .join(" · ");
    context.fillText(meta.slice(0, 46), 158, 140);
    const about = String(follower?.about || "").trim();
    context.font = '600 24px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = about ? "#d9ffea" : "#65776f";
    const words = (about || "No public bio.").split(/\s+/u);
    let line = "";
    let row = 0;
    for (const word of words) {
      const candidate = line ? `${line} ${word}` : word;
      if (context.measureText(candidate).width > 494 && line) {
        context.fillText(line, 32, 196 + row * 36);
        row += 1;
        line = word;
        if (row >= 3) break;
      } else {
        line = candidate;
      }
    }
    if (row < 3 && line) context.fillText(line, 32, 196 + row * 36);
  });
}

function makeRepositoryFollowerFigure(THREE, follower) {
  // A seated visitor on the ground under the repository circle, leaning back to
  // look up at it. Purpose-built (not the player avatar) so the crowd stays
  // small, cheap, and clearly a gallery rather than another walking body.
  const seed = repositoryFollowerSeed(follower);
  const accent =
    REPOSITORY_FOLLOWER_ACCENTS[seed % REPOSITORY_FOLLOWER_ACCENTS.length];
  const group = new THREE.Group();
  group.name = `repository-follower:${follower.handle || follower.profileUrl}`;
  const cloth = makeMaterial(THREE, accent, {
    emissive: accent,
    emissiveIntensity: 0.14,
    roughness: 0.74,
    metalness: 0.06,
  });
  const skin = makeMaterial(
    THREE,
    REPOSITORY_FOLLOWER_SKINS[seed % REPOSITORY_FOLLOWER_SKINS.length],
    { roughness: 0.92 },
  );
  const dark = makeMaterial(THREE, "#12241f", { roughness: 0.86 });

  const legs = new THREE.Mesh(new THREE.BoxGeometry(1.12, 0.34, 0.94), dark);
  legs.position.set(0, 0.19, -0.2);
  group.add(legs);
  const torso = new THREE.Mesh(
    new THREE.CapsuleGeometry(0.38, 0.56, 6, 14),
    cloth,
  );
  torso.position.set(0, 0.82, 0.06);
  // Leaning back is what makes the pose read as "looking up at the circle".
  torso.rotation.x = 0.22;
  group.add(torso);
  const armGeometry = new THREE.CapsuleGeometry(0.12, 0.44, 5, 10);
  const leftArm = new THREE.Mesh(armGeometry, cloth);
  leftArm.position.set(-0.46, 0.62, -0.1);
  leftArm.rotation.set(0.5, 0, 0.42);
  group.add(leftArm);
  const rightArm = new THREE.Mesh(armGeometry, cloth);
  rightArm.position.set(0.46, 0.62, -0.1);
  rightArm.rotation.set(0.5, 0, -0.42);
  group.add(rightArm);
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.33, 20, 16), skin);
  head.position.set(0, 1.44, 0.16);
  group.add(head);
  const hair = new THREE.Mesh(
    new THREE.SphereGeometry(0.35, 18, 12, 0, Math.PI * 2, 0, Math.PI * 0.55),
    dark,
  );
  hair.position.set(0, 1.5, 0.14);
  // Tilted back with the head, so the face stays clear while they look up.
  hair.rotation.x = 0.3;
  group.add(hair);
  const halo = new THREE.Mesh(
    new THREE.TorusGeometry(0.42, 0.03, 6, 28),
    makeMaterial(THREE, accent, {
      emissive: accent,
      emissiveIntensity: 1.1,
      metalness: 0.3,
      roughness: 0.3,
    }),
  );
  halo.position.set(0, 0.03, -0.16);
  halo.rotation.x = Math.PI / 2;
  group.add(halo);
  setShadows(group, true, true);

  const card = new THREE.Sprite(
    new THREE.SpriteMaterial({
      map: repositoryFollowerCardTexture(THREE, follower, accent),
      transparent: true,
      depthTest: true,
      depthWrite: false,
      toneMapped: false,
    }),
  );
  card.name = `repository-follower-card:${follower.handle || follower.profileUrl}`;
  card.scale.set(2.45, 1.4, 1);
  card.position.set(0, 2.05, 0);
  card.renderOrder = 13;
  group.add(card);
  loadRepositoryFollowerAvatar(follower.avatar, (image) => {
    // The layer may have been rebuilt (catalog refresh) while the avatar was
    // in flight; a detached card is dropped instead of repainted.
    if (!card.parent) return;
    const next = repositoryFollowerCardTexture(THREE, follower, accent, image);
    card.material.map?.dispose?.();
    card.material.map = next;
    card.material.needsUpdate = true;
  });
  group.userData.repositoryFollower = { ...follower };
  return group;
}

function safeRepositoryPath(value) {
  const path = String(value || "");
  if (!path || path.length > 1000 || /[\u0000-\u001f\u007f]/u.test(path)) {
    return "";
  }
  const parts = path.split("/");
  if (
    parts.length > 64 ||
    parts.some((part) => !part || part === "." || part === "..")
  ) {
    return "";
  }
  return path;
}

function repositorySizeMapShade(hex, depth, index) {
  const factor = Math.min(
    0.5,
    Math.max(0, (depth - 1) * 0.13 + (depth > 1 ? (index % 2) * 0.055 : 0)),
  );
  const channel = (offset) => {
    const value = Number.parseInt(hex.slice(1 + offset * 2, 3 + offset * 2), 16);
    return Math.round(value + (235 - value) * factor);
  };
  return `rgb(${channel(0)}, ${channel(1)}, ${channel(2)})`;
}

function repositorySizeMapColors(root) {
  const colors = new Map();
  const walk = (node, depth, base, index) => {
    const hue =
      depth === 1
        ? node?.name !== "…" && index < REPOSITORY_SIZE_MAP_COLORS.length
          ? REPOSITORY_SIZE_MAP_COLORS[index]
          : REPOSITORY_SIZE_MAP_GRAY
        : base;
    colors.set(node, repositorySizeMapShade(hue, depth, index));
    const children = Array.isArray(node?.children) ? node.children : [];
    children.forEach((child, childIndex) =>
      walk(child, depth + 1, hue, childIndex),
    );
  };
  (Array.isArray(root?.children) ? root.children : []).forEach((child, index) =>
    walk(child, 1, REPOSITORY_SIZE_MAP_GRAY, index),
  );
  return colors;
}

function repositorySizeMapFocus(root, requestedPath) {
  const parts = safeRepositoryPath(requestedPath).split("/").filter(Boolean);
  let node = root;
  const trail = [];
  for (const part of parts) {
    const next = (Array.isArray(node?.children) ? node.children : []).find(
      (child) =>
        child?.type === "directory" && String(child.name || "") === part,
    );
    if (!next) return { node: root, path: "", trail: [] };
    node = next;
    trail.push(part);
  }
  return { node, path: trail.join("/"), trail };
}

function repositorySizeMapSegments(root, requestedPath) {
  const focus = repositorySizeMapFocus(root, requestedPath);
  const colors = repositorySizeMapColors(root);
  const segments = [];
  const minSpan = (Math.PI * 2) / 900;
  let deepest = 1;
  const walk = (node, depth, from, span, parentPath) => {
    if (
      depth > REPOSITORY_SIZE_MAP_MAX_RINGS ||
      segments.length >= REPOSITORY_SIZE_MAP_MAX_SEGMENTS
    ) {
      return;
    }
    const size = Math.max(0, Number(node?.size) || 0);
    if (!(size > 0)) return;
    let at = from;
    const children = Array.isArray(node?.children) ? node.children : [];
    children.forEach((child) => {
      const childSize = Math.max(0, Number(child?.size) || 0);
      const childSpan = span * Math.min(1, childSize / size);
      const reportedPath = safeRepositoryPath(child?.path);
      const childName = String(child?.name || "item").slice(0, 100);
      const childPath =
        reportedPath ||
        safeRepositoryPath([parentPath, childName].filter(Boolean).join("/"));
      if (
        childSpan >= minSpan &&
        segments.length < REPOSITORY_SIZE_MAP_MAX_SEGMENTS
      ) {
        const segment = {
          node: child,
          name: childName,
          path: childPath,
          depth,
          from: at,
          to: at + childSpan,
          size: childSize,
          color: colors.get(child) || REPOSITORY_SIZE_MAP_GRAY,
          type:
            child?.type === "directory"
              ? "directory"
              : child?.type === "file"
                ? "file"
                : "summary",
        };
        segments.push(segment);
        deepest = Math.max(deepest, depth);
        if (segment.type === "directory") {
          walk(child, depth + 1, at, childSpan, childPath);
        }
      }
      at += childSpan;
    });
  };
  walk(focus.node, 1, -Math.PI / 2, Math.PI * 2, focus.path);
  return {
    ...focus,
    segments,
    ringCount: Math.max(1, Math.min(REPOSITORY_SIZE_MAP_MAX_RINGS, deepest)),
  };
}

function repositoryWedgeGeometry(
  THREE,
  innerRadius,
  outerRadius,
  from,
  to,
  extrusion,
) {
  const span = Math.min(to - from, Math.PI * 2 - 0.0004);
  const gap = Math.min(0.018, span * 0.14);
  const start = from + gap * 0.5;
  const end = from + span - gap * 0.5;
  if (end <= start) return null;
  const shape = new THREE.Shape();
  shape.moveTo(
    Math.cos(start) * innerRadius,
    Math.sin(start) * innerRadius,
  );
  shape.lineTo(
    Math.cos(start) * outerRadius,
    Math.sin(start) * outerRadius,
  );
  shape.absarc(0, 0, outerRadius, start, end, false);
  shape.lineTo(Math.cos(end) * innerRadius, Math.sin(end) * innerRadius);
  shape.absarc(0, 0, innerRadius, end, start, true);
  shape.closePath();
  const geometry = new THREE.ExtrudeGeometry(shape, {
    depth: extrusion,
    steps: 1,
    curveSegments: 10,
    bevelEnabled: true,
    bevelSegments: 1,
    bevelSize: 0.012,
    bevelThickness: 0.018,
  });
  geometry.translate(0, 0, -extrusion * 0.5);
  return geometry;
}

function createRepositoryDistrict(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const ringMaterial = makeMaterial(THREE, "#77d9ff", {
    metalness: 0.2,
    roughness: 0.28,
    emissive: "#1a6d8b",
    emissiveIntensity: 0.65,
  });
  const frameMaterial = makeMaterial(THREE, "#102a32", { roughness: 0.7 });

  const portal = new THREE.Group();
  const repositoryCore = new THREE.Group();
  repositoryCore.position.y = 3.35;
  const globe = new THREE.Mesh(
    new THREE.IcosahedronGeometry(0.72, 3),
    makeMaterial(THREE, "#184a59", {
      metalness: 0.22,
      roughness: 0.35,
      emissive: "#1b7896",
      emissiveIntensity: 0.72,
      transparent: true,
      opacity: 0.92,
    }),
  );
  globe.name = "repository-world-core";
  repositoryCore.add(globe);
  [0.91, 1.08].forEach((radius, index) => {
    const orbit = new THREE.Mesh(
      new THREE.TorusGeometry(radius, 0.025, 6, 72),
      ringMaterial,
    );
    orbit.rotation.x = index ? Math.PI / 2.7 : Math.PI / 5.4;
    orbit.rotation.y = index ? Math.PI / 4 : -Math.PI / 5;
    repositoryCore.add(orbit);
  });
  portal.add(repositoryCore);

  const frameLeft = new THREE.Mesh(
    new THREE.BoxGeometry(0.38, 6.6, 0.54),
    frameMaterial,
  );
  frameLeft.position.set(-4.25, 3.3, 0);
  portal.add(frameLeft);
  const frameRight = frameLeft.clone();
  frameRight.position.x = 4.25;
  portal.add(frameRight);

  const legacyFiles = new THREE.Group();
  legacyFiles.name = "repository-legacy-file-graph";
  const files = [];
  const fileColors = ["#77d9ff", "#9ef7c6", "#d5b6ff", "#f7c96b", "#ff9eb7"];
  for (let index = 0; index < 17; index += 1) {
    const size = 0.22 + (index % 5) * 0.07;
    const file = new THREE.Mesh(
      new THREE.BoxGeometry(size, size * 1.22, 0.08),
      makeMaterial(THREE, fileColors[index % fileColors.length], {
        emissive: fileColors[index % fileColors.length],
        emissiveIntensity: 0.18,
      }),
    );
    const layer = index % 3;
    const angle = (index / 17) * Math.PI * 2;
    const radius = 1.15 + layer * 0.5;
    file.position.set(Math.cos(angle) * radius, 3.1 + Math.sin(angle * 2) * 0.4, Math.sin(angle) * 0.3);
    file.userData = { angle, radius, layer };
    files.push(file);
    legacyFiles.add(file);
  }
  portal.add(legacyFiles);
  // Repository navigation now lives on the perimeter portals and their
  // interactive sunbursts. Keep the plaque, but remove the obsolete central
  // globe/ring/upright centerpiece from the district.
  portal.visible = false;
  portal.userData.legacyPortalHidden = true;
  group.add(portal);

  group.position.set(...position);
  group.userData.landmark = "repositories";
  group.userData.fileMeshes = files;
  group.userData.legacyFiles = legacyFiles;
  group.userData.portal = portal;
  portal.userData.repositoryCore = repositoryCore;
  portal.userData.repositoryOrbitLayer = null;
  portal.userData.repositorySizeLayer = null;
  portal.userData.repositorySizeMeshes = [];
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "repositories";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    portal.rotation.y = portal.userData.repositorySizeLayer
      ? -0.12
      : Math.sin(time * 0.00018) * 0.09;
    globe.rotation.y = time * 0.00012;
    globe.rotation.x = Math.sin(time * 0.00009) * 0.12;
    if (portal.userData.repositoryOrbitLayer) {
      portal.userData.repositoryOrbitLayer.rotation.z = time * 0.000012;
    }
    files.forEach((file, index) => {
      if (file.userData.layoutPosition) {
        const base = file.userData.layoutPosition;
        const phase = time * 0.00045 + index * 1.71;
        file.position.set(
          base.x + Math.sin(phase) * 0.035,
          base.y + Math.cos(phase * 0.83) * 0.045,
          base.z + Math.cos(phase) * 0.025,
        );
        file.rotation.z = Math.sin(phase) * 0.08;
        return;
      }
      const phase = time * 0.0002 * (1 + file.userData.layer * 0.3);
      const angle = file.userData.angle + phase;
      file.position.x = Math.cos(angle) * file.userData.radius;
      file.position.y = 3.1 + Math.sin(angle * 2 + index) * 0.42;
      file.rotation.z = Math.sin(phase * 2 + index) * 0.25;
    });
    const relationshipLines = portal.userData.relationshipLines;
    const relationshipPairs = portal.userData.relationshipPairs || [];
    const positions = relationshipLines?.geometry?.attributes?.position;
    if (positions && relationshipPairs.length * 2 === positions.count) {
      relationshipPairs.forEach(([source, target], index) => {
        positions.setXYZ(
          index * 2,
          source.position.x,
          source.position.y,
          source.position.z,
        );
        positions.setXYZ(
          index * 2 + 1,
          target.position.x,
          target.position.y,
          target.position.z,
        );
      });
      positions.needsUpdate = true;
    }
  });
  return group;
}

function createOrganizationQuarter(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const buildingMaterial = makeMaterial(THREE, "#42385c", { roughness: 0.55 });
  const edgeMaterial = makeMaterial(THREE, "#d5b6ff", {
    emissive: "#60458b",
    emissiveIntensity: 0.72,
    metalness: 0.18,
  });
  const windowMaterial = makeMaterial(THREE, "#e7dcff", {
    emissive: "#a17bdd",
    emissiveIntensity: 0.82,
    roughness: 0.32,
  });

  const heights = [6.8, 4.7, 8.4];
  const xs = [-2.9, 0, 2.7];
  heights.forEach((height, index) => {
    const tower = new THREE.Mesh(
      new THREE.BoxGeometry(index === 1 ? 2.5 : 2.15, height, 2.8),
      buildingMaterial,
    );
    tower.position.set(xs[index], height / 2, index === 1 ? 0.5 : 0);
    group.add(tower);
    for (let floor = 0; floor < Math.floor(height / 1.15); floor += 1) {
      const window = new THREE.Mesh(
        new THREE.BoxGeometry(index === 1 ? 1.45 : 1.2, 0.28, 0.04),
        windowMaterial,
      );
      window.position.set(xs[index], 0.9 + floor * 1.12, -1.42);
      group.add(window);
    }
    const roof = new THREE.Mesh(
      new THREE.BoxGeometry(index === 1 ? 2.8 : 2.45, 0.16, 3.1),
      edgeMaterial,
    );
    roof.position.set(xs[index], height + 0.08, index === 1 ? 0.5 : 0);
    group.add(roof);
  });

  const lobby = new THREE.Mesh(new THREE.BoxGeometry(6.7, 1.55, 2.9), edgeMaterial);
  lobby.position.set(0, 0.8, -1.15);
  group.add(lobby);
  const doorway = new THREE.Mesh(
    new THREE.BoxGeometry(1.1, 1.15, 0.06),
    makeMaterial(THREE, "#9ef7c6", {
      transparent: true,
      opacity: 0.7,
      emissive: "#38d58d",
      emissiveIntensity: 0.75,
    }),
  );
  doorway.position.set(0, 0.64, -2.64);
  group.add(doorway);
  addSectionPlaque(THREE, group, position, "ORGANIZATIONS", "permissioned team spaces", "#d5b6ff", 4.4);

  group.position.set(...position);
  group.userData.landmark = "organizations";
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "organizations";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    doorway.material.opacity = 0.58 + Math.sin(time * 0.002) * 0.17;
  });
  return group;
}

function createFediverseCenter(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const base = new THREE.Mesh(
    new THREE.CylinderGeometry(4.8, 5.2, 0.42, 7),
    makeMaterial(THREE, "#3c2531"),
  );
  base.position.y = 0.21;
  group.add(base);
  const colors = ["#8c8dff", "#8fcf85", "#ffffff", "#ff806f", "#77d9ff"];
  const labels = ["M", "L", "X", "R", "↗"];
  const orbs = [];
  for (let index = 0; index < 5; index += 1) {
    const angle = (index / 5) * Math.PI * 2 + Math.PI / 4;
    const orb = new THREE.Mesh(
      new THREE.IcosahedronGeometry(0.92, 1),
      makeMaterial(THREE, colors[index], {
        emissive: colors[index],
        emissiveIntensity: 0.32,
        roughness: 0.38,
      }),
    );
    orb.position.set(Math.cos(angle) * 2.8, 2.6, Math.sin(angle) * 2.8);
    orb.userData = { angle, radius: 2.8, index };
    orbs.push(orb);
    group.add(orb);

    const label = makeLabelSprite(
      THREE,
      labels[index],
      ["Mastodon", "Lemmy", "Twitter / X", "Reddit", "public web"][index],
      colors[index],
    );
    label.scale.set(2.1, 0.7, 1);
    label.position.copy(orb.position).add(new THREE.Vector3(0, 1.35, 0));
    group.add(label);
  }
  const hub = new THREE.Mesh(
    new THREE.OctahedronGeometry(1.28, 1),
    makeMaterial(THREE, "#ff9eb7", {
      emissive: "#b8466d",
      emissiveIntensity: 0.68,
      metalness: 0.18,
    }),
  );
  hub.position.y = 2.2;
  group.add(hub);
  const connections = new THREE.Group();
  orbs.forEach((orb) => {
    const points = [new THREE.Vector3(0, 2.2, 0), orb.position.clone()];
    const geometry = new THREE.BufferGeometry().setFromPoints(points);
    connections.add(
      new THREE.Line(
        geometry,
        new THREE.LineBasicMaterial({ color: "#ff9eb7", transparent: true, opacity: 0.48 }),
      ),
    );
  });
  group.add(connections);
  addSectionPlaque(THREE, group, position, "FEDIVERSE", "consent-aware social center", "#ff9eb7", 6.4);
  group.userData.socialOrbs = orbs;

  group.position.set(...position);
  group.userData.landmark = "fediverse";
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "fediverse";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    hub.rotation.y = time * 0.00035;
    hub.rotation.x = time * 0.00018;
    orbs.forEach((orb, index) => {
      const angle = orb.userData.angle + time * 0.00009 * (index % 2 ? -1 : 1);
      orb.position.x = Math.cos(angle) * orb.userData.radius;
      orb.position.z = Math.sin(angle) * orb.userData.radius;
      orb.position.y = 2.6 + Math.sin(time * 0.0013 + index) * 0.24;
      orb.rotation.y += 0.004;
    });
  });
  return group;
}

function createSecurityWorkshop(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const platform = new THREE.Mesh(
    new THREE.BoxGeometry(7.8, 0.4, 5.4),
    makeMaterial(THREE, "#173c37"),
  );
  platform.position.y = 0.2;
  group.add(platform);
  const building = new THREE.Mesh(
    new THREE.BoxGeometry(6.4, 3.7, 3.8),
    makeMaterial(THREE, "#1d544d"),
  );
  building.position.y = 2.15;
  group.add(building);

  const clipboard = new THREE.Mesh(
    new THREE.BoxGeometry(2.6, 3.6, 0.28),
    makeMaterial(THREE, "#d9ece2"),
  );
  clipboard.position.set(0, 3.3, -2.08);
  clipboard.rotation.x = -0.04;
  group.add(clipboard);
  const clip = new THREE.Mesh(
    new THREE.BoxGeometry(0.95, 0.34, 0.18),
    makeMaterial(THREE, "#88f0df", {
      metalness: 0.25,
      emissive: "#42aa9a",
      emissiveIntensity: 0.52,
    }),
  );
  clip.position.set(0, 5.05, -2.27);
  group.add(clip);
  for (let index = 0; index < 5; index += 1) {
    const line = new THREE.Mesh(
      new THREE.BoxGeometry(index === 4 ? 1.2 : 1.75, 0.09, 0.035),
      makeMaterial(THREE, index === 0 ? "#ff8e78" : "#315b52"),
    );
    line.position.set(index === 4 ? -0.28 : 0, 4.32 - index * 0.54, -2.245);
    group.add(line);
  }
  const scan = new THREE.Mesh(
    new THREE.BoxGeometry(2.4, 0.08, 0.06),
    makeMaterial(THREE, "#88f0df", {
      emissive: "#88f0df",
      emissiveIntensity: 1.6,
    }),
  );
  scan.position.set(0, 4.62, -2.29);
  group.add(scan);
  addSectionPlaque(THREE, group, position, "SECURITY", "scoped scans + human review", "#88f0df", 5.2);

  group.position.set(...position);
  group.userData.landmark = "security";
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "security";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    scan.position.y = 2.15 + ((time * 0.0011) % 2.9);
  });
  return group;
}

function finishLandmark(group, id, position, interactive) {
  group.position.set(...position);
  group.userData.landmark = id;
  group.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.landmark = id;
    interactive.push(child);
  });
  setShadows(group);
  return group;
}

function createCommunityStage(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const platform = new THREE.Mesh(
    new THREE.CylinderGeometry(4.4, 4.8, 0.5, 10),
    makeMaterial(THREE, "#5b3827"),
  );
  platform.position.y = 0.25;
  group.add(platform);
  const stage = new THREE.Mesh(
    new THREE.BoxGeometry(6.4, 0.55, 3.8),
    makeMaterial(THREE, "#ffb77d", { roughness: 0.48 }),
  );
  stage.position.set(0, 0.62, 0.6);
  group.add(stage);
  const board = new THREE.Mesh(
    new THREE.BoxGeometry(5.8, 3.1, 0.28),
    makeMaterial(THREE, "#13231e"),
  );
  board.position.set(0, 3.0, 1.75);
  group.add(board);
  for (let index = 0; index < 4; index += 1) {
    const row = new THREE.Mesh(
      new THREE.BoxGeometry(4.6 - index * 0.35, 0.12, 0.04),
      makeMaterial(THREE, index === 0 ? "#ffb77d" : "#9ef7c6", {
        emissive: index === 0 ? "#ff8b52" : "#2b9c66",
        emissiveIntensity: 0.85,
      }),
    );
    row.position.set(-0.25 + index * 0.08, 3.85 - index * 0.57, 1.58);
    group.add(row);
  }
  addSectionPlaque(THREE, group, position, "EVENTS", "UTC community stage", "#ffb77d", 6);
  finishLandmark(group, "events", position, interactive);
  animated.push((time) => {
    board.material.emissive?.set?.("#2c1711");
    board.material.emissiveIntensity = 0.12 + Math.sin(time * 0.0012) * 0.05;
  });
  return group;
}

function createNeighborhood(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const houseColors = ["#8dbd77", "#6e9f82", "#9ac89c"];
  for (let index = 0; index < 3; index += 1) {
    const house = new THREE.Group();
    const body = new THREE.Mesh(
      new THREE.BoxGeometry(2.5, 2.3, 2.4),
      makeMaterial(THREE, houseColors[index]),
    );
    body.position.y = 1.25;
    house.add(body);
    const roof = new THREE.Mesh(
      new THREE.ConeGeometry(2.1, 1.25, 4),
      makeMaterial(THREE, "#315b45"),
    );
    roof.rotation.y = Math.PI / 4;
    roof.position.y = 3.05;
    house.add(roof);
    const door = new THREE.Mesh(
      new THREE.BoxGeometry(0.62, 1.35, 0.08),
      makeMaterial(THREE, index === 0 ? "#b8e986" : "#263f35", {
        emissive: index === 0 ? "#5aa733" : "#000000",
        emissiveIntensity: index === 0 ? 0.52 : 0,
      }),
    );
    door.position.set(0, 0.83, -1.23);
    house.add(door);
    house.position.set((index - 1) * 3.3, 0, index === 1 ? 0.8 : 0);
    group.add(house);
  }

  addSectionPlaque(
    THREE,
    group,
    position,
    "NEIGHBORHOOD",
    "knock · visit · privacy",
    "#b8e986",
    6.4,
  );
  finishLandmark(group, "neighborhood", position, interactive);
  animated.push((time) => {
    group.rotation.y = Math.sin(time * 0.00008) * 0.025;
  });
  return group;
}

function createBroadcastGarden(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const garden = new THREE.Mesh(
    new THREE.CylinderGeometry(4.5, 4.9, 0.38, 16),
    makeMaterial(THREE, "#24513f"),
  );
  garden.position.y = 0.19;
  group.add(garden);
  const speakerMaterial = makeMaterial(THREE, "#1b2d35", {
    roughness: 0.48,
  });
  for (const x of [-2.8, 2.8]) {
    const speaker = new THREE.Mesh(
      new THREE.BoxGeometry(1.15, 2.75, 1.05),
      speakerMaterial,
    );
    speaker.position.set(x, 1.55, 0.5);
    group.add(speaker);
    for (const y of [0.95, 1.9]) {
      const cone = new THREE.Mesh(
        new THREE.CylinderGeometry(0.33, 0.46, 0.12, 24),
        makeMaterial(THREE, "#8fcfff", {
          emissive: "#387ba6",
          emissiveIntensity: 0.48,
        }),
      );
      cone.rotation.x = Math.PI / 2;
      cone.position.set(x, y, -0.05);
      group.add(cone);
    }
  }
  const screen = new THREE.Mesh(
    new THREE.BoxGeometry(4.0, 2.35, 0.2),
    makeMaterial(THREE, "#8fcfff", {
      transparent: true,
      opacity: 0.62,
      emissive: "#2b6e9b",
      emissiveIntensity: 0.78,
    }),
  );
  screen.position.set(0, 2.3, 1.35);
  group.add(screen);
  const note = new THREE.Mesh(
    new THREE.TorusGeometry(0.72, 0.12, 10, 42, Math.PI * 1.55),
    makeMaterial(THREE, "#9ef7c6", {
      emissive: "#39c783",
      emissiveIntensity: 1.0,
    }),
  );
  note.position.set(0, 2.35, 1.12);
  group.add(note);
  addSectionPlaque(
    THREE,
    group,
    position,
    "BROADCAST",
    "opt-in media garden",
    "#8fcfff",
    6,
  );
  finishLandmark(group, "broadcast", position, interactive);
  animated.push((time) => {
    note.rotation.z = Math.sin(time * 0.001) * 0.2;
    screen.material.opacity = 0.54 + Math.sin(time * 0.0015) * 0.1;
  });
  return group;
}

function officeKeypadDisplayTexture(THREE, value = "", mode = "entry") {
  const digits = String(value || "").replace(/\D/g, "").slice(0, 4);
  const slots = Array.from(
    { length: 4 },
    (_unused, index) => digits[index] || "_",
  ).join(" ");
  return canvasTexture(THREE, 512, 168, (context) => {
    context.fillStyle = "#04110d";
    context.fillRect(0, 0, 512, 168);
    context.strokeStyle = "#63e6a5";
    context.lineWidth = 8;
    context.strokeRect(4, 4, 504, 160);
    context.fillStyle = "#8ca99b";
    context.font = '700 24px "ForkMesh Mono", ui-monospace, monospace';
    context.textAlign = "left";
    context.textBaseline = "middle";
    context.fillText(
      mode === "set"
        ? "SET CODE"
        : mode === "guide"
          ? "SET INSIDE"
          : "ACCESS CODE",
      24,
      34,
    );
    context.fillStyle = "#d9ffea";
    context.font = '800 64px "ForkMesh Mono", ui-monospace, monospace';
    context.textAlign = "center";
    context.fillText(slots, 256, 108);
  });
}

function officeKeypadButtonTexture(THREE, label) {
  return canvasTexture(THREE, 128, 128, (context) => {
    context.fillStyle = "#d9eee3";
    context.fillRect(0, 0, 128, 128);
    context.strokeStyle = "#315b4c";
    context.lineWidth = 8;
    context.strokeRect(4, 4, 120, 120);
    context.fillStyle = "#0b2119";
    context.font = '800 64px "ForkMesh Mono", ui-monospace, monospace';
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(String(label), 64, 68);
  });
}

function officeGuideBoardTexture(THREE) {
  return canvasTexture(THREE, 1024, 704, (context) => {
    context.fillStyle = "#071b15";
    context.fillRect(0, 0, 1024, 704);
    context.strokeStyle = "#78e9b0";
    context.lineWidth = 12;
    context.strokeRect(10, 10, 1004, 684);
    context.fillStyle = "#d9ffea";
    context.font = '800 62px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("OFFICE GUIDE", 58, 96);
    context.fillStyle = "#9bd1b5";
    context.font = '600 33px "ForkMesh Mono", ui-monospace, monospace';
    [
      "1  ENTER THROUGH THE FRONT DOOR",
      "2  CHOOSE #GENERAL ON THIS WALL",
      "3  CLICK A CHAIR TO TAKE A SEAT",
      "4  USE THE TASK BOARD FOR ORG WORK",
    ].forEach((line, index) => context.fillText(line, 58, 196 + index * 92));
    context.fillStyle = "#f7d58a";
    context.font = '700 28px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("MEETINGS AND TASKS STAY INSIDE THE OFFICE.", 58, 600);
  });
}

function worldBulletinTexture(THREE, events = [], offset = 0) {
  const allEntries = (Array.isArray(events) ? events : [])
    .filter((event) => event && String(event.title || "").trim())
    // Newest alerts always take priority at the top of the board.
    .sort((left, right) => Date.parse(right.startsAt || 0) - Date.parse(left.startsAt || 0));
  const start = clamp(Number(offset) || 0, 0, Math.max(0, allEntries.length - 10));
  const entries = allEntries.slice(start, start + 10);
  const wrapText = (context, text, x, y, maxWidth, lineHeight, maxLines = Infinity) => {
    const words = String(text || "").split(/\s+/).filter(Boolean);
    let line = "";
    let lines = 0;
    for (const word of words) {
      const candidate = line ? `${line} ${word}` : word;
      if (line && context.measureText(candidate).width > maxWidth) {
        context.fillText(line, x, y + lines * lineHeight);
        lines += 1;
        if (lines >= maxLines) return lines;
        line = word;
      } else {
        line = candidate;
      }
    }
    if (line && lines < maxLines) {
      context.fillText(line, x, y + lines * lineHeight);
      lines += 1;
    }
    return lines;
  };
  const formatTime = (value) => {
    const timestamp = Date.parse(value || "");
    return Number.isFinite(timestamp)
      ? new Date(timestamp).toLocaleString([], { dateStyle: "medium", timeStyle: "short" })
      : "Time to be announced";
  };
  // Stay within common mobile GPU texture limits (4096px) even though this is
  // a deliberately oversized physical board.
  return canvasTexture(THREE, 1536, 4096, (context) => {
    context.scale(0.75, 2 / 3);
    context.fillStyle = "#0b1820";
    context.fillRect(0, 0, 2048, 6144);
    context.strokeStyle = "#7ed9ff";
    context.lineWidth = 20;
    context.strokeRect(16, 16, 2016, 6112);
    context.fillStyle = "#e5f8ff";
    context.font = '800 108px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("WORLD BULLETIN", 84, 142);
    context.fillStyle = "#8eddf7";
    context.font = '700 42px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("PUBLIC ALERTS · LIVE COMMUNITY EVENTS · NEWEST FIRST", 86, 210);
    context.strokeStyle = "rgba(126,217,255,0.42)";
    context.lineWidth = 3;
    context.beginPath();
    context.moveTo(86, 250);
    context.lineTo(1960, 250);
    context.stroke();
    if (!entries.length) {
      context.fillStyle = "#c3dbe3";
      context.font = '700 58px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO ACTIVE PUBLIC ALERTS", 86, 430);
      context.font = '600 38px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("THE COMMUNITY SCHEDULE WILL APPEAR HERE.", 86, 520);
      return;
    }
    entries.forEach((event, index) => {
      const y = 350 + index * 565;
      context.fillStyle = "#f7d58a";
      context.font = '800 54px "ForkMesh Mono", ui-monospace, monospace';
      wrapText(context, `${start + index + 1}. ${event.title}`, 86, y, 1840, 64, 2);
      context.fillStyle = "#bad0d8";
      context.font = '700 35px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(`${event.type || "Community"} · ${event.destination || "Town Square"}`, 110, y + 150);
      context.fillStyle = "#8eddf7";
      context.font = '600 32px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(`START  ${formatTime(event.startsAt)}`, 110, y + 202);
      context.fillText(`END    ${formatTime(event.endsAt)}`, 110, y + 248);
      context.fillStyle = "#d5e6e9";
      // Event descriptions are sanitized to 500 characters upstream. This
      // compact body size fits that entire description in each of the ten
      // slots rather than silently truncating the useful details.
      context.font = '600 24px "ForkMesh Mono", ui-monospace, monospace';
      wrapText(context, event.description || "Community event", 110, y + 308, 1780, 32);
      context.strokeStyle = "rgba(126,217,255,0.28)";
      context.beginPath();
      context.moveTo(86, y + 520);
      context.lineTo(1960, y + 520);
      context.stroke();
    });
    context.fillStyle = "#8eddf7";
    context.font = '700 32px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      `SHOWING ${start + 1}-${Math.min(start + 10, allEntries.length)} OF ${allEntries.length} · USE THE ▲ / ▼ CONTROLS OR SCROLL OVER THIS BOARD`,
      86,
      6090,
    );
  });
}

const MASTODON_KIOSK_VISIBLE_TOOTS = 3;

function wrapCanvasText(context, text, x, y, maxWidth, lineHeight, maxLines = Infinity) {
  const words = String(text || "").split(/\s+/).filter(Boolean);
  let line = "";
  let lines = 0;
  for (const word of words) {
    const candidate = line ? `${line} ${word}` : word;
    if (line && context.measureText(candidate).width > maxWidth) {
      context.fillText(line, x, y + lines * lineHeight);
      lines += 1;
      if (lines >= maxLines) return lines;
      line = word;
    } else {
      line = candidate;
    }
  }
  if (line && lines < maxLines) {
    context.fillText(line, x, y + lines * lineHeight);
    lines += 1;
  }
  return lines;
}

function mastodonKioskTexture(THREE, snapshot = null, offset = 0, resolveImage = null) {
  return canvasTexture(THREE, 1280, 1600, (context) => {
    context.fillStyle = "#191a2e";
    context.fillRect(0, 0, 1280, 1600);
    if (!snapshot) {
      // No live profile yet (still fetching, or mastodon.social unreachable):
      // fall back to the static kiosk sign describing the board.
      context.fillStyle = "#6364ff";
      context.fillRect(12, 12, 1256, 222);
      context.fillStyle = "#f2f3ff";
      context.font = '800 132px "ForkMesh Favorit", sans-serif';
      context.fillText("MASTODON", 82, 172);
      context.fillStyle = "#c8c9ff";
      context.font = '700 64px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("@forkmesh", 82, 400);
      context.font = '600 50px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("@mastodon.social", 82, 482);
      context.strokeStyle = "rgba(99,100,255,0.5)";
      context.lineWidth = 4;
      context.beginPath();
      context.moveTo(82, 564);
      context.lineTo(1198, 564);
      context.stroke();
      context.fillStyle = "#e8e9ff";
      context.font = '700 56px "ForkMesh Mono", ui-monospace, monospace';
      [
        "LIVE PUBLIC PROFILE",
        "FOLLOWERS · FOLLOWING · POSTS",
        "BIO · VERIFIED LINKS",
        "LATEST TOOTS, SCROLLABLE",
      ].forEach((line, index) => {
        context.fillText(line, 82, 700 + index * 120);
      });
      context.fillStyle = "#8b9bf4";
      context.font = '800 64px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("TAP / CLICK TO OPEN", 82, 1350);
      context.fillStyle = "#7a7ca8";
      context.font = '600 42px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("READ-ONLY · FETCHED FROM MASTODON.SOCIAL", 82, 1475);
      context.strokeStyle = "#6364ff";
      context.lineWidth = 16;
      context.strokeRect(12, 12, 1256, 1576);
      return;
    }
    const image = (url) =>
      typeof resolveImage === "function" ? resolveImage(url) : null;
    // Header banner, cover-cropped like the profile page. Text over the band
    // sits on a darkening gradient so it stays readable on any artwork.
    const header = image(snapshot.headerURL);
    context.save();
    context.beginPath();
    context.rect(16, 16, 1248, 300);
    context.clip();
    if (header?.naturalWidth > 0 && header?.naturalHeight > 0) {
      const scale = Math.max(
        1248 / header.naturalWidth,
        300 / header.naturalHeight,
      );
      const width = header.naturalWidth * scale;
      const height = header.naturalHeight * scale;
      context.drawImage(
        header,
        16 + (1248 - width) / 2,
        16 + (300 - height) / 2,
        width,
        height,
      );
    } else {
      context.fillStyle = "#43389c";
      context.fillRect(16, 16, 1248, 300);
    }
    const shade = context.createLinearGradient(0, 96, 0, 316);
    shade.addColorStop(0, "rgba(15,16,36,0)");
    shade.addColorStop(1, "rgba(15,16,36,0.9)");
    context.fillStyle = shade;
    context.fillRect(16, 16, 1248, 300);
    context.fillStyle = "rgba(15,16,36,0.62)";
    roundedRect(context, 36, 36, 386, 64, 18);
    context.fill();
    context.fillStyle = "#f2f3ff";
    context.font = '800 40px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("MASTODON · LIVE", 58, 82);
    context.restore();
    // Avatar overlapping the banner edge, then identity beside it.
    const avatar = image(snapshot.avatarURL);
    context.fillStyle = "#191a2e";
    roundedRect(context, 38, 226, 188, 188, 40);
    context.fill();
    context.save();
    roundedRect(context, 48, 236, 168, 168, 32);
    context.clip();
    if (avatar?.naturalWidth > 0) {
      context.drawImage(avatar, 48, 236, 168, 168);
    } else {
      context.fillStyle = "#43389c";
      context.fillRect(48, 236, 168, 168);
      context.fillStyle = "#c8c9ff";
      context.font = '800 104px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("@", 92, 358);
    }
    context.restore();
    context.fillStyle = "#f2f3ff";
    context.font = '800 58px "ForkMesh Favorit", sans-serif';
    context.fillText(String(snapshot.displayName || "ForkMesh"), 248, 392);
    context.fillStyle = "#c8c9ff";
    context.font = '600 34px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(String(snapshot.acct || "@forkmesh@mastodon.social"), 248, 442);
    [
      ["FOLLOWERS", snapshot.followers],
      ["FOLLOWING", snapshot.following],
      ["POSTS", snapshot.posts],
      ["JOINED", snapshot.joined],
    ].forEach(([label, value], index) => {
      const x = 48 + index * 308;
      context.fillStyle = "#8b8db8";
      context.font = '700 26px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(label, x, 510);
      context.fillStyle = "#f2f3ff";
      context.font = '800 54px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(String(value ?? "—"), x, 572);
    });
    context.strokeStyle = "rgba(99,100,255,0.5)";
    context.lineWidth = 4;
    context.beginPath();
    context.moveTo(48, 616);
    context.lineTo(1232, 616);
    context.stroke();
    context.fillStyle = "#8b9bf4";
    context.font = '800 36px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("LATEST TOOTS", 48, 674);
    const toots = Array.isArray(snapshot.toots) ? snapshot.toots : [];
    const start = clamp(
      Number(offset) || 0,
      0,
      Math.max(0, toots.length - MASTODON_KIOSK_VISIBLE_TOOTS),
    );
    if (toots.length > MASTODON_KIOSK_VISIBLE_TOOTS) {
      context.textAlign = "right";
      context.fillStyle = "#7a7ca8";
      context.font = '700 28px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        `${start + 1}–${Math.min(
          start + MASTODON_KIOSK_VISIBLE_TOOTS,
          toots.length,
        )} / ${toots.length} · SCROLL ▲▼`,
        1232,
        672,
      );
      context.textAlign = "left";
    }
    const entries = toots.slice(start, start + MASTODON_KIOSK_VISIBLE_TOOTS);
    if (!entries.length) {
      context.fillStyle = "#c8c9ff";
      context.font = '600 36px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO PUBLIC TOOTS YET", 48, 770);
    }
    entries.forEach((toot, index) => {
      const y = 712 + index * 262;
      context.fillStyle = "#c8c9ff";
      context.font = '700 30px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        [toot.author, toot.date].filter(Boolean).join(" · "),
        48,
        y + 30,
      );
      context.fillStyle = "#e8e9ff";
      context.font = '600 32px "ForkMesh Mono", ui-monospace, monospace';
      wrapCanvasText(context, toot.text, 48, y + 86, 1184, 44, 4);
      context.strokeStyle = "rgba(99,100,255,0.28)";
      context.lineWidth = 3;
      context.beginPath();
      context.moveTo(48, y + 240);
      context.lineTo(1232, y + 240);
      context.stroke();
    });
    context.fillStyle = "#8b9bf4";
    context.font = '800 34px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("TAP / CLICK TO OPEN THE FULL PROFILE", 48, 1534);
    context.fillStyle = "#7a7ca8";
    context.font = '600 26px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("LIVE · READ-ONLY · FETCHED FROM MASTODON.SOCIAL", 48, 1576);
    context.strokeStyle = "#6364ff";
    context.lineWidth = 16;
    context.strokeRect(12, 12, 1256, 1576);
  });
}

function createMastodonKiosk(THREE, interactive) {
  const group = new THREE.Group();
  group.name = "forkmesh-mastodon-kiosk";
  // A newsstand beside the Office approach: close enough to read on the walk
  // to the door, far enough not to block it or the exterior keypad.
  group.position.set(33.5, 0, -18.5);
  group.rotation.y = Math.atan2(-group.position.x, -group.position.z);
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(5.8, 0.4, 2.2),
    makeMaterial(THREE, "#20213a", { metalness: 0.2, roughness: 0.7 }),
  );
  base.position.y = 0.2;
  const post = new THREE.Mesh(
    new THREE.BoxGeometry(0.5, 2.2, 0.5),
    makeMaterial(THREE, "#2c2d4d", { metalness: 0.4, roughness: 0.5 }),
  );
  post.position.y = 1.3;
  // A larger billboard so the live profile header, stats, and latest toots
  // are readable from the Office approach.
  const frame = new THREE.Mesh(
    new THREE.BoxGeometry(5.5, 7.2, 0.36),
    makeMaterial(THREE, "#43389c", { metalness: 0.35, roughness: 0.45 }),
  );
  frame.position.y = 5.35;
  const face = new THREE.Mesh(
    new THREE.PlaneGeometry(5.0, 6.25),
    new THREE.MeshBasicMaterial({
      map: mastodonKioskTexture(THREE),
      toneMapped: false,
    }),
  );
  face.name = "forkmesh-mastodon-kiosk-face";
  face.position.set(0, 5.35, 0.2);
  const makeKioskControl = (label, direction, y) => {
    const control = new THREE.Mesh(
      new THREE.PlaneGeometry(0.7, 0.7),
      new THREE.MeshBasicMaterial({
        map: canvasTexture(THREE, 256, 256, (context) => {
          context.fillStyle = "#20213a";
          context.fillRect(0, 0, 256, 256);
          context.strokeStyle = "#8b9bf4";
          context.lineWidth = 14;
          context.strokeRect(8, 8, 240, 240);
          context.fillStyle = "#e8e9ff";
          context.font = '800 160px "ForkMesh Mono", ui-monospace, monospace';
          context.textAlign = "center";
          context.textBaseline = "middle";
          context.fillText(label, 128, 136);
        }),
        toneMapped: false,
      }),
    );
    control.position.set(2.15, y, 0.3);
    control.userData.interactive = `mastodon-kiosk-scroll-${direction}`;
    return control;
  };
  const scrollUp = makeKioskControl("▲", "up", 8.4);
  const scrollDown = makeKioskControl("▼", "down", 2.3);
  group.add(base, post, frame, face, scrollUp, scrollDown);
  group.traverse((child) => {
    if (!child.isMesh) return;
    if (!child.userData.interactive) {
      child.userData.interactive = "mastodon-board";
    }
    interactive.push(child);
  });
  setShadows(group);
  return group;
}

function createForkMeshOffice(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const wallThickness = 0.35;
  const doorWidth = OFFICE_DOOR_WIDTH;
  const doorHeight = 4.4;
  const concrete = makeMaterial(THREE, "#18231f", {
    metalness: 0.08,
    roughness: 0.88,
  });
  const structure = makeMaterial(THREE, "#163f32", {
    metalness: 0.48,
    roughness: 0.42,
  });
  const glass = makeMaterial(THREE, "#9ef7c6", {
    transparent: true,
    opacity: 0.42,
    metalness: 0.08,
    roughness: 0.22,
  });
  const wood = makeMaterial(THREE, "#7b5a38", {
    roughness: 0.72,
  });
  const warm = makeMaterial(THREE, "#ffd18a", {
    emissive: "#e28b35",
    emissiveIntensity: 0.85,
    roughness: 0.4,
  });
  const doorMaterial = makeMaterial(THREE, "#245845", {
    metalness: 0.34,
    roughness: 0.5,
  });
  const keypadMaterial = makeMaterial(THREE, "#123b2c", {
    emissive: "#31b978",
    emissiveIntensity: 0.28,
    metalness: 0.58,
    roughness: 0.36,
  });

  const slab = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_WIDTH, 0.46, OFFICE_DEPTH),
    concrete,
  );
  slab.position.y = 0.23;
  group.add(slab);

  const rearWall = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_WIDTH, OFFICE_HEIGHT, wallThickness),
    concrete,
  );
  rearWall.position.set(
    0,
    OFFICE_HEIGHT / 2,
    -OFFICE_FRONT_Z + wallThickness / 2,
  );
  group.add(rearWall);

  for (const x of [
    -OFFICE_WIDTH / 2 + wallThickness / 2,
    OFFICE_WIDTH / 2 - wallThickness / 2,
  ]) {
    const sideWall = new THREE.Mesh(
      new THREE.BoxGeometry(wallThickness, OFFICE_HEIGHT, OFFICE_DEPTH),
      concrete,
    );
    sideWall.position.set(x, OFFICE_HEIGHT / 2, 0);
    group.add(sideWall);
  }

  const roof = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_WIDTH, 0.38, OFFICE_DEPTH),
    structure,
  );
  roof.position.y = OFFICE_HEIGHT - 0.19;
  group.add(roof);

  const frontZ = OFFICE_FRONT_Z - wallThickness / 2;
  const frontBottom = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_WIDTH, 0.62, wallThickness),
    concrete,
  );
  frontBottom.position.set(0, 0.31, frontZ);
  group.add(frontBottom);
  const frontTop = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_WIDTH, 1.18, wallThickness),
    concrete,
  );
  frontTop.position.set(0, OFFICE_HEIGHT - 0.59, frontZ);
  group.add(frontTop);

  const windowWidth = (OFFICE_WIDTH - doorWidth - 2.7) / 2;
  const windowX = doorWidth / 2 + 0.55 + windowWidth / 2;
  for (const x of [-windowX, windowX]) {
    const windowPane = new THREE.Mesh(
      new THREE.BoxGeometry(windowWidth, 4.8, 0.12),
      glass,
    );
    windowPane.position.set(x, 3.35, OFFICE_FRONT_Z - 0.03);
    group.add(windowPane);
  }
  for (const x of [
    -OFFICE_WIDTH / 2 + wallThickness / 2,
    -doorWidth / 2 - 0.28,
    doorWidth / 2 + 0.28,
    OFFICE_WIDTH / 2 - wallThickness / 2,
  ]) {
    const mullion = new THREE.Mesh(
      new THREE.BoxGeometry(0.34, OFFICE_HEIGHT - 0.62, 0.42),
      structure,
    );
    mullion.position.set(x, OFFICE_HEIGHT / 2, frontZ);
    group.add(mullion);
  }

  const reception = new THREE.Mesh(
    new THREE.BoxGeometry(3.15, 1.18, 0.95),
    wood,
  );
  reception.position.set(-1.9, 1.05, 1.35);
  reception.rotation.y = -0.12;
  group.add(reception);

  const sharedTable = new THREE.Mesh(
    new THREE.BoxGeometry(3.8, 0.28, 1.5),
    wood,
  );
  sharedTable.position.set(0.65, 1.55, -0.65);
  group.add(sharedTable);
  for (const x of [-0.75, 2.05]) {
    for (const z of [-1.05, -0.25]) {
      const leg = new THREE.Mesh(
        new THREE.BoxGeometry(0.18, 1.25, 0.18),
        structure,
      );
      leg.position.set(x, 0.9, z);
      group.add(leg);
    }
  }

  const terminal = new THREE.Mesh(
    new THREE.BoxGeometry(1.3, 0.82, 0.16),
    warm,
  );
  terminal.position.set(0.65, 2.22, -0.78);
  terminal.rotation.x = -0.12;
  group.add(terminal);

  const wallDisplay = new THREE.Mesh(
    new THREE.BoxGeometry(3.5, 1.65, 0.14),
    glass,
  );
  wallDisplay.position.set(1.6, 3.55, -OFFICE_FRONT_Z + 0.28);
  group.add(wallDisplay);

  for (const x of [-2.55, 0, 2.55]) {
    const light = new THREE.Mesh(
      new THREE.BoxGeometry(1.2, 0.08, 0.22),
      warm,
    );
    light.position.set(x, OFFICE_HEIGHT - 0.3, 0.25);
    group.add(light);
  }

  const signTexture = canvasTexture(THREE, 1024, 192, (context) => {
    context.fillStyle = "#0b1713";
    context.fillRect(0, 0, 1024, 192);
    context.strokeStyle = "#9ef7c6";
    context.lineWidth = 10;
    context.strokeRect(8, 8, 1008, 176);
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '800 72px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#d9ffea";
    context.fillText("FORKMESH OFFICE", 512, 96);
  });
  const sign = new THREE.Mesh(
    new THREE.PlaneGeometry(8.2, 1.35),
    new THREE.MeshBasicMaterial({
      map: signTexture,
      transparent: false,
      toneMapped: false,
    }),
  );
  sign.position.set(0, OFFICE_HEIGHT + 0.9, OFFICE_FRONT_Z + 0.2);
  group.add(sign);

  const doorPivot = new THREE.Group();
  doorPivot.name = "forkmesh-office-door-pivot";
  doorPivot.position.set(-doorWidth / 2, 0, OFFICE_FRONT_Z + 0.03);
  const door = new THREE.Mesh(
    new THREE.BoxGeometry(doorWidth, doorHeight, 0.18),
    doorMaterial,
  );
  door.name = "forkmesh-office-door";
  door.position.set(doorWidth / 2, doorHeight / 2 + 0.34, 0);
  door.userData.officeEnter = true;
  doorPivot.add(door);
  const handle = new THREE.Mesh(
    new THREE.SphereGeometry(0.1, 12, 8),
    warm,
  );
  handle.position.set(doorWidth - 0.28, doorHeight / 2 + 0.28, 0.12);
  handle.userData.officeEnter = true;
  doorPivot.add(handle);
  group.add(doorPivot);

  const keypad = new THREE.Group();
  keypad.name = "forkmesh-office-keypad";
  keypad.position.set(doorWidth / 2 + 0.78, 2.12, OFFICE_FRONT_Z + 0.13);
  const keypadBody = new THREE.Mesh(
    new THREE.BoxGeometry(0.82, 1.72, 0.2),
    keypadMaterial,
  );
  keypad.add(keypadBody);
  const keypadLight = new THREE.Mesh(
    new THREE.BoxGeometry(0.52, 0.12, 0.05),
    makeMaterial(THREE, "#63e6a5", {
      emissive: "#31b978",
      emissiveIntensity: 1.8,
      metalness: 0.08,
    }),
  );
  keypadLight.position.set(0, 0.72, 0.13);
  keypad.add(keypadLight);
  const keypadDisplay = new THREE.Mesh(
    new THREE.PlaneGeometry(0.64, 0.21),
    new THREE.MeshBasicMaterial({
      map: officeKeypadDisplayTexture(THREE),
      toneMapped: false,
    }),
  );
  keypadDisplay.name = "forkmesh-office-keypad-display";
  keypadDisplay.position.set(0, 0.48, 0.126);
  keypad.add(keypadDisplay);
  const keypadKeys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "clear", "0", "enter"];
  const keypadLabels = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "↵"];
  for (let index = 0; index < keypadKeys.length; index += 1) {
    const button = new THREE.Mesh(
      new THREE.BoxGeometry(0.17, 0.17, 0.04),
      makeMaterial(THREE, "#aec9bc", {
        emissive: "#355f4d",
        emissiveIntensity: 0.3,
        roughness: 0.52,
      }),
    );
    const row = Math.floor(index / 3);
    const column = index % 3;
    button.position.set((column - 1) * 0.23, 0.25 - row * 0.23, 0.13);
    button.userData.officeKeypadDigit = keypadKeys[index];
    keypad.add(button);
    const label = new THREE.Mesh(
      new THREE.PlaneGeometry(0.145, 0.145),
      new THREE.MeshBasicMaterial({
        map: officeKeypadButtonTexture(THREE, keypadLabels[index]),
        toneMapped: false,
      }),
    );
    label.position.set(button.position.x, button.position.y, 0.156);
    label.userData.officeKeypadDigit = keypadKeys[index];
    keypad.add(label);
  }
  keypad.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.officeAccessPanel = true;
    child.userData.officeKeypadLocation = "exterior";
  });
  keypad.userData.officeKeypadLocation = "exterior";
  keypad.userData.officeKeypadDisplay = keypadDisplay;
  keypad.userData.officeKeypadDigits = "";
  keypad.userData.officeKeypadMode = "entry";
  group.add(keypad);

  group.position.set(...position);
  group.userData.landmark = "office";
  group.userData.officeDoorPivot = doorPivot;
  group.userData.officeDoor = door;
  group.userData.officeKeypad = keypad;
  group.userData.officeKeypadBody = keypadBody;
  group.userData.officeKeypadLight = keypadLight;
  group.userData.officeKeypadDisplay = keypadDisplay;
  group.userData.officeKeypadDigits = "";
  group.userData.officeKeypadMode = "entry";
  group.userData.officeOccupied = false;
  group.userData.officeAvailable = true;
  doorPivot.rotation.y = Math.PI / 2;
  group.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.landmark = "office";
    interactive.push(child);
  });
  setShadows(group);
  sign.castShadow = false;
  animated.push((time) => {
    terminal.material.emissiveIntensity = 0.72 + Math.sin(time * 0.002) * 0.13;
    keypadLight.material.emissiveIntensity = 1.65 + Math.sin(time * 0.004) * 0.22;
  });
  return group;
}

function createWeather(THREE, scene) {
  const count = 700;
  const positions = new Float32Array(count * 3);
  for (let index = 0; index < count; index += 1) {
    positions[index * 3] = (Math.random() - 0.5) * 80;
    positions[index * 3 + 1] = Math.random() * 28;
    positions[index * 3 + 2] = (Math.random() - 0.5) * 80;
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  const rain = new THREE.Points(
    geometry,
    new THREE.PointsMaterial({
      color: "#9edfff",
      size: 0.07,
      transparent: true,
      opacity: 0.55,
    }),
  );
  rain.visible = false;
  rain.userData.kind = "rain";
  scene.add(rain);

  const snow = new THREE.Points(
    geometry.clone(),
    new THREE.PointsMaterial({
      color: "#ffffff",
      size: 0.18,
      transparent: true,
      opacity: 0.75,
    }),
  );
  snow.visible = false;
  snow.userData.kind = "snow";
  scene.add(snow);
  return { rain, snow };
}

function animateWeather(points, time, delta, kind) {
  if (!points.visible) return;
  const positions = points.geometry.attributes.position.array;
  const speed = kind === "rain" ? 15 : 1.4;
  for (let index = 0; index < positions.length / 3; index += 1) {
    positions[index * 3 + 1] -= delta * speed;
    if (kind === "snow") {
      positions[index * 3] += Math.sin(time * 0.001 + index) * delta * 0.35;
      positions[index * 3 + 2] += Math.cos(time * 0.0008 + index) * delta * 0.25;
    }
    if (positions[index * 3 + 1] < 0) positions[index * 3 + 1] = 28;
  }
  points.geometry.attributes.position.needsUpdate = true;
}

function updatePlayerLabel(element, identity) {
  const status = normalizeWorldStatus(
    identity?.statusEmoji,
    identity?.statusNote,
  );
  const name = String(identity?.name || "visitor").slice(0, 32);
  const nameCopy = document.createElement("span");
  nameCopy.className = "world-player-label-name";
  nameCopy.textContent = name;
  element.replaceChildren(nameCopy);
  if (status.emoji) {
    const statusCopy = document.createElement("span");
    statusCopy.className = "world-player-label-status";
    statusCopy.textContent = [status.emoji, status.note]
      .filter(Boolean)
      .join(" ");
    element.appendChild(statusCopy);
    element.setAttribute(
      "aria-label",
      `${name}, public status ${status.emoji}${
        status.note ? ` ${status.note}` : ""
      }`,
    );
  } else {
    element.setAttribute("aria-label", name);
  }
}

function makePlayerLabel(player, labelLayer) {
  const element = document.createElement("div");
  element.className = "world-player-label";
  element.dataset.playerLabel = player.userData.id || "";
  updatePlayerLabel(element, player.userData);
  labelLayer.appendChild(element);
  return element;
}

function updateScreenLabel(THREE, object, element, camera, width, height, yOffset = 0) {
  const position = new THREE.Vector3();
  object.getWorldPosition(position);
  position.y += yOffset;
  position.project(camera);
  const visible = position.z > -1 && position.z < 1 && Math.abs(position.x) < 1.25 && Math.abs(position.y) < 1.25;
  element.style.opacity = visible ? "1" : "0";
  element.style.visibility = visible ? "visible" : "hidden";
  if (!visible) return;
  element.style.left = `${(position.x * 0.5 + 0.5) * width}px`;
  element.style.top = `${(-position.y * 0.5 + 0.5) * height}px`;
}

export function createWorldScene({
  THREE,
  container,
  labelLayer,
  identity,
  reducedMotion = false,
  onLandmarkSelect = () => {},
  onOfficeProximity = () => {},
  onOfficeEnter = () => {},
  onOfficeTaskBoardSelect = () => {},
  onOfficeMeetingBoardSelect = () => {},
  onWorldBulletinSelect = () => {},
  onMastodonBoardSelect = () => {},
  onReferralBoardSelect = () => {},
  onSystemCapacityTableSelect = () => {},
  onRendererStateChange = () => {},
  onOfficeChairSelect = () => {},
  onOfficeMovement = () => {},
  onLocationChange = () => {},
  onRegionChange = () => {},
  onMovement = () => {},
  onModeration = () => {},
  onLayoutObjectMoved = () => {},
  onForkbotChat = () => {},
  onPlayForkmeshSong = () => {},
  onCreateRepository = () => {},
}) {
  // Phones frequently expose a high-density screen to a comparatively small
  // GPU.  Use the same scene, but avoid allocating multisample and shadow-map
  // buffers that can make WebGL context creation fail outright on those
  // devices.  Coarse pointer is capability-based, so a small desktop window
  // keeps its full renderer.
  const compactRenderer = Boolean(
    window.matchMedia?.("(pointer: coarse)")?.matches,
  );
  const scene = new THREE.Scene();
  scene.background = new THREE.Color("#174434");
  scene.fog = new THREE.FogExp2("#8ebaa8", 0.0085);

  const camera = new THREE.PerspectiveCamera(
    44,
    1,
    0.1,
    CAMERA_FAR_PLANE,
  );
  camera.position.set(...CAMERA_OFFSET);

  const renderer = new THREE.WebGLRenderer({
    antialias: !compactRenderer,
    alpha: false,
    stencil: !compactRenderer,
    powerPreference: compactRenderer ? "default" : "high-performance",
  });
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.08;
  renderer.shadowMap.enabled = !compactRenderer;
  renderer.shadowMap.type = THREE.PCFShadowMap;
  renderer.domElement.className = "world-canvas";
  renderer.domElement.setAttribute("aria-hidden", "true");
  // The canvas is purely visual; leaving a tabindex (even -1) lets a pointer
  // click move focus into an aria-hidden subtree.  A later dialog close then
  // triggers the browser's hidden-focused-element accessibility warning.
  renderer.domElement.removeAttribute("tabindex");
  renderer.domElement.dataset.cameraControl = "drag";
  renderer.domElement.dataset.cameraMode = "third-person";
  renderer.domElement.dataset.dragging = "false";
  container.appendChild(renderer.domElement);
  const handleContextLost = () => {
    onRendererStateChange("lost");
  };
  const handleContextRestored = () => {
    resize();
    onRendererStateChange("restored");
  };
  renderer.domElement.addEventListener(
    "webglcontextlost",
    handleContextLost,
  );
  renderer.domElement.addEventListener(
    "webglcontextrestored",
    handleContextRestored,
  );

  const hemisphere = new THREE.HemisphereLight("#d5fff1", "#19362d", 2.1);
  scene.add(hemisphere);
  const sun = new THREE.DirectionalLight("#fff1c4", 3.4);
  sun.position.set(-24, 35, 18);
  sun.castShadow = !compactRenderer;
  // A 2k shadow map is disproportionately costly while the player is moving.
  // 1k keeps the soft, low-poly look while leaving far more frame budget for
  // input and world animation.
  sun.shadow.mapSize.set(1024, 1024);
  sun.shadow.camera.left = -90;
  sun.shadow.camera.right = 90;
  sun.shadow.camera.top = 90;
  sun.shadow.camera.bottom = -90;
  sun.shadow.camera.near = 1;
  sun.shadow.camera.far = 220;
  sun.shadow.bias = -0.0003;
  scene.add(sun);

  const world = new THREE.Group();
  scene.add(world);
  const interactive = [];
  const animated = [];
  const landmarkObjects = new Map();

  // Fixed Town Square objects a platform administrator may reposition. The
  // shared placement is persisted server-side and re-applied for every
  // visitor when the world loads.
  const movableWorldObjects = new Map();
  const layoutHandles = new Map();
  const layoutDragOffset = new THREE.Vector3();
  // Last placement received from /api/world/layout, kept so objects that only
  // exist after live data arrives (node cabinets) can adopt their locked spot
  // the moment they are built.
  const lockedWorldLayout = new Map();
  // One R press is a 15° step: fine enough to line a placard up with a path,
  // coarse enough that a quarter turn is six taps.
  const LAYOUT_ROTATION_STEP = Math.PI / 12;
  const LAYOUT_COMMIT_DELAY_MS = 450;
  let layoutEditingEnabled = false;
  let draggedLayoutObject = null;
  let activeLayoutObject = null;
  let layoutCommitTimer = 0;

  function registerMovableObject(id, object) {
    if (!object) return;
    object.userData.layoutId = id;
    if (!movableWorldObjects.has(id)) {
      // Authored heading. Locked rotations are stored as an offset from it so
      // a row written before rotation existed (rotation 0) leaves the object
      // facing exactly the way the scene built it.
      if (!Number.isFinite(object.userData.layoutBaseRotation)) {
        object.userData.layoutBaseRotation = object.rotation.y;
      }
      movableWorldObjects.set(id, object);
    }
    applyLockedPlacement(id, object);
    if (layoutEditingEnabled) ensureLayoutHandles();
  }

  const worldBulletin = new THREE.Group();
  worldBulletin.name = "forkmesh-world-bulletin";
  // Keep this well outside the arrival / join grid: it is a destination, not
  // another object visitors need to navigate around when they first arrive.
  worldBulletin.position.set(-68, 0, 30);
  // Plane textures face local +Z. Rotate the board so its readable face looks
  // back into the World from the outer edge of the circular terrain.
  worldBulletin.rotation.y = Math.atan2(
    -worldBulletin.position.x,
    -worldBulletin.position.z,
  );
  let worldBulletinEvents = [];
  let worldBulletinOffset = 0;
  const bulletinFrame = new THREE.Mesh(
    new THREE.BoxGeometry(14.2, 36.9, 0.42),
    makeMaterial(THREE, "#163849", { metalness: 0.35, roughness: 0.44 }),
  );
  bulletinFrame.position.y = 18.7;
  bulletinFrame.userData.interactive = "world-bulletin";
  const bulletinFace = new THREE.Mesh(
    new THREE.PlaneGeometry(13.55, 36.13),
    new THREE.MeshBasicMaterial({
      map: worldBulletinTexture(THREE),
      toneMapped: false,
    }),
  );
  bulletinFace.name = "forkmesh-world-bulletin-face";
  bulletinFace.position.set(0, 18.7, 0.24);
  bulletinFace.userData.interactive = "world-bulletin";
  const makeBulletinControl = (label, direction, y) => {
    const control = new THREE.Mesh(
      new THREE.PlaneGeometry(1.35, 1.35),
      new THREE.MeshBasicMaterial({
        map: canvasTexture(THREE, 256, 256, (context) => {
          context.fillStyle = "#12384b";
          context.fillRect(0, 0, 256, 256);
          context.strokeStyle = "#7ed9ff";
          context.lineWidth = 14;
          context.strokeRect(8, 8, 240, 240);
          context.fillStyle = "#e5f8ff";
          context.font = '800 160px "ForkMesh Mono", ui-monospace, monospace';
          context.textAlign = "center";
          context.textBaseline = "middle";
          context.fillText(label, 128, 136);
        }),
        toneMapped: false,
      }),
    );
    control.position.set(5.9, y, 0.3);
    control.userData.interactive = `world-bulletin-scroll-${direction}`;
    return control;
  };
  const bulletinScrollUp = makeBulletinControl("▲", "up", 34.7);
  const bulletinScrollDown = makeBulletinControl("▼", "down", 2.65);
  worldBulletin.add(bulletinFrame, bulletinFace, bulletinScrollUp, bulletinScrollDown);
  interactive.push(bulletinFrame, bulletinFace, bulletinScrollUp, bulletinScrollDown);
  world.add(worldBulletin);
  registerMovableObject("world-bulletin", worldBulletin);

  const ground = new THREE.Mesh(
    new THREE.CircleGeometry(WORLD_GROUND_RADIUS, 128),
    makeMaterial(THREE, "#174434", { roughness: 1 }),
  );
  ground.rotation.x = -Math.PI / 2;
  ground.receiveShadow = true;
  ground.userData.ground = true;
  world.add(ground);

  // The room still assigns one of 64 ephemeral slots, but the grid itself is
  // no longer drawn: the plaques at the front edge carry the arrival story and
  // the lawn reads as open ground.
  const arrivalBox = new THREE.Group();
  arrivalBox.name = "world-arrival-box";
  const arrivalPlaque = makeArrivalPlaque(THREE);
  // Just past the grid's front edge (cells end at z ≈ 17.4), facing inward
  // toward the Town Square center as visitors leave the welcome grid.
  arrivalPlaque.position.set(0, 0, 15.2);
  arrivalPlaque.rotation.y = Math.PI;
  setShadows(arrivalPlaque);
  arrivalBox.add(arrivalPlaque);
  const songPlaque = makeGroundPlaque(
    THREE,
    "PLAY FORKMESH SONG",
    "CLICK TO PLAY · LOCAL LOOP",
    "#9ef7c6",
  );
  songPlaque.name = "forkmesh-song-plaque";
  songPlaque.position.set(4.8, 0, 15.4);
  songPlaque.rotation.y = Math.PI;
  songPlaque.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.playForkmeshSong = true;
    interactive.push(child);
  });
  arrivalBox.add(songPlaque);
  world.add(arrivalBox);
  registerMovableObject("arrival-box", arrivalBox);

  const landmarkFactories = {
    fountain: createFountain,
    repositories: createRepositoryDistrict,
    office: createForkMeshOffice,
  };
  LANDMARKS.forEach((landmark) => {
    const object = landmarkFactories[landmark.id](
      THREE,
      landmark.position,
      interactive,
      animated,
    );
    landmarkObjects.set(landmark.id, object);
    world.add(object);
    registerMovableObject("landmark-" + landmark.id, object);
  });
  const mastodonKiosk = createMastodonKiosk(THREE, interactive);
  world.add(mastodonKiosk);
  registerMovableObject("mastodon-kiosk", mastodonKiosk);
  let mastodonKioskSnapshot = null;
  let mastodonKioskOffset = 0;
  const mastodonKioskImages = new Map();
  const mastodonKioskWheelTargets = [];
  mastodonKiosk.traverse((child) => {
    if (child.isMesh) mastodonKioskWheelTargets.push(child);
  });

  const campfire = new THREE.Group();
  campfire.position.set(8, 0, 8);
  const firePit = new THREE.Mesh(
    new THREE.CylinderGeometry(0.85, 1.0, 0.22, 12),
    makeMaterial(THREE, "#4a4038", { roughness: 0.9 }),
  );
  firePit.position.y = 0.11;
  firePit.userData.campfirePit = true;
  interactive.push(firePit);
  campfire.add(firePit);
  for (let index = 0; index < 8; index += 1) {
    const stoneAngle = (index / 8) * Math.PI * 2;
    const stone = new THREE.Mesh(
      new THREE.DodecahedronGeometry(0.22, 0),
      makeMaterial(THREE, "#7d766c", { roughness: 0.95 }),
    );
    stone.position.set(
      Math.cos(stoneAngle) * 1.05,
      0.16,
      Math.sin(stoneAngle) * 1.05,
    );
    campfire.add(stone);
  }
  for (let index = 0; index < 3; index += 1) {
    const log = new THREE.Mesh(
      new THREE.CylinderGeometry(0.09, 0.09, 1.15, 8),
      makeMaterial(THREE, "#5a3b24", { roughness: 0.9 }),
    );
    log.rotation.z = Math.PI / 2;
    log.rotation.y = (index / 3) * Math.PI;
    log.position.y = 0.3;
    campfire.add(log);
  }
  const logPile = new THREE.Group();
  logPile.name = "campfire-log-pile";
  for (let index = 0; index < 5; index += 1) {
    const log = new THREE.Mesh(
      new THREE.CylinderGeometry(0.13, 0.13, 1.25, 10),
      makeMaterial(THREE, "#70472a", { roughness: 0.86 }),
    );
    log.rotation.z = Math.PI / 2;
    log.rotation.y = index * 0.38;
    log.position.set(-2.1 + (index % 2) * 0.22, 0.18 + Math.floor(index / 2) * 0.22, 1.55);
    log.userData.campfireLog = true;
    logPile.add(log);
    interactive.push(log);
  }
  campfire.add(logPile);
  const flame = new THREE.Mesh(
    new THREE.ConeGeometry(0.42, 1.05, 8),
    makeMaterial(THREE, "#ffb547", {
      emissive: "#ff7a2f",
      emissiveIntensity: 1.6,
      transparent: true,
      opacity: 0.92,
    }),
  );
  flame.position.y = 0.82;
  campfire.add(flame);
  const innerFlame = new THREE.Mesh(
    new THREE.ConeGeometry(0.24, 0.72, 8),
    makeMaterial(THREE, "#fff0a6", {
      emissive: "#ffb547",
      emissiveIntensity: 2.1,
      transparent: true,
      opacity: 0.94,
    }),
  );
  innerFlame.position.y = 0.72;
  campfire.add(innerFlame);
  let fireLevel = 1;
  const fireLight = new THREE.PointLight("#ffa14d", 3.2, 14, 1.8);
  fireLight.position.y = 1.1;
  campfire.add(fireLight);
  animated.push((time) => {
    const flicker = 1 + Math.sin(time * 0.011) * 0.12 + Math.sin(time * 0.023) * 0.06;
    const size = fireLevel * flicker;
    flame.scale.set(size, fireLevel * (1 + Math.sin(time * 0.017) * 0.16), size);
    innerFlame.scale.set(size * 0.82, size * 0.9, size * 0.82);
    fireLight.intensity = 3.2 * fireLevel + Math.sin(time * 0.013) * 0.7;
  });
  // Benches sit back far enough from the pit to leave a wide walkable ring
  // between the seats and the stones (and to clear the log pile at ~2.6). The
  // circle carries one wooden bench per registered member — occupied by a
  // seated directory figure while the member is away, left empty (and
  // sittable) while they walk the world as a live avatar — plus one bench
  // that always stays open so an arriving guest has a spot by the fire, and
  // widens whenever a new account joins so everyone still fits.
  const CAMPFIRE_BENCH_RADIUS = 6.2;
  const CAMPFIRE_CIRCLE_MIN_SEATS = 6;
  const CAMPFIRE_CIRCLE_MAX_SEATS = 96;
  const CAMPFIRE_SEAT_SPACING = 2.1;
  function rebuildCampfireCircle(neededSeats) {
    const count = Math.max(
      CAMPFIRE_CIRCLE_MIN_SEATS,
      Math.min(
        CAMPFIRE_CIRCLE_MAX_SEATS,
        Math.round(Number(neededSeats) || 0),
      ),
    );
    if (campfire.userData.seatCount === count) {
      return campfire.userData.seatOffsets;
    }
    const previous = campfire.userData.seatRing;
    if (previous) {
      previous.traverse((child) => {
        const interactiveIndex = interactive.indexOf(child);
        if (interactiveIndex >= 0) interactive.splice(interactiveIndex, 1);
      });
      campfire.remove(previous);
      disposeObject3D(previous);
    }
    const ring = new THREE.Group();
    ring.name = "campfire-member-circle";
    const radius = Math.max(
      CAMPFIRE_BENCH_RADIUS,
      (count * CAMPFIRE_SEAT_SPACING) / (2 * Math.PI),
    );
    const seatOffsets = [];
    for (let index = 0; index < count; index += 1) {
      const angle = (index / count) * Math.PI * 2;
      const bench = new THREE.Group();
      bench.position.set(Math.cos(angle) * radius, 0, Math.sin(angle) * radius);
      // Long axis tangent to the ring so every bench fronts the flames.
      bench.rotation.y = -angle + Math.PI / 2;
      const seat = new THREE.Mesh(
        new THREE.BoxGeometry(1.6, 0.14, 0.6),
        makeMaterial(THREE, "#8a5a33", { roughness: 0.86 }),
      );
      seat.position.y = 0.48;
      // Seat coordinates are read from the live world matrix at click time, so a
      // relocated campfire needs no bookkeeping and the seats can never drift.
      seat.userData.campfireBench = true;
      interactive.push(seat);
      bench.add(seat);
      [-0.62, 0.62].forEach((end) => {
        const leg = new THREE.Mesh(
          new THREE.BoxGeometry(0.16, 0.41, 0.5),
          makeMaterial(THREE, "#4f3018", { roughness: 0.9 }),
        );
        leg.position.set(end, 0.205, 0);
        bench.add(leg);
      });
      ring.add(bench);
      // Offsets land sitters just above the plank, matching the local
      // player's bench-seat pose height.
      seatOffsets.push(
        new THREE.Vector3(bench.position.x, seat.position.y + 0.1, bench.position.z),
      );
    }
    setShadows(ring);
    campfire.add(ring);
    campfire.userData.seatRing = ring;
    campfire.userData.seatCount = count;
    campfire.userData.seatOffsets = seatOffsets;
    return seatOffsets;
  }
  rebuildCampfireCircle(CAMPFIRE_CIRCLE_MIN_SEATS);
  setShadows(campfire);
  world.add(campfire);
  registerMovableObject("campfire", campfire);

  const activeLeaderboardSign = makeActiveLeaderboardSign(THREE);
  activeLeaderboardSign.position.set(...ACTIVE_LEADERBOARD_POSITION);
  activeLeaderboardSign.rotation.y = Math.atan2(
    -ACTIVE_LEADERBOARD_POSITION[0],
    -ACTIVE_LEADERBOARD_POSITION[2],
  );
  world.add(activeLeaderboardSign);
  registerMovableObject("active-leaderboard-sign", activeLeaderboardSign);
  const referralLeaderboardSign = makeReferralLeaderboardSign(THREE);
  referralLeaderboardSign.position.set(...REFERRAL_LEADERBOARD_POSITION);
  referralLeaderboardSign.rotation.y = Math.atan2(
    -REFERRAL_LEADERBOARD_POSITION[0],
    -REFERRAL_LEADERBOARD_POSITION[2],
  );
  world.add(referralLeaderboardSign);
  registerMovableObject("referral-leaderboard-sign", referralLeaderboardSign);
  // Tapping the board copies the viewer's referral link (world.js supplies
  // the handler); the whole face is the hit target.
  referralLeaderboardSign.userData.face.userData.interactive =
    "referral-leaderboard";
  interactive.push(referralLeaderboardSign.userData.face);

  // Repaints the referral sign only when the ranked rows or the viewer's own
  // share link actually changed, mirroring the active-leaderboard swap.
  function updateReferralLeaderboard(rows = [], viewerLink = "") {
    const face = referralLeaderboardSign.userData.face;
    const key = JSON.stringify([
      String(viewerLink || ""),
      rankedReferralRows(rows).map((row) => [
        String(row?.name || ""),
        Number(row?.clicks) || 0,
        Number(row?.signups) || 0,
      ]),
    ]);
    if (!face || referralLeaderboardSign.userData.key === key) return;
    face.material.map?.dispose?.();
    face.material.map = referralLeaderboardTexture(THREE, rows, viewerLink);
    face.material.needsUpdate = true;
    referralLeaderboardSign.userData.key = key;
  }
  const systemCapacityPlatform = createSystemCapacityPlatform(THREE);
  world.add(systemCapacityPlatform);
  registerMovableObject("system-capacity-platform", systemCapacityPlatform);

  // Every placard is individually movable, not just the section it belongs to:
  // a sign that reads well from the path is often a step away from where the
  // section itself wants to sit.
  world.traverse((child) => {
    const plaqueId = String(child.userData?.plaqueLayoutId || "");
    if (!plaqueId || movableWorldObjects.has(plaqueId)) return;
    registerMovableObject(plaqueId, child);
  });

  const player = createAvatar(THREE, identity);
  player.position.set(-8.1, 0.38, 30);
  player.rotation.y = Math.PI;
  world.add(player);
  const playerLabel = makePlayerLabel(player, labelLayer);

  // ForkBot is a compact rolling droid, rather than another humanoid avatar.
  // Its full body is a single click target that opens the shared General chat.
  const forkbot = new THREE.Group();
  forkbot.name = "forkbot-rolling-droid";
  forkbot.userData.name = "ForkBot";
  forkbot.userData.id = FORKBOT_PEER_ID;
  forkbot.userData.accountStatus = "Bot";
  forkbot.userData.phase = hashNumber(FORKBOT_PEER_ID) * 0.0001;
  forkbot.position.set(...FORKBOT_HOME);
  const rollingBall = new THREE.Mesh(
    new THREE.SphereGeometry(0.56, 24, 16),
    makeMaterial(THREE, "#173d4a", {
      metalness: 0.7,
      roughness: 0.22,
      emissive: "#0e6075",
      emissiveIntensity: 0.35,
    }),
  );
  rollingBall.position.y = 0.62;
  forkbot.add(rollingBall);
  const chassis = new THREE.Mesh(
    new THREE.CylinderGeometry(0.5, 0.57, 0.4, 20),
    makeMaterial(THREE, "#c9eee1", { metalness: 0.55, roughness: 0.3 }),
  );
  chassis.position.y = 1.08;
  forkbot.add(chassis);
  const dome = new THREE.Mesh(
    new THREE.SphereGeometry(0.46, 24, 14, 0, Math.PI * 2, 0, Math.PI / 2),
    makeMaterial(THREE, "#4dc8e8", {
      metalness: 0.65,
      roughness: 0.18,
      emissive: "#176b88",
      emissiveIntensity: 0.5,
    }),
  );
  dome.position.y = 1.26;
  forkbot.add(dome);
  const eye = new THREE.Mesh(
    new THREE.SphereGeometry(0.11, 14, 10),
    makeMaterial(THREE, "#ffdc76", { emissive: "#ffbd3d", emissiveIntensity: 2 }),
  );
  eye.position.set(0, 1.35, 0.43);
  forkbot.add(eye);
  const robotChest = new THREE.Mesh(
    new THREE.BoxGeometry(0.76, 0.28, 0.08),
    makeMaterial(THREE, "#18343b", {
      emissive: "#1d94a8",
      emissiveIntensity: 0.5,
      metalness: 0.72,
      roughness: 0.28,
    }),
  );
  robotChest.position.set(0, 1.02, 0.53);
  forkbot.add(robotChest);
  const antenna = new THREE.Mesh(
    new THREE.CylinderGeometry(0.035, 0.035, 0.42, 8),
    makeMaterial(THREE, "#77d9ff", { emissive: "#2b9eb8", emissiveIntensity: 1.2 }),
  );
  antenna.position.set(0, 1.78, 0);
  forkbot.add(antenna);
  const antennaLight = new THREE.Mesh(
    new THREE.SphereGeometry(0.1, 12, 8),
    makeMaterial(THREE, "#ff6f91", { emissive: "#ff406f", emissiveIntensity: 1.8 }),
  );
  antennaLight.position.set(0, 2.02, 0);
  forkbot.add(antennaLight);
  forkbot.userData.forkbotAntennaLight = antennaLight;
  forkbot.userData.rollingBall = rollingBall;
  forkbot.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.forkbotChat = true;
    interactive.push(child);
  });
  animated.push((time) => {
    antennaLight.material.emissiveIntensity = 1.2 + (Math.sin(time * 0.006) + 1) * 0.5;
    eye.material.emissiveIntensity = 1.25 + (Math.sin(time * 0.008) + 1) * 0.75;
  });
  world.add(forkbot);

  const remotePlayers = new Map();
  const remoteLabels = new Map();
  const moderationActions = new WeakMap();
  const moderationControlKeys = new Map();
  const officeParticipants = new Map();
  const officeParticipantLabels = new Map();
  const officeBubbles = new Map();
  const officeChairs = new Map();
  const officeInterior = new THREE.Group();
  officeInterior.name = "forkmesh-office-interior";
  officeInterior.visible = false;
  scene.add(officeInterior);

  const officeFloor = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_WIDTH, 0.4, OFFICE_DEPTH),
    makeMaterial(THREE, "#15231f", { roughness: 0.9 }),
  );
  officeFloor.position.y = 0.2;
  officeFloor.receiveShadow = true;
  officeInterior.add(officeFloor);
  const officeBackWall = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_WIDTH, OFFICE_HEIGHT, 0.35),
    makeMaterial(THREE, "#18322a", { roughness: 0.84 }),
  );
  officeBackWall.position.set(0, OFFICE_HEIGHT / 2, -OFFICE_FRONT_Z + 0.2);
  officeInterior.add(officeBackWall);
  for (const x of [
    -OFFICE_WIDTH / 2 + 0.2,
    OFFICE_WIDTH / 2 - 0.2,
  ]) {
    const wall = new THREE.Mesh(
      new THREE.BoxGeometry(0.35, OFFICE_HEIGHT, OFFICE_DEPTH),
      makeMaterial(THREE, "#163029", { roughness: 0.86 }),
    );
    wall.position.set(x, OFFICE_HEIGHT / 2, 0);
    officeInterior.add(wall);
  }
  const officeFrontPanelWidth = (OFFICE_WIDTH - OFFICE_DOOR_WIDTH) / 2;
  for (const x of [
    -(OFFICE_DOOR_WIDTH / 2 + officeFrontPanelWidth / 2),
    OFFICE_DOOR_WIDTH / 2 + officeFrontPanelWidth / 2,
  ]) {
    const wall = new THREE.Mesh(
      new THREE.BoxGeometry(officeFrontPanelWidth, OFFICE_HEIGHT, 0.35),
      makeMaterial(THREE, "#163029", { roughness: 0.86 }),
    );
    wall.position.set(x, OFFICE_HEIGHT / 2, OFFICE_FRONT_Z - 0.2);
    officeInterior.add(wall);
  }
  const officeDoorHeader = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_DOOR_WIDTH, 2.25, 0.35),
    makeMaterial(THREE, "#163029", { roughness: 0.86 }),
  );
  officeDoorHeader.position.set(
    0,
    OFFICE_HEIGHT - 1.125,
    OFFICE_FRONT_Z - 0.2,
  );
  officeInterior.add(officeDoorHeader);
  const officeInteriorDoorPivot = new THREE.Group();
  officeInteriorDoorPivot.name = "forkmesh-office-interior-door-pivot";
  officeInteriorDoorPivot.position.set(
    -OFFICE_DOOR_WIDTH / 2,
    0,
    OFFICE_FRONT_Z - 0.38,
  );
  const officeInteriorDoor = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_DOOR_WIDTH, 4.4, 0.18),
    makeMaterial(THREE, "#245845", {
      metalness: 0.34,
      roughness: 0.5,
    }),
  );
  officeInteriorDoor.name = "forkmesh-office-interior-door";
  officeInteriorDoor.position.set(OFFICE_DOOR_WIDTH / 2, 2.54, 0);
  officeInteriorDoorPivot.add(officeInteriorDoor);
  officeInteriorDoorPivot.rotation.y = Math.PI / 2;
  officeInterior.add(officeInteriorDoorPivot);
  const officeInteriorKeypad = new THREE.Group();
  officeInteriorKeypad.name = "forkmesh-office-interior-keypad";
  officeInteriorKeypad.position.set(
    OFFICE_DOOR_WIDTH / 2 + 0.78,
    2.12,
    OFFICE_FRONT_Z - 0.43,
  );
  // The exterior control faces Town Square; this control faces into the room.
  officeInteriorKeypad.rotation.y = Math.PI;
  const officeInteriorKeypadBody = new THREE.Mesh(
    new THREE.BoxGeometry(0.82, 1.72, 0.2),
    makeMaterial(THREE, "#123b2c", {
      emissive: "#31b978",
      emissiveIntensity: 0.28,
      metalness: 0.58,
      roughness: 0.36,
    }),
  );
  officeInteriorKeypad.add(officeInteriorKeypadBody);
  const officeInteriorKeypadLight = new THREE.Mesh(
    new THREE.BoxGeometry(0.52, 0.12, 0.05),
    makeMaterial(THREE, "#63e6a5", {
      emissive: "#31b978",
      emissiveIntensity: 1.8,
      metalness: 0.08,
    }),
  );
  officeInteriorKeypadLight.position.set(0, 0.72, 0.13);
  officeInteriorKeypad.add(officeInteriorKeypadLight);
  const officeInteriorKeypadDisplay = new THREE.Mesh(
    new THREE.PlaneGeometry(0.64, 0.21),
    new THREE.MeshBasicMaterial({
      map: officeKeypadDisplayTexture(THREE, "", "set"),
      toneMapped: false,
    }),
  );
  officeInteriorKeypadDisplay.name = "forkmesh-office-interior-keypad-display";
  officeInteriorKeypadDisplay.position.set(0, 0.48, 0.126);
  officeInteriorKeypad.add(officeInteriorKeypadDisplay);
  const officeInteriorKeypadKeys = [
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "clear",
    "0",
    "enter",
  ];
  const officeInteriorKeypadLabels = [
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "C",
    "0",
    "↵",
  ];
  officeInteriorKeypadKeys.forEach((key, index) => {
    const button = new THREE.Mesh(
      new THREE.BoxGeometry(0.17, 0.17, 0.04),
      makeMaterial(THREE, "#aec9bc", {
        emissive: "#355f4d",
        emissiveIntensity: 0.3,
        roughness: 0.52,
      }),
    );
    const row = Math.floor(index / 3);
    const column = index % 3;
    button.position.set((column - 1) * 0.23, 0.25 - row * 0.23, 0.13);
    button.userData.officeKeypadDigit = key;
    officeInteriorKeypad.add(button);
    const label = new THREE.Mesh(
      new THREE.PlaneGeometry(0.145, 0.145),
      new THREE.MeshBasicMaterial({
        map: officeKeypadButtonTexture(
          THREE,
          officeInteriorKeypadLabels[index],
        ),
        toneMapped: false,
      }),
    );
    label.position.set(button.position.x, button.position.y, 0.156);
    label.userData.officeKeypadDigit = key;
    officeInteriorKeypad.add(label);
  });
  officeInteriorKeypad.userData.officeKeypadLocation = "interior";
  officeInteriorKeypad.userData.officeKeypadDisplay =
    officeInteriorKeypadDisplay;
  officeInteriorKeypad.userData.officeKeypadDigits = "";
  officeInteriorKeypad.userData.officeKeypadMode = "set";
  officeInteriorKeypad.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.officeAccessPanel = true;
    child.userData.officeKeypadLocation = "interior";
    interactive.push(child);
  });
  officeInterior.add(officeInteriorKeypad);
  const windowMaterial = makeMaterial(THREE, "#8eeac5", {
    transparent: true,
    opacity: 0.3,
    emissive: "#1f8b65",
    emissiveIntensity: 0.36,
  });
  for (const x of [-5.5, -1.85, 1.85, 5.5]) {
    const windowPane = new THREE.Mesh(
      new THREE.BoxGeometry(2.7, 3.4, 0.08),
      windowMaterial,
    );
    windowPane.position.set(x, 3.6, -OFFICE_FRONT_Z + 0.42);
    officeInterior.add(windowPane);
  }
  const officeTable = new THREE.Mesh(
    new THREE.BoxGeometry(7.4, 0.34, 3.6),
    makeMaterial(THREE, "#715238", { roughness: 0.66 }),
  );
  officeTable.position.set(0, 1.45, 0);
  officeInterior.add(officeTable);
  for (const x of [-3, 3]) {
    for (const z of [-1.2, 1.2]) {
      const tableLeg = new THREE.Mesh(
        new THREE.BoxGeometry(0.22, 1.25, 0.22),
        makeMaterial(THREE, "#10231e", { metalness: 0.35 }),
      );
      tableLeg.position.set(x, 0.78, z);
      officeInterior.add(tableLeg);
    }
  }
  const chairTransforms = [
    [-4.75, -1.35, Math.PI / 2],
    [-2.2, -2.85, 0],
    [0, -2.85, 0],
    [2.2, -2.85, 0],
    [4.75, 1.35, -Math.PI / 2],
    [2.2, 2.85, Math.PI],
    [0, 2.85, Math.PI],
    [-2.2, 2.85, Math.PI],
  ];
  chairTransforms.forEach(([x, z, yaw], index) => {
    const chairId = `chair-${index + 1}`;
    const chair = new THREE.Group();
    const seat = new THREE.Mesh(
      new THREE.BoxGeometry(1.05, 0.18, 1.05),
      makeMaterial(THREE, "#2f6d56", { roughness: 0.72 }),
    );
    seat.position.y = 0.82;
    chair.add(seat);
    const back = new THREE.Mesh(
      new THREE.BoxGeometry(1.05, 1.45, 0.18),
      makeMaterial(THREE, "#347a61", { roughness: 0.7 }),
    );
    back.position.set(0, 1.42, 0.48);
    chair.add(back);
    chair.position.set(x, 0, z);
    chair.rotation.y = yaw;
    chair.userData.officeChairId = chairId;
    chair.traverse((child) => {
      if (!child.isMesh) return;
      child.userData.officeChairId = chairId;
      child.userData.interactive = "office-chair";
      interactive.push(child);
    });
    officeChairs.set(chairId, chair);
    officeInterior.add(chair);
  });
  const officeMarketingTaskBoard = new THREE.Group();
  officeMarketingTaskBoard.name = "forkmesh-office-marketing-task-board";
  officeMarketingTaskBoard.position.set(-8.02, 3.55, -0.55);
  officeMarketingTaskBoard.rotation.y = Math.PI / 2;
  const officeMarketingTaskBoardFrame = new THREE.Mesh(
    new THREE.BoxGeometry(6.7, 4.85, 0.18),
    makeMaterial(THREE, "#315b52", {
      metalness: 0.34,
      roughness: 0.48,
      emissive: "#173f34",
      emissiveIntensity: 0.24,
    }),
  );
  officeMarketingTaskBoardFrame.name =
    "forkmesh-office-marketing-task-board-frame";
  officeMarketingTaskBoardFrame.userData.interactive =
    "office-marketing-task-board";
  officeMarketingTaskBoard.add(officeMarketingTaskBoardFrame);
  const officeMarketingTaskBoardFace = new THREE.Mesh(
    new THREE.PlaneGeometry(6.38, 4.52),
    new THREE.MeshBasicMaterial({
      map: officeMarketingTasksTexture(THREE, {
        authorized: false,
        state: "locked",
      }),
    }),
  );
  officeMarketingTaskBoardFace.name =
    "forkmesh-office-marketing-task-board-face";
  officeMarketingTaskBoardFace.position.z = 0.101;
  officeMarketingTaskBoardFace.userData.interactive =
    "office-marketing-task-board";
  officeMarketingTaskBoard.add(officeMarketingTaskBoardFace);
  officeMarketingTaskBoard.userData.face = officeMarketingTaskBoardFace;
  officeMarketingTaskBoard.userData.taskState = "locked";
  officeMarketingTaskBoard.userData.taskCount = 0;
  interactive.push(
    officeMarketingTaskBoardFrame,
    officeMarketingTaskBoardFace,
  );
  officeInterior.add(officeMarketingTaskBoard);
  const officeGuideBoard = new THREE.Mesh(
    new THREE.PlaneGeometry(5.8, 3.98),
    new THREE.MeshBasicMaterial({
      map: officeGuideBoardTexture(THREE),
      toneMapped: false,
    }),
  );
  officeGuideBoard.name = "forkmesh-office-guide-board";
  officeGuideBoard.position.set(4.65, 3.4, -OFFICE_FRONT_Z + 0.42);
  officeGuideBoard.userData.interactive = "office-meeting-board";
  interactive.push(officeGuideBoard);
  officeInterior.add(officeGuideBoard);
  const officeRoomSign = makeLabelSprite(
    THREE,
    "FORKMESH OFFICE",
    "encrypted meeting room",
    "#9ef7c6",
  );
  officeRoomSign.position.set(0, 6.2, -5.35);
  officeInterior.add(officeRoomSign);
  const officeLight = new THREE.PointLight("#ffd8a3", 5.5, 28, 1.6);
  officeLight.position.set(0, 6.2, 0);
  officeInterior.add(officeLight);
  const officeLobbyPlayer = createAvatar(
    THREE,
    {
      ...identity,
      id: `${String(identity?.id || "visitor").slice(0, 24)}-office`,
    },
    { scale: 0.9 },
  );
  officeLobbyPlayer.name = "forkmesh-office-lobby-player";
  officeLobbyPlayer.position.set(0, 0.38, 4.62);
  officeLobbyPlayer.rotation.y = Math.PI;
  officeLobbyPlayer.visible = false;
  officeInterior.add(officeLobbyPlayer);
  const neighborhoodHomes = new Map();
  const nodeInfrastructure = new Map();
  const botAgents = new Map();
  const loungeMembers = new Map();
  const repositoryPortals = new Map();
  const emoteSprites = [];
  const rewardFlights = [];
  const forkbotWanderTarget = new THREE.Vector3(...FORKBOT_HOME);
  let forkbotNextWanderAt = 0;
  let forkbotGreeting = null;
  const keys = new Set();
  const touchKeys = new Set();
  const touchMovement = new THREE.Vector2();
  const touchPointers = new Map();
  const raycaster = new THREE.Raycaster();
  const groundPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
  const pointer = new THREE.Vector2();
  const pointerStart = new THREE.Vector2();
  const pointerLast = new THREE.Vector2();
  let currentLocation = "Town Square";
  let currentRegion = "central";
  let currentSpace = "town-square";
  let currentFloorY = 0.38;
  let currentTheme = "world";
  let running = true;
  let disposed = false;
  let lastFrame = performance.now();
  let diagnosticsSampleAt = lastFrame;
  let diagnosticsFrameCount = 0;
  let diagnosticsRendererCalls = 0;
  let diagnosticsRendererTriangles = 0;
  let diagnosticsLongestFrameMs = 0;
  let diagnosticsLongFrames = 0;
  let lastRenderStallLogAt = 0;
  let diagnosticsPointerMoves = 0;
  let diagnosticsPointerLastAt = 0;
  let diagnosticsPointerWorstGapMs = 0;
  let lastMovementEmit = 0;
  let lastPosition = player.position.clone();
  let wasWalking = false;
  let cameraFocus = null;
  let officeZoneState = "distant";
  let focusedRepositoryKey = "";
  let cameraZoom = 1;
  let firstPersonZoom = 1;
  let environmentFogDensity = 0.0085;
  let cameraYaw = Math.atan2(CAMERA_OFFSET[0], CAMERA_OFFSET[2]);
  let cameraPitch = Math.asin(CAMERA_OFFSET[1] / CAMERA_DISTANCE);
  let firstPersonPitch = 0;
  let cameraMode = "third-person";
  let jumpVelocity = 0;
  let jumpQueued = false;
  // Per-device movement tuning (scales the shared defaults above). Acceleration
  // may be Infinity, meaning the player snaps to top speed the instant a key is
  // pressed. Both are adjustable from the World's local controls.
  let moveSpeedScale = 1;
  let moveAccelScale = 1;
  let keyboardMovementSpeed = PLAYER_SPEED;
  let dashTarget = null;
  // Set while the player is sitting on a campfire bench: the seat pose is held
  // every frame until the visitor walks, dashes or jumps away from it.
  let benchSeat = null;
  let primaryPointerId = null;
  let draggedCampfireLog = null;
  let pointerGestureMoved = false;
  let lastGestureDragged = false;
  let pinchStartDistance = 0;
  let pinchStartZoom = cameraZoom;
  let pinchActive = false;
  let lightLevel = LIGHT_LEVEL_DEFAULT;
  let officeSceneMode = "town";
  // main dropped its own selectedLandmark when the floating landmark labels went
  // away (adhoc #243); the Office still tracks it to frame the camera on entry.
  let selectedLandmark = "";
  let officeLocalParticipantId = "";
  let lastOfficeMovementEmit = 0;
  let officeWasMoving = false;
  let officeExitPending = false;
  let officeDoorwayEntryPending = false;
  let officeDoorwayEntryArmed = true;
  let officeKeypadFocused = false;
  let officeKeypadFocusedLocation = "exterior";
  let officeExitHandler = null;
  let officeKeypadHandler = null;
  const weather = createWeather(THREE, scene);

  function updateWorldEnvironment() {
    const state = {
      background: new THREE.Color(DAYLIGHT_ENVIRONMENT.background),
      fog: new THREE.Color(DAYLIGHT_ENVIRONMENT.fog),
      hemiSky: new THREE.Color(DAYLIGHT_ENVIRONMENT.hemiSky),
      hemiGround: new THREE.Color(DAYLIGHT_ENVIRONMENT.hemiGround),
      sun: new THREE.Color(DAYLIGHT_ENVIRONMENT.sun),
      sunPower: DAYLIGHT_ENVIRONMENT.sunPower,
      hemiPower: DAYLIGHT_ENVIRONMENT.hemiPower,
      exposure: DAYLIGHT_ENVIRONMENT.exposure,
      fogDensity: DAYLIGHT_ENVIRONMENT.fogDensity,
    };
    const overlay = LOCAL_ENVIRONMENT_OVERLAYS[currentTheme];
    if (overlay) {
      const tint = new THREE.Color(overlay.tint);
      state.background.lerp(tint, overlay.tintStrength);
      state.fog.lerp(tint, overlay.tintStrength * 0.72);
      state.hemiSky.lerp(tint, overlay.tintStrength * 0.42);
      state.sun.lerp(tint, overlay.tintStrength * 0.18);
    }
    scene.background.copy(state.background);
    scene.fog.color.copy(state.fog);
    environmentFogDensity =
      state.fogDensity * (overlay?.fogMultiplier || 1);
    scene.fog.density = environmentFogDensity * zoomFogMultiplier();
    const lightMultiplier = lightLevel / LIGHT_LEVEL_DEFAULT;
    hemisphere.color.copy(state.hemiSky);
    hemisphere.groundColor.copy(state.hemiGround);
    hemisphere.intensity =
      state.hemiPower * (overlay?.lightMultiplier || 1) * lightMultiplier;
    sun.color.copy(state.sun);
    sun.intensity =
      state.sunPower * (overlay?.lightMultiplier || 1) * lightMultiplier;
    renderer.toneMappingExposure =
      state.exposure *
      (overlay?.exposureMultiplier || 1) *
      lightMultiplier;
  }

  function setTheme(theme) {
    currentTheme =
      theme === "world" || LOCAL_ENVIRONMENT_OVERLAYS[theme]
        ? theme
        : "world";
    weather.rain.visible = currentTheme === "rain";
    weather.snow.visible = currentTheme === "snow" || currentTheme === "winter";
    updateWorldEnvironment();
  }

  function zoomFogMultiplier() {
    // At the strategic overview distance, atmospheric fog would wash the
    // whole world into a pale blur. Preserve local depth, but make the far
    // view readable across the bounded overview range.
    const normalized = clamp(
      (cameraZoom - 1) / Math.max(0.001, CAMERA_ZOOM_MAX - 1),
      0,
      1,
    );
    if (normalized >= 0.45) return 0;
    return 1 - normalized / 0.45;
  }

  function setLightLevel(value) {
    const numeric = Number(value);
    lightLevel = clamp(
      Number.isFinite(numeric) ? numeric : LIGHT_LEVEL_DEFAULT,
      LIGHT_LEVEL_MIN,
      LIGHT_LEVEL_MAX,
    );
    updateWorldEnvironment();
    return lightLevel;
  }

  function baseMoveSpeed() {
    return PLAYER_SPEED * moveSpeedScale;
  }

  function setMovementTuning(tuning = {}) {
    if (tuning.speed !== undefined) {
      const numeric = Number(tuning.speed);
      moveSpeedScale = Number.isFinite(numeric) && numeric > 0 ? numeric : 1;
    }
    if (tuning.acceleration !== undefined) {
      const numeric = Number(tuning.acceleration);
      // A non-finite (Infinity) or huge value means "instant" — snap to top
      // speed the moment a movement key goes down.
      moveAccelScale = Number.isFinite(numeric)
        ? Math.max(0, numeric)
        : Infinity;
    }
    // Keep the live speed within the new ceiling so a lowered cap takes effect
    // immediately rather than only after the player stops and restarts.
    keyboardMovementSpeed = Math.min(
      keyboardMovementSpeed,
      PLAYER_MAX_SPEED * moveSpeedScale,
    );
    return { speed: moveSpeedScale, acceleration: moveAccelScale };
  }

  function cancelDash() {
    dashTarget = null;
  }

  function setOfficeExitHandler(handler) {
    officeExitHandler = typeof handler === "function" ? handler : null;
  }

  function setOfficeDoorwayEntryPending(pending = false) {
    officeDoorwayEntryPending = pending === true;
    return officeDoorwayEntryPending;
  }

  function setOfficeKeypadHandler(handler) {
    officeKeypadHandler = typeof handler === "function" ? handler : null;
  }

  function setOfficeKeypadDigits(
    value = "",
    mode = "entry",
    location = officeKeypadFocusedLocation,
  ) {
    const office = landmarkObjects.get("office");
    const safeLocation = location === "interior" ? "interior" : "exterior";
    const keypad =
      safeLocation === "interior"
        ? officeInteriorKeypad
        : office?.userData?.officeKeypad;
    const display =
      keypad?.userData?.officeKeypadDisplay ||
      office?.userData?.officeKeypadDisplay;
    const digits = String(value || "").replace(/\D/g, "").slice(0, 4);
    const safeMode =
      mode === "set" ? "set" : mode === "guide" ? "guide" : "entry";
    if (!keypad || !display?.material) return digits;
    if (
      keypad.userData.officeKeypadDigits === digits &&
      keypad.userData.officeKeypadMode === safeMode
    ) {
      return digits;
    }
    display.material.map?.dispose?.();
    display.material.map = officeKeypadDisplayTexture(
      THREE,
      digits,
      safeMode,
    );
    display.material.needsUpdate = true;
    keypad.userData.officeKeypadDigits = digits;
    keypad.userData.officeKeypadMode = safeMode;
    if (safeLocation === "exterior" && office) {
      office.userData.officeKeypadDigits = digits;
      office.userData.officeKeypadMode = safeMode;
    }
    return digits;
  }

  function focusOfficeKeypad(mode = "entry", location = "exterior") {
    const safeLocation = location === "interior" ? "interior" : "exterior";
    if (
      (safeLocation === "exterior" && officeSceneMode !== "town") ||
      (safeLocation === "interior" && officeSceneMode === "town")
    ) {
      return false;
    }
    const office = landmarkObjects.get("office");
    const keypad =
      safeLocation === "interior"
        ? officeInteriorKeypad
        : office?.userData?.officeKeypad;
    if (!office || !keypad) return false;
    officeKeypadFocused = true;
    officeKeypadFocusedLocation = safeLocation;
    setOfficeKeypadDigits("", mode, safeLocation);
    if (safeLocation === "interior") {
      cancelDash();
      selectedLandmark = "office";
      cameraFocus = keypad.position.clone();
      cameraYaw = Math.PI;
      cameraPitch = 0.24;
      cameraZoom = Math.min(cameraZoom, 0.64);
      return true;
    }
    office.updateMatrixWorld(true);
    const position = new THREE.Vector3();
    keypad.getWorldPosition(position);
    cancelDash();
    selectedLandmark = "office";
    focusedRepositoryKey = "";
    currentSpace = "town-square";
    currentFloorY = 0.38;
    player.position.set(position.x, currentFloorY, position.z + 1.34);
    player.rotation.y = Math.PI;
    cameraYaw = 0;
    firstPersonPitch = 0.39;
    setCameraMode("first-person");
    lastPosition.copy(player.position);
    onMovement({
      x: Number(player.position.x.toFixed(2)),
      y: Number(player.position.y.toFixed(2)),
      z: Number(player.position.z.toFixed(2)),
      heading: Number(player.rotation.y.toFixed(3)),
      activity: "visiting the ForkMesh Office entrance",
      space: currentSpace,
      moving: false,
    });
    return true;
  }

  function blurOfficeKeypad() {
    if (!officeKeypadFocused) {
      setOfficeKeypadDigits("", "entry", officeKeypadFocusedLocation);
      return false;
    }
    const previousLocation = officeKeypadFocusedLocation;
    officeKeypadFocused = false;
    setOfficeKeypadDigits(
      "",
      previousLocation === "interior" ? "set" : "entry",
      previousLocation,
    );
    officeKeypadFocusedLocation = "exterior";
    if (previousLocation === "interior") {
      cameraFocus = new THREE.Vector3(0, 2.15, 0);
      cameraYaw = 0;
      cameraPitch = 0.48;
      return true;
    }
    setCameraMode("third-person");
    const office = landmarkById("office");
    selectedLandmark = "office";
    cameraFocus = new THREE.Vector3(
      office.position[0],
      2.5,
      office.position[2],
    );
    return true;
  }

  function constrainTownOfficeWalls(previousPosition) {
    if (officeSceneMode !== "town") return false;
    const office = landmarkObjects.get("office");
    if (!office) return false;
    const localX = player.position.x - office.position.x;
    const localZ = player.position.z - office.position.z;
    const halfWidth = OFFICE_WIDTH / 2 + OFFICE_AVATAR_RADIUS;
    const halfDepth = OFFICE_DEPTH / 2 + OFFICE_AVATAR_RADIUS;
    if (Math.abs(localX) >= halfWidth || Math.abs(localZ) >= halfDepth) {
      return false;
    }

    const previousX = previousPosition.x - office.position.x;
    const previousZ = previousPosition.z - office.position.z;
    const doorClearance = OFFICE_DOOR_WIDTH / 2 - OFFICE_AVATAR_RADIUS;
    const doorwayThreshold = OFFICE_FRONT_Z + OFFICE_AVATAR_RADIUS;
    if (
      localZ > doorwayThreshold + 0.32 &&
      localZ > previousZ + 0.01
    ) {
      officeDoorwayEntryArmed = true;
    }
    const crossedDoorway =
      Math.abs(localX) <= doorClearance &&
      previousZ >= doorwayThreshold &&
      localZ < doorwayThreshold;
    // The player physically crosses the threshold first, but remains outside
    // until the server admission promise resolves. The armed/pending pair
    // prevents a held movement key from posting once per animation frame.
    if (
      Math.abs(localX) <= doorClearance &&
      previousZ >= OFFICE_FRONT_Z
    ) {
      player.position.z = office.position.z + doorwayThreshold;
      cancelDash();
      if (
        crossedDoorway &&
        officeDoorwayEntryArmed &&
        !officeDoorwayEntryPending
      ) {
        officeDoorwayEntryArmed = false;
        officeDoorwayEntryPending = true;
        onOfficeEnter({
          source: "doorway",
          occupied: Boolean(office.userData.officeOccupied),
          available: office.userData.officeAvailable !== false,
        });
      }
      return true;
    }

    const candidates = [
      {
        distance: Math.abs(previousX + halfWidth),
        x: office.position.x - halfWidth,
        z: player.position.z,
      },
      {
        distance: Math.abs(previousX - halfWidth),
        x: office.position.x + halfWidth,
        z: player.position.z,
      },
      {
        distance: Math.abs(previousZ + halfDepth),
        x: player.position.x,
        z: office.position.z - halfDepth,
      },
      {
        distance: Math.abs(previousZ - halfDepth),
        x: player.position.x,
        z: office.position.z + halfDepth,
      },
    ].sort((left, right) => left.distance - right.distance);
    player.position.x = candidates[0].x;
    player.position.z = candidates[0].z;
    cancelDash();
    return true;
  }

  function constrainOfficeInteriorWalls(avatar) {
    if (!avatar) return false;
    avatar.position.x = clamp(
      avatar.position.x,
      -OFFICE_WIDTH / 2 + OFFICE_AVATAR_RADIUS,
      OFFICE_WIDTH / 2 - OFFICE_AVATAR_RADIUS,
    );
    avatar.position.z = Math.max(
      -OFFICE_FRONT_Z + OFFICE_AVATAR_RADIUS,
      avatar.position.z,
    );
    const doorClearance = OFFICE_DOOR_WIDTH / 2 - OFFICE_AVATAR_RADIUS;
    if (
      avatar.position.z > OFFICE_INTERIOR_WALL_LIMIT &&
      Math.abs(avatar.position.x) > doorClearance
    ) {
      avatar.position.z = OFFICE_INTERIOR_WALL_LIMIT;
    }
    if (
      avatar.position.z >= OFFICE_INTERIOR_EXIT_Z &&
      Math.abs(avatar.position.x) <= doorClearance
    ) {
      avatar.position.z = OFFICE_INTERIOR_EXIT_Z;
      if (!officeExitPending && officeExitHandler) {
        officeExitPending = true;
        officeExitHandler();
      }
      return true;
    }
    avatar.position.z = Math.min(avatar.position.z, OFFICE_INTERIOR_EXIT_Z);
    return false;
  }

  function setOfficeOccupancy({ occupied = false, available = true } = {}) {
    const office = landmarkObjects.get("office");
    if (!office) return { occupied: false, available: false };
    const nextState = {
      occupied: Boolean(occupied),
      available: available !== false,
    };
    office.userData.officeOccupied = nextState.occupied;
    office.userData.officeAvailable = nextState.available;

    const doorPivot = office.userData.officeDoorPivot;
    const doorOpen = nextState.available && !nextState.occupied;
    if (doorPivot) doorPivot.rotation.y = doorOpen ? Math.PI / 2 : 0;

    const statusColor = !nextState.available
      ? "#f7c96b"
      : nextState.occupied
        ? "#ff6f7f"
        : "#63e6a5";
    const statusEmissive = !nextState.available
      ? "#c67c24"
      : nextState.occupied
        ? "#d73b50"
        : "#31b978";
    const keypadLight = office.userData.officeKeypadLight;
    if (keypadLight?.material) {
      keypadLight.material.color.set(statusColor);
      keypadLight.material.emissive.set(statusEmissive);
    }
    const keypadBody = office.userData.officeKeypadBody;
    if (keypadBody?.material) {
      keypadBody.material.color.set(
        !nextState.available
          ? "#443514"
          : nextState.occupied
            ? "#4b151c"
            : "#123b2c",
      );
      keypadBody.material.emissive.set(statusEmissive);
      keypadBody.material.emissiveIntensity = 0.28;
    }
    return { ...nextState };
  }

  function nearestLandmark() {
    if (
      !["town-square", "east", "central", "west"].includes(currentSpace)
    ) {
      if (officeZoneState !== "distant") {
        officeZoneState = "distant";
        onOfficeProximity(officeZoneState);
      }
      return;
    }
    let nearest = null;
    let distance = Infinity;
    LANDMARKS.forEach((landmark) => {
      const next = Math.hypot(
        player.position.x - landmark.position[0],
        player.position.z - landmark.position[2],
      );
      if (next < distance) {
        nearest = landmark;
        distance = next;
      }
    });
    const office = landmarkById("office");
    const officeEntranceDistance = Math.hypot(
      player.position.x - office.position[0],
      player.position.z - (office.position[2] + OFFICE_FRONT_Z),
    );
    const nextOfficeState = nextOfficeZoneState(
      officeZoneState,
      officeEntranceDistance,
    );
    if (nextOfficeState !== officeZoneState) {
      officeZoneState = nextOfficeState;
      onOfficeProximity(officeZoneState);
    }
    let nearestRepository = null;
    let repositoryDistance = Infinity;
    repositoryPortals.forEach((record) => {
      const next = Math.hypot(
        player.position.x - record.group.position.x,
        player.position.z - record.group.position.z,
      );
      if (next < repositoryDistance) {
        nearestRepository = record;
        repositoryDistance = next;
      }
    });
    const nextLocation =
      repositoryDistance < 4.8
        ? `${nearestRepository.owner}/${nearestRepository.name}`
        : distance < 7.5
          ? nearest.label
          : "Town Square";
    const nextLocationId =
      repositoryDistance < 4.8
        ? "repositories"
        : distance < 7.5
          ? nearest?.id || ""
          : "";
    if (nextLocation !== currentLocation) {
      currentLocation = nextLocation;
      onLocationChange(nextLocation, nextLocationId);
    }
    const nextRegion =
      player.position.x > 12
        ? "east"
        : player.position.x < -12
          ? "west"
          : "central";
    if (nextRegion !== currentRegion) {
      currentRegion = nextRegion;
      const region = WORLD_REGIONS.find((item) => item.id === currentRegion);
      onRegionChange(region || { id: currentRegion, label: currentRegion });
    }
  }

  function updateRepositoryPortalLabels() {
    let nearestKey = "";
    let nearestDistance = Infinity;
    repositoryPortals.forEach((record, key) => {
      const distance = Math.hypot(
        player.position.x - record.group.position.x,
        player.position.z - record.group.position.z,
      );
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearestKey = key;
      }
    });
    repositoryPortals.forEach((record, key) => {
      const label = record.group.userData.repositoryLabel;
      if (!label) return;
      label.visible =
        key === focusedRepositoryKey ||
        record.group === world.userData.repositorySizeMount ||
        (key === nearestKey && nearestDistance < 9.5);
    });
  }

  function travelToRegion(regionId) {
    const destinations = {
      east: new THREE.Vector3(27, 0.38, 2),
      central: new THREE.Vector3(0, 0.38, 10),
      west: new THREE.Vector3(-27, 0.38, 2),
    };
    const destination = destinations[regionId];
    if (!destination) return false;
    currentSpace = regionId;
    currentFloorY = 0.38;
    player.position.copy(destination);
    cameraFocus = null;
    cancelDash();
    focusedRepositoryKey = "";
    nearestLandmark();
    onMovement({
      x: destination.x,
      y: destination.y,
      z: destination.z,
      heading: player.rotation.y,
      activity: `arriving in ${regionId} campus`,
      space: regionId,
      moving: false,
    });
    return true;
  }

  function focusLandmark(id) {
    const landmark = landmarkById(id);
    const object = landmarkObjects.get(landmark.id);
    if (!object) return;
    currentSpace = "town-square";
    currentFloorY = 0.38;
    player.position.y = currentFloorY;
    cancelDash();
    focusedRepositoryKey = "";
    cameraFocus = new THREE.Vector3(
      landmark.position[0],
      1.5,
      landmark.position[2],
    );
  }

  function enterOffice() {
    if (officeZoneState !== "nearby") return false;
    const office = landmarkById("office");
    selectedLandmark = "office";
    officeKeypadFocused = false;
    officeExitPending = false;
    officeDoorwayEntryPending = false;
    currentSpace = "town-square";
    currentFloorY = 0.38;
    cameraFocus = new THREE.Vector3(
      office.position[0],
      2.5,
      office.position[2] + OFFICE_FRONT_Z,
    );
    player.position.set(
      office.position[0],
      currentFloorY,
      office.position[2] + OFFICE_FRONT_Z + 0.72,
    );
    player.rotation.y = Math.PI;
    if (reducedMotion) {
      const target = cameraFocus.clone();
      camera.position.copy(target.clone().add(
        new THREE.Vector3(9.5, 8.2, 11.5),
      ));
      camera.lookAt(target);
    }
    return true;
  }

  function enterOfficeLobby() {
    // The Office uses its own shared orbital framing and participant model.
    // Never leave the town avatar's eye camera active behind that scene.
    setCameraMode("third-person");
    const enteringFromTown = officeSceneMode === "town";
    officeSceneMode = "lobby";
    selectedLandmark = "office";
    currentSpace = "office";
    // No click-to-move target to clear -- main removed that control, and pull/46
    // already applied the same fix to enterOffice().
    cameraFocus = new THREE.Vector3(0, 2.15, 0);
    cameraYaw = 0;
    cameraPitch = 0.48;
    cameraZoom = Math.min(1, Math.max(0.58, cameraZoom));
    world.visible = false;
    officeInterior.visible = true;
    officeInteriorDoorPivot.rotation.y = Math.PI / 2;
    if (enteringFromTown) {
      officeLobbyPlayer.position.set(0, 0.38, 4.62);
      officeLobbyPlayer.rotation.y = Math.PI;
    } else {
      const localParticipant = officeParticipants.get(
        officeLocalParticipantId,
      );
      if (localParticipant) {
        officeLobbyPlayer.position.copy(localParticipant.position);
        officeLobbyPlayer.position.y = 0.38;
        officeLobbyPlayer.rotation.y = localParticipant.rotation.y;
      }
    }
    officeLobbyPlayer.visible = true;
    officeExitPending = false;
    scene.background.set("#0d1e19");
    scene.fog.color.set("#0d1e19");
    return true;
  }

  function officeChairTransform(chairId) {
    const chair = officeChairs.get(String(chairId || ""));
    if (!chair) return null;
    return {
      position: chair.position.clone(),
      yaw: chair.rotation.y,
    };
  }

  function applyOfficeParticipantPose(avatar, participant) {
    const seated = participant?.pose === "seated" && participant?.chairId;
    const chair = seated ? officeChairTransform(participant.chairId) : null;
    if (chair) {
      avatar.position.copy(chair.position);
      avatar.position.y = 0.72;
      avatar.rotation.y = chair.yaw + Math.PI;
      avatar.userData.leftLeg.rotation.x = -1.3;
      avatar.userData.rightLeg.rotation.x = -1.3;
    } else {
      const seed = hashNumber(participant?.id || "office-participant");
      avatar.position.set(
        clamp(Number(participant?.x) || ((seed % 7) - 3) * 0.55, -6.8, 6.8),
        0.38,
        clamp(Number(participant?.z) || 4.15 + ((seed >> 3) % 3) * 0.35, -4.7, 4.7),
      );
      avatar.rotation.y = Number(participant?.yaw) || Math.PI;
      avatar.userData.leftLeg.rotation.x = 0;
      avatar.userData.rightLeg.rotation.x = 0;
    }
    avatar.userData.leftArm.rotation.x = 0;
    avatar.userData.rightArm.rotation.x = 0;
    avatar.userData.officePresence = { ...participant };
  }

  function setOfficeParticipants(participants = []) {
    const seenOfficeParticipants = new Set();
    participants.forEach((participant) => {
      const id = String(participant?.id || "");
      if (!id) return;
      seenOfficeParticipants.add(id);
      let avatar = officeParticipants.get(id);
      const participantIdentity = {
        id,
        name: String(participant.name || "Office visitor").slice(0, 32),
        flag: "◌",
        browser: "Browser",
        os: "Device",
        status: participant.pose === "seated" ? "seated" : "in meeting",
        accountStatus: participant.accountStatus || "Guest",
        nodes: [],
      };
      if (!avatar) {
        avatar = createAvatar(THREE, participantIdentity, {
          remote: true,
          scale: 0.9,
        });
        officeInterior.add(avatar);
        officeParticipants.set(id, avatar);
        officeParticipantLabels.set(
          id,
          makePlayerLabel(avatar, labelLayer),
        );
      } else if (avatar.userData.name !== participantIdentity.name) {
        updateAvatarBadge(THREE, avatar, participantIdentity, true);
        officeParticipantLabels.get(id).textContent = participantIdentity.name;
      }
      applyOfficeParticipantPose(avatar, participant);
    });
    officeParticipants.forEach((avatar, id) => {
      if (seenOfficeParticipants.has(id)) return;
      officeInterior.remove(avatar);
      avatar.traverse((child) => {
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
      officeParticipants.delete(id);
      officeParticipantLabels.get(id)?.remove();
      officeParticipantLabels.delete(id);
      officeBubbles.get(id)?.remove();
      officeBubbles.delete(id);
    });
    officeLobbyPlayer.visible =
      officeSceneMode !== "town" &&
      (!officeLocalParticipantId ||
        !officeParticipants.has(officeLocalParticipantId));
  }

  function updateOfficeMarketingTasks(payload = {}) {
    const snapshot = normalizeOfficeMarketingTasks(payload);
    const key = JSON.stringify(snapshot);
    if (officeMarketingTaskBoard.userData.taskKey === key) return snapshot;
    const face = officeMarketingTaskBoard.userData.face;
    face.material.map?.dispose?.();
    face.material.map = officeMarketingTasksTexture(THREE, snapshot);
    face.material.needsUpdate = true;
    officeMarketingTaskBoard.userData.taskKey = key;
    officeMarketingTaskBoard.userData.taskState = snapshot.state;
    officeMarketingTaskBoard.userData.taskCount = snapshot.tasks.length;
    return snapshot;
  }

  function updateWorldBulletin(events = []) {
    const face = worldBulletin.getObjectByName("forkmesh-world-bulletin-face");
    if (!face?.material) return false;
    worldBulletinEvents = Array.isArray(events) ? events : [];
    worldBulletinOffset = clamp(
      worldBulletinOffset,
      0,
      Math.max(0, worldBulletinEvents.length - 10),
    );
    face.material.map?.dispose?.();
    face.material.map = worldBulletinTexture(
      THREE,
      worldBulletinEvents,
      worldBulletinOffset,
    );
    face.material.needsUpdate = true;
    return true;
  }

  function scrollWorldBulletin(direction) {
    const nextOffset = clamp(
      worldBulletinOffset + direction,
      0,
      Math.max(0, worldBulletinEvents.length - 10),
    );
    if (nextOffset === worldBulletinOffset) return false;
    worldBulletinOffset = nextOffset;
    return updateWorldBulletin(worldBulletinEvents);
  }

  // Header art and avatars come from mastodon.social's media host. They only
  // reach the board texture when they load CORS-clean; otherwise the board
  // keeps its text-only rendering (a tainted canvas cannot feed WebGL).
  function mastodonKioskImage(url) {
    const key = String(url || "");
    if (!key) return null;
    const cached = mastodonKioskImages.get(key);
    if (cached) return cached.image;
    const record = { image: null };
    mastodonKioskImages.set(key, record);
    const image = new Image();
    image.crossOrigin = "anonymous";
    image.onload = () => {
      if (disposed) return;
      record.image = image;
      repaintMastodonKiosk();
    };
    image.src = key;
    return null;
  }

  function repaintMastodonKiosk() {
    const face = mastodonKiosk.getObjectByName("forkmesh-mastodon-kiosk-face");
    if (!face?.material) return false;
    face.material.map?.dispose?.();
    face.material.map = mastodonKioskTexture(
      THREE,
      mastodonKioskSnapshot,
      mastodonKioskOffset,
      mastodonKioskImage,
    );
    face.material.needsUpdate = true;
    return true;
  }

  function mastodonKioskMaxOffset() {
    return Math.max(
      0,
      (mastodonKioskSnapshot?.toots?.length || 0) -
        MASTODON_KIOSK_VISIBLE_TOOTS,
    );
  }

  function updateMastodonKiosk(snapshot = null) {
    mastodonKioskSnapshot =
      snapshot && typeof snapshot === "object"
        ? {
            ...snapshot,
            toots: Array.isArray(snapshot.toots)
              ? snapshot.toots.filter(Boolean)
              : [],
          }
        : null;
    mastodonKioskOffset = clamp(
      mastodonKioskOffset,
      0,
      mastodonKioskMaxOffset(),
    );
    return repaintMastodonKiosk();
  }

  function scrollMastodonKiosk(direction) {
    const nextOffset = clamp(
      mastodonKioskOffset + direction,
      0,
      mastodonKioskMaxOffset(),
    );
    if (nextOffset === mastodonKioskOffset) return false;
    mastodonKioskOffset = nextOffset;
    return repaintMastodonKiosk();
  }

  function enterOfficeMeeting({
    roomName = "general",
    participants = [],
    participantId = "",
  } = {}) {
    enterOfficeLobby();
    officeSceneMode = "meeting";
    officeLocalParticipantId = String(participantId || officeLocalParticipantId);
    officeRoomSign.userData.roomName = String(roomName || "general");
    setOfficeParticipants(participants);
    officeLobbyPlayer.visible =
      !officeLocalParticipantId ||
      !officeParticipants.has(officeLocalParticipantId);
    return true;
  }

  function setOfficeSeatState({ participantId, chairId = "", pose = "standing" } = {}) {
    const avatar = officeParticipants.get(String(participantId || ""));
    if (!avatar) return false;
    applyOfficeParticipantPose(avatar, {
      ...(avatar.userData.officePresence || {}),
      id: String(participantId),
      chairId: String(chairId || ""),
      pose: pose === "seated" ? "seated" : "standing",
    });
    return true;
  }

  function showOfficeBubble(participantId, bubble = {}) {
    const id = String(participantId || "");
    const avatar = officeParticipants.get(id);
    if (!avatar) return false;
    officeBubbles.get(id)?.remove();
    const element = document.createElement("div");
    element.className = "world-office-bubble";
    element.dataset.worldOfficeBubble = id;
    element.textContent = String(bubble.text || bubble.fileName || "Shared an attachment")
      .trim()
      .slice(0, 180);
    element.style.left = "50%";
    element.style.top = "76px";
    labelLayer.appendChild(element);
    officeBubbles.set(id, element);
    window.setTimeout(() => {
      if (officeBubbles.get(id) !== element) return;
      element.remove();
      officeBubbles.delete(id);
    }, 7000);
    return true;
  }

  function leaveOfficeInterior() {
    officeSceneMode = "town";
    officeInterior.visible = false;
    world.visible = true;
    currentSpace = "town-square";
    const office = landmarkById("office");
    player.position.set(
      office.position[0],
      0.38,
      office.position[2] + OFFICE_FRONT_Z + 0.82,
    );
    player.rotation.y = 0;
    lastPosition.copy(player.position);
    officeLocalParticipantId = "";
    officeWasMoving = false;
    officeExitPending = false;
    officeDoorwayEntryPending = false;
    officeLobbyPlayer.visible = false;
    cameraFocus = null;
    setOfficeParticipants([]);
    officeBubbles.forEach((element) => element.remove());
    officeBubbles.clear();
    // Re-apply the theme to restore the town background/fog the Office overrode.
    // PR #47 called applyTheme(); main's equivalent is setTheme().
    setTheme(currentTheme);
    onMovement({
      x: Number(player.position.x.toFixed(2)),
      y: 0.38,
      z: Number(player.position.z.toFixed(2)),
      heading: Number(player.rotation.y.toFixed(3)),
      activity: "exploring the Town Square",
      space: "town-square",
      moving: false,
    });
  }

  function beginOfficeExit() {
    if (officeSceneMode === "town") return false;
    officeExitPending = false;
    officeInteriorDoorPivot.rotation.y = Math.PI / 2;
    cameraYaw = Math.PI;
    cameraPitch = 0.42;
    cameraFocus = new THREE.Vector3(0, 2.05, 2.8);
    const avatar =
      officeSceneMode === "meeting"
        ? officeParticipants.get(officeLocalParticipantId)
        : officeLobbyPlayer;
    if (avatar && avatar.userData?.officePresence?.pose !== "seated") {
      avatar.rotation.y = 0;
    }
    return true;
  }

  function setCameraMode(mode) {
    const wantsFirstPerson =
      mode === true ||
      String(mode || "").trim().toLocaleLowerCase() === "first-person";
    // First person follows the Town avatar. The Office has a separate local
    // participant and camera target, so reject that mode while its scene is open.
    const nextMode =
      wantsFirstPerson && officeSceneMode === "town"
        ? "first-person"
        : "third-person";
    if (nextMode !== cameraMode) cancelDash();
    cameraMode = nextMode;
    if (cameraMode === "first-person") {
      cameraFocus = null;
      player.visible = false;
      playerLabel.style.opacity = "0";
      playerLabel.style.visibility = "hidden";
    } else {
      player.visible = true;
      camera.fov = 44;
      camera.updateProjectionMatrix();
    }
    renderer.domElement.dataset.cameraMode = cameraMode;
    return cameraMode;
  }

  function focusRepositoryPortal(owner, name) {
    const key = `${String(owner || "").toLocaleLowerCase()}/${String(
      name || "",
    ).toLocaleLowerCase()}`;
    const record = repositoryPortals.get(key);
    if (!record?.group) return false;
    currentSpace = "town-square";
    currentFloorY = 0.38;
    player.position.y = currentFloorY;
    cancelDash();
    cameraFocus = record.group.position.clone();
    cameraFocus.y += 0.1;
    focusedRepositoryKey = key;
    // Every perimeter portal faces toward the world center. Put the camera on
    // that inward normal so the selected sunburst is presented straight-on.
    cameraYaw = -record.angle - Math.PI / 2;
    cameraPitch = 0.16;
    cameraZoom = Math.min(cameraZoom, 0.55);
    return true;
  }

  function enterRepositoryFirstPerson(owner, name) {
    if (officeSceneMode !== "town") return false;
    const key = `${String(owner || "").toLocaleLowerCase()}/${String(
      name || "",
    ).toLocaleLowerCase()}`;
    const record = repositoryPortals.get(key);
    if (!record?.group || !focusRepositoryPortal(owner, name)) return false;

    const portalPosition = new THREE.Vector3();
    record.group.getWorldPosition(portalPosition);
    // focusRepositoryPortal points the view from the portal's inward normal
    // toward its face. Put the actual avatar on that normal so this remains a
    // true first-person view and multiplayer peers see the same spatial visit.
    player.position.set(
      portalPosition.x +
        Math.sin(cameraYaw) * REPOSITORY_FIRST_PERSON_DISTANCE,
      currentFloorY,
      portalPosition.z +
        Math.cos(cameraYaw) * REPOSITORY_FIRST_PERSON_DISTANCE,
    );
    player.rotation.y = cameraYaw;
    jumpQueued = false;
    jumpVelocity = 0;
    keyboardMovementSpeed = baseMoveSpeed();
    lastPosition.copy(player.position);
    wasWalking = false;
    lastMovementEmit = performance.now();
    firstPersonPitch = REPOSITORY_FIRST_PERSON_PITCH;
    setCameraMode("first-person");
    focusedRepositoryKey = key;
    onMovement({
      x: Number(player.position.x.toFixed(2)),
      y: Number(player.position.y.toFixed(2)),
      z: Number(player.position.z.toFixed(2)),
      heading: Number(player.rotation.y.toFixed(3)),
      activity: "exploring a repository graph",
      space: currentSpace,
      moving: false,
    });
    return true;
  }

  function clearFocus() {
    cameraFocus = null;
    cancelDash();
    focusedRepositoryKey = "";
  }

  function setControl(control, pressed) {
    const normalized = {
      forward: "KeyW",
      back: "KeyS",
      left: "KeyA",
      right: "KeyD",
    }[control];
    if (!normalized) return;
    if (pressed) touchKeys.add(normalized);
    else touchKeys.delete(normalized);
  }

  function setTouchMovement(x, y) {
    const nextX = Number(x);
    const nextY = Number(y);
    if (!Number.isFinite(nextX) || !Number.isFinite(nextY)) {
      touchMovement.set(0, 0);
      return { x: 0, y: 0, strength: 0 };
    }
    const length = Math.hypot(nextX, nextY);
    const scale = length > 1 ? 1 / length : 1;
    touchMovement.set(nextX * scale, nextY * scale);
    if (touchMovement.lengthSq() < 0.0001) touchMovement.set(0, 0);
    return {
      x: touchMovement.x,
      y: touchMovement.y,
      strength: touchMovement.length(),
    };
  }

  function movementInput() {
    const movement = new THREE.Vector3();
    const forwardInput =
      Number(keys.has("KeyW") || keys.has("ArrowUp")) -
      Number(keys.has("KeyS") || keys.has("ArrowDown"));
    const rightInput =
      Number(keys.has("KeyD") || keys.has("ArrowRight")) -
      Number(keys.has("KeyA") || keys.has("ArrowLeft"));
    const touchStrength = Math.min(1, touchMovement.length());
    const forward = new THREE.Vector3(
      -Math.sin(cameraYaw),
      0,
      -Math.cos(cameraYaw),
    );
    const right = new THREE.Vector3(
      Math.cos(cameraYaw),
      0,
      -Math.sin(cameraYaw),
    );
    if (forwardInput || rightInput || touchStrength) {
      // Keyboard and analog movement follow the view direction. Only yaw
      // participates, so looking up or down never changes walking speed.
      movement.addScaledVector(forward, forwardInput);
      movement.addScaledVector(right, rightInput);
      movement.addScaledVector(forward, -touchMovement.y);
      movement.addScaledVector(right, touchMovement.x);
    }

    // Retain the programmatic directional API for older controllers, but the
    // coarse-pointer UI now uses the proportional analog vector above.
    if (touchKeys.has("KeyW")) movement.z -= 1;
    if (touchKeys.has("KeyS")) movement.z += 1;
    if (touchKeys.has("KeyA")) movement.x -= 1;
    if (touchKeys.has("KeyD")) movement.x += 1;
    const keyboardActive = Boolean(forwardInput || rightInput);
    const legacyTouchActive = touchKeys.size > 0;
    const inputStrength =
      keyboardActive || legacyTouchActive ? 1 : touchStrength;
    return {
      keyboardActive,
      touchStrength,
      inputStrength,
      movement: movement.lengthSq() ? movement.normalize() : movement,
    };
  }

  function walkOfficeParticipant(delta, time) {
    const avatar = officeParticipants.get(officeLocalParticipantId);
    if (!avatar) return;
    const presence = avatar.userData.officePresence || {};
    const input = presence.pose === "seated"
      ? { movement: new THREE.Vector3(), inputStrength: 0 }
      : movementInput();
    const movement = input.movement;
    const walking = movement.lengthSq() > 0;
    if (walking) {
      avatar.position.addScaledVector(
        movement,
        PLAYER_SPEED * 0.72 * Math.max(0, input.inputStrength) * delta,
      );
      avatar.position.y = 0.38;
      avatar.rotation.y = Math.atan2(movement.x, movement.z);
      if (constrainOfficeInteriorWalls(avatar)) return;
    }
    const gait = walking ? Math.sin(time * 0.012) * 0.48 : 0;
    avatar.userData.leftArm.rotation.x = gait;
    avatar.userData.rightArm.rotation.x = -gait;
    avatar.userData.leftLeg.rotation.x = -gait * 0.72;
    avatar.userData.rightLeg.rotation.x = gait * 0.72;
    avatar.userData.officePresence = {
      ...presence,
      x: avatar.position.x,
      y: 0.38,
      z: avatar.position.z,
      yaw: avatar.rotation.y,
      moving: walking,
    };
    const shouldEmit = walking
      ? performance.now() - lastOfficeMovementEmit >= 120
      : officeWasMoving;
    if (shouldEmit) {
      lastOfficeMovementEmit = performance.now();
      onOfficeMovement({
        x: Number(avatar.position.x.toFixed(2)),
        y: 0.38,
        z: Number(avatar.position.z.toFixed(2)),
        yaw: Number(avatar.rotation.y.toFixed(3)),
        moving: walking,
      });
    }
    officeWasMoving = walking;
  }

  function walkOfficeLobbyPlayer(delta, time) {
    const input = movementInput();
    const movement = input.movement;
    const walking = movement.lengthSq() > 0;
    if (walking) {
      officeLobbyPlayer.position.addScaledVector(
        movement,
        PLAYER_SPEED * 0.72 * Math.max(0, input.inputStrength) * delta,
      );
      officeLobbyPlayer.position.y = 0.38;
      officeLobbyPlayer.rotation.y = Math.atan2(movement.x, movement.z);
      if (constrainOfficeInteriorWalls(officeLobbyPlayer)) return;
    }
    const gait = walking ? Math.sin(time * 0.012) * 0.48 : 0;
    officeLobbyPlayer.userData.leftArm.rotation.x = gait;
    officeLobbyPlayer.userData.rightArm.rotation.x = -gait;
    officeLobbyPlayer.userData.leftLeg.rotation.x = -gait * 0.72;
    officeLobbyPlayer.userData.rightLeg.rotation.x = gait * 0.72;
    animateAvatarActivity(officeLobbyPlayer, time, delta, reducedMotion);
  }

  // Sitting is a local pose, not a teleport: the avatar lands on the plank it
  // was clicked on, faces the flames and keeps that pose until it moves again.
  function sitOnCampfireBench(seat) {
    const seatPoint = seat.getWorldPosition(new THREE.Vector3());
    benchSeat = {
      // Just above the plank so the avatar rests on the seat rather than in it.
      position: new THREE.Vector3(seatPoint.x, seatPoint.y + 0.1, seatPoint.z),
      // Face the flames: headings elsewhere use atan2(-dx, -dz), so pointing at
      // the pit means negating the seat -> campfire vector.
      heading: Math.atan2(
        seatPoint.x - campfire.position.x,
        seatPoint.z - campfire.position.z,
      ),
    };
    cancelDash();
    cameraFocus = null;
    jumpVelocity = 0;
    jumpQueued = false;
    applyBenchSeatPose();
    lastPosition.copy(player.position);
    onMovement({
      x: Number(player.position.x.toFixed(2)),
      y: Number(player.position.y.toFixed(2)),
      z: Number(player.position.z.toFixed(2)),
      heading: Number(player.rotation.y.toFixed(3)),
      space: currentSpace,
      moving: false,
      activity: CAMPFIRE_SEATED_ACTIVITY,
    });
  }

  function applyBenchSeatPose() {
    if (!benchSeat) return;
    player.position.copy(benchSeat.position);
    player.rotation.y = benchSeat.heading;
    player.userData.leftArm.rotation.x = 0;
    player.userData.rightArm.rotation.x = 0;
    player.userData.leftLeg.rotation.x = -1.3;
    player.userData.rightLeg.rotation.x = -1.3;
  }

  function standUpFromBench() {
    if (!benchSeat) return;
    benchSeat = null;
    player.position.y = currentFloorY;
    player.userData.leftLeg.rotation.x = 0;
    player.userData.rightLeg.rotation.x = 0;
  }

  function walkPlayer(delta, time) {
    const previousHorizontalPosition = player.position.clone();
    const {
      keyboardActive,
      touchStrength,
      inputStrength,
      movement,
    } = movementInput();
    if (benchSeat) {
      // Anything that relocates the avatar (a space change, a teleport) also
      // ends the sit; otherwise the held pose would drag it back to the bench.
      const displaced =
        player.position.distanceToSquared(benchSeat.position) > 9;
      if (!displaced && !movement.lengthSq() && !dashTarget && !jumpQueued) {
        applyBenchSeatPose();
        animateAvatarActivity(player, time, delta, reducedMotion);
        wasWalking = false;
        return;
      }
      standUpFromBench();
    }
    if (jumpQueued && player.position.y <= currentFloorY + 0.02) {
      jumpVelocity = 5.4;
      jumpQueued = false;
    }
    jumpVelocity -= 14 * delta;
    player.position.y += jumpVelocity * delta;
    if (player.position.y <= currentFloorY) {
      player.position.y = currentFloorY;
      jumpVelocity = 0;
    }
    let walking = false;
    if (movement.lengthSq()) {
      cameraFocus = null;
      // Any manual input takes the wheel back from a double-click dash.
      cancelDash();
      focusedRepositoryKey = "";
      const topSpeed = PLAYER_MAX_SPEED * moveSpeedScale;
      // Infinite acceleration collapses the ramp: keyboardMovementSpeed jumps to
      // topSpeed on the first press instead of easing up over several frames.
      keyboardMovementSpeed =
        keyboardActive
          ? Math.min(
              topSpeed,
              keyboardMovementSpeed + PLAYER_ACCELERATION * moveAccelScale * delta,
            )
          : touchStrength > 0
            ? topSpeed * inputStrength
            : baseMoveSpeed();
      player.position.addScaledVector(
        movement,
        keyboardMovementSpeed * delta,
      );
      player.rotation.y = Math.atan2(-movement.x, -movement.z);
      walking = true;
    } else if (dashTarget) {
      // Double-click travel: run straight at the clicked ground point, then
      // land exactly on it instead of jittering around the destination.
      const toTarget = new THREE.Vector3(
        dashTarget.x - player.position.x,
        0,
        dashTarget.z - player.position.z,
      );
      const remaining = toTarget.length();
      const step = PLAYER_DASH_SPEED * moveSpeedScale * delta;
      if (remaining <= Math.max(step, PLAYER_DASH_ARRIVE_DISTANCE)) {
        player.position.x = dashTarget.x;
        player.position.z = dashTarget.z;
        cancelDash();
      } else {
        toTarget.divideScalar(remaining);
        player.position.x += toTarget.x * step;
        player.position.z += toTarget.z * step;
        player.rotation.y = Math.atan2(-toTarget.x, -toTarget.z);
      }
      keyboardMovementSpeed = baseMoveSpeed();
      walking = true;
    } else {
      keyboardMovementSpeed = baseMoveSpeed();
    }

    const radius = Math.hypot(player.position.x, player.position.z);
    if (radius > WORLD_RADIUS) {
      player.position.x *= WORLD_RADIUS / radius;
      player.position.z *= WORLD_RADIUS / radius;
    }
    constrainTownOfficeWalls(previousHorizontalPosition);
    const gait = walking ? Math.sin(time * 0.012) * 0.52 : 0;
    player.userData.leftArm.rotation.x = gait;
    player.userData.rightArm.rotation.x = -gait;
    player.userData.leftLeg.rotation.x = -gait * 0.72;
    player.userData.rightLeg.rotation.x = gait * 0.72;
    if (jumpVelocity === 0) {
      player.position.y += walking ? Math.abs(Math.sin(time * 0.012)) * 0.035 : 0;
    }
    animateAvatarActivity(player, time, delta, reducedMotion);

    if (
      performance.now() - lastMovementEmit > 450 &&
      player.position.distanceToSquared(lastPosition) > 0.025
    ) {
      lastMovementEmit = performance.now();
      lastPosition.copy(player.position);
      onMovement({
        x: Number(player.position.x.toFixed(2)),
        y: Number(player.position.y.toFixed(2)),
        z: Number(player.position.z.toFixed(2)),
        heading: Number(player.rotation.y.toFixed(3)),
        space: currentSpace,
        moving: true,
        activity: currentLocation === "Town Square" ? "exploring the Town Square" : `visiting ${currentLocation}`,
      });
    }
    if (!walking && wasWalking) {
      lastPosition.copy(player.position);
      onMovement({
        x: Number(player.position.x.toFixed(2)),
        y: Number(player.position.y.toFixed(2)),
        z: Number(player.position.z.toFixed(2)),
        heading: Number(player.rotation.y.toFixed(3)),
        space: currentSpace,
        moving: false,
        activity:
          currentLocation === "Town Square"
            ? "exploring the Town Square"
            : `visiting ${currentLocation}`,
      });
    }
    wasWalking = walking;
  }

  function updateRemotePlayers(delta, time) {
    remotePlayers.forEach((avatar) => {
      avatar.position.lerp(avatar.userData.targetPosition, 1 - Math.pow(0.002, delta));
      let headingDelta = avatar.userData.targetHeading - avatar.rotation.y;
      headingDelta = Math.atan2(Math.sin(headingDelta), Math.cos(headingDelta));
      avatar.rotation.y += headingDelta * (1 - Math.pow(0.01, delta));
      const walking = avatar.position.distanceToSquared(avatar.userData.targetPosition) > 0.01;
      const recentlyActiveInLounge =
        avatar.userData.loungeActivity === "recent";
      const gait = walking
        ? Math.sin(time * 0.009 + avatar.userData.phase) * 0.42
        : recentlyActiveInLounge
          ? Math.sin(time * 0.012 + avatar.userData.phase) * 0.18
          : 0;
      // Members occupying a stool in the campfire circle sit like local bench
      // sitters do once they arrive at their seat.
      const seatedAtCampfire =
        (avatar.userData.campfireSeated === true ||
          Boolean(avatar.userData.loungeActivity)) &&
        !walking;
      avatar.userData.leftArm.rotation.x = gait;
      avatar.userData.rightArm.rotation.x = -gait;
      avatar.userData.leftLeg.rotation.x = seatedAtCampfire ? -1.3 : -gait * 0.7;
      avatar.userData.rightLeg.rotation.x = seatedAtCampfire ? -1.3 : gait * 0.7;
      if (recentlyActiveInLounge && !walking && !reducedMotion) {
        avatar.position.y =
          avatar.userData.targetPosition.y +
          Math.abs(Math.sin(time * 0.004 + avatar.userData.phase)) * 0.055;
      }
      animateAvatarActivity(avatar, time, delta, reducedMotion);
    });
  }

  // ForkBot wanders the Town Square on its own; when world.js reports a
  // visitor's first movement or mouse activity (greetForkbot) it walks over
  // and floats a welcome bubble instead of picking the next wander spot.
  function updateForkbot(delta, time) {
    const data = forkbot.userData;
    let target = forkbotWanderTarget;
    if (forkbotGreeting) {
      target = player.position;
      const waited = performance.now() - forkbotGreeting.startedAt;
      const reach = forkbot.position.distanceTo(player.position);
      if (
        reach <= FORKBOT_GREETING_RANGE ||
        waited >= FORKBOT_GREETING_TIMEOUT_MS
      ) {
        showChatBubble(FORKBOT_PEER_ID, forkbotGreeting.message);
        forkbotGreeting = null;
        // Linger beside the visitor for a moment before wandering off.
        forkbotWanderTarget.copy(forkbot.position);
        forkbotNextWanderAt = time + 9000;
        target = forkbotWanderTarget;
      }
    } else if (time >= forkbotNextWanderAt) {
      const angle = Math.random() * Math.PI * 2;
      const radius = 3 + Math.random() * FORKBOT_WANDER_RADIUS;
      forkbotWanderTarget.set(
        clamp(
          FORKBOT_HOME[0] + Math.cos(angle) * radius,
          -WORLD_RADIUS,
          WORLD_RADIUS,
        ),
        FORKBOT_HOME[1],
        clamp(
          FORKBOT_HOME[2] + Math.sin(angle) * radius,
          -WORLD_RADIUS,
          WORLD_RADIUS,
        ),
      );
      forkbotNextWanderAt = time + 4000 + Math.random() * 8000;
    }
    const dx = target.x - forkbot.position.x;
    const dz = target.z - forkbot.position.z;
    const distance = Math.hypot(dx, dz);
    const arrive = forkbotGreeting ? FORKBOT_GREETING_RANGE * 0.8 : 0.4;
    const walking = distance > arrive;
    if (walking) {
      const step = Math.min(distance - arrive, FORKBOT_SPEED * delta);
      forkbot.position.x += (dx / distance) * step;
      forkbot.position.z += (dz / distance) * step;
    }
    if (distance > 0.05) {
      data.targetHeading = Math.atan2(dx, dz);
    }
    let headingDelta = data.targetHeading - forkbot.rotation.y;
    headingDelta = Math.atan2(Math.sin(headingDelta), Math.cos(headingDelta));
    forkbot.rotation.y += headingDelta * (1 - Math.pow(0.01, delta));
    if (walking) {
      data.rollingBall.rotation.x -= FORKBOT_SPEED * delta * 1.8;
      data.rollingBall.rotation.z = Math.sin(time * 0.006 + data.phase) * 0.08;
    }
  }

  function greetForkbot(text) {
    const message = String(text || "")
      .replace(/\s+/g, " ")
      .trim()
      .slice(0, 140);
    if (!message || forkbotGreeting) return false;
    forkbotGreeting = { message, startedAt: performance.now() };
    return true;
  }

  function updateCamera(delta) {
    if (cameraMode === "first-person" && officeSceneMode === "town") {
      const eye = player.position
        .clone()
        .add(new THREE.Vector3(0, FIRST_PERSON_EYE_HEIGHT, 0));
      const horizontal = Math.cos(firstPersonPitch);
      const direction = new THREE.Vector3(
        -Math.sin(cameraYaw) * horizontal,
        -Math.sin(firstPersonPitch),
        -Math.cos(cameraYaw) * horizontal,
      );
      // A following eye camera must not ease behind the moving avatar. Copying
      // the position directly avoids visible lag and motion sickness.
      camera.position.copy(eye);
      camera.lookAt(eye.clone().add(direction));
      return;
    }
    const target = cameraFocus
      ? cameraFocus.clone()
      : player.position
          .clone()
          .add(new THREE.Vector3(0, FIRST_PERSON_EYE_HEIGHT, 0));
    const officeFocused = Boolean(cameraFocus && selectedLandmark === "office");
    const officeInteriorFocused = officeSceneMode !== "town";
    // PR #47's Office framing, expressed as a distance scale so the orbital
    // camera keeps its yaw/pitch/pinch-zoom control instead of being pinned to
    // a fixed offset: the interior sits closer (0.60x of the default distance),
    // the exterior landmark reads slightly wider, and narrow viewports pull
    // back far enough to fit the whole building.
    const focusScale = officeInteriorFocused
      ? 0.6
      : officeFocused
        ? camera.aspect < 0.75 ? 1.36 : 0.87
        : cameraFocus
          ? 0.78
          : 1;
    const distance = CAMERA_DISTANCE * cameraZoom * focusScale;
    const horizontalDistance = Math.cos(cameraPitch) * distance;
    const desired = target.clone().add(
      new THREE.Vector3(
        Math.sin(cameraYaw) * horizontalDistance,
        Math.sin(cameraPitch) * distance,
        Math.cos(cameraYaw) * horizontalDistance,
      ),
    );
    if (reducedMotion) camera.position.copy(desired);
    else camera.position.lerp(desired, 1 - Math.pow(0.0008, delta));
    camera.lookAt(target);
  }

  function setSpawn(spawn = {}) {
    const requestedSpace = String(spawn.space || "");
    const space = Object.hasOwn(WORLD_SPACE_FLOORS, requestedSpace)
      ? requestedSpace
      : "town-square";
    const x = clamp(Number(spawn.x) || 0, -WORLD_RADIUS, WORLD_RADIUS);
    const z = clamp(Number(spawn.z) || 0, -WORLD_RADIUS, WORLD_RADIUS);
    const heading = clamp(Number(spawn.heading) || 0, -Math.PI, Math.PI);
    currentSpace = space;
    currentFloorY = WORLD_SPACE_FLOORS[space];
    player.position.set(x, currentFloorY, z);
    if (space === "town-square") {
      constrainTownOfficeWalls(player.position.clone());
    }
    player.rotation.y = arrivalFacingHeading(x, z, heading);
    lastPosition.copy(player.position);
    wasWalking = false;
    cameraFocus = null;
    cancelDash();
    focusedRepositoryKey = "";
    if (space === "town-square") {
      nearestLandmark();
    } else {
      currentLocation = space
        .split("-")
        .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
        .join(" ");
      onLocationChange(currentLocation, "");
    }
  }

  function removeRemoteModerationControls(avatar, peerId) {
    const controls = avatar?.userData?.moderationControls;
    if (controls) {
      controls.traverse((child) => {
        moderationActions.delete(child);
        const interactiveIndex = interactive.indexOf(child);
        if (interactiveIndex >= 0) interactive.splice(interactiveIndex, 1);
      });
      avatar.remove(controls);
      disposeObject3D(controls);
      delete avatar.userData.moderationControls;
    }
    moderationControlKeys.delete(String(peerId || ""));
  }

  function syncRemoteModerationControls(avatar, remote) {
    const peerId = String(remote?.id || "");
    const name = String(remote?.name || "visitor").slice(0, 32);
    const handles = sanitizedModerationHandles(
      remote,
      identity?.isAdmin === true,
    );
    const key = JSON.stringify([
      name,
      handles.ip || "",
      handles.agent || "",
    ]);
    if (
      moderationControlKeys.get(peerId) === key &&
      avatar?.userData?.moderationControls
    ) {
      return;
    }
    removeRemoteModerationControls(avatar, peerId);
    if (!handles.ip && !handles.agent) return;

    const controls = createAvatarModerationControls(THREE, handles);
    controls.children.forEach((control) => {
      const targetType = control.userData.worldModerationControl;
      const handle = handles[targetType];
      if (!WORLD_MODERATION_HANDLE_PATTERN.test(handle || "")) return;
      moderationActions.set(control, {
        targetType,
        handle,
        peerId,
        name,
      });
      interactive.push(control);
    });
    avatar.add(controls);
    avatar.userData.moderationControls = controls;
    moderationControlKeys.set(peerId, key);
  }

  function setRemotePlayers(players = []) {
    const seen = new Set();
    players.forEach((remote) => {
      if (!remote?.id || remote.id === identity.id) return;
      seen.add(remote.id);
      let avatar = remotePlayers.get(remote.id);
      const badgeIdentity = {
        id: remote.id,
        name: remote.name || "visitor",
        flag: remote.flag || "◌",
        countryCode: remote.countryCode || "",
        browser: remote.browser || "Browser",
        os: remote.os || "Device",
        status: remote.activity || "exploring",
        accountStatus: remote.accountStatus || "Guest",
        localTime: remote.localTime || "",
        activityCategory:
          remote.category || remote.activityCategory || "hidden",
        inputActive: remote.inputActive === true,
        visitCount: Math.max(
          0,
          Math.min(999, Number(remote.visitCount) || 0),
        ),
        firstVisitAge: remote.firstVisitAge || "hidden",
        nodes: Array.isArray(remote.nodes) ? remote.nodes.slice(0, 6) : [],
        statusEmoji: remote.statusEmoji || "",
        statusNote: remote.statusNote || "",
      };
      const badgeKey = JSON.stringify(badgeIdentity);
      if (!avatar) {
        avatar = createAvatar(
          THREE,
          badgeIdentity,
          { remote: true, scale: 0.92 },
        );
        avatar.userData.badgeKey = badgeKey;
        const initialX = Number(remote.x);
        const initialZ = Number(remote.z);
        avatar.position.set(
          Number.isFinite(initialX) ? initialX : 0,
          0.38,
          Number.isFinite(initialZ) ? initialZ : 0,
        );
        world.add(avatar);
        remotePlayers.set(remote.id, avatar);
        remoteLabels.set(remote.id, makePlayerLabel(avatar, labelLayer));
      }
      const sharedInactive = remote.activity === "idle";
      const loungeEligible = REGISTERED_LOUNGE_STATUSES.has(
        String(remote.accountStatus || ""),
      );
      const recentlyActive =
        remote.recent === true ||
        ["recent", "returning"].includes(String(remote.availability || ""));
      const useRegisteredLounge =
        loungeEligible && (sharedInactive || recentlyActive);
      avatar.userData.loungeActivity = useRegisteredLounge
        ? recentlyActive
          ? "recent"
          : "idle"
        : "";
      avatar.userData.campfireSeated =
        String(remote.activity || "") === CAMPFIRE_SEATED_ACTIVITY;
      if (useRegisteredLounge) {
        // Idle and returning members walk to the empty stools left after the
        // seated directory figures, joining the same circle around the fire.
        const seats = campfire.userData.seatOffsets || [];
        const taken = Math.min(
          campfire.userData.memberFigureCount || 0,
          Math.max(0, seats.length - 1),
        );
        const open = Math.max(1, seats.length - taken);
        const seat = seats[taken + (hashNumber(remote.id) % open)];
        const offset = seat || new THREE.Vector3();
        avatar.userData.targetPosition.copy(campfire.position);
        avatar.userData.targetPosition.add(offset);
        // Face the flames at the circle's centre: avatar fronts face local
        // -Z, so the inward heading is atan2(x, z) — the same heading
        // sitOnCampfireBench gives the local player.
        avatar.userData.targetHeading = Math.atan2(offset.x, offset.z);
      } else if (sharedInactive) {
        const restArea = landmarkById("neighborhood").position;
        const seat = hashNumber(remote.id) % 8;
        avatar.userData.targetPosition.set(
          restArea[0] - 2.8 + (seat % 4) * 1.8,
          0.38,
          restArea[2] + 4.1 + Math.floor(seat / 4) * 0.8,
        );
      } else {
        avatar.userData.targetPosition.set(
          clamp(Number(remote.x) || 0, -WORLD_RADIUS, WORLD_RADIUS),
          clamp(Number(remote.y) || 0.38, 0.38, 40),
          clamp(Number(remote.z) || 0, -WORLD_RADIUS, WORLD_RADIUS),
        );
        avatar.userData.targetHeading = arrivalFacingHeading(
          avatar.userData.targetPosition.x,
          avatar.userData.targetPosition.z,
          Number(remote.heading) || 0,
        );
      }
      avatar.userData.status = remote.activity || "exploring";
      if (avatar.userData.badgeKey !== badgeKey) {
        updateAvatarBadge(THREE, avatar, badgeIdentity, true);
        syncOperatorBelt(THREE, avatar, badgeIdentity.nodes.length);
        avatar.userData.badgeKey = badgeKey;
        updatePlayerLabel(remoteLabels.get(remote.id), badgeIdentity);
      }
      syncRemoteModerationControls(avatar, remote);
    });
    remotePlayers.forEach((avatar, id) => {
      if (seen.has(id)) return;
      removeRemoteModerationControls(avatar, id);
      world.remove(avatar);
      avatar.traverse((child) => {
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
      remotePlayers.delete(id);
      remoteLabels.get(id)?.remove();
      remoteLabels.delete(id);
    });
    updateNeighborhoodHomes(players);
  }

  function updateNeighborhoodHomes(players = []) {
    const neighborhood = landmarkObjects.get("neighborhood");
    if (!neighborhood) return;
    const homes = players
      .filter((player) => player?.id)
      .slice(0, 12)
      .map((player) => ({
        id: String(player.id),
        name: String(player.name || "visitor").slice(0, 20),
        door: ["closed", "knock", "open"].includes(player.publicDoor)
          ? player.publicDoor
          : "closed",
      }));
    const key = JSON.stringify(homes);
    if (neighborhood.userData.homesKey === key) return;
    const existing = neighborhood.userData.publicHomes;
    if (existing) {
      neighborhood.remove(existing);
      existing.traverse((child) => {
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
    }
    const group = new THREE.Group();
    neighborhoodHomes.clear();
    homes.forEach((home, index) => {
      const angle = (index / Math.max(1, homes.length)) * Math.PI * 2;
      const house = new THREE.Mesh(
        new THREE.BoxGeometry(0.75, 0.75, 0.7),
        makeMaterial(
          THREE,
          home.door === "open"
            ? "#72d49b"
            : home.door === "knock"
              ? "#d6b66e"
              : "#6e7973",
        ),
      );
      house.position.set(Math.cos(angle) * 6.3, 0.48, Math.sin(angle) * 6.3);
      group.add(house);
      neighborhoodHomes.set(home.id, {
        name: home.name,
        position: new THREE.Vector3(
          neighborhood.position.x + house.position.x,
          0.38,
          neighborhood.position.z + house.position.z,
        ),
        angle,
      });
      const label = makeLabelSprite(
        THREE,
        home.name,
        home.door === "open" ? "open lobby" : home.door,
        "#b8e986",
      );
      label.scale.set(1.45, 0.48, 1);
      label.position.copy(house.position).add(new THREE.Vector3(0, 1.15, 0));
      group.add(label);
    });
    neighborhood.add(group);
    neighborhood.userData.publicHomes = group;
    neighborhood.userData.homesKey = key;
  }

  function updateArrivalStats(stats) {
    const face = arrivalPlaque.userData.statsFace;
    if (!face || !stats || typeof stats !== "object") return;
    const safe = {
      total: Math.max(0, Math.round(Number(stats.total) || 0)),
      today: Math.max(0, Math.round(Number(stats.today) || 0)),
      yesterdaySameTime: Math.max(
        0,
        Math.round(Number(stats.yesterdaySameTime) || 0),
      ),
      pastHour: Math.max(0, Math.round(Number(stats.pastHour) || 0)),
      pastHourYesterday: Math.max(
        0,
        Math.round(Number(stats.pastHourYesterday) || 0),
      ),
    };
    const key = JSON.stringify(safe);
    if (arrivalPlaque.userData.statsShown === key) return;
    face.material.map?.dispose?.();
    face.material.map = arrivalPlaqueTexture(THREE, safe);
    face.material.needsUpdate = true;
    arrivalPlaque.userData.statsShown = key;
  }

  function updateMemberLounge(
    members = [],
    totalCount = 0,
    leaderboardMembers = members,
  ) {
    const total = Math.max(0, Math.min(999999, Number(totalCount) || 0));
    const leaderboardFace = activeLeaderboardSign.userData.face;
    const leaderboardKey = JSON.stringify(
      rankedActiveLeaderboardMembers(leaderboardMembers)
        .map((member) => [
          String(member?.name || ""),
          member?.totalActiveMs ?? member?.activeMs ?? null,
          member?.activeNow === true,
        ]),
    );
    if (leaderboardFace && activeLeaderboardSign.userData.key !== leaderboardKey) {
      leaderboardFace.material.map?.dispose?.();
      leaderboardFace.material.map = activeLeaderboardTexture(
        THREE,
        leaderboardMembers,
      );
      leaderboardFace.material.needsUpdate = true;
      activeLeaderboardSign.userData.key = leaderboardKey;
    }
    // Registered members sit in a circle around the campfire facing the
    // flames. The circle holds one bench per registered account, so members
    // currently walking the world as live avatars leave visibly empty seats,
    // plus one extra bench that always stays open for the next guest: when a
    // new account joins and takes it, the roster grows and the rebuilt ring
    // brings a fresh open bench with it.
    const roster = (Array.isArray(members) ? members : []).filter((member) =>
      String(member?.name || "").trim(),
    );
    const seats = rebuildCampfireCircle(Math.max(total, roster.length) + 1);
    const seen = new Set();
    roster
      .slice(0, Math.max(1, seats.length))
      .forEach((member, index) => {
        const name = String(member.name).trim().slice(0, 32);
        const id = `member:${name.toLowerCase()}`;
        if (seen.has(id)) return;
        seen.add(id);
        let figure = loungeMembers.get(id);
        if (!figure) {
          figure = createAvatar(
            THREE,
            {
              id,
              name,
              flag: "◌",
              countryCode: "",
              browser: "Hidden",
              os: "Hidden",
              status: "sitting around the campfire",
              accountStatus: "Registered",
              localTime: "",
              activityCategory: "hidden",
              inputActive: false,
              visitCount: 0,
              firstVisitAge: "hidden",
              // The public directory shares the joined timestamp and coarse
              // active-time aggregate, so the nameplate can show "JOINED …
              // AGO" and "ACTIVE … IN WORLD" instead of the hidden labels.
              joinedAt: Number(member.createdAt) || 0,
              totalActiveMs: Math.max(
                0,
                Number(member.totalActiveMs ?? member.activeMs) || 0,
              ),
              nodes: Array.isArray(member.nodes)
                ? member.nodes.slice(0, 6)
                : [],
              statusEmoji: "",
              statusNote: "",
            },
            { remote: true, scale: 0.88 },
          );
          world.add(figure);
          loungeMembers.set(id, figure);
        }
        const seat = seats[index % Math.max(1, seats.length)];
        figure.position.copy(campfire.position);
        if (seat) figure.position.add(seat);
        // Face the fire at the circle's centre and hold a seated pose on the
        // bench, matching the local player's bench-seat legs. Avatar fronts
        // face local -Z, so the inward heading is atan2(x, z), matching
        // sitOnCampfireBench.
        figure.rotation.y = seat ? Math.atan2(seat.x, seat.z) : 0;
        figure.userData.leftLeg.rotation.x = -1.3;
        figure.userData.rightLeg.rotation.x = -1.3;
      });
    campfire.userData.memberFigureCount = Math.min(seen.size, seats.length);
    loungeMembers.forEach((figure, id) => {
      if (seen.has(id)) return;
      world.remove(figure);
      disposeObject3D(figure);
      loungeMembers.delete(id);
    });
  }

  function visitNeighborhoodHome(ownerId) {
    const home = neighborhoodHomes.get(String(ownerId || ""));
    if (!home) return false;
    const destination = home.position.clone();
    destination.x += Math.cos(home.angle) * 1.15;
    destination.z += Math.sin(home.angle) * 1.15;
    currentSpace = "town-square";
    currentFloorY = 0.38;
    player.position.copy(destination);
    cameraFocus = null;
    cancelDash();
    focusedRepositoryKey = "";
    currentLocation = `${home.name}'s front yard`;
    onLocationChange(currentLocation, "neighborhood");
    onMovement({
      x: destination.x,
      y: destination.y,
      z: destination.z,
      heading: player.rotation.y,
      activity: "visiting a consented front yard",
      space: "town-square",
      moving: false,
    });
    return true;
  }

  function updateNetworkNodes(nodes = []) {
    const routingX = SERVER_CABINET_YARD_ORIGIN[0];
    const routingZ = SERVER_CABINET_YARD_ORIGIN[2];
    // A dedicated 8×8 server yard keeps all 64 bounded live slots separate
    // without requiring the retired routing-station landmark. Fill the inward
    // slots first so a small healthy fleet stays closest to the Town Square.
    const serverSlots = [];
    for (let column = 0; column < 8; column += 1) {
      for (let row = 0; row < 8; row += 1) {
        const x = routingX + 9.5 + column * 3;
        const z = routingZ - 10.5 + row * 3;
        serverSlots.push({
          x,
          z,
          distance: Math.hypot(x - routingX, z - routingZ),
        });
      }
    }
    serverSlots.sort(
      (left, right) =>
        left.distance - right.distance || left.z - right.z || left.x - right.x,
    );
    const seen = new Set();
    const takenLayoutIds = new Set();
    const removeCabinet = (cabinet) => {
      cabinet?.traverse?.((child) => {
        if (!child.userData?.nodeCabinet) return;
        const interactiveIndex = interactive.indexOf(child);
        if (interactiveIndex >= 0) interactive.splice(interactiveIndex, 1);
      });
      forgetMovableObject(cabinet?.userData?.layoutId);
      world.remove(cabinet);
      disposeObject3D(cabinet);
    };
    const usableNodes = (Array.isArray(nodes) ? nodes : [])
      .filter((node) => String(node?.name || node?.label || "").trim())
      .slice(0, 64);
    usableNodes.forEach((node, index) => {
      const nodeName = String(node.name || node.label).trim().slice(0, 80);
      const id = `node:${nodeName.toLowerCase()}`;
      const dataKey = nodeDataKey({ ...node, name: nodeName });
      seen.add(id);
      let cabinet = nodeInfrastructure.get(id);
      if (
        cabinet &&
        cabinet.userData.dataKey !== dataKey
      ) {
        removeCabinet(cabinet);
        nodeInfrastructure.delete(id);
        cabinet = null;
      }
      if (!cabinet) {
        cabinet = createMirrorServerCabinet(
          THREE,
          { ...node, name: nodeName },
          id,
        );
        world.add(cabinet);
        for (const panelName of [
          "mirror-server-front-panel",
          "mirror-server-rear-panel",
        ]) {
          const panel = cabinet.getObjectByName(panelName);
          if (panel) interactive.push(panel);
        }
        nodeInfrastructure.set(id, cabinet);
      }
      const slot = serverSlots[index];
      cabinet.position.set(slot.x, 0.38, slot.z);
      // The front display faces inward so each cabinet remains individually
      // readable from the surrounding walkway.
      cabinet.rotation.y = Math.atan2(
        routingX - slot.x,
        routingZ - slot.z,
      );
      // The yard slot is only the default: registering the cabinet re-applies
      // any administrator-locked position and turn on top of it, and it has to
      // happen after the slot assignment above overwrites both.
      const layoutId = worldLayoutId("node-", nodeName);
      if (layoutId && !takenLayoutIds.has(layoutId)) {
        takenLayoutIds.add(layoutId);
        cabinet.userData.layoutBaseRotation = cabinet.rotation.y;
        registerMovableObject(layoutId, cabinet);
      }
    });
    nodeInfrastructure.forEach((cabinet, id) => {
      if (seen.has(id)) return;
      removeCabinet(cabinet);
      nodeInfrastructure.delete(id);
    });
  }

  function focusNetworkNode(name) {
    const targetName = String(name || "").trim().toLowerCase();
    let target = null;
    nodeInfrastructure.forEach((cabinet) => {
      if (
        !target &&
        String(cabinet.userData?.nodeRecord?.name || "")
          .trim()
          .toLowerCase() === targetName
      ) {
        target = cabinet;
      }
    });
    if (!target) return false;
    cameraFocus = target.position.clone();
    cameraFocus.y += 1.65;
    return true;
  }

  function updateFederatedInstances(instances = []) {
    const district = landmarkObjects.get("fediverse");
    if (!district) return;
    const existing = district.userData.federatedInstanceLayer;
    if (existing) {
      district.remove(existing);
      existing.traverse((child) => {
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
    }
    const layer = new THREE.Group();
    layer.name = "approved-federated-instances";
    (Array.isArray(instances) ? instances : [])
      .filter((instance) => instance?.approved === true)
      .slice(0, 18)
      .forEach((instance, index, approved) => {
        const angle = (index / Math.max(1, approved.length)) * Math.PI * 2;
        const online = instance?.online === true;
        const tower = new THREE.Mesh(
          new THREE.CylinderGeometry(0.22, 0.34, online ? 1.2 : 0.72, 8),
          makeMaterial(THREE, online ? "#80e8ff" : "#718087", {
            emissive: online ? "#267e9a" : "#313a3d",
            emissiveIntensity: online ? 0.72 : 0.22,
          }),
        );
        tower.position.set(
          Math.cos(angle) * 4.3,
          online ? 0.75 : 0.5,
          Math.sin(angle) * 4.3,
        );
        layer.add(tower);
        const label = makeLabelSprite(
          THREE,
          String(instance?.label || "ForkMesh instance").slice(0, 22),
          online ? "verified online" : "approved · health unavailable",
          online ? "#80e8ff" : "#9aa6aa",
        );
        label.scale.set(1.6, 0.54, 1);
        label.position.copy(tower.position).add(
          new THREE.Vector3(0, online ? 1.05 : 0.78, 0),
        );
        layer.add(label);
      });
    district.add(layer);
    district.userData.federatedInstanceLayer = layer;
  }

  function updateBots(bots = []) {
    const seen = new Set();
    const namedBots = (Array.isArray(bots) ? bots : [])
      .filter(
        (bot) =>
          String(bot?.id || "").trim() &&
          String(bot?.name || "").trim() &&
          String(bot?.type || "").trim(),
      )
      .slice(0, 12);
    namedBots.forEach((bot, index) => {
      const id = `bot:${String(bot.id).trim().slice(0, 48)}`;
      seen.add(id);
      let robot = botAgents.get(id);
      if (!robot) {
        robot = createAgentRobot(
          THREE,
          {
            ...bot,
            name: String(bot.name).trim().slice(0, 32),
            type: String(bot.type).trim().slice(0, 20),
          },
          id,
        );
        world.add(robot);
        botAgents.set(id, robot);
      }
      const center = landmarkById(
        index % 2 ? "fediverse" : "security",
      ).position;
      const angle = (index / Math.max(1, namedBots.length)) * Math.PI * 2;
      robot.position.set(
          center[0] + Math.cos(angle) * 5.2,
          0.38,
          center[2] + Math.sin(angle) * 5.2,
      );
      robot.rotation.y = -angle;
    });
    botAgents.forEach((robot, id) => {
      if (seen.has(id)) return;
      world.remove(robot);
      disposeObject3D(robot);
      botAgents.delete(id);
    });
  }

  function updateSystemCapacity(metrics = {}) {
    const records = Array.isArray(metrics)
      ? metrics
      : Array.isArray(metrics?.objects)
        ? metrics.objects
        : [];
    const safeRecords = records
      .map((record) => {
        const name = String(record?.name || record?.id || "").trim();
        const pairs = systemCapacityMetricPairs(record);
        return name && pairs.length
          ? {
              name: name.slice(0, 36),
              id: String(record?.id || name).slice(0, 72),
              pairs,
            }
          : null;
      })
      .filter(Boolean)
      .slice(0, 4);
    const tables = Array.isArray(metrics?.tables) ? metrics.tables : [];
    const safeTables = [
      ...new Map(
        tables
          .map((table) => {
            const name = String(table?.name || "").trim();
            const rowCount = Number(table?.rowCount);
            return /^[A-Za-z_][A-Za-z0-9_]{0,62}$/.test(name) &&
              Number.isSafeInteger(rowCount) &&
              rowCount > 1
              ? [name, { name, rowCount }]
              : null;
          })
          .filter(Boolean),
      ).values(),
    ]
      .sort(
        (left, right) =>
          right.rowCount - left.rowCount ||
          left.name.localeCompare(right.name),
      )
      .slice(0, 128);
    const signature = JSON.stringify({
      objects: safeRecords,
      tables: safeTables,
    });
    if (
      systemCapacityPlatform.userData.metricsSignature === signature
    ) {
      return safeRecords.length + safeTables.length;
    }
    systemCapacityPlatform.userData.metricsSignature = signature;

    const existing = systemCapacityPlatform.userData.metricsLayer;
    if (existing) {
      // Table bars are pickable, so the old ones have to leave the interactive
      // list before they are disposed or clicks would raycast dead meshes.
      removeInteractiveObject(interactive, existing);
      systemCapacityPlatform.remove(existing);
      disposeObject3D(existing);
      systemCapacityPlatform.userData.metricsLayer = null;
    }

    const emptyMarker = systemCapacityPlatform.getObjectByName(
      "system-capacity-metrics-unavailable",
    );
    const hasMetrics = safeRecords.length > 0 || safeTables.length > 0;
    if (emptyMarker) emptyMarker.visible = !hasMetrics;
    systemCapacityPlatform.userData.metricsAvailable =
      hasMetrics;
    systemCapacityPlatform.userData.visibleObjectCount = safeRecords.length;
    systemCapacityPlatform.userData.visibleTableCount = safeTables.length;
    if (!hasMetrics) return 0;

    const layer = new THREE.Group();
    layer.name = "system-capacity-current-usage";
    if (safeTables.length) {
      const tableLayer = new THREE.Group();
      tableLayer.name = "system-capacity-database-tables";
      tableLayer.userData.tableCount = safeTables.length;
      const maxRows = Math.max(
        ...safeTables.map((table) => table.rowCount),
      );
      const usableWidth = 11.5;
      const usableDepth = 6.8;
      const columns = Math.max(
        1,
        Math.ceil(Math.sqrt(safeTables.length * (usableWidth / usableDepth))),
      );
      const rows = Math.ceil(safeTables.length / columns);
      const cellWidth = usableWidth / columns;
      const cellDepth = usableDepth / rows;
      const barWidth = clamp(
        Math.min(cellWidth, cellDepth) * 0.54,
        0.075,
        0.52,
      );
      const tableTopLabel = (rowCount, tableName, width) => {
        const texture = canvasTexture(THREE, 768, 768, (context) => {
          context.clearRect(0, 0, 768, 768);
          context.fillStyle = "#071c16";
          context.textAlign = "center";
          context.textBaseline = "middle";
          context.font = `800 ${rowCount.length > 5 ? 146 : 184}px "ForkMesh Mono", ui-monospace, monospace`;
          context.fillText(rowCount, 384, 278, 710);
          context.font = '700 72px "ForkMesh Mono", ui-monospace, monospace';
          context.fillText(tableName, 384, 508, 710);
        });
        return new THREE.Mesh(
          new THREE.PlaneGeometry(width, width),
          new THREE.MeshBasicMaterial({
            map: texture,
            transparent: true,
            toneMapped: false,
            side: THREE.DoubleSide,
            depthWrite: false,
          }),
        );
      };
      safeTables.forEach((table, index) => {
        const column = index % columns;
        const row = Math.floor(index / columns);
        const normalized =
          Math.log1p(table.rowCount) / Math.log1p(maxRows);
        const height = 0.18 + normalized * 3.15;
        const bar = new THREE.Mesh(
          new THREE.BoxGeometry(barWidth, height, barWidth),
          makeMaterial(
            THREE,
            normalized > 0.72
              ? "#9ef7c6"
              : normalized > 0.38
                ? "#80e8ff"
                : "#587d75",
            {
              emissive:
                normalized > 0.72
                  ? "#2f8c5f"
                  : normalized > 0.38
                    ? "#267e9a"
                    : "#203f38",
              emissiveIntensity: 0.72,
              metalness: 0.24,
              roughness: 0.42,
            },
          ),
        );
        bar.name = `system-capacity-table:${table.name}`;
        bar.position.set(
          -usableWidth / 2 + cellWidth * (column + 0.5),
          0.38 + height / 2,
          -3.45 + cellDepth * (row + 0.5),
        );
        bar.userData.tableName = table.name;
        bar.userData.rowCount = table.rowCount;
        // Clicking a bar opens the floating table browser for that table.
        bar.userData.interactive = "system-capacity-table";
        interactive.push(bar);
        tableLayer.add(bar);
        // The complete table identity is printed on its top face: a large row
        // count fills the width, with the table name immediately beneath it.
        const topLabel = tableTopLabel(
          table.rowCount.toLocaleString("en-US"),
          table.name,
          barWidth * 0.96,
        );
        topLabel.name = `system-capacity-row-count:${table.name}`;
        topLabel.position.set(
          bar.position.x,
          0.38 + height + 0.014,
          bar.position.z,
        );
        topLabel.rotation.x = -Math.PI / 2;
        tableLayer.add(topLabel);
      });
      layer.add(tableLayer);
    }

    safeRecords.forEach((record, index) => {
      const object = new THREE.Group();
      object.name = `system-capacity-service:${record.id}`;
      object.userData.metrics = record.pairs.map((metric) => ({
        key: metric.key,
        currentUsage: metric.usage,
        configuredLimit: metric.limit,
      }));

      const spacing = 2.5;
      object.position.set(
        (index - (safeRecords.length - 1) / 2) * spacing,
        0.38,
        safeTables.length ? 3.75 : 0,
      );
      const plinth = new THREE.Mesh(
        new THREE.CylinderGeometry(0.52, 0.64, 0.2, 8),
        makeMaterial(THREE, "#173840", {
          emissive: "#1d5665",
          emissiveIntensity: 0.38,
          metalness: 0.28,
        }),
      );
      plinth.position.y = 0.13;
      object.add(plinth);

      const firstMetric = record.pairs[0];
      const rawRatio = firstMetric.usage / firstMetric.limit;
      const ratio = clamp(rawRatio, 0, 1);
      const overLimit = rawRatio > 1;
      const housing = new THREE.Mesh(
        new THREE.BoxGeometry(0.62, 1.35, 0.62),
        makeMaterial(THREE, "#1d343b", {
          transparent: true,
          opacity: 0.62,
          roughness: 0.46,
        }),
      );
      housing.position.y = 0.9;
      object.add(housing);
      const fillHeight = Math.max(0.08, ratio * 1.28);
      const fill = new THREE.Mesh(
        new THREE.BoxGeometry(0.44, fillHeight, 0.44),
        makeMaterial(THREE, overLimit ? "#ff6d78" : "#80e8ff", {
          emissive: overLimit ? "#9f2632" : "#267e9a",
          emissiveIntensity: 0.92,
          metalness: 0.22,
        }),
      );
      fill.position.y = 0.25 + fillHeight / 2;
      fill.userData.currentUsage = firstMetric.usage;
      fill.userData.configuredLimit = firstMetric.limit;
      object.add(fill);

      const exactValues = record.pairs
        .slice(0, 2)
        .map(
          (metric) =>
            `${metric.key} ${metric.usage}/${metric.limit}`,
        )
        .join(" · ");
      const label = makeLabelSprite(
        THREE,
        record.name,
        exactValues,
        overLimit ? "#ff9ca4" : "#80e8ff",
      );
      label.scale.set(2.15, 0.72, 1);
      label.position.y = 2.05;
      object.add(label);
      setShadows(object);
      layer.add(object);
    });
    systemCapacityPlatform.add(layer);
    systemCapacityPlatform.userData.metricsLayer = layer;
    return safeRecords.length + safeTables.length;
  }

  function updateOrganizations(organizations = []) {
    const district = landmarkObjects.get("organizations");
    if (!district) return;
    const existing = district.userData.organizationProfiles;
    if (existing) {
      district.remove(existing);
      existing.traverse((child) => {
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
    }
    const profiles = new THREE.Group();
    profiles.name = "organization-profiles";
    organizations.slice(0, 8).forEach((organization, index) => {
      const name = String(
        organization.displayName || organization.name || organization.org || "Organization",
      ).slice(0, 28);
      const repoCount = Array.isArray(organization.repos)
        ? organization.repos.length
        : Number(organization.repos || 0);
      const column = index % 3;
      const row = Math.floor(index / 3);
      const sign = makeLabelSprite(
        THREE,
        name.toUpperCase(),
        `${repoCount} repository ${repoCount === 1 ? "floor" : "floors"}`,
        "#d5b6ff",
      );
      sign.scale.set(2.8, 0.92, 1);
      sign.position.set((column - 1) * 2.75, 4.6 + row * 1.0, -1.62);
      profiles.add(sign);
      const repos = Array.isArray(organization.repos)
        ? organization.repos.slice(0, 8)
        : [];
      repos.forEach((repo, repoIndex) => {
        const flower = new THREE.Mesh(
          new THREE.IcosahedronGeometry(0.17 + (repoIndex % 3) * 0.035, 1),
          makeMaterial(THREE, ["#d5b6ff", "#9ef7c6", "#77d9ff"][repoIndex % 3], {
            emissive: "#60458b",
            emissiveIntensity: 0.4,
          }),
        );
        flower.position.set(
          (column - 1) * 2.75 - 0.7 + (repoIndex % 4) * 0.45,
          0.55,
          2.1 + row * 0.7,
        );
        profiles.add(flower);
      });
      const offices = Array.isArray(organization.memberList)
        ? organization.memberList.slice(0, 6)
        : [];
      offices.forEach((member, officeIndex) => {
        const office = new THREE.Mesh(
          new THREE.BoxGeometry(0.38, 0.42, 0.32),
          makeMaterial(THREE, "#f2d4ff", {
            emissive: "#60458b",
            emissiveIntensity: 0.25,
          }),
        );
        office.position.set(
          (column - 1) * 2.75 - 0.75 + (officeIndex % 3) * 0.75,
          1.1 + Math.floor(officeIndex / 3) * 0.58,
          -1.35,
        );
        office.userData.officeMember = String(member?.name || "").slice(0, 50);
        profiles.add(office);
      });
    });
    district.add(profiles);
    district.userData.organizationProfiles = profiles;
  }

  function updateFediverseDirectory(directory = {}) {
    const center = landmarkObjects.get("fediverse");
    if (!center) return;
    const existing = center.userData.instanceProfiles;
    if (existing) {
      center.remove(existing);
      existing.traverse((child) => {
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
    }
    const layer = new THREE.Group();
    layer.name = "consented-fediverse-profiles";
    const instances = [
      ...(Array.isArray(directory.mastodon)
        ? directory.mastodon.map((instance) => ({
            ...instance,
            network: "Mastodon",
          }))
        : []),
      ...(Array.isArray(directory.lemmy)
        ? directory.lemmy.map((instance) => ({
            ...instance,
            network: "Lemmy",
          }))
        : []),
      ...(Array.isArray(directory.x)
        ? directory.x.map((instance) => ({
            ...instance,
            network: "X",
          }))
        : []),
      ...(Array.isArray(directory.reddit)
        ? directory.reddit.map((instance) => ({
            ...instance,
            network: "Reddit",
          }))
        : []),
    ].slice(0, 16);
    instances.forEach((instance, instanceIndex) => {
      const angle = (instanceIndex / Math.max(1, instances.length)) * Math.PI * 2;
      const instanceNode = new THREE.Mesh(
        new THREE.IcosahedronGeometry(0.3, 1),
        makeMaterial(THREE, {
          Mastodon: "#8c8dff",
          Lemmy: "#8fcf85",
          X: "#d8e3ea",
          Reddit: "#ff8a69",
        }[instance.network] || "#b6d8ff", {
          emissive: {
            Mastodon: "#5355bd",
            Lemmy: "#467f42",
            X: "#53626a",
            Reddit: "#a43c25",
          }[instance.network] || "#425b73",
          emissiveIntensity: 0.6,
        }),
      );
      instanceNode.position.set(
        Math.cos(angle) * 4.15,
        1.15 + (instanceIndex % 3) * 0.32,
        Math.sin(angle) * 4.15,
      );
      layer.add(instanceNode);
      const approvedSource =
        instance.network === "Mastodon"
          ? instance.consentedFollowers
          : instance.consentedProfiles;
      const approved = Array.isArray(approvedSource)
        ? approvedSource
            .filter(
              (follower) =>
                follower?.consent === true &&
                follower?.public === true &&
                follower?.consentEvidence?.oauthVerifiedByForkMesh === false,
            )
            .slice(0, 8)
        : [];
      approved.forEach((follower, followerIndex) => {
        const followerAngle =
          angle - 0.55 + (followerIndex / Math.max(1, approved.length - 1)) * 1.1;
        const face = makeConsentedProfileFace(THREE, follower);
        face.position.set(
          Math.cos(followerAngle) * 5.25,
          1.8 + (followerIndex % 2) * 0.48,
          Math.sin(followerAngle) * 5.25,
        );
        layer.add(face);
        const profile = makeLabelSprite(
          THREE,
          String(follower.handle || "public").slice(0, 18),
          "user-approved",
          "#ff9eb7",
        );
        profile.scale.set(1.2, 0.4, 1);
        profile.position.set(
          Math.cos(followerAngle) * 5.25,
          2.45 + (followerIndex % 2) * 0.48,
          Math.sin(followerAngle) * 5.25,
        );
        layer.add(profile);
      });
    });
    center.add(layer);
    center.userData.instanceProfiles = layer;
  }

  function updateMediaSpaces(spaces = [], activeSpace = {}) {
    const garden = landmarkObjects.get("broadcast");
    if (!garden) return;
    const existing = garden.userData.sharedMediaSpaces;
    if (existing) {
      garden.remove(existing);
      existing.traverse((child) => {
        const interactiveIndex = interactive.indexOf(child);
        if (interactiveIndex >= 0) interactive.splice(interactiveIndex, 1);
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
    }
    const layer = new THREE.Group();
    layer.name = "shared-media-spaces";
    const rooms = (Array.isArray(spaces) ? spaces : []).slice(0, 8);
    rooms.forEach((room, index) => {
      const angle = (index / Math.max(1, rooms.length)) * Math.PI * 2;
      const isActive = String(room?.id || "") === String(activeSpace?.id || "");
      const roomNode = new THREE.Mesh(
        new THREE.CylinderGeometry(
          isActive ? 0.6 : 0.42,
          isActive ? 0.72 : 0.52,
          isActive ? 1.25 : 0.88,
          10,
        ),
        makeMaterial(THREE, isActive ? "#9ef7c6" : "#8fcfff", {
          emissive: isActive ? "#2f9f69" : "#2b6e9b",
          emissiveIntensity: isActive ? 0.85 : 0.45,
          metalness: 0.22,
        }),
      );
      roomNode.position.set(
        Math.cos(angle) * 3.65,
        0.68,
        Math.sin(angle) * 3.65,
      );
      roomNode.userData.mediaSpaceId = String(room?.id || "").slice(0, 40);
      roomNode.userData.landmark = "broadcast";
      interactive.push(roomNode);
      layer.add(roomNode);
      const itemCount = Math.max(0, Number(room?.itemCount) || 0);
      const scheduleCount = Math.max(0, Number(room?.scheduleCount) || 0);
      const label = makeLabelSprite(
        THREE,
        String(room?.name || "Shared room").slice(0, 24),
        `${itemCount} links · ${scheduleCount} scheduled`,
        isActive ? "#9ef7c6" : "#8fcfff",
      );
      label.scale.set(1.75, 0.58, 1);
      label.position.copy(roomNode.position).add(new THREE.Vector3(0, 1.1, 0));
      layer.add(label);
    });

    // The selected server-authoritative playlist and schedule become physical
    // records in the Broadcast Garden. These are coordination markers only:
    // no media URL, stream, or playback data is rendered or relayed here.
    const items = (Array.isArray(activeSpace?.items)
      ? activeSpace.items
      : []).slice(0, 16);
    items.forEach((item, index) => {
      const angle = (index / Math.max(1, items.length)) * Math.PI * 2;
      const disc = new THREE.Mesh(
        new THREE.TorusGeometry(0.18, 0.055, 8, 24),
        makeMaterial(THREE, item?.status === "stopped" ? "#9a9a9a" : "#ffd37a", {
          emissive: item?.status === "stopped" ? "#333333" : "#a96722",
          emissiveIntensity: 0.58,
        }),
      );
      disc.rotation.x = Math.PI / 2;
      disc.position.set(
        Math.cos(angle) * 2.0,
        1.1 + (index % 3) * 0.24,
        Math.sin(angle) * 2.0,
      );
      layer.add(disc);
    });
    const schedules = (Array.isArray(activeSpace?.schedules)
      ? activeSpace.schedules
      : []).filter((schedule) => schedule?.status === "scheduled").slice(0, 6);
    schedules.forEach((schedule, index) => {
      const marker = new THREE.Mesh(
        new THREE.OctahedronGeometry(0.2, 0),
        makeMaterial(THREE, "#d5b6ff", {
          emissive: "#7650a7",
          emissiveIntensity: 0.72,
        }),
      );
      marker.position.set(-1.25 + index * 0.5, 3.75, 1.15);
      marker.userData.startsAt = Number(schedule?.startsAt) || 0;
      layer.add(marker);
    });
    garden.add(layer);
    garden.userData.sharedMediaSpaces = layer;
  }

  function updateIdentity(nextIdentity) {
    Object.assign(identity, nextIdentity);
    updateAvatarBadge(THREE, player, identity, false);
    syncOperatorBelt(THREE, player, identity.nodes?.length || 0);
    updateAvatarBadge(THREE, officeLobbyPlayer, identity, false);
    syncOperatorBelt(
      THREE,
      officeLobbyPlayer,
      identity.nodes?.length || 0,
    );
    updatePlayerLabel(playerLabel, identity);
    if (identity.isAdmin !== true) {
      remotePlayers.forEach((avatar, peerId) => {
        removeRemoteModerationControls(avatar, peerId);
      });
    }
  }

  function updateRepositoryCatalog(repositories = [], activeRepository = {}) {
    const district = landmarkObjects.get("repositories");
    const districtPortal = district?.userData?.portal;
    if (!districtPortal) return;
    const records = (Array.isArray(repositories) ? repositories : [])
      .filter((record) => record && (record.owner || record.name))
      .slice(0, REPOSITORY_CATALOG_MAX)
      .map((record) => {
        const owner = String(record.owner || "external").slice(0, 40);
        const name = String(record.name || "repository").slice(0, 60);
        const rawBytes = Number(record.sizeBytes);
        const sizeBytes =
          Number.isSafeInteger(rawBytes) && rawBytes >= 0
            ? Math.min(rawBytes, 2 ** 50)
            : 0;
        const rawStarCount = Number(record.starCount);
        const rawFollowerCount = Number(record.fediverseFollowerCount);
        return {
          owner,
          name,
          key: `${owner.toLocaleLowerCase()}/${name.toLocaleLowerCase()}`,
          sizeBytes,
          liveHost: record.liveHost === true,
          isPrivate: record.isPrivate === true,
          source: String(record.source || "").slice(0, 40),
          starCount:
            Number.isSafeInteger(rawStarCount) && rawStarCount >= 0
              ? Math.min(rawStarCount, 10_000_000)
              : null,
          starred: record.starred === true,
          fediverseFollowerCount:
            Number.isSafeInteger(rawFollowerCount) && rawFollowerCount >= 0
              ? Math.min(rawFollowerCount, 10_000_000)
              : null,
          fediverseFollowerStatus: String(
            record.fediverseFollowerStatus || "idle",
          ).slice(0, 20),
          fediverseFollowers: Array.isArray(record.fediverseFollowers)
            ? record.fediverseFollowers.slice(0, REPOSITORY_FOLLOWERS_VISIBLE)
            : [],
        };
      })
      .sort((left, right) => left.key.localeCompare(right.key));
    const activeKey = `${
      String(activeRepository?.owner || "").toLocaleLowerCase()
    }/${
      String(
        activeRepository?.repo || activeRepository?.name || "",
      ).toLocaleLowerCase()
    }`;
    const catalogSignature = JSON.stringify([
      activeKey,
      records.map((record) => [
        record.key,
        record.sizeBytes,
        record.liveHost,
        record.isPrivate,
        record.source,
        record.starCount,
        record.starred,
        record.fediverseFollowerCount,
        record.fediverseFollowerStatus,
        // Identity only: a follower's avatar/bio arriving does not need a
        // portal rebuild, but a different follower does.
        record.fediverseFollowers.map((follower) => follower?.handle || ""),
      ]),
    ]);
    const previousCatalog = world.userData.repositoryCatalogLayer;
    if (
      catalogSignature === world.userData.repositoryCatalogSignature &&
      (records.length
        ? previousCatalog?.parent === world
        : !previousCatalog)
    ) {
      return;
    }
    removeGeneratedLayer(world, previousCatalog, interactive);
    world.userData.repositoryCatalogLayer = null;
    world.userData.repositorySizeLayer = null;
    world.userData.repositorySizeMount = null;
    world.userData.repositoryRecordDeskLayer = null;
    world.userData.repositoryRecordDeskMount = null;
    world.userData.repositoryRecordDeskData = null;
    world.userData.repositoryPortalMeshes = [];
    world.userData.repositoryCatalogSignature = catalogSignature;
    repositoryPortals.clear();
    if (district.userData.legacyFiles) {
      district.userData.legacyFiles.visible = !records.length;
    }
    if (!records.length) return;

    const layer = new THREE.Group();
    layer.name = "repository-perimeter-portals";
    const guide = new THREE.Mesh(
      new THREE.TorusGeometry(REPOSITORY_EDGE_RADIUS, 0.045, 6, 256),
      makeMaterial(THREE, "#77d9ff", {
        emissive: "#1e637c",
        emissiveIntensity: 0.45,
        transparent: true,
        opacity: 0.38,
        roughness: 0.5,
      }),
    );
    guide.name = "repository-perimeter-guide";
    guide.rotation.x = Math.PI / 2;
    guide.position.y = 0.12;
    layer.add(guide);

    const maxBytes = Math.max(0, ...records.map((record) => record.sizeBytes));
    const portalSpacing =
      (Math.PI * 2 * REPOSITORY_EDGE_RADIUS) /
      Math.max(1, records.length);
    const portalDensityScale = clamp(
      (portalSpacing - 0.06) / 3.15,
      0.58,
      1,
    );
    const diskGeometry = new THREE.CircleGeometry(1, 36);
    const outlineGeometry = new THREE.TorusGeometry(1.08, 0.065, 8, 40);
    const baseGeometry = new THREE.BoxGeometry(2.35, 0.22, 1.15);
    const materials = {
      live: makeMaterial(THREE, "#77d9ff", {
        emissive: "#207f9e",
        emissiveIntensity: 0.74,
        metalness: 0.25,
        roughness: 0.34,
      }),
      selected: makeMaterial(THREE, "#9ef7c6", {
        emissive: "#2ca76c",
        emissiveIntensity: 1.1,
        metalness: 0.25,
        roughness: 0.26,
      }),
      private: makeMaterial(THREE, "#d5b6ff", {
        emissive: "#7650a7",
        emissiveIntensity: 0.76,
        metalness: 0.24,
        roughness: 0.36,
      }),
      stub: makeMaterial(THREE, "#748a82", {
        emissive: "#294139",
        emissiveIntensity: 0.25,
        metalness: 0.08,
        roughness: 0.72,
      }),
      outline: makeMaterial(THREE, "#b9edff", {
        emissive: "#3aa1c7",
        emissiveIntensity: 0.78,
        metalness: 0.2,
        roughness: 0.35,
      }),
      base: makeMaterial(THREE, "#102a32", {
        emissive: "#163f49",
        emissiveIntensity: 0.24,
        roughness: 0.72,
      }),
    };
    const usedMaterials = new Set();
    const portalMeshes = [];
    records.forEach((record, index) => {
      const angle =
        Math.PI / 2 +
        (index / Math.max(1, records.length)) * Math.PI * 2;
      const isActive = record.key === activeKey;
      const material = isActive
        ? materials.selected
        : record.isPrivate
          ? materials.private
          : record.liveHost
            ? materials.live
            : materials.stub;
      const outlineMaterial = isActive
        ? materials.selected
        : materials.outline;
      usedMaterials.add(material);
      usedMaterials.add(outlineMaterial);
      usedMaterials.add(materials.base);
      const sizeRatio =
        maxBytes > 0 && record.sizeBytes > 0
          ? Math.log1p(record.sizeBytes) / Math.log1p(maxBytes)
          : 0.36;
      const nodeRadius =
        0.72 + sizeRatio * 0.2 + (isActive ? 0.08 : 0);
      const node = new THREE.Group();
      node.name = `repository-portal:${record.owner}/${record.name}`;
      node.position.set(
        Math.cos(angle) * REPOSITORY_EDGE_RADIUS,
        2.55,
        Math.sin(angle) * REPOSITORY_EDGE_RADIUS,
      );
      node.rotation.y = -angle - Math.PI / 2;
      const face = new THREE.Group();
      face.name = `repository-portal-face:${record.owner}/${record.name}`;
      face.scale.setScalar(portalDensityScale);
      const disk = new THREE.Mesh(diskGeometry, material);
      disk.position.z = 0.016;
      disk.scale.setScalar(nodeRadius);
      const outline = new THREE.Mesh(
        outlineGeometry,
        outlineMaterial,
      );
      outline.scale.setScalar(nodeRadius);
      const repositoryPortal = {
        owner: record.owner,
        name: record.name,
        sizeBytes: record.sizeBytes,
        liveHost: record.liveHost,
        isPrivate: record.isPrivate,
        source: record.source,
        starCount: record.starCount,
        starred: record.starred,
        angle,
      };
      for (const mesh of [disk, outline]) {
        mesh.userData.landmark = "repositories";
        mesh.userData.repositoryPortal = repositoryPortal;
        interactive.push(mesh);
        portalMeshes.push(mesh);
      }
      face.add(disk, outline);
      if (isActive) {
        const selectedHalo = new THREE.Mesh(
          new THREE.TorusGeometry(1.38, 0.055, 8, 40),
          materials.selected,
        );
        selectedHalo.name = "repository-selected-halo";
        selectedHalo.scale.setScalar(nodeRadius);
        face.add(selectedHalo);
        usedMaterials.add(materials.selected);
      }
      node.add(face);

      const label = repositorySizeLabelSprite(
        THREE,
        `${record.owner}/${record.name}`,
        record.isPrivate
          ? "AUTHORIZED PRIVATE"
          : record.liveHost
            ? record.sizeBytes
              ? `${compactSceneBytes(record.sizeBytes)} HOSTED`
              : "LIVE MIRROR"
            : "STUB · MIRROR NEEDED",
        isActive ? "#9ef7c6" : record.isPrivate ? "#d5b6ff" : "#77d9ff",
      );
      label.name = `repository-portal-label:${record.owner}/${record.name}`;
      label.scale.set(2.8, 0.76, 1);
      label.position.set(0, -1.42, 0.12);
      label.visible = isActive;
      node.add(label);

      if (isActive && !record.isPrivate) {
        const starData = {
          owner: record.owner,
          name: record.name,
          starCount: record.starCount,
          starred: record.starred,
        };
        // This is intentionally a fixed plane, not a 3D extrusion or a
        // camera-following sprite. Every repository therefore gets the same
        // readable, point-up star control when approached from the ring.
        const starButton = new THREE.Mesh(
          new THREE.PlaneGeometry(1.92, 1.92),
          new THREE.MeshBasicMaterial({
            map: repositoryStarPlaneTexture(
              THREE,
              record.starCount,
              record.starred,
            ),
            transparent: true,
            side: THREE.DoubleSide,
            depthWrite: false,
            toneMapped: false,
          }),
        );
        starButton.name = `repository-star-button:${record.owner}/${record.name}`;
        starButton.position.set(0, 4.05, 0.5);
        starButton.userData.landmark = "repositories";
        starButton.userData.repositoryStar = starData;
        node.add(starButton);
        interactive.push(starButton);
        const starCaption = repositorySizeLabelSprite(
          THREE,
          Number.isSafeInteger(record.starCount)
            ? `${record.starCount.toLocaleString("en-US")} STARS`
            : "STARS",
          record.starred ? "STARRED · CLICK TO REMOVE" : "CLICK TO STAR",
          record.starred ? "#f7c96b" : "#9ef7c6",
        );
        starCaption.name = `repository-star-caption:${record.owner}/${record.name}`;
        starCaption.scale.set(2.4, 0.68, 1);
        starCaption.position.set(0, 2.92, 0.5);
        node.add(starCaption);
      }
      if (isActive) {
        const createButton = new THREE.Mesh(
          new THREE.BoxGeometry(0.9, 0.34, 0.18),
          makeMaterial(THREE, "#9ef7c6", {
            emissive: "#2ca76c",
            emissiveIntensity: 0.9,
            metalness: 0.28,
          }),
        );
        createButton.name = "repository-create-button";
        createButton.position.set(1.45, 0, 0.24);
        createButton.userData.createRepository = true;
        node.add(createButton);
        interactive.push(createButton);
        const createLabel = makeLabelSprite(THREE, "+ REPO", "CREATE", "#9ef7c6");
        createLabel.scale.set(0.9, 0.3, 1);
        createLabel.position.set(1.45, 0, 0.38);
        node.add(createLabel);
      }

      const base = new THREE.Mesh(baseGeometry, materials.base);
      base.name = `repository-portal-base:${record.owner}/${record.name}`;
      base.position.set(0, -2.38, 0);
      base.scale.x = portalDensityScale;
      // The foundation is a direct link to the repository page. Keep its
      // action distinct from the face, which selects the portal in-world.
      base.userData.landmark = "repositories";
      base.userData.repositoryBase = repositoryPortal;
      interactive.push(base);
      node.add(base);
      if (isActive) {
        // The selected repository's canonical name is engraved into a physical
        // stone at the base of its face, so it remains identifiable even when
        // the orbit labels are hidden by the surrounding geometry.
        const plaqueStone = new THREE.Mesh(
          new THREE.BoxGeometry(2.28, 0.48, 0.2),
          makeMaterial(THREE, "#778078", { roughness: 0.9 }),
        );
        plaqueStone.position.set(0, -2.0, 0.18);
        const plaqueFace = new THREE.Mesh(
          new THREE.PlaneGeometry(2.08, 0.31),
          new THREE.MeshBasicMaterial({
            map: repositoryStonePlaqueTexture(
              THREE,
              `${record.owner}/${record.name}`,
            ),
            toneMapped: false,
          }),
        );
        plaqueFace.position.set(0, -2.0, 0.295);
        plaqueStone.userData.landmark = "repositories";
        plaqueStone.userData.repositoryBase = repositoryPortal;
        plaqueFace.userData.landmark = "repositories";
        plaqueFace.userData.repositoryBase = repositoryPortal;
        interactive.push(plaqueStone, plaqueFace);
        node.add(plaqueStone, plaqueFace);
      }
      // Who follows this repository over ActivityPub, seated on the ground
      // inside the ring and looking back up at the circle. Only the selected
      // repository draws its gallery: these are real remote accounts read from
      // ap_followers, not a decoration, and 200 crowds would bury the district.
      if (isActive && !record.isPrivate) {
        const followers = Array.isArray(record.fediverseFollowers)
          ? record.fediverseFollowers.filter(
              (follower) =>
                follower &&
                String(follower.handle || follower.profileUrl || "").trim(),
            )
          : [];
        const reportedFollowers = Number.isSafeInteger(
          record.fediverseFollowerCount,
        )
          ? record.fediverseFollowerCount
          : followers.length;
        const followerStatus = String(record.fediverseFollowerStatus || "idle");
        const gallery = new THREE.Group();
        gallery.name = `repository-fediverse-followers:${record.owner}/${record.name}`;
        // Ground level for this portal (the plinth sits at -2.38) and inside
        // the perimeter ring, so the crowd faces back out at the circle.
        const seated = followers.slice(0, REPOSITORY_FOLLOWERS_VISIBLE);
        seated.forEach((follower, followerIndex) => {
          // Two per row, rows stepping inward: each card then sits at its own
          // depth, so eight of them never overlap into an unreadable pile.
          const column = followerIndex % 2;
          const row = Math.floor(followerIndex / 2);
          const figure = makeRepositoryFollowerFigure(THREE, follower);
          figure.position.set(
            // Odd rows sit in the gaps of the row in front of them, so a
            // head-on view never buries one card behind another.
            (column - 0.5) * 2.72 + (row % 2 ? 1.36 : 0),
            // The portal plinth ends at -2.38; -2.53 puts them on the ground.
            -2.53,
            2.2 + row * 2.4,
          );
          // Turn around to look back up at the circle they follow.
          figure.rotation.y = Math.PI;
          const card = figure.getObjectByName(
            `repository-follower-card:${follower.handle || follower.profileUrl}`,
          );
          // Each row further back holds its card a little higher, so the whole
          // gallery reads as tiers rather than one overlapping pile.
          if (card) card.position.y += row * 0.62;
          gallery.add(figure);
        });
        const caption = repositorySizeLabelSprite(
          THREE,
          followerStatus === "loading" && !reportedFollowers
            ? "FEDIVERSE FOLLOWERS"
            : `${reportedFollowers.toLocaleString("en-US")} FEDIVERSE ${
                reportedFollowers === 1 ? "FOLLOWER" : "FOLLOWERS"
              }`,
          followerStatus === "unavailable"
            ? "FOLLOWER LIST UNAVAILABLE"
            : followerStatus === "loading"
              ? "READING ap_followers…"
              : reportedFollowers > seated.length
                ? `WATCHING · SHOWING ${seated.length}`
                : reportedFollowers
                  ? "WATCHING THIS REPOSITORY"
                  : "NOBODY FOLLOWS THIS REPOSITORY YET",
          "#d5b6ff",
        );
        caption.name = `repository-fediverse-follower-caption:${record.owner}/${record.name}`;
        caption.scale.set(3.4, 0.96, 1);
        caption.position.set(0, -1.86, 1.1);
        gallery.add(caption);
        node.add(gallery);
        node.userData.fediverseFollowers = seated;
      }
      node.userData.repositoryPortal = repositoryPortal;
      node.userData.repositoryFace = face;
      node.userData.repositoryLabel = label;
      layer.add(node);
      repositoryPortals.set(record.key, {
        group: node,
        owner: record.owner,
        name: record.name,
        angle,
      });
    });

    world.add(layer);
    Object.values(materials).forEach((material) => {
      if (!usedMaterials.has(material)) material.dispose();
    });
    world.userData.repositoryCatalogLayer = layer;
    world.userData.repositoryPortalMeshes = portalMeshes;
  }

  function updateRepositorySizeMap(sizeTree = {}, selection = {}) {
    const district = landmarkObjects.get("repositories");
    const districtPortal = district?.userData?.portal;
    if (!districtPortal) return;
    const previousLayer = world.userData.repositorySizeLayer;
    const previousMount =
      world.userData.repositorySizeMount || previousLayer?.parent;
    removeGeneratedLayer(previousMount, previousLayer, interactive);
    world.userData.repositorySizeLayer = null;
    world.userData.repositorySizeMount = null;
    world.userData.repositorySizeLoadingTail = null;
    repositoryPortals.forEach((record) => {
      if (record.group.userData.repositoryFace) {
        record.group.userData.repositoryFace.visible = true;
      }
      if (record.group.userData.repositoryLabel) {
        record.group.userData.repositoryLabel.position.y = -1.42;
      }
      record.group.userData.repositorySizeLayer = null;
      record.group.userData.repositorySizeMeshes = [];
      record.group.userData.repositorySizeFocus = "";
    });
    if (district.userData.legacyFiles) {
      district.userData.legacyFiles.visible = repositoryPortals.size === 0;
    }
    if (districtPortal.userData.repositoryCore) {
      districtPortal.userData.repositoryCore.visible = true;
    }
    if (districtPortal.userData.repositoryEntityLayer) {
      districtPortal.userData.repositoryEntityLayer.visible =
        repositoryPortals.size === 0;
    }
    if (districtPortal.userData.relationshipLines) {
      districtPortal.userData.relationshipLines.visible =
        repositoryPortals.size === 0;
    }

    const owner = String(selection?.owner || "").slice(0, 40);
    const name = String(selection?.repo || selection?.name || "").slice(0, 60);
    const repositoryKey =
      `${owner.toLocaleLowerCase()}/${name.toLocaleLowerCase()}`;
    const mount = repositoryPortals.get(repositoryKey)?.group;
    const totalSize = Math.max(0, Number(sizeTree?.size) || 0);
    const children = Array.isArray(sizeTree?.children)
      ? sizeTree.children
      : [];
    if (!mount || !(totalSize > 0) || !children.length) return;

    const map = repositorySizeMapSegments(sizeTree, selection?.path || "");
    if (!map.segments.length) return;
    const layer = new THREE.Group();
    layer.name = "repository-3d-size-map";
    // Lift the sunburst clear of the portal base so its lowest wedges do not
    // intersect the ground plane.
    layer.position.y = 0.55;
    layer.position.z = 0.18;
    layer.userData.repositorySizeFocus = map.path;
    layer.userData.repositoryKey = repositoryKey;
    const innerRadius = 0.72;
    const outerRadius = 2.5;
    const ringWidth = (outerRadius - innerRadius) / map.ringCount;
    const focusSize = Math.max(1, Number(map.node?.size) || totalSize);
    const sizeMeshes = [];
    let labelCount = 0;

    // A small comet-like tail lives in the gap between the hub (the current
    // folder) and the first directory ring. It is only visible while a folder
    // or file request is in flight, so the map itself stays readable.
    const loadingTail = new THREE.Group();
    loadingTail.name = "repository-size-map-loading-tail";
    [
      { offset: 0, arc: 0.22, opacity: 1 },
      { offset: -0.27, arc: 0.16, opacity: 0.7 },
      { offset: -0.49, arc: 0.1, opacity: 0.42 },
    ].forEach(({ offset, arc, opacity }) => {
      const trail = new THREE.Mesh(
        new THREE.TorusGeometry(innerRadius + 0.12, 0.035, 7, 16, arc),
        new THREE.MeshBasicMaterial({
          color: "#fff1ad",
          transparent: true,
          opacity,
          toneMapped: false,
        }),
      );
      trail.rotation.z = offset;
      trail.position.z = 0.3;
      loadingTail.add(trail);
    });
    const spark = new THREE.Mesh(
      new THREE.SphereGeometry(0.075, 12, 8),
      makeMaterial(THREE, "#ffffff", {
        emissive: "#92f4ff",
        emissiveIntensity: 2.2,
      }),
    );
    spark.position.set(innerRadius + 0.12, 0, 0.31);
    loadingTail.add(spark);
    loadingTail.visible = Boolean(world.userData.repositorySizeLoading);
    layer.add(loadingTail);

    map.segments.forEach((segment, index) => {
      const radialGap = 0.025;
      const inner =
        innerRadius + (segment.depth - 1) * ringWidth + radialGap;
      const outer =
        innerRadius + segment.depth * ringWidth - radialGap;
      const extrusion =
        0.075 +
        Math.min(
          0.12,
          (Math.log1p(segment.size) / Math.log1p(focusSize)) * 0.1,
        );
      const geometry = repositoryWedgeGeometry(
        THREE,
        inner,
        outer,
        segment.from,
        segment.to,
        extrusion,
      );
      if (!geometry) return;
      const capMaterial = new THREE.MeshBasicMaterial({
        color: segment.color,
        transparent: segment.type === "summary",
        opacity: segment.type === "summary" ? 0.58 : 0.98,
        side: THREE.DoubleSide,
        toneMapped: false,
      });
      const sideColor = new THREE.Color(segment.color).multiplyScalar(0.55);
      const sideMaterial = makeMaterial(THREE, sideColor, {
        emissive: sideColor,
        emissiveIntensity: 0.14,
        metalness: 0.12,
        roughness: 0.44,
        transparent: segment.type === "summary",
        opacity: segment.type === "summary" ? 0.5 : 1,
        side: THREE.DoubleSide,
      });
      const mesh = new THREE.Mesh(geometry, [capMaterial, sideMaterial]);
      mesh.name = `repository-size-segment-${index}`;
      mesh.position.z = segment.depth * 0.018;
      mesh.userData.landmark = "repositories";
      mesh.userData.path = segment.path;
      mesh.userData.kind = segment.type;
      mesh.userData.repositorySizeNode = {
        owner: String(selection?.owner || "").slice(0, 40),
        name: String(selection?.repo || selection?.name || "").slice(0, 60),
        commit: String(selection?.commit || "").slice(0, 64),
        path: segment.path,
        label: segment.name,
        type: segment.type,
        size: segment.size,
        percent: Math.max(
          0,
          Math.min(100, (segment.size / focusSize) * 100),
        ),
      };
      layer.add(mesh);
      sizeMeshes.push(mesh);
      if (segment.type !== "summary") interactive.push(mesh);

      const span = segment.to - segment.from;
      if (
        labelCount < 18 &&
        segment.depth <= 3 &&
        span >= 0.34 &&
        segment.type !== "summary"
      ) {
        const middle = (segment.from + segment.to) * 0.5;
        const labelRadius = (inner + outer) * 0.5;
        const label = repositorySizeLabelSprite(
          THREE,
          segment.name.slice(0, 18),
          compactSceneBytes(segment.size),
          segment.color,
        );
        label.name = `repository-size-label-${index}`;
        label.scale.set(Math.min(1.55, 0.92 + span * 0.32), 0.42, 1);
        label.position.set(
          Math.cos(middle) * labelRadius,
          Math.sin(middle) * labelRadius,
          0.24 + segment.depth * 0.018,
        );
        layer.add(label);
        labelCount += 1;
      }
    });

    const hub = new THREE.Mesh(
      new THREE.CylinderGeometry(0.64, 0.64, 0.18, 48),
      makeMaterial(THREE, "#0b2630", {
        emissive: "#1b6e87",
        emissiveIntensity: 0.58,
        metalness: 0.2,
        roughness: 0.36,
      }),
    );
    hub.name = "repository-size-map-hub";
    hub.rotation.x = Math.PI / 2;
    hub.position.z = 0.055;
    hub.userData.landmark = "repositories";
    const parentPath = map.path.split("/").slice(0, -1).join("/");
    hub.userData.repositorySizeNode = {
      owner: String(selection?.owner || "").slice(0, 40),
      name: String(selection?.repo || selection?.name || "").slice(0, 60),
      commit: String(selection?.commit || "").slice(0, 64),
      path: map.path,
      targetPath: parentPath,
      label: map.path.split("/").pop() || selection?.repo || "repository",
      type: "center",
      size: focusSize,
      percent: 100,
    };
    interactive.push(hub);
    layer.add(hub);

    const title = repositorySizeLabelSprite(
      THREE,
      String(
        map.path.split("/").pop() ||
          selection?.repo ||
          selection?.name ||
          "repository",
      ).slice(0, 18),
      compactSceneBytes(focusSize),
      "#9ef7c6",
    );
    title.name = "repository-size-map-title";
    title.scale.set(1.34, 0.45, 1);
    title.position.z = 0.26;
    layer.add(title);

    mount.add(layer);
    world.userData.repositorySizeLayer = layer;
    world.userData.repositorySizeMount = mount;
    world.userData.repositorySizeLoadingTail = loadingTail;
    mount.userData.repositorySizeLayer = layer;
    mount.userData.repositorySizeMeshes = sizeMeshes;
    mount.userData.repositorySizeFocus = map.path;
    if (mount.userData.repositoryFace) {
      mount.userData.repositoryFace.visible = false;
    }
    if (mount.userData.repositoryLabel) {
      mount.userData.repositoryLabel.position.y = -3.05;
    }
    if (district.userData.legacyFiles) {
      district.userData.legacyFiles.visible = false;
    }
    if (districtPortal.userData.repositoryEntityLayer) {
      districtPortal.userData.repositoryEntityLayer.visible = false;
    }
    if (districtPortal.userData.relationshipLines) {
      districtPortal.userData.relationshipLines.visible = false;
    }
  }

  function setRepositorySizeLoading(loading = false) {
    world.userData.repositorySizeLoading = Boolean(loading);
    const tail = world.userData.repositorySizeLoadingTail;
    if (tail) tail.visible = Boolean(loading);
    const perimeter = world.userData.repositorySizeLayer?.getObjectByName(
      "repository-size-map-perimeter",
    );
    if (perimeter) perimeter.userData.loading = Boolean(loading);
  }

  const REPOSITORY_ISSUE_PAGES_VISIBLE = 10;
  const REPOSITORY_PULL_CARDS_VISIBLE = 5;

  function safeRecordNumber(value) {
    const number = Number(value);
    return Number.isSafeInteger(number) && number >= 1 && number <= 10_000_000
      ? number
      : 0;
  }

  // The open crate of issue pages and the pull-request review board that stand
  // beside the selected repository portal. Both are rebuilt from bounded,
  // commit-matched records the shell already verified; the scene never invents
  // an issue or pull number of its own.
  function updateRepositoryRecordDesk(selection = {}, records = {}) {
    const previousLayer = world.userData.repositoryRecordDeskLayer;
    const previousMount =
      world.userData.repositoryRecordDeskMount || previousLayer?.parent;
    removeGeneratedLayer(previousMount, previousLayer, interactive);
    world.userData.repositoryRecordDeskLayer = null;
    world.userData.repositoryRecordDeskMount = null;
    world.userData.repositoryRecordDeskData = null;

    const owner = String(selection?.owner || "").slice(0, 40);
    const name = String(selection?.repo || selection?.name || "").slice(0, 60);
    const repositoryKey =
      `${owner.toLocaleLowerCase()}/${name.toLocaleLowerCase()}`;
    const mount = repositoryPortals.get(repositoryKey)?.group;
    const issues = (Array.isArray(records?.issues) ? records.issues : [])
      .map((record) => ({
        number: safeRecordNumber(record?.number),
        state: ["open", "closed"].includes(record?.state) ? record.state : "",
      }))
      .filter((record) => record.number)
      .sort((left, right) =>
        (left.state === "closed") === (right.state === "closed")
          ? right.number - left.number
          : left.state === "closed"
            ? 1
            : -1,
      )
      .slice(0, REPOSITORY_ISSUE_PAGES_VISIBLE);
    const pulls = (Array.isArray(records?.pulls) ? records.pulls : [])
      .map((record) => ({
        number: safeRecordNumber(record?.number),
        state: ["open", "closed", "merged"].includes(record?.state)
          ? record.state
          : "",
        title: String(record?.title || "").slice(0, 80),
      }))
      .filter((record) => record.number)
      .sort((left, right) =>
        (left.state === "open") === (right.state === "open")
          ? right.number - left.number
          : left.state === "open"
            ? -1
            : 1,
      )
      .slice(0, REPOSITORY_PULL_CARDS_VISIBLE);
    if (!mount || (!issues.length && !pulls.length)) return;

    const expandedIssue = safeRecordNumber(records?.expandedIssue);
    const layer = new THREE.Group();
    layer.name = "repository-record-desk";
    const repositoryName = `${owner}/${name}`;
    // Ground level beside the portal plinth; the plinth base sits at -2.38 and
    // the follower gallery stands on -2.53.
    const groundY = -2.53;

    if (issues.length) {
      const box = new THREE.Group();
      // The portal face points at the ring centre, so the visitor's left is
      // the portal's +x: the issue crate keeps clear of the follower gallery.
      box.name = `repository-issue-box:${repositoryKey}`;
      box.position.set(4.4, 0, 1.35);
      const crateMaterial = makeMaterial(THREE, "#8a6b4a", {
        roughness: 0.86,
        metalness: 0.04,
      });
      const floor = new THREE.Mesh(
        new THREE.BoxGeometry(2.5, 0.12, 1.3),
        crateMaterial,
      );
      floor.position.y = groundY + 0.06;
      box.add(floor);
      [
        // An open box: four walls, no lid, pages standing up out of it.
        { size: [2.5, 1.0, 0.1], position: [0, groundY + 0.56, 0.6] },
        { size: [2.5, 1.0, 0.1], position: [0, groundY + 0.56, -0.6] },
        { size: [0.1, 1.0, 1.3], position: [-1.2, groundY + 0.56, 0] },
        { size: [0.1, 1.0, 1.3], position: [1.2, groundY + 0.56, 0] },
      ].forEach(({ size, position }) => {
        const wall = new THREE.Mesh(
          new THREE.BoxGeometry(...size),
          crateMaterial,
        );
        wall.position.set(...position);
        box.add(wall);
      });
      issues.forEach((issue, index) => {
        const page = new THREE.Mesh(
          new THREE.PlaneGeometry(0.72, 0.96),
          new THREE.MeshBasicMaterial({
            map: repositoryIssuePageTexture(THREE, issue, false, repositoryName),
            side: THREE.DoubleSide,
            toneMapped: false,
          }),
        );
        page.name = `repository-issue-page:${issue.number}`;
        const spread = issues.length > 1 ? index / (issues.length - 1) : 0.5;
        page.position.set(
          -0.95 + spread * 1.9,
          groundY + 1.06,
          -0.34 + (index % 3) * 0.34,
        );
        page.rotation.y = (spread - 0.5) * -0.5;
        page.rotation.z = (index % 2 ? -1 : 1) * 0.05;
        page.userData.landmark = "repositories";
        page.userData.repositoryIssuePage = {
          owner,
          name,
          number: issue.number,
          state: issue.state,
        };
        box.add(page);
        interactive.push(page);
      });
      if (expandedIssue && issues.some((issue) => issue.number === expandedIssue)) {
        const issue = issues.find((candidate) => candidate.number === expandedIssue);
        const sheet = new THREE.Mesh(
          new THREE.PlaneGeometry(2.5, 3.28),
          new THREE.MeshBasicMaterial({
            map: repositoryIssuePageTexture(THREE, issue, true, repositoryName),
            side: THREE.DoubleSide,
            toneMapped: false,
          }),
        );
        sheet.name = `repository-issue-page-expanded:${issue.number}`;
        sheet.position.set(0, groundY + 3.1, 0.75);
        sheet.userData.landmark = "repositories";
        sheet.userData.repositoryIssuePage = {
          owner,
          name,
          number: issue.number,
          state: issue.state,
          expanded: true,
        };
        box.add(sheet);
        interactive.push(sheet);
      }
      const caption = repositorySizeLabelSprite(
        THREE,
        `${issues.length} ISSUE ${issues.length === 1 ? "PAGE" : "PAGES"}`,
        expandedIssue ? "CLICK AGAIN TO OPEN" : "CLICK A PAGE TO EXPAND",
        "#f7c96b",
      );
      caption.name = `repository-issue-box-caption:${repositoryKey}`;
      caption.scale.set(2.9, 0.82, 1);
      caption.position.set(0, groundY + 1.86, 0.9);
      box.add(caption);
      layer.add(box);
    }

    if (pulls.length) {
      const desk = new THREE.Group();
      // Mirrored to the visitor's right: the review board for pull requests.
      desk.name = `repository-pull-desk:${repositoryKey}`;
      desk.position.set(-4.4, 0, 1.35);
      const boardHeight = 0.62 + pulls.length * 0.6;
      const board = new THREE.Mesh(
        new THREE.BoxGeometry(2.75, boardHeight, 0.12),
        makeMaterial(THREE, "#171429", {
          emissive: "#241d45",
          emissiveIntensity: 0.32,
          roughness: 0.6,
        }),
      );
      board.position.set(0, groundY + 0.8 + boardHeight / 2, 0);
      desk.add(board);
      [-1.05, 1.05].forEach((legX) => {
        const leg = new THREE.Mesh(
          new THREE.BoxGeometry(0.14, 0.9, 0.14),
          makeMaterial(THREE, "#0e0c1c", { roughness: 0.7 }),
        );
        leg.position.set(legX, groundY + 0.45, 0);
        desk.add(leg);
      });
      pulls.forEach((pull, index) => {
        const card = new THREE.Mesh(
          new THREE.PlaneGeometry(2.45, 0.54),
          new THREE.MeshBasicMaterial({
            map: repositoryPullCardTexture(THREE, pull),
            toneMapped: false,
          }),
        );
        card.name = `repository-pull-card:${pull.number}`;
        card.position.set(
          0,
          groundY + 0.8 + boardHeight - 0.56 - index * 0.6,
          0.08,
        );
        card.userData.landmark = "repositories";
        card.userData.repositoryPullPage = {
          owner,
          name,
          number: pull.number,
          state: pull.state,
        };
        desk.add(card);
        interactive.push(card);
      });
      const caption = repositorySizeLabelSprite(
        THREE,
        `${pulls.length} PULL ${pulls.length === 1 ? "REQUEST" : "REQUESTS"}`,
        "CLICK TO REVIEW THE DIFF",
        "#d5b6ff",
      );
      caption.name = `repository-pull-desk-caption:${repositoryKey}`;
      caption.scale.set(2.9, 0.82, 1);
      caption.position.set(0, groundY + boardHeight + 1.4, 0.4);
      desk.add(caption);
      layer.add(desk);
    }

    mount.add(layer);
    world.userData.repositoryRecordDeskLayer = layer;
    world.userData.repositoryRecordDeskMount = mount;
    world.userData.repositoryRecordDeskData = { selection, records };
  }

  function setRepositoryIssuePageExpanded(number = 0) {
    const data = world.userData.repositoryRecordDeskData;
    if (!data) return;
    updateRepositoryRecordDesk(data.selection, {
      ...data.records,
      expandedIssue: safeRecordNumber(number),
    });
  }

  function updateRepositoryGraph(entries = [], entities = []) {
    const district = landmarkObjects.get("repositories");
    const meshes = district?.userData?.fileMeshes || [];
    const portal = district?.userData?.portal;
    const safeEntries = Array.isArray(entries) ? entries.slice(0, meshes.length) : [];
    const safeEntities = Array.isArray(entities)
      ? entities
          .filter(
            (entity) =>
              entity &&
              ["contributor", "issue", "issue-collection", "pull-request",
                "pull-request-collection"].includes(String(entity.kind || "")),
          )
          .slice(0, 32)
      : [];
    const maxSize = Math.max(
      1,
      ...safeEntries.map((entry) => Math.max(0, Number(entry?.size) || 0)),
    );
    const byPath = new Map(
      safeEntries.map((entry, index) => [String(entry.path || ""), index]),
    );
    const edgeIndexes = [];
    const connected = new Set();
    safeEntries.forEach((entry, sourceIndex) => {
      const dependencies = Array.isArray(entry.dependencies)
        ? entry.dependencies
        : [];
      dependencies.forEach((dependency) => {
        const targetIndex = byPath.get(String(dependency));
        if (targetIndex === undefined || targetIndex === sourceIndex) return;
        const key = [sourceIndex, targetIndex].sort((a, b) => a - b).join(":");
        if (connected.has(key)) return;
        connected.add(key);
        edgeIndexes.push([sourceIndex, targetIndex]);
      });
    });
    // A small deterministic force layout makes the actual import graph legible:
    // spring forces pull adjacent files together, while repulsion prevents one
    // dense dependency component from collapsing into a single icon.
    const layout = safeEntries.map((entry, index) => {
      const angle = (index / Math.max(1, safeEntries.length)) * Math.PI * 2;
      const directoryBias =
        String(entry.path || "")
          .split("/")
          .slice(0, -1)
          .join("")
          .split("")
          .reduce((total, character) => total + character.charCodeAt(0), 0) %
        13;
      return {
        x: Math.cos(angle) * (1.45 + (directoryBias % 3) * 0.12),
        z: Math.sin(angle) * (0.52 + (directoryBias % 2) * 0.08),
      };
    });
    for (let iteration = 0; iteration < 36; iteration += 1) {
      const forces = layout.map(() => ({ x: 0, z: 0 }));
      for (let left = 0; left < layout.length; left += 1) {
        for (let right = left + 1; right < layout.length; right += 1) {
          let dx = layout[right].x - layout[left].x;
          let dz = layout[right].z - layout[left].z;
          const distanceSquared = Math.max(0.04, dx * dx + dz * dz);
          const distance = Math.sqrt(distanceSquared);
          dx /= distance;
          dz /= distance;
          const push = 0.018 / distanceSquared;
          forces[left].x -= dx * push;
          forces[left].z -= dz * push;
          forces[right].x += dx * push;
          forces[right].z += dz * push;
        }
      }
      edgeIndexes.forEach(([sourceIndex, targetIndex]) => {
        let dx = layout[targetIndex].x - layout[sourceIndex].x;
        let dz = layout[targetIndex].z - layout[sourceIndex].z;
        const distance = Math.max(0.001, Math.hypot(dx, dz));
        dx /= distance;
        dz /= distance;
        const pull = (distance - 0.72) * 0.075;
        forces[sourceIndex].x += dx * pull;
        forces[sourceIndex].z += dz * pull;
        forces[targetIndex].x -= dx * pull;
        forces[targetIndex].z -= dz * pull;
      });
      layout.forEach((point, index) => {
        point.x = THREE.MathUtils.clamp(
          point.x + forces[index].x * 0.65 - point.x * 0.008,
          -2.1,
          2.1,
        );
        point.z = THREE.MathUtils.clamp(
          point.z + forces[index].z * 0.65 - point.z * 0.006,
          -0.85,
          0.85,
        );
      });
    }
    meshes.forEach((mesh, index) => {
      const entry = safeEntries[index];
      mesh.visible = Boolean(entry);
      if (!entry) return;
      const ratio = Math.sqrt(Math.max(0, Number(entry.size) || 0) / maxSize);
      const scale = entry.type === "directory" ? 1.55 : 0.72 + ratio * 1.5;
      mesh.scale.setScalar(scale);
      mesh.userData.path = String(entry.path || entry.name || "").slice(0, 160);
      mesh.userData.kind = entry.type === "directory" ? "directory" : "file";
      const architecturalLayers = [
        "directory",
        "module",
        "package",
        "database-model",
        "service",
        "api",
        "test",
      ];
      const architecturalLayer = Math.max(
        0,
        architecturalLayers.indexOf(String(entry.role || "module")),
      );
      const depth = Math.min(
        4,
        Math.max(
          String(entry.path || "").split("/").length - 1,
          Number(entry.dependencyDepth) || 0,
        ),
      );
      mesh.userData.layer = depth;
      mesh.userData.radius = 1.0 + depth * 0.42 + (index % 3) * 0.16;
      mesh.userData.angle =
        (architecturalLayer / architecturalLayers.length) * Math.PI * 2 +
        (index % 4) * 0.11;
      const point = layout[index] || { x: 0, z: 0 };
      mesh.userData.layoutPosition = {
        x: point.x,
        y: 2.65 + depth * 0.18 + architecturalLayer * 0.035,
        z: point.z,
      };
      mesh.position.set(
        mesh.userData.layoutPosition.x,
        mesh.userData.layoutPosition.y,
        mesh.userData.layoutPosition.z,
      );
      if (mesh.material) {
        mesh.material.emissiveIntensity =
          entry.frequency === "high" ? 0.9 : entry.frequency === "medium" ? 0.5 : 0.18;
        mesh.material.wireframe = entry.redundantCandidate === true;
      }
    });
    const previousEntityLayer = portal?.userData?.repositoryEntityLayer;
    if (previousEntityLayer && portal) {
      previousEntityLayer.traverse((child) => {
        const interactiveIndex = interactive.indexOf(child);
        if (interactiveIndex >= 0) interactive.splice(interactiveIndex, 1);
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
      portal.remove(previousEntityLayer);
      portal.userData.repositoryEntityLayer = null;
      portal.userData.repositoryEntityMeshes = [];
    }
    const entityRelationships = [];
    if (portal && safeEntities.length) {
      const layer = new THREE.Group();
      layer.name = "repository-entity-layer";
      const entityMeshes = [];
      const entityColors = {
        contributor: "#9ef7c6",
        issue: "#f7c96b",
        "issue-collection": "#f7c96b",
        "pull-request": "#d5b6ff",
        "pull-request-collection": "#d5b6ff",
      };
      const targetIndexFor = (target) => {
        const normalized = String(target || "");
        if (byPath.has(normalized)) return byPath.get(normalized);
        let bestIndex;
        let bestLength = -1;
        safeEntries.forEach((entry, index) => {
          const path = String(entry.path || "");
          if (
            path &&
            (normalized === path || normalized.startsWith(`${path}/`)) &&
            path.length > bestLength
          ) {
            bestIndex = index;
            bestLength = path.length;
          }
        });
        return bestIndex;
      };
      safeEntities.forEach((entity, index) => {
        const kind = String(entity.kind || "");
        let geometry;
        if (kind === "contributor") {
          geometry = new THREE.SphereGeometry(0.19, 14, 10);
        } else if (kind.startsWith("issue")) {
          geometry = new THREE.OctahedronGeometry(0.22, 0);
        } else {
          geometry = new THREE.TorusGeometry(0.18, 0.065, 8, 20);
        }
        const color = entityColors[kind] || "#ffffff";
        const mesh = new THREE.Mesh(
          geometry,
          makeMaterial(THREE, color, {
            emissive: color,
            emissiveIntensity: 0.7,
            metalness: 0.18,
            roughness: 0.4,
          }),
        );
        const angle =
          (index / Math.max(1, safeEntities.length)) * Math.PI * 2 +
          (kind === "contributor" ? 0 : kind.startsWith("issue") ? 0.35 : 0.7);
        const radius =
          kind === "contributor" ? 2.55 : kind.startsWith("issue") ? 2.9 : 3.25;
        mesh.position.set(
          Math.cos(angle) * radius,
          2.5 + (index % 4) * 0.22,
          Math.sin(angle) * 0.78,
        );
        mesh.name = `repository-entity-${kind}-${index}`;
        mesh.userData.landmark = "repositories";
        mesh.userData.graphNode = {
          id: String(entity.id || "").slice(0, 96),
          kind,
          label: String(entity.label || "").slice(0, 100),
          detail: String(entity.detail || "").slice(0, 240),
          href: String(entity.href || "").startsWith("/")
            ? String(entity.href).slice(0, 400)
            : "",
        };
        layer.add(mesh);
        interactive.push(mesh);
        entityMeshes.push(mesh);
        const label = makeLabelSprite(
          THREE,
          String(entity.label || "").slice(0, 22),
          kind.replaceAll("-", " "),
          color,
        );
        label.name = `repository-entity-label-${kind}-${index}`;
        label.scale.set(1.45, 0.48, 1);
        label.position.copy(mesh.position);
        label.position.y += 0.48;
        layer.add(label);
        const seenTargets = new Set();
        (Array.isArray(entity.targetPaths) ? entity.targetPaths : [])
          .slice(0, 24)
          .forEach((target) => {
            const targetIndex = targetIndexFor(target);
            if (
              targetIndex === undefined ||
              seenTargets.has(targetIndex) ||
              !meshes[targetIndex]?.visible
            ) {
              return;
            }
            seenTargets.add(targetIndex);
            entityRelationships.push([mesh, meshes[targetIndex]]);
          });
      });
      portal.add(layer);
      portal.userData.repositoryEntityLayer = layer;
      portal.userData.repositoryEntityMeshes = entityMeshes;
    }
    if (portal?.userData?.relationshipLines) {
      portal.remove(portal.userData.relationshipLines);
      portal.userData.relationshipLines.geometry?.dispose?.();
      portal.userData.relationshipLines.material?.dispose?.();
      portal.userData.relationshipLines = null;
      portal.userData.relationshipPairs = [];
    }
    if (portal && (safeEntries.length > 1 || entityRelationships.length)) {
      const positions = [];
      edgeIndexes.forEach(([sourceIndex, targetIndex]) => {
        positions.push(
          meshes[sourceIndex].position.x,
          meshes[sourceIndex].position.y,
          meshes[sourceIndex].position.z,
          meshes[targetIndex].position.x,
          meshes[targetIndex].position.y,
          meshes[targetIndex].position.z,
        );
      });
      entityRelationships.forEach(([source, target]) => {
        positions.push(
          source.position.x,
          source.position.y,
          source.position.z,
          target.position.x,
          target.position.y,
          target.position.z,
        );
      });
      if (positions.length) {
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute(
          "position",
          new THREE.Float32BufferAttribute(positions, 3),
        );
        const relationships = new THREE.LineSegments(
          geometry,
          new THREE.LineBasicMaterial({
            color: "#77d9ff",
            transparent: true,
            opacity: 0.34,
          }),
        );
        relationships.name = "repository-relationship-lines";
        portal.add(relationships);
        portal.userData.relationshipLines = relationships;
        portal.userData.relationshipPairs = [
          ...edgeIndexes.map(([sourceIndex, targetIndex]) => [
            meshes[sourceIndex],
            meshes[targetIndex],
          ]),
          ...entityRelationships,
        ];
      }
    }
    portal?.scale.setScalar(safeEntries.length ? 1.05 : 1);
  }

  function playEmote(peerId, emote, local = false) {
    const avatar = local
      ? player
      : remotePlayers.get(peerId) || (peerId === identity.id ? player : null);
    if (!avatar) return;
    const glyphs = { wave: "WAVE", idea: "IDEA ✦", celebrate: "NICE ★" };
    const sprite = makeLabelSprite(
      THREE,
      glyphs[emote] || "HELLO",
      "public emote",
      emote === "celebrate"
        ? "#f7c96b"
        : emote === "idea"
          ? "#d5b6ff"
          : "#9ef7c6",
    );
    sprite.scale.set(1.8, 0.6, 1);
    world.add(sprite);
    emoteSprites.push({
      sprite,
      avatar,
      startedAt: performance.now(),
      duration: 1900,
    });
  }

  function showChatBubble(peerId, text, local = false) {
    const message = String(text || "").replace(/\s+/g, " ").trim().slice(0, 140);
    if (!message) return false;
    const avatar =
      local || peerId === identity.id
        ? player
        : peerId === FORKBOT_PEER_ID
          ? forkbot
          : remotePlayers.get(String(peerId || ""));
    if (!avatar) return false;
    // One bubble per speaker: a rapid follow-up message replaces the first
    // instead of stacking on top of it.
    for (let index = emoteSprites.length - 1; index >= 0; index -= 1) {
      const existing = emoteSprites[index];
      if (existing.chat && existing.avatar === avatar) {
        world.remove(existing.sprite);
        existing.sprite.material?.map?.dispose?.();
        existing.sprite.material?.dispose?.();
        emoteSprites.splice(index, 1);
      }
    }
    const sprite = new THREE.Sprite(
      new THREE.SpriteMaterial({
        map: chatBubbleTexture(
          THREE,
          avatar.userData?.name || "visitor",
          message,
        ),
        transparent: true,
        depthTest: false,
        depthWrite: false,
      }),
    );
    sprite.renderOrder = 11;
    sprite.scale.set(6.6, 2.2, 1);
    world.add(sprite);
    emoteSprites.push({
      sprite,
      avatar,
      chat: true,
      startedAt: performance.now(),
      // Longer messages linger longer before fading out.
      duration: Math.min(14000, 10000 + message.length * 30),
      baseHeight: 5.2,
      rise: 0.5,
      fadeStart: 0.75,
    });
    return true;
  }

  // Repaints the fountain's treasury board with the public pool address QR
  // code and the balance reported by /api/accounts/central-fund.
  function updateRewardPool(state = {}) {
    const sign = landmarkObjects.get("fountain")?.userData?.treasurySign;
    if (!sign) return;
    applyRewardTreasury(THREE, sign, state);
  }

  function playRewardEvent(targetHint = "") {
    let targetPylon = null;
    nodeInfrastructure.forEach((pylon) => {
      if (
        !targetPylon &&
        (!targetHint ||
          String(pylon.userData?.nodeId || "")
            .toLowerCase()
            .includes(String(targetHint).toLowerCase()))
      ) {
        targetPylon = pylon;
      }
    });
    const target = targetPylon
      ? targetPylon.position.clone()
      : new THREE.Vector3(...landmarkById("organizations").position);
    target.y = 1.2;
    const start = new THREE.Vector3(...landmarkById("fountain").position);
    start.y = 2.4;
    const group = new THREE.Group();
    const particles = [];
    for (let index = 0; index < 18; index += 1) {
      const coin = new THREE.Mesh(
        new THREE.OctahedronGeometry(0.1 + (index % 3) * 0.025, 0),
        makeMaterial(THREE, index % 2 ? "#f7c96b" : "#9ef7c6", {
          emissive: index % 2 ? "#d49a28" : "#39c783",
          emissiveIntensity: 1.15,
          metalness: 0.5,
        }),
      );
      coin.position.copy(start);
      group.add(coin);
      particles.push(coin);
    }
    world.add(group);
    rewardFlights.push({
      group,
      particles,
      start,
      target,
      startedAt: performance.now(),
      duration: 2800,
    });
  }

  function pointerCoordinates(event) {
    const rect = renderer.domElement.getBoundingClientRect();
    pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  }

  function setCameraZoom(value) {
    const next = Number(value);
    if (!Number.isFinite(next)) return cameraZoom;
    if (cameraMode === "first-person") {
      firstPersonZoom = clamp(next, 0.25, 5);
      camera.fov = clamp(44 / firstPersonZoom, 10, 110);
      camera.updateProjectionMatrix();
      return firstPersonZoom;
    }
    cameraZoom = clamp(next, CAMERA_ZOOM_MIN, CAMERA_ZOOM_MAX);
    scene.fog.density = environmentFogDensity * zoomFogMultiplier();
    return cameraZoom;
  }

  function touchDistance() {
    const points = [...touchPointers.values()];
    if (points.length < 2) return 0;
    return Math.hypot(
      points[0].x - points[1].x,
      points[0].y - points[1].y,
    );
  }

  function beginPinchIfReady() {
    if (touchPointers.size < 2) return;
    const distance = touchDistance();
    if (distance <= 0) return;
    pinchStartDistance = distance;
    pinchStartZoom =
      cameraMode === "first-person" ? firstPersonZoom : cameraZoom;
    pinchActive = true;
    pointerGestureMoved = true;
    renderer.domElement.dataset.dragging = "true";
  }

  function rotateCamera(deltaX, deltaY) {
    if (!Number.isFinite(deltaX) || !Number.isFinite(deltaY)) return;
    // The canvas behaves like a grabbed world: pull the scene with the
    // pointer, so the camera turns opposite to the hand's travel direction.
    cameraYaw -= deltaX * CAMERA_LOOK_SENSITIVITY;
    if (cameraMode === "first-person") {
      firstPersonPitch = clamp(
        firstPersonPitch + deltaY * CAMERA_LOOK_SENSITIVITY,
        FIRST_PERSON_PITCH_MIN,
        FIRST_PERSON_PITCH_MAX,
      );
    } else {
      cameraPitch = clamp(
        cameraPitch + deltaY * CAMERA_LOOK_SENSITIVITY,
        CAMERA_PITCH_MIN,
        CAMERA_PITCH_MAX,
      );
    }
  }

  function handlePointerDown(event) {
    if (event.pointerType === "mouse" && event.button !== 0) return;
    if (event.pointerType === "touch") {
      touchPointers.set(event.pointerId, {
        x: event.clientX,
        y: event.clientY,
      });
      try {
        renderer.domElement.setPointerCapture(event.pointerId);
      } catch (_) {}
      if (touchPointers.size === 2) beginPinchIfReady();
    }
    if (primaryPointerId !== null) return;
    pointerCoordinates(event);
    raycaster.setFromCamera(pointer, camera);
    const pressHits = raycaster.intersectObjects(interactive, false);
    if (layoutEditingEnabled) {
      const handleHit = pressHits.find(
        ({ object }) => object.visible && object.userData?.layoutHandle,
      );
      draggedLayoutObject = handleHit
        ? movableWorldObjects.get(handleHit.object.userData.layoutHandle) ||
          null
        : null;
      if (draggedLayoutObject) {
        // Drag by delta from the press point so grabbing the tiny handle
        // never snaps the object's origin to the pointer.
        const point = layoutGroundPoint(
          draggedLayoutObject,
          event.clientX,
          event.clientY,
        );
        if (point) {
          layoutDragOffset.set(
            draggedLayoutObject.position.x - point.x,
            0,
            draggedLayoutObject.position.z - point.z,
          );
          setActiveLayoutObject(draggedLayoutObject);
        } else {
          draggedLayoutObject = null;
        }
      }
    }
    const logHit = draggedLayoutObject
      ? null
      : pressHits
          .find(({ object }) => object.visible && object.userData?.campfireLog);
    if (logHit) {
      draggedCampfireLog = logHit.object;
      draggedCampfireLog.userData.campfireCarried = true;
      draggedCampfireLog.material.emissive?.set?.("#d88a43");
      draggedCampfireLog.material.emissiveIntensity = 0.5;
    }
    primaryPointerId = event.pointerId;
    pointerStart.set(event.clientX, event.clientY);
    pointerLast.copy(pointerStart);
    pointerGestureMoved = false;
    renderer.domElement.dataset.dragging = "true";
    try {
      renderer.domElement.setPointerCapture(event.pointerId);
    } catch (_) {}
  }

  function handlePointerMove(event) {
    const pointerNow = performance.now();
    if (diagnosticsPointerLastAt > 0) {
      diagnosticsPointerWorstGapMs = Math.max(
        diagnosticsPointerWorstGapMs,
        pointerNow - diagnosticsPointerLastAt,
      );
    }
    diagnosticsPointerLastAt = pointerNow;
    diagnosticsPointerMoves = Math.min(1_000_000, diagnosticsPointerMoves + 1);
    let trackedTouch = false;
    if (event.pointerType === "touch" && touchPointers.has(event.pointerId)) {
      trackedTouch = true;
      touchPointers.set(event.pointerId, {
        x: event.clientX,
        y: event.clientY,
      });
      if (touchPointers.size >= 2) {
        if (!pinchActive) beginPinchIfReady();
        const distance = touchDistance();
        if (pinchActive && distance > 0) {
          // Spreading two fingers zooms in; bringing them together zooms out.
          // Every move is clamped, so repeated gestures can traverse the entire
          // supported near/far range without overshooting it.
          setCameraZoom(
            pinchStartZoom * (pinchStartDistance / distance),
          );
          event.preventDefault();
        }
        // A two-finger gesture is zoom-only. Never let either constituent
        // pointer also rotate the camera.
        pointerGestureMoved = true;
        event.preventDefault();
        return;
      }
    }
    if (event.pointerId !== primaryPointerId) return;
    if (draggedLayoutObject) {
      const point = layoutGroundPoint(
        draggedLayoutObject,
        event.clientX,
        event.clientY,
      );
      if (point) {
        moveWorldObject(
          draggedLayoutObject,
          point.x + layoutDragOffset.x,
          point.z + layoutDragOffset.z,
        );
      }
      pointerGestureMoved = true;
      return;
    }
    if (draggedCampfireLog) {
      const point = groundPointAt(event.clientX, event.clientY);
      if (point) {
        draggedCampfireLog.position.set(
          point.x - campfire.position.x,
          0.34,
          point.z - campfire.position.z,
        );
      }
      pointerGestureMoved = true;
      return;
    }
    const currentPointer = new THREE.Vector2(event.clientX, event.clientY);
    const movedDistance = pointerStart.distanceTo(currentPointer);
    const canRotate =
      event.pointerType === "mouse" ||
      (event.pointerType === "touch" &&
        trackedTouch &&
        touchPointers.size === 1 &&
        !pinchActive);
    if (canRotate && movedDistance > 4) {
      if (pointerGestureMoved) {
        rotateCamera(
          currentPointer.x - pointerLast.x,
          currentPointer.y - pointerLast.y,
        );
      } else {
        pointerGestureMoved = true;
        rotateCamera(
          currentPointer.x - pointerStart.x,
          currentPointer.y - pointerStart.y,
        );
      }
      event.preventDefault();
    } else if (movedDistance > 9) {
      pointerGestureMoved = true;
    }
    pointerLast.copy(currentPointer);
  }

  function finishPointer(event, cancelled = false) {
    const wasPinching = pinchActive || touchPointers.size > 1;
    let remainingTouch = null;
    if (event.pointerType === "touch") {
      touchPointers.delete(event.pointerId);
      if (touchPointers.size < 2) {
        pinchActive = false;
        pinchStartDistance = 0;
      }
      remainingTouch = touchPointers.entries().next().value || null;
    }
    try {
      renderer.domElement.releasePointerCapture(event.pointerId);
    } catch (_) {}
    if (event.pointerId !== primaryPointerId) {
      if (wasPinching && remainingTouch && primaryPointerId !== null) {
        const [, point] = remainingTouch;
        pointerStart.set(point.x, point.y);
        pointerLast.copy(pointerStart);
        pointerGestureMoved = true;
      }
      if (!touchPointers.size) renderer.domElement.dataset.dragging = "false";
      return;
    }
    if (event.pointerType === "touch" && remainingTouch) {
      const [pointerId, point] = remainingTouch;
      primaryPointerId = pointerId;
      pointerStart.set(point.x, point.y);
      pointerLast.copy(pointerStart);
      pointerGestureMoved = true;
      renderer.domElement.dataset.dragging = "true";
      return;
    }
    primaryPointerId = null;
    renderer.domElement.dataset.dragging =
      touchPointers.size ? "true" : "false";
    if (draggedLayoutObject) {
      const movedObject = draggedLayoutObject;
      draggedLayoutObject = null;
      if (layoutCommitTimer) {
        clearTimeout(layoutCommitTimer);
        layoutCommitTimer = 0;
      }
      commitLayoutObject(movedObject);
      lastGestureDragged = true;
      pointerGestureMoved = false;
      return;
    }
    if (draggedCampfireLog) {
      const droppedLog = draggedCampfireLog;
      draggedCampfireLog = null;
      const point = groundPointAt(event.clientX, event.clientY);
      if (point && Math.hypot(point.x - campfire.position.x, point.z - campfire.position.z) < 1.05) {
        droppedLog.visible = false;
        droppedLog.userData.campfireCarried = false;
        fireLevel = Math.min(2.4, fireLevel + 0.28);
      }
      lastGestureDragged = true;
      pointerGestureMoved = false;
      return;
    }
    const suppressTap =
      cancelled ||
      wasPinching ||
      pointerGestureMoved;
    // Remembered past the reset below so a double-click that ended in a camera
    // drag or pinch does not also fire off a dash.
    lastGestureDragged = suppressTap;
    pointerGestureMoved = false;
    if (suppressTap) return;

    // Use the down coordinates so a small amount of click jitter cannot select
    // an object that was not underneath the visible cursor at press time.
    pointerCoordinates({
      clientX: pointerStart.x,
      clientY: pointerStart.y,
    });
    raycaster.setFromCamera(pointer, camera);
    const hit = raycaster
      .intersectObjects(interactive, false)
      .find(({ object }) => objectIsEffectivelyVisible(object));
    if (
      officeSceneMode !== "town" &&
      hit?.object?.userData?.interactive === "office-marketing-task-board"
    ) {
      onOfficeTaskBoardSelect({
        state: officeMarketingTaskBoard.userData.taskState,
        count: officeMarketingTaskBoard.userData.taskCount,
      });
      return;
    }
    if (hit?.object?.userData?.interactive === "world-bulletin-scroll-up") {
      scrollWorldBulletin(-1);
      return;
    }
    if (hit?.object?.userData?.interactive === "world-bulletin-scroll-down") {
      scrollWorldBulletin(1);
      return;
    }
    if (hit?.object?.userData?.interactive === "world-bulletin") {
      onWorldBulletinSelect();
      return;
    }
    if (hit?.object?.userData?.interactive === "mastodon-kiosk-scroll-up") {
      scrollMastodonKiosk(-1);
      return;
    }
    if (hit?.object?.userData?.interactive === "mastodon-kiosk-scroll-down") {
      scrollMastodonKiosk(1);
      return;
    }
    if (hit?.object?.userData?.interactive === "mastodon-board") {
      onMastodonBoardSelect();
      return;
    }
    if (hit?.object?.userData?.interactive === "referral-leaderboard") {
      onReferralBoardSelect();
      return;
    }
    if (hit?.object?.userData?.interactive === "system-capacity-table") {
      onSystemCapacityTableSelect({
        name: String(hit.object.userData.tableName || ""),
        rowCount: Number(hit.object.userData.rowCount) || 0,
      });
      return;
    }
    if (
      officeSceneMode !== "town" &&
      hit?.object?.userData?.interactive === "office-meeting-board"
    ) {
      onOfficeMeetingBoardSelect();
      return;
    }
    if (
      officeSceneMode === "meeting" &&
      hit?.object?.userData?.interactive === "office-chair"
    ) {
      onOfficeChairSelect(hit.object.userData.officeChairId);
      return;
    }
    const moderationAction = hit?.object
      ? moderationActions.get(hit.object)
      : null;
    if (moderationAction) {
      onModeration({
        targetType: moderationAction.targetType,
        handle: moderationAction.handle,
        peerId: moderationAction.peerId,
        name: moderationAction.name,
      });
      return;
    }
    if (hit?.object?.userData?.campfireLog) {
      hit.object.userData.campfireCarried = true;
      hit.object.material.emissive?.set?.("#d88a43");
      hit.object.material.emissiveIntensity = 0.5;
      return;
    }
    if (hit?.object?.userData?.campfirePit) {
      const carried = interactive.find((object) => object.userData?.campfireCarried);
      if (carried) {
        carried.userData.campfireCarried = false;
        carried.visible = false;
        fireLevel = Math.min(2.4, fireLevel + 0.28);
      }
      return;
    }
    if (hit?.object?.userData?.campfireBench) {
      sitOnCampfireBench(hit.object);
      return;
    }
    if (hit?.object?.userData?.forkbotChat) {
      onForkbotChat();
      return;
    }
    if (hit?.object?.userData?.playForkmeshSong) {
      onPlayForkmeshSong();
      return;
    }
    if (hit?.object?.userData?.createRepository) {
      onCreateRepository();
      return;
    }
    if (hit?.object?.userData?.officeKeypadDigit) {
      officeKeypadHandler?.(
        String(hit.object.userData.officeKeypadDigit),
        {
          location:
            hit.object.userData.officeKeypadLocation === "interior"
              ? "interior"
              : "exterior",
        },
      );
      return;
    }
    if (hit?.object?.userData?.officeAccessPanel) {
      if (hit.object.userData.officeKeypadLocation === "interior") {
        officeKeypadHandler?.("focus", { location: "interior" });
        return;
      }
      const office = landmarkObjects.get("office");
      onOfficeEnter({
        source: "keypad",
        occupied: Boolean(office?.userData?.officeOccupied),
        available: office?.userData?.officeAvailable !== false,
      });
      return;
    }
    if (hit?.object?.userData?.officeEnter) {
      const office = landmarkObjects.get("office");
      onOfficeEnter({
        source: "door",
        occupied: Boolean(office?.userData?.officeOccupied),
        available: office?.userData?.officeAvailable !== false,
      });
      return;
    }
    if (hit?.object?.userData?.nodeCabinet) {
      focusNetworkNode(hit.object.userData.nodeCabinet.name);
      onLandmarkSelect("repositories", {
        source: "world",
        nodeCabinet: { ...hit.object.userData.nodeCabinet },
      });
      return;
    }
    if (hit?.object?.userData?.landmark) {
      const id = hit.object.userData.landmark;
      const repository =
        id === "repositories" && hit.object.userData.repositoryPortal
          ? { ...hit.object.userData.repositoryPortal }
          : null;
      const repositorySizeNode =
        id === "repositories" && hit.object.userData.repositorySizeNode
          ? { ...hit.object.userData.repositorySizeNode }
          : null;
      const repositoryStar =
        id === "repositories" && hit.object.userData.repositoryStar
          ? { ...hit.object.userData.repositoryStar }
          : null;
      const repositoryBase =
        id === "repositories" && hit.object.userData.repositoryBase
          ? { ...hit.object.userData.repositoryBase }
          : null;
      const repositoryIssuePage =
        id === "repositories" && hit.object.userData.repositoryIssuePage
          ? { ...hit.object.userData.repositoryIssuePage }
          : null;
      const repositoryPullPage =
        id === "repositories" && hit.object.userData.repositoryPullPage
          ? { ...hit.object.userData.repositoryPullPage }
          : null;
      if (
        !repository &&
        !repositorySizeNode &&
        !repositoryStar &&
        !repositoryBase &&
        !repositoryIssuePage &&
        !repositoryPullPage
      ) {
        focusLandmark(id);
      } else if (repository || repositorySizeNode) {
        const target = repository || repositorySizeNode;
        focusRepositoryPortal(target.owner, target.name);
      }
      onLandmarkSelect(id, {
        source: "world",
        mediaSpaceId: String(
          hit.object.userData.mediaSpaceId || "",
        ).slice(0, 40),
        repository,
        repositorySizeNode,
        repositoryStar,
        repositoryBase,
        repositoryIssuePage,
        repositoryPullPage,
        graphNode:
          id === "repositories" && hit.object.userData.graphNode
            ? { ...hit.object.userData.graphNode }
            : null,
      });
      return;
    }
  }

  function handlePointerUp(event) {
    finishPointer(event, false);
  }

  // Raycast against the current shared floor so double-click travel stays
  // accurate throughout the Town Square and its regional campuses.
  function groundPointAt(clientX, clientY) {
    pointerCoordinates({ clientX, clientY });
    raycaster.setFromCamera(pointer, camera);
    groundPlane.constant = -currentFloorY;
    const point = raycaster.ray.intersectPlane(
      groundPlane,
      new THREE.Vector3(),
    );
    if (!point || !Number.isFinite(point.x) || !Number.isFinite(point.z)) {
      return null;
    }
    const radius = Math.hypot(point.x, point.z);
    if (radius > WORLD_RADIUS) {
      point.x *= WORLD_RADIUS / radius;
      point.z *= WORLD_RADIUS / radius;
    }
    point.y = currentFloorY;
    return point;
  }

  // Section placards and node cabinets can sit inside a parent group, so the
  // pointer's ground point is converted into the space the object's position
  // actually lives in before it is used as a drag target.
  function layoutGroundPoint(object, clientX, clientY) {
    const point = groundPointAt(clientX, clientY);
    if (!point) return null;
    const parent = object?.parent;
    return parent && parent !== world ? parent.worldToLocal(point) : point;
  }

  function moveWorldObject(object, targetX, targetZ) {
    const radius = Math.hypot(targetX, targetZ);
    const scale = radius > WORLD_RADIUS ? WORLD_RADIUS / radius : 1;
    const x = targetX * scale;
    const z = targetZ * scale;
    const deltaX = x - object.position.x;
    const deltaZ = z - object.position.z;
    if (!deltaX && !deltaZ) return;
    object.position.x = x;
    object.position.z = z;
    // Landmark proximity and focus math read LANDMARKS positions, not the
    // group transform, so a relocated landmark must update its record too.
    const layoutId = String(object.userData.layoutId || "");
    if (layoutId.startsWith("landmark-")) {
      const landmark = LANDMARKS.find(
        (entry) => "landmark-" + entry.id === layoutId,
      );
      if (Array.isArray(landmark?.position)) {
        landmark.position[0] = x;
        landmark.position[2] = z;
      }
    }
  }

  function normalizeLayoutRotation(value) {
    const turn = Math.PI * 2;
    const rotation = Number(value);
    if (!Number.isFinite(rotation)) return 0;
    return ((rotation % turn) + turn) % turn;
  }

  // Locked rotation is an offset from the object's authored heading, applied
  // on top of it rather than replacing it.
  function rotateWorldObject(object, rotation) {
    if (!object) return;
    const offset = normalizeLayoutRotation(rotation);
    const base = Number(object.userData.layoutBaseRotation);
    object.userData.layoutRotation = offset;
    object.rotation.y = (Number.isFinite(base) ? base : 0) + offset;
  }

  function applyLockedPlacement(id, object) {
    const locked = lockedWorldLayout.get(String(id || ""));
    if (!locked || !object) return;
    moveWorldObject(object, locked.x, locked.z);
    rotateWorldObject(object, locked.rotation);
  }

  function applyWorldLayout(objects) {
    (Array.isArray(objects) ? objects : []).forEach((entry) => {
      const id = String(entry?.id || "");
      const x = Number(entry?.x);
      const z = Number(entry?.z);
      if (!id || !Number.isFinite(x) || !Number.isFinite(z)) return;
      lockedWorldLayout.set(id, {
        x,
        z,
        rotation: normalizeLayoutRotation(entry?.rotation),
      });
      const object = movableWorldObjects.get(id);
      if (object) applyLockedPlacement(id, object);
    });
  }

  function commitLayoutObject(object) {
    const id = String(object?.userData?.layoutId || "");
    if (!id) return;
    const move = {
      id,
      x: Number(object.position.x.toFixed(2)),
      z: Number(object.position.z.toFixed(2)),
      rotation: Number(
        normalizeLayoutRotation(object.userData.layoutRotation).toFixed(4),
      ),
    };
    lockedWorldLayout.set(id, {
      x: move.x,
      z: move.z,
      rotation: move.rotation,
    });
    onLayoutObjectMoved(move);
  }

  // Holding R spins the object continuously; only the settled result is worth
  // an administrator write, so coalesce the burst into a single save.
  function scheduleLayoutCommit(object) {
    if (layoutCommitTimer) clearTimeout(layoutCommitTimer);
    layoutCommitTimer = setTimeout(() => {
      layoutCommitTimer = 0;
      commitLayoutObject(object);
    }, LAYOUT_COMMIT_DELAY_MS);
  }

  function setActiveLayoutObject(object) {
    activeLayoutObject = object || null;
    // The grabbed object's handle turns amber so it is obvious which prop the
    // R key will rotate.
    layoutHandles.forEach((handle, id) => {
      handle.material?.color?.set?.(
        activeLayoutObject && activeLayoutObject.userData.layoutId === id
          ? "#ffd25f"
          : "#ff5df1",
      );
    });
  }

  // Where the object's move handle currently sits, expressed in the space its
  // position lives in.
  function layoutHandlePoint(object) {
    const handle = layoutHandles.get(String(object.userData.layoutId || ""));
    if (!handle) return null;
    const point = handle.getWorldPosition(new THREE.Vector3());
    const parent = object.parent;
    return parent && parent !== world ? parent.worldToLocal(point) : point;
  }

  function rotateActiveLayoutObject(direction) {
    const target = draggedLayoutObject || activeLayoutObject;
    if (!target) return false;
    // Turn the object about the point its handle marks — its visible centre —
    // rather than the group origin. The arrival grid and other groups keep
    // their geometry well away from origin, where a plain yaw would swing them
    // across the square instead of spinning them where they stand.
    const pivot = layoutHandlePoint(target);
    rotateWorldObject(
      target,
      Number(target.userData.layoutRotation || 0) +
        LAYOUT_ROTATION_STEP * direction,
    );
    const moved = pivot ? layoutHandlePoint(target) : null;
    if (pivot && moved) {
      moveWorldObject(
        target,
        target.position.x + (pivot.x - moved.x),
        target.position.z + (pivot.z - moved.z),
      );
    }
    // A dragged object saves on release; a parked one has no other trigger.
    if (!draggedLayoutObject) scheduleLayoutCommit(target);
    return true;
  }

  function forgetMovableObject(id) {
    const key = String(id || "");
    const object = movableWorldObjects.get(key);
    if (!object) return;
    if (draggedLayoutObject === object) draggedLayoutObject = null;
    if (activeLayoutObject === object) activeLayoutObject = null;
    const handle = layoutHandles.get(key);
    if (handle) {
      const index = interactive.indexOf(handle);
      if (index >= 0) interactive.splice(index, 1);
      layoutHandles.delete(key);
    }
    movableWorldObjects.delete(key);
  }

  function ensureLayoutHandles() {
    movableWorldObjects.forEach((object, id) => {
      if (layoutHandles.has(id)) return;
      // Deliberately minuscule: the handle only becomes a comfortable click
      // target once an administrator zooms right up to the object it moves.
      const handle = new THREE.Mesh(
        new THREE.OctahedronGeometry(0.09, 0),
        new THREE.MeshBasicMaterial({
          color: "#ff5df1",
          toneMapped: false,
          depthTest: false,
          transparent: true,
          opacity: 0.92,
        }),
      );
      handle.name = "world-layout-handle-" + id;
      // Anchor the handle to the object's visible mass, not the group
      // origin: the arrival plaques sit ~15 units away from their group
      // origin, where a fixed-origin handle would float in the town center.
      const center = new THREE.Box3()
        .setFromObject(object)
        .getCenter(new THREE.Vector3());
      object.worldToLocal(center);
      handle.position.set(
        Number.isFinite(center.x) ? center.x : 0,
        0.34,
        Number.isFinite(center.z) ? center.z : 0,
      );
      handle.renderOrder = 30;
      handle.userData.layoutHandle = id;
      // Node cabinets register long after the editor was switched on, so a
      // fresh handle adopts the current editing state instead of staying dark
      // until the next toggle.
      handle.visible = layoutEditingEnabled;
      object.add(handle);
      interactive.push(handle);
      layoutHandles.set(id, handle);
    });
  }

  function setLayoutEditor(enabled) {
    layoutEditingEnabled = enabled === true;
    if (layoutEditingEnabled) ensureLayoutHandles();
    layoutHandles.forEach((handle) => {
      handle.visible = layoutEditingEnabled;
    });
    if (!layoutEditingEnabled) {
      draggedLayoutObject = null;
      setActiveLayoutObject(null);
    }
  }

  function handleDoubleClick(event) {
    if (event.button !== undefined && event.button !== 0) return;
    if (lastGestureDragged) return;
    // A quick double tap on a bench is still a request to sit on it, not to
    // dash to the patch of ground the bench happens to stand on.
    pointerCoordinates(event);
    raycaster.setFromCamera(pointer, camera);
    const benchHit = raycaster
      .intersectObjects(interactive, false)
      .find(
        ({ object }) =>
          object.userData?.campfireBench && objectIsEffectivelyVisible(object),
      );
    if (benchHit) {
      event.preventDefault();
      sitOnCampfireBench(benchHit.object);
      return;
    }
    const point = groundPointAt(event.clientX, event.clientY);
    if (!point) return;
    event.preventDefault();
    // Following the avatar again keeps the dash visible; a landmark focus left
    // over from the two selection clicks would pin the camera in place.
    cameraFocus = null;
    dashTarget = point;
  }

  function handlePointerCancel(event) {
    finishPointer(event, true);
  }

  function handleWheel(event) {
    const lineHeight = 16;
    const deltaPixels =
      event.deltaY *
      (event.deltaMode === WheelEvent.DOM_DELTA_LINE
        ? lineHeight
        : event.deltaMode === WheelEvent.DOM_DELTA_PAGE
          ? Math.max(1, renderer.domElement.clientHeight)
          : 1);
    if (!Number.isFinite(deltaPixels) || deltaPixels === 0) return;
    event.preventDefault();
    pointerCoordinates(event);
    raycaster.setFromCamera(pointer, camera);
    const bulletinHit = raycaster
      .intersectObjects([bulletinFrame, bulletinFace, bulletinScrollUp, bulletinScrollDown], false)
      .find(({ object }) => objectIsEffectivelyVisible(object));
    if (bulletinHit) {
      scrollWorldBulletin(Math.sign(deltaPixels));
      return;
    }
    if (mastodonKioskMaxOffset() > 0) {
      const kioskHit = raycaster
        .intersectObjects(mastodonKioskWheelTargets, false)
        .find(({ object }) => objectIsEffectivelyVisible(object));
      if (kioskHit) {
        scrollMastodonKiosk(Math.sign(deltaPixels));
        return;
      }
    }
    const currentZoom =
      cameraMode === "first-person" ? firstPersonZoom : cameraZoom;
    setCameraZoom(currentZoom * Math.exp(deltaPixels * 0.0015));
  }

  function handleKeyDown(event) {
    if (
      event.target instanceof HTMLInputElement ||
      event.target instanceof HTMLTextAreaElement ||
      event.target instanceof HTMLSelectElement ||
      event.target?.isContentEditable
    ) return;
    if (MOVEMENT_KEYS.has(event.code)) {
      keys.add(event.code);
      event.preventDefault();
    }
    if (event.code === "Space") {
      if (player.position.y <= currentFloorY + 0.02) jumpQueued = true;
      event.preventDefault();
    }
    // R turns the object being dragged — or the one most recently grabbed —
    // a step at a time; hold Shift to turn it back the other way.
    if (event.code === "KeyR" && layoutEditingEnabled) {
      if (rotateActiveLayoutObject(event.shiftKey ? -1 : 1)) {
        event.preventDefault();
      }
    }
    if (event.code === "Escape") {
      if (cameraMode === "first-person") setCameraMode("third-person");
      else clearFocus();
    }
  }

  function handleKeyUp(event) {
    keys.delete(event.code);
    if (
      MOVEMENT_KEYS.has(event.code) &&
      ![...keys].some((code) => MOVEMENT_KEYS.has(code))
    ) {
      keyboardMovementSpeed = baseMoveSpeed();
    }
  }

  function handleWindowBlur() {
    keys.clear();
    touchKeys.clear();
    touchMovement.set(0, 0);
    touchPointers.clear();
    keyboardMovementSpeed = baseMoveSpeed();
    jumpQueued = false;
    jumpVelocity = 0;
    cancelDash();
    primaryPointerId = null;
    pointerGestureMoved = false;
    pinchActive = false;
    pinchStartDistance = 0;
    draggedLayoutObject = null;
    renderer.domElement.dataset.dragging = "false";
  }

  renderer.domElement.addEventListener("pointerdown", handlePointerDown);
  renderer.domElement.addEventListener("dblclick", handleDoubleClick);
  renderer.domElement.addEventListener("pointermove", handlePointerMove, {
    passive: false,
  });
  renderer.domElement.addEventListener("wheel", handleWheel, {
    passive: false,
  });
  window.addEventListener("pointerup", handlePointerUp);
  window.addEventListener("pointercancel", handlePointerCancel);
  window.addEventListener("keydown", handleKeyDown);
  window.addEventListener("keyup", handleKeyUp);
  window.addEventListener("blur", handleWindowBlur);

  const resize = () => {
    const rect = container.getBoundingClientRect();
    const width = Math.max(1, Math.floor(rect.width));
    const height = Math.max(1, Math.floor(rect.height));
    // Keep the drawing buffer deliberately modest. The previous 1.75 cap made
    // the GPU shade over three times as many pixels as a 1x canvas on dense
    // displays, which showed up as movement hitching.
    renderer.setPixelRatio(
      Math.min(
        window.devicePixelRatio || 1,
        compactRenderer ? 1 : width < 700 ? 1.1 : 1.35,
      ),
    );
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    // ResizeObserver fires after the animation-loop rAF but before paint, and
    // setSize() clears the WebGL drawing buffer — so without an immediate
    // re-render the browser composites a blank frame, making the whole world
    // flicker throughout a live drag-resize. Paint the resized frame now.
    if (running && !disposed) {
      renderer.render(scene, camera);
    }
  };
  // ResizeObserver is unavailable in older mobile WebViews.  A window resize
  // listener still gives those browsers a correctly sized, working World.
  const resizeObserver =
    typeof ResizeObserver === "function" ? new ResizeObserver(resize) : null;
  resizeObserver?.observe(container);
  window.addEventListener("resize", resize, { passive: true });
  window.visualViewport?.addEventListener("resize", resize, { passive: true });
  resize();

  function animate(time) {
    if (!running || disposed) return;
    const rawFrameMs = Math.max(0, time - lastFrame);
    const delta = clamp(rawFrameMs / 1000, 0, 0.05);
    lastFrame = time;
    diagnosticsLongestFrameMs = Math.max(diagnosticsLongestFrameMs, rawFrameMs);
    if (rawFrameMs > 34) diagnosticsLongFrames += 1;
    // Report genuine visible-tab stalls without flooding DevTools during a
    // prolonged hitch. Ordinary 30–60fps variance remains in diagnostics but
    // does not produce console noise.
    if (
      rawFrameMs >= RENDER_STALL_THRESHOLD_MS &&
      document.visibilityState === "visible" &&
      time - lastRenderStallLogAt >= RENDER_STALL_LOG_COOLDOWN_MS
    ) {
      lastRenderStallLogAt = time;
      const drawingBuffer = renderer.getDrawingBufferSize(new THREE.Vector2());
      const memory = performance.memory;
      console.warn("[ForkMesh World] Render stall detected", {
        observedAt: new Date().toISOString(),
        frameMs: Math.round(rawFrameMs),
        estimatedMissedFrames: Math.max(0, Math.round(rawFrameMs / 16.67) - 1),
        detectionPoint: "world-scene animate()",
        cameraMode,
        camera: {
          zoom: Number(
            (cameraMode === "first-person" ? firstPersonZoom : cameraZoom).toFixed(3),
          ),
          yaw: Number(cameraYaw.toFixed(3)),
          pitch: Number(
            (cameraMode === "first-person" ? firstPersonPitch : cameraPitch).toFixed(3),
          ),
          position: camera.position.toArray().map((value) => Number(value.toFixed(2))),
        },
        player: {
          position: player.position.toArray().map((value) => Number(value.toFixed(2))),
          moving: wasWalking,
        },
        space: currentSpace,
        region: currentRegion,
        officeSceneMode,
        input: {
          dragging: primaryPointerId !== null,
          pinching: pinchActive,
          pressedKeys: [...keys],
          touchPointers: touchPointers.size,
        },
        renderer: {
          pixelRatio: renderer.getPixelRatio(),
          drawingBuffer: [drawingBuffer.x, drawingBuffer.y],
          calls: Number(renderer.info?.render?.calls) || 0,
          triangles: Number(renderer.info?.render?.triangles) || 0,
          geometries: Number(renderer.info?.memory?.geometries) || 0,
          textures: Number(renderer.info?.memory?.textures) || 0,
        },
        viewport: {
          width: window.innerWidth,
          height: window.innerHeight,
          devicePixelRatio: window.devicePixelRatio || 1,
        },
        jsHeap: memory
          ? {
              usedMB: Math.round(memory.usedJSHeapSize / 1024 / 1024),
              totalMB: Math.round(memory.totalJSHeapSize / 1024 / 1024),
              limitMB: Math.round(memory.jsHeapSizeLimit / 1024 / 1024),
            }
          : null,
        interactiveObjects: interactive.length,
        animatedObjects: animated.length,
        stack: new Error("Render stall observed").stack,
      });
    }
    // Only the town scene walks the shared avatar and its neighbours; inside an
    // Office meeting the seated participant is driven instead (PR #47).
    if (officeSceneMode === "town") {
      walkPlayer(delta, time);
      updateRemotePlayers(delta, time);
      updateForkbot(delta, time);
    } else if (officeSceneMode === "lobby") {
      walkOfficeLobbyPlayer(delta, time);
    } else if (officeSceneMode === "meeting") {
      walkOfficeParticipant(delta, time);
    }
    const repositoryLoadingTail = world.userData.repositorySizeLoadingTail;
    if (repositoryLoadingTail?.visible) {
      repositoryLoadingTail.rotation.z = time * 0.008;
      const spark = repositoryLoadingTail.children.at(-1);
      if (spark?.material?.emissive) {
        spark.material.emissiveIntensity = 1.45 + Math.sin(time * 0.02) * 0.75;
      }
    }
    if (!reducedMotion) {
      // Node beacons intentionally hold a steady colour and size — no spin or
      // pulse — so a status reads the same in a screenshot as it does live.
      botAgents.forEach((robot, id) => {
        const phase = hashNumber(id) * 0.0001;
        robot.position.y =
          0.38 + Math.sin(time * 0.0017 + phase) * 0.16;
        robot.userData.core.rotation.y = time * 0.0011 + phase;
      });
    }
    for (let index = emoteSprites.length - 1; index >= 0; index -= 1) {
      const flight = emoteSprites[index];
      const progress = Math.min(
        1,
        (performance.now() - flight.startedAt) / flight.duration,
      );
      const fadeStart = flight.fadeStart ?? 0.65;
      flight.sprite.position.copy(flight.avatar.position);
      flight.sprite.position.y +=
        (flight.baseHeight ?? 4.7) + progress * (flight.rise ?? 1.1);
      flight.sprite.material.opacity =
        1 - Math.max(0, progress - fadeStart) / (1 - fadeStart);
      if (progress >= 1) {
        world.remove(flight.sprite);
        flight.sprite.material?.map?.dispose?.();
        flight.sprite.material?.dispose?.();
        emoteSprites.splice(index, 1);
      }
    }
    for (let index = rewardFlights.length - 1; index >= 0; index -= 1) {
      const flight = rewardFlights[index];
      const elapsed = performance.now() - flight.startedAt;
      const complete = elapsed / flight.duration;
      flight.particles.forEach((particle, particleIndex) => {
        const stagger = particleIndex * 0.018;
        const progress = clamp((complete - stagger) / (1 - stagger), 0, 1);
        particle.position.lerpVectors(flight.start, flight.target, progress);
        particle.position.y +=
          Math.sin(progress * Math.PI) * (3.2 + (particleIndex % 4) * 0.18);
        particle.position.x += Math.sin(particleIndex * 2.1) * 0.18;
        particle.position.z += Math.cos(particleIndex * 1.7) * 0.18;
        particle.rotation.y = progress * Math.PI * 8 + particleIndex;
      });
      if (complete >= 1.1) {
        world.remove(flight.group);
        flight.group.traverse((child) => {
          child.geometry?.dispose?.();
          child.material?.dispose?.();
        });
        rewardFlights.splice(index, 1);
      }
    }
    updateCamera(delta);
    // Landmark/portal proximity is a town-scene concern only (PR #47). PR #47
    // also called updateWorldSun() here; main removed wall-clock sun entirely
    // in favour of fixed full daylight, so that call is not restored.
    if (officeSceneMode === "town") {
      nearestLandmark();
      updateRepositoryPortalLabels();
    }
    if (!reducedMotion) {
      animated.forEach((callback) => callback(time, delta));
      animateWeather(weather.rain, time, delta, "rain");
      animateWeather(weather.snow, time, delta, "snow");
    }
    const rect = container.getBoundingClientRect();
    // main removed the floating landmark labels (adhoc #243); only the player
    // and remote name plates remain, and they are a town-scene concern.
    if (officeSceneMode === "town") {
      if (cameraMode === "first-person") {
        playerLabel.style.opacity = "0";
        playerLabel.style.visibility = "hidden";
      } else {
        updateScreenLabel(
          THREE,
          player,
          playerLabel,
          camera,
          rect.width,
          rect.height,
          player.userData.emojiStatusSprite ? 5.7 : 4.5,
        );
      }
      remotePlayers.forEach((avatar, id) => {
        updateScreenLabel(
          THREE,
          avatar,
          remoteLabels.get(id),
          camera,
          rect.width,
          rect.height,
          avatar.userData.emojiStatusSprite ? 5.35 : 4.2,
        );
      });
      officeParticipantLabels.forEach((element) => {
        element.style.visibility = "hidden";
      });
      officeBubbles.forEach((element) => {
        element.style.visibility = "hidden";
      });
    } else {
      playerLabel.style.visibility = "hidden";
      remoteLabels.forEach((element) => {
        element.style.visibility = "hidden";
      });
      officeParticipants.forEach((avatar, id) => {
        updateScreenLabel(
          THREE,
          avatar,
          officeParticipantLabels.get(id),
          camera,
          rect.width,
          rect.height,
          4.2,
        );
        const bubble = officeBubbles.get(id);
        if (bubble) {
          updateScreenLabel(
            THREE,
            avatar,
            bubble,
            camera,
            rect.width,
            rect.height,
            5.1,
          );
          const bubbleHalfWidth = Math.min(136, Math.max(84, rect.width / 2 - 12));
          const bubbleLeft = Number.parseFloat(bubble.style.left) || rect.width / 2;
          const bubbleTop = Number.parseFloat(bubble.style.top) || 72;
          bubble.style.left = `${clamp(
            bubbleLeft,
            bubbleHalfWidth,
            rect.width - bubbleHalfWidth,
          )}px`;
          bubble.style.top = `${clamp(bubbleTop, 68, rect.height - 18)}px`;
        }
      });
    }
    renderer.render(scene, camera);
    diagnosticsFrameCount = Math.min(
      1_000_000,
      diagnosticsFrameCount + 1,
    );
    diagnosticsRendererCalls = Math.max(
      0,
      Math.min(10_000_000, Number(renderer.info?.render?.calls) || 0),
    );
    diagnosticsRendererTriangles = Math.max(
      0,
      Math.min(1_000_000_000, Number(renderer.info?.render?.triangles) || 0),
    );
  }

  function setPaused(paused) {
    running = !paused;
    if (running) {
      lastFrame = performance.now();
      renderer.setAnimationLoop(animate);
    } else {
      renderer.setAnimationLoop(null);
    }
  }

  function getDiagnostics(now = performance.now()) {
    const sampleNow = Number.isFinite(Number(now))
      ? Number(now)
      : performance.now();
    const elapsedMs = Math.max(1, sampleNow - diagnosticsSampleAt);
    const frames = diagnosticsFrameCount;
    const fps = running
      ? Math.max(0, Math.min(1000, (frames * 1000) / elapsedMs))
      : 0;
    const frameTimeMs =
      running && frames
        ? Math.max(0, Math.min(60_000, elapsedMs / frames))
        : 0;
    diagnosticsSampleAt = sampleNow;
    diagnosticsFrameCount = 0;
    const longestFrameMs = diagnosticsLongestFrameMs;
    const longFrames = diagnosticsLongFrames;
    const pointerMoves = diagnosticsPointerMoves;
    const pointerWorstGapMs = diagnosticsPointerWorstGapMs;
    diagnosticsLongestFrameMs = 0;
    diagnosticsLongFrames = 0;
    diagnosticsPointerMoves = 0;
    diagnosticsPointerWorstGapMs = 0;
    return {
      fps,
      frameTimeMs,
      rendererCalls: diagnosticsRendererCalls,
      rendererTriangles: diagnosticsRendererTriangles,
      longestFrameMs,
      longFrames,
      pointerMoves,
      pointerWorstGapMs,
      dragging: primaryPointerId !== null,
      interactiveObjects: interactive.length,
      animations: animated.length,
      pixelRatio: renderer.getPixelRatio(),
      cameraMode,
      space: currentSpace,
      moving: wasWalking,
      zoom: cameraMode === "first-person" ? firstPersonZoom : cameraZoom,
      paused: !running,
    };
  }

  function dispose() {
    disposed = true;
    renderer.setAnimationLoop(null);
    resizeObserver?.disconnect();
    window.removeEventListener("resize", resize);
    window.visualViewport?.removeEventListener("resize", resize);
    renderer.domElement.removeEventListener("pointerdown", handlePointerDown);
    renderer.domElement.removeEventListener("dblclick", handleDoubleClick);
    renderer.domElement.removeEventListener("pointermove", handlePointerMove);
    renderer.domElement.removeEventListener("wheel", handleWheel);
    renderer.domElement.removeEventListener(
      "webglcontextlost",
      handleContextLost,
    );
    renderer.domElement.removeEventListener(
      "webglcontextrestored",
      handleContextRestored,
    );
    window.removeEventListener("pointerup", handlePointerUp);
    window.removeEventListener("pointercancel", handlePointerCancel);
    window.removeEventListener("keydown", handleKeyDown);
    window.removeEventListener("keyup", handleKeyUp);
    window.removeEventListener("blur", handleWindowBlur);
    touchPointers.clear();
    keys.clear();
    touchKeys.clear();
    touchMovement.set(0, 0);
    if (layoutCommitTimer) {
      clearTimeout(layoutCommitTimer);
      layoutCommitTimer = 0;
    }
    scene.traverse((child) => {
      child.geometry?.dispose?.();
      if (Array.isArray(child.material)) {
        child.material.forEach((material) => {
          material.map?.dispose?.();
          material.dispose?.();
        });
      } else {
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      }
    });
    renderer.dispose();
    renderer.domElement.remove();
    labelLayer.replaceChildren();
  }

  setTheme("world");
  camera.lookAt(player.position);
  renderer.setAnimationLoop(animate);

  return {
    camera,
    scene,
    player,
    renderer,
    focusLandmark,
    enterOffice,
    enterOfficeLobby,
    enterOfficeMeeting,
    beginOfficeExit,
    setOfficeExitHandler,
    setOfficeDoorwayEntryPending,
    focusOfficeKeypad,
    blurOfficeKeypad,
    setOfficeKeypadDigits,
    setOfficeKeypadHandler,
    setOfficeOccupancy,
    setOfficeParticipants,
    updateOfficeMarketingTasks,
    updateWorldBulletin,
    updateMastodonKiosk,
    setOfficeSeatState,
    showOfficeBubble,
    leaveOfficeInterior,
    focusRepositoryPortal,
    enterRepositoryFirstPerson,
    clearFocus,
    setCameraMode,
    setTheme,
    setLightLevel,
    setMovementTuning,
    setControl,
    setTouchMovement,
    setSpawn,
    travelToRegion,
    visitNeighborhoodHome,
    setRemotePlayers,
    updateArrivalStats,
    updateMemberLounge,
    updateReferralLeaderboard,
    updateNetworkNodes,
    focusNetworkNode,
    updateFederatedInstances,
    updateBots,
    updateSystemCapacity,
    updateOrganizations,
    updateFediverseDirectory,
    updateMediaSpaces,
    updateIdentity,
    updateRepositoryCatalog,
    updateRepositoryGraph,
    updateRepositorySizeMap,
    updateRepositoryRecordDesk,
    setRepositoryIssuePageExpanded,
    setRepositorySizeLoading,
    applyWorldLayout,
    setLayoutEditor,
    playEmote,
    showChatBubble,
    greetForkbot,
    updateRewardPool,
    playRewardEvent,
    setPaused,
    dispose,
    setCameraZoom,
    getCameraState: () => ({
      mode: cameraMode,
      firstPerson: cameraMode === "first-person",
      zoom: cameraMode === "first-person" ? firstPersonZoom : cameraZoom,
      minZoom: cameraMode === "first-person" ? 0.25 : CAMERA_ZOOM_MIN,
      maxZoom: cameraMode === "first-person" ? 5 : CAMERA_ZOOM_MAX,
      yaw: cameraYaw,
      pitch:
        cameraMode === "first-person" ? firstPersonPitch : cameraPitch,
      orbitPitch: cameraPitch,
      firstPersonPitch,
      dragging: primaryPointerId !== null,
      pointerLocked: false,
    }),
    getMovementState: () => ({
      speed: keyboardMovementSpeed,
      baseSpeed: baseMoveSpeed(),
      maxSpeed: PLAYER_MAX_SPEED * moveSpeedScale,
      speedScale: moveSpeedScale,
      accelerationScale: moveAccelScale,
      keyboardActive: [...keys].some((code) => MOVEMENT_KEYS.has(code)),
      touchActive: touchMovement.lengthSq() > 0,
      touchStrength: touchMovement.length(),
      touchX: touchMovement.x,
      touchY: touchMovement.y,
    }),
    getDiagnostics,
    getEnvironmentState: () => ({
      theme: currentTheme,
      lightLevel,
      sunIntensity: sun.intensity,
      hemisphereIntensity: hemisphere.intensity,
      exposure: renderer.toneMappingExposure,
      sunPosition: sun.position.toArray(),
    }),
    getPosition: () => ({
      x: player.position.x,
      y: player.position.y,
      z: player.position.z,
      heading: player.rotation.y,
      space: currentSpace,
    }),
  };
}
