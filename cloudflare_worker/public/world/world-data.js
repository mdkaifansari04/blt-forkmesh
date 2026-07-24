export const WORLD_DAY_MS = 4 * 60 * 60 * 1000;

export const LANDMARKS = [
  {
    id: "information",
    label: "Information booth",
    shortLabel: "Start here",
    eyebrow: "WELCOME / 01",
    icon: "i",
    color: "#9ef7c6",
    position: [-9, 0, 8],
    summary: "Your orientation point for the mesh, privacy, wallets, and running a node.",
    metaphor: "A staffed welcome booth at the Town Square entrance.",
    reality:
      "A guide to ForkMesh’s local-first Git mirrors, edge routing, encrypted collaboration, account setup, and desktop node.",
    status: "Available now",
    statusTone: "live",
    bullets: [
      "Repositories stay on independently operated nodes.",
      "In the active community-member reward path, self-custodial payout keys stay on the user’s device and the Worker stores only public payout addresses, unsigned intents, and finalized signatures. The separate community-pool signer stays in the first instance operator’s encrypted local Qt client.",
      "The Worker coordinates discovery and routes; it is not canonical Git storage.",
      "Country is approximate. Raw IP addresses are never shown in the world.",
      "Encryption reduces exposure but does not eliminate endpoint, authorization, or operational risk.",
    ],
    primary: { label: "Take the 90-second tour", action: "tour" },
    secondary: { label: "Read technical docs", href: "/docs" },
  },
  {
    id: "fountain",
    label: "SOL reward fountain",
    shortLabel: "Reward pool",
    eyebrow: "COMMUNITY REWARDS / 02",
    icon: "◎",
    color: "#f7c96b",
    position: [0, 0, 0],
    summary: "A visual model of transparent community incentives for healthy mirror operators.",
    metaphor: "Sunlight and coins flow from a shared fountain toward eligible nodes.",
    reality:
      "The production mainnet-beta flow reads public on-chain state, accepts direct wallet-to-pool contributions, applies auditable eligibility, and sends unsigned plans to the instance owner’s local Qt signer. Development instances may explicitly select a test network.",
    status: "Mainnet-beta · external signer",
    statusTone: "live",
    bullets: [
      "Choose an immediate all-eligible-node contribution or add funds to the randomized trickle pool.",
      "Community members’ wallet keys never reach ForkMesh; the pool key stays only in the instance operator’s encrypted local Qt signer.",
      "Pending walletless allocations expire after 24 hours without moving funds.",
      "Visual coins are not guaranteed rewards or investments.",
      "Public pool, pending allocation, and finalized on-chain states are shown separately.",
    ],
    primary: { label: "Join / fund reward program", action: "reward" },
    secondary: { label: "Payout eligibility", href: "/mirror-payouts" },
  },
  {
    id: "repositories",
    label: "Repository rings",
    shortLabel: "Repositories",
    eyebrow: "CODE DISTRICT / 03",
    icon: "{ }",
    color: "#77d9ff",
    position: [11, 0, 3],
    summary: "Walk through live, stub, and mirrored repositories as navigable structures.",
    metaphor: "Files orbit their repository entrance in architectural rings.",
    reality:
      "Public catalog records come from the relay API. A repository marked external or stub is not represented as actively mirrored.",
    status: "Live catalog connected",
    statusTone: "live",
    bullets: [
      "Large files render as larger icons; directory rings show depth.",
      "Signed state, checksums, and host health distinguish available mirrors.",
      "A healthy response proves availability, not automatic trust; identity and content integrity are verified separately.",
      "Authorized private repositories appear only to their owners and explicitly authorized collaborators, and are never broadcast into public presence.",
      "The game socket never carries Git data; clone, browse, release, and private-replica ciphertext use the authenticated direct-HTTPS routing layer.",
    ],
    primary: { label: "Browse repository portals", action: "repositories" },
    secondary: { label: "Open 2D repository view", href: "/dashboard/repos" },
  },
  {
    id: "routing",
    label: "Cloud routing station",
    shortLabel: "Routing",
    eyebrow: "EDGE ROUTING / 04",
    icon: "↝",
    color: "#80e8ff",
    position: [20, 0, 3],
    summary: "Inspect how a stable ForkMesh URL selects an available, authenticated mirror.",
    metaphor: "A dispatch station sends each repository request along a healthy route.",
    reality:
      "Normal repository traffic stays on HTTPS. The edge layer performs service discovery and failover without becoming repository storage, exposing a mirror hostname, or carrying Git data over the game socket.",
    status: "Direct HTTPS routing integration",
    statusTone: "prototype",
    bullets: [
      "Cloudflare-proxied mirror hostnames keep the public repository URL stable.",
      "Signed endpoint manifests bind a mirror identity to its declared HTTPS origin.",
      "Fresh health, integrity, and authorization state are evaluated independently.",
      "Private repositories remain fail-closed and non-discoverable before route selection.",
    ],
    primary: { label: "Inspect mirror network", action: "repositories" },
    secondary: { label: "Read routing operations", href: "/docs/operations" },
  },
  {
    id: "organizations",
    label: "Organization quarter",
    shortLabel: "Organizations",
    eyebrow: "TEAM SPACES / 05",
    icon: "▤",
    color: "#d5b6ff",
    position: [9, 0, -10],
    summary: "Buildings and gardens map teams, repositories, offices, and permission boundaries.",
    metaphor: "Floors are teams; offices are developer workspaces; lobbies are public project rooms.",
    reality:
      "Organization membership and repository authorization come from explicit role checks. Private floors stay disabled unless owner-controlled encryption can enforce their intended access boundary.",
    status: "Preview district",
    statusTone: "prototype",
    bullets: [
      "Public, restricted, and private floors are visually distinct.",
      "Members can display current work without revealing sensitive browsing data.",
      "Mirror nodes can be linked to a user or organization.",
      "Sky offices remain functional collaboration rooms, not decorative NFTs.",
    ],
    primary: { label: "Visit organization lobby", action: "organizations" },
    secondary: { label: "Manage organizations", href: "/dashboard/settings/organizations" },
  },
  {
    id: "fediverse",
    label: "Fediverse arcade",
    shortLabel: "Social",
    eyebrow: "FEDIVERSE / 06",
    icon: "⁂",
    color: "#ff9eb7",
    position: [-12, 0, -7],
    summary: "Mastodon and Lemmy communities become consent-aware social spaces.",
    metaphor: "Connected arches show instances, communities, and public relationships.",
    reality:
      "Only public API data and user-approved connections can be displayed. Automated repository updates are digested at most once daily.",
    status: "ActivityPub foundation live",
    statusTone: "live",
    bullets: [
      "Private followers and private-account relationships are excluded.",
      "Empty or duplicate automated posts are not published.",
      "Repository owners can preview or disable outbound digests.",
      "Social avatars are decorative context, not behavioral surveillance.",
    ],
    primary: { label: "See community map", action: "fediverse" },
    secondary: { label: "ActivityPub design", href: "/docs/activitypub" },
  },
  {
    id: "security",
    label: "Security workshop",
    shortLabel: "Security",
    eyebrow: "TRUST & ANALYSIS / 07",
    icon: "⌁",
    color: "#88f0df",
    position: [-2, 0, -13],
    summary: "Clipboards show scan scope, commit, findings, limitations, and review state.",
    metaphor: "A workshop clipboard follows every repository build.",
    reality:
      "Automated findings apply only to the scanned commit. A clean scan is never presented as a guarantee, and exploit detail remains restricted.",
    status: "Daily scoped scans · owner-sealed agents",
    statusTone: "live",
    bullets: [
      "Scanner, version, policy, duration, scope, and exclusions stay visible.",
      "Critical findings are redacted until authorized reviewers can act.",
      "False positives and human review are first-class states.",
      "Private agent payloads are owner-sealed; platform administrators do not receive an automatic decryption path.",
    ],
    primary: { label: "Open scan clipboard", action: "security" },
    secondary: { label: "Report a vulnerability", href: "/security-report" },
  },
  {
    id: "launchpad",
    label: "World launchpad",
    shortLabel: "Other worlds",
    eyebrow: "PORTALS / 08",
    icon: "↑",
    color: "#b6d8ff",
    position: [18, 0, 12],
    summary: "Portals lead to repository worlds, sky campuses, events, and workshops.",
    metaphor: "Launch pads turn URLs and collaboration rooms into destinations.",
    reality:
      "Each destination keeps standard permission checks and graceful HTML fallbacks. Scheduled events use UTC even when visual regions show different times.",
    status: "Route preview",
    statusTone: "prototype",
    bullets: [
      "Shared world time keeps events synchronized.",
      "Personal weather and lighting themes stay local to your device.",
      "Live collaboration can degrade to polling when realtime transport is unavailable.",
      "Large transfers and releases remain ordinary HTTPS downloads.",
    ],
    primary: { label: "Preview destinations", action: "launchpad" },
    secondary: { label: "See events", href: "/changelog" },
  },
  {
    id: "events",
    label: "Community stage",
    shortLabel: "Events",
    eyebrow: "EVENTS / 09",
    icon: "◫",
    color: "#ffb77d",
    position: [-21, 0, 9],
    summary: "UTC-scheduled hackathons, releases, workshops, and community broadcasts.",
    metaphor: "A public stage and departure board make shared events visible from the plaza.",
    reality:
      "Event records use UTC internally and are formatted in the viewer’s selected time zone. Announcements are ordinary HTTPS data and remain usable without realtime presence.",
    status: "UTC event board live",
    statusTone: "live",
    bullets: [
      "Hackathons, releases, workshops, and presentations have explicit event types.",
      "The board never infers attendance from private browsing activity.",
      "Realtime signals can announce a change, but event detail is fetched over HTTPS.",
      "Every event destination retains its normal authorization checks.",
    ],
    primary: { label: "Open event board", action: "events" },
    secondary: { label: "Release history", href: "/changelog" },
  },
  {
    id: "neighborhood",
    label: "Contributor neighborhood",
    shortLabel: "Activity homes",
    eyebrow: "PRESENCE / 10",
    icon: "⌂",
    color: "#b8e986",
    position: [-23, 0, -7],
    summary: "Consent-aware homes, gardens, and a quiet seating area for contributors.",
    metaphor: "An unlocked door invites visitors; a closed door asks them to knock.",
    reality:
      "Door and availability controls expose only a chosen public state. Inactivity can be hidden, and no exact URL, search, form value, or behavioral history enters public presence.",
    status: "Privacy controls live",
    statusTone: "live",
    bullets: [
      "Availability may be Online, Away, Inactive, Offline mirror operator, or Returning contributor.",
      "A user can hide availability and inactivity entirely.",
      "Knocking is a consent request, not permission to enter a restricted room.",
      "Houses and yards are public profile metaphors, not precise location tracking.",
    ],
    primary: { label: "Set availability", action: "neighborhood" },
    secondary: { label: "Edit public profile", href: "/dashboard/settings/profile" },
  },
  {
    id: "workshops",
    label: "Code workshops",
    shortLabel: "Workshops",
    eyebrow: "ANALYSIS / 11",
    icon: "⌘",
    color: "#73f0ad",
    position: [1, 0, 22],
    summary: "Collaborative, recommendation-first analysis rooms for repository architecture.",
    metaphor: "A workbench turns code structure, dependencies, models, tests, and findings into inspectable maps.",
    reality:
      "Analysis runs only against an explicitly selected authorized repository. Findings are recommendations with supporting paths, not guaranteed facts.",
    status: "Client workshop live",
    statusTone: "live",
    bullets: [
      "Architecture, models, dependencies, redundancy, dead code, security, coverage, documentation, performance, and licenses are separate scopes.",
      "Database workshops map model definitions, relationships, use sites, and possible cycles.",
      "Private code is never sent to an external model without explicit repository-owner authorization.",
      "Live collaboration uses the existing encrypted room and degrades to saved results when realtime is unavailable.",
    ],
    primary: { label: "Open a workshop", action: "workshops" },
    secondary: { label: "Open agents", href: "/dashboard/agents" },
  },
  {
    id: "broadcast",
    label: "Broadcast garden",
    shortLabel: "Media",
    eyebrow: "COMMUNITY MEDIA / 12",
    icon: "♫",
    color: "#8fcfff",
    position: [16, 0, 22],
    summary: "Opt-in radio, playlists, presentations, and moderated watch rooms.",
    metaphor: "A garden stage becomes a shared listening room or presentation screen.",
    reality:
      "ForkMesh links to permitted first-party streams or official embeds. Nothing autoplays; the viewer must consent, can mute instantly, and cannot use the room to rebroadcast unlicensed media.",
    status: "Opt-in radio controls live",
    statusTone: "live",
    bullets: [
      "Station and track metadata are shown only when the provider supplies them.",
      "Shared playlists store links and scheduling metadata, not copied media.",
      "Room moderators can remove an item or stop a shared session.",
      "Video rooms use official embeds and preserve each provider’s controls and terms.",
    ],
    primary: { label: "Open media controls", action: "broadcast" },
    secondary: { label: "Open community chat", href: "/dashboard/chat" },
  },
  {
    id: "support",
    label: "Project Support Center",
    shortLabel: "Support",
    eyebrow: "PROJECT SUSTAINABILITY / 13",
    icon: "♥",
    color: "#ffd08f",
    position: [-15, 0, 20],
    summary: "Transparent, voluntary ways to support ForkMesh itself, separate from node rewards.",
    metaphor: "A public help desk connects contributors, members, and recurring project supporters.",
    reality:
      "Support destinations disclose their recipient and purpose. They do not buy rewards, investment returns, or governance dominance, and ForkMesh does not custody a supporter’s wallet.",
    status: "Transparent support routes",
    statusTone: "live",
    bullets: [
      "Recurring project support uses the project’s published Patreon destination.",
      "Direct sponsorship starts with the published founders contact; no hosted wallet secret form exists.",
      "Community membership and code contributions do not require a payment.",
      "The Support Center is not the Global Reward Pool and never promises a financial return.",
    ],
    primary: { label: "Compare support options", action: "support" },
    secondary: { label: "Contribute code", href: "/dashboard/repos" },
  },
];

