import {
  LANDMARKS,
  WORLD_REGIONS,
  landmarkById,
  normalizeWorldStatus,
} from "./world-data.js";

const WORLD_RADIUS = 72;
const WORLD_GROUND_RADIUS = 88;
// Base (from-rest) speed. Raised so keyboard movement leaves standstill with
// more pace by default; multiplied by the per-device move-speed control.
const PLAYER_SPEED = 6.4;
const PLAYER_MAX_SPEED = 13;
const PLAYER_ACCELERATION = 5.4;
const CAMERA_OFFSET = [17, 16, 21];
const CAMERA_DISTANCE = Math.hypot(...CAMERA_OFFSET);
const CAMERA_ZOOM_MIN = 0.12;
const CAMERA_ZOOM_MAX = 3.2;
const CAMERA_LOOK_SENSITIVITY = 0.0022;
const CAMERA_PITCH_MIN = 0.08;
const CAMERA_PITCH_MAX = 1.24;
const LIGHT_LEVEL_MIN = 40;
const LIGHT_LEVEL_MAX = 140;
const LIGHT_LEVEL_DEFAULT = 100;
// Nothing hovers over the Town Square any more. The five unfinished
// destinations are parked on the ground inside the works-in-progress barn, so
// every space shares the same walkable floor as the square itself.
const WORLD_SPACE_FLOORS = Object.freeze({
  "town-square": 0.38,
  east: 0.38,
  central: 0.38,
  west: 0.38,
  "sky-campus": 0.38,
  "space-station": 0.38,
  "code-planet": 0.38,
  "organization-region": 0.38,
  "planet-atlas": 0.38,
});
// The barn sits south of the square, past the last ring of trees and benches
// and behind the default chase camera, and is wide enough that every parked
// destination fits in its own bay.
const WORKSHOP_BARN_CENTER_Z = 62;
const WORKSHOP_BARN_HALF_WIDTH = 22;
const WORKSHOP_BARN_HALF_DEPTH = 8;
const WORKSHOP_BARN_WALL_HEIGHT = 10;
const WORKSHOP_BARN_DOOR_HEIGHT = 5.6;
// The chase camera always sits south of the player and looks north, so the bays
// go at the north end of the barn and visitors arrive in the aisle south of
// them, with the plaques in between.
const WORKSHOP_BARN_BAY_Z = WORKSHOP_BARN_CENTER_Z - 3;
const WORKSHOP_BARN_PLAQUE_Z = WORKSHOP_BARN_CENTER_Z + 1.6;
const WORKSHOP_BARN_AISLE_Z = WORKSHOP_BARN_CENTER_Z + 4.5;
// Bay centres in world coordinates. `y` lifts each exhibit onto its cradle so
// it rests in the barn instead of floating; `plaque` is the work-in-progress
// note standing in front of it.
const WORKSHOP_BARN_BAYS = Object.freeze({
  "sky-campus": Object.freeze({
    x: -16.5,
    y: 0.5,
    radius: 3,
    color: "#d5b6ff",
    plaque: "sky office · unfinished",
  }),
  "organization-region": Object.freeze({
    x: -9,
    y: 0.75,
    radius: 3.7,
    color: "#d5b6ff",
    plaque: "garden campus · unfinished",
  }),
  "code-planet": Object.freeze({
    x: -1,
    y: 3.4,
    radius: 3.4,
    color: "#77d9ff",
    plaque: "code planet · unfinished",
  }),
  "planet-atlas": Object.freeze({
    x: 8,
    y: 1.4,
    radius: 4.1,
    color: "#f7c96b",
    plaque: "community planets · unfinished",
  }),
  "space-station": Object.freeze({
    x: 16.3,
    y: 2.2,
    radius: 2.9,
    color: "#b6d8ff",
    plaque: "space station · unfinished",
  }),
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
const CITY_GRID_EXTENT = 64;
const CITY_GRID_COORDINATES = Object.freeze([-48, -32, -16, 0, 16, 32, 48]);
const REGISTERED_LOUNGE_POSITION = Object.freeze([-48, 0, 32]);
const DURABLE_OBJECT_DISTRICT_POSITION = Object.freeze([48, 0, -32]);
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
  background: "#9ed2bd",
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
    context.fillText(identity.flag || "◌", 256, 86);

    roundedRect(context, 42, 158, 428, 62, 10);
    context.fillStyle = "rgba(255,255,255,0.11)";
    context.fill();
    context.font = '700 27px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#d9ffea";
    context.fillText(
      `${String(identity.browser || "BROWSER").toUpperCase()} · ${String(identity.os || "DEVICE").toUpperCase()}`,
      256,
      189,
    );

    context.font = '700 43px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#ffffff";
    const name = String(identity.name || "guest").slice(0, 15);
    context.fillText(name, 256, 265);

    context.font = '700 21px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#9ef7c6";
    context.fillText(
      firstSeenLabels[identity.firstVisitAge] || firstSeenLabels.hidden,
      256,
      324,
    );
    const activity =
      activityLabels[identity.activityCategory] || activityLabels.hidden;
    const visits = Math.max(0, Math.min(999, Number(identity.visitCount) || 0));
    context.fillStyle = "#77d9ff";
    context.fillText(`${activity} · ${visits} PUBLIC URL VISITS`, 256, 366);

    const statusColors = {
      online: "#9ef7c6",
      available: "#9ef7c6",
      away: "#f7c96b",
      inactive: "#91a39a",
      recent: "#91a39a",
      returning: "#77d9ff",
      hidden: "#65776f",
    };
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
  return new THREE.MeshStandardMaterial({
    color,
    roughness: options.roughness ?? 0.68,
    metalness: options.metalness ?? 0.08,
    emissive: options.emissive || 0x000000,
    emissiveIntensity: options.emissiveIntensity ?? 0,
    transparent: Boolean(options.transparent),
    opacity: options.opacity ?? 1,
    side: options.side,
  });
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

function makeGroundPlaque(THREE, title, subtitle, color) {
  const plaque = new THREE.Group();
  plaque.name = "forkmesh-section-plaque";
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

// Places a section's name plaque on the ground in front of the section — on
// the side facing the Town Square center, where visitors walk up. `position`
// is the section's world position; sections at the center face the arrival
// grid instead.
function addSectionPlaque(THREE, group, position, title, subtitle, color, distance) {
  const x = Array.isArray(position) ? position[0] : 0;
  const z = Array.isArray(position) ? position[2] : 0;
  const length = Math.hypot(x, z);
  const ux = length > 0.001 ? -x / length : 0;
  const uz = length > 0.001 ? -z / length : 1;
  const plaque = makeGroundPlaque(THREE, title, subtitle, color);
  plaque.position.set(ux * distance, 0, uz * distance);
  plaque.rotation.y = Math.atan2(ux, uz);
  group.add(plaque);
  return plaque;
}

function avatarStatusTexture(THREE, emoji, note) {
  return canvasTexture(THREE, 512, 192, (context) => {
    context.clearRect(0, 0, 512, 192);
    roundedRect(context, 8, 8, 496, 176, 32);
    context.fillStyle = "rgba(7,17,15,0.94)";
    context.fill();
    context.strokeStyle = "#9ef7c6";
    context.lineWidth = 6;
    context.stroke();
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font =
      '82px system-ui, "Apple Color Emoji", "Segoe UI Emoji", sans-serif';
    context.fillStyle = "#ffffff";
    context.fillText(emoji, note ? 116 : 256, 96);
    if (note) {
      context.textAlign = "left";
      context.font =
        '700 42px "ForkMesh Mono", ui-monospace, monospace';
      context.fillStyle = "#d9ffea";
      context.fillText(note.slice(0, 20), 188, 98, 282);
    }
  });
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
  if (!status.emoji) return;
  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({
      map: avatarStatusTexture(THREE, status.emoji, status.note),
      transparent: true,
      depthTest: false,
      depthWrite: false,
    }),
  );
  sprite.name = "forkmesh-avatar-emoji-status";
  sprite.position.set(0, 4.75, 0);
  sprite.scale.set(status.note ? 3.25 : 1.6, 1.22, 1);
  sprite.renderOrder = 12;
  avatar.add(sprite);
  avatar.userData.emojiStatusSprite = sprite;
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

  const skinColors = ["#d59a70", "#9a6043", "#f0bd91", "#704832", "#c98255"];
  const skin = makeMaterial(THREE, skinColors[seed % skinColors.length], { roughness: 0.92 });
  const shirt = makeMaterial(THREE, "#ffffff", {
    roughness: 0.8,
  });
  shirt.map = countryShirtTexture(THREE, identity);
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
  hair.position.y = 3.48;
  group.add(hair);

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
  };
  syncOperatorBelt(THREE, group, identity.nodes?.length || 0);
  syncAvatarStatus(THREE, group, identity);
  setShadows(group, true, true);
  return group;
}

function syncCountryShirt(THREE, avatar, identity) {
  const shirt = avatar?.userData?.shirt;
  if (!shirt) return;
  const previous = shirt.map;
  shirt.map = countryShirtTexture(THREE, identity);
  shirt.color.set("#ffffff");
  shirt.needsUpdate = true;
  previous?.dispose?.();
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

function createRoad(THREE, width, depth, x, z, rotation = 0) {
  const road = new THREE.Mesh(
    new THREE.BoxGeometry(width, 0.06, depth),
    makeMaterial(THREE, "#18372c", { roughness: 0.96 }),
  );
  road.position.set(x, 0.055, z);
  road.rotation.y = rotation;
  road.receiveShadow = true;
  return road;
}

function createElectricMeshCityGrid(THREE, animated) {
  const grid = new THREE.Group();
  grid.name = "electric-mesh-city-block-grid";
  grid.userData.blockSize = 16;
  grid.userData.extent = CITY_GRID_EXTENT;

  const conduitMaterial = makeMaterial(THREE, "#72f2bd", {
    emissive: "#2bcc86",
    emissiveIntensity: 1.05,
    metalness: 0.28,
    roughness: 0.32,
  });
  const junctionMaterial = makeMaterial(THREE, "#8ce7ff", {
    emissive: "#35acd0",
    emissiveIntensity: 1.2,
    metalness: 0.35,
    roughness: 0.28,
  });
  const conduits = [];
  const junctions = [];

  CITY_GRID_COORDINATES.forEach((coordinate) => {
    const northSouth = createRoad(
      THREE,
      3.6,
      CITY_GRID_EXTENT * 2,
      coordinate,
      0,
    );
    northSouth.userData.cityGridRoad = true;
    grid.add(northSouth);
    const eastWest = createRoad(
      THREE,
      CITY_GRID_EXTENT * 2,
      3.6,
      0,
      coordinate,
    );
    eastWest.userData.cityGridRoad = true;
    grid.add(eastWest);

    const verticalConduit = new THREE.Mesh(
      new THREE.BoxGeometry(0.075, 0.035, CITY_GRID_EXTENT * 2 - 2),
      conduitMaterial,
    );
    verticalConduit.position.set(coordinate, 0.105, 0);
    verticalConduit.userData.electricMeshConduit = true;
    grid.add(verticalConduit);
    conduits.push(verticalConduit);

    const horizontalConduit = new THREE.Mesh(
      new THREE.BoxGeometry(CITY_GRID_EXTENT * 2 - 2, 0.035, 0.075),
      conduitMaterial,
    );
    horizontalConduit.position.set(0, 0.105, coordinate);
    horizontalConduit.userData.electricMeshConduit = true;
    grid.add(horizontalConduit);
    conduits.push(horizontalConduit);
  });

  CITY_GRID_COORDINATES.forEach((x) => {
    CITY_GRID_COORDINATES.forEach((z) => {
      const junction = new THREE.Mesh(
        new THREE.CylinderGeometry(0.16, 0.2, 0.08, 8),
        junctionMaterial,
      );
      junction.position.set(x, 0.14, z);
      junction.userData.electricMeshJunction = true;
      grid.add(junction);
      junctions.push(junction);
    });
  });

  animated.push((time) => {
    const pulse = (Math.sin(time * 0.0012) + 1) * 0.5;
    conduitMaterial.emissiveIntensity = 0.8 + pulse * 0.45;
    junctionMaterial.emissiveIntensity = 0.95 + (1 - pulse) * 0.55;
    if (junctions.length) {
      const active = Math.floor(time / 180) % junctions.length;
      junctions.forEach((junction, index) => {
        junction.scale.y = index === active ? 2.2 : 1;
      });
    }
  });
  grid.userData.conduitCount = conduits.length;
  grid.userData.junctionCount = junctions.length;
  return grid;
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
    version: node?.version,
    platform: node?.platform,
    lastSync: node?.lastSync,
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

function serverPanelTexture(THREE, node) {
  const online = mirrorNodeIsOnline(node);
  const integrity = String(node?.integrity || "unknown").toLowerCase();
  const repo = Array.isArray(node?.repositories) ? node.repositories[0] : null;
  const repositoryLabel =
    repo?.owner && repo?.name
      ? `${String(repo.owner).slice(0, 32)}/${String(repo.name).slice(0, 44)}`
      : "REPOSITORY NOT REPORTED";
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
  const statusColors = [
    online ? "#73f0ad" : "#71837a",
    node?.cloneAvailable === true ? "#77d9ff" : online ? "#f7c96b" : "#71837a",
    integrity === "ok"
      ? "#73f0ad"
      : integrity === "rejected" || integrity === "degraded"
        ? "#ff7e88"
        : integrity === "healing"
          ? "#f7c96b"
          : "#71837a",
  ];
  const statusLights = new THREE.Group();
  statusColors.forEach((color, index) => {
    const light = new THREE.Mesh(
      new THREE.SphereGeometry(0.075, 12, 10),
      makeMaterial(THREE, color, {
        emissive: color,
        emissiveIntensity: online ? 1.25 : 0.25,
        metalness: 0.22,
        roughness: 0.26,
      }),
    );
    light.position.set(-0.24 + index * 0.24, 3.08, 0.76);
    statusLights.add(light);
  });
  group.add(statusLights);

  const vent = new THREE.Mesh(
    new THREE.BoxGeometry(1.42, 0.26, 0.05),
    makeMaterial(THREE, "#35463e", {
      metalness: 0.76,
      roughness: 0.38,
    }),
  );
  vent.position.set(0, 0.22, 0.75);
  group.add(vent);
  group.userData.signalRing = statusLights.children[0];
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

function createRegisteredUserLounge(THREE, animated) {
  const lounge = new THREE.Group();
  lounge.name = "registered-user-lounge";
  lounge.userData.spaceKind = "registered-user-lounge";
  lounge.position.set(...REGISTERED_LOUNGE_POSITION);
  const base = new THREE.Mesh(
    new THREE.CylinderGeometry(7.4, 7.8, 0.42, 12),
    makeMaterial(THREE, "#1a3f34", {
      emissive: "#154f3b",
      emissiveIntensity: 0.28,
      roughness: 0.62,
    }),
  );
  base.position.y = 0.21;
  lounge.add(base);
  const canopy = new THREE.Mesh(
    new THREE.TorusGeometry(5.8, 0.16, 10, 64),
    makeMaterial(THREE, "#9ef7c6", {
      emissive: "#39c783",
      emissiveIntensity: 0.8,
      metalness: 0.3,
    }),
  );
  canopy.rotation.x = Math.PI / 2;
  canopy.position.y = 4.2;
  lounge.add(canopy);
  for (const angle of [0, Math.PI / 2, Math.PI, Math.PI * 1.5]) {
    const column = new THREE.Mesh(
      new THREE.CylinderGeometry(0.12, 0.17, 3.9, 8),
      makeMaterial(THREE, "#507d68", { metalness: 0.22 }),
    );
    column.position.set(Math.cos(angle) * 5.8, 2.1, Math.sin(angle) * 5.8);
    lounge.add(column);
  }
  const seatOffsets = [];
  for (let row = 0; row < 3; row += 1) {
    for (let column = 0; column < 6; column += 1) {
      const x = -4.5 + column * 1.8;
      const z = -1.8 + row * 1.8;
      const seat = new THREE.Mesh(
        new THREE.BoxGeometry(1.25, 0.25, 1.05),
        makeMaterial(THREE, row === 0 ? "#5ca783" : "#3f755e", {
          emissive: row === 0 ? "#245c46" : "#173d2e",
          emissiveIntensity: 0.35,
        }),
      );
      seat.position.set(x, 0.53, z);
      lounge.add(seat);
      seatOffsets.push(new THREE.Vector3(x, 0.38, z));
    }
  }
  lounge.userData.seatOffsets = seatOffsets;
  addSectionPlaque(
    THREE,
    lounge,
    REGISTERED_LOUNGE_POSITION,
    "MEMBER LOUNGE",
    "registered contributors · recent activity glows",
    "#9ef7c6",
    9.2,
  );
  const memberCountSign = makeLabelSprite(
    THREE,
    "MEMBERS",
    "counting registered users",
    "#9ef7c6",
  );
  memberCountSign.scale.set(5.6, 1.9, 1);
  // At the base edge facing the Town Square center, so the total reads
  // before a visitor walks into the lounge itself.
  memberCountSign.position.set(6.9, 1.8, -4.6);
  lounge.add(memberCountSign);
  lounge.userData.memberCountSign = memberCountSign;
  const activityBeacon = new THREE.PointLight("#9ef7c6", 1.4, 18, 2);
  activityBeacon.position.set(0, 4.2, 0);
  lounge.add(activityBeacon);
  lounge.userData.activityBeacon = activityBeacon;
  setShadows(lounge);
  animated.push((time) => {
    const pulse = (Math.sin(time * 0.0018) + 1) * 0.5;
    canopy.material.emissiveIntensity = 0.55 + pulse * 0.55;
    activityBeacon.intensity = 0.9 + pulse * 1.1;
  });
  return lounge;
}

function createDurableObjectDistrict(THREE) {
  const district = new THREE.Group();
  district.name = "durable-object-infrastructure";
  district.userData.infrastructureKind = "durable-objects";
  district.userData.metricsAvailable = false;
  district.position.set(...DURABLE_OBJECT_DISTRICT_POSITION);
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
    DURABLE_OBJECT_DISTRICT_POSITION,
    "DURABLE OBJECTS",
    "usage appears only with explicit configured limits",
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
  emptyMarker.name = "durable-object-metrics-unavailable";
  emptyMarker.position.y = 1.4;
  emptyMarker.rotation.x = Math.PI / 2;
  district.add(emptyMarker);
  setShadows(district);
  return district;
}

function durableObjectMetricPairs(record) {
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

function createInformationBooth(THREE, position, interactive) {
  const group = new THREE.Group();
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(4.4, 0.35, 3.3),
    makeMaterial(THREE, "#dbeee3"),
  );
  base.position.y = 0.18;
  group.add(base);

  const counter = new THREE.Mesh(
    new THREE.BoxGeometry(4, 1.45, 0.72),
    makeMaterial(THREE, "#164c37"),
  );
  counter.position.set(0, 1, -0.92);
  group.add(counter);

  for (const x of [-1.75, 1.75]) {
    const post = new THREE.Mesh(
      new THREE.BoxGeometry(0.18, 2.8, 0.18),
      makeMaterial(THREE, "#e8fff1"),
    );
    post.position.set(x, 1.6, 0);
    group.add(post);
  }

  const canopy = new THREE.Mesh(
    new THREE.BoxGeometry(4.55, 0.2, 3.05),
    makeMaterial(THREE, "#9ef7c6", {
      emissive: "#1e704a",
      emissiveIntensity: 0.42,
    }),
  );
  canopy.position.y = 3.02;
  group.add(canopy);

  addSectionPlaque(THREE, group, position, "START HERE", "information booth", "#9ef7c6", 3.6);

  const beacon = new THREE.PointLight("#9ef7c6", 2.4, 10, 1.8);
  beacon.position.set(0, 3.5, 0);
  group.add(beacon);

  group.position.set(...position);
  group.userData.landmark = "information";
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "information";
      interactive.push(child);
    }
  });
  setShadows(group);
  return group;
}

function createFountain(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const stone = makeMaterial(THREE, "#c5d8cf", { roughness: 0.62 });
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

  const foundation = new THREE.Mesh(new THREE.CylinderGeometry(4.9, 5.25, 0.5, 48), stone);
  foundation.position.y = 0.25;
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

  addSectionPlaque(
    THREE,
    group,
    position,
    "GLOBAL REWARD POOL",
    "public on-chain balance · external signer",
    "#f7c96b",
    6.4,
  );

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
  for (let index = 0; index < 3; index += 1) {
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(2.4 - index * 0.48, 0.11, 12, 64),
      ringMaterial,
    );
    ring.position.y = 3.1;
    ring.rotation.y = index * 0.15;
    portal.add(ring);
  }
  const frameLeft = new THREE.Mesh(new THREE.BoxGeometry(0.42, 5.8, 0.58), frameMaterial);
  frameLeft.position.set(-2.75, 2.9, 0);
  portal.add(frameLeft);
  const frameRight = frameLeft.clone();
  frameRight.position.x = 2.75;
  portal.add(frameRight);

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
    portal.add(file);
  }
  group.add(portal);

  const plinth = new THREE.Mesh(
    new THREE.BoxGeometry(7.5, 0.45, 4.2),
    makeMaterial(THREE, "#18313a"),
  );
  plinth.position.y = 0.23;
  group.add(plinth);
  addSectionPlaque(THREE, group, position, "REPOSITORIES", "walk through the code", "#77d9ff", 4.4);

  group.position.set(...position);
  group.userData.landmark = "repositories";
  group.userData.fileMeshes = files;
  group.userData.portal = portal;
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "repositories";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    portal.rotation.y = Math.sin(time * 0.00018) * 0.09;
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

