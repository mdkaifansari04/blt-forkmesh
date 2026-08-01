"""End-to-end source contracts for Lemmy-compatible collaboration surfaces."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / "cloudflare_worker"
FIXTURES = WORKER / "tests" / "fixtures" / "activitypub" / "lemmy"


def text(path):
    return path.read_text(encoding="utf-8")


def test_fixtures_cover_issue_pull_comment_and_nested_discussion_lifecycles():
    issue = json.loads(text(FIXTURES / "issue-thread.json"))
    pull = json.loads(text(FIXTURES / "pull-comment-thread.json"))
    discussion = json.loads(text(FIXTURES / "discussion-thread.json"))

    assert issue["context"]["kind"] == "issue"
    assert [item["type"] for item in issue["activities"]] == [
        "Create",
        "Update",
    ]
    assert pull["context"]["kind"] == "pull"
    assert [item["type"] for item in pull["activities"]] == [
        "Create",
        "Delete",
    ]
    assert discussion["context"]["kind"] == "discussion"
    assert discussion["activities"][1]["object"]["inReplyTo"] == \
        discussion["activities"][0]["object"]["id"]
    assert discussion["activities"][2]["type"] == "Remove"


def test_worker_dispatches_lifecycle_handlers_and_keeps_separate_storage():
    entry = text(WORKER / "src" / "entry.py")
    module = text(WORKER / "src" / "activitypub_threads.py")
    schema = text(WORKER / "src" / "schema.py")
    migration = text(
        WORKER / "migrations" / "0065_activitypub_thread_lifecycle.sql"
    )

    assert "import activitypub_threads as ap_threads" in entry
    assert 'if activity_type == "Update":' in entry
    assert 'if activity_type == "Remove":' in entry
    assert "UPDATE ap_comments SET lifecycle=?, data=?" in entry
    assert "ap_threads.thread_projection" in entry
    assert "STORAGE_NAMESPACE = \"federatedReplies\"" in module
    assert '"nativeEvent": False' in module
    assert "parent_remote_id_bi" in schema
    assert "parent_remote_id_bi" in migration
    assert "lifecycle" in migration


    delete_start = entry.index("async def _ap_handle_delete")
    delete_end = entry.index("async def ap_inbox_handler", delete_start)
    delete_handler = entry[delete_start:delete_end]
    assert "UPDATE ap_comments" in delete_handler
    assert "DELETE FROM ap_comments" not in delete_handler
    assert "IssueStore" not in delete_handler
    assert "PullStore" not in delete_handler
    assert "DiscussionStore" not in delete_handler


def test_world_web_flutter_and_qt_render_provenance_backlinks_as_non_native():
    world = text(WORKER / "public" / "world" / "world.js")
    dashboard = text(
        WORKER / "public" / "dashboard" / "js" / "06-repo-content.js"
    )
    flutter_model = text(ROOT / "flutter_app" / "lib" / "models" / "models.dart")
    flutter_api = text(
        ROOT / "flutter_app" / "lib" / "services" / "api_service.dart"
    )
    flutter_ui = text(
        ROOT / "flutter_app" / "lib" / "screens" / "repo_detail_screen.dart"
    )
    qt_view = text(ROOT / "qt_client" / "src" / "FederatedThreadView.cpp")
    qt_issue = text(ROOT / "qt_client" / "src" / "MainWindowIssues.cpp")
    qt_pull = text(ROOT / "qt_client" / "src" / "MainWindowPulls.cpp")
    qt_discussion = text(
        ROOT / "qt_client" / "src" / "MainWindowDiscussions.cpp"
    )

    assert "data-world-fediverse-thread" in world
    assert "openFediverseThread(id)" in world
    assert "data-native-event=\"false\"" in world
    assert "not a signed ForkMesh native event" in world
    assert "data-repo-federated-replies" in dashboard
    assert "loadFederatedReplies(repo, kind, number" in dashboard
    assert "not a signed ForkMesh event" in dashboard
    assert "class FederatedReply" in flutter_model
    assert "nativeEvent: false" in flutter_model
    assert "fedi-comments" in flutter_api
    assert "_FederatedRepliesCard" in flutter_ui
    assert "not a signed native event" in flutter_ui
    assert "fedi-comments" in qt_view
    assert "not a signed native event" in qt_view
    assert "new FederatedThreadView" in qt_issue
    assert "new FederatedThreadView" in qt_pull
    assert "new FederatedThreadView" in qt_discussion


def test_documentation_states_context_and_signature_boundaries():
    docs = text(ROOT / "docs" / "lemmy-threaded-collaboration.md")
    assert "A remote object cannot select a ForkMesh context" in docs
    assert "does not make the reply a ForkMesh native event" in docs
    assert "Undo(Remove)" in docs
    assert "Private repository" in docs
