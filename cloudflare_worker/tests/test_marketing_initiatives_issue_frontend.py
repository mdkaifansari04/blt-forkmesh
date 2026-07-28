"""Web issue details can securely promote an issue into Marketing."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = (ROOT / "public" / "dashboard.js").read_text(encoding="utf-8")


def test_issue_detail_has_marketing_initiative_action_and_authenticated_post():
    for contract in (
        "async function moveIssueToMarketingInitiatives(button)",
        'detail?.kind !== "issues"',
        '"/api/world/office/marketing-tasks/initiatives"',
        "authorization: `Bearer ${state.session.sessionToken}`",
        "owner: repo.owner",
        "repo: repo.name",
        "number: Number(detail.number)",
        "data-repo-marketing-initiative",
        "Move to Marketing initiatives",
    ):
        assert contract in DASHBOARD


def test_pending_records_do_not_offer_promotion_and_errors_are_recoverable():
    assert "isIssues && !options.pending" in DASHBOARD
    assert "button.disabled = false" in DASHBOARD
    assert "Unable to move issue" in DASHBOARD
