"""Repository issue storage layout contracts."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ISSUES = ROOT / ".forkmesh" / "issues"


def test_repository_issues_live_under_forkmesh_as_one_json_per_issue():
    assert not (ROOT / "issues").exists()
    assert ISSUES.is_dir()
    assert not list(ISSUES.rglob("*.md"))

    issue_dirs = sorted(
        (path for path in ISSUES.iterdir() if path.is_dir() and path.name.isdigit()),
        key=lambda path: int(path.name),
    )
    assert len(issue_dirs) >= 300

    for issue_dir in issue_dirs:
        number = int(issue_dir.name)
        issue_file = issue_dir / f"issue-{number}.json"
        assert issue_file.is_file()
        data = json.loads(issue_file.read_text(encoding="utf-8"))
        assert data["schema"] == "forkmesh-issue-v1"
        assert data["number"] == number
        assert isinstance(data.get("events"), list) and data["events"]


def test_issue_media_stays_in_the_numeric_issue_folder():
    issue = json.loads((ISSUES / "382" / "issue-382.json").read_text(encoding="utf-8"))
    attachments = issue["events"][0]["attachments"]

    assert attachments == ["541a5b95.png", "e0ab979f.png"]
    for attachment in attachments:
        assert (ISSUES / "382" / attachment).is_file()
