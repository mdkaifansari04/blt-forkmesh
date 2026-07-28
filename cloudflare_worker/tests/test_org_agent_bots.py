"""Organization-scoped Claude/Codex agent security and World contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src/entry.py").read_text(encoding="utf-8")
SCHEMA = (ROOT / "src/schema.py").read_text(encoding="utf-8")
URLS = (ROOT / "src/urls.py").read_text(encoding="utf-8")
DASHBOARD = (
    ROOT / "public/dashboard/js/06-repo-content.js"
).read_text(encoding="utf-8")
CHAT = (ROOT / "public/dashboard-chat.js").read_text(encoding="utf-8")
WORLD = (ROOT / "public/world/world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")
QT = (ROOT.parent / "qt_client/src/MainWindowAgents.cpp").read_text(
    encoding="utf-8"
)
QT_REPOS = (ROOT.parent / "qt_client/src/MainWindowRepos.cpp").read_text(
    encoding="utf-8"
)


def test_org_agents_are_separate_member_scoped_encrypted_records():
    assert "CREATE TABLE IF NOT EXISTS org_agent_sessions" in SCHEMA
    assert "CREATE TABLE IF NOT EXISTS org_agent_jobs" in SCHEMA
    assert "_org_agent_member_context" in ENTRY
    assert "role not in ORG_ROLES" in ENTRY
    assert "repository_not_linked" in ENTRY
    assert "_request_same_origin(request)" in ENTRY
    assert "await encrypt_row(env, record)" in ENTRY
    assert "await encrypt_row(env, job)" in ENTRY
    assert "privacyBoundary" in ENTRY
    assert "owner-device E2EE" in ENTRY
    assert "engineering_team_required" in ENTRY
    assert "team='engineering'" in ENTRY
    assert '"engineeringAccess": True' in ENTRY
    assert '"privacyBoundary": "engineering-team-encrypted-at-rest"' in ENTRY
    assert (
        "visible and controllable only by current members"
        in ENTRY
    )


def test_only_a_fresh_signed_provider_capable_mirror_receives_bounded_jobs():
    assert "agent_provider_mirror_candidates" in ENTRY
    assert "10 * 60 * 1000" in ENTRY
    assert "context[\"nodeOwner\"]" in ENTRY
    assert "provider, preferred_node" in ENTRY
    assert '"agentProviders"' in QT_REPOS
    assert 'QStringLiteral("claude-code")' in QT_REPOS
    assert 'QStringLiteral("codex")' in QT_REPOS
    assert "no_eligible_headless_mirror" in ENTRY
    assert "ORG_AGENT_MAX_PROMPT = 8000" in ENTRY
    assert "ORG_AGENT_JOB_LEASE_MS = 2 * 60 * 1000" in ENTRY
    assert "status='leased'" in ENTRY
    assert "lease_not_found" in ENTRY
    assert "ORG_AGENT_BOTS_RE" in URLS
    assert "REPO_ORG_AGENT_JOB_RESULT_RE" in URLS


def test_mirror_runs_a_tool_free_fail_closed_haiku_gate_before_dispatch():
    assert 'security.value(QStringLiteral("model")).toString() !=' in QT
    assert 'security.value(QStringLiteral("tools")).toBool(true)' in QT
    assert '!security.value(QStringLiteral("failClosed")).toBool()' in QT
    assert "--model 'haiku' --max-turns 1 --tools ''" in QT
    assert "QJsonParseError::NoError" in QT
    assert 'QLatin1String("ALLOW")' in QT
    assert '"securityVerdict"' in QT
    assert 'verdict not in ("approved", "rejected")' in ENTRY
    assert 'verdict != "approved" and run_status != "rejected"' in ENTRY


def test_agents_ui_chat_and_world_expose_both_fixed_bot_identities():
    assert "Start Claude Code" in DASHBOARD
    assert "Start Codex" in DASHBOARD
    assert "data-org-agent-followup" in DASHBOARD
    assert "CLAUDE_MENTION_RE" in CHAT
    assert "CODEX_MENTION_RE" in CHAT
    assert "maybeAskOrgAgent" in CHAT
    assert '"claude"' in SCENE
    assert '"codex"' in SCENE
    assert "exciteAgentBot" in SCENE
    assert "onAgentBotChat" in SCENE
    assert "refreshOrgAgentBots" in WORLD
    assert "mirrorNodeAgentSessionsHTML" in WORLD
    assert "wireMirrorNodeAgentWorkspace" in WORLD
    assert "data-world-agent-followup" in WORLD
    assert "Only members of the" in WORLD


def test_agent_chat_fails_closed_and_never_enters_the_shared_room():
    assert "orgAgentEngineeringAccess = false" in CHAT
    assert "await loadOrgAgentChatAccess()" in CHAT
    assert "data?.engineeringAccess === true" in CHAT
    assert "orgAgentIdentity(entry.sender, entry.senderId)" in CHAT
    assert "if (!orgAgentIdentity(plain.sender, plain.senderId)) send(plain)" in CHAT
    assert "Claude and Codex chat is available only to the Engineering team." in CHAT
    assert "Only Engineering team members can start this agent." in CHAT
    assert "setAgentBotAccess" in SCENE
    assert "let agentBotAccessAllowed = false" in SCENE
    assert "avatar.visible = false" in SCENE
    assert "this.world?.setAgentBotAccess?.(false)" in WORLD
    assert 'this.orgAgentAccess?.state !== "allowed"' in WORLD
    assert 'if (!agentBotAccessAllowed) return false;' in SCENE
    assert "record.state === \"open\" && agentBotAccessAllowed" in SCENE


def test_qt_reports_full_session_and_runtime_availability_to_world():
    for marker in (
        "localCliAvailability",
        "Claude Code binary is missing",
        "Claude Code login is missing",
        'QStringLiteral("agentInfo")',
        'QStringLiteral("availability")',
        'QStringLiteral("contextWindow")',
        'QStringLiteral("costUsd")',
        'QStringLiteral("lastError")',
    ):
        assert marker in QT
    assert "_org_agent_info_projection" in ENTRY
    assert "_org_agent_availability_projection" in ENTRY
    assert '"agentInfo": agent_info' in ENTRY


def test_world_explains_stalled_agent_jobs_and_bot_clicks_open_full_status():
    assert '"code": "mirror_not_claiming_jobs"' in ENTRY
    assert '"diagnostic": diagnostic' in ENTRY
    assert "session?.diagnostic" in SCENE
    assert 'diagnostic.level || "").toLowerCase() === "attention"' in SCENE
    assert "openAgentBotDetail(name)" in WORLD
    assert "ENGINEERING AGENT / LIVE SESSION STATUS" in WORLD
    assert "{ provider, allNodes: true }" in WORLD
    assert "data-world-agent-open-chat" in WORLD


def test_qt_accepts_supported_headless_credential_sources_without_relaying_secrets():
    for marker in (
        "ANTHROPIC_API_KEY",
        "ANTHROPIC_AUTH_TOKEN",
        "CLAUDE_CODE_OAUTH_TOKEN",
        "credentialSource",
    ):
        assert marker in QT
    assert '"credentialSource": clean_string' in ENTRY


def test_issue_agent_model_choice_is_allowlisted_and_bound_to_the_qt_issue():
    for marker in (
        '"haiku": "claude-haiku-4-5"',
        '"sonnet": "claude-sonnet-4-6"',
        '"opus": "claude-opus-4-8"',
        '"fable": "claude-fable-5"',
        '"sol": "gpt-5.6-sol"',
        '"luna": "gpt-5.6-luna"',
        '"terra": "gpt-5.6-terra"',
        '"invalid_model"',
        '"invalid_issue_task_key"',
        '"issueNumber": issue_number',
        '"model": model or ""',
    ):
        assert marker in ENTRY
    assert "allowedWebsiteModels" in QT
    assert "startAgentForIssue(" in QT
    assert "requestedModel" in QT


def test_only_successful_codex_results_complete_a_valid_tracked_board_key():
    assert 'str(row.get("provider") or "") == "codex"' in ENTRY
    assert 'run_status == "completed"' in ENTRY
    assert "completed_by_bi=excluded.completed_by_bi" in ENTRY
    assert '"completedKeys"' in (
        ROOT / "src/world_build_board.py"
    ).read_text(encoding="utf-8")
    assert "completeAgentTask" in WORLD
    assert "Completed ${taskLabel}. Moving it to Done." in SCENE
