#!/usr/bin/env python3
"""Static contracts for the playable ForkMesh World frontend."""

from pathlib import Path
import json
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
SECURITY_LATEST = json.loads(
    (PUBLIC / "security" / "latest.json").read_text(encoding="utf-8")
)
FEDIVERSE_DIRECTORY = json.loads(
    (WORLD / "fediverse-directory.json").read_text(encoding="utf-8")
)
BOT_DIRECTORY = json.loads(
    (WORLD / "bot-directory.json").read_text(encoding="utf-8")
)


def test_world_is_an_immediate_accessible_game_shell():
    assert '<forkmesh-world data-world-mode="public">' in INDEX
    assert 'class="world-static-fallback"' in INDEX
    assert 'href="#world-information"' in INDEX
    assert 'id="world-information" tabindex="-1"' in INDEX
    assert 'class="world-information-anchor"' in APP
    assert 'id="world-information"' in APP
    assert ".world-information-anchor:focus" in CSS
    assert 'src="/world/world.js"' in INDEX
    assert 'href="/world/world.css"' in INDEX
    assert "JavaScript and WebGL enhance this page" in INDEX


def test_world_contains_the_initial_city_districts_without_a_clock():
    for landmark in (
        "information",
        "fountain",
        "repositories",
        "routing",
        "organizations",
        "fediverse",
        "security",
        "launchpad",
        "support",
    ):
        assert f'id: "{landmark}"' in DATA
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


def test_scene_builds_playable_landmarks_and_badged_avatars():
    for builder in (
        "createAvatar",
        "createInformationBooth",
        "createFountain",
        "createRepositoryDistrict",
        "createRoutingStation",
        "createOrganizationQuarter",
        "createFediverseCenter",
        "createSecurityWorkshop",
        "createLaunchpad",
        "createSupportCenter",
        "createSkyOffice",
        "createOtherWorlds",
    ):
        assert f"function {builder}" in SCENE
    assert "badgeTexture" in SCENE
    assert "identity.flag" in SCENE
    assert "identity.browser" in SCENE
    assert "identity.os" in SCENE
    assert "identity.name" in SCENE
    assert "world-shirt-account" in APP
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
    assert "You entered as a guest immediately" in DATA
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
    assert "userAgent:" not in APP
    assert "identity.browser" in SCENE
    assert "identity.os" in SCENE
    assert "presenceBrowser(" in APP
    assert "presenceOS(" in APP


def test_avatar_chest_activity_country_shirt_and_input_state_are_privacy_safe():
    for contract in (
        "FIRST SEEN THIS SESSION",
        "PUBLIC URL VISITS",
        "identity.activityCategory",
        "identity.firstVisitAge",
        "identity.visitCount",
        "function countryShirtTexture",
        "identity.countryCode",
        'bracelet.name = "mouse-activity-bracelet"',
        "function syncAvatarActivity",
        "function animateAvatarActivity",
        'avatar.userData.accountStatus === "Guest"',
        "inactiveFor >= 12000",
        'avatar.userData.accountStatus === "Guest"',
    ):
        assert contract in SCENE
    assert "inputActive:" in APP
    assert "visitCount:" in APP
    assert "firstVisitAge:" in APP
    assert "settings.privacy.activity && identity.inputActive === true" in APP
    assert "countryCode:" in APP
    assert "userAgent:" not in APP
    assert "url:" not in APP[APP.index("  sendPresence(message) {"):APP.index(
        "\n  receivePresence(message)", APP.index("  sendPresence(message) {")
    )]


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
    assert 'this.fetchJSON("/api/repositories")' in APP
    # The Member Lounge (adhoc #228) reads the same public roster the chat
    # page uses — profile names only, fetched without credentials. Anything
    # beyond that anonymous directory stays off-limits to the world client.
    assert (
        'this.fetchJSON("/api/accounts/users", {\n          auth: false,'
        in APP
    )
    assert APP.count("/api/accounts/users") == 1
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
    assert "if (!document.hidden && readSession()?.sessionToken)" in APP
    assert "window.clearInterval(this.worldTicketTimer)" in APP
    assert "session?.organizationAdmin" not in APP
    assert "session?.accountTier" not in APP


