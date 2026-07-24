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


def test_world_contains_the_initial_city_districts_and_shared_clock():
    for landmark in (
        "information",
        "fountain",
        "repositories",
        "routing",
        "organizations",
        "fediverse",
        "security",
        "quarantine",
        "launchpad",
        "support",
    ):
        assert f'id: "{landmark}"' in DATA
    assert "export const WORLD_DAY_MS = 4 * 60 * 60 * 1000" in DATA
    for theme in (
        "world",
        "day",
        "sunset",
        "night",
        "rain",
        "snow",
        "cyberpunk",
        "low-light",
    ):
        assert f'id: "{theme}"' in DATA


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
        "createQuarantine",
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
    assert "Guests land in the world immediately" in SCENE
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


def test_presence_client_uses_only_coarse_ephemeral_world_protocol():
    assert 'this.fetchJSON("/api/world/context"' in APP
    assert 'this.fetchJSON("/api/world/ticket"' in APP
    assert 'this.fetchJSON("/api/world/inactive"' in APP
    assert 'this.fetchJSON("/api/network/overview"' in APP
    assert 'this.fetchJSON("/api/repositories")' in APP
    assert "/api/accounts/users" not in APP
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
    assert "WORLD_TICKET_REFRESH_MS = 5 * 60 * 1000" in APP
    assert "startWorldTicketRefresh" in APP
    assert "if (!document.hidden && readSession()?.sessionToken)" in APP
    assert "window.clearInterval(this.worldTicketTimer)" in APP
    assert "session?.organizationAdmin" not in APP
    assert "session?.accountTier" not in APP


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
    assert "WORLD_DAY_MS / 24" in SCENE
    assert "utcOffsetHours" in SCENE
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
    assert 'data-world-emote="wave"' in APP
    assert 'data-world-emote="idea"' in APP
    assert 'data-world-emote="celebrate"' in APP
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


def test_visual_jail_consumes_only_generalized_live_quarantine_data():
    assert 'this.fetchJSON("/api/security/quarantine"' in APP
    assert '"x-forkmesh-world-view": "generalized"' in APP
    assert "normalizeQuarantinePayload" in APP
    assert "privateEvidence" not in APP
    assert "data-world-quarantine-refresh" in APP
    assert "data-world-quarantine-revoke" in APP
    assert "function updateQuarantine" in SCENE
    assert "privacy-safe-live-quarantine" in SCENE
    assert "updateQuarantine," in SCENE


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
    assert "durationMs: WORLD_DAY_MS" in APP
    assert "15 * 60 * 1000" in APP
    assert "CC0-1.0" in APP
    assert "World-day offset" in APP
    assert "Audio never starts automatically" in APP
    assert "Original four-hour procedural score generated locally" in DATA
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
