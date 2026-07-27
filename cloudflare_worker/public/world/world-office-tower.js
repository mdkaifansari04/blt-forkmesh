export const OFFICE_ISLAND_CENTER = Object.freeze([0, 0, -215]);
export const OFFICE_ISLAND_RADIUS = 110;
export const OFFICE_BRIDGE_WIDTH = 12;
export const OFFICE_BRIDGE_START_Z = -86;
export const OFFICE_BRIDGE_END_Z = -108;

// The old single-room office was 17 units wide. The campus tower is deliberately
// about ten times wider while remaining a low-poly, browser-friendly structure.
export const OFFICE_WIDTH = 170;
export const OFFICE_DEPTH = 90;
export const OFFICE_FLOOR_HEIGHT = 8;
export const OFFICE_FLOOR_COUNT = 10;
export const OFFICE_TOWER_HEIGHT = OFFICE_FLOOR_HEIGHT * OFFICE_FLOOR_COUNT;
export const OFFICE_FRONT_Z = OFFICE_DEPTH / 2;
export const OFFICE_DOOR_WIDTH = 10;
export const OFFICE_AVATAR_RADIUS = 0.46;

export const OFFICE_FLOORS = Object.freeze([
  Object.freeze({
    id: "lobby",
    level: 0,
    label: "Lobby",
    team: "",
    publicForMembers: true,
    description: "Welcome desk, time clock, and chrome ForkMesh fountain",
  }),
  Object.freeze({
    id: "marketing",
    level: 1,
    label: "Marketing",
    team: "marketing",
    publicForMembers: true,
    description: "Campaign studio, task wall, and encrypted meeting table",
  }),
  Object.freeze({
    id: "engineering",
    level: 2,
    label: "Engineering",
    team: "engineering",
    description: "Pairing stations and a live build runway",
  }),
  Object.freeze({
    id: "product-design",
    level: 3,
    label: "Product & Design",
    team: "product-design",
    description: "Prototype gallery and color lab",
  }),
  Object.freeze({
    id: "security",
    level: 4,
    label: "Security",
    team: "security",
    description: "Threat-model arena and shield table",
  }),
  Object.freeze({
    id: "infrastructure",
    level: 5,
    label: "Infrastructure",
    team: "infrastructure",
    description: "Mirror racks and network observatory",
  }),
  Object.freeze({
    id: "community",
    level: 6,
    label: "Community",
    team: "community",
    description: "Town hall, games, and contributor lounge",
  }),
  Object.freeze({
    id: "partnerships",
    level: 7,
    label: "Partnerships",
    team: "partnerships",
    description: "Orbit table and collaboration booths",
  }),
  Object.freeze({
    id: "operations",
    level: 8,
    label: "Operations",
    team: "operations",
    description: "Mission control and incident timeline",
  }),
  Object.freeze({
    id: "rooftop",
    level: 9,
    label: "Rooftop Patio",
    team: "",
    publicForMembers: true,
    description: "Glass-safe patio, telescope, planters, and seating",
  }),
]);

const OFFICE_FLOOR_BY_ID = new Map(
  OFFICE_FLOORS.map((floor) => [floor.id, floor]),
);

export function officeFloorById(value) {
  return OFFICE_FLOOR_BY_ID.get(String(value || "").trim().toLowerCase()) || null;
}

export function officeFloorY(value) {
  const floor =
    typeof value === "number"
      ? OFFICE_FLOORS[Math.max(0, Math.min(OFFICE_FLOORS.length - 1, value))]
      : officeFloorById(value);
  return (floor?.level || 0) * OFFICE_FLOOR_HEIGHT;
}

export function normalizeOfficeFloorAccess(payload = {}) {
  const authenticated = payload?.authenticated === true;
  const supplied = new Set(
    Array.isArray(payload?.allowedFloorIds)
      ? payload.allowedFloorIds
          .map((value) => officeFloorById(value)?.id)
          .filter(Boolean)
      : [],
  );
  if (authenticated) {
    // These are the three member-visible floors required by the physical
    // design. Restricted floor data remains server-authorized separately.
    supplied.add("lobby");
    supplied.add("marketing");
    supplied.add("rooftop");
  }
  return {
    authenticated,
    account: authenticated
      ? String(payload?.account || "").trim().toLowerCase().slice(0, 64)
      : "",
    allowedFloorIds: [...supplied],
    teams: Array.isArray(payload?.teams)
      ? payload.teams
          .map((value) => String(value || "").trim().toLowerCase().slice(0, 64))
          .filter(Boolean)
          .slice(0, 32)
      : [],
  };
}

export function canAccessOfficeFloor(access, floorId) {
  const normalized = normalizeOfficeFloorAccess(access);
  const floor = officeFloorById(floorId);
  return Boolean(
    normalized.authenticated &&
      floor &&
      normalized.allowedFloorIds.includes(floor.id),
  );
}

