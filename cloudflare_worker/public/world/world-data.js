export const LANDMARKS = [
  {
    id: "information",
    label: "Information booth",
    shortLabel: "Start here",
    eyebrow: "WELCOME / 01",
    icon: "i",
    color: "#9ef7c6",
    position: [-40, 0, 30],
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
    label: "Repository portals",
    shortLabel: "Repositories",
    eyebrow: "CODE DISTRICT / 03",
    icon: "{ }",
    color: "#77d9ff",
    position: [35, 0, 38],
    summary: "Authorized repositories form distinct perimeter portals with size-weighted file rings.",
    metaphor: "Each repository opens as its own three-dimensional sunburst around the city perimeter.",
    reality:
      "Public catalog records come from the relay API. A repository marked external or stub is not represented as actively mirrored.",
    status: "Live catalog connected",
    statusTone: "live",
    bullets: [
      "The live catalog distinguishes available mirrors, external stubs, private repositories authorized to the viewer, and unavailable hosts.",
      "Each catalog entry has a separate perimeter portal; arc width reflects commit-pinned bytes and each concentric ring adds one directory level.",
      "Signed state, checksums, and host health distinguish available mirrors.",
      "A healthy response proves availability, not automatic trust; identity and content integrity are verified separately.",
      "Authorized private repositories appear only to their owners and explicitly authorized collaborators, and are never broadcast into public presence.",
      "Normal repository traffic stays on HTTPS; the game socket never carries Git data. Clone, browse, release, and private-replica ciphertext use the authenticated direct-HTTPS routing layer.",
    ],
    primary: { label: "Browse repositories", action: "repositories" },
    secondary: { label: "Open 2D repository view", href: "/dashboard/repos" },
  },
  {
    id: "office",
    label: "ForkMesh Office",
    shortLabel: "Office",
    eyebrow: "COLLABORATION / 10",
    icon: "⌁",
    color: "#9ef7c6",
    position: [45, 0, -27],
    summary:
      "Meet collaborators through ForkMesh's existing encrypted channel system.",
    metaphor:
      "An open office lobby where the chat terminal becomes available after deliberate entry.",
    reality:
      "The office embeds the normal authorized chat client. Avatar position never grants room access or exposes private membership.",
    status: "Encrypted chat available",
    statusTone: "live",
    bullets: [
      "Guests can use only public World #general.",
      "Registered users see only public and authorized private channels.",
      "Messages, room keys, tokens, and attachments never enter world presence.",
      "The hidden chat iframe disconnects after leaving the office.",
    ],
    primary: { label: "Walk to the office", action: "office" },
    secondary: { label: "Open full chat", href: "/chat" },
  },
];

