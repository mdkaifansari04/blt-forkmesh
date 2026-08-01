"""Contract coverage for the browser/Qt issue lifecycle parity surface."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ISSUE_IO = (ROOT / "public/dashboard/js/03-issues-profile-io.js").read_text()
REPO_UI = (ROOT / "public/dashboard/js/06-repo-content.js").read_text()
WIRING = (ROOT / "public/dashboard/js/08-repo-detail-network.js").read_text()
ENTRY = (ROOT / "src/entry.py").read_text()
EVENTS = (ROOT / "src/events.py").read_text()


def _slice(source, start, end):
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_browser_signs_every_user_facing_qt_issue_mutation():
    content = _slice(
        ISSUE_IO,
        "function webIssueEventContent(type, fields = {})",
        "function webIssueEventId(type, ts)",
    )
    for event_type in (
        "edit", "title", "status", "labels", "milestone", "dates",
        "priority", "progress", "assignees", "delete", "vote",
    ):
        assert f'type === "{event_type}"' in content

    submit = _slice(
        ISSUE_IO,
        "async function submitWebIssueEvent(repo, number, type, fields = {})",
        "async function setWebIssueSubscription",
    )
    assert "forkmesh-issue-event-v1" in submit
    assert "ownerAccount: state.session?.nodeName" in submit
    assert "sessionToken: state.session?.sessionToken" in submit


def test_issue_detail_exposes_github_style_lifecycle_controls():
    detail = _slice(
        REPO_UI,
        "function renderRepoRecordDetail(repo, kind, number, parsed)",
        "async function loadRepoRecordDetail",
    )
    for marker in (
        "data-repo-issue-title-edit",
        "data-repo-issue-description-edit",
        "data-repo-issue-status",
        "Reopen issue",
        "Close issue",
        "data-repo-issue-delete",
        "data-repo-issue-vote",
        "data-repo-issue-subscription",
        "data-repo-issue-metadata-form",
        "data-repo-issue-assignees",
        "data-repo-issue-labels",
        "data-repo-issue-milestone-edit",
        "data-repo-issue-priority",
        "data-repo-issue-progress",
        "data-repo-issue-start-date",
        "data-repo-issue-end-date",
    ):
        assert marker in detail


def test_issue_mutation_controls_are_delegated_to_handlers():
    for marker in (
        "handleWebIssueTitleSubmit(issueTitleForm)",
        "handleWebIssueDescriptionSubmit(issueDescriptionForm)",
        "handleWebIssueMetadataSubmit(issueMetadataForm)",
        'handleWebIssueAction("vote")',
        'handleWebIssueAction("delete-issue")',
        '"edit-comment"',
        '"delete-comment"',
    ):
        assert marker in WIRING


def test_structural_issue_events_require_repository_authority():
    handler = _slice(
        ENTRY,
        "async def issues_handler(env, request, owner, repo):",
        "async def discussions_handler",
    )
    assert "owner_event_types = {" in handler
    for event_type in (
        "edit", "title", "status", "labels", "milestone", "dates",
        "priority", "progress", "bounty", "assignees", "agent", "delete",
    ):
        assert f'"{event_type}"' in handler
    assert "await _authorize_issue_manager(env, owner, data, request)" in handler
    # Open/comment/vote stay relay-capable and are deliberately outside the
    # repository-owner structural-event set.
    owner_set = _slice(handler, "owner_event_types = {", "if event_type in owner_event_types")
    for public_type in ("open", "comment", "vote"):
        assert f'"{public_type}"' not in owner_set


def test_issue_manager_supports_personal_and_org_repository_owners():
    authorization = _slice(
        ENTRY,
        "async def _authorize_issue_manager",
        "async def _inbox_author_over_quota",
    )
    assert "await _authorize_owner_account" in authorization
    assert "await _org_row" in authorization
    assert "await _org_role" in authorization
    assert 'role in ("owner", "admin")' in authorization


def test_subscription_can_be_set_and_reloaded_with_account_session():
    backend = _slice(
        ENTRY,
        "async def subscribe_handler",
        "async def send_notification_digests",
    )
    assert 'action == "status"' in backend
    assert "session_authorized" in backend
    assert '"subscribed": bool(subscription' in backend
    assert "async function loadWebIssueSubscription" in ISSUE_IO
    assert "parsed.issueSubscribed = await loadWebIssueSubscription" in REPO_UI


def test_worker_accepts_only_known_qt_issue_events_and_matches_agent_signing():
    for event_type in (
        "open", "comment", "edit", "title", "status", "labels",
        "milestone", "dates", "priority", "progress", "bounty",
        "assignees", "agent", "delete", "vote",
    ):
        assert f'"{event_type}"' in EVENTS
    assert "event_type not in ISSUE_EVENT_TYPES" in EVENTS
    agent = _slice(EVENTS, 'if t == "agent":', 'if t == "delete":')
    assert '"pr" if ev.get("agentCreatePr") else "no-pr"' in agent
