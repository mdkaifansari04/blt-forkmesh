  function renderRepoDetail(repo) {
    const detail = $("[data-repo-detail]");
    if (!detail || !repo) return;
    // Capture the signed-in participant's workshop continuation parameters
    // before navigateHistory removes the query string from the canonical repo
    // URL. The consumer independently verifies repository, run, result, and
    // commit against the encrypted workshop record before preparing a prompt.
    const workshopDeepLink = workshopAgentDeepLink(repo);
    state.selectedRepo = repo;
    state.repoCollectionPages = { issues: 1, pulls: 1 };
    state.repoMirrors = [];
    state.repoLatestCommit = null;
    state.repoServedBy = null;
    state.agentsView = { agents: [], selectedAgentId: null };
    // A search left over from the previously-open repo must not carry into
    // this one — the search box is rendered immediately (below, via
    // renderRepoCollectionPanel), before the Issues tab's own lazy load
    // would otherwise reset it.
    state.issuesView = { filter: "open", items: [], query: "" };
    // Pull requests and discussions load lazily the first time their tab is
    // opened rather than on every page load. Eagerly fetching every record's
    // blob up front is what flooded the host with requests and tripped the rate
    // limit after a few refreshes; this map remembers which tabs have loaded.
    state.loadedRepoTabs = {};
    // Show the clean, shareable /owner/name URL in the address bar instead of
    // the /dashboard/owner/name... path that 404.html bounces refreshed repo
    // links (including /owner/name/issues etc.) to. Carry over whatever tab or
    // tree/blob suffix the incoming URL already pointed at instead of
    // collapsing it to the bare repo root - otherwise a refresh on the Issues
    // tab would lose its place and land back on Code. Only trust that suffix
    // when the URL is actually addressing THIS repo already (a fresh open from
    // the repo list/sidebar while some other repo's tab URL is showing should
    // still land on Code, not inherit the other repo's tab).
    const routeParts = repoRouteParts();
    const routeRepoKey = routeParts.length >= 2
      ? `${safeDecodeURIComponent(routeParts[0])}/${safeDecodeURIComponent(routeParts[1])}`
      : "";
    const routeMatchesRepo = routeParts.length >= 2
      && repoMatchesKey(repo, routeRepoKey);
    const routeKind = routeMatchesRepo ? routeParts[2] : undefined;
    const routePath = routeMatchesRepo && routeParts.length > 3 ? routeParts.slice(3).map(decodeURIComponent).join("/") : "";
    // /owner/repo/pulls/<N> is a record deep link (the desktop client's
    // "View on website" button, or a refreshed/shared PR detail URL): keep
    // the number in the address bar and open that record's detail page below.
    // Issues deep-link the same way so a refresh on an open issue stays on it.
    const recordRoute = ["pulls", "discussions", "issues"].includes(routeKind) && /^\d+$/.test(routePath)
      ? { kind: routeKind, number: routePath }
      : null;
    const detailPath = recordRoute
      ? `${repoPathUrl(repo)}/${recordRoute.kind}/${recordRoute.number}`
      : repoTabRoutesFor(repo).includes(routeKind)
        ? `${repoPathUrl(repo)}/${routeKind}`
        : repoPathUrl(repo, routeKind === "blob" ? "blob" : "tree", routePath);
    navigateHistory(detailPath);
    const crumb = $("[data-repo-detail-crumb]");
    if (crumb) crumb.textContent = `${repo.owner || "owner"}/${repo.name || "repository"}`;
    const branch = repoSelectedBranch(repo);
    const issuesCount = repoCount(repo, ["issueCount", "issues", "issuesCount", "openIssues"]);
    const pullsCount = repoCount(repo, ["pullCount", "pulls", "pullsCount", "openPulls", "pullRequests"]);
    const discussionsCount = repoCount(repo, ["discussions", "discussionCount"]);
    const commitsCount = repoCount(repo, ["commits", "commitCount", "commitHistory"]);
    const mirrorsCount = repoCount(repo, ["mirrors", "mirrorCount", "hosts"]);
    const live = repoIsLive(repo);
    const viaMirror = repoServedByMirror(repo);
    const canEditAbout = sessionOwnsRepo(repo);
    const readmePath = "README.md";
    const readmeHref = repoPathUrl(repo, "blob", readmePath);
    const tabMeta = {
      code: { label: "Code", icon: "code-2", count: "" },
      commits: { label: "Commits", icon: "git-commit-horizontal", count: commitsCount },
      insights: { label: "Insights", icon: "chart-no-axes-combined", count: "" },
      sizemap: { label: "Size map", icon: "chart-pie", count: "" },
      releases: { label: "Releases", icon: "tag", count: "" },
      issues: { label: "Issues", icon: "circle-dot", count: issuesCount },
      projects: { label: "Projects", icon: "chart-gantt", count: "" },
      pulls: { label: "Pull requests", icon: "git-pull-request", count: pullsCount },
      discussions: { label: "Discussions", icon: "message-square", count: discussionsCount },
      mirrors: { label: "Mirrors", icon: "radio", count: mirrorsCount },
      agents: { label: "Agents", icon: "bot", count: "" },
      settings: { label: "Settings", icon: "settings", count: "" },
    };
    const canSeeAgentsTab = sessionCanAssignAgent(repo);
    const canSeeSettingsTab = canEditAbout;
    const actionSeed = repoKey(repo);
    const forkCount = stableMockNumber(`${actionSeed}:fork`, 0, 12);
    // Watch is real: it is the repo's fediverse follower count (see
    // loadRepoFediverse), and the button opens the follow-from-Mastodon card.
    // The public repository actor is the canonical ForkMesh identity. Do not
    // derive it from a mirror hostname or replica owner name.
    const fediHandle = "@forkmesh.forkmesh@forkmesh.com";
    // Deep link that opens this repo's fediverse actor on Mastodon (any
    // instance resolves a remote acct handle; mastodon.social is the default).
    const mastodonUrl = `https://mastodon.social/${fediHandle}`;
    const settingsPanel = canSeeSettingsTab ? `
      <section data-dashboard-repo-tab-panel="settings" class="hidden">
        <div class="mt-4 overflow-hidden rounded-lg border border-border bg-background">
          <div class="border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="settings" class="h-3.5 w-3.5 text-primary"></i>Repository settings</span>
          </div>
          <form data-repo-settings-form class="grid gap-4 p-4">
            <fieldset class="grid gap-2 rounded-md border border-border p-3 text-xs text-muted-foreground">
              <legend class="px-1 text-[11px] font-semibold uppercase tracking-wide">ActivityPub federation</legend>
              <label class="inline-flex items-start gap-2"><input data-repo-ap-federate type="checkbox" class="mt-0.5 h-3.5 w-3.5" checked />Federate this repository (fediverse actor and handle)</label>
              <label class="inline-flex items-start gap-2"><input data-repo-ap-broadcast type="checkbox" class="mt-0.5 h-3.5 w-3.5" checked />Include meaningful public updates in one automated digest at most every 24 hours</label>
              <label class="inline-flex items-start gap-2"><input data-repo-ap-comments type="checkbox" class="mt-0.5 h-3.5 w-3.5" checked />Accept fediverse replies as federated comments</label>
              ${repo.isPrivate ? "" : `
              <div class="mt-1 rounded-md border border-border bg-secondary/40 p-3">
                <div class="flex items-center justify-between gap-2">
                  <span class="font-semibold text-foreground">Daily digest preview</span>
                  <button type="button" data-repo-digest-preview-refresh class="rounded-md border border-border px-2 py-1 text-[10px] font-semibold text-foreground hover:bg-secondary">Refresh</button>
                </div>
                <p class="mt-1 leading-5">Only public titles, stable links, categories and UTC times enter the encrypted bounded queue. Empty digests are never posted.</p>
                <pre data-repo-digest-preview class="mt-2 max-h-64 overflow-auto whitespace-pre-wrap break-words rounded bg-background p-3 font-mono text-[11px] leading-5 text-foreground">Loading preview…</pre>
              </div>`}
            </fieldset>
            <p class="text-[11px] leading-5 text-muted-foreground">Saved to the relay now and written into the repository's committed <span class="font-mono">.forkmesh/info.json</span> the next time the owner's node syncs.</p>
            <div class="flex flex-wrap items-center justify-between gap-2">
              <span data-repo-settings-status class="text-[11px] text-muted-foreground"></span>
              <button type="submit" class="inline-flex h-8 items-center rounded-md bg-primary px-3 text-xs font-medium text-primary-foreground hover:bg-primary/90">Save settings</button>
            </div>
          </form>
        </div>
        <div data-repo-danger-zone class="mt-5 overflow-hidden rounded-lg border border-destructive/50 bg-background">
          <div class="border-b border-destructive/30 px-4 py-3">
            <h3 class="text-sm font-semibold text-destructive">Danger zone</h3>
          </div>
          <div class="flex flex-col gap-3 p-4 sm:flex-row sm:items-center sm:justify-between">
            <div>
              <p class="text-sm font-medium text-foreground">Delete this repository</p>
              <p class="mt-1 text-xs leading-5 text-muted-foreground">Removes the repository from ForkMesh, including relay metadata, pending inboxes, agents, mirror presence, and repository chat history. Source files on the owner's device are not erased.</p>
              <p data-repo-delete-status class="mt-1 text-xs text-muted-foreground"></p>
            </div>
            <button type="button" data-repo-delete class="inline-flex h-8 shrink-0 items-center justify-center gap-1.5 rounded-md border border-destructive/60 px-3 text-xs font-semibold text-destructive hover:bg-destructive/10"><i data-lucide="trash-2" class="h-3.5 w-3.5"></i>Delete repository</button>
          </div>
        </div>
      </section>` : "";
    detail.innerHTML = `
      <div data-repo-layout="github-like" class="min-w-0">
        <div data-repo-github-header class="rounded-t-lg border border-border bg-background">
          <div class="grid gap-4 border-b border-border p-4 lg:grid-cols-[minmax(0,1fr)_auto]">
            <div class="min-w-0">
              <div class="flex min-w-0 flex-wrap items-center gap-2">
                <i data-lucide="book-marked" class="h-4 w-4 text-muted-foreground"></i>
                <h2 class="min-w-0 truncate text-lg font-semibold text-foreground"><span class="text-muted-foreground"><a href="/@${encodeURIComponent(String(repo.owner || "").toLowerCase())}" data-repo-owner-link class="hover:text-foreground hover:underline">${escapeHtml(repo.owner || "owner")}</a>/</span>${escapeHtml(repo.name || "repository")}</h2>
                <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${repo.isPrivate ? "private" : "public"}</span>
                ${repositoryTermsBadge(repo)}
                <span data-repo-availability-status class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono ${live ? "text-primary" : "text-muted-foreground"}">${viaMirror ? "served by mirror" : live ? "mirror online" : "mirror offline"}</span>
              </div>
              <p class="mt-2 max-w-3xl text-sm leading-6 text-muted-foreground">${escapeHtml(repo.description || "No description published.")}</p>
            </div>
            <div aria-label="Repository facts" class="flex flex-wrap items-start gap-2 lg:justify-end">
              <div data-repo-watch-wrap class="relative">
                <button type="button" data-repo-action-watch class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-background">
                  <i data-lucide="eye" class="h-3.5 w-3.5 text-muted-foreground"></i>
                  Watch
                  <span data-repo-watch-count class="rounded-full bg-background px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">–</span>
                  <i data-lucide="chevron-down" class="h-3 w-3 text-muted-foreground"></i>
                </button>
                <div data-repo-watch-menu class="absolute right-0 z-30 mt-1 hidden w-72 rounded-lg border border-border bg-background p-3 text-left shadow-xl">
                  <p class="text-xs font-semibold text-foreground">Watch on the fediverse</p>
                  <p class="mt-1 text-[11px] leading-5 text-muted-foreground">Follow this repository from Mastodon (or any ActivityPub app) to get new issues, pull requests, discussions and releases in your feed.</p>
                  <div class="mt-2 flex items-center gap-2">
                    <code data-repo-watch-handle class="min-w-0 flex-1 truncate rounded-md border border-border bg-secondary px-2 py-1 font-mono text-[11px] text-foreground">${escapeHtml(fediHandle)}</code>
                    <button type="button" data-dashboard-copy="${escapeHtml(fediHandle)}" aria-label="Copy fediverse handle" class="copy-button inline-flex h-7 w-7 shrink-0 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="copy" class="copy-icon h-3.5 w-3.5"></i><i data-lucide="check" class="copy-check h-3.5 w-3.5"></i></button>
                  </div>
                  <p class="mt-2 text-[11px] text-muted-foreground"><span data-repo-watch-followers class="font-mono text-foreground">–</span> fediverse watchers</p>
                  <p data-repo-watch-disabled class="mt-1 hidden text-[11px] text-yellow-500">Federation is turned off for this repository.</p>
                  <div data-repo-watch-list class="mt-2 hidden max-h-44 overflow-auto rounded-md border border-border bg-secondary/40 p-2"></div>
                  <a data-repo-mastodon-link href="${escapeHtml(mastodonUrl)}" target="_blank" rel="noopener noreferrer" class="dashboard-accent-link mt-2 inline-flex items-center gap-1.5 text-[11px] hover:underline"><i data-lucide="external-link" class="h-3.5 w-3.5 shrink-0"></i>View on Mastodon</a>
                </div>
              </div>
              <button type="button" data-repo-action-fork class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-background">
                <i data-lucide="git-fork" class="h-3.5 w-3.5 text-muted-foreground"></i>
                Fork
                <span class="rounded-full bg-background px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">${formatCount(forkCount)}</span>
                <i data-lucide="chevron-down" class="h-3 w-3 text-muted-foreground"></i>
              </button>
              <button type="button" data-repo-action-star data-repo-key="${escapeHtml(actionSeed)}" aria-pressed="false" class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-background">
                <i data-lucide="star" data-repo-star-icon class="h-3.5 w-3.5 text-muted-foreground"></i>
                <span data-repo-star-label>Star</span>
                <span data-repo-star-count class="rounded-full bg-background px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">${formatCount(0)}</span>
              </button>
              <button type="button" data-dashboard-repo-tab="mirrors" aria-label="Show repository mirror status" class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-background"><i data-lucide="radio" class="h-3.5 w-3.5 text-muted-foreground"></i>Mirrors <span data-dashboard-repo-count="mirrors" class="rounded-full bg-background px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">${tabCountLabel(mirrorsCount)}</span></button>
            </div>
          </div>
          <div class="flex min-w-0 overflow-x-auto px-3" role="tablist">
            ${["code", "commits", "insights", "sizemap", "releases", "issues", "projects", "pulls", "discussions", "mirrors", ...(canSeeAgentsTab ? ["agents"] : []), ...(canSeeSettingsTab ? ["settings"] : [])].map((tab) => {
              const meta = tabMeta[tab];
              const iconAttr = tab === "issues"
                ? 'data-lucide="circle-dot"'
                : tab === "pulls"
                  ? 'data-lucide="git-pull-request"'
                  : `data-lucide="${meta.icon}"`;
              // Inbox-backed tabs get a second (hidden until filled) badge for
              // items still sitting in the relay's inbox awaiting the owner
              // node's next sync — see loadRepoPendingCounts.
              const pendingBadge = ["issues", "pulls", "discussions", "commits"].includes(tab)
                ? `<span data-dashboard-repo-tab-pending="${tab}" class="hidden rounded-full border border-yellow-500/40 bg-yellow-500/10 px-1.5 py-0.5 text-[10px] font-mono text-yellow-500"></span>`
                : "";
              return `<button type="button" role="tab" data-dashboard-repo-tab="${tab}" aria-selected="${tab === "code" ? "true" : "false"}" class="relative inline-flex h-12 items-center gap-2 border-b-2 px-3 text-xs font-medium transition-colors ${tab === "code" ? "border-primary text-foreground" : "border-transparent text-muted-foreground hover:bg-secondary hover:text-foreground"}"><i ${iconAttr} class="h-3.5 w-3.5"></i><span>${meta.label}</span>${meta.count !== "" ? `<span data-dashboard-repo-tab-count="${tab}" class="rounded-full bg-secondary px-1.5 py-0.5 text-[10px] font-mono text-muted-foreground">${tabCountLabel(meta.count)}</span>` : ""}${pendingBadge}</button>`;
            }).join("")}
          </div>
        </div>
	        <div data-repo-content-grid class="grid min-w-0 gap-5 pt-5 lg:grid-cols-[minmax(0,1fr)_18rem]">
		          <div class="min-w-0">
		            <section data-dashboard-repo-tab-panel="code">
		              <div data-repo-root-toolbar class="grid gap-2 md:grid-cols-[auto_minmax(0,1fr)_auto_auto]">
		                ${renderRepoBranchToolbar(repo, branch)}
		                <button type="button" data-repo-file-finder-open class="inline-flex h-9 min-w-0 items-center gap-2 rounded-md border border-border bg-background px-3 text-left text-xs text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="search" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Go to file</span><span class="ml-auto hidden rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground sm:inline">T</span></button>
		                <button type="button" aria-disabled="true" class="inline-flex h-9 items-center justify-center gap-2 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-background"><i data-lucide="plus" class="h-3.5 w-3.5 text-muted-foreground"></i>Add file<i data-lucide="chevron-down" class="h-3 w-3 text-muted-foreground"></i></button>
		                ${renderRepoCodeButton(repo, false)}
		              </div>
		              <div data-repo-pathbar class="my-3 flex min-w-0 flex-col gap-2 md:flex-row md:items-center md:justify-between">
			                <div class="flex min-w-0 items-center gap-2">
			                  <div class="min-w-0 truncate text-xs text-muted-foreground" data-repo-breadcrumb></div>
			                  <span data-repo-served-by hidden title="Mirror node that served this page (round-robined across online mirrors)" class="shrink-0 items-center gap-1 rounded-full border border-border bg-background px-2 py-0.5 font-mono text-[10px] text-muted-foreground"></span>
			                </div>
		                <div data-repo-focus-actions class="hidden flex shrink-0 flex-wrap items-center gap-2">
		                  <button type="button" data-repo-file-finder-open class="inline-flex h-8 min-w-0 items-center gap-2 rounded-md border border-border bg-background px-3 text-left text-xs text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="search" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Go to file</span><span class="ml-auto rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">T</span></button>
		                  <button type="button" aria-disabled="true" class="inline-flex h-8 items-center justify-center gap-2 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-background"><i data-lucide="plus" class="h-3.5 w-3.5 text-muted-foreground"></i>Add file</button>
		                  ${renderRepoCodeButton(repo, true)}
		                </div>
		              </div>
		              <div data-repo-code-workspace class="min-w-0 gap-4">
		                <aside data-repo-code-explorer class="hidden min-w-0 overflow-hidden rounded-lg border border-border bg-background" data-repo-code-sidebar>
		                  <div class="flex h-11 items-center gap-2 border-b border-border bg-secondary/40 px-3 text-sm font-semibold text-foreground">
		                    <i data-lucide="panel-left" class="h-3.5 w-3.5 text-muted-foreground"></i>
		                    Files
	                  </div>
	                  <div class="grid gap-2 border-b border-border p-3">
	                    ${renderRepoBranchToolbar(repo, branch)}
	                    <button type="button" data-repo-file-finder-open class="inline-flex h-8 min-w-0 items-center gap-2 rounded-md border border-border bg-background px-3 text-left text-xs text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="search" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Go to file</span><span class="ml-auto rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">T</span></button>
		                  </div>
		                  <div data-repo-explorer-tree class="max-h-[35rem] overflow-auto py-2"></div>
		                </aside>
		                <div data-repo-code-main class="min-w-0">
		                  <div data-repo-tree-panel data-repo-file-table class="overflow-hidden rounded-lg border border-border bg-background">
	                    <div data-repo-commit-summary class="grid gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs sm:grid-cols-[minmax(0,1fr)_auto_auto_auto] sm:items-center">
	                      <div class="flex min-w-0 items-center gap-2">
	                        <span data-repo-commit-avatar class="flex h-6 w-6 shrink-0 items-center justify-center rounded-full border border-primary/30 bg-primary/10 font-mono text-[10px] font-semibold text-primary">${escapeHtml((repo.owner || "F")[0] || "F").toUpperCase()}</span>
	                        <span data-repo-commit-author class="min-w-0 truncate text-foreground font-medium"><span class="inline-block h-3.5 w-24 max-w-full animate-pulse rounded bg-muted-foreground/20 align-middle"></span></span>
	                        <span data-repo-commit-message class="min-w-0 truncate text-muted-foreground"><span class="inline-block h-3.5 w-40 max-w-full animate-pulse rounded bg-muted-foreground/20 align-middle"></span></span>
	                      </div>
	                      <span data-repo-commit-hash class="font-mono text-muted-foreground"><span class="inline-block h-3.5 w-14 animate-pulse rounded bg-muted-foreground/20 align-middle"></span></span>
	                      <span data-repo-commit-date class="font-mono text-muted-foreground"><span class="inline-block h-3.5 w-16 animate-pulse rounded bg-muted-foreground/20 align-middle"></span></span>
	                      <button type="button" data-dashboard-history-button aria-label="Open commit history" class="inline-flex items-center gap-1 font-medium text-foreground hover:text-primary transition-colors"><i data-lucide="history" class="h-3.5 w-3.5 text-muted-foreground"></i>History</button>
	                    </div>
	                    <div data-repo-tree></div>
	                  </div>
	                  <div data-repo-blob class="hidden"></div>
	                  <section data-repo-readme class="mt-4 overflow-hidden rounded-lg border border-border bg-background">
	                    <div data-repo-readme-filename class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="book-open" class="h-3.5 w-3.5 text-muted-foreground"></i>README.md</div>
	                    <div data-repo-readme-body class="p-4 text-sm leading-6 text-muted-foreground">
	                      <p class="mt-1">${loadingHtml("Loading README...")}</p>
	                    </div>
	                  </section>
	                </div>
	              </div>
	              <div data-repo-file-finder class="fixed inset-0 z-50 hidden items-start justify-center bg-background/80 p-4 pt-20 backdrop-blur-sm" role="dialog" aria-modal="true" aria-label="Go to file">
	                <button type="button" data-repo-file-finder-backdrop class="absolute inset-0 cursor-default" aria-label="Close file finder"></button>
	                <div class="relative z-10 grid w-full max-w-2xl overflow-hidden rounded-xl border border-border bg-background shadow-2xl">
	                  <div class="flex items-center gap-3 border-b border-border px-4 py-3">
	                    <i data-lucide="search" class="h-4 w-4 text-muted-foreground"></i>
	                    <input data-repo-file-finder-input type="search" autocomplete="off" spellcheck="false" placeholder="Search files in this live mirror" class="min-w-0 flex-1 bg-transparent font-mono text-sm text-foreground outline-none placeholder:text-muted-foreground" />
	                    <button type="button" data-repo-file-finder-close class="rounded-md border border-border px-2 py-1 text-xs text-muted-foreground hover:bg-secondary hover:text-foreground">Esc</button>
	                  </div>
	                  <div data-repo-file-finder-status class="border-b border-border px-4 py-2 text-xs text-muted-foreground">Type to search files.</div>
	                  <div data-repo-file-finder-results class="max-h-[26rem] overflow-auto p-2"></div>
	                </div>
	              </div>
		            </section>
            <section data-dashboard-repo-tab-panel="commits" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="git-commit-horizontal" class="h-3.5 w-3.5 text-muted-foreground"></i>Commits</span><span class="font-mono text-[10px] text-muted-foreground">live mirror history</span></div><div data-repo-commits></div></div></section>
            <section data-dashboard-repo-tab-panel="releases" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="tag" class="h-3.5 w-3.5 text-primary"></i>Releases</span><span class="font-mono text-[10px] text-muted-foreground">signed release manifests</span></div><div data-repo-releases></div></div></section>
            ${renderRepoCollectionPanel("issues", repo, issuesCount, repoCount(repo, ["closedIssues", "closedIssueCount"]))}
            <section data-dashboard-repo-tab-panel="projects" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="chart-gantt" class="h-3.5 w-3.5 text-primary"></i>Projects</span><span class="font-mono text-[10px] text-muted-foreground">linked issues · milestones · gantt</span></div><div data-repo-projects></div></div></section>
            ${renderRepoCollectionPanel("pulls", repo, pullsCount, repoCount(repo, ["closedPulls", "closedPullCount"]))}
            <section data-dashboard-repo-tab-panel="discussions" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="message-square" class="h-3.5 w-3.5 text-muted-foreground"></i>Discussions and comments</span><span class="rounded-md border border-border px-3 py-1.5 text-xs text-muted-foreground">Create from desktop client for signed submissions</span></div><div data-repo-discussions></div></div></section>
            <section data-dashboard-repo-tab-panel="insights" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="chart-no-axes-combined" class="h-3.5 w-3.5 text-muted-foreground"></i>Insights</span><span class="font-mono text-[10px] text-muted-foreground">contributors and activity</span></div><div data-repo-insights></div></div></section>
            <section data-dashboard-repo-tab-panel="sizemap" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="chart-pie" class="h-3.5 w-3.5 text-primary"></i>Size map</span><span class="font-mono text-[10px] text-muted-foreground">directory sizes · default branch</span></div><div data-repo-sizemap class="p-4"></div></div></section>
            <section data-dashboard-repo-tab-panel="mirrors" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="radio" class="h-3.5 w-3.5 text-primary"></i>Mirrors</span><span class="font-mono text-[10px] text-muted-foreground">reachable mirror health</span></div><div data-mirror-request hidden class="border-b border-border px-4 py-3"><label class="mb-1.5 block text-[11px] font-medium text-foreground">Ask a node to mirror this repo</label><div class="flex items-center gap-2"><input data-mirror-request-target type="text" autocomplete="off" spellcheck="false" placeholder="node name" class="h-8 min-w-0 flex-1 rounded-md border border-border bg-background px-2 font-mono text-xs text-foreground outline-none placeholder:text-muted-foreground" /><button type="button" data-mirror-request-send class="h-8 shrink-0 rounded-md border border-border bg-secondary px-3 text-xs font-medium text-foreground transition-colors hover:bg-secondary/70">Ask to mirror</button></div><p data-mirror-request-hint class="mt-1.5 text-[11px] text-muted-foreground">They get a ping; if they accept, their node starts mirroring your repo.</p></div><div data-repo-mirrors></div></div></section>
            ${canSeeAgentsTab ? `<section data-dashboard-repo-tab-panel="agents" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="bot" class="h-3.5 w-3.5 text-primary"></i>Agents</span><button type="button" data-repo-agents-refresh class="inline-flex h-7 items-center gap-1.5 rounded-md border border-border px-2.5 text-xs font-medium text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="refresh-cw" class="h-3.5 w-3.5"></i>Refresh</button></div><div data-workshop-agent-context hidden></div><div data-repo-agents></div></div></section>` : ""}
            ${settingsPanel}
          </div>
          <aside data-repo-about data-repo-about-rail class="min-w-0 rounded-lg border border-border bg-background p-4">
            ${repo.isPrivate ? "" : `
            <div data-repo-social-badge class="-mx-4 -mt-4 mb-4 overflow-hidden rounded-t-lg border-b border-border">
              <div data-repo-social-banner class="h-20 w-full bg-secondary bg-cover bg-center" style="background-image:url('/assets/fediverse-banner.png')"></div>
              <div class="flex items-end gap-3 px-4 pb-3">
                <img data-repo-social-logo alt="Repository logo" class="-mt-7 hidden h-14 w-14 shrink-0 rounded-xl border-2 border-background bg-background object-cover shadow" />
                <div class="min-w-0 pb-0.5">
                  <a data-repo-social-handle data-repo-mastodon-link href="${escapeHtml(mastodonUrl)}" target="_blank" rel="noopener noreferrer" title="${escapeHtml(fediHandle)}" class="block min-w-0 truncate font-mono text-[11px] text-foreground hover:underline">${escapeHtml(fediHandle)}</a>
                  <div class="text-[11px] text-muted-foreground"><span data-repo-social-followers class="font-mono text-foreground">–</span> fediverse watchers</div>
                </div>
              </div>
            </div>`}
            <div class="flex items-center justify-between gap-3">
              <h3 class="text-sm font-semibold text-foreground">About</h3>
              ${canEditAbout
                ? `<button type="button" data-repo-about-edit aria-label="Edit About" class="inline-flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="pencil" class="h-3.5 w-3.5"></i></button>`
                : `<i data-lucide="pencil" class="h-3.5 w-3.5 text-muted-foreground"></i>`}
            </div>
            <p data-repo-about-description class="mt-3 text-sm leading-6 text-foreground">${escapeHtml(repo.description || "No description published.")}</p>
            <a data-repo-about-website href="#" target="_blank" rel="noopener noreferrer" class="dashboard-accent-link mt-1 hidden min-w-0 items-center gap-1.5 text-xs hover:underline"><i data-lucide="globe" class="h-3.5 w-3.5 shrink-0"></i><span data-repo-about-website-label class="min-w-0 truncate"></span></a>
            <form data-repo-about-form class="mt-3 hidden grid gap-2">
              <textarea data-repo-about-input rows="4" maxlength="240" class="min-h-24 rounded-md border border-border bg-background px-3 py-2 text-sm text-foreground outline-none focus:border-primary">${escapeHtml(repo.description || "")}</textarea>
              <label class="grid gap-1 text-[11px] text-muted-foreground">Website
                <input data-repo-about-website-input type="url" maxlength="240" placeholder="https://example.com" class="h-8 rounded-md border border-border bg-background px-3 text-sm text-foreground outline-none focus:border-primary" />
              </label>
              <label class="grid gap-1 text-[11px] text-muted-foreground">Logo — square PNG, up to 256 KB (fediverse avatar)
                <input data-repo-about-logo type="file" accept="image/png" class="text-xs text-muted-foreground file:mr-2 file:rounded-md file:border file:border-border file:bg-secondary file:px-2 file:py-1 file:text-xs file:text-foreground" />
              </label>
              <label class="grid gap-1 text-[11px] text-muted-foreground">Banner — 1500×500 PNG, up to 1 MB (fediverse header)
                <input data-repo-about-banner type="file" accept="image/png" class="text-xs text-muted-foreground file:mr-2 file:rounded-md file:border file:border-border file:bg-secondary file:px-2 file:py-1 file:text-xs file:text-foreground" />
              </label>
              <span class="flex flex-wrap gap-3 text-[11px] text-muted-foreground">
                <label class="inline-flex items-center gap-1.5"><input data-repo-about-logo-clear type="checkbox" class="h-3 w-3" />Remove logo</label>
                <label class="inline-flex items-center gap-1.5"><input data-repo-about-banner-clear type="checkbox" class="h-3 w-3" />Remove banner</label>
              </span>
              <div class="flex flex-wrap items-center justify-between gap-2">
                <span data-repo-about-status class="text-[11px] text-muted-foreground"></span>
                <span class="inline-flex items-center gap-2">
                  <button type="button" data-repo-about-cancel class="inline-flex h-8 items-center rounded-md border border-border px-3 text-xs font-medium text-muted-foreground hover:bg-secondary hover:text-foreground">Cancel</button>
                  <button type="submit" class="inline-flex h-8 items-center rounded-md bg-primary px-3 text-xs font-medium text-primary-foreground hover:bg-primary/90">Save</button>
                </span>
              </div>
            </form>
            ${repo.isPrivate ? "" : `
            <details data-repo-logo-workflow class="mt-4 border-t border-border pt-4">
              <summary class="flex cursor-pointer list-none items-center justify-between gap-2 text-xs font-semibold uppercase tracking-wide text-muted-foreground">
                <span class="inline-flex items-center gap-1.5"><i data-lucide="image-plus" class="h-3.5 w-3.5"></i>Repository logo</span>
                <i data-lucide="chevron-down" class="h-3.5 w-3.5"></i>
              </summary>
              <p class="mt-2 text-[11px] leading-4 text-muted-foreground">ForkMesh generates an original local mark from public repository metadata when no approved logo exists. Signed-in community members may suggest PNG, JPEG, or WebP artwork; the repository owner or a moderator must approve it before it becomes official.</p>
              <form data-repo-logo-suggestion-form class="mt-3 grid gap-2">
                <input data-repo-logo-suggestion-file type="file" accept="image/png,image/jpeg,image/webp" required class="text-xs text-muted-foreground file:mr-2 file:rounded-md file:border file:border-border file:bg-secondary file:px-2 file:py-1 file:text-xs file:text-foreground" />
                <input data-repo-logo-suggestion-attribution type="text" maxlength="240" placeholder="Artwork attribution (optional)" class="h-8 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary" />
                <label class="inline-flex items-start gap-1.5 text-[11px] text-muted-foreground"><input data-repo-logo-suggestion-rights type="checkbox" required class="mt-0.5 h-3 w-3" />I created this image or have permission to submit it.</label>
                <label class="inline-flex items-start gap-1.5 text-[11px] text-muted-foreground"><input data-repo-logo-suggestion-ai type="checkbox" class="mt-0.5 h-3 w-3" />This image was AI-generated.</label>
                <div class="flex items-center justify-between gap-2">
                  <span data-repo-logo-suggestion-status class="text-[11px] text-muted-foreground"></span>
                  <button type="submit" class="h-8 rounded-md border border-border bg-secondary px-3 text-xs font-medium text-foreground hover:bg-secondary/70">Suggest logo</button>
                </div>
              </form>
              <div data-repo-logo-suggestions class="mt-3 grid gap-2"></div>
            </details>`}
            <div class="mt-4 grid gap-2 text-xs text-muted-foreground">
              <a href="${escapeHtml(cloneUrl(repo))}" class="dashboard-accent-link inline-flex min-w-0 items-center gap-2 hover:underline"><i data-lucide="link" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Open clean URL</span></a>
              <a href="${escapeHtml(readmeHref)}" data-repo-readme-link data-repo-readme-path="${escapeHtml(readmePath)}" class="inline-flex min-w-0 items-center gap-2 hover:text-foreground hover:underline"><i data-lucide="book-open" class="h-3.5 w-3.5"></i><span>Readme</span></a>
              <a href="${escapeHtml(`${repoPathUrl(repo)}/insights`)}" data-repo-activity-link class="inline-flex min-w-0 items-center gap-2 hover:text-foreground hover:underline"><i data-lucide="activity" class="h-3.5 w-3.5"></i><span>Activity</span></a>
            </div>
            ${canEditAbout && !repo.isPrivate ? `
            <details data-repo-fedi-posts class="mt-5 border-t border-border pt-4">
              <summary class="flex cursor-pointer list-none items-center justify-between gap-2 text-xs font-semibold uppercase tracking-wide text-muted-foreground"><span class="inline-flex items-center gap-1.5"><i data-lucide="megaphone" class="h-3.5 w-3.5"></i>Fediverse posts</span><i data-lucide="chevron-down" class="h-3.5 w-3.5 shrink-0 transition-transform"></i></summary>
              <p class="mt-2 text-[11px] leading-4 text-muted-foreground">Posts this repository published to its Mastodon followers. Deleting one sends a removal to every follower's server so it disappears from their timelines.</p>
              <div data-repo-fedi-posts-list class="mt-2 grid gap-2"></div>
            </details>` : ""}
            <div data-repo-about-release class="mt-5 hidden border-t border-border pt-4">
              <h4 class="text-xs font-semibold uppercase tracking-wide text-muted-foreground">Latest release</h4>
              <div data-repo-about-release-body class="mt-2 text-xs text-muted-foreground"></div>
            </div>
            <div data-repo-about-langs class="mt-5 hidden border-t border-border pt-4">
              <h4 class="text-xs font-semibold uppercase tracking-wide text-muted-foreground">Languages</h4>
              <div data-repo-about-langs-bar class="mt-2 flex h-2 w-full overflow-hidden rounded-full bg-secondary"></div>
              <div data-repo-about-langs-legend class="mt-2 flex flex-wrap gap-x-3 gap-y-1 text-[11px] text-muted-foreground"></div>
            </div>
            <div data-repo-about-files class="mt-5 hidden border-t border-border pt-4">
              <h4 class="text-xs font-semibold uppercase tracking-wide text-muted-foreground">Files</h4>
              <p data-repo-about-files-count class="mt-2 text-xs text-muted-foreground"></p>
            </div>
            <div data-repo-about-sizemap class="mt-5 hidden border-t border-border pt-4">
              <h4 class="text-xs font-semibold uppercase tracking-wide text-muted-foreground">Size map</h4>
              <button type="button" data-repo-about-sizemap-open class="mt-2 block w-full rounded-md p-1 transition-colors hover:bg-secondary/50" title="Open the size map tab" aria-label="Open the size map tab">
                <span data-repo-about-sizemap-chart class="block"></span>
              </button>
              <p class="mt-1.5 text-[11px] text-muted-foreground">Directory sizes on the default branch — open the <span class="text-foreground">Size map</span> tab to explore.</p>
            </div>
            <div data-repo-about-contribs class="mt-5 hidden border-t border-border pt-4">
              <h4 class="text-xs font-semibold uppercase tracking-wide text-muted-foreground">Contributors <span data-repo-about-contribs-count class="font-mono text-foreground"></span></h4>
              <div data-repo-about-contribs-list class="mt-2 flex flex-wrap gap-1.5"></div>
            </div>
            <div data-repo-live-summary class="mt-5 border-t border-border pt-4">
              <h4 class="text-xs font-semibold text-foreground">Live mirror</h4>
              <dl class="mt-3 grid gap-3 text-xs">
                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Mirrors</dt><dd data-dashboard-repo-count="mirrors" class="min-w-0 truncate text-right text-foreground font-mono">${tabCountLabel(mirrorsCount)}</dd></div>
              </dl>
              <div data-repo-live-mirror-list class="mt-3 overflow-hidden rounded-md border border-border"></div>
            </div>
          </aside>
        </div>
      </div>`;
    // Restore whichever tab the URL points at (e.g. a refresh on
    // /owner/repo/issues) instead of always defaulting back to Code. This
    // runs right after painting the DOM, before anything else that could
    // throw (icon rendering, feature-panel loads) — otherwise a later error
    // would leave the page stuck showing Code even though the URL (and the
    // markup underneath) is already on the right tab.
    const initialTab = repoTabRoutesFor(repo).includes(routeKind) ? routeKind : "code";
    setRepoTab(initialTab);
    window.lucide?.createIcons();
    // When the URL restored a feature tab (or a record detail), the tree/README
    // load is only a warm-up for a later click on Code — run it in background
    // mode so it can't flip the visible tab or rewrite the restored URL.
    loadRepositoryTree(repo, routeKind === "tree" ? routePath : "", { background: initialTab !== "code" });
    // A deep link into a subfolder (or a blob) loads a subpath/blob tree that
    // carries no served counts, so the tab badges would stay on the stale
    // catalog seed (e.g. Issues showing 7 while the open/ folder holds 11).
    // Refresh them from the mirror's root counts in that case; the root code
    // view and the feature-tab routes already fetch the root tree themselves.
    if ((routeKind === "tree" || routeKind === "blob") && routePath) refreshServedCounts(repo);
    if (routeKind === "blob" && routePath) loadRepositoryBlob(repo, routePath);
    loadRepoFeaturePanels(repo, recordRoute);
    loadRepoPendingCounts(repo);
    loadRepoAboutRail(repo);
    loadRepoStarState(repo, $("[data-repo-action-star]"));
    consumeWorkshopAgentDeepLink(repo, workshopDeepLink);
  }

  // GitHub-style "Code" button: a green trigger that opens a popover with the
  // clone info instead of silently copying on click. Shows the HTTPS clone URL
  // (readonly, selectable, with a copy button) and the ready-to-paste
  // `git clone` command. Used both in the code toolbar and the compact
  // scroll-pinned focus bar (compact=true), each self-contained in its own
  // relative wrapper so the toggle handler can scope to the clicked one.
  function renderRepoCodeButton(repo, compact = false) {
    const url = cloneUrl(repo);
    const gitCmd = `git clone ${url}`;
    const sshUrl = String(repo?.sshUrl || "").trim();
    const sshCloneCmd = sshUrl ? `git clone ${sshUrl}` : "";
    const sshPushCmd = sshUrl ? `git remote set-url --push origin ${sshUrl}` : "";
    const btnHeight = compact ? "h-8" : "h-9";
    const copyBtn = (value, label) =>
      `<button type="button" data-dashboard-copy="${escapeHtml(value)}" aria-label="${label}" class="copy-button inline-flex h-7 w-7 shrink-0 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="copy" class="copy-icon h-3.5 w-3.5"></i><i data-lucide="check" class="copy-check h-3.5 w-3.5"></i></button>`;
    return `
      <div data-repo-code-wrap class="relative">
        <button type="button" data-repo-code-button aria-haspopup="true" aria-expanded="false" class="inline-flex ${btnHeight} items-center justify-center gap-2 rounded-md border border-primary/40 bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90 transition-colors">
          <i data-lucide="code" class="h-3.5 w-3.5"></i><span>Code</span><i data-lucide="chevron-down" class="h-3 w-3"></i>
        </button>
        <div data-repo-code-menu class="absolute right-0 z-40 mt-1 hidden w-96 max-w-[calc(100vw-2rem)] rounded-lg border border-border bg-background p-3 text-left shadow-xl">
          <div class="flex items-center justify-between gap-2">
            <span class="inline-flex items-center gap-1.5 text-xs font-semibold text-foreground"><i data-lucide="terminal" class="h-3.5 w-3.5 text-muted-foreground"></i>Clone</span>
            <span class="rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">HTTPS</span>
          </div>
          <p class="mt-1 text-[11px] leading-4 text-muted-foreground">Clone this repository from its live ForkMesh mirror over HTTPS.</p>
          <div class="mt-2 flex items-center gap-2">
            <input data-repo-clone-url type="text" readonly value="${escapeHtml(url)}" aria-label="Clone URL" class="min-w-0 flex-1 rounded-md border border-border bg-secondary px-2 py-1 font-mono text-[11px] text-foreground outline-none focus:border-primary" />
            ${copyBtn(url, "Copy clone URL")}
          </div>
          <div class="mt-3 border-t border-border pt-2">
            <span class="text-[11px] font-medium text-muted-foreground">Command line</span>
            <div class="mt-1 flex items-center gap-2">
              <code class="min-w-0 flex-1 truncate rounded-md border border-border bg-secondary px-2 py-1 font-mono text-[11px] text-foreground">${escapeHtml(gitCmd)}</code>
              ${copyBtn(gitCmd, "Copy git clone command")}
            </div>
          </div>
          ${sshUrl ? `
            <div class="mt-3 border-t border-border pt-3">
              <div class="flex items-center justify-between gap-2">
                <span class="inline-flex items-center gap-1.5 text-xs font-semibold text-foreground"><i data-lucide="key-round" class="h-3.5 w-3.5 text-muted-foreground"></i>SSH clone and push</span>
                <span class="rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">SSH</span>
              </div>
              <p class="mt-1 text-[11px] leading-4 text-muted-foreground">Register your public key in Settings. Your private key stays on your device; write permission is checked on every push.</p>
              <div class="mt-2 flex items-center gap-2">
                <input data-repo-ssh-url type="text" readonly value="${escapeHtml(sshUrl)}" aria-label="SSH clone and push URL" class="min-w-0 flex-1 rounded-md border border-border bg-secondary px-2 py-1 font-mono text-[11px] text-foreground outline-none focus:border-primary" />
                ${copyBtn(sshUrl, "Copy SSH URL")}
              </div>
              <div class="mt-2 flex items-center gap-2">
                <code class="min-w-0 flex-1 truncate rounded-md border border-border bg-secondary px-2 py-1 font-mono text-[11px] text-foreground">${escapeHtml(sshCloneCmd)}</code>
                ${copyBtn(sshCloneCmd, "Copy SSH clone command")}
              </div>
              <div class="mt-2 flex items-center gap-2">
                <code class="min-w-0 flex-1 truncate rounded-md border border-border bg-secondary px-2 py-1 font-mono text-[11px] text-foreground">${escapeHtml(sshPushCmd)}</code>
                ${copyBtn(sshPushCmd, "Copy SSH push-remote command")}
              </div>
            </div>` : ""}
        </div>
      </div>`;
  }

  function findRepository(key) {
    const wanted = String(key || "").trim();
    if (!wanted) return null;
    for (const group of groupRepositories(state.repositories)) {
      const origin = sourceOfTruth(group);
      if (repoMatchesKey(origin, wanted)) return origin;
      if ((group.members || []).some((member) => repoMatchesKey(member, wanted))) {
        return origin;
      }
    }
    return null;
  }

  async function findOrganizationRepository(key) {
    const wanted = String(key || "").trim();
    const parts = wanted.split("/");
    if (parts.length !== 2) return null;
    const organization = normalizeRepoSegment(parts[0]);
    const repository = normalizeRepoSegment(parts[1]);
    if (!organization || !repository) return null;

    let data;
    try {
      data = await fetchJson(
        `/api/orgs/${encodeURIComponent(organization)}/repos`,
      );
    } catch (_) {
      return null;
    }
    const linked = (Array.isArray(data?.repos) ? data.repos : []).find(
      (item) =>
        String(item?.repo || "").trim().toLowerCase() === repository.toLowerCase() &&
        normalizeRepoSegment(item?.node),
    );
    if (!linked) return null;

    const linkedOwner = normalizeRepoSegment(linked.node);
    const linkedRepository = normalizeRepoSegment(linked.repo);
    const linkedKey = `${linkedOwner}/${linkedRepository}`.toLowerCase();
    for (const group of groupRepositories(state.repositories)) {
      const member = (group.members || []).find(
        (item) => repoKey(item).toLowerCase() === linkedKey,
      );
      if (!member) continue;
      const origin = sourceOfTruth(group);
      const aliases = new Set(
        (Array.isArray(origin?._repoAliases) ? origin._repoAliases : [])
          .map((alias) => String(alias || "").toLowerCase()),
      );
      aliases.add(wanted.toLowerCase());
      aliases.add(linkedKey);
      return {
        ...origin,
        owner: organization,
        name: repository,
        sshUrl: String(linked.sshUrl || origin.sshUrl || "").trim(),
        canonicalOwner: organization,
        canonicalName: repository,
        servingOwner: linkedOwner,
        servingName: linkedRepository,
        _repoAliases: [...aliases],
      };
    }
    return null;
  }

  // Opening a repo from any list/search control is a real page navigation now
  // (repo pages are their own documents). Prefer the canonical origin's clean
  // URL when the catalog already resolved the key (alias groups), falling back
  // to the raw owner/name path — the repo page resolves it again on boot.
  function openRepoPage(key) {
    const wanted = String(key || "").trim();
    if (!wanted) return;
    const repo = findRepository(wanted);
    const url = repo ? repoPathUrl(repo) : "/" + wanted.split("/").map(encodeURIComponent).join("/");
    closeMobileDrawers();
    location.assign(url);
  }

  // A node's dot is only filled green when it is online *right now*; historical
  // uptime-leaderboard entries that have since gone offline render hollow so an
  // idle node no longer looks active (the right-rail bug in adhoc #86).
  function nodeDotClass(row, size) {
    return row.online
      ? `${size} fill-primary text-primary shrink-0`
      : `${size} fill-transparent text-muted-foreground/50 shrink-0`;
  }

  function nodeMetaLabel(row) {
    if (row.minutes) return formatCount(row.minutes);
    return row.online ? "live" : "";
  }

  // Node detail chips shown under each row in the full "Connected nodes" list
  // (not the compact home-page rail). Text fields fall back to an em dash when
  // a node hasn't reported them; counts default to 0 rather than a dash since
  // "no repos yet" is a real, distinct state from "field not tracked". CPU/RAM/
  // Disk aren't collected by any node -> worker path today (that telemetry is
  // desktop-only peer-room presence, see ServerNode::sampleSystemStats), so
  // they always render as a dash here for parity with the desktop Mirror nodes
  // panel's columns rather than being omitted.
  function nodeDetailChips(row) {
    const textChips = [
      ["Commit", row.commit ? String(row.commit).slice(0, 7) : ""],
      ["Synced", row.lastSync || ""],
      ["Platform", row.platform || ""],
      ["Version", row.version || ""],
      ["Node id", row.nodeId ? String(row.nodeId).slice(0, 12) : ""],
      ["CPU", ""],
      ["RAM", ""],
      ["Disk", ""],
    ];
    const countChips = [
      ["Size", formatSize(row.sizeBytes)],
      ["Issues", formatCount(row.issueCount)],
      ["Commits", formatCount(row.commitCount)],
      ["Branches", formatCount(row.branchCount)],
      ["Pulls", formatCount(row.pullCount)],
      ["Discussions", formatCount(row.discussionCount)],
      ["Worktrees", formatCount(row.worktreeCount)],
      ["Clones", formatCount(row.clonesServed)],
      ["Website", formatCount(row.websiteServed)],
      ["Artifacts", formatCount(row.artifactCount)],
    ];
    return [...textChips, ...countChips]
      .map(([label, value]) => `
        <span class="inline-flex items-center gap-1 rounded-md border border-border px-1.5 py-0.5 text-[10px] font-mono">
          <span class="text-muted-foreground">${escapeHtml(label)}</span>
          <span class="text-foreground">${escapeHtml(value === "" || value === undefined || value === null ? "-" : value)}</span>
        </span>`)
      .join("");
  }

  // The "Online only" toggle in the Connected nodes header hides offline nodes
  // (the default, matching the desktop Mirror nodes panel). Turning it off
  // surfaces nodes that have gone offline but still published a mirror record,
  // rendered inactive rather than live. Defaults to on until the user flips it.
  function networkOnlineOnly() {
    return state.networkOnlineOnly !== false;
  }

  function renderNetworkRows(rows) {
    const list = $("[data-network-node-list]");
    const count = $("[data-network-node-count]");
    const toggle = $("[data-network-online-only]");
    const onlineOnly = networkOnlineOnly();
    const onlineCount = rows.filter((row) => row.online).length;
    const offlineCount = rows.length - onlineCount;
    const visible = onlineOnly ? rows.filter((row) => row.online) : rows;

    if (toggle) {
      toggle.setAttribute("aria-pressed", onlineOnly ? "true" : "false");
      toggle.classList.toggle("border-primary", onlineOnly);
      toggle.classList.toggle("text-foreground", onlineOnly);
      toggle.classList.toggle("text-muted-foreground", !onlineOnly);
      const label = toggle.querySelector("[data-network-online-only-label]");
      if (label) label.textContent = onlineOnly ? "Online only" : "Showing offline";
    }
    if (count) {
      count.textContent = offlineCount
        ? `${formatCount(onlineCount)} online · ${formatCount(offlineCount)} offline`
        : `${formatCount(onlineCount)} online`;
    }
    if (list) {
      list.innerHTML = visible.length
        ? visible.map((row) => `
          <div class="px-4 py-3 hover:bg-secondary/50 transition-colors">
            <div class="flex items-center gap-4">
              <div class="flex items-center gap-2 flex-1 min-w-0">
                <i data-lucide="circle" class="${nodeDotClass(row, "w-2 h-2")}"></i>
                <span class="text-sm ${row.online ? "text-foreground" : "text-muted-foreground"} font-medium truncate font-mono">${escapeHtml(row.name || "node")}</span>
                ${row.online ? "" : '<span class="shrink-0 rounded border border-border px-1.5 py-0.5 text-[10px] font-mono text-muted-foreground">offline</span>'}
              </div>
              <span class="text-xs text-muted-foreground w-20 text-right font-mono">${escapeHtml(nodeMetaLabel(row))}</span>
            </div>
            <div class="mt-2 flex flex-wrap gap-1.5 pl-4">${nodeDetailChips(row)}</div>
          </div>
        `).join("")
        : `<div class="px-4 py-3 text-sm text-muted-foreground">${onlineOnly ? "No nodes online right now." : "No nodes yet."}</div>`;
    }
  }

  function toggleNetworkOnlineOnly() {
    state.networkOnlineOnly = !networkOnlineOnly();
    renderNetworkRows(Array.isArray(state.networkNodeRows) ? state.networkNodeRows : []);
    window.lucide?.createIcons();
  }

  async function renderNetwork() {
    try {
      const overview = await fetchJson("/api/network/overview");
      const stats = overview.stats || {};
      const leaderboards = overview.leaderboards || {};
      const history = overview.history || {};
      const hosts = Number(stats.hosts) || 0;
      const repos = Number(stats.repos) || 0;
      const clients = Number(stats.clients) || 0;
      const uptime = Array.isArray(leaderboards.uptime)
        ? leaderboards.uptime
        : [];
      const nodeDetails = new Map(
        (Array.isArray(leaderboards.nodes) ? leaderboards.nodes : [])
          .map((node) => [String(node.name || "").trim().toLowerCase(), node]),
      );
      const activityHours = Array.isArray(history.hours)
        ? history.hours
        : [];
      const activeMinutes = activityHours.reduce(
        (sum, hour) => sum + (Number(hour.nodeMinutes) || 0),
        0,
      );

      $("[data-network-hosts]") && ($("[data-network-hosts]").textContent = formatCount(hosts));
      $("[data-network-repos]") && ($("[data-network-repos]").textContent = formatCount(repos));
      $("[data-network-clients]") && ($("[data-network-clients]").textContent = formatCount(clients));
      $("[data-network-uptime]") && ($("[data-network-uptime]").textContent = activeMinutes ? "Active" : "Idle");

      // Merge the 48h uptime leaderboard (name + minutes) with the set of nodes
      // that are online right now. Online nodes sort first and always appear even
      // with no accrued minutes yet, so a freshly-started node (e.g. a VM host)
      // shows up on the rail; offline leaderboard nodes stay listed but render
      // as inactive rather than looking live.
      const onlineNames = Array.isArray(stats.onlineNodes) ? stats.onlineNodes : [];
      const onlineSet = new Set(
        onlineNames.map((name) => String(name || "").trim().toLowerCase()).filter(Boolean),
      );
      const nodeRows = new Map();
      uptime.forEach((row) => {
        const name = String(row.name || "").trim();
        if (!name) return;
        const key = name.toLowerCase();
        nodeRows.set(key, {
          ...nodeDetails.get(key),
          name,
          minutes: Number(row.minutes) || 0,
          online: onlineSet.has(key),
        });
      });
      onlineNames.forEach((name) => {
        const clean = String(name || "").trim();
        if (!clean) return;
        const key = clean.toLowerCase();
        if (!nodeRows.has(key)) {
          nodeRows.set(key, { ...nodeDetails.get(key), name: clean, minutes: 0, online: true });
        }
      });
      const rows = [...nodeRows.values()].sort(
        (a, b) =>
          Number(b.online) - Number(a.online) ||
          b.minutes - a.minutes ||
          a.name.localeCompare(b.name),
      );
      state.networkNodeRows = rows;
      renderNetworkRows(rows);
    } catch (_) {
      $("[data-network-node-list]") && ($("[data-network-node-list]").innerHTML =
        '<div class="px-4 py-3 text-sm text-muted-foreground">Network data is unavailable right now.</div>');
    } finally {
      window.lucide?.createIcons();
    }
  }

  function notificationIcon(kind) {
    return ({
      mention: "at-sign",
      pull_submitted: "git-pull-request",
      issue_assigned: "circle-dot",
      repo_shared: "share-2",
      bounty_funded: "badge-dollar-sign",
      bounty_paid: "circle-dollar-sign",
      release_published: "tag",
      host_online: "wifi",
      host_offline: "wifi-off",
      pending_inbox: "inbox",
      mirror_request: "radio",
    })[kind] || "bell";
  }

  function notificationTimeLabel(ts) {
    const value = Number(ts) || 0;
    if (!value) return "just now";
    const elapsed = Math.max(0, Date.now() - value);
    const minutes = Math.floor(elapsed / 60000);
    if (minutes < 1) return "just now";
    if (minutes < 60) return `${minutes}m ago`;
    const hours = Math.floor(minutes / 60);
    if (hours < 24) return `${hours}h ago`;
    return formatDate(value);
  }

  function renderNotificationPreview() {
    const countEl = $("[data-notification-count]");
    const badge = $("[data-notification-badge]");
    const list = $("[data-notification-preview-list]");
    const unread = Number(state.notificationUnread) || 0;
    if (countEl) countEl.textContent = unread === 1 ? "1 new" : `${unread} new`;
    badge?.classList.toggle("hidden", unread <= 0);
    if (!list) return;
    const items = state.notifications.slice(0, 5);
    if (!items.length) {
      list.innerHTML = '<div class="px-3 py-4 text-xs text-muted-foreground">No pings yet. Mentions, PRs, assignments, shares, bounties, releases, and host status changes will appear here.</div>';
      return;
    }
    list.innerHTML = items.map((item) => `
      <button type="button" data-notification-open="${escapeHtml(item.id || "")}" class="w-full px-3 py-2 text-left hover:bg-secondary transition-colors ${item.readAt ? "opacity-70" : ""}">
        <div class="flex items-start gap-2">
          <i data-lucide="${notificationIcon(item.kind)}" class="mt-0.5 h-3.5 w-3.5 ${item.readAt ? "text-muted-foreground" : "text-primary"}"></i>
          <div class="min-w-0 flex-1">
            <p class="truncate text-xs font-medium text-foreground">${escapeHtml(item.title || "Ping")}</p>
            <p class="mt-0.5 truncate text-[11px] text-muted-foreground">${escapeHtml(item.body || item.repo || "ForkMesh update")}</p>
          </div>
          <span class="shrink-0 text-[10px] text-muted-foreground font-mono">${escapeHtml(notificationTimeLabel(item.ts))}</span>
        </div>
      </button>
    `).join("");
    window.lucide?.createIcons();
  }

  function renderNotificationDetail(item) {
    const detail = $("[data-notification-modal-detail]");
    if (!detail) return;
    if (!item) {
      detail.innerHTML = '<div class="text-sm text-muted-foreground">Select a ping to read it.</div>';
      return;
    }
    detail.innerHTML = `
      <div class="flex items-start gap-3">
        <span class="flex h-9 w-9 shrink-0 items-center justify-center rounded-full border border-border bg-secondary text-primary">
          <i data-lucide="${notificationIcon(item.kind)}" class="h-4 w-4"></i>
        </span>
        <div class="min-w-0 flex-1">
          <p class="text-sm font-semibold text-foreground">${escapeHtml(item.title || "Ping")}</p>
          <p class="mt-1 text-xs text-muted-foreground">${escapeHtml(notificationTimeLabel(item.ts))}${item.repo ? ` · ${escapeHtml(item.repo)}` : ""}</p>
        </div>
      </div>
      <p class="mt-5 whitespace-pre-wrap text-sm leading-6 text-muted-foreground">${escapeHtml(item.body || "ForkMesh notification")}</p>
      ${mirrorRequestActionsHtml(item)}
      ${item.href ? `<a href="${escapeHtml(item.href)}" class="mt-5 inline-flex h-9 items-center justify-center rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary transition-colors">Open context</a>` : ""}
    `;
    window.lucide?.createIcons();
  }

  // Accept/Reject controls on an incoming "someone asked your node to mirror
  // their repo" notification (issue #385). Only the still-pending request the
  // recipient can act on gets buttons; replies ("X accepted…") carry none.
  function mirrorRequestActionsHtml(item) {
    if (!item || item.kind !== "mirror_request") return "";
    const meta = item.meta && typeof item.meta === "object" ? item.meta : {};
    if (String(meta.state || "") !== "pending") return "";
    const id = String(meta.requestId || "");
    if (!id) return "";
    const enc = escapeHtml(id);
    return `
      <div data-mirror-request-actions="${enc}" class="mt-5 flex items-center gap-2">
        <button type="button" data-mirror-request-accept="${enc}" class="inline-flex h-9 items-center justify-center rounded-md bg-primary px-3 text-xs font-medium text-primary-foreground hover:bg-primary/90 transition-colors">Accept &amp; mirror</button>
        <button type="button" data-mirror-request-reject="${enc}" class="inline-flex h-9 items-center justify-center rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary transition-colors">Decline</button>
        <span data-mirror-request-hint class="text-[11px] text-muted-foreground"></span>
      </div>
    `;
  }

  async function resolveMirrorRequest(id, action, trigger) {
    const node = state.session?.nodeName || "";
    if (!node || !id) return;
    const container = trigger?.closest("[data-mirror-request-actions]");
    const hint = container?.querySelector("[data-mirror-request-hint]");
    container?.querySelectorAll("button").forEach((b) => { b.disabled = true; });
    if (hint) hint.textContent = action === "accept" ? "Accepting…" : "Declining…";
    try {
      const res = await fetch("/api/mirror-requests", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          node, action, requestId: id,
          sessionToken: state.session?.sessionToken || "",
        }),
      });
      if (!res.ok) throw new Error("request failed");
      if (hint) hint.textContent = action === "accept"
        ? "Accepted — your node will start mirroring it shortly."
        : "Declined.";
      if (container) container.querySelectorAll("button").forEach((b) => b.remove());
      await loadNotifications();
    } catch (_) {
      if (hint) hint.textContent = "Could not update the request. Try again.";
      container?.querySelectorAll("button").forEach((b) => { b.disabled = false; });
    }
  }

  // "Ask a node to mirror your repo" (issue #385): the owner types a node name;
  // that node's holder gets a notification and, if they accept, their node
  // starts mirroring this repo. Only shown to the repo owner (see
  // renderMirrorRequestForm) — the worker re-checks ownership from the session.
  async function askNodeToMirror(trigger) {
    const repo = state.selectedRepo;
    if (!repo || !isRepoOwner(repo)) return;
    const wrap = trigger.closest("[data-mirror-request]") || document;
    const input = wrap.querySelector("[data-mirror-request-target]");
    const hint = wrap.querySelector("[data-mirror-request-hint]");
    const setHint = (text, tone) => {
      if (!hint) return;
      hint.className = `mt-1.5 text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      hint.textContent = text;
    };
    const target = String(input?.value || "").trim().toLowerCase();
    if (!target) {
      setHint("Enter the node name to ask.", "bad");
      input?.focus();
      return;
    }
    if (target === String(repo.owner || "").trim().toLowerCase()) {
      setHint("That's this repo's own node.", "bad");
      return;
    }
    trigger.disabled = true;
    setHint("Sending request…");
    try {
      const res = await fetch("/api/mirror-requests", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          node: state.session?.nodeName || "",
          action: "create",
          target,
          owner: String(repo.owner || ""),
          repo: String(repo.name || ""),
          sessionToken: state.session?.sessionToken || "",
        }),
      });
      const body = await res.json().catch(() => ({}));
      if (!res.ok || !body.ok) {
        const reason = {
          target_not_found: "No node by that name.",
          repo_not_found: "This repo isn't published yet.",
          private_repo: "Only public repos can be mirrored this way.",
          forbidden: "You can only ask others to mirror your own repos.",
          self_target: "That's this repo's own node.",
        }[String(body.error || "")] || "Could not send the request.";
        setHint(reason, "bad");
        trigger.disabled = false;
        return;
      }
      if (input) input.value = "";
      setHint(`Asked ${target} to mirror this repo.`, "good");
    } catch (_) {
      setHint("Could not send the request. Try again.", "bad");
    } finally {
      trigger.disabled = false;
    }
  }

  function renderMirrorRequestForm(repo) {
    const form = $("[data-mirror-request]");
    if (!form) return;
    form.hidden = !isRepoOwner(repo);
  }

  function renderNotificationModal() {
    const list = $("[data-notification-modal-list]");
    if (!list) return;
    if (!state.notifications.length) {
      list.innerHTML = '<div class="p-3 text-xs text-muted-foreground">No pings yet.</div>';
      renderNotificationDetail(null);
      return;
    }
    const selected = state.notifications.find((item) => item.id === state.selectedNotificationId)
      || state.notifications[0];
    state.selectedNotificationId = selected?.id || "";
    list.innerHTML = state.notifications.map((item) => {
      const active = item.id === state.selectedNotificationId;
      return `
        <button type="button" data-notification-open="${escapeHtml(item.id || "")}" class="mb-1 w-full rounded-md px-3 py-2 text-left transition-colors ${active ? "bg-secondary text-foreground" : "text-muted-foreground hover:bg-secondary/60 hover:text-foreground"}">
          <span class="block truncate text-xs font-medium">${escapeHtml(item.title || "Ping")}</span>
          <span class="mt-1 block truncate text-[11px] font-mono">${escapeHtml(item.repo || item.kind || "forkmesh")}</span>
        </button>
      `;
    }).join("");
    renderNotificationDetail(selected);
  }

  async function loadNotifications() {
    const node = state.session?.nodeName || "";
    if (!node) {
      state.notifications = [];
      state.notificationUnread = 0;
      renderNotificationPreview();
      renderHomeFeed();
      return;
    }
    try {
      const data = await fetchJson(`/api/notifications?node=${encodeURIComponent(node)}`);
      state.notifications = Array.isArray(data.notifications) ? data.notifications : [];
      state.notificationUnread = Number(data.unread) || 0;
    } catch (_) {
      state.notifications = [];
      state.notificationUnread = 0;
    }
    renderNotificationPreview();
    renderNotificationModal();
    renderHomeFeed();
  }

  function setNotificationDropdownOpen(open) {
    const toggle = $("#notificationToggle");
    const dropdown = $("#notificationDropdown");
    dropdown?.classList.toggle("hidden", !open);
    toggle?.setAttribute("aria-expanded", open ? "true" : "false");
  }

  function setAccountMenuOpen(open) {
    const toggle = $("#accountMenuToggle");
    const dropdown = $("#accountMenuDropdown");
    dropdown?.classList.toggle("hidden", !open);
    toggle?.setAttribute("aria-expanded", open ? "true" : "false");
  }

  function setNotificationModalOpen(open) {
    const modal = $("#notificationModal");
    if (!modal) return;
    modal.classList.toggle("hidden", !open);
    modal.classList.toggle("flex", open);
    if (open) renderNotificationModal();
  }

  async function markNotificationsRead(ids = [], all = false) {
    const node = state.session?.nodeName || "";
    if (!node) return;
    try {
      await fetch("/api/notifications", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          node, ids, all,
          sessionToken: state.session?.sessionToken || "",
        }),
      });
    } catch (_) {}
    await loadNotifications();
  }

  async function openNotification(id, modal = false) {
    const item = state.notifications.find((candidate) => candidate.id === id);
    if (!item) return;
    state.selectedNotificationId = id;
    renderNotificationModal();
    if (!item.readAt) await markNotificationsRead([id]);
    if (modal) setNotificationModalOpen(true);
  }

  // The release version changes at most per deploy: cache it in sessionStorage
  // for an hour so repeat page navigations don't refetch /api/version.
  const APP_VERSION_STORAGE = "forkmesh.appVersion";
  const APP_VERSION_TTL_MS = 60 * 60 * 1000;

  async function renderAppVersion() {
    // Show the live ForkMesh release version (same number as the desktop app -
    // deploy.sh stamps it from qt_client/CMakeLists.txt as the APP_VERSION Worker
    // var) next to the logo. Best-effort: stay hidden if the endpoint or version
    // is unavailable so the header never shows a broken "v".
    const el = $("[data-app-version]");
    if (!el) return;
    const show = (version) => {
      el.textContent = version[0] === "v" ? version : "v" + version;
      el.classList.remove("hidden");
    };
    try {
      const cached = JSON.parse(sessionStorage.getItem(APP_VERSION_STORAGE) || "null");
      if (cached?.version && Number(cached.expiresAt) > Date.now()) {
        show(String(cached.version));
        return;
      }
    } catch (_) {
      /* unreadable cache entry: fall through to the fetch */
    }
    try {
      const data = await fetchJson("/api/version");
      const version = (data && data.version ? String(data.version) : "").trim();
      if (!version) return;
      try {
        sessionStorage.setItem(APP_VERSION_STORAGE, JSON.stringify({
          version,
          expiresAt: Date.now() + APP_VERSION_TTL_MS,
        }));
      } catch (_) {
        /* best-effort cache */
      }
      show(version);
    } catch (_) {
      /* leave the version chip hidden */
    }
  }

  function setHomeAgentStatus(message, tone = "") {
    const status = $("[data-home-agent-status]");
    if (!status) return;
    status.textContent = message || "";
    status.className = "min-w-0 truncate " + (
      tone === "bad" ? "text-destructive"
        : tone === "good" ? "text-primary"
        : "text-muted-foreground");
  }

  function normalizeHomeForkbotMessage(message) {
    const text = String(message || "").trim();
    if (!text) return "";
    return /\bforkbot\b/i.test(text) ? text : "forkbot " + text;
  }

  async function submitHomeAgentPrompt() {
    const input = $("[data-home-agent-input]");
    const button = $("[data-home-agent-submit]");
    const raw = String(input?.value || "").trim();
    if (!raw) {
      setHomeAgentStatus("Enter a ForkBot issue command first.", "bad");
      input?.focus();
      return;
    }
    const message = normalizeHomeForkbotMessage(raw);
    if (button) button.disabled = true;
    setHomeAgentStatus("Sending to ForkBot...");
    try {
      const response = await fetch("/api/forkbot/chat", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          message,
          sender: state.session?.nodeName || "dashboard",
        }),
      });
      const responseText = await response.text();
      let body = {};
      try {
        body = responseText ? JSON.parse(responseText) : {};
      } catch (_) {
        body = {};
      }
      if (!response.ok || body.error) {
        const detail = body.error || responseText || `HTTP ${response.status}`;
        if (String(detail).includes("DATA_KEY is unset")) {
          setHomeAgentStatus("ForkBot is not configured on this Worker.", "bad");
          return;
        }
        throw new Error(detail);
      }
      const reply = body.botMessage || (
        body.ignored
          ? "ForkBot did not find a command. Try: forkbot create an issue to describe the task."
          : "ForkBot handled the request."
      );
      setHomeAgentStatus(reply, body.action === "issue_created" ? "good" : "");
      if (body.action === "issue_created" && input) input.value = "";
    } catch (error) {
      setHomeAgentStatus(
        String(error?.message || "") === "rate_limited"
          ? "ForkBot is rate limited. Try again later."
          : "Could not reach ForkBot.",
        "bad");
    } finally {
      if (button) button.disabled = false;
    }
  }

  // Each page is its own document (marked <body data-page="...">). Boot runs
  // the shared chrome first, then that page's init — the page's own markup is
  // already visible at parse time, so there is no flash-then-swap.
  function currentPage() {
    return document.body?.dataset?.page || "home";
  }

  // Clean path per legacy ?section= name — links from old builds and the
  // desktop app still arrive as /dashboard?section=X. The Worker 308s these
  // too; this client shim is belt-and-braces for cached home documents.
  const SECTION_PATHS = {
    home: "/dashboard",
    repos: "/dashboard/repos",
    network: "/dashboard/network",
    chat: "/dashboard/chat",
    profile: "/dashboard/settings",
    "profile-overview": "/dashboard/profile",
    "profile-repositories": "/dashboard/profile/repositories",
  };

  function legacyRedirectTarget() {
    if (location.pathname !== "/dashboard") return "";
    const params = new URLSearchParams(location.search);
    const section = (params.get("section") || "").trim();
    if (section && SECTION_PATHS[section]) {
      params.delete("section");
      const rest = params.toString();
      return SECTION_PATHS[section] + (rest ? `?${rest}` : "");
    }
    const repo = (params.get("repo") || "").trim();
    if (repo.includes("/")) {
      return "/" + repo.split("/").map(encodeURIComponent).join("/");
    }
    return "";
  }

  // Fetches the repository catalog once and fans it out to whatever containers
  // exist on this page (sidebar list is chrome on every page; the repos list,
  // home feed, and profile views fill in when present). Never awaited before
  // first paint — each page shows its own skeleton immediately. The repo page
  // awaits this promise before resolving its /owner/repo path.
  let repositoriesReady = null;

  // Resolves once hydrateCanonicalProfile has refreshed the session with the
  // authoritative nodes/isAdmin fields. The owner-only Agents tab (adhoc #182)
  // is gated on those, so the repo page waits on this before deciding whether
  // an /owner/repo/agents deep link is a real tab route or must collapse to
  // Code — otherwise a hard refresh on Agents races the hydration and bounces
  // back to the repo root (adhoc #93).
  let canonicalProfileReady = null;

  // Boot/shared-chrome loads take the cached path (browser + edge cache honor
  // the server's max-age); only explicit user refresh actions pass fresh:true.
  async function loadRepositories({ fresh = false } = {}) {
    if (dashboardMockRepositoriesEnabled()) {
      renderRepositories(dashboardMockRepositories(), state.session);
      return;
    }
    try {
      const data = await fetchJson("/api/repositories", { fresh });
      renderRepositories(data.repositories, state.session);
    } catch (_) {
      state.repositoriesLoading = false;
      const list = $("#repoList");
      const count = $("[data-repo-count]");
      if (count) count.textContent = "Unavailable";
      if (list) {
        list.innerHTML = '<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">Repository catalog is temporarily unavailable.</div>';
      }
      renderSidebarRepositories(state.session);
      renderHomeFeed();
    }
  }

  // Returns false when boot is aborting into a redirect (login bounce, legacy
  // URL shim) so the page init doesn't race the navigation.
  function initSharedChrome() {
    const session = readSession();
    state.session = session;
    renderAppVersion();

    const grant = pendingLinkGrant();
    if (grant && !session?.nodeName) {
      // A link grant arrived but nobody is logged in: bounce through login and
      // come straight back with the grant intact so the link completes then.
      location.replace("/login?next=" + encodeURIComponent(`${location.pathname}${location.search}`));
      return false;
    }
    if (grant && currentPage() !== "settings") {
      // The desktop app hard-codes /dashboard?link_node=... — the Nodes panel
      // lives on the settings document now. Carry the grant params over
      // untouched; offerLinkGrant strips them there.
      location.replace("/dashboard/settings" + location.search);
      return false;
    }
    const legacyTarget = legacyRedirectTarget();
    if (legacyTarget) {
      location.replace(legacyTarget);
      return false;
    }

    // Guests can browse repositories without an account: instead of bouncing
    // signed-out visitors back to the landing page, the header swaps the
    // profile/notification controls for a Sign Up / Log In link.
    const guest = !session || (!session.nodeName && !session.email);
    if (guest) {
      // The profile and settings documents are account pages — signed-out
      // visitors have no profile to show there, so bounce through login and
      // come back once they have one. /@name public profiles share the profile
      // document and stay open to guests.
      const page = currentPage();
      const accountPage = page === "settings" ||
        ((page === "profile" || page === "profile-repositories") && !publicProfileNameFromPath());
      if (accountPage) {
        location.replace("/login?next=" + encodeURIComponent(`${location.pathname}${location.search}`));
        return false;
      }
      const authLink = $("[data-guest-auth-link]");
      if (authLink) {
        authLink.classList.remove("hidden");
        authLink.classList.add("inline-flex");
      }
      $("[data-profile-settings-button]")?.classList.add("hidden");
      $("#notificationToggle")?.classList.add("hidden");
      $("#agentModalToggle")?.classList.add("hidden");
      // Keep the presence cookie honest: localStorage says logged out, so the
      // Worker must stop 302ing / to the dashboard.
      document.cookie = "forkmesh_session=; Path=/; Max-Age=0; SameSite=Lax";
    }

    if (guest) {
      // No fabricated "guest" identity: the chrome keeps its baked defaults;
      // only the per-page header context still needs to be set.
      renderHeaderContext();
    } else {
      renderProfile(session);
    }
    if (session?.nodeName) {
      if (grant) offerLinkGrant(grant);
      canonicalProfileReady = hydrateCanonicalProfile(session).then(() => loadNotifications()).catch(() => {});
    }
    repositoriesReady = loadRepositories();
    return true;
  }

  function initHomePage() {
    renderHomeBlogPosts();
    // The blog card fills in from the edge-cached feed; the baked markup
    // already shows its loading state.
    void loadHomeBlogPosts();
    void loadHomeOrganizationRepositories();
    // Feed + top repositories fill in when loadRepositories()/loadNotifications()
    // resolve — both re-render the home containers.
    // Active agent sessions (adhoc #81) need the catalog first so we know which
    // repos to poll; fetch them once repositories are loaded.
    repositoriesReady?.then(() => loadHomeAgentSessions()).catch(() => {});
  }

  function initReposPage() {
    // The catalog fetch from initSharedChrome() owns this page's content; the
    // baked markup already shows the loading state.
    loadExternalRepositories();
  }

  function initNetworkPage() {
    renderNetwork();
  }

  function initChatPage() {
    // dashboard-chat.js self-boots off the #fullChatMessages markup.
  }

  function initProfileOverviewPage() {
    // /@name serves this same document in public-profile mode: render the
    // named account's public data instead of the logged-in session (viewing
    // your own /@name keeps the full owner view).
    const publicName = publicProfileNameFromPath();
    const own = String(state.session?.nodeName || "").toLowerCase();
    if (publicName && publicName !== own) {
      loadPublicProfile(publicName);
      return;
    }
    renderProfilePage(state.session);
    refreshPublicProfile(state.session);
  }

  function initSettingsPage() {
    renderProfilePage(state.session);
    refreshPublicProfile(state.session);
    setSettingsSection(settingsSectionFromPath(), { scroll: false });
  }

  async function initRepoPage() {
    const requested = requestedRepoKey();
    const detail = $("[data-repo-detail]");
    const crumb = $("[data-repo-detail-crumb]");
    if (crumb && requested) crumb.textContent = requested;
    if (detail && requested) {
      detail.innerHTML = `<p class="text-sm text-muted-foreground">${loadingHtml("Loading repository…")}</p>`;
    }
    // findRepository needs the catalog (alias/canonical grouping), so this page
    // does wait on the shared fetch before rendering the detail body.
    await (repositoriesReady || loadRepositories());
    let repo = requested ? findRepository(requested) : null;
    if (
      repo &&
      requested &&
      repoKey(repo).toLowerCase() !== requested.toLowerCase()
    ) {
      // A catalog alias normally resolves to its backing source record. Before
      // accepting that canonical identity, check whether the URL is a durable
      // organization alias so subsequent tab/tree navigation keeps the public
      // /org/repo address instead of appearing to redirect to /node/repo.
      repo = (await findOrganizationRepository(requested)) || repo;
    } else if (!repo && requested) {
      // Organization URLs are public aliases backed by a node-owned catalog
      // record. The catalog deliberately publishes only the signing node's
      // identity, so resolve the public org link on a direct-page visit and
      // keep the requested organization identity while every data request
      // continues through the Worker's existing org-alias authorization path.
      repo = await findOrganizationRepository(requested);
    }
    if (repo) {
      startRepoMirrorPolling();
      // Owner/admin Agents and owner-only Settings depend on nodes/isAdmin
      // fields that land after canonical profile hydration. On a hard refresh
      // of either private control route, wait before deciding whether the tab
      // exists so the URL is not incorrectly collapsed back to Code.
      const routeParts = repoRouteParts();
      if (
        (routeParts[2] === "agents" && !sessionCanAssignAgent(repo)) ||
        (routeParts[2] === "settings" && !sessionOwnsRepo(repo))
      ) {
        await (canonicalProfileReady || Promise.resolve());
      }
      renderRepoDetail(repo);
      return;
    }
    if (detail) {
      detail.innerHTML =
        '<p class="text-sm text-muted-foreground">Repository ' +
        `<span class="font-mono text-foreground">${escapeHtml(requested || "")}</span>` +
        ' was not found. <a class="dashboard-accent-link hover:underline" href="/dashboard/repos">Back to repositories</a>.</p>';
    }
  }

  document.addEventListener("click", async (event) => {
    const mobileMenuToggle = event.target.closest("[data-mobile-menu-toggle]");
    if (mobileMenuToggle) {
      setMobileSidebarOpen(!document.body.classList.contains("dashboard-sidebar-open"));
      return;
    }

    if (event.target.closest("[data-mobile-sidebar-backdrop], [data-mobile-drawer-close]")) {
      setMobileSidebarOpen(false);
      return;
    }

    if (event.target.closest("[data-network-online-only]")) {
      toggleNetworkOnlineOnly();
      return;
    }

    const homeAgentSample = event.target.closest("[data-home-agent-sample]");
    if (homeAgentSample) {
      const input = $("[data-home-agent-input]");
      if (input) {
        input.value = homeAgentSample.dataset.homeAgentSample || "";
        input.focus();
      }
      setHomeAgentStatus("Ready to send to ForkBot.");
      return;
    }

    if (event.target.closest("[data-home-agent-submit]")) {
      await submitHomeAgentPrompt();
      return;
    }

    const profileToggle = event.target.closest("[data-profile-toggle]");
    if (profileToggle) {
      const popover = $("[data-profile-popover]");
      const open = popover?.classList.contains("hidden");
      popover?.classList.toggle("hidden", !open);
      profileToggle.setAttribute("aria-expanded", open ? "true" : "false");
      return;
    }

    const notificationToggle = event.target.closest("#notificationToggle");
    if (notificationToggle) {
      event.stopPropagation();
      const dropdown = $("#notificationDropdown");
      setNotificationDropdownOpen(dropdown?.classList.contains("hidden"));
      if (state.session?.nodeName) loadNotifications();
      return;
    }

    const accountMenuToggle = event.target.closest("#accountMenuToggle");
    if (accountMenuToggle) {
      event.stopPropagation();
      const dropdown = $("#accountMenuDropdown");
      setAccountMenuOpen(dropdown?.classList.contains("hidden"));
      return;
    }
    const accountMenuLogout = event.target.closest("[data-account-menu-logout]");
    if (accountMenuLogout) {
      logout();
      return;
    }

    const mirrorAccept = event.target.closest("[data-mirror-request-accept]");
    if (mirrorAccept) {
      event.stopPropagation();
      await resolveMirrorRequest(mirrorAccept.dataset.mirrorRequestAccept || "", "accept", mirrorAccept);
      return;
    }
    const mirrorReject = event.target.closest("[data-mirror-request-reject]");
    if (mirrorReject) {
      event.stopPropagation();
      await resolveMirrorRequest(mirrorReject.dataset.mirrorRequestReject || "", "reject", mirrorReject);
      return;
    }
    const mirrorAsk = event.target.closest("[data-mirror-request-send]");
    if (mirrorAsk) {
      event.stopPropagation();
      await askNodeToMirror(mirrorAsk);
      return;
    }

    const notificationOpen = event.target.closest("[data-notification-open]");
    if (notificationOpen) {
      await openNotification(notificationOpen.dataset.notificationOpen || "", Boolean(event.target.closest("#notificationModal")));
      return;
    }

    if (event.target.closest("[data-show-all-notifications]")) {
      setNotificationDropdownOpen(false);
      setNotificationModalOpen(true);
      return;
    }

    if (event.target.closest("[data-close-notification-modal], [data-notification-modal-backdrop]")) {
      setNotificationModalOpen(false);
      return;
    }

    if (event.target.closest("#agentModalToggle")) {
      event.stopPropagation();
      setAgentModalOpen($("#agentModal")?.classList.contains("hidden"));
      return;
    }

    if (event.target.closest("[data-agent-modal-close]")) {
      setAgentModalOpen(false);
      return;
    }

    const settingsSectionButton = event.target.closest("[data-settings-section-link]");
    if (settingsSectionButton) {
      setSettingsSection(settingsSectionButton.dataset.settingsSectionLink || "public-profile", { push: true });
      return;
    }

    const sshRevokeButton = event.target.closest("[data-ssh-key-revoke]");
    if (sshRevokeButton) {
      revokeSshKey(sshRevokeButton.dataset.sshKeyRevoke || "");
      return;
    }

    const appearanceThemeButton = event.target.closest("[data-appearance-theme]");
    if (appearanceThemeButton) {
      saveDashboardTheme(appearanceThemeButton.dataset.appearanceTheme);
      return;
    }

    if (event.target.closest("[data-profile-modal-close], [data-profile-modal-backdrop]")) {
      setProfileModalOpen(false);
      return;
    }

    if (event.target.closest("[data-profile-about-edit]")) {
      setProfileAboutModalOpen(true);
      return;
    }

    if (event.target.closest("[data-profile-about-modal-close], [data-profile-about-modal-backdrop], [data-profile-about-cancel]")) {
      setProfileAboutModalOpen(false);
      return;
    }

    if (event.target.closest("[data-profile-about-save]")) {
      saveProfileAbout();
      return;
    }

    const contributionPeriod = event.target.closest("[data-profile-contribution-period]");
    if (contributionPeriod) {
      renderProfileContributionGraph({ period: contributionPeriod.dataset.profileContributionPeriod });
      return;
    }

    if (event.target.closest("[data-profile-contribution-retry]")) {
      renderProfileContributionGraph({ force: true });
      return;
    }

    if (event.target.closest("[data-profile-save]")) {
      saveProfile();
      return;
    }

    if (event.target.closest("[data-profile-verify-email]")) {
      resendVerification();
      return;
    }

    if (event.target.closest("[data-profile-page-save]")) {
      saveProfile({
        passwordSelector: "[data-profile-page-password]",
        solanaSelector: "[data-profile-page-solana]",
        hintSelector: "[data-profile-page-hint]",
        buttonSelector: "[data-profile-page-save]",
        buttonText: "Save payout address",
      });
      return;
    }

    if (event.target.closest("[data-profile-public-save]")) {
      savePublicProfile();
      return;
    }

    if (event.target.closest("[data-notification-preferences-save]")) {
      saveNotificationPreferences();
      return;
    }

    if (event.target.closest("[data-profile-page-verify-email]")) {
      resendVerification({
        passwordSelector: "[data-profile-page-password]",
        hintSelector: "[data-profile-page-hint]",
        buttonSelector: "[data-profile-page-verify-email]",
      });
      return;
    }

    if (event.target.closest("[data-profile-rename-save]")) {
      renameNodeName();
      return;
    }

    if (event.target.closest("[data-claim-node-send]")) {
      claimNode();
      return;
    }

    if (event.target.closest("[data-claim-code-confirm]")) {
      confirmClaimCode();
      return;
    }

    if (event.target.closest("[data-link-grant-confirm]")) {
      redeemLinkGrant();
      return;
    }

    if (event.target.closest("[data-profile-delete-account]")) {
      deleteAccount();
      return;
    }

    const logoutButton = event.target.closest("[data-logout-button]");
    if (logoutButton) {
      logout();
      return;
    }

    if (!event.target.closest("[data-sidebar-profile]")) {
      $("[data-profile-popover]")?.classList.add("hidden");
      $("[data-profile-toggle]")?.setAttribute("aria-expanded", "false");
    }
    if (!event.target.closest("#notificationToggle, #notificationDropdown")) {
      setNotificationDropdownOpen(false);
    }
    if (!event.target.closest("#accountMenuToggle, #accountMenuDropdown")) {
      setAccountMenuOpen(false);
    }
    if (!event.target.closest("#agentModal, #agentModalToggle")) {
      setAgentModalOpen(false);
    }
    if (!event.target.closest("[data-repo-branch-control]")) {
      closeRepoBranchMenus();
    }
    if (!event.target.closest("[data-global-search-shell]")) {
      closeGlobalSearch();
    }

    const pageButton = event.target.closest("[data-dashboard-repo-page]");
    if (pageButton) {
      state.page = Number(pageButton.dataset.dashboardRepoPage) || 1;
      updateRepositoryPagination();
    }

      const globalSearchResult = event.target.closest("[data-global-search-result]");
      if (globalSearchResult) {
        event.preventDefault();
        selectGlobalSearchResult(globalSearchResult.dataset.dashboardOpenRepo || "");
        return;
      }

      const starTrigger = event.target.closest("[data-repo-action-star], [data-repo-star-button]");
      if (starTrigger) {
        event.preventDefault();
        event.stopPropagation();
        toggleRepoStar(starTrigger);
        return;
      }

      const openButton = event.target.closest("[data-dashboard-open-repo]");
      if (openButton) {
        const nestedControl = event.target.closest("a, button, input, textarea, select, [contenteditable='true']");
        if (nestedControl && nestedControl !== openButton && openButton.contains(nestedControl)) {
          return;
        }
        event.preventDefault();
        // Repo pages are real documents now: opening one is a real navigation.
        openRepoPage(openButton.dataset.dashboardOpenRepo);
        return;
      }

      const historyButton = event.target.closest("[data-dashboard-history-button]");
      if (historyButton) {
        setRepoTab("commits");
        return;
      }

      const repoTabButton = event.target.closest("[data-dashboard-repo-tab]");
      if (repoTabButton) {
        const tab = repoTabButton.dataset.dashboardRepoTab || "code";
        activateRepoTab(tab);
        return;
      }

      // Watch popover: the button shows the repo's fediverse follower count
      // and opens the follow-from-Mastodon card; any click outside closes it.
      const watchButton = event.target.closest("[data-repo-action-watch]");
      if (watchButton) {
        $("[data-repo-watch-menu]")?.classList.toggle("hidden");
        return;
      }
      if (!event.target.closest("[data-repo-watch-wrap]")) {
        $("[data-repo-watch-menu]")?.classList.add("hidden");
      }

      // GitHub-style Code dropdown: toggle the clone popover scoped to the
      // clicked button; a click anywhere outside a code wrapper closes them.
      const codeButton = event.target.closest("[data-repo-code-button]");
      if (codeButton) {
        const wrap = codeButton.closest("[data-repo-code-wrap]");
        const menu = wrap?.querySelector("[data-repo-code-menu]");
        const willOpen = Boolean(menu?.classList.contains("hidden"));
        $$("[data-repo-code-menu]").forEach((m) => m.classList.add("hidden"));
        $$("[data-repo-code-button]").forEach((b) => b.setAttribute("aria-expanded", "false"));
        if (menu && willOpen) {
          menu.classList.remove("hidden");
          codeButton.setAttribute("aria-expanded", "true");
          // Pre-select the URL so Ctrl/Cmd+C works immediately, like GitHub.
          wrap.querySelector("[data-repo-clone-url]")?.select?.();
        }
        return;
      }
      if (!event.target.closest("[data-repo-code-wrap]")) {
        $$("[data-repo-code-menu]").forEach((m) => m.classList.add("hidden"));
      }

      const aboutEditButton = event.target.closest("[data-repo-about-edit]");
      if (aboutEditButton && state.selectedRepo && sessionOwnsRepo(state.selectedRepo)) {
        setRepoAboutStatus("");
        setRepoAboutEditing(true);
        return;
      }

      const digestPreviewButton = event.target.closest(
        "[data-repo-digest-preview-refresh]");
      if (digestPreviewButton && state.selectedRepo
          && sessionOwnsRepo(state.selectedRepo)) {
        loadRepoDigestPreview(state.selectedRepo);
        return;
      }

      if (event.target.closest("[data-repo-about-cancel]")) {
        setRepoAboutStatus("");
        setRepoAboutEditing(false);
        return;
      }

      const deleteRepoButton = event.target.closest("[data-repo-delete]");
      if (
        deleteRepoButton &&
        state.selectedRepo &&
        sessionOwnsRepo(state.selectedRepo)
      ) {
        const repo = state.selectedRepo;
        const expected = repoKey(repo);
        const status = $("[data-repo-delete-status]");
        const confirmation = window.prompt(
          `Type ${expected} to confirm repository deletion.`,
          "",
        );
        if (confirmation === null) return;
        if (String(confirmation).trim() !== expected) {
          if (status) {
            status.textContent = `Confirmation did not match ${expected}.`;
            status.className = "mt-1 text-xs text-destructive";
          }
          return;
        }
        deleteRepoButton.disabled = true;
        if (status) {
          status.textContent = "Deleting repository…";
          status.className = "mt-1 text-xs text-muted-foreground";
        }
        try {
          await deleteRepoFromWeb(repo);
          location.assign("/dashboard/repos");
        } catch (error) {
          const code = String(error?.message || "");
          if (status) {
            status.textContent =
              code === "not_authorized"
                ? "Only the source node owner can delete this repository."
                : code === "account_required"
                  ? "Sign in as the source node owner first."
                  : "Repository deletion failed. Please try again.";
            status.className = "mt-1 text-xs text-destructive";
          }
          deleteRepoButton.disabled = false;
        }
        return;
      }

      const logoWorkflowSummary = event.target.closest(
        "[data-repo-logo-workflow] > summary",
      );
      if (logoWorkflowSummary && state.selectedRepo) {
        const details = logoWorkflowSummary.parentElement;
        setTimeout(() => {
          if (details?.open) loadRepoLogoSuggestions(state.selectedRepo);
        }, 0);
        return;
      }

      const logoReview = event.target.closest("[data-repo-logo-review]");
      if (logoReview && state.selectedRepo) {
        const action = logoReview.getAttribute("data-repo-logo-review") || "";
        const suggestionId =
          logoReview.getAttribute("data-repo-logo-suggestion-id") || "";
        logoReview.disabled = true;
        setRepoLogoSuggestionStatus(
          action === "approve" ? "Approving…" : "Rejecting…",
        );
        reviewRepoLogoSuggestion(state.selectedRepo, suggestionId, action)
          .then(() =>
            setRepoLogoSuggestionStatus(
              action === "approve" ? "Official logo updated." : "Suggestion rejected.",
              "good",
            ),
          )
          .catch(() =>
            setRepoLogoSuggestionStatus(
              "Only the repository owner or a moderator can review suggestions.",
              "bad",
            ),
          )
          .finally(() => {
            logoReview.disabled = false;
          });
        return;
      }

      // Owner "Fediverse posts" dropdown: load the post list the moment it's
      // expanded (setTimeout so the <details> default toggle has applied), and
      // delete a post from its trash button.
      const fediPostsSummary = event.target.closest("[data-repo-fedi-posts] > summary");
      if (fediPostsSummary && state.selectedRepo) {
        const repo = state.selectedRepo;
        const details = fediPostsSummary.parentElement;
        setTimeout(() => { if (details.open) loadRepoFediPosts(repo); }, 0);
        return;
      }

      const fediPostDelete = event.target.closest("[data-repo-fedi-post-delete]");
      if (fediPostDelete && state.selectedRepo) {
        removeRepoFediPost(
          state.selectedRepo,
          fediPostDelete.getAttribute("data-repo-fedi-post-delete"),
          fediPostDelete);
        return;
      }

      const readmeLink = event.target.closest("[data-repo-readme-link]");
      if (readmeLink && state.selectedRepo) {
        event.preventDefault();
        setRepoTab("code");
        loadRepositoryBlob(state.selectedRepo, readmeLink.dataset.repoReadmePath || "README.md");
        return;
      }

      const activityLink = event.target.closest("[data-repo-activity-link]");
      if (activityLink && state.selectedRepo) {
        event.preventDefault();
        activateRepoTab("insights");
        return;
      }

      const commitButton = event.target.closest("[data-dashboard-commit-hash]");
      if (commitButton && state.selectedRepo) {
        loadRepoCommitDetail(state.selectedRepo, commitButton.dataset.dashboardCommitHash || "");
        return;
      }

      if (event.target.closest("[data-repo-commit-back]") && state.selectedRepo) {
        loadRepoCommits(state.selectedRepo);
        return;
      }

      if (event.target.closest("[data-repo-branch-close]")) {
        closeRepoBranchMenus();
        return;
      }

      if (event.target.closest("[data-repo-branch-view-all]") && state.selectedRepo) {
        setRepoBranchQuery(state.selectedRepo, "");
        updateRepoBranchControls(state.selectedRepo, event.target.closest("[data-repo-branch-control]"));
        return;
      }

      const branchItem = event.target.closest("[data-repo-branch-name]");
      if (branchItem && state.selectedRepo) {
        setRepoSelectedBranch(state.selectedRepo, branchItem.dataset.repoBranchName || "");
        setRepoBranchQuery(state.selectedRepo, "");
        resetRepoFileFinder(state.selectedRepo);
        updateRepoBranchControls(state.selectedRepo, null);
        loadRepositoryTree(state.selectedRepo, "");
        loadRepoFeaturePanels(state.selectedRepo);
        return;
      }

      const branchButton = event.target.closest("[data-repo-branch-button]");
      if (branchButton && state.selectedRepo) {
        await toggleRepoBranchMenu(state.selectedRepo, branchButton);
        return;
      }

      const fileFinderOpenButton = event.target.closest("[data-repo-file-finder-open]");
      if (fileFinderOpenButton) {
        openRepoFileFinder();
        return;
      }

      if (event.target.closest("[data-repo-file-finder-close], [data-repo-file-finder-backdrop]")) {
        closeRepoFileFinder();
        return;
      }

      const fileFinderResult = event.target.closest("[data-repo-file-finder-result]");
      if (fileFinderResult && state.selectedRepo) {
        state.repoFileFinder.selectedIndex = $$('[data-repo-file-finder-result]').indexOf(fileFinderResult);
        closeRepoFileFinder();
        loadRepositoryBlob(state.selectedRepo, fileFinderResult.dataset.repoFileFinderPath || "");
        return;
      }

      const treePathButton = event.target.closest("[data-dashboard-tree-path]");
      if (treePathButton && state.selectedRepo) {
        loadRepositoryTree(state.selectedRepo, treePathButton.dataset.dashboardTreePath || "");
        return;
      }

      const blobPathButton = event.target.closest("[data-dashboard-blob-path]");
      if (blobPathButton && state.selectedRepo) {
        loadRepositoryBlob(state.selectedRepo, blobPathButton.dataset.dashboardBlobPath || "");
        return;
      }

      const issueNewButton = event.target.closest("[data-repo-issue-new]");
      if (issueNewButton && state.selectedRepo) {
        if (!state.session?.nodeName) {
          location.href = "/login";
          return;
        }
        openIssueCompose(state.selectedRepo);
        return;
      }

      const issueImportButton = event.target.closest("[data-repo-issue-import]");
      if (issueImportButton && state.selectedRepo) {
        if (!state.session?.nodeName) {
          location.href = "/login";
          return;
        }
        openIssueImport(state.selectedRepo);
        return;
      }

      const issueTemplateButton = event.target.closest("[data-repo-issue-template]");
      if (issueTemplateButton) {
        downloadIssueCsvTemplate();
        return;
      }

      const pullNewButton = event.target.closest("[data-repo-pull-new]");
      if (pullNewButton && state.selectedRepo) {
        if (!state.session?.nodeName) {
          location.href = "/login";
          return;
        }
        openPullCompose(state.selectedRepo);
        return;
      }

      const pullCancelButton = event.target.closest("[data-repo-pull-cancel]");
      if (pullCancelButton && state.selectedRepo) {
        loadRepoCollection(state.selectedRepo, "pulls", "[data-repo-pulls]");
        return;
      }

      const issueFilterButton = event.target.closest("[data-dashboard-issue-filter]");
      if (issueFilterButton) {
        setIssueFilter(issueFilterButton.dataset.dashboardIssueFilter || "open");
        return;
      }

      const issuesReloadButton = event.target.closest("[data-repo-issues-reload]");
      if (issuesReloadButton && state.selectedRepo) {
        loadRepoIssues(state.selectedRepo);
        return;
      }

      const projectFilterButton = event.target.closest("[data-dashboard-project-filter]");
      if (projectFilterButton) {
        setProjectFilter(projectFilterButton.dataset.dashboardProjectFilter || "open");
        return;
      }

      const projectViewButton = event.target.closest("[data-dashboard-project-view]");
      if (projectViewButton) {
        setProjectView(projectViewButton.dataset.dashboardProjectView || "gantt");
        return;
      }

      const issueCancelButton = event.target.closest("[data-repo-issue-cancel]");
      if (issueCancelButton && state.selectedRepo) {
        renderRepoIssues();
        return;
      }

      const recordButton = event.target.closest("[data-repo-record-kind][data-repo-record-number]");
      if (recordButton && state.selectedRepo) {
        const kind = recordButton.dataset.repoRecordKind || "";
        const number = recordButton.dataset.repoRecordNumber || "";
        // Mirror the opened record into the address bar (/owner/repo/pulls/4)
        // so refresh and the desktop client's "View on website" button land on
        // this same detail page. Pending records have no mirror number yet.
        if (["pulls", "discussions", "issues"].includes(kind) && /^\d+$/.test(number)) {
          navigateHistory(`${repoPathUrl(state.selectedRepo)}/${kind}/${number}`);
        }
        loadRepoRecordDetail(state.selectedRepo, kind, number);
        return;
      }

      const recordBackButton = event.target.closest("[data-repo-record-back]");
      if (recordBackButton && state.selectedRepo) {
        const kind = recordBackButton.dataset.repoRecordBack || "";
        if (["pulls", "discussions", "issues"].includes(kind)) {
          navigateHistory(`${repoPathUrl(state.selectedRepo)}/${kind}`);
        }
        state.repoRecordDetail = null;
        if (kind === "issues") {
          // A deep-linked refresh straight into the issue detail never loaded
          // the list, so fetch it now instead of flashing an empty "No issues".
          if (state.issuesView.items.length) renderRepoIssues();
          else loadRepoIssues(state.selectedRepo);
        } else {
          loadRepoCollection(state.selectedRepo, kind, `[data-repo-${kind}]`);
        }
        return;
      }

      const marketingInitiativeButton = event.target.closest(
        "[data-repo-marketing-initiative]",
      );
      if (marketingInitiativeButton) {
        void moveIssueToMarketingInitiatives(marketingInitiativeButton);
        return;
      }

      const pullViewedButton = event.target.closest("[data-repo-pull-viewed]");
      if (pullViewedButton && state.selectedRepo) {
        toggleRepoPullViewed(
          state.selectedRepo,
          pullViewedButton.dataset.repoPullViewed || "",
        );
        return;
      }

      const pullFileButton = event.target.closest("[data-repo-pull-file]");
      if (pullFileButton) {
        const article = pullFileButton.closest("[data-repo-record-detail]");
        const path = pullFileButton.dataset.repoPullFile || "";
        const target = Array.from(
          article?.querySelectorAll("[data-repo-pull-diff-file]") || [],
        ).find((block) => block.dataset.repoPullDiffFile === path);
        target?.scrollIntoView({ behavior: "smooth", block: "start" });
        return;
      }

      const pullMergeButton = event.target.closest("[data-repo-pull-merge]");
      if (pullMergeButton && state.selectedRepo) {
        handleRepoPullMerge(state.selectedRepo);
        return;
      }

      // PR detail section tabs are isolated panels, matching the GitHub review
      // surface: only the selected section is visible at a time.
      const recordTabButton = event.target.closest("[data-repo-record-tab]");
      if (recordTabButton) {
        const article = recordTabButton.closest("[data-repo-record-detail]");
        if (article) {
          const tab = recordTabButton.dataset.repoRecordTab || "";
          article.querySelectorAll("[data-repo-record-tab]").forEach((button) => {
            const active = button === recordTabButton;
            button.setAttribute("aria-selected", active ? "true" : "false");
            button.classList.toggle("border-primary", active);
            button.classList.toggle("border-transparent", !active);
            button.classList.toggle("text-foreground", active);
            button.classList.toggle("text-muted-foreground", !active);
          });
          article.querySelectorAll("[data-repo-record-panel]").forEach((panel) => {
            panel.classList.toggle(
              "hidden",
              panel.dataset.repoRecordPanel !== tab,
            );
          });
        }
        return;
      }

      const repoCollectionPageButton = event.target.closest("[data-repo-collection-page]");
      if (repoCollectionPageButton && state.selectedRepo) {
        const kind = repoCollectionPageButton.dataset.repoCollectionPage;
        const targetPage = Number(repoCollectionPageButton.dataset.repoCollectionPageTarget);
        if (kind && Number.isFinite(targetPage)) {
          state.repoCollectionPages[kind] = targetPage;
          // Issues keep their open/closed/all filter (and its filter bar) that
          // renderRepoIssues applies; routing pagination through
          // loadRepoCollection would re-render the raw record list unfiltered,
          // leaking closed issues into the default "open" view (issue #420).
          if (kind === "issues") renderRepoIssues();
          else loadRepoCollection(state.selectedRepo, kind, `[data-repo-${kind}]`);
        }
        return;
      }

      const copyButton = event.target.closest("[data-dashboard-copy]");
      if (copyButton) {
        const text = copyButton.dataset.dashboardCopy || "";
        const copied = await copyTextToClipboard(text);
        if (copied) {
          copyButton.classList.add("copied");
          window.setTimeout(() => copyButton.classList.remove("copied"), 1600);
        }
        return;
      }

      const agentsRefreshButton = event.target.closest("[data-repo-agents-refresh]");
      if (agentsRefreshButton && state.selectedRepo) {
        // On the detail page, Refresh reloads that agent's transcript; on the
        // list it reloads the session list (adhoc #259).
        if (state.agentsView.selectedAgentId != null) {
          loadRepoAgentTranscript(state.selectedRepo, state.agentsView.selectedAgentId);
        } else {
          loadRepoAgents(state.selectedRepo);
        }
        return;
      }

      // Open an agent's detail page - live transcript + prompt (adhoc #259).
      // Toggle: clicking a selected agent returns to the list (issue #375).
      const agentOpenButton = event.target.closest("[data-repo-agent-open]");
      if (agentOpenButton && state.selectedRepo) {
        const agentId = agentOpenButton.dataset.repoAgentId || "";
        if (String(state.agentsView.selectedAgentId ?? "") === String(agentId)) {
          closeRepoAgentDetail(state.selectedRepo);
        } else {
          openRepoAgentDetail(state.selectedRepo, agentId);
        }
        return;
      }
      const agentBackButton = event.target.closest("[data-repo-agent-back]");
      if (agentBackButton && state.selectedRepo) {
        closeRepoAgentDetail(state.selectedRepo);
        return;
      }
  });

  document.addEventListener("submit", async (event) => {
    const logoSuggestionForm = event.target.closest(
      "[data-repo-logo-suggestion-form]",
    );
    if (logoSuggestionForm && state.selectedRepo) {
      event.preventDefault();
      const submit = logoSuggestionForm.querySelector('button[type="submit"]');
      if (submit) submit.disabled = true;
      setRepoLogoSuggestionStatus("Submitting…");
      try {
        await submitRepoLogoSuggestion(state.selectedRepo, logoSuggestionForm);
        setRepoLogoSuggestionStatus(
          "Suggestion submitted for owner or moderator review.",
          "good",
        );
      } catch (error) {
        const code = String(error?.message || "");
        setRepoLogoSuggestionStatus(
          code === "sign_in_required"
            ? "Sign in to suggest a logo."
            : code === "rights_required"
              ? "Confirm that you have rights to submit the image."
              : code === "logo_too_large"
                ? "Choose a PNG, JPEG, or WebP image up to 256 KB."
                : "The logo suggestion could not be submitted.",
          "bad",
        );
      } finally {
        if (submit) submit.disabled = false;
      }
      return;
    }
    const settingsForm = event.target.closest("[data-repo-settings-form]");
    if (settingsForm && state.selectedRepo) {
      event.preventDefault();
      const submit = settingsForm.querySelector('button[type="submit"]');
      if (submit) submit.disabled = true;
      setRepoSettingsStatus("Saving…");
      try {
        await saveRepoSettingsFromWeb(state.selectedRepo, {
          fediverse: {
            federate: Boolean(settingsForm.querySelector("[data-repo-ap-federate]")?.checked),
            broadcastEvents: Boolean(settingsForm.querySelector("[data-repo-ap-broadcast]")?.checked),
            acceptComments: Boolean(settingsForm.querySelector("[data-repo-ap-comments]")?.checked),
          },
        });
        setRepoSettingsStatus("Settings saved.", "good");
        loadRepoFediverse(state.selectedRepo);
        loadRepoDigestPreview(state.selectedRepo);
      } catch (error) {
        const code = String(error?.message || "");
        setRepoSettingsStatus(
          code === "not_authorized"
            ? "Only the source node owner can change repository settings."
            : code === "account_required"
              ? "Sign in as the source node owner first."
              : "Could not save repository settings.",
          "bad",
        );
      } finally {
        if (submit) submit.disabled = false;
      }
      return;
    }

    const aboutForm = event.target.closest("[data-repo-about-form]");
    if (aboutForm && state.selectedRepo) {
      event.preventDefault();
      const input = aboutForm.querySelector("[data-repo-about-input]");
      const submit = aboutForm.querySelector('button[type="submit"]');
      const description = String(input?.value || "").trim();
      if (submit) submit.disabled = true;
      setRepoAboutStatus("Saving...");
      try {
        // Optional branding uploads ride along with the description: a chosen
        // file becomes a data-URL PNG, a checked "Remove" sends "" (clear),
        // and an untouched image is simply omitted (left unchanged).
        const media = {};
        const readAsDataUrl = (file) => new Promise((resolve, reject) => {
          const reader = new FileReader();
          reader.onload = () => resolve(String(reader.result || ""));
          reader.onerror = () => reject(new Error("image_read_failed"));
          reader.readAsDataURL(file);
        });
        const logoFile = aboutForm.querySelector("[data-repo-about-logo]")?.files?.[0];
        const bannerFile = aboutForm.querySelector("[data-repo-about-banner]")?.files?.[0];
        if (logoFile && logoFile.size > 256 * 1024) throw new Error("logo_too_large");
        if (bannerFile && bannerFile.size > 1024 * 1024) throw new Error("banner_too_large");
        if (aboutForm.querySelector("[data-repo-about-logo-clear]")?.checked) media.logoPng = "";
        else if (logoFile) media.logoPng = await readAsDataUrl(logoFile);
        if (aboutForm.querySelector("[data-repo-about-banner-clear]")?.checked) media.bannerPng = "";
        else if (bannerFile) media.bannerPng = await readAsDataUrl(bannerFile);
        const website = String(
          aboutForm.querySelector("[data-repo-about-website-input]")?.value || "").trim();
        media.website = website;
        const body = await saveRepoAboutFromWeb(state.selectedRepo, description, media);
        applyRepoAboutDescription(state.selectedRepo, body.description ?? description);
        applyRepoAboutWebsite(website);
        setRepoAboutStatus("Saved.", "good");
        setRepoAboutEditing(false);
        // Refresh the badge header + watch count so the new logo/banner (and
        // the ?v= cache-buster) show immediately.
        loadRepoFediverse(state.selectedRepo);
      } catch (error) {
        const code = String(error?.message || "");
        setRepoAboutStatus(
          code === "not_authorized" ? "Only the source node owner can edit About."
            : code === "account_required" ? "Sign in as the source node owner first."
            : code === "logo_too_large" ? "Logo must be a PNG up to 256 KB."
            : code === "banner_too_large" ? "Banner must be a PNG up to 1 MB."
            : code === "image_too_large" ? "Image too large (logo ≤256 KB, banner ≤1 MB)."
            : code === "bad_image" ? "Images must be PNG files."
            : "Could not save About.",
          "bad");
      } finally {
        if (submit) submit.disabled = false;
      }
      return;
    }
    const issueForm = event.target.closest("[data-repo-issue-form]");
    if (issueForm && state.selectedRepo) {
      event.preventDefault();
      handleIssueComposeSubmit(state.selectedRepo, issueForm);
      return;
    }
    const issueImportForm = event.target.closest("[data-repo-issue-import-form]");
    if (issueImportForm && state.selectedRepo) {
      event.preventDefault();
      handleIssueImportSubmit(state.selectedRepo, issueImportForm);
      return;
    }
    const issueCommentForm = event.target.closest("[data-repo-issue-comment-form]");
    if (issueCommentForm && state.selectedRepo) {
      event.preventDefault();
      handleIssueCommentSubmit(state.selectedRepo, issueCommentForm);
      return;
    }
    const discussionReplyForm = event.target.closest("[data-repo-discussion-reply-form]");
    if (discussionReplyForm && state.selectedRepo) {
      event.preventDefault();
      handleDiscussionReplySubmit(state.selectedRepo, discussionReplyForm);
      return;
    }
    const pullReviewForm = event.target.closest("[data-repo-pull-review-form]");
    if (pullReviewForm && state.selectedRepo) {
      event.preventDefault();
      const action = event.submitter?.dataset.repoPullReviewAction || "comment";
      handlePullReviewSubmit(state.selectedRepo, pullReviewForm, action);
      return;
    }
    const pullNewForm = event.target.closest("[data-repo-pull-new-form]");
    if (pullNewForm && state.selectedRepo) {
      event.preventDefault();
      handlePullComposeSubmit(state.selectedRepo, pullNewForm);
      return;
    }
    const agentPromptForm = event.target.closest("[data-repo-agent-prompt-form]");
    if (agentPromptForm && state.selectedRepo) {
      event.preventDefault();
      handleRepoAgentPromptSubmit(state.selectedRepo, agentPromptForm);
      return;
    }
    const agentNewForm = event.target.closest("[data-repo-agent-new-form]");
    if (agentNewForm) {
      event.preventDefault();
      // The form lives in the header modal (adhoc #62): the target repo comes
      // from the modal's own repository picker, not the open repo page.
      handleRepoAgentNewSubmit(agentModalSelectedRepo(), agentNewForm);
    }
  });

  $("#repoSearch")?.addEventListener("input", () => {
    state.page = 1;
    applyRepositoryFilter();
  });
  $("[data-global-search]")?.addEventListener("focus", () => {
    setGlobalSearchOpen(true);
  });
  $("[data-global-search]")?.addEventListener("input", () => {
    state.globalSearch.open = true;
    state.globalSearch.selectedIndex = 0;
    renderGlobalSearchResults();
  });
  $("[data-global-search]")?.addEventListener("keydown", (event) => {
    if (event.key === "ArrowDown") {
      event.preventDefault();
      moveGlobalSearchSelection(1);
      return;
    }
    if (event.key === "ArrowUp") {
      event.preventDefault();
      moveGlobalSearchSelection(-1);
      return;
    }
    if (event.key === "Enter") {
      event.preventDefault();
      selectGlobalSearchResult();
      return;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      closeGlobalSearch();
    }
  });
  $("[data-home-repo-search]")?.addEventListener("input", () => {
    renderHomeRepositories();
  });
  $("[data-profile-repo-search]")?.addEventListener("input", () => {
    renderProfileRepositories();
  });
  $("[data-repo-prev]")?.addEventListener("click", () => {
    state.page -= 1;
    updateRepositoryPagination();
  });
  $("[data-repo-next]")?.addEventListener("click", () => {
    state.page += 1;
    updateRepositoryPagination();
  });

  // New-repository modal (adhoc #30): open from the Repos header, collect the
  // create-and-mirror details, then hand off to the desktop node (see
  // handleNewRepoSubmit — the signed publish + git mirror are desktop-only).
  $("[data-new-repo-open]")?.addEventListener("click", () => setNewRepoModalOpen(true));
  $("[data-new-repo-close]")?.addEventListener("click", () => setNewRepoModalOpen(false));
  $("[data-new-repo-backdrop]")?.addEventListener("click", () => setNewRepoModalOpen(false));
  $("[data-new-repo-cancel]")?.addEventListener("click", () => setNewRepoModalOpen(false));
  $$("[data-new-repo-source]").forEach((btn) => {
    btn.addEventListener("click", () => setNewRepoSource(btn.dataset.newRepoSource));
  });
  $("[data-new-repo-form]")?.addEventListener("submit", (event) => {
    event.preventDefault();
    handleNewRepoSubmit();
  });
  $("[data-external-repo-list]")?.addEventListener("click", (event) => {
    const button = event.target.closest("[data-external-mirror-volunteer]");
    if (!button) return;
    volunteerForExternalMirror(
      button.dataset.externalMirrorVolunteer || "", button);
  });
  $("[data-external-repo-list]")?.addEventListener("change", (event) => {
    const checkbox = event.target.closest("[data-external-repo-select]");
    if (!checkbox) return;
    const id = String(checkbox.dataset.externalRepoSelect || "");
    if (checkbox.checked) state.externalRepositorySelection.add(id);
    else state.externalRepositorySelection.delete(id);
    syncExternalRepositoryActions();
  });
  $("[data-external-repo-select-all]")?.addEventListener("change", (event) => {
    toggleAllExternalRepositories(Boolean(event.currentTarget.checked));
  });
  $("[data-external-repo-delete-selected]")?.addEventListener("click", () => {
    deleteSelectedExternalRepositories();
  });

  // [data-profile-settings-button] is a real link to /dashboard/settings now,
  // and [data-settings-section-link] clicks are handled by the delegated
  // document click handler above (with push: true for URL reflection).
  $("[data-profile-modal-close]")?.addEventListener("click", () => setProfileModalOpen(false));
  $("[data-profile-modal-backdrop]")?.addEventListener("click", () => setProfileModalOpen(false));
  $("[data-profile-save]")?.addEventListener("click", saveProfile);
  $("[data-profile-verify-email]")?.addEventListener("click", resendVerification);
  $("[data-profile-rename-input]")?.addEventListener("input", () => {
    window.clearTimeout(state.nodeNameAvailability.timer);
    state.nodeNameAvailability.timer = window.setTimeout(checkNodeNameAvailability, 250);
  });
  $("[data-profile-rename-password]")?.addEventListener("input", updateRenameButton);
  $("[data-profile-delete-confirm]")?.addEventListener("input", () => {
    const button = $("[data-profile-delete-account]");
    if (!button) return;
    button.disabled = ($("[data-profile-delete-confirm]")?.value || "").trim() !== "DELETE";
    button.classList.toggle("opacity-40", button.disabled);
  });

  $("[data-ssh-key-form]")?.addEventListener("submit", (event) => {
    event.preventDefault();
    addSshKey();
  });

  document.addEventListener("input", (event) => {
    if (event.target?.matches?.('[data-repo-filter-query="issues"]') && state.selectedRepo) {
      state.issuesView.query = event.target.value || "";
      renderRepoIssues();
      return;
    }
    if (event.target?.matches?.("[data-repo-branch-search]") && state.selectedRepo) {
      setRepoBranchQuery(state.selectedRepo, event.target.value || "");
      updateRepoBranchControls(state.selectedRepo, event.target.closest("[data-repo-branch-control]"));
      return;
    }
    if (event.target?.matches?.("[data-repo-file-finder-input]")) {
      state.repoFileFinder.selectedIndex = 0;
      renderRepoFileFinderResults(event.target.value || "");
    }
  });

  document.addEventListener("keydown", (event) => {
    const typingTarget = event.target?.matches?.("input, textarea, select, [contenteditable='true']");
    if (event.target?.matches?.("[data-home-agent-input]") && event.key === "Enter" && (event.metaKey || event.ctrlKey)) {
      event.preventDefault();
      submitHomeAgentPrompt();
      return;
    }
    if (!typingTarget && event.key === "/") {
      if (focusGlobalSearch()) event.preventDefault();
      return;
    }
	    if (event.key === "Escape") {
	      setProfileModalOpen(false);
	      setNotificationDropdownOpen(false);
	      setNotificationModalOpen(false);
	      setAgentModalOpen(false);
	      setNewRepoModalOpen(false);
	      closeRepoBranchMenus();
	      closeRepoFileFinder();
	      closeGlobalSearch();
	      closeMobileDrawers();
	      return;
	    }
    const openCard = event.target?.closest?.("[data-dashboard-open-repo][role='link']");
    if (!typingTarget && openCard && (event.key === "Enter" || event.key === " ")) {
      event.preventDefault();
      openRepoPage(openCard.dataset.dashboardOpenRepo);
      return;
    }
    if (fileFinderOpen()) {
      if (event.key === "ArrowDown") {
        event.preventDefault();
        moveRepoFileFinderSelection(1);
        return;
      }
      if (event.key === "ArrowUp") {
        event.preventDefault();
        moveRepoFileFinderSelection(-1);
        return;
      }
      if (event.key === "Enter") {
        event.preventDefault();
        selectRepoFileFinderResult();
        return;
      }
    }
    if (!typingTarget && event.key.toLowerCase() === "t" && state.selectedRepo) {
      event.preventDefault();
      openRepoFileFinder();
    }
  });

  // History handling is page-scoped now: only the repo page (tabs/tree/blob)
  // and the settings page (sub-tabs) push same-document states. Back/Forward
  // across pages is native navigation between real documents.
  function initPageHistory() {
    const page = currentPage();
    if (page === "settings") {
      window.addEventListener("popstate", () => {
        setSettingsSection(settingsSectionFromPath(), { scroll: false });
      });
      return;
    }
    if (page !== "repo") return;
    window.addEventListener("popstate", () => {
      const requested = requestedRepoKey();
      const repo = requested ? findRepository(requested) : null;
      if (!repo) {
        // The entry points outside this repo document — a real navigation.
        location.reload();
        return;
      }
      if (state.selectedRepo && repoKey(state.selectedRepo) === repoKey(repo)) {
        // Same repo; restore the path/tab from the URL instead of tearing down
        // and rebuilding the whole detail view.
        const parts = repoRouteParts();
        const kind = parts[2];
        const path = parts.length > 3 ? parts.slice(3).map(decodeURIComponent).join("/") : "";
        if (repoTabRoutesFor(repo).includes(kind)) {
          // Feature tab (issues, pulls, etc.): restore without re-loading
          // records since they cache in state.
          setRepoTab(kind);
          // Step Back/Forward between a record detail (/pulls/4) and its list.
          if (["pulls", "discussions", "issues"].includes(kind)) {
            if (/^\d+$/.test(path)) {
              loadRepoRecordDetail(repo, kind, path);
            } else if (state.repoRecordDetail?.kind === kind) {
              state.repoRecordDetail = null;
              // Issues re-render through their filtered list (loadRepoCollection
              // would leak closed issues into the default Open view); fetch it
              // if a deep-link landing never populated the list.
              if (kind === "issues") {
                if (state.issuesView.items.length) renderRepoIssues();
                else loadRepoIssues(repo);
              } else {
                loadRepoCollection(repo, kind, `[data-repo-${kind}]`);
              }
            }
          }
        } else if (kind === "blob" && path) {
          loadRepositoryBlob(repo, path);
        } else {
          loadRepositoryTree(repo, kind === "tree" ? path : "");
        }
        return;
      }
      renderRepoDetail(repo);
    });
  }
