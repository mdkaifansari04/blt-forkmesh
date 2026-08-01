#!/usr/bin/env python3
"""Commenting on an issue from the website (adhoc #388).

Issues were read-only on the web: the detail view rendered the signed comment
events already committed to the mirror, but there was no way to add one - unlike
discussions (renderDiscussionReplyForm) and pull requests (renderPullReviewForm).
These contract tests pin the composer, its wiring, and - most importantly - that
the browser signs an issue comment over exactly the bytes the Worker and the
desktop client hash, so the submission verifies instead of 401ing.
"""

import ast
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js


ROOT = Path(__file__).resolve().parents[1]
EVENTS = ROOT / "src" / "events.py"
DASHBOARD_JS = assembled_dashboard_js()
ISSUE_STORE = (
    ROOT.parent / "qt_client" / "src" / "IssueStore.cpp"
).read_text(encoding="utf-8")


def _load_issue_event_content():
    """The Worker's pure canonicalizer, lifted out of the js-importing module."""
    tree = ast.parse(EVENTS.read_text(encoding="utf-8"), filename=str(EVENTS))
    node = next(n for n in tree.body
                if isinstance(n, ast.FunctionDef) and n.name == "issue_event_content")
    namespace = {}
    exec(compile(ast.fix_missing_locations(ast.Module(body=[node], type_ignores=[])),
                 str(EVENTS), "exec"), namespace)
    return namespace["issue_event_content"]


ISSUE_EVENT_CONTENT = _load_issue_event_content()


def _slice(start, end):
    return DASHBOARD_JS[DASHBOARD_JS.index(start):DASHBOARD_JS.index(end)]


def test_web_signs_an_issue_comment_over_body_nul_attachments():




    signer = _slice("async function submitWebIssueComment",
                    "  // Mirrors DiscussionStore::contentForSigning")
    assert 'const NUL = String.fromCharCode(0);' in signer
    assert "const contentHash = await sha256HexLower(cleanBody + NUL);" in signer
    assert (
        "const canonical = `forkmesh-issue-event-v1\\ncomment\\n${number}\\n"
        "${pub}\\n${ts}\\n${contentHash}`;"
    ) in signer

    assert ISSUE_EVENT_CONTENT({"type": "comment", "body": "hi"}) == "hi\x00"
    assert 'return ev.body + nul + ev.attachments.join(",");' in ISSUE_STORE


def test_web_issue_comment_posts_a_signed_event_against_the_real_number():



    signer = _slice("async function submitWebIssueComment",
                    "  // Mirrors DiscussionStore::contentForSigning")
    assert 'type: "comment",' in signer
    assert 'id: "comment-web-" + ts,' in signer


    assert "body: cleanBody," in signer
    assert "attachments: []," in signer
    assert "const payload = { owner: repo.owner, repo: repo.name, number, event };" in signer
    assert 'await fetch(`${repoApiBase(repo)}/issues`, {' in signer
    assert "number: 0" not in signer


def test_issue_detail_renders_a_comment_composer():
    form = _slice("function renderIssueCommentForm(number)",
                  "function renderDiscussionReplyForm(number)")

    assert "if (!state.session?.nodeName)" in form
    assert "to comment on this issue." in form
    assert "data-repo-issue-comment-form" in form
    assert 'data-repo-issue-comment-number="${escapeHtml(number)}"' in form
    assert "data-repo-issue-comment-body" in form
    assert "data-repo-issue-comment-hint" in form
    assert "data-repo-issue-comment-submit" in form


def test_record_detail_mounts_the_composer_for_numbered_issues_only():
    detail = _slice("function renderRepoRecordDetail(repo, kind, number, parsed)",
                    "async function loadRepoRecordDetail(repo, kind, number)")
    assert "const isIssues = !isPulls && !isDiscussions;" in detail


    assert "const issueCommentSection = isIssues && !options.pending" in detail
    assert "? renderIssueCommentForm(number)" in detail
    assert "${issueCommentSection}" in detail


    assert 'data-repo-issue-timeline data-empty="${issueTimeline ? "false" : "true"}"' in detail


def test_issue_comment_form_is_wired_to_the_delegated_submit_listener():
    listener = _slice('const issueCommentForm = event.target.closest("[data-repo-issue-comment-form]");',
                      'const discussionReplyForm = event.target.closest(')
    assert "event.preventDefault();" in listener
    assert "handleIssueCommentSubmit(state.selectedRepo, issueCommentForm);" in listener


def test_issue_comment_handler_appends_optimistically_and_maps_inbox_errors():
    handler = _slice("async function handleIssueCommentSubmit(repo, form)",
                     "async function handleDiscussionReplySubmit(repo, form)")
    assert "const number = Number(form.dataset.repoIssueCommentNumber || 0);" in handler
    assert "await submitWebIssueComment(repo, number, body);" in handler



    assert '[data-repo-issue-timeline]' in handler
    assert "timeline.insertAdjacentHTML(\"beforeend\", renderIssueTimelineComment({" in handler
    assert 'timeline.dataset.empty = "false";' in handler
    for code in ("inbox_full", "author_quota", "issue_too_large", "bad_signature"):
        assert f'code === "{code}"' in handler


def test_timeline_comment_row_is_shared_by_history_and_new_comments():


    row = _slice("function renderIssueTimelineComment(ev)",
                 "  // Full issue activity timeline")
    assert "<span>commented</span>" in row
    assert "escapeHtml(body)" in row
    timeline = _slice("function renderIssueTimeline(events)",
                      "function projectJsonPath(number)")
    assert "items.push(renderIssueTimelineComment(ev));" in timeline
