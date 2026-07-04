#!/usr/bin/env python3
"""Static contracts for the landing/dashboard split."""

from pathlib import Path
import tomllib

from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
REDIRECTS = (PUBLIC / "_redirects").read_text(encoding="utf-8")


def _read(path: Path) -> str:
    # dashboard/index.html and its dashboard.html duplicate are now shell
    # scaffolds full of <!--#include partial="name"--> placeholders; the Worker
    # composes them from public/dashboard/partials/*.html at request time (see
    # src/dashboard_shell.py). Frontend contracts here assert on the composed
    # document a browser actually receives. test_dashboard_shell_is_split_into_
    # composable_partials pins the two raw shell files byte-identical.
    if path.name == "dashboard.html" or (
            path.name == "index.html" and path.parent.name == "dashboard"):
        return assembled_dashboard()
    # dashboard.js is likewise split into ordered public/dashboard/js/*.js
    # fragments the Worker concatenates into one /dashboard.js at request time
    # (see src/dashboard_bundle.py); assert on the composed script.
    if path.name == "dashboard.js":
        return assembled_dashboard_js()
    return path.read_text(encoding="utf-8")


def test_dashboard_shell_is_split_into_composable_partials():
    raw_index = (PUBLIC / "dashboard" / "index.html").read_text(encoding="utf-8")
    raw_dupe = (PUBLIC / "dashboard.html").read_text(encoding="utf-8")

    # The served shell and its 308-redirect duplicate must stay byte-identical.
    assert raw_index == raw_dupe
    # The shell is genuinely split — it references partials rather than inlining
    # the chrome.
    assert "<!--#include" in raw_index
    for name in ("header", "sidebar", "main", "network-rail", "modals"):
        assert (PUBLIC / "dashboard" / "partials" / (name + ".html")).is_file()
        assert ('<!--#include partial="%s"-->' % name) in raw_index

    # Composition leaves no placeholder behind and the Worker uses ASSETS to
    # fetch each partial before stitching them together.
    composed = assembled_dashboard()
    assert "<!--#include" not in composed
    assert "self.env.ASSETS.fetch(" in ENTRY_TEXT
    assert "assemble_shell(" in ENTRY_TEXT


def test_feature_landing_is_promoted_to_index_with_signed_in_redirect():
    index = _read(PUBLIC / "index.html")

    assert "Never lose the code that matters" in index
    assert "forkmesh.session" in index
    assert 'location.replace("/dashboard")' in index
    assert 'location.replace("/dashboard.html")' not in index
    assert "Code hosting that lives on the network." not in index


def test_dashboard_exposes_live_hydration_targets():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    assert 'src="/dashboard.js"' in dashboard
    assert 'src="/dashboard-chat.js"' in dashboard
    for marker in (
        "data-dashboard-profile-name",
        "data-sidebar-repo-list",
        "data-sidebar-repo-count",
        "data-logout-button",
        "data-repo-count",
        "data-network-node-list",
        "data-network-summary",
        "data-network-rail-summary",
    ):
        assert marker in dashboard

    for placeholder in (
        'data-repo="you/meshcore"',
        'data-repo="relay-eu-1/relay-monitor"',
        "1,204",
    ):
        assert placeholder not in dashboard


def test_dashboard_get_paid_button_uses_small_sol_logo():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    assert 'href="/mirror-payouts"' in dashboard
    assert 'src="/assets/sol.png"' in dashboard
    assert 'alt="" aria-hidden="true"' in dashboard
    assert 'class="h-3.5 w-3.5 shrink-0 rounded-full object-contain"' in dashboard
    assert '<span class="hidden sm:inline">Get paid to mirror</span>' in dashboard


def test_dashboard_uses_local_helvetica_without_affecting_code_or_site_fonts():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    dashboard_js = _read(PUBLIC / "dashboard.js")
    site_css = _read(PUBLIC / "styles.css")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    assert 'font-family: "ForkMesh Helvetica"' in dashboard
    assert 'src: url("/assets/fonts/HelveticaNeueRoman.otf") format("opentype")' in dashboard
    assert 'src: url("/assets/fonts/HelveticaNeueBold.otf") format("opentype")' in dashboard
    assert 'sans: ["ForkMesh Helvetica", "Helvetica Neue", "Helvetica", "Arial", "sans-serif"]' in dashboard
    assert 'font-family: "ForkMesh Dashboard Mono"' in dashboard
    assert 'mono: ["ForkMesh Dashboard Mono", "ui-monospace", "SFMono-Regular", "monospace"]' in dashboard
    assert "fonts.googleapis.com" not in dashboard
    assert "Outfit" not in dashboard
    assert "Geist Mono" not in dashboard
    assert (
        '<pre class="max-h-[32rem] overflow-auto p-4 font-mono text-xs leading-5 text-muted-foreground"><code>' in dashboard_js
        or 'font-mono text-foreground">${escapeHtml(line) || " "}</span>' in dashboard_js
        or 'font-mono text-zinc-200">${highlightCodeLine(line, path)}</span>' in dashboard_js
    )
    assert '--sans: "ForkMesh Lato"' in site_css
    assert "ForkMesh Helvetica" not in site_css


def test_dashboard_hydrator_uses_existing_worker_apis():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for endpoint in (
        "/api/repositories",
        "/api/network/stats",
        "/api/network/leaderboards",
        "/api/network/online-history",
    ):
        assert endpoint in dashboard_js

    assert "forkmesh.session" in dashboard_js
    assert "renderRepositories" in dashboard_js
    assert "renderNetwork" in dashboard_js
    assert "requestedRepoKey()" in dashboard_js
    # Signed-out visitors browse repositories as guests instead of being
    # bounced back to the landing page (adhoc #123).
    assert 'location.replace("/");\n        return;' not in dashboard_js
    assert "data-guest-auth-link" in dashboard_js
    assert 'renderProfile(session || { nodeName: "guest" })' in dashboard_js
    assert "localStorage.removeItem(\"forkmesh.session\")" in dashboard_js
    assert "forkmesh_session=; Path=/; Max-Age=0" in dashboard_js


