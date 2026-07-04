  function renderRepoDetail(repo) {
    const detail = $("[data-repo-detail]");
    if (!detail || !repo) return;
    state.selectedRepo = repo;
    state.repoCollectionPages = { issues: 1, pulls: 1 };
    state.repoMirrors = [];
    state.repoServedBy = null;
    state.agentsView = { agents: [], selectedAgentId: null };
    stopRepoAgentsAutoRefresh();
    // Pull requests and discussions load lazily the first time their tab is
    // opened rather than on every page load. Eagerly fetching every record's
    // blob up front is what flooded the host with requests and tripped the rate
    // limit after a few refreshes; this map remembers which tabs have loaded.
    state.loadedRepoTabs = {};
    // Show the clean, shareable /owner/name URL in the address bar instead of
    // the /dashboard/owner/name... path that 404.html bounces refreshed repo
    // links (including /owner/name/issues etc.) to. Carry over whatever tab or
    // tree/blob suffix the incoming URL already pointed at instead of
    // collapsing it to the bare repo root — otherwise a refresh on the Issues
    // tab would lose its place and land back on Code. Only trust that suffix
    // when the URL is actually addressing THIS repo already (a fresh open from
    // the repo list/sidebar while some other repo's tab URL is showing should
    // still land on Code, not inherit the other repo's tab).
    const routeParts = repoRouteParts();
    const routeMatchesRepo = routeParts.length >= 2
      && decodeURIComponent(routeParts[0]) === (repo.owner || "")
      && decodeURIComponent(routeParts[1]) === (repo.name || "");
    const routeKind = routeMatchesRepo ? routeParts[2] : undefined;
    const routePath = routeMatchesRepo && routeParts.length > 3 ? routeParts.slice(3).map(decodeURIComponent).join("/") : "";
    const detailPath = repoTabRoutesFor(repo).includes(routeKind)
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
    const commitId = String(repo.rootCommit || repo.latestCommit || repo.commit || "").slice(0, 7) || "live";
    const updatedAt = formatDate(repo.updatedAt || repo.lastSync);
    const live = repoIsLive(repo);
    const viaMirror = repoServedByMirror(repo);
    const tabMeta = {
      code: { label: "Code", icon: "code-2", count: "" },
      commits: { label: "Commits", icon: "git-commit-horizontal", count: commitsCount },
      releases: { label: "Releases", icon: "tag", count: "" },
      issues: { label: "Issues", icon: "circle-dot", count: issuesCount },
      pulls: { label: "Pull requests", icon: "git-pull-request", count: pullsCount },
      discussions: { label: "Discussions", icon: "message-square", count: discussionsCount },
      mirrors: { label: "Mirrors", icon: "radio", count: mirrorsCount },
      agents: { label: "Agents", icon: "bot", count: "" },
    };
    const canSeeAgentsTab = sessionCanAssignAgent(repo);
    detail.innerHTML = `
      <div data-repo-layout="github-like" class="min-w-0">
        <div class="rounded-t-lg border border-border bg-background">
          <div class="grid gap-4 border-b border-border p-4 lg:grid-cols-[minmax(0,1fr)_auto]">
            <div class="min-w-0">
              <div class="flex min-w-0 flex-wrap items-center gap-2">
                <i data-lucide="book-marked" class="h-4 w-4 text-muted-foreground"></i>
                <h2 class="min-w-0 truncate text-lg font-semibold text-foreground"><span class="text-muted-foreground">${escapeHtml(repo.owner || "owner")}/</span>${escapeHtml(repo.name || "repository")}</h2>
                <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${repo.isPrivate ? "private" : "public"}</span>
                <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono ${live ? "text-primary" : "text-muted-foreground"}">${viaMirror ? "served by mirror" : live ? "host online" : "host offline"}</span>
              </div>
              <p class="mt-2 max-w-3xl text-sm leading-6 text-muted-foreground">${escapeHtml(repo.description || "No description published.")}</p>
            </div>
            <div aria-label="Repository facts" class="flex flex-wrap items-start gap-2 lg:justify-end">
              <span class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs text-muted-foreground"><i data-lucide="radio" class="h-3.5 w-3.5"></i>Mirrors <span data-dashboard-repo-count="mirrors" class="font-mono text-foreground">${tabCountLabel(mirrorsCount)}</span></span>
              <span class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs text-muted-foreground"><i data-lucide="hard-drive" class="h-3.5 w-3.5"></i>Data <span class="font-mono text-foreground">${escapeHtml(formatSize(repo.sizeBytes))}</span></span>
              <span class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs text-muted-foreground"><i data-lucide="activity" class="h-3.5 w-3.5"></i>Host <span class="font-mono ${live ? "text-primary" : "text-muted-foreground"}">${viaMirror ? "via mirror" : live ? "online" : "offline"}</span></span>
            </div>
          </div>
          <div class="flex min-w-0 overflow-x-auto px-3" role="tablist">
            ${["code", "commits", "releases", "issues", "pulls", "discussions", "mirrors", ...(canSeeAgentsTab ? ["agents"] : [])].map((tab) => {
              const meta = tabMeta[tab];
              const iconAttr = tab === "issues"
                ? 'data-lucide="circle-dot"'
                : tab === "pulls"
                  ? 'data-lucide="git-pull-request"'
                  : `data-lucide="${meta.icon}"`;
              return `<button type="button" role="tab" data-dashboard-repo-tab="${tab}" aria-selected="${tab === "code" ? "true" : "false"}" class="relative inline-flex h-12 items-center gap-2 border-b-2 px-3 text-xs font-medium transition-colors ${tab === "code" ? "border-primary text-foreground" : "border-transparent text-muted-foreground hover:bg-secondary hover:text-foreground"}"><i ${iconAttr} class="h-3.5 w-3.5"></i><span>${meta.label}</span>${meta.count !== "" ? `<span data-dashboard-repo-tab-count="${tab}" class="rounded-full bg-secondary px-1.5 py-0.5 text-[10px] font-mono text-muted-foreground">${tabCountLabel(meta.count)}</span>` : ""}</button>`;
            }).join("")}
          </div>
        </div>
        ${canSeeAgentsTab ? renderRepoAgentNewComposer() : ""}
	        <div data-repo-content-grid class="grid min-w-0 gap-5 pt-5 xl:grid-cols-[minmax(0,1fr)_18rem]">
		          <div class="min-w-0">
		            <section data-dashboard-repo-tab-panel="code">
		              <div data-repo-root-toolbar class="grid gap-2 md:grid-cols-[auto_minmax(0,1fr)_auto]">
		                ${renderRepoBranchToolbar(repo, branch)}
		                <button type="button" data-repo-file-finder-open class="inline-flex h-9 min-w-0 items-center gap-2 rounded-md border border-border bg-background px-3 text-left text-xs text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="search" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Go to file</span><span class="ml-auto hidden rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground sm:inline">T</span></button>
		                <button data-dashboard-copy="git clone ${escapeHtml(cloneUrl(repo))}" class="copy-button inline-flex h-9 items-center justify-center gap-2 rounded-md border border-primary/40 bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90 transition-colors"><i data-lucide="copy" class="copy-icon h-3.5 w-3.5"></i><i data-lucide="check" class="copy-check h-3.5 w-3.5"></i>Copy clone</button>
		              </div>
		              <div data-repo-pathbar class="my-3 flex min-w-0 flex-col gap-2 md:flex-row md:items-center md:justify-between">
			                <div class="flex min-w-0 items-center gap-2">
			                  <div class="min-w-0 truncate text-xs text-muted-foreground" data-repo-breadcrumb></div>
			                  <span data-repo-served-by hidden title="Mirror node that served this page (round-robined across online mirrors)" class="shrink-0 items-center gap-1 rounded-full border border-border bg-background px-2 py-0.5 font-mono text-[10px] text-muted-foreground"></span>
			                </div>
		                <div data-repo-focus-actions class="hidden flex shrink-0 flex-wrap items-center gap-2">
		                  <button type="button" data-repo-file-finder-open class="inline-flex h-8 min-w-0 items-center gap-2 rounded-md border border-border bg-background px-3 text-left text-xs text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="search" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Go to file</span><span class="ml-auto rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">T</span></button>
		                  <button data-dashboard-copy="git clone ${escapeHtml(cloneUrl(repo))}" class="copy-button inline-flex h-8 items-center justify-center gap-2 rounded-md border border-primary/40 bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90 transition-colors"><i data-lucide="copy" class="copy-icon h-3.5 w-3.5"></i><i data-lucide="check" class="copy-check h-3.5 w-3.5"></i>Copy clone</button>
		                </div>
		              </div>
		              <div data-repo-code-workspace class="min-w-0 gap-4">
		                <aside data-repo-code-explorer class="hidden min-w-0 overflow-hidden rounded-lg border border-border bg-background">
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
		                  <div data-repo-tree-panel class="overflow-hidden rounded-lg border border-border bg-background">
	                    <div data-repo-commit-summary class="grid gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs sm:grid-cols-[minmax(0,1fr)_auto_auto_auto] sm:items-center">
	                      <div class="flex min-w-0 items-center gap-2">
	                        <span class="flex h-6 w-6 shrink-0 items-center justify-center rounded-full border border-primary/30 bg-primary/10 font-mono text-[10px] font-semibold text-primary">${escapeHtml((repo.owner || "F")[0] || "F").toUpperCase()}</span>
	                        <span class="min-w-0 truncate text-foreground font-medium">${escapeHtml(repo.maintainer || repo.owner || "maintainer")}</span>
	                        <span class="min-w-0 truncate text-muted-foreground">published latest mirror metadata</span>
	                      </div>
	                      <span class="font-mono text-muted-foreground">${escapeHtml(commitId)}</span>
	                      <span class="font-mono text-muted-foreground">${escapeHtml(updatedAt)}</span>
	                      <button type="button" data-dashboard-history-button aria-label="Open commit history" class="inline-flex items-center gap-1 font-medium text-foreground hover:text-primary transition-colors"><i data-lucide="history" class="h-3.5 w-3.5 text-muted-foreground"></i>History</button>
	                    </div>
	                    <div class="grid grid-cols-[1.5rem_minmax(0,1fr)_auto] gap-3 border-b border-border bg-secondary/25 px-4 py-2 text-[10px] font-semibold uppercase tracking-wide text-muted-foreground sm:grid-cols-[1.5rem_minmax(9rem,0.8fr)_minmax(0,1fr)_auto]">
	                      <span></span>
	                      <span>Name</span>
	                      <span class="hidden sm:block">Last commit message</span>
	                      <span>Type</span>
	                    </div>
	                    <div data-repo-tree></div>
	                  </div>
	                  <div data-repo-blob class="hidden"></div>
	                  <section data-repo-readme class="mt-4 overflow-hidden rounded-lg border border-border bg-background">
	                    <div data-repo-readme-filename class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="book-open" class="h-3.5 w-3.5 text-muted-foreground"></i>README.md</div>
	                    <div data-repo-readme-body class="p-4 text-sm leading-6 text-muted-foreground">
	                      <p class="mt-1">Loading README...</p>
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
            ${renderRepoCollectionPanel("issues", repo, null, null)}
            ${renderRepoCollectionPanel("pulls", repo, null, null)}
            <section data-dashboard-repo-tab-panel="discussions" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="message-square" class="h-3.5 w-3.5 text-muted-foreground"></i>Discussions and comments</span><span class="rounded-md border border-border px-3 py-1.5 text-xs text-muted-foreground">Create from desktop client for signed submissions</span></div><div data-repo-discussions></div></div></section>
            <section data-dashboard-repo-tab-panel="mirrors" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="radio" class="h-3.5 w-3.5 text-primary"></i>Mirrors</span><span class="font-mono text-[10px] text-muted-foreground">live host health</span></div><div data-repo-mirrors></div></div></section>
            ${canSeeAgentsTab ? `<section data-dashboard-repo-tab-panel="agents" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="bot" class="h-3.5 w-3.5 text-primary"></i>Agents</span><button type="button" data-repo-agents-refresh class="inline-flex h-7 items-center gap-1.5 rounded-md border border-border px-2.5 text-xs font-medium text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="refresh-cw" class="h-3.5 w-3.5"></i>Refresh</button></div><div data-repo-agents></div></div></section>` : ""}
          </div>
          <aside data-repo-about class="min-w-0 rounded-lg border border-border bg-background p-4">
            <div class="flex items-center justify-between gap-3">
              <h3 class="text-sm font-semibold text-foreground">About</h3>
              <i data-lucide="settings" class="h-3.5 w-3.5 text-muted-foreground"></i>
            </div>
            <p class="mt-3 text-sm leading-6 text-foreground">${escapeHtml(repo.description || "No description published.")}</p>
            <div class="mt-4 grid gap-2 text-xs text-muted-foreground">
              <a href="${escapeHtml(cloneUrl(repo))}" class="dashboard-accent-link inline-flex min-w-0 items-center gap-2 hover:underline"><i data-lucide="link" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Open clean URL</span></a>
              <div class="inline-flex items-center gap-2"><i data-lucide="book-open" class="h-3.5 w-3.5"></i><span>Readme</span></div>
              <div class="inline-flex items-center gap-2"><i data-lucide="activity" class="h-3.5 w-3.5"></i><span>Activity</span></div>
            </div>
            <div class="mt-5 border-t border-border pt-4">
	              <h4 class="text-xs font-semibold text-foreground">Repository metadata</h4>
	              <dl class="mt-3 grid gap-3 text-xs">
	                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Channel</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(repo.channel || "general")}</dd></div>
	                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Source</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(repo.source || "desktop")}</dd></div>
	                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Maintainer</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(repo.maintainer || repo.owner || "unknown")}</dd></div>
	                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Updated</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(updatedAt)}</dd></div>
	              </dl>
	            </div>
		            <div data-repo-live-summary class="mt-5 border-t border-border pt-4">
		              <h4 class="text-xs font-semibold text-foreground">Live mirror</h4>
		              <dl class="mt-3 grid gap-3 text-xs">
		                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Mirrors</dt><dd data-dashboard-repo-count="mirrors" class="min-w-0 truncate text-right text-foreground font-mono">${tabCountLabel(mirrorsCount)}</dd></div>
		                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Data</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(formatSize(repo.sizeBytes))}</dd></div>
		                <div class="grid grid-cols-[auto_minmax(0,1fr)] items-center gap-3"><dt class="text-muted-foreground">Clone</dt><dd class="min-w-0 truncate text-right font-mono ${live ? "text-foreground" : "text-muted-foreground"}">${viaMirror ? "via mirror" : live ? "available" : "offline"}</dd></div>
		              </dl>
		              <div data-repo-live-mirror-list class="mt-3 overflow-hidden rounded-md border border-border"></div>
		            </div>
          </aside>
        </div>
      </div>`;
    setSection("explore");
    window.lucide?.createIcons();
    // Restore whichever tab the URL points at (e.g. a refresh on
    // /owner/repo/issues) instead of always defaulting back to Code.
    setRepoTab(repoTabRoutesFor(repo).includes(routeKind) ? routeKind : "code");
    loadRepositoryTree(repo, routeKind === "tree" ? routePath : "");
    if (routeKind === "blob" && routePath) loadRepositoryBlob(repo, routePath);
    loadRepoFeaturePanels(repo);
  }

  function findRepository(key) {
    return state.repositories.find((repo) => repoKey(repo) === key);
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
      ["Clones", formatCount(row.clonesServed)],
      ["Website", formatCount(row.websiteServed)],
      ["Artifacts", formatCount(row.artifactCount)],
    ];
    return [...textChips, ...countChips]
      .map(([label, value]) => `
        <span class="inline-flex items-center gap-1 rounded-md border border-border px-1.5 py-0.5 text-[10px] font-mono">
          <span class="text-muted-foreground">${escapeHtml(label)}</span>
          <span class="text-foreground">${escapeHtml(value === "" || value === undefined || value === null ? "—" : value)}</span>
        </span>`)
      .join("");
  }

  function renderNetworkRows(rows) {
    const list = $("[data-network-node-list]");
    const rail = $("[data-network-rail-nodes]");
    const count = $("[data-network-node-count]");
    const recent = rows.slice(0, 6);
    const onlineCount = rows.filter((row) => row.online).length;

    if (count) count.textContent = `${formatCount(onlineCount)} online`;
    if (list) {
      list.innerHTML = recent.length
        ? recent.map((row) => `
          <div class="px-4 py-3 hover:bg-secondary/50 transition-colors">
            <div class="flex items-center gap-4">
              <div class="flex items-center gap-2 flex-1 min-w-0">
                <i data-lucide="circle" class="${nodeDotClass(row, "w-2 h-2")}"></i>
                <span class="text-sm ${row.online ? "text-foreground" : "text-muted-foreground"} font-medium truncate font-mono">${escapeHtml(row.name || "node")}</span>
              </div>
              <span class="text-xs text-muted-foreground w-20 text-right font-mono">${escapeHtml(nodeMetaLabel(row))}</span>
            </div>
            <div class="mt-2 flex flex-wrap gap-1.5 pl-4">${nodeDetailChips(row)}</div>
          </div>
        `).join("")
        : '<div class="px-4 py-3 text-sm text-muted-foreground">No nodes online right now.</div>';
    }
    if (rail) {
      rail.innerHTML = recent.slice(0, 3).length
        ? recent.slice(0, 3).map((row) => `
          <div class="flex items-center gap-2">
            <i data-lucide="circle" class="${nodeDotClass(row, "w-1.5 h-1.5")}"></i>
            <span class="text-xs ${row.online ? "text-foreground" : "text-muted-foreground"} truncate flex-1 font-mono">${escapeHtml(row.name || "node")}</span>
            ${row.version ? `<span class="text-[10px] text-muted-foreground/70 font-mono">v${escapeHtml(row.version)}</span>` : ""}
            <span class="text-[10px] text-muted-foreground font-mono">${escapeHtml(nodeMetaLabel(row))}</span>
          </div>
        `).join("")
        : '<div class="text-xs text-muted-foreground">No nodes online right now.</div>';
    }
  }

  async function renderNetwork() {
    try {
      const [stats, leaderboards, history] = await Promise.all([
        fetchJson("/api/network/stats"),
        fetchJson("/api/network/leaderboards"),
        fetchJson("/api/network/online-history"),
      ]);
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
      $("[data-network-rail-hosts]") && ($("[data-network-rail-hosts]").textContent = formatCount(hosts));
      $("[data-network-rail-repos]") && ($("[data-network-rail-repos]").textContent = formatCount(repos));
      $("[data-network-rail-clients]") && ($("[data-network-rail-clients]").textContent = formatCount(clients));
      $("[data-network-rail-uptime]") && ($("[data-network-rail-uptime]").textContent = activeMinutes ? "Active" : "Idle");

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
      renderNetworkRows(rows);
    } catch (_) {
      $("[data-network-node-list]") && ($("[data-network-node-list]").innerHTML =
        '<div class="px-4 py-3 text-sm text-muted-foreground">Network data is unavailable right now.</div>');
      $("[data-network-rail-nodes]") && ($("[data-network-rail-nodes]").innerHTML =
        '<div class="text-xs text-muted-foreground">Network data unavailable.</div>');
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
      list.innerHTML = '<div class="px-3 py-4 text-xs text-muted-foreground">No notifications yet. Mentions, PRs, assignments, shares, bounties, releases, and host status changes will appear here.</div>';
      return;
    }
    list.innerHTML = items.map((item) => `
      <button type="button" data-notification-open="${escapeHtml(item.id || "")}" class="w-full px-3 py-2 text-left hover:bg-secondary transition-colors ${item.readAt ? "opacity-70" : ""}">
        <div class="flex items-start gap-2">
          <i data-lucide="${notificationIcon(item.kind)}" class="mt-0.5 h-3.5 w-3.5 ${item.readAt ? "text-muted-foreground" : "text-primary"}"></i>
          <div class="min-w-0 flex-1">
            <p class="truncate text-xs font-medium text-foreground">${escapeHtml(item.title || "Notification")}</p>
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
      detail.innerHTML = '<div class="text-sm text-muted-foreground">Select a notification to read it.</div>';
      return;
    }
    detail.innerHTML = `
      <div class="flex items-start gap-3">
        <span class="flex h-9 w-9 shrink-0 items-center justify-center rounded-full border border-border bg-secondary text-primary">
          <i data-lucide="${notificationIcon(item.kind)}" class="h-4 w-4"></i>
        </span>
        <div class="min-w-0 flex-1">
          <p class="text-sm font-semibold text-foreground">${escapeHtml(item.title || "Notification")}</p>
          <p class="mt-1 text-xs text-muted-foreground">${escapeHtml(notificationTimeLabel(item.ts))}${item.repo ? ` · ${escapeHtml(item.repo)}` : ""}</p>
        </div>
      </div>
      <p class="mt-5 whitespace-pre-wrap text-sm leading-6 text-muted-foreground">${escapeHtml(item.body || "ForkMesh notification")}</p>
      ${item.href ? `<a href="${escapeHtml(item.href)}" class="mt-5 inline-flex h-9 items-center justify-center rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary transition-colors">Open context</a>` : ""}
    `;
    window.lucide?.createIcons();
  }

  function renderNotificationModal() {
    const list = $("[data-notification-modal-list]");
    if (!list) return;
    if (!state.notifications.length) {
      list.innerHTML = '<div class="p-3 text-xs text-muted-foreground">No notifications yet.</div>';
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
          <span class="block truncate text-xs font-medium">${escapeHtml(item.title || "Notification")}</span>
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
  }

  function setNotificationDropdownOpen(open) {
    const toggle = $("#notificationToggle");
    const dropdown = $("#notificationDropdown");
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
        body: JSON.stringify({ node, ids, all }),
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

  async function renderAppVersion() {
    // Show the live ForkMesh release version (same number as the desktop app —
    // deploy.sh stamps it from qt_client/CMakeLists.txt as the APP_VERSION Worker
    // var) next to the logo. Best-effort: stay hidden if the endpoint or version
    // is unavailable so the header never shows a broken "v".
    const el = $("[data-app-version]");
    if (!el) return;
    try {
      const data = await fetchJson("/api/version");
      const version = (data && data.version ? String(data.version) : "").trim();
      if (!version) return;
      el.textContent = version[0] === "v" ? version : "v" + version;
      el.classList.remove("hidden");
    } catch (_) {
      /* leave the version chip hidden */
    }
  }

  async function init() {
    const session = readSession();
    state.session = session;
    renderAppVersion();
    const grant = pendingLinkGrant();
    if (grant && !session?.nodeName) {
      // A link grant arrived but nobody is logged in: bounce through login and
      // come straight back with the grant intact so the link completes then.
      location.replace("/login?next=" + encodeURIComponent(`${location.pathname}${location.search}`));
      return;
    }
    const requested = requestedRepoKey();
    // Guests can browse repositories without an account: instead of bouncing
    // signed-out visitors back to the landing page, the header swaps the
    // profile/notification controls for a Sign Up / Log In link.
    const guest = !session || (!session.nodeName && !session.email);
    if (guest) {
      const authLink = $("[data-guest-auth-link]");
      if (authLink) {
        authLink.classList.remove("hidden");
        authLink.classList.add("inline-flex");
      }
      $("[data-profile-settings-button]")?.classList.add("hidden");
      $("#notificationToggle")?.classList.add("hidden");
    }

    renderProfile(session || { nodeName: "guest" });
    if (session?.nodeName) {
      if (grant) offerLinkGrant(grant);
      // Seed the poll tokens and do the initial full profile + notification
      // load in one pass; subsequent ticks poll /api/poll and only re-fetch
      // what actually changed.
      await pollStatus(true);
      startProfileSync();
    }
    try {
      const data = await fetchJson("/api/repositories");
      renderRepositories(data.repositories, session);
      if (requested) {
        const repo = findRepository(requested);
        if (repo) renderRepoDetail(repo);
        else showSection(requestedSection() || "repos", { push: false });
      } else {
        // Refresh landed on a section URL (?section=network/profile/...) —
        // restore it instead of falling back to the repos list.
        showSection(requestedSection() || "repos", { push: false });
      }
    } catch (_) {
      const list = $("#repoList");
      const count = $("[data-repo-count]");
      if (count) count.textContent = "Unavailable";
      if (list) {
        list.innerHTML = '<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">Repository catalog is temporarily unavailable.</div>';
      }
      renderSidebarRepositories(session);
    }
    renderNetwork();
  }

  document.addEventListener("click", async (event) => {
    const mobileMenuToggle = event.target.closest("[data-mobile-menu-toggle]");
    if (mobileMenuToggle) {
      setMobileSidebarOpen(!document.body.classList.contains("dashboard-sidebar-open"));
      return;
    }

    const mobileNetworkToggle = event.target.closest("[data-mobile-network-toggle]");
    if (mobileNetworkToggle) {
      setMobileNetworkOpen(!document.body.classList.contains("network-drawer-open"));
      return;
    }

    if (event.target.closest("[data-mobile-sidebar-backdrop], [data-mobile-drawer-close]")) {
      setMobileSidebarOpen(false);
      return;
    }

    if (event.target.closest("[data-mobile-network-backdrop], [data-mobile-network-close]")) {
      setMobileNetworkOpen(false);
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

    const profileSettings = event.target.closest("[data-profile-settings-button]");
    if (profileSettings) {
      $("[data-profile-popover]")?.classList.add("hidden");
      $("[data-profile-toggle]")?.setAttribute("aria-expanded", "false");
      setProfileHint("", "");
      showSection("profile");
      closeMobileDrawers();
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
    if (!event.target.closest("[data-repo-branch-control]")) {
      closeRepoBranchMenus();
    }

    const sectionButton = event.target.closest("[data-section]");
    if (sectionButton) {
      const targetSection = sectionButton.dataset.section;
      // Each section gets its own address-bar entry (?section=network, ...) so
      // Back returns to the repo/prior page instead of exiting the app AND a
      // refresh keeps you here. "explore" is the repo-detail view, addressed by
      // its /owner/repo path, so it manages its own URL.
      if (targetSection !== "explore") {
        showSection(targetSection);
      } else {
        setSection(targetSection);
      }
      closeMobileDrawers();
    }

    const pageButton = event.target.closest("[data-dashboard-repo-page]");
    if (pageButton) {
      state.page = Number(pageButton.dataset.dashboardRepoPage) || 1;
      updateRepositoryPagination();
    }

      const openButton = event.target.closest("[data-dashboard-open-repo]");
      if (openButton) {
        const repo = findRepository(openButton.dataset.dashboardOpenRepo);
        if (repo) {
          closeMobileDrawers();
          renderRepoDetail(repo);
        }
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

      const issueFilterButton = event.target.closest("[data-dashboard-issue-filter]");
      if (issueFilterButton) {
        setIssueFilter(issueFilterButton.dataset.dashboardIssueFilter || "open");
        return;
      }

      const issueCancelButton = event.target.closest("[data-repo-issue-cancel]");
      if (issueCancelButton && state.selectedRepo) {
        renderRepoIssues();
        return;
      }

      const recordButton = event.target.closest("[data-repo-record-kind][data-repo-record-number]");
      if (recordButton && state.selectedRepo) {
        loadRepoRecordDetail(state.selectedRepo, recordButton.dataset.repoRecordKind || "", recordButton.dataset.repoRecordNumber || "");
        return;
      }

      const recordBackButton = event.target.closest("[data-repo-record-back]");
      if (recordBackButton && state.selectedRepo) {
        const kind = recordBackButton.dataset.repoRecordBack || "";
        if (kind === "issues") {
          renderRepoIssues();
        } else {
          loadRepoCollection(state.selectedRepo, kind, `[data-repo-${kind}]`);
        }
        return;
      }

      const repoCollectionPageButton = event.target.closest("[data-repo-collection-page]");
      if (repoCollectionPageButton && state.selectedRepo) {
        const kind = repoCollectionPageButton.dataset.repoCollectionPage;
        const targetPage = Number(repoCollectionPageButton.dataset.repoCollectionPageTarget);
        if (kind && Number.isFinite(targetPage)) {
          state.repoCollectionPages[kind] = targetPage;
          loadRepoCollection(state.selectedRepo, kind, `[data-repo-${kind}]`);
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

      // Open an agent's detail page — live transcript + prompt (adhoc #259).
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

  document.addEventListener("submit", (event) => {
    const issueForm = event.target.closest("[data-repo-issue-form]");
    if (issueForm && state.selectedRepo) {
      event.preventDefault();
      handleIssueComposeSubmit(state.selectedRepo, issueForm);
      return;
    }
    const discussionReplyForm = event.target.closest("[data-repo-discussion-reply-form]");
    if (discussionReplyForm && state.selectedRepo) {
      event.preventDefault();
      handleDiscussionReplySubmit(state.selectedRepo, discussionReplyForm);
      return;
    }
    const agentPromptForm = event.target.closest("[data-repo-agent-prompt-form]");
    if (agentPromptForm && state.selectedRepo) {
      event.preventDefault();
      handleRepoAgentPromptSubmit(state.selectedRepo, agentPromptForm);
      return;
    }
    const agentNewForm = event.target.closest("[data-repo-agent-new-form]");
    if (agentNewForm && state.selectedRepo) {
      event.preventDefault();
      handleRepoAgentNewSubmit(state.selectedRepo, agentNewForm);
    }
  });

  $("#repoSearch")?.addEventListener("input", () => {
    state.page = 1;
    applyRepositoryFilter();
  });
  $("[data-repo-prev]")?.addEventListener("click", () => {
    state.page -= 1;
    updateRepositoryPagination();
  });
  $("[data-repo-next]")?.addEventListener("click", () => {
    state.page += 1;
    updateRepositoryPagination();
  });

	  $("[data-profile-settings-button]")?.addEventListener("click", (event) => {
	    event.stopPropagation();
	    $("[data-profile-popover]")?.classList.add("hidden");
	    $("[data-profile-toggle]")?.setAttribute("aria-expanded", "false");
	    setProfileHint("", "");
	    showSection("profile");
	    closeMobileDrawers();
	  });
  $("[data-profile-modal-close]")?.addEventListener("click", () => setProfileModalOpen(false));
  $("[data-profile-modal-backdrop]")?.addEventListener("click", () => setProfileModalOpen(false));
  $("[data-profile-save]")?.addEventListener("click", saveProfile);
  $("[data-profile-verify-email]")?.addEventListener("click", resendVerification);
  $("[data-profile-rename-input]")?.addEventListener("input", () => {
    window.clearTimeout(state.nodeNameAvailability.timer);
    state.nodeNameAvailability.timer = window.setTimeout(checkNodeNameAvailability, 250);
  });
  $("[data-profile-page-password]")?.addEventListener("input", updateRenameButton);
  $("[data-profile-delete-confirm]")?.addEventListener("input", () => {
    const button = $("[data-profile-delete-account]");
    if (!button) return;
    button.disabled = ($("[data-profile-delete-confirm]")?.value || "").trim() !== "DELETE";
    button.classList.toggle("opacity-40", button.disabled);
  });

  document.addEventListener("input", (event) => {
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
	    if (event.key === "Escape") {
	      setProfileModalOpen(false);
	      setNotificationDropdownOpen(false);
	      setNotificationModalOpen(false);
	      closeRepoBranchMenus();
	      closeRepoFileFinder();
	      closeMobileDrawers();
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

  window.addEventListener("popstate", () => {
    const requested = requestedRepoKey();
    const repo = requested ? findRepository(requested) : null;
    if (!repo) {
      state.selectedRepo = null;
      // Restore whichever section the URL points at (Back out of a repo into
      // Network/Profile, or forward into one) rather than snapping to repos.
      showSection(requestedSection() || "repos", { push: false });
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
      } else if (kind === "blob" && path) {
        loadRepositoryBlob(repo, path);
      } else {
        loadRepositoryTree(repo, kind === "tree" ? path : "");
      }
      return;
    }
    renderRepoDetail(repo);
  });

  applyDashboardTheme(readDashboardTheme());
  init();
})();