export const TOUR_STEPS = [
  {
    landmark: "information",
    title: "Welcome to ForkMesh",
    copy: "You entered as a guest immediately. Move with WASD, arrow keys, a click on the plaza, or the touch controls.",
  },
  {
    landmark: "fountain",
    title: "Rewards are transparent, not custodial",
    copy: "The fountain is a metaphor. Actual transfers require an external self-custodial address and an explicit on-chain state.",
  },
  {
    landmark: "repositories",
    title: "Repositories become places",
    copy: "Repository portals are seeded from the public catalog. Live, mirrored, stub, and unavailable are separate states.",
  },
  {
    landmark: "organizations",
    title: "Permissions shape the city",
    copy: "Organization floors, offices, and private repo spaces remain gated by the same authorization rules as the standard interface.",
  },
  {
    landmark: "security",
    title: "Trust stays inspectable",
    copy: "Every metaphor has a technical panel. Scan results, mirror health, and reward visuals disclose their scope and limitations.",
  },
];

export const THEME_OPTIONS = [
  { id: "world", label: "World sync" },
  { id: "day", label: "Day" },
  { id: "sunset", label: "Sunset" },
  { id: "night", label: "Night" },
  { id: "rain", label: "Rain" },
  { id: "snow", label: "Snow" },
  { id: "winter", label: "Winter" },
  { id: "cyberpunk", label: "Cyberpunk" },
  { id: "low-light", label: "Low light" },
];