def test_world_hud_counts_authoritative_connected_visitors_without_duplicates():
    assert 'data-world-players>1</strong><span>in world</span>' in APP
    assert "const socketOnline =" in APP
    assert "1 + this.remotePlayers.size + (socketOnline ? 0 : this.localPeers.size)" in APP
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
        'ownSpace === "town-square"',
        "player.space === ownSpace",
        ") < ARRIVAL_CLEARANCE",
        "(!this.spawnSelected || spawnBlocked)",
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


def test_avatar_faces_keyboard_travel_direction_and_intro_can_stay_dismissed():
    assert "player.rotation.y = Math.atan2(-movement.x, -movement.z)" in SCENE
    assert "toTarget" not in SCENE
    assert "player.rotation.y = 0" in SCENE
    assert "INTRO_DISMISSED_KEY" in APP
    assert "data-world-arrival-dismiss" in APP
    assert 'localStorage.setItem(INTRO_DISMISSED_KEY, "1")' in APP
    assert 'introDismissed() ? "hidden" : ""' in APP
    assert ".world-arrival-card[hidden]" in CSS


def test_world_has_consent_aware_activity_events_workshops_and_media():
    for landmark in ("events", "neighborhood", "workshops", "broadcast"):
        assert f'id: "{landmark}"' in DATA
    for builder in (
        "createCommunityStage",
        "createNeighborhood",
        "createCodeWorkshops",
        "createBroadcastGarden",
    ):
        assert f"function {builder}" in SCENE
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
    assert "travelToSpace" in SCENE
    assert "utcClock" not in APP
    assert "worldClock" not in SCENE
    assert "setClockOffset" not in SCENE
    assert "utcOffsetHours" not in SCENE
    assert 'data-world-travel="sky-campus"' in APP
    assert 'data-world-travel="code-planet"' in APP
    assert 'data-world-travel="space-station"' in APP
    assert 'data-world-travel="planet-atlas"' in APP
    for destination in (
        "SPACE STATION",
        "CODE PLANET",
        "GARDEN CAMPUS",
        "COMMUNITY PLANETS",
        "functional-world-destinations",
    ):
        assert destination in SCENE
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
    assert "/dashboard/chat?space=sky-campus" in APP
    assert "SPACE_CHANNELS" in DASHBOARD_CHAT
    for channel in (
        "#world-sky-campus",
        "#world-space-station",
        "#world-code-planet",
        "#world-organization-region",
        "#world-planet-atlas",
    ):
        assert channel in DASHBOARD_CHAT
    assert "entry.channel === CHANNEL" in DASHBOARD_CHAT
    assert "startRewardPolling" in APP
    assert "confirmed community reward event" in APP
    assert "60000" in APP
    assert "function updateNeighborhoodHomes" in SCENE
    assert "Empty houses reveal nothing about offline users" in APP
    assert "data-world-run-workshop" in APP
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


def test_chat_opens_inside_the_world_without_popup_permission():
    assert "data-world-chat-open" in APP
    assert "data-world-chat-frame" in APP
    assert 'role="dialog"' in APP
    assert 'sandbox="allow-forms allow-same-origin allow-scripts"' in APP
    assert "allow-popups" not in APP
    assert "openWorldChat(" in APP
    assert "closeWorldChat()" in APP
    assert "destination.origin !== location.origin" in APP
    assert '["/dashboard/chat", "/dashboard/chat/"]' in APP
    assert 'destination.searchParams.set("worldEmbed", "1")' in APP
    assert "data-world-chat-terminal" in APP
    assert "data-world-chat-terminal-frame" in APP
    assert '"/dashboard/chat?worldEmbed=1"' in APP
    assert "world-chat-terminal" in CSS
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
    assert "WORLD_NOTIFICATION_POLL_MS = 30 * 1000" in APP
    assert "normalizeWorldNotifications" in APP
    assert "/api/notifications?node=${encodeURIComponent(" in APP
    assert 'this.fetchJSON("/api/world/events"' in APP
    assert 'this.postJSON("/api/notifications"' in APP
    assert "data-world-notification-count" in APP
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
    assert "Node scale reflects blob size" in APP
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
        "repository-relationship-lines",
    ):
        assert graph_feature in APP or graph_feature in SCENE
    for data_filter in ("frequency", "dependency", "security", "coverage"):
        assert f'data-world-repo-filter="{data_filter}"' in APP
    assert 'data-world-repo-filter="frequency" disabled' not in APP
    assert 'data-world-repo-filter="dependency" disabled' not in APP
    assert 'data-world-repo-filter="coverage" disabled' not in APP