def test_dashboard_profile_page_removes_secondary_profile_picture_card():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    dashboard_js = _read(PUBLIC / "dashboard.js")
    login_js = _read(PUBLIC / "login.js")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    assert "A generated placeholder is shown for now" not in dashboard
    assert ">IMG<" not in dashboard
    assert "data-profile-page-avatar-image" in dashboard
    assert "<h2 class=\"text-sm font-semibold text-foreground\">Profile picture</h2>" not in dashboard
    assert "data-profile-picture-avatar" not in dashboard
    assert "data-profile-picture-name" not in dashboard
    assert "data-profile-picture-status" not in dashboard
    assert '$("[data-profile-picture-avatar]")' not in dashboard_js
    assert '$("[data-profile-picture-name]")' not in dashboard_js
    assert '$("[data-profile-picture-status]")' not in dashboard_js
    assert "avatarPng: body.avatarPng || \"\"" in dashboard_js
    assert "avatarUpdatedAt: Number(body.avatarUpdatedAt) || 0" in dashboard_js
    assert "function applyAvatar" in dashboard_js
    assert "refreshPublicProfile" in dashboard_js
    assert "avatarPng: body.avatarPng || \"\"" in login_js


def test_dashboard_periodically_refreshes_worker_profile_for_qt_updates():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        "PROFILE_SYNC_INTERVAL_MS",
        "function startProfileSync()",
        "state.profileSyncTimer = window.setInterval",
        "document.visibilityState === \"hidden\"",
        "refreshPublicProfile(state.session)",
        "startProfileSync();",
    ):
        assert marker in dashboard_js


def test_dashboard_defaults_to_repositories_and_hides_unready_home_desktop_nav():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")

    assert '<!-- Home dashboard tab is intentionally hidden' in dashboard
    assert '<!-- Desktop Client is intentionally hidden' in dashboard
    assert 'data-section="repos" data-nav-link aria-current="page"' in dashboard
    visible = _strip_html_comments(dashboard)
    assert 'data-view="repos" class="view active' in visible
    assert 'data-view="home" class="view active' not in visible
    rendered_nav = dashboard[
        dashboard.index('<nav data-top-nav')
        : dashboard.index('</nav>', dashboard.index('<nav data-top-nav'))
    ]
    visible_nav = rendered_nav.replace(
        rendered_nav[rendered_nav.index('<!-- Home dashboard tab'):rendered_nav.index('-->', rendered_nav.index('<!-- Home dashboard tab')) + 3],
        '',
    ).replace(
        rendered_nav[rendered_nav.index('<!-- Desktop Client'):rendered_nav.index('-->', rendered_nav.index('<!-- Desktop Client')) + 3],
        '',
    )
    assert "Home" not in visible_nav
    assert "Desktop Client" not in visible_nav


def test_dashboard_has_mobile_responsive_navigation_drawers():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    for marker in (
        "data-dashboard-shell",
        "data-mobile-menu-toggle",
        "data-mobile-sidebar-backdrop",
        "data-dashboard-sidebar",
        "data-mobile-network-toggle",
        "data-mobile-network-backdrop",
        "data-mobile-drawer-close",
        "data-mobile-network-close",
        "dashboard-sidebar-open",
        "network-drawer-open",
    ):
        assert marker in dashboard

    for marker in (
        "function setMobileSidebarOpen(open)",
        "function setMobileNetworkOpen(open)",
        "function closeMobileDrawers()",
        "matchMedia(\"(min-width: 1024px)\")",
        "data-mobile-menu-toggle",
        "data-mobile-network-toggle",
        "closeMobileDrawers();",
    ):
        assert marker in dashboard_js


def test_dashboard_has_scoped_light_dark_appearance_controls():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    dashboard_js = _read(PUBLIC / "dashboard.js")
    landing = _read(PUBLIC / "index.html")
    login = _read(PUBLIC / "login.html")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    for marker in (
        'data-dashboard-theme',
        'meta name="color-scheme" content="light dark"',
        "data-profile-appearance-panel",
        "data-appearance-theme",
        "data-appearance-theme-status",
        "forkmesh.dashboard.theme",
    ):
        assert marker in dashboard

    profile_view = dashboard[
        dashboard.index('data-view="profile"') : dashboard.index('data-view="chat"')
    ]
    assert "data-profile-appearance-panel" in profile_view
    assert "data-appearance-theme" in profile_view

    profile_popover = dashboard[
        dashboard.index("data-profile-popover") : dashboard.index("data-profile-toggle")
    ]
    assert "data-appearance-settings-button" not in profile_popover

    for marker in (
        "function readDashboardTheme()",
        "function applyDashboardTheme(theme)",
        "forkmesh.dashboard.theme",
        "[data-appearance-theme]",
    ):
        assert marker in dashboard_js

    assert "function setAppearanceModalOpen(open)" not in dashboard_js
    assert "data-appearance-settings-button" not in dashboard_js
    assert "data-appearance-modal" not in dashboard

    for page in (landing, login):
        assert "data-profile-appearance-panel" not in page
        assert "data-appearance-theme" not in page
        assert "forkmesh.dashboard.theme" not in page


def _strip_html_comments(html: str) -> str:
    while "<!--" in html:
        start = html.index("<!--")
        end = html.index("-->", start) + 3
        html = html[:start] + html[end:]
    return html


def test_dashboard_repository_detail_keeps_code_comments_issues_shell():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        'data-view="repos"',
        'data-view="explore"',
        'data-repo-detail',
        'id="dashboard-repo-feature-template"',
        'data-state="open"',
        'data-state="closed"',
        'data-state="all"',
    ):
        assert marker in dashboard
    for marker in (
        "function repositoryCard",
        "function renderRepoDetail",
        "loadRepositoryTree(repo",
        "loadRepositoryBlob(repo",
        # Issues load from the repo's git tree so the Open-by-default state
        # filter (issue #270) has real published issues to work with.
        "loadRepoIssues(repo)",
        # Pull requests and discussions load lazily on first tab view rather than
        # eagerly on every refresh (adhoc #105: eager per-record blob fan-out
        # tripped the host rate limit and made reloads stop working).
        "loadRepoCollection(state.selectedRepo, tab,",
        "loadRepoCommits(repo)",
        "loadRepoMirrors(repo)",
        "Copy clone",
        "Open clean URL",
        "data-dashboard-repo-tab=\"${tab}\"",
        '"code", "commits", "releases", "issues", "pulls", "discussions", "mirrors"',
        # Releases load lazily on first tab view from releases/<channel>/release.json.
        "loadRepoReleases(state.selectedRepo)",
    ):
        assert marker in dashboard_js