export function officeCampusSurfaceContains(x, z, radius = 0) {
  const px = Number(x);
  const pz = Number(z);
  const margin = Math.max(0, Number(radius) || 0);
  if (!Number.isFinite(px) || !Number.isFinite(pz)) return false;
  const islandDistance = Math.hypot(
    px - OFFICE_ISLAND_CENTER[0],
    pz - OFFICE_ISLAND_CENTER[2],
  );
  if (islandDistance <= OFFICE_ISLAND_RADIUS - margin) return true;
  return (
    Math.abs(px) <= OFFICE_BRIDGE_WIDTH / 2 - margin &&
    pz <= OFFICE_BRIDGE_START_Z + margin &&
    pz >= OFFICE_BRIDGE_END_Z - margin
  );
}

const FLOOR_OBSTACLES = Object.freeze({
  lobby: Object.freeze([
    Object.freeze({ type: "circle", x: -18, z: -2, radius: 8.5 }),
    Object.freeze({ type: "rect", minX: 8, maxX: 38, minZ: 17, maxZ: 22 }),
  ]),
  marketing: Object.freeze([
    Object.freeze({ type: "rect", minX: -9, maxX: 9, minZ: -7, maxZ: 7 }),
    Object.freeze({ type: "rect", minX: -77, maxX: -69, minZ: -15, maxZ: 15 }),
  ]),
  engineering: Object.freeze([
    Object.freeze({ type: "rect", minX: -44, maxX: 44, minZ: -5, maxZ: 5 }),
  ]),
  "product-design": Object.freeze([
    Object.freeze({ type: "circle", x: -36, z: 0, radius: 4 }),
    Object.freeze({ type: "circle", x: -12, z: 0, radius: 4 }),
    Object.freeze({ type: "circle", x: 12, z: 0, radius: 4 }),
    Object.freeze({ type: "circle", x: 36, z: 0, radius: 4 }),
  ]),
  security: Object.freeze([
    Object.freeze({ type: "circle", x: 0, z: -4, radius: 9 }),
  ]),
  infrastructure: Object.freeze([
    Object.freeze({ type: "rect", minX: -58, maxX: -42, minZ: -31, maxZ: 31 }),
    Object.freeze({ type: "rect", minX: 42, maxX: 58, minZ: -31, maxZ: 31 }),
  ]),
  community: Object.freeze([
    Object.freeze({ type: "circle", x: 0, z: -3, radius: 10 }),
  ]),
  partnerships: Object.freeze([
    Object.freeze({ type: "circle", x: 0, z: 0, radius: 9 }),
  ]),
  operations: Object.freeze([
    Object.freeze({ type: "rect", minX: -38, maxX: 38, minZ: -8, maxZ: 8 }),
  ]),
  rooftop: Object.freeze([
    Object.freeze({ type: "circle", x: 58, z: -24, radius: 4 }),
    Object.freeze({ type: "circle", x: -40, z: 8, radius: 6 }),
    Object.freeze({ type: "circle", x: 0, z: 8, radius: 6 }),
    Object.freeze({ type: "circle", x: 40, z: 8, radius: 6 }),
  ]),
});

const OFFICE_COMMON_OBSTACLES = Object.freeze([
  // The elevator call panel stands immediately left of the glass cabin.
  Object.freeze({
    type: "rect",
    minX: 62.35,
    maxX: 63.65,
    minZ: -33.5,
    maxZ: -24.5,
  }),
  // Three sides of the elevator shaft are solid collision surfaces. The
  // south/front face stays open while the car is parked so visitors can walk
  // out onto the selected floor.
  Object.freeze({
    type: "rect",
    minX: 64.55,
    maxX: 65.45,
    minZ: -34.45,
    maxZ: -25.55,
  }),
  Object.freeze({
    type: "rect",
    minX: 74.55,
    maxX: 75.45,
    minZ: -34.45,
    maxZ: -25.55,
  }),
  Object.freeze({
    type: "rect",
    minX: 64.55,
    maxX: 75.45,
    minZ: -34.45,
    maxZ: -33.55,
  }),
]);

export function officeInteriorContains(x, z, radius = OFFICE_AVATAR_RADIUS) {
  const margin = Math.max(0, Number(radius) || 0);
  return (
    Math.abs(Number(x)) <= OFFICE_WIDTH / 2 - margin - 0.65 &&
    Math.abs(Number(z)) <= OFFICE_DEPTH / 2 - margin - 0.65
  );
}

export function officePointHitsObstacle(
  floorId,
  x,
  z,
  radius = OFFICE_AVATAR_RADIUS,
) {
  const px = Number(x);
  const pz = Number(z);
  const margin = Math.max(0, Number(radius) || 0);
  return [
    ...OFFICE_COMMON_OBSTACLES,
    ...(FLOOR_OBSTACLES[officeFloorById(floorId)?.id] || []),
  ].some(
    (obstacle) => {
      if (obstacle.type === "circle") {
        return (
          Math.hypot(px - obstacle.x, pz - obstacle.z) <
          obstacle.radius + margin
        );
      }
      return (
        px >= obstacle.minX - margin &&
        px <= obstacle.maxX + margin &&
        pz >= obstacle.minZ - margin &&
        pz <= obstacle.maxZ + margin
      );
    },
  );
}

export function officeInteriorPointIsWalkable(
  floorId,
  x,
  z,
  radius = OFFICE_AVATAR_RADIUS,
) {
  return (
    officeInteriorContains(x, z, radius) &&
    !officePointHitsObstacle(floorId, x, z, radius)
  );
}
