(() => {
  const state = {
    repositories: [],
    filteredRepositories: [],
    page: 1,
    pageSize: 5,
    selectedRepo: null,
  };

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));

  function readSession() {
    try {
      return JSON.parse(localStorage.getItem("forkmesh.session") || "null");
    } catch (_) {
      return null;
    }
  }

  function logout() {
    try {
      localStorage.removeItem("forkmesh.session");
      document.cookie = "forkmesh_session=; Path=/; Max-Age=0; SameSite=Lax";
    } catch (_) {}
    location.replace("/");
  }

  function escapeHtml(value) {
    return String(value ?? "").replace(/[&<>"']/g, (char) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#039;",
    })[char]);
  }

  function formatCount(value) {
    const number = Number(value) || 0;
    return new Intl.NumberFormat().format(number);
  }

  function formatDate(value) {
    if (!value) return "unknown";
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return "unknown";
    return date.toLocaleDateString(undefined, {
      month: "short",
      day: "numeric",
      year: "numeric",
    });
  }

  function formatSize(bytes) {
    let value = Number(bytes) || 0;
    const units = ["B", "KB", "MB", "GB", "TB"];
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit += 1;
    }
    return `${unit === 0 ? value : value.toFixed(1)} ${units[unit]}`;
  }

  function cloneUrl(repo) {
    const owner = encodeURIComponent(repo.owner || "");
    const name = encodeURIComponent(repo.name || "");
    return `${location.origin}/${owner}/${name}`;
  }

  function repoKey(repo) {
    return `${repo.owner || ""}/${repo.name || ""}`;
  }

  function repoApiBase(repo) {
    return `/api/repo/${encodeURIComponent(repo.owner || "")}/${encodeURIComponent(repo.name || "")}`;
  }

  function repoPathUrl(repo, kind = "tree", path = "") {
    const owner = encodeURIComponent(repo.owner || "");
    const name = encodeURIComponent(repo.name || "");
    const suffix = path ? `/${kind}/${path.split("/").map(encodeURIComponent).join("/")}` : "";
    return `/${owner}/${name}${suffix}`;
  }

  function requestedRepoKey() {
    const params = new URLSearchParams(location.search);
    const value = params.get("repo");
    if (value && value.includes("/")) return decodeURIComponent(value);
    const parts = location.pathname.split("/").filter(Boolean);
    if (parts.length >= 2 && !["api", "assets", "dashboard", "docs", "blogs", "blog", "login", "signup", "network", "desktop", "about", "careers", "changelog", "privacy", "terms"].includes(parts[0])) {
      return `${decodeURIComponent(parts[0])}/${decodeURIComponent(parts[1])}`;
    }
    return "";
  }

  async function fetchJson(path) {
    const response = await fetch(path, {
      headers: { accept: "application/json" },
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  function setSection(section) {
    $$("[data-view]").forEach((view) => {
      view.classList.toggle("active", view.dataset.view === section);
    });
    $$("[data-nav-link]").forEach((button) => {
      const active = button.dataset.section === section;
      button.setAttribute("aria-current", active ? "page" : "false");
      button.classList.toggle("text-foreground", active);
      button.classList.toggle("text-muted-foreground", !active);
      button.classList.toggle("hover:text-foreground", !active);
    });
  }

  function renderProfile(session) {
    const name = session?.nodeName || session?.email || "My Profile";
    const initial = (name[0] || "F").toUpperCase();
    const nameEl = $("[data-dashboard-profile-name]");
    const statusEl = $("[data-dashboard-profile-status]");
    const avatar = $("[data-dashboard-profile-avatar]");

    if (nameEl) nameEl.textContent = name;
    if (statusEl) {
      statusEl.textContent = session?.nodeName
        ? "The node is online"
        : "Signed in";
    }
    if (avatar) avatar.textContent = initial;
  }

  function renderSidebarRepositories(session) {
    const list = $("[data-sidebar-repo-list]");
    const count = $("[data-sidebar-repo-count]");
    if (!list) return;

    const owner = String(session?.nodeName || "").toLowerCase();
    const ownRepos = owner
      ? state.repositories.filter((repo) => String(repo.owner || "").toLowerCase() === owner)
      : [];
    const repos = ownRepos.slice(0, 8);

    if (count) {
      count.textContent = ownRepos.length
        ? `${ownRepos.length} active`
        : "0 active";
    }
    if (!repos.length) {
      list.innerHTML = `
        <button data-section="repos" class="nav-jump w-full flex items-center gap-2 px-2 py-1.5 rounded-md text-xs text-muted-foreground hover:text-foreground hover:bg-secondary transition-colors">
          <i data-lucide="git-branch" class="w-3 h-3 shrink-0"></i>
          <span class="truncate">No owned repos yet</span>
        </button>
      `;
      return;
    }

    list.innerHTML = repos.map((repo) => `
      <button data-dashboard-open-repo="${escapeHtml(repoKey(repo))}" class="w-full flex items-center gap-2 px-2 py-1.5 rounded-md text-xs text-muted-foreground hover:text-foreground hover:bg-secondary transition-colors">
        <i data-lucide="git-branch" class="w-3 h-3 shrink-0"></i>
        <span class="truncate">${escapeHtml(repo.name || "repository")}</span>
        <i data-lucide="circle" class="w-1.5 h-1.5 ml-auto fill-current shrink-0 ${repo.liveHost ? "text-primary" : "text-muted-foreground"}"></i>
      </button>
    `).join("");
  }

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

  function repositoryCard(repo) {
    const key = repoKey(repo);
    const live = Boolean(repo.liveHost);
    const visibility = repo.isPrivate ? "private" : "public";
    const statusClass = live ? "text-primary" : "text-muted-foreground";
    const statusText = live ? "online" : "offline";
    return `
      <div data-repo="${escapeHtml(key.toLowerCase())}" class="repo-card group px-4 sm:px-5 py-5 hover:bg-secondary/40 transition-colors">
        <div class="repo-layout flex flex-col gap-5 lg:grid lg:grid-cols-[minmax(0,1fr)_auto] lg:items-stretch">
          <div class="min-w-0">
            <p class="text-sm font-medium text-foreground truncate">
              <span class="text-muted-foreground">${escapeHtml(repo.owner || "owner")}/</span>${escapeHtml(repo.name || "repository")}
            </p>
            <p class="mt-2 text-sm text-muted-foreground">${escapeHtml(repo.description || "No description published.")}</p>
            <div class="mt-4 flex items-center gap-x-5 gap-y-2 flex-wrap">
              <span class="flex items-center gap-1.5 text-xs text-muted-foreground">
                <span class="w-2 h-2 rounded-full bg-primary"></span>${escapeHtml(visibility)}
              </span>
              <span class="flex items-center gap-1 text-xs text-muted-foreground">
                <i data-lucide="radio" class="w-3 h-3"></i>${live ? "live host" : "host offline"}
              </span>
              <span class="text-xs text-muted-foreground font-mono">updated ${escapeHtml(formatDate(repo.updatedAt || repo.lastSync))}</span>
            </div>
            <button data-dashboard-copy="git clone ${escapeHtml(cloneUrl(repo))}" class="copy-button mt-5 inline-flex items-center gap-2 px-3 py-1.5 rounded-md border border-border bg-secondary text-xs text-foreground hover:bg-secondary/80 transition-colors">
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
    const total = state.filteredRepositories.length;
    const pages = Math.max(1, Math.ceil(total / state.pageSize));
    state.page = Math.min(Math.max(1, state.page), pages);

    const start = (state.page - 1) * state.pageSize;
    const end = Math.min(start + state.pageSize, total);
    const visible = state.filteredRepositories.slice(start, end);
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
        ? `Showing ${start + 1}-${end} of ${total} mirrored repositories`
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
    updateRepositoryPagination();
  }

  function renderRepositories(repositories, session) {
    state.repositories = Array.isArray(repositories) ? repositories : [];
    state.filteredRepositories = state.repositories.slice();
    state.page = 1;

    const count = $("[data-repo-count]");
    if (count) count.textContent = `${formatCount(state.repositories.length)} mirrored`;

    renderSidebarRepositories(session);
    applyRepositoryFilter();
  }

  function setRepoTab(tab) {
    const detail = $(`[data-dashboard-repo-tab-panel="${tab}"]`)?.closest("[data-repo-detail]") || $("[data-repo-detail]");
    if (!detail) return;
    detail.querySelectorAll("[data-dashboard-repo-tab]").forEach((button) => {
      const active = button.dataset.dashboardRepoTab === tab;
      button.setAttribute("aria-selected", active ? "true" : "false");
      button.classList.toggle("bg-secondary", active);
      button.classList.toggle("text-foreground", active);
      button.classList.toggle("text-muted-foreground", !active);
    });
    detail.querySelectorAll("[data-dashboard-repo-tab-panel]").forEach((panel) => {
      panel.classList.toggle("hidden", panel.dataset.dashboardRepoTabPanel !== tab);
    });
  }

  async function loadRepositoryTree(repo, path = "") {
    const detail = $("[data-repo-detail]");
    if (!detail) return;
    const treeBody = detail.querySelector("[data-repo-tree]");
    const crumb = detail.querySelector("[data-repo-breadcrumb]");
    if (!treeBody) return;

    if (crumb) {
      const parts = path ? path.split("/").filter(Boolean) : [];
      let acc = "";
      crumb.innerHTML = [`<button data-dashboard-tree-path="" class="text-primary hover:underline">root</button>`]
        .concat(parts.map((part) => {
          acc = acc ? `${acc}/${part}` : part;
          return `<span class="text-muted-foreground">/</span><button data-dashboard-tree-path="${escapeHtml(acc)}" class="text-primary hover:underline">${escapeHtml(part)}</button>`;
        })).join(" ");
    }

    treeBody.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Loading tree...</div>';
    try {
      const data = await fetchJson(`${repoApiBase(repo)}/tree?path=${encodeURIComponent(path)}`);
      const entries = Array.isArray(data.entries) ? data.entries.slice() : [];
      if (!entries.length) {
        treeBody.innerHTML = '<div class="py-3 text-sm text-muted-foreground">This directory is empty.</div>';
        return;
      }
      entries.sort((a, b) => {
        if (a.type !== b.type) return a.type === "tree" ? -1 : 1;
        return String(a.name || "").localeCompare(String(b.name || ""));
      });
      treeBody.innerHTML = entries.map((entry) => {
        const childPath = path ? `${path}/${entry.name || ""}` : String(entry.name || "");
        const isTree = entry.type === "tree";
        return `
          <button data-dashboard-${isTree ? "tree" : "blob"}-path="${escapeHtml(childPath)}" class="flex w-full items-center gap-2 border-t border-border py-2 text-left text-sm hover:bg-secondary/40 transition-colors">
            <i data-lucide="${isTree ? "folder" : "file"}" class="h-4 w-4 shrink-0 text-muted-foreground"></i>
            <span class="min-w-0 flex-1 truncate text-foreground">${escapeHtml(entry.name || "entry")}</span>
            <span class="shrink-0 text-xs text-muted-foreground font-mono">${isTree ? "dir" : escapeHtml(formatSize(entry.size))}</span>
          </button>`;
      }).join("");
      window.history.replaceState(null, "", repoPathUrl(repo, "tree", path));
      window.lucide?.createIcons();
    } catch (_) {
      treeBody.innerHTML = '<div class="py-3 text-sm text-muted-foreground">No live desktop host is serving this repository tree right now.</div>';
    }
  }

  async function loadRepositoryBlob(repo, path) {
    const detail = $("[data-repo-detail]");
    const viewer = detail?.querySelector("[data-repo-blob]");
    if (!viewer) return;
    setRepoTab("code");
    viewer.classList.remove("hidden");
    viewer.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Loading file...</div>';
    try {
      const data = await fetchJson(`${repoApiBase(repo)}/blob?path=${encodeURIComponent(path)}`);
      const content = data.content || data.text || "";
      viewer.innerHTML = `
        <div class="mt-4 overflow-hidden rounded-lg border border-border bg-background">
          <div class="flex items-center justify-between border-b border-border px-4 py-2">
            <span class="min-w-0 truncate text-xs font-mono text-foreground">${escapeHtml(path)}</span>
            <span class="text-[10px] font-mono text-muted-foreground">${escapeHtml(formatSize(content.length))}</span>
          </div>
          <pre class="max-h-[32rem] overflow-auto p-4 text-xs leading-5 text-muted-foreground"><code>${escapeHtml(content)}</code></pre>
        </div>`;
      window.history.replaceState(null, "", repoPathUrl(repo, "blob", path));
    } catch (_) {
      viewer.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Could not load this file from a live host.</div>';
    }
  }

  function setRepoTabCount(tab, value) {
    const badge = $(`[data-dashboard-repo-tab-count="${tab}"]`);
    if (!badge) return;
    if (Number.isFinite(value)) {
      badge.textContent = String(value);
      badge.hidden = false;
    } else {
      badge.hidden = true;
    }
  }

  // The repo root tree reply bundles per-tab tallies (issue #93), so the badges
  // fill from that one request instead of a probe per counter. The Issues badge
  // is owned by loadRepoIssues, which counts the open issues for the default view.
  function applyServedCounts(counts) {
    if (!counts || typeof counts !== "object") return;
    setRepoTabCount("pulls", Number(counts.pulls));
    setRepoTabCount("discussions", Number(counts.discussions));
  }

  // Parse the leading "---\nkey: value\n---" frontmatter block of an issue.md.
  function parseFrontmatter(content) {
    const out = {};
    if (!content) return out;
    const lines = String(content).split("\n");
    if (lines[0].trim() !== "---") return out;
    for (let i = 1; i < lines.length; i++) {
      if (lines[i].trim() === "---") break;
      const idx = lines[i].indexOf(":");
      if (idx < 0) continue;
      out[lines[i].slice(0, idx).trim()] = lines[i].slice(idx + 1).trim();
    }
    return out;
  }

  // Issues default to the Open view; Closed and All are opt-in (issue #270).
  const issuesView = { token: 0, items: [], filter: "open" };

  function renderRepoIssues() {
    const container = $("[data-repo-issues]");
    if (!container) return;
    const visible = issuesView.items.filter((issue) => {
      if (issuesView.filter === "all") return true;
      if (issuesView.filter === "open") return issue.status === "open";
      return issue.status !== "open";
    });
    if (!visible.length) {
      const note =
        issuesView.filter === "open"
          ? "No open issues. Switch to All to see closed ones."
          : issuesView.filter === "closed"
            ? "No closed issues."
            : "No issues have been filed for this repository yet.";
      container.innerHTML = `<div class="py-3 text-sm text-muted-foreground">${note}</div>`;
      return;
    }
    container.innerHTML = visible.map((issue) => {
      const closed = issue.status !== "open";
      return `
        <button type="button" data-dashboard-blob-path="issues/${issue.number}/issue.md" class="flex w-full items-center gap-3 border-t border-border py-3 text-left transition-colors hover:bg-secondary/40">
          <span class="h-2 w-2 shrink-0 rounded-full ${closed ? "bg-muted-foreground" : "bg-primary"}"></span>
          <span class="min-w-0 flex-1 truncate text-sm text-foreground">#${issue.number} ${escapeHtml(issue.title)}</span>
          <span class="shrink-0 rounded-full border border-border px-2 py-0.5 text-[10px] font-mono ${closed ? "text-muted-foreground" : "text-primary"}">${closed ? "Closed" : "Open"}</span>
        </button>`;
    }).join("");
  }

  function setIssueFilter(filter) {
    issuesView.filter = filter;
    $$("[data-dashboard-issue-filter]").forEach((button) => {
      const active = button.dataset.dashboardIssueFilter === filter;
      button.setAttribute("aria-pressed", active ? "true" : "false");
      button.classList.toggle("bg-secondary", active);
      button.classList.toggle("text-foreground", active);
      button.classList.toggle("text-muted-foreground", !active);
    });
    renderRepoIssues();
  }

  // List the repo's published issues from its git issues/ folder, defaulting to
  // the Open view. issue.md frontmatter carries the current status (the owner
  // rewrites it as later -status.md events land), so one blob per issue suffices.
  async function loadRepoIssues(repo) {
    const container = $("[data-repo-issues]");
    if (!container) return;
    const token = ++issuesView.token;
    issuesView.items = [];
    issuesView.filter = "open";
    container.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Loading...</div>';
    let entries;
    try {
      const data = await fetchJson(`${repoApiBase(repo)}/tree?path=${encodeURIComponent("issues")}`);
      entries = Array.isArray(data.entries) ? data.entries : [];
    } catch (_) {
      if (token !== issuesView.token) return;
      container.innerHTML = '<div class="py-3 text-sm text-muted-foreground">No live host is serving this repository\'s issues right now.</div>';
      return;
    }
    const numbers = entries
      .filter((entry) => entry.type === "tree" && /^\d+$/.test(entry.name))
      .map((entry) => parseInt(entry.name, 10))
      .sort((a, b) => b - a);
    if (!numbers.length) {
      if (token !== issuesView.token) return;
      setRepoTabCount("issues", 0);
      container.innerHTML = '<div class="py-3 text-sm text-muted-foreground">No issues have been filed for this repository yet.</div>';
      return;
    }
    const items = await Promise.all(numbers.map(async (number) => {
      try {
        const blob = await fetchJson(`${repoApiBase(repo)}/blob?path=${encodeURIComponent(`issues/${number}/issue.md`)}`);
        const fm = parseFrontmatter(blob.content || blob.text || "");
        return { number, title: fm.title || `Issue #${number}`, status: fm.status || "open" };
      } catch (_) {
        return { number, title: `Issue #${number}`, status: "open" };
      }
    }));
    if (token !== issuesView.token) return;
    issuesView.items = items;
    // The Issues badge tracks open issues, matching the default Open view (issue #270).
    setRepoTabCount("issues", items.filter((issue) => issue.status === "open").length);
    renderRepoIssues();
  }

  async function loadRepoCollection(repo, kind, containerSelector) {
    const container = $(containerSelector);
    if (!container) return;
    container.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Loading...</div>';
    try {
      const data = await fetchJson(`${repoApiBase(repo)}/${kind}`);
      const items = data[kind] || data.items || data.entries || [];
      if (!Array.isArray(items) || !items.length) {
        container.innerHTML = `<div class="py-3 text-sm text-muted-foreground">No ${kind} published for this repository yet.</div>`;
        return;
      }
      container.innerHTML = items.slice(0, 25).map((item) => `
        <div class="border-t border-border py-3">
          <div class="flex items-center justify-between gap-3">
            <p class="min-w-0 truncate text-sm font-medium text-foreground">${escapeHtml(item.title || item.subject || item.name || item.id || `${kind.slice(0, -1)} item`)}</p>
            <span class="shrink-0 rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${escapeHtml(item.status || item.state || item.type || kind.slice(0, -1))}</span>
          </div>
          <p class="mt-1 text-xs text-muted-foreground">${escapeHtml(item.author || item.source || item.sender || "unknown")}</p>
        </div>`).join("");
    } catch (_) {
      container.innerHTML = `<div class="py-3 text-sm text-muted-foreground">${kind} are unavailable right now.</div>`;
    }
  }

  async function loadRepoMirrors(repo) {
    const container = $("[data-repo-mirrors]");
    if (!container) return;
    container.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Loading mirrors...</div>';
    try {
      const data = await fetchJson(`${repoApiBase(repo)}/mirrors`);
      const mirrors = data.mirrors || [];
      setRepoTabCount("mirrors", mirrors.length);
      if (!mirrors.length) {
        container.innerHTML = '<div class="py-3 text-sm text-muted-foreground">No mirrors reported yet.</div>';
        return;
      }
      container.innerHTML = mirrors.map((mirror) => `
        <div class="grid grid-cols-[1fr_auto] gap-3 border-t border-border py-3 text-sm">
          <span class="min-w-0 truncate text-foreground font-mono">${escapeHtml(mirror.owner || mirror.node || mirror.name || "mirror")}</span>
          <span class="text-xs font-mono ${mirror.status === "online" ? "text-primary" : "text-muted-foreground"}">${escapeHtml(mirror.status || "unknown")}</span>
        </div>`).join("");
    } catch (_) {
      container.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Mirror health is unavailable right now.</div>';
    }
  }

  function loadRepoFeaturePanels(repo) {
    loadRepoIssues(repo);
    loadRepoCollection(repo, "pulls", "[data-repo-pulls]");
    loadRepoCollection(repo, "discussions", "[data-repo-discussions]");
    loadRepoMirrors(repo);
  }

  function renderRepoDetail(repo) {
    const detail = $("[data-repo-detail]");
    if (!detail || !repo) return;
    state.selectedRepo = repo;
    detail.innerHTML = `
      <div class="grid gap-5 lg:grid-cols-[minmax(0,1fr)_18rem]">
        <div class="min-w-0">
          <div class="flex flex-wrap items-center gap-2">
            <h2 class="min-w-0 truncate text-lg font-semibold text-foreground"><span class="text-muted-foreground">${escapeHtml(repo.owner || "owner")}/</span>${escapeHtml(repo.name || "repository")}</h2>
            <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${repo.isPrivate ? "private" : "public"}</span>
            <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono ${repo.liveHost ? "text-primary" : "text-muted-foreground"}">${repo.liveHost ? "host online" : "host offline"}</span>
          </div>
          <p class="mt-3 text-sm leading-6 text-muted-foreground">${escapeHtml(repo.description || "No description published.")}</p>
          <div class="mt-5 flex flex-wrap gap-2">
            <button data-dashboard-copy="git clone ${escapeHtml(cloneUrl(repo))}" class="copy-button inline-flex items-center gap-2 rounded-md border border-border bg-secondary px-3 py-2 text-xs text-foreground hover:bg-secondary/80 transition-colors"><i data-lucide="copy" class="copy-icon h-3.5 w-3.5"></i><i data-lucide="check" class="copy-check h-3.5 w-3.5 text-primary"></i>Copy clone</button>
            <a href="${escapeHtml(cloneUrl(repo))}" class="inline-flex items-center gap-2 rounded-md border border-border px-3 py-2 text-xs text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors">Open clean URL<i data-lucide="external-link" class="h-3.5 w-3.5"></i></a>
          </div>
          <div class="mt-6 flex flex-wrap gap-2 border-b border-border pb-2" role="tablist">
            ${["code", "issues", "pulls", "discussions", "mirrors"].map((tab) => `<button type="button" role="tab" data-dashboard-repo-tab="${tab}" class="inline-flex items-center rounded-md px-3 py-1.5 text-xs font-medium transition-colors ${tab === "code" ? "bg-secondary text-foreground" : "text-muted-foreground hover:bg-secondary hover:text-foreground"}">${tab === "pulls" ? "Pulls" : tab[0].toUpperCase() + tab.slice(1)}<span data-dashboard-repo-tab-count="${tab}" class="ml-1.5 inline-flex items-center justify-center rounded-full bg-secondary px-1.5 text-[10px] font-mono text-muted-foreground" hidden></span></button>`).join("")}
          </div>
          <section data-dashboard-repo-tab-panel="code">
            <div class="mt-4 text-xs text-muted-foreground" data-repo-breadcrumb></div>
            <div class="mt-3 rounded-lg border border-border bg-background px-4"><div class="flex items-center justify-between py-3"><span class="text-xs font-medium text-foreground">Code</span><span class="text-[10px] text-muted-foreground font-mono">live host</span></div><div data-repo-tree></div></div>
            <div data-repo-blob class="hidden"></div>
          </section>
          <section data-dashboard-repo-tab-panel="issues" class="hidden"><div class="mt-4 rounded-lg border border-border bg-background px-4"><div class="flex items-center justify-between gap-3 py-3"><span class="text-xs font-medium text-foreground">Issues</span><div class="issue-filter inline-flex items-center gap-1" role="group" aria-label="Issue state">${["open", "closed", "all"].map((stateName) => `<button type="button" data-dashboard-issue-filter="${stateName}" aria-pressed="${stateName === "open" ? "true" : "false"}" class="rounded-md px-2 py-1 text-[11px] font-medium transition-colors ${stateName === "open" ? "bg-secondary text-foreground" : "text-muted-foreground hover:bg-secondary hover:text-foreground"}">${stateName[0].toUpperCase() + stateName.slice(1)}</button>`).join("")}</div></div><div data-repo-issues></div></div></section>
          <section data-dashboard-repo-tab-panel="pulls" class="hidden"><div class="mt-4 rounded-lg border border-border bg-background px-4"><div class="py-3 text-xs font-medium text-foreground">Pull requests</div><div data-repo-pulls></div></div></section>
          <section data-dashboard-repo-tab-panel="discussions" class="hidden"><div class="mt-4 rounded-lg border border-border bg-background px-4"><div class="py-3 text-xs font-medium text-foreground">Discussions and comments</div><div data-repo-discussions></div></div></section>
          <section data-dashboard-repo-tab-panel="mirrors" class="hidden"><div class="mt-4 rounded-lg border border-border bg-background px-4"><div class="py-3 text-xs font-medium text-foreground">Mirrors</div><div data-repo-mirrors></div></div></section>
        </div>
        <aside class="rounded-lg border border-border bg-background p-4">
          <h3 class="text-xs font-medium text-foreground">Repository metadata</h3>
          <dl class="mt-4 grid gap-3 text-xs">
            <div class="flex justify-between gap-3"><dt class="text-muted-foreground">Channel</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(repo.channel || "general")}</dd></div>
            <div class="flex justify-between gap-3"><dt class="text-muted-foreground">Source</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(repo.source || "desktop")}</dd></div>
            <div class="flex justify-between gap-3"><dt class="text-muted-foreground">Maintainer</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(repo.maintainer || repo.owner || "unknown")}</dd></div>
            <div class="flex justify-between gap-3"><dt class="text-muted-foreground">Updated</dt><dd class="min-w-0 truncate text-right text-foreground font-mono">${escapeHtml(formatDate(repo.updatedAt || repo.lastSync))}</dd></div>
          </dl>
        </aside>
      </div>`;
    setSection("explore");
    window.lucide?.createIcons();
    const parts = location.pathname.split("/").filter(Boolean);
    const kind = parts[2];
    const path = parts.length > 3 ? parts.slice(3).map(decodeURIComponent).join("/") : "";
    loadRepositoryTree(repo, kind === "tree" ? path : "");
    if (kind === "blob" && path) loadRepositoryBlob(repo, path);
    loadRepoFeaturePanels(repo);
  }

  function findRepository(key) {
    return state.repositories.find((repo) => repoKey(repo) === key);
  }

  function renderNetworkRows(rows) {
    const list = $("[data-network-node-list]");
    const rail = $("[data-network-rail-nodes]");
    const count = $("[data-network-node-count]");
    const recent = rows.slice(0, 6);

    if (count) count.textContent = `${recent.length} recent`;
    if (list) {
      list.innerHTML = recent.length
        ? recent.map((row) => `
          <div class="px-4 py-3 flex items-center gap-4 hover:bg-secondary/50 transition-colors">
            <div class="flex items-center gap-2 flex-1 min-w-0">
              <i data-lucide="circle" class="w-2 h-2 fill-primary text-primary shrink-0"></i>
              <span class="text-sm text-foreground font-medium truncate font-mono">${escapeHtml(row.name || "node")}</span>
            </div>
            <span class="text-xs text-muted-foreground w-20 text-right font-mono">${formatCount(row.minutes || row.repos || row.total || 0)}</span>
          </div>
        `).join("")
        : '<div class="px-4 py-3 text-sm text-muted-foreground">No recent network activity yet.</div>';
    }
    if (rail) {
      rail.innerHTML = recent.slice(0, 3).length
        ? recent.slice(0, 3).map((row) => `
          <div class="flex items-center gap-2">
            <i data-lucide="circle" class="w-1.5 h-1.5 fill-primary text-primary shrink-0"></i>
            <span class="text-xs text-muted-foreground truncate flex-1 font-mono">${escapeHtml(row.name || "node")}</span>
            <span class="text-[10px] text-muted-foreground font-mono">${formatCount(row.minutes || row.repos || row.total || 0)}</span>
          </div>
        `).join("")
        : '<div class="text-xs text-muted-foreground">No recent network activity.</div>';
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

      renderNetworkRows(uptime);
    } catch (_) {
      $("[data-network-node-list]") && ($("[data-network-node-list]").innerHTML =
        '<div class="px-4 py-3 text-sm text-muted-foreground">Network data is unavailable right now.</div>');
      $("[data-network-rail-nodes]") && ($("[data-network-rail-nodes]").innerHTML =
        '<div class="text-xs text-muted-foreground">Network data unavailable.</div>');
    } finally {
      window.lucide?.createIcons();
    }
  }

  async function init() {
    const session = readSession();
    const requested = requestedRepoKey();
    if (!session || (!session.nodeName && !session.email)) {
      if (!requested) {
        location.replace("/");
        return;
      }
    }

    renderProfile(session || { nodeName: "guest" });
    try {
      const data = await fetchJson("/api/repositories");
      renderRepositories(data.repositories, session);
      if (requested) {
        const repo = findRepository(requested);
        if (repo) renderRepoDetail(repo);
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
    const profileToggle = event.target.closest("[data-profile-toggle]");
    if (profileToggle) {
      const popover = $("[data-profile-popover]");
      const open = popover?.classList.contains("hidden");
      popover?.classList.toggle("hidden", !open);
      profileToggle.setAttribute("aria-expanded", open ? "true" : "false");
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

    const sectionButton = event.target.closest("[data-section]");
    if (sectionButton) {
      setSection(sectionButton.dataset.section);
    }

    const pageButton = event.target.closest("[data-dashboard-repo-page]");
    if (pageButton) {
      state.page = Number(pageButton.dataset.dashboardRepoPage) || 1;
      updateRepositoryPagination();
    }

    const openButton = event.target.closest("[data-dashboard-open-repo]");
    if (openButton) {
      const repo = findRepository(openButton.dataset.dashboardOpenRepo);
      if (repo) renderRepoDetail(repo);
    }

    const repoTabButton = event.target.closest("[data-dashboard-repo-tab]");
    if (repoTabButton) {
      setRepoTab(repoTabButton.dataset.dashboardRepoTab);
      return;
    }

    const issueFilterButton = event.target.closest("[data-dashboard-issue-filter]");
    if (issueFilterButton) {
      setIssueFilter(issueFilterButton.dataset.dashboardIssueFilter);
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

    const copyButton = event.target.closest("[data-dashboard-copy]");
    if (copyButton) {
      const text = copyButton.dataset.dashboardCopy || "";
      try {
        await navigator.clipboard.writeText(text);
      } catch (_) {
        const temp = document.createElement("textarea");
        temp.value = text;
        temp.setAttribute("readonly", "");
        temp.style.position = "fixed";
        temp.style.opacity = "0";
        document.body.appendChild(temp);
        temp.select();
        document.execCommand("copy");
        temp.remove();
      }
      copyButton.classList.add("copied");
      window.setTimeout(() => copyButton.classList.remove("copied"), 1600);
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

  init();
})();