export const AVAILABILITY_OPTIONS = [
  { id: "online", label: "Online" },
  { id: "away", label: "Away" },
  { id: "inactive", label: "Inactive" },
  { id: "recent", label: "Last active recently" },
  { id: "offline-operator", label: "Offline mirror operator" },
  { id: "returning", label: "Returning contributor" },
];

export const ACTIVITY_OPTIONS = [
  { id: "automatic", label: "Automatic, generalized" },
  { id: "exploring-town-square", label: "Exploring the Town Square" },
  { id: "viewing-repository", label: "Viewing a repository" },
  { id: "reading-documentation", label: "Reading documentation" },
  { id: "visiting-organization", label: "Visiting an organization" },
  { id: "browsing-code-visualization", label: "Browsing a code visualization" },
];

export const WORLD_REGIONS = [
  { id: "east", label: "East Campus", phase: "Morning", utcOffsetHours: -5 },
  { id: "central", label: "Central Campus", phase: "Afternoon", utcOffsetHours: 0 },
  { id: "west", label: "West Campus", phase: "Evening", utcOffsetHours: 5 },
];

export const RADIO_STATIONS = [
  {
    id: "forkmesh-focus",
    name: "ForkMesh Focus Tones",
    provider: "ForkMesh",
    description:
      "Original four-hour procedural score generated locally under CC0-1.0; no streamed media.",
    playMode: "generated",
    homepageUrl:
      "https://github.com/forkmesh/forkmesh/blob/main/docs/world-soundtrack-license.md",
  },
  {
    id: "somafm-groovesalad",
    name: "Groove Salad",
    provider: "SomaFM",
    description: "Ambient and downtempo instrumental grooves.",
    playMode: "external",
    homepageUrl: "https://somafm.com/groovesalad/",
  },
  {
    id: "somafm-defcon",
    name: "DEF CON Radio",
    provider: "SomaFM",
    description: "Music for hacking, coding, and security workshops.",
    playMode: "external",
    homepageUrl: "https://somafm.com/defcon/",
  },
];

