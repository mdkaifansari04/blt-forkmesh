"""Repository issue storage layout contracts.

Issues are split by status (adhoc #14): every issue folder lives under
.forkmesh/issues/open/<n>/ or .forkmesh/issues/closed/<n>/, and the folder
moves when the status flips. Only labels.json/milestones.json (and the two
status folders) sit at the root — a numbered folder directly under
.forkmesh/issues/ would be the pre-split legacy layout resurfacing.
"""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ISSUES = ROOT / ".forkmesh" / "issues"


def _numeric_dirs(root):
    if not root.is_dir():
        return []
    return sorted(
        (path for path in root.iterdir() if path.is_dir() and path.name.isdigit()),
        key=lambda path: int(path.name),
    )


def test_repository_issues_live_under_open_closed_status_folders():
    assert not (ROOT / "issues").exists()
    assert ISSUES.is_dir()
    assert not list(ISSUES.rglob("*.md"))



    assert _numeric_dirs(ISSUES) == []

    issue_dirs = _numeric_dirs(ISSUES / "open") + _numeric_dirs(ISSUES / "closed")
    assert len(issue_dirs) >= 300

    seen = set()
    for issue_dir in issue_dirs:
        number = int(issue_dir.name)
        assert number not in seen
        seen.add(number)
        issue_file = issue_dir / f"issue-{number}.json"
        assert issue_file.is_file()
        data = json.loads(issue_file.read_text(encoding="utf-8"))
        assert data["schema"] == "forkmesh-issue-v1"
        assert data["number"] == number
        assert isinstance(data.get("events"), list) and data["events"]


def test_issue_folder_matches_recorded_status():



    for side in ("open", "closed"):
        for issue_dir in _numeric_dirs(ISSUES / side):
            number = int(issue_dir.name)
            data = json.loads(
                (issue_dir / f"issue-{number}.json").read_text(encoding="utf-8"))
            status = data.get("status") or "open"
            for event in data.get("events") or []:
                if isinstance(event, dict) and event.get("type") == "status" \
                        and event.get("status"):
                    status = event["status"]
            assert (status == "closed") == (side == "closed"), (
                f"issue #{number} has status {status!r} but sits in {side}/"
            )


def test_issue_media_stays_in_the_numeric_issue_folder():
    issue = json.loads(
        (ISSUES / "closed" / "382" / "issue-382.json").read_text(encoding="utf-8"))
    attachments = issue["events"][0]["attachments"]

    assert attachments == ["541a5b95.png", "e0ab979f.png"]
    for attachment in attachments:
        assert (ISSUES / "closed" / "382" / attachment).is_file()