def test_dashboard_repository_detail_uses_github_like_inner_layout():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]

    for marker in (
        'data-repo-layout="github-like"',
        "Repository facts",
        "Go to file",
        "data-repo-commit-summary",
        "data-repo-about",
        "data-repo-live-summary",
        "data-repo-readme",
        "Pull requests",
    ):
        assert marker in render
    assert 'data-lucide="${icon}"' in dashboard_js
    assert 'icon = isPulls ? "git-pull-request" : "circle-dot"' in dashboard_js


def test_dashboard_repository_detail_view_uses_full_width_container():
    for path in (PUBLIC / "dashboard.html", PUBLIC / "dashboard" / "index.html"):
        dashboard = _read(path)
        explore = dashboard[
            dashboard.index('data-view="explore"')
            : dashboard.index('data-repo-detail')
        ]

        assert "w-full max-w-none" in explore
        assert "max-w-5xl mx-auto" not in explore


def test_dashboard_repository_tabs_keep_border_without_selected_background():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]
    tab_state = dashboard_js[
        dashboard_js.index("function setRepoTab")
        : dashboard_js.index("function repoPathParts")
    ]

    assert 'border-primary text-foreground' in render
    assert 'border-primary bg-secondary text-foreground' not in render
    assert 'button.classList.toggle("bg-secondary", active);' not in tab_state


def test_dashboard_repository_folder_icons_are_grey():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    tree_loader = dashboard_js[
        dashboard_js.index("async function loadRepositoryTree")
        : dashboard_js.index("async function loadRepositoryBlob")
    ]

    assert 'data-lucide="${isTree ? "folder" : "file"}" class="h-4 w-4 shrink-0 text-muted-foreground"' in tree_loader
    assert '${isTree ? "text-primary" : "text-muted-foreground"}' not in tree_loader


def test_dashboard_repository_metadata_constrains_long_values_without_fake_language_mix():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]

    assert 'grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3' in render
    assert 'class="min-w-0 truncate text-right text-foreground font-mono"' in render
    assert "flex justify-between gap-3" not in render
    assert "data-repo-live-summary" in render
    assert "formatSize(repo.sizeBytes)" in render
    assert "TypeScript" not in render
    assert "CSS" not in render
    assert "JavaScript" not in render
    assert "grid-cols-[72fr_18fr_10fr]" not in render


def test_dashboard_repository_tabs_read_public_mirror_data_not_owner_inbox():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]

    for marker in (
        'commits: { label: "Commits"',
        'data-dashboard-repo-tab-panel="commits"',
        "data-repo-commits",
        "async function loadRepoCommits(repo)",
        "fetchJson(repoLiveUrl(repo, \"history\"))",
        "function parseFrontMatter(markdown)",
        "async function loadRepoRecordsFromMirror(repo, config)",
        # Issues read the public git tree via loadRepoIssues; pulls and
        # discussions read it lazily through loadRepoCollection on tab view.
        "loadRepoIssues(repo)",
        "loadRepoCollection(state.selectedRepo, tab,",
        'dir: "issues", file: "issue.md"',
        'dir: "pulls", file: "pull.md"',
        'dir: "discussions", file: "discussion.md"',
        "fetchRepoJson(repoLiveUrl(repo, \"tree\", { path: config.dir }))",
        "fetchRepoJson(repoLiveUrl(repo, \"blob\", { path: recordPath }))",
        "Create from desktop client for signed submissions",
    ):
        assert marker in dashboard_js

    assert "fetchJson(`${repoApiBase(repo)}/${kind}`)" not in dashboard_js
    assert "fetchJson(`${repoApiBase(repo)}/commits`)" not in dashboard_js
    assert "New issue" not in render
    assert "New pull request" not in render
    assert "New discussion" not in render


def test_dashboard_repository_commit_rows_use_github_like_hash_copy_controls():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    commits = dashboard_js[
        dashboard_js.index("async function loadRepoCommits")
        : dashboard_js.index("function renderRepoCommitFiles")
    ]
    copy_handler_start = dashboard_js.index('const copyButton = event.target.closest("[data-dashboard-copy]")')
    click_handler = dashboard_js[
        copy_handler_start
        : dashboard_js.index('$("#repoSearch")', copy_handler_start)
    ]

    for marker in (
        "const hash = String(commit.hash || \"\");",
        "const shortHash = hash.slice(0, 7) || \"unknown\";",
        "data-dashboard-commit-row",
        "data-dashboard-commit-hash=\"${escapeHtml(hash)}\"",
        "data-dashboard-copy=\"${escapeHtml(hash)}\"",
        "data-dashboard-copy-kind=\"commit-hash\"",
        "aria-label=\"Copy commit hash ${escapeHtml(shortHash)}\"",
        "data-dashboard-commit-short-hash",
        "font-mono text-xs font-semibold text-foreground",
    ):
        assert marker in commits

    assert 'data-lucide="git-commit-horizontal" class="h-3.5 w-3.5 text-muted-foreground"' in dashboard_js
    assert 'text-primary">${escapeHtml(String(commit.hash || "").slice(0, 7))}</span>' not in commits
    assert 'data-lucide="git-commit-horizontal" class="h-3.5 w-3.5 text-primary"></i>Commits' not in dashboard_js
    assert "const copied = await copyTextToClipboard(text);" in click_handler
    assert "if (copied) {" in click_handler
    assert "copyButton.classList.add(\"copied\");" in click_handler


def test_dashboard_latest_commit_history_button_opens_commits_tab():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]
    click_handler_start = dashboard_js.index('const historyButton = event.target.closest("[data-dashboard-history-button]")')
    click_handler = dashboard_js[
        click_handler_start
        : dashboard_js.index('const repoTabButton = event.target.closest("[data-dashboard-repo-tab]")', click_handler_start)
    ]

    for marker in (
        'button type="button" data-dashboard-history-button',
        'aria-label="Open commit history"',
        'data-lucide="history"',
        "History</button>",
    ):
        assert marker in render

    assert 'setRepoTab("commits");' in click_handler
    assert "return;" in click_handler


