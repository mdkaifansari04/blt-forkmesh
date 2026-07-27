import {
  LANDMARKS,
  OUTFIT_COLOR_OPTIONS,
  OUTFIT_STYLE_OPTIONS,
  WORLD_REGIONS,
  flagEmoji,
  landmarkById,
  normalizeWorldStatus,
} from "./world-data.js";
import { nextOfficeZoneState } from "./world-office.js";
import {
  OFFICE_AVATAR_RADIUS,
  OFFICE_BRIDGE_END_Z,
  OFFICE_BRIDGE_START_Z,
  OFFICE_BRIDGE_WIDTH,
  OFFICE_DEPTH,
  OFFICE_DOOR_WIDTH,
  OFFICE_ELEVATOR_CENTER_X,
  OFFICE_ELEVATOR_CENTER_Z,
  OFFICE_FLOOR_COUNT,
  OFFICE_FLOOR_HEIGHT,
  OFFICE_FLOORS,
  OFFICE_FRONT_Z,
  OFFICE_ISLAND_CENTER,
  OFFICE_ISLAND_RADIUS,
  OFFICE_TOWER_HEIGHT,
  OFFICE_WIDTH,
  canAccessOfficeFloor,
  normalizeOfficeFloorAccess,
  officeCampusSurfaceContains,
  officeElevatorCabinContains,
  officeFloorById,
  officeFloorY,
  officeInteriorPointIsWalkable,
} from "./world-office-tower.js";
import { createWorldSky } from "./world-sky.js";
// Side-effect import: ForkMesh's own QR generator publishes globalThis.ForkMeshQR,
// used for the reward-pool treasury address board.
import "../qr.js";

const OUTFIT_COLOR_HEX = Object.fromEntries(
  OUTFIT_COLOR_OPTIONS.map((option) => [option.id, option.color]),
);
const OUTFIT_STYLE_IDS = OUTFIT_STYLE_OPTIONS.map((option) => option.id);

const WORLD_RADIUS = 340;
const WORLD_GROUND_RADIUS = 88;
const REPOSITORY_ISLAND_CENTER_X = 130;
const REPOSITORY_ISLAND_RADIUS = 38;
const REPOSITORY_ISLAND_RING_RADIUS = 31;
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
const OFFICE_HEIGHT = OFFICE_FLOOR_HEIGHT;
const OFFICE_INTERIOR_WALL_LIMIT = OFFICE_FRONT_Z - 0.54;
const OFFICE_INTERIOR_EXIT_Z = OFFICE_FRONT_Z + 0.18;
const OFFICE_ELEVATOR_HALF_WIDTH = 5;
const OFFICE_ELEVATOR_HALF_DEPTH = 4;
const OFFICE_ELEVATOR_CUT_MARGIN = 0.35;
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
// Noah speaks only on a genuine outside -> desk-side approach. The wider reset
// radius adds hysteresis, while the cooldown prevents a visitor pacing on the
// boundary from creating a new local bubble every frame.
const OFFICE_RECEPTION_TALK_RANGE = 2.2;
const OFFICE_RECEPTION_RESET_RANGE = 3.8;
const OFFICE_RECEPTION_TALK_COOLDOWN_MS = 12000;
const OFFICE_RECEPTION_GUEST_TIPS = Object.freeze([
  "The lobby is open to everyone. Log in to use elevator buttons for restricted team floors.",
  "Guests can browse public repository portals; log in before requesting access to restricted team floors.",
  "Public mirrors keep source readable. Log in to unlock only the team floors granted to your account.",
]);
const OFFICE_RECEPTION_MEMBER_TIPS = Object.freeze([
  "Elevator buttons reflect your server-verified team access; locked floors stay locked.",
  "Repository portals show live mirrors, branches, issues, pull requests, and discussions.",
  "The rooftop laptop opens the real ForkMesh source browser; source edits stay in the desktop or IDE extension.",
]);
// A chat mention (exciteForkbot) sends the droid rushing to the speaker at a
// faster clip than its idle wander. The chest screen echoes the mention alone
// for a beat before the thinking dots join it, and the wait for a reply is
// bounded so an unavailable bot doesn't leave the dots running forever.
const FORKBOT_EXCITED_SPEED = 5.6;
const FORKBOT_ECHO_MS = 2500;
const FORKBOT_THINKING_TIMEOUT_MS = 45000;
// The public World has one shared ground plane plus three regional labels.
// Deprecated off-world destinations are deliberately not valid spawn spaces.
const WORLD_SPACE_FLOORS = Object.freeze({
  "town-square": 0.38,
  east: 0.38,
  central: 0.38,
  west: 0.38,
});

function worldWalkSurfaceContains(x, z, radius = OFFICE_AVATAR_RADIUS) {
  const px = Number(x);
  const pz = Number(z);
  const margin = Math.max(0, Number(radius) || 0);
  if (!Number.isFinite(px) || !Number.isFinite(pz)) return false;
  if (Math.hypot(px, pz) <= WORLD_GROUND_RADIUS - margin) return true;
  if (
    Math.hypot(px - REPOSITORY_ISLAND_CENTER_X, pz) <=
    REPOSITORY_ISLAND_RADIUS - margin
  ) {
    return true;
  }
  if (
    px >= WORLD_GROUND_RADIUS - 4 - margin &&
    px <=
      REPOSITORY_ISLAND_CENTER_X -
      REPOSITORY_ISLAND_RADIUS +
      4 +
      margin &&
    Math.abs(pz) <= 2.4 - margin
  ) {
    return true;
  }
  return officeCampusSurfaceContains(px, pz, margin);
}
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
// a bench sitter sitting rather than standing on the plank. Exported because
// the shell must keep it out of the landmark-proximity activity label.
export const CAMPFIRE_SEATED_ACTIVITY = "sitting beside the campfire";
// Shared by the swing-set ride and the presence frame for the same reason:
// exported so the shell keeps it out of the landmark-proximity activity label
// while a visitor is riding one of the town swings.
export const SWING_RIDING_ACTIVITY = "swinging on the town swing set";
// Legs hinge at the hip and again at the knee. Avatar fronts face local -Z, so
// the positive pitch about X is the one that swings the knee over the front
// edge of the bench instead of out behind the sitter; the knee then folds back
// by the same amount, which drops the shin straight down and leaves the shoe
// flat on the floor.
const SEATED_LEG_PITCH = 1.45;
const SEATED_KNEE_PITCH = -SEATED_LEG_PITCH;
// Walking swings the whole leg from the hip, so the stride pitch is about half
// what the old mid-leg pivot needed for the same amount of foot travel.
const GAIT_LEG_SWING = 0.38;
// Avatar leg metrics, in local (unscaled) avatar units: the hip joint height,
// the length of one leg segment (thigh and shin are the same), and how far the
// sole of the shoe hangs below the knee.
const AVATAR_HIP_Y = 1.405;
const AVATAR_LEG_SEGMENT = 0.625;
const AVATAR_SHOE_Y = 0.15;
const AVATAR_SHOE_HEIGHT = 0.26;
const AVATAR_KNEE_TO_SOLE =
  AVATAR_HIP_Y - AVATAR_LEG_SEGMENT - AVATAR_SHOE_Y + AVATAR_SHOE_HEIGHT / 2;
// A seated avatar is placed by its hips, not by its feet: the folded thigh
// rests on top of the plank (its half-depth once pitched over, so it lies on
// the seat instead of sinking through it) and the shin carries the shoe down
// to the floor, which lands the sole SEATED_SEAT_TO_SOLE below the plank.
const SEATED_HIP_ABOVE_SEAT = 0.3;
// How far below the plank a sitter's soles end up, which is what a bench has
// to be built up to for the feet to reach the floor.
const SEATED_SEAT_TO_SOLE =
  AVATAR_LEG_SEGMENT * Math.cos(SEATED_LEG_PITCH) +
  AVATAR_KNEE_TO_SOLE -
  SEATED_HIP_ABOVE_SEAT;
// Standing avatars are parked with their origin on the floor height (0.38),
// which puts the soles of their shoes on the walking plane just above it.
const WORLD_WALKING_PLANE_Y = 0.4;
function seatedAvatarY(seatTopY, scale = 1) {
  return seatTopY - (AVATAR_HIP_Y - SEATED_HIP_ABOVE_SEAT) * scale;
}
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
  },
  snow: {
    tint: "#dcece9",
    tintStrength: 0.22,
    lightMultiplier: 1.04,
    exposureMultiplier: 1.02,
  },
  winter: {
    tint: "#b9ccca",
    tintStrength: 0.27,
    lightMultiplier: 0.94,
    exposureMultiplier: 0.96,
  },
  cyberpunk: {
    tint: "#35104c",
    tintStrength: 0.34,
    lightMultiplier: 0.9,
    exposureMultiplier: 0.9,
  },
  "low-light": {
    tint: "#07110f",
    tintStrength: 0.18,
    lightMultiplier: 0.64,
    exposureMultiplier: 0.7,
  },
});
const DAYLIGHT_ENVIRONMENT = Object.freeze({
  // A deep teal night sky keeps the ground readable while letting the
  // deterministic stars, planets, and public-orbit satellites remain visible.
  background: "#06131d",
  hemiSky: "#d8fff1",
  hemiGround: "#25493a",
  sun: "#fff0bd",
  sunPower: 3.7,
  hemiPower: 2.1,
  exposure: 1.12,
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
    // The campfire's clearing is filled by its bench circle, whose radius
    // reaches past where these trees would stand.
    if (landmark.id === "campfire") return;
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
// total active time. A live avatar is handed the same directory reading, so a
// member wears the identical record whether they are walking around or seated.
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

// "FIRST SEEN 14 MINUTES AGO". The exact reading only exists while the visitor
// shares generalized activity; the coarse bucket label remains the fallback.
function firstSeenAgoLabel(minutes) {
  const total = Number(minutes);
  if (!Number.isFinite(total) || total <= 0) return "";
  const units = [
    [365 * 24 * 60, "YEAR"],
    [30 * 24 * 60, "MONTH"],
    [7 * 24 * 60, "WEEK"],
    [24 * 60, "DAY"],
    [60, "HOUR"],
    [1, "MINUTE"],
  ];
  for (const [size, unit] of units) {
    const count = Math.floor(total / size);
    if (count) {
      return `FIRST SEEN ${count} ${unit}${count === 1 ? "" : "S"} AGO`;
    }
  }
  return "";
}

// The visitor's own browser family and operating system, as the two coarse
// categories world.js derived locally — never a raw user-agent string.
function badgeClientLabel(identity) {
  // "Hidden" is a privacy choice and "Browser"/"Device" are the placeholders a
  // peer carries when its family is unrecognized: neither is worth a row.
  const placeholders = new Set(["hidden", "browser", "device"]);
  const parts = [identity.browser, identity.os]
    .map((value) => String(value || "").trim())
    .filter((value) => value && !placeholders.has(value.toLowerCase()));
  return parts.length ? parts.join(" · ").toUpperCase().slice(0, 30) : "";
}

function badgeStatusLabel(identity) {
  const note = String(identity.statusNote || "").trim();
  const emoji = String(identity.statusEmoji || "").trim();
  if (!emoji) return "";
  return `${emoji} ${note}`.trim().slice(0, 24);
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
    context.font = '104px system-ui, "Apple Color Emoji", "Segoe UI Emoji"';
    context.fillStyle = "#ffffff";
    context.fillText(identity.flag || "◌", 256, 88);

    context.font = '700 42px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#ffffff";
    context.fillText(String(identity.name || "guest").slice(0, 15), 256, 172);

    const joined = joinedAgoLabel(identity.joinedAt);
    const firstSeen =
      firstSeenAgoLabel(identity.firstSeenMinutes) ||
      firstSeenLabels[identity.firstVisitAge] ||
      // A directory figure carries a joined date but no live first-seen
      // reading; leave the row out instead of stating "hidden" twice.
      (joined ? "" : firstSeenLabels.hidden);
    const activity =
      activityLabels[identity.activityCategory] || activityLabels.hidden;
    const visits = Math.max(0, Math.min(999, Number(identity.visitCount) || 0));
    // Every reading the badge holds gets its own row. The world active-time
    // aggregate no longer replaces the shared activity line, so nobody's chest
    // shows less than what is publicly known about them.
    const activeRow =
      identity.totalActiveMs != null &&
      Number.isFinite(Number(identity.totalActiveMs))
        ? `ACTIVE ${badgeActiveDurationLabel(identity.totalActiveMs)} IN WORLD`
        : "";
    const sharesActivity =
      Boolean(identity.activityCategory) &&
      identity.activityCategory !== "hidden";
    const rows = [
      [firstSeen, "#9ef7c6"],
      [joined ? `FIRST ${joined}` : "", "#77d9ff"],
      [badgeClientLabel(identity), "#f7c96b"],
      [activeRow, "#9ef7c6"],
      // A directory figure publishes no live activity, so its "hidden · 0
      // visits" line is the one row worth dropping once the active-time row
      // carries a real reading.
      [
        sharesActivity || !activeRow
          ? `${activity} · ${visits} PUBLIC URL VISITS`
          : "",
        "#b9cfc4",
      ],
    ].filter(([text]) => text);
    // Five rows still have to clear the status pill at y=374.
    const step = rows.length > 4 ? 30 : 34;
    context.font = '700 21px "ForkMesh Mono", ui-monospace, monospace';
    rows.forEach(([text, color], index) => {
      context.fillStyle = color;
      context.fillText(text, 256, 224 + index * step);
    });

    // The world status the visitor set for themselves, on the chest rather
    // than only floating over the head.
    const status = badgeStatusLabel(identity);
    if (status) {
      context.font = '600 24px "ForkMesh Mono", ui-monospace, monospace';
      const width = Math.min(452, context.measureText(status).width + 44);
      roundedRect(context, 256 - width / 2, 374, width, 46, 22);
      context.fillStyle = "rgba(158,247,198,0.14)";
      context.fill();
      context.strokeStyle = "rgba(158,247,198,0.5)";
      context.lineWidth = 2;
      context.stroke();
      context.fillStyle = "#eafff2";
      context.fillText(status, 256, 398);
    }

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

// The follow control lives inside the fediverse tab's canvas rather than on a
// separate sliver of chest: the click handler hits it by UV rectangle.
const BADGE_FOLLOW_PILL = Object.freeze({
  minU: 96 / 512,
  maxU: 416 / 512,
  minV: 1 - 476 / 512,
  maxV: 1 - 424 / 512,
});

function badgeFollowPillHit(uv) {
  return Boolean(
    uv &&
      uv.x >= BADGE_FOLLOW_PILL.minU &&
      uv.x <= BADGE_FOLLOW_PILL.maxU &&
      uv.y >= BADGE_FOLLOW_PILL.minV &&
      uv.y <= BADGE_FOLLOW_PILL.maxV,
  );
}

function fediverseFollowLabel(profile) {
  if (profile.state !== "ready") return "";
  if (profile.self === true) return "THIS IS YOU";
  if (profile.canFollow !== true) return "SIGN IN TO FOLLOW";
  if (profile.pending === true) return "…";
  return profile.isFollowing === true ? "✓ FOLLOWING" : "+ FOLLOW";
}

/**
 * The second chest tab: the account's public ForkMesh profile, the activity
 * ForkMesh federates for it, the fediverse handle its owner published, and a
 * follow control. Every value comes from public /api/accounts/{name} data.
 */
function fediverseBadgeTexture(THREE, identity, accent, profile = {}) {
  const state = String(profile.state || "loading");
  const posts = Array.isArray(profile.posts) ? profile.posts.slice(0, 5) : [];
  return canvasTexture(THREE, 512, 512, (context) => {
    context.fillStyle = "#101724";
    context.fillRect(0, 0, 512, 512);
    context.strokeStyle = accent;
    context.lineWidth = 12;
    context.strokeRect(8, 8, 496, 496);

    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '700 22px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#a9b8ff";
    context.fillText("FEDIVERSE", 256, 52);

    context.font = '700 27px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#ffffff";
    context.fillText(
      String(profile.handle || `@${identity.name || "guest"}`).slice(0, 24),
      256,
      96,
    );

    let cursor = 130;
    // ForkMesh federates repositories, not accounts, so the fediverse address
    // shown here is the one this account published on its own profile.
    const fediverse = String(profile.fediverse || "").trim();
    if (fediverse) {
      context.font = '400 19px "ForkMesh Mono", ui-monospace, monospace';
      context.fillStyle = "#a9b8ff";
      context.fillText(fediverse.slice(0, 30), 256, cursor);
      cursor += 30;
    }
    context.font = '700 20px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#9ef7c6";
    context.fillText(
      state === "ready"
        ? `${Math.max(0, Number(profile.followers) || 0)} FOLLOWERS · ` +
            `${Math.max(0, Number(profile.following) || 0)} FOLLOWING`
        : state === "unavailable"
          ? "NO PUBLIC PROFILE"
          : "LOADING…",
      256,
      cursor,
    );
    cursor += 26;

    context.beginPath();
    context.moveTo(48, cursor);
    context.lineTo(464, cursor);
    context.strokeStyle = "rgba(169,184,255,0.35)";
    context.lineWidth = 2;
    context.stroke();
    cursor += 30;

    context.font = '400 19px "ForkMesh Mono", ui-monospace, monospace';
    const bio = String(profile.bio || "").trim();
    if (bio) {
      context.fillStyle = "#c9d6ff";
      context.fillText(bio.slice(0, 34), 256, cursor);
      cursor += 40;
    }
    if (posts.length) {
      posts.forEach((post, index) => {
        context.fillStyle = index % 2 ? "#93a4c8" : "#dfe8ff";
        context.fillText(String(post).slice(0, 36), 256, cursor + index * 30);
      });
    } else if (state === "ready") {
      context.fillStyle = "#7f8ea8";
      context.fillText("NO FEDERATED ACTIVITY YET", 256, cursor);
    }

    const label = fediverseFollowLabel(profile);
    if (label) {
      const active = profile.isFollowing === true;
      roundedRect(context, 96, 424, 320, 52, 26);
      context.fillStyle = active
        ? "rgba(158,247,198,0.18)"
        : "rgba(169,184,255,0.2)";
      context.fill();
      context.strokeStyle = active ? "#9ef7c6" : "#a9b8ff";
      context.lineWidth = 3;
      context.stroke();
      context.font = '700 23px "ForkMesh Mono", ui-monospace, monospace';
      context.fillStyle = active ? "#9ef7c6" : "#eaefff";
      context.fillText(label, 256, 451);
    }
  });
}

// Two small tabs under the badge switch the chest display between the
// visitor's world info and their fediverse card.
function chestTabTexture(THREE, label, active) {
  return canvasTexture(THREE, 256, 128, (context) => {
    context.clearRect(0, 0, 256, 128);
    roundedRect(context, 4, 4, 248, 120, 22);
    context.fillStyle = active ? "rgba(158,247,198,0.9)" : "rgba(8,20,17,0.92)";
    context.fill();
    context.strokeStyle = active ? "#eafff2" : "#9ef7c6";
    context.lineWidth = 6;
    context.stroke();
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '700 46px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = active ? "#08241a" : "#9ef7c6";
    context.fillText(String(label).slice(0, 8), 128, 66);
  });
}

function createAvatarChestTabs(THREE) {
  const group = new THREE.Group();
  group.name = "forkmesh-chest-tabs";
  [
    { tab: "info", label: "INFO", x: -0.19 },
    { tab: "fediverse", label: "FEDI", x: 0.19 },
  ].forEach((spec) => {
    const button = new THREE.Mesh(
      new THREE.PlaneGeometry(0.35, 0.19),
      new THREE.MeshBasicMaterial({
        map: chestTabTexture(THREE, spec.label, spec.tab === "info"),
        transparent: true,
      }),
    );
    button.name = `world-chest-tab-${spec.tab}`;
    // Avatar fronts face -Z, matching the badge just above these tabs. The
    // tabs sit flush under the badge so the wallet chip fits beneath them.
    button.position.set(spec.x, 1.79, -0.318);
    button.rotation.y = Math.PI;
    button.userData.chestTab = spec.tab;
    button.renderOrder = 3;
    group.add(button);
  });
  return group;
}

function syncChestTabs(THREE, avatar) {
  const tabs = avatar?.userData?.chestTabs;
  if (!tabs) return;
  const active = avatar.userData.chestTab === "fediverse" ? "fediverse" : "info";
  // Badge repaints are frequent; the two small tab textures only change when
  // the selected tab does.
  if (avatar.userData.chestTabRendered === active) return;
  avatar.userData.chestTabRendered = active;
  tabs.children.forEach((button) => {
    const label = button.userData.chestTab === "fediverse" ? "FEDI" : "INFO";
    const old = button.material.map;
    button.material.map = chestTabTexture(
      THREE,
      label,
      button.userData.chestTab === active,
    );
    button.material.needsUpdate = true;
    old?.dispose?.();
  });
}

// Leading colours of the drawn flag glyph, strongest first, bucketed the same
// way as dominantSampleColor. Buckets under 6% of the opaque pixels are noise
// (antialiasing, thin emblems) and are dropped. Empty when the canvas is
// tainted or the glyph did not render as a colour flag.
function flagShirtPalette(context, canvas) {
  const pixels = emojiPixels(context, canvas);
  if (!pixels) return [];
  const buckets = new Map();
  let total = 0;
  for (let i = 0; i < pixels.length; i += 4) {
    if (pixels[i + 3] < 224) continue;
    total += 1;
    const key =
      ((pixels[i] >> 4) << 8) |
      ((pixels[i + 1] >> 4) << 4) |
      (pixels[i + 2] >> 4);
    const bucket = buckets.get(key);
    if (bucket) {
      bucket.r += pixels[i];
      bucket.g += pixels[i + 1];
      bucket.b += pixels[i + 2];
      bucket.count += 1;
    } else {
      buckets.set(key, {
        r: pixels[i],
        g: pixels[i + 1],
        b: pixels[i + 2],
        count: 1,
      });
    }
  }
  if (!total) return [];
  const channel = (sum, count) =>
    Math.max(0, Math.min(255, Math.round(sum / count)))
      .toString(16)
      .padStart(2, "0");
  return [...buckets.values()]
    .filter((bucket) => bucket.count / total >= 0.06)
    .sort((a, b) => b.count - a.count)
    .slice(0, 3)
    .map(
      (bucket) =>
        `#${channel(bucket.r, bucket.count)}${channel(
          bucket.g,
          bucket.count,
        )}${channel(bucket.b, bucket.count)}`,
    );
}

// ---------------------------------------------------------------------------
// The procedural outfit tailor.
//
// Mirrors the Qt client's procedural avatar faces: the visitor's public name
// is folded through FNV-1a into a splitmix32 stream, and that one seed picks
// an entire tailored kit — cut, colourway, collar, hem, fastenings, pocket,
// seams and a stitched monogram — so the same coder wears the same outfit on
// every device while the huge feature space keeps any two names dressed
// differently. Supporting members may pin a specific cut and colourway
// (identity.outfitStyle / identity.outfitColor); everyone else wears what
// their name tailors, dyed with their flag's palette when a country is shared.
// ---------------------------------------------------------------------------

function outfitRandom(seedText) {
  // FNV-1a fold of the seed, then splitmix32 streams unlimited draws out of
  // it — the same seeding recipe the Qt client's forkMeshAvatarPng uses.
  let state = 0x811c9dc5;
  for (const char of String(seedText || "")) {
    state = Math.imul(state ^ char.charCodeAt(0), 0x01000193) >>> 0;
  }
  const next = () => {
    state = (state + 0x9e3779b9) >>> 0;
    let word = state;
    word = Math.imul(word ^ (word >>> 16), 0x21f0aaad);
    word = Math.imul(word ^ (word >>> 15), 0x735a2d97);
    return ((word ^ (word >>> 15)) >>> 0) / 4294967296;
  };
  return {
    next,
    int: (n) => (n > 0 ? Math.floor(next() * n) % n : 0),
    chance: (pct) => next() * 100 < pct,
    range: (low, high) => low + next() * (high - low),
  };
}

function mixHex(hex, target, amount) {
  const parse = (value) => {
    const raw = String(value).replace("#", "");
    const packed = parseInt(
      raw.length === 3 ? raw.replace(/./g, "$&$&") : raw,
      16,
    );
    return [(packed >> 16) & 255, (packed >> 8) & 255, packed & 255];
  };
  const from = parse(hex);
  const to = parse(target);
  const channel = (index) =>
    Math.round(from[index] + (to[index] - from[index]) * amount)
      .toString(16)
      .padStart(2, "0");
  return `#${channel(0)}${channel(1)}${channel(2)}`;
}
const shadeHex = (hex, amount) => mixHex(hex, "#06110e", amount);
const tintHex = (hex, amount) => mixHex(hex, "#f7fbf8", amount);

// Curated colourways a name can draw when no flag palette dresses the kit:
// [body, accent, trim].
const OUTFIT_COLORWAYS = [
  ["#174f3d", "#9ef7c6", "#f7fbf8"],
  ["#183f62", "#77d9ff", "#f2f8ff"],
  ["#633e23", "#f7c96b", "#fff4dc"],
  ["#573965", "#d5b6ff", "#f6eeff"],
  ["#6a3346", "#ff9eb7", "#ffeef3"],
  ["#0f3a3a", "#3fd8c2", "#e8fffb"],
  ["#40320f", "#e0b23e", "#f7ecc8"],
  ["#2a2f5e", "#8b9dff", "#eef1ff"],
  ["#4a1f1f", "#ff8a5c", "#ffe9dc"],
  ["#123b1f", "#7ce07c", "#eaffea"],
  ["#3b1f4a", "#e07ce0", "#fbe9ff"],
  ["#20343c", "#9ec9f7", "#eff7ff"],
  ["#5e2a10", "#ffd166", "#fff1cf"],
  ["#101d3a", "#f7e96b", "#fffbe0"],
];

// One painter per outfit cut. Each draws over a canvas already filled with
// kit.base; ids stay in lockstep with OUTFIT_STYLE_OPTIONS in world-data.js.
const OUTFIT_CUTS = {
  sash(context, rng, kit) {
    // The classic kit: an accent sash with trim piping over a faint weave.
    context.save();
    context.globalAlpha = 0.16;
    context.fillStyle = kit.deep;
    for (let y = 0; y < 256; y += 14) context.fillRect(0, y, 256, 5);
    context.restore();
    context.save();
    context.translate(128, 128);
    context.rotate(rng.chance(50) ? -Math.PI / 8 : Math.PI / 8);
    const width = rng.range(64, 96);
    context.fillStyle = kit.accent;
    context.fillRect(-256, -width / 2, 512, width);
    context.fillStyle = kit.trim;
    context.fillRect(-256, -width / 2 - 24, 512, 12);
    context.fillRect(-256, width / 2 + 12, 512, 12);
    if (rng.chance(45)) {
      context.fillStyle = kit.deep;
      context.fillRect(-256, -6, 512, 12);
    }
    context.restore();
  },
  racer(context, rng, kit) {
    const x = rng.range(84, 130);
    const width = rng.range(22, 34);
    const gap = rng.range(10, 18);
    context.fillStyle = kit.accent;
    context.fillRect(x, 0, width, 256);
    context.fillRect(x + width + gap, 0, width * 0.55, 256);
    context.fillStyle = kit.trim;
    context.fillRect(x - 6, 0, 4, 256);
    context.fillRect(x + width + gap + width * 0.55 + 2, 0, 4, 256);
    if (rng.chance(60)) {
      // A pit-lane checker band across one shoulder line.
      const bandY = rng.range(30, 60);
      for (let i = 0; i < 16; i += 1) {
        context.fillStyle = i % 2 ? kit.trim : kit.deep;
        context.fillRect(i * 16, bandY, 16, 12);
      }
    }
  },
  chevron(context, rng, kit) {
    const count = 3 + rng.int(3);
    const step = rng.range(34, 46);
    const drop = rng.range(50, 90);
    for (let i = 0; i < count; i += 1) {
      context.fillStyle = i % 2 ? kit.trim : kit.accent;
      context.beginPath();
      const top = 30 + i * step;
      context.moveTo(-8, top);
      context.lineTo(128, top + drop);
      context.lineTo(264, top);
      context.lineTo(264, top + 18);
      context.lineTo(128, top + drop + 18);
      context.lineTo(-8, top + 18);
      context.closePath();
      context.fill();
    }
  },
  argyle(context, rng, kit) {
    const size = rng.range(40, 56);
    context.save();
    context.translate(128, 128);
    context.rotate(Math.PI / 4);
    for (let x = -220; x < 220; x += size) {
      for (let y = -220; y < 220; y += size) {
        const even = (Math.round(x / size) + Math.round(y / size)) % 2 === 0;
        context.globalAlpha = even ? 0.85 : 0.6;
        context.fillStyle = even ? kit.accent : shadeHex(kit.base, 0.28);
        context.fillRect(x + 2, y + 2, size - 4, size - 4);
      }
    }
    context.globalAlpha = 1;
    context.strokeStyle = kit.trim;
    context.lineWidth = 2;
    context.setLineDash([6, 6]);
    for (let d = -220; d < 220; d += size) {
      context.beginPath();
      context.moveTo(d, -220);
      context.lineTo(d, 220);
      context.stroke();
      context.beginPath();
      context.moveTo(-220, d);
      context.lineTo(220, d);
      context.stroke();
    }
    context.setLineDash([]);
    context.restore();
  },
  circuit(context, rng, kit) {
    context.fillStyle = shadeHex(kit.base, 0.35);
    context.fillRect(0, 0, 256, 256);
    context.strokeStyle = kit.accent;
    context.lineWidth = 3;
    const pads = [];
    for (let trace = 0; trace < 9; trace += 1) {
      let x = rng.range(16, 240);
      let y = rng.range(16, 240);
      context.beginPath();
      context.moveTo(x, y);
      const turns = 2 + rng.int(3);
      for (let turn = 0; turn < turns; turn += 1) {
        // Manhattan routing: each turn moves along one axis only.
        if (rng.chance(50)) x = rng.range(16, 240);
        else y = rng.range(16, 240);
        context.lineTo(x, y);
      }
      context.stroke();
      pads.push([x, y]);
    }
    pads.forEach(([x, y]) => {
      context.fillStyle = kit.trim;
      context.beginPath();
      context.arc(x, y, 5, 0, Math.PI * 2);
      context.fill();
      context.fillStyle = shadeHex(kit.base, 0.5);
      context.beginPath();
      context.arc(x, y, 2, 0, Math.PI * 2);
      context.fill();
    });
    const chipX = rng.range(60, 160);
    const chipY = rng.range(60, 160);
    context.fillStyle = kit.deep;
    context.fillRect(chipX, chipY, 44, 30);
    context.fillStyle = kit.trim;
    for (let leg = 0; leg < 4; leg += 1) {
      context.fillRect(chipX + 6 + leg * 9, chipY - 6, 4, 6);
      context.fillRect(chipX + 6 + leg * 9, chipY + 30, 4, 6);
    }
  },
  pixel(context, rng, kit) {
    // A mirrored identicon mosaic — the same trick the fallback avatar
    // services use, worn as knitwear.
    const cells = 6;
    const size = 256 / cells;
    const palette = [kit.accent, kit.trim, shadeHex(kit.base, 0.3), kit.light];
    for (let x = 0; x < cells / 2; x += 1) {
      for (let y = 0; y < cells; y += 1) {
        if (!rng.chance(52)) continue;
        context.fillStyle = palette[rng.int(palette.length)];
        context.fillRect(x * size + 1, y * size + 1, size - 2, size - 2);
        context.fillRect(
          (cells - 1 - x) * size + 1,
          y * size + 1,
          size - 2,
          size - 2,
        );
      }
    }
  },
  waves(context, rng, kit) {
    const bands = 3 + rng.int(3);
    for (let band = 0; band < bands; band += 1) {
      const baseY = 40 + band * (190 / bands);
      const amplitude = rng.range(10, 26);
      const wavelength = rng.range(60, 120);
      const phase = rng.range(0, Math.PI * 2);
      context.globalAlpha = band % 2 ? 0.9 : 0.55;
      context.fillStyle = band % 2 ? kit.accent : kit.trim;
      context.beginPath();
      context.moveTo(0, 256);
      for (let x = 0; x <= 256; x += 4) {
        context.lineTo(
          x,
          baseY + Math.sin(phase + (x / wavelength) * Math.PI * 2) * amplitude,
        );
      }
      context.lineTo(256, 256);
      context.closePath();
      context.fill();
    }
    context.globalAlpha = 1;
  },
  starfield(context, rng, kit) {
    context.fillStyle = shadeHex(kit.base, 0.55);
    context.fillRect(0, 0, 256, 256);
    for (let star = 0; star < 70; star += 1) {
      const x = rng.range(0, 256);
      const y = rng.range(0, 256);
      const radius = rng.range(0.6, 2.2);
      context.globalAlpha = rng.range(0.35, 1);
      context.fillStyle = rng.chance(20) ? kit.accent : kit.trim;
      context.beginPath();
      context.arc(x, y, radius, 0, Math.PI * 2);
      context.fill();
    }
    context.globalAlpha = 1;
    const planetX = rng.range(60, 196);
    const planetY = rng.range(120, 210);
    const planetR = rng.range(16, 26);
    context.fillStyle = kit.accent;
    context.beginPath();
    context.arc(planetX, planetY, planetR, 0, Math.PI * 2);
    context.fill();
    context.strokeStyle = kit.trim;
    context.lineWidth = 3;
    context.save();
    context.translate(planetX, planetY);
    context.rotate(-0.5);
    context.beginPath();
    context.ellipse(0, 0, planetR * 1.7, planetR * 0.5, 0, 0, Math.PI * 2);
    context.stroke();
    context.restore();
  },
  hex(context, rng, kit) {
    const radius = rng.range(16, 24);
    const height = radius * Math.sqrt(3);
    context.lineWidth = 2.5;
    for (let row = -1; row * height * 0.5 < 290; row += 1) {
      for (let col = -1; col * radius * 3 < 290; col += 1) {
        const x = col * radius * 3 + (row % 2 ? radius * 1.5 : 0);
        const y = row * height * 0.5;
        context.beginPath();
        for (let corner = 0; corner < 6; corner += 1) {
          const angle = (Math.PI / 3) * corner;
          const px = x + Math.cos(angle) * radius;
          const py = y + Math.sin(angle) * radius;
          if (corner === 0) context.moveTo(px, py);
          else context.lineTo(px, py);
        }
        context.closePath();
        if (rng.chance(18)) {
          context.globalAlpha = 0.8;
          context.fillStyle = kit.accent;
          context.fill();
        }
        context.globalAlpha = 0.55;
        context.strokeStyle = kit.trim;
        context.stroke();
        context.globalAlpha = 1;
      }
    }
  },
  bolt(context, rng, kit) {
    // One big embroidered hotfix bolt with a glow halo.
    const cx = rng.range(96, 160);
    const lean = rng.range(-18, 18);
    const points = [
      [cx + 26 + lean, 18],
      [cx - 30 + lean * 0.5, 118],
      [cx - 2, 118],
      [cx - 26, 238],
      [cx + 34, 108],
      [cx + 2, 108],
    ];
    context.save();
    context.shadowColor = kit.accent;
    context.shadowBlur = 22;
    context.fillStyle = kit.accent;
    context.beginPath();
    points.forEach(([x, y], index) =>
      index ? context.lineTo(x, y) : context.moveTo(x, y),
    );
    context.closePath();
    context.fill();
    context.restore();
    context.strokeStyle = kit.trim;
    context.lineWidth = 3;
    context.stroke();
  },
  tartan(context, rng, kit) {
    const step = rng.range(44, 64);
    const bandWidth = rng.range(14, 24);
    const xOffset = rng.range(0, step);
    const yOffset = rng.range(0, step);
    context.globalAlpha = 0.85;
    context.fillStyle = kit.accent;
    for (let x = xOffset; x < 256; x += step) {
      context.fillRect(x, 0, bandWidth, 256);
    }
    context.globalAlpha = 0.55;
    context.fillStyle = kit.deep;
    for (let y = yOffset; y < 256; y += step) {
      context.fillRect(0, y, 256, bandWidth);
    }
    context.globalAlpha = 1;
    context.strokeStyle = kit.trim;
    context.lineWidth = 2;
    context.setLineDash([4, 4]);
    for (let x = xOffset; x < 256; x += step) {
      context.beginPath();
      context.moveTo(x + bandWidth + 6, 0);
      context.lineTo(x + bandWidth + 6, 256);
      context.stroke();
    }
    context.setLineDash([]);
  },
  binary(context, rng, kit) {
    context.fillStyle = shadeHex(kit.base, 0.4);
    context.fillRect(0, 0, 256, 256);
    context.font = '700 18px "ForkMesh Mono", ui-monospace, monospace';
    context.textAlign = "center";
    context.textBaseline = "middle";
    const columns = 9;
    for (let col = 0; col < columns; col += 1) {
      const x = 16 + col * (224 / (columns - 1));
      const drop = rng.range(0, 20);
      const bright = rng.int(12);
      for (let row = 0; row < 12; row += 1) {
        context.globalAlpha = row === bright ? 1 : rng.range(0.16, 0.5);
        context.fillStyle = row === bright ? kit.trim : kit.accent;
        context.fillText(rng.chance(50) ? "1" : "0", x, drop + 10 + row * 21);
      }
    }
    context.globalAlpha = 1;
  },
};

// Tailoring details layered over every cut. Each is its own seeded roll, so
// two coders who happen to share a cut still differ in collar, hem,
// fastenings, pocket, seams, and monogram.
function tailorOutfitDetails(context, rng, kit, monogram) {
  if (rng.chance(70)) {
    // Ribbed collar band along the shoulder line.
    context.fillStyle = rng.chance(50) ? kit.trim : kit.deep;
    context.fillRect(0, 0, 256, 16);
    context.fillStyle = kit.accent;
    context.fillRect(0, 16, 256, 3);
  }
  if (rng.chance(55)) {
    context.fillStyle = rng.chance(50) ? kit.deep : kit.accent;
    context.fillRect(0, 244, 256, 12);
  }
  const fastening = rng.int(3); // 0 plain · 1 zip · 2 buttons
  if (fastening === 1) {
    context.strokeStyle = kit.trim;
    context.lineWidth = 3;
    context.beginPath();
    context.moveTo(128, 20);
    context.lineTo(128, 236);
    context.stroke();
    context.fillStyle = kit.trim;
    context.fillRect(124, 26, 8, 12);
  } else if (fastening === 2) {
    context.fillStyle = kit.trim;
    for (let button = 0; button < 4; button += 1) {
      context.beginPath();
      context.arc(128, 48 + button * 48, 4.5, 0, Math.PI * 2);
      context.fill();
    }
  }
  if (rng.chance(45)) {
    // A chest pocket with a trim flap.
    const x = rng.chance(50) ? 52 : 168;
    context.fillStyle = shadeHex(kit.base, 0.22);
    context.fillRect(x, 140, 40, 34);
    context.fillStyle = kit.trim;
    context.fillRect(x, 140, 40, 7);
  }
  if (rng.chance(50)) {
    // Dashed side seams.
    context.strokeStyle = kit.trim;
    context.globalAlpha = 0.7;
    context.lineWidth = 2;
    context.setLineDash([5, 7]);
    [10, 246].forEach((x) => {
      context.beginPath();
      context.moveTo(x, 22);
      context.lineTo(x, 240);
      context.stroke();
    });
    context.setLineDash([]);
    context.globalAlpha = 1;
  }
  if (monogram) {
    // The stitched monogram patch — the first letter of the public name, the
    // same letter the Qt fallback avatar tile shows.
    context.fillStyle = kit.deep;
    roundedRect(context, 22, 30, 34, 34, 8);
    context.fill();
    context.strokeStyle = kit.trim;
    context.lineWidth = 2;
    context.setLineDash([3, 3]);
    context.stroke();
    context.setLineDash([]);
    context.fillStyle = kit.light;
    context.font = '700 22px "ForkMesh Favorit", system-ui, sans-serif';
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(monogram, 39, 48);
  }
}

// The one flag glyph the avatar wears is the chest badge's; the shirt never
// prints it. When a country is shared its sampled palette dyes the kit, and
// the visitor's public name tailors everything else — cut, trims, monogram —
// falling back entirely to the name-seeded colourway when the flag cannot be
// drawn or sampled.
function countryShirtTexture(THREE, identity) {
  const code = /^[A-Z]{2}$/.test(String(identity.countryCode || ""))
    ? String(identity.countryCode)
    : "";
  const flag = code && identity.flag && identity.flag !== "◌"
    ? identity.flag
    : "◌";
  const wornCut = OUTFIT_STYLE_IDS.includes(String(identity.outfitStyle || ""))
    ? String(identity.outfitStyle)
    : "";
  const wornColor = OUTFIT_COLOR_HEX[identity.outfitColor] || "";
  return canvasTexture(THREE, 256, 256, (context, canvas) => {
    const rng = outfitRandom(
      "outfit:" + String(identity.name || "guest").trim().toLowerCase(),
    );
    // Fixed draw order keeps the stream stable: the seeded cut and colourway
    // are always consumed, even when a Supporting member's pick replaces them.
    const seededCut = OUTFIT_STYLE_IDS[rng.int(OUTFIT_STYLE_IDS.length)];
    let [base, accent, trim] =
      OUTFIT_COLORWAYS[rng.int(OUTFIT_COLORWAYS.length)];
    if (flag !== "◌") {
      // Drawn big only to be sampled, then painted over entirely: nothing of
      // the glyph itself survives onto the cloth.
      context.clearRect(0, 0, 256, 256);
      context.textAlign = "center";
      context.textBaseline = "middle";
      context.font = '224px system-ui, "Apple Color Emoji", "Segoe UI Emoji"';
      context.fillText(flag, 128, 128);
      const sampled = flagShirtPalette(context, canvas);
      if (sampled.length >= 2) {
        [base, accent] = sampled;
        trim = sampled[2] || "#f7fbf8";
      }
    }
    if (wornColor) {
      // The Supporting-member colourway perk re-dyes the whole kit.
      base = wornColor;
      accent = tintHex(wornColor, 0.55);
      trim = shadeHex(wornColor, 0.45);
    }
    const kit = {
      base,
      accent,
      trim,
      deep: shadeHex(base, 0.42),
      light: tintHex(base, 0.6),
    };
    context.clearRect(0, 0, 256, 256);
    context.textAlign = "left";
    context.textBaseline = "alphabetic";
    context.fillStyle = kit.base;
    context.fillRect(0, 0, 256, 256);
    const cut = OUTFIT_CUTS[wornCut || seededCut] || OUTFIT_CUTS.sash;
    cut(context, rng, kit);
    const name = String(identity.name || "").trim();
    tailorOutfitDetails(
      context,
      rng,
      kit,
      /^[A-Za-z0-9]/.test(name) ? name[0].toUpperCase() : "",
    );
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

// Repaints ForkBot's chest screen in place. `state` is null for the idle
// wordmark, or { message, thinking, dotPhase } while ForkBot is answering a
// mention: the echoed line renders in quotes and, once thinking starts, a row
// of pulsing dots runs beneath it until the reply is broadcast.
function drawForkbotScreen(context, canvas, state) {
  const width = canvas.width;
  const height = canvas.height;
  context.clearRect(0, 0, width, height);
  roundedRect(context, 3, 3, width - 6, height - 6, 20);
  context.fillStyle = "#06181d";
  context.fill();
  context.strokeStyle = "#4dc8e8";
  context.lineWidth = 5;
  context.stroke();
  context.textAlign = "center";
  context.textBaseline = "middle";
  if (!state) {
    context.fillStyle = "#2fa5c4";
    context.font = '700 44px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("FORKBOT", width / 2, height / 2);
    return;
  }
  context.fillStyle = "#e9fbff";
  context.font = '600 34px "ForkMesh Favorit", system-ui, sans-serif';
  const words = `“${state.message}”`.split(/\s+/).filter(Boolean);
  const lines = [""];
  for (const word of words) {
    const current = lines[lines.length - 1];
    const candidate = current ? `${current} ${word}` : word;
    if (!current || context.measureText(candidate).width <= width - 56) {
      lines[lines.length - 1] = candidate;
    } else if (lines.length < 3) {
      lines.push(word);
    } else {
      lines[2] += "…";
      break;
    }
  }
  const textCenter = state.thinking ? height / 2 - 26 : height / 2;
  lines.forEach((line, index) => {
    context.fillText(
      line,
      width / 2,
      textCenter + (index - (lines.length - 1) / 2) * 40,
      width - 56,
    );
  });
  if (state.thinking) {
    for (let dot = 0; dot < 3; dot += 1) {
      const active = dot === state.dotPhase % 3;
      context.fillStyle = active ? "#9ef7c6" : "#2c5a66";
      context.beginPath();
      context.arc(
        width / 2 + (dot - 1) * 44,
        height - 40,
        active ? 13 : 9,
        0,
        Math.PI * 2,
      );
      context.fill();
    }
  }
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
  if (options.depthWrite !== undefined) {
    parameters.depthWrite = options.depthWrite;
  }
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

function makeOfficeWallPlacard(
  THREE,
  title,
  subtitle,
  color = "#9ef7c6",
  width = 22,
  height = 3.6,
) {
  const placard = new THREE.Mesh(
    new THREE.PlaneGeometry(width, height),
    new THREE.MeshBasicMaterial({
      map: wordTexture(THREE, title, subtitle, color),
      toneMapped: false,
    }),
  );
  placard.userData.officeWallMounted = true;
  return placard;
}

// Branded onto the plank itself (the seat's top face texture) instead of a
// floating sign, so an empty bench still says whose seat it is: the account
// it belongs to, and whether they are on it or out walking the world. An
// unclaimed seat reads as the open guest bench. Canvas aspect (480x180)
// matches the seat top's width:depth ratio (1.6:0.6) so the wood grain and
// lettering aren't stretched.
function campfireSeatPlateTexture(THREE, name, away) {
  const label = String(name || "").trim().slice(0, 18);
  const glow = label ? (away ? "#ffd479" : "#9ef7c6") : "#77d9ff";
  return canvasTexture(THREE, 480, 180, (context) => {
    context.fillStyle = "#8a5a33";
    context.fillRect(0, 0, 480, 180);
    context.strokeStyle = "rgba(63,38,17,0.35)";
    context.lineWidth = 2;
    for (let grain = 20; grain < 180; grain += 24) {
      context.beginPath();
      context.moveTo(0, grain);
      context.bezierCurveTo(120, grain - 5, 360, grain + 5, 480, grain);
      context.stroke();
    }
    context.textAlign = "center";
    context.textBaseline = "middle";
    embossPlankText(
      context,
      label || "OPEN SEAT",
      240,
      74,
      '700 52px "ForkMesh Favorit", system-ui, sans-serif',
      glow,
    );
    embossPlankText(
      context,
      label ? (away ? "OUT AND ABOUT" : "AT THE FIRE") : "GUESTS WELCOME",
      240,
      124,
      '400 24px "ForkMesh Mono", ui-monospace, monospace',
      glow,
    );
  });
}

// Who joined last, by the public directory's joined timestamp. An account
// seated straight from a presence frame carries no joined date yet (0), so it
// is skipped rather than ranked as the oldest member in the circle.
function newestMemberName(members) {
  let newest = "";
  let joinedAt = 0;
  (Array.isArray(members) ? members : []).forEach((member) => {
    const name = String(member?.name || "").trim();
    const created = Number(member?.createdAt) || 0;
    if (!name || created <= joinedAt) return;
    joinedAt = created;
    newest = name.slice(0, 32);
  });
  return newest;
}

// The headline membership number, drawn as glowing embers on transparency so
// it can hang inside the campfire's flames without a plate behind it. The
// account that joined most recently is credited on a line underneath, so the
// fire says who the latest arrival is and not just how many there are.
function campfireMemberCountTexture(THREE, total, newest) {
  const count = Math.max(0, Math.min(999999, Math.round(Number(total) || 0)));
  const digits = count.toLocaleString("en-US");
  const latest = String(newest || "").trim().slice(0, 18);
  return canvasTexture(THREE, 512, 256, (context) => {
    context.clearRect(0, 0, 512, 256);
    context.textAlign = "center";
    context.textBaseline = "middle";
    const ember = context.createLinearGradient(0, 20, 0, 150);
    ember.addColorStop(0, "#fff6cf");
    ember.addColorStop(0.55, "#ffc457");
    ember.addColorStop(1, "#ff7a2f");
    context.shadowColor = "rgba(255,122,47,0.95)";
    context.shadowBlur = 36;
    context.fillStyle = ember;
    context.font = '700 132px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText(digits, 256, latest ? 84 : 104);
    context.shadowBlur = 20;
    context.fillStyle = "#ffdcac";
    context.font = '400 40px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(count === 1 ? "MEMBER" : "MEMBERS", 256, latest ? 172 : 198);
    if (!latest) return;
    context.shadowBlur = 14;
    context.fillStyle = "#ffbd7a";
    context.font = '400 24px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("NEWEST", 256, 210);
    context.fillStyle = "#fff1d2";
    context.font = '700 30px "ForkMesh Favorit", system-ui, sans-serif';
    context.fillText(latest, 256, 240);
  });
}

// Carves rather than paints: a dark shadow above and a warm highlight below
// read as a groove branded into the wood instead of ink sitting on top of it.
function embossPlankText(context, text, x, y, font, glow) {
  context.font = font;
  context.fillStyle = "rgba(20,10,4,0.6)";
  context.fillText(text, x, y - 1.4);
  context.fillStyle = glow;
  context.globalAlpha = 0.5;
  context.fillText(text, x, y + 1.4);
  context.globalAlpha = 1;
  context.fillStyle = "#2a160a";
  context.fillText(text, x, y);
}

// Straight-legged pitches (standing, walking) leave the knees locked, so the
// leg reads as the one block it draws as.
function applyLegPitch(avatar, leftPitch, rightPitch) {
  const legs = avatar?.userData;
  if (!legs?.leftLeg || !legs?.rightLeg) return;
  legs.leftLeg.rotation.x = leftPitch;
  legs.rightLeg.rotation.x = rightPitch;
  if (legs.leftKnee) legs.leftKnee.rotation.x = 0;
  if (legs.rightKnee) legs.rightKnee.rotation.x = 0;
}

function applySeatedLegPose(avatar) {
  const legs = avatar?.userData;
  if (!legs?.leftLeg || !legs?.rightLeg) return;
  legs.leftLeg.rotation.x = SEATED_LEG_PITCH;
  legs.rightLeg.rotation.x = SEATED_LEG_PITCH;
  if (legs.leftKnee) legs.leftKnee.rotation.x = SEATED_KNEE_PITCH;
  if (legs.rightKnee) legs.rightKnee.rotation.x = SEATED_KNEE_PITCH;
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
// Fallback for the head sphere when the worn emoji cannot be sampled. The
// live colour is read back out of the rendered glyph instead (see
// edgeEmojiColor) so the sphere is exactly the emoji's own rim colour.
const AVATAR_EMOJI_SKIN_COLOR = "#ffcc4d";
// How far the emoji decal is wrapped around the head, centred on the front.
// Both spans are wider than the glyph itself so the padded canvas carries the
// emoji's rim colour past where the face ends and no bare sphere is left over.
const AVATAR_FACE_PHI_START = Math.PI * 0.03;
const AVATAR_FACE_PHI_LENGTH = Math.PI * 0.94;
const AVATAR_FACE_THETA_START = Math.PI * 0.117;
const AVATAR_FACE_THETA_LENGTH = Math.PI * 0.766;

// Reads back the drawn glyph, or "" when the canvas is tainted.
function emojiPixels(context, canvas) {
  try {
    return context.getImageData(0, 0, canvas.width, canvas.height).data;
  } catch (_) {
    return null;
  }
}

// Dominant colour of a flat list of sampled [r, g, b] triples, bucketed at
// 4 bits per channel. Returns "" unless one bucket owns at least `share` of
// the samples: a glyph that barely rendered (or is mostly outline) would
// otherwise hand back a near-black head.
function dominantSampleColor(samples, share) {
  const buckets = new Map();
  const total = samples.length / 3;
  for (let i = 0; i < samples.length; i += 3) {
    const key =
      ((samples[i] >> 4) << 8) |
      ((samples[i + 1] >> 4) << 4) |
      (samples[i + 2] >> 4);
    const bucket = buckets.get(key);
    if (bucket) {
      bucket.r += samples[i];
      bucket.g += samples[i + 1];
      bucket.b += samples[i + 2];
      bucket.count += 1;
    } else {
      buckets.set(key, {
        r: samples[i],
        g: samples[i + 1],
        b: samples[i + 2],
        count: 1,
      });
    }
  }
  let best = null;
  buckets.forEach((bucket) => {
    if (!best || bucket.count > best.count) best = bucket;
  });
  if (!best || !total || best.count / total < share) return "";
  const channel = (sum) =>
    Math.max(0, Math.min(255, Math.round(sum / best.count)))
      .toString(16)
      .padStart(2, "0");
  return `#${channel(best.r)}${channel(best.g)}${channel(best.b)}`;
}

// Most-common opaque colour anywhere in the drawn emoji, used as the second
// choice when the rim itself is too varied to read.
function dominantEmojiColor(context, canvas) {
  const pixels = emojiPixels(context, canvas);
  if (!pixels) return "";
  const samples = [];
  for (let i = 0; i < pixels.length; i += 4) {
    if (pixels[i + 3] < 224) continue;
    samples.push(pixels[i], pixels[i + 1], pixels[i + 2]);
  }
  return dominantSampleColor(samples, 0.25);
}

// Colour of the emoji's own outer rim. The head sphere is painted with it and
// the decal's padding is flooded with it, so the wrapped glyph and the sphere
// behind it meet without a seam or a gap of some other yellow.
function edgeEmojiColor(context, canvas) {
  const pixels = emojiPixels(context, canvas);
  if (!pixels) return "";
  const width = canvas.width;
  const height = canvas.height;
  const centerX = (width - 1) / 2;
  const centerY = (height - 1) / 2;
  const reach = Math.round(Math.max(width, height) / 2);
  // Step back in from the antialiased rim (and any dark outline drawn on it)
  // before sampling, so the head takes the glyph's body colour.
  const inset = Math.max(2, Math.round(Math.min(width, height) * 0.04));
  const rays = 72;
  const samples = [];
  const opaqueAt = (x, y) =>
    x >= 0 &&
    y >= 0 &&
    x < width &&
    y < height &&
    pixels[(y * width + x) * 4 + 3] >= 224;
  for (let ray = 0; ray < rays; ray += 1) {
    const angle = (ray / rays) * Math.PI * 2;
    const stepX = Math.cos(angle);
    const stepY = Math.sin(angle);
    let rim = -1;
    for (let d = reach; d >= 0; d -= 1) {
      const x = Math.round(centerX + stepX * d);
      const y = Math.round(centerY + stepY * d);
      if (opaqueAt(x, y)) {
        rim = d;
        break;
      }
    }
    if (rim < 0) continue;
    const d = Math.max(0, rim - inset);
    const x = Math.round(centerX + stepX * d);
    const y = Math.round(centerY + stepY * d);
    if (!opaqueAt(x, y)) continue;
    const i = (y * width + x) * 4;
    samples.push(pixels[i], pixels[i + 1], pixels[i + 2]);
  }
  // A rim that is half one colour and half another (flags, split glyphs) is
  // not a skin tone; let the whole-glyph sampler answer instead.
  if (samples.length < rays * 3 * 0.5) return "";
  return dominantSampleColor(samples, 0.45);
}

// Mouse-activity antenna: one solid green, blinked hard on and hard off.
const ANTENNA_LIT_COLOR = "#22e06a";
const ANTENNA_DARK_COLOR = "#0d3b22";
const ANTENNA_STALK_COLOR = "#1aa856";
const ANTENNA_BLINK_MIN_HZ = 0.9;
const ANTENNA_BLINK_MAX_HZ = 5.4;

// Chest activity light: one colour per coarse account-recency bucket from the
// server ("active within …"). The freshest bucket breathes softly in
// animateAvatarActivity; every older bucket holds a steady colour, stepping
// bright green → dim green → green-orange → green-red → orange → red → grey.
const ACTIVITY_LIGHT_COLORS = Object.freeze({
  hour: "#3ce97f",
  "5h": "#2e8054",
  "24h": "#94b23a",
  "3d": "#b1892f",
  "5d": "#e0762c",
  "10d": "#d63b30",
  stale: "#767c85",
});
const ACTIVITY_LIGHT_BREATH_HZ = 0.33;

const AVATAR_SOLANA_ADDRESS_RE = /^[1-9A-HJ-NP-Za-km-z]{32,44}$/;

// The flag-coloured shirt cloth is mapped onto every face of the torso and
// arm boxes. The upward-facing tops are the ones read from across the square,
// so those four UVs are turned a half turn to keep the sash running the same
// way round.
function rotateBoxTopUVs(geometry) {
  const uv = geometry.attributes?.uv;
  if (!uv) return geometry;
  // BoxGeometry lays its faces out +X, -X, +Y, -Y, +Z, -Z with four vertices
  // each, so the top face owns vertices 8 through 11.
  for (let i = 8; i < 12; i += 1) {
    uv.setXY(i, 1 - uv.getX(i), 1 - uv.getY(i));
  }
  uv.needsUpdate = true;
  return geometry;
}

function avatarFaceTexture(THREE, emoji) {
  let color = "";
  // The glyph is drawn smaller than the canvas so the decal (which is widened
  // to match, see AVATAR_FACE_*) keeps the face at the same angular size while
  // its padding wraps further around the head.
  const texture = canvasTexture(THREE, 128, 128, (context, canvas) => {
    context.clearRect(0, 0, 128, 128);
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '104px system-ui, "Apple Color Emoji", "Segoe UI Emoji"';
    context.fillStyle = "#1d130c";
    context.fillText(emoji, 64, 67);
    color =
      edgeEmojiColor(context, canvas) || dominantEmojiColor(context, canvas);
    // Flood everything the glyph did not cover — the padding and the corners
    // outside a round emoji — with its own rim colour, painted underneath so
    // the face itself is untouched. Nothing of the head shows through, so
    // there is no gap and no seam where the decal ends.
    context.globalCompositeOperation = "destination-over";
    context.fillStyle = color || AVATAR_EMOJI_SKIN_COLOR;
    context.fillRect(0, 0, 128, 128);
    context.globalCompositeOperation = "source-over";
  });
  return { texture, color: color || AVATAR_EMOJI_SKIN_COLOR };
}

function syncAvatarFace(THREE, avatar) {
  const face = avatar.userData.faceMesh;
  if (!face?.material) return;
  // A Supporting member wearing their account avatar photo keeps it on
  // through status-emoji changes; the emoji face returns when the photo is
  // taken off.
  if (avatar.userData.faceImageUrl) return;
  const worn = avatar.userData.statusEmoji || AVATAR_DEFAULT_FACE_EMOJI;
  if (avatar.userData.faceEmojiShown === worn) return;
  const drawn = avatarFaceTexture(THREE, worn);
  face.material.map?.dispose?.();
  face.material.map = drawn.texture;
  face.material.needsUpdate = true;
  // Same unlit material family as the decal, so the sphere and the emoji
  // print the identical colour under every light in the world.
  const skin = avatar.userData.skin;
  if (skin) {
    skin.color.set(drawn.color);
    skin.needsUpdate = true;
  }
  avatar.userData.faceEmojiShown = worn;
}

// Wearing (or taking off) a consented account avatar photo as the 3D face —
// a Supporting member perk. Only an already-public /api/accounts image ever
// reaches here; presence frames carry nothing but the opt-in boolean.
function applyAvatarFaceImage(THREE, avatar, url) {
  const face = avatar?.userData?.faceMesh;
  if (!face?.material) return;
  const worn = String(url || "");
  if (avatar.userData.faceImageUrl === worn) return;
  avatar.userData.faceImageUrl = worn;
  if (!worn) {
    // Redraw the emoji face on the next sync.
    avatar.userData.faceEmojiShown = "";
    syncAvatarFace(THREE, avatar);
    return;
  }
  new THREE.TextureLoader().load(
    worn,
    (texture) => {
      if (avatar.userData.faceImageUrl !== worn) {
        texture.dispose();
        return;
      }
      texture.colorSpace = THREE.SRGBColorSpace;
      face.material.map?.dispose?.();
      face.material.map = texture;
      face.material.needsUpdate = true;
      avatar.userData.faceEmojiShown = "";
    },
    undefined,
    () => {},
  );
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

  // Unlit so the head sphere renders the sampled emoji colour exactly, with
  // no lighting term to pull it off the flat decal wrapped over it.
  const skin = new THREE.MeshBasicMaterial({
    color: AVATAR_EMOJI_SKIN_COLOR,
  });
  const shirt = makeMaterial(THREE, "#ffffff", {
    roughness: 0.8,
  });
  applyOutfit(THREE, shirt, identity);
  shirt.needsUpdate = true;
  const dark = makeMaterial(THREE, "#101d19", { roughness: 0.85 });
  const shoe = makeMaterial(THREE, "#07100e", { roughness: 0.82 });

  const torso = new THREE.Mesh(
    rotateBoxTopUVs(new THREE.BoxGeometry(1.1, 1.5, 0.62)),
    shirt,
  );
  torso.position.y = 2.15;
  group.add(torso);

  // Same tessellation as the face shell wrapped over it, so the two silhouettes
  // agree where the decal reaches around towards the ears.
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.45, 32, 24), skin);
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
      32,
      24,
      AVATAR_FACE_PHI_START,
      AVATAR_FACE_PHI_LENGTH,
      AVATAR_FACE_THETA_START,
      AVATAR_FACE_THETA_LENGTH,
    ),
    // The canvas is flooded opaque, so the decal renders in the solid pass and
    // never sorts against the head sphere it is hugging.
    new THREE.MeshBasicMaterial({}),
  );
  faceMesh.scale.y = 1.05;
  faceMesh.position.y = 3.36;
  faceMesh.rotation.y = Math.PI;
  group.add(faceMesh);

  const limbGeometry = rotateBoxTopUVs(new THREE.BoxGeometry(0.29, 1.25, 0.32));
  const leftArm = new THREE.Mesh(limbGeometry, shirt);
  leftArm.position.set(-0.73, 2.08, 0);
  group.add(leftArm);
  const rightArm = leftArm.clone();
  rightArm.position.x = 0.73;
  group.add(rightArm);

  // Mouse activity reads as a small antenna riding on the visitor's shoulder
  // that blinks solid green, faster the more their mouse is moving.
  const antenna = new THREE.Group();
  antenna.name = "mouse-activity-antenna";
  const antennaStalk = new THREE.Mesh(
    new THREE.CylinderGeometry(0.014, 0.018, 0.4, 10),
    new THREE.MeshBasicMaterial({ color: ANTENNA_STALK_COLOR }),
  );
  antennaStalk.position.y = 0.2;
  antenna.add(antennaStalk);
  const antennaBulb = new THREE.Mesh(
    new THREE.SphereGeometry(0.045, 14, 12),
    new THREE.MeshBasicMaterial({ color: ANTENNA_LIT_COLOR }),
  );
  antennaBulb.position.y = 0.42;
  antenna.add(antennaBulb);
  // Avatar fronts face -Z; perched on the right shoulder (top corner of the
  // torso, just inboard of the arm) and tipped outward away from the head.
  antenna.position.set(0.52, 2.86, 0.06);
  antenna.rotation.x = 0.1;
  antenna.rotation.z = -0.35;
  antenna.visible = identity.inputActive === true;
  group.add(antenna);

  // Account activity light: a small lamp pinned high on the chest whose
  // colour steps through ACTIVITY_LIGHT_COLORS as the account's coarse
  // recency bucket ages. syncAvatarActivity keeps it current and it stays
  // dark on anonymous guests.
  const activityLight = new THREE.Mesh(
    new THREE.SphereGeometry(0.055, 14, 12),
    new THREE.MeshBasicMaterial({ color: ACTIVITY_LIGHT_COLORS.hour }),
  );
  activityLight.name = "account-activity-light";
  // Avatar fronts face -Z; half-sunk into the torso above the badge corner.
  activityLight.position.set(-0.4, 2.82, -0.31);
  activityLight.visible = false;
  group.add(activityLight);

  // Each leg is a hip pivot carrying a thigh, and a knee pivot carrying the
  // shin plus that leg's shoe. Standing (every pitch at zero) the two segments
  // stack into the same block the single-box leg used to be, but the joints let
  // a sitter fold the thigh forward and keep the shin and foot under the knee
  // instead of swinging one rigid block — and shoes now travel with the leg
  // they belong to rather than staying planted on the plank.
  const legGeometry = new THREE.BoxGeometry(0.42, AVATAR_LEG_SEGMENT, 0.45);
  const shoeGeometry = new THREE.BoxGeometry(0.46, AVATAR_SHOE_HEIGHT, 0.7);
  const buildLeg = (side) => {
    const hip = new THREE.Group();
    hip.position.set(side * 0.3, AVATAR_HIP_Y, 0);
    const thigh = new THREE.Mesh(legGeometry, dark);
    thigh.position.y = -AVATAR_LEG_SEGMENT / 2;
    hip.add(thigh);
    const knee = new THREE.Group();
    knee.position.y = -AVATAR_LEG_SEGMENT;
    const shin = new THREE.Mesh(legGeometry, dark);
    shin.position.y = -AVATAR_LEG_SEGMENT / 2;
    knee.add(shin);
    const foot = new THREE.Mesh(shoeGeometry, shoe);
    foot.position.set(0, AVATAR_SHOE_Y - AVATAR_HIP_Y + AVATAR_LEG_SEGMENT, -0.09);
    knee.add(foot);
    hip.add(knee);
    group.add(hip);
    return { hip, knee };
  };
  const leftLegRig = buildLeg(-1);
  const rightLegRig = buildLeg(1);
  const leftLeg = leftLegRig.hip;
  const rightLeg = rightLegRig.hip;

  const badge = new THREE.Mesh(
    new THREE.PlaneGeometry(0.76, 0.76),
    new THREE.MeshBasicMaterial({
      map: badgeTexture(THREE, identity, remote ? "#77d9ff" : "#9ef7c6"),
      transparent: false,
    }),
  );
  badge.scale.set(1, 1.14, 1);
  badge.position.set(0, 2.32, -0.316);
  badge.rotation.y = Math.PI;
  badge.userData.chestBadge = true;
  group.add(badge);

  const chestTabs = createAvatarChestTabs(THREE);
  group.add(chestTabs);

  // Wallet chip: a small QR of the account's published Solana address worn
  // on the lower chest under the tabs. Hidden until the identity carries an
  // address; syncAvatarWallet paints the QR, balance, and recency ring.
  const walletChip = new THREE.Mesh(
    new THREE.PlaneGeometry(0.28, 0.28),
    new THREE.MeshBasicMaterial({ transparent: true }),
  );
  walletChip.name = "wallet-chip";
  // Slightly proud of the torso (and any operator belt) so nothing occludes
  // the QR; avatar fronts face -Z like the badge above it.
  walletChip.position.set(0, 1.54, -0.345);
  walletChip.rotation.y = Math.PI;
  walletChip.renderOrder = 3;
  walletChip.visible = false;
  group.add(walletChip);

  group.scale.setScalar(scale);
  group.userData = {
    id: identity.id,
    name: identity.name,
    leftArm,
    rightArm,
    leftLeg,
    rightLeg,
    leftKnee: leftLegRig.knee,
    rightKnee: rightLegRig.knee,
    badge,
    badgeIdentity: identity,
    badgeRemote: remote,
    chestTabs,
    chestTab: "info",
    chestTabRendered: "info",
    fediverseProfile: null,
    shirt,
    shirtMeshes: [torso, leftArm, rightArm],
    skin,
    antenna,
    antennaBulb,
    activityLight,
    activityBucket: "",
    walletChip,
    walletKey: "",
    inputEnergy: 0,
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
  syncAvatarActivity(group, identity);
  syncAvatarWallet(THREE, group, identity);
  setShadows(group, true, true);
  return group;
}

function avatarSecurityBadgeTexture(THREE, details = {}) {
  const ip = String(details.ip || "").trim().slice(0, 64);
  const userAgent = String(details.userAgent || "")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 256);
  const wrap = (text, width, rows) => {
    const words = String(text || "").split(" ").filter(Boolean);
    const lines = [];
    let line = "";
    words.forEach((word) => {
      const chunks = [];
      for (let at = 0; at < word.length; at += width) {
        chunks.push(word.slice(at, at + width));
      }
      chunks.forEach((chunk) => {
        const candidate = line ? `${line} ${chunk}` : chunk;
        if (candidate.length > width && line) {
          lines.push(line);
          line = chunk;
        } else {
          line = candidate;
        }
      });
    });
    if (line) lines.push(line);
    return lines.slice(0, rows);
  };
  return canvasTexture(THREE, 768, 960, (context) => {
    context.fillStyle = "#071714";
    context.fillRect(0, 0, 768, 960);
    context.strokeStyle = "#9ef7c6";
    context.lineWidth = 16;
    context.strokeRect(10, 10, 748, 940);
    context.textAlign = "left";
    context.textBaseline = "middle";
    context.fillStyle = "#9ef7c6";
    context.font = '900 58px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("YOUR SESSION", 44, 78);
    context.fillStyle = "#7fb9a5";
    context.font = '800 34px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("PRIVATE · SELF ONLY", 44, 132);

    context.fillStyle = "#f7c96b";
    context.font = '900 38px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("EDGE IP", 44, 214);
    context.fillStyle = "#ffffff";
    context.font = '700 36px "ForkMesh Mono", ui-monospace, monospace';
    wrap(ip || "UNAVAILABLE", 30, 3).forEach((line, index) => {
      context.fillText(line, 44, 272 + index * 48);
    });

    context.fillStyle = "#77d9ff";
    context.font = '900 38px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("USER AGENT", 44, 432);
    context.fillStyle = "#e9fff6";
    context.font = '650 31px "ForkMesh Mono", ui-monospace, monospace';
    wrap(userAgent || "UNAVAILABLE", 39, 8).forEach((line, index) => {
      context.fillText(line, 44, 486 + index * 43);
    });

    context.fillStyle = "#7fb9a5";
    context.font = '700 25px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("HIDDEN FROM PEERS + SCREENSHOTS", 44, 912);
  });
}

function setAvatarSecurityBadge(THREE, avatar, details, visible = true) {
  if (!avatar?.userData) return null;
  const ip = String(details?.ip || "").trim().slice(0, 64);
  const userAgent = String(details?.userAgent || "")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 256);
  let badge = avatar.userData.selfSecurityBadge;
  if (!ip && !userAgent) {
    if (badge) badge.visible = false;
    return badge || null;
  }
  if (!badge) {
    badge = new THREE.Mesh(
      new THREE.PlaneGeometry(1.06, 1.32),
      new THREE.MeshBasicMaterial({ toneMapped: false }),
    );
    badge.name = "forkmesh-self-security-back-badge";
    // Avatar fronts face -Z, so the owner-only security card sits on +Z.
    badge.position.set(0, 2.22, 0.318);
    badge.renderOrder = 4;
    avatar.add(badge);
    avatar.userData.selfSecurityBadge = badge;
  }
  const previous = badge.material.map;
  badge.material.map = avatarSecurityBadgeTexture(THREE, {
    ip,
    userAgent,
  });
  badge.material.needsUpdate = true;
  badge.visible = visible === true;
  previous?.dispose?.();
  return badge;
}

function applyOutfit(THREE, shirt, identity) {
  // Every public field the tailor reads is folded into one key so a presence
  // frame that changes none of them never redraws the cloth.
  const key = [
    identity.name,
    identity.countryCode,
    identity.flag,
    identity.outfitColor,
    identity.outfitStyle,
  ]
    .map((value) => String(value || ""))
    .join("\u0000");
  if (shirt.userData.outfitKey === key && shirt.map) return;
  shirt.userData.outfitKey = key;
  const previous = shirt.map;
  shirt.map = countryShirtTexture(THREE, identity);
  shirt.color.set("#ffffff");
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
  if (avatar.userData.antenna) avatar.userData.antenna.visible = active;
  const light = avatar.userData.activityLight;
  if (light) {
    const bucket = ACTIVITY_LIGHT_COLORS[identity.activityBucket]
      ? String(identity.activityBucket)
      : "";
    avatar.userData.activityBucket = bucket;
    // The light reads account recency, so it stays dark on anonymous guests.
    light.visible =
      Boolean(bucket) && avatar.userData.accountStatus !== "Guest";
    if (light.visible) {
      // The "hour" breath overrides colour and scale every frame; older
      // buckets hold their steady step here.
      light.material.color.set(ACTIVITY_LIGHT_COLORS[bucket]);
      light.scale.setScalar(1);
    }
  }
}

function renderAvatarBadge(THREE, avatar, remote = false) {
  const badge = avatar?.userData?.badge;
  if (!badge?.material) return;
  const identity = avatar.userData.badgeIdentity || {};
  const accent = remote ? "#77d9ff" : "#9ef7c6";
  const old = badge.material.map;
  badge.material.map =
    avatar.userData.chestTab === "fediverse"
      ? fediverseBadgeTexture(
          THREE,
          identity,
          accent,
          avatar.userData.fediverseProfile || { state: "loading" },
        )
      : badgeTexture(THREE, identity, accent);
  badge.material.needsUpdate = true;
  old?.dispose?.();
  syncChestTabs(THREE, avatar);
}

function updateAvatarBadge(THREE, avatar, identity, remote = false) {
  if (!avatar?.userData?.badge?.material) return;
  avatar.userData.badgeIdentity = identity;
  avatar.userData.badgeRemote = remote === true;
  renderAvatarBadge(THREE, avatar, remote);
  avatar.userData.name = identity.name;
  syncCountryShirt(THREE, avatar, identity);
  // Taking the perk off (or losing Supporting status) reverts to the emoji
  // face immediately; putting it on is driven by the app layer, which owns
  // the account-lookup fetch.
  if (identity.faceImage !== true && avatar.userData.faceImageUrl) {
    applyAvatarFaceImage(THREE, avatar, "");
  }
  syncAvatarActivity(avatar, identity);
  syncAvatarWallet(THREE, avatar, identity);
  syncAvatarStatus(THREE, avatar, identity);
}

function animateAvatarActivity(avatar, time, delta, reducedMotion) {
  if (!avatar?.userData) return;
  const antenna = avatar.userData.antenna;
  const bulb = avatar.userData.antennaBulb;
  if (antenna?.visible && bulb) {
    // A square blink rather than a pulse, so it reads as an unambiguous
    // on/off beacon. Idle mouse activity ticks slowly; a mouse that is
    // really moving drives it up to a few blinks a second.
    const energy = Math.max(0, Math.min(1, avatar.userData.inputEnergy || 0));
    const hz = ANTENNA_BLINK_MIN_HZ +
      energy * (ANTENNA_BLINK_MAX_HZ - ANTENNA_BLINK_MIN_HZ);
    const cycle = (time * 0.001 * hz + avatar.userData.phase) % 1;
    const lit = reducedMotion || cycle < 0.5;
    bulb.material.color.set(lit ? ANTENNA_LIT_COLOR : ANTENNA_DARK_COLOR);
  }
  const light = avatar.userData.activityLight;
  if (light?.visible && avatar.userData.activityBucket === "hour") {
    // Accounts active within the hour breathe: a slow sine swell rather than
    // the antenna's hard blink, easing between dimmed and full green.
    const breath = reducedMotion
      ? 1
      : 0.5 +
        0.5 *
          Math.sin(
            time * 0.001 * ACTIVITY_LIGHT_BREATH_HZ * Math.PI * 2 +
              avatar.userData.phase,
          );
    light.material.color
      .set(ACTIVITY_LIGHT_COLORS.hour)
      .multiplyScalar(0.55 + 0.45 * breath);
    light.scale.setScalar(0.92 + 0.16 * breath);
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
      (antenna && antenna === child.parent) ||
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

// The two ways a node answers for the repository — a git clone and a served
// repository web request — counted together, so either kind of visit reads as
// one "this cabinet just served somebody" event. Null when the node reports
// neither counter; unreported values are never estimated.
function mirrorServedTotal(node) {
  const clones = mirrorMetric(node?.clonesServed, 1_000_000_000);
  const website = mirrorMetric(node?.websiteServed, 1_000_000_000);
  if (clones === null && website === null) return null;
  return (clones || 0) + (website || 0);
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
    machineName: node?.machineName,
    online: mirrorNodeIsOnline(node),
    healthy: node?.healthy,
    integrity: node?.integrity,
    activity: node?.activity,
    activityUpdatedAt: node?.activityUpdatedAt,
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
  const activity = String(node?.activity || "unknown")
    .replace(/[^a-z0-9-]/gi, "")
    .replace(/-/g, " ")
    .toUpperCase()
    .slice(0, 24);
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
    context.fillText(
      String(node?.machineName || node?.name || "MIRROR")
        .toUpperCase()
        .slice(0, 24),
      58,
      70,
    );
    context.font = '700 25px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = statusColor;
    context.fillText(
      `${online ? "ONLINE" : "OFFLINE"} · ${integrity.toUpperCase()} · ${routeLabel}`,
      58,
      124,
    );
    context.font = '600 21px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#8ca99a";
    context.textAlign = "right";
    context.fillText(activity || `SYNCED ${mirrorCommitAgeLabel(node?.syncAgeMs)}`, 966, 124);
    context.textAlign = "left";

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

// A pocket-sized stand-in for the agent a node just answered: the little
// figure that shoots up out of a cabinet when it serves a clone or a
// repository page. Unlit and self-owning — every launch builds its own
// materials so it can fade out and dispose without touching shared palettes.
function createServedVisitorFigure(THREE, accent = "#9ef7c6") {
  const group = new THREE.Group();
  group.name = "served-visitor";
  const skin = new THREE.MeshBasicMaterial({
    color: AVATAR_EMOJI_SKIN_COLOR,
    transparent: true,
    toneMapped: false,
    depthWrite: false,
  });
  const suit = new THREE.MeshBasicMaterial({
    color: accent,
    transparent: true,
    toneMapped: false,
    depthWrite: false,
  });
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.19, 12, 10), skin);
  head.position.y = 1.16;
  group.add(head);
  const torso = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.5, 0.22), suit);
  torso.position.y = 0.7;
  group.add(torso);
  // Arms swept overhead and legs trailing straight below: the silhouette still
  // reads as a launched visitor at the size it shrinks to high in the sky.
  for (const side of [-1, 1]) {
    const arm = new THREE.Mesh(new THREE.BoxGeometry(0.1, 0.44, 0.1), suit);
    arm.position.set(side * 0.25, 0.86, 0);
    arm.rotation.z = side * 0.5;
    group.add(arm);
    const leg = new THREE.Mesh(new THREE.BoxGeometry(0.11, 0.44, 0.11), suit);
    leg.position.set(side * 0.1, 0.22, 0);
    group.add(leg);
  }
  group.userData.figureMaterials = [skin, suit];
  return group;
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
  const activity = String(node?.activity || "unknown").toLowerCase();
  const statusColor =
    integrity === "rejected" || integrity === "degraded"
      ? "#ff0000"
      : integrity === "healing" || activity === "syncing" ||
          activity === "awaiting-verification" ||
          (online && node?.cloneAvailable !== true)
        ? "#ffcc00"
        : online
          ? "#00cc44"
          : "#71837a";
  // Yellow and red are the two statuses that want attention, so their lamps
  // sweep like a rotating warning beacon; green and offline stay steady.
  const alerting = statusColor === "#ff0000" || statusColor === "#ffcc00";
  // A single beacon lamp sits on the cabinet roof; its color is the status.
  // The lens is an unlit cylinder so the status reads as one flat, solid
  // colour from every camera angle instead of shading into a gradient. The
  // top is left open — the world camera looks down on the yard, so a metal
  // cap would hide the one part of the lamp that carries the status.
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
    new THREE.CylinderGeometry(0.15, 0.15, 0.3, 20),
    new THREE.MeshBasicMaterial({ color: statusColor, toneMapped: false }),
  );
  beaconLens.position.y = 0.23;
  statusLight.add(beaconLens);
  // Only a thin collar rings the open mouth so the lens still reads as a
  // fixture rather than a bare peg.
  const beaconCollar = new THREE.Mesh(
    new THREE.TorusGeometry(0.152, 0.016, 8, 20),
    makeMaterial(THREE, "#35463e", {
      metalness: 0.82,
      roughness: 0.3,
    }),
  );
  beaconCollar.rotation.x = Math.PI / 2;
  beaconCollar.position.y = 0.378;
  statusLight.add(beaconCollar);
  if (alerting) {
    // Two opposed additive lobes hugging the lens: rotating the group reads as
    // a sweeping light without an actual light source or a per-frame material
    // rebuild, both of which are too expensive for a yard of 64 cabinets.
    const beaconSweep = new THREE.Group();
    const sweepMaterial = new THREE.MeshBasicMaterial({
      color: statusColor,
      toneMapped: false,
      transparent: true,
      opacity: 0.55,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
      side: THREE.DoubleSide,
    });
    for (const thetaStart of [0, Math.PI]) {
      const lobe = new THREE.Mesh(
        new THREE.CylinderGeometry(
          0.24,
          0.24,
          0.26,
          10,
          1,
          true,
          thetaStart,
          Math.PI / 3,
        ),
        sweepMaterial,
      );
      beaconSweep.add(lobe);
    }
    beaconSweep.position.y = 0.23;
    statusLight.add(beaconSweep);
    group.userData.beaconSweep = beaconSweep;
  }
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
  // The sweep is a glow, not geometry: shadow-casting it would paint a turning
  // dark band across the cabinet roof.
  if (group.userData.beaconSweep) {
    setShadows(group.userData.beaconSweep, false, false);
  }
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
    "durable objects + database rows",
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

function formatCapacityBytes(bytes) {
  const value = Number(bytes);
  if (!Number.isFinite(value) || value <= 0) return "0 B";
  const units = ["B", "KB", "MB", "GB", "TB", "PB"];
  const step = Math.min(
    units.length - 1,
    Math.floor(Math.log(value) / Math.log(1024)),
  );
  const scaled = value / 1024 ** step;
  const shown =
    scaled >= 100 || step === 0 ? Math.round(scaled) : scaled.toFixed(1);
  return `${shown} ${units[step]}`;
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

// The chest wallet chip: the account's published Solana address as a QR,
// ringed with the transaction-recency colour and captioned with the public
// balance the app layer fetched.
function walletChipTexture(THREE, wallet) {
  const ring =
    ACTIVITY_LIGHT_COLORS[wallet.txBucket] || ACTIVITY_LIGHT_COLORS.stale;
  return canvasTexture(THREE, 256, 256, (context) => {
    context.clearRect(0, 0, 256, 256);
    roundedRect(context, 6, 6, 244, 244, 24);
    context.fillStyle = "rgba(7, 18, 14, 0.94)";
    context.fill();
    // The ring wears the same colour ladder as the chest activity light,
    // keyed to how recently the wallet last saw a transaction.
    roundedRect(context, 40, 12, 176, 176, 14);
    context.strokeStyle = ring;
    context.lineWidth = 10;
    context.stroke();
    const drawn = drawQrModules(context, wallet.address, 53, 25, 150);
    if (!drawn) {
      context.fillStyle = "rgba(158, 247, 198, 0.14)";
      context.fillRect(53, 25, 150, 150);
    }
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '700 30px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#f7c96b";
    const sol = Number(wallet.sol);
    context.fillText(
      Number.isFinite(sol)
        ? `◎ ${sol.toLocaleString("en-US", { maximumFractionDigits: 4 })}`
        : "◎ …",
      128,
      222,
    );
  });
}

function syncAvatarWallet(THREE, avatar, identity) {
  const chip = avatar?.userData?.walletChip;
  if (!chip?.material) return;
  const address = AVATAR_SOLANA_ADDRESS_RE.test(String(identity?.solana || ""))
    ? String(identity.solana)
    : "";
  const wallet = {
    address,
    sol: Number.isFinite(Number(identity?.walletSol))
      ? Number(identity.walletSol)
      : null,
    txBucket: ACTIVITY_LIGHT_COLORS[identity?.walletTxBucket]
      ? String(identity.walletTxBucket)
      : "",
  };
  const key = `${wallet.address} ${wallet.sol} ${wallet.txBucket}`;
  if (avatar.userData.walletKey === key) return;
  avatar.userData.walletKey = key;
  chip.visible = Boolean(wallet.address);
  if (!wallet.address) return;
  const old = chip.material.map;
  chip.material.map = walletChipTexture(THREE, wallet);
  chip.material.needsUpdate = true;
  old?.dispose?.();
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

  // A physical import kiosk remains at the repository district even though
  // the old central repository portal is retired. Each provider pad opens the
  // same secure import flow with that provider preselected.
  const importKiosk = new THREE.Group();
  importKiosk.name = "repository-import-kiosk";
  importKiosk.position.set(0, 0, 5.2);
  const kioskBase = new THREE.Mesh(
    new THREE.CylinderGeometry(2.15, 2.35, 0.42, 8),
    makeMaterial(THREE, "#102a32", {
      emissive: "#123d49",
      emissiveIntensity: 0.35,
      metalness: 0.25,
      roughness: 0.52,
    }),
  );
  kioskBase.position.y = 0.21;
  importKiosk.add(kioskBase);
  const kioskColumn = new THREE.Mesh(
    new THREE.CylinderGeometry(0.24, 0.34, 2.15, 10),
    frameMaterial,
  );
  kioskColumn.position.y = 1.45;
  importKiosk.add(kioskColumn);
  const providerPads = [
    { id: "github", label: "GITHUB", color: "#f0f6fc" },
    { id: "gitlab", label: "GITLAB", color: "#fc8d45" },
    { id: "codeberg", label: "CODEBERG", color: "#77d9ff" },
  ];
  providerPads.forEach((provider, index) => {
    const angle = -0.72 + index * 0.72;
    const pad = new THREE.Mesh(
      new THREE.BoxGeometry(1.25, 0.2, 0.88),
      makeMaterial(THREE, provider.color, {
        emissive: provider.color,
        emissiveIntensity: 0.72,
        metalness: 0.22,
        roughness: 0.34,
      }),
    );
    pad.name = `repository-import-provider:${provider.id}`;
    pad.position.set(Math.sin(angle) * 1.45, 0.58, -Math.cos(angle) * 1.45);
    pad.rotation.y = -angle;
    pad.userData.landmark = "repositories";
    pad.userData.createRepository = provider.id;
    importKiosk.add(pad);
    const label = makeLabelSprite(
      THREE,
      provider.label,
      "IMPORT FROM",
      provider.color,
    );
    label.scale.set(1.18, 0.4, 1);
    label.position.copy(pad.position);
    label.position.y += 0.42;
    importKiosk.add(label);
  });
  const importBeam = new THREE.Mesh(
    new THREE.CylinderGeometry(0.08, 0.68, 3.5, 18, 1, true),
    makeMaterial(THREE, "#77d9ff", {
      emissive: "#39bfe8",
      emissiveIntensity: 1.1,
      transparent: true,
      opacity: 0.14,
      depthWrite: false,
      side: THREE.DoubleSide,
    }),
  );
  importBeam.name = "repository-import-beam";
  importBeam.position.y = 2.7;
  importBeam.visible = false;
  importKiosk.add(importBeam);
  const importSpark = new THREE.Mesh(
    new THREE.IcosahedronGeometry(0.28, 1),
    makeMaterial(THREE, "#9ef7c6", {
      emissive: "#42e99a",
      emissiveIntensity: 1.8,
      transparent: true,
      opacity: 0.92,
    }),
  );
  importSpark.name = "repository-import-spark";
  importSpark.position.y = 3.55;
  importSpark.visible = false;
  importKiosk.add(importSpark);
  const kioskSign = makeLabelSprite(
    THREE,
    "IMPORT REPOSITORY",
    "GITHUB · GITLAB · CODEBERG",
    "#9ef7c6",
  );
  kioskSign.scale.set(2.8, 0.78, 1);
  kioskSign.position.set(0, 4.35, 0);
  importKiosk.add(kioskSign);
  group.add(importKiosk);
  group.userData.repositoryImportKiosk = importKiosk;
  importKiosk.userData.importBeam = importBeam;
  importKiosk.userData.importSpark = importSpark;
  importKiosk.userData.importing = false;
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
    if (importKiosk.userData.importing) {
      importBeam.visible = true;
      importSpark.visible = true;
      importBeam.rotation.y = time * 0.0018;
      importBeam.material.opacity = 0.11 + Math.sin(time * 0.006) * 0.045;
      importSpark.rotation.y = time * 0.003;
      importSpark.rotation.x = time * 0.0017;
      importSpark.position.y = 3.45 + Math.sin(time * 0.004) * 0.28;
    } else {
      importBeam.visible = false;
      importSpark.visible = false;
    }
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

// The board is a normal standing banner now, not a tower: five entries per
// page keep it dense while every line stays readable at banner scale.
const WORLD_BULLETIN_VISIBLE_EVENTS = 5;
const WORLD_BULLETIN_WIDTH = 1536;
const WORLD_BULLETIN_HEIGHT = 1024;

function worldBulletinTexture(THREE, events = [], offset = 0) {
  const allEntries = (Array.isArray(events) ? events : [])
    .filter((event) => event && String(event.title || "").trim())
    // Newest alerts always take priority at the top of the board.
    .sort((left, right) => Date.parse(right.startsAt || 0) - Date.parse(left.startsAt || 0));
  const start = clamp(
    Number(offset) || 0,
    0,
    Math.max(0, allEntries.length - WORLD_BULLETIN_VISIBLE_EVENTS),
  );
  const entries = allEntries.slice(start, start + WORLD_BULLETIN_VISIBLE_EVENTS);
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
  // The right-hand column is left clear for the ▲ / ▼ scroll controls, so no
  // line is allowed to run under them.
  const textWidth = 1276;
  return canvasTexture(THREE, WORLD_BULLETIN_WIDTH, WORLD_BULLETIN_HEIGHT, (context) => {
    context.fillStyle = "#0b1820";
    context.fillRect(0, 0, WORLD_BULLETIN_WIDTH, WORLD_BULLETIN_HEIGHT);
    context.strokeStyle = "#7ed9ff";
    context.lineWidth = 10;
    context.strokeRect(8, 8, WORLD_BULLETIN_WIDTH - 16, WORLD_BULLETIN_HEIGHT - 16);
    context.fillStyle = "#e5f8ff";
    context.font = '800 54px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("WORLD BULLETIN", 42, 72);
    context.fillStyle = "#8eddf7";
    context.font = '700 22px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("PUBLIC ALERTS · LIVE COMMUNITY EVENTS · NEWEST FIRST", 44, 108);
    context.strokeStyle = "rgba(126,217,255,0.42)";
    context.lineWidth = 2;
    context.beginPath();
    context.moveTo(44, 128);
    context.lineTo(1492, 128);
    context.stroke();
    if (!entries.length) {
      context.fillStyle = "#c3dbe3";
      context.font = '700 34px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO ACTIVE PUBLIC ALERTS", 44, 210);
      context.font = '600 24px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("THE COMMUNITY SCHEDULE WILL APPEAR HERE.", 44, 256);
      return;
    }
    entries.forEach((event, index) => {
      const y = 168 + index * 168;
      context.fillStyle = "#f7d58a";
      context.font = '800 28px "ForkMesh Mono", ui-monospace, monospace';
      wrapText(context, `${start + index + 1}. ${event.title}`, 44, y, textWidth, 32, 1);
      context.fillStyle = "#bad0d8";
      context.font = '700 20px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        `${event.type || "Community"} · ${event.destination || "Town Square"}`,
        60,
        y + 30,
      );
      context.fillStyle = "#8eddf7";
      context.font = '600 19px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        `START ${formatTime(event.startsAt)} · END ${formatTime(event.endsAt)}`,
        60,
        y + 55,
      );
      context.fillStyle = "#d5e6e9";
      // Event descriptions are sanitized to 500 characters upstream; three
      // wrapped lines carry the opening of each one without pushing the five
      // slots off the board.
      context.font = '600 17px "ForkMesh Mono", ui-monospace, monospace';
      wrapText(context, event.description || "Community event", 60, y + 80, textWidth - 16, 21, 3);
      if (index < entries.length - 1) {
        context.strokeStyle = "rgba(126,217,255,0.28)";
        context.lineWidth = 2;
        context.beginPath();
        context.moveTo(44, y + 140);
        context.lineTo(1492, y + 140);
        context.stroke();
      }
    });
    context.fillStyle = "#8eddf7";
    context.font = '700 20px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      `SHOWING ${start + 1}-${Math.min(start + WORLD_BULLETIN_VISIBLE_EVENTS, allEntries.length)} OF ${allEntries.length} · ▲ / ▼ OR SCROLL OVER THIS BOARD`,
      44,
      998,
    );
  });
}

// Three posts at a time on a board twice as tall as the old one, so each card
// holds the full text, a deep image strip, and the post's engagement counts.
// The board is repainted from the same snapshot the mini-app renders.
const MASTODON_KIOSK_VISIBLE_TOOTS = 3;
const MASTODON_KIOSK_VISIBLE_REPLIES = 5;
// Vertical pitch of one toot card, and the top of the first card. The extra
// height over the old 470 all goes to the attachment strip.
const MASTODON_KIOSK_TOOT_PITCH = 720;
const MASTODON_KIOSK_TOOT_TOP = 1390;
const MASTODON_KIOSK_WIDTH = 1536;
const MASTODON_KIOSK_HEIGHT = 4096;
const MASTODON_KIOSK_REFRESH_MS = 10 * 60 * 1000;

// Shared by the board texture and the "open in a new tab" buttons so both
// agree on which two toots are on screen for a given scroll offset.
function mastodonKioskVisibleToots(snapshot, offset) {
  const toots = Array.isArray(snapshot?.toots) ? snapshot.toots : [];
  const start = clamp(
    Number(offset) || 0,
    0,
    Math.max(0, toots.length - MASTODON_KIOSK_VISIBLE_TOOTS),
  );
  return toots.slice(start, start + MASTODON_KIOSK_VISIBLE_TOOTS);
}

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

// One-line fit for the reply rows: replies get a single line each, so long
// bodies are cut at the card width with an ellipsis rather than wrapped away.
function clipCanvasText(context, text, maxWidth) {
  const flat = String(text || "").replace(/\s+/g, " ").trim();
  if (!flat || context.measureText(flat).width <= maxWidth) return flat;
  let cut = flat;
  while (cut.length > 1 && context.measureText(`${cut}…`).width > maxWidth) {
    cut = cut.slice(0, -1);
  }
  return `${cut.trimEnd()}…`;
}

function mastodonKioskTexture(THREE, snapshot = null, offset = 0, resolveImage = null) {
  const WIDTH = MASTODON_KIOSK_WIDTH;
  const HEIGHT = MASTODON_KIOSK_HEIGHT;
  return canvasTexture(THREE, WIDTH, HEIGHT, (context) => {
    context.fillStyle = "#191a2e";
    context.fillRect(0, 0, WIDTH, HEIGHT);
    if (!snapshot) {
      // No live profile yet (still fetching, or mastodon.social unreachable):
      // fall back to the static kiosk sign describing the board.
      context.fillStyle = "#6364ff";
      context.fillRect(12, 12, 1512, 260);
      context.fillStyle = "#f2f3ff";
      context.font = '800 150px "ForkMesh Favorit", sans-serif';
      context.fillText("MASTODON", 96, 200);
      context.fillStyle = "#c8c9ff";
      context.font = '700 72px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("@forkmesh", 96, 470);
      context.font = '600 56px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("@mastodon.social", 96, 562);
      context.strokeStyle = "rgba(99,100,255,0.5)";
      context.lineWidth = 4;
      context.beginPath();
      context.moveTo(96, 656);
      context.lineTo(1440, 656);
      context.stroke();
      context.fillStyle = "#e8e9ff";
      context.font = '700 60px "ForkMesh Mono", ui-monospace, monospace';
      [
        "LIVE PUBLIC PROFILE",
        "FOLLOWERS · FOLLOWING · POSTS",
        "BIO AND VERIFIED PROFILE LINKS",
        "FULL POSTS WITH IMAGES",
        "STARS · BOOSTS · REPLY COUNTS",
        "REPLIES WITH AUTHOR ICONS",
        "REFRESHED EVERY 10 MINUTES",
      ].forEach((line, index) => {
        context.fillText(line, 96, 810 + index * 136);
      });
      context.fillStyle = "#8b9bf4";
      context.font = '800 68px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("TAP / CLICK TO OPEN", 96, 3860);
      context.fillStyle = "#7a7ca8";
      context.font = '600 44px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("READ-ONLY · FETCHED FROM MASTODON.SOCIAL", 96, 4012);
      context.strokeStyle = "#6364ff";
      context.lineWidth = 16;
      context.strokeRect(12, 12, 1512, 4072);
      return;
    }
    const image = (url) =>
      typeof resolveImage === "function" ? resolveImage(url) : null;
    // Header banner, cover-cropped like the profile page. Text over the band
    // sits on a darkening gradient so it stays readable on any artwork.
    const header = image(snapshot.headerURL);
    context.save();
    context.beginPath();
    context.rect(16, 16, 1504, 560);
    context.clip();
    if (header?.naturalWidth > 0 && header?.naturalHeight > 0) {
      const scale = Math.max(
        1504 / header.naturalWidth,
        560 / header.naturalHeight,
      );
      const width = header.naturalWidth * scale;
      const height = header.naturalHeight * scale;
      context.drawImage(
        header,
        16 + (1504 - width) / 2,
        16 + (560 - height) / 2,
        width,
        height,
      );
    } else {
      context.fillStyle = "#43389c";
      context.fillRect(16, 16, 1504, 560);
    }
    const shade = context.createLinearGradient(0, 300, 0, 576);
    shade.addColorStop(0, "rgba(15,16,36,0)");
    shade.addColorStop(1, "rgba(15,16,36,0.9)");
    context.fillStyle = shade;
    context.fillRect(16, 16, 1504, 560);
    context.restore();
    // Avatar overlapping the banner edge, then identity beside it.
    const avatar = image(snapshot.avatarURL);
    context.fillStyle = "#191a2e";
    roundedRect(context, 40, 476, 208, 208, 44);
    context.fill();
    context.save();
    roundedRect(context, 52, 488, 184, 184, 36);
    context.clip();
    if (avatar?.naturalWidth > 0) {
      context.drawImage(avatar, 52, 488, 184, 184);
    } else {
      context.fillStyle = "#43389c";
      context.fillRect(52, 488, 184, 184);
      context.fillStyle = "#c8c9ff";
      context.font = '800 112px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("@", 100, 618);
    }
    context.restore();
    context.fillStyle = "#f2f3ff";
    context.font = '800 64px "ForkMesh Favorit", sans-serif';
    context.fillText(String(snapshot.displayName || "ForkMesh"), 280, 652);
    context.fillStyle = "#c8c9ff";
    context.font = '600 38px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(String(snapshot.acct || "@forkmesh@mastodon.social"), 280, 710);
    [
      ["FOLLOWERS", snapshot.followers],
      ["FOLLOWING", snapshot.following],
      ["POSTS", snapshot.posts],
      ["JOINED", snapshot.joined],
    ].forEach(([label, value], index) => {
      const x = 56 + index * 366;
      context.fillStyle = "#8b8db8";
      context.font = '700 28px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(label, x, 790);
      context.fillStyle = "#f2f3ff";
      context.font = '800 58px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(String(value ?? "—"), x, 858);
    });
    // Bio, then the profile's own link fields with their verification ticks —
    // the same block the mini-app prints under the stats.
    context.fillStyle = "#c8c9ff";
    context.font = '600 34px "ForkMesh Mono", ui-monospace, monospace';
    wrapCanvasText(context, snapshot.note, 56, 930, 1424, 46, 4);
    const fields = (Array.isArray(snapshot.fields) ? snapshot.fields : [])
      .filter(Boolean)
      .slice(0, 4);
    fields.forEach((field, index) => {
      const y = 1120 + index * 52;
      context.fillStyle = "#8b8db8";
      context.font = '700 28px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        clipCanvasText(context, String(field.name || "").toUpperCase(), 320),
        56,
        y,
      );
      context.fillStyle = field.verified ? "#9ef7c6" : "#e8e9ff";
      context.font = '600 30px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        clipCanvasText(
          context,
          `${field.value || ""}${field.verified ? " ✓" : ""}`,
          1024,
        ),
        420,
        y,
      );
    });
    context.strokeStyle = "rgba(99,100,255,0.5)";
    context.lineWidth = 4;
    context.beginPath();
    context.moveTo(56, 1290);
    context.lineTo(1480, 1290);
    context.stroke();
    context.fillStyle = "#8b9bf4";
    context.font = '800 38px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("LATEST TOOTS", 56, 1340);
    const toots = Array.isArray(snapshot.toots) ? snapshot.toots : [];
    const start = clamp(
      Number(offset) || 0,
      0,
      Math.max(0, toots.length - MASTODON_KIOSK_VISIBLE_TOOTS),
    );
    if (toots.length > MASTODON_KIOSK_VISIBLE_TOOTS) {
      context.textAlign = "right";
      context.fillStyle = "#7a7ca8";
      context.font = '700 30px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        `${start + 1}–${Math.min(
          start + MASTODON_KIOSK_VISIBLE_TOOTS,
          toots.length,
        )} / ${toots.length} · SCROLL ▲▼`,
        1480,
        1340,
      );
      context.textAlign = "left";
    }
    const entries = mastodonKioskVisibleToots(snapshot, offset);
    if (!entries.length) {
      context.fillStyle = "#c8c9ff";
      context.font = '600 40px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO PUBLIC TOOTS YET", 56, 1450);
    }
    entries.forEach((toot, index) => {
      const top = MASTODON_KIOSK_TOOT_TOP + index * MASTODON_KIOSK_TOOT_PITCH;
      const images = (Array.isArray(toot.images) ? toot.images : [])
        .map((url) => String(url || ""))
        .filter(Boolean)
        .slice(0, 4);
      context.fillStyle = "#c8c9ff";
      context.font = '700 32px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        clipCanvasText(
          context,
          [toot.author, toot.acct, toot.date].filter(Boolean).join(" · "),
          1424,
        ),
        56,
        top + 36,
      );
      // Pinned / boosted / content-warning markers get their own highlighted
      // line above the body instead of being folded into the text.
      const marker = String(toot.marker || "").trim();
      if (marker) {
        context.fillStyle = "#ffd257";
        context.font = '700 30px "ForkMesh Mono", ui-monospace, monospace';
        context.fillText(clipCanvasText(context, marker, 1424), 56, top + 82);
      }
      context.fillStyle = "#e8e9ff";
      context.font = '600 34px "ForkMesh Mono", ui-monospace, monospace';
      // The full post text runs until it hits the card's image strip; posts
      // longer than the card still end at a whole line rather than mid-word.
      wrapCanvasText(
        context,
        toot.text,
        56,
        marker ? top + 134 : top + 92,
        1424,
        46,
        images.length ? (marker ? 3 : 4) : marker ? 10 : 11,
      );
      if (images.length) {
        // Attachments below the text, cover-cropped into tiles that divide the
        // full card width. Tiles that have not loaded CORS-clean stay as empty
        // plates.
        const gap = 18;
        const height = 360;
        const width = (1424 - gap * (images.length - 1)) / images.length;
        const y = top + 296;
        images.forEach((url, position) => {
          const x = 56 + position * (width + gap);
          context.save();
          roundedRect(context, x, y, width, height, 18);
          context.clip();
          const media = image(url);
          if (media?.naturalWidth > 0 && media?.naturalHeight > 0) {
            const scale = Math.max(
              width / media.naturalWidth,
              height / media.naturalHeight,
            );
            context.drawImage(
              media,
              x + (width - media.naturalWidth * scale) / 2,
              y + (height - media.naturalHeight * scale) / 2,
              media.naturalWidth * scale,
              media.naturalHeight * scale,
            );
          } else {
            context.fillStyle = "#232445";
            context.fillRect(x, y, width, height);
            context.fillStyle = "#7a7ca8";
            context.font = '700 26px "ForkMesh Mono", ui-monospace, monospace';
            context.fillText("IMAGE", x + 24, y + height / 2 + 10);
          }
          context.restore();
        });
      }
      // Engagement counts sit on the card's bottom rule: stars, boosts, and
      // how many people replied to that post.
      context.fillStyle = "#8b9bf4";
      context.font = '700 30px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        [
          `⭐ ${toot.stars ?? 0} STARS`,
          `🔁 ${toot.boosts ?? 0} BOOSTS`,
          `💬 ${toot.replies ?? 0} REPLIES`,
        ].join("   ·   "),
        56,
        top + 692,
      );
      // Rule between the cards only: the last card runs straight into the
      // replies strip so the tall image tiles keep their clearance.
      if (index < entries.length - 1) {
        context.strokeStyle = "rgba(99,100,255,0.28)";
        context.lineWidth = 3;
        context.beginPath();
        context.moveTo(56, top + 706);
        context.lineTo(1480, top + 706);
        context.stroke();
      }
    });
    // Replies section: the newest public replies other accounts left on those
    // toots, each with the replier's own avatar so the board shows who is
    // talking back rather than just a reply count.
    const replies = (Array.isArray(snapshot.replies) ? snapshot.replies : [])
      .filter(Boolean)
      .slice(0, MASTODON_KIOSK_VISIBLE_REPLIES);
    context.fillStyle = "#8b9bf4";
    context.font = '800 38px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("REPLIES", 56, 3560);
    if (!replies.length) {
      context.fillStyle = "#7a7ca8";
      context.font = '600 32px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("NO PUBLIC REPLIES YET", 56, 3622);
    }
    replies.forEach((reply, index) => {
      const top = 3596 + index * 78;
      const icon = image(reply.avatar);
      context.save();
      roundedRect(context, 56, top, 60, 60, 16);
      context.clip();
      if (icon?.naturalWidth > 0) {
        context.drawImage(icon, 56, top, 60, 60);
      } else {
        context.fillStyle = "#43389c";
        context.fillRect(56, top, 60, 60);
        context.fillStyle = "#c8c9ff";
        context.font = '800 40px "ForkMesh Mono", ui-monospace, monospace';
        context.fillText("@", 68, top + 46);
      }
      context.restore();
      context.fillStyle = "#c8c9ff";
      context.font = '700 26px "ForkMesh Mono", ui-monospace, monospace';
      const who = [reply.author, reply.acct, reply.date]
        .map((part) => String(part || "").trim())
        .filter(Boolean)
        .join(" · ");
      context.fillText(clipCanvasText(context, who, 1344), 138, top + 24);
      context.fillStyle = "#e8e9ff";
      context.font = '600 30px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(clipCanvasText(context, reply.text, 1344), 138, top + 58);
    });
    context.fillStyle = "#8b9bf4";
    context.font = '800 38px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("TAP / CLICK TO OPEN THE FULL PROFILE", 56, 4028);
    context.fillStyle = "#7a7ca8";
    context.font = '600 28px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      "LIVE · READ-ONLY · REFRESHED EVERY 10 MINUTES FROM MASTODON.SOCIAL",
      56,
      4068,
    );
    context.strokeStyle = "#6364ff";
    context.lineWidth = 16;
    context.strokeRect(12, 12, 1512, 4072);
  });
}

// MM:SS left before the next fetch, floored at 00:00.
function mastodonCountdownClock(remainingMs) {
  const seconds = Math.max(0, Math.ceil((Number(remainingMs) || 0) / 1000));
  const minutes = Math.floor(seconds / 60);
  return `${String(minutes).padStart(2, "0")}:${String(seconds % 60).padStart(
    2,
    "0",
  )}`;
}

// The refresh timer that rides on the kiosk frame: a small MM:SS readout of
// the time left in the ten-minute refresh window. It repaints once a second
// on its own small texture so the big board texture is only rebuilt when the
// snapshot itself changes.
function mastodonCountdownTexture(
  THREE,
  remainingMs = MASTODON_KIOSK_REFRESH_MS,
  totalMs = MASTODON_KIOSK_REFRESH_MS,
  loading = false,
) {
  const total = Math.max(1000, Number(totalMs) || MASTODON_KIOSK_REFRESH_MS);
  const remaining = clamp(Number(remainingMs) || 0, 0, total);
  return canvasTexture(THREE, 256, 128, (context) => {
    // Unframed: just the label over a soft backing plate, no border, so it
    // reads as lettering on the stand rather than a badge on the board.
    context.fillStyle = "rgba(15,16,36,0.72)";
    roundedRect(context, 4, 4, 248, 120, 22);
    context.fill();
    context.textAlign = "center";
    context.fillStyle = "#8b8db8";
    context.font = '700 22px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(loading ? "REFRESHING" : "NEXT SYNC", 128, 44);
    context.fillStyle = loading ? "#8b9bf4" : "#ffd257";
    context.font = '800 54px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      loading ? "--:--" : mastodonCountdownClock(remaining),
      128,
      100,
    );
  });
}

// Posting-cadence thresholds for the last-post plate. Mastodon presence
// guides settle on roughly one post a day for an account that wants to stay
// visible: under a day since the newest toot is healthy green, a missed day
// turns amber ("time to post"), and three silent days reads as an abandoned
// profile and goes red.
const MASTODON_POST_FRESH_MS = 24 * 60 * 60 * 1000;
const MASTODON_POST_STALE_MS = 72 * 60 * 60 * 1000;

// Compact elapsed readout: minutes under an hour, hours under two days,
// whole days beyond that.
function mastodonLastPostClock(sinceMs) {
  const minutes = Math.max(0, Math.floor((Number(sinceMs) || 0) / 60_000));
  if (minutes < 60) return `${minutes}M AGO`;
  const hours = Math.floor(minutes / 60);
  if (hours < 48) return `${hours}H AGO`;
  return `${Math.floor(hours / 24)}D AGO`;
}

function mastodonLastPostColor(
  sinceMs,
  freshMs = MASTODON_POST_FRESH_MS,
  staleMs = MASTODON_POST_STALE_MS,
) {
  const since = Number(sinceMs) || 0;
  if (since < freshMs) return "#9ef7c6";
  if (since < staleMs) return "#ffb454";
  return "#ff7a7a";
}

// The plate beside the sync clock: how long since @forkmesh last posted,
// tinted green / amber / red by the cadence thresholds above so a glance at
// the stand says whether it is time to post again. sinceMs of null (nothing
// fetched yet) renders a neutral placeholder. The social banners reuse the
// same plate with their own label and thresholds (the blog board has no
// post dates, so its plate reads the snapshot age as "SYNCED").
function mastodonLastPostTexture(
  THREE,
  sinceMs = null,
  label = "LAST POST",
  freshMs = MASTODON_POST_FRESH_MS,
  staleMs = MASTODON_POST_STALE_MS,
) {
  const known = Number.isFinite(Number(sinceMs)) && Number(sinceMs) >= 0;
  return canvasTexture(THREE, 256, 128, (context) => {
    context.fillStyle = "rgba(15,16,36,0.72)";
    roundedRect(context, 4, 4, 248, 120, 22);
    context.fill();
    context.textAlign = "center";
    context.fillStyle = "#8b8db8";
    context.font = '700 22px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(label, 128, 44);
    context.fillStyle = known
      ? mastodonLastPostColor(sinceMs, freshMs, staleMs)
      : "#8b9bf4";
    context.font = '800 44px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(known ? mastodonLastPostClock(sinceMs) : "--", 128, 96);
  });
}

function createMastodonKiosk(THREE, interactive) {
  const group = new THREE.Group();
  group.name = "forkmesh-mastodon-kiosk";
  // A newsstand beside the Office approach: close enough to read on the walk
  // to the door, far enough not to block the campus bridge.
  group.position.set(33.5, 0, -18.5);
  group.rotation.y = Math.atan2(-group.position.x, -group.position.z);
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(7.6, 0.4, 2.2),
    makeMaterial(THREE, "#20213a", { metalness: 0.2, roughness: 0.7 }),
  );
  base.position.y = 0.2;
  const post = new THREE.Mesh(
    new THREE.BoxGeometry(0.5, 2.2, 0.5),
    makeMaterial(THREE, "#2c2d4d", { metalness: 0.4, roughness: 0.5 }),
  );
  post.position.y = 1.3;
  // A billboard twice as tall as the old one: the same 0.75-wide stand now
  // carries the live profile header, bio, stats, three full toot cards with
  // deep image strips, and the replies strip without crowding any of them.
  const frame = new THREE.Mesh(
    new THREE.BoxGeometry(7.9, 20.0, 0.36),
    makeMaterial(THREE, "#43389c", { metalness: 0.35, roughness: 0.45 }),
  );
  frame.position.y = 11.75;
  const face = new THREE.Mesh(
    new THREE.PlaneGeometry(7.2, 19.2),
    new THREE.MeshBasicMaterial({
      map: mastodonKioskTexture(THREE),
      toneMapped: false,
    }),
  );
  face.name = "forkmesh-mastodon-kiosk-face";
  face.position.set(0, 11.75, 0.2);
  // Sits on the stand under the board, off the artwork entirely, so the board
  // itself is all profile and posts. Wide and short: it reads a MM:SS clock,
  // not a dial.
  const countdown = new THREE.Mesh(
    new THREE.PlaneGeometry(1.6, 0.8),
    new THREE.MeshBasicMaterial({
      map: mastodonCountdownTexture(THREE),
      transparent: true,
      toneMapped: false,
    }),
  );
  countdown.name = "forkmesh-mastodon-kiosk-countdown";
  countdown.position.set(-0.95, 1.15, 0.3);
  // Its sibling plate: how long since the last toot, colored by whether we
  // are keeping up a healthy posting cadence.
  const lastPost = new THREE.Mesh(
    new THREE.PlaneGeometry(1.6, 0.8),
    new THREE.MeshBasicMaterial({
      map: mastodonLastPostTexture(THREE),
      transparent: true,
      toneMapped: false,
    }),
  );
  lastPost.name = "forkmesh-mastodon-kiosk-lastpost";
  lastPost.position.set(0.95, 1.15, 0.3);
  const makeKioskControl = (label, direction, x, y) => {
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
    control.position.set(x, y, 0.3);
    control.userData.interactive = `mastodon-kiosk-scroll-${direction}`;
    return control;
  };
  // Both scroll controls now live at the bottom of the board, up to the left
  // of down, out of the way of the toot cards above them.
  const scrollUp = makeKioskControl("▲", "up", 2.2, 2.55);
  const scrollDown = makeKioskControl("▼", "down", 3.1, 2.55);
  const makeOpenButton = (name, x, y) => {
    const button = new THREE.Mesh(
      new THREE.PlaneGeometry(0.55, 0.55),
      new THREE.MeshBasicMaterial({
        map: canvasTexture(THREE, 256, 256, (context) => {
          context.fillStyle = "#20213a";
          context.fillRect(0, 0, 256, 256);
          context.strokeStyle = "#8b9bf4";
          context.lineWidth = 14;
          context.strokeRect(8, 8, 240, 240);
          context.fillStyle = "#e8e9ff";
          context.font = '800 150px "ForkMesh Mono", ui-monospace, monospace';
          context.textAlign = "center";
          context.textBaseline = "middle";
          context.fillText("↗", 128, 136);
        }),
        toneMapped: false,
      }),
    );
    button.name = `forkmesh-mastodon-kiosk-open-${name}`;
    button.position.set(x, y, 0.3);
    button.userData.interactive = `mastodon-kiosk-open-${name}`;
    button.userData.href = "";
    button.visible = false;
    return button;
  };
  // One button beside the profile identity, one beside each of the three
  // visible toot cards, each opening that item's mastodon.social page in a
  // new tab instead of the in-app board. The heights track the card headings
  // on the taller board.
  const openProfile = makeOpenButton("profile", 3.25, 18.3);
  const openToot0 = makeOpenButton("toot-0", 3.25, 14.65);
  const openToot1 = makeOpenButton("toot-1", 3.25, 11.3);
  const openToot2 = makeOpenButton("toot-2", 3.25, 7.9);
  group.add(
    base,
    post,
    frame,
    face,
    countdown,
    lastPost,
    scrollUp,
    scrollDown,
    openProfile,
    openToot0,
    openToot1,
    openToot2,
  );
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

// Half-height siblings of the Mastodon kiosk for Twitter/X and Reddit: same
// stand, frame, and sign typography, but the whole board is one click target
// that opens the profile in a new tab. Neither network allows the browser to
// fetch its feed directly (no CORS, unlike mastodon.social), so the Worker
// proxies one edge-cached read via /api/world/social-posts and the board
// repaints from that snapshot; without one it keeps the static sign.
const SOCIAL_BANNER_VISIBLE_POSTS = 4;
const SOCIAL_BANNER_WIDTH = 1536;
const SOCIAL_BANNER_HEIGHT = 2048;
const SOCIAL_BANNER_POST_TOP = 760;
const SOCIAL_BANNER_POST_PITCH = 290;
// A board whose feed carries artwork and preview text (the blog's RSS items)
// draws taller cards: a 16:9 thumbnail beside the headline and the item's
// description under it, so fewer of them fit on the same face.
const SOCIAL_BANNER_ART_VISIBLE_POSTS = 3;
const SOCIAL_BANNER_ART_POST_PITCH = 372;
const SOCIAL_BANNER_ART_WIDTH = 400;
const SOCIAL_BANNER_ART_HEIGHT = 225;

const TWITTER_BANNER_OPTIONS = Object.freeze({
  id: "twitter",
  position: [37.2, 0, -9.2],
  accent: "#1d9bf2",
  frameColor: "#1b4e73",
  titleColor: "#f2faff",
  divider: "rgba(29,155,242,0.5)",
  title: "TWITTER / X",
  handle: "@forkmesh",
  host: "x.com/forkmesh",
  lines: [
    "OFFICIAL FORKMESH ACCOUNT",
    "RELEASES · OUTAGES · NEWS",
    "REPLIES WELCOME OVER THERE",
  ],
  footer: "OPENS X.COM IN A NEW TAB",
  url: "https://x.com/forkmesh",
  staleness: {
    label: "LAST POST",
    freshMs: MASTODON_POST_FRESH_MS,
    staleMs: MASTODON_POST_STALE_MS,
  },
});

const STATUS_BANNER_OPTIONS = Object.freeze({
  id: "status",
  position: [44, 0, -14],
  accent: "#49d98a",
  frameColor: "#17613f",
  titleColor: "#f2fff8",
  divider: "rgba(73,217,138,0.5)",
  title: "SYSTEM STATUS",
  handle: "forkmesh/forkmesh",
  host: "forkmesh.com/status",
  lines: [
    "CHECKING REPOSITORY PAGE",
    "README.MD · TREE · MIRRORS",
    "ONE SAMPLE EVERY MINUTE",
  ],
  footer: "OPENS FORKMESH.COM/STATUS",
  url: "/status",
  feedHeading: "ALL MONITORED SYSTEMS",
  staleness: {
    label: "LAST CHECK",
    freshMs: 2 * 60 * 1000,
    staleMs: 5 * 60 * 1000,
  },
});

const REDDIT_BANNER_OPTIONS = Object.freeze({
  id: "reddit",
  position: [27.6, 0, -26.6],
  accent: "#ff4500",
  frameColor: "#7a2d0e",
  titleColor: "#fff4ee",
  divider: "rgba(255,69,0,0.5)",
  title: "REDDIT",
  handle: "r/forkmesh",
  host: "reddit.com/r/forkmesh",
  lines: [
    "COMMUNITY SUBREDDIT",
    "QUESTIONS · SHOWCASES · HELP",
    "MODERATED BY THE CORE TEAM",
  ],
  footer: "OPENS REDDIT.COM IN A NEW TAB",
  url: "https://www.reddit.com/r/forkmesh/",
  staleness: {
    label: "LAST POST",
    freshMs: MASTODON_POST_FRESH_MS,
    staleMs: MASTODON_POST_STALE_MS,
  },
});

// The blog board continues the same ring past Reddit. Its posts come from
// the blog's own RSS feed (/blog/rss.xml), so each card shows the item's
// artwork and preview text. The articles carry no dates, so the staleness
// plate reads how old the fetched snapshot is: green within a healthy sync
// window, red once the feed looks stuck.
const BLOG_BANNER_OPTIONS = Object.freeze({
  id: "blog",
  position: [19.8, 0, -32.8],
  accent: "#3fb950",
  frameColor: "#1d5c33",
  titleColor: "#f0fff5",
  divider: "rgba(63,185,80,0.5)",
  title: "FORKMESH BLOG",
  handle: "forkmesh.com/blog",
  host: "every feature, explained",
  feedHeading: "FROM THE BLOG",
  postArt: true,
  lines: [
    "FEATURE DEEP DIVES",
    "MIRRORS · AGENTS · CI · CHAT",
    "RSS: FORKMESH.COM/BLOG/RSS.XML",
  ],
  footer: "RSS FEED AT /BLOG/RSS.XML",
  url: "https://forkmesh.com/blog",
  staleness: {
    label: "SYNCED",
    freshMs: 15 * 60 * 1000,
    staleMs: 60 * 60 * 1000,
  },
});

// One artwork-carrying card on a social board: the post's own image on the
// left (cover-cropped into a 16:9 tile), then its meta line, headline, and
// the preview text the feed item's description carries. An image that has
// not decoded yet — or one a host refuses to serve CORS-clean, which would
// taint the canvas and break the WebGL upload — keeps the placeholder plate.
function drawSocialBannerPostCard(context, post, top, resolveImage) {
  const width = SOCIAL_BANNER_ART_WIDTH;
  const height = SOCIAL_BANNER_ART_HEIGHT;
  const textX = 96 + width + 40;
  const textWidth = 1440 - textX;
  context.save();
  roundedRect(context, 96, top, width, height, 18);
  context.clip();
  const media =
    typeof resolveImage === "function" ? resolveImage(post.image) : null;
  if (media?.naturalWidth > 0 && media?.naturalHeight > 0) {
    const scale = Math.max(
      width / media.naturalWidth,
      height / media.naturalHeight,
    );
    context.drawImage(
      media,
      96 + (width - media.naturalWidth * scale) / 2,
      top + (height - media.naturalHeight * scale) / 2,
      media.naturalWidth * scale,
      media.naturalHeight * scale,
    );
  } else {
    context.fillStyle = "#232445";
    context.fillRect(96, top, width, height);
    context.fillStyle = "#7a7ca8";
    context.font = '700 28px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("IMAGE", 120, top + height / 2 + 10);
  }
  context.restore();
  context.fillStyle = "#8b9bf4";
  context.font = '600 34px "ForkMesh Mono", ui-monospace, monospace';
  context.fillText(
    clipCanvasText(context, post.meta, textWidth),
    textX,
    top + 36,
  );
  context.fillStyle = "#e8e9ff";
  context.font = '500 50px "ForkMesh Favorit", sans-serif';
  const headlines = wrapCanvasText(
    context, post.text, textX, top + 104, textWidth, 58, 2);
  context.fillStyle = "#a9abd4";
  context.font = '400 36px "ForkMesh Favorit", sans-serif';
  wrapCanvasText(
    context,
    post.detail,
    textX,
    top + 120 + headlines * 58,
    textWidth,
    44,
    3,
  );
}

function socialBannerTexture(
  THREE,
  options,
  snapshot = null,
  resolveImage = null,
) {
  const WIDTH = SOCIAL_BANNER_WIDTH;
  const HEIGHT = SOCIAL_BANNER_HEIGHT;
  return canvasTexture(THREE, WIDTH, HEIGHT, (context) => {
    context.fillStyle = "#191a2e";
    context.fillRect(0, 0, WIDTH, HEIGHT);
    context.fillStyle = options.accent;
    context.fillRect(12, 12, 1512, 260);
    context.fillStyle = options.titleColor;
    context.font = '800 150px "ForkMesh Favorit", sans-serif';
    context.fillText(options.title, 96, 200);
    context.fillStyle = "#c8c9ff";
    context.font = '700 72px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(options.handle, 96, 470);
    context.font = '600 56px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(options.host, 96, 562);
    context.strokeStyle = options.divider;
    context.lineWidth = 4;
    context.beginPath();
    context.moveTo(96, 656);
    context.lineTo(1440, 656);
    context.stroke();
    if (!snapshot) {
      // No proxy snapshot (still fetching, or the feed is unreachable):
      // fall back to the static sign describing the board.
      context.fillStyle = "#e8e9ff";
      context.font = '700 60px "ForkMesh Mono", ui-monospace, monospace';
      options.lines.forEach((line, index) => {
        context.fillText(line, 96, 810 + index * 136);
      });
      context.fillStyle = "#8b9bf4";
      context.font = '800 68px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("TAP / CLICK TO OPEN", 96, 1760);
      context.fillStyle = "#7a7ca8";
      context.font = '600 44px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(options.footer, 96, 1900);
    } else {
      const art = Boolean(options.postArt);
      const pitch = art
        ? SOCIAL_BANNER_ART_POST_PITCH
        : SOCIAL_BANNER_POST_PITCH;
      const posts = (Array.isArray(snapshot.posts) ? snapshot.posts : [])
        .slice(
          0,
          art ? SOCIAL_BANNER_ART_VISIBLE_POSTS : SOCIAL_BANNER_VISIBLE_POSTS,
        );
      context.fillStyle = "#8b9bf4";
      context.font = '700 48px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(options.feedHeading || "LATEST POSTS", 96, 726);
      if (!posts.length) {
        context.fillStyle = "#e8e9ff";
        context.font = '700 60px "ForkMesh Mono", ui-monospace, monospace';
        context.fillText("NO POSTS YET", 96, 900);
        context.fillStyle = "#7a7ca8";
        context.font = '600 48px "ForkMesh Mono", ui-monospace, monospace';
        context.fillText("BE THE FIRST OVER THERE", 96, 1000);
      }
      posts.forEach((post, index) => {
        const top = SOCIAL_BANNER_POST_TOP + index * pitch;
        if (art) {
          drawSocialBannerPostCard(context, post, top, resolveImage);
        } else {
          context.fillStyle = "#8b9bf4";
          context.font = '600 40px "ForkMesh Mono", ui-monospace, monospace';
          context.fillText(clipCanvasText(context, post.meta, 1344), 96, top);
          context.fillStyle = "#e8e9ff";
          context.font = '500 52px "ForkMesh Favorit", sans-serif';
          wrapCanvasText(context, post.text, 96, top + 76, 1344, 66, 3);
        }
        if (index < posts.length - 1) {
          // Art cards run taller than the text-only ones (three lines of
          // preview text under a two-line headline), so their rule sits
          // closer to the next card rather than through the last line.
          const rule = top + pitch - (art ? 20 : 66);
          context.strokeStyle = "rgba(139,155,244,0.25)";
          context.lineWidth = 2;
          context.beginPath();
          context.moveTo(96, rule);
          context.lineTo(1440, rule);
          context.stroke();
        }
      });
      context.fillStyle = "#8b9bf4";
      context.font = '800 68px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("TAP / CLICK TO OPEN", 96, 1920);
      context.fillStyle = "#7a7ca8";
      context.font = '600 44px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        "LIVE · REFRESHED EVERY 10 MINUTES · " + options.footer,
        96,
        2000,
      );
    }
    context.strokeStyle = options.accent;
    context.lineWidth = 16;
    context.strokeRect(12, 12, 1512, 2024);
  });
}

const SYSTEM_STATUS_COLORS = Object.freeze({
  operational: "#198a43",
  degraded: "#a66f00",
  down: "#d92d3a",
  unknown: "#d92d3a",
  future: "#f5f5f7",
});

function systemStatusColor(status) {
  return SYSTEM_STATUS_COLORS[String(status || "unknown")] ||
    SYSTEM_STATUS_COLORS.unknown;
}

// A compact, canvas-native copy of /status. The physical frame deliberately
// remains the same as the Twitter sign, while its face contains every system
// and the same day/hour/minute hierarchy and state colours as the web page.
function systemStatusBannerTexture(THREE, payload = null) {
  const WIDTH = SOCIAL_BANNER_WIDTH;
  const HEIGHT = SOCIAL_BANNER_HEIGHT;
  return canvasTexture(THREE, WIDTH, HEIGHT, (context) => {
    context.fillStyle = "#f7f7f8";
    context.fillRect(0, 0, WIDTH, HEIGHT);
    context.fillStyle = STATUS_BANNER_OPTIONS.accent;
    context.fillRect(12, 12, 1512, 190);
    context.fillStyle = "#f2fff8";
    context.font = '800 112px "ForkMesh Favorit", sans-serif';
    context.fillText("SYSTEM STATUS", 72, 145);
    context.fillStyle = "#25262b";
    context.font = '700 34px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText("forkmesh.com/status", 72, 250);
    context.fillStyle = "#686b74";
    context.font = '500 25px "ForkMesh Favorit", sans-serif';
    context.fillText(
      "30 days  ·  last 24 hours  ·  last 60 one-minute checks",
      72,
      292,
    );

    const systems = Array.isArray(payload?.systems) ? payload.systems : [];
    if (!systems.length) {
      context.fillStyle = "#25262b";
      context.font = '700 54px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("LOADING LIVE CHECKS…", 72, 480);
    }

    const rowTop = 325;
    const rowHeight = systems.length
      ? Math.min(205, 1640 / systems.length)
      : 205;
    systems.forEach((system, index) => {
      const top = rowTop + index * rowHeight;
      const scale = Math.min(1, rowHeight / 190);
      context.fillStyle = "#ffffff";
      roundedRect(
        context, 56, top, 1424, Math.max(24, rowHeight - 10), 14);
      context.fill();
      context.strokeStyle = "#dcdee4";
      context.lineWidth = 2;
      context.stroke();

      context.beginPath();
      context.fillStyle = systemStatusColor(system.status);
      context.arc(
        82, top + Math.max(16, 34 * scale), Math.max(4, 9 * scale),
        0, Math.PI * 2);
      context.fill();
      context.fillStyle = "#202126";
      context.font =
        `700 ${Math.max(13, 30 * scale)}px "ForkMesh Favorit", sans-serif`;
      context.fillText(
        clipCanvasText(context, String(system.label || system.id || ""), 650),
        104,
        top + Math.max(21, 44 * scale),
      );

      const metric = (value) => Number.isFinite(Number(value))
        ? `${Number(value).toFixed(1)}%`
        : "—";
      context.textAlign = "right";
      context.fillStyle = "#202126";
      context.font =
        `700 ${Math.max(12, 27 * scale)}px "ForkMesh Mono", ui-monospace, monospace`;
      context.fillText(
        metric(system.uptime24hPct), 1100, top + Math.max(18, 38 * scale));
      context.fillText(
        metric(system.uptimePct), 1282, top + Math.max(18, 38 * scale));
      context.fillText(
        metric(system.coverage24hPct), 1452, top + Math.max(18, 38 * scale));
      context.fillStyle = "#686b74";
      context.font =
        `500 ${Math.max(9, 17 * scale)}px "ForkMesh Favorit", sans-serif`;
      context.fillText("24h", 1100, top + Math.max(29, 61 * scale));
      context.fillText("30d", 1282, top + Math.max(29, 61 * scale));
      context.fillText(
        "coverage", 1452, top + Math.max(29, 61 * scale));
      context.textAlign = "left";

      // Top strip: one tile per day, with that day's hourly checks inside.
      const days = Array.isArray(system.days) ? system.days.slice(-30) : [];
      const dayX = 76;
      const dayY = top + Math.max(34, rowHeight * 0.36);
      const dayWidth = 44;
      const dayHeight = Math.max(8, rowHeight * 0.24);
      days.forEach((day, dayIndex) => {
        const x = dayX + dayIndex * 46;
        context.fillStyle = day?.status
          ? systemStatusColor(day.status)
          : "#dedfe4";
        roundedRect(context, x, dayY, dayWidth, dayHeight, 5);
        context.fill();
        const hours = Array.isArray(day?.hours) ? day.hours : [];
        hours.slice(0, 24).forEach((hour, hourIndex) => {
          context.fillStyle = systemStatusColor(hour?.status);
          context.fillRect(
            x + 4 + (hourIndex % 6) * 6,
            dayY + 2 + Math.floor(hourIndex / 6) *
              Math.max(1.5, (dayHeight - 4) / 4),
            4,
            Math.max(1, (dayHeight - 7) / 4),
          );
        });
      });

      // Middle strip: the latest 24 hourly checks.
      const hours = (
        Array.isArray(system.hours)
          ? system.hours
          : days.flatMap((day) =>
              Array.isArray(day?.hours) ? day.hours : [])
      ).slice(-24);
      hours.forEach((hour, hourIndex) => {
        context.fillStyle = systemStatusColor(hour?.status);
        roundedRect(
          context, 76 + hourIndex * 29, top + rowHeight * 0.69,
          23, Math.max(4, rowHeight * 0.10), 3);
        context.fill();
      });

      // Bottom strip: the latest 60 raw one-minute reachability checks.
      const minutes = Array.isArray(system.minutes)
        ? system.minutes.slice(-60)
        : [];
      minutes.forEach((minute, minuteIndex) => {
        context.fillStyle = systemStatusColor(minute?.status);
        roundedRect(
          context, 76 + minuteIndex * 18, top + rowHeight * 0.84,
          13, Math.max(3, rowHeight * 0.08), 2);
        context.fill();
      });
    });

    context.fillStyle = "#686b74";
    context.font = '600 24px "ForkMesh Mono", ui-monospace, monospace';
    context.fillText(
      "● OPERATIONAL   ● DEGRADED   ● DOWN   ● NO DATA",
      72,
      1992,
    );
    context.strokeStyle = STATUS_BANNER_OPTIONS.accent;
    context.lineWidth = 16;
    context.strokeRect(12, 12, 1512, 2024);
  });
}

function createSocialBanner(THREE, interactive, options) {
  const group = new THREE.Group();
  group.name = `forkmesh-${options.id}-banner`;
  group.position.set(...options.position);
  group.rotation.y = Math.atan2(-group.position.x, -group.position.z);
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(7.6, 0.4, 2.2),
    makeMaterial(THREE, "#20213a", { metalness: 0.2, roughness: 0.7 }),
  );
  base.position.y = 0.2;
  const post = new THREE.Mesh(
    new THREE.BoxGeometry(0.5, 2.2, 0.5),
    makeMaterial(THREE, "#2c2d4d", { metalness: 0.4, roughness: 0.5 }),
  );
  post.position.y = 1.3;
  const frame = new THREE.Mesh(
    new THREE.BoxGeometry(7.9, 10.0, 0.36),
    makeMaterial(THREE, options.frameColor, { metalness: 0.35, roughness: 0.45 }),
  );
  frame.position.y = 6.75;
  const face = new THREE.Mesh(
    new THREE.PlaneGeometry(7.2, 9.6),
    new THREE.MeshBasicMaterial({
      map: options.id === "status"
        ? systemStatusBannerTexture(THREE)
        : socialBannerTexture(THREE, options),
      toneMapped: false,
    }),
  );
  face.name = `forkmesh-${options.id}-banner-face`;
  face.position.set(0, 6.75, 0.2);
  // The same stand plates the Mastodon kiosk carries: the MM:SS countdown to
  // the next feed sync on the left, the staleness readout on the right.
  const countdown = new THREE.Mesh(
    new THREE.PlaneGeometry(1.6, 0.8),
    new THREE.MeshBasicMaterial({
      map: mastodonCountdownTexture(THREE),
      transparent: true,
      toneMapped: false,
    }),
  );
  countdown.name = `forkmesh-${options.id}-banner-countdown`;
  countdown.position.set(-0.95, 1.15, 0.3);
  const staleness = new THREE.Mesh(
    new THREE.PlaneGeometry(1.6, 0.8),
    new THREE.MeshBasicMaterial({
      map: mastodonLastPostTexture(THREE, null, options.staleness.label),
      transparent: true,
      toneMapped: false,
    }),
  );
  staleness.name = `forkmesh-${options.id}-banner-lastpost`;
  staleness.position.set(0.95, 1.15, 0.3);
  group.add(base, post, frame, face, countdown, staleness);
  group.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.interactive = "social-banner-open";
    child.userData.href = options.url;
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
  const rooftopY = officeFloorY("rooftop");
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
    opacity: 0.24,
    metalness: 0,
    roughness: 0.62,
    depthWrite: false,
  });
  const doorMaterial = makeMaterial(THREE, "#c9fff3", {
    transparent: true,
    opacity: 0.3,
    metalness: 0,
    roughness: 0.38,
    depthWrite: false,
  });
  const elevatorFacadeMinX =
    OFFICE_ELEVATOR_CENTER_X -
    OFFICE_ELEVATOR_HALF_WIDTH -
    OFFICE_ELEVATOR_CUT_MARGIN;
  const elevatorFacadeMaxX =
    OFFICE_ELEVATOR_CENTER_X +
    OFFICE_ELEVATOR_HALF_WIDTH +
    OFFICE_ELEVATOR_CUT_MARGIN;
  const addFacadeSegment = (
    minX,
    maxX,
    height,
    depth,
    y,
    z,
    material,
  ) => {
    const width = maxX - minX;
    if (width <= 0) return;
    const mesh = new THREE.Mesh(
      new THREE.BoxGeometry(width, height, depth),
      material,
    );
    mesh.position.set((minX + maxX) / 2, y, z);
    group.add(mesh);
  };
  const rearWall = new THREE.Mesh(
    new THREE.BoxGeometry(
      OFFICE_WIDTH,
      rooftopY,
      wallThickness,
    ),
    glass,
  );
  rearWall.position.set(
    0,
    rooftopY / 2,
    -OFFICE_FRONT_Z + wallThickness / 2,
  );
  group.add(rearWall);

  for (const x of [
    -OFFICE_WIDTH / 2 + wallThickness / 2,
    OFFICE_WIDTH / 2 - wallThickness / 2,
  ]) {
    const sideWall = new THREE.Mesh(
      new THREE.BoxGeometry(
        wallThickness,
        rooftopY,
        OFFICE_DEPTH,
      ),
      glass,
    );
    sideWall.position.set(x, rooftopY / 2, 0);
    group.add(sideWall);
  }

  const frontZ = OFFICE_FRONT_Z - wallThickness / 2;
  for (const [minX, maxX] of [
    [-OFFICE_WIDTH / 2, elevatorFacadeMinX],
    [elevatorFacadeMaxX, OFFICE_WIDTH / 2],
  ]) {
    addFacadeSegment(
      minX,
      maxX,
      0.62,
      wallThickness,
      0.31,
      frontZ,
      concrete,
    );
    addFacadeSegment(
      minX,
      maxX,
      1.18,
      wallThickness,
      OFFICE_HEIGHT - 0.59,
      frontZ,
      concrete,
    );
  }

  // Ground-floor glazing leaves both the staffed entrance and the panoramic
  // elevator bay physically open instead of drawing panes through them.
  for (const [minX, maxX] of [
    [-OFFICE_WIDTH / 2 + 0.6, -doorWidth / 2 - 0.55],
    [doorWidth / 2 + 0.55, elevatorFacadeMinX],
    [elevatorFacadeMaxX, OFFICE_WIDTH / 2 - 0.6],
  ]) {
    addFacadeSegment(
      minX,
      maxX,
      OFFICE_HEIGHT - 1.34,
      0.12,
      OFFICE_HEIGHT / 2,
      OFFICE_FRONT_Z - 0.03,
      glass,
    );
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

  // One continuous ten-story curtain wall. Repeated geometry stays simple and
  // the emissive floor bands make every team level legible from Town Square.
  for (let level = 1; level < OFFICE_FLOOR_COUNT; level += 1) {
    const baseY = level * OFFICE_FLOOR_HEIGHT;
    if (level < OFFICE_FLOOR_COUNT - 1) {
      for (const [minX, maxX] of [
        [-OFFICE_WIDTH / 2 + 0.6, elevatorFacadeMinX],
        [elevatorFacadeMaxX, OFFICE_WIDTH / 2 - 0.6],
      ]) {
        addFacadeSegment(
          minX,
          maxX,
          OFFICE_FLOOR_HEIGHT - 0.72,
          0.14,
          baseY + OFFICE_FLOOR_HEIGHT / 2,
          OFFICE_FRONT_Z - 0.03,
          glass,
        );
      }
    }
    const floorBandMaterial = makeMaterial(
      THREE,
      level === OFFICE_FLOOR_COUNT - 1 ? "#a8e7ff" : "#72efba",
      {
        emissive: level === OFFICE_FLOOR_COUNT - 1
          ? "#3e88ad"
          : "#1d8f69",
        emissiveIntensity: 0.75,
        metalness: 0.38,
        roughness: 0.28,
      },
    );
    for (const [minX, maxX] of [
      [-OFFICE_WIDTH / 2, elevatorFacadeMinX],
      [elevatorFacadeMaxX, OFFICE_WIDTH / 2],
    ]) {
      addFacadeSegment(
        minX,
        maxX,
        0.16,
        0.5,
        baseY + 0.1,
        OFFICE_FRONT_Z + 0.08,
        floorBandMaterial,
      );
    }
  }
  for (const x of [-72, -48, -24, 24, 48]) {
    const facadeColumn = new THREE.Mesh(
      new THREE.BoxGeometry(0.28, OFFICE_TOWER_HEIGHT, 0.34),
      structure,
    );
    facadeColumn.position.set(
      x,
      OFFICE_TOWER_HEIGHT / 2,
      OFFICE_FRONT_Z + 0.04,
    );
    group.add(facadeColumn);
  }
  const floorAccentColors = [
    "#9ef7c6",
    "#f7c96b",
    "#77d9ff",
    "#ff8ab6",
    "#ff7189",
    "#64d6ff",
    "#b6ef7e",
    "#c7a0ff",
    "#ffaf75",
    "#a7dfff",
  ];
  OFFICE_FLOORS.forEach((floor, index) => {
    const accent = floorAccentColors[index];
    const plaqueTexture = canvasTexture(THREE, 768, 176, (context) => {
      context.fillStyle = "#071714";
      context.fillRect(0, 0, 768, 176);
      context.strokeStyle = accent;
      context.lineWidth = 8;
      context.strokeRect(5, 5, 758, 166);
      context.fillStyle = accent;
      context.font = '900 64px "ForkMesh Mono", ui-monospace, monospace';
      context.textAlign = "left";
      context.textBaseline = "middle";
      context.fillText(String(floor.level + 1).padStart(2, "0"), 28, 88);
      context.fillStyle = "#effff8";
      context.font = '800 42px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(floor.label.toUpperCase(), 150, 88, 580);
    });
    const plaque = new THREE.Mesh(
      new THREE.PlaneGeometry(12, 2.75),
      new THREE.MeshBasicMaterial({
        map: plaqueTexture,
        toneMapped: false,
      }),
    );
    plaque.name = `forkmesh-office-facade-floor-${floor.id}`;
    plaque.position.set(
      -76,
      floor.level * OFFICE_FLOOR_HEIGHT + OFFICE_FLOOR_HEIGHT / 2,
      OFFICE_FRONT_Z + 0.26,
    );
    group.add(plaque);
  });

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
  sign.scale.set(2.25, 2.25, 1);
  sign.position.set(0, OFFICE_HEIGHT - 3, OFFICE_FRONT_Z + 0.24);
  group.add(sign);

  const slidingDoors = new THREE.Group();
  slidingDoors.name = "forkmesh-office-sliding-doors";
  slidingDoors.position.z = OFFICE_FRONT_Z + 0.03;
  const doorPanelWidth = doorWidth / 2 - 0.08;
  const doorClosedX = doorWidth / 4;
  const doorOpenX = doorWidth / 2 + doorPanelWidth / 2 + 0.18;
  const doorPanels = [-1, 1].map((side) => {
    const panel = new THREE.Mesh(
      new THREE.BoxGeometry(doorPanelWidth, doorHeight, 0.18),
      doorMaterial,
    );
    panel.name =
      `forkmesh-office-sliding-door-${side < 0 ? "left" : "right"}`;
    panel.position.set(
      side * doorClosedX,
      doorHeight / 2 + 0.34,
      0,
    );
    slidingDoors.add(panel);
    return panel;
  });
  group.add(slidingDoors);

  group.position.set(...position);
  group.userData.landmark = "office";
  group.userData.officeSlidingDoors = slidingDoors;
  group.userData.officeDoorPanels = doorPanels;
  group.userData.officeDoorClosedX = doorClosedX;
  group.userData.officeDoorOpenX = doorOpenX;
  group.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.landmark = "office";
    interactive.push(child);
  });
  setShadows(group);
  // Transparent walls are a view envelope, not shadow casters. Keeping them
  // out of both the depth and shadow buffers prevents bright/dark popping as
  // the camera crosses the tower while preserving the opaque frame.
  group.traverse((child) => {
    if (!child.isMesh || !child.material?.transparent) return;
    child.castShadow = false;
    child.receiveShadow = false;
  });
  sign.castShadow = false;
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
  onOfficeRooftopLaptopSelect = () => {},
  onWorldBulletinSelect = () => {},
  onMastodonBoardSelect = () => {},
  onMastodonOpenLink = () => {},
  onReferralBoardSelect = () => {},
  onSystemCapacityTableSelect = () => {},
  onRendererStateChange = () => {},
  onOfficeChairSelect = () => {},
  onOfficeMovement = () => {},
  onOfficeElevatorSound = () => {},
  onLocationChange = () => {},
  onRegionChange = () => {},
  onMovement = () => {},
  onModeration = () => {},
  onFediverseProfile = () => {},
  onFediverseFollow = () => {},
  onLayoutObjectMoved = () => {},
  onForkbotChat = () => {},
  onPlayForkmeshSong = () => {},
  onCreateRepository = () => {},
  onSwingRide = () => {},
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
  scene.background = new THREE.Color(DAYLIGHT_ENVIRONMENT.background);
  const worldSky = createWorldSky({
    THREE,
    parent: scene,
    compact: compactRenderer,
    starCount: compactRenderer ? 700 : 1100,
    maxSatellites: compactRenderer ? 80 : 160,
  });

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
  // Banner-sized boards have to be walked up to, so it now stands just past
  // the leaderboards instead of stranded near the rim of the terrain — still
  // clear of the arrival / join grid visitors spawn onto.
  worldBulletin.position.set(-24, 0, 33);
  // Plane textures face local +Z. Rotate the board so its readable face looks
  // back into the World from the outer edge of the circular terrain.
  worldBulletin.rotation.y = Math.atan2(
    -worldBulletin.position.x,
    -worldBulletin.position.z,
  );
  let worldBulletinEvents = [];
  let worldBulletinOffset = 0;
  // Sized like the other standing banners in the square rather than the tower
  // it used to be: the page holds five entries, and the ▲ / ▼ controls page
  // through the rest.
  const BULLETIN_FACE_CENTER_Y = 3.9;
  const bulletinBase = new THREE.Mesh(
    new THREE.BoxGeometry(7.9, 0.26, 1.5),
    makeMaterial(THREE, "#123241", { roughness: 0.8 }),
  );
  bulletinBase.position.y = 0.13;
  worldBulletin.add(bulletinBase);
  for (const x of [-3.35, 3.35]) {
    const post = new THREE.Mesh(
      new THREE.BoxGeometry(0.18, 6.5, 0.18),
      makeMaterial(THREE, "#1d5570", { metalness: 0.26, roughness: 0.5 }),
    );
    post.position.set(x, 3.25, 0);
    worldBulletin.add(post);
  }
  const bulletinFrame = new THREE.Mesh(
    new THREE.BoxGeometry(7.55, 5.1, 0.24),
    makeMaterial(THREE, "#163849", { metalness: 0.35, roughness: 0.44 }),
  );
  bulletinFrame.position.y = BULLETIN_FACE_CENTER_Y;
  bulletinFrame.userData.interactive = "world-bulletin";
  const bulletinFace = new THREE.Mesh(
    new THREE.PlaneGeometry(7.2, 4.8),
    new THREE.MeshBasicMaterial({
      map: worldBulletinTexture(THREE),
      toneMapped: false,
    }),
  );
  bulletinFace.name = "forkmesh-world-bulletin-face";
  bulletinFace.position.set(0, BULLETIN_FACE_CENTER_Y, 0.14);
  bulletinFace.userData.interactive = "world-bulletin";
  const makeBulletinControl = (label, direction, y) => {
    const control = new THREE.Mesh(
      new THREE.PlaneGeometry(0.52, 0.52),
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
    control.position.set(2.99, y, 0.2);
    control.userData.interactive = `world-bulletin-scroll-${direction}`;
    return control;
  };
  // Parked in the clear right-hand margin, level with the header and the
  // footer so neither control covers an entry.
  const bulletinScrollUp = makeBulletinControl("▲", "up", BULLETIN_FACE_CENTER_Y + 2.05);
  const bulletinScrollDown = makeBulletinControl("▼", "down", BULLETIN_FACE_CENTER_Y - 2.05);
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

  // Fully hosted imports live on their own repository island. The bridge
  // overlaps both shorelines, so it is a real, raycastable walking surface
  // rather than scenery visitors have to teleport across.
  const repositoryIsland = new THREE.Mesh(
    new THREE.CircleGeometry(REPOSITORY_ISLAND_RADIUS, 96),
    makeMaterial(THREE, "#215c42", { roughness: 1 }),
  );
  repositoryIsland.name = "hosted-repository-island";
  repositoryIsland.rotation.x = -Math.PI / 2;
  repositoryIsland.position.set(REPOSITORY_ISLAND_CENTER_X, 0.01, 0);
  repositoryIsland.receiveShadow = true;
  repositoryIsland.userData.ground = true;
  world.add(repositoryIsland);

  const repositoryBridge = new THREE.Group();
  repositoryBridge.name = "hosted-repository-bridge";
  repositoryBridge.position.set(
    (WORLD_GROUND_RADIUS +
      REPOSITORY_ISLAND_CENTER_X -
      REPOSITORY_ISLAND_RADIUS) /
      2,
    0,
    0,
  );
  const bridgeDeck = new THREE.Mesh(
    new THREE.BoxGeometry(12, 0.22, 4.8),
    makeMaterial(THREE, "#70563b", { roughness: 0.88 }),
  );
  bridgeDeck.position.y = 0.1;
  bridgeDeck.receiveShadow = true;
  bridgeDeck.userData.ground = true;
  repositoryBridge.add(bridgeDeck);
  for (let x = -5.4; x <= 5.4; x += 1.2) {
    const plank = new THREE.Mesh(
      new THREE.BoxGeometry(0.92, 0.12, 4.55),
      makeMaterial(THREE, x % 2.4 ? "#98724b" : "#aa8053", {
        roughness: 0.92,
      }),
    );
    plank.position.set(x, 0.25, 0);
    plank.userData.ground = true;
    repositoryBridge.add(plank);
  }
  world.add(repositoryBridge);

  // The Office is a real campus island, not another interior scene. Its glass
  // bridge overlaps both shores so walking, camera framing, and multiplayer
  // presence remain continuous from the Town Square all the way to the roof.
  const officeIsland = new THREE.Mesh(
    new THREE.CircleGeometry(OFFICE_ISLAND_RADIUS, 128),
    makeMaterial(THREE, "#183f38", { roughness: 0.96 }),
  );
  officeIsland.name = "forkmesh-office-island";
  officeIsland.rotation.x = -Math.PI / 2;
  officeIsland.position.set(
    OFFICE_ISLAND_CENTER[0],
    0.015,
    OFFICE_ISLAND_CENTER[2],
  );
  officeIsland.receiveShadow = true;
  officeIsland.userData.ground = true;
  world.add(officeIsland);

  const officeBridge = new THREE.Group();
  officeBridge.name = "forkmesh-office-bridge";
  const officeBridgeLength =
    Math.abs(OFFICE_BRIDGE_END_Z - OFFICE_BRIDGE_START_Z) + 3;
  officeBridge.position.set(
    0,
    0,
    (OFFICE_BRIDGE_START_Z + OFFICE_BRIDGE_END_Z) / 2,
  );
  const officeBridgeDeck = new THREE.Mesh(
    new THREE.BoxGeometry(OFFICE_BRIDGE_WIDTH, 0.3, officeBridgeLength),
    makeMaterial(THREE, "#567a72", {
      metalness: 0.44,
      roughness: 0.35,
    }),
  );
  officeBridgeDeck.position.y = 0.14;
  officeBridgeDeck.receiveShadow = true;
  officeBridgeDeck.userData.ground = true;
  officeBridge.add(officeBridgeDeck);
  const bridgeGlass = makeMaterial(THREE, "#b9fff0", {
    transparent: true,
    opacity: 0.34,
    metalness: 0.12,
    roughness: 0.08,
  });
  for (const x of [
    -OFFICE_BRIDGE_WIDTH / 2 + 0.16,
    OFFICE_BRIDGE_WIDTH / 2 - 0.16,
  ]) {
    const rail = new THREE.Mesh(
      new THREE.BoxGeometry(0.18, 2.4, officeBridgeLength),
      bridgeGlass,
    );
    rail.position.set(x, 1.35, 0);
    officeBridge.add(rail);
  }
  world.add(officeBridge);
  const officeApproachLength =
    Math.abs(
      OFFICE_ISLAND_CENTER[2] +
        OFFICE_FRONT_Z -
        OFFICE_BRIDGE_END_Z,
    ) - 1.5;
  const officeApproach = new THREE.Group();
  officeApproach.name = "forkmesh-office-island-approach";
  officeApproach.position.set(
    OFFICE_ISLAND_CENTER[0],
    0,
    (OFFICE_BRIDGE_END_Z +
      OFFICE_ISLAND_CENTER[2] +
      OFFICE_FRONT_Z) /
      2,
  );
  const approachDeck = new THREE.Mesh(
    new THREE.BoxGeometry(
      OFFICE_BRIDGE_WIDTH + 2,
      0.18,
      officeApproachLength,
    ),
    makeMaterial(THREE, "#294e46", {
      metalness: 0.24,
      roughness: 0.62,
    }),
  );
  approachDeck.position.y = 0.09;
  approachDeck.receiveShadow = true;
  approachDeck.userData.ground = true;
  officeApproach.add(approachDeck);
  for (const x of [
    -OFFICE_BRIDGE_WIDTH / 2 - 0.6,
    OFFICE_BRIDGE_WIDTH / 2 + 0.6,
  ]) {
    const guideLight = new THREE.Mesh(
      new THREE.BoxGeometry(0.18, 0.06, officeApproachLength - 1),
      makeMaterial(THREE, "#8ff1c3", {
        emissive: "#2bb87d",
        emissiveIntensity: 1.1,
        metalness: 0.2,
        roughness: 0.28,
      }),
    );
    guideLight.position.set(x, 0.22, 0);
    officeApproach.add(guideLight);
  }
  world.add(officeApproach);

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
    // The campfire owns a map spot but no district factory: its group and
    // bench circle are built below and registered under their own layout id.
    const factory = landmarkFactories[landmark.id];
    if (!factory) return;
    const object = factory(
      THREE,
      landmark.position,
      interactive,
      animated,
    );
    landmarkObjects.set(landmark.id, object);
    world.add(object);
    // The tower, island, bridge, admission boundary, and floor colliders form
    // one structural campus. Letting the layout editor move only the tower
    // would strand its entrance and is therefore deliberately unsupported.
    if (landmark.id !== "office") {
      registerMovableObject("landmark-" + landmark.id, object);
    }
  });
  const mastodonKiosk = createMastodonKiosk(THREE, interactive);
  world.add(mastodonKiosk);
  registerMovableObject("mastodon-kiosk", mastodonKiosk);
  // Twitter and Reddit flank the Mastodon kiosk on the same ring toward the
  // Office, one board-width of clearance on either side, and the blog board
  // continues the row past Reddit so all four read as one social row on the
  // approach.
  const twitterBanner = createSocialBanner(
    THREE, interactive, TWITTER_BANNER_OPTIONS);
  world.add(twitterBanner);
  registerMovableObject("twitter-banner", twitterBanner);
  const redditBanner = createSocialBanner(
    THREE, interactive, REDDIT_BANNER_OPTIONS);
  world.add(redditBanner);
  registerMovableObject("reddit-banner", redditBanner);
  const blogBanner = createSocialBanner(
    THREE, interactive, BLOG_BANNER_OPTIONS);
  world.add(blogBanner);
  registerMovableObject("blog-banner", blogBanner);
  // Same physical display format as the Twitter/X sign, placed on the Office
  // approach. Its face mirrors all systems and all three history strips from
  // /status instead of reducing the page to one repository summary.
  const statusBanner = createSocialBanner(
    THREE, interactive, STATUS_BANNER_OPTIONS);
  world.add(statusBanner);
  registerMovableObject("status-banner", statusBanner);
  const statusBannerRecord = {
    group: statusBanner,
    options: STATUS_BANNER_OPTIONS,
    snapshot: null,
  };
  const socialBanners = [
    { group: twitterBanner, options: TWITTER_BANNER_OPTIONS },
    { group: redditBanner, options: REDDIT_BANNER_OPTIONS },
    { group: blogBanner, options: BLOG_BANNER_OPTIONS },
  ];

  // Post artwork (the blog feed's item images) is a same-origin /assets URL:
  // it only reaches the board texture once it decodes CORS-clean, and its
  // load repaints the boards that draw art. A host that refuses the read
  // simply leaves the placeholder plate in place.
  const socialBannerImages = new Map();

  function socialBannerImage(url) {
    const key = String(url || "");
    if (!key) return null;
    const cached = socialBannerImages.get(key);
    if (cached) return cached.image;
    const record = { image: null };
    socialBannerImages.set(key, record);
    const image = new Image();
    image.crossOrigin = "anonymous";
    image.decoding = "async";
    image.onload = () => {
      if (disposed) return;
      record.image = image;
      for (const banner of socialBanners) {
        if (banner.options.postArt) repaintSocialBanner(banner);
      }
    };
    image.onerror = () => {};
    image.src = key;
    return null;
  }

  function repaintSocialBanner(record) {
    const face = record.group.getObjectByName(
      `forkmesh-${record.options.id}-banner-face`,
    );
    if (!face?.material) return false;
    face.material.map?.dispose?.();
    face.material.map = record.options.id === "status"
      ? systemStatusBannerTexture(THREE, record.snapshot || null)
      : socialBannerTexture(
          THREE, record.options, record.snapshot || null, socialBannerImage);
    face.material.needsUpdate = true;
    return true;
  }

  // Repaint the banner faces from the Worker's /api/world/social-posts
  // snapshot. A feed that is missing or unavailable keeps (or returns to)
  // the static sign rather than showing a blank board.
  function updateSocialBanners(payload) {
    for (const record of socialBanners) {
      const feed = payload?.[record.options.id];
      record.snapshot =
        feed && typeof feed === "object" && feed.state === "ready"
          ? { posts: Array.isArray(feed.posts) ? feed.posts : [] }
          : null;
      repaintSocialBanner(record);
    }
  }

  function updateSystemStatusBoard(payload) {
    statusBannerRecord.snapshot =
      payload && Array.isArray(payload.systems) ? payload : null;
    repaintSocialBanner(statusBannerRecord);

    const lastCheck = Number(payload?.current?.lastCronSampleTs) || 0;
    const since = lastCheck > 0 ? Math.max(0, Date.now() - lastCheck) : null;
    const plate = statusBanner.getObjectByName(
      "forkmesh-status-banner-lastpost");
    if (plate?.material) {
      plate.material.map?.dispose?.();
      plate.material.map = mastodonLastPostTexture(
        THREE,
        since,
        STATUS_BANNER_OPTIONS.staleness.label,
        STATUS_BANNER_OPTIONS.staleness.freshMs,
        STATUS_BANNER_OPTIONS.staleness.staleMs,
      );
      plate.material.needsUpdate = true;
    }
    const dial = statusBanner.getObjectByName(
      "forkmesh-status-banner-countdown");
    if (dial?.material) {
      const remaining = 60_000 - (Date.now() % 60_000);
      dial.material.map?.dispose?.();
      dial.material.map = mastodonCountdownTexture(
        THREE, remaining, 60_000, false);
      dial.material.needsUpdate = true;
    }
  }

  // The stand plates under each banner: a per-second countdown to the next
  // feed sync and a per-minute staleness readout. Both key their repaints
  // like the Mastodon kiosk plates so a tick that changes nothing visible
  // never rebuilds a texture.
  function updateSocialBannerTimers(payload) {
    let repainted = false;
    for (const record of socialBanners) {
      const timers = payload?.[record.options.id];
      if (!timers || typeof timers !== "object") continue;
      const total = Math.max(
        1000, Number(timers.totalMs) || MASTODON_KIOSK_REFRESH_MS);
      const remaining = clamp(Number(timers.remainingMs) || 0, 0, total);
      const loading = Boolean(timers.loading);
      const countdownKey =
        `${loading ? 1 : 0}:${Math.ceil(remaining / 1000)}:${total}`;
      if (countdownKey !== record.countdownKey) {
        record.countdownKey = countdownKey;
        const dial = record.group.getObjectByName(
          `forkmesh-${record.options.id}-banner-countdown`,
        );
        if (dial?.material) {
          dial.material.map?.dispose?.();
          dial.material.map = mastodonCountdownTexture(
            THREE, remaining, total, loading);
          dial.material.needsUpdate = true;
          repainted = true;
        }
      }
      const since =
        Number.isFinite(Number(timers.sinceMs)) && Number(timers.sinceMs) >= 0
          ? Number(timers.sinceMs)
          : null;
      const sinceKey =
        since === null ? "-" : String(Math.floor(since / 60_000));
      if (sinceKey !== record.stalenessKey) {
        record.stalenessKey = sinceKey;
        const plate = record.group.getObjectByName(
          `forkmesh-${record.options.id}-banner-lastpost`,
        );
        if (plate?.material) {
          const config = record.options.staleness;
          plate.material.map?.dispose?.();
          plate.material.map = mastodonLastPostTexture(
            THREE, since, config.label, config.freshMs, config.staleMs);
          plate.material.needsUpdate = true;
          repainted = true;
        }
      }
    }
    return repainted;
  }
  let mastodonKioskSnapshot = null;
  let mastodonKioskOffset = 0;
  let mastodonKioskCountdown = {
    remainingMs: MASTODON_KIOSK_REFRESH_MS,
    totalMs: MASTODON_KIOSK_REFRESH_MS,
    loading: false,
  };
  let mastodonKioskCountdownKey = "";
  let mastodonKioskLastPostSinceMs = null;
  let mastodonKioskLastPostKey = "";
  const mastodonKioskImages = new Map();
  const mastodonKioskWheelTargets = [];
  mastodonKiosk.traverse((child) => {
    if (child.isMesh) mastodonKioskWheelTargets.push(child);
  });

  const campfire = new THREE.Group();
  // The fire stands on its own map landmark, so the world-map spot and the
  // benches can never drift apart.
  campfire.position.set(...landmarkById("campfire").position);
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
  // The membership total rides in the flames themselves rather than on yet
  // another sign: an ember-lit numeral hovering over the pit, so the fire
  // reads as "this many accounts sit here" at a glance. Hidden until the
  // roster lands so it never flashes a placeholder zero.
  const memberCountSprite = new THREE.Sprite(
    new THREE.SpriteMaterial({
      transparent: true,
      depthWrite: false,
    }),
  );
  const MEMBER_COUNT_HOVER_Y = 1.95;
  memberCountSprite.position.y = MEMBER_COUNT_HOVER_Y;
  memberCountSprite.scale.set(2.8, 1.4, 1);
  memberCountSprite.visible = false;
  campfire.add(memberCountSprite);
  let memberCountShown = "";
  // Repaints only when the count or the newest member actually moved: the
  // roster refresh runs on a timer and would otherwise rebuild the canvas
  // every pass.
  function setCampfireMemberCount(total, newest = "") {
    const count = Math.max(0, Math.min(999999, Math.round(Number(total) || 0)));
    const latest = String(newest || "").trim().slice(0, 18);
    const key = `${count}|${latest}`;
    if (memberCountShown === key) return;
    memberCountShown = key;
    memberCountSprite.material.map?.dispose?.();
    memberCountSprite.material.map = campfireMemberCountTexture(
      THREE,
      count,
      latest,
    );
    memberCountSprite.material.needsUpdate = true;
    memberCountSprite.visible = true;
  }
  animated.push((time) => {
    const flicker = 1 + Math.sin(time * 0.011) * 0.12 + Math.sin(time * 0.023) * 0.06;
    const size = fireLevel * flicker;
    flame.scale.set(size, fireLevel * (1 + Math.sin(time * 0.017) * 0.16), size);
    innerFlame.scale.set(size * 0.82, size * 0.9, size * 0.82);
    fireLight.intensity = 3.2 * fireLevel + Math.sin(time * 0.013) * 0.7;
    // Drift with the flames so the number sits in the fire instead of on it.
    memberCountSprite.position.y =
      MEMBER_COUNT_HOVER_Y + Math.sin(time * 0.0017) * 0.07;
  });
  // The real bench count depends on the member roster, which is still an
  // in-flight network request when the scene first renders. Rather than
  // seat a placeholder ring that immediately resizes (and jumps every seated
  // avatar) once the roster arrives, show a spark orbiting the flames until
  // rebuildCampfireCircle first runs with real data.
  const benchLoadingSpark = new THREE.Mesh(
    new THREE.SphereGeometry(0.09, 12, 8),
    makeMaterial(THREE, "#ffffff", {
      emissive: "#ffd27a",
      emissiveIntensity: 2.4,
    }),
  );
  const BENCH_LOADING_SPARK_RADIUS = 1.6;
  campfire.add(benchLoadingSpark);
  animated.push((time) => {
    if (!benchLoadingSpark.visible) return;
    const spin = time * 0.004;
    benchLoadingSpark.position.set(
      Math.cos(spin) * BENCH_LOADING_SPARK_RADIUS,
      0.9 + Math.sin(time * 0.01) * 0.05,
      Math.sin(spin) * BENCH_LOADING_SPARK_RADIUS,
    );
  });
  // Benches sit back far enough from the pit to leave a wide walkable ring
  // between the seats and the stones (and to clear the log pile at ~2.6). The
  // circle carries one wooden bench per registered member — occupied by a
  // seated directory figure while the member is away, left empty (and
  // sittable) while they walk the world as a live avatar — plus one bench
  // that always stays open so an arriving guest has a spot by the fire, and
  // widens whenever a new account joins so everyone still fits.
  const CAMPFIRE_BENCH_RADIUS = 6.2;
  // Bench height is set by the sitters, not the other way round: the plank top
  // lands SEATED_SEAT_TO_SOLE above the walking plane so a seated avatar's
  // shins reach the ground instead of dangling (or folding through it).
  const CAMPFIRE_SEAT_HALF_THICKNESS = 0.07;
  const CAMPFIRE_SEAT_TOP_Y = WORLD_WALKING_PLANE_Y + SEATED_SEAT_TO_SOLE;
  const CAMPFIRE_SEAT_Y = CAMPFIRE_SEAT_TOP_Y - CAMPFIRE_SEAT_HALF_THICKNESS;
  const CAMPFIRE_BENCH_LEG_HEIGHT = CAMPFIRE_SEAT_Y - CAMPFIRE_SEAT_HALF_THICKNESS;
  const CAMPFIRE_CIRCLE_MIN_SEATS = 6;
  const CAMPFIRE_CIRCLE_MAX_SEATS = 96;
  const CAMPFIRE_SEAT_SPACING = 2.1;
  // The ring never closes all the way round: a doorway-wide span of it is kept
  // bench-free so visitors can walk straight in to the fire and back out again
  // instead of climbing over the planks, however many members the circle has
  // grown to. The radius reserves this arc alongside the seats, so widening the
  // ring for a new account never eats the opening.
  const CAMPFIRE_ENTRANCE_WIDTH = 3.4;
  // On a small ring the raw arc would swallow a third of the circle; cap it so
  // the seats still read as a ring rather than a horseshoe.
  const CAMPFIRE_ENTRANCE_MAX_ANGLE = Math.PI / 3;
  // The gap faces back toward the town centre, the side visitors arrive from,
  // wherever the fire's landmark stands.
  const CAMPFIRE_ENTRANCE_ANGLE = Math.atan2(
    -campfire.position.z,
    -campfire.position.x,
  );
  function rebuildCampfireCircle(neededSeats) {
    const count = Math.max(
      CAMPFIRE_CIRCLE_MIN_SEATS,
      Math.min(
        CAMPFIRE_CIRCLE_MAX_SEATS,
        Math.round(Number(neededSeats) || 0),
      ),
    );
    benchLoadingSpark.visible = false;
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
      (count * CAMPFIRE_SEAT_SPACING + CAMPFIRE_ENTRANCE_WIDTH) / (2 * Math.PI),
    );
    // Benches spread over everything but the entrance arc, with half a bench
    // gap of padding on each side of it, so the walkway stays clear and the
    // always-open bench at the end of the ring sits right beside the opening.
    const entranceAngle = Math.min(
      CAMPFIRE_ENTRANCE_MAX_ANGLE,
      CAMPFIRE_ENTRANCE_WIDTH / radius,
    );
    const seatStep = (Math.PI * 2 - entranceAngle) / count;
    const seatOffsets = [];
    const benches = [];
    for (let index = 0; index < count; index += 1) {
      const angle =
        CAMPFIRE_ENTRANCE_ANGLE + entranceAngle / 2 + (index + 0.5) * seatStep;
      const bench = new THREE.Group();
      bench.position.set(Math.cos(angle) * radius, 0, Math.sin(angle) * radius);
      // Long axis tangent to the ring so every bench fronts the flames.
      bench.rotation.y = -angle + Math.PI / 2;
      const plankSideMaterial = makeMaterial(THREE, "#8a5a33", {
        roughness: 0.86,
      });
      // The top face gets its own material so a seat's name can be branded
      // into just that face without a texture atlas stretching across the
      // sides and legs too.
      const plankTopMaterial = makeMaterial(THREE, "#8a5a33", {
        roughness: 0.86,
      });
      const seat = new THREE.Mesh(new THREE.BoxGeometry(1.6, 0.14, 0.6), [
        plankSideMaterial,
        plankSideMaterial,
        plankTopMaterial,
        plankSideMaterial,
        plankSideMaterial,
        plankSideMaterial,
      ]);
      seat.position.y = CAMPFIRE_SEAT_Y;
      // Seat coordinates are read from the live world matrix at click time, so a
      // relocated campfire needs no bookkeeping and the seats can never drift.
      seat.userData.campfireBench = true;
      interactive.push(seat);
      bench.add(seat);
      [-0.62, 0.62].forEach((end) => {
        const leg = new THREE.Mesh(
          new THREE.BoxGeometry(0.16, CAMPFIRE_BENCH_LEG_HEIGHT, 0.5),
          makeMaterial(THREE, "#4f3018", { roughness: 0.9 }),
        );
        leg.position.set(end, CAMPFIRE_BENCH_LEG_HEIGHT / 2, 0);
        bench.add(leg);
      });
      benches.push({ bench, seat, labelKey: null });
      ring.add(bench);
      // Offsets carry the top of the plank; sitters are placed by their hips
      // off it (seatedAvatarY), the same way the local player is.
      seatOffsets.push(
        new THREE.Vector3(
          bench.position.x,
          seat.position.y + CAMPFIRE_SEAT_HALF_THICKNESS,
          bench.position.z,
        ),
      );
    }
    setShadows(ring);
    campfire.add(ring);
    campfire.userData.seatRing = ring;
    campfire.userData.seatCount = count;
    campfire.userData.seatOffsets = seatOffsets;
    campfire.userData.seatBenches = benches;
    return seatOffsets;
  }

  // Rebrands one bench's plank top, skipping the canvas work when the seat
  // already shows this name and away state. The ring is not built until the
  // first updateMemberLounge, so an earlier call simply finds no bench.
  function setCampfireSeatLabel(index, name, away) {
    const bench = (campfire.userData.seatBenches || [])[index];
    if (!bench) return;
    const key = `${name} ${away ? "away" : "here"}`;
    if (bench.labelKey === key) return;
    bench.labelKey = key;
    const topMaterial = bench.seat.material[2];
    topMaterial.map?.dispose?.();
    topMaterial.map = campfireSeatPlateTexture(THREE, name, away);
    topMaterial.needsUpdate = true;
  }
  // No placeholder ring here: the spark above keeps the fire lively until
  // updateMemberLounge below runs with the real roster and calls
  // rebuildCampfireCircle with an accurate seat count.
  setShadows(campfire);
  world.add(campfire);
  landmarkObjects.set("campfire", campfire);
  registerMovableObject("campfire", campfire);

  // A wooden swing set west of the fountain: three swings hang from one beam,
  // so up to three visitors can ride at once. Clicking a seat starts the ride
  // and clicking it again hops off; the shell's swing-speed slider scales the
  // pumping, and the regular camera toggle watches the ride in first or third
  // person because both camera modes follow the player's position.
  const SWING_SET_POSITION = Object.freeze([-20, 0, 9]);
  const SWING_SEAT_COUNT = 3;
  const SWING_SEAT_SPACING = 2.3;
  const SWING_BEAM_HEIGHT = 4.6;
  const SWING_ROPE_LENGTH = 3.4;
  const SWING_SEAT_HALF_THICKNESS = 0.05;
  const SWING_MIN_AMPLITUDE = 0.14;
  const SWING_MAX_AMPLITUDE = 1.05;
  const SWING_REMOTE_AMPLITUDE = 0.55;
  // A remote visitor parked this close to a seat's rest spot is riding it.
  const SWING_REST_CLAIM_DISTANCE_SQ = 0.81;
  const SWING_ARM_HOLD_PITCH = 1.15;
  const swingSet = new THREE.Group();
  swingSet.name = "swing-set";
  swingSet.position.set(...SWING_SET_POSITION);
  // Local -Z (the way avatars face) points at the fountain so riders swing
  // looking across the Town Square.
  swingSet.rotation.y = Math.atan2(SWING_SET_POSITION[0], SWING_SET_POSITION[2]);
  const swingFrameMaterial = makeMaterial(THREE, "#6f5136", { roughness: 0.82 });
  const swingFrameWidth = SWING_SEAT_COUNT * SWING_SEAT_SPACING + 1.6;
  const swingLegSpread = 1.7;
  const swingLegLength = Math.hypot(SWING_BEAM_HEIGHT, swingLegSpread);
  [-1, 1].forEach((side) => {
    [-1, 1].forEach((lean) => {
      const leg = new THREE.Mesh(
        new THREE.CylinderGeometry(0.09, 0.11, swingLegLength, 8),
        swingFrameMaterial,
      );
      leg.position.set(
        (side * swingFrameWidth) / 2,
        SWING_BEAM_HEIGHT / 2,
        (lean * swingLegSpread) / 2,
      );
      leg.rotation.x = -lean * Math.atan2(swingLegSpread, SWING_BEAM_HEIGHT);
      swingSet.add(leg);
    });
  });
  const swingBeam = new THREE.Mesh(
    new THREE.CylinderGeometry(0.1, 0.1, swingFrameWidth + 0.7, 10),
    swingFrameMaterial,
  );
  swingBeam.rotation.z = Math.PI / 2;
  swingBeam.position.y = SWING_BEAM_HEIGHT;
  swingSet.add(swingBeam);
  const swingRopeMaterial = makeMaterial(THREE, "#d8cdb4", { roughness: 0.9 });
  const swingSeatMaterial = makeMaterial(THREE, "#a8763e", { roughness: 0.6 });
  const swingStates = [];
  for (let seatIndex = 0; seatIndex < SWING_SEAT_COUNT; seatIndex += 1) {
    const pivot = new THREE.Group();
    pivot.position.set(
      (seatIndex - (SWING_SEAT_COUNT - 1) / 2) * SWING_SEAT_SPACING,
      SWING_BEAM_HEIGHT,
      0,
    );
    [-1, 1].forEach((side) => {
      const rope = new THREE.Mesh(
        new THREE.CylinderGeometry(0.03, 0.03, SWING_ROPE_LENGTH, 6),
        swingRopeMaterial,
      );
      rope.position.set(side * 0.45, -SWING_ROPE_LENGTH / 2, 0);
      rope.userData.swingSeat = seatIndex;
      interactive.push(rope);
      pivot.add(rope);
    });
    const seat = new THREE.Mesh(
      new THREE.BoxGeometry(1.05, SWING_SEAT_HALF_THICKNESS * 2, 0.48),
      swingSeatMaterial,
    );
    seat.position.y = -SWING_ROPE_LENGTH;
    seat.userData.swingSeat = seatIndex;
    interactive.push(seat);
    pivot.add(seat);
    swingSet.add(pivot);
    // The rest anchor hangs outside the pivot so occupancy checks and the
    // step-off spot read the seat's still position, not the moving plank.
    const anchor = new THREE.Object3D();
    anchor.position.set(
      pivot.position.x,
      SWING_BEAM_HEIGHT - SWING_ROPE_LENGTH,
      0,
    );
    swingSet.add(anchor);
    swingStates.push({
      pivot,
      seat,
      anchor,
      amplitude: 0,
      // Staggered so idle and remote-ridden swings never move in lockstep.
      phase: seatIndex * 1.3,
      remoteId: "",
    });
  }
  const swingPendulumOmega = Math.sqrt(9.8 / SWING_ROPE_LENGTH);
  animated.push((time, delta) => {
    swingStates.forEach((swing, seatIndex) => {
      const target =
        swingRide?.index === seatIndex
          ? swingRideAmplitude()
          : swing.remoteId
            ? SWING_REMOTE_AMPLITUDE
            : 0;
      swing.amplitude += (target - swing.amplitude) * Math.min(1, delta * 1.4);
      if (swing.amplitude < 0.01 && !target) {
        swing.pivot.rotation.x = 0;
        return;
      }
      // Pumping harder swings a little faster as well as higher; the phase is
      // continuous so speed changes never snap the seat sideways.
      swing.phase += swingPendulumOmega * (0.8 + swing.amplitude * 0.5) * delta;
      swing.pivot.rotation.x = swing.amplitude * Math.sin(swing.phase);
    });
  });
  setShadows(swingSet);
  world.add(swingSet);
  registerMovableObject("swing-set", swingSet);

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
  // The chest screen (adhoc #369): idle it shows the FORKBOT wordmark; when a
  // mention pulls the droid over it echoes the speaker's line and then runs
  // the thinking dots (updateForkbot) until the reply lands in the room.
  const forkbotScreenTexture = canvasTexture(THREE, 512, 224, (context, canvas) =>
    drawForkbotScreen(context, canvas, null),
  );
  const forkbotScreen = new THREE.Mesh(
    new THREE.PlaneGeometry(0.82, 0.36),
    new THREE.MeshBasicMaterial({ map: forkbotScreenTexture, toneMapped: false }),
  );
  forkbotScreen.position.set(0, 1.02, 0.58);
  forkbot.add(forkbotScreen);
  forkbot.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.forkbotChat = true;
    interactive.push(child);
  });
  animated.push((time) => {
    // An excited ForkBot (someone just mentioned it) flashes its antenna and
    // eye much faster than the idle glow.
    const excited = Boolean(forkbotExcitement);
    antennaLight.material.emissiveIntensity = excited
      ? 2.2 + (Math.sin(time * 0.022) + 1) * 1.1
      : 1.2 + (Math.sin(time * 0.006) + 1) * 0.5;
    eye.material.emissiveIntensity = excited
      ? 2 + (Math.sin(time * 0.028) + 1) * 0.9
      : 1.25 + (Math.sin(time * 0.008) + 1) * 0.75;
  });
  world.add(forkbot);

  const remotePlayers = new Map();
  const remoteLabels = new Map();
  const moderationActions = new WeakMap();
  const moderationControlKeys = new Map();
  // Badge plane and tab buttons -> the avatar they belong to, so one click
  // handler can switch chest tabs and hit the follow pill.
  const chestControls = new WeakMap();
  registerAvatarChestControls(player, identity.id);
  const officeParticipants = new Map();
  const officeParticipantLabels = new Map();
  const officeBubbles = new Map();
  const officeChairs = new Map();
  const officeInterior = new THREE.Group();
  officeInterior.name = "forkmesh-office-interior";
  officeInterior.position.set(
    OFFICE_ISLAND_CENTER[0],
    0,
    OFFICE_ISLAND_CENTER[2],
  );
  officeInterior.visible = true;
  world.add(officeInterior);

  const officeLobbyFloorMaterial = makeMaterial(THREE, "#1c3029", {
    emissive: "#091b15",
    emissiveIntensity: 0.46,
    roughness: 0.86,
  });
  const officeFloorGroups = new Map([["lobby", officeInterior]]);
  const officeFloorSlabMaterial = makeMaterial(THREE, "#132821", {
    emissive: "#081912",
    emissiveIntensity: 0.36,
    metalness: 0.18,
    roughness: 0.82,
  });
  const officeFloorAccent = makeMaterial(THREE, "#67efb1", {
    emissive: "#1a9a68",
    emissiveIntensity: 0.82,
    metalness: 0.42,
    roughness: 0.28,
  });
  const elevatorCutMinX =
    OFFICE_ELEVATOR_CENTER_X -
    OFFICE_ELEVATOR_HALF_WIDTH -
    OFFICE_ELEVATOR_CUT_MARGIN;
  const elevatorCutMaxX =
    OFFICE_ELEVATOR_CENTER_X +
    OFFICE_ELEVATOR_HALF_WIDTH +
    OFFICE_ELEVATOR_CUT_MARGIN;
  const elevatorCutMinZ =
    OFFICE_ELEVATOR_CENTER_Z -
    OFFICE_ELEVATOR_HALF_DEPTH -
    OFFICE_ELEVATOR_CUT_MARGIN;
  function addOfficeFloorSurface(parent, material) {
    const minX = -OFFICE_WIDTH / 2;
    const maxX = OFFICE_WIDTH / 2;
    const minZ = -OFFICE_DEPTH / 2;
    const maxZ = OFFICE_DEPTH / 2;
    const segments = [
      {
        minX,
        maxX,
        minZ,
        maxZ: elevatorCutMinZ,
      },
      {
        minX,
        maxX: elevatorCutMinX,
        minZ: elevatorCutMinZ,
        maxZ,
      },
      {
        minX: elevatorCutMaxX,
        maxX,
        minZ: elevatorCutMinZ,
        maxZ,
      },
    ];
    segments.forEach((segment, index) => {
      const slab = new THREE.Mesh(
        new THREE.BoxGeometry(
          segment.maxX - segment.minX,
          0.38,
          segment.maxZ - segment.minZ,
        ),
        material,
      );
      slab.name =
        `forkmesh-office-floor-slab-` +
        `${parent.userData.officeFloorId || "lobby"}-${index + 1}`;
      slab.position.set(
        (segment.minX + segment.maxX) / 2,
        0.19,
        (segment.minZ + segment.maxZ) / 2,
      );
      slab.receiveShadow = true;
      parent.add(slab);
    });
  }
  addOfficeFloorSurface(officeInterior, officeLobbyFloorMaterial);
  OFFICE_FLOORS.slice(1).forEach((floor) => {
    const floorGroup = new THREE.Group();
    floorGroup.name = `forkmesh-office-floor-${floor.id}`;
    floorGroup.position.y = officeFloorY(floor.id);
    floorGroup.userData.officeFloorId = floor.id;
    addOfficeFloorSurface(floorGroup, officeFloorSlabMaterial);
    if (floor.id !== "rooftop") {
      for (let x = -72; x <= 72; x += 24) {
        const light = new THREE.Mesh(
          new THREE.BoxGeometry(12, 0.08, 0.28),
          officeFloorAccent,
        );
        light.position.set(x, OFFICE_FLOOR_HEIGHT - 0.34, 0);
        floorGroup.add(light);
      }
    }
    const sign = makeOfficeWallPlacard(
      THREE,
      `${floor.level + 1} · ${floor.label.toUpperCase()}`,
      floor.description,
      floor.id === "rooftop" ? "#a7dfff" : "#9ef7c6",
      30,
      4.2,
    );
    sign.name = `forkmesh-office-wall-placard-${floor.id}`;
    // Every team label is physically mounted to the rear glass. Plane meshes
    // keep neighboring floor names from hovering through slabs like sprites.
    sign.position.set(
      -62,
      floor.id === "rooftop" ? 4.2 : 7.3,
      -OFFICE_FRONT_Z + 0.48,
    );
    floorGroup.add(sign);
    officeInterior.add(floorGroup);
    officeFloorGroups.set(floor.id, floorGroup);
  });

  function addOfficeFunFloorProps() {
    const metal = makeMaterial(THREE, "#315e56", {
      metalness: 0.52,
      roughness: 0.35,
    });
    const glow = makeMaterial(THREE, "#73dfff", {
      emissive: "#2d82a4",
      emissiveIntensity: 1.15,
      metalness: 0.22,
      roughness: 0.24,
    });
    const engineering = officeFloorGroups.get("engineering");
    for (let x = -36; x <= 36; x += 18) {
      const station = new THREE.Group();
      const desk = new THREE.Mesh(
        new THREE.BoxGeometry(11, 0.42, 4.4),
        metal,
      );
      desk.position.y = 1.35;
      station.add(desk);
      const screen = new THREE.Mesh(
        new THREE.BoxGeometry(5.8, 2.8, 0.18),
        glow,
      );
      screen.position.set(0, 3, -1.6);
      station.add(screen);
      station.position.set(x, 0, 0);
      engineering.add(station);
    }

    const design = officeFloorGroups.get("product-design");
    const designColors = ["#ff7aa8", "#77d9ff", "#f7c96b", "#9ef7c6"];
    designColors.forEach((color, index) => {
      const prototype = new THREE.Mesh(
        index % 2
          ? new THREE.TorusKnotGeometry(2.8, 0.72, 48, 8)
          : new THREE.IcosahedronGeometry(3.2, 1),
        makeMaterial(THREE, color, {
          emissive: color,
          emissiveIntensity: 0.34,
          metalness: 0.38,
          roughness: 0.28,
        }),
      );
      prototype.name =
        `forkmesh-office-feature-product-design-${index + 1}`;
      prototype.position.set(
        -36 + index * 24,
        OFFICE_FLOOR_HEIGHT / 2,
        0,
      );
      design.add(prototype);
      animated.push((time) => {
        prototype.rotation.y = time * (0.00018 + index * 0.00003);
      });
    });

    const security = officeFloorGroups.get("security");
    const shield = new THREE.Mesh(
      new THREE.TorusKnotGeometry(3.8, 0.6, 72, 10, 2, 3),
      makeMaterial(THREE, "#ff7189", {
        emissive: "#a92945",
        emissiveIntensity: 0.78,
        metalness: 0.65,
        roughness: 0.2,
      }),
    );
    shield.name = "forkmesh-office-feature-security-shield";
    shield.position.set(0, OFFICE_FLOOR_HEIGHT / 2, -4);
    security.add(shield);
    animated.push((time) => {
      shield.rotation.y = time * 0.00024;
      shield.rotation.x = Math.sin(time * 0.0003) * 0.18;
    });

    const infrastructure = officeFloorGroups.get("infrastructure");
    for (const side of [-1, 1]) {
      for (let z = -27; z <= 27; z += 9) {
        const rack = new THREE.Mesh(
          new THREE.BoxGeometry(10, 5.8, 5.2),
          makeMaterial(THREE, "#17272f", {
            emissive: side > 0 ? "#164c71" : "#176143",
            emissiveIntensity: 0.35,
            metalness: 0.7,
            roughness: 0.3,
          }),
        );
        rack.position.set(side * 50, 3.3, z);
        infrastructure.add(rack);
      }
    }

    const community = officeFloorGroups.get("community");
    const gameTable = new THREE.Mesh(
      new THREE.BoxGeometry(16, 0.5, 8),
      makeMaterial(THREE, "#4c8a73", { roughness: 0.62 }),
    );
    gameTable.position.set(0, 1.45, -3);
    community.add(gameTable);
    const gameNet = new THREE.Mesh(
      new THREE.BoxGeometry(0.15, 1.2, 8),
      officeFloorAccent,
    );
    gameNet.position.set(0, 2.1, -3);
    community.add(gameNet);

    const partnerships = officeFloorGroups.get("partnerships");
    for (let radius = 4; radius <= 10; radius += 3) {
      const orbitPivot = new THREE.Group();
      orbitPivot.name = `forkmesh-office-feature-partnerships-orbit-${radius}`;
      orbitPivot.position.y = OFFICE_FLOOR_HEIGHT / 2;
      const orbit = new THREE.Mesh(
        new THREE.TorusGeometry(radius, 0.12, 8, 64),
        glow,
      );
      orbit.rotation.x = Math.PI / 2 + (radius - 7) * 0.015;
      orbitPivot.add(orbit);
      partnerships.add(orbitPivot);
      animated.push((time) => {
        orbitPivot.rotation.y = time * (0.00016 + radius * 0.000006);
      });
    }

    const operations = officeFloorGroups.get("operations");
    for (let x = -30; x <= 30; x += 12) {
      const consoleDesk = new THREE.Mesh(
        new THREE.BoxGeometry(9, 2.2, 6),
        makeMaterial(THREE, "#253e46", {
          emissive: "#22536b",
          emissiveIntensity: 0.42,
          metalness: 0.48,
          roughness: 0.35,
        }),
      );
      consoleDesk.position.set(x, 1.5, 0);
      operations.add(consoleDesk);
    }

    const rooftop = officeFloorGroups.get("rooftop");
    const roofGlass = makeMaterial(THREE, "#d8ffff", {
      transparent: true,
      opacity: 0.24,
      metalness: 0,
      roughness: 0.54,
      depthWrite: false,
    });
    for (const [width, depth, x, z] of [
      [OFFICE_WIDTH, 0.22, 0, -OFFICE_DEPTH / 2 + 0.2],
      [0.22, OFFICE_DEPTH, -OFFICE_WIDTH / 2 + 0.2, 0],
      [0.22, OFFICE_DEPTH, OFFICE_WIDTH / 2 - 0.2, 0],
    ]) {
      const barrier = new THREE.Mesh(
        new THREE.BoxGeometry(width, 3.2, depth),
        roofGlass,
      );
      barrier.position.set(x, 1.75, z);
      rooftop.add(barrier);
    }
    for (const [minX, maxX] of [
      [-OFFICE_WIDTH / 2, elevatorCutMinX],
      [elevatorCutMaxX, OFFICE_WIDTH / 2],
    ]) {
      const barrier = new THREE.Mesh(
        new THREE.BoxGeometry(maxX - minX, 3.2, 0.22),
        roofGlass,
      );
      barrier.position.set(
        (minX + maxX) / 2,
        1.75,
        OFFICE_DEPTH / 2 - 0.2,
      );
      rooftop.add(barrier);
    }
    const patioWood = makeMaterial(THREE, "#8b6848", {
      roughness: 0.7,
    });
    const patioFrame = makeMaterial(THREE, "#213b36", {
      metalness: 0.45,
      roughness: 0.44,
    });
    const patioSeat = makeMaterial(THREE, "#3f8369", {
      roughness: 0.68,
    });
    for (const [tableIndex, x] of [-40, 0, 40].entries()) {
      const patioTable = new THREE.Group();
      patioTable.name = `forkmesh-office-rooftop-table-${tableIndex + 1}`;
      patioTable.position.set(x, 0, 8);
      const tabletop = new THREE.Mesh(
        new THREE.CylinderGeometry(3.1, 3.1, 0.34, 24),
        patioWood,
      );
      tabletop.name =
        `forkmesh-office-rooftop-tabletop-${tableIndex + 1}`;
      tabletop.position.y = 2.68;
      patioTable.add(tabletop);
      for (const [legIndex, [legX, legZ]] of [
        [-1.8, -1.8],
        [-1.8, 1.8],
        [1.8, -1.8],
        [1.8, 1.8],
      ].entries()) {
        const leg = new THREE.Mesh(
          new THREE.BoxGeometry(0.28, 2.5, 0.28),
          patioFrame,
        );
        leg.name =
          `forkmesh-office-rooftop-table-${tableIndex + 1}-leg-${legIndex + 1}`;
        leg.position.set(legX, 1.25, legZ);
        patioTable.add(leg);
      }
      rooftop.add(patioTable);
      for (let index = 0; index < 4; index += 1) {
        const angle = index * Math.PI / 2;
        const chairId = `rooftop-chair-${tableIndex * 4 + index + 1}`;
        const chair = new THREE.Group();
        chair.name = `forkmesh-office-${chairId}`;
        const seatTopY = 1.42;
        const seat = new THREE.Mesh(
          new THREE.BoxGeometry(2.1, 0.22, 1.8),
          patioSeat,
        );
        seat.name = `forkmesh-office-${chairId}-seat`;
        seat.position.y = seatTopY - 0.11;
        chair.add(seat);
        for (const [legIndex, [legX, legZ]] of [
          [-0.78, -0.62],
          [-0.78, 0.62],
          [0.78, -0.62],
          [0.78, 0.62],
        ].entries()) {
          const leg = new THREE.Mesh(
            new THREE.BoxGeometry(0.2, 1.2, 0.2),
            patioFrame,
          );
          leg.name =
            `forkmesh-office-${chairId}-leg-${legIndex + 1}`;
          leg.position.set(legX, 0.6, legZ);
          chair.add(leg);
        }
        const back = new THREE.Mesh(
          new THREE.BoxGeometry(2.1, 1.65, 0.2),
          patioSeat,
        );
        back.name = `forkmesh-office-${chairId}-back`;
        back.position.set(0, 2.08, -0.8);
        chair.add(back);
        chair.position.set(
          x + Math.cos(angle) * 5,
          0,
          8 + Math.sin(angle) * 5,
        );
        chair.rotation.y = Math.atan2(
          -Math.cos(angle) * 5,
          -Math.sin(angle) * 5,
        );
        chair.userData.officeChairId = chairId;
        chair.userData.officeFloorId = "rooftop";
        chair.userData.officeSeatTopY = seatTopY;
        chair.traverse((child) => {
          if (!child.isMesh) return;
          child.userData.officeChairId = chairId;
          child.userData.officeFloorId = "rooftop";
          child.userData.interactive = "office-chair";
          interactive.push(child);
        });
        officeChairs.set(chairId, chair);
        rooftop.add(chair);
      }
    }

    // This is a launcher for the real repository source surface, not a mock
    // editor. The shell owns the fixed same-origin action and explains that
    // write-capable source work remains in ForkMesh desktop / IDE integration.
    const rooftopLaptop = new THREE.Group();
    rooftopLaptop.name = "forkmesh-office-rooftop-laptop";
    rooftopLaptop.position.set(0, 2.9, 8);
    rooftopLaptop.userData.officeFloorId = "rooftop";
    const laptopBase = new THREE.Mesh(
      new THREE.BoxGeometry(2.8, 0.12, 1.8),
      makeMaterial(THREE, "#151c24", {
        metalness: 0.72,
        roughness: 0.24,
      }),
    );
    laptopBase.name = "forkmesh-office-rooftop-laptop-base";
    laptopBase.position.z = -0.15;
    rooftopLaptop.add(laptopBase);
    const laptopScreen = new THREE.Mesh(
      new THREE.PlaneGeometry(2.52, 1.48),
      new THREE.MeshBasicMaterial({
        map: canvasTexture(THREE, 756, 444, (context) => {
          context.fillStyle = "#071714";
          context.fillRect(0, 0, 756, 444);
          context.strokeStyle = "#9ef7c6";
          context.lineWidth = 18;
          context.strokeRect(12, 12, 732, 420);
          context.fillStyle = "#eafff6";
          context.font =
            '800 48px "ForkMesh Mono", ui-monospace, monospace';
          context.textAlign = "center";
          context.fillText("FORKMESH SOURCE", 378, 190);
          context.fillStyle = "#8ecdb6";
          context.font =
            '600 29px "ForkMesh Mono", ui-monospace, monospace';
          context.fillText("CLICK TO OPEN REAL REPOSITORY", 378, 260);
        }),
        toneMapped: false,
      }),
    );
    laptopScreen.name = "forkmesh-office-rooftop-laptop-screen";
    laptopScreen.position.set(0, 0.86, 0.72);
    laptopScreen.rotation.x = -0.16;
    rooftopLaptop.add(laptopScreen);
    rooftopLaptop.traverse((child) => {
      if (!child.isMesh) return;
      child.userData.officeFloorId = "rooftop";
      child.userData.interactive = "office-rooftop-laptop";
      interactive.push(child);
    });
    rooftop.add(rooftopLaptop);
  }
  addOfficeFunFloorProps();
  // The exterior tower already owns the curtain wall. A second interior shell
  // sat almost coplanar with it and made the transparent panes flash as the
  // depth buffer alternated between layers. Interior props now render through
  // that single stable glass envelope.
  const officeBuilding = landmarkObjects.get("office");
  const officeSlidingDoorPanels =
    officeBuilding?.userData?.officeDoorPanels || [];
  const officeDoorClosedX =
    Number(officeBuilding?.userData?.officeDoorClosedX) ||
    OFFICE_DOOR_WIDTH / 4;
  const officeDoorOpenX =
    Number(officeBuilding?.userData?.officeDoorOpenX) ||
    OFFICE_DOOR_WIDTH * 0.76;
  let officeSlidingDoorOpen = 0;
  function updateOfficeSlidingDoors(_time, delta = 0.016) {
    const localPosition = officeAvatarLocalPosition(
      player,
      new THREE.Vector3(),
    );
    const onEntranceFloor =
      officeSceneMode === "town" ||
      (officeSceneMode === "lobby" && officeCurrentFloorId === "lobby");
    const approaching =
      onEntranceFloor &&
      Math.abs(localPosition.x) <= OFFICE_DOOR_WIDTH / 2 + 3.2 &&
      Math.abs(localPosition.z - OFFICE_FRONT_Z) <= 7.5;
    const target =
      approaching || officeDoorwayEntryPending || officeExitPending ? 1 : 0;
    const travel = Math.min(1, Math.max(0, Number(delta) || 0) * 5.8);
    officeSlidingDoorOpen +=
      (target - officeSlidingDoorOpen) * travel;
    officeSlidingDoorPanels.forEach((panel, index) => {
      const side = index === 0 ? -1 : 1;
      panel.position.x =
        side *
        THREE.MathUtils.lerp(
          officeDoorClosedX,
          officeDoorOpenX,
          officeSlidingDoorOpen,
        );
    });
  }

  function officeReceptionDeskDistance(localPosition) {
    const x = Number(localPosition?.x) || 0;
    const z = Number(localPosition?.z) || 0;
    const distanceX = Math.max(0, Math.abs(x) - 15);
    const distanceZ = Math.max(0, Math.abs(z + 36.5) - 2.5);
    return Math.hypot(distanceX, distanceZ);
  }

  function updateOfficeReceptionGuide(time = performance.now()) {
    const active =
      officeSceneMode === "lobby" &&
      officeCurrentFloorId === "lobby" &&
      !officeElevatorRide;
    if (!active) {
      officeReceptionWasNear = false;
      return false;
    }
    const distance = officeReceptionDeskDistance(
      officeAvatarLocalPosition(player, new THREE.Vector3()),
    );
    if (distance > OFFICE_RECEPTION_RESET_RANGE) {
      officeReceptionWasNear = false;
      return false;
    }
    if (
      distance > OFFICE_RECEPTION_TALK_RANGE ||
      officeReceptionWasNear
    ) {
      return false;
    }
    officeReceptionWasNear = true;
    const now = Number.isFinite(Number(time))
      ? Number(time)
      : performance.now();
    if (
      now - officeReceptionLastTipAt <
      OFFICE_RECEPTION_TALK_COOLDOWN_MS
    ) {
      return false;
    }
    const authenticated = officeFloorAccess.authenticated === true;
    const tips = authenticated
      ? OFFICE_RECEPTION_MEMBER_TIPS
      : OFFICE_RECEPTION_GUEST_TIPS;
    const index = authenticated
      ? officeReceptionMemberTipIndex
      : officeReceptionGuestTipIndex;
    const message = tips[index % tips.length];
    if (authenticated) {
      officeReceptionMemberTipIndex = (index + 1) % tips.length;
    } else {
      officeReceptionGuestTipIndex = (index + 1) % tips.length;
    }
    const shown = showAvatarChatBubble(
      officeReceptionNoah,
      message,
      { officeReception: true },
    );
    if (shown) officeReceptionLastTipAt = now;
    return shown;
  }

  const officeTable = new THREE.Mesh(
    new THREE.BoxGeometry(7.4, 0.34, 3.6),
    makeMaterial(THREE, "#715238", { roughness: 0.66 }),
  );
  officeTable.name = "forkmesh-office-marketing-tabletop";
  officeTable.position.set(0, officeFloorY("marketing") + 1.8, 0);
  officeInterior.add(officeTable);
  for (const x of [-3, 3]) {
    for (const z of [-1.2, 1.2]) {
      const tableLeg = new THREE.Mesh(
        new THREE.BoxGeometry(0.22, 1.2, 0.22),
        makeMaterial(THREE, "#10231e", { metalness: 0.35 }),
      );
      tableLeg.name = "forkmesh-office-marketing-table-leg";
      tableLeg.position.set(x, officeFloorY("marketing") + 1.02, z);
      officeInterior.add(tableLeg);
    }
  }
  // Meeting chairs are already about a seated avatar's hip height above the
  // office floor (0.4), so their sitters' shins reach the floor unchanged.
  const OFFICE_CHAIR_SEAT_TOP_Y = 0.91;
  const chairTransforms = [
    [-5.15, 0],
    [-2.35, -3.35],
    [0, -3.35],
    [2.35, -3.35],
    [5.15, 0],
    [2.35, 3.35],
    [0, 3.35],
    [-2.35, 3.35],
  ];
  chairTransforms.forEach(([x, z], index) => {
    const chairId = `chair-${index + 1}`;
    const yaw = Math.atan2(-x, -z);
    const chair = new THREE.Group();
    chair.name = `forkmesh-office-${chairId}`;
    const seat = new THREE.Mesh(
      new THREE.BoxGeometry(1.05, 0.18, 1.05),
      makeMaterial(THREE, "#2f6d56", { roughness: 0.72 }),
    );
    seat.position.y = OFFICE_CHAIR_SEAT_TOP_Y - 0.09;
    chair.add(seat);
    const back = new THREE.Mesh(
      new THREE.BoxGeometry(1.05, 1.45, 0.18),
      makeMaterial(THREE, "#347a61", { roughness: 0.7 }),
    );
    // The back is on the outside edge, leaving every seat visibly facing the
    // table. The seated avatar uses yaw + PI and therefore faces inward too.
    back.name = `forkmesh-office-${chairId}-back`;
    back.position.set(0, 1.42, -0.48);
    chair.add(back);
    chair.position.set(x, officeFloorY("marketing"), z);
    chair.rotation.y = yaw;
    chair.userData.officeChairId = chairId;
    chair.userData.officeFloorId = "marketing";
    chair.userData.officeSeatTopY = OFFICE_CHAIR_SEAT_TOP_Y;
    chair.traverse((child) => {
      if (!child.isMesh) return;
      child.userData.officeChairId = chairId;
      child.userData.officeFloorId = "marketing";
      child.userData.interactive = "office-chair";
      interactive.push(child);
    });
    officeChairs.set(chairId, chair);
    officeInterior.add(chair);
  });
  const officeMarketingTaskBoard = new THREE.Group();
  officeMarketingTaskBoard.name = "forkmesh-office-marketing-task-board";
  officeMarketingTaskBoard.position.set(
    -24,
    officeFloorY("marketing") + 6.4,
    -OFFICE_FRONT_Z + 0.55,
  );
  const officeMarketingTaskBoardFrame = new THREE.Mesh(
    new THREE.BoxGeometry(18.5, 11.4, 0.18),
    makeMaterial(THREE, "#315b52", {
      metalness: 0.34,
      roughness: 0.48,
      emissive: "#173f34",
      emissiveIntensity: 0.24,
    }),
  );
  officeMarketingTaskBoardFrame.name =
    "forkmesh-office-marketing-task-board-frame";
  officeMarketingTaskBoardFrame.userData.officeFloorId = "marketing";
  officeMarketingTaskBoardFrame.userData.interactive =
    "office-marketing-task-board";
  officeMarketingTaskBoard.add(officeMarketingTaskBoardFrame);
  const officeMarketingTaskBoardFace = new THREE.Mesh(
    new THREE.PlaneGeometry(18.12, 11.02),
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
  officeMarketingTaskBoardFace.userData.officeFloorId = "marketing";
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
    new THREE.PlaneGeometry(18.5, 12.72),
    new THREE.MeshBasicMaterial({
      map: officeGuideBoardTexture(THREE),
      toneMapped: false,
    }),
  );
  officeGuideBoard.name = "forkmesh-office-guide-board";
  officeGuideBoard.position.set(
    24,
    officeFloorY("marketing") + 6.65,
    -OFFICE_FRONT_Z + 0.56,
  );
  officeGuideBoard.userData.officeFloorId = "marketing";
  officeGuideBoard.userData.interactive = "office-meeting-board";
  interactive.push(officeGuideBoard);
  officeInterior.add(officeGuideBoard);
  const officeRoomSign = makeOfficeWallPlacard(
    THREE,
    "MARKETING STUDIO",
    "campaigns · task wall · encrypted meeting",
    "#9ef7c6",
    28,
    3.8,
  );
  officeRoomSign.name = "forkmesh-office-marketing-wall-title";
  officeRoomSign.position.set(
    0,
    officeFloorY("marketing") + 13.25,
    -OFFICE_FRONT_Z + 0.58,
  );
  officeInterior.add(officeRoomSign);
  const officeLight = new THREE.PointLight("#ffd8a3", 5.5, 28, 1.6);
  officeLight.position.set(0, officeFloorY("marketing") + 6.2, 0);
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

  // Staffed reception: Noah is a scene-native avatar, so visitors meet one
  // person at the far-wall desk instead of a modal login wall.
  const officeReception = new THREE.Group();
  officeReception.name = "forkmesh-office-reception";
  const receptionDesk = new THREE.Mesh(
    new THREE.BoxGeometry(30, 1.65, 5),
    makeMaterial(THREE, "#5b493c", {
      metalness: 0.25,
      roughness: 0.55,
    }),
  );
  receptionDesk.name = "forkmesh-office-reception-desk";
  receptionDesk.position.set(0, 1.05, -36.5);
  officeReception.add(receptionDesk);
  const officeReceptionNoah = createAvatar(
    THREE,
    {
      id: "office-greeter-noah",
      name: "Noah · Welcome desk",
      flag: "◌",
      browser: "Office",
      os: "Welcome desk",
      status: "available",
      accountStatus: "Supporting member",
      nodes: [],
      outfitColor: "ocean",
    },
    { remote: true, scale: 0.94 },
  );
  officeReceptionNoah.position.set(0, 0.38, -40);
  officeReceptionNoah.rotation.y = Math.PI;
  const noahHair = new THREE.Mesh(
    new THREE.BoxGeometry(1.05, 0.24, 0.86),
    makeMaterial(THREE, "#211b17", { roughness: 0.82 }),
  );
  noahHair.position.set(0, 3.88, -0.08);
  officeReceptionNoah.add(noahHair);
  officeReception.add(officeReceptionNoah);
  const noahNameplate = makeOfficeWallPlacard(
    THREE,
    "NOAH",
    "WELCOME · WORLD & REPOSITORY HELP",
    "#9ef7c6",
    11.2,
    1.35,
  );
  noahNameplate.name = "forkmesh-office-reception-nameplate-noah";
  // The nameplate sits on the desk's front face instead of hovering over Noah.
  noahNameplate.position.set(0, 1.2, -33.94);
  officeReception.add(noahNameplate);
  officeInterior.add(officeReception);
  for (const [x, z] of [
    [-52, 7],
    [0, -29],
    [52, 7],
  ]) {
    const lobbyLight = new THREE.PointLight("#b8ffe3", 4.8, 52, 1.7);
    lobbyLight.position.set(x, OFFICE_FLOOR_HEIGHT - 2, z);
    officeInterior.add(lobbyLight);
    const fixture = new THREE.Mesh(
      new THREE.BoxGeometry(14, 0.08, 0.32),
      officeFloorAccent,
    );
    fixture.position.set(x, OFFICE_FLOOR_HEIGHT - 0.45, z);
    officeInterior.add(fixture);
  }

  const officeGreetingBoard = new THREE.Mesh(
    new THREE.PlaneGeometry(24, 4),
    new THREE.MeshBasicMaterial({
      map: canvasTexture(THREE, 1200, 200, (context) => {
        context.fillStyle = "#071714";
        context.fillRect(0, 0, 1200, 200);
        context.strokeStyle = "#9ef7c6";
        context.lineWidth = 10;
        context.strokeRect(6, 6, 1188, 188);
        context.fillStyle = "#eafff6";
        context.font = '800 54px "ForkMesh Mono", ui-monospace, monospace';
        context.textAlign = "center";
        context.fillText("WELCOME · WALK RIGHT IN", 600, 92);
        context.fillStyle = "#8ecdb6";
        context.font = '600 30px "ForkMesh Mono", ui-monospace, monospace';
        context.fillText("Walk up to Noah for World and repository tips", 600, 145);
      }),
      toneMapped: false,
    }),
  );
  officeGreetingBoard.name = "forkmesh-office-greeting-board";
  officeGreetingBoard.position.set(0, 6.25, -OFFICE_FRONT_Z + 0.5);
  officeInterior.add(officeGreetingBoard);

  // Shared, bounded time clock on the left lobby wall. Authenticated punches
  // are server-timestamped; the public board exposes only the latest 20 visits.
  const officeAttendanceBoard = new THREE.Mesh(
    new THREE.PlaneGeometry(27, 13.2),
    new THREE.MeshBasicMaterial({ toneMapped: false }),
  );
  officeAttendanceBoard.name = "forkmesh-office-attendance";
  officeAttendanceBoard.position.set(-84.5, 7.1, 18);
  officeAttendanceBoard.rotation.y = Math.PI / 2;
  officeInterior.add(officeAttendanceBoard);

  function officeAttendanceTexture(snapshot = {}) {
    const visits = Array.isArray(snapshot?.visits)
      ? snapshot.visits.slice(0, 20)
      : [];
    return canvasTexture(THREE, 1600, 800, (context) => {
      const format = (value) => {
        const timestamp = Number(value);
        return Number.isFinite(timestamp) && timestamp > 0
          ? new Date(timestamp).toLocaleString([], {
              month: "short",
              day: "numeric",
              hour: "2-digit",
              minute: "2-digit",
            })
          : "—";
      };
      context.fillStyle = "#071714";
      context.fillRect(0, 0, 1600, 800);
      context.strokeStyle = "#79efb5";
      context.lineWidth = 14;
      context.strokeRect(8, 8, 1584, 784);
      context.fillStyle = "#9ef7c6";
      context.font = '900 48px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("OFFICE · LAST 20 VISITS", 42, 62);
      context.fillStyle = "#79a996";
      context.font = '800 27px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText("USER", 42, 112);
      context.fillText("IN", 570, 112);
      context.fillText("OUT", 1080, 112);
      context.strokeStyle = "rgba(121,239,181,0.28)";
      context.lineWidth = 2;
      context.beginPath();
      context.moveTo(38, 130);
      context.lineTo(1560, 130);
      context.stroke();
      context.font = '700 27px "ForkMesh Mono", ui-monospace, monospace';
      visits.forEach((visit, index) => {
        const y = 164 + index * 28;
        if (index % 2 === 0) {
          context.fillStyle = "rgba(158,247,198,0.045)";
          context.fillRect(30, y - 20, 1530, 30);
        }
        context.fillStyle = "#e9fff6";
        context.fillText(
          String(visit?.account || "Contributor").slice(0, 25),
          42,
          y,
        );
        context.fillStyle = "#a9d7c4";
        context.fillText(format(visit?.inAt), 570, y);
        context.fillStyle = visit?.outAt ? "#a9d7c4" : "#f7c96b";
        context.fillText(
          visit?.outAt ? format(visit.outAt) : "IN BUILDING",
          1080,
          y,
        );
      });
      context.fillStyle = "#83bba7";
      context.font = '650 25px "ForkMesh Mono", ui-monospace, monospace';
      context.fillText(
        visits.length
          ? "One row per visit · authenticated punches only"
          : "No recorded visits yet · signed-in members clock in automatically",
        42,
        760,
      );
    });
  }
  officeAttendanceBoard.material.map = officeAttendanceTexture();

  // A true reflective capture, throttled while the visitor is inside. The
  // sculpture follows the supplied chrome FM cube: branch graph on top, F/M
  // reliefs on its sides, slowly rotating over a lit fountain.
  const logoFountain = new THREE.Group();
  logoFountain.name = "forkmesh-office-logo-fountain";
  logoFountain.position.set(-18, 0, -2);
  const logoBasin = new THREE.Mesh(
    new THREE.CylinderGeometry(8.2, 8.8, 1.1, 48),
    makeMaterial(THREE, "#d6e2e9", {
      metalness: 0.88,
      roughness: 0.12,
    }),
  );
  logoBasin.position.y = 0.55;
  logoFountain.add(logoBasin);
  const logoWater = new THREE.Mesh(
    new THREE.CylinderGeometry(7.55, 7.55, 0.18, 48),
    makeMaterial(THREE, "#6fd9ff", {
      transparent: true,
      opacity: 0.72,
      emissive: "#1a789d",
      emissiveIntensity: 0.55,
      metalness: 0.15,
      roughness: 0.08,
    }),
  );
  logoWater.position.y = 1.12;
  logoFountain.add(logoWater);
  const fountainGlow = new THREE.PointLight("#91eaff", 4.2, 25, 1.7);
  fountainGlow.position.set(0, 4.8, 0);
  logoFountain.add(fountainGlow);
  const reflectionTarget = new THREE.WebGLCubeRenderTarget(
    compactRenderer ? 128 : 256,
    {
      generateMipmaps: true,
      minFilter: THREE.LinearMipmapLinearFilter,
    },
  );
  const reflectionCamera = new THREE.CubeCamera(
    0.2,
    180,
    reflectionTarget,
  );
  reflectionCamera.name = "forkmesh-office-logo-reflection-camera";
  reflectionCamera.userData.logoCaptureCount = 0;
  reflectionCamera.userData.logoCapturePolicy = "dirty-idle-once";
  reflectionCamera.position.y = 5.1;
  logoFountain.add(reflectionCamera);
  const chrome = new THREE.MeshPhysicalMaterial({
    // A neutral mid-silver base leaves headroom for the live cube map. Nearly
    // white metal clipped those reflected lobby greens and dark window bands
    // into a flat white surface under ACES tone mapping.
    color: "#aeb9c8",
    metalness: 1,
    roughness: 0.045,
    clearcoat: 1,
    clearcoatRoughness: 0.02,
    envMap: reflectionTarget.texture,
    envMapIntensity: 1.65,
  });
  const darkChrome = new THREE.MeshPhysicalMaterial({
    color: "#05070b",
    metalness: 1,
    roughness: 0.06,
    clearcoat: 1,
    envMap: reflectionTarget.texture,
    envMapIntensity: 1.5,
  });
  const chromeCube = new THREE.Group();
  chromeCube.name = "forkmesh-reflective-fm-cube";
  // The resting view presents the F/M corner together. Motion-enabled clients
  // then continue from this pose around the same world-vertical axis.
  chromeCube.rotation.y = 0;
  // Only this outer mount animates. Its Y rotation is the single vertical
  // spindle through the fountain; the inner edge-balanced tilt never changes.
  const chromeMark = new THREE.Group();
  chromeMark.name = "forkmesh-reflective-fm-cube-fixed-tilt";
  chromeCube.add(chromeMark);
  const logoHalfSize = 2.92;
  const logoSupportTopY = 2.4;
  // Align one body diagonal with world-up. The selected lower corner therefore
  // remains exactly over the single support even while the outer mount spins.
  const logoLowerCorner = new THREE.Vector3(-1, -1, -1).normalize();
  chromeMark.quaternion.setFromUnitVectors(
    logoLowerCorner,
    new THREE.Vector3(0, -1, 0),
  );
  chromeCube.position.y = logoSupportTopY + logoHalfSize * Math.sqrt(3);
  reflectionCamera.position.y = chromeCube.position.y;
  const logoPiece = (
    parent,
    width,
    height,
    depth,
    x,
    y,
    z,
    rotationZ = 0,
  ) => {
    const piece = new THREE.Mesh(
      new THREE.BoxGeometry(width, height, depth),
      chrome,
    );
    piece.position.set(x, y, z);
    piece.rotation.z = rotationZ;
    parent.add(piece);
    return piece;
  };

  const addFLogoFace = (z, rotationY) => {
    const face = new THREE.Group();
    face.name =
      `forkmesh-reflective-f-face-${z > 0 ? "front" : "back"}`;
    face.position.z = z;
    face.rotation.y = rotationY;
    logoPiece(face, 0.82, 4.9, 0.48, -1.64, -0.04, 0);
    logoPiece(face, 4.35, 0.82, 0.48, 0.12, 2.0, 0);
    logoPiece(face, 3.45, 0.82, 0.48, -0.33, 0.22, 0);
    chromeMark.add(face);
  };
  addFLogoFace(2.68, 0);
  addFLogoFace(-2.68, Math.PI);

  const addMLogoFace = (x, rotationY) => {
    const face = new THREE.Group();
    face.name =
      `forkmesh-reflective-m-face-${x > 0 ? "right" : "left"}`;
    face.position.x = x;
    face.rotation.y = rotationY;
    logoPiece(face, 0.82, 4.9, 0.48, -1.72, -0.04, 0);
    logoPiece(face, 0.82, 4.9, 0.48, 1.72, -0.04, 0);
    const addDiagonal = (fromX, fromY, toX, toY) => {
      const dx = toX - fromX;
      const dy = toY - fromY;
      logoPiece(
        face,
        0.82,
        Math.hypot(dx, dy),
        0.48,
        (fromX + toX) / 2,
        (fromY + toY) / 2,
        0,
        Math.atan2(-dx, dy),
      );
    };
    addDiagonal(-1.72, 2.0, 0, 0.08);
    addDiagonal(0, 0.08, 1.72, 2.0);
    chromeMark.add(face);
  };
  addMLogoFace(2.68, Math.PI / 2);
  addMLogoFace(-2.68, -Math.PI / 2);

  // Rounded mirrored top and bottom plates match the supplied cube silhouette.
  // The branch graph is built into the Shape as seven negative-space paths:
  // four circular nodes and three connecting slots. Extruding that Shape
  // leaves true holes through both chrome plates, including reflective inner
  // bevels, instead of placing dark decals over otherwise solid metal.
  const logoPanelShape = new THREE.Shape();
  const panelSize = 5.72;
  const panelHalf = panelSize / 2;
  const panelRadius = 0.34;
  logoPanelShape.moveTo(-panelHalf + panelRadius, -panelHalf);
  logoPanelShape.lineTo(panelHalf - panelRadius, -panelHalf);
  logoPanelShape.quadraticCurveTo(
    panelHalf,
    -panelHalf,
    panelHalf,
    -panelHalf + panelRadius,
  );
  logoPanelShape.lineTo(panelHalf, panelHalf - panelRadius);
  logoPanelShape.quadraticCurveTo(
    panelHalf,
    panelHalf,
    panelHalf - panelRadius,
    panelHalf,
  );
  logoPanelShape.lineTo(-panelHalf + panelRadius, panelHalf);
  logoPanelShape.quadraticCurveTo(
    -panelHalf,
    panelHalf,
    -panelHalf,
    panelHalf - panelRadius,
  );
  logoPanelShape.lineTo(-panelHalf, -panelHalf + panelRadius);
  logoPanelShape.quadraticCurveTo(
    -panelHalf,
    -panelHalf,
    -panelHalf + panelRadius,
    -panelHalf,
  );
  logoPanelShape.closePath();
  const topNodes = [
    [-1.8, -1.3],
    [0, -1.8],
    [1.7, -0.4],
    [0.2, 1.7],
  ];
  const topBranches = [
    [[-1.8, -1.3], [0, -1.8]],
    [[0, -1.8], [1.7, -0.4]],
    [[0, -1.8], [0.2, 1.7]],
  ];
  const logoNodeCutoutRadius = 0.5;
  topNodes.forEach(([x, z]) => {
    const hole = new THREE.Path();
    hole.absarc(x, z, logoNodeCutoutRadius, 0, Math.PI * 2, false);
    logoPanelShape.holes.push(hole);
  });
  topBranches.forEach(([[x1, z1], [x2, z2]]) => {
    const dx = x2 - x1;
    const dz = z2 - z1;
    const length = Math.hypot(dx, dz);
    const ux = dx / length;
    const uz = dz / length;
    const halfWidth = 0.16;
    const nx = -uz * halfWidth;
    const nz = ux * halfWidth;
    // A hairline of chrome between independently triangulated holes avoids
    // overlapping Shape holes (which Earcut cannot represent reliably) while
    // remaining visually continuous beneath the panel's bevel.
    const trim = logoNodeCutoutRadius + 0.018;
    const sx = x1 + ux * trim;
    const sz = z1 + uz * trim;
    const ex = x2 - ux * trim;
    const ez = z2 - uz * trim;
    const hole = new THREE.Path();
    hole.moveTo(sx + nx, sz + nz);
    hole.lineTo(ex + nx, ez + nz);
    hole.lineTo(ex - nx, ez - nz);
    hole.lineTo(sx - nx, sz - nz);
    hole.closePath();
    logoPanelShape.holes.push(hole);
  });
  const logoPanelGeometry = new THREE.ExtrudeGeometry(logoPanelShape, {
    depth: 0.36,
    steps: 1,
    curveSegments: 16,
    bevelEnabled: true,
    bevelSegments: 2,
    bevelSize: 0.08,
    bevelThickness: 0.08,
  });
  logoPanelGeometry.translate(0, 0, -0.18);
  logoPanelGeometry.rotateX(Math.PI / 2);
  for (const faceY of [-2.68, 2.68]) {
    const panel = new THREE.Mesh(logoPanelGeometry, chrome);
    panel.name =
      `forkmesh-reflective-fm-panel-${faceY > 0 ? "top" : "bottom"}`;
    panel.position.y = faceY;
    panel.userData.logoThroughCutouts = logoPanelShape.holes.length;
    chromeMark.add(panel);
  }
  const logoContactPoint = new THREE.Mesh(
    new THREE.OctahedronGeometry(0.2, 0),
    chrome,
  );
  logoContactPoint.name = "forkmesh-reflective-fm-cube-contact-point";
  logoContactPoint.position.set(
    -logoHalfSize,
    -logoHalfSize,
    -logoHalfSize,
  );
  chromeMark.add(logoContactPoint);
  const logoSupport = new THREE.Mesh(
    new THREE.CylinderGeometry(0.2, 0.28, 1.28, 20),
    darkChrome,
  );
  logoSupport.name = "forkmesh-reflective-fm-cube-support";
  logoSupport.position.y = logoSupportTopY - 0.64;
  logoFountain.add(logoSupport);
  logoFountain.add(chromeCube);
  for (const [x, y, z, intensity] of [
    [-7, 9, 5, 4.8],
    [6, 7, -5, 3.8],
    [0, 12, 2, 3.2],
  ]) {
    const highlight = new THREE.PointLight(
      x < 0 ? "#dff7ff" : "#ffffff",
      intensity,
      28,
      1.45,
    );
    highlight.position.set(x, y, z);
    logoFountain.add(highlight);
  }
  officeInterior.add(logoFountain);
  let officeLobbyPlayerMoving = false;
  let logoReflectionDirty = true;
  let logoReflectionWasInLobby = false;
  let logoReflectionWasBusy = false;
  let logoReflectionEligibleAt = Infinity;
  // One cube-map refresh renders the scene six times. Treat it as an idle
  // snapshot, not a recurring animation: entry and motion only mark the map
  // dirty, then one capture runs after the visitor has settled. This keeps the
  // Office and the local avatar legible in the chrome without a one-second
  // render spike or visible reflection flash while walking.
  const logoReflectionSettleMs = compactRenderer ? 1400 : 900;
  reflectionCamera.userData.logoCaptureSettleMs = logoReflectionSettleMs;
  animated.push((time) => {
    chromeCube.rotation.y = time * 0.00022;
    logoWater.rotation.y = -time * 0.00012;
  });
  function updateOfficeLogoReflection(time) {
    const inLobby =
      officeSceneMode === "lobby" &&
      officeCurrentFloorId === "lobby" &&
      !officeElevatorRide;
    if (!inLobby) {
      logoReflectionWasInLobby = false;
      logoReflectionWasBusy = false;
      logoReflectionDirty = true;
      logoReflectionEligibleAt = Infinity;
      return;
    }
    if (!logoReflectionWasInLobby) {
      logoReflectionWasInLobby = true;
      logoReflectionDirty = true;
      logoReflectionEligibleAt = time + logoReflectionSettleMs;
      return;
    }
    const busy =
      officeLobbyPlayerMoving ||
      primaryPointerId !== null ||
      pinchActive;
    if (busy) {
      logoReflectionDirty = true;
      logoReflectionWasBusy = true;
      logoReflectionEligibleAt = time + logoReflectionSettleMs;
      return;
    }
    if (logoReflectionWasBusy) {
      logoReflectionWasBusy = false;
      logoReflectionEligibleAt = time + logoReflectionSettleMs;
      return;
    }
    if (logoReflectionDirty && time >= logoReflectionEligibleAt) {
      const cubeWasVisible = chromeCube.visible;
      const playerWasVisible = player.visible;
      chromeCube.visible = false;
      // First-person mode normally hides the local body from the main camera.
      // The reflection camera is independent, so reveal it for this capture
      // and restore the exact prior state immediately afterwards.
      player.visible = true;
      try {
        reflectionCamera.update(renderer, scene);
      } finally {
        player.visible = playerWasVisible;
        chromeCube.visible = cubeWasVisible;
      }
      logoReflectionDirty = false;
      logoReflectionEligibleAt = Infinity;
      reflectionCamera.userData.logoCaptureCount += 1;
      reflectionCamera.userData.logoCapturedPlayer = true;
    }
  }

  // One physical selector rides inside the panoramic car. Access colors are
  // updated from the server-derived grant set; locked buttons stay visible
  // but cannot initiate travel.
  const officeElevatorButtons = [];
  const officeElevatorShaft = new THREE.Group();
  officeElevatorShaft.name = "forkmesh-office-glass-elevator-shaft";
  officeElevatorShaft.position.set(
    OFFICE_ELEVATOR_CENTER_X,
    0,
    OFFICE_ELEVATOR_CENTER_Z,
  );
  const elevatorShaftFrame = makeMaterial(THREE, "#8ccfbd", {
    metalness: 0.82,
    roughness: 0.2,
  });
  const elevatorCarGlass = makeMaterial(THREE, "#c9fff3", {
    transparent: true,
    opacity: 0.13,
    metalness: 0,
    roughness: 0.5,
    depthWrite: false,
  });
  for (const [x, z] of [
    [-5, -4],
    [5, -4],
    [-5, 4],
    [5, 4],
  ]) {
    const beam = new THREE.Mesh(
      new THREE.BoxGeometry(0.24, OFFICE_TOWER_HEIGHT, 0.24),
      elevatorShaftFrame,
    );
    beam.position.set(x, OFFICE_TOWER_HEIGHT / 2, z);
    officeElevatorShaft.add(beam);
  }
  // The moving car supplies the only glass envelope. Full-height shaft panes
  // created a second parallel layer a few inches away, so their transparent
  // sort order changed whenever the rider turned the camera.
  officeInterior.add(officeElevatorShaft);

  const officeElevatorCar = new THREE.Group();
  officeElevatorCar.name = "forkmesh-office-glass-elevator-car";
  officeElevatorCar.position.set(
    OFFICE_ELEVATOR_CENTER_X,
    0,
    OFFICE_ELEVATOR_CENTER_Z,
  );
  const elevatorCarFloor = new THREE.Mesh(
    new THREE.BoxGeometry(9.4, 0.32, 7.4),
    makeMaterial(THREE, "#203b35", {
      metalness: 0.64,
      roughness: 0.32,
    }),
  );
  elevatorCarFloor.position.y = 0.16;
  officeElevatorCar.add(elevatorCarFloor);
  const elevatorCarCeiling = new THREE.Mesh(
    new THREE.BoxGeometry(9.4, 0.22, 7.4),
    elevatorShaftFrame,
  );
  elevatorCarCeiling.position.y = 7.15;
  officeElevatorCar.add(elevatorCarCeiling);
  for (const [geometry, x, z] of [
    [new THREE.BoxGeometry(0.14, 6.8, 7.25), -4.62, 0],
    [new THREE.BoxGeometry(0.14, 6.8, 7.25), 4.62, 0],
    [new THREE.BoxGeometry(9.25, 6.8, 0.14), 0, 3.55],
  ]) {
    const pane = new THREE.Mesh(geometry, elevatorCarGlass);
    pane.position.set(x, 3.65, z);
    officeElevatorCar.add(pane);
  }
  const officeElevatorDoors = [-1, 1].map((side) => {
    const door = new THREE.Mesh(
      new THREE.BoxGeometry(4.45, 6.65, 0.12),
      makeMaterial(THREE, "#d8fff6", {
        transparent: true,
        opacity: 0.24,
        metalness: 0,
        roughness: 0.48,
        depthWrite: false,
      }),
    );
    door.position.set(side * 4.36, 3.62, -3.56);
    officeElevatorCar.add(door);
    return door;
  });
  const elevatorCabinLight = new THREE.Mesh(
    new THREE.BoxGeometry(5.8, 0.08, 0.3),
    officeFloorAccent,
  );
  elevatorCabinLight.position.set(0, 6.92, -1.2);
  officeElevatorCar.add(elevatorCabinLight);
  officeInterior.add(officeElevatorCar);

  const elevatorPanelGeometry = new THREE.BoxGeometry(5.4, 5.2, 0.4);
  const elevatorPanelMaterial = makeMaterial(THREE, "#172224", {
    metalness: 0.8,
    roughness: 0.2,
  });
  const elevatorButtonGeometry = new THREE.BoxGeometry(1.25, 0.72, 0.24);
  const elevatorButtonMaterials = new Map(
    OFFICE_FLOORS.map((destination) => {
      const texture = canvasTexture(THREE, 192, 112, (context) => {
        context.fillStyle = "#10261f";
        context.fillRect(0, 0, 192, 112);
        context.strokeStyle = "#8ff1c3";
        context.lineWidth = 8;
        context.strokeRect(5, 5, 182, 102);
        context.fillStyle = "#effff8";
        context.textAlign = "center";
        context.textBaseline = "middle";
        context.font =
          '900 52px "ForkMesh Mono", ui-monospace, monospace';
        context.fillText(String(destination.level + 1), 96, 39);
        context.fillStyle = "#b9f8da";
        context.font =
          '800 17px "ForkMesh Mono", ui-monospace, monospace';
        const teamLabel = (
          destination.team ||
          (destination.id === "rooftop" ? "ROOF" : destination.label)
        )
          .replaceAll("-", " ")
          .toUpperCase();
        context.fillText(teamLabel, 96, 85, 172);
      });
      return [
        destination.id,
        new THREE.MeshStandardMaterial({
          color: "#2d8063",
          map: texture,
          emissive: "#1f9c6a",
          emissiveIntensity: 0.9,
          metalness: 0.46,
          roughness: 0.35,
        }),
      ];
    }),
  );
  const elevatorPanel = new THREE.Group();
  elevatorPanel.name = "forkmesh-office-elevator-cabin-panel";
  elevatorPanel.position.set(4.34, 3.3, -0.6);
  elevatorPanel.rotation.y = -Math.PI / 2;
  const panelBody = new THREE.Mesh(
    elevatorPanelGeometry,
    elevatorPanelMaterial,
  );
  elevatorPanel.add(panelBody);
  OFFICE_FLOORS.forEach((destination, index) => {
    const button = new THREE.Mesh(
      elevatorButtonGeometry,
      elevatorButtonMaterials.get(destination.id),
    );
    const column = index % 2;
    const rowFromBottom = Math.floor(index / 2);
    // Conventional lift ordering: floor 1 starts at the lower-left, rises
    // left-to-right, and the rooftop ends at the top.
    button.position.set(
      (column - 0.5) * 2.2,
      -1.65 + rowFromBottom * 0.78,
      0.3,
    );
    button.name = `forkmesh-office-elevator-button-${destination.id}`;
    button.userData.interactive = "office-elevator-floor";
    button.userData.officeFloorId = destination.id;
    button.userData.officeFloorLevel = destination.level;
    button.userData.officeFloorNumber = destination.level + 1;
    button.userData.officeFloorTeamLabel = (
      destination.team ||
      (destination.id === "rooftop" ? "ROOF" : destination.label)
    )
      .replaceAll("-", " ")
      .toUpperCase();
    interactive.push(button);
    elevatorPanel.add(button);
    officeElevatorButtons.push(button);
  });
  officeElevatorCar.add(elevatorPanel);
  const neighborhoodHomes = new Map();
  const nodeInfrastructure = new Map();
  const botAgents = new Map();
  const loungeMembers = new Map();
  // Public account facts the member directory publishes but a live presence
  // frame never carries — joined date and total active time — keyed by
  // lowercased display name and refreshed by updateMemberLounge. Every avatar
  // drawn for a signed-in account wears them, so nobody's chest badge shows
  // less than their campfire bench figure does.
  const memberFacts = new Map();
  const repositoryPortals = new Map();
  const repositoryPortalBornAt = new Map();
  const emoteSprites = [];
  const rewardFlights = [];
  const pushSurges = [];
  const serveFlights = [];
  const forkbotWanderTarget = new THREE.Vector3(...FORKBOT_HOME);
  let forkbotNextWanderAt = 0;
  let forkbotGreeting = null;
  let forkbotExcitement = null;
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
  // Rolling 0..1 measure of how much the mouse is actually moving, decayed
  // between samples. It only drives the local avatar's antenna blink rate;
  // nothing about it is sent to other visitors.
  let pointerEnergy = 0;
  let pointerEnergyAt = 0;
  let pointerEnergyX = 0;
  let pointerEnergyY = 0;
  let lastMovementEmit = 0;
  let lastPosition = player.position.clone();
  let wasWalking = false;
  let cameraFocus = null;
  let officeZoneState = "distant";
  let focusedRepositoryKey = "";
  let cameraZoom = 1;
  let firstPersonZoom = 1;
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
  // Set while the player is riding a swing: the avatar is glued to the moving
  // seat every frame until a second click — or any movement input — hops off.
  let swingRide = null;
  // 0..1 from the shell's swing-speed slider; maps onto the pendulum amplitude.
  let swingSpeedLevel = 0.55;
  let primaryPointerId = null;
  let draggedCampfireLog = null;
  let pointerGestureMoved = false;
  let lastGestureDragged = false;
  let pinchStartDistance = 0;
  let pinchStartZoom = cameraZoom;
  let pinchActive = false;
  let lightLevel = LIGHT_LEVEL_DEFAULT;
  let officeSceneMode = "town";
  let officeCurrentFloorId = "lobby";
  let officeChairSeat = null;
  let officeFloorAccess = normalizeOfficeFloorAccess();
  let officeFloorHandler = null;
  let officeElevatorRide = null;
  let officeAttendance = { visits: [] };
  let officeReceptionWasNear = false;
  let officeReceptionLastTipAt = -Infinity;
  let officeReceptionGuestTipIndex = 0;
  let officeReceptionMemberTipIndex = 0;
  let selfSecurityDetails = { ip: "", userAgent: "" };
  let selfSecurityBadgeVisible = true;
  // main dropped its own selectedLandmark when the floating landmark labels went
  // away (adhoc #243); the Office still tracks it to frame the camera on entry.
  let selectedLandmark = "";
  let officeLocalParticipantId = "";
  let lastOfficeMovementEmit = 0;
  let officeWasMoving = false;
  let officeExitPending = false;
  let officeDoorwayEntryPending = false;
  let officeDoorwayEntryArmed = true;
  let officeExitHandler = null;
  const weather = createWeather(THREE, scene);

  function updateWorldEnvironment() {
    const state = {
      background: new THREE.Color(DAYLIGHT_ENVIRONMENT.background),
      hemiSky: new THREE.Color(DAYLIGHT_ENVIRONMENT.hemiSky),
      hemiGround: new THREE.Color(DAYLIGHT_ENVIRONMENT.hemiGround),
      sun: new THREE.Color(DAYLIGHT_ENVIRONMENT.sun),
      sunPower: DAYLIGHT_ENVIRONMENT.sunPower,
      hemiPower: DAYLIGHT_ENVIRONMENT.hemiPower,
      exposure: DAYLIGHT_ENVIRONMENT.exposure,
    };
    const overlay = LOCAL_ENVIRONMENT_OVERLAYS[currentTheme];
    if (overlay) {
      const tint = new THREE.Color(overlay.tint);
      state.background.lerp(tint, overlay.tintStrength);
      state.hemiSky.lerp(tint, overlay.tintStrength * 0.42);
      state.sun.lerp(tint, overlay.tintStrength * 0.18);
    }
    scene.background.copy(state.background);
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

  function movementSpeedForInput(input = {}, delta = 0) {
    const topSpeed = PLAYER_MAX_SPEED * moveSpeedScale;
    const inputStrength = Math.max(0, Number(input.inputStrength) || 0);
    keyboardMovementSpeed =
      input.keyboardActive
        ? Math.min(
            topSpeed,
            keyboardMovementSpeed +
              PLAYER_ACCELERATION * moveAccelScale * Math.max(0, delta),
          )
        : Number(input.touchStrength) > 0
          ? topSpeed * inputStrength
          : baseMoveSpeed();
    return keyboardMovementSpeed;
  }

  function officeAvatarLocalPosition(
    avatar,
    target = new THREE.Vector3(),
    { entry = false } = {},
  ) {
    if (!avatar) return target.set(0, 0, 0);
    avatar.getWorldPosition(target);
    officeInterior.worldToLocal(target);
    if (entry) {
      // Admission may resolve one frame after the physical threshold crossing.
      // Keep that live pose at the doorway instead of letting latency place the
      // visitor beyond the front face before interior collision takes over.
      const doorwayEdge = Math.min(
        OFFICE_FRONT_Z + OFFICE_AVATAR_RADIUS,
        OFFICE_INTERIOR_EXIT_Z,
      );
      target.z = Math.min(target.z, doorwayEdge);
    }
    return target;
  }

  function applyOfficeAvatarLocalPosition(avatar, localPosition) {
    if (!avatar || !localPosition) return;
    const worldPosition = officeInterior.localToWorld(localPosition.clone());
    avatar.parent?.worldToLocal?.(worldPosition);
    avatar.position.copy(worldPosition);
  }

  function nearestOfficeWalkablePosition(floorId, localPosition) {
    const origin = localPosition.clone();
    if (
      officeInteriorPointIsWalkable(
        floorId,
        origin.x,
        origin.z,
        OFFICE_AVATAR_RADIUS,
      )
    ) {
      return origin;
    }
    // Meeting poses cluster around the Marketing table, which is intentionally
    // collidable in the free-walking floor. Search outward in small rings so
    // leaving a room resumes at the closest valid point instead of trapping
    // the continuous player inside that table.
    for (let distance = 0.75; distance <= 24; distance += 0.75) {
      for (let index = 0; index < 24; index += 1) {
        const angle = (index / 24) * Math.PI * 2;
        const x = origin.x + Math.cos(angle) * distance;
        const z = origin.z + Math.sin(angle) * distance;
        if (
          officeInteriorPointIsWalkable(
            floorId,
            x,
            z,
            OFFICE_AVATAR_RADIUS,
          )
        ) {
          origin.x = x;
          origin.z = z;
          return origin;
        }
      }
    }
    origin.x = 12;
    origin.z = -10;
    return origin;
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

  function setOfficeFloorHandler(handler) {
    officeFloorHandler = typeof handler === "function" ? handler : null;
  }

  function setOfficeAccess(payload = {}) {
    officeFloorAccess = normalizeOfficeFloorAccess(payload);
    officeElevatorButtons.forEach((button) => {
      const allowed = canAccessOfficeFloor(
        officeFloorAccess,
        button.userData.officeFloorId,
      );
      button.userData.officeFloorAllowed = allowed;
      button.material.color.set(allowed ? "#2d8063" : "#3b3034");
      button.material.emissive?.set?.(allowed ? "#1f9c6a" : "#6e2737");
      button.material.emissiveIntensity = allowed ? 0.9 : 0.2;
    });
    return { ...officeFloorAccess };
  }

  function setSelfSecurityDetails(details = {}) {
    selfSecurityDetails = {
      ip: String(details?.ip || "").trim().slice(0, 64),
      userAgent: String(details?.userAgent || "")
        .replace(/\s+/g, " ")
        .trim()
        .slice(0, 256),
    };
    setAvatarSecurityBadge(
      THREE,
      player,
      selfSecurityDetails,
      selfSecurityBadgeVisible,
    );
    setAvatarSecurityBadge(
      THREE,
      officeLobbyPlayer,
      selfSecurityDetails,
      selfSecurityBadgeVisible,
    );
    const localParticipant =
      officeParticipants.get(officeLocalParticipantId);
    if (localParticipant) {
      setAvatarSecurityBadge(
        THREE,
        localParticipant,
        selfSecurityDetails,
        selfSecurityBadgeVisible,
      );
    }
    return { ...selfSecurityDetails };
  }

  function setSelfSecurityBadgeVisibility(visible = true) {
    const previous = selfSecurityBadgeVisible;
    selfSecurityBadgeVisible = visible === true;
    [
      player,
      officeLobbyPlayer,
      officeParticipants.get(officeLocalParticipantId),
    ].forEach((avatar) => {
      if (avatar?.userData?.selfSecurityBadge) {
        avatar.userData.selfSecurityBadge.visible =
          selfSecurityBadgeVisible;
      }
    });
    return previous;
  }

  function setOfficeAttendance(event = {}) {
    const visits = Array.isArray(event?.visits)
      ? event.visits.slice(0, 20).map((visit) => ({
          id: String(visit?.id || "").slice(0, 32),
          account: String(visit?.account || "Contributor")
            .replace(/\s+/g, " ")
            .trim()
            .slice(0, 32),
          inAt: Math.max(0, Number(visit?.inAt) || 0),
          outAt: Math.max(0, Number(visit?.outAt) || 0),
        }))
      : [];
    officeAttendance = { visits };
    const previous = officeAttendanceBoard.material.map;
    officeAttendanceBoard.material.map =
      officeAttendanceTexture(officeAttendance);
    officeAttendanceBoard.material.needsUpdate = true;
    previous?.dispose?.();
    return {
      visits: officeAttendance.visits.map((visit) => ({ ...visit })),
    };
  }

  function travelToOfficeFloor(floorId) {
    const floor = officeFloorById(floorId);
    const avatar = player;
    const localPosition = officeAvatarLocalPosition(avatar);
    if (
      officeSceneMode === "town" ||
      officeSceneMode === "meeting" ||
      officeElevatorRide ||
      !floor ||
      !canAccessOfficeFloor(officeFloorAccess, floor.id) ||
      !officeElevatorCabinContains(
        localPosition.x,
        localPosition.z,
        OFFICE_AVATAR_RADIUS,
      )
    ) {
      return false;
    }
    if (floor.id === officeCurrentFloorId) return true;
    officeElevatorRide = {
      fromY: avatar.position.y,
      toY: officeFloorY(floor.id) + 0.38,
      floorId: floor.id,
      startedAt: performance.now(),
      // The doubled-height tower is a view, not a teleport. Give riders enough
      // time to watch Town and the sky move through the panoramic front glass.
      duration: 1400 + Math.abs(floor.level -
        (officeFloorById(officeCurrentFloorId)?.level || 0)) * 360,
    };
    cancelDash();
    jumpQueued = false;
    jumpVelocity = 0;
    onOfficeElevatorSound("depart", {
      from: officeCurrentFloorId,
      to: floor.id,
      duration: officeElevatorRide.duration,
    });
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
    // Crossing the threshold switches into the continuous lobby immediately.
    // The armed/pending pair prevents a held movement key from firing the
    // controller more than once before that scene-mode handoff completes.
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

  function constrainOfficeInteriorWalls(avatar, previousPosition) {
    if (!avatar) return false;
    const position = officeAvatarLocalPosition(avatar);
    const previous =
      previousPosition
        ? previousPosition.clone()
        : position.clone();
    if (avatar === player && previousPosition) {
      previous.set(
        previousPosition.x - OFFICE_ISLAND_CENTER[0],
        previousPosition.y - OFFICE_ISLAND_CENTER[1],
        previousPosition.z - OFFICE_ISLAND_CENTER[2],
      );
    }
    const commitPosition = () => {
      applyOfficeAvatarLocalPosition(avatar, position);
    };
    const doorClearance = OFFICE_DOOR_WIDTH / 2 - OFFICE_AVATAR_RADIUS;
    if (
      officeCurrentFloorId === "lobby" &&
      position.z > OFFICE_INTERIOR_WALL_LIMIT &&
      Math.abs(position.x) > doorClearance &&
      !officeElevatorCabinContains(
        position.x,
        position.z,
        OFFICE_AVATAR_RADIUS,
      )
    ) {
      position.z = OFFICE_INTERIOR_WALL_LIMIT;
      commitPosition();
    }
    if (
      officeCurrentFloorId === "lobby" &&
      position.z >= OFFICE_INTERIOR_EXIT_Z &&
      Math.abs(position.x) <= doorClearance
    ) {
      position.z = OFFICE_INTERIOR_EXIT_Z;
      commitPosition();
      if (!officeExitPending && officeExitHandler) {
        officeExitPending = true;
        officeExitHandler();
      }
      return true;
    }
    const walkableAt = (x, z) => {
      const inLobbyDoorway =
        officeCurrentFloorId === "lobby" &&
        Math.abs(x) <= doorClearance &&
        z >= OFFICE_INTERIOR_WALL_LIMIT &&
        z <= OFFICE_INTERIOR_EXIT_Z;
      return (
        inLobbyDoorway ||
        officeInteriorPointIsWalkable(
          officeCurrentFloorId,
          x,
          z,
          OFFICE_AVATAR_RADIUS,
        )
      );
    };
    if (!walkableAt(position.x, position.z)) {
      const attemptedX = position.x;
      const attemptedZ = position.z;
      if (walkableAt(attemptedX, previous.z)) {
        position.z = previous.z;
      } else if (walkableAt(previous.x, attemptedZ)) {
        position.x = previous.x;
      } else {
        position.x = previous.x;
        position.z = previous.z;
      }
      commitPosition();
      cancelDash();
    }
    return false;
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
    selectedLandmark = "office";
    officeExitPending = false;
    officeDoorwayEntryPending = false;
    cameraFocus = null;
    cancelDash();
    return true;
  }

  function enterOfficeLobby({ floorId = "lobby" } = {}) {
    // The tower is physically part of the World. Entry changes only the active
    // local avatar/floor controls; the campus, Town Square, sky, and other
    // visitors remain rendered around it.
    const enteringFromTown = officeSceneMode === "town";
    const leavingMeeting = officeSceneMode === "meeting";
    standUpFromOfficeChair();
    const meetingAvatar = leavingMeeting
      ? officeParticipants.get(officeLocalParticipantId)
      : null;
    const requestedFloor = officeFloorById(floorId);
    const destinationFloor =
      !enteringFromTown &&
      requestedFloor &&
      canAccessOfficeFloor(officeFloorAccess, requestedFloor.id)
        ? requestedFloor
        : officeFloorById("lobby");
    officeSceneMode = "lobby";
    officeCurrentFloorId = destinationFloor.id;
    officeElevatorRide = null;
    selectedLandmark = "office";
    currentSpace = `office-${destinationFloor.id}`;
    currentFloorY = officeFloorY(destinationFloor.id) + 0.38;
    cameraFocus = null;
    officeInterior.visible = true;
    officeSlidingDoorOpen = 1;
    if (enteringFromTown) {
      const localPosition = officeAvatarLocalPosition(
        player,
        new THREE.Vector3(),
        { entry: true },
      );
      const doorClearance =
        OFFICE_DOOR_WIDTH / 2 - OFFICE_AVATAR_RADIUS - 0.08;
      localPosition.x = clamp(
        localPosition.x,
        -doorClearance,
        doorClearance,
      );
      localPosition.y = currentFloorY;
      localPosition.z = Math.min(
        localPosition.z,
        OFFICE_INTERIOR_WALL_LIMIT - 0.72,
      );
      applyOfficeAvatarLocalPosition(player, localPosition);
    } else if (meetingAvatar) {
      const localPosition = nearestOfficeWalkablePosition(
        destinationFloor.id,
        officeAvatarLocalPosition(meetingAvatar),
      );
      localPosition.y = currentFloorY;
      applyOfficeAvatarLocalPosition(player, localPosition);
      setOfficeParticipants([]);
      officeLocalParticipantId = "";
    } else {
      player.position.y = currentFloorY;
    }
    officeElevatorCar.position.y = officeFloorY(destinationFloor.id);
    officeElevatorDoors[0].position.x = -4.36;
    officeElevatorDoors[1].position.x = 4.36;
    officeLobbyPlayer.visible = false;
    player.visible = cameraMode !== "first-person";
    lastPosition.copy(player.position);
    keyboardMovementSpeed = baseMoveSpeed();
    officeExitPending = false;
    return true;
  }

  function officeChairTransform(chairId) {
    const chair = officeChairs.get(String(chairId || ""));
    if (!chair) return null;
    return {
      position: chair.position.clone(),
      yaw: chair.rotation.y,
      floorId: String(chair.userData.officeFloorId || "marketing"),
      seatTopY:
        Number(chair.userData.officeSeatTopY) ||
        OFFICE_CHAIR_SEAT_TOP_Y,
    };
  }

  function applyOfficeChairSeatPose() {
    const chair = officeChairs.get(String(officeChairSeat?.chairId || ""));
    if (!chair) {
      officeChairSeat = null;
      return false;
    }
    const seatPoint = chair.getWorldPosition(new THREE.Vector3());
    player.parent?.worldToLocal?.(seatPoint);
    const floorId = String(
      chair.userData.officeFloorId || "marketing",
    );
    const seatTopY =
      Number(chair.userData.officeSeatTopY) ||
      OFFICE_CHAIR_SEAT_TOP_Y;
    player.position.set(
      seatPoint.x,
      seatedAvatarY(
        officeFloorY(floorId) + seatTopY,
        player.scale.x,
      ),
      seatPoint.z,
    );
    player.rotation.y = chair.rotation.y + Math.PI;
    player.userData.leftArm.rotation.x = 0;
    player.userData.rightArm.rotation.x = 0;
    applySeatedLegPose(player);
    return true;
  }

  function standUpFromOfficeChair() {
    if (!officeChairSeat) return false;
    officeChairSeat = null;
    player.position.y = currentFloorY;
    applyLegPitch(player, 0, 0);
    return true;
  }

  function sitOnOfficeChair(chairId) {
    const normalized = String(chairId || "");
    const chair = officeChairs.get(normalized);
    if (
      officeSceneMode !== "lobby" ||
      !chair ||
      String(chair.userData.officeFloorId || "marketing") !==
        officeCurrentFloorId
    ) {
      return false;
    }
    if (officeChairSeat?.chairId === normalized) {
      standUpFromOfficeChair();
      return true;
    }
    officeChairSeat = { chairId: normalized };
    cancelDash();
    cameraFocus = null;
    jumpVelocity = 0;
    jumpQueued = false;
    const seated = applyOfficeChairSeatPose();
    lastPosition.copy(player.position);
    return seated;
  }

  function applyOfficeParticipantPose(avatar, participant) {
    // The live meeting currently occupies the Marketing story. Keep this
    // explicit so DOM labels can obey floor occlusion; unlike Three.js meshes,
    // HTML overlays are not hidden by an opaque floor slab automatically.
    avatar.userData.officeFloorId = "marketing";
    const seated = participant?.pose === "seated" && participant?.chairId;
    const chair = seated ? officeChairTransform(participant.chairId) : null;
    if (chair) {
      avatar.position.copy(chair.position);
      avatar.position.y = seatedAvatarY(
        chair.position.y + OFFICE_CHAIR_SEAT_TOP_Y,
        avatar.scale.x,
      );
      avatar.rotation.y = chair.yaw + Math.PI;
      applySeatedLegPose(avatar);
    } else {
      const seed = hashNumber(participant?.id || "office-participant");
      avatar.position.set(
        clamp(Number(participant?.x) || ((seed % 7) - 3) * 0.55, -6.8, 6.8),
        officeFloorY("marketing") + 0.38,
        clamp(Number(participant?.z) || 4.15 + ((seed >> 3) % 3) * 0.35, -4.7, 4.7),
      );
      avatar.rotation.y = Number(participant?.yaw) || Math.PI;
      applyLegPitch(avatar, 0, 0);
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
      // Meeting frames carry only a name and account status, so a member in a
      // room would otherwise wear a barer chest than the same member standing
      // in the square: fill in their public joined date and active time.
      const participantIdentity = withMemberFacts({
        id,
        name: String(participant.name || "Office visitor").slice(0, 32),
        flag: "◌",
        browser: "Browser",
        os: "Device",
        status: participant.pose === "seated" ? "seated" : "in meeting",
        accountStatus: participant.accountStatus || "Guest",
        nodes: [],
      });
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
      if (id === officeLocalParticipantId) {
        setAvatarSecurityBadge(
          THREE,
          avatar,
          selfSecurityDetails,
          selfSecurityBadgeVisible,
        );
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
      officeSceneMode === "meeting" &&
      (!officeLocalParticipantId ||
        !officeParticipants.has(officeLocalParticipantId));
    const localParticipant =
      officeParticipants.get(officeLocalParticipantId);
    if (localParticipant && officeSceneMode === "meeting") {
      localParticipant.visible = cameraMode !== "first-person";
    }
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
      Math.max(0, worldBulletinEvents.length - WORLD_BULLETIN_VISIBLE_EVENTS),
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
      Math.max(0, worldBulletinEvents.length - WORLD_BULLETIN_VISIBLE_EVENTS),
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

  function setMastodonOpenButton(name, href) {
    const button = mastodonKiosk.getObjectByName(
      `forkmesh-mastodon-kiosk-open-${name}`,
    );
    if (!button) return;
    button.userData.href = href || "";
    button.visible = Boolean(href);
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
    setMastodonOpenButton("profile", mastodonKioskSnapshot?.profileURL);
    const visibleToots = mastodonKioskVisibleToots(
      mastodonKioskSnapshot,
      mastodonKioskOffset,
    );
    setMastodonOpenButton("toot-0", visibleToots[0]?.url);
    setMastodonOpenButton("toot-1", visibleToots[1]?.url);
    setMastodonOpenButton("toot-2", visibleToots[2]?.url);
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
            replies: Array.isArray(snapshot.replies)
              ? snapshot.replies.filter(Boolean)
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

  // The dial is its own small texture: a one-second countdown tick must not
  // rebuild the 1536×4096 board texture.
  function repaintMastodonCountdown() {
    const dial = mastodonKiosk.getObjectByName(
      "forkmesh-mastodon-kiosk-countdown",
    );
    if (!dial?.material) return false;
    dial.material.map?.dispose?.();
    dial.material.map = mastodonCountdownTexture(
      THREE,
      mastodonKioskCountdown.remainingMs,
      mastodonKioskCountdown.totalMs,
      mastodonKioskCountdown.loading,
    );
    dial.material.needsUpdate = true;
    return true;
  }

  function repaintMastodonLastPost() {
    const plate = mastodonKiosk.getObjectByName(
      "forkmesh-mastodon-kiosk-lastpost",
    );
    if (!plate?.material) return false;
    plate.material.map?.dispose?.();
    plate.material.map = mastodonLastPostTexture(
      THREE,
      mastodonKioskLastPostSinceMs,
    );
    plate.material.needsUpdate = true;
    return true;
  }

  function updateMastodonCountdown({
    remainingMs = MASTODON_KIOSK_REFRESH_MS,
    totalMs = MASTODON_KIOSK_REFRESH_MS,
    loading = false,
    lastPostAgoMs = null,
  } = {}) {
    let repainted = false;
    // The last-post plate only changes on a minute boundary (or when a fresh
    // snapshot moves the newest toot), so it keys on whole minutes and skips
    // the second-by-second repaints the sync clock needs.
    const since =
      Number.isFinite(Number(lastPostAgoMs)) && Number(lastPostAgoMs) >= 0
        ? Number(lastPostAgoMs)
        : null;
    const sinceKey = since === null ? "-" : String(Math.floor(since / 60_000));
    if (sinceKey !== mastodonKioskLastPostKey) {
      mastodonKioskLastPostKey = sinceKey;
      mastodonKioskLastPostSinceMs = since;
      repainted = repaintMastodonLastPost() || repainted;
    }
    const total = Math.max(1000, Number(totalMs) || MASTODON_KIOSK_REFRESH_MS);
    const remaining = clamp(Number(remainingMs) || 0, 0, total);
    const key = `${loading ? 1 : 0}:${Math.ceil(remaining / 1000)}:${total}`;
    if (key === mastodonKioskCountdownKey) return repainted;
    mastodonKioskCountdownKey = key;
    mastodonKioskCountdown = {
      remainingMs: remaining,
      totalMs: total,
      loading: Boolean(loading),
    };
    return repaintMastodonCountdown() || repainted;
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
    enterOfficeLobby({ floorId: "marketing" });
    officeSceneMode = "meeting";
    officeCurrentFloorId = "marketing";
    currentFloorY = officeFloorY("marketing") + 0.38;
    currentSpace = "office-marketing";
    officeLobbyPlayer.position.y = currentFloorY;
    officeElevatorCar.position.y = officeFloorY("marketing");
    officeLocalParticipantId = String(participantId || officeLocalParticipantId);
    officeRoomSign.userData.roomName = String(roomName || "general");
    setOfficeParticipants(participants);
    officeLobbyPlayer.visible =
      !officeLocalParticipantId ||
      !officeParticipants.has(officeLocalParticipantId);
    player.visible = false;
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
    standUpFromOfficeChair();
    const localPosition = officeAvatarLocalPosition(player);
    officeSceneMode = "town";
    officeCurrentFloorId = "lobby";
    officeElevatorRide = null;
    currentSpace = "town-square";
    localPosition.x = clamp(
      localPosition.x,
      -OFFICE_DOOR_WIDTH / 2 + OFFICE_AVATAR_RADIUS,
      OFFICE_DOOR_WIDTH / 2 - OFFICE_AVATAR_RADIUS,
    );
    localPosition.y = 0.38;
    localPosition.z = Math.max(localPosition.z, OFFICE_FRONT_Z + 0.82);
    applyOfficeAvatarLocalPosition(player, localPosition);
    lastPosition.copy(player.position);
    officeLocalParticipantId = "";
    officeWasMoving = false;
    officeExitPending = false;
    officeDoorwayEntryPending = false;
    officeLobbyPlayer.visible = false;
    player.visible = cameraMode !== "first-person";
    cameraFocus = null;
    setOfficeParticipants([]);
    officeBubbles.forEach((element) => element.remove());
    officeBubbles.clear();
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
    if (
      officeSceneMode === "lobby" &&
      officeCurrentFloorId !== "lobby"
    ) {
      return travelToOfficeFloor("lobby");
    }
    officeExitPending = false;
    officeSlidingDoorOpen = 1;
    const avatar =
      officeSceneMode === "meeting"
        ? officeParticipants.get(officeLocalParticipantId)
        : player;
    if (avatar && avatar.userData?.officePresence?.pose !== "seated") {
      avatar.rotation.y = 0;
    }
    return true;
  }

  function setCameraMode(mode) {
    const wantsFirstPerson =
      mode === true ||
      String(mode || "").trim().toLocaleLowerCase() === "first-person";
    const nextMode = wantsFirstPerson ? "first-person" : "third-person";
    if (nextMode !== cameraMode) cancelDash();
    cameraMode = nextMode;
    if (cameraMode === "first-person") {
      cameraFocus = null;
      player.visible = false;
      if (officeSceneMode === "meeting") {
        const localParticipant =
          officeParticipants.get(officeLocalParticipantId);
        if (localParticipant) localParticipant.visible = false;
      }
      playerLabel.style.opacity = "0";
      playerLabel.style.visibility = "hidden";
    } else {
      player.visible = officeSceneMode !== "meeting";
      if (officeSceneMode === "meeting") {
        const localParticipant =
          officeParticipants.get(officeLocalParticipantId);
        if (localParticipant) localParticipant.visible = true;
      }
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
    const movementSpeed = movementSpeedForInput(input, delta);
    if (walking) {
      const previousPosition = avatar.position.clone();
      avatar.position.addScaledVector(
        movement,
        movementSpeed * delta,
      );
      avatar.position.y = officeFloorY("marketing") + 0.38;
      avatar.rotation.y = Math.atan2(-movement.x, -movement.z);
      if (constrainOfficeInteriorWalls(avatar, previousPosition)) return;
    }
    const gait = walking ? Math.sin(time * 0.012) * 0.48 : 0;
    avatar.userData.leftArm.rotation.x = gait;
    avatar.userData.rightArm.rotation.x = -gait;
    applyLegPitch(avatar, -gait * GAIT_LEG_SWING, gait * GAIT_LEG_SWING);
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
    const avatar = player;
    if (officeElevatorRide) {
      officeLobbyPlayerMoving = true;
      avatar.userData.leftArm.rotation.x = 0;
      avatar.userData.rightArm.rotation.x = 0;
      applyLegPitch(avatar, 0, 0);
      animateAvatarActivity(avatar, time, delta, reducedMotion);
      return;
    }
    const input = movementInput();
    const movement = input.movement;
    if (officeChairSeat) {
      if (!movement.lengthSq() && !dashTarget && !jumpQueued) {
        officeLobbyPlayerMoving = false;
        applyOfficeChairSeatPose();
        animateAvatarActivity(avatar, time, delta, reducedMotion);
        return;
      }
      standUpFromOfficeChair();
    }
    let walking = movement.lengthSq() > 0;
    const movementSpeed = movementSpeedForInput(input, delta);
    if (walking) {
      cancelDash();
      const previousPosition = avatar.position.clone();
      avatar.position.addScaledVector(
        movement,
        movementSpeed * delta,
      );
      avatar.position.y = officeFloorY(officeCurrentFloorId) + 0.38;
      avatar.rotation.y = Math.atan2(-movement.x, -movement.z);
      if (
        constrainOfficeInteriorWalls(avatar, previousPosition)
      ) {
        return;
      }
    } else if (dashTarget) {
      const previousPosition = avatar.position.clone();
      const toTarget = new THREE.Vector3(
        dashTarget.x - avatar.position.x,
        0,
        dashTarget.z - avatar.position.z,
      );
      const remaining = toTarget.length();
      const step = PLAYER_DASH_SPEED * moveSpeedScale * delta;
      if (remaining <= Math.max(step, PLAYER_DASH_ARRIVE_DISTANCE)) {
        avatar.position.x = dashTarget.x;
        avatar.position.z = dashTarget.z;
        cancelDash();
      } else {
        toTarget.divideScalar(remaining);
        avatar.position.x += toTarget.x * step;
        avatar.position.z += toTarget.z * step;
        avatar.rotation.y = Math.atan2(-toTarget.x, -toTarget.z);
      }
      avatar.position.y = officeFloorY(officeCurrentFloorId) + 0.38;
      walking = true;
      if (constrainOfficeInteriorWalls(avatar, previousPosition)) {
        return;
      }
    }
    officeLobbyPlayerMoving = walking;
    const gait = walking ? Math.sin(time * 0.012) * 0.48 : 0;
    avatar.userData.leftArm.rotation.x = gait;
    avatar.userData.rightArm.rotation.x = -gait;
    applyLegPitch(
      avatar,
      -gait * GAIT_LEG_SWING,
      gait * GAIT_LEG_SWING,
    );
    animateAvatarActivity(avatar, time, delta, reducedMotion);
  }

  function updateOfficeElevator(time) {
    const ride = officeElevatorRide;
    if (!ride) return false;
    const progress = clamp(
      (time - ride.startedAt) / Math.max(1, ride.duration),
      0,
      1,
    );
    const eased =
      progress < 0.5
        ? 4 * progress ** 3
        : 1 - (-2 * progress + 2) ** 3 / 2;
    player.position.y =
      ride.fromY + (ride.toY - ride.fromY) * eased;
    officeElevatorCar.position.y = player.position.y - 0.38;
    const closed =
      progress < 0.16
        ? progress / 0.16
        : progress > 0.84
          ? (1 - progress) / 0.16
          : 1;
    officeElevatorDoors[0].position.x = -4.36 + 2.12 * closed;
    officeElevatorDoors[1].position.x = 4.36 - 2.12 * closed;
    currentFloorY = player.position.y;
    if (progress < 1) return true;
    officeCurrentFloorId = ride.floorId;
    currentFloorY = officeFloorY(ride.floorId) + 0.38;
    player.position.y = currentFloorY;
    officeElevatorCar.position.y = officeFloorY(ride.floorId);
    officeElevatorDoors[0].position.x = -4.36;
    officeElevatorDoors[1].position.x = 4.36;
    currentSpace = `office-${ride.floorId}`;
    officeElevatorRide = null;
    onOfficeElevatorSound("arrive", {
      floor: ride.floorId,
      duration: ride.duration,
    });
    return true;
  }

  // Sitting is a local pose, not a teleport: the avatar lands on the plank it
  // was clicked on, faces the flames and keeps that pose until it moves again.
  function sitOnCampfireBench(seat) {
    dismountSwing({ relocate: false });
    const seatPoint = seat.getWorldPosition(new THREE.Vector3());
    benchSeat = {
      // Hips on the plank, not feet: the avatar drops until its thighs rest on
      // the seat and its shins hang off the front edge down to the ground.
      position: new THREE.Vector3(
        seatPoint.x,
        seatedAvatarY(
          seatPoint.y + CAMPFIRE_SEAT_HALF_THICKNESS,
          player.scale.x,
        ),
        seatPoint.z,
      ),
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
    applySeatedLegPose(player);
  }

  function standUpFromBench() {
    if (!benchSeat) return;
    benchSeat = null;
    player.position.y = currentFloorY;
    applyLegPitch(player, 0, 0);
  }

  function swingRideAmplitude() {
    return (
      SWING_MIN_AMPLITUDE +
      (SWING_MAX_AMPLITUDE - SWING_MIN_AMPLITUDE) * swingSpeedLevel
    );
  }

  function setSwingSpeed(value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) return;
    swingSpeedLevel = Math.min(1, Math.max(0.1, numeric / 100));
  }

  function swingSeatRestPoint(index) {
    return swingStates[index].anchor.getWorldPosition(new THREE.Vector3());
  }

  // Riding is a local pose like the campfire sit, but on a moving seat: the
  // avatar is re-glued to the plank every frame, so both camera modes follow
  // the arc for free. A second click on the same swing hops off.
  function rideSwing(index) {
    if (officeSceneMode !== "town") return;
    const swing = swingStates[index];
    if (!swing) return;
    if (swingRide?.index === index) {
      dismountSwing();
      return;
    }
    if (swing.remoteId) {
      onSwingRide({
        riding: Boolean(swingRide),
        seat: swingRide ? swingRide.index : -1,
        denied: true,
      });
      return;
    }
    standUpFromBench();
    swingRide = { index };
    cancelDash();
    cameraFocus = null;
    jumpVelocity = 0;
    jumpQueued = false;
    applySwingRidePose();
    lastPosition.copy(player.position);
    onMovement({
      x: Number(player.position.x.toFixed(2)),
      y: Number(player.position.y.toFixed(2)),
      z: Number(player.position.z.toFixed(2)),
      heading: Number(player.rotation.y.toFixed(3)),
      space: currentSpace,
      moving: false,
      activity: SWING_RIDING_ACTIVITY,
    });
    onSwingRide({ riding: true, seat: index, denied: false });
  }

  function applySwingRidePose() {
    if (!swingRide) return;
    const swing = swingStates[swingRide.index];
    const seatTop = swing.seat.getWorldPosition(new THREE.Vector3());
    player.position.set(
      seatTop.x,
      seatedAvatarY(seatTop.y + SWING_SEAT_HALF_THICKNESS, player.scale.x),
      seatTop.z,
    );
    // Yaw first, then pitch in the yawed frame: with the default XYZ order the
    // lean below would be a world-X pitch, which on a swing set that faces the
    // fountain at an angle reads as the rider rocking side to side.
    player.rotation.order = "YXZ";
    player.rotation.y = swingSet.rotation.y;
    // Lean with the ropes so the body traces the arc instead of staying bolt
    // upright at the peaks.
    player.rotation.x = swing.pivot.rotation.x;
    player.userData.leftArm.rotation.x = SWING_ARM_HOLD_PITCH;
    player.userData.rightArm.rotation.x = SWING_ARM_HOLD_PITCH;
    applySeatedLegPose(player);
  }

  function dismountSwing({ relocate = true } = {}) {
    if (!swingRide) return;
    const index = swingRide.index;
    swingRide = null;
    player.rotation.x = 0;
    player.userData.leftArm.rotation.x = 0;
    player.userData.rightArm.rotation.x = 0;
    applyLegPitch(player, 0, 0);
    if (relocate) {
      // Step off just in front of the seat's rest spot rather than standing
      // up inside the moving plank.
      const rest = swingSeatRestPoint(index);
      const heading = swingSet.rotation.y;
      player.position.set(
        rest.x - Math.sin(heading) * 1.1,
        currentFloorY,
        rest.z - Math.cos(heading) * 1.1,
      );
      player.rotation.y = heading;
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
    } else {
      player.position.y = currentFloorY;
    }
    onSwingRide({ riding: false, seat: -1, denied: false });
  }

  // The world map's Campfire spot is a trip home: it puts the avatar on the
  // bench that carries this member's name and holds the same seated pose
  // clicking the plank gives. Guests — and members the directory has not
  // seated yet — take the bench the circle always keeps open. Leaving the
  // Office stays a deliberate walk through its door, so this refuses while
  // the interior is open rather than teleporting out of it.
  function returnToCampfireBench(name) {
    if (officeSceneMode !== "town") return false;
    const benches = campfire.userData.seatBenches || [];
    if (!benches.length) return false;
    const owned = campfire.userData.seatByName?.get(
      String(name || "").trim().toLowerCase(),
    );
    const index = Number.isInteger(owned) ? owned : benches.length - 1;
    const seat = benches[index]?.seat;
    if (!seat) return false;
    currentSpace = "town-square";
    currentFloorY = 0.38;
    focusedRepositoryKey = "";
    sitOnCampfireBench(seat);
    return true;
  }

  function walkPlayer(delta, time) {
    const previousHorizontalPosition = player.position.clone();
    const {
      keyboardActive,
      touchStrength,
      inputStrength,
      movement,
    } = movementInput();
    if (swingRide) {
      // Like the bench below: relocation ends the ride, and so does any
      // movement input, which keeps WASD as the universal "get off" gesture
      // alongside the second click on the seat.
      const displaced =
        player.position.distanceToSquared(
          swingSeatRestPoint(swingRide.index),
        ) > 36;
      if (!displaced && !movement.lengthSq() && !dashTarget && !jumpQueued) {
        applySwingRidePose();
        player.userData.inputEnergy = decayedPointerEnergy(performance.now());
        animateAvatarActivity(player, time, delta, reducedMotion);
        wasWalking = false;
        return;
      }
      dismountSwing({ relocate: false });
    }
    if (benchSeat) {
      // Anything that relocates the avatar (a space change, a teleport) also
      // ends the sit; otherwise the held pose would drag it back to the bench.
      const displaced =
        player.position.distanceToSquared(benchSeat.position) > 9;
      if (!displaced && !movement.lengthSq() && !dashTarget && !jumpQueued) {
        applyBenchSeatPose();
        player.userData.inputEnergy = decayedPointerEnergy(performance.now());
        animateAvatarActivity(player, time, delta, reducedMotion);
        wasWalking = false;
        return;
      }
      standUpFromBench();
    }
    const officeCampus =
      officeCampusSurfaceContains(player.position.x, player.position.z);
    if (officeCampus) {
      jumpQueued = false;
      jumpVelocity = 0;
      player.position.y = currentFloorY;
    } else if (jumpQueued && player.position.y <= currentFloorY + 0.02) {
      jumpVelocity = 5.4;
      jumpQueued = false;
    }
    if (!officeCampus) {
      jumpVelocity -= 14 * delta;
      player.position.y += jumpVelocity * delta;
    }
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
      movementSpeedForInput(
        { keyboardActive, touchStrength, inputStrength },
        delta,
      );
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

    if (
      !worldWalkSurfaceContains(
        player.position.x,
        player.position.z,
        OFFICE_AVATAR_RADIUS,
      )
    ) {
      player.position.x = previousHorizontalPosition.x;
      player.position.z = previousHorizontalPosition.z;
      cancelDash();
    }
    constrainTownOfficeWalls(previousHorizontalPosition);
    const gait = walking ? Math.sin(time * 0.012) * 0.52 : 0;
    player.userData.leftArm.rotation.x = gait;
    player.userData.rightArm.rotation.x = -gait;
    applyLegPitch(player, -gait * GAIT_LEG_SWING, gait * GAIT_LEG_SWING);
    if (jumpVelocity === 0) {
      player.position.y += walking ? Math.abs(Math.sin(time * 0.012)) * 0.035 : 0;
    }
    player.userData.inputEnergy = decayedPointerEnergy(performance.now());
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
    // Swing occupancy is re-derived every frame from where visitors stand: a
    // remote visitor parked on a seat's rest spot is riding that swing, which
    // is what caps the ride at three people and lets a local click on a taken
    // seat be refused.
    swingStates.forEach((swing) => {
      swing.remoteId = "";
    });
    const swingRestPoints = swingStates.map((swing) =>
      swing.anchor.getWorldPosition(new THREE.Vector3()),
    );
    remotePlayers.forEach((avatar, remoteId) => {
      avatar.position.lerp(avatar.userData.targetPosition, 1 - Math.pow(0.002, delta));
      let headingDelta = avatar.userData.targetHeading - avatar.rotation.y;
      headingDelta = Math.atan2(Math.sin(headingDelta), Math.cos(headingDelta));
      avatar.rotation.y += headingDelta * (1 - Math.pow(0.01, delta));
      const walking = avatar.position.distanceToSquared(avatar.userData.targetPosition) > 0.01;
      let riddenSwing = -1;
      if (!walking) {
        for (let seatIndex = 0; seatIndex < swingStates.length; seatIndex += 1) {
          if (swingRide?.index === seatIndex) continue;
          if (swingStates[seatIndex].remoteId) continue;
          const rest = swingRestPoints[seatIndex];
          const dx = avatar.userData.targetPosition.x - rest.x;
          const dz = avatar.userData.targetPosition.z - rest.z;
          if (dx * dx + dz * dz < SWING_REST_CLAIM_DISTANCE_SQ) {
            riddenSwing = seatIndex;
            swingStates[seatIndex].remoteId = remoteId;
            break;
          }
        }
      }
      if (riddenSwing >= 0) {
        const swing = swingStates[riddenSwing];
        const seatTop = swing.seat.getWorldPosition(new THREE.Vector3());
        avatar.position.set(
          seatTop.x,
          seatedAvatarY(seatTop.y + SWING_SEAT_HALF_THICKNESS, avatar.scale.x),
          seatTop.z,
        );
        // Same yaw-then-pitch order as the local rider so the lean stays
        // front-to-back along the ropes instead of rolling sideways.
        avatar.rotation.order = "YXZ";
        avatar.rotation.y = swingSet.rotation.y;
        avatar.rotation.x = swing.pivot.rotation.x;
        avatar.userData.leftArm.rotation.x = SWING_ARM_HOLD_PITCH;
        avatar.userData.rightArm.rotation.x = SWING_ARM_HOLD_PITCH;
        applySeatedLegPose(avatar);
        animateAvatarActivity(avatar, time, delta, reducedMotion);
        return;
      }
      avatar.rotation.x = 0;
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
      if (seatedAtCampfire) {
        applySeatedLegPose(avatar);
      } else {
        applyLegPitch(avatar, -gait * GAIT_LEG_SWING, gait * GAIT_LEG_SWING);
      }
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
  // and floats a welcome bubble instead of picking the next wander spot. A
  // chat mention (exciteForkbot) outranks both: the droid rushes to whoever
  // spoke while its chest screen echoes the line and then thinks out loud.
  function updateForkbot(delta, time) {
    const data = forkbot.userData;
    let target = forkbotWanderTarget;
    let speed = FORKBOT_SPEED;
    if (forkbotExcitement) {
      target = forkbotExcitement.avatar.position;
      speed = FORKBOT_EXCITED_SPEED;
      const waited = performance.now() - forkbotExcitement.startedAt;
      if (!forkbotExcitement.thinking && waited >= FORKBOT_ECHO_MS) {
        forkbotExcitement.thinking = true;
      }
      if (forkbotExcitement.thinking) {
        const dotPhase = Math.floor(time / 400) % 3;
        if (dotPhase !== forkbotExcitement.dotPhase) {
          forkbotExcitement.dotPhase = dotPhase;
          paintForkbotScreen({
            message: forkbotExcitement.message,
            thinking: true,
            dotPhase,
          });
        }
      }
      // The reply normally clears this state (showChatBubble); the timeout
      // only covers an unavailable bot so the dots don't run forever.
      if (waited >= FORKBOT_THINKING_TIMEOUT_MS) {
        settleForkbot(time);
        target = forkbotWanderTarget;
      }
    } else if (forkbotGreeting) {
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
    const arrive =
      forkbotGreeting || forkbotExcitement ? FORKBOT_GREETING_RANGE * 0.8 : 0.4;
    const walking = distance > arrive;
    if (walking) {
      const step = Math.min(distance - arrive, speed * delta);
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
      data.rollingBall.rotation.x -= speed * delta * 1.8;
      data.rollingBall.rotation.z = Math.sin(time * 0.006 + data.phase) * 0.08;
    }
    // Excited hops so the mention visibly lands even from across the square.
    forkbot.position.y =
      forkbotExcitement && !reducedMotion
        ? FORKBOT_HOME[1] +
          Math.abs(Math.sin(time * 0.012 + data.phase)) * 0.16
        : FORKBOT_HOME[1];
  }

  function paintForkbotScreen(state) {
    const canvas = forkbotScreenTexture.image;
    drawForkbotScreen(canvas.getContext("2d"), canvas, state);
    forkbotScreenTexture.needsUpdate = true;
  }

  // adhoc #369: any live speaker mentioning ForkBot in chat (world.js
  // handleWorldChatMessage) pulls the droid over to them. The chest screen
  // echoes the line straight away; once the echo has had a beat the thinking
  // dots run (updateForkbot) until the reply is broadcast into the room.
  function exciteForkbot(peerId, text) {
    const message = String(text || "").replace(/\s+/g, " ").trim().slice(0, 90);
    if (!message) return false;
    const avatar =
      peerId === identity.id
        ? player
        : remotePlayers.get(String(peerId || "")) ||
          loungeMembers.get(String(peerId || ""));
    if (!avatar) return false;
    forkbotGreeting = null;
    forkbotExcitement = {
      avatar,
      message,
      startedAt: performance.now(),
      thinking: false,
      dotPhase: 0,
    };
    paintForkbotScreen({ message, thinking: false, dotPhase: 0 });
    return true;
  }

  // Back to the idle wordmark, lingering beside the speaker for a moment
  // before the next wander pick.
  function settleForkbot(time) {
    forkbotExcitement = null;
    paintForkbotScreen(null);
    forkbotWanderTarget.copy(forkbot.position);
    forkbotNextWanderAt = time + 9000;
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

  function officeCameraDistanceLimit(
    target,
    direction,
    requestedDistance,
  ) {
    const requested = Math.max(0, Number(requestedDistance) || 0);
    if (
      officeSceneMode === "town" ||
      !target ||
      !direction ||
      requested <= 0
    ) {
      return requested;
    }
    const localTarget = officeInterior.worldToLocal(target.clone());
    const ridingElevator =
      Boolean(officeElevatorRide) ||
      officeElevatorCabinContains(
        localTarget.x,
        localTarget.z,
        0.08,
      );
    if (officeCurrentFloorId === "rooftop" && !ridingElevator) {
      return requested;
    }
    const floorBase = ridingElevator
      ? officeElevatorCar.position.y
      : officeFloorY(officeCurrentFloorId);
    const bounds = ridingElevator
      ? {
          minX: OFFICE_ELEVATOR_CENTER_X - 4.42,
          maxX: OFFICE_ELEVATOR_CENTER_X + 4.42,
          minY: floorBase + 0.5,
          maxY: floorBase + 7.02,
          minZ: OFFICE_ELEVATOR_CENTER_Z - 3.42,
          maxZ: OFFICE_ELEVATOR_CENTER_Z + 3.42,
        }
      : {
          minX: -OFFICE_WIDTH / 2 + 0.72,
          maxX: OFFICE_WIDTH / 2 - 0.72,
          minY: floorBase + 0.5,
          maxY: floorBase + OFFICE_FLOOR_HEIGHT - 0.55,
          minZ: -OFFICE_DEPTH / 2 + 0.72,
          maxZ: OFFICE_FRONT_Z - 0.72,
        };
    const axes = [
      ["x", "minX", "maxX"],
      ["y", "minY", "maxY"],
      ["z", "minZ", "maxZ"],
    ];
    let boundaryDistance = Infinity;
    axes.forEach(([axis, minKey, maxKey]) => {
      const component = Number(direction[axis]) || 0;
      if (Math.abs(component) < 1e-6) return;
      const edge = component > 0 ? bounds[maxKey] : bounds[minKey];
      const distance = (edge - localTarget[axis]) / component;
      if (distance >= 0) boundaryDistance = Math.min(boundaryDistance, distance);
    });
    if (!Number.isFinite(boundaryDistance)) return requested;
    return Math.min(requested, Math.max(0, boundaryDistance - 0.24));
  }

  function updateCamera(delta) {
    const officeAvatar =
      officeSceneMode === "meeting"
        ? officeParticipants.get(officeLocalParticipantId) || officeLobbyPlayer
        : officeSceneMode === "lobby"
          ? player
          : null;
    if (cameraMode === "first-person") {
      const firstPersonAvatar = officeAvatar || player;
      const eye = firstPersonAvatar
        .getWorldPosition(new THREE.Vector3())
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
    const target = officeAvatar
      ? officeAvatar
          .getWorldPosition(new THREE.Vector3())
          .add(new THREE.Vector3(0, FIRST_PERSON_EYE_HEIGHT, 0))
      : cameraFocus
        ? cameraFocus.clone()
        : player.position
            .clone()
            .add(new THREE.Vector3(0, FIRST_PERSON_EYE_HEIGHT, 0));
    const officeFocused = Boolean(cameraFocus && selectedLandmark === "office");
    const officeInteriorFocused = officeSceneMode !== "town";
    // The Office keeps the same orbit feel as the rest of the World. A local
    // ray-to-bounds limit below stops zoom at the current floor walls or the
    // panoramic elevator glass instead of shrinking the whole interior view.
    const focusScale = officeInteriorFocused
      ? 1
      : officeFocused
        ? camera.aspect < 0.75 ? 1.36 : 0.87
        : cameraFocus
          ? 0.78
          : 1;
    const requestedDistance = CAMERA_DISTANCE * cameraZoom * focusScale;
    const offsetDirection = new THREE.Vector3(
      Math.sin(cameraYaw) * Math.cos(cameraPitch),
      Math.sin(cameraPitch),
      Math.cos(cameraYaw) * Math.cos(cameraPitch),
    );
    const distance = officeCameraDistanceLimit(
      target,
      offsetDirection,
      requestedDistance,
    );
    const desired = target
      .clone()
      .addScaledVector(offsetDirection, distance);
    const localTarget = officeInterior.worldToLocal(target.clone());
    const rooftopPatioCamera =
      officeSceneMode === "lobby" &&
      officeCurrentFloorId === "rooftop" &&
      !officeElevatorRide &&
      !officeElevatorCabinContains(
        localTarget.x,
        localTarget.z,
        0.08,
      );
    if (rooftopPatioCamera) {
      // Looking upward puts an orbit camera below its eye target. Preserve the
      // requested outward X/Z zoom, but keep that eye just above the roof slab
      // instead of letting a steep pitch pass through the story below.
      const localDesired = officeInterior.worldToLocal(desired.clone());
      localDesired.y = Math.max(
        localDesired.y,
        officeFloorY("rooftop") + 0.55,
      );
      desired.copy(officeInterior.localToWorld(localDesired));
    }
    if (reducedMotion) camera.position.copy(desired);
    else camera.position.lerp(desired, 1 - Math.pow(0.0008, delta));
    if (rooftopPatioCamera) {
      // Clamp the actual eased camera as well as its destination. Otherwise a
      // prior below-slab frame could lerp through the roof on its way back up.
      const localCamera = officeInterior.worldToLocal(
        camera.position.clone(),
      );
      localCamera.y = Math.max(
        localCamera.y,
        officeFloorY("rooftop") + 0.55,
      );
      camera.position.copy(officeInterior.localToWorld(localCamera));
    }
    if (officeElevatorRide) {
      // The cabin moves faster than a softly lerped orbit camera. Clamp the
      // eased result back inside its live glass envelope so the eye never
      // trails through a floor slab while the car is between stories.
      const localCamera = officeInterior.worldToLocal(camera.position.clone());
      localCamera.x = clamp(
        localCamera.x,
        OFFICE_ELEVATOR_CENTER_X - 4.42,
        OFFICE_ELEVATOR_CENTER_X + 4.42,
      );
      localCamera.y = clamp(
        localCamera.y,
        officeElevatorCar.position.y + 0.5,
        officeElevatorCar.position.y + 7.02,
      );
      localCamera.z = clamp(
        localCamera.z,
        OFFICE_ELEVATOR_CENTER_Z - 3.42,
        OFFICE_ELEVATOR_CENTER_Z + 3.42,
      );
      camera.position.copy(officeInterior.localToWorld(localCamera));
    }
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

  // The chest is clickable on the local player and on live peers: the two
  // tabs under the badge swap the display, and the fediverse tab's follow
  // pill is hit by UV inside the badge plane itself.
  function registerAvatarChestControls(avatar, peerId) {
    if (!avatar?.userData?.badge || avatar.userData.chestRegistered) return;
    const meshes = [
      avatar.userData.badge,
      ...(avatar.userData.chestTabs?.children || []),
    ];
    meshes.forEach((mesh) => {
      chestControls.set(mesh, { avatar, peerId: String(peerId || "") });
      interactive.push(mesh);
    });
    avatar.userData.chestRegistered = true;
  }

  function unregisterAvatarChestControls(avatar) {
    if (!avatar?.userData?.chestRegistered) return;
    [
      avatar.userData.badge,
      ...(avatar.userData.chestTabs?.children || []),
    ].forEach((mesh) => {
      if (!mesh) return;
      chestControls.delete(mesh);
      const index = interactive.indexOf(mesh);
      if (index >= 0) interactive.splice(index, 1);
    });
    avatar.userData.chestRegistered = false;
  }

  function setAvatarChestTab(avatar, tab) {
    if (!avatar?.userData?.badge) return;
    const next = tab === "fediverse" ? "fediverse" : "info";
    if (avatar.userData.chestTab === next) return;
    avatar.userData.chestTab = next;
    renderAvatarBadge(THREE, avatar, avatar.userData.badgeRemote === true);
  }

  /** Attach a loaded (or failed) fediverse card to one avatar's chest. */
  function setAvatarFediverseProfile(peerId, profile) {
    const id = String(peerId || "");
    const avatar =
      id === identity.id
        ? player
        : remotePlayers.get(id) || loungeMembers.get(id);
    if (!avatar?.userData?.badge) return;
    avatar.userData.fediverseProfile =
      profile && typeof profile === "object" ? profile : { state: "unavailable" };
    if (avatar.userData.chestTab === "fediverse") {
      renderAvatarBadge(THREE, avatar, avatar.userData.badgeRemote === true);
    }
    if (avatar === player && officeLobbyPlayer?.userData?.badge) {
      officeLobbyPlayer.userData.fediverseProfile =
        avatar.userData.fediverseProfile;
      if (officeLobbyPlayer.userData.chestTab === "fediverse") {
        renderAvatarBadge(THREE, officeLobbyPlayer, false);
      }
    }
  }

  /** Dress one avatar's face with a consented account avatar photo. */
  function setAvatarFaceImage(peerId, url) {
    const id = String(peerId || "");
    const avatar =
      id === identity.id
        ? player
        : remotePlayers.get(id) || loungeMembers.get(id);
    if (avatar) applyAvatarFaceImage(THREE, avatar, url);
    if (id === identity.id && officeLobbyPlayer) {
      applyAvatarFaceImage(THREE, officeLobbyPlayer, url);
    }
  }

  function handleChestControl(control, hit) {
    const avatar = control.avatar;
    const tab = hit.object.userData?.chestTab;
    if (tab) {
      setAvatarChestTab(avatar, tab);
      if (tab === "fediverse" && !avatar.userData.fediverseProfile) {
        avatar.userData.fediverseProfile = { state: "loading" };
        renderAvatarBadge(THREE, avatar, avatar.userData.badgeRemote === true);
        onFediverseProfile({
          peerId: control.peerId,
          name: String(avatar.userData.badgeIdentity?.name || ""),
          accountStatus: String(
            avatar.userData.badgeIdentity?.accountStatus || "Guest",
          ),
          self: control.peerId === identity.id,
        });
      }
      return;
    }
    // The badge plane: only the fediverse tab's follow pill is actionable.
    if (
      avatar.userData.chestTab !== "fediverse" ||
      !badgeFollowPillHit(hit.uv)
    ) {
      return;
    }
    const profile = avatar.userData.fediverseProfile || {};
    if (profile.state !== "ready" || profile.canFollow !== true) return;
    avatar.userData.fediverseProfile = { ...profile, pending: true };
    renderAvatarBadge(THREE, avatar, avatar.userData.badgeRemote === true);
    onFediverseFollow({
      peerId: control.peerId,
      name: String(profile.account || avatar.userData.badgeIdentity?.name || ""),
      following: profile.isFollowing === true,
    });
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

  // Fill in the directory-only rows for an avatar's badge. A guest can type
  // any display name, so only a server-stamped account status may claim the
  // public record filed under that name.
  function withMemberFacts(identity) {
    if (!identity || String(identity.accountStatus || "Guest") === "Guest") {
      return identity;
    }
    const facts = memberFacts.get(
      String(identity.name || "").trim().toLowerCase(),
    );
    if (!facts) return identity;
    return {
      ...identity,
      joinedAt:
        Number(identity.joinedAt) > 0 ? identity.joinedAt : facts.joinedAt,
      totalActiveMs:
        identity.totalActiveMs == null
          ? facts.totalActiveMs
          : identity.totalActiveMs,
    };
  }

  function setRemotePlayers(players = []) {
    const seen = new Set();
    players.forEach((remote) => {
      if (!remote?.id || remote.id === identity.id) return;
      seen.add(remote.id);
      let avatar = remotePlayers.get(remote.id);
      const badgeIdentity = withMemberFacts({
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
        firstSeenMinutes: Math.max(0, Number(remote.firstSeenMinutes) || 0),
        joinedAt: Math.max(0, Number(remote.joinedAt) || 0),
        nodes: Array.isArray(remote.nodes) ? remote.nodes.slice(0, 6) : [],
        statusEmoji: remote.statusEmoji || "",
        statusNote: remote.statusNote || "",
        outfitColor: remote.outfitColor || "",
        outfitStyle: remote.outfitStyle || "",
        faceImage: remote.faceImage === true,
        activityBucket: remote.activityBucket || "",
        // The public directory's total-active-time aggregate, forwarded by the
        // app layer so a live member's chest carries the same row their
        // campfire bench figure does.
        totalActiveMs: Number.isFinite(Number(remote.totalActiveMs))
          ? Math.max(0, Number(remote.totalActiveMs))
          : null,
        solana: remote.solana || "",
        walletSol: remote.walletSol ?? null,
        walletTxBucket: remote.walletTxBucket || "",
      });
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
        registerAvatarChestControls(avatar, remote.id);
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
        // Idle and returning members walk back to the bench that carries
        // their own name; anyone the directory has not caught up with yet
        // takes one of the open stools past the seated figures.
        const seats = campfire.userData.seatOffsets || [];
        const owned = campfire.userData.seatByName?.get(
          String(remote.name || "").trim().toLowerCase(),
        );
        const taken = Math.min(
          campfire.userData.memberFigureCount || 0,
          Math.max(0, seats.length - 1),
        );
        const open = Math.max(1, seats.length - taken);
        const seat = Number.isInteger(owned)
          ? seats[owned]
          : seats[taken + (hashNumber(remote.id) % open)];
        const offset = seat || new THREE.Vector3();
        avatar.userData.targetPosition.copy(campfire.position);
        avatar.userData.targetPosition.add(offset);
        // Seat offsets carry the plank top; the sitter rides its hips on it.
        avatar.userData.targetPosition.y = seatedAvatarY(
          campfire.position.y + offset.y,
          avatar.scale.x,
        );
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
      unregisterAvatarChestControls(avatar);
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
    guests = 0,
  ) {
    const total = Math.max(0, Math.min(999999, Number(totalCount) || 0));
    // The flames carry the headline count of registered accounts, plus the
    // name of whoever joined last so the newest member is visible at a glance.
    setCampfireMemberCount(total, newestMemberName(members));
    // Refresh the badge facts a live presence frame cannot carry, so the same
    // account reads identically on its bench, on its walking avatar, and in
    // an office meeting.
    memberFacts.clear();
    (Array.isArray(members) ? members : []).forEach((member) => {
      const name = String(member?.name || "").trim().toLowerCase();
      if (!name) return;
      const activeMs = Number(member?.totalActiveMs ?? member?.activeMs);
      memberFacts.set(name, {
        joinedAt: Math.max(0, Number(member?.createdAt) || 0),
        totalActiveMs:
          Number.isFinite(activeMs) && activeMs >= 0 ? activeMs : null,
      });
    });
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
    // flames. Every account in the directory owns one numbered bench for the
    // whole session — a member out walking the world leaves theirs visibly
    // empty, with their name still on it — and the ring carries one bench
    // that always stays open for the next guest plus one more for every
    // guest already here, so the circle grows as people arrive.
    const roster = (Array.isArray(members) ? members : []).filter((member) =>
      String(member?.name || "").trim(),
    );
    const guestSeats = Math.max(0, Math.min(64, Math.round(Number(guests) || 0)));
    const seats = rebuildCampfireCircle(
      Math.max(total, roster.length) + guestSeats + 1,
    );
    const seen = new Set();
    const seatByName = new Map();
    roster
      .slice(0, Math.max(1, seats.length))
      .forEach((member, index) => {
        const name = String(member.name).trim().slice(0, 32);
        const id = `member:${name.toLowerCase()}`;
        if (seatByName.has(name.toLowerCase())) {
          // Two rows for one account: leave the extra bench open.
          setCampfireSeatLabel(index, "", false);
          return;
        }
        seatByName.set(name.toLowerCase(), index);
        // The bench keeps the member's name whether or not they are on it,
        // so the empty seats read as "who is out and about" rather than as
        // unclaimed furniture.
        setCampfireSeatLabel(index, name, member.away === true);
        if (member.away === true) {
          const parked = loungeMembers.get(id);
          if (parked) {
            unregisterAvatarChestControls(parked);
            world.remove(parked);
            disposeObject3D(parked);
            loungeMembers.delete(id);
          }
          return;
        }
        seen.add(id);
        let figure = loungeMembers.get(id);
        if (!figure) {
          // The directory carries the coarse country/browser/OS the member
          // saved on their account, so an away member's bench figure wears
          // their own flag shirt and client badge instead of a blank one.
          const memberCountry = /^[A-Z]{2}$/.test(
            String(member.countryCode || "").toUpperCase(),
          )
            ? String(member.countryCode).toUpperCase()
            : "";
          figure = createAvatar(
            THREE,
            {
              id,
              name,
              flag: memberCountry ? flagEmoji(memberCountry) : "◌",
              countryCode: memberCountry,
              browser: String(member.browser || "Hidden"),
              os: String(member.os || "Hidden"),
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
              // The directory's coarse recency bucket drives the bench
              // figure's chest activity light.
              activityBucket: member.activityBucket || "",
            },
            { remote: true, scale: 0.88 },
          );
          world.add(figure);
          loungeMembers.set(id, figure);
          // Directory figures are real accounts, so their chest tabs work the
          // same way a live peer's do.
          registerAvatarChestControls(figure, id);
        }
        // The 30s directory refresh can age a seated member's recency bucket
        // without recreating the figure, so re-sync the chest light in place.
        syncAvatarActivity(figure, {
          inputActive: false,
          accountStatus: "Registered",
          activityBucket: member.activityBucket || "",
        });
        const seat = seats[index % Math.max(1, seats.length)];
        figure.position.copy(campfire.position);
        if (seat) figure.position.add(seat);
        figure.position.y = seatedAvatarY(
          campfire.position.y + (seat ? seat.y : 0),
          figure.scale.x,
        );
        // Face the fire at the circle's centre and hold a seated pose on the
        // bench, matching the local player's bench-seat legs. Avatar fronts
        // face local -Z, so the inward heading is atan2(x, z), matching
        // sitOnCampfireBench.
        figure.rotation.y = seat ? Math.atan2(seat.x, seat.z) : 0;
        applySeatedLegPose(figure);
      });
    // Benches past the roster are the open guest seats.
    for (let index = roster.length; index < seats.length; index += 1) {
      setCampfireSeatLabel(index, "", false);
    }
    campfire.userData.seatByName = seatByName;
    campfire.userData.memberFigureCount = Math.min(roster.length, seats.length);
    loungeMembers.forEach((figure, id) => {
      if (seen.has(id)) return;
      unregisterAvatarChestControls(figure);
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
      // The commit shown before this update, captured ahead of any rebuild.
      // Comparing it against the fresh signed record is what detects "code
      // was just pushed onto this node" — for both the instant socket-driven
      // refresh and the regular poll — without trusting any relay frame.
      const priorCommit = String(
        cabinet?.userData?.nodeRecord?.commit || "",
      );
      // Same idea for "this node just answered somebody": the served counters
      // shown before this update, captured ahead of any rebuild.
      const priorServed = mirrorServedTotal(cabinet?.userData?.nodeRecord);
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
      const nextCommit = String(node?.commit || "");
      if (priorCommit && nextCommit && nextCommit !== priorCommit) {
        spawnPushSurge(cabinet.position);
      }
      const nextServed = mirrorServedTotal(node);
      if (
        priorServed !== null &&
        nextServed !== null &&
        nextServed > priorServed
      ) {
        spawnServeFlights(cabinet.position, nextServed - priorServed);
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
        // A discovered Durable Object with no client-observable limit still
        // earns a plinth: its relayed byte total is the whole point, and a
        // binding that has relayed nothing yet is itself worth showing.
        const bytesTotal = Number(record?.bytesTotal);
        const bytes =
          Number.isSafeInteger(bytesTotal) && bytesTotal > 0 ? bytesTotal : 0;
        return name
          ? {
              name: name.slice(0, 36),
              id: String(record?.id || name).slice(0, 72),
              pairs,
              bytes,
            }
          : null;
      })
      .filter(Boolean)
      .slice(0, 32);
    const tables = Array.isArray(metrics?.tables) ? metrics.tables : [];
    const safeTables = [
      ...new Map(
        tables
          .map((table) => {
            const name = String(table?.name || "").trim();
            const rowCount = Number(table?.rowCount);
            // A table that holds nothing yet still gets a bar; it is part of
            // the platform's shape and hiding it shrank the inventory.
            return /^[A-Za-z_][A-Za-z0-9_]{0,62}$/.test(name) &&
              Number.isSafeInteger(rowCount) &&
              rowCount >= 0
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
      .slice(0, 256);
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
        // An all-empty inventory has no scale to draw against; every bar then
        // sits at its minimum height instead of collapsing to NaN geometry.
        const normalized = maxRows
          ? Math.log1p(table.rowCount) / Math.log1p(maxRows)
          : 0;
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

    // Relayed bytes have no configured ceiling, so the busiest Durable Object
    // sets the scale the others are drawn against.
    const peakBytes = safeRecords.reduce(
      (peak, record) => Math.max(peak, record.bytes),
      0,
    );
    safeRecords.forEach((record, index) => {
      const object = new THREE.Group();
      object.name = `system-capacity-service:${record.id}`;
      object.userData.metrics = record.pairs.map((metric) => ({
        key: metric.key,
        currentUsage: metric.usage,
        configuredLimit: metric.limit,
      }));
      object.userData.bytesRelayed = record.bytes;

      // Plinths share the platform with the table bars, so a large binding
      // list tightens its spacing rather than marching off the edge.
      const spacing = Math.min(
        safeRecords.length > 5 ? 1.65 : 2.5,
        11.5 / Math.max(1, safeRecords.length - 1),
      );
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
      // Without a limit to fill against, the column shows this object's share
      // of the busiest object's traffic (log-scaled, like the table bars).
      const rawRatio = firstMetric
        ? firstMetric.usage / firstMetric.limit
        : peakBytes
          ? Math.log1p(record.bytes) / Math.log1p(peakBytes)
          : 0;
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
      fill.userData.currentUsage = firstMetric
        ? firstMetric.usage
        : record.bytes;
      fill.userData.configuredLimit = firstMetric ? firstMetric.limit : 0;
      object.add(fill);

      const exactValues = [
        ...record.pairs
          .slice(0, 2)
          .map((metric) => `${metric.key} ${metric.usage}/${metric.limit}`),
        ...(record.bytes || !record.pairs.length
          ? [`${formatCapacityBytes(record.bytes)} relayed`]
          : []),
      ].join(" · ");
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
    // The visitor's own chest reads exactly like everyone else's: their live
    // activity ticket wins, and the directory record fills the rest in.
    const badgeIdentity = withMemberFacts(identity);
    updateAvatarBadge(THREE, player, badgeIdentity, false);
    syncOperatorBelt(THREE, player, identity.nodes?.length || 0);
    updateAvatarBadge(THREE, officeLobbyPlayer, badgeIdentity, false);
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
        const rawSizeTree =
          record.sizeTree &&
          typeof record.sizeTree === "object" &&
          Number(record.sizeTree.size) > 0 &&
          Array.isArray(record.sizeTree.children)
            ? record.sizeTree
            : null;
        return {
          owner,
          name,
          key: `${owner.toLocaleLowerCase()}/${name.toLocaleLowerCase()}`,
          sizeBytes,
          liveHost: record.liveHost === true,
          isPrivate: record.isPrivate === true,
          source: String(record.source || "").slice(0, 40),
          provider: String(record.provider || "").slice(0, 20),
          providerLabel: String(record.providerLabel || "").slice(0, 30),
          externalUrl: String(record.externalUrl || "").slice(0, 500),
          importId: String(record.importId || "").slice(0, 64),
          mirrorState: String(record.mirrorState || "").slice(0, 24),
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
          sizeTree: rawSizeTree,
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
        record.provider,
        record.importId,
        record.starCount,
        record.starred,
        record.fediverseFollowerCount,
        record.fediverseFollowerStatus,
        record.sizeTree
          ? [
              String(record.sizeTree.commit || "").slice(0, 64),
              Number(record.sizeTree.size) || 0,
              record.sizeTree.children.length,
            ]
          : null,
        // Identity only: a follower's avatar/bio arriving does not need a
        // portal rebuild, but a different follower does.
        record.fediverseFollowers.map((follower) => follower?.handle || ""),
      ]),
    ]);
    const previousCatalog = world.userData.repositoryCatalogLayer;
    const previousPortalKeys = new Set(repositoryPortals.keys());
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
    const coreRecords = records.filter(
      (record) => record.source !== "hosted-import",
    );
    const hostedRecords = records.filter(
      (record) => record.source === "hosted-import",
    );
    [
      ["repository-perimeter-guide", REPOSITORY_EDGE_RADIUS, 0, coreRecords],
      [
        "hosted-repository-island-guide",
        REPOSITORY_ISLAND_RING_RADIUS,
        REPOSITORY_ISLAND_CENTER_X,
        hostedRecords,
      ],
    ].forEach(([name, radius, centerX, cohort]) => {
      if (!cohort.length) return;
      const guide = new THREE.Mesh(
        new THREE.TorusGeometry(radius, 0.045, 6, 256),
        makeMaterial(THREE, "#77d9ff", {
          emissive: "#1e637c",
          emissiveIntensity: 0.45,
          transparent: true,
          opacity: 0.38,
          roughness: 0.5,
        }),
      );
      guide.name = name;
      guide.rotation.x = Math.PI / 2;
      guide.position.set(centerX, 0.12, 0);
      layer.add(guide);
    });

    const maxBytes = Math.max(0, ...records.map((record) => record.sizeBytes));
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
      syncing: makeMaterial(THREE, "#f0c66f", {
        emissive: "#8d641e",
        emissiveIntensity: 0.62,
        metalness: 0.12,
        roughness: 0.52,
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
    const orderedRecords = [...coreRecords, ...hostedRecords];
    orderedRecords.forEach((record, index) => {
      const islandRecord = record.source === "hosted-import";
      const cohort = islandRecord ? hostedRecords : coreRecords;
      const cohortIndex = cohort.findIndex(
        (candidate) => candidate.key === record.key,
      );
      const ringRadius = islandRecord
        ? REPOSITORY_ISLAND_RING_RADIUS
        : REPOSITORY_EDGE_RADIUS;
      const centerX = islandRecord ? REPOSITORY_ISLAND_CENTER_X : 0;
      const angle =
        Math.PI / 2 +
        (cohortIndex / Math.max(1, cohort.length)) * Math.PI * 2;
      const portalSpacing =
        (Math.PI * 2 * ringRadius) / Math.max(1, cohort.length);
      const portalDensityScale = clamp(
        (portalSpacing - 0.06) / 3.15,
        0.58,
        1,
      );
      const isActive = record.key === activeKey;
      const material = isActive
        ? materials.selected
          : record.isPrivate
            ? materials.private
            : record.liveHost
              ? materials.live
              : record.mirrorState === "syncing"
                ? materials.syncing
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
        centerX + Math.cos(angle) * ringRadius,
        2.55,
        Math.sin(angle) * ringRadius,
      );
      node.rotation.y = -angle - Math.PI / 2;
      if (
        ["external-import", "hosted-import"].includes(record.source) &&
        !previousPortalKeys.has(record.key)
      ) {
        const importedBefore = records
          .slice(0, index)
          .filter(
            (candidate) =>
              ["external-import", "hosted-import"].includes(candidate.source) &&
              !previousPortalKeys.has(candidate.key),
          ).length;
        repositoryPortalBornAt.set(
          record.key,
          performance.now() + importedBefore * 320,
        );
        node.scale.setScalar(reducedMotion ? 1 : 0.015);
      }
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
        provider: record.provider,
        providerLabel: record.providerLabel,
        externalUrl: record.externalUrl,
        importId: record.importId,
        mirrorState: record.mirrorState,
        starCount: record.starCount,
        starred: record.starred,
        angle,
        island: islandRecord,
      };
      for (const mesh of [disk, outline]) {
        mesh.userData.landmark = "repositories";
        mesh.userData.repositoryPortal = repositoryPortal;
        interactive.push(mesh);
        portalMeshes.push(mesh);
      }
      face.add(disk, outline);
      if (!isActive && record.sizeTree) {
        const map = repositorySizeMapSegments(record.sizeTree, "");
        const miniature = new THREE.Group();
        miniature.name =
          `repository-mini-size-map:${record.owner}/${record.name}`;
        miniature.position.z = 0.09;
        const innerRadius = nodeRadius * 0.18;
        const outerRadius = nodeRadius * 0.91;
        const ringWidth =
          (outerRadius - innerRadius) / Math.max(1, map.ringCount);
        map.segments.slice(0, 24).forEach((segment) => {
          const geometry = repositoryWedgeGeometry(
            THREE,
            innerRadius + (segment.depth - 1) * ringWidth + 0.006,
            innerRadius + segment.depth * ringWidth - 0.006,
            segment.from,
            segment.to,
            0.045,
          );
          if (!geometry) return;
          const wedge = new THREE.Mesh(
            geometry,
            new THREE.MeshBasicMaterial({
              color: segment.color,
              transparent: segment.type === "summary",
              opacity: segment.type === "summary" ? 0.62 : 0.98,
              side: THREE.DoubleSide,
              toneMapped: false,
            }),
          );
          wedge.position.z = segment.depth * 0.006;
          miniature.add(wedge);
        });
        face.add(miniature);
      }
      if (isActive) {
        const selectedHalo = new THREE.Mesh(
          new THREE.TorusGeometry(1.38, 0.055, 8, 40),
          materials.selected,
        );
        selectedHalo.name = "repository-selected-halo";
        selectedHalo.scale.setScalar(nodeRadius);
        selectedHalo.userData.repositoryHaloScale = nodeRadius;
        face.add(selectedHalo);
        usedMaterials.add(materials.selected);
        const orbitMarker = new THREE.Mesh(
          new THREE.SphereGeometry(0.105, 12, 8),
          materials.selected,
        );
        orbitMarker.name = "repository-live-orbit-marker";
        orbitMarker.userData.repositoryOrbitRadius = nodeRadius * 1.38;
        orbitMarker.position.set(
          orbitMarker.userData.repositoryOrbitRadius,
          0,
          0.08,
        );
        face.add(orbitMarker);
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
            : record.source === "external-import"
              ? `${record.providerLabel || "EXTERNAL"} IMPORT`
            : record.mirrorState === "syncing"
              ? "MIRRORS SYNCING"
              : record.mirrorState === "offline"
                ? "MIRRORS OFFLINE"
                : "STUB · MIRROR NEEDED",
        isActive
          ? "#9ef7c6"
          : record.isPrivate
            ? "#d5b6ff"
            : record.mirrorState === "syncing"
              ? "#f0c66f"
              : record.mirrorState === "offline"
                ? "#91a39a"
                : "#77d9ff",
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
        key: record.key,
      });
    });

    world.add(layer);
    Object.values(materials).forEach((material) => {
      if (!usedMaterials.has(material)) material.dispose();
    });
    world.userData.repositoryCatalogLayer = layer;
    world.userData.repositoryPortalMeshes = portalMeshes;
  }

  function setRepositoryImportState(state = {}) {
    const district = landmarkObjects.get("repositories");
    const kiosk = district?.userData?.repositoryImportKiosk;
    if (!kiosk) return;
    kiosk.userData.importing = state.active === true;
    kiosk.userData.importStage = String(state.stage || "").slice(0, 40);
    kiosk.userData.importProvider = String(state.provider || "").slice(0, 20);
    const spark = kiosk.userData.importSpark;
    if (spark?.material?.color) {
      const color =
        state.status === "error"
          ? "#ff7f8f"
          : state.status === "complete"
            ? "#9ef7c6"
            : state.provider === "gitlab"
              ? "#fc8d45"
              : state.provider === "codeberg"
                ? "#77d9ff"
                : "#f0f6fc";
      spark.material.color.set(color);
      spark.material.emissive?.set?.(color);
    }
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

  function showAvatarChatBubble(avatar, text, options = {}) {
    const message = String(text || "").replace(/\s+/g, " ").trim().slice(0, 140);
    if (!avatar || !message) return false;
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
    sprite.userData.message = message;
    sprite.userData.officeReceptionGreeting =
      options.officeReception === true;
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

  function showChatBubble(peerId, text, local = false) {
    const avatar =
      local || peerId === identity.id
        ? player
        : peerId === FORKBOT_PEER_ID
          ? forkbot
          : remotePlayers.get(String(peerId || "")) ||
            loungeMembers.get(String(peerId || ""));
    if (!avatar) return false;
    // ForkBot's own line is the reply the thinking dots were waiting for.
    if (avatar === forkbot && forkbotExcitement) {
      settleForkbot(performance.now());
    }
    return showAvatarChatBubble(avatar, text);
  }

  // A registered member who is not in the world as a live peer still sits on
  // their own campfire bench (updateMemberLounge), so a chat line from them
  // floats over the seated figure's head instead of going nowhere.
  function showMemberChatBubble(name, text) {
    const wanted = String(name || "").trim().toLowerCase();
    if (!wanted) return false;
    if (loungeMembers.has(`member:${wanted}`)) {
      return showChatBubble(`member:${wanted}`, text);
    }
    // The public room truncates asserted names to 16 characters, so a
    // truncated sender may only be a prefix of the seated member's name.
    if (wanted.length < 16) return false;
    for (const id of loungeMembers.keys()) {
      if (id.slice("member:".length).startsWith(wanted)) {
        return showChatBubble(id, text);
      }
    }
    return false;
  }

  // Repaints the fountain's treasury board with the public pool address QR
  // code and the balance reported by /api/accounts/central-fund.
  function updateRewardPool(state = {}) {
    const sign = landmarkObjects.get("fountain")?.userData?.treasurySign;
    if (!sign) return;
    applyRewardTreasury(THREE, sign, state);
  }

  // Freshly pushed code announces itself: a tall light column rises from the
  // cabinet and a ground shockwave ring expands far past the server yard, so
  // the arrival reads from anywhere in the town — not just beside the rack.
  // Purely cosmetic and driven only by a verified commit change in the signed
  // mirror payload (see updateNetworkNodes), never by an unauthenticated frame.
  function spawnPushSurge(position) {
    // Bound a burst of simultaneous publishes to a fixed effect budget.
    if (pushSurges.length >= 8) return;
    const group = new THREE.Group();
    group.name = "mirror-push-surge";
    const beam = new THREE.Mesh(
      new THREE.CylinderGeometry(0.55, 0.9, 56, 18, 1, true),
      new THREE.MeshBasicMaterial({
        color: "#7dffb8",
        toneMapped: false,
        transparent: true,
        opacity: 0.85,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
        side: THREE.DoubleSide,
      }),
    );
    beam.position.y = 28;
    group.add(beam);
    const ring = new THREE.Mesh(
      new THREE.RingGeometry(0.9, 1, 48),
      new THREE.MeshBasicMaterial({
        color: "#9ef7c6",
        toneMapped: false,
        transparent: true,
        opacity: 0.8,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
        side: THREE.DoubleSide,
      }),
    );
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = 0.07;
    group.add(ring);
    group.position.set(position.x, 0, position.z);
    world.add(group);
    pushSurges.push({
      group,
      beam,
      ring,
      startedAt: performance.now(),
      duration: 4200,
    });
  }

  // Serving reads as traffic leaving the rack: each clone or repository page a
  // node answers launches a small figure for the agent it served, shooting up
  // out of the cabinet and shrinking away into the sky. Purely cosmetic and
  // driven only by the node's own served counters in the signed mirror payload
  // (see updateNetworkNodes), never by an unauthenticated frame.
  function spawnServeFlights(position, count = 1) {
    const requested = Math.floor(Number(count));
    const wanted = Math.min(
      3,
      Math.max(1, Number.isFinite(requested) ? requested : 1),
    );
    for (let index = 0; index < wanted; index += 1) {
      // Bound a busy yard's burst to a fixed effect budget.
      if (serveFlights.length >= 12) return;
      const figure = createServedVisitorFigure(
        THREE,
        index % 2 ? "#8fd8ff" : "#9ef7c6",
      );
      figure.position.set(
        position.x + (Math.random() - 0.5) * 0.7,
        position.y + 3.3,
        position.z + (Math.random() - 0.5) * 0.7,
      );
      figure.rotation.y = Math.random() * Math.PI * 2;
      figure.visible = index === 0;
      world.add(figure);
      serveFlights.push({
        group: figure,
        materials: figure.userData.figureMaterials,
        // Staggered so a multi-request update reads as a stream rather than
        // one clump of overlapping figures.
        startedAt: performance.now() + index * 220,
        duration: 2400,
        baseY: figure.position.y,
        climb: 34 + Math.random() * 12,
        spin: (Math.random() < 0.5 ? -1 : 1) * (1.2 + Math.random()),
      });
    }
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

  // Energy fades with a ~0.5s half life so the antenna slows back down soon
  // after the mouse stops instead of coasting.
  function decayedPointerEnergy(now) {
    if (!pointerEnergyAt) return 0;
    const elapsed = Math.max(0, now - pointerEnergyAt) / 1000;
    return pointerEnergy * Math.pow(0.25, elapsed);
  }

  function recordPointerEnergy(event, now) {
    const x = Number(event.clientX) || 0;
    const y = Number(event.clientY) || 0;
    if (pointerEnergyAt) {
      const moved = Math.hypot(x - pointerEnergyX, y - pointerEnergyY);
      // ~700px of travel inside one half life saturates the blink rate.
      pointerEnergy = Math.min(1, decayedPointerEnergy(now) + moved / 700);
    }
    pointerEnergyAt = now;
    pointerEnergyX = x;
    pointerEnergyY = y;
  }

  function handlePointerMove(event) {
    const pointerNow = performance.now();
    recordPointerEnergy(event, pointerNow);
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

  function officeObjectMatchesCurrentFloor(object) {
    if (!object || officeSceneMode === "town") return true;
    // A car-mounted selector's floor id is its destination, not the story the
    // mesh occupies. Keep every destination button clickable from the cabin;
    // the server-derived allowed flag remains the authority on travel.
    if (object.userData?.interactive === "office-elevator-floor") {
      return true;
    }
    let current = object;
    let floorId = "";
    let insideOffice = false;
    while (current) {
      if (!floorId && current.userData?.officeFloorId) {
        floorId = String(current.userData.officeFloorId);
      }
      if (current === officeInterior) {
        insideOffice = true;
        break;
      }
      current = current.parent;
    }
    if (!insideOffice) return false;
    return !floorId || floorId === officeCurrentFloorId;
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
      .find(
        ({ object }) =>
          objectIsEffectivelyVisible(object) &&
          officeObjectMatchesCurrentFloor(object),
      );
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
    if (
      String(hit?.object?.userData?.interactive || "").startsWith(
        "mastodon-kiosk-open-",
      )
    ) {
      const href = String(hit.object.userData.href || "");
      if (href) onMastodonOpenLink(href);
      return;
    }
    if (hit?.object?.userData?.interactive === "mastodon-board") {
      onMastodonBoardSelect();
      return;
    }
    if (hit?.object?.userData?.interactive === "social-banner-open") {
      const href = String(hit.object.userData.href || "");
      if (href) onMastodonOpenLink(href);
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
    if (hit?.object?.userData?.interactive === "office-chair") {
      if (officeSceneMode === "meeting") {
        onOfficeChairSelect(hit.object.userData.officeChairId);
      } else if (officeSceneMode === "lobby") {
        sitOnOfficeChair(hit.object.userData.officeChairId);
      }
      return;
    }
    if (
      hit?.object?.userData?.interactive === "office-rooftop-laptop"
    ) {
      onOfficeRooftopLaptopSelect();
      return;
    }
    const chestControl = hit?.object ? chestControls.get(hit.object) : null;
    if (chestControl) {
      handleChestControl(chestControl, hit);
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
    if (Number.isInteger(hit?.object?.userData?.swingSeat)) {
      rideSwing(hit.object.userData.swingSeat);
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
      onCreateRepository({
        provider: String(hit.object.userData.createRepository || ""),
      });
      return;
    }
    if (
      hit?.object?.userData?.interactive === "office-elevator-floor"
    ) {
      if (officeSceneMode !== "town") {
        officeFloorHandler?.({
          floorId: hit.object.userData.officeFloorId,
          allowed: hit.object.userData.officeFloorAllowed === true,
        });
      }
      return;
    }
    if (hit?.object?.userData?.officeEnter) {
      onOfficeEnter({
        source: "door",
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
    if (officeSceneMode === "lobby") {
      const localPoint = officeInterior.worldToLocal(point.clone());
      if (
        !officeInteriorPointIsWalkable(
          officeCurrentFloorId,
          localPoint.x,
          localPoint.z,
          OFFICE_AVATAR_RADIUS,
        )
      ) {
        return null;
      }
    } else if (!worldWalkSurfaceContains(point.x, point.z)) {
      return null;
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
    // Districts register under a "landmark-" prefix; the campfire keeps its
    // own id but owns a map spot all the same, so match either form.
    const layoutId = String(object.userData.layoutId || "");
    const landmark = layoutId
      ? LANDMARKS.find(
          (entry) =>
            layoutId === "landmark-" + entry.id || layoutId === entry.id,
        )
      : null;
    if (Array.isArray(landmark?.position)) {
      landmark.position[0] = x;
      landmark.position[2] = z;
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
      // A mid-drag turn shifts the object to keep its centre still; fold the
      // shift into the drag offset or the next pointer move would undo it.
      if (draggedLayoutObject === target) {
        layoutDragOffset.x += pivot.x - moved.x;
        layoutDragOffset.z += pivot.z - moved.z;
      }
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
    if (officeSceneMode === "meeting" || officeElevatorRide) return;
    // A quick double tap on a bench is still a request to sit on it, not to
    // dash to the patch of ground the bench happens to stand on.
    pointerCoordinates(event);
    raycaster.setFromCamera(pointer, camera);
    if (officeSceneMode === "lobby") {
      const officeHit = raycaster
        .intersectObjects(interactive, false)
        .find(
          ({ object }) =>
            objectIsEffectivelyVisible(object) &&
            officeObjectMatchesCurrentFloor(object),
        );
      if (officeHit) {
        // The two click events already performed the object interaction.
        // Suppress only the follow-up dash through that same object/floor.
        event.preventDefault();
        return;
      }
    }
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
    // The same rule for the swing set: a double tap on a seat is a request to
    // ride it, not to dash to the ground the swing hangs over.
    const swingHit = raycaster
      .intersectObjects(interactive, false)
      .find(
        ({ object }) =>
          Number.isInteger(object.userData?.swingSeat) &&
          objectIsEffectivelyVisible(object),
      );
    if (swingHit) {
      event.preventDefault();
      rideSwing(swingHit.object.userData.swingSeat);
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
    // While an object is being dragged by its pink move handle the wheel
    // turns it one step per notch instead of zooming the camera; releasing
    // the drag saves the settled position and heading together.
    if (draggedLayoutObject) {
      rotateActiveLayoutObject(Math.sign(deltaPixels));
      return;
    }
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
    const firstPerson = cameraMode === "first-person";
    const currentZoom = firstPerson ? firstPersonZoom : cameraZoom;
    // Third person pulls the camera back as the wheel scrolls down; through
    // the visitor's own eyes that reads backwards, so first person scrolls
    // the other way — wheel down zooms in on what they are looking at.
    setCameraZoom(
      currentZoom * Math.exp(deltaPixels * (firstPerson ? -0.0015 : 0.0015)),
    );
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
      const canJump =
        officeSceneMode === "town" &&
        !officeCampusSurfaceContains(player.position.x, player.position.z);
      if (canJump && player.position.y <= currentFloorY + 0.02) {
        jumpQueued = true;
      }
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
    updateOfficeElevator(time);
    if (officeSceneMode === "town") {
      walkPlayer(delta, time);
    } else if (officeSceneMode === "lobby") {
      walkOfficeLobbyPlayer(delta, time);
    } else if (officeSceneMode === "meeting") {
      walkOfficeParticipant(delta, time);
    }
    updateOfficeReceptionGuide(time);
    // The Office is part of the same live World. Neighbours and ForkBot keep
    // animating while the local visitor is in the tower instead of freezing
    // the landscape visible through its glass walls.
    updateRemotePlayers(delta, time);
    updateForkbot(delta, time);
    const repositoryLoadingTail = world.userData.repositorySizeLoadingTail;
    if (repositoryLoadingTail?.visible) {
      repositoryLoadingTail.rotation.z = time * 0.008;
      const spark = repositoryLoadingTail.children.at(-1);
      if (spark?.material?.emissive) {
        spark.material.emissiveIntensity = 1.45 + Math.sin(time * 0.02) * 0.75;
      }
    }
    if (!reducedMotion) {
      repositoryPortals.forEach(({ group }) => {
        const halo = group.getObjectByName("repository-selected-halo");
        const marker = group.getObjectByName("repository-live-orbit-marker");
        if (halo?.userData?.repositoryHaloScale) {
          const pulse =
            halo.userData.repositoryHaloScale *
            (1 + Math.sin(time * 0.0032) * 0.055);
          halo.scale.setScalar(pulse);
        }
        if (marker?.userData?.repositoryOrbitRadius) {
          const angle = time * 0.0024;
          const radius = marker.userData.repositoryOrbitRadius;
          marker.position.x = Math.cos(angle) * radius;
          marker.position.y = Math.sin(angle) * radius;
        }
      });
      repositoryPortals.forEach(({ group, key }) => {
        const bornAt = repositoryPortalBornAt.get(key);
        if (!bornAt) return;
        const progress = clamp((time - bornAt) / 1050, 0, 1);
        if (progress <= 0) {
          group.scale.setScalar(0.015);
          return;
        }
        // Overshoot once, then settle into the ring like a portal locking
        // onto its perimeter coordinate.
        const back = 1.70158;
        const shifted = progress - 1;
        const scale =
          1 + (back + 1) * shifted ** 3 + back * shifted ** 2;
        group.scale.setScalar(Math.max(0.015, scale));
        group.rotation.z =
          Math.sin(progress * Math.PI) * (1 - progress) * 0.24;
        if (progress >= 1) {
          group.scale.setScalar(1);
          group.rotation.z = 0;
          repositoryPortalBornAt.delete(key);
        }
      });
      // Node beacons hold a steady colour and size — no pulse — so a status
      // reads the same in a screenshot as it does live. Only degraded and
      // healing nodes carry a sweep, and it turns rather than fades, so the
      // colour itself stays legible in a still frame.
      nodeInfrastructure.forEach((cabinet) => {
        const sweep = cabinet.userData?.beaconSweep;
        if (sweep) sweep.rotation.y = time * 0.0038;
      });
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
      // Most speakers are direct World children, while Noah is nested inside
      // the translated Office reception group. World coordinates keep the
      // shared talk-bubble UX attached correctly in both cases.
      flight.avatar.getWorldPosition(flight.sprite.position);
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
    for (let index = pushSurges.length - 1; index >= 0; index -= 1) {
      const surge = pushSurges[index];
      const progress = Math.min(
        1,
        (performance.now() - surge.startedAt) / surge.duration,
      );
      const fade = 1 - progress;
      // The ring races out to roughly yard scale while the column burns down.
      surge.ring.scale.setScalar(1 + progress * 34);
      surge.ring.material.opacity = 0.8 * fade;
      surge.beam.scale.x = 1 + progress * 0.7;
      surge.beam.scale.z = surge.beam.scale.x;
      surge.beam.material.opacity = 0.85 * fade * fade;
      if (progress >= 1) {
        world.remove(surge.group);
        surge.group.traverse((child) => {
          child.geometry?.dispose?.();
          child.material?.dispose?.();
        });
        pushSurges.splice(index, 1);
      }
    }
    for (let index = serveFlights.length - 1; index >= 0; index -= 1) {
      const flight = serveFlights[index];
      const elapsed = performance.now() - flight.startedAt;
      // Staggered launches wait on the pad, hidden, until their turn.
      if (elapsed < 0) {
        flight.group.visible = false;
        continue;
      }
      flight.group.visible = true;
      const progress = Math.min(1, elapsed / flight.duration);
      // Hard off the cabinet, easing out as it climbs away.
      const eased = 1 - (1 - progress) ** 2.4;
      flight.group.position.y = flight.baseY + eased * flight.climb;
      flight.group.rotation.y += flight.spin * delta;
      flight.group.scale.setScalar(1 - eased * 0.6);
      const fade = progress < 0.55 ? 1 : 1 - (progress - 0.55) / 0.45;
      flight.materials.forEach((material) => {
        material.opacity = Math.max(0, fade);
      });
      if (progress >= 1) {
        world.remove(flight.group);
        flight.group.traverse((child) => {
          child.geometry?.dispose?.();
          child.material?.dispose?.();
        });
        serveFlights.splice(index, 1);
      }
    }
    updateCamera(delta);
    worldSky.tick(Date.now(), camera.position);
    // Landmark/portal proximity is a town-scene concern only (PR #47). PR #47
    // also called updateWorldSun() here; main removed wall-clock sun entirely
    // in favour of fixed full daylight, so that call is not restored.
    if (officeSceneMode === "town") {
      nearestLandmark();
      updateRepositoryPortalLabels();
    }
    updateOfficeSlidingDoors(time, delta);
    updateOfficeLogoReflection(time);
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
        const participantFloorId =
          String(avatar.userData?.officeFloorId || "marketing");
        const sameVisibleFloor =
          !officeElevatorRide &&
          participantFloorId === officeCurrentFloorId;
        const label = officeParticipantLabels.get(id);
        const bubble = officeBubbles.get(id);
        if (!sameVisibleFloor) {
          if (label) {
            label.style.opacity = "0";
            label.style.visibility = "hidden";
          }
          if (bubble) {
            bubble.style.opacity = "0";
            bubble.style.visibility = "hidden";
          }
          return;
        }
        updateScreenLabel(
          THREE,
          avatar,
          label,
          camera,
          rect.width,
          rect.height,
          4.2,
        );
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
      sky: worldSky.getState(),
    };
  }

  function dispose() {
    disposed = true;
    renderer.setAnimationLoop(null);
    worldSky.dispose();
    reflectionTarget.dispose();
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
    setOfficeFloorHandler,
    setOfficeAccess,
    setOfficeAttendance,
    setSelfSecurityDetails,
    setSelfSecurityBadgeVisibility,
    travelToOfficeFloor,
    sitOnOfficeChair,
    setOfficeParticipants,
    updateOfficeMarketingTasks,
    updateWorldBulletin,
    updateSatelliteSky: (snapshot, sgp4Engine) =>
      worldSky.update(snapshot, sgp4Engine),
    updateMastodonKiosk,
    updateMastodonCountdown,
    updateSocialBanners,
    updateSocialBannerTimers,
    updateSystemStatusBoard,
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
    returnToCampfireBench,
    rideSwing,
    dismountSwing,
    setSwingSpeed,
    getSwingState: () => ({
      riding: Boolean(swingRide),
      seat: swingRide ? swingRide.index : -1,
      speedLevel: swingSpeedLevel,
      seats: swingStates.map((swing) => ({
        remoteId: swing.remoteId,
        amplitude: Number(swing.amplitude.toFixed(3)),
      })),
    }),
    setRemotePlayers,
    setAvatarFediverseProfile,
    setAvatarFaceImage,
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
    setRepositoryImportState,
    updateRepositorySizeMap,
    updateRepositoryRecordDesk,
    setRepositoryIssuePageExpanded,
    setRepositorySizeLoading,
    applyWorldLayout,
    setLayoutEditor,
    playEmote,
    showChatBubble,
    showMemberChatBubble,
    greetForkbot,
    exciteForkbot,
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