export const WORKSHOP_TYPES = [
  "Architecture analysis",
  "Database-model analysis",
  "Dependency mapping",
  "Redundancy detection",
  "Dead-code detection",
  "Security analysis",
  "Test-coverage analysis",
  "Documentation analysis",
  "Performance analysis",
  "License compatibility analysis",
];

export function landmarkById(id) {
  return LANDMARKS.find((landmark) => landmark.id === id) || LANDMARKS[0];
}

export function worldClock(now = Date.now()) {
  const progress = (now % WORLD_DAY_MS) / WORLD_DAY_MS;
  const virtualMinutes = Math.floor(progress * 24 * 60);
  const hour = Math.floor(virtualMinutes / 60);
  const minute = virtualMinutes % 60;
  let phase = "Day";
  if (hour < 5 || hour >= 22) phase = "Night";
  else if (hour < 7) phase = "Sunrise";
  else if (hour >= 19) phase = "Sunset";
  return {
    progress,
    hour,
    minute,
    phase,
    label: `${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}`,
  };
}

export function detectClient() {
  const ua = navigator.userAgent || "";
  const platform = navigator.userAgentData?.platform || navigator.platform || "";
  let browser = "Browser";
  if (/Edg\//.test(ua)) browser = "Edge";
  else if (/Firefox\//.test(ua)) browser = "Firefox";
  else if (/CriOS\//.test(ua)) browser = "Chrome";
  else if (/Chrome\//.test(ua)) browser = "Chrome";
  else if (/Safari\//.test(ua)) browser = "Safari";

  let os = "Device";
  if (/Android/i.test(ua)) os = "Android";
  else if (/iPhone|iPad|iPod/i.test(ua)) os = "iOS";
  else if (/Mac/i.test(platform)) os = "macOS";
  else if (/Win/i.test(platform)) os = "Windows";
  else if (/Linux/i.test(platform)) os = "Linux";

  return { browser, os, touch: navigator.maxTouchPoints > 0 };
}

export function flagEmoji(code) {
  const normalized = String(code || "").trim().toUpperCase();
  if (!/^[A-Z]{2}$/.test(normalized)) return "◌";
  return String.fromCodePoint(...[...normalized].map((char) => 127397 + char.charCodeAt(0)));
}

export function sanitizePresenceText(value, fallback, max = 28) {
  const clean = String(value || "")
    .replace(/[^\p{L}\p{N} ._@-]/gu, "")
    .trim()
    .slice(0, max);
  return clean || fallback;
}