def test_dashboard_repository_issue_and_pull_tabs_use_filter_toolbars_without_create_buttons():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]

    for marker in (
        "function renderRepoCollectionPanel(kind, repo, openCount, closedCount)",
        'data-repo-collection-toolbar="${kind}"',
        'data-repo-filter-menu="${kind}"',
        'data-repo-filter-query="${kind}"',
        'placeholder="${kind === "pulls" ? "is:pr is:open" : "is:issue is:open"}"',
        "Author",
        "Labels",
        "Projects",
        "Milestones",
        "Assignees",
        "Sort",
        "Reviews",
    ):
        assert marker in dashboard_js

    assert 'renderRepoCollectionPanel("issues", repo, issuesCount, repoCount(repo, ["closedIssues", "closedIssueCount"]))' in render
    assert 'renderRepoCollectionPanel("pulls", repo, pullsCount, repoCount(repo, ["closedPulls", "closedPullCount"]))' in render
    assert "New issue" not in render
    assert "New pull request" not in render


def test_dashboard_repository_issue_and_pull_tabs_paginate_records_at_the_bottom():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    record_renderer = dashboard_js[
        dashboard_js.index("function renderRepoRecordList")
        : dashboard_js.index("async function loadRepoCollection")
    ]
    collection_loader = dashboard_js[
        dashboard_js.index("async function loadRepoCollection")
        : dashboard_js.index("async function loadRepoCommits")
    ]
    click_handler = dashboard_js[
        dashboard_js.index('const repoCollectionPageButton = event.target.closest("[data-repo-collection-page]")')
        : dashboard_js.index('const copyButton = event.target.closest("[data-dashboard-copy]")')
    ]

    for marker in (
        "REPO_COLLECTION_PAGE_SIZE",
        "repoCollectionPages",
        "function renderRepoCollectionPagination(kind, page, totalPages, totalItems)",
        'data-repo-collection-pagination="${kind}"',
        'data-repo-collection-page="${kind}"',
        'data-repo-collection-page-target="${page - 1}"',
        'data-repo-collection-page-target="${page + 1}"',
        "items.slice(start, end)",
        "Page ${page} of ${totalPages}",
    ):
        assert marker in dashboard_js

    assert "renderRepoRecordList(items, config, kind)" in collection_loader
    assert "const page = state.repoCollectionPages[kind] || 1;" in record_renderer
    assert "loadRepoCollection(state.selectedRepo, kind, `[data-repo-${kind}]`);" in click_handler


def test_dashboard_repository_record_chips_and_sidebar_links_use_neutral_github_like_colors():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    pagination = dashboard_js[
        dashboard_js.index("function renderRepoCollectionPagination")
        : dashboard_js.index("function renderRepoRecordList")
    ]
    records = dashboard_js[
        dashboard_js.index("function renderRepoRecordList")
        : dashboard_js.index("async function loadRepoCollection")
    ]
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]

    assert "self-start rounded-md border border-border bg-secondary/60 px-2 py-0.5" in records
    assert "data-repo-record-state" in records
    assert "data-repo-record-state" in dashboard_js[
        dashboard_js.index("function renderRepoRecordDetail")
        : dashboard_js.index("async function loadRepoRecordDetail")
    ]
    assert "rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground" not in records
    assert 'number === page ? "bg-secondary text-foreground border-border"' in pagination
    assert 'number === page ? "bg-primary text-primary-foreground border-primary"' not in pagination
    assert "browse-repo-button inline-flex items-center gap-1 text-xs font-medium text-foreground transition-colors" in dashboard_js
    # The Clone availability chip keys off the group-liveness verdict (`live`,
    # which folds in an online mirror serving in place — adhoc #61) but keeps the
    # neutral GitHub-like foreground/muted colors, never the accent primary.
    assert '${live ? "text-foreground" : "text-muted-foreground"}' in render
    assert '${live ? "text-primary" : "text-muted-foreground"}">${live ? "available" : "offline"}</dd>' not in render


def test_dashboard_repository_records_open_live_markdown_detail_views():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        "async function loadRepoRecordDetail(repo, kind, number)",
        "function renderRepoRecordDetail(repo, kind, number, parsed)",
        "function recordDetailMeta(kind, values)",
        "data-repo-record-kind",
        "data-repo-record-number",
        "data-repo-record-detail",
        "data-repo-record-back",
        "data-repo-record-body",
        "const recordPath = `${config.dir}/${number}/${config.file}`;",
        "fetchRepoJson(repoLiveUrl(repo, \"blob\", { path: recordPath }))",
        "loadRepoRecordDetail(state.selectedRepo, recordButton.dataset.repoRecordKind || \"\", recordButton.dataset.repoRecordNumber || \"\")",
        "loadRepoCollection(state.selectedRepo, kind, `[data-repo-${kind}]`);",
    ):
        assert marker in dashboard_js

    assert "fetchJson(`${repoApiBase(repo)}/issues" not in dashboard_js
    assert "fetchJson(`${repoApiBase(repo)}/pulls" not in dashboard_js
    assert "fetchJson(`${repoApiBase(repo)}/discussions" not in dashboard_js


def test_dashboard_pull_detail_reads_committed_patch_for_files_changed():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        "async function loadRepoPullPatch(repo, number)",
        "function parsePatchStats(patch)",
        "function renderRepoPullFiles(files)",
        "function renderRepoPullPatch(patch)",
        "data-repo-pull-files",
        "data-repo-pull-patch",
        "pulls/${number}/changes.patch",
        "fetchRepoJson(repoLiveUrl(repo, \"blob\", { path: patchPath }))",
        "const pullPatch = kind === \"pulls\" ? await loadRepoPullPatch(repo, number) : null;",
        "renderRepoPullFiles(pullPatch.files)",
        "renderRepoPullPatch(pullPatch.patch)",
    ):
        assert marker in dashboard_js

    assert "pulls/${number}/changes.patch" in dashboard_js
    assert "fetchJson(`${repoApiBase(repo)}/pulls" not in dashboard_js


