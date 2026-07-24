import {
  LANDMARKS,
  WORLD_DAY_MS,
  WORLD_REGIONS,
  landmarkById,
  worldClock,
} from "./world-data.js";

const WORLD_RADIUS = 36;
const PLAYER_SPEED = 6.2;
const CAMERA_OFFSET = [17, 16, 21];
const ACCOUNT_STATUS_ICONS = Object.freeze({
  Guest: "○",
  Registered: "✓",
  "Supporting member": "♥",
  "Mirror operator": "◈",
  "Organization admin": "◆",
  "Verified bot": "⌘",
});

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
  return canvasTexture(THREE, 384, 384, (context) => {
    context.fillStyle = "#0c2019";
    context.fillRect(0, 0, 384, 384);
    context.strokeStyle = accent;
    context.lineWidth = 10;
    context.strokeRect(7, 7, 370, 370);

    context.textAlign = "center";
    context.textBaseline = "middle";
    context.font = '118px system-ui, "Apple Color Emoji", "Segoe UI Emoji"';
    context.fillStyle = "#ffffff";
    context.fillText(identity.flag || "◌", 192, 94);

    roundedRect(context, 42, 170, 300, 82, 10);
    context.fillStyle = "rgba(255,255,255,0.11)";
    context.fill();
    context.font = '700 32px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#d9ffea";
    context.fillText(
      `${String(identity.browser || "BROWSER").toUpperCase()} · ${String(identity.os || "DEVICE").toUpperCase()}`,
      192,
      211,
    );

    context.font = '700 41px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#ffffff";
    const name = String(identity.name || "guest").slice(0, 15);
    context.fillText(name, 192, 294);

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
    context.arc(52, 348, 11, 0, Math.PI * 2);
    context.fillStyle = statusColors[identity.status] || "#9ef7c6";
    context.fill();
    context.textAlign = "left";
    context.font = '700 18px "ForkMesh Mono", ui-monospace, monospace';
    context.fillStyle = "#b9cfc4";
    const account = String(identity.accountStatus || "Guest")
      .replace(/\s+/g, " ")
      .slice(0, 18);
    context.fillText(
      [
        `${ACCOUNT_STATUS_ICONS[account] || "○"} ${account}`,
        identity.localTime,
      ].filter(Boolean).join(" · "),
      72,
      349,
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
  const shirtColors = ["#176642", "#225a72", "#58447b", "#694d27", "#215c54"];
  const skin = makeMaterial(THREE, skinColors[seed % skinColors.length], { roughness: 0.92 });
  const shirt = makeMaterial(THREE, shirtColors[(seed >> 3) % shirtColors.length], {
    roughness: 0.8,
  });
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
  badge.position.set(0, 2.25, -0.316);
  badge.rotation.y = Math.PI;
  group.add(badge);

  // Guests land in the world immediately with the phone/laptop affordance from
  // the product model. It is a local visual cue only — no browsing details are
  // attached to it or sent through presence.
  if (String(identity.accountStatus || "").toLowerCase().startsWith("guest")) {
    const device = new THREE.Group();
    const screen = new THREE.Mesh(
      new THREE.BoxGeometry(0.78, 0.5, 0.08),
      makeMaterial(THREE, "#17211f", { metalness: 0.36, roughness: 0.32 }),
    );
    screen.position.set(0, 1.95, -0.72);
    screen.rotation.x = -0.18;
    device.add(screen);
    const display = new THREE.Mesh(
      new THREE.PlaneGeometry(0.58, 0.32),
      new THREE.MeshBasicMaterial({ color: "#77d9ff" }),
    );
    display.position.set(0, 1.95, -0.765);
    display.rotation.y = Math.PI;
    display.rotation.x = 0.18;
    device.add(display);
    const keyboard = new THREE.Mesh(
      new THREE.BoxGeometry(0.82, 0.07, 0.48),
      makeMaterial(THREE, "#283633", { metalness: 0.28, roughness: 0.44 }),
    );
    keyboard.position.set(0, 1.67, -0.48);
    keyboard.rotation.x = 0.28;
    device.add(keyboard);
    group.add(device);
  }

  group.scale.setScalar(scale);
  group.userData = {
    id: identity.id,
    name: identity.name,
    leftArm,
    rightArm,
    leftLeg,
    rightLeg,
    badge,
    phase: (seed % 100) / 10,
    targetPosition: new THREE.Vector3(),
    targetHeading: 0,
    status: identity.status || "exploring",
    nodeCount: 0,
  };
  syncOperatorBelt(THREE, group, identity.nodes?.length || 0);
  setShadows(group, true, true);
  return group;
}

function updateAvatarBadge(THREE, avatar, identity, remote = false) {
  const badge = avatar?.userData?.badge;
  if (!badge?.material) return;
  const old = badge.material.map;
  badge.material.map = badgeTexture(THREE, identity, remote ? "#77d9ff" : "#9ef7c6");
  badge.material.needsUpdate = true;
  old?.dispose?.();
  avatar.userData.name = identity.name;
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

  const sign = makeLabelSprite(THREE, "START HERE", "information booth", "#9ef7c6");
  sign.scale.set(5.6, 1.86, 1);
  sign.position.set(0, 3.95, 0);
  group.add(sign);

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

  const label = makeLabelSprite(
    THREE,
    "GLOBAL REWARD POOL",
    "public on-chain balance · external signer",
    "#f7c96b",
  );
  label.position.set(0, 7.4, 0);
  group.add(label);

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
  const sign = makeLabelSprite(THREE, "REPOSITORIES", "walk through the code", "#77d9ff");
  sign.position.set(0, 7.25, 0);
  group.add(sign);

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

  const sign = makeLabelSprite(
    THREE,
    "ROUTING STATION",
    "healthy HTTPS mirrors",
    "#80e8ff",
  );
  sign.position.set(0, 6.75, 0);
  group.add(sign);

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
  const sign = makeLabelSprite(THREE, "ORGANIZATIONS", "permissioned team spaces", "#d5b6ff");
  sign.position.set(0, 10, 0);
  group.add(sign);

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
  const sign = makeLabelSprite(THREE, "FEDIVERSE", "consent-aware social center", "#ff9eb7");
  sign.position.set(0, 6.3, 0);
  group.add(sign);
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
  const sign = makeLabelSprite(THREE, "SECURITY", "scoped scans + human review", "#88f0df");
  sign.position.set(0, 7.1, 0);
  group.add(sign);

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

function createQuarantine(THREE, position, interactive, animated) {
  const group = new THREE.Group();
  const pad = new THREE.Mesh(
    new THREE.CylinderGeometry(3.7, 4.1, 0.4, 8),
    makeMaterial(THREE, "#3a2521"),
  );
  pad.position.y = 0.2;
  group.add(pad);
  const barMaterial = makeMaterial(THREE, "#ff8e78", {
    emissive: "#8b2d24",
    emissiveIntensity: 0.52,
    metalness: 0.48,
    roughness: 0.3,
  });
  for (let index = 0; index < 12; index += 1) {
    const angle = (index / 12) * Math.PI * 2;
    const bar = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 4.4, 8), barMaterial);
    bar.position.set(Math.cos(angle) * 2.5, 2.4, Math.sin(angle) * 2.5);
    group.add(bar);
  }
  for (const y of [0.55, 4.45]) {
    const ring = new THREE.Mesh(new THREE.TorusGeometry(2.5, 0.09, 8, 48), barMaterial);
    ring.rotation.x = Math.PI / 2;
    ring.position.y = y;
    group.add(ring);
  }
  const warning = new THREE.Mesh(
    new THREE.OctahedronGeometry(0.75, 0),
    makeMaterial(THREE, "#ff8e78", {
      emissive: "#ff4e3a",
      emissiveIntensity: 1.1,
    }),
  );
  warning.position.y = 2.35;
  group.add(warning);
  const sign = makeLabelSprite(THREE, "QUARANTINE", "private evidence · public safeguards", "#ff8e78");
  sign.position.set(0, 6.3, 0);
  group.add(sign);
  group.position.set(...position);
  group.userData.landmark = "quarantine";
  group.traverse((child) => {
    if (child.isMesh) {
      child.userData.landmark = "quarantine";
      interactive.push(child);
    }
  });
  setShadows(group);
  animated.push((time) => {
    warning.rotation.y = time * 0.0008;
    warning.position.y = 2.35 + Math.sin(time * 0.0018) * 0.22;
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
  const sign = makeLabelSprite(THREE, "LAUNCHPAD", "events · sky offices · worlds", "#b6d8ff");
  sign.position.set(0, 6.5, 0);
  group.add(sign);
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
  const sign = makeLabelSprite(THREE, "EVENTS", "UTC community stage", "#ffb77d");
  sign.position.set(0, 6.1, 0);
  group.add(sign);
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
  const sign = makeLabelSprite(
    THREE,
    "NEIGHBORHOOD",
    "knock · visit · privacy",
    "#b8e986",
  );
  sign.position.set(0, 5.4, 0);
  group.add(sign);
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
  const sign = makeLabelSprite(
    THREE,
    "CODE WORKSHOPS",
    "analysis + collaboration",
    "#73f0ad",
  );
  sign.position.set(0, 5.4, 0);
  group.add(sign);
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
  const sign = makeLabelSprite(
    THREE,
    "BROADCAST",
    "opt-in media garden",
    "#8fcfff",
  );
  sign.position.set(0, 5.7, 0);
  group.add(sign);
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
  const sign = makeLabelSprite(
    THREE,
    "SUPPORT CENTER",
    "voluntary · transparent · no returns",
    "#ffd08f",
  );
  sign.position.set(0, 4.7, 0);
  group.add(sign);
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

function createSkyOffice(THREE, animated) {
  const group = new THREE.Group();
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
    cloud.scale.y = 0.5;
    cloud.position.set((index % 3) * 1.7 - 1.7, Math.floor(index / 3) * 0.35, (index % 2) * 1.35);
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
  office.position.y = 1.75;
  group.add(office);
  const roof = new THREE.Mesh(
    new THREE.ConeGeometry(3.2, 1.15, 4),
    makeMaterial(THREE, "#4b3b6b"),
  );
  roof.position.y = 3.45;
  roof.rotation.y = Math.PI / 4;
  group.add(roof);
  group.position.set(-17, 15, 18);
  group.scale.setScalar(0.86);
  setShadows(group);
  animated.push((time) => {
    group.position.y = 15 + Math.sin(time * 0.00038) * 0.7;
    group.rotation.y = Math.sin(time * 0.00011) * 0.15;
  });
  return group;
}

function createOtherWorlds(THREE, animated) {
  const destinations = new THREE.Group();
  destinations.name = "functional-world-destinations";

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
    "chat · watch rooms · broadcasts",
    "#b6d8ff",
  );
  stationLabel.position.y = 3.7;
  station.add(stationLabel);
  station.position.set(24, 18, 24);
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
    "repository world + workshops",
    "#77d9ff",
  );
  codeLabel.position.y = 4.1;
  codePlanet.add(codeLabel);
  codePlanet.position.set(28, 15, -22);
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
    "organization-owned region",
    "#d5b6ff",
  );
  orgLabel.position.y = 3.8;
  orgRegion.add(orgLabel);
  orgRegion.position.set(-29, 14, -23);
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
    "events · achievements · regions",
    "#f7c96b",
  );
  atlasLabel.position.y = 3.8;
  planetAtlas.add(atlasLabel);
  planetAtlas.position.set(-2, 22, -31);
  destinations.add(planetAtlas);

  animated.push((time) => {
    station.rotation.y = time * 0.00016;
    stationRing.rotation.z = time * 0.0004;
    codePlanet.rotation.y = -time * 0.00011;
    orgRegion.position.y = 14 + Math.sin(time * 0.00035) * 0.55;
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
  button.textContent = landmark.shortLabel;
  button.setAttribute("aria-label", `Open ${landmark.label}`);
  button.addEventListener("click", () => onSelect(landmark.id, { source: "label" }));
  wrapper.appendChild(button);
  labelLayer.appendChild(wrapper);
  return wrapper;
}

function makePlayerLabel(player, labelLayer) {
  const element = document.createElement("div");
  element.className = "world-player-label";
  element.dataset.playerLabel = player.userData.id || "";
  element.textContent = player.userData.name || "visitor";
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
}) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color("#93c9b3");
  scene.fog = new THREE.FogExp2("#8ebaa8", 0.0135);

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
  container.appendChild(renderer.domElement);

  const hemisphere = new THREE.HemisphereLight("#d5fff1", "#19362d", 2.1);
  scene.add(hemisphere);
  const sun = new THREE.DirectionalLight("#fff1c4", 3.4);
  sun.position.set(-24, 35, 18);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  sun.shadow.camera.left = -45;
  sun.shadow.camera.right = 45;
  sun.shadow.camera.top = 45;
  sun.shadow.camera.bottom = -45;
  sun.shadow.camera.near = 1;
  sun.shadow.camera.far = 100;
  sun.shadow.bias = -0.0003;
  scene.add(sun);

  const world = new THREE.Group();
  scene.add(world);
  const interactive = [];
  const animated = [];
  const landmarkObjects = new Map();
  const landmarkLabels = new Map();

  const ground = new THREE.Mesh(
    new THREE.CircleGeometry(58, 96),
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

  [
    [20, 4.2, 17, 5, 0.2],
    [21, 4.2, -18, 6, -0.12],
    [20, 4.2, 1, -17, Math.PI / 2],
    [20, 4.2, 2, 18, Math.PI / 2],
    [15, 3.4, -12, 12, -Math.PI / 4],
    [15, 3.4, 12, -12, -Math.PI / 4],
    [15, 3.4, -13, -11, Math.PI / 4],
    [15, 3.4, 13, 12, Math.PI / 4],
  ].forEach(([width, depth, x, z, rotation]) => world.add(createRoad(THREE, width, depth, x, z, rotation)));

  const landmarkFactories = {
    information: createInformationBooth,
    fountain: createFountain,
    repositories: createRepositoryDistrict,
    routing: createRoutingStation,
    organizations: createOrganizationQuarter,
    fediverse: createFediverseCenter,
    security: createSecurityWorkshop,
    quarantine: createQuarantine,
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

  world.add(createSkyOffice(THREE, animated));
  world.add(createOtherWorlds(THREE, animated));

  const player = createAvatar(THREE, identity);
  player.position.set(-8.5, 0.38, 12.5);
  player.rotation.y = Math.PI * 0.82;
  world.add(player);
  const playerLabel = makePlayerLabel(player, labelLayer);

  const remotePlayers = new Map();
  const remoteLabels = new Map();
  const neighborhoodHomes = new Map();
  const nodeAvatars = new Map();
  const botAvatars = new Map();
  const emoteSprites = [];
  const rewardFlights = [];
  const keys = new Set();
  const touchKeys = new Set();
  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();
  const pointerStart = new THREE.Vector2();
  const moveTarget = new THREE.Vector3();
  let hasMoveTarget = false;
  let selectedLandmark = "information";
  let currentLocation = "Town Square";
  let currentRegion = "central";
  let currentSpace = "town-square";
  let currentFloorY = 0.38;
  let currentTheme = "world";
  let running = true;
  let disposed = false;
  let lastFrame = performance.now();
  let lastMovementEmit = 0;
  let lastPosition = player.position.clone();
  let cameraFocus = null;
  let pointerDown = false;
  let clockOffset = 0;
  const weather = createWeather(THREE, scene);

  function setClockOffset(offset) {
    clockOffset = Number(offset) || 0;
  }

  function themeState(theme, now = Date.now()) {
    const region =
      WORLD_REGIONS.find((item) => item.id === currentRegion) ||
      WORLD_REGIONS.find((item) => item.id === "central");
    const regionalOffset =
      currentTheme === "world"
        ? Number(region?.utcOffsetHours || 0) * (WORLD_DAY_MS / 24)
        : 0;
    const synced = worldClock(now + clockOffset + regionalOffset);
    const states = {
      day: {
        background: "#9ed2bd",
        fog: "#9bc6b5",
        hemiSky: "#d8fff1",
        hemiGround: "#25493a",
        sun: "#fff0bd",
        sunPower: 3.7,
        exposure: 1.12,
      },
      sunset: {
        background: "#ce9174",
        fog: "#a17469",
        hemiSky: "#ffd1aa",
        hemiGround: "#442e39",
        sun: "#ffad68",
        sunPower: 4.2,
        exposure: 1.0,
      },
      night: {
        background: "#071322",
        fog: "#0a1723",
        hemiSky: "#31527a",
        hemiGround: "#101b1b",
        sun: "#83a7ff",
        sunPower: 0.82,
        exposure: 0.72,
      },
      cyberpunk: {
        background: "#12091d",
        fog: "#1c0b29",
        hemiSky: "#7b3fff",
        hemiGround: "#071717",
        sun: "#ff4fa7",
        sunPower: 2.5,
        exposure: 0.88,
      },
      "low-light": {
        background: "#07110f",
        fog: "#081410",
        hemiSky: "#315244",
        hemiGround: "#06100d",
        sun: "#a5d3bd",
        sunPower: 1.0,
        exposure: 0.66,
      },
    };
    if (theme === "rain") return states.sunset;
    if (theme === "snow" || theme === "winter") {
      return {
        ...states.day,
        background: theme === "winter" ? "#aebfbd" : "#b7c9c4",
        fog: theme === "winter" ? "#adbfbc" : "#bdcbc8",
        hemiGround: theme === "winter" ? "#536a63" : "#63756f",
      };
    }
    if (theme !== "world") return states[theme] || states.day;
    if (synced.phase === "Night") return states.night;
    if (synced.phase === "Sunset") return states.sunset;
    if (synced.phase === "Sunrise") {
      return {
        ...states.sunset,
        background: "#bd9f8e",
        fog: "#a88f82",
        sun: "#ffd19a",
      };
    }
    return states.day;
  }

  function applyTheme(theme) {
    currentTheme = theme || "world";
    const state = themeState(currentTheme);
    scene.background.set(state.background);
    scene.fog.color.set(state.fog);
    hemisphere.color.set(state.hemiSky);
    hemisphere.groundColor.set(state.hemiGround);
    sun.color.set(state.sun);
    sun.intensity = state.sunPower;
    renderer.toneMappingExposure = state.exposure;
    weather.rain.visible = currentTheme === "rain";
    weather.snow.visible = currentTheme === "snow" || currentTheme === "winter";
  }

  function updateWorldSun(now) {
    if (currentTheme !== "world") return;
    const region =
      WORLD_REGIONS.find((item) => item.id === currentRegion) ||
      WORLD_REGIONS.find((item) => item.id === "central");
    const regionalOffset =
      Number(region?.utcOffsetHours || 0) * (WORLD_DAY_MS / 24);
    const clock = worldClock(now + clockOffset + regionalOffset);
    const angle = clock.progress * Math.PI * 2 - Math.PI / 2;
    sun.position.set(Math.cos(angle) * 34, 12 + Math.max(0, Math.sin(angle)) * 28, Math.sin(angle) * 24);
    if (Math.floor(now / 30000) !== Math.floor((now - 1000 / 60) / 30000)) applyTheme("world");
  }

  function setTheme(theme) {
    applyTheme(theme);
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
      if (currentTheme === "world") applyTheme("world");
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
    moveTarget.copy(destination);
    hasMoveTarget = false;
    cameraFocus = null;
    nearestLandmark();
    onMovement({
      x: destination.x,
      y: destination.y,
      z: destination.z,
      heading: player.rotation.y,
      activity: `arriving in ${regionId} campus`,
    });
    return true;
  }

  function travelToSpace(spaceId) {
    const destinations = {
      "sky-campus": new THREE.Vector3(-17, 15.45, 18),
      "space-station": new THREE.Vector3(24, 18.45, 24),
      "code-planet": new THREE.Vector3(28, 15.45, -22),
      "organization-region": new THREE.Vector3(-29, 14.45, -23),
      "planet-atlas": new THREE.Vector3(-2, 22.45, -31),
    };
    const destination = destinations[spaceId];
    if (!destination) return false;
    currentSpace = spaceId;
    currentFloorY = destination.y;
    player.position.copy(destination);
    moveTarget.copy(destination);
    hasMoveTarget = false;
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
    });
    return true;
  }

  function focusLandmark(id, options = {}) {
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
    const direction = new THREE.Vector3(
      player.position.x - landmark.position[0],
      0,
      player.position.z - landmark.position[2],
    );
    if (direction.lengthSq() < 0.1) direction.set(1, 0, 1);
    direction.normalize().multiplyScalar(options.distance || 5.2);
    moveTarget.set(
      clamp(landmark.position[0] + direction.x, -WORLD_RADIUS, WORLD_RADIUS),
      player.position.y,
      clamp(landmark.position[2] + direction.z, -WORLD_RADIUS, WORLD_RADIUS),
    );
    hasMoveTarget = options.move !== false;
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

  function movementVector() {
    const movement = new THREE.Vector3();
    const active = (code) => keys.has(code) || touchKeys.has(code);
    if (active("KeyW") || active("ArrowUp")) movement.z -= 1;
    if (active("KeyS") || active("ArrowDown")) movement.z += 1;
    if (active("KeyA") || active("ArrowLeft")) movement.x -= 1;
    if (active("KeyD") || active("ArrowRight")) movement.x += 1;
    return movement.lengthSq() ? movement.normalize() : movement;
  }

  function walkPlayer(delta, time) {
    const movement = movementVector();
    let walking = false;
    if (movement.lengthSq()) {
      hasMoveTarget = false;
      cameraFocus = null;
      player.position.addScaledVector(movement, PLAYER_SPEED * delta);
      player.rotation.y = Math.atan2(movement.x, movement.z);
      walking = true;
    } else if (hasMoveTarget) {
      const toTarget = moveTarget.clone().sub(player.position);
      toTarget.y = 0;
      if (toTarget.length() < 0.18) {
        hasMoveTarget = false;
      } else {
        toTarget.normalize();
        player.position.addScaledVector(toTarget, PLAYER_SPEED * 0.86 * delta);
        player.rotation.y = Math.atan2(toTarget.x, toTarget.z);
        walking = true;
      }
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
        activity: currentLocation === "Town Square" ? "exploring the Town Square" : `visiting ${currentLocation}`,
      });
    }
  }

  function updateRemotePlayers(delta, time) {
    remotePlayers.forEach((avatar) => {
      avatar.position.lerp(avatar.userData.targetPosition, 1 - Math.pow(0.002, delta));
      let headingDelta = avatar.userData.targetHeading - avatar.rotation.y;
      headingDelta = Math.atan2(Math.sin(headingDelta), Math.cos(headingDelta));
      avatar.rotation.y += headingDelta * (1 - Math.pow(0.01, delta));
      const walking = avatar.position.distanceToSquared(avatar.userData.targetPosition) > 0.01;
      const gait = walking ? Math.sin(time * 0.009 + avatar.userData.phase) * 0.42 : 0;
      avatar.userData.leftArm.rotation.x = gait;
      avatar.userData.rightArm.rotation.x = -gait;
      avatar.userData.leftLeg.rotation.x = -gait * 0.7;
      avatar.userData.rightLeg.rotation.x = gait * 0.7;
    });
  }

  function updateCamera(delta) {
    const target = cameraFocus
      ? cameraFocus.clone()
      : player.position.clone().add(new THREE.Vector3(0, 2.2, 0));
    const zoom = cameraFocus ? 0.78 : 1;
    const desired = target.clone().add(
      new THREE.Vector3(
        CAMERA_OFFSET[0] * zoom,
        CAMERA_OFFSET[1] * zoom,
        CAMERA_OFFSET[2] * zoom,
      ),
    );
    camera.position.lerp(desired, 1 - Math.pow(0.0008, delta));
    camera.lookAt(target);
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
        browser: remote.browser || "Browser",
        os: remote.os || "Device",
        status: remote.activity || "exploring",
        accountStatus: remote.accountStatus || "Guest",
        localTime: remote.localTime || "",
        nodes: Array.isArray(remote.nodes) ? remote.nodes.slice(0, 6) : [],
      };
      const badgeKey = JSON.stringify(badgeIdentity);
      if (!avatar) {
        avatar = createAvatar(
          THREE,
          badgeIdentity,
          { remote: true, scale: 0.92 },
        );
        avatar.userData.badgeKey = badgeKey;
        avatar.position.set(
          Number(remote.x) || (Math.random() - 0.5) * 8,
          0.38,
          Number(remote.z) || (Math.random() - 0.5) * 8,
        );
        world.add(avatar);
        remotePlayers.set(remote.id, avatar);
        remoteLabels.set(remote.id, makePlayerLabel(avatar, labelLayer));
      }
      const sharedInactive = remote.activity === "idle";
      if (sharedInactive) {
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
      }
      avatar.userData.targetHeading = Number(remote.heading) || 0;
      avatar.userData.status = remote.activity || "exploring";
      if (avatar.userData.badgeKey !== badgeKey) {
        updateAvatarBadge(THREE, avatar, badgeIdentity, true);
        syncOperatorBelt(THREE, avatar, badgeIdentity.nodes.length);
        avatar.userData.badgeKey = badgeKey;
        remoteLabels.get(remote.id).textContent = remote.name || "visitor";
      }
    });
    remotePlayers.forEach((avatar, id) => {
      if (seen.has(id)) return;
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

  function visitNeighborhoodHome(ownerId) {
    const home = neighborhoodHomes.get(String(ownerId || ""));
    if (!home) return false;
    const destination = home.position.clone();
    destination.x += Math.cos(home.angle) * 1.15;
    destination.z += Math.sin(home.angle) * 1.15;
    currentSpace = "town-square";
    currentFloorY = 0.38;
    player.position.copy(destination);
    moveTarget.copy(destination);
    hasMoveTarget = false;
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
    });
    return true;
  }

  function updateNetworkNodes(nodes = []) {
    const seen = new Set();
    nodes.slice(0, 10).forEach((node, index) => {
      const id = `node:${node.name || node.label || index}`;
      seen.add(id);
      let avatar = nodeAvatars.get(id);
      if (!avatar) {
        avatar = createAvatar(
          THREE,
          {
            id,
            name: node.name || node.label || `mirror-${index + 1}`,
            flag: "◈",
            browser: "MIRROR",
            os: "NODE",
            status: "operating a public mirror",
            nodes: [id],
          },
          { remote: true, scale: 0.82 },
        );
        const angle = (index / Math.max(1, Math.min(nodes.length, 10))) * Math.PI * 2;
        avatar.position.set(
          9 + Math.cos(angle) * 4.4,
          0.38,
          -10 + Math.sin(angle) * 4.4,
        );
        avatar.rotation.y = -angle;
        world.add(avatar);
        nodeAvatars.set(id, avatar);
      }
    });
    nodeAvatars.forEach((avatar, id) => {
      if (seen.has(id)) return;
      world.remove(avatar);
      nodeAvatars.delete(id);
    });
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
    bots.slice(0, 12).forEach((bot, index) => {
      const id = `bot:${String(bot.id || index).slice(0, 48)}`;
      seen.add(id);
      let avatar = botAvatars.get(id);
      if (!avatar) {
        avatar = createAvatar(
          THREE,
          {
            id,
            name: String(bot.name || "Automated agent").slice(0, 32),
            flag: "⌘",
            browser: String(bot.type || "BOT").slice(0, 18),
            os: "AGENT",
            status: "available",
            accountStatus: bot.verified === true ? "Verified bot" : "Unverified bot",
          },
          { remote: true, scale: 0.78 },
        );
        const center = landmarkById(index % 2 ? "fediverse" : "security").position;
        const angle = (index / Math.max(1, Math.min(bots.length, 12))) * Math.PI * 2;
        avatar.position.set(
          center[0] + Math.cos(angle) * 5.2,
          0.38,
          center[2] + Math.sin(angle) * 5.2,
        );
        avatar.rotation.y = -angle;
        world.add(avatar);
        botAvatars.set(id, avatar);
      }
    });
    botAvatars.forEach((avatar, id) => {
      if (seen.has(id)) return;
      world.remove(avatar);
      botAvatars.delete(id);
    });
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

  function updateQuarantine(quarantine = {}) {
    const jail = landmarkObjects.get("quarantine");
    if (!jail) return;
    const existing = jail.userData.liveRestrictionLayer;
    if (existing) {
      jail.remove(existing);
      existing.traverse((child) => {
        child.geometry?.dispose?.();
        child.material?.map?.dispose?.();
        child.material?.dispose?.();
      });
    }
    const layer = new THREE.Group();
    layer.name = "privacy-safe-live-quarantine";
    const summary = Array.isArray(quarantine.summary)
      ? quarantine.summary.slice(0, 12)
      : [];
    const restrictions = Array.isArray(quarantine.restrictions)
      ? quarantine.restrictions.slice(0, 24)
      : [];
    const aggregateTotal = summary.reduce(
      (total, item) => total + Math.max(0, Number(item?.count) || 0),
      0,
    );
    const markers = restrictions.length
      ? restrictions
      : Array.from(
          { length: Math.min(24, aggregateTotal) },
          (_, index) => summary[index % Math.max(1, summary.length)] || {},
        );
    markers.forEach((item, index) => {
      const angle = (index / Math.max(1, markers.length)) * Math.PI * 2;
      const marker = new THREE.Mesh(
        new THREE.DodecahedronGeometry(0.16, 0),
        makeMaterial(
          THREE,
          item?.status === "revoked" || item?.status === "expired"
            ? "#9da8aa"
            : "#ff8e78",
          {
            emissive:
              item?.status === "revoked" || item?.status === "expired"
                ? "#354044"
                : "#8b2d24",
            emissiveIntensity: 0.52,
          },
        ),
      );
      marker.position.set(
        Math.cos(angle) * (1.1 + (index % 3) * 0.32),
        0.95 + (index % 4) * 0.42,
        Math.sin(angle) * (1.1 + (index % 3) * 0.32),
      );
      layer.add(marker);
    });
    const statusLabel = makeLabelSprite(
      THREE,
      `${aggregateTotal} RETAINED`,
      restrictions.length
        ? "role-gated generalized records"
        : "public aggregate only",
      "#ffb3a4",
    );
    statusLabel.scale.set(2.4, 0.8, 1);
    statusLabel.position.set(0, 5.35, 0);
    layer.add(statusLabel);
    jail.add(layer);
    jail.userData.liveRestrictionLayer = layer;
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
    playerLabel.textContent = identity.name;
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

  function playRewardEvent(targetHint = "") {
    let targetAvatar = null;
    nodeAvatars.forEach((avatar) => {
      if (
        !targetAvatar &&
        (!targetHint ||
          String(avatar.userData?.name || "")
            .toLowerCase()
            .includes(String(targetHint).toLowerCase()))
      ) {
        targetAvatar = avatar;
      }
    });
    const target = targetAvatar
      ? targetAvatar.position.clone()
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

  function handlePointerDown(event) {
    pointerDown = true;
    pointerStart.set(event.clientX, event.clientY);
    renderer.domElement.dataset.dragging = "true";
  }

  function handlePointerUp(event) {
    renderer.domElement.dataset.dragging = "false";
    if (!pointerDown) return;
    pointerDown = false;
    if (pointerStart.distanceTo(new THREE.Vector2(event.clientX, event.clientY)) > 9) return;
    pointerCoordinates(event);
    raycaster.setFromCamera(pointer, camera);
    const hit = raycaster.intersectObjects(interactive, false)[0];
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
    const groundHit = raycaster.intersectObject(ground, false)[0];
    if (groundHit) {
      moveTarget.copy(groundHit.point);
      moveTarget.y = player.position.y;
      hasMoveTarget = true;
      cameraFocus = null;
    }
  }

  function handleKeyDown(event) {
    if (
      event.target instanceof HTMLInputElement ||
      event.target instanceof HTMLTextAreaElement ||
      event.target instanceof HTMLSelectElement ||
      event.target?.isContentEditable
    ) return;
    if (["KeyW", "KeyA", "KeyS", "KeyD", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"].includes(event.code)) {
      keys.add(event.code);
      event.preventDefault();
    }
    if (event.code === "Escape") clearFocus();
  }

  function handleKeyUp(event) {
    keys.delete(event.code);
  }

  renderer.domElement.addEventListener("pointerdown", handlePointerDown);
  window.addEventListener("pointerup", handlePointerUp);
  window.addEventListener("keydown", handleKeyDown);
  window.addEventListener("keyup", handleKeyUp);

  const resizeObserver = new ResizeObserver(() => {
    const rect = container.getBoundingClientRect();
    const width = Math.max(1, Math.floor(rect.width));
    const height = Math.max(1, Math.floor(rect.height));
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, width < 700 ? 1.35 : 1.75));
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  });
  resizeObserver.observe(container);

  function animate(time) {
    if (!running || disposed) return;
    const delta = clamp((time - lastFrame) / 1000, 0, 0.05);
    lastFrame = time;
    walkPlayer(delta, time);
    updateRemotePlayers(delta, time);
    for (let index = emoteSprites.length - 1; index >= 0; index -= 1) {
      const flight = emoteSprites[index];
      const progress = Math.min(
        1,
        (performance.now() - flight.startedAt) / flight.duration,
      );
      flight.sprite.position.copy(flight.avatar.position);
      flight.sprite.position.y += 4.7 + progress * 1.1;
      flight.sprite.material.opacity = 1 - Math.max(0, progress - 0.65) / 0.35;
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
    updateWorldSun(Date.now());
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
    updateScreenLabel(THREE, player, playerLabel, camera, rect.width, rect.height, 4.5);
    remotePlayers.forEach((avatar, id) => {
      updateScreenLabel(THREE, avatar, remoteLabels.get(id), camera, rect.width, rect.height, 4.2);
    });
    renderer.render(scene, camera);
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

  function dispose() {
    disposed = true;
    renderer.setAnimationLoop(null);
    resizeObserver.disconnect();
    renderer.domElement.removeEventListener("pointerdown", handlePointerDown);
    window.removeEventListener("pointerup", handlePointerUp);
    window.removeEventListener("keydown", handleKeyDown);
    window.removeEventListener("keyup", handleKeyUp);
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

  applyTheme("world");
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
    setClockOffset,
    setControl,
    travelToRegion,
    travelToSpace,
    visitNeighborhoodHome,
    setRemotePlayers,
    updateNetworkNodes,
    updateFederatedInstances,
    updateBots,
    updateOrganizations,
    updateFediverseDirectory,
    updateQuarantine,
    updateMediaSpaces,
    updateIdentity,
    updateRepositoryGraph,
    playEmote,
    playRewardEvent,
    setPaused,
    dispose,
    getPosition: () => ({
      x: player.position.x,
      y: player.position.y,
      z: player.position.z,
      heading: player.rotation.y,
      space: currentSpace,
    }),
  };
}
