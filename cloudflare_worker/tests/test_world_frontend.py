#!/usr/bin/env python3
"""Static contracts for the playable ForkMesh World frontend."""

from pathlib import Path
import hashlib
import json
import math
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
WORLD = PUBLIC / "world"
INDEX = (WORLD / "index.html").read_text(encoding="utf-8")
APP = (WORLD / "world.js").read_text(encoding="utf-8")
DATA = (WORLD / "world-data.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
CSS = (WORLD / "world.css").read_text(encoding="utf-8")
DASHBOARD_CHAT = (PUBLIC / "dashboard-chat.js").read_text(encoding="utf-8")
DASHBOARD = (PUBLIC / "dashboard.js").read_text(encoding="utf-8")
DASHBOARD_CHAT_VIEW = (
    PUBLIC / "dashboard" / "partials" / "views" / "chat.html"
).read_text(encoding="utf-8")
QT_CHAT = (
    ROOT.parent / "qt_client" / "src" / "MainWindowChat.cpp"
).read_text(encoding="utf-8")
QT_MAIN = (ROOT.parent / "qt_client" / "src" / "main.cpp").read_text(
    encoding="utf-8"
)
QT_CONTROL = (
    ROOT.parent / "qt_client" / "src" / "MainWindowControlNode.cpp"
).read_text(encoding="utf-8")
QT_ISSUES = (
    ROOT.parent / "qt_client" / "src" / "MainWindowIssues.cpp"
).read_text(encoding="utf-8")
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WORLD_PROTOCOL = (ROOT / "src" / "world.py").read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
FEDIVERSE_REVIEW_DOC = (
    ROOT.parent / "docs" / "fediverse-mention-review.md"
).read_text(encoding="utf-8")
QT_SINGLE_INSTANCE = (
    ROOT.parent / "qt_client" / "src" / "SingleInstance.cpp"
).read_text(encoding="utf-8")
QT_DOCS = (PUBLIC / "docs" / "qt-client" / "index.html").read_text(
    encoding="utf-8"
)
LINUX_DESKTOP_INSTALLER = (
    ROOT.parent / "qt_client" / "install.sh"
).read_text(encoding="utf-8")
APPIMAGE_PACKAGER = (
    ROOT.parent / "tools" / "package" / "appimage.sh"
).read_text(encoding="utf-8")
MACOS_PACKAGER = (
    ROOT.parent / "tools" / "package" / "macos.sh"
).read_text(encoding="utf-8")
WINDOWS_PACKAGER = (
    ROOT.parent / "tools" / "package" / "forkmesh.nsi"
).read_text(encoding="utf-8")
PAYOUTS = (PUBLIC / "mirror-payouts.html").read_text(encoding="utf-8")
PRIVACY = (PUBLIC / "privacy.html").read_text(encoding="utf-8")
SECURITY_LATEST = json.loads(
    (PUBLIC / "security" / "latest.json").read_text(encoding="utf-8")
)
FEDIVERSE_DIRECTORY = json.loads(
    (WORLD / "fediverse-directory.json").read_text(encoding="utf-8")
)
BOT_DIRECTORY = json.loads(
    (WORLD / "bot-directory.json").read_text(encoding="utf-8")
)


def test_world_boots_blank_with_a_ten_second_load_error_watchdog():
    # The shell stays blank while the world module boots (adhoc #240): no
    # static fallback content to jump away from, only a hidden error panel
    # that an inline watchdog reveals if the module never arrives.
    assert '<forkmesh-world data-world-mode="public"></forkmesh-world>' in INDEX
    assert "world-static-fallback" not in INDEX
    assert "JavaScript and WebGL enhance this page" not in INDEX
    assert 'data-world-load-error hidden' in INDEX
    assert 'world.dataset.worldReady === "true"' in INDEX
    assert "}, 10000);" in INDEX
    assert "did not load within 10 seconds" in INDEX
    assert ".world-load-error[hidden]" in CSS
    assert 'document.querySelector("[data-world-load-error]")?.remove()' in APP
    assert 'href="#world-information"' in INDEX
    assert 'class="world-information-anchor"' in APP
    assert 'id="world-information"' in APP
    assert ".world-information-anchor:focus" in CSS
    assert 'src="/world/world.js"' in INDEX
    assert 'href="/world/world.css"' in INDEX


def test_world_entry_modules_have_valid_ecmascript_module_syntax():
    """Catch merge artifacts that leave a top-level export inside a function."""
    for module in WORLD.glob("*.js"):
        completed = subprocess.run(
            [
                "node",
                "--check",
                str(module),
            ],
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 0, (
            f"{module.name} is not valid ECMAScript module syntax:\n"
            f"{completed.stderr}"
        )


def test_world_contains_the_initial_city_districts_without_a_clock():
    for landmark in (
        "fountain",
        "campfire",
        "repositories",
        "office",
    ):
        assert f'id: "{landmark}"' in DATA
    assert 'id: "security"' not in DATA
    assert 'id: "workshops"' not in DATA
    assert 'id: "routing"' not in DATA
    assert 'id: "launchpad"' not in DATA
    assert "utcClock" not in DATA
    assert "WORLD_DAY_MS" not in DATA
    for theme in (
        "world",
        "rain",
        "snow",
        "winter",
        "cyberpunk",
        "low-light",
    ):
        assert f'id: "{theme}"' in DATA
    assert "world-clock" not in APP
    assert "data-world-clock" not in APP
    assert "data-world-phase" not in APP
    assert "world-clock" not in CSS


def test_forkmesh_office_is_a_navigable_world_landmark():
    assert 'id: "office"' in DATA
    assert 'label: "ForkMesh Office"' in DATA
    assert 'shortLabel: "Office"' in DATA
    assert "position: [0, 0, -215]" in DATA
    assert 'id: "visiting-office"' in DATA


def test_landmarks_use_stable_positions_inside_the_world_and_office_campus():
    positions = {
        "fountain": (0, 0),
        "campfire": (0, 130),
        "repositories": (130, 0),
        "office": (0, -215),
    }
    for landmark, (x, z) in positions.items():
        start = DATA.index(f'id: "{landmark}"')
        block = DATA[start: DATA.index("\n  },", start)]
        assert f"position: [{x}, 0, {z}]" in block

    town_landmarks = [positions["campfire"], positions["repositories"]]
    assert min(math.dist(left, right) for left in town_landmarks for right in town_landmarks if left != right) >= 18
    assert math.dist(positions["office"], positions["fountain"]) >= 180
    assert "const REPOSITORY_EDGE_RADIUS = 68" in SCENE
    assert "const SERVER_CABINET_YARD_ORIGIN = Object.freeze([18, 0, 0])" in SCENE
    assert "const SYSTEM_CAPACITY_INFRASTRUCTURE_POSITION = Object.freeze([-24, 0, 7])" in SCENE


def test_static_world_fallback_links_to_chat():
    assert 'href="/chat"' in INDEX
    assert "Open ForkMesh chat" in INDEX


def test_scene_builds_playable_landmarks_and_badged_avatars():
    for builder in (
        "createAvatar",
        "createFountain",
        "createRepositoryDistrict",
        "createOrganizationQuarter",
        "createFediverseCenter",
        "createSecurityWorkshop",
        "createCommunityStage",
        "createNeighborhood",
        "createBroadcastGarden",
        "createForkMeshOffice",
    ):
        assert f"function {builder}" in SCENE
    assert "badgeTexture" in SCENE
    assert "identity.flag" in SCENE
    assert "identity.browser" in SCENE
    assert "identity.os" in SCENE
    assert "identity.name" in SCENE
    assert "world-shirt-account" not in APP
    for status_icon in (
        "Guest: \"○\"",
        "Registered: \"✓\"",
        '"Supporting member": "♥"',
        '"Mirror operator": "◈"',
        '"Organization admin": "◆"',
        '"Verified bot": "⌘"',
    ):
        assert status_icon in APP
        assert status_icon in SCENE
    for source in (APP, SCENE):
        assert "function accountStatusIcon(identity)" in source
        assert (
            'status === "Registered" && identity?.emailVerified !== true'
            in source
        )
        assert 'return "×";' in source
    assert "Everyone, including guests, can cross the bridge and enter the lobby" in DATA
    assert "function updateOrganizations" in SCENE
    assert "organization-profiles" in SCENE
    assert "organization.memberList" in SCENE
    assert "data-world-office-member" in APP
    assert "data-world-active-office" in APP
    assert "function makeConsentedProfileFace" in SCENE
    assert "publicProfileHandle" in SCENE
    assert "https://x.com/forkmesh" in APP
    assert "https://www.reddit.com/user/forkmesh" in APP
    assert "function visitNeighborhoodHome" in SCENE
    assert "data-world-home-grant" in APP
    assert "data-world-home-decline" in APP
    assert "data-world-enter-home" in APP
    assert "pendingKnocks" in APP
    assert 'arrivalBox.name = "world-arrival-box"' in SCENE
    assert '"10 visitors per row · face the square"' in SCENE
    assert "function setSpawn(spawn = {})" in SCENE
    assert '"self": world_protocol.public_presence(state)' in ENTRY


def test_user_agent_is_reduced_locally_to_generalized_badge_categories():
    assert 'Firefox\\/|FxiOS\\/' in DATA
    assert "/CrOS/i.test(ua)" in DATA
    assert "/Linux/i.test(platform)" in DATA
    assert "navigator.userAgent" in DATA
    assert "presenceBrowser(" in APP
    assert "presenceOS(" in APP
    context = APP[
        APP.index("  async loadContext() {"):
        APP.index("\n  async loadSatelliteSky()", APP.index("  async loadContext() {"))
    ]
    # The shared context is a guest-visible payload: no connection card rides
    # back on it, so nothing here reads an address or a raw user agent.
    assert "securityDetails" not in context
    presence = APP[
        APP.index("  sendPresence(message) {"):
        APP.index("\n  receivePresence(message)", APP.index("  sendPresence(message) {"))
    ]
    assert "securityDetails" not in presence
    assert "selfWorkBoard" not in presence


def test_avatar_chest_activity_country_shirt_and_input_state_are_privacy_safe():
    for contract in (
        "FIRST SEEN THIS SESSION",
        "PUBLIC URL VISITS",
        "identity.activityCategory",
        "identity.firstVisitAge",
        "identity.visitCount",
        "function countryShirtTexture",
        "identity.countryCode",
        'antenna.name = "mouse-activity-antenna"',
        "function syncAvatarActivity",
        "function animateAvatarActivity",
        "Guests and members remain fully",
        "opaque until they actually leave the World",
    ):
        assert contract in SCENE
    assert ".world-saved-views {" in CSS
    assert ".world-saved-view img," in CSS
    saved_view_render = APP[
        APP.index("  renderSavedViews() {"):
        APP.index("\n  captureSavedViewThumbnail()", APP.index("  renderSavedViews() {"))
    ]
    assert 'aria-label="Return to ${escapeHTML(view.label)}"' in saved_view_render
    assert "<span>${escapeHTML(view.label)}</span>" not in saved_view_render
    assert "inputActive:" in APP
    assert "visitCount:" in APP
    assert "firstVisitAge:" in APP
    assert "settings.privacy.activity && identity.inputActive === true" in APP
    assert "countryCode:" in APP
    presence = APP[APP.index("  sendPresence(message) {"):APP.index(
        "\n  receivePresence(message)", APP.index("  sendPresence(message) {")
    )]
    assert "userAgent" not in presence
    assert "securityDetails" not in presence
    assert "url:" not in presence


def test_chest_badge_shows_client_categories_exact_ages_and_status_note():
    for contract in (
        "function firstSeenAgoLabel(minutes)",
        "function badgeClientLabel(identity)",
        "function badgeStatusLabel(identity)",
        '!placeholders.has(value.toLowerCase())',
        "firstSeenAgoLabel(identity.firstSeenMinutes)",
        "return `JOINED ${count}${unit} AGO`",
    ):
        assert contract in SCENE
    # Public chest badges still show only coarse categories. The owner-only
    # back plate carries the assigned-work board and never enters an identity.
    assert "navigator.userAgent" not in SCENE
    assert "function avatarWorkBadgeTexture" in SCENE
    assert 'badge.name = "forkmesh-self-work-back-badge"' in SCENE
    assert "setSelfWorkBadgeVisibility" in SCENE
    assert "HIDDEN FROM PEERS + SCREENSHOTS" in SCENE
    # The retired session card must not come back on the avatar's back.
    assert "YOUR SESSION" not in SCENE
    assert "EDGE IP" not in SCENE
    for contract in (
        "firstSeenMinutes:",
        "joinedAt:",
        "function firstSeenMinutes(timestamp, now = Date.now())",
        "function boundedJoinedAt(value, now = Date.now())",
    ):
        assert contract in APP
    assert '"firstSeenMinutes", "joinedAt",' in WORLD_PROTOCOL
    assert "def _bounded_first_seen_minutes(value, fallback):" in WORLD_PROTOCOL
    assert "def _bounded_joined_at(value, now, fallback):" in WORLD_PROTOCOL


def test_chest_badge_shows_the_same_full_record_for_every_avatar():
    # The world active-time row is an extra line, never a replacement for the
    # activity·visits line, so no avatar's chest shows less than the badge
    # knows about that visitor.
    assert "const activeRow =" in SCENE
    assert "IN WORLD`" in SCENE
    assert "`${activity} · ${visits} PUBLIC URL VISITS`" in SCENE
    assert "sharesActivity || !activeRow" in SCENE
    # A live presence frame carries no joined date or active-time aggregate, so
    # every avatar for a known account is filled in from the public directory:
    # walking peers, office-meeting participants, and the visitor themselves.
    for contract in (
        "function withMemberFacts(identity)",
        "const badgeIdentity = withMemberFacts({",
        "const participantIdentity = withMemberFacts({",
        "const badgeIdentity = withMemberFacts(identity);",
        "memberFacts.clear();",
    ):
        assert contract in SCENE
    # A guest can type any display name, so only a server-stamped account
    # status may claim the record filed under it.
    assert 'String(identity.accountStatus || "Guest") === "Guest"' in SCENE
    # The visitor's own chest wears their live activity-ticket total, repainted
    # once a minute rather than on every one-second tick.
    assert "syncOwnBadgeActivity()" in APP
    assert "this.syncOwnBadgeActivity();" in APP
    assert "worldActivityRenderedMinute" in APP
    assert "totalActiveMs: Number.isFinite(Number(identity.totalActiveMs))" in APP


def test_unified_chest_card_uses_public_profile_wallet_and_explicit_follow():
    for contract in (
        "function badgeTexture(",
        '"RECENT FEDIVERSE"',
        "drawQrModules(context, walletAddress",
        'identity.walletEditable ? "ADD WALLET" : "NO WALLET"',
        "function badgeWalletSquareHit(uv)",
        "function badgeFollowPillHit(uv)",
        "function setAvatarFediverseProfile(peerId, profile)",
        "onFediverseProfile = () => {},",
        "onFediverseFollow = () => {},",
        "onAvatarWalletAction = () => {},",
        "setAvatarFediverseProfile,",
        "queueMicrotask(() => onFediverseProfile(target))",
        '"FEDIVERSE · UNAVAILABLE"',
        'activityRing.name = "avatar-activity-ring"',
        "new THREE.PlaneGeometry(0.88, 0.88)",
    ):
        assert contract in SCENE
    assert '"account-activity-light"' not in SCENE
    assert 'window.location.assign("/dashboard/settings/payout")' in APP
    assert "const previousName = String(identity.name" in SCENE
    assert "nextName !== previousName || nextStatus !== previousStatus" in SCENE
    assert "player.userData.fediverseProfile = loadingProfile;" in SCENE
    assert "onFediverseProfile({" in SCENE
    # The unified card is loaded when an avatar is registered; there are no
    # runtime mode buttons to hide identity fields from seated members.
    register = SCENE[
        SCENE.index("  function registerAvatarChestControls"):
        SCENE.index("  function unregisterAvatarChestControls")
    ]
    assert "queueMicrotask(() => onFediverseProfile(target))" in register
    assert "avatar.userData.badge" in register
    avatar = SCENE[
        SCENE.index("function createAvatar("):
        SCENE.index("function updateAvatarBadge")
    ]
    assert "createAvatarChestTabs(" not in avatar
    assert "chestTabs: null" in avatar
    assert "async loadWorldFediverseProfile(target = {}) {" in APP
    assert "async toggleWorldFediverseFollow(target = {}) {" in APP
    assert "`/api/accounts/${encodeURIComponent(account)}${query}`" in APP
    assert (
        '`/api/accounts/${encodeURIComponent(account)}/follow`' in APP
    )


def test_self_profile_has_follower_avatars_and_activitypub_selfie_composer():
    for contract in (
        "data-world-profile-followers",
        "data-world-profile-publisher",
        "data-world-profile-selfie",
        "data-world-profile-alt-text",
        '"/api/world/profile-social"',
        "wireWorldProfileSocial(detail, backdrop)",
        "captureWorldSelfie(detail, backdrop)",
        "compactWorldSelfie(source)",
        '"image/webp"',
        "A selfie from ForkMesh World while",
        "Published to ActivityPub.",
    ):
        assert contract in APP
    assert "requestIdleCallback" in APP
    assert "blob.size < 63 * 1024" in APP
    assert ".world-profile-follower-strip" in CSS
    assert ".world-profile-selfie-preview" in CSS
    # The private profile drawer is omitted from the selfie while the rest of
    # the current World view and public HUD are captured.
    selfie = APP[
        APP.index("  async captureWorldSelfie"):
        APP.index("  async compactWorldSelfie")
    ]
    assert 'detail.style.visibility = "hidden"' in selfie
    assert 'backdrop.style.visibility = "hidden"' in selfie
    assert 'method: following ? "DELETE" : "POST"' in APP
    assert "function worldFediverseFeedLines(recentActivity)" in APP
    for timer in ("setInterval", "setTimeout"):
        assert (
            timer
            not in APP[
                APP.index("  async loadWorldFediverseProfile(target = {}) {"):
                APP.index("  applyWorldLayoutEditor() {")
            ]
        )


def test_avatars_do_not_render_laptop_or_phone_props():
    assert "phone/laptop affordance" not in SCENE
    assert 'startsWith("guest")' not in SCENE
    assert "const device = new THREE.Group()" not in SCENE
    assert "const keyboard = new THREE.Mesh(" not in SCENE


def test_admin_remote_avatar_back_controls_use_only_opaque_manual_handles():
    for contract in (
        "const WORLD_MODERATION_HANDLE_PATTERN = /^[a-f0-9]{64}$/",
        "function sanitizedModerationHandles(remote, isAdmin)",
        "isAdmin !== true",
        'remote?.persistedInactive === true',
        'remote?.accountStatus === "Verified bot"',
        "/^(?:inactive|local|node|bot):/",
        'typeof remote.moderationHandles !== "object"',
        "function createAvatarModerationControls",
        '"TEMP BLOCK IP"',
        '"ROTATING IP TOKEN · 1 HOUR"',
        '"TEMP BLOCK AGENT"',
        '"BROWSER AGENT · 1 HOUR"',
        "control.position.set(0, spec.y, 0.321)",
        "onModeration = () => {}",
        "identity?.isAdmin === true",
        "moderationActions.set(control",
        "interactive.push(control)",
        "function removeRemoteModerationControls",
        "moderationActions.delete(child)",
        "interactive.splice(interactiveIndex, 1)",
        "disposeObject3D(controls)",
        "syncRemoteModerationControls(avatar, remote)",
        "removeRemoteModerationControls(avatar, id)",
    ):
        assert contract in SCENE

    click = SCENE[
        SCENE.index("const moderationAction = hit?.object"):
        SCENE.index(
            "if (hit?.object?.userData?.landmark)",
            SCENE.index("const moderationAction = hit?.object"),
        )
    ]
    for field in ("targetType", "handle", "peerId", "name"):
        assert f"{field}: moderationAction.{field}" in click
    assert "onModeration({" in click

    texture = SCENE[
        SCENE.index("function moderationControlTexture"):
        SCENE.index(
            "\nfunction sanitizedModerationHandles",
            SCENE.index("function moderationControlTexture"),
        )
    ]
    assert "handle" not in texture.lower()


def test_presence_client_uses_only_coarse_ephemeral_world_protocol():
    assert 'this.fetchJSON("/api/world/context"' in APP
    assert 'this.fetchJSON("/api/world/ticket"' in APP
    assert 'this.fetchJSON("/api/world/inactive"' in APP
    assert 'this.fetchJSON("/api/network/overview"' in APP
    assert 'this.fetchJSON("/api/repositories", { auth: hasSession })' in APP
    # The campfire circle (adhoc #228) reads the same public roster the chat
    # page uses — profile names only, fetched without credentials. Anything
    # beyond that anonymous directory stays off-limits to the world client.
    assert (
        'this.fetchJSON("/api/accounts/users", {\n          auth: false,'
        in APP
    )
    # Boot fetch plus the throttled refresh that seats accounts created after
    # the tab opened; both anonymous, and nothing else touches the endpoint.
    assert APP.count("/api/accounts/users") == 2
    assert (
        'this.fetchJSON("/api/accounts/users", {\n        auth: false,' in APP
    )
    assert "async refreshMemberDirectory(force = false, probed = [])" in APP
    assert 'type: "presence"' in APP
    assert "shareCountry: Boolean(this.settings.privacy.country)" in APP
    assert "shareName: Boolean(this.settings.privacy.name)" in APP
    assert "shareNodes: Boolean(this.settings.privacy.nodes)" in APP
    assert "status: presenceStatus(this.settings)" in APP
    assert "localTime: presenceLocalTime(this.settings.privacy.localTime)" in APP
    assert 'accountStatus: remote.accountStatus || "Guest"' in SCENE
    assert "remote.localTime ||" in SCENE
    assert 'return "hidden"' in APP
    assert 'safe = { type: "ping" }' in APP
    assert "serverPeerId" in APP
    assert 'this.socket?.close(1000, "page hidden")' in APP
    assert 'socketURL.searchParams.set("ticket", this.worldTicket)' in APP
    assert "socket = new WebSocket(socketURL.href)" in APP
    assert '`${protocol}//${location.host}/api/world/ws`' in APP
    assert "WORLD_TICKET_REFRESH_MS = 5 * 60 * 1000" in APP
    assert "startWorldTicketRefresh" in APP
    assert (
        "        !document.hidden &&\n"
        "        (readSession()?.sessionToken || this.sessionAuthenticated)"
        in APP
    )
    assert "window.clearInterval(this.worldTicketTimer)" in APP
    assert "session?.organizationAdmin" not in APP
    assert "session?.accountTier" not in APP


def test_presence_pongs_do_not_trigger_full_avatar_or_capacity_rebuilds():
    receiver = APP[
        APP.index("  receivePresence(message) {"):
        APP.index("\n  setupBroadcastChannel()", APP.index("  receivePresence(message) {"))
    ]
    assert "let peersChanged = false" in receiver
    assert "if (peersChanged) this.renderPeers()" in receiver
    assert 'message.type === "move"' in receiver
    assert 'message.type === "ping"' not in receiver


def test_world_hud_omits_the_redundant_repository_node_and_player_counts():
    assert "world-metrics" not in APP
    assert " data-world-repos>" not in APP
    assert " data-world-nodes>" not in APP
    assert " data-world-players>" not in APP
    render_peers = APP[APP.index("  renderPeers() {"):APP.index(
        "\n  destroy() {", APP.index("  renderPeers() {")
    )]
    assert "if (!socketOnline && !reconnectGrace)" in render_peers
    assert "this.world?.setRemotePlayers" in render_peers


def test_world_position_is_one_fresh_bounded_identity_local_record():
    for contract in (
        'const POSITION_KEY_PREFIX = "forkmesh.world.position.v1."',
        "const POSITION_MAX_AGE_MS = 30 * 24 * 60 * 60 * 1000",
        "function positionStorageKey(identityId)",
        "function normalizedWorldPosition(record, now = Date.now())",
        "Math.abs(x) > POSITION_RADIUS",
        "Math.abs(z) > POSITION_RADIUS",
        "Math.abs(y - floor) > POSITION_FLOOR_TOLERANCE",
        "updatedAt < now - POSITION_MAX_AGE_MS",
        "this.positionKey = positionStorageKey(this.identity.id)",
        "this.world.setSpawn?.(this.restoredPosition)",
        "this.captureWorldPosition(true)",
    ):
        assert contract in APP
    writer = APP[APP.index("  flushWorldPosition() {"):APP.index(
        "\n  queueMovementPresence(", APP.index("  flushWorldPosition() {")
    )]
    for field in ("x", "y", "z", "heading", "space", "updatedAt"):
        assert f"      {field}," in writer
    for forbidden in ("activity", "url", "history", "repository"):
        assert forbidden not in writer.lower()
    assert "Object.hasOwn(WORLD_SPACE_FLOORS, requestedSpace)" in SCENE
    assert "currentFloorY = WORLD_SPACE_FLOORS[space]" in SCENE


def test_world_welcome_relocates_a_restored_spawn_blocked_by_a_visitor():
    for contract in (
        "const ARRIVAL_CLEARANCE = 0.9",
        "const spawnBlocked =",
        "this.initialPresenceWelcomePending &&",
        'ownSpace === "town-square"',
        "player.space === ownSpace",
        ") < ARRIVAL_CLEARANCE",
        "(!this.spawnSelected || spawnBlocked)",
        "this.initialPresenceWelcomePending = false;",
    ):
        assert contract in APP
    assert "arrival_slot_near_position" in ENTRY


def test_world_client_coalesces_disposable_frames_and_reconnects_with_grace():
    for contract in (
        "const MOVEMENT_SEND_INTERVAL_MS = 1000",
        "const PRESENCE_PROFILE_DEBOUNCE_MS = 300",
        "const SOCKET_BUFFER_HIGH_WATER_BYTES = 64 * 1024",
        "this.queueMovementPresence({",
        "this.sendPresenceNow({",
        "Number(this.socket.bufferedAmount || 0) > SOCKET_BUFFER_HIGH_WATER_BYTES",
        "schedulePresenceReconnect()",
        "const jitter = 0.75 + Math.random() * 0.5",
        "this.schedulePresenceReconnect();",
        "const reconnectGrace =",
        "this.startPeerReconnectGrace();",
        "this.socketRetry = 1000",
    ):
        assert contract in APP
    assert "this.remotePlayers.clear();" not in APP[
        APP.index('socket.addEventListener("close"'):APP.index(
            'socket.addEventListener("error"',
            APP.index('socket.addEventListener("close"'),
        )
    ]
    assert "if (!walking && wasWalking)" in SCENE
    assert "moving: true" in SCENE
    assert "moving: false" in SCENE


def test_avatar_faces_keyboard_travel_direction_without_an_entry_gate():
    assert "player.rotation.y = Math.atan2(-movement.x, -movement.z)" in SCENE
    # A single click still only selects — walk-to-click stays gone. Travel by
    # pointer is opt-in through the double-click dash (see the dash test below),
    # so no target may be set from the single-tap path.
    assert "moveTarget" not in SCENE
    single_tap = SCENE[
        SCENE.index("  function finishPointer"):
        SCENE.index("  function handlePointerUp")
    ]
    assert "dashTarget" not in single_tap
    assert "function arrivalFacingHeading(x, z, heading)" in SCENE
    assert "isArrivalGridPosition(x, z) ? Math.PI : heading" in SCENE
    assert "player.rotation.y = arrivalFacingHeading(x, z, heading)" in SCENE
    assert "avatar.userData.targetHeading = arrivalFacingHeading(" in SCENE
    assert "INTRO_DISMISSED_KEY" not in APP
    assert "data-world-arrival-dismiss" not in APP
    assert "world-arrival-card" not in APP
    assert "Entering ForkMesh World" not in APP
    assert "data-world-loading" in APP
    assert 'loading.dataset.ready = "true"' in APP
    assert "initialWorldLayout: mergedInitialLayout" in APP
    assert ".world-loading-screen" not in CSS


def test_world_has_consent_aware_activity_events_and_media():
    for retired_landmark in ("events", "neighborhood", "broadcast"):
        assert f'id: "{retired_landmark}"' not in DATA
    assert "broadcast: createBroadcastGarden" not in SCENE
    assert "AVAILABILITY_OPTIONS" in DATA
    assert "ACTIVITY_OPTIONS" in DATA
    for category in (
        "viewing-repository",
        "reading-documentation",
        "exploring-town-square",
        "visiting-organization",
        "browsing-code-visualization",
    ):
        assert category in DATA
    assert "WORLD_REGIONS" in DATA
    assert "travelToRegion" in SCENE
    assert "travelToSpace" not in SCENE
    assert "utcClock" not in APP
    assert "worldClock" not in SCENE
    assert "setClockOffset" not in SCENE
    assert "utcOffsetHours" not in SCENE
    for retired in (
        "createWorkshopBarn",
        "WORKSHOP_BARN",
        "createSkyOffice",
        "createOtherWorlds",
        "createLaunchpad",
        "createRoutingStation",
        "QUIET SEATING",
        "sky-campus",
        "space-station",
        "code-planet",
        "organization-region",
        "planet-atlas",
        "createCodeWorkshops",
        "CODE WORKSHOPS",
    ):
        assert retired not in SCENE
        assert retired not in APP
    assert 'remote.activity === "idle"' in SCENE
    assert 'landmarkById("neighborhood").position' in SCENE
    assert "data-world-public-door" in APP
    assert "sendWorldInteraction" in APP
    assert "data-world-emote" not in APP
    assert "world-emote-bar" not in APP
    assert "function playEmote" in SCENE
    assert "function playRewardEvent" in SCENE
    assert "syncOperatorBelt" in SCENE
    assert "this.inactivePlayers" in APP
    assert "SPACE_CHANNELS" in DASHBOARD_CHAT
    assert "entry.channel === CHANNEL" in DASHBOARD_CHAT
    assert "startStatusBoardPolling" in APP
    assert "confirmed community reward event" in APP
    assert "60000" in APP
    assert "function updateNeighborhoodHomes" in SCENE
    assert "Empty houses reveal nothing about offline users" in APP
    assert 'id: "workshops"' not in DATA
    assert 'workshops: () => this.workshopPanelHTML()' not in APP
    assert "data-world-radio-stop" in APP
    assert "Audio never starts automatically" in APP
    assert "ice5.somafm.com" not in DATA
    assert "data-world-sound-toggle" in APP
    assert "this.soundEnabled = false" in APP
    assert "This is the only path that creates the shared cue context" in APP
    assert "if (!this.soundEnabled)" in APP
    assert "Audio never starts automatically" in APP


def test_broadcast_garden_offers_the_first_party_forkmesh_song_on_demand():
    assert (PUBLIC / "assets" / "songs" / "ForkMeshForever(IndiePop).mp3").exists()
    assert "Listen to the ForkMesh song" in DATA
    assert '"/assets/songs/ForkMeshForever(IndiePop).mp3"' in DATA
    assert 'playMode: "hosted"' in DATA
    assert "station.actionLabel" in APP
    assert 'station.playMode === "hosted"' in APP
    assert "async playHostedTrack(station, now)" in APP
    hosted = APP[APP.index("  async playHostedTrack("):APP.index(
        "\n  stopRadio(", APP.index("  async playHostedTrack(")
    )]
    assert "element.loop = false" in hosted
    assert "await element.play()" in hosted
    assert "data-world-radio-stop" in hosted


def test_focus_music_catalog_manifest_and_bundles_are_complete_long_form_and_platform_safe(
):
    music_dir = PUBLIC / "assets" / "music"
    manifest = json.loads(
        (music_dir / "music-manifest.json").read_text(encoding="utf-8")
    )
    module_uri = (WORLD / "world-data.js").resolve().as_uri()
    completed = subprocess.run(
        [
            "node",
            "--input-type=module",
            "-e",
            (
                f'import {{ FOCUS_MUSIC_TRACKS }} from {json.dumps(module_uri)};'
                "process.stdout.write(JSON.stringify(FOCUS_MUSIC_TRACKS));"
            ),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    tracks = json.loads(completed.stdout)

    assert len(tracks) == 3
    assert tracks[0]["id"] == "cosmic-waves"
    assert tracks[0]["name"] == "Cosmic Waves"
    assert "const DEFAULT_FOCUS_MUSIC_TRACK_ID = FOCUS_MUSIC_TRACKS[0].id;" in APP
    assert len({track["id"] for track in tracks}) == 3
    assert len({track["trackUrl"] for track in tracks}) == 3

    assert manifest["schemaVersion"] == 2
    assert manifest["license"] == {
        "id": "CC0-1.0",
        "url": "https://creativecommons.org/publicdomain/zero/1.0/",
    }
    manifest_tracks = manifest["tracks"]
    assert [track["id"] for track in manifest_tracks] == [
        track["id"] for track in tracks
    ]

    total_bytes = 0
    for track, record in zip(tracks, manifest_tracks, strict=True):
        bundled = music_dir / record["bundledFile"]
        payload = bundled.read_bytes()
        size = len(payload)
        total_bytes += size

        assert track["trackUrl"] == f"/assets/music/{record['bundledFile']}"
        assert track["name"] == record["title"]
        assert track["artist"] == record["creator"]
        assert track["sourceUrl"] == record["sourcePage"]
        assert track["license"] == "CC0 1.0"
        assert track["licenseUrl"] == manifest["license"]["url"]
        assert record["sourcePage"].startswith(
            "https://freemusicarchive.org/music/holiznacc0/"
        )
        assert record["sourceFile"].startswith(
            "https://files.freemusicarchive.org/"
        )
        assert len(record["sourceSha256"]) == 64
        assert set(record["sourceSha256"]) <= set("0123456789abcdef")
        assert record["sourceDurationSeconds"] >= 20 * 60
        assert record["sourceBytes"] > size
        assert abs(
            record["sourceDurationSeconds"] - record["durationSeconds"]
        ) < 0.1
        assert record["durationSeconds"] >= 20 * 60
        assert record["bundledCodec"] == "Ogg Vorbis"
        assert record["bundledSampleRateHz"] == 44100
        assert record["bundledChannels"] == 2
        assert record["modification"]
        assert size == record["bundledBytes"]
        assert size <= 25 * 1024 * 1024
        assert hashlib.sha256(payload).hexdigest() == record["bundledSha256"]

    assert total_bytes == sum(
        record["bundledBytes"] for record in manifest_tracks
    )
    assert total_bytes <= 50 * 1024 * 1024
    assert max(record["durationSeconds"] for record in manifest_tracks) >= 45 * 60


def test_focus_music_is_local_long_form_playback_without_polling():
    for selector in (
        "data-world-focus-track",
        "data-world-focus-play",
        "data-world-focus-pause",
        "data-world-focus-stop",
        "data-world-focus-mute",
        "data-world-focus-volume",
        "data-world-focus-now",
    ):
        assert selector in APP
    assert "Three full-length ambient instrumentals ship with ForkMesh." in APP
    assert "Each plays for 22–45 minutes before repeating" in APP
    assert "focusMusicTrackId: DEFAULT_FOCUS_MUSIC_TRACK_ID" in APP
    assert "focusMusicVolume: DEFAULT_FOCUS_MUSIC_VOLUME" in APP
    assert "focusMusicMuted: false" in APP
    assert "writeJSON(localStorage, SETTINGS_KEY, {" in APP

    start = APP.index("  async playFocusMusic(")
    end = APP.index("\n  stopFocusMusic(", start)
    playback = APP[start:end]
    assert "new AudioElement(track.trackUrl)" in playback
    assert "element.loop = true" in playback
    assert "await element.play()" in playback
    assert "setInterval(" not in playback
    assert "setTimeout(" not in playback
    assert "fetch(" not in playback
    assert "WebSocket" not in playback
    assert "BroadcastChannel" not in playback

    connected = APP[
        APP.index("  connectedCallback()"):APP.index(
            "\n  disconnectedCallback()", APP.index("  connectedCallback()")
        )
    ]
    assert "playFocusMusic(" not in connected
    bootstrap = APP[
        APP.index("  async bootstrap()"):APP.index(
            "\n  handleVisibility", APP.index("  async bootstrap()")
        )
    ]
    assert "void this.playFocusMusic({ autoplay: true })" in bootstrap


def test_join_cues_are_country_specific_local_opt_in_and_rate_limited():
    assert "playCountryJoinSound(countryCode, force = false)" in APP
    assert 'message.type === "join"' in APP
    assert "this.playCountryJoinSound(player.countryCode)" in APP
    assert "code.charCodeAt(0) * 37 + code.charCodeAt(1) * 17" in APP
    assert "this.joinSoundTimes.length >= 3" in APP
    cue = APP[APP.index("  playCountryJoinSound("):APP.index(
        "\n  saveSettings()", APP.index("  playCountryJoinSound(")
    )]
    assert "if (!this.soundEnabled || !context" in cue
    assert "countryCode" in cue
    assert "speechSynthesis" not in cue


def test_mobile_world_stays_stable_while_walking_and_keeps_the_quick_map():
    assert "if (this.mobileMovementActive) return;" in APP
    assert "this.mobileMovementActive = true;" in APP
    assert "this.mobileMovementActive = false;" in APP
    assert "this.coarsePointerViewport" in APP
    assert "this.lastStableViewportWidth" in APP
    assert "Math.abs(width - this.lastStableViewportWidth) < 2" in APP
    assert "address-bar expansion and contraction" in APP
    assert "overscroll-behavior: none;" in CSS
    assert "position: fixed;" in CSS[
        CSS.index("body.world-active {"):
        CSS.index("}", CSS.index("body.world-active {"))
    ]
    assert 'this.addEventListener("touchmove", this.blockWorldPullToRefresh' in APP
    assert "capture: true" in APP
    pull_guard = APP[
        APP.index("  blockWorldPullToRefresh ="):
        APP.index("\n  syncViewportHeight =", APP.index("  blockWorldPullToRefresh ="))
    ]
    assert "event.preventDefault();" in pull_guard
    assert "[data-world-thumbstick]" in pull_guard
    assert "[data-world-canvas-wrap]" in pull_guard
    assert "pendingTouchResize = true;" in SCENE
    assert "externalTouchInteractionActive" in SCENE
    assert "setTouchInteractionActive" in SCENE
    assert "touchPointers.size > 0 || externalTouchInteractionActive" in SCENE
    assert "this.world?.setTouchInteractionActive?.(true);" in APP
    assert "this.world?.setTouchInteractionActive?.(false);" in APP
    mobile = CSS[CSS.index("@media (max-width: 720px)"):]
    assert ".world-right-rail {" in mobile
    assert "display: grid;" in mobile
    assert ".world-map {" not in CSS[
        CSS.index("@media (max-width: 980px)"):
        CSS.index("@media (max-width: 720px)")
    ]


def test_saved_world_views_keep_a_thumbnail_label_position_and_camera():
    for contract in (
        'const SAVED_VIEWS_KEY_PREFIX = "forkmesh.world.savedViews.v1."',
        "const SAVED_VIEWS_MAX = 5;",
        "function normalizedSavedWorldView(record)",
        "data-world-save-view",
        "data-world-saved-view-list",
        "captureSavedViewThumbnail()",
        'thumbnail.toDataURL("image/webp", 0.62)',
        "saveCurrentWorldView()",
        "editSavedWorldView(id)",
        "restoreSavedWorldView(id)",
        "this.officeController?.restoreSavedView?.(view)",
    ):
        assert contract in APP
    for contract in (
        "function getSavedViewState()",
        "function restoreSavedViewState(view = {})",
        "floorId: officeCurrentFloorId",
        "camera: cameraState",
        "getSavedViewState,",
        "restoreSavedViewState,",
    ):
        assert contract in SCENE


def test_world_navigation_uses_fixed_spatial_shortcuts_and_five_saved_views():
    template = APP.split("function worldTemplate(", 1)[1].split(
        "\nfunction ", 1
    )[0]
    assert 'new Set(["office", "campfire"])' in template
    assert 'data-expanded="true"' in template
    assert "Remember" in template
    assert "Quick views" not in template
    assert "Reward pool" not in template
    assert '<div class="world-saved-view-list" data-world-saved-view-list>' in template
    assert '.slice(0, SAVED_VIEWS_MAX)' in APP.split("renderSavedViews()", 1)[1]


def test_clicking_the_physical_fire_frames_the_people_around_it():
    hit = SCENE.split("if (hit?.object?.userData?.campfirePit)", 1)[1].split(
        "\n    if (", 1
    )[0]
    assert "focusCampfireCircle();" in hit
    focus = SCENE.split("function focusCampfireCircle()", 1)[1].split(
        "\n  }\n", 1
    )[0]
    assert 'setCameraMode("third-person", "campfire-focus")' in focus
    assert "cameraFocus = campfire.position.clone();" in focus
    assert "setCameraZoom(Math.min(cameraZoom, 0.68));" in focus


def test_retired_vm1_forkmesh_stub_is_not_rendered_as_a_portal():
    catalog = SCENE.split("function updateRepositoryCatalog(", 1)[1].split(
        "const activeKey", 1
    )[0]
    assert 'String(record.owner || "").toLowerCase() === "vm1"' in catalog
    assert 'String(record.name || "").toLowerCase() === "forkmesh"' in catalog
    assert "record.liveHost !== true" in catalog


def test_toolbar_sound_button_is_the_master_switch_for_all_local_audio():
    toggle = APP[
        APP.index("  async toggleWorldSound() {"):
        APP.index("\n  syncWorldSoundButton()", APP.index("  async toggleWorldSound() {"))
    ]
    assert "this.stopRadio();" in toggle
    assert "this.focusMusicAutoplayPending = false;" in toggle
    assert toggle.count("this.syncWorldSoundButton();") >= 3
    assert "if (!this.soundEnabled)" in APP[
        APP.index("  async playFocusMusic("):
        APP.index("\n  async toggleFocusMusicPause()", APP.index("  async playFocusMusic("))
    ]
    assert "if (!this.soundEnabled)" in APP[
        APP.index("  async playHostedTrack("):
        APP.index("\n  stopRadio(", APP.index("  async playHostedTrack("))
    ]


def test_chat_opens_through_the_spatial_forkmesh_office_and_terminal():
    assert "data-world-office-enter" not in APP
    assert "data-world-office-prompt" not in APP
    assert "data-world-office-fallback" in APP
    assert "data-world-office-chat" in APP
    assert "data-world-office-frame" in APP
    assert "Open accessible chat fallback" in APP
    assert "ForkMesh Office chat" in APP
    assert 'office: "visiting-office"' in APP
    assert "/chat?embed=office" not in APP
    assert "allow-popups" not in APP
    # Two doors to the same encrypted chat: the spatial Office walk-in (above)
    # and the docked chat terminal panel. The Office deliberately does not
    # replace the terminal, so both entrances are asserted here.
    assert "openWorldChat(" in APP
    assert "closeWorldChat()" in APP
    assert "data-world-chat-terminal" in APP
    assert "data-world-native-chat" in APP
    assert "data-world-chat-terminal-frame" not in APP
    assert 'data-world-default-repository="forkmesh/forkmesh"' in APP
    assert 'id="fullChatAction"' in APP
    assert 'id="fullChatAction" type="hidden" value="chat"' in APP
    assert "Send to bot</option>" not in APP
    assert '<span data-dashboard-chat-send-label>Chat</span>' in APP
    assert 'title="Enter will send this task to the bot"' in APP
    assert "world-chat-terminal-channel" in APP
    assert "world-chat-terminal-connection" in APP
    chat_version = hashlib.sha256(DASHBOARD_CHAT.encode()).hexdigest()[:12]
    assert f'script.src = "/dashboard-chat.js?v={chat_version}"' in APP
    assert "world-chat-terminal" in CSS
    assert "DEBUG owns the lower-left; chat owns the lower-right" in CSS
    diagnostics = CSS[
        CSS.index(".world-diagnostics {"):
        CSS.index(".world-diagnostics summary {")
    ]
    assert "left: 0;" in diagnostics
    terminal = CSS[
        CSS.index(".world-chat-terminal {"):
        CSS.index(".world-chat-terminal-body {")
    ]
    assert "bottom: 0;" in terminal
    bind_ui = APP[
        APP.index("  bindUI() {"):
        APP.index("\n  bindDetailResize()", APP.index("  bindUI() {"))
    ]
    assert '!event.target.closest("[data-world-chat-terminal]")' in bind_ui
    assert "chatTerminal.removeAttribute(\"open\")" in bind_ui
    assert '!event.target.closest("[data-world-right-rail]")' in bind_ui
    assert "this.setWorldRightRailExpanded(false)" in bind_ui
    assert 'a[href^=\'/dashboard/chat\']' in APP
    assert 'href="/dashboard/chat" target="_blank"' not in APP
    assert 'playMode: "external"' in DATA
    assert "does not embed, restream, record" in APP
    for media_feature in (
        "Shared listening room",
        "DJ session",
        "Video-sharing room",
        "Watch party",
        "Repository launch event",
        "Organization presentation",
        "user-created playlist",
        "Open moderated shared room",
    ):
        assert media_feature in APP
    assert "normalizeMediaRoom" in APP
    assert "data-world-media-remove" in APP


def test_mobile_world_chat_composer_stays_above_safe_area_and_terminal_bars():
    assert (
        "var(--safe-bottom) + var(--world-diagnostics-height) + 8px"
        in CSS
    )
    open_chat = APP[
        APP.index("  openWorldChat("):
        APP.index("\n  loadNativeWorldChat()", APP.index("  openWorldChat("))
    ]
    assert "this.openChatTerminal();" in open_chat
    assert ".world-native-chat" in CSS
    assert "var(--forkmesh-chat-viewport-height, 100dvh)" in DASHBOARD_CHAT_VIEW
    assert "const layoutHeight = window.innerHeight" in DASHBOARD_CHAT_VIEW
    assert "window.frameElement?.getBoundingClientRect?.().height" in (
        DASHBOARD_CHAT_VIEW
    )
    assert "Math.min(...candidates)" in DASHBOARD_CHAT_VIEW
    composer = DASHBOARD_CHAT_VIEW[
        DASHBOARD_CHAT_VIEW.index(
            'html[data-world-embed="1"] [data-dashboard-chat-composer]'
        ):
        DASHBOARD_CHAT_VIEW.index(
            "@media (max-width: 767px)", DASHBOARD_CHAT_VIEW.index(
                'html[data-world-embed="1"] [data-dashboard-chat-composer]'
            )
        )
    ]
    assert "position: absolute" in composer
    assert "bottom: 0" in composer
    assert "env(safe-area-inset-bottom, 0px)" in composer
    assert "Playback remains individual and opt-in" in APP
    assert 'this.fetchJSON("/api/world/media/spaces"' in APP
    assert "termsConfirmed: true" in APP
    assert "noRebroadcast: true" in APP
    assert "autoplay: false" in APP
    assert "grantMediaModerator" in APP
    assert "removeMediaModerator" in APP
    assert "stopMediaRoom" in APP
    assert "cancelMediaSchedule" in APP
    assert "MEDIA_ROOM_KEY" not in APP
    assert "function updateMediaSpaces" in SCENE
    assert "shared-media-spaces" in SCENE
    assert "roomNode.userData.landmark = \"broadcast\"" in SCENE
    assert "mediaSpaceId: String(" in SCENE
    assert "this.loadMediaSpace(meta.mediaSpaceId, true)" in APP
    for workshop_feature in (
        "collectWorkshopSnapshot",
        "analyzeDatabaseModels",
        "findDirectedCycles",
        "Potentially duplicated concept",
        "Potentially redundant field",
        "Possible circular relationship",
        "use sites",
        "Candidate visual relationship graph",
        "Open live encrypted collaboration",
        "Continue with an owner-authorized agent",
        "Findings are recommendations, not guaranteed facts",
    ):
        assert workshop_feature in APP
    assert "/blobs?" in APP
    assert "did not send private code to an external model" in APP


def test_world_receives_private_notifications_and_global_announcements():
    assert "WORLD_NOTIFICATION_POLL_MS = 60 * 1000" in APP
    assert "normalizeWorldNotifications" in APP
    assert "/api/notifications?node=${encodeURIComponent(" in APP
    assert 'this.fetchJSON("/api/world/events"' in APP
    assert 'this.postJSON("/api/notifications"' in APP
    assert "data-world-notification-count" in APP
    assert "data-world-notifications-open" in APP
    assert "Show global and personal notifications" in APP
    assert 'this.openLandmark("events")' in APP
    assert 'id === "events"' in APP
    assert 'label: "Notifications"' in APP
    assert "data-world-notifications-refresh" in APP
    assert "data-world-notifications-read" in APP
    assert "World announcement:" in APP
    assert "It is never included in multiplayer presence." in APP
    assert "safeNotificationURL" in APP
    assert "sanitizeNotificationText" in APP
    assert "item.description" in APP
    assert "this.refreshOpenEventsPanel()" in APP
    assert 'window.addEventListener("storage", this.handleStorage)' in APP
    assert 'window.removeEventListener("storage", this.handleStorage)' in APP
    assert "window.clearInterval(this.notificationsTimer)" in APP
    assert ".world-notification-button [data-world-notification-count]" in CSS
    assert '.world-notification-list article[data-unread="true"]' in CSS
    assert ".world-event-list article p" in CSS


def test_organization_buildings_load_role_checked_floors_and_offices():
    assert "loadOrganizationSpaces" in APP
    assert 'this.fetchJSON("/api/world/organizations"' in APP
    assert "Array.isArray(orgResult.value?.organizations)" in APP
    for suffix in ('`${root}/members`', '`${root}/teams`'):
        assert suffix in APP
    for label in (
        "team floor",
        "Repository bed",
        "Personal offices",
        "Enter lobby",
        "Watch projects",
        "Follow on Fediverse",
        "Manage access",
    ):
        assert label in APP
    assert "updateOrganizations(this.organizations)" in APP


def test_repository_world_uses_authorized_https_metadata_and_size_aware_nodes():
    for route in ("/tree?path=", "/sizes", "/stats", "/mirrors"):
        assert route in APP
    assert "normalizeTreeEntries" in APP
    assert "data-world-repo-filter" in APP
    assert "updateRepositoryGraph" in APP
    assert "updateRepositoryGraph" in SCENE
    assert "byte share to arc width and directory depth to concentric rings" in APP
    assert "updateRepositoryCatalog" in APP
    assert "updateRepositoryCatalog" in SCENE
    assert "updateRepositorySizeMap" in APP
    assert "updateRepositorySizeMap" in SCENE
    assert "new THREE.ExtrudeGeometry" in SCENE
    assert "active.sizes" in APP
    assert "hydrateHostedRepositorySizeMaps" in APP
    assert "repositorySizeTrees" in APP
    assert "repository-mini-size-map:" in SCENE
    assert '"bulk-import"' in SCENE
    assert "const coreRecords = [];" in SCENE
    assert "const hostedRecords = records;" in SCENE
    assert "const islandRecord = true;" in SCENE
    assert "legacyBulkGroups" in APP
    assert 'mirrorOwners: ["mirror2", "mirror3"]' in APP
    assert '"forkmesh-continuous-city-land"' in SCENE
    assert 'repositoryBridge.name = "hosted-repository-bridge"' in SCENE
    assert "REPOSITORY_ISLAND_CENTER_X" in SCENE
    for entity in (
        "Contributor",
        "Issues",
        "Pull requests",
        "Dependencies",
        "Database models",
        "Services / APIs",
    ):
        assert entity in APP
    for graph_feature in (
        "repositoryNodeRole",
        "modificationBand",
        "redundantCandidate",
        "dependencyDepth",
    ):
        assert graph_feature in APP or graph_feature in SCENE
    assert "repository-relationship-lines" not in SCENE
    for data_filter in ("frequency", "dependency", "security", "coverage"):
        assert f'data-world-repo-filter="{data_filter}"' in APP
    assert 'data-world-repo-filter="frequency" disabled' not in APP
    assert 'data-world-repo-filter="dependency" disabled' not in APP
    assert 'data-world-repo-filter="coverage" disabled' not in APP


def test_legacy_bulk_import_pair_is_coalesced_onto_import_island_only():
    script = "const SOURCE = " + json.dumps(APP) + ";\n" + r"""
const assert = require("assert");
function extract(name) {
  const start = SOURCE.indexOf(`function ${name}(`);
  assert(start >= 0);
  const open = SOURCE.indexOf("{", start);
  let depth = 0;
  for (let i = open; i < SOURCE.length; i += 1) {
    if (SOURCE[i] === "{") depth += 1;
    if (SOURCE[i] === "}" && --depth === 0) return SOURCE.slice(start, i + 1);
  }
  throw new Error(`unterminated ${name}`);
}
eval(extract("mergeHostedRepositoryImports"));
const base = 1785054296350;
const repositories = [];
for (let index = 0; index < 54; index += 1) {
  for (const owner of ["mirror2", "mirror3"]) {
    repositories.push({
      owner,
      name: `import-${index}`,
      source: "remote-clone",
      hostedSince: base + index * 3000,
      updatedAt: base + index,
      liveHost: owner === "mirror2",
    });
  }
}
repositories.push(
  { owner: "jett", name: "forkmesh", source: "local-node", hostedSince: 1 },
  { owner: "mirror2", name: "forkmesh", source: "remote-clone", hostedSince: 2 },
  {
    owner: "mirror2",
    name: "ordinary",
    source: "remote-clone",
    hostedSince: base + 86400000,
  },
  {
    owner: "mirror3",
    name: "ordinary",
    source: "remote-clone",
    hostedSince: base + 86401000,
  },
);
const output = mergeHostedRepositoryImports(repositories, []);
assert.equal(output.filter((record) => record.source === "bulk-import").length, 54);
assert.equal(output.filter((record) => record.name === "ordinary").length, 2);
assert.equal(output.filter((record) => record.name === "forkmesh").length, 2);
"""
    subprocess.run(
        ["node"],
        input=script,
        check=True,
        capture_output=True,
        text=True,
    )


def test_repository_world_reuses_the_star_api_and_keeps_login_in_world():
    for selector in (
        "data-world-repo-star",
        "data-world-repo-key",
        "data-world-repo-star-count",
    ):
        assert selector in APP
    star_control_start = APP.index('class="world-repo-star"')
    star_control = APP[
        star_control_start:
        star_control_start + 1200
    ]
    assert "aria-pressed" in star_control
    assert "data-world-repo-star-count" in star_control
    loader = APP[
        APP.index("  async loadRepositoryStarState("):
        APP.index(
            "\n  async toggleRepositoryStar(",
            APP.index("  async loadRepositoryStarState("),
        )
    ]
    assert ")}/star`" in loader
    assert "this.fetchJSON(base" in loader
    assert 'cache: "no-store"' in loader
    toggle = APP[
        APP.index("  async toggleRepositoryStar("):
        APP.index(
            "\n  syncRepositoryScene()",
            APP.index("  async toggleRepositoryStar("),
        )
    ]
    assert ")}/star`" in toggle
    assert "this.postJSON(" in toggle
    assert 'nextStarred ? "POST" : "DELETE"' in toggle
    assert 'this.toggleWorldAccount(\n        true,\n        "login",' in toggle
    assert "data-world-account-panel" in APP


def test_world_boot_defers_optional_repository_star_and_security_fanout():
    bootstrap = APP[
        APP.index("  async bootstrap() {"):
        APP.index("\n  handleVisibility =", APP.index("  async bootstrap() {"))
    ]
    assert ".slice(0, 48)" not in bootstrap
    assert "Do not fan out a star request" in bootstrap
    loader = APP[
        APP.index("  async loadRepositoryMap("):
        APP.index(
            "\n  async loadRepositorySecurity(",
            APP.index("  async loadRepositoryMap("),
        )
    ]
    assert "if (!automatic)" in loader
    assert loader.index("if (!automatic)") < loader.rindex(
        "this.loadRepositorySecurity("
    )


def test_world_first_person_camera_has_accessible_toggle_and_scene_api():
    toggle_start = APP.index("data-world-camera-toggle")
    toggle = APP[toggle_start:toggle_start + 500]
    assert 'aria-pressed="false"' in toggle
    assert 'title="Enter first-person view"' in toggle
    assert "data-world-camera-label" in toggle
    assert (
        'event.target.closest("[data-world-camera-toggle]")'
        in APP
    )

    sync_start = APP.index("  syncWorldCameraModeButton() {")
    sync = APP[
        sync_start:
        APP.index("\n  toggleWorldCameraMode()", sync_start)
    ]
    for contract in (
        '"first-person"',
        '"aria-pressed"',
        '"Exit first-person view"',
        '"Enter first-person view"',
        '"Third person"',
        '"First person"',
    ):
        assert contract in sync
    scene_mode_start = SCENE.index(
        '  function setCameraMode(mode, reason = "request") {',
    )
    scene_mode = SCENE[
        scene_mode_start:
        SCENE.index("\n  function focusRepositoryPortal(", scene_mode_start)
    ]
    assert 'player.visible = false' in scene_mode
    # Third-person restores the shared World avatar everywhere except an Office
    # meeting, where the local meeting participant is the visible camera target.
    assert 'player.visible = officeSceneMode !== "meeting"' in scene_mode
    assert "if (localParticipant) localParticipant.visible = true" in scene_mode
    assert "firstPersonZoom = 1;" in scene_mode
    assert "renderer.domElement.dataset.cameraMode = cameraMode" in scene_mode
    assert "onCameraMode({ mode: cameraMode, reason })" in scene_mode

    # Visiting a repository sunburst frames it from the orbital camera; only
    # the camera button puts the visitor inside the avatar's head.
    reveal_start = APP.index("  revealRepositoryScene() {")
    reveal = APP[
        reveal_start:
        APP.index("\n  selectRepositoryPortal(", reveal_start)
    ]
    assert "enterRepositoryFirstPerson" not in APP
    assert "enterRepositoryFirstPerson" not in SCENE
    assert 'this.world?.setCameraMode?.("third-person")' in reveal
    assert "this.world?.focusRepositoryPortal?.(" in reveal
    assert "this.syncWorldCameraModeButton();" in reveal
    assert "setCameraMode," in SCENE
    assert "getCameraState:" in SCENE


def test_top_toolbar_opens_dashboard_in_a_safe_new_tab():
    assert 'class="world-top-link world-dashboard-link"' in APP
    assert 'href="/dashboard"' in APP
    assert 'target="_blank"' in APP
    assert 'rel="noopener noreferrer"' in APP
    assert 'aria-label="Open Dashboard in a new tab"' in APP
    assert (
        '.world-top-actions > .world-top-link[href="/dashboard"]'
        not in CSS
    )


def test_admin_error_button_opens_a_sortable_in_world_error_table():
    start = APP.index("  async openAdminErrors(")
    method = APP[start:APP.index("\n  // Read the locked placement", start)]
    assert 'detail.dataset.openLandmark = "admin-errors"' in method
    assert "this.adminErrorsPanelHTML()" in method
    assert "this.showDetailOverlay(" in method
    assert "await this.refreshAdminErrorRows()" in method
    assert "window.open(" not in method
    assert 'data-world-activity-search="errors"' in APP
    assert 'data-world-activity-filter="errors"' in APP
    assert 'data-world-activity-sort="errors"' in APP


def test_world_task_button_and_inactive_avatar_visibility_contracts():
    assert "data-world-tasks-open" in APP
    assert "data-world-task-count" in APP
    assert 'this.selectSettingsTab("work")' in APP
    assert "avatarOpacity" not in SCENE
    assert "inactiveSince" not in SCENE
    assert "never the visibility of the person" in SCENE


def test_render_stalls_include_bounded_likely_component_attribution():
    assert (
        "Render stall detected; we think it was "
        "${component}" in SCENE
    )
    assert "likelyCause: { component, codeArea }," in SCENE
    assert 'component = "camera controls";' in SCENE
    assert 'component = "Three.js renderer workload";' in SCENE
    assert 'component = `office ${officeSceneMode} scene`;' in SCENE
    assert 'codeArea = "renderer.render(scene, camera)";' in SCENE
    assert "const frameWorkStartedAt = performance.now();" in SCENE
    assert "performance.now() - frameWorkStartedAt" in SCENE
    assert "scheduleRenderStallWarning(frameWorkMs);" in SCENE
    assert "scheduleRenderStallWarning(rawFrameMs);" not in SCENE
    assert "renderer.shadowMap.autoUpdate = false;" in SCENE
    assert "nextShadowMapUpdateAt = time + 500;" in SCENE
    assert "function canvasReadableImageURL(value)" in SCENE
    assert "url.origin !== window.location.origin" in SCENE
    assert "const key = canvasReadableImageURL(url);" in SCENE


def test_world_first_person_zoom_out_falls_back_to_third_person():
    zoom_start = SCENE.index("  function setCameraZoom(value) {")
    zoom = SCENE[
        zoom_start:
        SCENE.index("\n  function touchDistance()", zoom_start)
    ]
    assert "next < FIRST_PERSON_ZOOM_MIN" in zoom
    assert "firstPersonZoom <= FIRST_PERSON_ZOOM_MIN" in zoom
    assert 'setCameraMode("third-person", "zoom-out")' in zoom
    assert "pinchStartZoom = cameraZoom;" in zoom
    assert "const FIRST_PERSON_ZOOM_MIN = 0.25;" in SCENE
    assert "const FIRST_PERSON_ZOOM_MAX = 5;" in SCENE

    handler_start = APP.index("  handleWorldCameraMode(state) {")
    handler = APP[
        handler_start:
        APP.index("\n  toggleWorldCameraMode()", handler_start)
    ]
    assert "this.syncWorldCameraModeButton();" in handler
    assert 'state?.reason === "zoom-out"' in handler
    assert "onCameraMode: (state) => this.handleWorldCameraMode(state)" in APP


def test_world_autoloads_the_live_catalog_attested_flagship_repository_map():
    assert 'owner: "forkmesh",\n  repo: "forkmesh"' in APP
    bootstrap = APP[
        APP.index("  async bootstrap() {"):
        APP.index("\n  handleVisibility =", APP.index("  async bootstrap() {"))
    ]
    assert "await Promise.allSettled([contextPromise, dataPromise]);" in bootstrap
    assert "void this.autoLoadFlagshipRepositoryMap();" in bootstrap
    assert (
        bootstrap.index("await Promise.allSettled([contextPromise, dataPromise]);")
        < bootstrap.index("void this.autoLoadFlagshipRepositoryMap();")
    )
    assert "this.world = createWorldScene({" in bootstrap
    assert (
        bootstrap.index("this.world = createWorldScene({")
        < bootstrap.index("void this.autoLoadFlagshipRepositoryMap();")
    )

    catalog = APP[
        APP.index("  flagshipCatalogCommits() {"):
        APP.index(
            "\n  async autoLoadFlagshipRepositoryMap() {",
            APP.index("  flagshipCatalogCommits() {"),
        )
    ]
    assert "record?.owner" in catalog
    assert (
        '["local-node", "remote-clone", "organization-alias"].includes(source)'
        in catalog
    )
    assert "!record?.isPrivate" in catalog
    assert "!record?.archived" in catalog
    assert "record?.commit" in catalog
    assert "record?.stateHash" in catalog


def test_flagship_portal_retries_until_the_default_repository_is_open():
    # The alias pin needs the repository catalog and the mirror snapshot to
    # agree. Both are re-read after entry, so the automatic load is retried
    # from the freshest pair instead of only the boot snapshot.
    reconcile = APP[
        APP.index("  reconcileRepositoryAliasCatalog() {"):
        APP.index(
            "\n  async retryFlagshipPortal() {",
            APP.index("  reconcileRepositoryAliasCatalog() {"),
        )
    ]
    assert "reconcileRepositoryAliases(\n      this.rawNativeRepositories," in reconcile
    assert "this.mirrorCatalogs," in reconcile
    assert "if (signature === this.repositoryAliasSignature) return false;" in reconcile

    retry = APP[
        APP.index("  async retryFlagshipPortal() {"):
        APP.index(
            "\n  syncRepositoryScene() {",
            APP.index("  async retryFlagshipPortal() {"),
        )
    ]
    # Never override a visitor's own choice, and never poll without a bound.
    assert "this.repositoryManualSelection ||" in retry
    assert "this.activeRepository ||" in retry
    assert "this.flagshipPortalRetries >= FLAGSHIP_PORTAL_RETRY_LIMIT" in retry
    assert 'this.fetchJSON("/api/repositories"' in retry
    assert "if (records.length) this.rawNativeRepositories = records;" in retry
    assert "void this.autoLoadFlagshipRepositoryMap();" in retry
    assert "const FLAGSHIP_PORTAL_RETRY_LIMIT = 20;" in APP

    # The retry rides the existing import poll rather than adding a timer.
    poll = APP[
        APP.index("  startRepositoryImportPolling() {"):
        APP.index(
            "\n  updateLocation(",
            APP.index("  startRepositoryImportPolling() {"),
        )
    ]
    assert "await this.retryFlagshipPortal();" in poll
    assert "REPOSITORY_IMPORT_POLL_MS" in poll


def test_login_and_signup_stay_inside_the_world_and_out_of_presence():
    assert "data-world-account-open" in APP
    assert "data-world-login-form" in APP
    assert "data-world-signup-form" in APP
    assert '"/api/accounts/login"' in APP
    assert '"/api/accounts/signup"' in APP
    assert "storeWorldSession(body)" in APP
    assert "location.reload()" in APP
    assert "password, totp" in APP
    assert "password },\n        { auth: false" in APP
    assert "Credentials are never placed in URLs" in APP
    assert ".world-account" in CSS


def test_flagship_graph_requires_commit_matched_tree_sizes_stats_and_entities():
    entity_loader = APP[
        APP.index("  async loadRepositoryEntityRecords("):
        APP.index(
            "\n  flagshipCatalogCommits() {",
            APP.index("  async loadRepositoryEntityRecords("),
        )
    ]
    assert (
        entity_loader.index("const pullResult =")
        < entity_loader.index("const issueResults = [];")
    )
    assert "await this.loadRepositoryPullRecords(base)" in entity_loader
    assert "const issueConcurrency = 2;" in entity_loader
    assert "timeout: 6000" in entity_loader

    fetch_map = APP[
        APP.index("  async fetchRepositoryMapSnapshot("):
        APP.index(
            "\n  async loadRepositoryMap(",
            APP.index("  async fetchRepositoryMapSnapshot("),
        )
    ]
    assert "`${base}/tree?path=`" in fetch_map
    assert "const ref = `?ref=${encodeURIComponent(commit)}`;" in fetch_map
    assert "`${base}/sizes${ref}`" in fetch_map
    assert "`${base}/stats${ref}`" in fetch_map
    assert "this.loadRepositoryEntityRecords(base, commit, {" in fetch_map
    assert "privateRepository: catalogRecord?.isPrivate === true" in fetch_map
    assert "pullResult," in fetch_map
    # Pull metadata is optional enrichment. It starts alongside the immutable
    # size/stats requests so it cannot hold the first repository frame blank.
    assert "const pullResultPromise =" in fetch_map
    assert "const sizeRequest = this.fetchJSON(`${base}/sizes${ref}`" in fetch_map
    assert (
        fetch_map.index("const pullResultPromise =")
        < fetch_map.index("await Promise.allSettled([")
    )
    assert (
        fetch_map.index("const sizeRequest =")
        < fetch_map.index("await Promise.allSettled([")
    )
    assert "options.onTree?.(" in fetch_map
    assert "options.onSizes?.(" in fetch_map
    assert "const REPOSITORY_METADATA_TIMEOUT_MS = 45 * 1000;" in APP
    assert fetch_map.count(
        'String(sizeResult.value?.commit || "").toLowerCase() === commit'
    ) == 1
    assert fetch_map.count(
        'String(statsResult.value?.commit || "").toLowerCase() === commit'
    ) == 1
    assert (
        'String(entityRecordsResult.value?.commit || "").toLowerCase() === commit'
        in fetch_map
    )
    assert "snapshot.entries.length > 0" in fetch_map
    assert "Boolean(sizes)" in fetch_map
    assert "Boolean(stats)" in fetch_map
    assert "Boolean(entityRecords)" in fetch_map

    load_map = APP[
        APP.index("  async loadRepositoryMap("):
        APP.index(
            "\n  async loadRepositorySecurity(",
            APP.index("  async loadRepositoryMap("),
        )
    ]
    gate = load_map[
        load_map.index("if (\n      (options.requireComplete"):
        load_map.index("this.activeRepository = result.snapshot;")
    ]
    assert "!result.complete" in gate
    assert "!expectedCommits.has" in gate
    assert 'this.repositoryMapState = "unavailable"' in gate
    assert "updateRepositoryGraph?.([], [])" in gate
    assert "this.activeRepository = result.snapshot;" not in gate
    assert "this.previewRepositoryMap(" in gate


def test_repository_map_autoload_is_deduplicated_and_never_overrides_manual_choice():
    auto = APP[
        APP.index("  async autoLoadFlagshipRepositoryMap() {"):
        APP.index(
            "\n  async fetchRepositoryMapSnapshot(",
            APP.index("  async autoLoadFlagshipRepositoryMap() {"),
        )
    ]
    assert "this.repositoryManualSelection" in auto
    assert "this.activeRepository" in auto
    # The tree preview paints immediately; mirror metadata may converge behind
    # it without collapsing the flagship wheel into a syncing placeholder.
    assert "this.world.updateRepositoryGraph?.([], []);" not in auto
    assert "if (!catalogCommits.size)" not in auto
    assert "requireComplete: false" in auto
    assert "catalogCommits.size ? catalogCommits : undefined" in auto

    load_map = APP[
        APP.index("  async loadRepositoryMap("):
        APP.index(
            "\n  async loadRepositorySecurity(",
            APP.index("  async loadRepositoryMap("),
        )
    ]
    assert "this.repositoryMapLoads.get(key)" in load_map
    assert "this.repositoryMapLoads.set(key, request)" in load_map
    assert "if (!automatic) this.repositoryManualSelection = key;" in load_map
    assert "const selection = ++this.repositoryMapSelection;" in load_map
    assert "selection !== this.repositoryMapSelection" in load_map
    assert "this.loadRepositoryMap(owner, name, {" in APP
    assert "automatic: false" in APP
    assert "revealScene: true" in APP
    assert "data-world-repository-map-state=\"unavailable\"" in APP
    assert "did not substitute sample files, guessed entries, or stale analysis" in APP


def test_repository_graph_uses_commit_produced_edges_coverage_and_scan_state():
    for contract in (
        "payload?.analysis?.commit",
        'query.set("ref", commit)',
        "mergeCommitMatchedSecurity",
        "latest?detail=rich",
        "analysisCommit",
        "coverage",
    ):
        assert contract in APP
    for graph_contract in (
        "edgeIndexes",
        "forces",
    ):
        assert graph_contract in SCENE
    assert "relationshipPairs" not in SCENE
    assert "repository-relationship-lines" not in SCENE
    assert "currentRoot" not in SCENE
    assert "previousRoot" not in SCENE
    assert "security: \"unavailable\"" in APP
    assert "Sensitive candidate" not in APP


def test_public_security_clipboard_starts_fail_closed_and_redacted():
    assert SECURITY_LATEST["status"] == "Scan unavailable"
    assert SECURITY_LATEST["publicRedaction"] == {
        "sourceCode": True,
        "secrets": True,
        "sensitivePrompts": True,
        "exploitDetails": True,
    }
    for limitation in (
        "Automated scans may miss vulnerabilities.",
        "Results apply only to the scanned commit.",
        "A clean scan is not a guarantee of security.",
        "Human review may still be required.",
    ):
        assert limitation in SECURITY_LATEST["limitations"]
    for integration_contract in (
        "data-world-security-scan",
        "loadRepositorySecurity",
        "/security-scans",
        "`${base}/latest`",
        "`${base}/history?limit=10`",
        "Scan and review record",
        "Report a security issue privately",
        "latest?.clipboard",
    ):
        assert integration_contract in APP


def test_security_triage_is_authoritative_and_owner_interactive():
    for contract in (
        "`${base}/triage`",
        "falsePositiveStatus",
        "data-world-security-triage",
        'method: "PATCH"',
        "findingId",
        "scanId",
        "Owner / security-reviewer triage",
    ):
        assert contract in APP


def test_workshops_persist_share_and_deep_link_by_repo_commit_run_and_result():
    for contract in (
        '"/api/world/workshops"',
        "/results",
        "/participants",
        "/events?after=",
        "resultId",
        "workshopSession",
        "workshopResult",
        "data-world-workshop-save",
        "data-world-workshop-load",
        "data-world-workshop-share",
        "Model definitions and their own use sites",
        "definitionPath",
        "model.useSites",
    ):
        assert contract in APP
    assert "forkmesh.world.workshop" not in APP
    assert "analysisMatches" in APP
    assert "commitMatchedCoverage" in APP
    assert "scopedWorkshop" in DASHBOARD_CHAT
    assert "#world-workshop/" in DASHBOARD_CHAT
    assert "workshopRepoParts" in DASHBOARD_CHAT
    assert "encodeURIComponent(ROOM_OWNER)" in DASHBOARD_CHAT
    assert "encodeURIComponent(ROOM_REPO)" in DASHBOARD_CHAT
    assert "Never multiplex a private repo/run channel into the Town" in DASHBOARD_CHAT
    assert "liveWorkshopChatAvailable" in APP
    assert "private and unresolved workshops stay on the authorized persisted event stream" in APP
    assert "workshopAgentDeepLink" in DASHBOARD
    assert "/api/world/workshops/${encodeURIComponent(" in DASHBOARD
    assert "Authorized workshop context" in DASHBOARD
    assert "repoMatchesKey(repo, savedSession?.repository)" in DASHBOARD
    assert "savedSession?.runId !== deepLink.runId" in DASHBOARD
    assert "savedSession?.commit !== deepLink.commit" in DASHBOARD
    assert "result?.runId !== savedSession?.runId" in DASHBOARD
    assert "result?.commit !== savedSession?.commit" in DASHBOARD
    assert "No agent has been started." in DASHBOARD
    assert "workshopResult: resultId,\n      run: runId,\n      commit," in APP


def test_database_workshop_maps_each_model_to_its_own_use_site_paths():
    cycle_start = APP.index("function findDirectedCycles")
    database_start = APP.index("function analyzeDatabaseModels")
    analysis_start = APP.index("function analyzeWorkshopSnapshot")
    functions = APP[cycle_start:database_start] + APP[database_start:analysis_start]
    script = f"""
function sanitizePresenceText(value, fallback, limit) {{
  const text = String(value || "").trim();
  return (text || fallback).slice(0, limit);
}}
{functions}
const report = analyzeDatabaseModels([
  {{ path: "db/models.ts", text: "class User {{}}\\nclass Project {{}}" }},
  {{ path: "api/user.ts", text: "User User" }},
  {{ path: "jobs/project.ts", text: "Project" }},
]);
process.stdout.write(JSON.stringify(report.models.map((model) => ({{
  name: model.name,
  uses: model.uses,
}}))));
"""
    completed = subprocess.run(
        ["node", "-e", script],
        check=True,
        capture_output=True,
        text=True,
    )
    models = {model["name"]: model["uses"] for model in json.loads(completed.stdout)}
    assert models["User"] == [{"path": "api/user.ts", "count": 2}]
    assert models["Project"] == [{"path": "jobs/project.ts", "count": 1}]


def test_reachable_behind_mirrors_are_syncing_not_a_stub():
    reconcile_start = APP.index("function reconcileRepositoryAliases")
    reconcile_end = APP.index(
        "function normalizeCommunityEvents", reconcile_start)
    reconcile_source = APP[reconcile_start:reconcile_end]
    script = reconcile_source + r"""
function sanitizePresenceText(value, fallback = "", limit = 80) {
  return String(value || fallback).slice(0, limit);
}
function immutableGitOid(value) {
  const oid = String(value || "").toLowerCase();
  return /^[0-9a-f]{40,64}$/.test(oid) ? oid : "";
}
const records = [{
  owner: "mirror2",
  name: "forkmesh",
  isPrivate: false,
  commit: "a".repeat(40),
  stateHash: "b".repeat(64),
  updatedAt: 10,
}];
const catalogs = [{
  requestedOwner: "forkmesh",
  requestedRepo: "forkmesh",
  mirrors: [{
    node: "mirror2",
    status: "online",
    cloneAvailable: true,
    integrity: "ok",
    behind: true,
    commit: "a".repeat(40),
  }],
}];
const [alias] = reconcileRepositoryAliases(records, catalogs);
process.stdout.write(JSON.stringify({
  owner: alias.owner,
  liveHost: alias.liveHost,
  mirrorState: alias.mirrorState,
  mirrorCount: alias.mirrorCount,
}));
"""
    completed = subprocess.run(
        ["node", "-e", script],
        check=True,
        capture_output=True,
        text=True,
    )
    assert json.loads(completed.stdout) == {
        "owner": "forkmesh",
        "liveHost": False,
        "mirrorState": "syncing",
        "mirrorCount": 1,
    }
    scene = (
        ROOT / "public" / "world" / "world-scene.js"
    ).read_text(encoding="utf-8")
    assert '"MIRRORS SYNCING"' in scene
    assert 'record.mirrorState === "syncing"' in scene


def test_coverage_workshop_uses_only_commit_matched_per_file_values():
    analysis_start = APP.index("function analyzeWorkshopSnapshot")
    clean_start = APP.index("function cleanRepositories")
    function_source = APP[analysis_start:clean_start]
    script = function_source + r"""
const measured = analyzeWorkshopSnapshot("Test-coverage analysis", [
  { path: "src/covered.js", type: "file", coverage: 95, role: "module" },
  { path: "src/partial.js", type: "file", coverage: 35, role: "module" },
  { path: "src/uncovered.js", type: "file", coverage: 0, role: "module" },
  { path: "src/unknown.js", type: "file", coverage: null, role: "module" },
  { path: "tests/covered.test.js", type: "file", coverage: 100, role: "test" },
], [], {});
const unavailable = analyzeWorkshopSnapshot("Test-coverage analysis", [
  { path: "src/app.js", type: "file", coverage: null, role: "module" },
  { path: "tests/app.test.js", type: "file", coverage: null, role: "test" },
], [], {});
process.stdout.write(JSON.stringify({ measured, unavailable }));
"""
    completed = subprocess.run(
        ["node", "-e", script],
        check=True,
        capture_output=True,
        text=True,
    )
    reports = json.loads(completed.stdout)
    measured = reports["measured"]
    assert (
        "4 of 5 mapped files have commit-matched coverage values."
        in measured["findings"]
    )
    assert (
        "Artifact-reported file states: 2 covered (80%+), 1 partial, "
        "1 uncovered, and 1 unknown."
        in measured["findings"]
    )
    assert {
        (node["label"], node["kind"], node["detail"])
        for node in measured["nodes"]
    } >= {
        ("src/partial.js", "Coverage: partial", "35% at the analyzed commit"),
        (
            "src/uncovered.js",
            "Coverage: uncovered",
            "0% at the analyzed commit",
        ),
        (
            "src/unknown.js",
            "Coverage: unknown",
            "No commit-matched per-file value",
        ),
    }
    assert any(
        "1 unknown file paths" in item
        for item in measured["recommendations"]
    )
    unavailable = reports["unavailable"]
    assert unavailable["findings"] == [
        "1 test paths and 1 non-test paths were mapped; no line-coverage "
        "claim is made without an artifact."
    ]
    assert unavailable["recommendations"] == [
        "Upload a commit-matched coverage artifact to populate per-file "
        "coverage and uncovered filters."
    ]


def test_tree_normalization_rejects_missing_or_wrong_commit_coverage():
    normalize_start = APP.index("function normalizeTreeEntries")
    merge_start = APP.index("function mergeCommitMatchedSecurity")
    normalize_source = APP[normalize_start:merge_start]
    script = r"""
function sanitizePresenceText(value, fallback, limit) {
  const text = String(value || "").trim();
  return (text || fallback).slice(0, limit);
}
function fileLanguage() { return "JavaScript"; }
function modificationBand() { return "unknown"; }
function repositoryNodeRole(_path, type) {
  return type === "directory" ? "directory" : "module";
}
""" + normalize_source + r"""
const commit = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const normalized = normalizeTreeEntries({
  analysis: { commit },
  entries: [
    { name: "covered.js", path: "src/covered.js", type: "blob", coverage: 91 },
    { name: "missing.js", path: "src/missing.js", type: "blob", coverage: null },
    {
      name: "stale.js",
      path: "src/stale.js",
      type: "blob",
      coverage: 88,
      analysisCommit: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    },
  ],
});
process.stdout.write(JSON.stringify(
  Object.fromEntries(normalized.map((entry) => [entry.name, entry.coverage]))
));
"""
    completed = subprocess.run(
        ["node", "-e", script],
        check=True,
        capture_output=True,
        text=True,
    )
    assert json.loads(completed.stdout) == {
        "covered.js": 91,
        "missing.js": None,
        "stale.js": None,
    }


def test_fediverse_directory_is_public_only_and_consent_aware():
    assert FEDIVERSE_DIRECTORY["privacy"] == {
        "publicInstanceMetadataOnly": True,
        "publicDirectoryMetadataOnly": True,
        "privateFollowersExcluded": True,
        "privateAccountsExcluded": True,
        "connectionsRequireUserConsent": True,
        "operatorAttestationsAreNotOAuthVerification": True,
    }
    assert 'this.fetchJSON("/api/world/fediverse"' in APP
    assert 'this.fetchJSON("/world/fediverse-directory.json"' not in APP
    assert "Participating public directory records" in APP
    assert "private followers" not in json.dumps(FEDIVERSE_DIRECTORY).lower()
    for supported_field in (
        "publicActivity",
        "communities",
        "relationships",
        "consentedFollowers",
        "approvedSubscriptions",
    ):
        assert supported_field in APP
    assert "follower?.consent === true" in APP
    assert "follower?.public === true" in APP
    assert "function updateFediverseDirectory" in SCENE
    assert "consented-fediverse-profiles" in SCENE
    assert "consentedProfiles" in APP
    assert "consentedProfiles" in SCENE
    assert 'network: "X"' in APP
    assert 'network: "Reddit"' in APP
    assert "oauthVerifiedByForkMesh" in APP


def test_world_has_no_abuse_quarantine_ui_or_api_integration():
    for source in (APP, DATA, SCENE, CSS):
        assert "quarantine" not in source.lower()
    assert "/api/security/quarantine" not in APP
    assert "data-world-quarantine" not in APP
    assert "createQuarantine" not in SCENE
    assert "updateQuarantine" not in SCENE


def test_known_bots_are_verified_from_a_deployed_public_directory():
    assert BOT_DIRECTORY["privacy"] == {
        "publicResourcesOnly": True,
        "privateRepositoryPathsExcluded": True,
        "credentialsExcluded": True,
        "exactVisitorActivityExcluded": True,
    }
    assert BOT_DIRECTORY["bots"]
    for bot in BOT_DIRECTORY["bots"]:
        assert set(bot) == {
            "id",
            "name",
            "type",
            "verified",
            "generalActivity",
            "publicResourceCategory",
        }
    assert 'this.fetchJSON("/world/bot-directory.json"' in APP
    assert "updateBots" in APP
    assert "function updateBots" in SCENE
    for label in ("Verified", "Unverified", "Known automated agents"):
        assert label in APP


def test_financial_and_security_metaphors_disclose_current_limitations():
    assert (
        "The production mainnet-beta flow reads public on-chain state, accepts "
        "direct wallet-to-pool contributions"
    ) in DATA
    assert "sends unsigned plans to the instance owner’s local Qt signer" in DATA
    assert "Mainnet-beta · external signer" in DATA
    assert "Development instances may explicitly select a test network" in DATA
    assert "Visual coins are not guaranteed rewards or investments" in DATA
    assert "a clean scan is not a guarantee" in APP
    assert (
        "platform administrators do not automatically receive those recipient "
        "private keys"
    ) in PRIVACY
    assert "Raw IP addresses, full User-Agent strings, and precise location are never public" in PRIVACY
    assert "Every metaphor has a technical panel" in DATA
    assert "availability, not automatic trust" in DATA
    assert "Normal repository traffic stays on HTTPS" in DATA
    assert "What is actually happening" in APP
    assert 'worldQuery.get("landmark")' in APP
    assert "this.openLandmark(this.requestedLandmark)" in APP
    assert "Join or fund the community reward program" in APP


def test_world_reward_program_is_a_voluntary_direct_wallet_flow():
    assert 'primary: { label: "Join / fund reward program", action: "reward" }' in DATA
    assert "production mainnet-beta flow" in DATA
    assert 'this.postJSON(\n        "/api/rewards/contributions"' in APP
    assert 'action: "prepare"' in APP
    assert "Joining is a voluntary direct contribution" in APP
    assert "does not purchase ownership, guaranteed rewards, investment returns" in APP
    assert "your own wallet reviews and signs the transfer" in APP
    assert "Prepare direct wallet transfer" in APP
    assert "Review in self-custodial wallet" in APP
    assert "Never paste a private key, seed, or recovery phrase" in APP


def test_world_has_responsive_and_reduced_motion_fallbacks():
    assert "@media (max-width: 720px)" in CSS
    assert "@media (pointer: coarse), (hover: none)" in CSS
    assert "@media (max-height: 520px) and (orientation: landscape)" in CSS
    assert "@media (prefers-reduced-motion: reduce)" in CSS
    assert "world-touch-controls" in CSS
    assert "world-touch-thumbstick" in CSS
    assert "world-touch-button" not in CSS
    assert "left: calc(var(--safe-left) + 12px)" in CSS
    assert "var(--world-viewport-height, 100dvh)" in CSS
    assert "window.visualViewport?.height" in APP
    assert 'window.addEventListener("orientationchange", this.syncViewportHeight)' in APP
    assert "renderWebGLFallback" in APP
    assert "BroadcastChannel" in APP


def test_world_supports_bounded_pinch_wheel_and_drag_controls():
    # Pinch and wheel share one explicit near/far camera range rather than
    # changing page scale or maintaining two controls that can drift apart.
    for contract in (
        "CAMERA_ZOOM_MIN",
        "CAMERA_ZOOM_MAX",
        "function setCameraZoom",
        "function beginPinchIfReady",
        "pinchStartDistance / distance",
        'addEventListener("pointermove", handlePointerMove',
        'addEventListener("wheel", handleWheel',
        # First person inverts the wheel: through the visitor's own eyes,
        # scrolling down zooms in on what they are looking at (adhoc #303).
        "Math.exp(deltaPixels * (firstPerson ? -0.0015 : 0.0015))",
        "getCameraState",
    ):
        assert contract in SCENE
    assert 'addEventListener("pointermove", handlePointerMove, {' in SCENE
    assert "passive: false" in SCENE
    assert 'class="world-controls"' not in APP
    assert "world-controls-hint" not in APP
    assert "data-world-thumbstick" in APP
    assert "data-world-thumbstick-handle" in APP
    assert "data-move=" not in APP
    assert "click the plaza" not in APP
    assert "click on the plaza" not in DATA

    # Fine-pointer navigation keeps the cursor visible and rotates only while
    # the primary pointer is dragged. One touch rotates, two touches only pinch,
    # and the proportional thumbstick stays camera-relative like WASD.
    for contract in (
        'dataset.cameraControl = "drag"',
        "function rotateCamera",
        "cameraYaw -= deltaX * CAMERA_LOOK_SENSITIVITY",
        "cameraPitch + deltaY * CAMERA_LOOK_SENSITIVITY",
        "pointerLast.copy(pointerStart)",
        "setPointerCapture(event.pointerId)",
        "CAMERA_LOOK_SENSITIVITY",
        "movement.addScaledVector(forward, forwardInput)",
        "movement.addScaledVector(right, rightInput)",
        "const touchMovement = new THREE.Vector2()",
        "function setTouchMovement",
        "movement.addScaledVector(forward, -touchMovement.y)",
        "movement.addScaledVector(right, touchMovement.x)",
        "touchPointers.size === 1",
        "A two-finger gesture is zoom-only",
        "topSpeed * inputStrength",
    ):
        assert contract in SCENE
    assert "requestPointerLock" not in SCENE
    assert "pointerLockElement" not in SCENE
    assert "raycaster.intersectObject(ground" not in SCENE

    # Keyboard movement reaches its named cap on the first frame, while analog
    # input retains proportional control and blur still clears stale input.
    for contract in (
        "PLAYER_MAX_SPEED",
        "function movementSpeedForInput",
        "? topSpeed",
        "topSpeed * inputStrength",
        "keyboardMovementSpeed = baseMoveSpeed()",
        "function setMovementTuning",
        "setMovementTuning,",
        'window.addEventListener("blur", handleWindowBlur)',
        "getMovementState",
    ):
        assert contract in SCENE

    # Every global/canvas listener introduced by these controls has a matching
    # cleanup path when the custom element is disconnected.
    for cleanup in (
        'removeEventListener("pointermove", handlePointerMove)',
        'removeEventListener("wheel", handleWheel)',
        'removeEventListener("pointercancel", handlePointerCancel)',
        'removeEventListener("blur", handleWindowBlur)',
    ):
        assert cleanup in SCENE


def test_world_overview_zoom_keeps_the_finite_ground_inside_camera_depth():
    assert "const CONTINUOUS_CITY_RADIUS = 365;" in SCENE
    assert "const CAMERA_OFFSET = [17, 16, 21];" in SCENE
    assert "const CAMERA_ZOOM_MIN = 0.06;" in SCENE
    assert "const CAMERA_ZOOM_MAX = 28;" in SCENE
    assert "const CAMERA_FAR_PLANE = 1800;" in SCENE
    assert (
        "new THREE.PerspectiveCamera(\n"
        "    44,\n"
        "    1,\n"
        "    0.1,\n"
        "    CAMERA_FAR_PLANE,\n"
        "  )"
    ) in SCENE

    # At maximum strategic zoom, a camera looking at the world center can still
    # see through the opposite edge of the finite ground before its far plane.
    camera_distance = math.hypot(17, 16, 21)
    assert camera_distance * 28 + 365 < 1800


def test_world_uses_nonhuman_infrastructure_without_the_world_spanning_grid():
    assert "const WORLD_RADIUS = 620" in SCENE
    assert '"forkmesh-continuous-city-land"' in SCENE
    assert "const CONTINUOUS_CITY_RADIUS = 365" in SCENE
    assert "electric-mesh-city-block-grid" not in SCENE
    assert "electricMeshConduit" not in SCENE
    assert "electricMeshJunction" not in SCENE
    assert "createElectricMeshCityGrid" not in SCENE
    assert '"ACTIVE LEADERBOARD"' in SCENE
    for status in (
        "Registered",
        "Supporting member",
        "Mirror operator",
        "Organization admin",
    ):
        assert status in SCENE
    assert "useRegisteredLounge" in SCENE
    assert 'avatar.userData.loungeActivity === "recent"' in SCENE


def test_leaderboards_share_walkable_grass_and_keep_an_inward_facing_ring():
    assert "const LEADERBOARD_ISLAND_CENTER_X = -130;" in SCENE
    assert "const LEADERBOARD_CONNECTION_MIN_X = -103;" in SCENE
    assert '"forkmesh-continuous-city-land"' in SCENE
    assert (
        'leaderboardConnection.name = "forkmesh-leaderboard-island-connection"'
        in SCENE
    )
    assert 'leaderboardPromenade.name = "forkmesh-leaderboard-promenade"' in SCENE
    assert "CONTINUOUS_CITY_RADIUS - margin" in SCENE
    assert "Math.atan2(-x, -z)" in SCENE
    assert "placeBillboardOnIsland(" in SCENE
    assert "makeReferralLeaderboardSign(THREE)" in SCENE


def test_world_has_no_pale_plaza_and_places_trees_deterministically_clear_of_use():
    assert "new THREE.CylinderGeometry(16.5, 17.4, 0.34, 64)" not in SCENE
    assert "const plazaLines = new THREE.Group()" not in SCENE
    assert "function createFountain" in SCENE
    assert "new THREE.CylinderGeometry(4.4, 4.4, 0.16, 48)" in SCENE
    assert "const TREES_PER_LANDMARK = 3" in SCENE
    assert "function deterministicTreeLayout()" in SCENE
    assert "deterministicFraction(`tree-radius:${landmark.id}:${treeIndex}`)" in SCENE
    assert "pointInsideBounds(x, z, ARRIVAL_GRID_BOUNDS)" in SCENE
    assert "pointInsideBounds(x, z, cabinetBounds)" in SCENE
    assert "const durableBounds" not in SCENE
    assert "TREE_MIN_SPACING" in SCENE
    assert "const radius = 20 + (index % 7) * 2.25" not in SCENE


def test_world_member_directory_seats_registered_users_from_roster():
    # The directory figures come from the public users directory (adhoc #228)
    # and, since adhoc #287, sit in a circle around the campfire facing the
    # flames. The Member Lounge structure that used to hold them (and its
    # count plaque / account button) was removed entirely.
    assert "function updateMemberLounge" in SCENE
    assert "const loungeMembers = new Map();" in SCENE
    assert "registered-user-lounge" not in SCENE
    assert "function createRegisteredUserLounge" not in SCENE
    assert "function memberLoungePlaqueTexture" not in SCENE
    assert "function memberLoungeAuthTexture" not in SCENE
    assert "function makeMemberLoungePlaque" not in SCENE
    assert "function syncLoungeAuthButton" not in SCENE
    # world.js feeds it the public roster (no email/device material) and
    # dedupes accounts already rendered as live or opted-in idle avatars.
    assert '"/api/accounts/users"' in APP
    assert "syncMemberLounge" in APP
    assert "this.memberDirectory.length" in APP


def test_world_members_sit_in_an_expanding_circle_around_the_campfire():
    # adhoc #287: one bench per registered account rings the campfire. Away
    # members appear as seated figures facing the fire, members walking the
    # world as live avatars leave their bench empty, and the ring rebuilds
    # wider whenever a new account joins so everyone still fits. adhoc #291
    # adds one extra bench that always stays open for the next guest, and
    # adhoc #303 one more per guest already in the world, names every bench
    # so an empty one says who is out and about, and seats accounts that
    # signed up after the tab loaded straight from their presence frame.
    assert "function rebuildCampfireCircle" in SCENE
    assert '"campfire-member-circle"' in SCENE
    assert (
        "rebuildCampfireCircle(\n      Math.max(total, roster.length) "
        "+ guestSeats + 1,\n    )" in SCENE
    )
    assert "function setCampfireSeatLabel" in SCENE
    assert "function campfireSeatPlateTexture" in SCENE
    assert '"OPEN SEAT"' in SCENE
    assert '"OUT AND ABOUT"' in SCENE
    assert "campfire.userData.seatByName" in SCENE
    assert "noteDirectoryMembers" in APP
    # Concentric rows hold 25 people each and preserve one aligned walk-in gap.
    assert "const CAMPFIRE_MEMBERS_PER_ROW = 25;" in SCENE
    assert "const rowCount = Math.ceil(count / CAMPFIRE_MEMBERS_PER_ROW);" in SCENE
    assert "CAMPFIRE_ENTRANCE_WIDTH / radius" in SCENE
    assert '"sitting around the campfire"' in SCENE
    # Figures and idle live avatars both face the pit at the circle's centre.
    # Avatar fronts face local -Z, so the inward heading is atan2(x, z) — the
    # negated form pointed everyone away from the flames (adhoc #291).
    assert SCENE.count("Math.atan2(offset.x, offset.z)") >= 1
    assert SCENE.count("Math.atan2(seat.x, seat.z)") >= 1
    assert "Math.atan2(-offset.x, -offset.z)" not in SCENE
    assert "Math.atan2(-seat.x, -seat.z)" not in SCENE
    # Idle/returning live members take the empty tail benches.
    assert "campfire.userData.memberFigureCount" in SCENE

    nodes = SCENE[
        SCENE.index("  function updateNetworkNodes"):
        SCENE.index("  function updateFederatedInstances")
    ]
    bots = SCENE[
        SCENE.index("  function updateBots"):
        SCENE.index("  function updateSystemCapacity")
    ]
    assert "createAvatar(" not in nodes
    assert "createAvatar(" not in bots
    assert "createMirrorServerCabinet" in nodes
    assert "createAgentRobot" in bots
    assert "nodeInfrastructure" in nodes
    assert "botAgents" in bots


def test_system_capacity_scene_combines_service_limits_and_database_rows():
    assert "system-capacity-infrastructure" in SCENE
    assert '"SYSTEM CAPACITY"' in SCENE
    assert "function systemCapacityMetricPairs" in SCENE
    assert "record?.usage" in SCENE
    assert "record?.limits" in SCENE
    assert "Object.prototype.hasOwnProperty.call(limits, key)" in SCENE
    assert "function updateSystemCapacity" in SCENE
    assert "currentUsage: metric.usage" in SCENE
    assert "configuredLimit: metric.limit" in SCENE
    assert "system-capacity-metrics-unavailable" in SCENE
    assert "system-capacity-database-tables" in SCENE
    assert 'systemCapacityPlatform.userData.officeFloorId = "infrastructure"' in SCENE
    assert "infrastructureFloor.add(systemCapacityPlatform);" in SCENE
    assert 'registerMovableObject("system-capacity-platform"' not in SCENE
    assert "Math.log1p(table.rowCount)" in SCENE
    # Every table the Worker counted is drawn, empty ones included, up to the
    # same ceiling the Worker itself enumerates.
    assert "rowCount >= 0" in SCENE
    assert ".slice(0, 256)" in SCENE
    assert "visibleTableCount" in SCENE
    assert "system-capacity-row-count:" in SCENE
    # The table name is printed on the bar's top face only; the old upright
    # label behind each bar was removed.
    assert "system-capacity-table-name:" not in SCENE


def test_infrastructure_floor_has_opt_in_local_redacted_console_display():
    assert '"forkmesh-infrastructure-local-console"' in SCENE
    assert '"infrastructure-console-switch"' in SCENE
    assert '`LOCAL CONSOLE · ${enabled ? "STREAMING" : "OFF"}`' in SCENE
    assert "THIS SCREEN ONLY · BOUNDED + REDACTED · NOT SENT OR SAVED" in SCENE
    assert "setInfrastructureConsoleLogs" in SCENE
    assert "setInfrastructureConsoleEnabled(enabled)" in APP
    assert "createInfrastructureConsoleCapture" in APP
    assert "sanitizeInfrastructureConsoleText" in APP
    assert 'window.addEventListener("unhandledrejection"' in APP
    assert 'window.removeEventListener("unhandledrejection"' in APP
    assert "this.infrastructureConsoleCapture.stop();" in APP
    assert "INFRASTRUCTURE_CONSOLE_MAX_ENTRIES = 40" in APP
    assert "Bearer [redacted]" in APP
    assert "[redacted-jwt]" in APP
    assert "metricsAvailable" in SCENE
    assert "metricsSignature" in SCENE
    assert "metricsSignature === signature" in SCENE
    assert "updateSystemCapacity," in SCENE


def test_system_capacity_tables_open_a_sortable_scrollable_panel():
    assert '"system-capacity-table"' in SCENE
    assert "onSystemCapacityTableSelect" in SCENE
    assert "removeInteractiveObject(interactive, existing)" in SCENE
    assert "onSystemCapacityTableSelect: (table) =>" in APP
    assert "openSystemCapacityTables(table)" in APP
    assert "sortSystemCapacityTables(key)" in APP
    assert "renderSystemCapacityTables()" in APP
    assert 'data-world-capacity-sort="name"' in APP
    assert 'data-world-capacity-sort="rowCount"' in APP
    assert '[data-world-capacity-sort]' in APP
    assert 'detail.dataset.openLandmark !== "system-capacity-tables"' in APP
    assert ".world-capacity-scroll" in CSS
    assert ".world-capacity-table thead th" in CSS
    assert "position: sticky" in CSS


def test_detail_panels_resize_from_their_left_border_and_never_drift():
    # The left border is a real drag grip, and the chosen width is a
    # device-local preference floored by each panel's base width.
    assert 'data-world-detail-resize' in APP
    assert 'role="separator"' in APP
    assert 'aria-orientation="vertical"' in APP
    assert "bindDetailResize()" in APP
    assert 'localStorage.setItem(DETAIL_WIDTH_KEY' in APP
    assert '"--world-detail-user-width"' in APP
    assert "anchorRight - event.clientX" in APP
    assert ".world-detail-resize" in CSS
    assert '.world-detail[data-open="true"] ~ .world-detail-resize' in CSS
    assert "--world-detail-width: min(" in CSS
    assert "max(var(--world-detail-user-width), var(--world-detail-base-width))" in CSS
    assert (
        '.fm-world:has(.world-detail[data-repository-review="true"])' in CSS
    )
    # A long unbreakable title must not widen the header past the panel: that
    # pushes the close button out of view, and focusing it scrolls the clipped
    # panel sideways into blank space with the text cut off.
    assert "grid-template-columns: minmax(0, 1fr) auto;" in CSS
    assert "overflow-wrap: anywhere;" in CSS
    assert "detail.scrollLeft = 0;" in APP
    assert "?.focus({ preventScroll: true });" in APP
    assert "scrollIntoView({ block: \"center\" })" not in APP


def test_world_lighting_supports_local_auto_day_night_and_brightness_controls():
    assert "DAYLIGHT_ENVIRONMENT" in SCENE
    assert "function updateWorldEnvironment" in SCENE
    assert "function setLightLevel" in SCENE
    assert "function setDaylightMode" in SCENE
    assert 'daylightMode === "day"' in SCENE
    assert 'daylightMode === "night"' in SCENE
    assert "worldSky.setDaylightMinute?.(minuteOfDay, easedDaylight)" in SCENE
    assert "lightLevel / LIGHT_LEVEL_DEFAULT" in SCENE
    assert "setLightLevel," in SCENE
    assert "getEnvironmentState" in SCENE
    assert "updateWorldEnvironment(Date.now())" not in SCENE
    assert "sharedEnvironmentState" not in SCENE
    assert "worldClock" not in SCENE
    assert "setClockOffset" not in SCENE
    assert "regionalOffset" not in SCENE
    assert "utcOffsetHours" not in SCENE
    assert "Math.floor(now / 30000)" not in SCENE
    assert "LOCAL_ENVIRONMENT_OVERLAYS" in SCENE
    assert "data-world-light-level" in APP
    assert "data-world-light-level-output" in APP
    assert 'data-world-daylight-mode="auto"' in APP
    assert 'data-world-daylight-mode="day"' in APP
    assert 'data-world-daylight-mode="night"' in APP
    assert "this.settings.daylightMode = mode" in APP
    assert "this.world?.setDaylightMode?.(mode)" in APP
    assert "this.settings.lightLevel = next" in APP
    assert "this.world?.setLightLevel(next)" in APP
    assert "writeJSON(localStorage, SETTINGS_KEY, {" in APP
    assert "lightLevel" not in APP[
        APP.index("function publicIdentity"):APP.index("function presenceBrowser")
    ]
    for overlay in ("rain", "snow", "winter", "cyberpunk", '"low-light"'):
        assert overlay in SCENE


def test_world_right_rail_reveals_as_one_fixed_square_shortcut_column():
    assert "data-world-right-rail" in APP
    assert 'data-expanded="false"' in APP
    assert "setWorldRightRailExpanded(expanded)" in APP
    assert "rail.dataset.expanded = String(active)" in APP
    assert "data-world-map-toggle" not in APP
    assert 'map.classList.toggle("is-expanded"' not in APP
    assert ".world-right-rail[data-expanded=\"false\"]" in CSS
    fixed_rail = CSS.rsplit("/* Fixed launcher geometry.", 1)[1]
    assert "width: 48px;" in fixed_rail
    assert ".world-map-button," in fixed_rail
    assert ".world-share-view-button," in fixed_rail
    assert ".world-remember-view" in fixed_rail
    for token in (
        "--primer-canvas-default:",
        "--primer-canvas-subtle:",
        "--primer-control-bg:",
        "--primer-control-hover:",
        "--primer-border-default:",
        "--primer-fg-default:",
        "--primer-fg-muted:",
        "--primer-accent-fg:",
        "--primer-accent-subtle:",
    ):
        assert token in CSS
    assert "var(--primer-border-default)" in fixed_rail
    assert "var(--primer-control-bg)" in fixed_rail
    assert "var(--primer-control-hover)" in fixed_rail
    assert "border-radius: 6px;" in fixed_rail
    assert "aside:not(.world-right-rail)" in CSS


def test_world_movement_speed_is_adjustable_and_keyboard_input_is_immediate():
    # Scene exposes speed tuning, but digital input has no configurable ramp.
    assert "function setMovementTuning" in SCENE
    assert "setMovementTuning," in SCENE
    assert "let moveSpeedScale = 1" in SCENE
    assert "PLAYER_MAX_SPEED * moveSpeedScale" in SCENE
    assert "input.keyboardActive" in SCENE
    assert "? topSpeed" in SCENE
    assert "moveAccelScale" not in SCENE
    # App retains speed control and explains immediate keyboard response.
    assert "data-world-move-speed" in APP
    assert "data-world-move-speed-output" in APP
    assert "this.settings.moveSpeed = next" in APP
    assert "this.world?.setMovementTuning?.(this.movementTuning())" in APP
    assert "moveSpeed: WORLD_MOVE_SPEED_DEFAULT" in APP
    assert "responds at the selected speed on its first frame" in APP


def test_double_clicking_the_ground_dashes_the_avatar_to_that_point():
    # Double-click travel raycasts the current space's floor plane (so it works
    # on elevated spaces too), clamps inside the world radius, and runs there at
    # a dash speed well above the walking cap instead of teleporting.
    for contract in (
        "const PLAYER_DASH_SPEED = 48",
        "const PLAYER_DASH_ARRIVE_DISTANCE = 0.3",
        "function groundPointAt",
        "groundPlane.constant = -currentFloorY",
        "raycaster.ray.intersectPlane(",
        "function handleDoubleClick",
        'addEventListener("dblclick", handleDoubleClick)',
        'removeEventListener("dblclick", handleDoubleClick)',
        "dashTarget = point",
        "PLAYER_DASH_SPEED * moveSpeedScale * delta",
        "} else if (dashTarget) {",
        "function cancelDash",
    ):
        assert contract in SCENE
    assert "const PLAYER_MAX_SPEED = 13" in SCENE
    # A drag or pinch that happens to end in a double-click must not dash, and
    # manual input, teleports, focus clears and blur all cancel a running dash.
    assert "lastGestureDragged = suppressTap" in SCENE
    assert "if (lastGestureDragged) return" in SCENE
    dash_cancels = SCENE.count("cancelDash()")
    assert dash_cancels >= 8, dash_cancels
    walk = SCENE[
        SCENE.index("  function walkPlayer"):
        SCENE.index("  function updateRemotePlayers")
    ]
    assert "cancelDash();" in walk
    blur = SCENE[
        SCENE.index("  function handleWindowBlur"):
        SCENE.index('  renderer.domElement.addEventListener("pointerdown"')
    ]
    assert "cancelDash();" in blur


def test_qt_main_navigation_does_not_open_the_world_root():
    assert 'setToolTip("Open ForkMesh World in your browser")' not in QT_CHAT
    assert "[this] { openServerWebsite(m_activeServer); }" not in QT_CHAT
    assert "m_worldNavButton" not in QT_CHAT
    assert "m_relayOpenButton" not in QT_CHAT
    assert "Non-custodial payout address" in QT_CHAT
    assert "Never enter a private key or recovery" in QT_CHAT


def test_approved_federated_instances_render_without_private_relay_material():
    assert 'this.fetchJSON("/api/world/instances"' in APP
    assert "normalizeFederatedInstances" in APP
    assert "function updateFederatedInstances" in SCENE
    assert "approved-federated-instances" in SCENE
    assert 'const district = landmarkObjects.get("fediverse")' in SCENE
    assert 'landmarkObjects.get("routing")' not in SCENE
    assert "instance?.approved === true" in SCENE


def test_cloudflare_setup_deep_link_carries_no_cloudflare_secret():
    assert 'url.scheme() != QLatin1String("forkmesh")' in QT_MAIN
    assert 'url.host() != QLatin1String("control")' in QT_MAIN
    assert 'url.path() != QLatin1String("/cloudflare")' in QT_MAIN
    assert "isCloudflareSetupLink" in QT_MAIN
    assert "prohibited.match(item.first)" in QT_MAIN
    assert "openCloudflareSetupFromSystemLink" in QT_CONTROL
    assert "m_cloudflareTokenEdit->setFocus" in QT_CONTROL
    assert 'query.queryItemValue(name, QUrl::FullyDecoded)' in QT_CONTROL
    assert "activationTarget.toUtf8()" in QT_SINGLE_INSTANCE
    assert '<h2 id="cloudflare">Cloudflare setup stays local</h2>' in QT_DOCS
    assert "bounded public topology only" in QT_DOCS
    assert "token-, password-, key-, and" in QT_DOCS
    assert 'Exec="$BIN" %u' in LINUX_DESKTOP_INSTALLER
    assert "MimeType=x-scheme-handler/forkmesh;" in LINUX_DESKTOP_INSTALLER
    assert "Exec=forkmesh %u" in APPIMAGE_PACKAGER
    assert "MimeType=x-scheme-handler/forkmesh;" in APPIMAGE_PACKAGER
    assert "CFBundleURLTypes" in MACOS_PACKAGER
    assert "CFBundleURLSchemes:0 string forkmesh" in MACOS_PACKAGER
    assert 'Software\\Classes\\forkmesh' in WINDOWS_PACKAGER
    assert '"URL Protocol" ""' in WINDOWS_PACKAGER


def test_support_center_is_not_rendered_in_the_world():
    assert 'id: "support"' not in DATA
    assert "Project Support Center" not in DATA
    assert "function createSupportCenter" not in SCENE
    assert "SUPPORT CENTER" not in SCENE
    assert "supportPanelHTML()" not in APP
    # Supporting-member account routes remain available outside the removed
    # in-World landmark.
    assert "https://www.patreon.com/16434219/join" in APP


def test_town_square_community_placement_is_context_only_and_collapsible():
    assert (
        '"/api/world/community-ads/placements?context=town-square"' in APP
    )
    assert "normalizeCommunityPlacement" in APP
    assert 'value?.tracking !== "none"' in APP
    assert "value?.behavioralTargeting !== false" in APP
    assert "value?.sensitiveTargeting !== false" in APP
    assert "value?.personalDataUsed !== false" in APP
    assert 'rel="sponsored noopener noreferrer"' in APP
    assert 'referrerpolicy="no-referrer"' in APP
    map_index = APP.index('class="world-map"')
    placement_index = APP.index('class="world-community-placement"')
    activity_index = APP.index('class="world-fediverse-activity"')
    assert map_index < placement_index < activity_index
    assert ".world-community-placement[hidden]" in CSS
    assert (
        ".fm-world:has(.world-detail[data-open=\"true\"]) "
        ".world-right-rail"
    ) in CSS
    assert ".world-community-placement,\n  .world-fediverse-activity" in CSS


def test_verified_fediverse_feedback_is_manual_pending_and_owner_confirmed():
    assert 'this.fetchJSON("/api/world/fediverse-mentions"' in APP
    assert "normalizeFediverseMentions" in APP
    assert "Nothing is filed automatically" in APP
    assert "data-world-fediverse-confirm" in APP
    assert "data-world-fediverse-followup" in APP
    assert "Pending is not created" in APP
    assert "Pending owner-node materialization" in APP
    assert "fediverse_mentions_api.record_verified(" in ENTRY
    assert "if not _ap_activity_is_public(activity):" in ENTRY
    assert "public_activity=True" in ENTRY
    mention = ENTRY[ENTRY.index("async def _ap_handle_repo_mention"):]
    mention = mention[:mention.index("\n\n\nasync def")]
    assert "_forkbot_enqueue_issue(" not in mention
    assert "_ap_mention_ai_intent(" not in mention
    assert "_ap_send_mention_reply(" not in mention
    assert "federated_mention_id" in ENTRY
    assert "fediverseMentionId" in ENTRY
    assert "fediverseMaterializations" in QT_ISSUES
    assert "the previous owner-node write may have committed successfully" in QT_ISSUES
    assert "FediverseMaterialization{mentionId, ev.id, inboxId}" in QT_ISSUES
    assert 'ackQuery.addQueryItem(\n                QStringLiteral("materialized")' in QT_ISSUES
    assert "world_fediverse_mentions" in SCHEMA
    assert "owner node confirms" in FEDIVERSE_REVIEW_DOC
    assert "Follow-up consent is off by default" in FEDIVERSE_REVIEW_DOC


def test_generated_world_score_is_opt_in_four_hour_and_redistributable():
    assert "function createProceduralWorldSoundtrack" in APP
    assert "durationMs: WORLD_SCORE_LOOP_MS" in APP
    assert "15 * 60 * 1000" in APP
    assert "CC0-1.0" in APP
    assert "Local score offset" in APP
    assert "loops independently of the UTC display" in APP
    assert "Audio never starts automatically" in APP
    assert "Original four-hour local procedural score" in DATA
    assert "world-soundtrack-license.md" in DATA


def test_linked_payout_page_has_a_truthful_non_custodial_notice():
    assert "Non-custodial payout wallet" in PAYOUTS
    assert "Wallet keys and recovery phrases stay" in PAYOUTS
    assert "User-owned funds" in PAYOUTS
    assert "community-pool funds" in PAYOUTS
    assert "pending rewards" in PAYOUTS
    assert "completed on-chain transfers" in PAYOUTS
    assert "first instance owner’s local Qt client" in PAYOUTS
    assert "community incentives, not investments" in PAYOUTS
    assert "guaranteed returns." in PAYOUTS
    assert "never its private key" in PAYOUTS


def test_forkbot_rolls_through_the_world_for_explicit_chat_interactions():
    # ForkBot is a rolling Town Square guide. Chat replies broadcast with
    # the fixed sender "forkbot" float over the droid. Entry remains immediate
    # and quiet until a visitor chooses to interact.
    assert 'const FORKBOT_PEER_ID = "forkbot";' in SCENE
    assert "const forkbot = new THREE.Group();" in SCENE
    assert 'forkbot.name = "forkbot-rolling-droid";' in SCENE
    assert "forkbot.userData.rollingBall = rollingBall;" in SCENE
    assert "function updateForkbot(delta, time)" in SCENE
    assert "updateForkbot(delta, time);" in SCENE
    assert "function greetForkbot(text)" in SCENE
    assert "greetForkbot," in SCENE
    # Bubbles addressed to the bot peer id resolve to the bot avatar.
    assert "peerId === FORKBOT_PEER_ID" in SCENE
    # Out-of-reach visitors still get greeted from wherever the bot got to.
    assert "const FORKBOT_GREETING_TIMEOUT_MS = 12000;" in SCENE
    assert (
        'const FORKBOT_GREETED_KEY = "forkmesh.world.forkbotGreeted.v1";'
        in APP
    )
    # Entering the World is immediate and does not force an unsolicited bot
    # greeting; ForkBot remains available for explicit chat interactions.
    assert APP.count("this.maybeGreetForkbot();") == 0
    assert 'this.world?.showChatBubble?.("forkbot", text);' in APP
    # The asking client mirrors ForkBot replies into the World embed, and
    # guests inside the public World room can talk to the bot (the endpoint
    # itself is sessionless).
    assert (
        "emitWorldChatBubble(plain.sender, plain.senderId, plain.text);"
        in DASHBOARD_CHAT
    )
    assert "if (!canJoinChat()) return;" in DASHBOARD_CHAT


def test_forkbot_mention_excites_the_droid_with_echo_screen_and_thinking_dots():
    # adhoc #369: as soon as anyone in the world mentions ForkBot in chat, the
    # droid gets excited and rushes over to the speaker. Its chest screen
    # echoes the line that mentioned it, then runs a thinking indicator until
    # the reply (broadcast with sender "forkbot") lands in the room.
    assert "const FORKBOT_EXCITED_SPEED = 5.6;" in SCENE
    assert "const FORKBOT_ECHO_MS = 2500;" in SCENE
    assert "const FORKBOT_THINKING_TIMEOUT_MS = 45000;" in SCENE
    assert "function drawForkbotScreen(context, canvas, state)" in SCENE
    assert "function exciteForkbot(peerId, text)" in SCENE
    assert "exciteForkbot," in SCENE
    # The screen repaints in place (echo, then animated dots) rather than
    # allocating a new texture per frame.
    assert "forkbotScreenTexture.needsUpdate = true;" in SCENE
    # ForkBot's own reply bubble is what stops the thinking indicator, with a
    # bounded fallback so an unavailable bot doesn't think forever.
    assert "if (avatar === forkbot && forkbotExcitement) {" in SCENE
    assert "waited >= FORKBOT_THINKING_TIMEOUT_MS" in SCENE
    # The world client matches the same mention pattern the chat clients
    # forward to /api/forkbot/chat, for the visitor's own line and for remote
    # peers' lines alike.
    assert (
        "const FORKBOT_MENTION_RE = /(?:^|[^A-Za-z0-9_-])@?forkbot\\b/i;"
        in APP
    )
    assert APP.count("this.world?.exciteForkbot?.(") == 1


def test_unified_chat_stream_does_not_duplicate_replayed_activity():
    # adhoc #25: a fresh load opened on a stack of activity cards — the chat
    # backlog the relay re-sends on connect, the first notification/event read,
    # and any mirror doorbell that landed while the scene was booting. Those
    # are all old news to someone who just arrived, so the stream only narrates
    # what happens after the World is up.
    assert "const ACTIVITY_JOIN_GRACE_MS = CHAT_BUBBLE_JOIN_GRACE_MS;" in APP
    assert (
        "this.activityNoticesEnabledAt = Date.now() + ACTIVITY_JOIN_GRACE_MS;"
        in APP
    )
    assert (
        "  activityNoticesSettled() {\n"
        "    return Date.now() >= this.activityNoticesEnabledAt;\n"
        "  }"
    ) in APP
    # Native chat already owns both message and system rows. The World accepts
    # those signals for avatar/unread state without drawing a second overlay.
    handler = APP[
        APP.index("  handleWorldChatMessage = (event) => {"):
        APP.index("\n  };", APP.index("  handleWorldChatMessage = (event) => {"))
    ]
    assert (
        'if (data.type === "forkmesh:world-activity") return;' in handler
    )
    assert "this.activityNotice(" not in handler
    # The first notification/event reads still seed the seen sets, so nothing
    # already waiting at load is announced on a later poll either.
    announce = APP[
        APP.index("  announceWorldNotifications() {"):
        APP.index("\n  }", APP.index("  announceWorldNotifications() {"))
    ]
    assert (
        "globalEvents.forEach((item) => this.seenWorldEvents.add(item.id));"
        in announce
    )
    assert (
        "if (announcements.length && this.activityNoticesSettled()) {" in announce
    )
    # A mirror doorbell during the grace still arms the scene effect and the
    # catalog refresh; only its narration is held back.
    push = APP[
        APP.index("  handleMirrorPush(message) {"):
        APP.index("\n  }", APP.index("  handleMirrorPush(message) {"))
    ]
    assert "if (this.activityNoticesSettled()) {" in push
    assert "this.world?.armMirrorPushEffect?.(" in push
    # Notices a visitor causes by acting are never gated.
    toast_start = APP.index("  toast(message, { priority = 0, lockMs = 0 } = {}) {")
    toast = APP[toast_start:APP.index("\n  }", toast_start)]
    assert "activityNoticesSettled" not in toast


def test_collapsed_chat_bar_shows_an_unread_count_excluding_own_lines():
    # adhoc #426: the docked CHAT bar showed only the newest line, so a visitor
    # walking around had no idea how much they had missed. It now carries an
    # unread pill that counts live remote lines and resets whenever chat opens.
    assert "data-world-chat-terminal-unread" in APP
    assert ".world-chat-terminal-unread {" in CSS
    # Replayed history and this browser's own messages never bump it, and
    # neither does the signed-in account talking from another tab or device
    # (`own`, which dashboard-chat.js resolves by account name).
    handler = APP[
        APP.index("  handleWorldChatMessage = (event) => {"):
        APP.index("\n  };", APP.index("  handleWorldChatMessage = (event) => {"))
    ]
    assert "if (data.history === true) return;" in handler
    assert (
        "if (data.self !== true && data.own !== true) "
        "this.bumpChatTerminalUnread();"
    ) in handler
    assert "own: isOwnChatLine(sender, senderId)," in DASHBOARD_CHAT
    # `self` stays a strict senderId match: only the browser's own line may
    # raise a bubble over the local avatar.
    assert "self: senderId === selfId," in DASHBOARD_CHAT
    # An already-open panel never accumulates, and both doors into the room
    # (the bar itself and the full overlay) clear the count.
    assert (
        'if (this.$("[data-world-chat-terminal]")?.open) return;' in APP
    )
    assert APP.count("this.clearChatTerminalUnread();") == 2
    # The mobile layout shrinks the collapsed bar to the word CHAT; the pill
    # gets room there rather than being clipped or hidden.
    assert ".world-chat-terminal-unread:not([hidden])" in CSS


def test_clicking_forkbot_opens_the_terminal_bar_with_a_mention_prefilled():
    # adhoc #284: ForkBot should drop a visitor into the small, docked CHAT
    # bar (not the full-screen chat overlay), with "@forkbot " already typed
    # in so they can start chatting immediately.
    assert 'onForkbotChat: () => {\n          this.openChatTerminal("@forkbot ");' in APP
    terminal = APP[
        APP.index("  openChatTerminal("):
        APP.index("\n  }", APP.index("  openChatTerminal("))
    ]
    assert 'this.$("[data-world-chat-terminal]")' in terminal
    assert "this.closeWorldChat();" in terminal
    assert "details.open = true;" in terminal
    assert '"forkmesh:chat-prefill"' in terminal
    assert 'new MessageEvent("message"' in terminal
    # The native controller listens for that message and fills + focuses its
    # composer without a nested document.
    assert 'data.type !== "forkmesh:chat-prefill"' in DASHBOARD_CHAT
    assert "input.focus();" in DASHBOARD_CHAT


def test_signed_in_visitors_keep_their_account_name_for_every_peer():
    # adhoc #276: a world ticket is the only account proof peers receive, so
    # applying one must never be gated on the optional activity accounting that
    # ships in the same response — a browser that keeps no ticket rejoins as an
    # anonymous "Guest ####", which also silences its chat bubbles (they are
    # matched to an avatar by display name).
    identity = APP[
        APP.index("  applyWorldTicketIdentity(ticket) {"):
        APP.index("\n  applyWorldActivityTicket(ticket)")
    ]
    assert 'ticket?.authenticated !== true' in identity
    assert 'ticket.accountStatus === "Guest"' in identity
    assert "this.worldTicket = String(ticket.ticket || \"\");" in identity
    assert "applyWorldActivityTicket" not in identity

    refresh = APP[
        APP.index("  async refreshWorldTicket() {"):
        APP.index("\n  startWorldTicketRefresh()")
    ]
    assert "if (this.applyWorldTicketIdentity(ticket)) {" in refresh
    assert "this.applyWorldActivityTicket(ticket);" in refresh
    # A ticket that failed during bootstrap left the visitor stranded under the
    # placeholder guest name for the rest of the session; a later ticket now
    # repairs the identity and republishes it.
    assert "this.identity.name !== previousName" in refresh
    assert 'this.sendPresence({ type: "presence" });' in refresh
    # Cookie-authenticated browsers hold no bearer token in localStorage.
    assert (
        "if (!readSession()?.sessionToken && !this.sessionAuthenticated) {"
        in refresh
    )
    assert APP.count("this.clearWorldTicketIdentity();") >= 3

    connect = APP[
        APP.index("  async connectPresence() {"):
        APP.index("socket = new WebSocket(socketURL.href)")
    ]
    assert (
        "(readSession()?.sessionToken || this.sessionAuthenticated) &&" in connect
    )
    # Never present an expired ticket: the relay drops the claim silently.
    assert "if (this.worldTicket && this.worldTicketExpires <= Date.now())" in connect


def test_world_guests_chat_under_the_name_their_avatar_wears():
    # The embedded chat is matched to an avatar by display name, so a signed-out
    # visitor has to speak as the same "Guest ####" the World shows above them.
    assert 'const GUEST_ID_KEY = "forkmesh.world.guestId.v1";' in APP
    assert 'const WORLD_GUEST_ID_KEY = "forkmesh.world.guestId.v1";' in DASHBOARD_CHAT
    assert "worldGuestPresenceName() ||" in DASHBOARD_CHAT
    guest_name = DASHBOARD_CHAT[
        DASHBOARD_CHAT.index("  function worldGuestPresenceName() {"):
        DASHBOARD_CHAT.index("\n  function displayName()")
    ]
    assert 'requestedParams.get("worldEmbed") !== "1"' in guest_name
    # Same derivation as world.js hashSuffix()/accountIdentity().
    assert "hash = (Math.imul(hash, 31) + char.charCodeAt(0)) >>> 0;" in guest_name
    assert "String(hash % 10000).padStart(4, \"0\")" in guest_name
    assert "`guest:${guest}`" in guest_name
    assert "`guest:${guestId()}`" in APP


def test_world_updates_apply_layout_live_and_ask_before_code_refresh():
    # Layout is data and can be applied to the active scene. A deployed code
    # revision instead raises an explicit refresh action and never reloads the
    # visitor out from under an active walk.
    assert "const WORLD_UPDATE_CHECK_INTERVAL_MS = 5 * 60 * 1000;" in APP
    assert "startUpdateWatch()" in APP
    assert "startWorldLayoutWatch()" in APP
    assert "this.applyFetchedWorldLayout(layout);" in APP
    assert "async checkForWorldUpdate()" in APP
    update = APP[
        APP.index("  async checkForWorldUpdate()"):
        APP.index("\n  startDiagnostics()", APP.index("  async checkForWorldUpdate()"))
    ]
    assert 'this.renderDeployStatus("ready");' in update
    assert "location.reload()" not in update
    assert "WORLD_UPDATE_RELOAD_DELAY_MS" not in APP
    assert "data-world-update-refresh" in APP
    assert "refreshWorldForUpdate" in APP
    deploy = APP[
        APP.index("  renderDeployStatus(state)"):
        APP.index("\n  async checkDeployStatus()", APP.index("  renderDeployStatus(state)"))
    ]
    assert 'this.$("[data-world-update-notice]")' in deploy
    assert 'state === "deploying"' in deploy
    assert 'state === "ready"' in deploy
    assert "refresh.hidden = true;" in deploy
    assert "refresh.hidden = false;" in deploy
    # Bounded: hidden tabs never poll and visibility bursts are throttled.
    assert (
        "if (this.destroyed || this.updateReloadPending || document.hidden) "
        "return;" in APP
    )
    assert "const WORLD_UPDATE_CHECK_MIN_GAP_MS = 60 * 1000;" in APP
    assert "window.clearInterval(this.updateCheckTimer);" in APP


def test_arrival_grid_sign_is_a_front_plaque_with_visit_counters():
    # The Arrival Grid sign stands at the grid's front edge as a physical
    # plaque (no more floating sprite) and shows aggregate visit counters:
    # all-time total, today vs the same time yesterday, and the past hour vs
    # the same hour yesterday.
    assert "function makeArrivalPlaque" in SCENE
    assert "function arrivalPlaqueTexture" in SCENE
    assert 'plaque.name = "world-arrival-plaque"' in SCENE
    assert "arrivalPlaque.position.set(0, 0, 15.2)" in SCENE
    assert "arrivalPlaque.rotation.y = Math.PI" in SCENE
    assert "arrivalLabel" not in SCENE
    assert '"UNIQUE VISITORS"' in SCENE
    assert '"ALL TIME · APPROXIMATE · NO RAW IP STORED"' in SCENE
    assert '"TODAY"' in SCENE
    assert '"PAST HOUR"' in SCENE
    assert "function updateArrivalStats" in SCENE
    assert "yesterdaySameTime" in SCENE
    assert "pastHourYesterday" in SCENE
    # The client feeds the plaque from the public aggregate endpoint; the
    # response carries counters only, so it is shared and cacheable.
    assert '"/api/world/visitors"' in APP
    assert "updateArrivalStats" in APP
    assert '"/api/world/visitors", "/api/world/visitors/"' in ENTRY
    assert "record_world_visit" in ENTRY


def test_world_presence_socket_has_a_bounded_open_deadline_and_retirement():
    connect = APP[
        APP.index("  async connectPresence() {"):
        APP.index("  setPresenceState(", APP.index("  async connectPresence() {"))
    ]
    assert "this.socketRecovery.adopt(socket" in connect
    assert "this.socketRecovery.markOpen(socket)" in connect
    assert "this.socketRecovery.retire(socket)" in connect
    assert "this.socketRecovery.scheduleReconnect(" in APP