def test_dashboard_commit_history_opens_live_commit_detail_not_inbox_route():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        "async function loadRepoCommitDetail(repo, hash)",
        "function renderRepoCommitDetail(repo, data)",
        "function renderRepoCommitDiff(diff, imageDiffs)",
        "function renderRepoCommitFiles(files)",
        "fetchJson(repoLiveUrl(repo, \"commit\", { path: hash }))",
        "data-repo-commit-detail",
        "data-repo-commit-back",
        "data-repo-commit-files",
        "data-repo-commit-diff",
        "data-repo-commit-truncated",
        "loadRepoCommitDetail(state.selectedRepo, commitButton.dataset.dashboardCommitHash || \"\")",
    ):
        assert marker in dashboard_js

    assert "fetchJson(`${repoApiBase(repo)}/commits" not in dashboard_js


def test_dashboard_repository_tab_counts_wait_for_live_data_and_update_mirrors():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]
    repo_count = dashboard_js[
        dashboard_js.index("function repoCount")
        : dashboard_js.index("function repoDefaultBranch")
    ]
    mirror_loader = dashboard_js[
        dashboard_js.index("async function loadRepoMirrors")
        : dashboard_js.index("function loadRepoFeaturePanels")
    ]

    assert "function tabCountLabel(value)" in dashboard_js
    assert "return null;" in repo_count
    assert "tabCountLabel(meta.count)" in render
    # Tab badges carry the -tab-count hook so live counts (open issues, served
    # pulls/discussions, mirrors) update in place without a full re-render.
    assert 'data-dashboard-repo-tab-count="${tab}"' in render
    assert 'data-dashboard-repo-count="mirrors"' in render
    assert "updateRepoLiveCounts(repo, data.counts);" in dashboard_js
    assert "updateRepoLiveCounts(repo, { mirrors: mirrorCount });" in mirror_loader
    for fake_action in ("Watch <span", "Fork <span", "Star <span"):
        assert fake_action not in render


def test_dashboard_formats_catalog_millisecond_timestamps():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    formatter = dashboard_js[
        dashboard_js.index("function formatDate")
        : dashboard_js.index("function formatSize")
    ]

    assert "const numeric = Number(value);" in formatter
    assert "1000000000000" in formatter
    assert "new Date(numeric" in formatter


def test_worker_routes_public_history_through_live_host_not_commit_inbox():
    route = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _route")
        : ENTRY_TEXT.index("async def _select_clone_fallback")
    ]
    live_history_dispatch = route.index("host_match = REPO_HOST_RE.match")
    owner_inbox_dispatch = route.index("commits_match = REPO_COMMITS_RE.match")

    assert owner_inbox_dispatch < live_history_dispatch
    assert 'r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history|commit|branches)$"' in ENTRY_TEXT
    assert 'op = "commits" if action == "history" else action' in ENTRY_TEXT


def test_worker_keeps_commit_inbox_route_separate_from_public_history_route():
    assert 'REPO_COMMITS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/commits$")' in ENTRY_TEXT
    assert 'r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history|commit|branches)$"' in ENTRY_TEXT
    assert 'elif host_match.group(3) in ("tree", "blobs", "blob", "raw", "history", "commit", "branches"):' in ENTRY_TEXT
    assert 'if action in ("tree", "blob", "history", "commit", "branches"):' in ENTRY_TEXT
    assert 'op = "commits" if action == "history" else action' in ENTRY_TEXT


def test_dashboard_repository_go_to_file_is_real_and_add_file_removed():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]

    assert "Add file" not in render
    for marker in (
        "data-repo-file-finder-open",
        "data-repo-file-finder",
        "data-repo-file-finder-input",
        "data-repo-file-finder-results",
        "function openRepoFileFinder()",
        "function closeRepoFileFinder()",
        "async function buildRepoFileIndex(repo)",
        "function renderRepoFileFinderResults(query = \"\")",
        "function moveRepoFileFinderSelection(delta)",
        "MAX_REPO_FILE_FINDER_RESULTS",
        "while (queue.length && files.length < MAX_REPO_FILE_FINDER_RESULTS",
        "fetchJson(repoLiveUrl(repo, \"tree\", { path }))",
        "data-repo-file-finder-result",
        "loadRepositoryBlob(state.selectedRepo, selected.dataset.repoFileFinderPath || \"\")",
        "event.key.toLowerCase() === \"t\"",
        "openRepoFileFinder();",
        "event.key === \"Escape\"",
        "closeRepoFileFinder();",
        "event.key === \"Enter\"",
    ):
        assert marker in dashboard_js


def test_dashboard_repository_branch_button_lists_live_remote_branches():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]
    click_handler_start = dashboard_js.index('const branchButton = event.target.closest("[data-repo-branch-button]")')
    click_handler = dashboard_js[
        click_handler_start
        : dashboard_js.index('const fileFinderOpenButton = event.target.closest("[data-repo-file-finder-open]")', click_handler_start)
    ]

    for marker in (
        "function repoBranchList(repo)",
        "function repoBranchCount(repo)",
        "function repoBranchQuery(repo)",
        "function setRepoBranchQuery(repo, query)",
        "function renderRepoBranchButton(branch)",
        "function renderRepoBranchSummary(repo)",
        "function renderRepoBranchToolbar(repo, branch)",
        "function renderRepoBranchMenu(repo, branches, open)",
        "async function toggleRepoBranchMenu(repo, button)",
        "fetchJson(`${repoApiBase(repo)}/branches`)",
        "filterRepoBranches(repo, branches)",
        "branches.map((branch) =>",
        "data-repo-branch-button",
        "data-repo-branch-summary",
        "data-repo-branch-count",
        "data-repo-branch-menu",
        "data-repo-branch-close",
        "data-repo-branch-search",
        "data-repo-branch-name",
        "data-repo-branch-current",
        "data-repo-branch-default",
        "Switch branches",
        "Find a branch...",
        "View all branches",
        "aria-haspopup=\"menu\"",
        "aria-expanded=\"false\"",
        "role=\"menu\"",
        "role=\"menuitemradio\"",
    ):
        assert marker in dashboard_js

    assert "renderRepoBranchToolbar(repo, branch)" in render
    assert "toggleRepoBranchMenu(state.selectedRepo, branchButton);" in click_handler


