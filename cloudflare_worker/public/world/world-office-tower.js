export const OFFICE_ISLAND_CENTER = Object.freeze([0, 0, -215]);
export const OFFICE_ISLAND_RADIUS = 110;
export const OFFICE_BRIDGE_WIDTH = 12;
export const OFFICE_BRIDGE_START_Z = -86;
export const OFFICE_BRIDGE_END_Z = -108;

// The old single-room office was 17 units wide. The campus tower is deliberately
// about ten times wider while remaining a low-poly, browser-friendly structure.
export const OFFICE_WIDTH = 170;
export const OFFICE_DEPTH = 90;
// Give every team a genuinely spacious story. The original eight-unit
// spacing let the larger floor exhibits visually intersect the slabs above.
export const OFFICE_FLOOR_HEIGHT = 16;
export const OFFICE_FLOOR_COUNT = 10;
export const OFFICE_TOWER_HEIGHT = OFFICE_FLOOR_HEIGHT * OFFICE_FLOOR_COUNT;
export const OFFICE_FRONT_Z = OFFICE_DEPTH / 2;
export const OFFICE_DOOR_WIDTH = 10;
export const OFFICE_AVATAR_RADIUS = 0.46;
// The panoramic lift straddles the front curtain wall: its doors open back
// into each floor while the outward half gives riders a live view of Town.
export const OFFICE_ELEVATOR_CENTER_X = 70;
export const OFFICE_ELEVATOR_CENTER_Z = OFFICE_FRONT_Z;

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

// Keep this list byte-for-byte aligned with entry.py's
// OFFICE_FLOOR_TEAM_ALIASES. The Worker remains authoritative: this copy only
// explains which elevator floor a website organization-team checkbox will
// unlock after the server accepts the membership change and /office/floors is
// refreshed.
export const OFFICE_FLOOR_TEAM_ALIASES = Object.freeze({
  marketing: Object.freeze([
    "marketing",
    "marketing-team",
    "growth",
    "brand",
    "communications",
    "comms",
  ]),
  engineering: Object.freeze([
    "engineering",
    "engineers",
    "development",
    "developers",
    "platform",
    "frontend",
    "backend",
  ]),
  "product-design": Object.freeze([
    "product-design",
    "product",
    "design",
    "ux",
    "ui-ux",
  ]),
  security: Object.freeze([
    "security",
    "security-team",
    "trust-safety",
    "trust-and-safety",
  ]),
  infrastructure: Object.freeze([
    "infrastructure",
    "infra",
    "devops",
    "site-reliability",
    "sre",
  ]),
  community: Object.freeze([
    "community",
    "community-team",
    "developer-relations",
    "devrel",
  ]),
  partnerships: Object.freeze([
    "partnerships",
    "partnership",
    "business-development",
    "bizdev",
  ]),
  operations: Object.freeze([
    "operations",
    "ops",
    "people-operations",
    "finance-operations",
  ]),
});

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

export function officeTeamSlug(value) {
  return String(value || "")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/^-|-$/g, "")
    .slice(0, 64);
}

export function officeFloorsForTeam(value) {
  const team = officeTeamSlug(value);
  if (!team) return [];
  return OFFICE_FLOORS.filter((floor) =>
    OFFICE_FLOOR_TEAM_ALIASES[floor.id]?.includes(team),
  );
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
    // The lobby and the rooftop patio are the two common floors every member
    // shares. Every department story — Marketing included — is a team floor:
    // it only unlocks through the server-issued allowlist above.
    supplied.add("lobby");
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
    Object.freeze({
      type: "rect",
      minX: -15.5,
      maxX: 15.5,
      minZ: -39.2,
      maxZ: -33.8,
    }),
  ]),
  marketing: Object.freeze([
    // Match the actual tabletop instead of fencing off the chairs, boards,
    // and most of the studio with oversized invisible rectangles.
    Object.freeze({
      type: "rect",
      minX: -3.9,
      maxX: 3.9,
      minZ: -2,
      maxZ: 2,
    }),
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
    // Keep only the table footprints solid. The former six-unit circles also
    // swallowed every chair at radius five, so sitting or standing trapped a
    // visitor inside an invisible collider.
    Object.freeze({ type: "circle", x: -40, z: 8, radius: 3.5 }),
    Object.freeze({ type: "circle", x: 0, z: 8, radius: 3.5 }),
    Object.freeze({ type: "circle", x: 40, z: 8, radius: 3.5 }),
  ]),
});

const OFFICE_COMMON_OBSTACLES = Object.freeze([
  // The two side panes and outward glass face are solid collision surfaces.
  // The rear (-Z) face stays open so visitors walk into the car from a floor.
  Object.freeze({
    type: "rect",
    minX: OFFICE_ELEVATOR_CENTER_X - 5.45,
    maxX: OFFICE_ELEVATOR_CENTER_X - 4.55,
    minZ: OFFICE_ELEVATOR_CENTER_Z - 4.45,
    maxZ: OFFICE_ELEVATOR_CENTER_Z + 4.45,
  }),
  Object.freeze({
    type: "rect",
    minX: OFFICE_ELEVATOR_CENTER_X + 4.55,
    maxX: OFFICE_ELEVATOR_CENTER_X + 5.45,
    minZ: OFFICE_ELEVATOR_CENTER_Z - 4.45,
    maxZ: OFFICE_ELEVATOR_CENTER_Z + 4.45,
  }),
  Object.freeze({
    type: "rect",
    minX: OFFICE_ELEVATOR_CENTER_X - 5.45,
    maxX: OFFICE_ELEVATOR_CENTER_X + 5.45,
    minZ: OFFICE_ELEVATOR_CENTER_Z + 3.55,
    maxZ: OFFICE_ELEVATOR_CENTER_Z + 4.45,
  }),
]);

export function officeInteriorContains(x, z, radius = OFFICE_AVATAR_RADIUS) {
  const margin = Math.max(0, Number(radius) || 0);
  return (
    Math.abs(Number(x)) <= OFFICE_WIDTH / 2 - margin - 0.65 &&
    Math.abs(Number(z)) <= OFFICE_DEPTH / 2 - margin - 0.65
  );
}

export function officeElevatorCabinContains(
  x,
  z,
  radius = OFFICE_AVATAR_RADIUS,
) {
  const px = Number(x);
  const pz = Number(z);
  const margin = Math.max(0, Number(radius) || 0);
  if (!Number.isFinite(px) || !Number.isFinite(pz)) return false;
  return (
    Math.abs(px - OFFICE_ELEVATOR_CENTER_X) <= 4.45 - margin &&
    Math.abs(pz - OFFICE_ELEVATOR_CENTER_Z) <= 3.45 - margin
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
    (
      officeInteriorContains(x, z, radius) ||
      officeElevatorCabinContains(x, z, radius)
    ) &&
    !officePointHitsObstacle(floorId, x, z, radius)
  );
}
