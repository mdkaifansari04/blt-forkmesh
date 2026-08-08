"""GitHub-style pull review surface and owner-only mirror merge contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTENT = (
    ROOT / "app/public/dashboard/js/06-repo-content.js"
).read_text(encoding="utf-8")
NETWORK = (
    ROOT / "app/public/dashboard/js/08-repo-detail-network.js"
).read_text(encoding="utf-8")


def test_pull_tabs_are_real_isolated_panels():
    for name in ("conversation", "commits", "files", "badge"):
        assert f'data-repo-record-tab="{name}"' in CONTENT
        assert f'data-repo-record-panel="{name}"' in CONTENT
    assert 'aria-selected="true"' in CONTENT
    assert "panel.dataset.repoRecordPanel !== tab" in NETWORK
    assert "scrolls its section into view" not in NETWORK


def test_changed_files_use_left_navigation_and_persistent_viewed_state():
    assert "data-repo-pull-file-list" in CONTENT
    assert "data-repo-pull-file-tree" in CONTENT
    assert "lg:grid-cols-[20rem_minmax(0,1fr)]" in CONTENT
    assert "data-lucide=\"folder\"" in CONTENT
    assert "pullViewedStorageKey" in CONTENT
    assert "localStorage.setItem(key" in CONTENT
    assert "data-repo-pull-viewed-summary" in CONTENT
    assert 'aria-pressed="${isViewed ? "true" : "false"}"' in CONTENT
    assert "toggleRepoPullViewed(" in NETWORK


def test_branch_backed_pull_diff_is_reconstructed_from_creation_oids():
    assert 'repoLiveUrl(repo, "compare"' in CONTENT
    assert "values.creationBaseOid" in CONTENT
    assert "values.creationHeadOid" in CONTENT
    assert "loadRepoPullPatch(repo, number, pullMetadataCommit, parsed.values || {})" in CONTENT


def test_files_panel_uses_full_width_and_tracks_selected_file():
    assert "data-repo-record-layout" in CONTENT
    assert 'sidebar?.classList.toggle("hidden", !conversation)' in NETWORK
    assert 'pullFileButton.setAttribute("aria-current", "true")' in NETWORK


def test_pull_review_approval_and_owner_only_mirror_merge_are_available():
    assert 'data-repo-pull-review-action="approved"' in CONTENT
    assert "loadRepoPullMergeAuthorization" in CONTENT
    assert 'profile?.viewerRole === "owner"' in CONTENT
    assert "data-repo-pull-merge" in CONTENT
    assert "expectedBaseOid" in CONTENT
    assert "expectedHeadOid" in CONTENT
    assert "expectedPullsOid" in CONTENT
    assert "handleRepoPullMerge(state.selectedRepo)" in NETWORK
