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
                "--experimental-default-type=module",
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
        "information",
        "fountain",
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
    assert "position: [45, 0, -27]" in DATA
    assert 'id: "visiting-office"' in DATA


def test_landmarks_are_spread_out_inside_the_repository_portal_perimeter():
    positions = {
        "information": (-40, 30),
        "fountain": (0, 0),
        "repositories": (35, 38),
        "office": (45, -27),
    }
    for landmark, (x, z) in positions.items():
        start = DATA.index(f'id: "{landmark}"')
        block = DATA[start: DATA.index("\n  },", start)]
        assert f"position: [{x}, 0, {z}]" in block

    perimeter_radius = 68
    noncentral = {
        landmark: position
        for landmark, position in positions.items()
        if landmark != "fountain"
    }
    assert all(
        math.hypot(x, z) <= perimeter_radius - 15
        for x, z in noncentral.values()
    )
    assert min(
        math.dist(left, right)
        for index, left in enumerate(noncentral.values())
        for right in list(noncentral.values())[index + 1:]
    ) >= 18
    assert "const REPOSITORY_EDGE_RADIUS = 68" in SCENE
    assert "const SERVER_CABINET_YARD_ORIGIN = Object.freeze([18, 0, 0])" in SCENE
    assert "const SYSTEM_CAPACITY_PLATFORM_POSITION = Object.freeze([8, 0, -27])" in SCENE


def test_static_world_fallback_links_to_chat():
    assert 'href="/chat"' in INDEX
    assert "Open ForkMesh chat" in INDEX