def test_world_autoloads_the_live_catalog_attested_flagship_repository_map():
    assert 'owner: "forkmesh",\n  repo: "forkmesh"' in APP
    bootstrap = APP[
        APP.index("  async bootstrap() {"):
        APP.index("\n  hideLoading()", APP.index("  async bootstrap() {"))
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
    assert (
        fetch_map.index("await this.loadRepositoryPullRecords(base)")
        < fetch_map.index("await Promise.allSettled([")
    )
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
    assert "this.world.updateRepositoryGraph?.([], []);" in auto
    assert "requireComplete: true" in auto
    assert "expectedCommits: catalogCommits" in auto

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
    assert (
        'this.loadRepositoryMap(owner, name, { automatic: false });'
        in APP
    )
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
        "relationshipPairs",
        "forces",
        "repository-relationship-lines",
    ):
        assert graph_contract in SCENE
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
        "Worker stores only public payout addresses, unsigned intents, and "
        "finalized signatures"
    ) in DATA
    assert "community-pool signer stays in the first instance operator’s encrypted local Qt client" in DATA
    assert "Mainnet-beta · external signer" in DATA
    assert "Development instances may explicitly select a test network" in DATA
    assert "Visual coins are not guaranteed rewards or investments" in DATA
    assert "A clean scan is never presented as a guarantee" in DATA
    assert "platform administrators do not receive an automatic decryption path" in DATA
    assert "Raw IP addresses are never shown in the world" in DATA
    assert "does not eliminate endpoint, authorization, or operational risk" in DATA
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
    assert "world-quick-dock" in CSS
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
        "Math.exp(deltaPixels * 0.0015)",
        "getCameraState",
    ):
        assert contract in SCENE
    assert 'addEventListener("pointermove", handlePointerMove, {' in SCENE
    assert "passive: false" in SCENE
    assert 'aria-label="Movement and camera controls"' in APP
    assert "drag to rotate" in APP
    assert "click the plaza" not in APP
    assert "click on the plaza" not in DATA

    # Fine-pointer navigation keeps the cursor visible and rotates only while
    # the primary pointer is dragged. WASD stays camera-relative, while touch
    # arrows remain an independent fallback.
    for contract in (
        'dataset.cameraControl = "drag"',
        "function rotateCamera",
        "pointerLast.copy(pointerStart)",
        "setPointerCapture(event.pointerId)",
        "CAMERA_LOOK_SENSITIVITY",
        "movement.addScaledVector(forward, forwardInput)",
        "movement.addScaledVector(right, rightInput)",
        'if (touchKeys.has("KeyW")) movement.z -= 1',
    ):
        assert contract in SCENE
    assert "requestPointerLock" not in SCENE
    assert "pointerLockElement" not in SCENE
    assert "raycaster.intersectObject(ground" not in SCENE

    # Keyboard movement accelerates toward a named cap and resets to the base
    # from-rest speed as soon as movement input is released or focus is lost.
    # Speed and acceleration are scaled by per-device controls; an infinite
    # acceleration scale collapses the ramp to instant top speed.
    for contract in (
        "PLAYER_MAX_SPEED",
        "PLAYER_ACCELERATION",
        "keyboardMovementSpeed + PLAYER_ACCELERATION * moveAccelScale * delta",
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


def test_world_uses_nonhuman_infrastructure_a_member_lounge_and_city_grid():
    assert "const WORLD_RADIUS = 72" in SCENE
    assert "const WORLD_GROUND_RADIUS = 88" in SCENE
    assert "electric-mesh-city-block-grid" in SCENE
    assert "electricMeshConduit" in SCENE
    assert "electricMeshJunction" in SCENE
    assert "registered-user-lounge" in SCENE
    assert "registered contributors · recent activity glows" in SCENE
    for status in (
        "Registered",
        "Supporting member",
        "Mirror operator",
        "Organization admin",
    ):
        assert status in SCENE
    assert "useRegisteredLounge" in SCENE
    assert 'avatar.userData.loungeActivity === "recent"' in SCENE


def test_world_member_lounge_seats_directory_users_with_total_count():
    # The lounge is populated from the public users directory (adhoc #228):
    # registered accounts appear seated even when offline, and a sign at the
    # lounge front shows the total registered-user count.
    assert "function updateMemberLounge" in SCENE
    assert "memberCountSign" in SCENE
    assert "total registered users" in SCENE
    assert "MEMBER${total === 1" in SCENE
    assert "const loungeMembers = new Map();" in SCENE
    # world.js feeds it the public roster (no email/device material) and
    # dedupes accounts already rendered as live or opted-in idle avatars.
    assert '"/api/accounts/users"' in APP
    assert "syncMemberLounge" in APP
    assert "this.memberDirectory.length" in APP

    nodes = SCENE[
        SCENE.index("  function updateNetworkNodes"):
        SCENE.index("  function updateFederatedInstances")
    ]
    bots = SCENE[
        SCENE.index("  function updateBots"):
        SCENE.index("  function updateDurableObjects")
    ]
    assert "createAvatar(" not in nodes
    assert "createAvatar(" not in bots
    assert "createMirrorServerCabinet" in nodes
    assert "createAgentRobot" in bots
    assert "nodeInfrastructure" in nodes
    assert "botAgents" in bots


def test_durable_object_scene_requires_explicit_current_usage_and_limits():
    assert "durable-object-infrastructure" in SCENE
    assert "function durableObjectMetricPairs" in SCENE
    assert "record?.usage" in SCENE
    assert "record?.limits" in SCENE
    assert "Object.prototype.hasOwnProperty.call(limits, key)" in SCENE
    assert "function updateDurableObjects" in SCENE
    assert "currentUsage: metric.usage" in SCENE
    assert "configuredLimit: metric.limit" in SCENE
    assert "durable-object-metrics-unavailable" in SCENE
    assert "metricsAvailable" in SCENE
    assert "updateDurableObjects," in SCENE


def test_world_lighting_is_static_daylight_with_a_local_persisted_control():
    assert "DAYLIGHT_ENVIRONMENT" in SCENE
    assert "function updateWorldEnvironment" in SCENE
    assert "function setLightLevel" in SCENE
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
    assert "this.settings.lightLevel = next" in APP
    assert "this.world?.setLightLevel(next)" in APP
    assert "writeJSON(localStorage, SETTINGS_KEY, this.settings)" in APP
    assert "lightLevel" not in APP[
        APP.index("function publicIdentity"):APP.index("function presenceBrowser")
    ]
    for overlay in ("rain", "snow", "winter", "cyberpunk", '"low-light"'):
        assert overlay in SCENE


def test_world_movement_speed_and_acceleration_are_locally_adjustable():
    # Scene exposes a movement-tuning setter and scales the shared defaults by
    # per-device speed/acceleration factors, with Infinity meaning instant.
    assert "function setMovementTuning" in SCENE
    assert "setMovementTuning," in SCENE
    assert "let moveSpeedScale = 1" in SCENE
    assert "let moveAccelScale = 1" in SCENE
    assert "PLAYER_MAX_SPEED * moveSpeedScale" in SCENE
    assert "PLAYER_ACCELERATION * moveAccelScale * delta" in SCENE
    assert "moveAccelScale = Number.isFinite(numeric)" in SCENE
    # App renders local-controls sliders wired to persisted settings and pushes
    # the tuning (Infinity at the top acceleration position) into the scene.
    assert "data-world-move-speed" in APP
    assert "data-world-move-accel" in APP
    assert "data-world-move-speed-output" in APP
    assert "data-world-move-accel-output" in APP
    assert "this.settings.moveSpeed = next" in APP
    assert "this.settings.moveAccel = next" in APP
    assert "this.world?.setMovementTuning?.(this.movementTuning())" in APP
    assert "? Infinity" in APP
    assert "moveSpeed: WORLD_MOVE_SPEED_DEFAULT" in APP
    assert "moveAccel: WORLD_MOVE_ACCEL_DEFAULT" in APP


def test_qt_main_navigation_opens_the_world_root():
    assert 'setToolTip("Open ForkMesh World in your browser")' in QT_CHAT
    assert "[this] { openServerWebsite(m_activeServer); }" in QT_CHAT
    assert "Non-custodial payout address" in QT_CHAT
    assert "Never enter a private key or recovery" in QT_CHAT


def test_approved_federated_instances_render_without_private_relay_material():
    assert 'this.fetchJSON("/api/world/instances"' in APP
    assert "normalizeFederatedInstances" in APP
    assert "No approved federated instance has a publishable origin yet" in APP
    assert "fresh signed node health through an approved relay" in APP
    assert "Federation keys, signatures, tokens, wallets" in APP
    assert "function updateFederatedInstances" in SCENE
    assert "approved-federated-instances" in SCENE
    assert "instance?.approved === true" in SCENE


def test_information_booth_deep_link_carries_no_cloudflare_secret():
    exact = "forkmesh://control/cloudflare"
    assert exact in APP
    assert "The hosted World never accepts, proxies, or stores a Cloudflare API token" in APP
    assert 'type="password"' not in APP[APP.index("informationPanelHTML()"):APP.index("routingPanelHTML()")]
    assert exact in QT_MAIN
    assert "target == QLatin1String" in QT_MAIN
    assert "openCloudflareSetupFromSystemLink" in QT_CONTROL
    assert "m_cloudflareTokenEdit->setFocus" in QT_CONTROL
    assert "activationTarget.toUtf8()" in QT_SINGLE_INSTANCE
    assert '<h2 id="cloudflare">Cloudflare setup stays local</h2>' in QT_DOCS
    assert "carries no token or other secret in its URL" in QT_DOCS
    assert 'Exec="$BIN" %u' in LINUX_DESKTOP_INSTALLER
    assert "MimeType=x-scheme-handler/forkmesh;" in LINUX_DESKTOP_INSTALLER
    assert "Exec=forkmesh %u" in APPIMAGE_PACKAGER
    assert "MimeType=x-scheme-handler/forkmesh;" in APPIMAGE_PACKAGER
    assert "CFBundleURLTypes" in MACOS_PACKAGER
    assert "CFBundleURLSchemes:0 string forkmesh" in MACOS_PACKAGER
    assert 'Software\\Classes\\forkmesh' in WINDOWS_PACKAGER
    assert '"URL Protocol" ""' in WINDOWS_PACKAGER


def test_support_center_is_distinct_truthful_and_non_custodial():
    assert 'id: "support"' in DATA
    assert "Project Support Center" in DATA
    assert "function createSupportCenter" in SCENE
    assert "SUPPORT CENTER" in SCENE
    assert "https://www.patreon.com/16434219/join" in APP
    assert "mailto:founders@forkmesh.com" in APP
    assert "Voluntary project support · no financial return" in APP
    assert "does not buy governance dominance" in APP
    assert "separate from the Global Reward Pool" in APP


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


def test_world_updates_arrive_via_a_gentle_in_place_reload():
    # A deploy flips BUILD_REV on /api/version. The world notices on a slow
    # watcher, flushes the player's position, and reloads once behind a toast,
    # so the new build appears in place without anyone touching refresh.
    assert "const WORLD_UPDATE_CHECK_INTERVAL_MS = 5 * 60 * 1000;" in APP
    assert "startUpdateWatch()" in APP
    assert "async checkForWorldUpdate()" in APP
    assert (
        "window.setTimeout(() => location.reload(), "
        "WORLD_UPDATE_RELOAD_DELAY_MS)" in APP
    )
    # Gentle means the position survives: it is flushed before the reload so
    # the restored spawn puts the player exactly where they were.
    assert "this.captureWorldPosition(true);\n    this.toast(" in APP
    # Bounded: hidden tabs never poll, visibility bursts are throttled to one
    # request per minute, and one reload per revision prevents reload loops
    # behind a stale cache.
    assert (
        "if (this.destroyed || this.updateReloadPending || document.hidden) "
        "return;" in APP
    )
    assert "const WORLD_UPDATE_CHECK_MIN_GAP_MS = 60 * 1000;" in APP
    assert "sessionStorage.getItem(WORLD_UPDATE_RELOADED_REV_KEY)" in APP
    assert "window.clearInterval(this.updateCheckTimer);" in APP