export const TOUR_STEPS = [
  {
    landmark: "information",
    title: "Welcome to ForkMesh",
    copy: "You entered as a guest immediately. Move with WASD, arrow keys, or the touch controls. Drag with a visible cursor to rotate the camera.",
  },
  {
    landmark: "fountain",
    title: "Rewards are transparent, not custodial",
    copy: "The fountain is a metaphor. Actual transfers require an external self-custodial address and an explicit on-chain state.",
  },
  {
    landmark: "repositories",
    title: "Repositories become places",
    copy: "The repository explorer is seeded from the authorized live catalog. Live, mirrored, stub, and unavailable are separate states.",
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
  {
    landmark: "office",
    title: "Collaboration has a place",
    copy: "Enter the Office deliberately to use the same encrypted, authorization-aware chat available outside the World.",
  },
];

export const THEME_OPTIONS = [
  { id: "world", label: "Full daylight" },
  { id: "rain", label: "Daylight + rain" },
  { id: "snow", label: "Daylight + snow" },
  { id: "winter", label: "Daylight + winter" },
  { id: "cyberpunk", label: "Local cyberpunk" },
  { id: "low-light", label: "Local low light" },
];

// A Supporting member perk: a shared, other-visitors-see-it-too outfit color
// that replaces the default country-flag shirt. Guests and Registered
// accounts keep the default flag shirt; the server drops this field back to
// "" for anyone whose trusted accountStatus isn't "Supporting member".
export const OUTFIT_COLOR_OPTIONS = [
  { id: "aurora", label: "Aurora green", color: "#39c783" },
  { id: "ember", label: "Ember orange", color: "#f2793a" },
  { id: "violet", label: "Signal violet", color: "#8b6df2" },
  { id: "gold", label: "Founder's gold", color: "#e0b23e" },
  { id: "slate", label: "Slate blue", color: "#3a6ea5" },
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
  { id: "visiting-office", label: "Visiting the ForkMesh Office" },
  { id: "browsing-code-visualization", label: "Browsing a code visualization" },
];

export const WORLD_STATUS_NOTE_MAX = 20;

// The picker is intentionally broad while the adjacent free-form emoji field
// accepts any single valid Unicode emoji sequence, including flags, skin tones,
// keycaps, and joined family/profession sequences. Keeping the picker data local
// means opening it never sends a search term or profile hint to a third party.
export const WORLD_EMOJI_CATEGORIES = Object.freeze([
  {
    id: "faces",
    label: "Faces",
    emoji: [
      "😀", "😃", "😄", "😁", "😆", "😅", "😂", "🙂", "🙃", "😉",
      "😊", "🥰", "😍", "🤩", "😘", "😎", "🤓", "🧐", "🤔", "🫡",
      "🤗", "🤫", "🤭", "😴", "🥳", "😭", "😤", "😱", "😇", "🤠",
    ],
  },
  {
    id: "gestures",
    label: "People",
    emoji: [
      "👋", "🤚", "🖐️", "✋", "🖖", "🫶", "👌", "🤌", "🤏", "✌️",
      "🤞", "🫰", "🤟", "🤘", "🤙", "👈", "👉", "👆", "👇", "☝️",
      "👍", "👎", "✊", "👊", "🤝", "🙏", "💪", "🧠", "🧑‍💻", "🧑🏽‍🔬",
    ],
  },
  {
    id: "nature",
    label: "Nature",
    emoji: [
      "🐶", "🐱", "🐭", "🐹", "🐰", "🦊", "🐻", "🐼", "🐨", "🐯",
      "🦁", "🐮", "🐷", "🐸", "🐵", "🦄", "🐝", "🦋", "🐙", "🐢",
      "🌱", "🌿", "🍀", "🌵", "🌲", "🌳", "🌴", "🌻", "🌈", "🔥",
    ],
  },
  {
    id: "food",
    label: "Food",
    emoji: [
      "🍏", "🍎", "🍊", "🍋", "🍉", "🍇", "🍓", "🫐", "🍒", "🥝",
      "🍅", "🥑", "🌽", "🥕", "🥐", "🍞", "🧀", "🍕", "🌮", "🍜",
      "🍣", "🍪", "🍩", "🍫", "☕", "🫖", "🧃", "🥤", "🧋", "🍿",
    ],
  },
  {
    id: "activity",
    label: "Activity",
    emoji: [
      "⚽", "🏀", "🏈", "⚾", "🎾", "🏐", "🏓", "🏸", "🥅", "⛳",
      "🛹", "🛼", "🚲", "🏆", "🥇", "🎯", "🎮", "🎲", "🧩", "🎨",
      "🎭", "🎸", "🎹", "🎧", "🎤", "📷", "🎬", "🚀", "🧘", "🏕️",
    ],
  },
  {
    id: "travel",
    label: "Travel",
    emoji: [
      "🚗", "🚕", "🚌", "🚎", "🏎️", "🚓", "🚑", "🚒", "🚜", "🛵",
      "🚆", "🚇", "🚄", "✈️", "🛫", "🛸", "🚁", "⛵", "🚢", "⚓",
      "🗺️", "🧭", "🏔️", "🏝️", "🏙️", "🌋", "🌍", "🌎", "🌏", "🌌",
    ],
  },
  {
    id: "objects",
    label: "Objects",
    emoji: [
      "⌚", "📱", "💻", "⌨️", "🖥️", "🖱️", "💾", "💿", "📡", "🔋",
      "🔌", "💡", "🔦", "🧰", "🔧", "🔨", "⚙️", "🧲", "🧪", "🔬",
      "🔭", "📚", "📝", "📌", "📎", "🔐", "🔑", "🎁", "💎", "🪄",
    ],
  },
  {
    id: "symbols",
    label: "Symbols",
    emoji: [
      "❤️", "🧡", "💛", "💚", "💙", "💜", "🖤", "🤍", "🤎", "💔",
      "❣️", "💕", "💯", "💢", "💬", "💭", "💤", "✨", "⭐", "🌟",
      "⚡", "✅", "❌", "❓", "❗", "♻️", "⚠️", "🔔", "🔕", "♾️",
    ],
  },
  {
    id: "flags",
    label: "Flags",
    emoji: [
      "🏳️", "🏴", "🏁", "🚩", "🏳️‍🌈", "🏳️‍⚧️", "🇦🇺", "🇧🇷", "🇨🇦", "🇨🇳",
      "🇪🇺", "🇫🇷", "🇩🇪", "🇮🇳", "🇮🇪", "🇮🇹", "🇯🇵", "🇲🇽", "🇳🇿", "🇳🇬",
      "🇰🇷", "🇿🇦", "🇪🇸", "🇸🇪", "🇺🇦", "🇬🇧", "🇺🇸", "🇺🇳", "🇵🇷", "🇸🇬",
    ],
  },
]);

const WORLD_FLAG_EMOJI_RE = /^\p{Regional_Indicator}{2}$/u;
const WORLD_KEYCAP_EMOJI_RE = /^[#*0-9]\uFE0F?\u20E3$/u;
const WORLD_PICTOGRAPH_EMOJI_RE =
  /^(?:\p{Extended_Pictographic}(?:\uFE0E|\uFE0F)?(?:\p{Emoji_Modifier})?(?:[\u{E0020}-\u{E007E}]+\u{E007F})?)(?:\u200D\p{Extended_Pictographic}(?:\uFE0E|\uFE0F)?(?:\p{Emoji_Modifier})?)*$/u;
const WORLD_STATUS_NOTE_RE =
  /^[\p{L}\p{N}][\p{L}\p{M}\p{N}'’-]*$/u;

export function normalizeWorldEmoji(value) {
  const emoji = String(value || "").trim().normalize("NFC");
  if (!emoji || [...emoji].length > 24 || emoji.length > 48) return "";
  return (
    WORLD_FLAG_EMOJI_RE.test(emoji) ||
    WORLD_KEYCAP_EMOJI_RE.test(emoji) ||
    WORLD_PICTOGRAPH_EMOJI_RE.test(emoji)
  )
    ? emoji
    : "";
}

export function normalizeWorldStatusNote(value) {
  const note = String(value || "").trim().normalize("NFKC");
  if (
    !note ||
    [...note].length > WORLD_STATUS_NOTE_MAX ||
    !WORLD_STATUS_NOTE_RE.test(note)
  ) {
    return "";
  }
  return note;
}

export function normalizeWorldStatus(emojiValue, noteValue) {
  const emoji = normalizeWorldEmoji(emojiValue);
  return {
    emoji,
    note: emoji ? normalizeWorldStatusNote(noteValue) : "",
  };
}

export const WORLD_REGIONS = [
  { id: "east", label: "East Campus", phase: "Local daylight view" },
  { id: "central", label: "Central Campus", phase: "Local daylight view" },
  { id: "west", label: "West Campus", phase: "Local daylight view" },
];

export const FOCUS_MUSIC_TRACKS = Object.freeze([
  Object.freeze({
    id: "cosmic-waves",
    name: "Cosmic Waves",
    artist: "HoliznaCC0",
    duration: "33:04",
    trackUrl: "/assets/music/cosmic-waves.ogg",
    sourceUrl:
      "https://freemusicarchive.org/music/holiznacc0/space-sleep-meditation/cosmic-waves/",
    license: "CC0 1.0",
    licenseUrl: "https://creativecommons.org/publicdomain/zero/1.0/",
  }),
  Object.freeze({
    id: "dreamscape",
    name: "DreamScape",
    artist: "HoliznaCC0",
    duration: "21:59",
    trackUrl: "/assets/music/dreamscape.ogg",
    sourceUrl:
      "https://freemusicarchive.org/music/holiznacc0/space-sleep-meditation/dreamscape/",
    license: "CC0 1.0",
    licenseUrl: "https://creativecommons.org/publicdomain/zero/1.0/",
  }),
  Object.freeze({
    id: "too-brief-a-time",
    name: "Too Brief A Time To Be Anything",
    artist: "HoliznaCC0",
    duration: "45:00",
    trackUrl: "/assets/music/too-brief-a-time.ogg",
    sourceUrl:
      "https://freemusicarchive.org/music/holiznacc0/space-sleep-meditation/too-brief-a-time-to-be-anything/",
    license: "CC0 1.0",
    licenseUrl: "https://creativecommons.org/publicdomain/zero/1.0/",
  }),
]);

export const RADIO_STATIONS = [
  {
    id: "forkmesh-song",
    name: "ForkMesh Forever (Indie Pop)",
    provider: "ForkMesh",
    description:
      "The project’s own song, hosted by ForkMesh. It plays on this device only after you press the button; nothing autoplays and no stream is relayed.",
    playMode: "hosted",
    actionLabel: "Listen to the ForkMesh song",
    trackUrl: "/assets/songs/ForkMeshForever(IndiePop).mp3",
    homepageUrl:
      "https://github.com/forkmesh/forkmesh/blob/main/docs/world-soundtrack-license.md",
  },
  {
    id: "forkmesh-focus",
    name: "ForkMesh Focus Tones",
    provider: "ForkMesh",
    description:
      "Original four-hour local procedural score under CC0-1.0; no streamed media or shared-clock synchronization.",
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

export function landmarkById(id) {
  return LANDMARKS.find((landmark) => landmark.id === id) || LANDMARKS[0];
}

export function detectClient() {
  const ua = navigator.userAgent || "";
  const platform = navigator.userAgentData?.platform || navigator.platform || "";
  let browser = "Browser";
  if (/Edg\//.test(ua)) browser = "Edge";
  else if (/Firefox\/|FxiOS\//.test(ua)) browser = "Firefox";
  else if (/CriOS\//.test(ua)) browser = "Chrome";
  else if (/Chrome\//.test(ua)) browser = "Chrome";
  else if (/Safari\//.test(ua)) browser = "Safari";

  let os = "Device";
  if (/Android/i.test(ua)) os = "Android";
  else if (/iPhone|iPad|iPod/i.test(ua)) os = "iOS";
  else if (/CrOS/i.test(ua)) os = "ChromeOS";
  else if (/Mac/i.test(platform)) os = "macOS";
  else if (/Win/i.test(platform)) os = "Windows";
  else if (/Linux/i.test(platform) || /Linux/i.test(ua)) os = "Linux";

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