def test_scene_builds_playable_landmarks_and_badged_avatars():
    for builder in (
        "createAvatar",
        "createInformationBooth",
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
    assert 'this.fetchJSON("/api/repositories", { auth: hasSession })' in APP
    # The campfire circle (adhoc #228) reads the same public roster the chat
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
    assert "player.rotation.y = Math.PI" in SCENE
    assert "function arrivalFacingHeading(x, z, heading)" in SCENE
    assert "isArrivalGridPosition(x, z) ? Math.PI : heading" in SCENE
    assert "player.rotation.y = arrivalFacingHeading(x, z, heading)" in SCENE
    assert "avatar.userData.targetHeading = arrivalFacingHeading(" in SCENE
    assert "INTRO_DISMISSED_KEY" not in APP
    assert "data-world-arrival-dismiss" not in APP
    assert "world-arrival-card" not in APP
    assert "Entering ForkMesh World" not in APP
    assert "data-world-loading" not in APP
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
    assert "startRewardPolling" in APP
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
    assert "writeJSON(localStorage, SETTINGS_KEY, this.settings)" in APP

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


def test_chat_opens_through_the_spatial_forkmesh_office_and_terminal():
    assert "data-world-office-focus" in APP
    assert "data-world-office-enter" in APP
    assert "data-world-office-chat" in APP
    assert "data-world-office-frame" in APP
    assert "Visit ForkMesh Office" in APP
    assert "ForkMesh Office chat" in APP
    assert 'office: "visiting-office"' in APP
    assert "/chat?embed=office" not in APP
    assert 'sandbox="allow-forms allow-same-origin allow-scripts"' in APP
    assert "allow-popups" not in APP
    # Two doors to the same encrypted chat: the spatial Office walk-in (above)
    # and the docked chat terminal panel. The Office deliberately does not
    # replace the terminal, so both entrances are asserted here.
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


def test_mobile_world_chat_composer_stays_above_safe_area_and_terminal_bars():
    assert (
        "var(--safe-bottom) + var(--world-diagnostics-height) + 8px"
        in CSS
    )
    open_chat = APP[
        APP.index("  openWorldChat("):
        APP.index("\n  loadChatTerminalFrame()", APP.index("  openWorldChat("))
    ]
    assert (
        'this.$("[data-world-chat-terminal]")?.removeAttribute("open")'
        in open_chat
    )
    assert (
        'this.$("[data-world-diagnostics]")?.removeAttribute("open")'
        in open_chat
    )
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
    assert "byte share to arc width and directory depth to concentric rings" in APP
    assert "updateRepositoryCatalog" in APP
    assert "updateRepositoryCatalog" in SCENE
    assert "updateRepositorySizeMap" in APP
    assert "updateRepositorySizeMap" in SCENE
    assert "new THREE.ExtrudeGeometry" in SCENE
    assert "active.sizes" in APP
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

    scene_mode_start = SCENE.index("  function setCameraMode(mode) {")
    scene_mode = SCENE[
        scene_mode_start:
        SCENE.index("\n  function focusRepositoryPortal(", scene_mode_start)
    ]
    assert 'player.visible = false' in scene_mode
    assert 'player.visible = true' in scene_mode
    assert "renderer.domElement.dataset.cameraMode = cameraMode" in scene_mode

    repository_entry_start = SCENE.index(
        "  function enterRepositoryFirstPerson(",
    )
    repository_entry = SCENE[
        repository_entry_start:
        SCENE.index("\n  function clearFocus()", repository_entry_start)
    ]
    assert 'setCameraMode("first-person")' in repository_entry
    assert "player.position.set(" in repository_entry
    assert "onMovement({" in repository_entry

    reveal_start = APP.index("  revealRepositoryScene() {")
    reveal = APP[
        reveal_start:
        APP.index("\n  selectRepositoryPortal(", reveal_start)
    ]
    assert "this.world?.enterRepositoryFirstPerson?.(" in reveal
    assert 'this.world?.setCameraMode?.("third-person")' in reveal
    assert "this.syncWorldCameraModeButton();" in reveal
    assert "enterRepositoryFirstPerson," in SCENE
    assert "setCameraMode," in SCENE
    assert "getCameraState:" in SCENE


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
    assert "a clean scan is not a guarantee" in APP
    assert (
        "platform administrators do not automatically receive those recipient "
        "private keys"
    ) in PRIVACY
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
        "Math.exp(deltaPixels * 0.0015)",
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


def test_world_overview_zoom_keeps_the_finite_ground_inside_camera_depth():
    assert "const WORLD_GROUND_RADIUS = 88;" in SCENE
    assert "const CAMERA_OFFSET = [17, 16, 21];" in SCENE
    assert "const CAMERA_ZOOM_MIN = 0.12;" in SCENE
    assert "const CAMERA_ZOOM_MAX = 3.2;" in SCENE
    assert "const CAMERA_FAR_PLANE = 240;" in SCENE
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
    assert camera_distance * 3.2 + 88 < 240

    zoom_fog_start = SCENE.index("  function zoomFogMultiplier() {")
    zoom_fog = SCENE[
        zoom_fog_start:
        SCENE.index("\n  function setLightLevel(", zoom_fog_start)
    ]
    assert "CAMERA_ZOOM_MAX - 1" in zoom_fog
    assert "(cameraZoom - 1) / 7" not in zoom_fog


def test_world_uses_nonhuman_infrastructure_without_the_world_spanning_grid():
    assert "const WORLD_RADIUS = 72" in SCENE
    assert "const WORLD_GROUND_RADIUS = 88" in SCENE
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


def test_active_leaderboard_position_and_orientation():
    assert (
        "const ACTIVE_LEADERBOARD_POSITION = Object.freeze([-11.5, 0, 25]);"
        in SCENE
    )
    assert (
        "activeLeaderboardSign.position.set(...ACTIVE_LEADERBOARD_POSITION)"
        in SCENE
    )
    assert "-ACTIVE_LEADERBOARD_POSITION[0]" in SCENE
    assert "-ACTIVE_LEADERBOARD_POSITION[2]" in SCENE


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
    assert "pointInsideBounds(x, z, durableBounds)" in SCENE
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
    # adds one extra bench that always stays open for the next guest.
    assert "function rebuildCampfireCircle" in SCENE
    assert '"campfire-member-circle"' in SCENE
    assert "rebuildCampfireCircle(Math.max(total, roster.length) + 1)" in SCENE
    assert "(count * CAMPFIRE_SEAT_SPACING) / (2 * Math.PI)" in SCENE
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
    assert "Math.log1p(table.rowCount)" in SCENE
    assert "rowCount > 1" in SCENE
    assert ".slice(0, 128)" in SCENE
    assert "visibleTableCount" in SCENE
    assert "system-capacity-row-count:" in SCENE
    # The table name is printed on the bar's top face only; the old upright
    # label behind each bar was removed.
    assert "system-capacity-table-name:" not in SCENE
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


def test_qt_main_navigation_opens_the_world_root():
    assert 'setToolTip("Open ForkMesh World in your browser")' in QT_CHAT
    assert "[this] { openServerWebsite(m_activeServer); }" in QT_CHAT
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


def test_information_booth_deep_link_carries_no_cloudflare_secret():
    exact = "forkmesh://control/cloudflare"
    assert exact in APP
    assert "The hosted World never accepts, proxies, or stores a Cloudflare API token" in APP
    assert 'type="password"' not in APP[
        APP.index("informationPanelHTML()"):
        APP.index("rewardPanelHTML()")
    ]
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
    assert "frame.contentWindow?.postMessage(" in terminal
    # The embedded /dashboard/chat page (used by both the terminal bar and the
    # full overlay) listens for that message and fills + focuses its composer.
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