def test_dashboard_repository_branch_selection_drives_live_mirror_requests():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        "function repoLiveUrl(repo, action, params = {})",
        'query.set("ref", repoSelectedBranch(repo));',
        'return `${repoApiBase(repo)}/${action}?${query.toString()}`;',
        "fetchJson(repoLiveUrl(repo, \"tree\", { path }))",
        "fetchJson(repoLiveUrl(repo, \"blob\", { path }))",
        "fetchRepoJson(repoLiveUrl(repo, \"tree\", { path: config.dir }))",
        "fetchRepoJson(repoLiveUrl(repo, \"blob\", { path: recordPath }))",
        "fetchJson(repoLiveUrl(repo, \"history\"))",
        "fetchJson(repoLiveUrl(repo, \"commit\", { path: hash }))",
        "resetRepoFileFinder(state.selectedRepo);",
        "loadRepositoryTree(state.selectedRepo, \"\");",
        "loadRepoFeaturePanels(state.selectedRepo);",
        'event.target?.matches?.("[data-repo-branch-search]")',
        "setRepoBranchQuery(state.selectedRepo, event.target.value || \"\");",
        "updateRepoBranchControls(state.selectedRepo, event.target.closest(\"[data-repo-branch-control]\"));",
    ):
        assert marker in dashboard_js


def test_dashboard_repository_code_explorer_switches_between_tree_and_file_review():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    render = dashboard_js[
        dashboard_js.index("function renderRepoDetail")
        : dashboard_js.index("function findRepository")
    ]
    tree_loader = dashboard_js[
        dashboard_js.index("async function loadRepositoryTree")
        : dashboard_js.index("async function loadRepositoryBlob")
    ]
    blob_loader = dashboard_js[
        dashboard_js.index("async function loadRepositoryBlob")
        : dashboard_js.index("async function loadRepoCollection")
    ]

    for marker in (
        "data-repo-code-workspace",
        "data-repo-code-explorer",
        "data-repo-explorer-tree",
        "data-repo-code-main",
        "data-repo-tree-panel",
        "data-repo-root-toolbar",
        "data-repo-focus-actions",
        "data-repo-content-grid",
    ):
        assert marker in render

    assert "function renderRepoExplorer(repo, path, entries)" in dashboard_js
    assert "function setRepoExplorerFocusMode(active)" in dashboard_js
    assert 'data-repo-code-explorer class="hidden' in render
    assert 'treePanel?.classList.remove("hidden");' in tree_loader
    assert 'viewer?.classList.add("hidden");' in tree_loader
    assert 'readmePanel?.classList.toggle("hidden", Boolean(path));' in tree_loader
    assert "setRepoExplorerFocusMode(Boolean(path));" in tree_loader
    assert 'treePanel?.classList.add("hidden");' in blob_loader
    assert 'readmePanel?.classList.add("hidden");' in blob_loader
    assert "setRepoExplorerFocusMode(repoPathParts(path).length > 1);" in blob_loader
    assert 'data-repo-file-toolbar' in blob_loader
    assert "Blame" in blob_loader
    assert "Raw" in blob_loader
    assert "function highlightCodeLine(line, path)" in dashboard_js
    assert "highlightCodeLine(line, path)" in dashboard_js[
        dashboard_js.index("function renderRepoFileRows")
        : dashboard_js.index("function setRepoFileMode")
    ]
    file_rows = dashboard_js[
        dashboard_js.index("function renderRepoFileRows")
        : dashboard_js.index("function setRepoFileMode")
    ]
    assert "border-r border-border/70" not in file_rows
    assert "text-sky-300" in dashboard_js
    assert "text-emerald-300" in dashboard_js
    assert "text-amber-300" in dashboard_js


def test_dashboard_repository_file_toolbar_actions_are_wired_without_web_edit():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    blob_loader = dashboard_js[
        dashboard_js.index("async function loadRepositoryBlob")
        : dashboard_js.index("async function loadRepoCollection")
    ]

    for marker in (
        'data-repo-file-mode="code"',
        'data-repo-file-mode="blame"',
        'data-repo-file-action="raw"',
        'data-repo-file-action="copy"',
        'data-repo-file-action="download"',
        'data-repo-file-action="fullscreen"',
        "bindRepoFileToolbar(viewer, repo, path, content, lines",
        "function setRepoFileMode(viewer, mode, lines, path, meta)",
        "function bindRepoFileToolbar(viewer, repo, path, content, lines, meta)",
        "function copyTextToClipboard(text)",
        "return true;",
        "return false;",
        "function downloadRepoFile(path, content)",
        "window.setTimeout(() => URL.revokeObjectURL(url), 30000);",
        "function toggleRepoFileFullscreen(viewer)",
    ):
        assert marker in dashboard_js

    assert "setRepoFileMode(viewer, \"code\", lines, path, meta);" in blob_loader
    assert 'aria-label="Copy file contents"' in blob_loader
    assert "const copied = await copyTextToClipboard(content);" in dashboard_js
    assert 'updateRepoFileStatus(viewer, "Could not copy file contents.", "bad");' in dashboard_js
    assert "renderRepoFileRows(lines, path, mode, meta)" in dashboard_js

    for marker in (
        'data-repo-file-action="edit"',
        'aria-label="Edit local draft"',
        "data-repo-file-editor",
        'data-repo-file-action="copy-draft"',
        'data-repo-file-action="download-draft"',
        "Local draft only",
    ):
        assert marker not in dashboard_js


def test_dashboard_repository_blob_viewer_previews_media_csv_pdf_and_binary():
    dashboard_js = _read(PUBLIC / "dashboard.js")
    blob_loader = dashboard_js[
        dashboard_js.index("async function loadRepositoryBlob")
        : dashboard_js.index("async function loadRepoCollection")
    ]

    for marker in (
        "function repoPreviewKindFromPath(path)",
        "function repoRawUrl(repo, path)",
        "function repoPreviewMeta(data, repo, path)",
        "function renderRepoMediaPreview(meta)",
        "function renderRepoCsvPreview(meta)",
        "function renderRepoUnsupportedBinary(meta)",
        "function renderRepoPreview(viewer, repo, path, data)",
        'data-repo-media-preview',
        'data-repo-csv-preview',
        'data-repo-binary-preview',
        "<video",
        "<audio",
        "<iframe",
        'href="${escapeHtml(meta.rawUrl)}"',
        'image/avif',
        'image/svg+xml',
        'video/mp4',
        'video/quicktime',
        'video/webm',
        'audio/mpeg',
        'audio/flac',
        'application/pdf',
        'text/csv',
        'text/tab-separated-values',
    ):
        assert marker in dashboard_js

    assert "if (renderRepoPreview(viewer, repo, path, data)) return;" in blob_loader
    assert "setRepoFileMode(viewer, \"code\", lines, path, meta);" in blob_loader


