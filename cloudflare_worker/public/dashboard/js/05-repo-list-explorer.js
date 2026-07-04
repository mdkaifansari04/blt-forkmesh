  function repositoryMatchesQuery(repo, query) {
    if (!query) return true;
    const haystack = [
      repo.owner,
      repo.name,
      repo.description,
      repo.channel,
      repo.source,
    ].join(" ").toLowerCase();
    return haystack.includes(query);
  }

  // A repo is reachable when its own host is live OR — for a public repo — a peer
  // mirroring the same logical repo is online and the relay serves it in place
  // through the repo's own URL (adhoc #61). cloneOnline is the worker's group
  // verdict; fall back to liveHost for older payloads that predate it.
  function repoIsLive(repo) {
    return Boolean(repo?.cloneOnline ?? repo?.liveHost);
  }
  // Live, but the named source of truth is down — a mirror node is serving it.
  function repoServedByMirror(repo) {
    return repoIsLive(repo) && !repo?.liveHost;
  }

  function repositoryCard(group) {
    const origin = sourceOfTruth(group);
    const repo = group.primary;
    const key = repoKey(origin);
    const live = repoIsLive(repo);
    const viaMirror = repoServedByMirror(repo);
    const visibility = origin.isPrivate ? "private" : "public";
    const statusClass = live ? "text-primary" : "text-muted-foreground";
    const nodeCount = group.members.length;
    const liveCount = group.members.reduce((n, m) => n + (repoIsLive(m) ? 1 : 0), 0);
    const statusText = nodeCount > 1
      ? `${liveCount} of ${nodeCount} nodes`
      : (viaMirror ? "via mirror" : live ? "online" : "offline");
    return `
      <div data-repo="${escapeHtml(key.toLowerCase())}" class="repo-card group px-4 sm:px-5 py-5 hover:bg-secondary/40 transition-colors">
        <div class="repo-layout flex flex-col gap-5 lg:grid lg:grid-cols-[minmax(0,1fr)_auto] lg:items-stretch">
          <div class="min-w-0">
            <p class="text-sm font-medium text-foreground truncate">
              <span class="text-muted-foreground">${escapeHtml(origin.owner || "owner")}/</span>${escapeHtml(origin.name || "repository")}
            </p>
            <p class="mt-2 text-sm text-muted-foreground">${escapeHtml(repo.description || origin.description || "No description published.")}</p>
            <div class="mt-4 flex items-center gap-x-5 gap-y-2 flex-wrap">
              <span class="flex items-center gap-1.5 text-xs text-muted-foreground">
                <span class="w-2 h-2 rounded-full bg-primary"></span>${escapeHtml(visibility)}
              </span>
              <span class="flex items-center gap-1 text-xs text-muted-foreground">
                <i data-lucide="radio" class="w-3 h-3"></i>${nodeCount > 1 ? `${nodeCount} nodes` : (viaMirror ? "served by mirror" : live ? "live host" : "host offline")}
              </span>
              <span class="text-xs text-muted-foreground font-mono">updated ${escapeHtml(formatDate(repo.updatedAt || repo.lastSync))}</span>
            </div>
            <button data-dashboard-copy="git clone ${escapeHtml(cloneUrl(origin))}" class="copy-button mt-5 inline-flex items-center gap-2 px-3 py-1.5 rounded-md border border-border bg-secondary text-xs text-foreground hover:bg-secondary/80 transition-colors">
              <i data-lucide="copy" class="copy-icon w-3.5 h-3.5"></i>
              <i data-lucide="check" class="copy-check w-3.5 h-3.5 text-primary"></i>
              Copy clone
            </button>
          </div>
          <div class="flex flex-col justify-between items-start lg:items-end gap-5 lg:min-w-48">
            <span class="flex items-center gap-1 text-[10px] px-2 py-0.5 rounded-full border border-border font-medium font-mono ${statusClass}">
              <i data-lucide="circle" class="w-1.5 h-1.5 fill-current"></i>
              ${statusText}
            </span>
            <button data-dashboard-open-repo="${escapeHtml(key)}" class="browse-repo-button inline-flex items-center gap-1 text-xs font-medium text-foreground transition-colors">
              Browse repository
              <i data-lucide="arrow-right" class="browse-repo-arrow w-3.5 h-3.5"></i>
            </button>
          </div>
        </div>
      </div>
    `;
  }

  function updateRepositoryPagination() {
    const total = state.filteredGroups.length;
    const pages = Math.max(1, Math.ceil(total / state.pageSize));
    state.page = Math.min(Math.max(1, state.page), pages);

    const start = (state.page - 1) * state.pageSize;
    const end = Math.min(start + state.pageSize, total);
    const visible = state.filteredGroups.slice(start, end);
    const list = $("#repoList");
    const summary = $("[data-repo-summary]");
    const prev = $("[data-repo-prev]");
    const next = $("[data-repo-next]");
    const pageList = $("[data-repo-pages]");

    if (list) {
      list.innerHTML = visible.length
        ? visible.map(repositoryCard).join("")
        : '<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">No repositories match this filter.</div>';
    }
    if (summary) {
      summary.textContent = total
        ? `Showing ${start + 1}-${end} of ${total} repositories`
        : "No repositories match this filter";
    }
    if (prev) {
      prev.disabled = state.page <= 1;
      prev.classList.toggle("opacity-40", prev.disabled);
    }
    if (next) {
      next.disabled = state.page >= pages;
      next.classList.toggle("opacity-40", next.disabled);
    }
    if (pageList) {
      pageList.innerHTML = "";
      for (let page = 1; page <= pages; page += 1) {
        const button = document.createElement("button");
        const active = page === state.page;
        button.type = "button";
        button.textContent = String(page);
        button.dataset.dashboardRepoPage = String(page);
        button.className = [
          "h-8 min-w-8 rounded-md border px-2 text-xs font-mono transition-colors",
          active
            ? "border-border bg-secondary text-foreground"
            : "border-transparent text-muted-foreground hover:border-border hover:bg-secondary hover:text-foreground",
        ].join(" ");
        pageList.append(button);
      }
    }

    window.lucide?.createIcons();
  }

  function applyRepositoryFilter() {
    const query = ($("#repoSearch")?.value || "").trim().toLowerCase();
    state.filteredRepositories = state.repositories.filter((repo) =>
      repositoryMatchesQuery(repo, query));
    state.filteredGroups = groupRepositories(state.filteredRepositories);
    updateRepositoryPagination();
  }

  function renderRepositories(repositories, session) {
    state.repositories = Array.isArray(repositories) ? repositories : [];
    state.filteredRepositories = state.repositories.slice();
    state.filteredGroups = groupRepositories(state.repositories);
    state.page = 1;

    const count = $("[data-repo-count]");
    if (count) count.textContent = `${formatCount(state.filteredGroups.length)} mirrored`;

    applyRepositoryFilter();
  }

  function setRepoTab(tab) {
    const detail = $(`[data-dashboard-repo-tab-panel="${tab}"]`)?.closest("[data-repo-detail]") || $("[data-repo-detail]");
    if (!detail) return;
    state.activeRepoTab = tab;
    detail.querySelectorAll("[data-dashboard-repo-tab]").forEach((button) => {
      const active = button.dataset.dashboardRepoTab === tab;
      button.setAttribute("aria-selected", active ? "true" : "false");
      button.classList.toggle("border-primary", active);
      button.classList.toggle("border-transparent", !active);
      button.classList.toggle("text-foreground", active);
      button.classList.toggle("text-muted-foreground", !active);
    });
    detail.querySelectorAll("[data-dashboard-repo-tab-panel]").forEach((panel) => {
      panel.classList.toggle("hidden", panel.dataset.dashboardRepoTabPanel !== tab);
    });
  }

  // Tab switch requested by the user (or a Back/Forward step): shows the tab,
  // mirrors it into the address bar so refresh/back land on the same page,
  // and fetches its records on first view. Issues, pull requests and
  // discussions load lazily here rather than on repo open so a repo with many
  // records doesn't fire record reads for tabs nobody opened.
  function activateRepoTab(tab) {
    setRepoTab(tab);
    // The Agents auto-refresh poll only makes sense while that tab is the one
    // on screen; leaving it (to any other tab) stops the poll.
    if (tab !== "agents") {
      stopRepoAgentsAutoRefresh();
      // Leaving the tab closes any open agent detail page (adhoc #259) so
      // returning to Agents lands on the session list, not a stale transcript.
      state.agentsView.selectedAgentId = null;
    }
    if (!state.selectedRepo) return;
    navigateHistory(tab === "code"
      ? (state.repoCodeUrl || repoPathUrl(state.selectedRepo))
      : `${repoPathUrl(state.selectedRepo)}/${tab}`);
    if (["issues", "pulls", "discussions", "releases", "agents"].includes(tab) && !state.loadedRepoTabs?.[tab]) {
      if (!state.loadedRepoTabs) state.loadedRepoTabs = {};
      state.loadedRepoTabs[tab] = true;
      if (tab === "issues") loadRepoIssues(state.selectedRepo);
      else if (tab === "releases") loadRepoReleases(state.selectedRepo);
      else if (tab === "agents") loadRepoAgents(state.selectedRepo);
      else loadRepoCollection(state.selectedRepo, tab, `[data-repo-${tab}]`);
    } else if (tab === "issues") {
      // Re-selecting the tab should return to the issues list even if the
      // new-issue compose form was left open.
      renderRepoIssues();
    } else if (tab === "agents") {
      // Resume polling instead of re-fetching immediately if already loaded.
      startRepoAgentsAutoRefresh(state.selectedRepo);
    }
  }

  function repoPathParts(path) {
    return String(path || "").split("/").filter(Boolean);
  }

  function repoChildPath(path, name) {
    const child = String(name || "");
    return path ? `${path}/${child}` : child;
  }

  function renderRepoBreadcrumb(repo, path, terminalKind = "tree") {
    const detail = $("[data-repo-detail]");
    const crumb = detail?.querySelector("[data-repo-breadcrumb]");
    if (!crumb) return;

    const parts = repoPathParts(path);
    let acc = "";
    const rootLabel = repo.name || "repository";
    crumb.innerHTML = [`<button type="button" data-dashboard-tree-path="" class="font-semibold text-primary hover:underline">${escapeHtml(rootLabel)}</button>`]
      .concat(parts.map((part, index) => {
        acc = acc ? `${acc}/${part}` : part;
        const isBlobTerminal = terminalKind === "blob" && index === parts.length - 1;
        const pathAttribute = isBlobTerminal ? "data-dashboard-blob-path" : "data-dashboard-tree-path";
        return `<span class="text-muted-foreground">/</span> <button type="button" ${pathAttribute}="${escapeHtml(acc)}" class="${isBlobTerminal ? "text-foreground" : "text-primary hover:underline"}">${escapeHtml(part)}</button>`;
      })).join(" ");
  }

  function renderRepoServedBy(node, tookMs) {
    const name = String(node || "").trim();
    state.repoServedBy = name ? { name, tookMs: Number(tookMs) || 0 } : null;
    const badge = $("[data-repo-detail]")?.querySelector("[data-repo-served-by]");
    if (badge) {
      if (!name) {
        badge.hidden = true;
        badge.textContent = "";
      } else {
        // Confirms the page loaded from a live mirror and which one (the
        // router round-robins browse traffic across every online mirror of
        // the repo).
        const speed = formatServeSpeed(tookMs);
        badge.textContent = speed ? `served by ${name} - ${speed}` : `served by ${name}`;
        badge.hidden = false;
      }
    }
    // Re-render the repo's mirror lists so the node that just answered gets
    // its green "serving this request" highlight without waiting on a fresh
    // /mirrors fetch.
    renderRepoMirrorLists(state.repoMirrors, state.repoServedBy);
  }

  function repoExplorerRowClass(active = false) {
    return [
      "flex w-full min-w-0 items-center gap-2 border-l-2 px-3 py-2 text-left text-xs transition-colors",
      active ? "border-primary bg-secondary text-foreground" : "border-transparent text-muted-foreground hover:bg-secondary/50 hover:text-foreground",
    ].join(" ");
  }

  function renderRepoExplorer(repo, path, entries) {
    const detail = $("[data-repo-detail]");
    const explorer = detail?.querySelector("[data-repo-explorer-tree]");
    if (!explorer) return;

    const parts = repoPathParts(path);
    const parentPath = parts.slice(0, -1).join("/");
    const currentName = parts[parts.length - 1] || repo.name || "repository";
    const rows = [
      `<button type="button" data-dashboard-tree-path="" class="${repoExplorerRowClass(!path)}"><i data-lucide="book-marked" class="h-3.5 w-3.5 shrink-0 text-muted-foreground"></i><span class="min-w-0 truncate">${escapeHtml(repo.name || "repository")}</span></button>`,
    ];

    if (path) {
      rows.push(`<button type="button" data-dashboard-tree-path="${escapeHtml(parentPath)}" class="${repoExplorerRowClass(false)}"><i data-lucide="corner-up-left" class="h-3.5 w-3.5 shrink-0 text-muted-foreground"></i><span class="min-w-0 truncate">..</span></button>`);
      rows.push(`<div data-repo-explorer-current-path="${escapeHtml(path)}" class="${repoExplorerRowClass(true)}"><i data-lucide="folder-open" class="h-3.5 w-3.5 shrink-0 text-muted-foreground"></i><span class="min-w-0 truncate">${escapeHtml(currentName)}</span></div>`);
    }

    if (entries.length) {
      rows.push(entries.map((entry) => {
        const isTree = entry.type === "tree";
        const childPath = repoChildPath(path, entry.name);
        return `<button type="button" data-repo-explorer-entry data-dashboard-${isTree ? "tree" : "blob"}-path="${escapeHtml(childPath)}" class="${repoExplorerRowClass(false)}"><i data-lucide="${isTree ? "folder" : "file"}" class="h-3.5 w-3.5 shrink-0 text-muted-foreground"></i><span class="min-w-0 truncate">${escapeHtml(entry.name || "entry")}</span></button>`;
      }).join(""));
    } else {
      rows.push('<div class="px-3 py-2 text-xs text-muted-foreground">No files in this folder.</div>');
    }

    explorer.innerHTML = rows.join("");
  }

  function setRepoExplorerSelection(path, kind = "tree") {
    const detail = $("[data-repo-detail]");
    detail?.querySelectorAll("[data-repo-explorer-tree] [data-dashboard-tree-path], [data-repo-explorer-tree] [data-dashboard-blob-path]").forEach((button) => {
      const buttonPath = button.dataset.dashboardTreePath ?? button.dataset.dashboardBlobPath ?? "";
      const activeKind = button.hasAttribute("data-dashboard-blob-path") ? "blob" : "tree";
      const active = buttonPath === path && activeKind === kind;
      button.classList.toggle("border-primary", active);
      button.classList.toggle("border-transparent", !active);
      button.classList.toggle("bg-secondary", active);
      button.classList.toggle("text-foreground", active);
      button.classList.toggle("text-muted-foreground", !active);
    });
  }

  function setRepoExplorerFocusMode(active) {
    const detail = $("[data-repo-detail]");
    const contentGrid = detail?.querySelector("[data-repo-content-grid]");
    const workspace = detail?.querySelector("[data-repo-code-workspace]");
    const explorer = detail?.querySelector("[data-repo-code-explorer]");
    const about = detail?.querySelector("[data-repo-about]");
    const rootToolbar = detail?.querySelector("[data-repo-root-toolbar]");
    const focusActions = detail?.querySelector("[data-repo-focus-actions]");
    if (!detail) return;

    contentGrid?.classList.toggle("xl:grid-cols-[minmax(0,1fr)_18rem]", !active);
    contentGrid?.classList.toggle("xl:grid-cols-1", active);
    workspace?.classList.toggle("grid", active);
    workspace?.classList.toggle("lg:grid-cols-[13rem_minmax(0,1fr)]", active);
    workspace?.classList.toggle("xl:grid-cols-[14rem_minmax(0,1fr)]", active);
    workspace?.classList.toggle("2xl:grid-cols-[16rem_minmax(0,1fr)]", active);
    explorer?.classList.toggle("hidden", !active);
    about?.classList.toggle("hidden", active);
    rootToolbar?.classList.toggle("hidden", active);
    focusActions?.classList.toggle("hidden", !active);
  }

  function fileFinderRepoKey(repo) {
    return repo ? `${repoKey(repo)}@${repoSelectedBranch(repo)}` : "";
  }

  function resetRepoFileFinder(repo) {
    state.repoFileFinder = {
      repoKey: fileFinderRepoKey(repo),
      files: [],
      indexed: false,
      indexing: false,
      partial: false,
      error: "",
      selectedIndex: 0,
    };
  }

  function fileFinderOpen() {
    const finder = $("[data-repo-file-finder]");
    return Boolean(finder && !finder.classList.contains("hidden"));
  }

  async function buildRepoFileIndex(repo) {
    const key = fileFinderRepoKey(repo);
    if (!repo) return [];
    if (state.repoFileFinder.repoKey !== key) resetRepoFileFinder(repo);
    if (state.repoFileFinder.indexed || state.repoFileFinder.indexing) return state.repoFileFinder.files;

    state.repoFileFinder.indexing = true;
    state.repoFileFinder.error = "";
    renderRepoFileFinderResults($("[data-repo-file-finder-input]")?.value || "");
    const files = [];
    const queue = [""];
    const started = Date.now();
    try {
      while (queue.length && files.length < MAX_REPO_FILE_FINDER_RESULTS && ((Date.now() - started) / 1000) < MAX_REPO_FILE_FINDER_SECONDS) {
        const path = queue.shift();
        const data = await fetchJson(repoLiveUrl(repo, "tree", { path }));
        const entries = Array.isArray(data.entries) ? data.entries.slice() : [];
        entries.sort((a, b) => {
          if (a.type !== b.type) return a.type === "tree" ? -1 : 1;
          return String(a.name || "").localeCompare(String(b.name || ""));
        });
        entries.forEach((entry) => {
          const childPath = repoChildPath(path, entry.name);
          if (entry.type === "tree") {
            queue.push(childPath);
          } else if (entry.type === "blob" && files.length < MAX_REPO_FILE_FINDER_RESULTS) {
            files.push(childPath);
          }
        });
      }
      state.repoFileFinder.files = files;
      state.repoFileFinder.partial = queue.length > 0 || files.length >= MAX_REPO_FILE_FINDER_RESULTS;
      state.repoFileFinder.indexed = true;
      return files;
    } catch (error) {
      state.repoFileFinder.error = error?.message || "Could not index files from the live mirror.";
      state.repoFileFinder.files = files;
      return files;
    } finally {
      state.repoFileFinder.indexing = false;
      renderRepoFileFinderResults($("[data-repo-file-finder-input]")?.value || "");
    }
  }

  function renderRepoFileFinderResults(query = "") {
    const finder = $("[data-repo-file-finder]");
    if (!finder) return;
    const results = finder.querySelector("[data-repo-file-finder-results]");
    const status = finder.querySelector("[data-repo-file-finder-status]");
    if (!results || !status) return;

    const needle = String(query || "").trim().toLowerCase();
    const matches = state.repoFileFinder.files
      .filter((path) => !needle || path.toLowerCase().includes(needle))
      .slice(0, 50);
    state.repoFileFinder.selectedIndex = Math.min(state.repoFileFinder.selectedIndex, Math.max(matches.length - 1, 0));

    if (state.repoFileFinder.indexing) {
      status.textContent = "Indexing live mirror...";
    } else if (state.repoFileFinder.error && !state.repoFileFinder.files.length) {
      status.textContent = "No live desktop host is available to index files.";
    } else if (state.repoFileFinder.partial) {
      status.textContent = `Showing ${formatCount(matches.length)} matches from the first ${formatCount(state.repoFileFinder.files.length)} indexed files.`;
    } else {
      status.textContent = `${formatCount(matches.length)} matching file${matches.length === 1 ? "" : "s"}.`;
    }

    if (!matches.length) {
      results.innerHTML = `<div class="px-3 py-6 text-center text-sm text-muted-foreground">${state.repoFileFinder.indexing ? "Still indexing files..." : "No files match your search."}</div>`;
      return;
    }
    results.innerHTML = matches.map((path, index) => `
      <button type="button" data-repo-file-finder-result data-repo-file-finder-path="${escapeHtml(path)}" aria-selected="${index === state.repoFileFinder.selectedIndex ? "true" : "false"}" class="grid w-full grid-cols-[1.25rem_minmax(0,1fr)] items-center gap-2 rounded-md px-3 py-2 text-left text-sm transition-colors ${index === state.repoFileFinder.selectedIndex ? "bg-secondary text-foreground" : "text-muted-foreground hover:bg-secondary/60 hover:text-foreground"}">
        <i data-lucide="file" class="h-3.5 w-3.5 text-muted-foreground"></i>
        <span class="min-w-0 truncate font-mono">${escapeHtml(path)}</span>
      </button>`).join("");
    window.lucide?.createIcons();
  }

  function moveRepoFileFinderSelection(delta) {
    const results = $$('[data-repo-file-finder-result]');
    if (!results.length) return;
    state.repoFileFinder.selectedIndex = (state.repoFileFinder.selectedIndex + delta + results.length) % results.length;
    results.forEach((button, index) => {
      const active = index === state.repoFileFinder.selectedIndex;
      button.setAttribute("aria-selected", active ? "true" : "false");
      button.classList.toggle("bg-secondary", active);
      button.classList.toggle("text-foreground", active);
      button.classList.toggle("text-muted-foreground", !active);
      if (active) button.scrollIntoView({ block: "nearest" });
    });
  }

  function selectRepoFileFinderResult() {
    const selected = $('[data-repo-file-finder-result][aria-selected="true"]') || $('[data-repo-file-finder-result]');
    if (!selected || !state.selectedRepo) return;
    closeRepoFileFinder();
    loadRepositoryBlob(state.selectedRepo, selected.dataset.repoFileFinderPath || "");
  }

  function closeRepoFileFinder() {
    const finder = $("[data-repo-file-finder]");
    if (!finder) return;
    finder.classList.add("hidden");
    finder.classList.remove("flex");
  }

  async function openRepoFileFinder() {
    if (!state.selectedRepo) return;
    const finder = $("[data-repo-file-finder]");
    if (!finder) return;
    if (state.repoFileFinder.repoKey !== fileFinderRepoKey(state.selectedRepo)) resetRepoFileFinder(state.selectedRepo);
    finder.classList.remove("hidden");
    finder.classList.add("flex");
    const input = finder.querySelector("[data-repo-file-finder-input]");
    if (input) input.value = "";
    state.repoFileFinder.selectedIndex = 0;
    renderRepoFileFinderResults("");
    window.setTimeout(() => input?.focus(), 0);
    buildRepoFileIndex(state.selectedRepo);
  }

  function repoLanguage(path) {
    const file = String(path || "").toLowerCase();
    if (file.endsWith(".css") || file.endsWith(".scss")) return "css";
    if (file.endsWith(".ts") || file.endsWith(".tsx") || file.endsWith(".js") || file.endsWith(".jsx") || file.endsWith(".mjs") || file.endsWith(".cjs")) return "javascript";
    if (file.endsWith(".json") || file.endsWith(".jsonc")) return "json";
    if (file.endsWith(".html") || file.endsWith(".xml") || file.endsWith(".svg")) return "markup";
    return "text";
  }

  function highlightWithRules(line, rules) {
    let index = 0;
    let html = "";
    const source = String(line ?? "");
    while (index < source.length) {
      const chunk = source.slice(index);
      const rule = rules.find((candidate) => candidate.pattern.test(chunk));
      if (rule) {
        rule.pattern.lastIndex = 0;
        const match = chunk.match(rule.pattern)?.[0] || "";
        html += `<span class="${rule.className}">${escapeHtml(match)}</span>`;
        index += match.length;
      } else {
        html += escapeHtml(source[index]);
        index += 1;
      }
    }
    return html || " ";
  }

  function highlightCodeLine(line, path) {
    const language = repoLanguage(path);
    const shared = [
      { pattern: /^\/\/.*/, className: "text-zinc-500" },
      { pattern: /^\/\*.*?\*\//, className: "text-zinc-500" },
      { pattern: /^"(?:\\.|[^"\\])*"/, className: "text-emerald-300" },
      { pattern: /^'(?:\\.|[^'\\])*'/, className: "text-emerald-300" },
      { pattern: /^`(?:\\.|[^`\\])*`/, className: "text-emerald-300" },
      { pattern: /^\b\d+(?:\.\d+)?(?:px|rem|em|vh|vw|%|ms|s)?\b/, className: "text-amber-300" },
    ];

    if (language === "css") {
      return highlightWithRules(line, [
        { pattern: /^\/\*.*?\*\//, className: "text-zinc-500" },
        { pattern: /^"(?:\\.|[^"\\])*"/, className: "text-emerald-300" },
        { pattern: /^'(?:\\.|[^'\\])*'/, className: "text-emerald-300" },
        { pattern: /^@[a-zA-Z-]+/, className: "text-sky-300" },
        { pattern: /^--[a-zA-Z0-9-_]+(?=\s*:)?/, className: "text-blue-300" },
        { pattern: /^[a-zA-Z-]+(?=\s*:)/, className: "text-blue-300" },
        { pattern: /^#[0-9a-fA-F]{3,8}\b/, className: "text-amber-300" },
        { pattern: /^\b\d+(?:\.\d+)?(?:px|rem|em|vh|vw|%|ms|s)?\b/, className: "text-amber-300" },
        { pattern: /^[a-zA-Z-]+(?=\()/, className: "text-purple-300" },
      ]);
    }

    if (language === "json") {
      return highlightWithRules(line, [
        { pattern: /^"(?:\\.|[^"\\])*"(?=\s*:)/, className: "text-blue-300" },
        { pattern: /^"(?:\\.|[^"\\])*"/, className: "text-emerald-300" },
        { pattern: /^-?\b\d+(?:\.\d+)?\b/, className: "text-amber-300" },
        { pattern: /^(?:true|false|null)\b/, className: "text-sky-300" },
      ]);
    }

    if (language === "markup") {
      return highlightWithRules(line, [
        { pattern: /^<!--.*?-->/, className: "text-zinc-500" },
        { pattern: /^<\/?[a-zA-Z][a-zA-Z0-9:-]*/, className: "text-sky-300" },
        { pattern: /^\s+[a-zA-Z_:][-a-zA-Z0-9_:.]*(?==)/, className: "text-purple-300" },
        { pattern: /^"(?:\\.|[^"\\])*"/, className: "text-emerald-300" },
        { pattern: /^'(?:\\.|[^'\\])*'/, className: "text-emerald-300" },
      ]);
    }

    if (language === "javascript") {
      return highlightWithRules(line, [
        ...shared,
        { pattern: /^@[a-zA-Z_$][\w$]*/, className: "text-amber-300" },
        { pattern: /^(?:import|from|export|const|let|var|function|return|if|else|for|while|await|async|class|extends|interface|type|new|try|catch|throw|switch|case|break|continue|default|true|false|null|undefined)\b/, className: "text-sky-300" },
        { pattern: /^[A-Z][a-zA-Z0-9_$]*(?=\b)/, className: "text-cyan-300" },
        { pattern: /^[a-zA-Z_$][\w$]*(?=\()/, className: "text-purple-300" },
      ]);
    }

    return escapeHtml(line) || " ";
  }

  function repoFileName(path) {
    const name = repoPathParts(path).pop() || "file.txt";
    return name.replace(/[\\/:*?"<>|]+/g, "-") || "file.txt";
  }

  function repoPathExtension(path) {
    const name = String(repoFileName(path) || "").toLowerCase();
    const dot = name.lastIndexOf(".");
    return dot >= 0 ? name.slice(dot + 1) : "";
  }

  function repoPreviewKindFromPath(path) {
    const extension = repoPathExtension(path);
    const previews = {
      apng: ["image", "image/png", "Image"],
      avif: ["image", "image/avif", "Image"],
      bmp: ["image", "image/bmp", "Image"],
      gif: ["image", "image/gif", "Image"],
      ico: ["image", "image/x-icon", "Image"],
      jfif: ["image", "image/jpeg", "Image"],
      jpeg: ["image", "image/jpeg", "Image"],
      jpg: ["image", "image/jpeg", "Image"],
      png: ["image", "image/png", "Image"],
      svg: ["image", "image/svg+xml", "Image"],
      tif: ["image", "image/tiff", "Image"],
      tiff: ["image", "image/tiff", "Image"],
      webp: ["image", "image/webp", "Image"],
      "3g2": ["video", "video/3gpp2", "Video"],
      "3gp": ["video", "video/3gpp", "Video"],
      avi: ["video", "video/x-msvideo", "Video"],
      m4v: ["video", "video/mp4", "Video"],
      mkv: ["video", "video/x-matroska", "Video"],
      mov: ["video", "video/quicktime", "Video"],
      mp4: ["video", "video/mp4", "Video"],
      mpeg: ["video", "video/mpeg", "Video"],
      mpg: ["video", "video/mpeg", "Video"],
      ogv: ["video", "video/ogg", "Video"],
      webm: ["video", "video/webm", "Video"],
      aac: ["audio", "audio/aac", "Audio"],
      flac: ["audio", "audio/flac", "Audio"],
      m4a: ["audio", "audio/mp4", "Audio"],
      mid: ["audio", "audio/midi", "Audio"],
      midi: ["audio", "audio/midi", "Audio"],
      mp3: ["audio", "audio/mpeg", "Audio"],
      oga: ["audio", "audio/ogg", "Audio"],
      ogg: ["audio", "audio/ogg", "Audio"],
      opus: ["audio", "audio/ogg", "Audio"],
      wav: ["audio", "audio/wav", "Audio"],
      weba: ["audio", "audio/webm", "Audio"],
      pdf: ["pdf", "application/pdf", "PDF"],
      csv: ["csv", "text/csv", "CSV"],
      tab: ["csv", "text/tab-separated-values", "TSV"],
      tsv: ["csv", "text/tab-separated-values", "TSV"],
    };
    const preview = previews[extension];
    if (!preview) return null;
    return { kind: preview[0], mime: preview[1], label: preview[2] };
  }

  function repoRawUrl(repo, path) {
    return repoLiveUrl(repo, "raw", { path });
  }

  function repoLiveUrl(repo, action, params = {}) {
    const query = new URLSearchParams();
    Object.entries(params || {}).forEach(([key, value]) => {
      query.set(key, String(value ?? ""));
    });
    const current = new URLSearchParams(location.search || "");
    ["viewer", "ts", "sig"].forEach((key) => {
      const value = current.get(key);
      if (value) query.set(key, value);
    });
    query.set("ref", repoSelectedBranch(repo));
    return `${repoApiBase(repo)}/${action}?${query.toString()}`;
  }

  function repoPreviewMeta(data, repo, path) {
    const preview = repoPreviewKindFromPath(path);
    const encoding = String(data?.encoding || "").toLowerCase();
    const content = String(data?.content ?? data?.text ?? "");
    const baseSize = encoding === "base64"
      ? Math.ceil((content.replace(/\s/g, "").length * 3) / 4)
      : content.length;
    if (!preview && encoding !== "base64") return null;
    if (preview?.kind === "csv" && encoding === "base64") {
      return null;
    }
    return {
      path,
      rawUrl: repoRawUrl(repo, path),
      kind: preview?.kind || "binary",
      mime: preview?.mime || "application/octet-stream",
      label: preview?.label || "Binary",
      content,
      size: Number(data?.size) || baseSize,
      truncated: Boolean(data?.truncated),
    };
  }

  function repoFileModeButtonClass(active) {
    return [
      "rounded px-3 py-1 text-xs font-medium transition-colors",
      active ? "bg-secondary text-foreground" : "text-muted-foreground hover:text-foreground",
    ].join(" ");
  }

  async function copyTextToClipboard(text) {
    const value = String(text || "");
    try {
      if (!navigator.clipboard?.writeText) throw new Error("clipboard unavailable");
      await navigator.clipboard.writeText(value);
      return true;
    } catch (_) {
      const temp = document.createElement("textarea");
      temp.value = value;
      temp.setAttribute("readonly", "");
      temp.style.position = "fixed";
      temp.style.opacity = "0";
      document.body.appendChild(temp);
      try {
        temp.focus();
        temp.select();
        return document.execCommand?.("copy") === true;
      } catch (_) {
        return false;
      } finally {
        temp.remove();
      }
    }
  }

  function downloadRepoFile(path, content) {
    const blob = new Blob([String(content || "")], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = repoFileName(path);
    link.rel = "noopener";
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 30000);
  }

  function downloadRepoRawBlob(meta) {
    const link = document.createElement("a");
    link.href = meta.rawUrl;
    link.download = repoFileName(meta.path);
    link.rel = "noopener";
    document.body.appendChild(link);
    link.click();
    link.remove();
  }

  function repoFileStatusForMode(mode) {
    if (mode === "blame") return "Blame view - current mirror commit shown for each line.";
    if (mode === "raw") return "Raw view - exact file contents.";
    return "Code view - highlighted source.";
  }

  function updateRepoFileStatus(viewer, message, tone = "muted") {
    const status = viewer?.querySelector("[data-repo-file-status]");
    if (!status) return;
    status.textContent = message;
    status.classList.toggle("text-primary", tone === "good");
    status.classList.toggle("text-red-400", tone === "bad");
    status.classList.toggle("text-muted-foreground", tone !== "good" && tone !== "bad");
  }

  function renderRepoFileRows(lines, path, mode, meta) {
    const content = String(meta?.content ?? lines.join("\n"));
    if (mode === "raw") {
      return `<pre data-repo-file-raw class="max-h-[34rem] overflow-auto whitespace-pre p-4 font-mono text-xs leading-5 text-zinc-200">${escapeHtml(content)}</pre>`;
    }

    const blame = mode === "blame";
    const commit = String(meta?.commitId || "live").slice(0, 7);
    const author = String(meta?.author || meta?.owner || "mirror");
    const blameLabel = `${commit} ${author}`;
    const grid = blame
      ? "grid-cols-[10rem_3.25rem_minmax(32rem,1fr)]"
      : "grid-cols-[3.25rem_minmax(32rem,1fr)]";
    return `<div data-repo-file-code class="max-h-[34rem] overflow-auto py-3 text-xs leading-5">
      ${lines.map((line, index) => `
        <div class="grid min-w-max ${grid}">
          ${blame ? `<span class="select-none truncate px-3 font-mono text-muted-foreground" title="${escapeHtml(blameLabel)}">${escapeHtml(blameLabel)}</span>` : ""}
          <span class="select-none px-3 text-right font-mono text-muted-foreground">${index + 1}</span>
          <span class="whitespace-pre px-4 font-mono text-zinc-200">${highlightCodeLine(line, path)}</span>
        </div>`).join("")}
    </div>`;
  }

  function renderRepoMediaPreview(meta) {
    const name = escapeHtml(repoFileName(meta.path));
    const mime = escapeHtml(meta.mime);
    const rawUrl = escapeHtml(meta.rawUrl);
    let media = "";
    if (meta.kind === "image") {
      media = `<img data-repo-image class="max-h-[34rem] max-w-full rounded-sm object-contain" src="${rawUrl}" alt="${name}" />`;
    } else if (meta.kind === "video") {
      media = `<video data-repo-video controls preload="metadata" class="max-h-[34rem] w-full max-w-5xl rounded-md bg-black"><source src="${rawUrl}" type="${mime}" />This browser cannot play this video.</video>`;
    } else if (meta.kind === "audio") {
      media = `<audio data-repo-audio controls preload="metadata" class="w-full max-w-3xl"><source src="${rawUrl}" type="${mime}" />This browser cannot play this audio.</audio>`;
    } else if (meta.kind === "pdf") {
      media = `<iframe data-repo-pdf title="${name}" src="${rawUrl}" class="h-[34rem] w-full rounded-md border border-border bg-background"></iframe>`;
    }
    return `
      <figure data-repo-media-preview class="grid gap-3 p-4">
        <div class="flex min-h-[18rem] items-center justify-center rounded-md border border-border bg-secondary/20 p-3">
          ${media}
        </div>
        <figcaption class="flex min-w-0 flex-wrap items-center gap-x-3 gap-y-1 text-xs text-muted-foreground">
          <span class="min-w-0 truncate font-mono text-foreground">${name}</span>
          <span class="font-mono">${mime}</span>
          ${meta.size ? `<span class="font-mono">${escapeHtml(formatSize(meta.size))}</span>` : ""}
          ${meta.truncated ? '<span class="font-mono text-amber-300">metadata truncated</span>' : ""}
        </figcaption>
      </figure>`;
  }

  function parseDelimitedPreviewRows(content, delimiter) {
    const rows = [];
    let row = [];
    let field = "";
    let quoted = false;
    const text = String(content || "").replace(/\r\n/g, "\n").replace(/\r/g, "\n");
    for (let index = 0; index < text.length && rows.length < 51; index += 1) {
      const char = text[index];
      if (quoted) {
        if (char === '"' && text[index + 1] === '"') {
          field += '"';
          index += 1;
        } else if (char === '"') {
          quoted = false;
        } else {
          field += char;
        }
      } else if (char === '"') {
        quoted = true;
      } else if (char === delimiter) {
        row.push(field);
        field = "";
      } else if (char === "\n") {
        row.push(field);
        rows.push(row);
        row = [];
        field = "";
      } else {
        field += char;
      }
    }
    if (field || row.length || text.endsWith(delimiter)) {
      row.push(field);
      rows.push(row);
    }
    return rows.filter((items) => items.some((item) => String(item).length));
  }

  function renderRepoCsvPreview(meta) {
    const delimiter = meta.mime === "text/tab-separated-values" ? "\t" : ",";
    const rows = parseDelimitedPreviewRows(meta.content, delimiter);
    const visibleRows = rows.slice(0, 50);
    const columnCount = Math.min(20, Math.max(1, ...visibleRows.map((row) => row.length)));
    const headers = visibleRows[0] || [];
    const body = visibleRows.slice(1);
    const cellClass = "max-w-[18rem] truncate border-t border-border px-3 py-2 text-left align-top";
    return `
      <div data-repo-csv-preview class="max-h-[34rem] overflow-auto p-4">
        <table class="min-w-full border-separate border-spacing-0 text-xs">
          <thead class="sticky top-0 z-10 bg-background text-muted-foreground">
            <tr>${Array.from({ length: columnCount }, (_, index) => `<th class="${cellClass} font-mono">${escapeHtml(headers[index] || `Column ${index + 1}`)}</th>`).join("")}</tr>
          </thead>
          <tbody class="text-zinc-200">
            ${body.length ? body.map((row) => `<tr>${Array.from({ length: columnCount }, (_, index) => `<td class="${cellClass} font-mono">${escapeHtml(row[index] || "")}</td>`).join("")}</tr>`).join("") : `<tr><td class="${cellClass} font-mono" colspan="${columnCount}">${rows.length ? "No data rows." : "No previewable rows."}</td></tr>`}
          </tbody>
        </table>
      </div>`;
  }

  function renderRepoUnsupportedBinary(meta) {
    return `
      <div data-repo-binary-preview class="grid gap-4 p-6">
        <div class="flex min-w-0 items-center gap-3 rounded-md border border-border bg-secondary/20 p-4">
          <i data-lucide="file-archive" class="h-5 w-5 shrink-0 text-muted-foreground"></i>
          <div class="min-w-0">
            <div class="truncate font-mono text-sm text-foreground">${escapeHtml(repoFileName(meta.path))}</div>
            <div class="mt-1 flex flex-wrap gap-x-3 gap-y-1 text-xs text-muted-foreground">
              <span class="font-mono">${escapeHtml(meta.mime)}</span>
              ${meta.size ? `<span class="font-mono">${escapeHtml(formatSize(meta.size))}</span>` : ""}
              <span>Preview unavailable in browser.</span>
            </div>
          </div>
        </div>
        <div class="flex flex-wrap items-center gap-2">
          <a href="${escapeHtml(meta.rawUrl)}" target="_blank" rel="noopener" class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary transition-colors"><i data-lucide="external-link" class="h-3.5 w-3.5"></i>Raw</a>
          <a href="${escapeHtml(meta.rawUrl)}" download="${escapeHtml(repoFileName(meta.path))}" rel="noopener" class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary transition-colors"><i data-lucide="download" class="h-3.5 w-3.5"></i>Download</a>
        </div>
      </div>`;
  }

  function bindRepoPreviewToolbar(viewer, meta) {
    viewer._repoFileToolbarAbort?.abort();
    const controller = new AbortController();
    viewer._repoFileToolbarAbort = controller;
    viewer.addEventListener("click", (event) => {
      const actionButton = event.target.closest("button[data-repo-preview-action]");
      if (!actionButton || !viewer.contains(actionButton)) return;
      const action = actionButton.dataset.repoPreviewAction;
      if (action === "download") {
        downloadRepoRawBlob(meta);
        updateRepoFileStatus(viewer, `Downloading ${repoFileName(meta.path)}.`, "good");
      } else if (action === "fullscreen") {
        toggleRepoFileFullscreen(viewer);
      }
    }, { signal: controller.signal });

    document.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      setRepoFileFullscreenFallback(viewer.querySelector("[data-repo-file-shell]"), false);
    }, { signal: controller.signal });
  }

  function renderRepoPreview(viewer, repo, path, data) {
    const meta = repoPreviewMeta(data, repo, path);
    if (!meta) return false;
    const content = meta.kind === "csv"
      ? renderRepoCsvPreview(meta)
      : ["image", "video", "audio", "pdf"].includes(meta.kind)
        ? renderRepoMediaPreview(meta)
        : renderRepoUnsupportedBinary(meta);
    const status = meta.kind === "binary"
      ? "Binary file - use raw or download."
      : `${meta.label} preview.`;
    viewer.innerHTML = `
      <div data-repo-file-shell class="overflow-hidden rounded-lg border border-border bg-background">
        <div data-repo-file-toolbar class="flex min-h-11 flex-wrap items-center justify-between gap-2 border-b border-border bg-secondary/40 px-3 py-2">
          <div class="flex min-w-0 flex-wrap items-center gap-2">
            <span class="font-mono text-[10px] text-muted-foreground">${escapeHtml(meta.label)}</span>
            ${meta.size ? `<span class="font-mono text-[10px] text-muted-foreground">${escapeHtml(formatSize(meta.size))}</span>` : ""}
            <span class="font-mono text-[10px] text-muted-foreground">${escapeHtml(meta.mime)}</span>
            <span data-repo-file-status class="min-w-0 font-mono text-[10px] text-muted-foreground">${escapeHtml(status)}</span>
          </div>
          <div class="flex items-center gap-1.5">
            <a href="${escapeHtml(meta.rawUrl)}" target="_blank" rel="noopener" aria-label="Open raw file" class="inline-flex h-7 w-7 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="external-link" class="h-3.5 w-3.5"></i></a>
            <button type="button" data-repo-preview-action="download" aria-label="Download file" class="inline-flex h-7 w-7 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="download" class="h-3.5 w-3.5"></i></button>
            <button type="button" data-repo-preview-action="fullscreen" aria-label="Open full screen" class="inline-flex h-7 w-7 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="maximize-2" class="h-3.5 w-3.5"></i></button>
          </div>
        </div>
        <div data-repo-file-content>${content}</div>
      </div>`;
    bindRepoPreviewToolbar(viewer, meta);
    navigateHistory(repoPathUrl(repo, "blob", path));
    window.lucide?.createIcons();
    return true;
  }

  function setRepoFileMode(viewer, mode, lines, path, meta) {
    const content = viewer?.querySelector("[data-repo-file-content]");
    if (!content) return;
    content.innerHTML = renderRepoFileRows(lines, path, mode, meta);
    viewer.dataset.repoFileCurrentMode = mode;
    delete viewer.dataset.repoFileMode;
    viewer.querySelectorAll("button[data-repo-file-mode]").forEach((button) => {
      const active = button.dataset.repoFileMode === mode;
      button.className = repoFileModeButtonClass(active);
      button.setAttribute("aria-pressed", active ? "true" : "false");
    });
    updateRepoFileStatus(viewer, repoFileStatusForMode(mode));
    window.lucide?.createIcons();
  }

  function setRepoFileFullscreenFallback(shell, active) {
    if (!shell) return;
    shell.classList.toggle("fixed", active);
    shell.classList.toggle("inset-3", active);
    shell.classList.toggle("z-50", active);
    shell.classList.toggle("shadow-2xl", active);
    shell.classList.toggle("max-h-[calc(100vh-1.5rem)]", active);
    shell.classList.toggle("overflow-auto", active);
    document.body.classList.toggle("overflow-hidden", active);
    if (active) {
      shell.dataset.repoFileFullscreen = "fallback";
    } else {
      delete shell.dataset.repoFileFullscreen;
    }
  }

  function toggleRepoFileFullscreen(viewer) {
    const shell = viewer?.querySelector("[data-repo-file-shell]");
    if (!shell) return;
    if (shell.dataset.repoFileFullscreen === "fallback") {
      setRepoFileFullscreenFallback(shell, false);
      return;
    }
    if (document.fullscreenElement === shell) {
      document.exitFullscreen?.();
      return;
    }
    if (shell.requestFullscreen) {
      shell.requestFullscreen().catch(() => setRepoFileFullscreenFallback(shell, true));
      return;
    }
    setRepoFileFullscreenFallback(shell, true);
  }

  function bindRepoFileToolbar(viewer, repo, path, content, lines, meta) {
    viewer._repoFileToolbarAbort?.abort();
    const controller = new AbortController();
    viewer._repoFileToolbarAbort = controller;
    viewer.addEventListener("click", async (event) => {
      const modeButton = event.target.closest("button[data-repo-file-mode]");
      if (modeButton && viewer.contains(modeButton)) {
        setRepoFileMode(viewer, modeButton.dataset.repoFileMode || "code", lines, path, meta);
        return;
      }

      const actionButton = event.target.closest("button[data-repo-file-action]");
      if (!actionButton || !viewer.contains(actionButton)) return;
      const action = actionButton.dataset.repoFileAction;
      if (action === "raw") {
        setRepoFileMode(viewer, "raw", lines, path, meta);
      } else if (action === "copy") {
        const copied = await copyTextToClipboard(content);
        if (copied) {
          actionButton.classList.add("text-primary");
          window.setTimeout(() => actionButton.classList.remove("text-primary"), 1200);
          updateRepoFileStatus(viewer, "File contents copied.", "good");
        } else {
          updateRepoFileStatus(viewer, "Could not copy file contents.", "bad");
        }
      } else if (action === "download") {
        downloadRepoFile(path, content);
        updateRepoFileStatus(viewer, `Downloading ${repoFileName(path)}.`, "good");
      } else if (action === "fullscreen") {
        toggleRepoFileFullscreen(viewer);
      }
    }, { signal: controller.signal });

    document.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      setRepoFileFullscreenFallback(viewer.querySelector("[data-repo-file-shell]"), false);
    }, { signal: controller.signal });
  }