function createRoutingStation(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const base = new THREE.Mesh(
    new THREE.CylinderGeometry(3.5, 3.9, 0.42, 12),
    makeMaterial(THREE, "#12313b", { roughness: 0.68 }),
  );
  base.position.y = 0.22;
  group.add(base);

  const tower = new THREE.Mesh(
    new THREE.CylinderGeometry(0.5, 0.86, 4.7, 10),
    makeMaterial(THREE, "#80e8ff", {
      metalness: 0.28,
      roughness: 0.34,
      emissive: "#13586c",
      emissiveIntensity: 0.72,
    }),
  );
  tower.position.y = 2.65;
  group.add(tower);

  const routeRing = new THREE.Group();
  routeRing.position.y = 3.25;
  for (let index = 0; index < 3; index += 1) {
    const angle = (index / 3) * Math.PI * 2;
    const endpoint = new THREE.Mesh(
      new THREE.BoxGeometry(0.84, 0.62, 0.84),
      makeMaterial(THREE, index === 0 ? "#9ef7c6" : "#77d9ff", {
        emissive: index === 0 ? "#235f44" : "#164d63",
        emissiveIntensity: 0.62,
      }),
    );
    endpoint.position.set(Math.cos(angle) * 2.4, 0, Math.sin(angle) * 2.4);
    endpoint.userData.endpoint = true;
    routeRing.add(endpoint);

    const beam = new THREE.Mesh(
      new THREE.BoxGeometry(2.05, 0.08, 0.08),
      makeMaterial(THREE, "#80e8ff", {
        transparent: true,
        opacity: 0.65,
        emissive: "#80e8ff",
        emissiveIntensity: 0.9,
      }),
    );
    beam.position.set(Math.cos(angle) * 1.18, 0, Math.sin(angle) * 1.18);
    beam.rotation.y = -angle;
    routeRing.add(beam);
  }
  group.add(routeRing);

  const dish = new THREE.Mesh(
    new THREE.TorusGeometry(0.9, 0.09, 10, 36),
    makeMaterial(THREE, "#e6fbff", {
      emissive: "#80e8ff",
      emissiveIntensity: 0.55,
    }),
  );
  dish.position.y = 5.35;
  dish.rotation.x = Math.PI / 2;
  group.add(dish);

  addSectionPlaque(
    THREE,
    group,
    position,
    "ROUTING STATION",
    "healthy HTTPS mirrors",
    "#80e8ff",
    5,
  );

  const finished = finishLandmark(group, "routing", position, interactive);
  animated.push((time) => {
    routeRing.rotation.y = time * 0.00022;
    routeRing.children.forEach((child, index) => {
      if (child.userData.endpoint) {
        child.position.y = Math.sin(time * 0.0018 + index) * 0.16;
      }
    });
    dish.rotation.z = time * 0.00045;
  });
  return finished;
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

function createLaunchpad(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const padMaterial = makeMaterial(THREE, "#213c4b", { roughness: 0.58 });
  const glowMaterial = makeMaterial(THREE, "#b6d8ff", {
    emissive: "#5e9bd9",
    emissiveIntensity: 0.8,
    roughness: 0.28,
  });
  const base = new THREE.Mesh(new THREE.CylinderGeometry(4.4, 4.8, 0.55, 12), padMaterial);
  base.position.y = 0.28;
  group.add(base);
  for (let index = 0; index < 4; index += 1) {
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(1.7 + index * 0.5, 0.055, 8, 64),
      glowMaterial,
    );
    ring.rotation.x = Math.PI / 2;
    ring.position.y = 0.61 + index * 0.09;
    ring.userData.spin = index % 2 ? -1 : 1;
    group.add(ring);
  }
  const portal = new THREE.Mesh(new THREE.TorusGeometry(2.1, 0.13, 12, 64), glowMaterial);
  portal.position.y = 3.1;
  group.add(portal);
  const portalFill = new THREE.Mesh(
    new THREE.CircleGeometry(2, 48),
    makeMaterial(THREE, "#6a90d5", {
      transparent: true,
      opacity: 0.26,
      emissive: "#4b68a8",
      emissiveIntensity: 1.2,
      side: THREE.DoubleSide,
    }),
  );
  portalFill.position.y = 3.1;
  group.add(portalFill);
  addSectionPlaque(THREE, group, position, "LAUNCHPAD", "events · sky offices · worlds", "#b6d8ff", 6);
  group.position.set(...position);
  group.userData.landmark = "launchpad";
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "launchpad";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    portal.rotation.z = time * 0.0003;
    portalFill.material.opacity = 0.2 + Math.sin(time * 0.0015) * 0.08;
    group.children.forEach((child) => {
      if (child.userData.spin) child.rotation.z += 0.003 * child.userData.spin;
    });
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

  // A quiet, non-punitive waiting area. No avatar is placed here unless that
  // contributor has elected to share an away/inactive state.
  for (let row = 0; row < 3; row += 1) {
    const bleacher = new THREE.Mesh(
      new THREE.BoxGeometry(6.7 - row * 0.45, 0.28, 0.7),
      makeMaterial(THREE, "#6c5539"),
    );
    bleacher.position.set(0, 0.35 + row * 0.34, 4.1 + row * 0.52);
    group.add(bleacher);
  }
  const quietSign = makeLabelSprite(
    THREE,
    "QUIET SEATING",
    "away status is optional",
    "#b8e986",
  );
  quietSign.scale.set(4.2, 1.4, 1);
  quietSign.position.set(0, 2.3, 5.0);
  group.add(quietSign);
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

function createCodeWorkshops(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const base = new THREE.Mesh(
    new THREE.BoxGeometry(8.2, 0.45, 5.8),
    makeMaterial(THREE, "#153a2a"),
  );
  base.position.y = 0.23;
  group.add(base);
  const benches = [];
  for (let index = 0; index < 4; index += 1) {
    const bench = new THREE.Mesh(
      new THREE.BoxGeometry(2.7, 0.38, 1.25),
      makeMaterial(THREE, "#3e6f55"),
    );
    bench.position.set(index % 2 ? 1.8 : -1.8, 1.0, index < 2 ? -1.4 : 1.4);
    group.add(bench);
    const graph = new THREE.Mesh(
      new THREE.IcosahedronGeometry(0.52 + index * 0.08, 1),
      makeMaterial(THREE, ["#73f0ad", "#77d9ff", "#d5b6ff", "#f7c96b"][index], {
        emissive: ["#259059", "#237a99", "#60458b", "#9d7023"][index],
        emissiveIntensity: 0.72,
      }),
    );
    graph.position.set(bench.position.x, 1.85, bench.position.z);
    graph.userData.phase = index;
    benches.push(graph);
    group.add(graph);
  }
  addSectionPlaque(
    THREE,
    group,
    position,
    "CODE WORKSHOPS",
    "analysis + collaboration",
    "#73f0ad",
    5.4,
  );
  finishLandmark(group, "workshops", position, interactive);
  animated.push((time) => {
    benches.forEach((graph, index) => {
      graph.rotation.y = time * 0.0005 * (index % 2 ? -1 : 1);
      graph.position.y = 1.85 + Math.sin(time * 0.0012 + index) * 0.16;
    });
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

function createSupportCenter(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const base = new THREE.Mesh(
    new THREE.CylinderGeometry(3.6, 4.1, 0.48, 12),
    makeMaterial(THREE, "#3a3025", { metalness: 0.16 }),
  );
  base.position.y = 0.24;
  group.add(base);
  const desk = new THREE.Mesh(
    new THREE.BoxGeometry(4.8, 1.35, 1.35),
    makeMaterial(THREE, "#cf9f61", {
      emissive: "#765026",
      emissiveIntensity: 0.28,
    }),
  );
  desk.position.set(0, 1.0, 0);
  group.add(desk);
  for (let index = 0; index < 3; index += 1) {
    const beacon = new THREE.Mesh(
      new THREE.OctahedronGeometry(0.36, 0),
      makeMaterial(THREE, ["#ffd08f", "#9ef7c6", "#8fcfff"][index], {
        emissive: ["#a86b28", "#2f8f60", "#356f9f"][index],
        emissiveIntensity: 0.75,
      }),
    );
    beacon.position.set((index - 1) * 1.35, 2.3, 0);
    group.add(beacon);
    animated.push((time) => {
      beacon.position.y = 2.3 + Math.sin(time * 0.0015 + index) * 0.12;
      beacon.rotation.y = time * 0.0006 * (index % 2 ? -1 : 1);
    });
  }
  addSectionPlaque(
    THREE,
    group,
    position,
    "SUPPORT CENTER",
    "voluntary · transparent · no returns",
    "#ffd08f",
    5.2,
  );
  group.position.set(...position);
  group.userData.landmark = "support";
  group.traverse((child) => {
    if (!child.isMesh) return;
    child.userData.landmark = "support";
    interactive.push(child);
  });
  setShadows(group);
  return group;
}

// The barn that replaced the sky. Every destination that used to hover over the
// Town Square is parked here on the ground, one per bay, under a roof frame
// that is itself unfinished, behind doorways wide enough to roll them back out
// once the work is done.
function createWorkshopBarn(THREE) {
  const barn = new THREE.Group();
  barn.name = "works-in-progress-barn";
  const plank = makeMaterial(THREE, "#8c3f2e", { roughness: 0.86 });
  const trim = makeMaterial(THREE, "#e7d8bd", { roughness: 0.72 });
  const roofing = makeMaterial(THREE, "#5a4335", { roughness: 0.82 });
  const halfWidth = WORKSHOP_BARN_HALF_WIDTH;
  const halfDepth = WORKSHOP_BARN_HALF_DEPTH;
  const wallHeight = WORKSHOP_BARN_WALL_HEIGHT;

  const floor = new THREE.Mesh(
    new THREE.BoxGeometry(halfWidth * 2, 0.3, halfDepth * 2),
    makeMaterial(THREE, "#4b453d", { roughness: 0.94 }),
  );
  floor.position.y = 0.15;
  barn.add(floor);

  // Both ends are full-width doorways and the long sides are stall-height plank
  // walls under open timber framing. Nothing above knee height stands between
  // the chase camera and the parked work, whichever way a visitor faces.
  const stallHeight = 3.2;
  for (const side of [-1, 1]) {
    const stall = new THREE.Mesh(
      new THREE.BoxGeometry(0.6, stallHeight, halfDepth * 2),
      plank,
    );
    stall.position.set(side * (halfWidth - 0.3), stallHeight / 2, 0);
    barn.add(stall);
    const plate = new THREE.Mesh(
      new THREE.BoxGeometry(0.6, 0.5, halfDepth * 2),
      trim,
    );
    plate.position.set(side * (halfWidth - 0.3), wallHeight - 0.25, 0);
    barn.add(plate);
  }

  // Posts and header beams frame both doorways. The mid posts land in the gaps
  // between bays, so each end reads as a row of garage doors rather than one
  // undivided hole.
  const headerHeight = wallHeight - WORKSHOP_BARN_DOOR_HEIGHT;
  for (const end of [-1, 1]) {
    const endZ = end * (halfDepth - 0.3);
    for (const x of [-(halfWidth - 0.3), -13.1, 12.75, halfWidth - 0.3]) {
      const post = new THREE.Mesh(
        new THREE.BoxGeometry(0.7, wallHeight, 0.7),
        plank,
      );
      post.position.set(x, wallHeight / 2, endZ);
      barn.add(post);
    }
    // Only the town-facing side carries a header. Leaving the aisle side open
    // to the trusses keeps the beam out of the arriving camera's sightline.
    if (end > 0) continue;
    const header = new THREE.Mesh(
      new THREE.BoxGeometry(halfWidth * 2, headerHeight, 0.6),
      plank,
    );
    header.position.set(
      0,
      WORKSHOP_BARN_DOOR_HEIGHT + headerHeight / 2,
      endZ,
    );
    barn.add(header);
  }
  for (const side of [-1, 1]) {
    for (const half of [-1, 1]) {
      const brace = new THREE.Mesh(
        new THREE.BoxGeometry(0.18, 0.34, 8.4),
        trim,
      );
      brace.position.set(
        side * (halfWidth - 0.3),
        stallHeight + 1.9,
        (half * halfDepth) / 2,
      );
      brace.rotation.x = half * 0.55;
      barn.add(brace);
    }
  }

  // The gambrel roof is still only its frame: six trusses carrying five
  // purlins, and no decking. That keeps the barn itself honestly unfinished and
  // leaves the bays lit and readable from the raised camera.
  const trussSegments = [
    { z: 6.7, y: 11.2, length: 4.84, tilt: 0.519 },
    { z: 2.3, y: 13.3, length: 4.94, tilt: 0.373 },
  ];
  for (const x of [-halfWidth + 0.3, -13.1, -4.4, 4.4, 12.75, halfWidth - 0.3]) {
    trussSegments.forEach((segment) => {
      for (const side of [-1, 1]) {
        const beam = new THREE.Mesh(
          new THREE.BoxGeometry(0.34, 0.34, segment.length),
          roofing,
        );
        beam.position.set(x, segment.y, side * segment.z);
        beam.rotation.x = side * segment.tilt;
        barn.add(beam);
      }
    });
  }
  const purlins = [
    { y: wallHeight, z: halfDepth + 0.8 },
    { y: wallHeight, z: -halfDepth - 0.8 },
    { y: 12.4, z: 4.6 },
    { y: 12.4, z: -4.6 },
    { y: 14.2, z: 0 },
  ];
  purlins.forEach((purlin) => {
    const beam = new THREE.Mesh(
      new THREE.BoxGeometry(halfWidth * 2 + 1.2, 0.26, 0.26),
      roofing,
    );
    beam.position.set(0, purlin.y, purlin.z);
    barn.add(beam);
  });

  // The barn name goes on both faces of the north header: the outer face reads
  // on the walk down from the Town Square, the inner one from the aisle.
  const signTexture = wordTexture(
    THREE,
    "WORKS IN PROGRESS BARN",
    "parked destinations · unfinished",
    "#f7c96b",
  );
  for (const facing of [-1, 1]) {
    const sign = new THREE.Mesh(
      new THREE.PlaneGeometry(13.2, 4.4),
      new THREE.MeshBasicMaterial({ map: signTexture, transparent: true }),
    );
    sign.position.set(
      0,
      WORKSHOP_BARN_DOOR_HEIGHT + headerHeight / 2,
      -halfDepth + 0.3 + facing * 0.35,
    );
    if (facing < 0) sign.rotation.y = Math.PI;
    barn.add(sign);
  }

  // Bay furniture: a plinth per destination, a mount post under the ones that
  // are held clear of the floor, and a plaque saying the work is unfinished.
  const bayZ = WORKSHOP_BARN_BAY_Z - WORKSHOP_BARN_CENTER_Z;
  Object.values(WORKSHOP_BARN_BAYS).forEach((bay) => {
    const plinth = new THREE.Mesh(
      new THREE.CylinderGeometry(bay.radius, bay.radius + 0.3, 0.5, 14),
      makeMaterial(THREE, "#2c2a25", { roughness: 0.88 }),
    );
    plinth.position.set(bay.x, 0.4, bayZ);
    barn.add(plinth);
    if (bay.y > 1.1) {
      const post = new THREE.Mesh(
        new THREE.CylinderGeometry(0.32, 0.5, bay.y - 0.65, 10),
        makeMaterial(THREE, "#6b665c", { metalness: 0.3, roughness: 0.6 }),
      );
      post.position.set(bay.x, 0.65 + (bay.y - 0.65) / 2, bayZ);
      barn.add(post);
      const cradle = new THREE.Mesh(
        new THREE.TorusGeometry(0.75, 0.12, 8, 20),
        makeMaterial(THREE, "#f7c96b", {
          emissive: "#7a5a17",
          emissiveIntensity: 0.4,
        }),
      );
      cradle.rotation.x = Math.PI / 2;
      cradle.position.set(bay.x, bay.y - 0.6, bayZ);
      barn.add(cradle);
    }
    const plaque = makeGroundPlaque(
      THREE,
      "WORK IN PROGRESS",
      bay.plaque,
      bay.color,
    );
    plaque.position.set(
      bay.x,
      0.3,
      WORKSHOP_BARN_PLAQUE_Z - WORKSHOP_BARN_CENTER_Z,
    );
    // makeGroundPlaque faces +z, which is already the aisle side.
    barn.add(plaque);
  });

  for (const x of [-15, -5, 5, 15]) {
    const lamp = new THREE.Mesh(
      new THREE.CylinderGeometry(0.55, 0.42, 0.22, 12),
      makeMaterial(THREE, "#ffe9b0", {
        emissive: "#ffd27a",
        emissiveIntensity: 0.7,
      }),
    );
    lamp.position.set(x, wallHeight - 0.7, 0);
    barn.add(lamp);
  }

  // The barn never casts: a 44-unit roof would drop the whole interior into
  // shadow and hide the work parked underneath it.
  setShadows(barn, false, true);
  barn.position.set(0, 0, WORKSHOP_BARN_CENTER_Z);
  return barn;
}

// The sky office kept its name and its cloud raft, but the raft is now a
// deflated prop sitting on the barn floor under the office it used to carry.
function createSkyOffice(THREE) {
  const group = new THREE.Group();
  const bay = WORKSHOP_BARN_BAYS["sky-campus"];
  const cloudMaterial = makeMaterial(THREE, "#d8edf2", {
    transparent: true,
    opacity: 0.72,
    roughness: 0.9,
  });
  for (let index = 0; index < 9; index += 1) {
    const cloud = new THREE.Mesh(
      new THREE.SphereGeometry(1.2 + (index % 3) * 0.28, 16, 12),
      cloudMaterial,
    );
    cloud.scale.y = 0.22;
    cloud.position.set((index % 3) * 1.7 - 1.7, 0.16, (index % 2) * 1.35 - 0.68);
    group.add(cloud);
  }
  const office = new THREE.Mesh(
    new THREE.BoxGeometry(4.6, 2.25, 3.4),
    makeMaterial(THREE, "#d5b6ff", {
      transparent: true,
      opacity: 0.76,
      metalness: 0.18,
      roughness: 0.34,
    }),
  );
  office.position.y = 1.55;
  group.add(office);
  const roof = new THREE.Mesh(
    new THREE.ConeGeometry(3.2, 1.15, 4),
    makeMaterial(THREE, "#4b3b6b"),
  );
  roof.position.y = 3.25;
  roof.rotation.y = Math.PI / 4;
  group.add(roof);
  const label = makeLabelSprite(
    THREE,
    "SKY OFFICE",
    "work in progress · parked indoors",
    "#d5b6ff",
  );
  label.position.y = 4.6;
  group.add(label);
  group.position.set(bay.x, bay.y, WORKSHOP_BARN_BAY_Z);
  group.scale.setScalar(0.86);
  setShadows(group);
  return group;
}

// The other worlds are unfinished, so none of them orbits overhead any more:
// each one stands in its own barn bay, on the plinth and mount post that
// createWorkshopBarn puts under it.
function createOtherWorlds(THREE, animated) {
  const destinations = new THREE.Group();
  destinations.name = "functional-world-destinations";
  const bayPosition = (spaceId) => {
    const bay = WORKSHOP_BARN_BAYS[spaceId];
    return [bay.x, bay.y, WORKSHOP_BARN_BAY_Z];
  };

  const station = new THREE.Group();
  const stationCore = new THREE.Mesh(
    new THREE.SphereGeometry(1.4, 20, 16),
    makeMaterial(THREE, "#b6d8ff", {
      emissive: "#456fac",
      emissiveIntensity: 0.65,
      metalness: 0.55,
      roughness: 0.24,
    }),
  );
  station.add(stationCore);
  const stationRing = new THREE.Mesh(
    new THREE.TorusGeometry(2.5, 0.22, 12, 64),
    makeMaterial(THREE, "#77d9ff", {
      emissive: "#237a99",
      emissiveIntensity: 0.72,
      metalness: 0.48,
    }),
  );
  stationRing.rotation.x = Math.PI / 2.8;
  station.add(stationRing);
  const stationLabel = makeLabelSprite(
    THREE,
    "SPACE STATION",
    "work in progress · chat rooms",
    "#b6d8ff",
  );
  stationLabel.position.y = 3.7;
  station.add(stationLabel);
  station.position.set(...bayPosition("space-station"));
  destinations.add(station);

  const codePlanet = new THREE.Group();
  const codeGlobe = new THREE.Mesh(
    new THREE.IcosahedronGeometry(2.2, 2),
    makeMaterial(THREE, "#153a46", {
      emissive: "#1c718d",
      emissiveIntensity: 0.48,
      metalness: 0.2,
    }),
  );
  codePlanet.add(codeGlobe);
  for (let index = 0; index < 14; index += 1) {
    const file = new THREE.Mesh(
      new THREE.BoxGeometry(0.22, 0.3, 0.06),
      makeMaterial(THREE, ["#77d9ff", "#9ef7c6", "#d5b6ff"][index % 3], {
        emissive: "#276f73",
        emissiveIntensity: 0.55,
      }),
    );
    const angle = (index / 14) * Math.PI * 2;
    file.position.set(Math.cos(angle) * 3, Math.sin(angle * 2) * 0.75, Math.sin(angle) * 3);
    codePlanet.add(file);
  }
  const codeLabel = makeLabelSprite(
    THREE,
    "CODE PLANET",
    "work in progress · repository world",
    "#77d9ff",
  );
  codeLabel.position.y = 4.1;
  codePlanet.add(codeLabel);
  codePlanet.position.set(...bayPosition("code-planet"));
  destinations.add(codePlanet);

  const orgRegion = new THREE.Group();
  const garden = new THREE.Mesh(
    new THREE.CylinderGeometry(3.1, 3.5, 0.5, 18),
    makeMaterial(THREE, "#3c684d"),
  );
  orgRegion.add(garden);
  for (let index = 0; index < 18; index += 1) {
    const flower = new THREE.Mesh(
      new THREE.IcosahedronGeometry(0.22 + (index % 3) * 0.05, 1),
      makeMaterial(THREE, ["#d5b6ff", "#ff9eb7", "#9ef7c6"][index % 3], {
        emissive: "#60458b",
        emissiveIntensity: 0.42,
      }),
    );
    const angle = (index / 18) * Math.PI * 2;
    flower.position.set(Math.cos(angle) * (1.2 + (index % 3) * 0.65), 0.62, Math.sin(angle) * (1.2 + (index % 3) * 0.65));
    orgRegion.add(flower);
  }
  const orgLabel = makeLabelSprite(
    THREE,
    "GARDEN CAMPUS",
    "work in progress · org region",
    "#d5b6ff",
  );
  orgLabel.position.y = 3.8;
  orgRegion.add(orgLabel);
  orgRegion.position.set(...bayPosition("organization-region"));
  destinations.add(orgRegion);

  const planetAtlas = new THREE.Group();
  ["#f7c96b", "#ff9eb7", "#8fcfff"].forEach((color, index) => {
    const planet = new THREE.Mesh(
      new THREE.SphereGeometry(0.65 + index * 0.28, 18, 14),
      makeMaterial(THREE, color, {
        emissive: color,
        emissiveIntensity: 0.28,
      }),
    );
    planet.position.set((index - 1) * 2.7, index * 0.65, 0);
    planetAtlas.add(planet);
  });
  const atlasLabel = makeLabelSprite(
    THREE,
    "COMMUNITY PLANETS",
    "work in progress · events · regions",
    "#f7c96b",
  );
  atlasLabel.position.y = 3.8;
  planetAtlas.add(atlasLabel);
  planetAtlas.position.set(...bayPosition("planet-atlas"));
  destinations.add(planetAtlas);

  // Parked exhibits still turn on their mounts; nothing drifts up and down any
  // more, because everything is resting on the barn floor.
  animated.push((time) => {
    station.rotation.y = time * 0.00016;
    stationRing.rotation.z = time * 0.0004;
    codePlanet.rotation.y = -time * 0.00011;
    planetAtlas.children.forEach((child, index) => {
      if (child.isMesh) child.rotation.y += 0.0008 * (index + 1);
    });
  });
  return destinations;
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

function makeObjectLabel(landmark, labelLayer, onSelect) {
  const wrapper = document.createElement("div");
  wrapper.className = "world-object-label";
  wrapper.dataset.landmarkLabel = landmark.id;
  wrapper.style.setProperty("--label-color", landmark.color);
  const button = document.createElement("button");
  button.type = "button";
  const copy = document.createElement("span");
  copy.textContent = landmark.shortLabel;
  const construction = document.createElement("span");
  construction.className =
    "world-construction-mark world-construction-mark-destination";
  construction.dataset.worldConstructionMarker = landmark.id;
  construction.setAttribute("role", "img");
  construction.setAttribute(
    "aria-label",
    "Under construction: this integration has not been verified in this session.",
  );
  construction.title =
    "Under construction: this integration has not been verified in this session.";
  const icon = document.createElement("span");
  icon.setAttribute("aria-hidden", "true");
  icon.textContent = "🚧";
  construction.appendChild(icon);
  button.append(copy, construction);
  button.setAttribute("aria-label", `Open ${landmark.label}`);
  button.addEventListener("click", () => onSelect(landmark.id, { source: "label" }));
  wrapper.appendChild(button);
  labelLayer.appendChild(wrapper);
  return wrapper;
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
  onLocationChange = () => {},
  onRegionChange = () => {},
  onMovement = () => {},
  onModeration = () => {},
}) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color("#93c9b3");
  scene.fog = new THREE.FogExp2("#8ebaa8", 0.0085);

  const camera = new THREE.PerspectiveCamera(44, 1, 0.1, 240);
  camera.position.set(...CAMERA_OFFSET);

  const renderer = new THREE.WebGLRenderer({
    antialias: true,
    alpha: false,
    powerPreference: "high-performance",
  });
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.08;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.domElement.className = "world-canvas";
  renderer.domElement.setAttribute("aria-hidden", "true");
  renderer.domElement.tabIndex = -1;
  renderer.domElement.dataset.cameraControl = "drag";
  renderer.domElement.dataset.dragging = "false";
  container.appendChild(renderer.domElement);

  const hemisphere = new THREE.HemisphereLight("#d5fff1", "#19362d", 2.1);
  scene.add(hemisphere);
  const sun = new THREE.DirectionalLight("#fff1c4", 3.4);
  sun.position.set(-24, 35, 18);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
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
  const landmarkLabels = new Map();

  const ground = new THREE.Mesh(
    new THREE.CircleGeometry(WORLD_GROUND_RADIUS, 128),
    makeMaterial(THREE, "#174434", { roughness: 1 }),
  );
  ground.rotation.x = -Math.PI / 2;
  ground.receiveShadow = true;
  ground.userData.ground = true;
  world.add(ground);

  const plaza = new THREE.Mesh(
    new THREE.CylinderGeometry(16.5, 17.4, 0.34, 64),
    makeMaterial(THREE, "#d2dfd6", { roughness: 0.82 }),
  );
  plaza.position.y = 0.17;
  plaza.receiveShadow = true;
  plaza.userData.ground = true;
  world.add(plaza);

  const plazaLines = new THREE.Group();
  for (let radius = 3; radius <= 15; radius += 3) {
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(radius, 0.025, 6, 96),
      makeMaterial(THREE, radius % 6 === 0 ? "#6e9a86" : "#9eb4a8"),
    );
    ring.rotation.x = Math.PI / 2;
    ring.position.y = 0.36;
    plazaLines.add(ring);
  }
  for (let index = 0; index < 12; index += 1) {
    const angle = (index / 12) * Math.PI * 2;
    const lineMesh = new THREE.Mesh(
      new THREE.BoxGeometry(0.025, 0.035, 16.2),
      makeMaterial(THREE, "#98afa3"),
    );
    lineMesh.position.y = 0.36;
    lineMesh.rotation.y = angle;
    plazaLines.add(lineMesh);
  }
  world.add(plazaLines);

  // The room assigns one of 64 ephemeral slots: ten columns per row. The
  // outline makes the arrival contract visible without turning it into a
  // barrier once a visitor starts walking.
  const arrivalBox = new THREE.Group();
  arrivalBox.name = "world-arrival-box";
  const arrivalFloor = new THREE.Mesh(
    new THREE.BoxGeometry(19.8, 0.05, 15.8),
    makeMaterial(THREE, "#143d31", {
      emissive: "#1f7b57",
      emissiveIntensity: 0.18,
      transparent: true,
      opacity: 0.78,
    }),
  );
  arrivalFloor.position.set(0, 0.04, 23.7);
  arrivalFloor.receiveShadow = true;
  arrivalFloor.userData.ground = true;
  arrivalBox.add(arrivalFloor);
  for (let column = 0; column <= 10; column += 1) {
    const line = new THREE.Mesh(
      new THREE.BoxGeometry(0.025, 0.035, 14.7),
      makeMaterial(THREE, "#61dca0", {
        emissive: "#2e9f70",
        emissiveIntensity: 0.7,
      }),
    );
    line.position.set(-9 + column * 1.8, 0.09, 23.7);
    arrivalBox.add(line);
  }
  for (let row = 0; row <= 7; row += 1) {
    const line = new THREE.Mesh(
      new THREE.BoxGeometry(18.1, 0.035, 0.025),
      makeMaterial(THREE, "#61dca0", {
        emissive: "#2e9f70",
        emissiveIntensity: 0.7,
      }),
    );
    line.position.set(0, 0.09, 30.95 - row * 2.1);
    arrivalBox.add(line);
  }
  const arrivalLabel = makeLabelSprite(
    THREE,
    "ARRIVAL GRID",
    "10 visitors per row · face the square",
    "#9ef7c6",
  );
  arrivalLabel.scale.set(4.8, 1.6, 1);
  arrivalLabel.position.set(0, 1.25, 31.3);
  arrivalBox.add(arrivalLabel);
  world.add(arrivalBox);

  world.add(createElectricMeshCityGrid(THREE, animated));

  const landmarkFactories = {
    information: createInformationBooth,
    fountain: createFountain,
    repositories: createRepositoryDistrict,
    routing: createRoutingStation,
    organizations: createOrganizationQuarter,
    fediverse: createFediverseCenter,
    security: createSecurityWorkshop,
    launchpad: createLaunchpad,
    events: createCommunityStage,
    neighborhood: createNeighborhood,
    workshops: createCodeWorkshops,
    broadcast: createBroadcastGarden,
    support: createSupportCenter,
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
    landmarkLabels.set(
      landmark.id,
      makeObjectLabel(landmark, labelLayer, (id, meta) => {
        focusLandmark(id);
        onLandmarkSelect(id, meta);
      }),
    );
  });

  const treeColors = ["#2f8c5f", "#397655", "#4b9e68", "#27634a"];
  for (let index = 0; index < 62; index += 1) {
    const angle = (index / 62) * Math.PI * 2 + (index % 5) * 0.07;
    const radius = 20 + (index % 7) * 2.25;
    const x = Math.cos(angle) * radius;
    const z = Math.sin(angle) * radius;
    if (LANDMARKS.some((landmark) => Math.hypot(x - landmark.position[0], z - landmark.position[2]) < 5)) {
      continue;
    }
    world.add(createTree(THREE, x, z, 0.72 + (index % 5) * 0.1, treeColors[index % treeColors.length]));
  }

  for (let index = 0; index < 28; index += 1) {
    const angle = (index / 28) * Math.PI * 2;
    const bench = new THREE.Group();
    const seat = new THREE.Mesh(
      new THREE.BoxGeometry(2.1, 0.15, 0.52),
      makeMaterial(THREE, "#6c4d35"),
    );
    seat.position.y = 0.62;
    bench.add(seat);
    for (const x of [-0.75, 0.75]) {
      const leg = new THREE.Mesh(
        new THREE.BoxGeometry(0.1, 0.55, 0.38),
        makeMaterial(THREE, "#26372f"),
      );
      leg.position.set(x, 0.3, 0);
      bench.add(leg);
    }
    const radius = index % 2 ? 18.2 : 25;
    bench.position.set(Math.cos(angle) * radius, 0, Math.sin(angle) * radius);
    bench.rotation.y = -angle + Math.PI / 2;
    setShadows(bench);
    world.add(bench);
  }

  world.add(createWorkshopBarn(THREE));
  world.add(createSkyOffice(THREE));
  world.add(createOtherWorlds(THREE, animated));
  const registeredUserLounge = createRegisteredUserLounge(THREE, animated);
  world.add(registeredUserLounge);
  const durableObjectDistrict = createDurableObjectDistrict(THREE);
  world.add(durableObjectDistrict);

  const player = createAvatar(THREE, identity);
  player.position.set(-8.1, 0.38, 30);
  player.rotation.y = 0;
  world.add(player);
  const playerLabel = makePlayerLabel(player, labelLayer);

  const remotePlayers = new Map();
  const remoteLabels = new Map();
  const moderationActions = new WeakMap();
  const moderationControlKeys = new Map();
  const neighborhoodHomes = new Map();
  const nodeInfrastructure = new Map();
  const botAgents = new Map();
  const loungeMembers = new Map();
  const emoteSprites = [];
  const rewardFlights = [];
  const keys = new Set();
  const touchKeys = new Set();
  const touchPointers = new Map();
  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();
  const pointerStart = new THREE.Vector2();
  const pointerLast = new THREE.Vector2();
  let selectedLandmark = "information";
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
  let lastMovementEmit = 0;
  let lastPosition = player.position.clone();
  let wasWalking = false;
  let cameraFocus = null;
  let cameraZoom = 1;
  let cameraYaw = Math.atan2(CAMERA_OFFSET[0], CAMERA_OFFSET[2]);
  let cameraPitch = Math.asin(CAMERA_OFFSET[1] / CAMERA_DISTANCE);
  // Per-device movement tuning (scales the shared defaults above). Acceleration
  // may be Infinity, meaning the player snaps to top speed the instant a key is
  // pressed. Both are adjustable from the World's local controls.
  let moveSpeedScale = 1;
  let moveAccelScale = 1;
  let keyboardMovementSpeed = PLAYER_SPEED;
  let primaryPointerId = null;
  let pointerGestureMoved = false;
  let pinchStartDistance = 0;
  let pinchStartZoom = cameraZoom;
  let pinchActive = false;
  let lightLevel = LIGHT_LEVEL_DEFAULT;
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
    scene.fog.density =
      state.fogDensity * (overlay?.fogMultiplier || 1);
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

  function nearestLandmark() {
    if (
      !["town-square", "east", "central", "west"].includes(currentSpace)
    ) {
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
    const nextLocation = distance < 7.5 ? nearest.label : "Town Square";
    if (nextLocation !== currentLocation) {
      currentLocation = nextLocation;
      onLocationChange(nextLocation, nearest?.id || "");
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

  function travelToSpace(spaceId) {
    // Every space is a barn bay now, so arrivals land in the aisle in front of
    // the parked destination rather than on a platform in the sky.
    const bay = WORKSHOP_BARN_BAYS[spaceId];
    const destination = bay
      ? new THREE.Vector3(bay.x, WORLD_SPACE_FLOORS[spaceId], WORKSHOP_BARN_AISLE_Z)
      : null;
    if (!destination) return false;
    currentSpace = spaceId;
    currentFloorY = destination.y;
    player.position.copy(destination);
    cameraFocus = null;
    currentLocation = spaceId
      .split("-")
      .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
      .join(" ");
    onLocationChange(currentLocation, "launchpad");
    onMovement({
      x: destination.x,
      y: destination.y,
      z: destination.z,
      heading: player.rotation.y,
      activity: `collaborating in ${spaceId}`,
      space: spaceId,
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
    selectedLandmark = landmark.id;
    cameraFocus = new THREE.Vector3(
      landmark.position[0],
      1.5,
      landmark.position[2],
    );
  }

  function clearFocus() {
    cameraFocus = null;
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

  function movementInput() {
    const movement = new THREE.Vector3();
    const forwardInput =
      Number(keys.has("KeyW") || keys.has("ArrowUp")) -
      Number(keys.has("KeyS") || keys.has("ArrowDown"));
    const rightInput =
      Number(keys.has("KeyD") || keys.has("ArrowRight")) -
      Number(keys.has("KeyA") || keys.has("ArrowLeft"));
    if (forwardInput || rightInput) {
      // Desktop movement follows the view direction, as in an FPS: W/S move
      // toward/away from the camera reticle and A/D strafe. Only the horizontal
      // yaw participates so looking up or down never changes walking speed.
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
      movement.addScaledVector(forward, forwardInput);
      movement.addScaledVector(right, rightInput);
    }

    // The coarse-pointer direction pad remains a predictable screen-independent
    // fallback and does not require a mouse.
    if (touchKeys.has("KeyW")) movement.z -= 1;
    if (touchKeys.has("KeyS")) movement.z += 1;
    if (touchKeys.has("KeyA")) movement.x -= 1;
    if (touchKeys.has("KeyD")) movement.x += 1;
    return {
      keyboardActive: Boolean(forwardInput || rightInput),
      movement: movement.lengthSq() ? movement.normalize() : movement,
    };
  }

  function walkPlayer(delta, time) {
    const { keyboardActive, movement } = movementInput();
    let walking = false;
    if (movement.lengthSq()) {
      cameraFocus = null;
      const topSpeed = PLAYER_MAX_SPEED * moveSpeedScale;
      // Infinite acceleration collapses the ramp: keyboardMovementSpeed jumps to
      // topSpeed on the first press instead of easing up over several frames.
      keyboardMovementSpeed = keyboardActive
        ? Math.min(
            topSpeed,
            keyboardMovementSpeed + PLAYER_ACCELERATION * moveAccelScale * delta,
          )
        : baseMoveSpeed();
      player.position.addScaledVector(
        movement,
        keyboardMovementSpeed * delta,
      );
      player.rotation.y = Math.atan2(-movement.x, -movement.z);
      walking = true;
    } else {
      keyboardMovementSpeed = baseMoveSpeed();
    }

    const radius = Math.hypot(player.position.x, player.position.z);
    if (radius > WORLD_RADIUS) {
      player.position.x *= WORLD_RADIUS / radius;
      player.position.z *= WORLD_RADIUS / radius;
    }
    player.position.y = currentFloorY;
    const gait = walking ? Math.sin(time * 0.012) * 0.52 : 0;
    player.userData.leftArm.rotation.x = gait;
    player.userData.rightArm.rotation.x = -gait;
    player.userData.leftLeg.rotation.x = -gait * 0.72;
    player.userData.rightLeg.rotation.x = gait * 0.72;
    player.position.y += walking ? Math.abs(Math.sin(time * 0.012)) * 0.035 : 0;
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
      avatar.userData.leftArm.rotation.x = gait;
      avatar.userData.rightArm.rotation.x = -gait;
      avatar.userData.leftLeg.rotation.x = -gait * 0.7;
      avatar.userData.rightLeg.rotation.x = gait * 0.7;
      if (recentlyActiveInLounge && !walking && !reducedMotion) {
        avatar.position.y =
          avatar.userData.targetPosition.y +
          Math.abs(Math.sin(time * 0.004 + avatar.userData.phase)) * 0.055;
      }
      animateAvatarActivity(avatar, time, delta, reducedMotion);
    });
  }

  function updateCamera(delta) {
    const target = cameraFocus
      ? cameraFocus.clone()
      : player.position.clone().add(new THREE.Vector3(0, 2.2, 0));
    const distance =
      CAMERA_DISTANCE * cameraZoom * (cameraFocus ? 0.78 : 1);
    const horizontalDistance = Math.cos(cameraPitch) * distance;
    const desired = target.clone().add(
      new THREE.Vector3(
        Math.sin(cameraYaw) * horizontalDistance,
        Math.sin(cameraPitch) * distance,
        Math.cos(cameraYaw) * horizontalDistance,
      ),
    );
    camera.position.lerp(desired, 1 - Math.pow(0.0008, delta));
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
    player.rotation.y = heading;
    lastPosition.copy(player.position);
    wasWalking = false;
    cameraFocus = null;
    if (space === "town-square") {
      nearestLandmark();
    } else {
      currentLocation = space
        .split("-")
        .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
        .join(" ");
      onLocationChange(currentLocation, "launchpad");
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
      if (useRegisteredLounge) {
        const seats = registeredUserLounge.userData.seatOffsets || [];
        const seat = seats[hashNumber(remote.id) % Math.max(1, seats.length)];
        const offset = seat || new THREE.Vector3();
        avatar.userData.targetPosition.copy(registeredUserLounge.position);
        avatar.userData.targetPosition.add(offset);
        avatar.userData.targetHeading = Math.PI;
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
        avatar.userData.targetHeading = Number(remote.heading) || 0;
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

  function updateMemberLounge(members = [], totalCount = 0) {
    const total = Math.max(0, Math.min(999999, Number(totalCount) || 0));
    const countSign = registeredUserLounge.userData.memberCountSign;
    if (countSign && registeredUserLounge.userData.memberCountShown !== total) {
      countSign.material.map?.dispose?.();
      countSign.material.map = wordTexture(
        THREE,
        `${total} MEMBER${total === 1 ? "" : "S"}`,
        "total registered users",
        "#9ef7c6",
      );
      countSign.material.needsUpdate = true;
      registeredUserLounge.userData.memberCountShown = total;
    }
    const seats = registeredUserLounge.userData.seatOffsets || [];
    const seen = new Set();
    (Array.isArray(members) ? members : [])
      .filter((member) => String(member?.name || "").trim())
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
              status: "resting in the member lounge",
              accountStatus: "Registered",
              localTime: "",
              activityCategory: "hidden",
              inputActive: false,
              visitCount: 0,
              firstVisitAge: "hidden",
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
        figure.position.copy(registeredUserLounge.position);
        if (seat) figure.position.add(seat);
        figure.rotation.y = Math.PI;
      });
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
    const routingPosition = landmarkById("routing").position;
    const routingX = Number(routingPosition?.[0]) || 32;
    const routingZ = Number(routingPosition?.[2]) || 16;
    // A dedicated 8×8 server aisle east of the routing station keeps all 64
    // bounded live slots separate without expanding rings through neighboring
    // repository and launchpad landmarks. Fill closest-to-routing slots first.
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
    const removeCabinet = (cabinet) => {
      cabinet?.traverse?.((child) => {
        if (!child.userData?.nodeCabinet) return;
        const interactiveIndex = interactive.indexOf(child);
        if (interactiveIndex >= 0) interactive.splice(interactiveIndex, 1);
      });
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
      // The front display faces inward toward the routing station, so each
      // cabinet remains individually readable from the surrounding walkway.
      cabinet.rotation.y = Math.atan2(
        routingX - slot.x,
        routingZ - slot.z,
      );
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
    selectedLandmark = "routing";
    cameraFocus = target.position.clone();
    cameraFocus.y += 1.65;
    return true;
  }

  function updateFederatedInstances(instances = []) {
    const station = landmarkObjects.get("routing");
    if (!station) return;
    const existing = station.userData.federatedInstanceLayer;
    if (existing) {
      station.remove(existing);
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
    station.add(layer);
    station.userData.federatedInstanceLayer = layer;
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

  function updateDurableObjects(metrics = {}) {
    const existing = durableObjectDistrict.userData.metricsLayer;
    if (existing) {
      durableObjectDistrict.remove(existing);
      disposeObject3D(existing);
      durableObjectDistrict.userData.metricsLayer = null;
    }

    const records = Array.isArray(metrics)
      ? metrics
      : Array.isArray(metrics?.objects)
        ? metrics.objects
        : [];
    const safeRecords = records
      .map((record) => {
        const name = String(record?.name || record?.id || "").trim();
        const pairs = durableObjectMetricPairs(record);
        return name && pairs.length
          ? {
              name: name.slice(0, 36),
              id: String(record?.id || name).slice(0, 72),
              pairs,
            }
          : null;
      })
      .filter(Boolean)
      .slice(0, 12);

    const emptyMarker = durableObjectDistrict.getObjectByName(
      "durable-object-metrics-unavailable",
    );
    if (emptyMarker) emptyMarker.visible = safeRecords.length === 0;
    durableObjectDistrict.userData.metricsAvailable =
      safeRecords.length > 0;
    durableObjectDistrict.userData.visibleObjectCount = safeRecords.length;
    if (!safeRecords.length) return 0;

    const layer = new THREE.Group();
    layer.name = "durable-object-current-usage";
    safeRecords.forEach((record, index) => {
      const object = new THREE.Group();
      object.name = `durable-object:${record.id}`;
      object.userData.metrics = record.pairs.map((metric) => ({
        key: metric.key,
        currentUsage: metric.usage,
        configuredLimit: metric.limit,
      }));

      const column = index % 4;
      const row = Math.floor(index / 4);
      object.position.set(-4.5 + column * 3, 0.38, -2.7 + row * 2.7);
      const plinth = new THREE.Mesh(
        new THREE.CylinderGeometry(0.75, 0.9, 0.26, 8),
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
        new THREE.BoxGeometry(0.82, 2.6, 0.82),
        makeMaterial(THREE, "#1d343b", {
          transparent: true,
          opacity: 0.62,
          roughness: 0.46,
        }),
      );
      housing.position.y = 1.55;
      object.add(housing);
      const fillHeight = Math.max(0.08, ratio * 2.5);
      const fill = new THREE.Mesh(
        new THREE.BoxGeometry(0.58, fillHeight, 0.58),
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
      label.scale.set(2.5, 0.84, 1);
      label.position.y = 3.55;
      object.add(label);
      setShadows(object);
      layer.add(object);
    });
    durableObjectDistrict.add(layer);
    durableObjectDistrict.userData.metricsLayer = layer;
    return safeRecords.length;
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
    updatePlayerLabel(playerLabel, identity);
    if (identity.isAdmin !== true) {
      remotePlayers.forEach((avatar, peerId) => {
        removeRemoteModerationControls(avatar, peerId);
      });
    }
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
      duration: Math.min(7500, 3200 + message.length * 30),
      baseHeight: 5.2,
      rise: 0.5,
      fadeStart: 0.75,
    });
    return true;
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
    pinchStartZoom = cameraZoom;
    pinchActive = true;
    pointerGestureMoved = true;
    renderer.domElement.dataset.dragging = "true";
  }

  function rotateCamera(deltaX, deltaY) {
    if (!Number.isFinite(deltaX) || !Number.isFinite(deltaY)) return;
    cameraYaw -= deltaX * CAMERA_LOOK_SENSITIVITY;
    cameraPitch = clamp(
      cameraPitch + deltaY * CAMERA_LOOK_SENSITIVITY,
      CAMERA_PITCH_MIN,
      CAMERA_PITCH_MAX,
    );
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
    if (event.pointerType === "touch" && touchPointers.has(event.pointerId)) {
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
      }
    }
    if (event.pointerId !== primaryPointerId) return;
    const currentPointer = new THREE.Vector2(event.clientX, event.clientY);
    const movedDistance = pointerStart.distanceTo(currentPointer);
    if (event.pointerType === "mouse" && movedDistance > 4) {
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
    if (event.pointerType === "touch") {
      touchPointers.delete(event.pointerId);
      if (touchPointers.size < 2) {
        pinchActive = false;
        pinchStartDistance = 0;
      }
    }
    try {
      renderer.domElement.releasePointerCapture(event.pointerId);
    } catch (_) {}
    if (event.pointerId !== primaryPointerId) {
      if (!touchPointers.size) renderer.domElement.dataset.dragging = "false";
      return;
    }
    primaryPointerId = null;
    renderer.domElement.dataset.dragging =
      touchPointers.size ? "true" : "false";
    const suppressTap =
      cancelled ||
      wasPinching ||
      pointerGestureMoved;
    pointerGestureMoved = false;
    if (suppressTap) return;

    // Use the down coordinates so a small amount of click jitter cannot select
    // an object that was not underneath the visible cursor at press time.
    pointerCoordinates({
      clientX: pointerStart.x,
      clientY: pointerStart.y,
    });
    raycaster.setFromCamera(pointer, camera);
    const hit = raycaster.intersectObjects(interactive, false)[0];
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
    if (hit?.object?.userData?.nodeCabinet) {
      focusNetworkNode(hit.object.userData.nodeCabinet.name);
      onLandmarkSelect("routing", {
        source: "world",
        nodeCabinet: { ...hit.object.userData.nodeCabinet },
      });
      return;
    }
    if (hit?.object?.userData?.landmark) {
      const id = hit.object.userData.landmark;
      focusLandmark(id);
      onLandmarkSelect(id, {
        source: "world",
        mediaSpaceId: String(
          hit.object.userData.mediaSpaceId || "",
        ).slice(0, 40),
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
    setCameraZoom(cameraZoom * Math.exp(deltaPixels * 0.0015));
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
    if (event.code === "Escape") {
      clearFocus();
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
    touchPointers.clear();
    keyboardMovementSpeed = baseMoveSpeed();
    primaryPointerId = null;
    pointerGestureMoved = false;
    pinchActive = false;
    pinchStartDistance = 0;
    renderer.domElement.dataset.dragging = "false";
  }

  renderer.domElement.addEventListener("pointerdown", handlePointerDown);
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

  const resizeObserver = new ResizeObserver(() => {
    const rect = container.getBoundingClientRect();
    const width = Math.max(1, Math.floor(rect.width));
    const height = Math.max(1, Math.floor(rect.height));
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, width < 700 ? 1.35 : 1.75));
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
  });
  resizeObserver.observe(container);

  function animate(time) {
    if (!running || disposed) return;
    const delta = clamp((time - lastFrame) / 1000, 0, 0.05);
    lastFrame = time;
    walkPlayer(delta, time);
    updateRemotePlayers(delta, time);
    if (!reducedMotion) {
      nodeInfrastructure.forEach((pylon, id) => {
        const phase = hashNumber(id) * 0.0001;
        pylon.userData.signalRing.rotation.z = time * 0.0015 + phase;
        pylon.userData.signalRing.scale.setScalar(
          1 + Math.sin(time * 0.002 + phase) * 0.08,
        );
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
    nearestLandmark();
    if (!reducedMotion) {
      animated.forEach((callback) => callback(time, delta));
      animateWeather(weather.rain, time, delta, "rain");
      animateWeather(weather.snow, time, delta, "snow");
    }
    const rect = container.getBoundingClientRect();
    LANDMARKS.forEach((landmark) => {
      const object = landmarkObjects.get(landmark.id);
      const element = landmarkLabels.get(landmark.id);
      const height =
        landmark.id === "organizations" ? 10.2 :
        landmark.id === "repositories" ? 7.6 :
        landmark.id === "fountain" ? 6.9 : 6.5;
      updateScreenLabel(THREE, object, element, camera, rect.width, rect.height, height);
      element.dataset.selected = String(selectedLandmark === landmark.id);
    });
    updateScreenLabel(
      THREE,
      player,
      playerLabel,
      camera,
      rect.width,
      rect.height,
      player.userData.emojiStatusSprite ? 5.7 : 4.5,
    );
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
    return {
      fps,
      frameTimeMs,
      rendererCalls: diagnosticsRendererCalls,
      rendererTriangles: diagnosticsRendererTriangles,
      paused: !running,
    };
  }

  function updateLandmarkConstruction(capabilities = {}) {
    LANDMARKS.forEach((landmark) => {
      const label = landmarkLabels.get(landmark.id);
      const marker = label?.querySelector(
        `[data-world-construction-marker="${landmark.id}"]`,
      );
      const button = label?.querySelector("button");
      if (!marker || !button) return;
      const capability = capabilities?.[landmark.id];
      const live = capability?.live === true;
      const reason =
        String(capability?.reason || "").trim() ||
        "This integration has not been verified in this session.";
      marker.hidden = live;
      marker.setAttribute("aria-label", `Under construction: ${reason}`);
      marker.title = `Under construction: ${reason}`;
      button.dataset.worldUnderConstruction = String(!live);
      button.setAttribute(
        "aria-label",
        live
          ? `Open ${landmark.label}`
          : `Open ${landmark.label} — under construction: ${reason}`,
      );
    });
  }

  function dispose() {
    disposed = true;
    renderer.setAnimationLoop(null);
    resizeObserver.disconnect();
    renderer.domElement.removeEventListener("pointerdown", handlePointerDown);
    renderer.domElement.removeEventListener("pointermove", handlePointerMove);
    renderer.domElement.removeEventListener("wheel", handleWheel);
    window.removeEventListener("pointerup", handlePointerUp);
    window.removeEventListener("pointercancel", handlePointerCancel);
    window.removeEventListener("keydown", handleKeyDown);
    window.removeEventListener("keyup", handleKeyUp);
    window.removeEventListener("blur", handleWindowBlur);
    touchPointers.clear();
    keys.clear();
    touchKeys.clear();
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
    clearFocus,
    setTheme,
    setLightLevel,
    setMovementTuning,
    setControl,
    setSpawn,
    travelToRegion,
    travelToSpace,
    visitNeighborhoodHome,
    setRemotePlayers,
    updateMemberLounge,
    updateNetworkNodes,
    focusNetworkNode,
    updateFederatedInstances,
    updateBots,
    updateDurableObjects,
    updateOrganizations,
    updateFediverseDirectory,
    updateMediaSpaces,
    updateIdentity,
    updateRepositoryGraph,
    updateLandmarkConstruction,
    playEmote,
    showChatBubble,
    playRewardEvent,
    setPaused,
    dispose,
    setCameraZoom,
    getCameraState: () => ({
      zoom: cameraZoom,
      minZoom: CAMERA_ZOOM_MIN,
      maxZoom: CAMERA_ZOOM_MAX,
      yaw: cameraYaw,
      pitch: cameraPitch,
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