def test_worker_routes_raw_repository_blobs_through_private_gated_host_tunnel():
    assert 'r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history|commit|branches)$"' in ENTRY_TEXT
    assert 'elif host_match.group(3) in ("tree", "blobs", "blob", "raw", "history", "commit", "branches"):' in ENTRY_TEXT
    assert 'if action == "raw":' in ENTRY_TEXT
    assert 'return await self._raw_blob(rel_path, ref)' in ENTRY_TEXT
    assert 'op": "raw-blob"' in ENTRY_TEXT
    # Raw blobs stream chunk-by-chunk through the tunnel (never reassembled in
    # DO memory — buffering large media is what blew the isolate memory limit),
    # keeping the same content-type headers repo_blob_bytes_response used.
    assert '"content-type": repo_blob_content_type(rel_path),' in ENTRY_TEXT
    assert "response, err = await self._stream_request(" in ENTRY_TEXT


def test_worker_and_desktop_host_route_live_repository_branches():
    repo_host = (ROOT.parent / "qt_client" / "src" / "RepoHost.cpp").read_text(encoding="utf-8")
    repo_host_h = (ROOT.parent / "qt_client" / "src" / "RepoHost.h").read_text(encoding="utf-8")

    for marker in (
        'r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history|commit|branches)$"',
        'elif host_match.group(3) in ("tree", "blobs", "blob", "raw", "history", "commit", "branches"):',
        'if action in ("tree", "blob", "history", "commit", "branches"):',
        'ref = (parse_qs(url.query).get("ref", [""])[0] or "").strip()',
        'op = "commits" if action == "history" else action',
        'return await self._tunnel(op, rel_path, ref, served_by)',
        '"ref": ref',
    ):
        assert marker in ENTRY_TEXT

    for marker in (
        'else if (op == "branches")',
        'action = QStringLiteral("list branches")',
        'const QString branch = request.value("ref").toString();',
        'QString displayBranchNameForRef(const QString &ref)',
        'QStringList RepoHost::branchRefCandidates(const QString &branch) const',
        'QString RepoHost::refForBranch(const QString &branch) const',
        '"refs/heads/%1"',
        'ref + QStringLiteral("^{commit}")',
        '"refs/remotes/"',
        'displayBranchNameForRef(ref) == raw',
        'if (seen.contains(name))',
        'QJsonObject RepoHost::buildBranchesReply() const',
        'QJsonObject RepoHost::buildTreeReply(const QString &path, const QString &branch) const',
        'QJsonObject RepoHost::buildBlobReply(const QString &path, const QString &branch) const',
        'QJsonObject RepoHost::buildCommitsReply(const QString &branch) const',
        'void RepoHost::streamRawBlob(const QString &reqId, const QString &path, const QString &branch)',
        'else if (op == "branches")',
        'streamRawBlob(reqId, path, branch);',
        'reply = buildTreeReply(path, branch);',
        # Blob replies build off-thread so large files don't stall the GUI.
        'return blobReplyFor(mirrorPath, path, branch);',
        'reply = buildCommitsReply(branch);',
        'reply = buildBranchesReply();',
        '"for-each-ref"',
        '"--format=%(refname)%x1f%(objectname)%x1f%(committerdate:iso8601)"',
        '"refs/heads/"',
        '{"branches", branches}',
    ):
        assert marker in repo_host
    assert "QStringList branchRefCandidates(const QString &branch) const;" in repo_host_h
    assert "QJsonObject buildBranchesReply() const;" in repo_host_h


def test_desktop_host_streams_raw_repository_blobs_without_json_base64_cap():
    repo_host = (ROOT.parent / "qt_client" / "src" / "RepoHost.cpp").read_text(encoding="utf-8")
    repo_host_h = (ROOT.parent / "qt_client" / "src" / "RepoHost.h").read_text(encoding="utf-8")

    for marker in (
        'else if (op == "raw-blob")',
        'action = QStringLiteral("stream raw file',
        'if (op == "raw-blob")',
        'streamRawBlob(reqId, path, branch);',
        'void RepoHost::streamRawBlob(const QString &reqId, const QString &path, const QString &branch)',
        'runGitStream(reqId, {"-C", m_mirrorPath, "cat-file", "-p", ref + ":" + path}, QByteArray());',
        "void streamRawBlob(const QString &reqId, const QString &path, const QString &branch);",
    ):
        assert marker in (repo_host + repo_host_h)


def test_dashboard_network_chat_uses_real_room_integration_without_mock_messages():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    chat_js = _read(PUBLIC / "dashboard-chat.js")
    visible = _strip_html_comments(dashboard)

    assert 'src="/dashboard-chat.js"' in dashboard
    assert 'CHAT_WS_PATH = "/api/repo/mainnode/forkmesh/rooms/general/ws"' in chat_js
    assert 'ROOM_PASSPHRASE = "forkmesh-shared-room-key-v1"' in chat_js
    assert "deriveRoomKey" in chat_js
    assert "encryptObject" in chat_js
    assert "decryptObject" in chat_js
    assert "WebSocket" in chat_js
    assert "Chat mock data removed" not in dashboard
    assert "const fullMessages" not in dashboard
    assert "const sideMessages" not in dashboard
    assert "notificationToggle" in visible
    assert "notificationModal" in visible
    assert "data-notification-count" in visible
    for mock in (
        "alice-node",
        "relay-eu-1",
        "devbox-kr",
        "sysop-eu",
        "morning - anyone else seeing",
        "Cloudflare had a blip",
    ):
        assert mock not in visible
        assert mock not in chat_js


def test_dashboard_deep_link_assets_binding_is_wired_up():
    # entry.py's /dashboard/* fallback (run_worker_first) calls
    # self.env.ASSETS.fetch(...) to serve the SPA shell for deep links like
    # /dashboard/owner/repo. Without an explicit binding name, env.ASSETS is
    # undefined and that call throws, 500ing every such request.
    assert WRANGLER["assets"].get("binding") == "ASSETS"
    assert 'self.env.ASSETS.fetch(' in ENTRY_TEXT


def test_clean_marketing_routes_target_static_pages():
    for redirect in (
        "/dashboard /dashboard/index.html 200",
        "/dashboard.html /dashboard 308",
        "/desktop /desktop.html 200",
        "/docs /docs/index.html 200",
        "/blog /blogs 308",
    ):
        assert redirect in REDIRECTS

    run_worker_first = WRANGLER["assets"]["run_worker_first"]
    for route in ("/blog", "/docs"):
        assert route in run_worker_first
    for route in ("/desktop", "/blogs"):
        assert route not in run_worker_first


def test_repo_shortcut_is_not_a_redirects_rule_so_assets_are_not_hijacked():
    # A /:owner/:repo rule in _redirects matches real two-segment static assets
    # (e.g. /assets/logo.png, /favicon/site.webmanifest) because Cloudflare always
    # applies _redirects before serving a matching static file — that 308'd those
    # assets and broke the deploy's public-asset check. The shortcut must live in
    # 404.html (post-asset-resolution) instead, so guard against the rule's return.
    rules = [
        line for line in REDIRECTS.splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    for rule in rules:
        assert ":owner" not in rule
        assert ":repo" not in rule
        assert "/dashboard?repo=" not in rule
        # Self-referential 200 rewrites for asset dirs are no-ops (a rewrite to the
        # same path); real files serve directly once nothing else hijacks them.
        assert not rule.startswith("/assets/*")
        assert not rule.startswith("/favicon/*")


def test_repo_shortcut_urls_redirect_to_dashboard_from_404_page():
    html = _read(PUBLIC / "404.html")

    # The 404 page bounces /owner/repo (and tree/blob deep links) to the dashboard.
    # Feature tabs (/issues, /pulls, etc.) are preserved in the bounce.
    assert '"/dashboard/"' in html
    assert 'var dashboardPath = "/dashboard/' in html
    assert "window.location.pathname.split" in html
    assert 'parts[2] === "tree" || parts[2] === "blob"' in html
    assert 'featureTabs' in html
    assert '["commits", "releases", "issues", "pulls", "discussions", "mirrors"]' in html
    # Real site sections must not be treated as repo owners.
    for reserved in ("assets", "favicon", "dashboard", "docs", "blogs"):
        assert '"%s"' % reserved in html


def test_dashboard_restores_feature_tab_on_hard_refresh():
    # Regression test: a page load (not just a client-side tab click or a
    # Back/Forward step) at a feature-tab URL like /owner/repo/issues must
    # restore that tab instead of silently falling back to Code. This was
    # broken because REPO_TAB_ROUTES/repoRouteParts() were referenced by the
    # popstate handler but never defined, and the initial-render path
    # (renderRepoDetail, run from init() on a hard load) never consulted the
    # URL's tab segment at all.
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert "function repoRouteParts()" in dashboard_js
    assert 'const REPO_TAB_ROUTES = ["commits", "releases", "issues", "pulls", "discussions", "mirrors"];' in dashboard_js

    render_start = dashboard_js.index("function renderRepoDetail(repo)")
    render_body = dashboard_js[render_start:dashboard_js.index("\n  function findRepository(key)")]
    assert "const routeParts = repoRouteParts();" in render_body
    # adhoc #182: the owner-only Agents tab is only ever a recognized route for
    # the account that can see it, so tab-route membership goes through
    # repoTabRoutesFor(repo) (REPO_TAB_ROUTES + "agents" when owner/admin)
    # instead of the bare REPO_TAB_ROUTES constant.
    assert 'setRepoTab(repoTabRoutesFor(repo).includes(routeKind) ? routeKind : "code");' in render_body


def test_dashboard_hard_refresh_preserves_tab_through_404_bounce():
    # Regression test: a hard refresh on /owner/repo/issues doesn't hit a
    # static asset, so Cloudflare serves 404.html, which bounces the browser
    # to /dashboard/owner/repo/issues. renderRepoDetail used to normalize that
    # back to a bare /owner/repo URL (it only checked whether the current path
    # *started with* the bare repo path, which is never true for the
    # /dashboard/-prefixed bounce path), silently discarding the /issues
    # segment before the tab-restore logic ever read it back out of the URL.
    dashboard_js = _read(PUBLIC / "dashboard.js")

    render_start = dashboard_js.index("function renderRepoDetail(repo)")
    render_body = dashboard_js[render_start:dashboard_js.index("\n  function findRepository(key)")]

    assert "routeMatchesRepo" in render_body
    assert 'repoTabRoutesFor(repo).includes(routeKind)\n      ? `${repoPathUrl(repo)}/${routeKind}`' in render_body
    assert "navigateHistory(detailPath);" in render_body
    # The old bare-collapse call must be gone.
    assert "const detailPath = repoPathUrl(repo);" not in render_body


def test_desktop_client_install_page_exists():
    html = _read(PUBLIC / "desktop.html")

    assert "<title>Install ForkMesh Desktop" in html
    # Per-OS install sections the homepage buttons deep-link into.
    for anchor in ('id="macos"', 'id="windows"', 'id="linux"'):
        assert anchor in html
    assert "curl -fsSL https://forkmesh.com/install.sh | bash" in html


def test_homepage_hero_has_per_os_install_buttons():
    index = _read(PUBLIC / "index.html")

    assert 'id="install-os-buttons"' in index
    for href in ("/desktop#macos", "/desktop#windows", "/desktop#linux"):
        assert href in index


def test_homepage_network_selector_hides_mobile_beam_and_bars():
    index = _read(PUBLIC / "index.html")
    network = index[
        index.index('<section\n        id="network"')
        : index.index('<section\n        id="solution"')
    ]

    assert "hidden lg:block" in network
    assert "ForkMesh interactive node selector" in network
    assert "top-[-0.5px] h-px" in network


def test_homepage_mobile_mockups_do_not_overflow_by_default():
    index = _read(PUBLIC / "index.html")

    assert "-mr-56" not in index
    assert "relative top-6 w-full" in index
    assert "relative -right-8 top-11" not in index
    assert ".fm-tabs .fm-tab:nth-child(n + 6)" in index
