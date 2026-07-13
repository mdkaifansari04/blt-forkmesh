(() => {
  const state = {
    repositories: [],
    filteredRepositories: [],
    filteredGroups: [],
    repositoriesLoading: true,
    page: 1,
    pageSize: 5,
    selectedRepo: null,
    fetchJsonInflight: {},
    fetchJsonCache: {},
    selectedBranches: {},
    repoBranches: {},
    repoBranchQueries: {},
    repoCollectionPages: {},
    session: null,
    // Public-profile mode (/@name): the FOREIGN account whose profile the
    // page is showing, or null when the profile pages show the session user.
    publicProfile: null,
    repoFileFinder: {
      repoKey: "",
      files: [],
      indexed: false,
      indexing: false,
      partial: false,
      error: "",
      selectedIndex: 0,
    },
    nodeNameAvailability: {
      candidate: "",
      available: false,
      checking: false,
      seq: 0,
    },
    notifications: [],
    notificationUnread: 0,
    selectedNotificationId: "",
    issuesView: { filter: "open", items: [], query: "" },
    // Projects tab (issue #384): projects link issues + a milestone and carry
    // start/end dates; "gantt" is the default sub-view, "list" the fallback.
    projectsView: { filter: "open", mode: "gantt", items: [] },
    claimNode: { pendingNodeId: "" },
    linkGrant: null,
    repoMirrors: [],
    repoServedBy: null,
    // Owner-only "Agents" tab (adhoc #225): owner verification by node account,
    // no password required. selectedAgentId (adhoc #259) is the id of the agent
    // whose detail page - live transcript + prompt - is currently open, or null
    // for the session list.
    agentsView: { agents: [], selectedAgentId: null },
    // Home left-rail "Active agent sessions" list (adhoc #81): aggregated,
    // non-terminal agent runs across the repos the session can assign agents
    // to. null until the first cross-repo fetch resolves so the panel can tell
    // "loading" apart from "no active sessions".
    homeAgentSessions: null,
    longDiffOverrides: {},
    repoCommitDetail: null,
    repoRecordDetail: null,
    profileContributions: {
      year: new Date().getFullYear(),
      liveHistory: {},
      loading: false,
      loadedYears: {},
    },
    settingsView: {
      section: "public-profile",
    },
    globalSearch: {
      open: false,
      selectedIndex: 0,
      results: [],
    },
  };

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const MAX_REPO_FILE_FINDER_RESULTS = 500;
  const MAX_REPO_FILE_FINDER_SECONDS = 6;
  const REPO_COLLECTION_PAGE_SIZE = 5;
  const DASHBOARD_THEME_KEY = "forkmesh.dashboard.theme";
  const DASHBOARD_LONG_DIFFS_KEY = "forkmesh.dashboard.longDiffs";
  const DASHBOARD_DIFF_AUTO_RENDER_MAX_CHARS = 250000;
  function readSession() {
    try {
      return JSON.parse(localStorage.getItem("forkmesh.session") || "null");
    } catch (_) {
      return null;
    }
  }

  function normalizeDashboardTheme(theme) {
    return theme === "light" ? "light" : "dark";
  }

  function readDashboardTheme() {
    try {
      return normalizeDashboardTheme(
        localStorage.getItem(DASHBOARD_THEME_KEY) ||
        localStorage.getItem("forkmesh.theme")
      );
    } catch (_) {
      return "dark";
    }
  }

  function renderAppearanceTheme(theme = readDashboardTheme()) {
    const current = normalizeDashboardTheme(theme);
    $$("[data-appearance-theme]").forEach((button) => {
      const active = button.dataset.appearanceTheme === current;
      button.setAttribute("aria-checked", active ? "true" : "false");
      button.classList.toggle("border-primary", active);
      button.classList.toggle("bg-secondary", active);
      button.classList.toggle("shadow-sm", active);
      button.querySelector("[data-appearance-check]")?.classList.toggle("hidden", !active);
    });
    const status = $("[data-appearance-theme-status]");
    if (status) {
      status.textContent = current === "light"
        ? "Light mode is active for the dashboard."
        : "Dark mode is active for the dashboard.";
    }
  }

  function applyDashboardTheme(theme) {
    const nextTheme = normalizeDashboardTheme(theme);
    document.documentElement.dataset.dashboardTheme = nextTheme;
    document.documentElement.style.colorScheme = nextTheme;
    document.querySelector('meta[name="theme-color"]')?.setAttribute(
      "content",
      nextTheme === "light" ? "#f6f8fb" : "#090909",
    );
    renderAppearanceTheme(nextTheme);
    return nextTheme;
  }

  function saveDashboardTheme(theme) {
    const nextTheme = normalizeDashboardTheme(theme);
    try {
      localStorage.setItem(DASHBOARD_THEME_KEY, nextTheme);
      localStorage.setItem("forkmesh.theme", nextTheme);
    } catch (_) {}
    applyDashboardTheme(nextTheme);
  }

  function readDashboardLongDiffs() {
    try {
      return localStorage.getItem(DASHBOARD_LONG_DIFFS_KEY) === "1";
    } catch (_) {
      return false;
    }
  }

  function renderLongDiffPreference(on = readDashboardLongDiffs()) {
    $$("[data-long-diff-toggle]").forEach((input) => {
      input.checked = Boolean(on);
    });
    const status = $("[data-long-diff-status]");
    if (status) {
      status.textContent = on
        ? "Large diffs render automatically on this device."
        : "Large diffs stay collapsed until you open them.";
    }
  }

  function saveDashboardLongDiffs(on) {
    try {
      if (on) localStorage.setItem(DASHBOARD_LONG_DIFFS_KEY, "1");
      else localStorage.removeItem(DASHBOARD_LONG_DIFFS_KEY);
    } catch (_) {}
    renderLongDiffPreference(Boolean(on));
  }

  function diffOverrideEnabled(key) {
    return Boolean(key && state.longDiffOverrides?.[key]);
  }

  function shouldRenderLongDiff(diff, key) {
    return readDashboardLongDiffs() ||
      diffOverrideEnabled(key) ||
      String(diff || "").length <= DASHBOARD_DIFF_AUTO_RENDER_MAX_CHARS;
  }

  function renderLongDiffNotice(label, key, diff) {
    const chars = String(diff || "").length;
    return `
      <div class="grid gap-2 px-4 py-4 text-sm text-muted-foreground">
        <div class="font-medium text-amber-300">Diff hidden for speed</div>
        <p>This ${escapeHtml(label)} is ${formatCount(chars)} characters. Enable long diffs in Profile settings to render these automatically.</p>
        <button type="button" data-show-full-diff="${escapeHtml(key || "")}" class="inline-flex h-8 w-fit items-center gap-2 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary transition-colors">
          <i data-lucide="file-diff" class="h-3.5 w-3.5"></i>
          Show full diff
        </button>
      </div>`;
  }

  function writeSession(nextSession) {
    state.session = nextSession;
    try {
      localStorage.setItem("forkmesh.session", JSON.stringify(nextSession));
      document.cookie = "forkmesh_session=1; Path=/; Max-Age=2592000; SameSite=Lax"
        + (location.protocol === "https:" ? "; Secure" : "");
    } catch (_) {}
  }

  function logout() {
    try {
      fetch("/api/accounts/logout", { method: "POST", keepalive: true }).catch(() => {});
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

  // Loading placeholder: spinner (see .fm-spinner in the shell <style>)
  // followed by the label. `label` is inserted as HTML so call sites can keep
  // their pre-escaped fragments.
  function loadingHtml(label) {
    return `<span class="fm-spinner" aria-hidden="true"></span>${label}`;
  }

  function formatCount(value) {
    const number = Number(value) || 0;
    return new Intl.NumberFormat().format(number);
  }

  // Compose-identity chip: avatar + username shown next to any box where the
  // session user is about to send content (issues, replies, reviews). Built as
  // an HTML string so it can be dropped straight into the template-literal forms
  // in 06/07; mirrors the initial/avatarPng logic in applyAvatar().
  function composeIdentityHtml(session, verb) {
    const name = String(session?.nodeName || session?.email || "you");
    const initial = escapeHtml((name[0] || "F").toUpperCase());
    const avatarPng = String(session?.avatarPng || "");
    const inner = avatarPng
      ? `<img class="h-full w-full object-cover" src="data:image/png;base64,${avatarPng}" alt="${escapeHtml(name)} avatar" />`
      : `<span class="text-[10px] font-semibold text-muted-foreground">${initial}</span>`;
    const label = verb
      ? `<span class="text-muted-foreground">${escapeHtml(verb)} as</span> <span class="font-semibold text-foreground">${escapeHtml(name)}</span>`
      : `<span class="font-semibold text-foreground">${escapeHtml(name)}</span>`;
    return `<span class="inline-flex min-w-0 items-center gap-2 text-xs">
        <span class="flex h-6 w-6 shrink-0 items-center justify-center overflow-hidden rounded-full border border-border bg-secondary">${inner}</span>
        <span class="min-w-0 truncate">${label}</span>
      </span>`;
  }

  function parseFlexibleDate(value) {
    if (value === undefined || value === null || value === "") return null;
    const numeric = Number(value);
    const date = Number.isFinite(numeric) && numeric > 0
      ? new Date(numeric < 1000000000000 ? numeric * 1000 : numeric)
      : new Date(value);
    return Number.isNaN(date.getTime()) ? null : date;
  }

  function formatDate(value) {
    const date = parseFlexibleDate(value);
    if (!date) return "unknown";
    return date.toLocaleDateString(undefined, {
      month: "short",
      day: "numeric",
      year: "numeric",
    });
  }

  // GitHub-style relative timestamp ("3 days ago") for the repo detail page's
  // commit/file dates - absolute dates there made every mirrored commit look
  // identically stale ("Jul 10, 2026") instead of showing recency at a glance.
  function formatTimeAgo(value) {
    const date = parseFlexibleDate(value);
    if (!date) return "unknown";
    const seconds = Math.max(0, Math.round((Date.now() - date.getTime()) / 1000));
    if (seconds < 45) return "just now";
    const units = [
      ["year", 31536000],
      ["month", 2592000],
      ["week", 604800],
      ["day", 86400],
      ["hour", 3600],
      ["minute", 60],
    ];
    for (const [name, secs] of units) {
      const count = Math.floor(seconds / secs);
      if (count >= 1) return `${count} ${name}${count === 1 ? "" : "s"} ago`;
    }
    return "just now";
  }

  // Map a file name to a vscode-icons SVG base name, mirroring the Qt client's
  // fileTypeIconName (MainWindowInternal.h) so the web file browser shows the
  // exact same per-filetype icons as the desktop app. The referenced subset is
  // copied into public/assets/file-icons/; unknown types fall back to
  // default_file.
  const FILE_ICON_BY_NAME = {
    "cmakelists.txt": "file_type_cmake",
    "dockerfile": "file_type_docker",
    "makefile": "file_type_makefile",
    "package.json": "file_type_npm",
    "package-lock.json": "file_type_npm",
    ".gitignore": "file_type_git",
    ".gitattributes": "file_type_git",
    ".gitmodules": "file_type_git",
    "license": "file_type_license",
    "license.md": "file_type_license",
    "license.txt": "file_type_license",
    "copying": "file_type_license",
    "readme.md": "file_type_markdown",
    "todo": "file_type_todo",
    ".env": "file_type_config",
  };
  const FILE_ICON_BY_EXT = {
    js: "file_type_js", mjs: "file_type_js", cjs: "file_type_js",
    jsx: "file_type_reactjs", ts: "file_type_typescript",
    tsx: "file_type_reactts", py: "file_type_python", pyw: "file_type_python",
    rb: "file_type_ruby", rs: "file_type_rust", go: "file_type_go",
    java: "file_type_java", kt: "file_type_kotlin", kts: "file_type_kotlin",
    swift: "file_type_swift", c: "file_type_c", h: "file_type_cheader",
    hpp: "file_type_cpp", hh: "file_type_cpp", hxx: "file_type_cpp",
    cpp: "file_type_cpp", cc: "file_type_cpp", cxx: "file_type_cpp",
    cs: "file_type_csharp", php: "file_type_php", pl: "file_type_perl",
    pm: "file_type_perl", lua: "file_type_lua", r: "file_type_r",
    scala: "file_type_scala", hs: "file_type_haskell", ex: "file_type_elixir",
    exs: "file_type_elixir", erl: "file_type_erlang", dart: "file_type_dart",
    html: "file_type_html", htm: "file_type_html", css: "file_type_css",
    scss: "file_type_scss", sass: "file_type_sass", less: "file_type_less",
    json: "file_type_json", yaml: "file_type_yaml", yml: "file_type_yaml",
    toml: "file_type_toml", xml: "file_type_xml", ini: "file_type_ini",
    cfg: "file_type_config", conf: "file_type_config", md: "file_type_markdown",
    markdown: "file_type_markdown", txt: "file_type_text", text: "file_type_text",
    log: "file_type_log", sql: "file_type_sql", sh: "file_type_shell",
    bash: "file_type_shell", zsh: "file_type_shell", ps1: "file_type_powershell",
    gradle: "file_type_gradle", svg: "file_type_svg", png: "file_type_image",
    jpg: "file_type_image", jpeg: "file_type_image", gif: "file_type_image",
    webp: "file_type_image", bmp: "file_type_image", ico: "file_type_image",
    mp3: "file_type_audio", wav: "file_type_audio", flac: "file_type_audio",
    ogg: "file_type_audio", mp4: "file_type_video", mov: "file_type_video",
    mkv: "file_type_video", webm: "file_type_video", pdf: "file_type_pdf",
    zip: "file_type_zip", tar: "file_type_zip", gz: "file_type_zip",
    "7z": "file_type_zip", rar: "file_type_zip", ttf: "file_type_font",
    otf: "file_type_font", woff: "file_type_font", woff2: "file_type_font",
    exe: "file_type_binary", bin: "file_type_binary", o: "file_type_binary",
    a: "file_type_binary", so: "file_type_binary", dll: "file_type_binary",
    key: "file_type_key", pem: "file_type_key", crt: "file_type_cert",
    cert: "file_type_cert", cer: "file_type_cert", cmake: "file_type_cmake",
  };

  function fileTypeIconName(fileName) {
    const lower = String(fileName || "").toLowerCase();
    if (FILE_ICON_BY_NAME[lower]) return FILE_ICON_BY_NAME[lower];
    const dot = lower.lastIndexOf(".");
    if (dot >= 0) {
      const ext = lower.slice(dot + 1);
      if (FILE_ICON_BY_EXT[ext]) return FILE_ICON_BY_EXT[ext];
    }
    return "default_file";
  }

  // <img> to a copied vscode-icons SVG, so the web file rows use the same icons
  // as the Qt desktop browser. Directories use the folder icon; the onerror
  // fallback covers the few Qt names we don't ship an SVG for (mirrors Qt's
  // "verify the file exists, else default_file"). The icon name comes from a
  // fixed allow-list, so it is safe to interpolate unescaped.
  function fileIconHtml(entry, cls = "h-4 w-4 shrink-0") {
    const isTree = entry?.type === "tree";
    const name = isTree ? "default_folder" : fileTypeIconName(entry?.name || "");
    const fallback = isTree ? "default_folder" : "default_file";
    const onErr = name === fallback
      ? ""
      : ` onerror="this.onerror=null;this.src='/assets/file-icons/${fallback}.svg'"`;
    return `<img src="/assets/file-icons/${name}.svg" alt="" aria-hidden="true" class="${cls}"${onErr} />`;
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

  function formatServeSpeed(ms) {
    const value = Number(ms);
    if (!Number.isFinite(value) || value < 0) return "";
    return value < 1000 ? `${Math.round(value)} ms` : `${(value / 1000).toFixed(1)} s`;
  }

  function formatUsd(value) {
    const number = Number(value);
    return `$${(Number.isFinite(number) ? number : 0).toFixed(2)}`;
  }

  function normalizedCount(value) {
    const number = Number(value);
    return Number.isFinite(number) && number >= 0 ? number : null;
  }

  function tabCountLabel(value) {
    const number = normalizedCount(value);
    return number === null ? "..." : formatCount(number);
  }

  function cloneUrl(repo) {
    const owner = encodeURIComponent(repo.owner || "");
    const name = encodeURIComponent(repo.name || "");
    return `${location.origin}/${owner}/${name}`;
  }

  function repoKey(repo) {
    return `${repo.owner || ""}/${repo.name || ""}`;
  }

  function normalizeRepoSegment(value) {
    const text = String(value || "").trim();
    return /^[A-Za-z0-9._:-]+$/.test(text) ? text : "";
  }

  function safeDecodeURIComponent(value) {
    try {
      return decodeURIComponent(value);
    } catch (_) {
      return String(value || "");
    }
  }

  function parseCloneUrlIdentity(value) {
    const raw = String(value || "").trim();
    if (!raw) return null;
    let path = raw;
    try {
      if (/^[a-z][a-z0-9+.-]*:\/\//i.test(path)) {
        path = new URL(path).pathname;
      } else if (path.startsWith("/") && typeof location !== "undefined") {
        path = new URL(path, location.origin).pathname;
      } else if (path.startsWith("git@") && path.includes(":")) {
        path = path.slice(path.indexOf(":") + 1);
      }
    } catch (_) {}
    path = path.split("?")[0].split("#")[0].replace(/^\/+|\/+$/g, "");
    const parts = path.split("/").filter(Boolean);
    if (parts.length < 2) return null;
    const owner = normalizeRepoSegment(safeDecodeURIComponent(parts[parts.length - 2] || ""));
    let name = normalizeRepoSegment(safeDecodeURIComponent(parts[parts.length - 1] || ""));
    if (name.toLowerCase().endsWith(".git")) {
      name = normalizeRepoSegment(name.slice(0, -4));
    }
    return owner && name ? { owner, name } : null;
  }

  function repoCanonicalIdentity(repo) {
    const source = String(repo?.source || "").trim();
    if (source === "remote-clone") {
      const parsed = parseCloneUrlIdentity(repo?.cloneUrl);
      if (parsed) return parsed;
    }
    return {
      owner: String(repo?.owner || "").trim(),
      name: String(repo?.name || "").trim(),
    };
  }

  function repoCanonicalKey(repo) {
    const id = repoCanonicalIdentity(repo);
    return `${id.owner || ""}/${id.name || ""}`;
  }

  function repoAliasKeys(members, canonical) {
    const aliases = new Set();
    const add = (key) => {
      const text = String(key || "").trim().toLowerCase();
      if (text && text.includes("/")) aliases.add(text);
    };
    add(repoKey(canonical));
    add(repoCanonicalKey(canonical));
    (members || []).forEach((member) => {
      add(repoKey(member));
      add(repoCanonicalKey(member));
    });
    return [...aliases];
  }

  function canonicalRepoFromGroup(primary, members) {
    const all = Array.isArray(members) ? members : [];
    const local = all.find((m) => (m.source || "").trim() === "local-node");
    if (local) {
      return { ...local, _repoAliases: repoAliasKeys(all, local) };
    }
    const mirror = all.find((m) =>
      (m.source || "").trim() === "remote-clone" && parseCloneUrlIdentity(m.cloneUrl));
    const base = mirror || primary || all[0] || {};
    const id = repoCanonicalIdentity(base);
    const canonical = {
      ...base,
      owner: id.owner || base.owner || "",
      name: id.name || base.name || "",
      canonicalOwner: id.owner || "",
      canonicalName: id.name || "",
      servingOwner: base.owner || "",
      servingName: base.name || "",
    };
    if ((base.owner || "") !== canonical.owner || (base.name || "") !== canonical.name) {
      canonical.liveHost = false;
      canonical.cloneOnline = Boolean(base.cloneOnline ?? base.liveHost);
    }
    canonical._repoAliases = repoAliasKeys(all, canonical);
    return canonical;
  }

  function repoMatchesKey(repo, key) {
    const wanted = String(key || "").trim().toLowerCase();
    if (!wanted) return false;
    if (repoKey(repo).toLowerCase() === wanted || repoCanonicalKey(repo).toLowerCase() === wanted) {
      return true;
    }
    return Array.isArray(repo?._repoAliases) && repo._repoAliases.includes(wanted);
  }

  function repoGroupKey(repo) {
    const root = (repo?.rootCommit || "").trim();
    return root ? "root:" + root : "name:" + (repo?.name || "").trim().toLowerCase();
  }

  function groupRepositories(repos) {
    const groups = new Map();
    for (const repo of repos) {
      const key = repoGroupKey(repo);
      if (!groups.has(key)) groups.set(key, []);
      groups.get(key).push(repo);
    }
    // Fold empty-root name-keyed mirrors into rooted groups (same logic as worker).
    const rootedByName = new Map();
    for (const [key, members] of groups) {
      if (!key.startsWith("root:")) continue;
      for (const m of members) {
        const n = (m.name || "").trim().toLowerCase();
        if (n && !rootedByName.has(n)) rootedByName.set(n, key);
      }
    }
    for (const [key, members] of [...groups]) {
      if (!key.startsWith("name:")) continue;
      const target = rootedByName.get(key.slice("name:".length));
      if (target && groups.has(target)) {
        groups.get(target).push(...members);
        groups.delete(key);
      }
    }
    return [...groups.values()].map((members) => {
      const sorted = [...members].sort((a, b) => {
        if (!!b.liveHost !== !!a.liveHost) return (b.liveHost ? 1 : 0) - (a.liveHost ? 1 : 0);
        return (b.lastSync || 0) > (a.lastSync || 0) ? 1 : -1;
      });
      const group = { primary: sorted[0], members: sorted };
      group.source = canonicalRepoFromGroup(group.primary, group.members);
      return group;
    });
  }

  function sourceOfTruth(group) {
    const canonical = group.source || canonicalRepoFromGroup(group.primary, group.members);
    return (canonical.owner || canonical.name)
      ? canonical
      : group.members.find((m) => (m.source || "").trim() === "local-node") || group.primary;
  }

  function repoApiBase(repo) {
    return `/api/repo/${encodeURIComponent(repo.owner || "")}/${encodeURIComponent(repo.name || "")}`;
  }

  function repoDataVersion(repo) {
    return String(
      repo?.updatedAt || repo?.lastSync || repo?.commit || repo?.stateHash || "",
    ).trim();
  }

  function repoPathUrl(repo, kind = "tree", path = "") {
    const owner = encodeURIComponent(repo.owner || "");
    const name = encodeURIComponent(repo.name || "");
    const suffix = path ? `/${kind}/${path.split("/").map(encodeURIComponent).join("/")}` : "";
    return `/${owner}/${name}${suffix}`;
  }

  // Feature-tab route segments (mirrors 404.html's `featureTabs` list) - tells
  // a tab route (e.g. /owner/repo/issues) apart from a tree/blob code deep link.
  const REPO_TAB_ROUTES = ["commits", "insights", "releases", "issues", "projects", "pulls", "discussions", "mirrors"];

  // The owner-only "Agents" tab (adhoc #182) is only ever a recognized route
  // for the account that can actually see it - sessionCanAssignAgent gates it
  // the same way it gates the "Assign to agent" issue checkbox (owner or
  // admin). A non-owner deep-linking /owner/repo/agents must NOT recognize it
  // as a tab route (it falls through to the harmless tree/blob path instead),
  // so the tab is never even addressable, let alone clickable, for them.
  function repoTabRoutesFor(repo) {
    return repo && sessionCanAssignAgent(repo)
      ? REPO_TAB_ROUTES.concat(["agents"])
      : REPO_TAB_ROUTES;
  }

  // Splits the current path into segments, stripping a leading /dashboard
  // route prefix (e.g. /dashboard/owner/repo/issues -> ["owner","repo","issues"])
  // so repo-route parsing sees the same clean owner/repo/tab shape regardless
  // of whether the browser landed here directly or via the 404.html bounce.
  function repoRouteParts() {
    let parts = location.pathname.split("/").filter(Boolean);
    if (parts[0] === "dashboard") parts = parts.slice(1);
    return parts;
  }

  function requestedRepoKey() {
    const params = new URLSearchParams(location.search);
    const value = params.get("repo");
    if (value && value.includes("/")) return decodeURIComponent(value);
    const parts = repoRouteParts();
    if (parts.length >= 2 && !["api", "assets", "dashboard", "docs", "blogs", "blog", "login", "signup", "network", "desktop", "about", "careers", "changelog", "privacy", "terms"].includes(parts[0])) {
      return `${decodeURIComponent(parts[0])}/${decodeURIComponent(parts[1])}`;
    }
    return "";
  }

  // Pushes a new history entry for real in-app navigations (opening a repo,
  // browsing into a folder/file, leaving a repo for another section) so the
  // browser Back/Forward buttons step through them one at a time instead of
  // exiting the app entirely. No-ops when already on that URL (e.g. while
  // restoring state from a popstate event) to avoid piling up duplicate
  // entries that would otherwise make Back a no-op.
  function navigateHistory(url) {
    if (`${location.pathname}${location.search}` === url) return;
    window.history.pushState(null, "", url);
  }

  function cloneJson(value) {
    if (typeof structuredClone === "function") return structuredClone(value);
    return JSON.parse(JSON.stringify(value));
  }

  function fetchJsonCacheTtl(path) {
    if (path === "/api/repositories") return 15000;
    if (path === "/api/network/overview") return 20000;
    if (path === "/api/version") return 60000;
    return 0;
  }

  function cacheBustedPath(path) {
    const url = new URL(path, location.origin);
    url.searchParams.set("_", String(Date.now()));
    return `${url.pathname}${url.search}${url.hash}`;
  }

  async function fetchJson(path, options = {}) {
    const fresh = options.fresh === true;
    const baseTtl = fetchJsonCacheTtl(path);
    const ttl = fresh ? 0 : baseTtl;
    if (!state.fetchJsonInflight) state.fetchJsonInflight = {};
    if (!state.fetchJsonCache) state.fetchJsonCache = {};
    if (fresh) delete state.fetchJsonCache[path];
    const now = Date.now();
    const cached = ttl ? state.fetchJsonCache[path] : null;
    if (cached && cached.expiresAt > now) return cloneJson(cached.data);
    const requestPath = fresh ? cacheBustedPath(path) : path;
    const inflightKey = fresh ? requestPath : path;
    if (state.fetchJsonInflight[inflightKey]) {
      return cloneJson(await state.fetchJsonInflight[inflightKey]);
    }

    const pending = (async () => {
      // Only explicit fresh loads bypass HTTP caching; plain fetches send no
      // cache-buster and no cache-control override so the browser can reuse
      // responses within the server's max-age (request budget).
      const noStore = fresh;
      // Attach the account session as a bearer token when logged in. Endpoints
      // that expose per-account data (e.g. /api/notifications) require it;
      // public endpoints simply ignore it. Same-origin only.
      const token = state.session?.sessionToken || "";
      const headers = noStore
        ? { accept: "application/json", "cache-control": "no-cache" }
        : { accept: "application/json" };
      if (token) headers.authorization = "Bearer " + token;
      const response = await fetch(requestPath, {
        cache: noStore ? "no-store" : "default",
        headers,
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) {
        throw new Error(data.error || `HTTP ${response.status}`);
      }
      if (ttl || (fresh && baseTtl)) {
        state.fetchJsonCache[path] = {
          data: cloneJson(data),
          expiresAt: Date.now() + (ttl || baseTtl),
        };
      }
      return data;
    })();
    state.fetchJsonInflight[inflightKey] = pending;
    try {
      return cloneJson(await pending);
    } finally {
      if (state.fetchJsonInflight[inflightKey] === pending) delete state.fetchJsonInflight[inflightKey];
    }
  }

  // ---- Web issue authoring (signed inbox submissions) ------------------------
  // A logged-in web user files an issue the same way a mirror node does: a signed
  // "open" event POSTed to the repo's inbox for the maintainer to drain. The
  // browser holds no desktop key, so it keeps a persistent Ed25519 identity of
  // its own; the logged-in node name rides along as authorName for display.
  const ISSUE_TEXT_ENCODER = new TextEncoder();
  const WEB_ISSUE_KEY_STORAGE = "forkmesh.issueKey";
  // Attached images are embedded as base64 data: URLs inside the issue body
  // text itself (no separate upload channel), so they share the body's 64 KB
  // server-side cap (MAX_ISSUE_BYTES in the worker). Kept well under that so a
  // couple of small screenshots plus title/description text still fit.
  const ISSUE_IMAGE_MAX_COUNT = 4;
  const ISSUE_IMAGE_MAX_BYTES = 40 * 1024;
  const ISSUE_IMAGE_MAX_TOTAL_BYTES = 45 * 1024;
  // Raw files can be much bigger than the final embedded size - anything under
  // this is accepted into the crop/compress modal rather than rejected outright.
  const ISSUE_IMAGE_RAW_MAX_BYTES = 20 * 1024 * 1024;
  // Screenshots pasted/attached onto a "start agent" prompt (adhoc #78). More
  // generous than issue images so a screenshot stays legible for the agent, but
  // still bounded to keep the queued-prompt row (and /api/sync payload) modest;
  // mirrors the worker's MAX_AGENT_PROMPT_IMAGE(S)* caps.
  const AGENT_IMAGE_MAX_COUNT = 3;
  const AGENT_IMAGE_MAX_BYTES = 1000 * 1024;
  const AGENT_IMAGE_MAX_TOTAL_BYTES = 1700 * 1024;

  function readAsDataUrl(fileOrBlob) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(String(reader.result || ""));
      reader.onerror = () => reject(reader.error || new Error("read_failed"));
      reader.readAsDataURL(fileOrBlob);
    });
  }

  function canvasToBlob(canvas, type, quality) {
    return new Promise((resolve) => canvas.toBlob(resolve, type, quality));
  }

  // Intermediary crop/compress step for images too large to embed directly.
  // The user drags a crop box, then the modal auto-retries JPEG re-encoding at
  // shrinking quality/scale ("multiple takes") until the result fits maxBytes,
  // or lets the user redraw a smaller crop and retry. Resolves to
  // { dataUrl, size } on confirm, or null if cancelled.
  function openImageResizeModal(file, maxBytes) {
    return new Promise((resolve) => {
      const objectUrl = URL.createObjectURL(file);
      let settled = false;
      const finish = (result) => {
        if (settled) return;
        settled = true;
        document.removeEventListener("keydown", onKeydown);
        URL.revokeObjectURL(objectUrl);
        overlay.remove();
        resolve(result);
      };
      const onKeydown = (event) => {
        if (event.key === "Escape") { event.preventDefault(); finish(null); }
      };

      const overlay = document.createElement("div");
      overlay.className = "fixed inset-0 z-50 flex items-start justify-center overflow-auto bg-background/80 p-4 pt-10 backdrop-blur-sm";
      overlay.setAttribute("role", "dialog");
      overlay.setAttribute("aria-modal", "true");
      overlay.setAttribute("aria-label", "Resize image");
      overlay.innerHTML = `
        <div class="flex max-h-full w-full max-w-2xl flex-col overflow-hidden rounded-lg border border-border bg-card shadow-xl">
          <div class="flex items-center justify-between gap-3 border-b border-border px-4 py-3">
            <span class="min-w-0 truncate text-sm font-semibold text-foreground">Resize "${escapeHtml(file.name)}"</span>
            <button type="button" data-resize-cancel class="shrink-0 rounded-md border border-border px-2 py-1 text-xs text-muted-foreground hover:bg-secondary hover:text-foreground">Cancel</button>
          </div>
          <div class="grid gap-3 overflow-auto p-4">
            <p class="text-xs text-muted-foreground">Drag on the image to crop it, then it's auto-compressed to fit. Attached images share the issue's size limit - redraw a smaller crop if it still doesn't fit.</p>
            <div data-resize-stage class="relative mx-auto inline-block max-h-[24rem] max-w-full touch-none select-none overflow-hidden rounded-md border border-border bg-secondary/30">
              <img data-resize-image src="${objectUrl}" class="block max-h-[24rem] max-w-full select-none" alt="" draggable="false" />
              <div data-resize-crop class="absolute hidden border-2 border-primary bg-primary/10"></div>
            </div>
            <div class="flex flex-wrap items-center justify-between gap-2 text-xs">
              <span data-resize-status class="text-muted-foreground">Estimating size…</span>
              <button type="button" data-resize-reset class="rounded-md border border-border px-2 py-1 text-muted-foreground hover:bg-secondary hover:text-foreground">Reset crop</button>
            </div>
          </div>
          <div class="flex items-center justify-end gap-2 border-t border-border px-4 py-3">
            <button type="button" data-resize-add disabled class="inline-flex h-8 items-center gap-2 rounded-md bg-primary px-3 text-xs font-medium text-primary-foreground hover:bg-primary/90 disabled:cursor-not-allowed disabled:opacity-50"><i data-lucide="check" class="h-3.5 w-3.5"></i>Add image</button>
          </div>
        </div>`;
      document.body.appendChild(overlay);
      window.lucide?.createIcons();
      document.addEventListener("keydown", onKeydown);

      const stage = overlay.querySelector("[data-resize-stage]");
      const modalImg = overlay.querySelector("[data-resize-image]");
      const cropDiv = overlay.querySelector("[data-resize-crop]");
      const status = overlay.querySelector("[data-resize-status]");
      const addButton = overlay.querySelector("[data-resize-add]");
      const resetButton = overlay.querySelector("[data-resize-reset]");
      const cancelButton = overlay.querySelector("[data-resize-cancel]");

      let cropRectCss = null;
      let dragStart = null;
      let pendingBlob = null;
      let compressToken = 0;

      const clamp = (value, min, max) => Math.min(Math.max(value, min), max);

      const updateCropVisual = () => {
        if (!cropRectCss) {
          cropDiv.classList.add("hidden");
          return;
        }
        cropDiv.classList.remove("hidden");
        cropDiv.style.left = `${cropRectCss.left}px`;
        cropDiv.style.top = `${cropRectCss.top}px`;
        cropDiv.style.width = `${cropRectCss.width}px`;
        cropDiv.style.height = `${cropRectCss.height}px`;
      };

      const attemptCompress = async () => {
        if (!modalImg.naturalWidth) return;
        const attemptId = (compressToken += 1);
        pendingBlob = null;
        addButton.disabled = true;
        status.className = "text-muted-foreground";
        status.textContent = "Compressing…";

        const scaleX = modalImg.naturalWidth / modalImg.clientWidth;
        const scaleY = modalImg.naturalHeight / modalImg.clientHeight;
        let sx = 0;
        let sy = 0;
        let sw = modalImg.naturalWidth;
        let sh = modalImg.naturalHeight;
        if (cropRectCss) {
          sx = Math.round(cropRectCss.left * scaleX);
          sy = Math.round(cropRectCss.top * scaleY);
          sw = Math.max(1, Math.round(cropRectCss.width * scaleX));
          sh = Math.max(1, Math.round(cropRectCss.height * scaleY));
        }

        const qualities = [0.82, 0.65, 0.5, 0.35, 0.22, 0.12];
        let scale = 1;
        for (let round = 0; round < 6; round += 1) {
          const outW = Math.max(24, Math.round(sw * scale));
          const outH = Math.max(24, Math.round(sh * scale));
          const canvas = document.createElement("canvas");
          canvas.width = outW;
          canvas.height = outH;
          const ctx = canvas.getContext("2d");
          ctx.fillStyle = "#fff";
          ctx.fillRect(0, 0, outW, outH);
          ctx.drawImage(modalImg, sx, sy, sw, sh, 0, 0, outW, outH);
          for (const quality of qualities) {
            const blob = await canvasToBlob(canvas, "image/jpeg", quality);
            if (attemptId !== compressToken) return;
            if (blob && blob.size <= maxBytes) {
              pendingBlob = blob;
              status.className = "text-primary";
              status.textContent = `Ready - ${formatSize(blob.size)} (fits under ${formatSize(maxBytes)}).`;
              addButton.disabled = false;
              return;
            }
          }
          scale *= 0.65;
        }
        status.className = "text-destructive";
        status.textContent = "Still too large after compressing - drag to crop a smaller area and it'll retry.";
      };

      stage.addEventListener("pointerdown", (event) => {
        const rect = stage.getBoundingClientRect();
        dragStart = {
          x: clamp(event.clientX - rect.left, 0, rect.width),
          y: clamp(event.clientY - rect.top, 0, rect.height),
        };
        stage.setPointerCapture(event.pointerId);
      });
      stage.addEventListener("pointermove", (event) => {
        if (!dragStart) return;
        const rect = stage.getBoundingClientRect();
        const x = clamp(event.clientX - rect.left, 0, rect.width);
        const y = clamp(event.clientY - rect.top, 0, rect.height);
        cropRectCss = {
          left: Math.min(dragStart.x, x),
          top: Math.min(dragStart.y, y),
          width: Math.abs(x - dragStart.x),
          height: Math.abs(y - dragStart.y),
        };
        updateCropVisual();
      });
      stage.addEventListener("pointerup", () => {
        if (!dragStart) return;
        dragStart = null;
        if (!cropRectCss || cropRectCss.width < 8 || cropRectCss.height < 8) cropRectCss = null;
        updateCropVisual();
        attemptCompress();
      });

      resetButton.addEventListener("click", () => {
        cropRectCss = null;
        updateCropVisual();
        attemptCompress();
      });
      cancelButton.addEventListener("click", () => finish(null));
      addButton.addEventListener("click", async () => {
        if (!pendingBlob) return;
        const dataUrl = await readAsDataUrl(pendingBlob);
        finish({ dataUrl, size: pendingBlob.size });
      });

      modalImg.addEventListener("load", () => attemptCompress());
      modalImg.addEventListener("error", () => finish(null));
      if (modalImg.complete && modalImg.naturalWidth) attemptCompress();
    });
  }

  function bytesToB64url(bytes) {
    const arr = new Uint8Array(bytes);
    let bin = "";
    for (let i = 0; i < arr.length; i += 1) bin += String.fromCharCode(arr[i]);
    return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  }

  async function sha256HexLower(text) {
    const digest = await crypto.subtle.digest("SHA-256", ISSUE_TEXT_ENCODER.encode(text));
    return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, "0")).join("");
  }

  async function getWebIssueKey() {
    let stored = null;
    try { stored = JSON.parse(localStorage.getItem(WEB_ISSUE_KEY_STORAGE) || "null"); } catch (_) {}
    if (stored && stored.jwk && stored.pub) {
      try {
        const privateKey = await crypto.subtle.importKey("jwk", stored.jwk, { name: "Ed25519" }, false, ["sign"]);
        return { privateKey, pub: stored.pub };
      } catch (_) { /* fall through and mint a fresh key */ }
    }
    const pair = await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]);
    const rawPub = await crypto.subtle.exportKey("raw", pair.publicKey);
    const jwk = await crypto.subtle.exportKey("jwk", pair.privateKey);
    const pub = bytesToB64url(rawPub);
    try { localStorage.setItem(WEB_ISSUE_KEY_STORAGE, JSON.stringify({ jwk, pub })); } catch (_) {}
    const privateKey = await crypto.subtle.importKey("jwk", jwk, { name: "Ed25519" }, false, ["sign"]);
    return { privateKey, pub };
  }

  // Issue #379: offline issues an owner filed while their source-of-truth node
  // was down (but a mirror was still serving the repo). They live only in the
  // relay's inbox until the node returns and drains them, so we also keep a
  // local copy per repo - keyed here - so they "show up fully" across reloads
  // instead of vanishing the moment the mirror-loaded list replaces the
  // optimistic, session-only placeholder.
  const PENDING_ISSUES_STORAGE = "forkmesh.pendingIssues";

  function pendingIssuesRepoKey(repo) {
    return `${String(repo?.owner || "").toLowerCase()}/${String(repo?.name || "").toLowerCase()}`;
  }

  function readPendingIssueStore() {
    try { return JSON.parse(localStorage.getItem(PENDING_ISSUES_STORAGE) || "{}") || {}; }
    catch (_) { return {}; }
  }

  function writePendingIssueStore(store) {
    try { localStorage.setItem(PENDING_ISSUES_STORAGE, JSON.stringify(store)); } catch (_) {}
  }

  function loadPendingIssues(repo) {
    const list = readPendingIssueStore()[pendingIssuesRepoKey(repo)];
    return Array.isArray(list) ? list : [];
  }

  function savePendingIssue(repo, item) {
    const store = readPendingIssueStore();
    const key = pendingIssuesRepoKey(repo);
    const list = Array.isArray(store[key]) ? store[key] : [];
    store[key] = [item, ...list].slice(0, 50);
    writePendingIssueStore(store);
  }

  // Drop any locally-held pending issues whose title now appears in the mirror
  // tree: the owner's node has come back and drained them, so the real numbered
  // issue served from the mirror wins and the local placeholder retires.
  function reconcilePendingIssues(repo, mirrorIssues) {
    const list = loadPendingIssues(repo);
    if (!list.length) return list;
    const drained = new Set(
      (mirrorIssues || []).map((issue) => String(issue.title || "").trim()));
    const kept = list.filter((item) => !drained.has(String(item.title || "").trim()));
    if (kept.length === list.length) return list;
    const store = readPendingIssueStore();
    store[pendingIssuesRepoKey(repo)] = kept;
    writePendingIssueStore(store);
    return kept;
  }

  // Mirrors IssueStore::contentForSigning + canonicalString and the desktop's
  // inbox POST (verify_issue_event in the worker). New issues are signed with
  // number 0; the maintainer assigns the durable number on drain.
  async function submitWebIssue(repo, title, body, assignAgent = false, agentModel = "", agentProvider = "") {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const cleanBody = String(body || "").replace(/[\r\n]+$/, "");
    // open event content = title \0 body \0 attachments(joined by ","; empty here)
    const NUL = String.fromCharCode(0);
    const content = title + NUL + cleanBody + NUL;
    const contentHash = await sha256HexLower(content);
    const canonical = `forkmesh-issue-event-v1\nopen\n0\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const event = {
      type: "open",
      id: "open-web-" + ts,
      title,
      body: cleanBody,
      attachments: [],
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    const payload = {
      owner: repo.owner,
      repo: repo.name,
      number: 0,
      titleIfNew: title,
      event,
      meta: { labels: [], milestone: "", priority: 0, assignees: [], wantsAgent: Boolean(assignAgent), model: assignAgent ? String(agentModel || "") : "", provider: assignAgent ? String(agentProvider || "") : "" },
    };
    // When assignAgent is true, include the account session so the server can
    // verify the caller really is the repo owner or an admin (adhoc #225). The
    // session token is the proof; ownerAccount stays only as a display hint.
    if (assignAgent) {
      payload.ownerAccount = state.session?.nodeName || "";
      payload.sessionToken = state.session?.sessionToken || "";
    }
    const response = await fetch(`${repoApiBase(repo)}/issues`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  // Mirrors DiscussionStore::contentForSigning's "comment" case (just the
  // reply body) and the desktop's discussion inbox POST (verify_discussion_event
  // in the worker). Replies are signed against the discussion's real number -
  // unlike a new discussion's "open" event, they don't use the placeholder 0.
  async function submitWebDiscussionComment(repo, number, body) {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const cleanBody = String(body || "").replace(/[\r\n]+$/, "");
    const contentHash = await sha256HexLower(cleanBody);
    const canonical = `forkmesh-discussion-event-v1\ncomment\n${number}\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const event = {
      type: "comment",
      id: "comment-web-" + ts,
      body: cleanBody,
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    const payload = { owner: repo.owner, repo: repo.name, number, event };
    const response = await fetch(`${repoApiBase(repo)}/discussions`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  // Mirrors PullStore::contentForSigning's per-type field order (comment,
  // review, line-comment, thread-comment, thread-reply, thread-state,
  // suggestion-state) and the desktop's pull inbox POST
  // (verify_pull_comment_event/pull_comment_content in the worker). Covers
  // every conversation-shaped pull event; opening a brand new pull request is
  // a distinct signed shape (submitWebPullOpen, below).
  async function submitWebPullEvent(repo, number, type, fields = {}) {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const NUL = String.fromCharCode(0);
    const cleanBody = String(fields.body || "").replace(/[\r\n]+$/, "");
    let content;
    if (type === "comment") {
      content = cleanBody;
    } else if (type === "review") {
      content = [fields.state || "", cleanBody].join(NUL);
    } else if (type === "line-comment") {
      content = [fields.path || "", fields.side || "", String(fields.line || 0), cleanBody].join(NUL);
    } else if (type === "thread-comment") {
      content = [
        fields.threadId || "", fields.path || "", fields.side || "",
        String(fields.lineStart || 0), String(fields.lineEnd || 0),
        cleanBody, fields.suggestionPatch || "",
      ].join(NUL);
    } else if (type === "thread-reply") {
      content = [fields.threadId || "", fields.parentId || "", cleanBody].join(NUL);
    } else if (type === "thread-state") {
      content = [fields.threadId || "", fields.state || "", cleanBody].join(NUL);
    } else if (type === "suggestion-state") {
      content = [fields.threadId || "", fields.state || "", fields.appliedCommit || "", cleanBody].join(NUL);
    } else {
      throw new Error("unsupported_pull_event_type");
    }
    const contentHash = await sha256HexLower(content);
    const canonical = `forkmesh-pull-comment-v1\n${type}\n${number}\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const event = {
      type,
      id: `${type}-web-${ts}`,
      body: cleanBody,
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    for (const key of ["state", "path", "side", "line", "threadId", "parentId", "lineStart", "lineEnd", "suggestionPatch", "appliedCommit"]) {
      if (fields[key] !== undefined && fields[key] !== "") event[key] = fields[key];
    }
    const payload = { owner: repo.owner, repo: repo.name, number, event };
    const response = await fetch(`${repoApiBase(repo)}/pulls`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  async function submitWebPullComment(repo, number, body) {
    return submitWebPullEvent(repo, number, "comment", { body });
  }

  async function submitWebPullReview(repo, number, reviewState, body) {
    return submitWebPullEvent(repo, number, "review", { state: reviewState, body });
  }

  // Mirrors PullStore::canonicalString for a new pull (verify_pull_event in
  // the worker): title/base/head/patch, with an empty patch for a
  // branch-referencing submission from the web - the desktop reconstructs the
  // diff on drain (see renderRepoPullPatch's "Branch-backed PRs are
  // reconstructed by the desktop client" copy).
  async function submitWebPullOpen(repo, title, body, base, head) {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const cleanBody = String(body || "").replace(/[\r\n]+$/, "");
    const NUL = String.fromCharCode(0);
    const patch = "";
    const content = [title, base, head, patch].join(NUL);
    const contentHash = await sha256HexLower(content);
    const canonical = `forkmesh-pull-event-v1\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const pull = {
      title,
      body: cleanBody,
      base,
      head,
      patch,
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    const payload = { owner: repo.owner, repo: repo.name, pull };
    const response = await fetch(`${repoApiBase(repo)}/pulls`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  async function handleDiscussionReplySubmit(repo, form) {
    if (!repo || !form) return;
    const number = Number(form.dataset.repoDiscussionReplyNumber || 0);
    const bodyInput = form.querySelector("[data-repo-discussion-reply-body]");
    const submit = form.querySelector("[data-repo-discussion-reply-submit]");
    const hint = form.querySelector("[data-repo-discussion-reply-hint]");
    const setHint = (text, tone) => {
      if (hint) hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      if (hint) hint.textContent = text;
    };
    const body = String(bodyInput?.value || "").trim();
    if (!number) {
      setHint("This discussion hasn't finished loading yet.", "bad");
      return;
    }
    if (!body) {
      setHint("Write a reply before sending.", "bad");
      bodyInput?.focus();
      return;
    }
    if (submit) submit.disabled = true;
    setHint("Signing and sending…");
    try {
      await submitWebDiscussionComment(repo, number, body);
      const list = form.parentElement?.querySelector("[data-repo-discussion-conversation]");
      if (list) {
        if (list.dataset.empty === "true") list.innerHTML = "";
        list.dataset.empty = "false";
        list.insertAdjacentHTML("beforeend", renderPullConversationEvent({
          type: "comment", authorName: state.session?.nodeName || "you",
          ts: Math.floor(Date.now() / 1000), body,
        }));
      }
      if (bodyInput) bodyInput.value = "";
      if (submit) submit.disabled = false;
      setHint("Reply sent to the maintainer's inbox for review.", "good");
    } catch (error) {
      if (submit) submit.disabled = false;
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "The maintainer's inbox is full. Try again later."
          : code === "author_quota" ? "You've reached the submission limit for this repository."
          : code === "discussion_too_large" ? "The reply is too large - please shorten it."
          : code === "bad_signature" ? "Could not verify the reply's signature."
          : "Could not send the reply. Please try again.",
        "bad");
    }
  }

  function sessionFromAccountPayload(body, base = {}) {
    const nextSession = {
      ...(base || {}),
      nodeName: body.nodeName || body.name || base.nodeName || "",
      email: body.email ?? base.email ?? "",
      status: body.status || base.status || "active",
      pubkey: body.pubkey || base.pubkey || "",
      emailVerified: Object.prototype.hasOwnProperty.call(body, "emailVerified")
        ? Boolean(body.emailVerified)
        : Boolean(base.emailVerified),
      isAdmin: Object.prototype.hasOwnProperty.call(body, "isAdmin")
        ? Boolean(body.isAdmin)
        : Boolean(base.isAdmin),
      adminUrl: body.adminUrl || base.adminUrl || "",
      solana: body.solana || base.solana || "",
      hasPayoutAddress: Object.prototype.hasOwnProperty.call(body, "hasPayoutAddress")
        ? Boolean(body.hasPayoutAddress)
        : Boolean(base.hasPayoutAddress),
      sessionToken: body.sessionToken ?? base.sessionToken ?? "",
      avatarPng: body.avatarPng || "",
      avatarUpdatedAt: Number(body.avatarUpdatedAt) || 0,
      profileBio: body.profileBio ?? base.profileBio ?? "",
      profileAbout: body.profileAbout ?? body.profileReadme ?? base.profileAbout ?? base.profileReadme ?? "",
      profileReadme: body.profileReadme ?? body.profileAbout ?? base.profileReadme ?? base.profileAbout ?? "",
      profileLocation: body.profileLocation ?? base.profileLocation ?? "",
      profileTimezone: body.profileTimezone ?? base.profileTimezone ?? "",
      profileFollowers: Number(body.followers ?? body.profileFollowers ?? base.profileFollowers ?? 0) || 0,
      profileFollowing: Number(body.following ?? body.profileFollowing ?? base.profileFollowing ?? 0) || 0,
      profileMirrorCount: Number(body.mirrorCount ?? body.profileMirrorCount ?? base.profileMirrorCount ?? 0) || 0,
      profilePrivate: Object.prototype.hasOwnProperty.call(body, "profilePrivate")
        ? Boolean(body.profilePrivate)
        : Boolean(base.profilePrivate),
      followersPublic: Object.prototype.hasOwnProperty.call(body, "followersPublic")
        ? Boolean(body.followersPublic)
        : Boolean(base.followersPublic),
      mastodon: body.mastodon ?? base.mastodon ?? "",
      mastodonUrl: body.mastodonUrl ?? base.mastodonUrl ?? "",
      profileLinks: Array.isArray(body.profileLinks)
        ? body.profileLinks
        : (base.profileLinks || []),
      emailNotifications: Object.prototype.hasOwnProperty.call(body, "emailNotifications")
        ? Boolean(body.emailNotifications)
        : Boolean(base.emailNotifications ?? true),
      notificationPreferences: body.notificationPreferences && typeof body.notificationPreferences === "object"
        ? body.notificationPreferences
        : (base.notificationPreferences || {}),
      kind: body.kind || base.kind || "",
      owner: body.owner ?? base.owner ?? "",
      online: Object.prototype.hasOwnProperty.call(body, "online")
        ? Boolean(body.online)
        : Boolean(base.online),
      nodes: Array.isArray(body.nodes) ? body.nodes : (base.nodes || []),
      at: Date.now(),
    };
    if (!Object.prototype.hasOwnProperty.call(body, "avatarPng")) {
      nextSession.avatarPng = base.avatarPng || "";
    }
    if (!Object.prototype.hasOwnProperty.call(body, "avatarUpdatedAt")) {
      nextSession.avatarUpdatedAt = Number(base.avatarUpdatedAt) || 0;
    }
    return nextSession;
  }

  function applyAvatar(container, session) {
    if (!container) return;
    const name = session?.nodeName || session?.email || "ForkMesh";
    const initial = (name[0] || "F").toUpperCase();
    const avatarPng = session?.avatarPng || "";
    const image = container.querySelector("img");
    const initialEl = container.querySelector("[data-avatar-initial]");
    if (image) {
      if (avatarPng) {
        image.src = `data:image/png;base64,${avatarPng}`;
        image.alt = `${name} profile picture`;
      } else {
        image.removeAttribute("src");
        image.alt = "";
      }
      image.classList.toggle("hidden", !avatarPng);
    }
    if (initialEl) {
      initialEl.textContent = initial;
      initialEl.classList.toggle("hidden", Boolean(avatarPng));
    } else if (!image) {
      container.textContent = initial;
    }
    container.classList.toggle("font-mono", !avatarPng);
  }

  async function refreshPublicProfile(session = state.session) {
    return hydrateCanonicalProfile(session);
  }

  async function hydrateCanonicalProfile(session) {
    const nodeName = String(session?.nodeName || "").trim().toLowerCase();
    if (!validNodeName(nodeName)) return session;
    try {
      const body = await fetchJson(`/api/accounts/${encodeURIComponent(nodeName)}`);
      if (body.exists === false) return session;
      const nextSession = sessionFromAccountPayload(body, session);
      writeSession(nextSession);
      renderProfile(nextSession);
      return nextSession;
    } catch (_) {
      return session;
    }
  }
  // Every top-level page is its own document now (/dashboard, /dashboard/repos,
  // /dashboard/network, ...) — navigation between them is a real page load via
  // plain <a href> links, so there is no client-side section router anymore.
  // The only client-routed state left is within-page: repo tabs/tree/blob on
  // the repo page, and the settings sub-tabs below.

  const SETTINGS_SECTIONS = ["public-profile", "account", "appearance", "notifications", "payout", "nodes", "danger"];

  function normalizeSettingsSection(section) {
    return SETTINGS_SECTIONS.includes(section) ? section : "public-profile";
  }

  // The settings sub-tab addressed by the URL (/dashboard/settings/<tab>), so a
  // refresh keeps the tab instead of snapping back to public-profile.
  function settingsSectionFromPath() {
    const parts = location.pathname.split("/").filter(Boolean);
    return normalizeSettingsSection(parts[0] === "dashboard" && parts[1] === "settings" ? parts[2] || "" : "");
  }

  function setSettingsSection(section, { scroll = true, push = false } = {}) {
    const activeSection = normalizeSettingsSection(section);
    if (!state.settingsView) state.settingsView = {};
    state.settingsView.section = activeSection;
    if (push) {
      // Reflect the tab in the URL so refresh/back keep it. public-profile is
      // the default, so it stays on the bare /dashboard/settings URL.
      navigateHistory(activeSection === "public-profile"
        ? "/dashboard/settings"
        : `/dashboard/settings/${activeSection}`);
    }

    $$("[data-settings-section]").forEach((panel) => {
      const active = panel.dataset.settingsSection === activeSection;
      panel.classList.toggle("hidden", !active);
    });

    $$("[data-settings-section-link]").forEach((button) => {
      const active = button.dataset.settingsSectionLink === activeSection;
      button.setAttribute("aria-current", active ? "page" : "false");
      button.className = active
        ? "relative flex h-10 items-center gap-3 rounded-md bg-secondary px-3 pl-4 text-left font-semibold text-foreground"
        : "relative flex h-10 items-center gap-3 rounded-md px-4 text-left text-muted-foreground hover:bg-secondary hover:text-foreground";
      button.querySelector("[data-settings-active-indicator]")?.classList.toggle("hidden", !active);
    });

    const settingsMain = $("[data-settings-main]");
    if (scroll && settingsMain) {
      settingsMain.scrollIntoView({ block: "start", behavior: "smooth" });
    }
  }

  function setMobileSidebarOpen(open) {
    document.body.classList.toggle("dashboard-sidebar-open", open);
    $$("[data-mobile-menu-toggle]").forEach((button) => {
      button.setAttribute("aria-expanded", open ? "true" : "false");
      button.setAttribute("aria-label", open ? "Close dashboard menu" : "Open dashboard menu");
    });
  }

  function closeMobileDrawers() {
    setMobileSidebarOpen(false);
  }

  function currentSection() {
    // The legacy section name is baked into the page document at build time
    // (dashboard_shell.PAGES[page]["section"] -> data-dashboard-section).
    return $("[data-dashboard-root]")?.dataset?.dashboardSection
      || $("[data-view].active")?.dataset?.view
      || "home";
  }

  function renderHeaderContext(section = currentSection()) {
    const headerContext = $("[data-dashboard-header-context]");
    if (!headerContext) return;
    if (section === "profile-overview" || section === "profile-repositories") {
      const renderedName = ($("[data-profile-page-node-name]")?.textContent || "").trim();
      headerContext.textContent = state.session?.nodeName || state.session?.email || renderedName || "Profile";
      return;
    }
    if (section === "profile") {
      headerContext.textContent = "Settings";
      return;
    }
    if (section === "repos") {
      headerContext.textContent = "Repositories";
      return;
    }
    if (section === "network") {
      headerContext.textContent = "Network";
      return;
    }
    if (section === "chat") {
      headerContext.textContent = "Chat";
      return;
    }
    if (section === "explore" && state.selectedRepo) {
      headerContext.textContent = repoKey(state.selectedRepo);
      return;
    }
    headerContext.textContent = "Dashboard";
  }

  // ---- Public-profile mode (/@name) ---------------------------------------
  // The worker serves the SAME prebuilt profile documents at /@name; the
  // profile-page machinery renders whatever profileSubject() returns, so
  // public mode is: fetch the named account's public payload, park it in
  // state.publicProfile, and strip the owner-only chrome.

  function publicProfileNameFromPath() {
    const match = /^\/@([a-z][a-z0-9-]{0,62})(?:\/repositories)?\/?$/
      .exec(location.pathname.toLowerCase());
    return match ? match[1] : "";
  }

  function profileSubject() {
    return state.publicProfile || state.session;
  }

  // On /@name pages the profile markup belongs to the fetched PUBLIC profile;
  // the shared-chrome boot path still calls the profile renderers with the
  // session, which must not overwrite (or briefly flash) the wrong identity.
  function profileMarkupOwnedByPublicProfile(session) {
    const publicName = publicProfileNameFromPath();
    return Boolean(publicName) && session !== state.publicProfile &&
      publicName !== String(session?.nodeName || "").toLowerCase();
  }

  async function loadPublicProfile(name) {
    document.body.classList.add("public-profile-mode");
    let body;
    try {
      const viewer = state.session?.nodeName
        ? "?viewer=" + encodeURIComponent(state.session.nodeName) : "";
      body = await fetchJson("/api/accounts/" + encodeURIComponent(name) + viewer);
    } catch (_) {
      $$("[data-profile-page-node-name]").forEach((el) => {
        el.textContent = "@" + name + " was not found";
      });
      return;
    }
    const profile = sessionFromAccountPayload(body, { nodeName: name });
    profile.nodeName = profile.nodeName || name;
    // Never show a mailbox on someone else's page — the handle is the
    // public identity here.
    profile.email = "@" + profile.nodeName;
    profile.isFollowing = Boolean(body?.social?.isFollowing);
    state.publicProfile = profile;
    renderProfilePage(profile);
    applyPublicProfileChrome(profile);
    // The catalog fetch re-renders repositories + the contribution graph when
    // it lands (renderRepositories -> renderProfileRepositories/Graph), and
    // those all read profileSubject() now.
    renderProfileContributionGraph();
    renderProfileRepositories();
  }

  function applyPublicProfileChrome(profile) {
    const name = profile.nodeName;
    // Tabs point at the public URLs, not the session dashboard pages.
    $$("[data-profile-tabs] a[href='/dashboard/profile']").forEach((a) => {
      a.href = "/@" + encodeURIComponent(name);
    });
    $$("[data-profile-tabs] a[href='/dashboard/profile/repositories']").forEach((a) => {
      a.href = "/@" + encodeURIComponent(name) + "/repositories";
    });
    // Owner-only affordances become a Follow button (or disappear).
    $$("[data-profile-about-edit]").forEach((el) => el.classList.add("hidden"));
    $$("[data-profile-about-owner]").forEach((el) => { el.textContent = name; });
    $$("[data-profile-sidebar-slot] a[href='/dashboard/settings']").forEach((edit) => {
      const wrap = edit.parentElement;
      edit.remove();
      if (!wrap || !state.session?.nodeName ||
          state.session.nodeName.toLowerCase() === name) return;
      const follow = document.createElement("button");
      follow.type = "button";
      follow.setAttribute("data-profile-follow", name);
      follow.className = "flex w-full items-center justify-center rounded-md " +
        "border border-border bg-secondary px-3 py-1.5 text-sm font-semibold " +
        "text-foreground hover:bg-background";
      follow.textContent = profile.isFollowing ? "Following" : "Follow";
      follow.addEventListener("click", async () => {
        const following = follow.textContent === "Following";
        follow.disabled = true;
        try {
          const response = await fetch(
            "/api/accounts/" + encodeURIComponent(name) + "/follow", {
              method: following ? "DELETE" : "POST",
              headers: {
                "Content-Type": "application/json",
                Authorization: "Bearer " + (state.session?.sessionToken || ""),
              },
              body: JSON.stringify({
                sessionToken: state.session?.sessionToken || "",
              }),
            });
          if (response.ok) {
            follow.textContent = following ? "Follow" : "Following";
            const count = $("[data-profile-followers-count]");
            if (count) {
              const current = parseInt(count.textContent, 10) || 0;
              count.textContent = String(Math.max(0, current + (following ? -1 : 1)));
            }
          }
        } catch (_) {}
        follow.disabled = false;
      });
      wrap.append(follow);
    });
  }

  function renderProfile(session) {
    const name = session?.nodeName || session?.email || "My Profile";
    const nameEl = $("[data-dashboard-profile-name]");
    const statusEl = $("[data-dashboard-profile-status]");
    const avatar = $("[data-dashboard-profile-avatar]");
    const adminButton = $("[data-admin-button]");
    const homeName = $("[data-home-user-name]");
    const composeName = $("[data-home-compose-name]");
    const sidebarName = $("[data-sidebar-user-name]");

    if (nameEl) nameEl.textContent = name;
    if (homeName) homeName.textContent = name;
    if (composeName) composeName.textContent = name;
    if (sidebarName) sidebarName.textContent = name;
    if (statusEl) {
      statusEl.textContent = session?.emailVerified
        ? "Email verified"
        : "Verify email in profile";
    }
    applyAvatar(avatar, session);
    applyAvatar($("[data-home-user-avatar]"), session);
    applyAvatar($("[data-home-compose-avatar]"), session);
    if (adminButton) {
      let adminUrl = session?.isAdmin ? (session?.adminUrl || "") : "";
      if (adminUrl && session?.nodeName && !/[?&]admin=/.test(adminUrl)) {
        adminUrl += (adminUrl.includes("?") ? "&" : "?") +
          "admin=" + encodeURIComponent(session.nodeName);
      }
      // Only show the button once we actually have somewhere to send it -
      // an admin session without adminUrl (ADMIN_PATH not picked up from the
      // Worker env yet) would otherwise show a button that links to "#".
      adminButton.classList.toggle("hidden", !adminUrl);
      adminButton.href = adminUrl || "#";
    }
    renderProfileModal(session);
    renderProfilePage(session);
    renderProfileAbout(session);
    renderHeaderContext();
  }

  function dashboardMockRepositoriesEnabled() {
    const value = new URLSearchParams(location.search).get("mockRepos");
    return ["1", "true", "yes"].includes(String(value || "").trim().toLowerCase());
  }

  function dashboardMockRepositories() {
    const now = Date.now();
    const day = 24 * 60 * 60 * 1000;
    return [
      {
        owner: "demo-alice",
        name: "mesh-workbench",
        description: "A busy collaboration workspace with issues, pulls, discussions, releases, and active mirrors.",
        source: "local-node",
        liveHost: true,
        cloneOnline: true,
        isPrivate: false,
        language: "TypeScript",
        license: "Apache-2.0",
        channel: "stable",
        updatedAt: now - day,
        lastSync: now - 38 * 60 * 1000,
        sizeBytes: 18_400_000,
        issueCount: 14,
        openIssues: 14,
        closedIssueCount: 31,
        pullCount: 5,
        openPulls: 5,
        closedPullCount: 18,
        discussionCount: 9,
        commitCount: 428,
        branchCount: 7,
        releaseCount: 4,
        activityWeeks: [0, 1, 3, 2, 5, 1, 0, 4, 6, 2, 8, 3, 5, 9, 4, 2, 7, 10, 6, 5, 12, 8, 4, 9, 13, 7, 15, 10, 8, 12, 16, 11, 9, 14, 18, 12, 16, 20, 13, 15, 19, 17, 12, 10, 14, 9, 8, 11, 7, 6, 4, 8],
      },
      {
        owner: "demo-bravo",
        name: "mobile-mirror-client",
        description: "Mobile-first mirror node shell with offline queueing and handoff screens.",
        source: "remote-clone",
        liveHost: false,
        cloneOnline: true,
        isPrivate: false,
        language: "Swift",
        license: "MIT License",
        channel: "beta",
        updatedAt: now - 4 * day,
        lastSync: now - 2 * 60 * 60 * 1000,
        cloneUrl: `${location.origin}/demo-bravo/mobile-mirror-client.git`,
        sizeBytes: 9_800_000,
        issueCount: 7,
        openIssues: 7,
        closedIssueCount: 12,
        pullCount: 2,
        openPulls: 2,
        closedPullCount: 6,
        discussionCount: 4,
        commitCount: 156,
        branchCount: 4,
        releaseCount: 2,
        activityWeeks: [0, 0, 1, 0, 2, 1, 3, 0, 2, 4, 1, 3, 5, 2, 4, 3, 6, 2, 5, 4, 7, 3, 4, 6, 8, 5, 7, 6, 4, 8, 9, 5, 7, 6, 10, 8, 5, 7, 9, 4, 6, 8, 5, 7, 6, 4, 5, 3, 6, 4, 2, 5],
      },
      {
        owner: "demo-cora",
        name: "security-review-lab",
        description: "Private security review sandbox showing locked repository states and quieter activity.",
        source: "local-node",
        liveHost: false,
        cloneOnline: false,
        isPrivate: true,
        language: "Rust",
        license: "Proprietary",
        channel: "internal",
        updatedAt: now - 13 * day,
        lastSync: now - 8 * day,
        sizeBytes: 4_200_000,
        issueCount: 3,
        openIssues: 3,
        closedIssueCount: 8,
        pullCount: 1,
        openPulls: 1,
        closedPullCount: 3,
        discussionCount: 2,
        commitCount: 64,
        branchCount: 3,
        releaseCount: 0,
        activityWeeks: [0, 0, 0, 1, 0, 0, 2, 0, 1, 0, 3, 1, 0, 2, 0, 1, 3, 0, 2, 1, 0, 4, 1, 0, 2, 0, 3, 1, 0, 2, 4, 1, 0, 2, 1, 3, 0, 1, 2, 0, 3, 1, 0, 2, 0, 1, 3, 0, 1, 0, 2, 0],
      },
    ];
  }

  function setProfileModalOpen(open) {
    const modal = $("[data-profile-modal]");
    if (!modal) return;
    modal.classList.toggle("hidden", !open);
    modal.classList.toggle("flex", open);
    if (open) {
      renderProfileModal(state.session);
      window.setTimeout(() => $("[data-profile-password]")?.focus(), 0);
    }
  }

  function setProfileHint(text, cls) {
    const hint = $("[data-profile-hint]");
    if (!hint) return;
    hint.textContent = text || "";
    hint.className = "min-h-4 text-xs " + (cls === "bad" ? "text-destructive" : cls === "good" ? "text-primary" : "text-muted-foreground");
  }

  function setProfilePageHint(selector, text, cls) {
    const hint = $(selector);
    if (!hint) return;
    hint.textContent = text || "";
    hint.className = "min-h-4 text-xs " + (cls === "bad" ? "text-destructive" : cls === "good" ? "text-primary" : "text-muted-foreground");
  }

  function profilePassword(selector) {
    return ($(selector || "[data-profile-password]")?.value || "").trim();
  }

  function profileTxtValue(session = state.session) {
    const name = String(session?.nodeName || "").trim().toLowerCase();
    return name ? `forkmesh-profile=${name}` : "forkmesh-profile=username";
  }

  function profilePublicUrl(session = profileSubject()) {
    const name = String(session?.nodeName || "").trim().toLowerCase();
    return name ? `${location.origin}/@${name}` : `${location.origin}/@username`;
  }

  const PROFILE_TIMEZONE_FALLBACKS = [
    "UTC",
    "Africa/Cairo",
    "Africa/Johannesburg",
    "America/Anchorage",
    "America/Argentina/Buenos_Aires",
    "America/Bogota",
    "America/Chicago",
    "America/Denver",
    "America/Los_Angeles",
    "America/Mexico_City",
    "America/New_York",
    "America/Phoenix",
    "America/Sao_Paulo",
    "America/Toronto",
    "Asia/Bangkok",
    "Asia/Dubai",
    "Asia/Hong_Kong",
    "Asia/Kolkata",
    "Asia/Seoul",
    "Asia/Singapore",
    "Asia/Tokyo",
    "Australia/Melbourne",
    "Australia/Sydney",
    "Europe/Amsterdam",
    "Europe/Berlin",
    "Europe/London",
    "Europe/Madrid",
    "Europe/Paris",
    "Pacific/Auckland",
  ];

  function profileTimezoneOptions() {
    try {
      if (typeof Intl.supportedValuesOf === "function") {
        const zones = Intl.supportedValuesOf("timeZone");
        if (Array.isArray(zones) && zones.length) {
          return ["UTC", ...zones.filter((zone) => zone !== "UTC")];
        }
      }
    } catch {
      // Use the curated fallback below when browser support is unavailable.
    }
    return PROFILE_TIMEZONE_FALLBACKS;
  }

  function profileTimezoneGmtOffset(zone) {
    if (!zone) return "";
    try {
      const parts = new Intl.DateTimeFormat("en", {
        hour: "2-digit",
        minute: "2-digit",
        hour12: false,
        timeZone: zone,
        timeZoneName: "shortOffset",
      }).formatToParts(new Date());
      const value = parts.find((part) => part.type === "timeZoneName")?.value || "";
      if (value === "GMT") return "GMT+00:00";
      const match = value.match(/^GMT([+-])(\d{1,2})(?::?(\d{2}))?$/);
      if (!match) return value.startsWith("GMT") ? value : "";
      return `GMT${match[1]}${match[2].padStart(2, "0")}:${match[3] || "00"}`;
    } catch {
      return "";
    }
  }

  function profileTimezoneGmtLabel(zone) {
    const offset = profileTimezoneGmtOffset(zone);
    return offset ? `${offset} - ${zone}` : zone.replace(/_/g, " ");
  }

  function renderProfileTimezoneOptions(session = state.session) {
    const select = $("[data-profile-page-timezone]");
    if (!select) return;
    if (document.activeElement === select && select.options.length > 1) return;
    const current = String(session?.profileTimezone || select.value || "").trim();
    const zones = profileTimezoneOptions();
    const values = current && !zones.includes(current) ? [current, ...zones] : zones;
    const signature = values.join("\n");
    if (select.dataset.timezoneOptionsKey !== signature) {
      const placeholder = document.createElement("option");
      placeholder.value = "";
      placeholder.textContent = "Use browser time zone";
      select.replaceChildren(placeholder);
      values.forEach((zone) => {
        const option = document.createElement("option");
        option.value = zone;
        option.textContent = profileTimezoneGmtLabel(zone);
        select.append(option);
      });
      select.dataset.timezoneOptionsKey = signature;
    }
    select.value = current;
  }

  function defaultProfileAbout(session = profileSubject()) {
    const name = String(session?.nodeName || session?.email || "ForkMesh").trim() || "ForkMesh";
    if (session?.kind === "node") {
      const owner = String(session?.owner || "").trim();
      return `# ${name} is a ForkMesh node\n\nIt mirrors Git repositories and helps serve them to the network.` +
        (owner ? `\n\nOperated by @${owner}.` : "");
    }
    return `# Hi, I'm ${name}\n\nPinned profile content and public activity live here.`;
  }

  function profileAboutMarkdown(session = profileSubject()) {
    const value = String(session?.profileAbout || session?.profileReadme || "");
    return value || defaultProfileAbout(session);
  }

  function flushProfileAboutParagraph(out, paragraph) {
    if (!paragraph.length) return;
    out.push(`<p>${paragraph.map(escapeHtml).join("<br>")}</p>`);
    paragraph.length = 0;
  }

  function flushProfileAboutList(out, list) {
    if (!list.length) return;
    out.push(`<ul class="list-disc space-y-1 pl-5">${list.map((item) => `<li>${escapeHtml(item)}</li>`).join("")}</ul>`);
    list.length = 0;
  }

  function renderProfileMarkdown(markdown) {
    const lines = String(markdown || "").replace(/\r\n/g, "\n").split("\n");
    const out = [];
    const paragraph = [];
    const list = [];
    let inCode = false;
    let code = [];
    const flushText = () => {
      flushProfileAboutParagraph(out, paragraph);
      flushProfileAboutList(out, list);
    };
    for (const line of lines) {
      if (/^```/.test(line.trim())) {
        if (inCode) {
          out.push(`<pre class="overflow-auto rounded-md border border-border bg-background p-3 font-mono text-xs leading-5 text-muted-foreground"><code>${escapeHtml(code.join("\n"))}</code></pre>`);
          code = [];
          inCode = false;
        } else {
          flushText();
          inCode = true;
          code = [];
        }
        continue;
      }
      if (inCode) {
        code.push(line);
        continue;
      }
      if (!line.trim()) {
        flushText();
        continue;
      }
      const heading = line.match(/^(#{1,3})\s+(.+)$/);
      if (heading) {
        flushText();
        const level = heading[1].length;
        const size = level === 1 ? "text-2xl" : level === 2 ? "text-xl" : "text-base";
        out.push(`<h${level} class="${size} font-semibold text-foreground">${escapeHtml(heading[2])}</h${level}>`);
        continue;
      }
      const item = line.match(/^\s*[-*]\s+(.+)$/);
      if (item) {
        flushProfileAboutParagraph(out, paragraph);
        list.push(item[1]);
        continue;
      }
      flushProfileAboutList(out, list);
      paragraph.push(line);
    }
    flushText();
    if (inCode && code.length) {
      out.push(`<pre class="overflow-auto rounded-md border border-border bg-background p-3 font-mono text-xs leading-5 text-muted-foreground"><code>${escapeHtml(code.join("\n"))}</code></pre>`);
    }
    return out.length ? out.join("") : `<p>${escapeHtml(defaultProfileAbout()).replace(/\n/g, "<br>")}</p>`;
  }

  function renderProfileAbout(session) {
    if (profileMarkupOwnedByPublicProfile(session)) return;
    const owner = $("[data-profile-about-owner]");
    const label = $("[data-profile-about-label]");
    const body = $("[data-profile-about-body]");
    const name = String(session?.nodeName || session?.email || "forkmesh").trim() || "forkmesh";
    if (owner) owner.textContent = name;
    if (label) label.textContent = session?.kind === "node" ? "About this node" : "About yourself";
    if (body) {
      body.className = "grid gap-4 p-5 text-sm leading-6 text-foreground";
      body.innerHTML = renderProfileMarkdown(profileAboutMarkdown(session));
    }
  }

  const CONTRIBUTION_COLORS = ["#161b22", "#0e4429", "#006d32", "#26a641", "#39d353"];
  const CONTRIBUTION_GRID_COLUMNS = "2.25rem repeat(53, 0.75rem)";
  const CONTRIBUTION_GRID_GAP = "0.1875rem";
  const PROFILE_HISTORY_CONCURRENCY = 3;
  const PROFILE_HISTORY_REPO_LIMIT = 12;
  // Per-repo /history responses feed only the contribution graph and change
  // slowly, so they are cached in sessionStorage: navigating back to a profile
  // page must not refetch every repo (request budget, free-tier Worker).
  const PROFILE_HISTORY_CACHE_TTL_MS = 10 * 60 * 1000;
  const PROFILE_HISTORY_CACHE_PREFIX = "forkmesh.profileHistory:";

  function profileHistoryCacheKey(repo, session = profileSubject()) {
    const subject = String(session?.nodeName || session?.email || "guest").trim().toLowerCase();
    return `${PROFILE_HISTORY_CACHE_PREFIX}${subject}:${repoKey(repo)}`;
  }

  function readProfileHistoryCache(repo) {
    try {
      const parsed = JSON.parse(sessionStorage.getItem(profileHistoryCacheKey(repo)) || "null");
      if (!parsed || !Array.isArray(parsed.commits)) return null;
      if (!(Number(parsed.expiresAt) > Date.now())) return null;
      return parsed.commits;
    } catch (_) {
      return null;
    }
  }

  function writeProfileHistoryCache(repo, commits) {
    try {
      sessionStorage.setItem(profileHistoryCacheKey(repo), JSON.stringify({
        commits,
        expiresAt: Date.now() + PROFILE_HISTORY_CACHE_TTL_MS,
      }));
    } catch (_) {
      /* best-effort: quota/private mode just means a refetch later */
    }
  }

  function contributionDateMs(value) {
    if (value === undefined || value === null || value === "") return null;
    const numeric = Number(value);
    const date = Number.isFinite(numeric) && numeric > 0
      ? new Date(numeric < 1000000000000 ? numeric * 1000 : numeric)
      : new Date(value);
    const ms = date.getTime();
    return Number.isNaN(ms) ? null : ms;
  }

  function contributionDayKey(ms) {
    const date = new Date(ms);
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, "0");
    const day = String(date.getDate()).padStart(2, "0");
    return `${year}-${month}-${day}`;
  }

  function contributionRange(year) {
    const selected = Number(year) || new Date().getFullYear();
    const now = new Date();
    if (selected === now.getFullYear()) {
      const end = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59, 999);
      const start = new Date(end);
      start.setDate(start.getDate() - 364);
      start.setHours(0, 0, 0, 0);
      return { start, end, label: "last year" };
    }
    return {
      start: new Date(selected, 0, 1, 0, 0, 0, 0),
      end: new Date(selected, 11, 31, 23, 59, 59, 999),
      label: String(selected),
    };
  }

  function contributionGridStart(range) {
    const start = new Date(range.start);
    start.setDate(start.getDate() - start.getDay());
    start.setHours(0, 0, 0, 0);
    return start;
  }

  function contributionInRange(ms, range) {
    return Number.isFinite(ms) && ms >= range.start.getTime() && ms <= range.end.getTime();
  }

  function profileContributionLevel(count, max) {
    const value = Number(count) || 0;
    if (value <= 0) return 0;
    if (max <= 1) return 1;
    const ratio = value / max;
    if (ratio >= 0.75) return 4;
    if (ratio >= 0.5) return 3;
    if (ratio >= 0.25) return 2;
    return 1;
  }

  function profileContributionAliases(session = profileSubject()) {
    const aliases = new Set();
    const add = (value) => {
      const text = String(value || "").trim().toLowerCase();
      if (text) aliases.add(text);
    };
    add(session?.nodeName);
    add(session?.email);
    (Array.isArray(session?.nodes) ? session.nodes : []).forEach(add);
    return aliases;
  }

  function repoBelongsToProfile(repo, aliases) {
    const owner = String(repo?.owner || "").trim().toLowerCase();
    const canonical = repoCanonicalIdentity(repo);
    const canonicalOwner = String(canonical.owner || "").trim().toLowerCase();
    return Boolean((owner && aliases.has(owner)) || (canonicalOwner && aliases.has(canonicalOwner)));
  }

  function profileContributionGroups(session = profileSubject()) {
    const aliases = profileContributionAliases(session);
    return groupRepositories(state.repositories || []).filter((group) => {
      const source = sourceOfTruth(group);
      if (repoBelongsToProfile(source, aliases)) return true;
      return (group.members || []).some((member) => repoBelongsToProfile(member, aliases));
    });
  }

  function addContribution(data, ms, count, event) {
    const amount = Math.max(0, Number(count) || 0);
    if (!amount || !contributionInRange(ms, data.range)) return;
    const day = contributionDayKey(ms);
    data.days.set(day, (data.days.get(day) || 0) + amount);
    data.total += amount;
    if (event) {
      data.events.push({
        ...event,
        ts: ms,
        count: amount,
        day,
      });
    }
  }

  function profileContributionHistoryKey(repo, year) {
    return `${year}:${repoKey(repo)}:${repoDataVersion(repo)}`;
  }

  function commitMatchesProfile(commit, aliases) {
    const fields = [
      commit?.author,
      commit?.authorName,
      commit?.committer,
      commit?.name,
      commit?.email,
      commit?.authorEmail,
      commit?.committerEmail,
    ].map((value) => String(value || "").trim().toLowerCase()).filter(Boolean);
    if (!fields.length) return true;
    return fields.some((field) => aliases.has(field));
  }

  function addLiveHistoryContributions(data, repo, commits, aliases) {
    const byDay = new Map();
    (Array.isArray(commits) ? commits : []).forEach((commit) => {
      if (!commitMatchesProfile(commit, aliases)) return;
      const ms = contributionDateMs(
        commit?.authorDate || commit?.date || commit?.committedAt ||
        commit?.commitDate || commit?.updatedAt || commit?.ts,
      );
      if (!contributionInRange(ms, data.range)) return;
      const day = contributionDayKey(ms);
      byDay.set(day, (byDay.get(day) || 0) + 1);
    });
    byDay.forEach((count, day) => {
      const ms = new Date(`${day}T12:00:00`).getTime();
      addContribution(data, ms, count, {
        type: "commits",
        repo,
        title: `${formatCount(count)} commit${count === 1 ? "" : "s"}`,
        icon: "git-commit-horizontal",
      });
    });
  }

  function addCatalogActivityWeeks(data, repo) {
    const series = normalizeActivityWeeks(repo.activityWeeks);
    const anchor = contributionDateMs(repo.updatedAt || repo.lastSync || repo.hostedSince);
    if (!anchor || !series.some(Boolean)) return;
    series.forEach((count, index) => {
      if (!count) return;
      const date = new Date(anchor);
      date.setDate(date.getDate() - ((series.length - 1 - index) * 7));
      date.setHours(12, 0, 0, 0);
      addContribution(data, date.getTime(), count, {
        type: "catalog_activity",
        repo,
        title: `${formatCount(count)} catalog-reported commit${count === 1 ? "" : "s"}`,
        icon: "activity",
      });
    });
  }

  function addRepositoryContribution(data, repo) {
    const created = contributionDateMs(repo.hostedSince || repo.createdAt);
    const updated = contributionDateMs(repo.updatedAt || repo.lastSync || repo.hostedSince);
    const createdMs = created || updated;
    if (createdMs) {
      addContribution(data, createdMs, 1, {
        type: "repo_created",
        repo,
        title: "Created repository",
        icon: "book-marked",
      });
    }
  }

  function profileContributionData(year = state.profileContributions.year, session = profileSubject()) {
    const range = contributionRange(year);
    const data = {
      year,
      range,
      days: new Map(),
      events: [],
      total: 0,
      max: 0,
      loading: Boolean(state.profileContributions.loading),
    };
    const aliases = profileContributionAliases(session);
    profileContributionGroups(session).forEach((group) => {
      const repo = sourceOfTruth(group);
      addRepositoryContribution(data, repo);
      const history = state.profileContributions.liveHistory[
        profileContributionHistoryKey(repo, year)
      ];
      if (Array.isArray(history) && history.length) addLiveHistoryContributions(data, repo, history, aliases);
      else addCatalogActivityWeeks(data, repo);
    });
    data.max = Math.max(0, ...data.days.values());
    data.events.sort((a, b) => b.ts - a.ts || repoKey(a.repo).localeCompare(repoKey(b.repo)));
    return data;
  }

  function profileContributionYears(session = profileSubject()) {
    const current = new Date().getFullYear();
    const years = new Set([current, current - 1, current - 2, current - 3, current - 4]);
    profileContributionGroups(session).forEach((group) => {
      const repo = sourceOfTruth(group);
      [repo.hostedSince, repo.createdAt, repo.updatedAt, repo.lastSync].forEach((value) => {
        const ms = contributionDateMs(value);
        if (ms) years.add(new Date(ms).getFullYear());
      });
    });
    return [...years].filter((year) => Number.isFinite(year) && year > 1970).sort((a, b) => b - a);
  }

  function renderProfileContributionYears(year = state.profileContributions.year) {
    const container = $("[data-profile-contribution-years]");
    if (!container) return;
    container.innerHTML = profileContributionYears().map((item) => {
      const active = Number(item) === Number(year);
      return `
        <button type="button" data-profile-contribution-year="${item}" class="h-10 rounded-md px-5 text-left ${active ? "bg-[#2f81f7] font-semibold text-white" : "font-medium text-muted-foreground hover:bg-secondary hover:text-foreground"}">${item}</button>
      `;
    }).join("");
  }

  function renderContributionMonthLabels(months, gridStart) {
    months.innerHTML = "";
    months.style.gridTemplateColumns = CONTRIBUTION_GRID_COLUMNS;
    months.style.columnGap = CONTRIBUTION_GRID_GAP;
    let lastMonth = "";
    for (let week = 0; week < 53; week += 1) {
      const date = new Date(gridStart);
      date.setDate(date.getDate() + (week * 7));
      const month = date.toLocaleString(undefined, { month: "short" });
      if (month === lastMonth) continue;
      lastMonth = month;
      const label = document.createElement("span");
      label.textContent = month;
      label.className = "min-w-0 truncate text-xs leading-4 text-muted-foreground";
      label.style.gridColumn = `${week + 2} / span 4`;
      months.append(label);
    }
  }

  function renderProfileActivity(data) {
    const container = $("[data-profile-activity-items]");
    const empty = $("[data-profile-activity-empty]");
    if (!container || !empty) return;
    const events = data.events.slice(0, 20);
    empty.classList.toggle("hidden", Boolean(events.length));
    if (!events.length) {
      if (data.loading) {
        empty.innerHTML = loadingHtml("Loading live contribution history...");
      } else {
        empty.textContent = "No contribution activity found for this year yet.";
      }
      container.innerHTML = "";
      return;
    }
    container.innerHTML = events.map((event) => {
      const repo = event.repo || {};
      const date = new Date(event.ts);
      const repoUrl = repoPathUrl(repo);
      return `
        <div data-profile-activity-item class="grid grid-cols-[2.5rem_minmax(0,1fr)] gap-4 md:grid-cols-[2.5rem_minmax(0,1fr)_8rem]">
          <div class="flex flex-col items-center">
            <span class="flex h-10 w-10 items-center justify-center rounded-full bg-secondary text-muted-foreground">
              <i data-lucide="${escapeHtml(event.icon || "activity")}" class="h-5 w-5"></i>
            </span>
            <span class="h-12 w-px bg-border"></span>
          </div>
          <div class="min-w-0">
            <p class="text-base font-semibold leading-6 text-foreground">${escapeHtml(event.title || "Contribution activity")}</p>
            <p class="mt-2 flex min-w-0 items-center gap-2 text-sm">
              <i data-lucide="git-fork" class="h-4 w-4 shrink-0 text-muted-foreground"></i>
              <a href="${escapeHtml(repoUrl)}" class="truncate text-accent hover:underline">${escapeHtml(repoKey(repo))}</a>
            </p>
          </div>
          <div class="hidden items-start justify-end text-sm text-muted-foreground md:flex">
            <span>${escapeHtml(date.toLocaleDateString(undefined, { month: "short", day: "numeric" }))}</span>
          </div>
        </div>
      `;
    }).join("");
  }

  async function loadProfileContributionHistories(year = state.profileContributions.year) {
    // The shared-chrome catalog load renders the contribution graph on EVERY
    // page, but only the profile documents actually ship the grid — never fan
    // out per-repo history fetches anywhere else (request budget).
    if (!$("[data-contribution-cells]")) return;
    const groups = profileContributionGroups();
    let hydratedFromCache = 0;
    const repos = groups.map((group) => sourceOfTruth(group))
      .filter((repo) => repoIsLive(repo))
      .filter((repo) => {
        const key = profileContributionHistoryKey(repo, year);
        if (Array.isArray(state.profileContributions.liveHistory[key])) return false;
        const cached = readProfileHistoryCache(repo);
        if (cached) {
          state.profileContributions.liveHistory[key] = cached;
          hydratedFromCache += 1;
          return false;
        }
        return true;
      })
      .slice(0, PROFILE_HISTORY_REPO_LIMIT);
    if (!repos.length || state.profileContributions.loadedYears[year]) {
      if (hydratedFromCache) renderProfileContributionGraph();
      return;
    }
    state.profileContributions.loading = true;
    renderProfileContributionGraph();
    let index = 0;
    const worker = async () => {
      while (index < repos.length) {
        const repo = repos[index];
        index += 1;
        const key = profileContributionHistoryKey(repo, year);
        try {
          const data = await fetchRepoJson(repoLiveUrl(repo, "history"));
          const commits = Array.isArray(data.commits) ? data.commits : [];
          state.profileContributions.liveHistory[key] = commits;
          writeProfileHistoryCache(repo, commits);
        } catch (_) {
          state.profileContributions.liveHistory[key] = null;
        }
      }
    };
    await Promise.all(Array.from({ length: Math.min(PROFILE_HISTORY_CONCURRENCY, repos.length) }, worker));
    state.profileContributions.loadedYears[year] = true;
    state.profileContributions.loading = false;
    renderProfileContributionGraph();
  }

  function renderProfileContributionGraph() {
    const months = $("[data-contribution-months]");
    const cells = $("[data-contribution-cells]");
    const legend = $("[data-contribution-legend]");
    const summary = $("[data-profile-contribution-summary]");
    const year = Number(state.profileContributions.year) || new Date().getFullYear();
    const data = profileContributionData(year);
    const gridStart = contributionGridStart(data.range);
    if (summary) {
      summary.textContent = `${formatCount(data.total)} contribution${data.total === 1 ? "" : "s"} in ${data.range.label}`;
    }
    if (months) renderContributionMonthLabels(months, gridStart);
    if (cells) {
      cells.innerHTML = "";
      cells.style.gridTemplateColumns = CONTRIBUTION_GRID_COLUMNS;
      cells.style.gridTemplateRows = "repeat(7, 0.75rem)";
      cells.style.gap = CONTRIBUTION_GRID_GAP;
      [
        { label: "Mon", row: 2 },
        { label: "Wed", row: 4 },
        { label: "Fri", row: 6 },
      ].forEach((dayLabel) => {
        const label = document.createElement("span");
        label.textContent = dayLabel.label;
        label.className = "self-center text-xs leading-3 text-foreground";
        label.style.gridColumn = "1";
        label.style.gridRow = String(dayLabel.row);
        cells.append(label);
      });
      for (let week = 0; week < 53; week += 1) {
        for (let day = 0; day < 7; day += 1) {
          const date = new Date(gridStart);
          date.setDate(date.getDate() + (week * 7) + day);
          const key = contributionDayKey(date.getTime());
          const count = data.days.get(key) || 0;
          const level = profileContributionLevel(count, data.max);
          const cell = document.createElement("span");
          cell.setAttribute("data-contribution-cell", `${week}-${day}`);
          cell.className = "h-3 w-3 rounded-sm";
          cell.style.backgroundColor = CONTRIBUTION_COLORS[level];
          cell.style.gridColumn = String(week + 2);
          cell.style.gridRow = String(day + 1);
          cell.title = `${formatCount(count)} contribution${count === 1 ? "" : "s"} on ${formatDate(date.getTime())}`;
          cell.setAttribute("aria-label", cell.title);
          cell.style.opacity = contributionInRange(date.getTime(), data.range) ? "1" : "0.45";
          cells.append(cell);
        }
      }
    }
    if (legend) {
      legend.innerHTML = "";
      const low = document.createElement("span");
      low.textContent = "Less";
      legend.append(low);
      CONTRIBUTION_COLORS.forEach((color) => {
        const swatch = document.createElement("span");
        swatch.className = "h-3 w-3 rounded-sm";
        swatch.style.backgroundColor = color;
        legend.append(swatch);
      });
      const high = document.createElement("span");
      high.textContent = "More";
      legend.append(high);
    }
    renderProfileContributionYears(year);
    renderProfileActivity(data);
    window.lucide?.createIcons();
    if (!state.profileContributions.loading) {
      loadProfileContributionHistories(year);
    }
  }

  function setProfileAboutHint(text, cls) {
    setProfilePageHint("[data-profile-about-hint]", text, cls);
  }

  function setProfileAboutModalOpen(open) {
    const modal = $("[data-profile-about-modal]");
    if (!modal) return;
    modal.classList.toggle("hidden", !open);
    modal.classList.toggle("flex", open);
    if (open) {
      const textarea = $("[data-profile-about-textarea]");
      if (textarea) textarea.value = profileAboutMarkdown(state.session);
      setProfileAboutHint("", "");
      window.setTimeout(() => textarea?.focus(), 0);
    }
  }

  function validNodeName(value) {
    return /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(String(value || ""));
  }

  // A node's Ed25519 public key (raw 32 bytes, base64url, unpadded) - the
  // value the desktop app's own profile card labels "Node ID" (issue #351),
  // so the claim-node input below must accept it alongside the account name.
  function validNodePubkey(value) {
    return /^[A-Za-z0-9_-]{43}$/.test(String(value || ""));
  }

  const NOTIFICATION_PREFERENCE_DEFAULTS = {
    mention: true,
    subscribed: true,
    pull_submitted: true,
    issue_assigned: true,
    repo_shared: true,
    bounty_funded: true,
    bounty_paid: true,
    release_published: true,
    pending_inbox: true,
    credits_refilled: true,
    general_chat: true,
    host_online: false,
    host_offline: false,
  };

  function normalizedNotificationPreferences(session = state.session) {
    const raw = session?.notificationPreferences;
    const prefs = { ...NOTIFICATION_PREFERENCE_DEFAULTS };
    if (raw && typeof raw === "object") {
      Object.keys(prefs).forEach((key) => {
        if (Object.prototype.hasOwnProperty.call(raw, key)) prefs[key] = Boolean(raw[key]);
      });
    }
    return prefs;
  }

  function renderNotificationPreferences(session = state.session) {
    const prefs = normalizedNotificationPreferences(session);
    $$("[data-notification-pref]").forEach((input) => {
      const key = input.dataset.notificationPref || "";
      if (Object.prototype.hasOwnProperty.call(prefs, key)) {
        input.checked = Boolean(prefs[key]);
      }
    });
  }

  function collectNotificationPreferences() {
    const prefs = { ...NOTIFICATION_PREFERENCE_DEFAULTS };
    $$("[data-notification-pref]").forEach((input) => {
      const key = input.dataset.notificationPref || "";
      if (Object.prototype.hasOwnProperty.call(prefs, key)) {
        prefs[key] = Boolean(input.checked);
      }
    });
    return prefs;
  }

  function setRenameStatus(text, cls) {
    setProfilePageHint("[data-profile-rename-status]", text, cls);
  }

  function updateRenameButton() {
    const button = $("[data-profile-rename-save]");
    if (!button) return;
    button.disabled = !(
      state.nodeNameAvailability.available &&
      Boolean(state.session?.emailVerified) &&
      Boolean(profilePassword("[data-profile-page-password]"))
    );
    button.classList.toggle("opacity-40", button.disabled);
  }

  function renderProfileModal(session) {
    const emailStatus = $("[data-profile-email-status]");
    const verifyButton = $("[data-profile-verify-email]");
    const solanaInput = $("[data-profile-solana]");
    if (emailStatus) {
      emailStatus.textContent = session?.emailVerified
        ? `${session.email || "Email"} is verified.`
        : `${session?.email || "Your email"} is not verified yet.`;
    }
    if (verifyButton) {
      verifyButton.disabled = Boolean(session?.emailVerified);
      verifyButton.classList.toggle("opacity-40", Boolean(session?.emailVerified));
      verifyButton.textContent = session?.emailVerified ? "Verified" : "Send link";
    }
    if (solanaInput && document.activeElement !== solanaInput) {
      solanaInput.value = session?.solana || "";
    }
  }

  function profileSidebarMarkup(session) {
    const template = $("[data-profile-sidebar-template]");
    return template ? template.innerHTML.trim() : "";
  }

  function renderProfileSidebars(session) {
    const markup = profileSidebarMarkup(session);
    $$("[data-profile-sidebar-slot]").forEach((slot) => {
      if (slot.dataset.profileSidebarRendered === "true" && slot.dataset.profileSidebarMarkup === markup) return;
      slot.innerHTML = markup;
      slot.dataset.profileSidebarRendered = "true";
      slot.dataset.profileSidebarMarkup = markup;
    });
  }

  function renderProfilePage(session) {
    if (profileMarkupOwnedByPublicProfile(session)) return;
    const name = session?.nodeName || "My Profile";
    const email = session?.email || "No email on file";
    renderProfileSidebars(session);
    const avatars = $$('[data-profile-page-avatar]');
    const nameEls = $$('[data-profile-page-node-name]');
    const emailEls = $$('[data-profile-page-email]');
    const bioEls = $$("[data-profile-bio]");
    const followersEls = $$("[data-profile-followers-count]");
    const followingEls = $$("[data-profile-following-count]");
    const mirrorsEls = $$("[data-profile-mirrors-count]");
    const locationEls = $$("[data-profile-location-text]");
    const localTimeEls = $$("[data-profile-local-time]");
    const websiteEls = $$("[data-profile-website]");
    const accountStatus = $("[data-profile-page-account-status]");
    const payoutStatus = $("[data-profile-page-payout-status]");
    const adminStatus = $("[data-profile-page-admin-status]");
    const emailStatus = $("[data-profile-page-email-status]");
    const verifyButton = $("[data-profile-page-verify-email]");
    const solanaInput = $("[data-profile-page-solana]");
    const renameInput = $("[data-profile-rename-input]");
    const bioInput = $("[data-profile-page-bio]");
    const locationInput = $("[data-profile-page-location]");
    const timezoneInput = $("[data-profile-page-timezone]");
    const mastodonInput = $("[data-profile-page-mastodon]");
    const privateInput = $("[data-profile-page-private]");
    const followersPublicInput = $("[data-profile-page-followers-public]");
    const publicUrl = $("[data-profile-public-url]");
    const txtValue = $("[data-profile-txt-value]");

    const isNode = session?.kind === "node";
    avatars.forEach((avatar) => applyAvatar(avatar, session));
    nameEls.forEach((nameEl) => {
      nameEl.textContent = name;
    });
    emailEls.forEach((emailEl) => {
      emailEl.textContent = email;
    });
    $$("[data-profile-kind-badge]").forEach((badge) => {
      badge.classList.toggle("hidden", !isNode);
    });
    $$("[data-profile-node-status]").forEach((row) => {
      row.classList.toggle("hidden", !isNode);
      const text = row.querySelector("[data-profile-node-status-text]");
      if (text) text.textContent = session?.online ? "Online" : "Offline";
    });
    bioEls.forEach((bioEl) => {
      bioEl.textContent = session?.profileBio || "No bio yet.";
    });
    followersEls.forEach((followersEl) => {
      followersEl.textContent = String(Number(session?.profileFollowers ?? session?.followers ?? 0).toLocaleString());
    });
    followingEls.forEach((followingEl) => {
      followingEl.textContent = String(Number(session?.profileFollowing ?? session?.following ?? 0).toLocaleString());
    });
    mirrorsEls.forEach((mirrorsEl) => {
      mirrorsEl.textContent = String(Number(session?.profileMirrorCount ?? session?.mirrorCount ?? 0).toLocaleString());
    });
    locationEls.forEach((locationEl) => {
      locationEl.textContent = session?.profileLocation || "No location";
    });
    localTimeEls.forEach((localTimeEl) => {
      const profileTimezone = session?.profileTimezone || "";
      try {
        localTimeEl.textContent = new Intl.DateTimeFormat([], {
          hour: "2-digit",
          minute: "2-digit",
          hour12: false,
          ...(profileTimezone ? { timeZone: profileTimezone } : {}),
          timeZoneName: "shortOffset",
        }).format(new Date());
      } catch {
        localTimeEl.textContent = profileTimezone || "No time zone";
      }
    });
    websiteEls.forEach((websiteEl) => {
      const firstLink = Array.isArray(session?.profileLinks) ? session.profileLinks.find((link) => link?.url) : null;
      const url = firstLink?.url || profilePublicUrl(session);
      websiteEl.textContent = url;
      websiteEl.href = url;
    });
    if (accountStatus) accountStatus.textContent = session?.status || "active";
    if (payoutStatus) payoutStatus.textContent = session?.hasPayoutAddress ? "Configured" : "Not configured";
    if (adminStatus) adminStatus.textContent = session?.isAdmin ? "Yes" : "No";
    if (emailStatus) {
      emailStatus.textContent = session?.emailVerified
        ? `${email} is verified.`
        : `${email} is not verified yet.`;
    }
    if (verifyButton) {
      verifyButton.disabled = Boolean(session?.emailVerified);
      verifyButton.classList.toggle("opacity-40", Boolean(session?.emailVerified));
      verifyButton.textContent = session?.emailVerified ? "Verified" : "Send link";
    }
    if (solanaInput && document.activeElement !== solanaInput) {
      solanaInput.value = session?.solana || "";
    }
    if (renameInput) {
      renameInput.placeholder = name;
    }
    if (bioInput && document.activeElement !== bioInput) {
      bioInput.value = session?.profileBio || "";
    }
    if (locationInput && document.activeElement !== locationInput) {
      locationInput.value = session?.profileLocation || "";
    }
    renderProfileTimezoneOptions(session);
    if (timezoneInput && document.activeElement !== timezoneInput) {
      timezoneInput.value = session?.profileTimezone || "";
    }
    if (mastodonInput && document.activeElement !== mastodonInput) {
      mastodonInput.value = session?.mastodon || "";
    }
    if (privateInput) {
      privateInput.checked = Boolean(session?.profilePrivate);
    }
    if (followersPublicInput) {
      followersPublicInput.checked = Boolean(session?.followersPublic);
    }
    if (publicUrl) publicUrl.textContent = profilePublicUrl(session);
    if (txtValue) txtValue.textContent = profileTxtValue(session);
    renderProfileLinksEditor(session);
    renderNotificationPreferences(session);
    if (!session?.emailVerified) {
      state.nodeNameAvailability.available = false;
      setRenameStatus("Verify your email before changing your node name.", "bad");
    } else if (!($("[data-profile-rename-input]")?.value || "").trim()) {
      state.nodeNameAvailability.available = false;
      setRenameStatus("Enter a new node name to check availability.", "");
    }
    updateRenameButton();
    renderClaimNodePanel(session);
    renderProfileContributionGraph();
  }

  function renderProfileLinksEditor(session = state.session) {
    const links = Array.isArray(session?.profileLinks) ? session.profileLinks : [];
    $$("[data-profile-link-row]").forEach((row, index) => {
      const link = links[index] || {};
      const label = row.querySelector("[data-profile-link-label]");
      const url = row.querySelector("[data-profile-link-url]");
      const status = row.querySelector("[data-profile-link-status]");
      if (label && document.activeElement !== label) label.value = link.label || "";
      if (url && document.activeElement !== url) url.value = link.url || "";
      if (status) {
        if (link.url) {
          status.textContent = link.verified
            ? `Verified for ${link.domain || "domain"}.`
            : `Add TXT ${link.txtValue || profileTxtValue(session)} on ${link.txtName || link.domain || "the domain"} to verify.`;
          status.className = "sm:col-span-2 text-[11px] " +
            (link.verified ? "text-primary" : "text-muted-foreground");
        } else {
          status.textContent = "";
          status.className = "sm:col-span-2 text-[11px] text-muted-foreground";
        }
      }
    });
  }

  function profilePayload(extra = {}, passwordOverride) {
    const password = passwordOverride ?? ($("[data-profile-password]")?.value || "");
    return {
      nodeName: state.session?.nodeName || "",
      email: state.session?.email || "",
      sessionToken: state.session?.sessionToken || "",
      password,
      ...extra,
    };
  }

  async function postProfile(extra, passwordOverride) {
    const response = await fetch("/api/accounts/profile", {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(profilePayload(extra, passwordOverride)),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body.ok === false) {
      throw new Error(body.error || `HTTP ${response.status}`);
    }
    const nextSession = sessionFromAccountPayload(body, state.session || {});
    writeSession(nextSession);
    renderProfile(nextSession);
    return body;
  }

  async function refreshRepositories() {
    const data = await fetchJson("/api/repositories", { fresh: true });
    renderRepositories(data.repositories, state.session);
  }

  async function saveProfile(options = {}) {
    const passwordSelector = options.passwordSelector || "[data-profile-password]";
    const solanaSelector = options.solanaSelector || "[data-profile-solana]";
    const hintSelector = options.hintSelector || "[data-profile-hint]";
    const buttonSelector = options.buttonSelector || "[data-profile-save]";
    const password = profilePassword(passwordSelector);
    const solana = ($(solanaSelector)?.value || "").trim();
    if (!password) {
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint("Enter your current password to save payout changes.", "bad");
      } else {
        setProfilePageHint(hintSelector, "Enter your current password to save payout changes.", "bad");
      }
      return;
    }
    const button = $(buttonSelector);
    if (button) { button.disabled = true; button.textContent = "Saving…"; }
    try {
      await postProfile({ solana }, password);
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(solana ? "Payout address saved." : "Payout address cleared.", "good");
      } else {
        setProfilePageHint(hintSelector, solana ? "Payout address saved." : "Payout address cleared.", "good");
      }
    } catch (error) {
      const message = error.message === "bad_solana"
        ? "Enter a valid public Solana address."
        : "Could not save payout address. Check your password and try again.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "bad");
      } else {
        setProfilePageHint(hintSelector, message, "bad");
      }
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = options.buttonText || "Save profile";
      }
    }
  }

  function collectProfileLinks() {
    const links = [];
    $$("[data-profile-link-row]").forEach((row) => {
      const label = (row.querySelector("[data-profile-link-label]")?.value || "").trim();
      const url = (row.querySelector("[data-profile-link-url]")?.value || "").trim();
      if (!label && !url) return;
      links.push({ label, url });
    });
    return links;
  }

  async function savePublicProfile() {
    const button = $("[data-profile-public-save]");
    if (button) { button.disabled = true; button.textContent = "Saving…"; }
    try {
      await postProfile({
        profileBio: ($("[data-profile-page-bio]")?.value || "").trim(),
        profileLocation: ($("[data-profile-page-location]")?.value || "").trim(),
        profileTimezone: ($("[data-profile-page-timezone]")?.value || "").trim(),
        mastodon: ($("[data-profile-page-mastodon]")?.value || "").trim(),
        profilePrivate: Boolean($("[data-profile-page-private]")?.checked),
        followersPublic: Boolean($("[data-profile-page-followers-public]")?.checked),
        profileLinks: collectProfileLinks(),
      });
      setProfilePageHint("[data-profile-public-hint]", "Public profile saved.", "good");
    } catch (error) {
      const messages = {
        bad_mastodon: "Enter a Mastodon handle like @you@example.social.",
        bad_profile_timezone: "Enter a valid IANA time zone like Asia/Kolkata.",
        bad_profile_links: "Check your profile links and try again.",
        bad_profile_link_url: "Profile links must be http or https URLs on a real domain.",
      };
      setProfilePageHint(
        "[data-profile-public-hint]",
        messages[error.message] || "Could not save public profile. Sign in again and try once more.",
        "bad");
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = "Save public profile";
      }
    }
  }

  async function saveProfileAbout() {
    const profileAbout = String($("[data-profile-about-textarea]")?.value || "").replace(/\r\n/g, "\n");
    const button = $("[data-profile-about-save]");
    if (button) { button.disabled = true; button.textContent = "Saving..."; }
    try {
      const body = await postProfile({ profileAbout });
      const nextSession = sessionFromAccountPayload(body, state.session || {});
      renderProfileAbout(nextSession);
      setProfileAboutHint("About saved.", "good");
      setProfileAboutModalOpen(false);
    } catch (error) {
      const message = error.message === "profile_about_too_large" || error.message === "profile_readme_too_large"
        ? "About is too large. Keep it under 32 KB."
        : "Could not save about. Sign in again and try once more.";
      setProfileAboutHint(message, "bad");
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = "Save about";
      }
    }
  }

  async function resendVerification(options = {}) {
    const passwordSelector = options.passwordSelector || "[data-profile-password]";
    const hintSelector = options.hintSelector || "[data-profile-hint]";
    const buttonSelector = options.buttonSelector || "[data-profile-verify-email]";
    const password = profilePassword(passwordSelector);
    if (!password) {
      const message = "Enter your current password first, then send a verification link.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "bad");
      } else {
        setProfilePageHint(hintSelector, message, "bad");
      }
      return;
    }
    const button = $(buttonSelector);
    if (button) { button.disabled = true; button.textContent = "Sending…"; }
    try {
      const body = await postProfile({ resendVerification: true }, password);
      const message = body.verificationSent
        ? "Verification email sent."
        : "Verification request queued for manual follow-up.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "good");
      } else {
        setProfilePageHint(hintSelector, message, "good");
      }
    } catch (_) {
      const message = "Could not send verification. Check your password and try again.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "bad");
      } else {
        setProfilePageHint(hintSelector, message, "bad");
      }
    } finally {
      renderProfileModal(state.session);
      renderProfilePage(state.session);
    }
  }

  async function checkNodeNameAvailability() {
    const input = $("[data-profile-rename-input]");
    const candidate = (input?.value || "").trim().toLowerCase();
    state.nodeNameAvailability.candidate = candidate;
    state.nodeNameAvailability.available = false;
    if (input && input.value !== candidate) input.value = candidate;
    if (!candidate) {
      setRenameStatus("Enter a new node name to check availability.", "");
      updateRenameButton();
      return;
    }
    if (!validNodeName(candidate)) {
      setRenameStatus("Use lowercase letters, numbers, and hyphens. Start with a letter.", "bad");
      updateRenameButton();
      return;
    }
    if (candidate === String(state.session?.nodeName || "").toLowerCase()) {
      setRenameStatus("This is already your current node name.", "bad");
      updateRenameButton();
      return;
    }
    if (!state.session?.emailVerified) {
      setRenameStatus("Verify your email before changing your node name.", "bad");
      updateRenameButton();
      return;
    }

    const seq = state.nodeNameAvailability.seq + 1;
    state.nodeNameAvailability.seq = seq;
    state.nodeNameAvailability.checking = true;
    setRenameStatus("Checking availability…", "");
    updateRenameButton();
    try {
      const response = await fetch(`/api/accounts/${encodeURIComponent(candidate)}`, {
        headers: { accept: "application/json" },
      });
      const body = await response.json().catch(() => ({}));
      if (seq !== state.nodeNameAvailability.seq) return;
      state.nodeNameAvailability.available = Boolean(response.ok && body.available);
      setRenameStatus(
        state.nodeNameAvailability.available
          ? "Node name is available."
          : "That node name is already taken.",
        state.nodeNameAvailability.available ? "good" : "bad",
      );
    } catch (_) {
      if (seq !== state.nodeNameAvailability.seq) return;
      setRenameStatus("Could not check availability right now.", "bad");
    } finally {
      if (seq === state.nodeNameAvailability.seq) {
        state.nodeNameAvailability.checking = false;
        updateRenameButton();
      }
    }
  }

  async function renameNodeName() {
    const input = $("[data-profile-rename-input]");
    const newNodeName = (input?.value || "").trim().toLowerCase();
    const password = profilePassword("[data-profile-page-password]");
    if (!state.nodeNameAvailability.available || !newNodeName) {
      setRenameStatus("Choose an available node name first.", "bad");
      return;
    }
    if (!password) {
      setRenameStatus("Enter your current password to update your node name.", "bad");
      updateRenameButton();
      return;
    }
    const button = $("[data-profile-rename-save]");
    if (button) { button.disabled = true; button.textContent = "Updating…"; }
    try {
      await postProfile({ newNodeName }, password);
      if (input) input.value = "";
      $("[data-profile-page-password]") && ($("[data-profile-page-password]").value = "");
      state.nodeNameAvailability.available = false;
      setRenameStatus("Node name updated. Repositories are refreshing.", "good");
      await refreshRepositories();
      renderProfilePage(state.session);
    } catch (error) {
      const messages = {
        email_not_verified: "Verify your email before changing your node name.",
        invalid_node_name: "Enter a valid node name.",
        node_name_taken: "That node name is already taken.",
        node_name_unchanged: "Enter a different node name.",
        repo_namespace_conflict: "That namespace already has repository data.",
      };
      setRenameStatus(messages[error.message] || "Could not update node name. Check your password and try again.", "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Update node name"; }
      updateRenameButton();
    }
  }

  // Users vs nodes (adhoc #53): claim a node (e.g. a headless mirror you
  // installed) by its node ID, then confirm the code that appears on that
  // node itself to complete the link.
  function renderClaimNodePanel(session) {
    const list = $("[data-claim-node-list]");
    if (!list) return;
    const nodes = Array.isArray(session?.nodes) ? session.nodes : [];
    list.innerHTML = "";
    if (!nodes.length) {
      const li = document.createElement("li");
      li.className = "text-muted-foreground";
      li.textContent = "No linked nodes yet.";
      list.appendChild(li);
      return;
    }
    for (const node of nodes) {
      const li = document.createElement("li");
      li.className = "font-mono";
      li.textContent = node;
      list.appendChild(li);
    }
  }

  async function claimNode() {
    const input = $("[data-claim-node-input]");
    // Not lowercased up front: a node's public-key ID is case-sensitive, and
    // only the plain-name form is meant to be case-insensitive.
    const nodeId = (input?.value || "").trim();
    const password = profilePassword("[data-claim-node-password]");
    if (!validNodeName(nodeId.toLowerCase()) && !validNodePubkey(nodeId)) {
      setProfilePageHint("[data-claim-node-status]", "Enter a valid node ID.", "bad");
      return;
    }
    if (!password) {
      setProfilePageHint("[data-claim-node-status]", "Enter your current password to claim a node.", "bad");
      return;
    }
    const button = $("[data-claim-node-send]");
    if (button) { button.disabled = true; button.textContent = "Sending…"; }
    try {
      const response = await fetch("/api/accounts/claim-node", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          identifier: state.session?.email || state.session?.nodeName || "",
          password, nodeId,
        }),
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || body.ok === false) throw new Error(body.error || `HTTP ${response.status}`);
      const codeRow = $("[data-claim-code-row]");
      if (body.alreadyLinked) {
        state.claimNode.pendingNodeId = "";
        if (codeRow) codeRow.classList.add("hidden");
        setProfilePageHint("[data-claim-node-status]", `"${nodeId}" is already linked to your account.`, "good");
        await refreshPublicProfile();
      } else {
        state.claimNode.pendingNodeId = nodeId;
        if (codeRow) codeRow.classList.remove("hidden");
        setProfilePageHint(
          "[data-claim-node-status]",
          `Confirmation code sent to "${nodeId}". Check that node's app for the code, then enter it below.`,
          "good");
      }
    } catch (error) {
      const messages = {
        invalid_credentials: "Incorrect password.",
        invalid_node_id: "Enter a valid node ID.",
        cannot_claim_self: "You can't claim your own account.",
        no_such_node: "No node with that ID was found.",
        not_a_node: "That ID belongs to a user account, not a claimable node.",
        node_already_owned: "That node is already linked to another account.",
      };
      setProfilePageHint(
        "[data-claim-node-status]",
        messages[error.message] || "Could not send a claim code. Check the node ID and your password.",
        "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Send claim code"; }
    }
  }

  async function confirmClaimCode() {
    const nodeId = state.claimNode.pendingNodeId;
    const code = ($("[data-claim-code-input]")?.value || "").trim();
    const password = profilePassword("[data-claim-node-password]");
    if (!nodeId) {
      setProfilePageHint("[data-claim-node-status]", "Send a claim code first.", "bad");
      return;
    }
    if (!/^[0-9]{6}$/.test(code)) {
      setProfilePageHint("[data-claim-node-status]", "Enter the 6-digit code shown on the node.", "bad");
      return;
    }
    if (!password) {
      setProfilePageHint("[data-claim-node-status]", "Enter your current password to link this node.", "bad");
      return;
    }
    const button = $("[data-claim-code-confirm]");
    if (button) { button.disabled = true; button.textContent = "Linking…"; }
    try {
      const response = await fetch("/api/accounts/claim-confirm", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          identifier: state.session?.email || state.session?.nodeName || "",
          password, nodeId, code,
        }),
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || body.ok === false) throw new Error(body.error || `HTTP ${response.status}`);
      const nextSession = {
        ...(state.session || {}),
        nodes: Array.isArray(body.nodes) ? body.nodes : state.session?.nodes,
      };
      writeSession(nextSession);
      renderProfile(nextSession);
      state.claimNode.pendingNodeId = "";
      const codeRow = $("[data-claim-code-row]");
      if (codeRow) codeRow.classList.add("hidden");
      const nodeInput = $("[data-claim-node-input]");
      if (nodeInput) nodeInput.value = "";
      const codeInput = $("[data-claim-code-input]");
      if (codeInput) codeInput.value = "";
      setProfilePageHint("[data-claim-node-status]", `Linked "${nodeId}" to your account.`, "good");
    } catch (error) {
      const messages = {
        invalid_credentials: "Incorrect password.",
        no_such_node: "No node with that ID was found.",
        no_pending_claim: "No pending claim for that node. Send a new claim code.",
        bad_code: "That code is incorrect.",
      };
      setProfilePageHint(
        "[data-claim-node-status]",
        messages[error.message] || "Could not link the node. Check the code and try again.",
        "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Link node"; }
    }
  }

  // "Link this node to your account" (adhoc #120): the desktop app opens
  // /dashboard?link_node=<node>&link_ts=<ts>&link_sig=<sig> - a short-lived
  // grant signed with the node's own key. The signature proves node-key
  // control and consents to the link, so whoever is logged in HERE becomes the
  // owner with no password re-entry or confirmation code.
  function pendingLinkGrant() {
    const params = new URLSearchParams(location.search);
    const nodeName = (params.get("link_node") || "").trim();
    const ts = (params.get("link_ts") || "").trim();
    const sig = (params.get("link_sig") || "").trim();
    if (!validNodeName(nodeName.toLowerCase()) || !ts || !sig) return null;
    return { nodeName, ts, sig };
  }

  function offerLinkGrant(grant) {
    // Strip the one-time grant from the address bar first so refresh/back
    // can't replay it (and it doesn't linger in the visible URL), then show
    // the settings page's Nodes panel and ask for one explicit "Authenticate &
    // link" click. The grant overrides any existing association, so the click
    // is the moment of consent on the browser side. (Boot redirects the grant
    // to the settings document before calling this, so the panel exists here.)
    const params = new URLSearchParams(location.search);
    for (const key of ["link_node", "link_ts", "link_sig"]) params.delete(key);
    const rest = params.toString();
    window.history.replaceState(null, "", location.pathname + (rest ? `?${rest}` : ""));
    state.linkGrant = grant;
    setSettingsSection("nodes", { scroll: false });
    const row = $("[data-link-grant-row]");
    if (row) row.classList.remove("hidden");
    const text = $("[data-link-grant-text]");
    if (text) {
      text.textContent =
        `Link node "${grant.nodeName}" to this account (` +
        `${state.session?.nodeName || "you"})? This node will belong to you - ` +
        "any existing link is replaced.";
    }
  }

  async function redeemLinkGrant() {
    const grant = state.linkGrant;
    if (!grant) return;
    const button = $("[data-link-grant-confirm]");
    if (button) { button.disabled = true; button.textContent = "Linking…"; }
    setProfilePageHint("[data-claim-node-status]", `Linking "${grant.nodeName}" to your account…`, "");
    try {
      const response = await fetch("/api/accounts/link-grant", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          nodeName: grant.nodeName,
          ts: grant.ts,
          sig: grant.sig,
          user: state.session?.nodeName || "",
        }),
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || body.ok === false) throw new Error(body.error || `HTTP ${response.status}`);
      state.linkGrant = null;
      $("[data-link-grant-row]")?.classList.add("hidden");
      const nextSession = {
        ...(state.session || {}),
        nodes: Array.isArray(body.nodes) ? body.nodes : state.session?.nodes,
      };
      writeSession(nextSession);
      renderProfile(nextSession);
      setProfilePageHint(
        "[data-claim-node-status]",
        body.selfAccount
          ? `"${body.nodeId || grant.nodeName}" is this account - already yours.`
          : body.alreadyLinked
            ? `"${body.nodeId || grant.nodeName}" is already linked to your account.`
            : `Linked "${body.nodeId || grant.nodeName}" to your account.`,
        "good");
    } catch (error) {
      state.linkGrant = null;
      $("[data-link-grant-row]")?.classList.add("hidden");
      const messages = {
        unauthorized: "The link expired - click \"Link this node to your account\" in the node's app again.",
        bad_signature: "The link couldn't be verified - click the button in the node's app again.",
        grant_used: "That link was already used - click the button in the node's app again.",
        no_such_node: "That node isn't registered with the relay yet.",
        not_a_user: "This login can't own nodes - sign up as a user (email + password) first.",
        no_such_user: "Log in with a user account first, then open the link again.",
      };
      setProfilePageHint(
        "[data-claim-node-status]",
        messages[error.message] || "Could not link the node. Click the button in the node's app and try again.",
        "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Authenticate & link"; }
    }
  }

  async function saveNotificationPreferences() {
    const password = profilePassword("[data-notification-preferences-password]");
    if (!password) {
      setProfilePageHint("[data-notification-preferences-hint]", "Enter your current password to save notification settings.", "bad");
      return;
    }
    const button = $("[data-notification-preferences-save]");
    if (button) { button.disabled = true; button.textContent = "Saving..."; }
    try {
      await postProfile({
        emailNotifications: true,
        notificationPreferences: collectNotificationPreferences(),
      }, password);
      if ($("[data-notification-preferences-password]")) $("[data-notification-preferences-password]").value = "";
      setProfilePageHint("[data-notification-preferences-hint]", "Notification settings saved.", "good");
    } catch (_) {
      setProfilePageHint("[data-notification-preferences-hint]", "Could not save notification settings. Check your password and try again.", "bad");
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = "Save notifications";
      }
    }
  }

  async function deleteAccount() {
    const password = profilePassword("[data-profile-delete-password]");
    const confirm = ($("[data-profile-delete-confirm]")?.value || "").trim();
    if (!password) {
      setProfilePageHint("[data-profile-delete-hint]", "Enter your current password to delete this account.", "bad");
      return;
    }
    if (confirm !== "DELETE") {
      setProfilePageHint("[data-profile-delete-hint]", "Type DELETE to confirm account deletion.", "bad");
      return;
    }
    const button = $("[data-profile-delete-account]");
    if (button) { button.disabled = true; button.textContent = "Deleting..."; }
    try {
      await postProfile({ deleteAccount: true }, password);
      logout();
    } catch (_) {
      setProfilePageHint("[data-profile-delete-hint]", "Could not delete account. Check your password and try again.", "bad");
      if (button) { button.disabled = false; button.textContent = "Delete account"; }
    }
  }
  function repositoryMatchesQuery(repo, query) {
    if (!query) return true;
    const canonical = repoCanonicalIdentity(repo);
    const haystack = [
      repo.owner,
      repo.name,
      canonical.owner,
      canonical.name,
      repo.description,
      repo.channel,
      repo.source,
    ].join(" ").toLowerCase();
    return haystack.includes(query);
  }

  function groupMatchesGlobalSearch(group, query) {
    if (!query) return true;
    if (repositoryMatchesQuery(sourceOfTruth(group), query)) return true;
    return (group.members || []).some((member) => repositoryMatchesQuery(member, query));
  }

  function globalSearchRepoSummary(group, repo) {
    const members = group.members || [];
    const liveCount = members.reduce((count, member) => count + (repoIsLive(member) ? 1 : 0), 0);
    const nodeText = members.length > 1 ? `${liveCount} of ${members.length} nodes online` : (repoIsLive(repo) ? "online" : "offline");
    const visibility = repo.isPrivate ? "private" : "public";
    const description = String(repo.description || "").trim();
    return [visibility, nodeText, description].filter(Boolean).join(" - ");
  }

  function setGlobalSearchOpen(open) {
    state.globalSearch.open = Boolean(open);
    renderGlobalSearchResults();
  }

  function closeGlobalSearch(options = {}) {
    const input = $("[data-global-search]");
    if (options.clear && input) input.value = "";
    state.globalSearch.open = false;
    state.globalSearch.selectedIndex = 0;
    renderGlobalSearchResults();
  }

  function renderGlobalSearchResults() {
    const input = $("[data-global-search]");
    const panel = $("[data-global-search-panel]");
    const container = $("[data-global-search-results]");
    if (!input || !panel || !container) return;

    const query = input.value.trim().toLowerCase();
    const groups = groupRepositories(state.repositories || [])
      .filter((group) => groupMatchesGlobalSearch(group, query))
      .slice(0, 8);
    state.globalSearch.results = groups.map((group) => sourceOfTruth(group));
    state.globalSearch.selectedIndex = Math.min(
      state.globalSearch.selectedIndex,
      Math.max(state.globalSearch.results.length - 1, 0),
    );

    input.setAttribute("aria-expanded", state.globalSearch.open ? "true" : "false");
    panel.classList.toggle("hidden", !state.globalSearch.open);
    if (!state.globalSearch.open) return;

    if (state.repositoriesLoading) {
      container.innerHTML = `<div class="px-3 py-5 text-sm text-muted-foreground">${loadingHtml("Loading repositories...")}</div>`;
      return;
    }

    if (!groups.length) {
      container.innerHTML = '<div class="px-3 py-5 text-sm text-muted-foreground">No repositories match this search.</div>';
      return;
    }

    container.innerHTML = groups.map((group, index) => {
      const repo = sourceOfTruth(group);
      const key = repoKey(repo);
      const selected = index === state.globalSearch.selectedIndex;
      return `
        <button
          type="button"
          data-global-search-result
          data-dashboard-open-repo="${escapeHtml(key)}"
          role="option"
          aria-selected="${selected ? "true" : "false"}"
          class="grid w-full grid-cols-[1.25rem_minmax(0,1fr)] items-center gap-2 px-3 py-2 text-left text-sm transition-colors ${selected ? "bg-secondary text-foreground" : "text-muted-foreground hover:bg-secondary/70 hover:text-foreground"}"
        >
          <i data-lucide="book-marked" class="h-3.5 w-3.5 text-muted-foreground"></i>
          <span class="min-w-0">
            <span class="block truncate font-medium text-foreground">${escapeHtml(key)}</span>
            <span class="block truncate text-xs text-muted-foreground">${escapeHtml(globalSearchRepoSummary(group, repo))}</span>
          </span>
        </button>`;
    }).join("");
    window.lucide?.createIcons();
  }

  function moveGlobalSearchSelection(delta) {
    const results = $$("[data-global-search-result]");
    if (!results.length) return;
    state.globalSearch.selectedIndex = (state.globalSearch.selectedIndex + delta + results.length) % results.length;
    results.forEach((button, index) => {
      const selected = index === state.globalSearch.selectedIndex;
      button.setAttribute("aria-selected", selected ? "true" : "false");
      button.classList.toggle("bg-secondary", selected);
      button.classList.toggle("text-foreground", selected);
      button.classList.toggle("text-muted-foreground", !selected);
      if (selected) button.scrollIntoView({ block: "nearest" });
    });
  }

  function selectGlobalSearchResult(key = "") {
    const selected = $('[data-global-search-result][aria-selected="true"]') || $("[data-global-search-result]");
    const wanted = key || selected?.dataset?.dashboardOpenRepo || repoKey(state.globalSearch.results[state.globalSearch.selectedIndex] || {});
    if (!wanted) return;
    closeGlobalSearch({ clear: true });
    openRepoPage(wanted);
  }

  function focusGlobalSearch() {
    const shell = $("[data-global-search-shell]");
    const input = $("[data-global-search]");
    if (!input || (shell && !shell.getClientRects().length)) return false;
    state.globalSearch.open = true;
    state.globalSearch.selectedIndex = 0;
    renderGlobalSearchResults();
    input.focus();
    input.select();
    return true;
  }

  // A repo is reachable when its own host is live OR - for a public repo - a peer
  // mirroring the same logical repo is online and the relay serves it in place
  // through the repo's own URL (adhoc #61). cloneOnline is the worker's group
  // verdict; fall back to liveHost for older payloads that predate it.
  function repoIsLive(repo) {
    return Boolean(repo?.cloneOnline ?? repo?.liveHost);
  }
  // Live, but the named source of truth is down - a mirror node is serving it.
  function repoServedByMirror(repo) {
    return repoIsLive(repo) && !repo?.liveHost;
  }
  // The signed-in account owns this repo when their node name matches the repo
  // owner slug (case-insensitive). Owners get to keep their own offline issue
  // submissions visible until their source-of-truth node drains them (#379).
  function isRepoOwner(repo) {
    const owner = String(repo?.owner || "").trim().toLowerCase();
    const me = String(state.session?.nodeName || "").trim().toLowerCase();
    return Boolean(owner && me && owner === me);
  }

  function groupRepoMetric(group, keys) {
    let best = null;
    for (const member of group.members || []) {
      const value = repoCount(member, keys);
      if (value === null) continue;
      best = best === null ? value : Math.max(best, value);
    }
    return best === null ? 0 : best;
  }

  function normalizeActivityWeeks(value) {
    const raw = Array.isArray(value) ? value.slice(-52) : [];
    const series = raw.map((item) => {
      const number = Number(item);
      return Number.isFinite(number) && number > 0 ? number : 0;
    });
    while (series.length < 52) series.unshift(0);
    return series;
  }

  function groupActivityWeeks(group) {
    const buckets = Array.from({ length: 52 }, () => 0);
    for (const member of group.members || []) {
      const series = normalizeActivityWeeks(member.activityWeeks);
      for (let i = 0; i < buckets.length; i += 1) {
        buckets[i] = Math.max(buckets[i], series[i] || 0);
      }
    }
    return buckets;
  }

  function stableMockNumber(seed, min, max) {
    const text = String(seed || "forkmesh");
    let hash = 0;
    for (let index = 0; index < text.length; index += 1) {
      hash = ((hash << 5) - hash) + text.charCodeAt(index);
      hash |= 0;
    }
    const span = Math.max(1, max - min + 1);
    return min + (Math.abs(hash) % span);
  }

  function repoLanguage(repo) {
    return String(repo.language || repo.primaryLanguage || repo.stack || "TypeScript");
  }

  function repoLicense(repo) {
    return String(repo.license || repo.licenseName || "MIT License");
  }

  function repoMetricChip(label, icon, value) {
    return `
      <span class="inline-flex min-w-0 items-center gap-1.5 rounded-md border border-border bg-background px-2 py-1 text-[11px] text-muted-foreground">
        <i data-lucide="${icon}" class="h-3 w-3 shrink-0"></i>
        <span class="truncate">${label}</span>
        <span class="ml-auto font-mono text-foreground">${formatCount(value)}</span>
      </span>
    `;
  }

  function repoActivitySparkline(values, options = {}) {
    const series = normalizeActivityWeeks(values);
    const max = Math.max(1, ...series);
    const total = series.reduce((sum, n) => sum + n, 0);
    const totalHint = Number(options.totalHint);
    const displayTotal = total || (Number.isFinite(totalHint) && totalHint > 0 ? totalHint : 0);
    const loading = Boolean(options.loading);
    const title = loading
      ? `Loading activity for ${formatCount(displayTotal)} commits`
      : total
        ? `${formatCount(total)} commits in the past 52 weeks`
        : `${formatCount(displayTotal)} commits`;
    const bars = series.map((value) => {
      const height = value > 0 ? Math.max(3, Math.round((value / max) * 30)) : 2;
      const tone = value > 0 ? "bg-primary" : loading ? "bg-muted-foreground/30" : "bg-muted-foreground/20";
      return `<span class="repo-activity-bar ${tone}" style="height:${height}px"></span>`;
    }).join("");
    return `
      <div data-repo-activity-sparkline class="repo-activity-sparkline w-full" title="${escapeHtml(title)}" aria-label="${escapeHtml(title)}">
        <div class="mb-1 flex items-center justify-between gap-2 text-[10px] font-mono text-muted-foreground">
          <span>52 weeks</span>
          <span>${loading ? "loading" : `${formatCount(displayTotal)} commits`}</span>
        </div>
        <div class="repo-activity-bars h-8">${bars}</div>
      </div>
    `;
  }

  function repositoryCard(group) {
    const origin = sourceOfTruth(group);
    const repo = group.primary;
    const key = repoKey(origin);
    const live = repoIsLive(origin);
    const viaMirror = repoServedByMirror(origin);
    const visibility = origin.isPrivate ? "private" : "public";
    const statusClass = live ? "text-primary" : "text-muted-foreground";
    const nodeCount = group.members.length;
    const liveCount = group.members.reduce((n, m) => n + (repoIsLive(m) ? 1 : 0), 0);
    const statusText = nodeCount > 1
      ? `${liveCount} of ${nodeCount} nodes`
      : (viaMirror ? "via mirror" : live ? "online" : "offline");
    const metrics = [
      repoMetricChip("Issues", "circle-dot", groupRepoMetric(group, ["issueCount", "issues", "issuesCount", "openIssues"])),
      repoMetricChip("Commits", "git-commit-horizontal", groupRepoMetric(group, ["commitCount", "commits", "commitHistory"])),
      repoMetricChip("Pulls", "git-pull-request", groupRepoMetric(group, ["pullCount", "pulls", "pullsCount", "openPulls", "pullRequests"])),
      repoMetricChip("Discussions", "message-square", groupRepoMetric(group, ["discussionCount", "discussions"])),
    ].join("");
    const commitTotal = groupRepoMetric(group, ["commitCount", "commits", "commitHistory"]);
    const activityWeeks = groupActivityWeeks(group);
    const language = repoLanguage(origin);
    return `
      <div data-repo="${escapeHtml(key.toLowerCase())}" data-dashboard-open-repo="${escapeHtml(key)}" data-clone-url="${escapeHtml(cloneUrl(origin))}" role="link" tabindex="0" aria-label="Open ${escapeHtml(key)}" class="repo-card group cursor-pointer px-4 py-3 hover:bg-secondary/40 transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary/60">
        <div class="repo-layout grid gap-3 md:grid-cols-[minmax(0,1fr)_minmax(10rem,12rem)] md:items-center">
          <div class="min-w-0">
            <div class="flex min-w-0 items-center gap-2">
              <p class="min-w-0 truncate text-sm font-medium text-foreground">
                <span class="text-muted-foreground">${escapeHtml(origin.owner || "owner")}/</span>${escapeHtml(origin.name || "repository")}
              </p>
              <span class="shrink-0 rounded-full border border-border px-2 py-0.5 text-[10px] font-mono ${statusClass}">
                ${statusText}
              </span>
              <button data-repo-star-button data-repo-key="${escapeHtml(key)}" type="button" aria-pressed="false" aria-label="Star ${escapeHtml(key)}" class="ml-auto hidden shrink-0 items-center gap-1 rounded-md border border-border bg-secondary px-2 py-1 text-xs text-foreground hover:bg-background sm:inline-flex">
                <i data-lucide="star" data-repo-star-icon class="h-3.5 w-3.5 text-muted-foreground"></i>
                <span data-repo-star-label>Star</span>
                <span data-repo-star-count class="font-mono text-muted-foreground">${formatCount(0)}</span>
              </button>
            </div>
            <p class="mt-1 truncate text-xs text-muted-foreground">${escapeHtml(repo.description || origin.description || "No description published.")}</p>
            <div class="mt-2 flex items-center gap-x-4 gap-y-1.5 flex-wrap">
              <span class="flex items-center gap-1.5 text-xs text-muted-foreground">
                <span data-repo-language-dot class="w-2 h-2 rounded-full bg-primary"></span>${escapeHtml(language)}
              </span>
              <span class="flex items-center gap-1.5 text-xs text-muted-foreground">
                <i data-lucide="${origin.isPrivate ? "lock" : "globe-2"}" class="h-3 w-3"></i>${escapeHtml(visibility)}
              </span>
              <span class="flex items-center gap-1 text-xs text-muted-foreground">
                <i data-lucide="radio" class="w-3 h-3"></i>${nodeCount > 1 ? `${nodeCount} nodes` : (viaMirror ? "served by mirror" : live ? "live host" : "host offline")}
              </span>
              <span class="text-xs text-muted-foreground font-mono">updated ${escapeHtml(formatDate(repo.updatedAt || repo.lastSync))}</span>
            </div>
            <div class="mt-3 grid grid-cols-2 gap-1.5 sm:grid-cols-4">${metrics}</div>
          </div>
          <div class="min-w-0">
            ${repoActivitySparkline(activityWeeks, { totalHint: commitTotal })}
          </div>
        </div>
      </div>
    `;
  }

  function profileRepositoryRow(group) {
    const repo = sourceOfTruth(group);
    const key = repoKey(repo);
    const visibility = repo.isPrivate ? "Private" : "Public";
    const language = repoLanguage(repo);
    const license = repoLicense(repo);
    const commitTotal = groupRepoMetric(group, ["commitCount", "commits", "commitHistory"]);
    const activityWeeks = groupActivityWeeks(group);
    return `<article data-profile-repository-row class="grid gap-3 px-4 py-5 md:grid-cols-[minmax(0,1fr)_12rem]">
      <a href="${escapeHtml(repoPathUrl(repo))}" class="block min-w-0 text-left">
        <span class="flex min-w-0 flex-wrap items-center gap-2">
          <span class="min-w-0 truncate text-lg font-semibold text-accent hover:underline">${escapeHtml(repo.name || "repository")}</span>
          <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${escapeHtml(visibility)}</span>
        </span>
        <span class="mt-1 block text-xs text-muted-foreground">Published from ${escapeHtml(repo.owner || "owner")}/${escapeHtml(repo.name || "repository")}</span>
        <span class="mt-2 line-clamp-2 text-sm text-muted-foreground">${escapeHtml(repo.description || "No description published.")}</span>
        <span class="mt-3 flex flex-wrap items-center gap-4 text-xs text-muted-foreground">
          <span class="inline-flex items-center gap-1.5"><span data-repo-language-dot class="h-2 w-2 rounded-full bg-primary"></span>${escapeHtml(language)}</span>
          <span class="inline-flex items-center gap-1.5"><i data-lucide="scale" class="h-3.5 w-3.5"></i>${escapeHtml(license)}</span>
          <span>Updated ${escapeHtml(formatDate(repo.updatedAt || repo.lastSync))}</span>
        </span>
      </a>
      <div class="grid content-center gap-3">
        <button data-repo-star-button data-repo-key="${escapeHtml(key)}" type="button" aria-pressed="false" aria-label="Star ${escapeHtml(key)}" class="justify-self-end inline-flex items-center gap-1 rounded-md border border-border bg-secondary px-3 py-1.5 text-xs font-semibold text-foreground hover:bg-background">
          <i data-lucide="star" data-repo-star-icon class="h-3.5 w-3.5 text-muted-foreground"></i>
          <span data-repo-star-label>Star</span>
          <span data-repo-star-count class="font-mono text-muted-foreground">${formatCount(0)}</span>
        </button>
        ${repoActivitySparkline(activityWeeks, { totalHint: commitTotal })}
      </div>
    </article>`;
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

    if (state.repositoriesLoading) {
      if (list) {
        list.innerHTML = `<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">${loadingHtml("Loading repositories from an online node...")}</div>`;
      }
      if (summary) summary.textContent = "Loading repositories from an online node";
      if (prev) {
        prev.disabled = true;
        prev.classList.add("opacity-40");
      }
      if (next) {
        next.disabled = true;
        next.classList.add("opacity-40");
      }
      if (pageList) pageList.innerHTML = "";
      return;
    }

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
    hydrateRepoStarButtons(list);
  }

  function renderSidebarRepositories(session) {
    const list = $("[data-sidebar-repo-list]");
    const count = $("[data-sidebar-repo-count]");
    const groups = Array.isArray(state.filteredGroups)
      ? state.filteredGroups
      : groupRepositories(state.repositories || []);
    if (count) count.textContent = formatCount(groups.length);
    if (!list) return;
    if (!groups.length) {
      list.innerHTML = '<div class="rounded-md border border-border bg-background px-2.5 py-2 text-xs text-muted-foreground">No mirrored repositories yet.</div>';
      return;
    }
    list.innerHTML = groups.slice(0, 12).map((group) => {
      const repo = sourceOfTruth(group);
      const key = repoKey(repo);
      const live = repoIsLive(repo);
      return `
        <a href="${escapeHtml(repoPathUrl(repo))}" class="group flex min-w-0 items-center gap-2 rounded-md px-2.5 py-2 text-left text-xs text-muted-foreground transition-colors hover:bg-secondary hover:text-foreground">
          <span class="h-2 w-2 shrink-0 rounded-full ${live ? "bg-primary" : "bg-muted-foreground/40"}"></span>
          <span class="min-w-0 flex-1 truncate"><span class="text-muted-foreground">${escapeHtml(repo.owner || "owner")}/</span><span class="text-foreground">${escapeHtml(repo.name || "repository")}</span></span>
        </a>`;
    }).join("");
  }

  function renderHomeRepositories() {
    const container = $("[data-home-top-repositories]");
    if (!container) return;
    const query = ($("[data-home-repo-search]")?.value || "").trim().toLowerCase();
    const groups = groupRepositories(state.repositories || []).filter((group) => {
      return repositoryMatchesQuery(sourceOfTruth(group), query);
    }).slice(0, 8);
    container.innerHTML = `
      ${groups.length
        ? `<div class="grid gap-1">${groups.map((group) => {
            const repo = sourceOfTruth(group);
            const key = repoKey(repo);
            return `<a href="${escapeHtml(repoPathUrl(repo))}" class="flex min-w-0 items-center gap-2 rounded-md px-2 py-1.5 text-left text-sm text-muted-foreground hover:bg-secondary hover:text-foreground">
              <i data-lucide="book-marked" class="h-3.5 w-3.5 shrink-0"></i>
              <span class="min-w-0 truncate">${escapeHtml(key)}</span>
            </a>`;
          }).join("")}</div>`
        : '<div class="px-2 py-3 text-sm text-muted-foreground">No repositories match this filter.</div>'}
    `;
    window.lucide?.createIcons();
  }

  function homeFeedNotificationCard(item) {
    const href = String(item?.href || "").trim();
    const tag = href ? "a" : "div";
    const hrefAttr = href ? ` href="${escapeHtml(href)}"` : "";
    return `
      <${tag}${hrefAttr} class="block overflow-hidden rounded-lg border border-border bg-card hover:bg-secondary/40">
        <div class="flex min-w-0 items-start gap-3 px-4 py-4">
          <span class="flex h-10 w-10 shrink-0 items-center justify-center rounded-full border border-border bg-secondary text-primary">
            <i data-lucide="${notificationIcon(item.kind)}" class="h-5 w-5"></i>
          </span>
          <span class="min-w-0 flex-1">
            <span class="block text-sm font-semibold text-foreground">${escapeHtml(item.title || "ForkMesh notification")}</span>
            <span class="mt-1 block text-sm leading-5 text-muted-foreground">${escapeHtml(item.body || item.repo || "ForkMesh activity")}</span>
            <span class="mt-2 block text-xs text-muted-foreground">${escapeHtml(notificationTimeLabel(item.ts))}${item.repo ? ` · ${escapeHtml(item.repo)}` : ""}</span>
          </span>
        </div>
      </${tag}>
    `;
  }

  function homeFeedRepositoryCard(group) {
    const repo = sourceOfTruth(group);
    const key = repoKey(repo);
    const live = repoIsLive(repo);
    const viaMirror = repoServedByMirror(repo);
    const description = repo.description || "No description published.";
    const updated = repo.updatedAt || repo.lastSync;
    return `
      <article class="overflow-hidden rounded-lg border border-border bg-card">
        <div class="flex min-w-0 items-start gap-3 px-4 py-4">
          <span class="flex h-10 w-10 shrink-0 items-center justify-center rounded-md border border-border bg-secondary ${live ? "text-primary" : "text-muted-foreground"}">
            <i data-lucide="${viaMirror ? "radio" : "book-marked"}" class="h-5 w-5"></i>
          </span>
          <div class="min-w-0 flex-1">
            <p class="text-sm text-muted-foreground">
              <a href="${escapeHtml(repoPathUrl(repo))}" class="font-semibold text-accent hover:underline">${escapeHtml(key)}</a>
              ${live ? "is available on the mesh" : "is waiting for a live host"}
            </p>
            <p class="mt-2 line-clamp-2 text-sm leading-5 text-muted-foreground">${escapeHtml(description)}</p>
            <p class="mt-3 flex flex-wrap items-center gap-3 text-xs text-muted-foreground">
              <span class="inline-flex items-center gap-1.5"><span class="h-2.5 w-2.5 rounded-full ${live ? "bg-primary" : "bg-muted-foreground/40"}"></span>${live ? (viaMirror ? "served by mirror" : "live host") : "offline"}</span>
              <span>${escapeHtml(formatDate(updated))}</span>
            </p>
          </div>
        </div>
      </article>
    `;
  }

  function renderHomeFeed() {
    const container = $("[data-home-feed]");
    if (!container) return;
    const notifications = Array.isArray(state.notifications) ? state.notifications.slice(0, 4) : [];
    const repoCards = groupRepositories(state.repositories || []).slice(0, 4).map(homeFeedRepositoryCard);
    const cards = notifications.map(homeFeedNotificationCard).concat(repoCards).slice(0, 8);
    container.innerHTML = cards.length
      ? cards.join("")
      : '<div class="rounded-lg border border-border bg-card px-4 py-6 text-sm text-muted-foreground">No dashboard activity yet. Publish a repository or receive a notification to start the feed.</div>';
    window.lucide?.createIcons();
  }

  function renderHomeChangelog() {
    const container = $("[data-home-changelog-list]");
    if (!container) return;
    const items = [
      { label: "The Agent Mesh", meta: "v0.5.0 · June 2026", href: "/changelog" },
      { label: "Autonomous agents", meta: "v0.4.0 · June 2026", href: "/changelog" },
      { label: "Signed patch pull requests", meta: "Blog", href: "/blog/signed-patch-pull-requests/" },
    ];
    container.innerHTML = items.map((item) => `
      <article class="relative">
        <span class="absolute -left-[1.18rem] top-1.5 h-2 w-2 rounded-full bg-muted-foreground"></span>
        <p class="text-xs text-muted-foreground">${escapeHtml(item.meta)}</p>
        <a href="${escapeHtml(item.href)}" class="mt-1 block text-sm font-semibold leading-5 text-foreground hover:text-accent">${escapeHtml(item.label)}</a>
      </article>
    `).join("");
  }

  // Home left-rail "Active agent sessions" (adhoc #81). Renders the aggregated
  // non-terminal agent runs collected by loadHomeAgentSessions(). The panel
  // stays hidden until there is at least one active session so it never shows
  // an empty box to accounts that don't run agents.
  function renderHomeAgentSessions() {
    const panel = $("[data-home-agent-sessions-panel]");
    const container = $("[data-home-agent-sessions]");
    if (!panel || !container) return;
    const sessions = Array.isArray(state.homeAgentSessions) ? state.homeAgentSessions : [];
    if (!sessions.length) {
      panel.classList.add("hidden");
      container.innerHTML = "";
      return;
    }
    panel.classList.remove("hidden");
    container.innerHTML = sessions.slice(0, 8).map((entry) => {
      const agent = entry.agent || {};
      const repo = entry.repo || {};
      const issueLabel = repoAgentIssueLabel(agent) || repoKey(repo);
      return `
        <a href="${escapeHtml(repoPathUrl(repo) + "/agents")}" class="grid gap-1 rounded-md px-2 py-1.5 text-left text-xs hover:bg-secondary">
          <div class="flex min-w-0 items-center gap-2">
            <span class="h-2 w-2 shrink-0 rounded-full bg-yellow-500"></span>
            <span class="min-w-0 flex-1 truncate font-medium text-foreground">${escapeHtml(issueLabel)}</span>
            <span class="shrink-0 font-mono ${repoAgentStatusTone(agent.status)}">${escapeHtml(agent.status || "unknown")}</span>
          </div>
          <span class="truncate pl-4 text-muted-foreground">${escapeHtml(repoKey(repo))}</span>
        </a>`;
    }).join("");
    window.lucide?.createIcons();
  }

  // Fetch the agent-session list for every repo the session can assign agents
  // to and keep only the still-running (non-terminal) ones. The per-repo
  // /agents/list endpoint is owner-gated, so this only runs for a signed-in
  // account and silently skips repos it can't read.
  async function loadHomeAgentSessions() {
    if (!state.session?.nodeName) return;
    const repos = (state.repositories || []).filter((repo) =>
      repo?.owner && repo?.name && sessionCanAssignAgent(repo));
    if (!repos.length) {
      state.homeAgentSessions = [];
      renderHomeAgentSessions();
      return;
    }
    const results = await Promise.all(repos.map(async (repo) => {
      try {
        const agents = await requestRepoAgentsList(repo);
        return agents
          .filter((agent) => repoAgentsCanPrompt(agent.status))
          .map((agent) => ({ repo, agent }));
      } catch (_) {
        return [];
      }
    }));
    state.homeAgentSessions = results.flat();
    renderHomeAgentSessions();
  }

  // The repo groups the profile pages list: the whole catalog on the
  // session's own dashboard, but ONLY the viewed account's repos in
  // public-profile mode (/@name).
  function profileRepositoryGroups() {
    let groups = groupRepositories(state.repositories || []);
    if (state.publicProfile) {
      const aliases = profileContributionAliases(state.publicProfile);
      groups = groups.filter((group) =>
        repoBelongsToProfile(sourceOfTruth(group), aliases) ||
        (group.members || []).some((member) => repoBelongsToProfile(member, aliases)));
    }
    return groups;
  }

  function renderProfileRepositories() {
    const container = $("[data-profile-repo-list]");
    if (!container) return;
    const query = ($("[data-profile-repo-search]")?.value || "").trim().toLowerCase();
    const groups = profileRepositoryGroups().filter((group) => {
      return repositoryMatchesQuery(sourceOfTruth(group), query);
    });
    container.innerHTML = groups.length
      ? groups.map((group) => profileRepositoryRow(group)).join("")
      : '<div class="px-4 py-8 text-sm text-muted-foreground">No repositories match this filter.</div>';
    window.lucide?.createIcons();
    hydrateRepoStarButtons(container);
  }

  function renderProfileRepositoryCount(count = profileRepositoryGroups().length) {
    $$("[data-profile-repo-count]").forEach((element) => {
      element.textContent = formatCount(count);
    });
  }

  function applyRepositoryFilter() {
    const query = ($("#repoSearch")?.value || "").trim().toLowerCase();
    state.filteredRepositories = state.repositories.filter((repo) =>
      repositoryMatchesQuery(repo, query));
    state.filteredGroups = groupRepositories(state.filteredRepositories);
    updateRepositoryPagination();
    renderSidebarRepositories();
    renderHomeRepositories();
    renderHomeFeed();
    renderHomeChangelog();
    renderHomeAgentSessions();
    renderProfileRepositories();
    renderProfileRepositoryCount();
  }

  function renderRepositories(repositories, session) {
    state.repositoriesLoading = false;
    state.repositories = Array.isArray(repositories) ? repositories : [];
    state.filteredRepositories = state.repositories.slice();
    state.filteredGroups = groupRepositories(state.repositories);
    state.page = 1;

    const count = $("[data-repo-count]");
    if (count) count.textContent = `${formatCount(state.filteredGroups.length)} mirrored`;

    renderProfileRepositoryCount();
    applyRepositoryFilter();
    renderProfileContributionGraph();
    renderGlobalSearchResults();
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
    // The About right-hand rail only belongs next to the file tree/README
    // (owner decision 2026-07-12, discussion #2): every other tab — commits,
    // releases, issues, projects, pulls, discussions, insights, mirrors,
    // agents — goes full-width instead of leaving a rail with nothing beside
    // it to explain. The explorer focus mode independently hides the rail
    // (and collapses this same grid) while active on the code tab.
    const contentGrid = detail.querySelector("[data-repo-content-grid]");
    const about = detail.querySelector("[data-repo-about]");
    const showAbout = tab === "code";
    contentGrid?.classList.toggle("lg:grid-cols-[minmax(0,1fr)_18rem]", showAbout);
    contentGrid?.classList.toggle("lg:grid-cols-1", !showAbout);
    about?.classList.toggle("hidden", !showAbout);
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
      // Leaving the tab closes any open agent detail page (adhoc #259) so
      // returning to Agents lands on the session list, not a stale transcript.
      state.agentsView.selectedAgentId = null;
    }
    if (!state.selectedRepo) return;
    navigateHistory(tab === "code"
      ? (state.repoCodeUrl || repoPathUrl(state.selectedRepo))
      : `${repoPathUrl(state.selectedRepo)}/${tab}`);
    if (["commits", "issues", "projects", "pulls", "discussions", "releases", "insights", "agents"].includes(tab) && !state.loadedRepoTabs?.[tab]) {
      if (!state.loadedRepoTabs) state.loadedRepoTabs = {};
      state.loadedRepoTabs[tab] = true;
      if (tab === "commits") loadRepoCommits(state.selectedRepo);
      else if (tab === "issues") loadRepoIssues(state.selectedRepo);
      else if (tab === "projects") loadRepoProjects(state.selectedRepo);
      else if (tab === "releases") loadRepoReleases(state.selectedRepo);
      else if (tab === "insights") loadRepoInsights(state.selectedRepo);
      else if (tab === "agents") loadRepoAgents(state.selectedRepo);
      else loadRepoCollection(state.selectedRepo, tab, `[data-repo-${tab}]`);
    } else if (tab === "issues") {
      // Re-selecting the tab should return to the issues list even if the
      // new-issue compose form was left open.
      renderRepoIssues();
    } else if (tab === "projects") {
      renderRepoProjects();
    } else if (tab === "agents") {
      loadRepoAgents(state.selectedRepo);
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
    crumb.innerHTML = [`<button type="button" data-dashboard-tree-path="" class="font-semibold text-accent hover:underline">${escapeHtml(rootLabel)}</button>`]
      .concat(parts.map((part, index) => {
        acc = acc ? `${acc}/${part}` : part;
        const isBlobTerminal = terminalKind === "blob" && index === parts.length - 1;
        const pathAttribute = isBlobTerminal ? "data-dashboard-blob-path" : "data-dashboard-tree-path";
        return `<span class="text-muted-foreground">/</span> <button type="button" ${pathAttribute}="${escapeHtml(acc)}" class="${isBlobTerminal ? "text-foreground" : "text-accent hover:underline"}">${escapeHtml(part)}</button>`;
      })).join(" ");
  }

  function servedMirrorStats(name) {
    const servedName = String(name || "").trim().toLowerCase();
    if (!servedName) return "";
    const mirror = (state.repoMirrors || []).find((candidate) => {
      const mirrorName = String(candidate.owner || candidate.node || candidate.name || "").trim().toLowerCase();
      return mirrorName && mirrorName === servedName;
    });
    if (!mirror) return "";
    const counters = [];
    const clones = normalizedCount(mirror.clonesServed);
    const website = normalizedCount(mirror.websiteServed);
    if (clones !== null) counters.push(`${formatCount(clones)} clones`);
    if (website !== null) counters.push(`${formatCount(website)} website requests`);
    return counters.join(" - ");
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
        badge.textContent = [`served by ${name}`, speed, servedMirrorStats(name)]
          .filter(Boolean)
          .join(" - ");
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
        return `<button type="button" data-repo-explorer-entry data-dashboard-${isTree ? "tree" : "blob"}-path="${escapeHtml(childPath)}" class="${repoExplorerRowClass(false)}">${fileIconHtml(entry, "h-3.5 w-3.5 shrink-0")}<span class="min-w-0 truncate">${escapeHtml(entry.name || "entry")}</span></button>`;
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

    // The grid collapse matches the base class renderRepoDetail emits (lg:) —
    // legitimate here because focus mode hides the About rail entirely.
    contentGrid?.classList.toggle("lg:grid-cols-[minmax(0,1fr)_18rem]", !active);
    contentGrid?.classList.toggle("lg:grid-cols-1", active);
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
        const data = await fetchRepoJson(repoLiveUrl(repo, "tree", { path }));
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
    // pulls/ metadata lives on its own dedicated branch (issue #399): the
    // host resolves a pulls/ path to refs/heads/forkmesh/pulls only when the
    // request names NO explicit ref, so pull readers pass ref:"" to defer to
    // the host instead of pinning the stale pulls/ copy left on the selected
    // code branch. Every other caller keeps the selected-branch default.
    if (!("ref" in (params || {}))) query.set("ref", repoSelectedBranch(repo));
    else if (!String(params.ref || "")) query.delete("ref");
    const version = repoDataVersion(repo);
    if (version) query.set("fmv", version);
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
  function commitSummaryField(commit, ...keys) {
    for (const key of keys) {
      const value = commit?.[key];
      if (value !== undefined && value !== null && String(value).trim()) {
        return String(value).trim();
      }
    }
    return "";
  }

  function updateRepoCommitSummary(commit, repo) {
    const summary = $("[data-repo-commit-summary]");
    if (!summary) return;
    const data = commit && typeof commit === "object" ? commit : {};
    const hash = commitSummaryField(data, "hash", "commitHash", "commit")
      || String(repo.rootCommit || repo.latestCommit || repo.commit || "");
    const subject = commitSummaryField(data, "subject", "message", "commitMessage");
    const author = commitSummaryField(data, "author", "committer", "name")
      || String(repo.maintainer || repo.owner || "maintainer");
    const date = commitSummaryField(data, "date", "committedAt", "updatedAt")
      || String(repo.updatedAt || repo.lastSync || "");
    const avatar = summary.querySelector("[data-repo-commit-avatar]");
    const authorNode = summary.querySelector("[data-repo-commit-author]");
    const messageNode = summary.querySelector("[data-repo-commit-message]");
    const hashNode = summary.querySelector("[data-repo-commit-hash]");
    const dateNode = summary.querySelector("[data-repo-commit-date]");
    if (avatar) avatar.textContent = (author[0] || repo.owner?.[0] || "F").toUpperCase();
    if (authorNode) authorNode.textContent = author;
    if (messageNode) messageNode.textContent = subject || "published latest mirror metadata";
    if (hashNode) hashNode.textContent = hash ? hash.slice(0, 7) : "live";
    if (dateNode) dateNode.textContent = formatTimeAgo(date);
  }

  function mdSafeUrl(url) {
    const trimmed = String(url || "").trim();
    if (/^(https?:|mailto:|#|\/)/i.test(trimmed)) return trimmed;
    if (/^[a-z0-9][a-z0-9+.-]*:/i.test(trimmed)) return "#";
    return trimmed;
  }

  // Operates on already-`escapeHtml`d text so the captured groups (urls,
  // labels, code) are safe to splice back into HTML without re-escaping.
  function renderMarkdownInline(escapedText) {
    return escapedText
      .replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g, (_, alt, url) =>
        `<img src="${mdSafeUrl(url)}" alt="${alt}" class="my-2 max-w-full rounded-md border border-border" loading="lazy" />`)
      .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (_, label, url) =>
        `<a href="${mdSafeUrl(url)}" class="dashboard-accent-link underline underline-offset-2 hover:text-foreground" target="_blank" rel="noopener noreferrer nofollow ugc">${label}</a>`)
      .replace(/(\*\*|__)(.+?)\1/g, "<strong>$2</strong>")
      .replace(/(^|[^\w*])\*(?!\*)([^*]+)\*(?!\*)/g, "$1<em>$2</em>")
      .replace(/(^|[^\w_])_(?!_)([^_]+)_(?!_)/g, "$1<em>$2</em>")
      .replace(/`([^`]+)`/g, (_, code) => `<code class="rounded bg-secondary px-1.5 py-0.5 font-mono text-xs">${code}</code>`);
  }

  // Small, dependency-free markdown renderer: headings, fenced code blocks,
  // block quotes, ordered/unordered lists, hr, and paragraphs, with basic
  // inline formatting, links and images. Not a full CommonMark
  // implementation - readmes just need to look reasonable, not pixel-match
  // GitHub.
  function renderMarkdown(raw) {
    const lines = String(raw ?? "").replace(/\r\n?/g, "\n").split("\n");
    const out = [];
    let list = null;
    const closeList = () => {
      if (!list) return;
      const tag = list.type;
      out.push(`<${tag} class="${tag === "ol" ? "list-decimal" : "list-disc"} my-2 ml-5 space-y-1">${list.items.join("")}</${tag}>`);
      list = null;
    };
    let i = 0;
    while (i < lines.length) {
      const line = lines[i];
      const fence = line.match(/^```(.*)$/);
      if (fence) {
        closeList();
        const code = [];
        i++;
        while (i < lines.length && !/^```/.test(lines[i])) { code.push(lines[i]); i++; }
        i++;
        out.push(`<pre class="my-3 overflow-auto rounded-md border border-border bg-secondary/40 p-3 text-xs"><code class="font-mono">${escapeHtml(code.join("\n"))}</code></pre>`);
        continue;
      }
      const heading = line.match(/^(#{1,6})\s+(.*)$/);
      if (heading) {
        closeList();
        const level = heading[1].length;
        const size = { 1: "text-2xl", 2: "text-xl", 3: "text-lg", 4: "text-base", 5: "text-sm", 6: "text-xs" }[level];
        out.push(`<h${level} class="${size} font-semibold text-foreground mt-5 mb-2 first:mt-0">${renderMarkdownInline(escapeHtml(heading[2]))}</h${level}>`);
        i++;
        continue;
      }
      if (/^(-{3,}|\*{3,}|_{3,})\s*$/.test(line)) {
        closeList();
        out.push('<hr class="my-4 border-border" />');
        i++;
        continue;
      }
      if (/^>\s?/.test(line)) {
        closeList();
        const quote = [];
        while (i < lines.length && /^>\s?/.test(lines[i])) { quote.push(lines[i].replace(/^>\s?/, "")); i++; }
        out.push(`<blockquote class="my-3 border-l-2 border-border pl-3 text-muted-foreground">${renderMarkdownInline(escapeHtml(quote.join(" ")))}</blockquote>`);
        continue;
      }
      const unordered = line.match(/^\s*[-*+]\s+(.*)$/);
      const ordered = line.match(/^\s*\d+\.\s+(.*)$/);
      if (unordered || ordered) {
        const type = ordered ? "ol" : "ul";
        if (!list || list.type !== type) { closeList(); list = { type, items: [] }; }
        list.items.push(`<li>${renderMarkdownInline(escapeHtml((unordered || ordered)[1]))}</li>`);
        i++;
        continue;
      }
      if (!line.trim()) {
        closeList();
        i++;
        continue;
      }
      closeList();
      const para = [line];
      i++;
      while (i < lines.length && lines[i].trim()
        && !/^(#{1,6})\s+/.test(lines[i]) && !/^```/.test(lines[i]) && !/^>\s?/.test(lines[i])
        && !/^\s*[-*+]\s+/.test(lines[i]) && !/^\s*\d+\.\s+/.test(lines[i])
        && !/^(-{3,}|\*{3,}|_{3,})\s*$/.test(lines[i])) {
        para.push(lines[i]);
        i++;
      }
      out.push(`<p class="my-2 leading-6">${renderMarkdownInline(escapeHtml(para.join(" ")))}</p>`);
    }
    closeList();
    return out.join("");
  }

  async function loadRepositoryTree(repo, path = "") {
    const detail = $("[data-repo-detail]");
    if (!detail) return;
    const treeBody = detail.querySelector("[data-repo-tree]");
    const treePanel = detail.querySelector("[data-repo-tree-panel]");
    const viewer = detail.querySelector("[data-repo-blob]");
    const readmePanel = detail.querySelector("[data-repo-readme]");
    if (!treeBody) return;

    setRepoTab("code");
    treePanel?.classList.remove("hidden");
    viewer?.classList.add("hidden");
    readmePanel?.classList.toggle("hidden", Boolean(path));
    setRepoExplorerFocusMode(Boolean(path));
    // The root/README view keeps the two-column layout: the About rail is a
    // permanent right-hand column (the old full-width README override that
    // dropped it below the content was removed, owner decision 2026-07-11).
    renderRepoBreadcrumb(repo, path);

    treeBody.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading tree...")}</div>`;
    renderRepoExplorer(repo, path, []);
    try {
      const requestedAt = performance.now();
      // A live-mirror read: fetch fresh (fetchRepoJson, no-store) like every
      // other tunnel surface (issues/pulls/releases/README), never the browser-
      // cached, account-scoped fetchJson. The router round-robins browse across
      // whichever mirrors are online, so a cached copy could pin the page to a
      // node that has since gone offline — the "served by mirror" view must
      // reflect the mirror that actually answered this request.
      const data = await fetchRepoJson(repoLiveUrl(repo, "tree", { path }));
      renderRepoServedBy(data.servedBy, performance.now() - requestedAt);
      updateRepoCommitSummary(data.latestCommit, repo);
      const entries = Array.isArray(data.entries) ? data.entries.slice() : [];
      if (!path && data.counts) { updateRepoLiveCounts(repo, data.counts); applyServedCounts(data.counts); }
      entries.sort((a, b) => {
        if (a.type !== b.type) return a.type === "tree" ? -1 : 1;
        return String(a.name || "").localeCompare(String(b.name || ""));
      });
      renderRepoExplorer(repo, path, entries);
      setRepoExplorerSelection(path, "tree");
      if (!entries.length) {
        treeBody.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">This directory is empty.</div>';
        navigateHistory(repoPathUrl(repo, "tree", path));
        window.lucide?.createIcons();
        return;
      }
      treeBody.innerHTML = entries.map((entry) => {
        const childPath = repoChildPath(path, entry.name);
        const isTree = entry.type === "tree";
        const message = entry.message || entry.commitMessage || entry.subject || "mirrored repository object";
        const date = formatTimeAgo(entry.date || entry.updatedAt || entry.committedAt || entry.commitDate || entry.mtime || repo.updatedAt || repo.lastSync);
        return `
            <button data-dashboard-${isTree ? "tree" : "blob"}-path="${escapeHtml(childPath)}" class="grid w-full grid-cols-[1.5rem_minmax(0,1fr)_auto] items-center gap-3 border-t border-border px-4 py-2.5 text-left text-sm hover:bg-secondary/40 transition-colors sm:grid-cols-[1.5rem_minmax(9rem,0.8fr)_minmax(0,1fr)_auto]">
              ${fileIconHtml(entry, "h-4 w-4 shrink-0")}
              <span class="min-w-0 truncate font-medium text-foreground">${escapeHtml(entry.name || "entry")}</span>
              <span class="hidden min-w-0 truncate text-xs text-muted-foreground sm:block">${escapeHtml(message)}</span>
              <span class="shrink-0 text-xs text-muted-foreground font-mono">${escapeHtml(date)}</span>
            </button>`;
      }).join("");
      navigateHistory(repoPathUrl(repo, "tree", path));
      window.lucide?.createIcons();
      if (!path) {
        const readmeEntry = entries.find((e) => e.type === "blob" && /^readme(\.md|\.txt|\.rst)?$/i.test(String(e.name || "")));
        const readmeBody = detail.querySelector("[data-repo-readme-body]");
        const readmeFilename = detail.querySelector("[data-repo-readme-filename]");
        const readmeLink = detail.querySelector("[data-repo-readme-link]");
        if (readmeBody) {
          if (readmeEntry) {
            if (readmeLink) {
              readmeLink.href = repoPathUrl(repo, "blob", readmeEntry.name);
              readmeLink.dataset.repoReadmePath = readmeEntry.name;
            }
            if (readmeFilename) readmeFilename.innerHTML = `<i data-lucide="book-open" class="h-3.5 w-3.5 text-muted-foreground"></i>${escapeHtml(readmeEntry.name)}`;
            try {
              const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: readmeEntry.name }));
              const text = blobText(blob);
              if (/\.(txt)$/i.test(readmeEntry.name)) {
                readmeBody.className = "whitespace-pre-wrap p-4 font-mono text-xs leading-5 text-foreground";
                readmeBody.textContent = text;
              } else {
                readmeBody.className = "p-4 text-sm text-foreground";
                readmeBody.innerHTML = renderMarkdown(text);
              }
            } catch (_) {
              readmeBody.className = "p-4 text-sm leading-6 text-muted-foreground";
              readmeBody.innerHTML = `<p class="mt-1">${escapeHtml(repo.description || "This repository has not published a README preview yet.")}</p>`;
            }
          } else {
            readmeBody.className = "p-4 text-sm leading-6 text-muted-foreground";
            readmeBody.innerHTML = `<p class="text-foreground font-medium">${escapeHtml(repo.name || "repository")}</p><p class="mt-1">${escapeHtml(repo.description || "This repository has not published a README preview yet.")}</p>`;
          }
          window.lucide?.createIcons();
        }
      }
    } catch (_) {
      treeBody.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">No live desktop host is serving this repository tree right now.</div>';
      if (!path) {
        const readmeBody = detail.querySelector("[data-repo-readme-body]");
        if (readmeBody) {
          readmeBody.className = "p-4 text-sm leading-6 text-muted-foreground";
          readmeBody.innerHTML = `<p class="mt-1">${escapeHtml(repo.description || "This repository has not published a README preview yet.")}</p>`;
        }
      }
    }
  }

  async function loadRepositoryBlob(repo, path) {
    const detail = $("[data-repo-detail]");
    const viewer = detail?.querySelector("[data-repo-blob]");
    const treePanel = detail?.querySelector("[data-repo-tree-panel]");
    const readmePanel = detail?.querySelector("[data-repo-readme]");
    if (!viewer) return;
    setRepoTab("code");
    treePanel?.classList.add("hidden");
    readmePanel?.classList.add("hidden");
    setRepoExplorerFocusMode(repoPathParts(path).length > 1);
    viewer.classList.remove("hidden");
    viewer.innerHTML = `<div class="py-3 text-sm text-muted-foreground">${loadingHtml("Loading file...")}</div>`;
    renderRepoBreadcrumb(repo, path, "blob");
    setRepoExplorerSelection(path, "blob");
    try {
      const pathPreview = repoPreviewKindFromPath(path);
      if (pathPreview && pathPreview.kind !== "csv") {
        if (renderRepoPreview(viewer, repo, path, {})) return;
      }
      const requestedAt = performance.now();
      // Fresh live-mirror read (see loadRepositoryTree): the blob must come from
      // the mirror serving this request, not a stale browser cache.
      const data = await fetchRepoJson(repoLiveUrl(repo, "blob", { path }));
      renderRepoServedBy(data.servedBy, performance.now() - requestedAt);
      if (renderRepoPreview(viewer, repo, path, data)) return;
      const content = data.content || data.text || "";
      const lines = String(content).split(/\r\n|\r|\n/);
      const loc = lines.filter((line) => line.trim()).length;
      const meta = {
        content,
        loc,
        commitId: repo.rootCommit || repo.latestCommit || repo.commit || "live",
        author: repo.maintainer || repo.owner || "mirror",
        owner: repo.owner || "owner",
      };
      viewer.innerHTML = `
        <div data-repo-file-shell class="overflow-hidden rounded-lg border border-border bg-background">
          <div data-repo-file-toolbar class="flex min-h-11 flex-wrap items-center justify-between gap-2 border-b border-border bg-secondary/40 px-3 py-2">
            <div class="flex min-w-0 flex-wrap items-center gap-2">
              <div class="inline-flex overflow-hidden rounded-md border border-border bg-background p-0.5">
                <button type="button" data-repo-file-mode="code" aria-pressed="true" class="${repoFileModeButtonClass(true)}">Code</button>
                <button type="button" data-repo-file-mode="blame" aria-pressed="false" class="${repoFileModeButtonClass(false)}">Blame</button>
              </div>
              <span class="font-mono text-[10px] text-muted-foreground">${formatCount(lines.length)} lines</span>
              <span class="font-mono text-[10px] text-muted-foreground">(${formatCount(loc)} loc)</span>
              <span class="font-mono text-[10px] text-muted-foreground">${escapeHtml(formatSize(content.length))}</span>
              <span data-repo-file-status class="min-w-0 font-mono text-[10px] text-muted-foreground">Code view - highlighted source.</span>
            </div>
            <div class="flex items-center gap-1.5">
              <button type="button" data-repo-file-action="raw" class="inline-flex h-7 items-center rounded-md border border-border px-2.5 text-[10px] font-medium text-foreground hover:bg-secondary transition-colors">Raw</button>
              <button type="button" data-repo-file-action="copy" aria-label="Copy file contents" class="inline-flex h-7 w-7 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="copy" class="h-3.5 w-3.5"></i></button>
              <button type="button" data-repo-file-action="download" aria-label="Download file" class="inline-flex h-7 w-7 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="download" class="h-3.5 w-3.5"></i></button>
              <button type="button" data-repo-file-action="fullscreen" aria-label="Open full screen" class="inline-flex h-7 w-7 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="maximize-2" class="h-3.5 w-3.5"></i></button>
            </div>
          </div>
          <div data-repo-file-content></div>
        </div>`;
      setRepoFileMode(viewer, "code", lines, path, meta);
      bindRepoFileToolbar(viewer, repo, path, content, lines, meta);
      navigateHistory(repoPathUrl(repo, "blob", path));
      window.lucide?.createIcons();
    } catch (_) {
      viewer.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Could not load this file from a live host.</div>';
    }
  }

  function parseFrontMatter(markdown) {
    const text = String(markdown || "");
    if (!text.startsWith("---\n") && !text.startsWith("---\r\n")) {
      return { values: {}, body: text.trim() };
    }
    const normalized = text.replace(/\r\n/g, "\n");
    const end = normalized.indexOf("\n---\n", 4);
    if (end < 0) return { values: {}, body: text.trim() };
    const values = {};
    normalized.slice(4, end).split("\n").forEach((line) => {
      const colon = line.indexOf(":");
      if (colon <= 0) return;
      values[line.slice(0, colon).trim()] = line.slice(colon + 1).trim();
    });
    return { values, body: normalized.slice(end + 5).trim() };
  }

  function parseFrontMatterList(value) {
    const text = String(value || "").trim();
    if (!text.startsWith("[") || !text.endsWith("]")) return [];
    return text.slice(1, -1).split(",").map((item) => item.trim()).filter(Boolean);
  }

  function issueJsonPath(number) {
    return `.forkmesh/issues/${Number(number)}/issue-${Number(number)}.json`;
  }

  function parseIssueJson(text, fallbackNumber) {
    let issue = {};
    try {
      const parsed = JSON.parse(String(text || "{}"));
      if (parsed && typeof parsed === "object") issue = parsed;
    } catch (_) {
      issue = {};
    }
    const number = Number(issue.number || fallbackNumber);
    const events = Array.isArray(issue.events) ? issue.events : [];
    const open = events.find((event) => event && event.type === "open") || {};
    const updatedAt = events.reduce((latest, event) => {
      const ts = Number(event?.ts || 0);
      return Number.isFinite(ts) && ts > latest ? ts : latest;
    }, Number(issue.updatedAt || issue.createdAt || open.ts || 0));
    const status = issue.status || issue.state || "open";
    const labelsList = (Array.isArray(issue.labels)
      ? issue.labels
      : parseFrontMatterList(issue.labels)).slice(0, 3);
    const labels = labelsList.join(", ");
    return {
      number,
      title: issue.title || open.title || `issue #${number}`,
      status,
      author: issue.authorName || open.authorName || issue.author || open.author || "unknown",
      date: formatRecordDate(updatedAt || issue.createdAt || open.ts),
      labels: labelsList,
      meta: [status, labels, issue.milestone].filter(Boolean).join(" · "),
      body: open.body || issue.body || "",
      wantsAgent: Boolean(issue.wantsAgent),
      milestone: String(issue.milestone || ""),
      progress: Number(issue.progress || 0) || 0,
      startDate: Number(issue.startDate || 0) || 0,
      endDate: Number(issue.endDate || 0) || 0,
      createdAtMs: Number(issue.createdAt || open.ts || 0) || 0,
    };
  }

  // Adapt an issue-N.json blob into the { values, body } shape that
  // renderRepoRecordDetail consumes for pulls/discussions front matter, so the
  // issue detail view renders from the same JSON the Issues list already reads.
  function issueDetailParsed(text, fallbackNumber) {
    const issue = parseIssueJson(text, fallbackNumber);
    return {
      values: {
        title: issue.title,
        status: issue.status,
        authorName: issue.author,
        milestone: issue.milestone,
        labels: `[${(issue.labels || []).join(", ")}]`,
        createdAt: issue.createdAtMs,
      },
      body: issue.body,
    };
  }

  function projectJsonPath(number) {
    return `.forkmesh/projects/${Number(number)}/project-${Number(number)}.json`;
  }

  // Projects (issue #384) are stored like issues - one signed-event JSON per
  // numbered folder under .forkmesh/projects/ - and carry gantt start/end
  // dates, an optional milestone link, and a list of linked issue numbers.
  function parseProjectJson(text, fallbackNumber) {
    let project = {};
    try {
      const parsed = JSON.parse(String(text || "{}"));
      if (parsed && typeof parsed === "object") project = parsed;
    } catch (_) {
      project = {};
    }
    const number = Number(project.number || fallbackNumber);
    const issues = (Array.isArray(project.issues) ? project.issues : [])
      .map((value) => Number(value))
      .filter((value) => Number.isFinite(value) && value > 0);
    return {
      number,
      title: project.title || `project #${number}`,
      status: project.status || "open",
      body: project.body || "",
      author: project.authorName || project.author || "unknown",
      date: formatRecordDate(project.updatedAt || project.createdAt),
      milestone: String(project.milestone || ""),
      issues,
      startDate: Number(project.startDate || 0) || 0,
      endDate: Number(project.endDate || 0) || 0,
      createdAtMs: Number(project.createdAt || 0) || 0,
    };
  }

  function formatRecordDate(value) {
    const number = Number(value);
    if (Number.isFinite(number) && number > 0) {
      return formatDate(number < 1000000000000 ? number * 1000 : number);
    }
    return value ? formatDate(value) : "unknown";
  }

  async function fetchRepoJson(path) {
    const response = await fetch(path, {
      cache: "no-store",
      headers: { accept: "application/json", "cache-control": "no-cache" },
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      const error = new Error(data.error || `HTTP ${response.status}`);
      error.status = response.status;
      error.code = data.error || "";
      throw error;
    }
    return data;
  }

  function isMissingMirrorFolder(error) {
    const message = String(error?.code || error?.message || "").toLowerCase();
    return message.includes("not_found")
      || message.includes("not a valid object name")
      || message.includes("pathspec")
      || message.includes("does not exist")
      || message.includes("unknown revision");
  }

  const repoCollectionConfig = {
    issues: {
      label: "Issues",
      itemLabel: "issue",
      dir: ".forkmesh/issues", file: (number) => `issue-${Number(number)}.json`,
      icon: "circle-dot",
      tone: "text-primary",
      empty: "No issues have been committed to this mirror yet.",
      meta(values) {
        const labels = Array.isArray(values.labels)
          ? values.labels.slice(0, 3).join(", ")
          : parseFrontMatterList(values.labels).slice(0, 3).join(", ");
        return [values.status || "open", labels, values.milestone].filter(Boolean).join(" · ");
      },
    },
    pulls: {
      label: "Pull requests",
      itemLabel: "pull request",
      dir: "pulls", file: "pull.md",
      icon: "git-pull-request",
      tone: "text-primary",
      empty: "No pull requests have been committed to this mirror yet.",
      meta(values) {
        return [values.status || "open", values.base && values.head ? `${values.base} ← ${values.head}` : ""].filter(Boolean).join(" · ");
      },
    },
    discussions: {
      label: "Discussions",
      itemLabel: "discussion",
      dir: "discussions", file: "discussion.md",
      icon: "message-square",
      tone: "text-muted-foreground",
      empty: "No discussions have been committed to this mirror yet.",
      meta(values) {
        return values.category || "discussion";
      },
    },
  };

  function blobText(data) {
    const content = data.content || data.text || "";
    if (data.encoding === "base64") {
      try {
        return decodeURIComponent(escape(atob(content)));
      } catch (_) {
        return "";
      }
    }
    return String(content || "");
  }

  async function fetchRepoBlobs(repo, paths, options = {}) {
    // Batched file read: ONE request returns every path (repeated ?path=
    // params); the worker fans the reads out over the live tunnel itself.
    // Fetching each record as its own /blob call flooded the relay with 50+
    // parallel requests per page view and tripped the per-repo rate limit.
    // Missing/unreadable paths come back null.
    if (!paths.length) return {};
    const query = new URLSearchParams();
    paths.forEach((path) => query.append("path", path));
    const current = new URLSearchParams(location.search || "");
    ["viewer", "ts", "sig"].forEach((key) => {
      const value = current.get(key);
      if (value) query.set(key, value);
    });
    // Same ref override contract as repoLiveUrl: pulls/ readers pass ref:""
    // so the host resolves the forkmesh/pulls metadata branch (issue #399).
    if (!("ref" in options)) query.set("ref", repoSelectedBranch(repo));
    else if (options.ref) query.set("ref", options.ref);
    const version = repoDataVersion(repo);
    if (version) query.set("fmv", version);
    const requestedAt = performance.now();
    const data = await fetchRepoJson(`${repoApiBase(repo)}/blobs?${query.toString()}`);
    renderRepoServedBy(data.servedBy, performance.now() - requestedAt);
    return data.blobs || {};
  }

  async function loadRepoRecordsFromMirror(repo, config) {
    // Pull requests moved to the dedicated forkmesh/pulls metadata branch
    // (issue #399). Sending ref:"" lets the host resolve that branch itself
    // (with its own fallback to the default branch for pre-#399 mirrors);
    // naming the selected code branch here is what kept serving the stale
    // pulls/ folder frozen on main at migration time.
    const refParams = config.dir === "pulls" ? { ref: "" } : {};
    let tree;
    try {
      tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: config.dir, ...refParams }));
    } catch (error) {
      if (isMissingMirrorFolder(error)) return [];
      throw error;
    }
    const dirs = (Array.isArray(tree.entries) ? tree.entries : [])
      .filter((entry) => entry.type === "tree" && /^\d+$/.test(String(entry.name || "")))
      .sort((a, b) => Number(b.name) - Number(a.name))
      .slice(0, 50);
    const recordPath = (entry) => {
      const file = typeof config.file === "function"
        ? config.file(Number(entry.name))
        : config.file;
      return `${config.dir}/${entry.name}/${file}`;
    };
    // One batched request for every record file instead of a per-record fan-out.
    const blobs = await fetchRepoBlobs(repo, dirs.map(recordPath), refParams);
    const records = dirs.map((entry) => {
      const number = Number(entry.name);
      const blob = blobs[recordPath(entry)];
      if (!blob) return null;
      const parsed = parseFrontMatter(blobText(blob));
      const values = parsed.values || {};
      return {
        number,
        title: values.title || `${config.itemLabel} #${number}`,
        state: values.status || values.state || values.category || "open",
        author: values.authorName || values.author || "unknown",
        date: formatRecordDate(values.updatedAt || values.createdAt || values.ts),
        meta: config.meta(values),
        body: parsed.body || "",
        wantsAgent: config.dir === "issues" ? Boolean(values.wantsAgent) : false,
      };
    });
    return records.filter(Boolean);
  }

  function renderRepoCollectionPagination(kind, page, totalPages, totalItems) {
    const previousDisabled = page <= 1;
    const nextDisabled = page >= totalPages;
    const pages = Array.from({ length: totalPages }, (_, index) => index + 1).slice(0, 7);
    return `
      <nav data-repo-collection-pagination="${kind}" class="flex min-w-0 flex-wrap items-center justify-between gap-3 border-t border-border bg-secondary/25 px-4 py-3 text-xs" aria-label="${escapeHtml(repoCollectionConfig[kind]?.label || "Records")} pagination">
        <span class="font-mono text-muted-foreground">Page ${page} of ${totalPages} · ${formatCount(totalItems)} total</span>
        <div class="flex min-w-0 flex-wrap items-center gap-1">
          <button type="button" data-repo-collection-page="${kind}" data-repo-collection-page-target="${page - 1}" ${previousDisabled ? "disabled" : ""} class="inline-flex h-8 items-center rounded-md border border-border px-3 font-medium text-muted-foreground transition-colors hover:bg-secondary hover:text-foreground disabled:cursor-not-allowed disabled:opacity-40">Previous</button>
          ${pages.map((number) => `<button type="button" data-repo-collection-page="${kind}" data-repo-collection-page-target="${number}" aria-current="${number === page ? "page" : "false"}" class="inline-flex h-8 min-w-8 items-center justify-center rounded-md border px-2 font-mono transition-colors ${number === page ? "bg-secondary text-foreground border-border" : "border-border text-muted-foreground hover:bg-secondary hover:text-foreground"}">${number}</button>`).join("")}
          <button type="button" data-repo-collection-page="${kind}" data-repo-collection-page-target="${page + 1}" ${nextDisabled ? "disabled" : ""} class="inline-flex h-8 items-center rounded-md border border-border px-3 font-medium text-muted-foreground transition-colors hover:bg-secondary hover:text-foreground disabled:cursor-not-allowed disabled:opacity-40">Next</button>
        </div>
      </nav>`;
  }

  function renderRepoRecordList(items, config, kind) {
    const totalPages = Math.max(1, Math.ceil(items.length / REPO_COLLECTION_PAGE_SIZE));
    const page = state.repoCollectionPages[kind] || 1;
    const safePage = Math.min(Math.max(1, page), totalPages);
    state.repoCollectionPages[kind] = safePage;
    const start = (safePage - 1) * REPO_COLLECTION_PAGE_SIZE;
    const end = start + REPO_COLLECTION_PAGE_SIZE;
    const pageItems = ["issues", "pulls"].includes(kind)
      ? items.slice(start, end)
      : items;
    return pageItems.map((item) => {
      const numberLabel = item.pending ? "pending" : `#${formatCount(item.number)}`;
      const stateLabel = item.pending ? "syncing..." : (item.state || "open");
      const labelParts = String(item.meta || "")
        .split(" · ")
        .map((part) => part.trim())
        .filter((part) => part && part !== stateLabel)
        .slice(0, 3);
      return `
        <button type="button" data-repo-record-kind="${escapeHtml(kind)}" data-repo-record-number="${escapeHtml(item.pending ? item.localId : item.number)}" class="grid w-full grid-cols-[1rem_1.25rem_minmax(0,1fr)_auto] gap-3 border-t border-border px-4 py-3 text-left transition-colors hover:bg-secondary/40">
          <span aria-hidden="true" class="mt-0.5 h-4 w-4 rounded border border-border bg-background"></span>
          <i data-lucide="${config.icon}" class="mt-0.5 h-4 w-4 ${config.tone}"></i>
          <span class="min-w-0">
            <span class="flex min-w-0 flex-wrap items-center gap-x-2 gap-y-1">
              <span class="min-w-0 truncate text-sm font-semibold text-foreground">${escapeHtml(item.title)}</span>
              ${labelParts.map((label) => `<span class="rounded-full border border-border bg-secondary px-2 py-0.5 text-[10px] font-medium text-muted-foreground">${escapeHtml(label)}</span>`).join("")}
            </span>
            <span class="mt-1 block truncate text-xs text-muted-foreground">${escapeHtml(numberLabel)} opened by ${escapeHtml(item.author)} ${escapeHtml(item.date)}${item.body ? ` - ${escapeHtml(item.body).slice(0, 140)}` : ""}</span>
          </span>
          <span class="self-start shrink-0 flex items-center gap-3">
            ${item.wantsAgent ? '<i data-lucide="zap" class="h-4 w-4 text-yellow-500" title="Assigned to agent"></i>' : ''}
            <span class="hidden items-center gap-1 text-xs text-muted-foreground sm:inline-flex"><i data-lucide="message-square" class="h-3.5 w-3.5"></i>0</span>
            <span data-repo-record-state class="self-start rounded-md border border-border bg-secondary/60 px-2 py-0.5 text-[10px] font-mono text-foreground">${escapeHtml(stateLabel)}</span>
          </span>
        </button>`;
    }).join("") + (["issues", "pulls"].includes(kind) ? renderRepoCollectionPagination(kind, safePage, totalPages, items.length) : "");
  }

  // Shared unified-diff parser used by both the commit diff and the pull
  // patch views. Groups lines per file and tracks real old/new line numbers
  // per hunk so the viewer can render a GitHub-style dual gutter instead of
  // a single running index.
  function parseDiffFiles(rawText) {
    const lines = String(rawText || "").split("\n");
    if (lines.length && lines[lines.length - 1] === "") lines.pop();
    const files = [];
    let current = null;
    let oldLine = 0;
    let newLine = 0;
    lines.forEach((line) => {
      if (line.startsWith("diff --git ")) {
        const match = line.match(/^diff --git a\/(.*?) b\/(.*)$/);
        current = { oldPath: match?.[1] || "", newPath: match?.[2] || match?.[1] || "file", status: "modified", binary: false, adds: 0, dels: 0, rows: [] };
        files.push(current);
        oldLine = 0;
        newLine = 0;
        return;
      }
      if (!current) return;
      if (line.startsWith("new file mode")) { current.status = "added"; return; }
      if (line.startsWith("deleted file mode")) { current.status = "deleted"; return; }
      if (line.startsWith("rename from ")) { current.status = "renamed"; current.oldPath = line.slice("rename from ".length); return; }
      if (line.startsWith("rename to ")) { current.newPath = line.slice("rename to ".length); return; }
      if (line.startsWith("Binary files ") || line.startsWith("GIT binary patch")) { current.binary = true; return; }
      if (line.startsWith("similarity index") || line.startsWith("index ") ||
          line.startsWith("old mode") || line.startsWith("new mode") ||
          line.startsWith("--- ") || line.startsWith("+++ ")) return;
      if (line.startsWith("@@")) {
        const match = line.match(/^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@/);
        if (match) { oldLine = Number(match[1]); newLine = Number(match[2]); }
        current.rows.push({ type: "hunk", text: line });
        return;
      }
      const marker = line[0];
      if (marker === "+") { current.rows.push({ type: "add", newLine: newLine++, text: line.slice(1) }); current.adds += 1; return; }
      if (marker === "-") { current.rows.push({ type: "del", oldLine: oldLine++, text: line.slice(1) }); current.dels += 1; return; }
      if (marker === "\\") { current.rows.push({ type: "meta", text: line }); return; }
      current.rows.push({ type: "ctx", oldLine: oldLine++, newLine: newLine++, text: line.slice(1) });
    });
    return { files };
  }

  function parsePatchStats(patch) {
    return parseDiffFiles(patch).files.map((file) => ({ path: file.newPath || file.oldPath || "file", adds: file.adds, dels: file.dels }));
  }

  function renderRepoPullFiles(files) {
    const rows = Array.isArray(files) ? files : [];
    if (!rows.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No committed patch file summary is available for this pull request.</div>';
    return rows.slice(0, 100).map((file) => `
      <div class="grid grid-cols-[minmax(0,1fr)_auto_auto] gap-3 border-t border-border px-4 py-2 text-xs">
        <span class="min-w-0 truncate font-mono text-foreground">${escapeHtml(file.path || "file")}</span>
        <span class="font-mono text-primary">+${formatCount(file.adds || 0)}</span>
        <span class="font-mono text-destructive">-${formatCount(file.dels || 0)}</span>
      </div>`).join("");
  }

  // Pull-request badge (adhoc #44): a visual fingerprint of the PR. One tile
  // per changed file — a file-type glyph over a green/red bar showing that
  // file's additions:deletions ratio — grouped by directory with a labeled
  // connector line. Mirrors the SVG the worker attaches to federated
  // PR-opened notes (src/pull_badge.py) and the desktop Badge tab.
  const fileBadgeStyles = {
    ts: ["TS", "#3178c6"], tsx: ["TSX", "#3178c6"],
    js: ["JS", "#f1e05a"], mjs: ["JS", "#f1e05a"], cjs: ["JS", "#f1e05a"],
    jsx: ["JSX", "#61dafb"], py: ["PY", "#4b8bbe"], pyw: ["PY", "#4b8bbe"],
    rb: ["RB", "#cc342d"], rs: ["RS", "#dea584"], go: ["GO", "#00add8"],
    java: ["JAVA", "#b07219"], kt: ["KT", "#a97bff"], swift: ["SWFT", "#f05138"],
    cs: ["C#", "#178600"], c: ["C", "#9cdcfe"], h: ["H", "#9cdcfe"],
    cpp: ["C++", "#f34b7d"], cc: ["C++", "#f34b7d"], cxx: ["C++", "#f34b7d"],
    hpp: ["H++", "#f34b7d"], php: ["PHP", "#777bb3"], dart: ["DART", "#00b4ab"],
    vue: ["VUE", "#41b883"], svelte: ["SVLT", "#ff3e00"],
    html: ["</>", "#e34c26"], htm: ["</>", "#e34c26"], css: ["CSS", "#563d7c"],
    scss: ["SCSS", "#c6538c"], less: ["LESS", "#1d365d"],
    md: ["MD", "#519aba"], markdown: ["MD", "#519aba"],
    json: ["{ }", "#cbcb41"], yaml: ["YAML", "#cb4b4b"], yml: ["YAML", "#cb4b4b"],
    toml: ["TOML", "#9c4221"], xml: ["XML", "#e37933"], ini: ["CFG", "#6d8086"],
    cfg: ["CFG", "#6d8086"], conf: ["CFG", "#6d8086"], env: ["ENV", "#6d8086"],
    sh: [">_", "#89e051"], bash: [">_", "#89e051"], zsh: [">_", "#89e051"],
    bat: [">_", "#c1f12e"], ps1: [">_", "#012456"], sql: ["SQL", "#e38c00"],
    svg: ["SVG", "#ffb13b"], png: ["IMG", "#a074c4"], jpg: ["IMG", "#a074c4"],
    jpeg: ["IMG", "#a074c4"], gif: ["IMG", "#a074c4"], webp: ["IMG", "#a074c4"],
    avif: ["IMG", "#a074c4"], ico: ["IMG", "#a074c4"], pdf: ["PDF", "#f40f02"],
    txt: ["TXT", "#8b949e"], lock: ["LOCK", "#8b949e"], qrc: ["QRC", "#41cd52"],
    ui: ["UI", "#41cd52"], cmake: ["CMK", "#649ad2"],
  };
  const fileBadgeNames = {
    dockerfile: ["DOCK", "#2496ed"], makefile: ["MAKE", "#6d8086"],
    "cmakelists.txt": ["CMK", "#649ad2"], license: ["LIC", "#d0b44c"],
    "readme.md": ["MD", "#519aba"], ".gitignore": ["GIT", "#f14e32"],
    ".gitattributes": ["GIT", "#f14e32"], ".gitmodules": ["GIT", "#f14e32"],
    "package.json": ["NPM", "#cb3837"], "package-lock.json": ["NPM", "#cb3837"],
  };

  // Directory connector labels show just the folder name (not the whole
  // path) capped to 8 chars, e.g. "one/two/three" -> "three".
  function dirBadgeLabel(dir) {
    const name = dir === "/" ? "/" : dir.slice(dir.lastIndexOf("/") + 1);
    return name.length > 8 ? `${name.slice(0, 8)}…` : name;
  }

  function fileBadgeGlyph(path) {
    const name = String(path || "").split("/").pop().toLowerCase();
    if (fileBadgeNames[name]) return fileBadgeNames[name];
    const stem = name.replace(/^\.+/, "");
    const ext = stem.includes(".") ? stem.split(".").pop() : "";
    if (fileBadgeStyles[ext]) return fileBadgeStyles[ext];
    return [ext.toUpperCase().slice(0, 4) || "FILE", "#8b949e"];
  }

  function renderPullBadgeTile(file) {
    const [label, color] = fileBadgeGlyph(file.path);
    const adds = file.adds || 0;
    const dels = file.dels || 0;
    const total = adds + dels;
    const addPct = total ? Math.round((adds / total) * 100) : 0;
    // Tiny per-file diff strip, same height as the glyph square: a green
    // slice on top sized to the addition share, red below for deletions.
    const diffStrip = total
      ? `<span class="block w-full" style="height:${addPct}%;background:#3fb950"></span><span class="block w-full" style="height:${100 - addPct}%;background:#f85149"></span>`
      : '<span class="block h-full w-full" style="background:#30363d"></span>';
    return `
      <div class="flex w-16 shrink-0 gap-1.5" title="${escapeHtml(file.path || "file")} +${formatCount(adds)} -${formatCount(dels)}">
        <div class="flex h-10 w-10 shrink-0 items-center justify-center rounded-md border border-border bg-secondary/60 font-mono text-[11px] font-bold" style="color:${color}">${escapeHtml(label)}</div>
        <div class="flex h-10 w-1.5 shrink-0 flex-col overflow-hidden rounded-full">${diffStrip}</div>
      </div>`;
  }

  // Deterministic hue per author name so the icon is stable across loads
  // without needing an avatar fetch (mirrors loadRepoAboutContributors).
  function badgeAuthorAvatarHtml(author) {
    const name = String(author || "unknown");
    let hash = 0;
    for (let i = 0; i < name.length; i += 1) hash = (hash * 31 + name.charCodeAt(i)) >>> 0;
    const hue = hash % 360;
    const initial = escapeHtml((name[0] || "?").toUpperCase());
    return `<span class="flex h-5 w-5 shrink-0 items-center justify-center rounded-full border border-background font-mono text-[10px] font-semibold text-white" style="background-color:hsl(${hue} 55% 42%)">${initial}</span>`;
  }

  function renderRepoPullBadge(title, number, author, files) {
    const rows = Array.isArray(files) ? files : [];
    if (!rows.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">The badge appears once the pull request\'s committed patch is available from the live mirror.</div>';
    const additions = rows.reduce((sum, file) => sum + (file.adds || 0), 0);
    const deletions = rows.reduce((sum, file) => sum + (file.dels || 0), 0);
    // Cluster files by directory, preserving a sorted order so each directory
    // forms one contiguous group under its labeled connector line.
    const groups = [];
    [...rows].sort((a, b) => String(a.path || "").localeCompare(String(b.path || ""))).forEach((file) => {
      const path = String(file.path || "file");
      const dir = path.includes("/") ? path.slice(0, path.lastIndexOf("/")) : "/";
      if (groups.length && groups[groups.length - 1].dir === dir) groups[groups.length - 1].files.push(file);
      else groups.push({ dir, files: [file] });
    });
    const stat = (value, caption, cls) => `
      <div class="text-right">
        <div class="font-mono text-lg font-bold ${cls}">${value}</div>
        <div class="text-[10px] text-muted-foreground">${caption}</div>
      </div>`;
    return `
      <div class="grid gap-5 rounded-lg border border-border p-4">
        <div class="flex flex-wrap items-start justify-between gap-4">
          <div class="min-w-0">
            <div class="truncate text-2xl font-bold text-foreground">${escapeHtml(title || "Pull request")}</div>
            <div class="mt-1 flex items-center gap-1.5 text-sm font-semibold text-primary">${number ? `#${escapeHtml(number)}` : "pull request"}<span class="ml-2 inline-flex items-center gap-1.5 font-normal text-muted-foreground">by ${badgeAuthorAvatarHtml(author)}${escapeHtml(author || "unknown")}</span></div>
          </div>
          <div class="flex shrink-0 gap-6">
            ${stat(`+${formatCount(additions)}`, "Additions", "text-emerald-400")}
            ${stat(`-${formatCount(deletions)}`, "Deletions", "text-red-400")}
            ${stat(formatCount(rows.length), "Files changed", "text-foreground")}
          </div>
        </div>
        <div class="flex flex-wrap items-end gap-x-8 gap-y-6">
          ${groups.map((group) => `
            <div class="flex flex-col gap-1.5">
              <div class="flex max-w-full flex-wrap gap-2">${group.files.map(renderPullBadgeTile).join("")}</div>
              <div class="flex items-center gap-2 text-[10px] text-muted-foreground">
                <span class="h-px min-w-3 flex-1 bg-border"></span>
                <span class="font-mono" title="${escapeHtml(group.dir)}">${escapeHtml(dirBadgeLabel(group.dir))}</span>
                <span class="font-mono">…${formatCount(group.files.length)}</span>
                <span class="h-px min-w-3 flex-1 bg-border"></span>
              </div>
            </div>`).join("")}
        </div>
      </div>`;
  }

  function diffRowClass(type) {
    if (type === "add") return "bg-emerald-950/40 text-emerald-300";
    if (type === "del") return "bg-red-950/35 text-red-300";
    if (type === "hunk") return "bg-primary/10 text-primary";
    if (type === "meta") return "text-muted-foreground";
    return "text-zinc-300";
  }

  function renderDiffFileRows(rows) {
    if (!rows.length) return '<div class="px-3 py-2 text-xs text-muted-foreground">No line changes.</div>';
    return rows.map((row) => {
      const full = row.type === "hunk" || row.type === "meta";
      return `<div class="grid min-w-max grid-cols-[3rem_3rem_minmax(40rem,1fr)] ${diffRowClass(row.type)}">
        <span class="select-none border-r border-border/60 px-2 text-right font-mono text-muted-foreground">${full ? "" : (row.oldLine ?? "")}</span>
        <span class="select-none border-r border-border/60 px-2 text-right font-mono text-muted-foreground">${full ? "" : (row.newLine ?? "")}</span>
        <span class="whitespace-pre px-3 font-mono">${escapeHtml(row.text || " ")}</span>
      </div>`;
    }).join("");
  }

  function diffFileHeaderPath(file) {
    if (file.status === "renamed" && file.oldPath && file.newPath && file.oldPath !== file.newPath) {
      return `${escapeHtml(file.oldPath)} &rarr; ${escapeHtml(file.newPath)}`;
    }
    return escapeHtml(file.newPath || file.oldPath || "file");
  }

  function diffFileStatusBadge(file) {
    if (file.status === "added") return '<span class="ml-2 rounded border border-emerald-500/30 bg-emerald-500/10 px-1.5 py-0.5 text-[10px] uppercase text-emerald-300">added</span>';
    if (file.status === "deleted") return '<span class="ml-2 rounded border border-red-500/30 bg-red-500/10 px-1.5 py-0.5 text-[10px] uppercase text-red-300">deleted</span>';
    if (file.status === "renamed") return '<span class="ml-2 rounded border border-border px-1.5 py-0.5 text-[10px] uppercase text-muted-foreground">renamed</span>';
    return "";
  }

  function renderDiffImagePreview(image) {
    const frame = (label, data) => `
      <figure class="m-0 grid gap-1">
        ${data
          ? `<img src="data:${escapeHtml(image.mime)};base64,${data}" alt="${label}" class="max-h-64 w-full rounded border border-border bg-background object-contain" />`
          : '<div class="flex h-32 items-center justify-center rounded border border-dashed border-border text-xs text-muted-foreground">Not present</div>'}
        <figcaption class="text-center text-[10px] text-muted-foreground">${label}</figcaption>
      </figure>`;
    return `<div class="grid gap-3 border-b border-border bg-background p-3 sm:grid-cols-2">${frame("Before", image.old)}${frame("After", image.new)}</div>`;
  }

  function renderDiffFileBlock(file, image) {
    return `
      <div class="overflow-hidden rounded-lg border border-border">
        <div class="flex flex-wrap items-center justify-between gap-2 border-b border-border bg-secondary/50 px-3 py-2">
          <span class="inline-flex min-w-0 items-center truncate font-mono text-xs font-medium text-foreground">${diffFileHeaderPath(file)}${diffFileStatusBadge(file)}</span>
          <span class="shrink-0 font-mono text-[10px]"><span class="text-primary">+${formatCount(file.adds)}</span><span class="ml-1 text-destructive">-${formatCount(file.dels)}</span></span>
        </div>
        ${image ? renderDiffImagePreview(image) : ""}
        ${file.binary
          ? (image ? "" : '<div class="px-3 py-3 text-xs text-muted-foreground">Binary file not shown.</div>')
          : `<div class="max-h-[34rem] overflow-auto text-xs leading-5">${renderDiffFileRows(file.rows)}</div>`}
      </div>`;
  }

  function renderDiffFiles(parsed, imageDiffs) {
    const { files } = parsed;
    if (!files.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No changes to display.</div>';
    const images = new Map();
    (Array.isArray(imageDiffs) ? imageDiffs : []).forEach((item) => {
      if (item?.path && item?.mime) images.set(item.path, item);
    });
    return `<div class="grid gap-3 p-3">${files.map((file) => renderDiffFileBlock(file, images.get(file.newPath) || images.get(file.oldPath))).join("")}</div>`;
  }

  function renderRepoPullPatch(patch, key = "") {
    if (!String(patch || "").trim()) return '<div class="px-4 py-3 text-sm text-muted-foreground">No textual patch is committed for this pull request. Branch-backed PRs are reconstructed by the desktop client.</div>';
    if (!shouldRenderLongDiff(patch, key)) return renderLongDiffNotice("pull request patch", key, patch);
    return `<div data-repo-pull-patch>${renderDiffFiles(parseDiffFiles(patch))}</div>`;
  }

  async function loadRepoPullPatch(repo, number) {
    const patchPath = `pulls/${number}/changes.patch`;
    try {
      // ref:"" — resolved by the host to the forkmesh/pulls metadata branch.
      const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: patchPath, ref: "" }));
      const patch = blobText(blob);
      return { patch, files: parsePatchStats(patch), unavailable: false };
    } catch (error) {
      if (isMissingMirrorFolder(error)) return { patch: "", files: [], unavailable: true };
      return { patch: "", files: [], unavailable: true };
    }
  }

  // A pull's conversation is an append-only, signed event log stored as
  // pulls/<N>/NNNN-<type>.md files alongside pull.md (see PullStore.h on the
  // desktop client). Types: comment, review, line-comment, thread-comment,
  // thread-reply, thread-state, suggestion-state.
  function pullEventTypeMeta(ev) {
    const neutral = "border-border bg-secondary/60 text-muted-foreground";
    if (ev.type === "review") {
      if (ev.state === "approved") return { label: "approved", tone: "border-emerald-500/30 bg-emerald-500/10 text-emerald-300" };
      if (ev.state === "changes_requested") return { label: "requested changes", tone: "border-red-500/30 bg-red-500/10 text-red-300" };
      return { label: "reviewed", tone: neutral };
    }
    if (ev.type === "line-comment") return { label: "line comment", tone: neutral };
    if (ev.type === "thread-comment") return { label: "review thread", tone: neutral };
    if (ev.type === "thread-reply") return { label: "reply", tone: neutral };
    if (ev.type === "thread-state") return { label: `thread ${ev.state || "updated"}`.replace(/_/g, " "), tone: neutral };
    if (ev.type === "suggestion-state") return { label: `suggestion ${ev.state || "updated"}`.replace(/_/g, " "), tone: neutral };
    return { label: "comment", tone: neutral };
  }

  // Approve/request-changes/comment state per reviewer, derived the same way
  // the desktop's PullStore::reviewSummary() does: the *last* review event
  // per author wins.
  function pullReviewSummary(events) {
    const rows = Array.isArray(events) ? events : [];
    const byAuthor = new Map();
    for (const ev of rows) {
      if (ev.type !== "review" || !ev.state) continue;
      const key = ev.author || ev.authorName || "";
      if (!key) continue;
      byAuthor.set(key, { authorName: ev.authorName || ev.author || "unknown", state: ev.state });
    }
    return Array.from(byAuthor.values());
  }

  function renderPullReviewers(events) {
    const reviewers = pullReviewSummary(events);
    if (!reviewers.length) return "No reviews";
    // sidebarSection wraps this in a <p>, so rows must stay phrasing content
    // (span, not div) or the browser silently closes the paragraph early.
    return reviewers.map((reviewer) => {
      const tone = reviewer.state === "approved" ? "text-emerald-400" : reviewer.state === "changes_requested" ? "text-red-400" : "text-muted-foreground";
      const label = reviewer.state === "approved" ? "Approved" : reviewer.state === "changes_requested" ? "Requested changes" : "Commented";
      return `<span class="mt-1.5 flex items-center justify-between gap-2 first:mt-0"><span class="truncate font-medium text-foreground">${escapeHtml(reviewer.authorName)}</span><span class="shrink-0 text-[10px] font-semibold ${tone}">${escapeHtml(label)}</span></span>`;
    }).join("");
  }

  function pullEventAnchorLabel(ev) {
    if (!ev.path) return "";
    if (ev.lineStart) {
      const range = ev.lineEnd && ev.lineEnd !== ev.lineStart ? `${ev.lineStart}-${ev.lineEnd}` : `${ev.lineStart}`;
      return `${ev.path}:${range}`;
    }
    if (ev.line) return `${ev.path}:${ev.line}`;
    return ev.path;
  }

  function renderPullConversationEvent(ev) {
    const meta = pullEventTypeMeta(ev);
    const anchor = pullEventAnchorLabel(ev);
    const author = ev.authorName || ev.author || "unknown";
    const date = formatRecordDate(ev.ts);
    const body = String(ev.body || "").trim();
    return `
      <div class="border-t border-border px-4 py-3 text-sm first:border-t-0">
        <div class="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
          <span class="font-medium text-foreground">${escapeHtml(author)}</span>
          <span class="rounded-md border px-1.5 py-0.5 text-[10px] uppercase ${meta.tone}">${escapeHtml(meta.label)}</span>
          ${anchor ? `<span class="font-mono">${escapeHtml(anchor)}</span>` : ""}
          <span>&middot;</span>
          <span>${escapeHtml(date)}</span>
        </div>
        ${body ? `<div class="mt-2 whitespace-pre-wrap text-sm leading-6 text-foreground">${escapeHtml(body)}</div>` : ""}
      </div>`;
  }

  function renderRepoPullConversation(events) {
    const rows = Array.isArray(events) ? events : [];
    if (!rows.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No conversation yet on this pull request.</div>';
    return rows.map(renderPullConversationEvent).join("");
  }

  async function loadRepoPullConversation(repo, number) {
    let tree;
    try {
      // ref:"" — resolved by the host to the forkmesh/pulls metadata branch.
      tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: `pulls/${number}`, ref: "" }));
    } catch (_) {
      return [];
    }
    const files = (Array.isArray(tree.entries) ? tree.entries : [])
      .filter((entry) => entry.type !== "tree" && /^\d+-/.test(String(entry.name || "")))
      .sort((a, b) => String(a.name).localeCompare(String(b.name)));
    if (!files.length) return [];
    const blobs = await fetchRepoBlobs(repo, files.map((entry) => `pulls/${number}/${entry.name}`), { ref: "" });
    return files.map((entry) => {
      const blob = blobs[`pulls/${number}/${entry.name}`];
      if (!blob) return null;
      const parsed = parseFrontMatter(blobText(blob));
      const values = parsed.values || {};
      return {
        type: values.type || "comment",
        author: values.author || "",
        authorName: values.authorName || "",
        ts: values.ts || "",
        state: values.state || "",
        path: values.path || "",
        line: Number(values.line || 0),
        lineStart: Number(values.lineStart || 0),
        lineEnd: Number(values.lineEnd || 0),
        body: parsed.body || "",
      };
    }).filter(Boolean);
  }

  // A discussion's replies are an append-only, signed event log stored as
  // .forkmesh/discussions/<N>/NNNN-comment.md files alongside discussion.md (see
  // DiscussionStore.cpp on the desktop client). Reuses the pull conversation
  // row renderer since a discussion comment event has the same shape.
  function renderRepoDiscussionConversation(events) {
    const rows = Array.isArray(events) ? events : [];
    if (!rows.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No replies yet on this discussion.</div>';
    return rows.map(renderPullConversationEvent).join("");
  }

  async function loadRepoDiscussionConversation(repo, number) {
    let tree;
    try {
      tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: `.forkmesh/discussions/${number}` }));
    } catch (_) {
      return [];
    }
    const files = (Array.isArray(tree.entries) ? tree.entries : [])
      .filter((entry) => entry.type !== "tree" && /^\d+-comment\.md$/.test(String(entry.name || "")))
      .sort((a, b) => String(a.name).localeCompare(String(b.name)));
    if (!files.length) return [];
    const blobs = await fetchRepoBlobs(repo, files.map((entry) => `.forkmesh/discussions/${number}/${entry.name}`));
    return files.map((entry) => {
      const blob = blobs[`.forkmesh/discussions/${number}/${entry.name}`];
      if (!blob) return null;
      const parsed = parseFrontMatter(blobText(blob));
      const values = parsed.values || {};
      return {
        type: "comment",
        author: values.author || "",
        authorName: values.authorName || "",
        ts: values.ts || "",
        body: parsed.body || "",
      };
    }).filter(Boolean);
  }

  function renderDiscussionReplyForm(number) {
    if (!state.session?.nodeName) {
      return `<div class="border-t border-border bg-secondary/20 px-4 py-3 text-xs text-muted-foreground"><a href="/login" class="font-medium text-primary hover:underline">Log in</a> to reply to this discussion.</div>`;
    }
    return `
      <form data-repo-discussion-reply-form data-repo-discussion-reply-number="${escapeHtml(number)}" class="grid gap-2 border-t border-border bg-secondary/20 p-4">
        ${composeIdentityHtml(state.session, "Replying")}
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Reply
          <textarea data-repo-discussion-reply-body rows="3" placeholder="Write a reply. Markdown is supported." class="rounded-md border border-border bg-background px-3 py-2 text-sm text-foreground outline-none focus:border-primary"></textarea>
        </label>
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span data-repo-discussion-reply-hint class="text-[11px] text-muted-foreground">Sent to the maintainer's inbox for review.</span>
          <button type="submit" data-repo-discussion-reply-submit class="inline-flex h-9 items-center gap-2 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="send" class="h-4 w-4"></i>Reply</button>
        </div>
      </form>`;
  }

  function renderPullReviewForm(number) {
    if (!state.session?.nodeName) {
      return `<div class="border-t border-border bg-secondary/20 px-4 py-3 text-xs text-muted-foreground"><a href="/login" class="font-medium text-primary hover:underline">Log in</a> to comment or review this pull request.</div>`;
    }
    return `
      <form data-repo-pull-review-form data-repo-pull-review-number="${escapeHtml(number)}" class="grid gap-2 border-t border-border bg-secondary/20 p-4">
        ${composeIdentityHtml(state.session, "Reviewing")}
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Review
          <textarea data-repo-pull-review-body rows="3" placeholder="Leave a comment. Markdown is supported." class="rounded-md border border-border bg-background px-3 py-2 text-sm text-foreground outline-none focus:border-primary"></textarea>
        </label>
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span data-repo-pull-review-hint class="text-[11px] text-muted-foreground">Sent to the maintainer's inbox for review.</span>
          <div class="flex flex-wrap items-center gap-2">
            <button type="submit" data-repo-pull-review-action="changes_requested" class="inline-flex h-9 items-center gap-2 rounded-md border border-border px-3 text-sm font-medium text-foreground transition-colors hover:bg-secondary disabled:opacity-50"><i data-lucide="circle-x" class="h-4 w-4"></i>Request changes</button>
            <button type="submit" data-repo-pull-review-action="approved" class="inline-flex h-9 items-center gap-2 rounded-md border border-border px-3 text-sm font-medium text-foreground transition-colors hover:bg-secondary disabled:opacity-50"><i data-lucide="circle-check" class="h-4 w-4"></i>Approve</button>
            <button type="submit" data-repo-pull-review-action="comment" class="inline-flex h-9 items-center gap-2 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="send" class="h-4 w-4"></i>Comment</button>
          </div>
        </div>
      </form>`;
  }

  async function handlePullReviewSubmit(repo, form, action) {
    if (!repo || !form) return;
    const number = Number(form.dataset.repoPullReviewNumber || 0);
    const bodyInput = form.querySelector("[data-repo-pull-review-body]");
    const buttons = form.querySelectorAll("[data-repo-pull-review-action]");
    const hint = form.querySelector("[data-repo-pull-review-hint]");
    const setHint = (text, tone) => {
      if (hint) hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      if (hint) hint.textContent = text;
    };
    const body = String(bodyInput?.value || "").trim();
    if (!number) {
      setHint("This pull request hasn't finished loading yet.", "bad");
      return;
    }
    const isReview = action === "approved" || action === "changes_requested";
    if (!isReview && !body) {
      setHint("Write a comment before sending.", "bad");
      bodyInput?.focus();
      return;
    }
    buttons.forEach((button) => { button.disabled = true; });
    setHint("Signing and sending…");
    try {
      if (isReview) {
        await submitWebPullReview(repo, number, action, body);
      } else {
        await submitWebPullComment(repo, number, body);
      }
      const newEvent = {
        type: isReview ? "review" : "comment",
        state: isReview ? action : "",
        authorName: state.session?.nodeName || "you",
        author: state.session?.nodeName || "",
        ts: Math.floor(Date.now() / 1000),
        body,
      };
      const list = form.parentElement?.querySelector("[data-repo-pull-conversation]");
      if (list) {
        if (list.dataset.empty === "true") list.innerHTML = "";
        list.dataset.empty = "false";
        list.insertAdjacentHTML("beforeend", renderPullConversationEvent(newEvent));
      }
      if (state.repoRecordDetail?.kind === "pulls" && String(state.repoRecordDetail.number) === String(number)) {
        const conversation = [...(state.repoRecordDetail.parsed.pullConversation || []), newEvent];
        state.repoRecordDetail.parsed.pullConversation = conversation;
        const reviewers = document.querySelector("[data-repo-pull-reviewers]");
        if (reviewers) reviewers.innerHTML = renderPullReviewers(conversation);
      }
      if (bodyInput) bodyInput.value = "";
      buttons.forEach((button) => { button.disabled = false; });
      setHint(isReview ? "Review sent to the maintainer's inbox for review." : "Comment sent to the maintainer's inbox for review.", "good");
    } catch (error) {
      buttons.forEach((button) => { button.disabled = false; });
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "The maintainer's inbox is full. Try again later."
          : code === "author_quota" ? "You've reached the submission limit for this repository."
          : code === "event_too_large" ? "The comment is too large - please shorten it."
          : code === "bad_signature" ? "Could not verify the review's signature."
          : "Could not send the review. Please try again.",
        "bad");
    }
  }

  function recordDetailMeta(kind, values) {
    if (kind === "pulls") {
      return [
        ["Base", values.base || "unknown"],
        ["Head", values.head || "unknown"],
        ["Status", values.status || values.state || "open"],
        ["Author", values.authorName || values.author || "unknown"],
      ];
    }
    if (kind === "discussions") {
      return [
        ["Category", values.category || "discussion"],
        ["Status", values.status || values.state || "open"],
        ["Author", values.authorName || values.author || "unknown"],
      ];
    }
    return [
      ["Status", values.status || values.state || "open"],
      ["Labels", parseFrontMatterList(values.labels).join(", ") || "none"],
      ["Milestone", values.milestone || "none"],
      ["Author", values.authorName || values.author || "unknown"],
    ];
  }

  function renderRepoRecordDetail(repo, kind, number, parsed) {
    const options = parsed.options || {};
    const config = repoCollectionConfig[kind] || repoCollectionConfig.issues;
    const values = parsed.values || {};
    const title = values.title || `${config.itemLabel} #${number}`;
    const state = values.status || values.state || values.category || "open";
    const author = values.authorName || values.author || "unknown";
    const date = formatRecordDate(values.updatedAt || values.createdAt || values.ts);
    const body = parsed.body || "No description was committed for this record.";
    const recordLabel = options.pending ? "pending" : `#${escapeHtml(number)}`;
    const isPulls = kind === "pulls";
    const isDiscussions = kind === "discussions";
    const baseBranch = values.base || "main";
    const headBranch = values.head || "contributor:branch";
    const labels = kind === "issues"
      ? parseFrontMatterList(values.labels)
      : parseFrontMatterList(values.labels || values.reviewLabels);
    const metadata = recordDetailMeta(kind, values);
    const pendingNotice = options.pending ? `
        <div class="rounded-lg border border-dashed border-border bg-secondary/30 px-4 py-3 text-xs text-muted-foreground">This ${escapeHtml(config.itemLabel)} is still syncing to the maintainer's inbox and hasn't been drained to the public mirror yet, so it doesn't have a number assigned.</div>` : "";
    const pullPatch = parsed.pullPatch || { patch: "", files: [], unavailable: false };
    const pullConversation = parsed.pullConversation || [];
    const pullConversationSection = isPulls ? `
          <div data-repo-pull-conversation data-empty="${pullConversation.length ? "false" : "true"}" class="border-t border-border">${renderRepoPullConversation(pullConversation)}</div>
          ${renderPullReviewForm(number)}` : "";
    const pullFilesSection = isPulls ? `
        <section class="overflow-hidden rounded-lg border border-border" data-repo-record-badge-panel>
          <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="fingerprint" class="h-3.5 w-3.5 text-primary"></i>Badge</div>
          ${renderRepoPullBadge(title, options.pending ? 0 : number, author, pullPatch.files)}
        </section>
        <section class="overflow-hidden rounded-lg border border-border" data-repo-record-files-panel>
          <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="files" class="h-3.5 w-3.5 text-primary"></i>Files changed</span>
            <span class="font-mono text-[10px] text-muted-foreground">${formatCount(pullPatch.files.length)} files</span>
          </div>
          <div data-repo-pull-files>${renderRepoPullFiles(pullPatch.files)}</div>
        </section>
        <section class="overflow-hidden rounded-lg border border-border" data-repo-record-patch-panel>
          <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="git-compare-arrows" class="h-3.5 w-3.5 text-primary"></i>Patch</div>
          ${renderRepoPullPatch(pullPatch.patch, `pull:${repoKey(repo)}:${number}`)}
        </section>` : "";
    const discussionConversation = parsed.discussionConversation || [];
    const discussionConversationSection = isDiscussions ? `
        <section class="overflow-hidden rounded-lg border border-border" data-repo-record-conversation>
          <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="message-square" class="h-3.5 w-3.5 text-primary"></i>Replies</span>
            <span class="font-mono text-[10px] text-muted-foreground">${formatCount(discussionConversation.length)} ${discussionConversation.length === 1 ? "reply" : "replies"}</span>
          </div>
          <div data-repo-discussion-conversation data-empty="${discussionConversation.length ? "false" : "true"}">${renderRepoDiscussionConversation(discussionConversation)}</div>
          ${renderDiscussionReplyForm(number)}
        </section>` : "";
    const recordTabs = isPulls ? `
        <nav class="flex min-w-0 overflow-x-auto border-b border-border" aria-label="Pull request sections">
          <button type="button" data-repo-record-tab="conversation" class="inline-flex h-11 items-center gap-2 border-b-2 border-primary px-3 text-xs font-semibold text-foreground"><i data-lucide="message-square" class="h-3.5 w-3.5"></i>Conversation<span class="rounded-full bg-secondary px-1.5 py-0.5 font-mono text-[10px]">${formatCount(pullConversation.length)}</span></button>
          <button type="button" data-repo-record-tab="commits" class="inline-flex h-11 items-center gap-2 border-b-2 border-transparent px-3 text-xs font-semibold text-muted-foreground"><i data-lucide="git-commit-horizontal" class="h-3.5 w-3.5"></i>Commits<span class="rounded-full bg-secondary px-1.5 py-0.5 font-mono text-[10px]">1</span></button>
          <button type="button" data-repo-record-tab="files" class="inline-flex h-11 items-center gap-2 border-b-2 border-transparent px-3 text-xs font-semibold text-muted-foreground"><i data-lucide="files" class="h-3.5 w-3.5"></i>Files changed<span class="rounded-full bg-secondary px-1.5 py-0.5 font-mono text-[10px]">${formatCount(pullPatch.files.length)}</span></button>
          <button type="button" data-repo-record-tab="badge" class="inline-flex h-11 items-center gap-2 border-b-2 border-transparent px-3 text-xs font-semibold text-muted-foreground"><i data-lucide="fingerprint" class="h-3.5 w-3.5"></i>Badge</button>
        </nav>` : "";
    const sidebarSection = (label, value) => `
      <div class="border-t border-border py-4 first:border-t-0 first:pt-0">
        <h4 class="text-xs font-semibold text-muted-foreground">${label}</h4>
        <p class="mt-2 text-xs text-foreground">${value}</p>
      </div>`;
    const labelValue = labels.length
      ? labels.map((label) => `<span class="mr-1 mt-1 inline-flex rounded-full border border-border bg-secondary px-2 py-0.5 text-[10px] text-muted-foreground">${escapeHtml(label)}</span>`).join("")
      : "No labels";
    return `
      <article data-repo-record-detail="${escapeHtml(kind)}" class="grid gap-5 border-t border-border bg-background p-4">
        <div class="flex flex-wrap items-center justify-between gap-3">
          <button type="button" data-repo-record-back="${escapeHtml(kind)}" class="inline-flex h-8 items-center gap-2 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary"><i data-lucide="arrow-left" class="h-3.5 w-3.5"></i>Back to ${escapeHtml(config.label)}</button>
          <span class="font-mono text-xs text-muted-foreground">${escapeHtml(repo.owner || "owner")}/${escapeHtml(repo.name || "repo")} · ${recordLabel}</span>
        </div>
        ${pendingNotice}
        <header data-repo-record-hero class="grid gap-3">
          <h2 class="text-2xl font-semibold leading-tight text-foreground">${escapeHtml(title)} <span class="font-normal text-muted-foreground">${recordLabel}</span></h2>
          <p class="flex min-w-0 flex-wrap items-center gap-2 text-sm text-muted-foreground">
            <span data-repo-record-state class="inline-flex items-center gap-1.5 rounded-full bg-primary px-2.5 py-1 text-xs font-semibold text-primary-foreground"><i data-lucide="${config.icon}" class="h-3.5 w-3.5"></i>${escapeHtml(state)}</span>
            <span><span class="font-semibold text-foreground">${escapeHtml(author)}</span> ${isPulls ? `wants to merge 1 commit into <span class="rounded-md bg-primary/10 px-1.5 py-0.5 font-mono text-primary">${escapeHtml(baseBranch)}</span> from <span class="rounded-md bg-primary/10 px-1.5 py-0.5 font-mono text-primary">${escapeHtml(headBranch)}</span>` : `opened this ${escapeHtml(config.itemLabel)} ${escapeHtml(date)}`}</span>
          </p>
          ${recordTabs}
        </header>
        <div class="grid gap-5 xl:grid-cols-[minmax(0,1fr)_18rem]">
          <div class="grid min-w-0 gap-4">
            ${isDiscussions ? discussionConversationSection : `
              <section data-repo-record-conversation class="overflow-hidden rounded-lg border border-border">
                <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
                  <span class="inline-flex min-w-0 items-center gap-2 text-xs text-muted-foreground"><span class="font-semibold text-foreground">${escapeHtml(author)}</span> commented ${escapeHtml(date)}</span>
                  <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-semibold text-muted-foreground">Contributor</span>
                </div>
                <div data-repo-record-body class="whitespace-pre-wrap px-4 py-4 text-sm leading-6 text-foreground">${escapeHtml(body)}</div>
                ${pullConversationSection}
              </section>`}
            ${pullFilesSection}
          </div>
          <aside data-repo-record-sidebar class="min-w-0 text-xs">
            ${isPulls ? sidebarSection("Reviewers", `<span data-repo-pull-reviewers class="grid gap-0.5">${renderPullReviewers(pullConversation)}</span>`) : ""}
            ${sidebarSection("Assignees", "No one assigned")}
            ${sidebarSection("Labels", labelValue)}
            ${sidebarSection("Type", isPulls ? "Pull request" : isDiscussions ? "Discussion" : "Issue")}
            ${sidebarSection("Fields", "No fields configured")}
            ${sidebarSection("Projects", "No projects")}
            ${sidebarSection("Milestone", metadata.find(([label]) => label === "Milestone")?.[1] || "No milestone")}
            ${sidebarSection("Relationships", "None yet")}
            ${sidebarSection("Development", isPulls ? "Successfully merging this pull request may close these issues." : "No branches or pull requests")}
            ${sidebarSection("Notifications", '<button type="button" class="inline-flex h-8 w-full items-center justify-center gap-2 rounded-md border border-border bg-secondary px-3 font-semibold text-foreground"><i data-lucide="bell" class="h-3.5 w-3.5"></i>Subscribe</button>')}
            ${sidebarSection("Participants", `1 participant - ${escapeHtml(author)}`)}
          </aside>
        </div>
      </article>`;
  }

  async function loadRepoRecordDetail(repo, kind, number) {
    const config = repoCollectionConfig[kind];
    const container = $(`[data-repo-${kind}]`);
    if (!repo || !config || !container || !number) return;
    // Issues just submitted from this session sit in the maintainer's inbox
    // until drained, so there's nothing to fetch from the mirror yet - render
    // the detail straight from the local placeholder instead.
    const pendingItem = kind === "issues"
      ? state.issuesView.items.find((item) => item.pending && item.localId === number)
      : null;
    if (pendingItem) {
      container.innerHTML = renderRepoRecordDetail(repo, kind, number, {
        values: { title: pendingItem.title, status: pendingItem.status, authorName: pendingItem.author },
        body: pendingItem.body,
        options: { pending: true },
      });
      window.lucide?.createIcons();
      return;
    }
    const recordFile = typeof config.file === "function" ? config.file(Number(number)) : config.file;
    const recordPath = `${config.dir}/${number}/${recordFile}`;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml(`Loading ${escapeHtml(config.itemLabel)} #${escapeHtml(number)} from the live mirror...`)}</div>`;
    try {
      // Pulls read with ref:"" so the host serves the forkmesh/pulls branch.
      const refParams = kind === "pulls" ? { ref: "" } : {};
      const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: recordPath, ...refParams }));
      // Issues are signed-event JSON (issue-N.json), not markdown front matter,
      // so parse them the same way the list does and map into the shape the
      // detail renderer expects. Any live mirror serving .forkmesh/issues/
      // answers this blob read, so the detail loads whenever the list does.
      const parsed = kind === "issues"
        ? issueDetailParsed(blobText(blob), number)
        : parseFrontMatter(blobText(blob));
      const pullPatch = kind === "pulls" ? await loadRepoPullPatch(repo, number) : null;
      if (pullPatch) parsed.pullPatch = pullPatch;
      if (kind === "pulls") parsed.pullConversation = await loadRepoPullConversation(repo, number);
      if (kind === "discussions") parsed.discussionConversation = await loadRepoDiscussionConversation(repo, number);
      state.repoRecordDetail = { repo, kind, number, parsed };
      container.innerHTML = renderRepoRecordDetail(repo, kind, number, parsed);
    } catch (_) {
      container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">This ${escapeHtml(config.itemLabel)} is unavailable until a live desktop host serves ${escapeHtml(recordPath)}.</div>`;
    } finally {
      window.lucide?.createIcons();
    }
  }

  function setRepoTabCount(tab, count) {
    const badge = $(`[data-dashboard-repo-tab-count="${tab}"]`);
    if (badge) badge.textContent = formatCount(count);
  }

  // Amber "+N" badge for inbox items the relay is still holding for the owner
  // node's next sync — signed submissions that exist but aren't in the served
  // git mirror yet, so the regular tab count can't include them.
  function setRepoTabPending(tab, count) {
    const badge = $(`[data-dashboard-repo-tab-pending="${tab}"]`);
    if (!badge) return;
    const n = Number(count) || 0;
    badge.classList.toggle("hidden", n <= 0);
    if (n > 0) {
      badge.textContent = `+${formatCount(n)} pending`;
      badge.title = `${formatCount(n)} incoming item${n === 1 ? "" : "s"} waiting for the owner node to sync`;
    }
  }

  // Star/unstar a repo (GET/POST/DELETE /api/repo/o/r/star). A star is a
  // plain per-account preference, not part of the owner-signed catalog
  // record, so it's fetched and toggled separately from the rest of the
  // repo's data.
  function setRepoStarButtonState(button, starred, count) {
    if (!button) return;
    button.setAttribute("aria-pressed", starred ? "true" : "false");
    const icon = button.querySelector("[data-repo-star-icon]");
    if (icon) {
      icon.classList.toggle("fill-yellow-400", starred);
      icon.classList.toggle("text-yellow-400", starred);
      icon.classList.toggle("text-muted-foreground", !starred);
    }
    const label = button.querySelector("[data-repo-star-label]");
    if (label) label.textContent = starred ? "Starred" : "Star";
    const countEl = button.querySelector("[data-repo-star-count]");
    if (countEl) countEl.textContent = formatCount(Number(count) || 0);
  }

  async function loadRepoStarState(repo, button) {
    if (!button) return;
    try {
      const headers = { accept: "application/json" };
      if (state.session?.sessionToken) headers.authorization = "Bearer " + state.session.sessionToken;
      const response = await fetch(`${repoApiBase(repo)}/star`, { headers });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false || !document.body.contains(button)) return;
      setRepoStarButtonState(button, Boolean(data.starred), data.count);
    } catch (_) {
      /* offline relay — star count stays at its initial placeholder */
    }
  }

  // Hydrates every rendered star button in `root` (repo list cards, profile
  // rows) with its real starred state + count, one request per distinct repo.
  async function hydrateRepoStarButtons(root) {
    const buttons = Array.from((root || document).querySelectorAll("[data-repo-star-button][data-repo-key]"));
    const byKey = new Map();
    buttons.forEach((button) => {
      const key = button.dataset.repoKey || "";
      if (!key) return;
      if (!byKey.has(key)) byKey.set(key, []);
      byKey.get(key).push(button);
    });
    await Promise.all(Array.from(byKey.entries()).map(async ([key, els]) => {
      const repo = findRepository(key);
      if (!repo) return;
      await loadRepoStarState(repo, els[0]);
      const applied = els[0] && document.body.contains(els[0])
        ? { starred: els[0].getAttribute("aria-pressed") === "true", count: els[0].querySelector("[data-repo-star-count]")?.textContent }
        : null;
      if (applied) els.slice(1).forEach((el) => setRepoStarButtonState(el, applied.starred, applied.count));
    }));
  }

  async function toggleRepoStar(button) {
    const key = button.dataset.repoKey || "";
    const repo = findRepository(key)
      || (state.selectedRepo && repoKey(state.selectedRepo) === key ? state.selectedRepo : null);
    if (!repo) return;
    if (!state.session?.sessionToken) {
      location.href = "/login";
      return;
    }
    const nextStarred = button.getAttribute("aria-pressed") !== "true";
    button.disabled = true;
    try {
      const response = await fetch(`${repoApiBase(repo)}/star`, {
        method: nextStarred ? "POST" : "DELETE",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({ sessionToken: state.session.sessionToken }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) throw new Error(data.error || `HTTP ${response.status}`);
      setRepoStarButtonState(button, Boolean(data.starred), data.count);
    } catch (_) {
      /* best-effort — button keeps its last known state */
    } finally {
      button.disabled = false;
    }
  }

  // Content-free pending-inbox tallies (GET /api/repo/o/r/pending). Plain
  // fetch, not the caching fetchJson — the counts change as the owner node
  // drains its inbox and must refresh on every repo open. Best-effort: a miss
  // just leaves the badges hidden.
  async function loadRepoPendingCounts(repo) {
    try {
      const response = await fetch(`${repoApiBase(repo)}/pending`, {
        headers: { accept: "application/json" },
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data || data.ok === false) return;
      const pending = data.pending || {};
      ["issues", "pulls", "discussions", "commits"].forEach((tab) => {
        setRepoTabPending(tab, pending[tab]);
      });
    } catch (_) {
      /* offline relay — badges stay hidden */
    }
  }

  // Refreshes the "N Open" / "N Closed" counts shown in an issues/pulls panel
  // header once the real records are loaded (the initial render only knows a
  // bundled total, not the open/closed split).
  function setRepoCollectionCounts(kind, openCount, closedCount) {
    const openEl = $(`[data-repo-collection-open-count="${kind}"]`);
    if (openEl) openEl.textContent = tabCountLabel(openCount);
    const closedEl = $(`[data-repo-collection-closed-count="${kind}"]`);
    if (closedEl) closedEl.textContent = tabCountLabel(closedCount);
  }

  function applyServedCounts(counts) {
    if (!counts || typeof counts !== "object") return;
    // The Issues header shows OPEN issues only (issue #397): closed issues are
    // opt-in behind the Closed filter, so the tab badge counts the open issues
    // the host serves (counts.openIssues), and the panel's "N Open / N Closed"
    // split is filled from the served open/closed tallies. Older hosts that only
    // report a bundled total fall back to that total; the first view of the
    // Issues tab still refines the badge to the open count once issues load.
    const openIssues = Number.isFinite(Number(counts.openIssues))
      ? Number(counts.openIssues)
      : Number(counts.issues);
    if (Number.isFinite(openIssues)) {
      setRepoTabCount("issues", openIssues);
      if (Number.isFinite(Number(counts.closedIssues)))
        setRepoCollectionCounts("issues", openIssues, Number(counts.closedIssues));
    }
    if (Number.isFinite(Number(counts.pulls))) setRepoTabCount("pulls", Number(counts.pulls));
    if (Number.isFinite(Number(counts.discussions))) setRepoTabCount("discussions", Number(counts.discussions));
  }

  // Free-text issue search: substring match (case-insensitive) over the
  // number, title, body snippet, author, and the status/labels/milestone
  // meta string — everything renderRepoRecordList already shows per row, so
  // "matches the search" and "matches what's visibly displayed" stay the
  // same thing. Runs client-side over the already-loaded (batched) issue set
  // rather than a new network call, per the existing loadRepoIssues contract.
  function issueMatchesQuery(issue, query) {
    const q = String(query || "").trim().toLowerCase();
    if (!q) return true;
    const haystack = [
      "#" + issue.number, String(issue.number), issue.title, issue.body,
      issue.author, issue.meta,
    ].join(" ").toLowerCase();
    return haystack.includes(q);
  }

  function renderRepoIssues() {
    const container = $("[data-repo-issues]");
    if (!container) return;
    const issuesView = state.issuesView;
    const filtered = issuesView.items.filter((issue) => {
      if (issuesView.filter === "all") return true;
      if (issuesView.filter === "open") return issue.status === "open";
      return issue.status !== "open";
    }).filter((issue) => issueMatchesQuery(issue, issuesView.query));
    const config = repoCollectionConfig.issues;
    const filterBar = `<div class="flex items-center gap-1 border-b border-border px-4 py-2">
      ${["open", "closed", "all"].map((stateName) => `<button type="button" data-dashboard-issue-filter="${stateName}" aria-pressed="${stateName === "open" ? "true" : "false"}" class="inline-flex h-7 items-center rounded-md px-2.5 text-xs font-medium transition-colors ${stateName === issuesView.filter ? "bg-secondary text-foreground" : "text-muted-foreground hover:text-foreground"}">${stateName[0].toUpperCase() + stateName.slice(1)}</button>`).join("")}
    </div>`;
    const emptyLabel = issuesView.query
      ? `No issues matching "${escapeHtml(issuesView.query)}".`
      : `No ${issuesView.filter === "all" ? "" : issuesView.filter + " "}issues.`;
    container.innerHTML = filterBar + (filtered.length
      ? renderRepoRecordList(filtered, config, "issues")
      : `<div class="px-4 py-3 text-sm text-muted-foreground">${emptyLabel}</div>`);
    window.lucide?.createIcons();
  }

  function setIssueFilter(filter) {
    state.issuesView.filter = filter;
    $$("[data-dashboard-issue-filter]").forEach((btn) => {
      btn.setAttribute("aria-pressed", btn.dataset.dashboardIssueFilter === filter ? "true" : "false");
      btn.className = `inline-flex h-7 items-center rounded-md px-2.5 text-xs font-medium transition-colors ${btn.dataset.dashboardIssueFilter === filter ? "bg-secondary text-foreground" : "text-muted-foreground hover:text-foreground"}`;
    });
    renderRepoIssues();
  }

  async function loadRepoIssues(repo) {
    const container = $("[data-repo-issues]");
    if (!container) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading issues from the live mirror...")}</div>`;
    try {
      let tree;
      try {
        // List the git tree under .forkmesh/issues/ then read one
        // .forkmesh/issues/${number}/issue-${number}.json blob for each issue.
        tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: ".forkmesh/issues" }));
      } catch (error) {
        if (isMissingMirrorFolder(error)) {
          // No issues on the mirror yet - still surface the owner's offline
          // submissions kept locally while their node was down (issue #379).
          state.issuesView.items = reconcilePendingIssues(repo, []);
          state.issuesView.filter = "open";
          state.issuesView.query = "";
          renderRepoIssues();
          return;
        }
        throw error;
      }
      const dirs = (Array.isArray(tree.entries) ? tree.entries : [])
        .filter((entry) => entry.type === "tree" && /^\d+$/.test(String(entry.name || "")))
        .sort((a, b) => Number(b.name) - Number(a.name))
        .slice(0, 50);
      // One batched request for all of them, not one /blob call per issue.
      const blobs = await fetchRepoBlobs(
        repo, dirs.map((entry) => issueJsonPath(Number(entry.name))));
      const items = dirs.map((entry) => {
        const number = Number(entry.name);
        const blob = blobs[issueJsonPath(number)];
        if (!blob) return null;
        return parseIssueJson(blobText(blob), number);
      }).filter(Boolean);
      // Issue #379: fold in the owner's offline submissions (kept locally while
      // their source-of-truth node was down) so they still show up on reload,
      // dropping any the node has since drained - the numbered mirror copy wins.
      const pending = reconcilePendingIssues(repo, items);
      const merged = pending.length ? [...pending, ...items] : items;
      state.issuesView.items = merged;
      state.issuesView.filter = "open";
      state.issuesView.query = "";
      setRepoTabCount("issues", merged.filter((issue) => issue.status === "open").length);
      const openIssues = merged.filter((issue) => issue.status === "open").length;
      setRepoCollectionCounts("issues", openIssues, merged.length - openIssues);
      renderRepoIssues();
    } catch (_) {
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Issues are unavailable until a live desktop host serves the .forkmesh/issues/ folder.</div>';
    }
  }

  // --- Projects tab (issue #384) -------------------------------------------

  function ganttDayLabel(ms) {
    return new Date(ms).toLocaleDateString(undefined, { month: "short", day: "numeric" });
  }

  // Percent of a project's linked issues that are closed; falls back to the
  // linked milestone's issues (within the loaded window) when nothing is
  // linked directly. Returns -1 when there is nothing to measure.
  function projectProgress(project, issues) {
    const linked = issues.filter((issue) => project.issues.includes(issue.number));
    const pool = linked.length
      ? linked
      : (project.milestone ? issues.filter((issue) => issue.milestone === project.milestone) : []);
    if (!pool.length) return -1;
    return Math.round(pool.filter((issue) => issue.status !== "open").length * 100 / pool.length);
  }

  async function loadRepoProjects(repo) {
    const container = $("[data-repo-projects]");
    if (!container) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading projects from the live mirror...")}</div>`;
    try {
      let tree;
      try {
        tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: ".forkmesh/projects" }));
      } catch (error) {
        if (isMissingMirrorFolder(error)) {
          state.projectsView.items = [];
          state.projectsView.issues = [];
          renderRepoProjects();
          return;
        }
        throw error;
      }
      const dirs = (Array.isArray(tree.entries) ? tree.entries : [])
        .filter((entry) => entry.type === "tree" && /^\d+$/.test(String(entry.name || "")))
        .sort((a, b) => Number(b.name) - Number(a.name))
        .slice(0, 50);
      const blobs = await fetchRepoBlobs(
        repo, dirs.map((entry) => projectJsonPath(Number(entry.name))));
      const projects = dirs.map((entry) => {
        const number = Number(entry.name);
        const blob = blobs[projectJsonPath(number)];
        if (!blob) return null;
        return parseProjectJson(blobText(blob), number);
      }).filter(Boolean);
      // The gantt rows and progress bars need the linked issues' dates and
      // states, so read exactly those issue files in one batched request.
      const linkedNumbers = [...new Set(projects.flatMap((project) => project.issues))]
        .filter((number) => Number.isFinite(number) && number > 0);
      let issues = [];
      if (linkedNumbers.length) {
        const issueBlobs = await fetchRepoBlobs(repo, linkedNumbers.map((number) => issueJsonPath(number)));
        issues = linkedNumbers
          .map((number) => {
            const blob = issueBlobs[issueJsonPath(number)];
            return blob ? parseIssueJson(blobText(blob), number) : null;
          })
          .filter(Boolean);
      }
      // Milestone-linked progress reads whatever issues the Issues tab already
      // loaded (its most-recent window) - good enough for an overview ratio.
      (state.issuesView.items || []).forEach((issue) => {
        if (!issues.some((existing) => existing.number === issue.number)) issues.push(issue);
      });
      state.projectsView.items = projects;
      state.projectsView.issues = issues;
      renderRepoProjects();
    } catch (_) {
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Projects are unavailable until a live desktop host serves the .forkmesh/projects/ folder.</div>';
    }
  }

  function renderRepoProjects() {
    const container = $("[data-repo-projects]");
    if (!container) return;
    const view = state.projectsView;
    const items = view.items || [];
    const filtered = items.filter((project) => {
      if (view.filter === "all") return true;
      if (view.filter === "open") return project.status === "open";
      return project.status !== "open";
    });
    const filterBar = `<div class="flex flex-wrap items-center justify-between gap-2 border-b border-border px-4 py-2">
      <div class="flex items-center gap-1">
        ${["open", "closed", "all"].map((name) => `<button type="button" data-dashboard-project-filter="${name}" aria-pressed="${name === view.filter ? "true" : "false"}" class="inline-flex h-7 items-center rounded-md px-2.5 text-xs font-medium transition-colors ${name === view.filter ? "bg-secondary text-foreground" : "text-muted-foreground hover:text-foreground"}">${name[0].toUpperCase() + name.slice(1)}</button>`).join("")}
      </div>
      <div class="flex items-center gap-1 rounded-md border border-border p-0.5">
        ${[["gantt", "chart-gantt", "Gantt"], ["list", "list", "List"]].map(([mode, icon, label]) => `<button type="button" data-dashboard-project-view="${mode}" aria-pressed="${mode === view.mode ? "true" : "false"}" class="inline-flex h-6 items-center gap-1.5 rounded px-2 text-xs font-medium transition-colors ${mode === view.mode ? "bg-secondary text-foreground" : "text-muted-foreground hover:text-foreground"}"><i data-lucide="${icon}" class="h-3.5 w-3.5"></i>${label}</button>`).join("")}
      </div>
    </div>`;
    const emptyLabel = items.length
      ? `No ${view.filter === "all" ? "" : view.filter + " "}projects.`
      : "No projects have been committed to this mirror yet. Create them from the desktop client's Projects tab.";
    container.innerHTML = filterBar + (filtered.length
      ? (view.mode === "gantt"
        ? renderRepoProjectsGantt(filtered, view.issues || [])
        : renderRepoProjectsList(filtered, view.issues || []))
      : `<div class="px-4 py-3 text-sm text-muted-foreground">${emptyLabel}</div>`);
    window.lucide?.createIcons();
  }

  function setProjectFilter(filter) {
    state.projectsView.filter = filter;
    renderRepoProjects();
  }

  function setProjectView(mode) {
    state.projectsView.mode = mode === "list" ? "list" : "gantt";
    renderRepoProjects();
  }

  function renderRepoProjectsList(projects, issues) {
    const issueByNumber = new Map(issues.map((issue) => [issue.number, issue]));
    return projects.map((project) => {
      const progress = projectProgress(project, issues);
      const dates = project.startDate || project.endDate
        ? `${project.startDate ? ganttDayLabel(project.startDate) : "…"} → ${project.endDate ? ganttDayLabel(project.endDate) : "…"}`
        : "";
      const chips = project.issues.slice(0, 12).map((number) => {
        const issue = issueByNumber.get(number);
        const closed = issue && issue.status !== "open";
        return `<button type="button" data-repo-record-kind="issues" data-repo-record-number="${number}" title="${escapeHtml(issue ? issue.title : `issue #${number}`)}" class="rounded-full border border-border px-2 py-0.5 font-mono text-[10px] transition-colors hover:bg-secondary ${closed ? "text-muted-foreground line-through" : "text-foreground"}">#${number}</button>`;
      }).join("");
      return `
        <div class="border-t border-border px-4 py-3">
          <div class="flex min-w-0 flex-wrap items-center gap-x-2 gap-y-1">
            <i data-lucide="chart-gantt" class="h-4 w-4 text-primary"></i>
            <span class="min-w-0 truncate text-sm font-semibold text-foreground">${escapeHtml(project.title)}</span>
            <span data-repo-record-state class="rounded-md border border-border bg-secondary/60 px-2 py-0.5 text-[10px] font-mono text-foreground">${escapeHtml(project.status)}</span>
            ${dates ? `<span class="font-mono text-[10px] text-muted-foreground">${escapeHtml(dates)}</span>` : ""}
            ${project.milestone ? `<span class="inline-flex items-center gap-1 rounded-full border border-border bg-secondary px-2 py-0.5 text-[10px] font-medium text-muted-foreground"><i data-lucide="milestone" class="h-3 w-3"></i>${escapeHtml(project.milestone)}</span>` : ""}
          </div>
          ${project.body ? `<p class="mt-1 truncate text-xs text-muted-foreground">${escapeHtml(project.body).slice(0, 200)}</p>` : ""}
          <div class="mt-2 flex items-center gap-3">
            <div class="h-1.5 w-40 overflow-hidden rounded-full bg-secondary"><span class="block h-full rounded-full bg-primary" style="width:${progress < 0 ? 0 : progress}%"></span></div>
            <span class="font-mono text-[10px] text-muted-foreground">${progress < 0 ? "no linked issues" : `${progress}% · ${project.issues.length} issue${project.issues.length === 1 ? "" : "s"}`}</span>
          </div>
          ${chips ? `<div class="mt-2 flex flex-wrap items-center gap-1.5">${chips}</div>` : ""}
        </div>`;
    }).join("");
  }

  // The gantt view: one labeled row per project (rounded track + progress
  // fill) with its linked issues indented beneath, over a shared month axis
  // with a "today" rule. Chrome uses theme tokens so both modes stay readable;
  // every bar is direct-labeled by its row so color never carries meaning
  // alone.
  function renderRepoProjectsGantt(projects, issues) {
    const issueByNumber = new Map(issues.map((issue) => [issue.number, issue]));
    const rows = [];
    projects.forEach((project) => {
      rows.push({
        kind: "project",
        label: project.title,
        start: project.startDate,
        end: project.endDate,
        createdAt: project.createdAtMs,
        status: project.status,
        progress: projectProgress(project, issues),
      });
      project.issues.forEach((number) => {
        const issue = issueByNumber.get(number);
        if (!issue) return;
        rows.push({
          kind: "issue",
          label: `#${number} ${issue.title}`,
          start: issue.startDate,
          end: issue.endDate,
          createdAt: issue.createdAtMs,
          status: issue.status,
          number,
        });
      });
    });
    const now = Date.now();
    const DAY = 24 * 60 * 60 * 1000;
    let min = Infinity;
    let max = -Infinity;
    rows.forEach((row) => {
      if (row.start > 0) min = Math.min(min, row.start);
      if (row.end > 0) max = Math.max(max, row.end);
      if (!(row.start > 0) && !(row.end > 0) && row.createdAt > 0) min = Math.min(min, row.createdAt);
    });
    if (!Number.isFinite(min)) min = now - 30 * DAY;
    if (!Number.isFinite(max)) max = now;
    max = Math.max(max, Math.min(now, min + 365 * DAY));
    if (max - min < 14 * DAY) max = min + 14 * DAY;
    const pad = (max - min) * 0.04;
    min -= pad;
    max += pad;
    const span = max - min;
    const pct = (ms) => Math.min(100, Math.max(0, (ms - min) * 100 / span));
    // Month ticks (day ticks when the whole range fits inside ~2 months).
    const ticks = [];
    const cursor = new Date(min);
    cursor.setHours(0, 0, 0, 0);
    if (span > 62 * DAY) {
      cursor.setDate(1);
      cursor.setMonth(cursor.getMonth() + 1);
      while (cursor.getTime() < max) {
        ticks.push({ ms: cursor.getTime(), label: cursor.toLocaleDateString(undefined, { month: "short", year: span > 300 * DAY ? "2-digit" : undefined }) });
        cursor.setMonth(cursor.getMonth() + 1);
      }
    } else {
      const step = span > 21 * DAY ? 7 : span > 10 * DAY ? 2 : 1;
      cursor.setDate(cursor.getDate() + step);
      while (cursor.getTime() < max) {
        ticks.push({ ms: cursor.getTime(), label: ganttDayLabel(cursor.getTime()) });
        cursor.setDate(cursor.getDate() + step);
      }
    }
    const todayVisible = now > min && now < max;
    const GUTTER = "13rem";
    const barTitle = (row) => {
      const range = row.start > 0 || row.end > 0
        ? `${row.start > 0 ? ganttDayLabel(row.start) : "?"} → ${row.end > 0 ? ganttDayLabel(row.end) : "?"}`
        : "no dates set";
      const progress = row.kind === "project" && row.progress >= 0 ? ` · ${row.progress}% done` : "";
      return `${row.label} · ${range} · ${row.status}${progress}`;
    };
    const barHtml = (row) => {
      const dated = row.start > 0 || row.end > 0;
      const startMs = row.start > 0 ? row.start : (dated ? min + pad : Math.max(min, row.createdAt || min));
      const endMs = row.end > 0 ? row.end : (dated ? Math.max(startMs + DAY, Math.min(now, max)) : Math.min(now, max));
      const left = pct(startMs);
      const width = Math.max(0.8, pct(Math.max(endMs, startMs + DAY)) - left);
      const closed = row.status !== "open";
      if (!dated) {
        return `<span class="absolute top-1/2 h-2 -translate-y-1/2 rounded-full border border-dashed border-muted-foreground/50" style="left:${left}%;width:${width}%"></span>`;
      }
      if (row.kind === "project") {
        const progress = row.progress < 0 ? 0 : row.progress;
        return `<span class="absolute top-1/2 h-4 -translate-y-1/2 overflow-hidden rounded-md border border-primary/50 bg-primary/20 ${closed ? "opacity-55" : ""}" style="left:${left}%;width:${width}%"><span class="absolute inset-y-0 left-0 bg-primary" style="width:${progress}%"></span></span>`;
      }
      return `<span class="absolute top-1/2 h-2.5 -translate-y-1/2 rounded-full bg-accent ${closed ? "opacity-40" : ""}" style="left:${left}%;width:${width}%"></span>`;
    };
    const rowsHtml = rows.map((row) => `
      <div class="grid grid-cols-[${GUTTER}_minmax(0,1fr)] items-center gap-3 rounded ${row.kind === "project" ? "h-9" : "h-7"} px-1 hover:bg-secondary/40">
        <span class="flex min-w-0 items-center gap-1.5 ${row.kind === "project" ? "text-xs font-semibold text-foreground" : "pl-5 text-[11px] text-muted-foreground"}">
          <span class="min-w-0 truncate">${escapeHtml(row.label)}</span>
          ${row.kind === "project" && row.progress >= 0 ? `<span class="shrink-0 font-mono text-[10px] font-normal text-muted-foreground">${row.progress}%</span>` : ""}
        </span>
        <div class="relative h-full" title="${escapeHtml(barTitle(row))}">${barHtml(row)}</div>
      </div>`).join("");
    return `
      <div class="p-4">
        <div class="mb-2 flex flex-wrap items-center justify-end gap-4 text-[10px] text-muted-foreground">
          <span class="inline-flex items-center gap-1.5"><span class="h-2 w-4 rounded-sm border border-primary/50 bg-primary/40"></span>Projects</span>
          <span class="inline-flex items-center gap-1.5"><span class="h-2 w-4 rounded-full bg-accent"></span>Issues</span>
          ${todayVisible ? '<span class="inline-flex items-center gap-1.5"><span class="h-3 w-px bg-destructive"></span>Today</span>' : ""}
        </div>
        <div class="overflow-x-auto">
          <div class="min-w-[36rem]" data-repo-projects-gantt>
            <div class="grid grid-cols-[${GUTTER}_minmax(0,1fr)] gap-3">
              <span></span>
              <div class="relative h-5">
                ${ticks.map((tick) => `<span class="absolute top-0 -translate-x-1/2 font-mono text-[10px] text-muted-foreground" style="left:${pct(tick.ms)}%">${escapeHtml(tick.label)}</span>`).join("")}
              </div>
            </div>
            <div class="relative">
              <div class="pointer-events-none absolute inset-y-0 right-0" style="left:calc(${GUTTER} + 0.75rem + 0.25rem)">
                ${ticks.map((tick) => `<span class="absolute inset-y-0 w-px bg-border/70" style="left:${pct(tick.ms)}%"></span>`).join("")}
                ${todayVisible ? `<span class="absolute inset-y-0 w-px bg-destructive/70" style="left:${pct(now)}%"></span>` : ""}
              </div>
              ${rowsHtml}
            </div>
          </div>
        </div>
      </div>`;
  }

  async function loadRepoCollection(repo, kind, containerSelector) {
    const container = $(containerSelector);
    const config = repoCollectionConfig[kind];
    if (!container || !config) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml(`Loading ${escapeHtml(config.label.toLowerCase())} from the live mirror...`)}</div>`;
    try {
      const items = await loadRepoRecordsFromMirror(repo, config);
      container.innerHTML = items.length
        ? renderRepoRecordList(items, config, kind)
        : `<div class="px-4 py-3 text-sm text-muted-foreground">${config.empty}</div>`;
      if (kind === "pulls") {
        const openPulls = items.filter((item) => item.state === "open").length;
        setRepoTabCount("pulls", openPulls);
        setRepoCollectionCounts("pulls", openPulls, items.length - openPulls);
      }
    } catch (_) {
      container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${escapeHtml(config.label)} are unavailable until a live desktop host serves the ${escapeHtml(config.dir)}/ folder.</div>`;
    } finally {
      window.lucide?.createIcons();
    }
  }

  // True when the logged-in account is the node that owns (hosts) this repo -
  // the only account whose node can actually pick an "assign to agent" issue up
  // and run a coding agent on it.
  function sessionOwnsRepo(repo) {
    // A repo's owner is a NODE account name. The logged-in account matches
    // either directly (it IS that node) or because the user OWNS that node
    // (adhoc #53 claim/link) — state.session.nodes is the list of node names
    // the user owns, the same alias set profileContributionAliases folds in.
    // Without this, a user whose repos are published by a linked node account
    // could never edit About / assign agents from the web (backend
    // _account_owns_node applies the identical rule).
    const repoOwner = String(repo?.owner || "").toLowerCase();
    if (!repoOwner) return false;
    const me = String(state.session?.nodeName || "").toLowerCase();
    if (me && me === repoOwner) return true;
    const owned = Array.isArray(state.session?.nodes) ? state.session.nodes : [];
    return owned.some((n) => String(n || "").toLowerCase() === repoOwner);
  }

  // True when the logged-in account may assign an issue to a coding agent on
  // this repo's node: the repo owner itself, or an admin acting on the owner's
  // behalf (admin node-ownership, adhoc #141). The backend independently
  // re-checks the account name against owner/admin status (no password,
  // adhoc #225), so this is only the client-side gate for showing the checkbox.
  function sessionCanAssignAgent(repo) {
    return sessionOwnsRepo(repo) || Boolean(state.session?.isAdmin);
  }

  function setRepoAboutEditing(on) {
    const detail = $("[data-repo-detail]");
    const text = detail?.querySelector("[data-repo-about-description]");
    const form = detail?.querySelector("[data-repo-about-form]");
    const input = detail?.querySelector("[data-repo-about-input]");
    if (!text || !form) return;
    text.classList.toggle("hidden", Boolean(on));
    form.classList.toggle("hidden", !on);
    if (on) input?.focus();
  }

  function setRepoAboutStatus(message, tone = "") {
    const status = $("[data-repo-about-status]");
    if (!status) return;
    status.textContent = message || "";
    status.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
  }

  function applyRepoAboutDescription(repo, description) {
    const text = String(description || "").trim();
    repo.description = text;
    const key = repoKey(repo).toLowerCase();
    const canonical = repoCanonicalKey(repo).toLowerCase();
    (state.repositories || []).forEach((item) => {
      if (repoKey(item).toLowerCase() === key ||
          repoCanonicalKey(item).toLowerCase() === canonical) {
        item.description = text;
      }
    });
    $$("[data-repo-about-description]").forEach((el) => {
      el.textContent = text || "No description published.";
    });
    $$("[data-repo-about-input]").forEach((el) => {
      el.value = text;
    });
  }

  async function saveRepoAboutFromWeb(repo, description, media = {}) {
    // media may carry logoPng / bannerPng (data-URL PNG, "" = remove). Only
    // keys actually present are sent, so an untouched image stays unchanged.
    const response = await fetch(`${repoApiBase(repo)}/about`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        ownerAccount: state.session?.nodeName || "",
        sessionToken: state.session?.sessionToken || "",
        description,
        ...media,
      }),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body?.ok === false) {
      throw new Error(body?.error || "about_update_failed");
    }
    return body;
  }

  // --- About rail: fediverse badge + desktop-parity sections ---------------
  //
  // The desktop app's About panel shows the repo's canonical info from
  // .forkmesh/info.json plus latest release / languages / files /
  // contributors it computes from the local git mirror. The website mirrors
  // all of it from data it can already reach: the live-mirror tunnel (blob,
  // tree, history) and the relay's public GET /about (branding + fediverse
  // follower count). Every section is best-effort and independent — a dead
  // host must not blank the whole rail.

  const REPO_LANGUAGE_EXTENSIONS = {
    c: "C", h: "C++", cc: "C++", cpp: "C++", cxx: "C++", hpp: "C++",
    py: "Python", js: "JavaScript", mjs: "JavaScript", jsx: "JavaScript",
    ts: "TypeScript", tsx: "TypeScript", html: "HTML", htm: "HTML",
    css: "CSS", json: "JSON", md: "Markdown", yml: "YAML", yaml: "YAML",
    sh: "Shell", bash: "Shell", dart: "Dart", java: "Java", kt: "Kotlin",
    rs: "Rust", go: "Go", rb: "Ruby", php: "PHP", swift: "Swift",
    m: "Objective-C", mm: "Objective-C", qml: "QML", cmake: "CMake",
    toml: "TOML", sql: "SQL", proto: "Protobuf", svg: "SVG",
  };
  const REPO_LANGUAGE_COLORS = {
    "C": "#555555", "C++": "#f34b7d", Python: "#3572A5",
    JavaScript: "#f1e05a", TypeScript: "#3178c6", HTML: "#e34c26",
    CSS: "#563d7c", JSON: "#8a8a8a", Markdown: "#083fa1", YAML: "#cb171e",
    Shell: "#89e051", Dart: "#00B4AB", Java: "#b07219", Kotlin: "#A97BFF",
    Rust: "#dea584", Go: "#00ADD8", Ruby: "#701516", PHP: "#4F5D95",
    Swift: "#F05138", "Objective-C": "#438eff", QML: "#44a51c",
    CMake: "#DA3434", TOML: "#9c4221", SQL: "#e38c00", Protobuf: "#4a76c6",
    SVG: "#ff9900",
  };

  function applyRepoAboutWebsite(website) {
    // Sync the About rail's website link + the gear form's input. Empty value
    // hides the link.
    const value = String(website || "").trim();
    const link = $("[data-repo-about-website]");
    const label = $("[data-repo-about-website-label]");
    const input = $("[data-repo-about-website-input]");
    if (input && document.activeElement !== input) input.value = value;
    if (!link || !label) return;
    if (value && /^https?:\/\//i.test(value)) {
      link.href = value;
      label.textContent = value.replace(/^https?:\/\//i, "").replace(/\/$/, "");
      link.classList.remove("hidden");
      link.classList.add("inline-flex");
    } else {
      link.classList.add("hidden");
      link.classList.remove("inline-flex");
    }
  }

  function repoAboutStillCurrent(repo) {
    return state.selectedRepo
      && repoKey(state.selectedRepo).toLowerCase() === repoKey(repo).toLowerCase();
  }

  function relativeTimeLabel(ms) {
    const ts = Number(ms) || 0;
    if (!ts) return "";
    const delta = Math.max(0, Date.now() - ts);
    const minutes = Math.floor(delta / 60000);
    if (minutes < 60) return `${Math.max(1, minutes)} minute${minutes === 1 ? "" : "s"} ago`;
    const hours = Math.floor(minutes / 60);
    if (hours < 48) return `${hours} hour${hours === 1 ? "" : "s"} ago`;
    const days = Math.floor(hours / 24);
    if (days < 60) return `${days} days ago`;
    return formatDate(ts);
  }

  async function loadRepoFediverse(repo) {
    // Public branding + follower count from the relay (GET /about). Fills the
    // Watch button count, the popover, and the social badge header.
    try {
      const body = await fetchJson(`${repoApiBase(repo)}/about`);
      if (!repoAboutStillCurrent(repo) || !body?.ok) return;
      const followers = Number(body.fediverse?.followers || 0);
      $$("[data-repo-watch-count], [data-repo-watch-followers], [data-repo-social-followers]").forEach((el) => {
        el.textContent = formatCount(followers);
      });
      // WHO is watching: newest followers (handle + remote profile link),
      // each row captioned with what they follow.
      const watchList = $("[data-repo-watch-list]");
      if (watchList) {
        const entries = Array.isArray(body.fediverse?.followersList)
          ? body.fediverse.followersList : [];
        if (entries.length) {
          const following = `Following ${repoKey(repo).toLowerCase()}`;
          watchList.innerHTML = entries.slice(0, 25).map((f) => {
            const handleText = escapeHtml(String(f?.handle || f?.url || ""));
            const url = String(f?.url || "");
            const who = /^https?:\/\//i.test(url)
              ? `<a href="${escapeHtml(url)}" target="_blank" rel="noopener noreferrer" class="min-w-0 truncate font-mono text-[11px] text-foreground hover:underline">${handleText}</a>`
              : `<span class="min-w-0 truncate font-mono text-[11px] text-foreground">${handleText}</span>`;
            return `<div class="flex items-center justify-between gap-2 py-0.5">${who}<span class="shrink-0 text-[10px] text-muted-foreground">${escapeHtml(following)}</span></div>`;
          }).join("");
          watchList.classList.remove("hidden");
        } else {
          watchList.innerHTML = "";
          watchList.classList.add("hidden");
        }
      }
      // Owner switched federation off (or the instance did): say so instead
      // of silently showing zeros.
      $("[data-repo-watch-disabled]")?.classList.toggle(
        "hidden", body.fediverse?.enabled !== false);
      // Seed the gear form's federation switches with the stored settings so
      // an untouched save round-trips them unchanged.
      const apSettings = body.fediverse?.settings;
      if (apSettings && typeof apSettings === "object") {
        [["[data-repo-ap-federate]", "federate"],
         ["[data-repo-ap-broadcast]", "broadcastEvents"],
         ["[data-repo-ap-comments]", "acceptComments"]].forEach(([sel, key]) => {
          const box = $(sel);
          if (box && key in apSettings) box.checked = Boolean(apSettings[key]);
        });
      }
      const handle = String(body.fediverse?.handle || "");
      if (handle) {
        const handleEl = $("[data-repo-watch-handle]");
        if (handleEl) handleEl.textContent = handle;
        const copyButton = $("[data-repo-watch-wrap] [data-dashboard-copy]");
        if (copyButton) copyButton.setAttribute("data-dashboard-copy", handle);
        const badgeHandle = $("[data-repo-social-handle]");
        if (badgeHandle) { badgeHandle.textContent = handle; badgeHandle.title = handle; }
        // Point every "View on Mastodon" link at the authoritative handle.
        $$("[data-repo-mastodon-link]").forEach((el) => {
          el.setAttribute("href", `https://mastodon.social/${handle}`);
        });
      }
      const logo = $("[data-repo-social-logo]");
      if (logo) logo.src = body.logoUrl || body.defaultLogoUrl || "/assets/fediverse-avatar.png";
      const banner = $("[data-repo-social-banner]");
      if (banner) {
        const url = body.bannerUrl || body.defaultBannerUrl || "/assets/fediverse-banner.png";
        banner.style.backgroundImage = `url('${url.replace(/'/g, "%27")}')`;
      }
      if (body.description && !repo.description) {
        applyRepoAboutDescription(repo, body.description);
      }
      // Relay-known website seeds the link/form; the committed
      // .forkmesh/info.json (loadRepoAboutInfo) overrides it when the live
      // mirror is reachable.
      if (body.website) applyRepoAboutWebsite(body.website);
    } catch (_) { /* fediverse card is an adornment, never an error */ }
  }

  async function loadRepoAboutInfo(repo) {
    // .forkmesh/info.json is the repo's own committed About (what the desktop
    // shows); when present it wins over the relay catalog description.
    try {
      const blobs = await fetchRepoBlobs(repo, [".forkmesh/info.json"]);
      const blob = blobs[".forkmesh/info.json"];
      if (!blob || !repoAboutStillCurrent(repo)) return;
      let info;
      try { info = JSON.parse(blobText(blob)); } catch (_) { return; }
      const about = String(info?.about || "").trim();
      // The committed file is canonical, so it also seeds the gear editor —
      // editing starts from exactly what the page (and the desktop app) show.
      if (about) applyRepoAboutDescription(repo, about);
      applyRepoAboutWebsite(String(info?.website || ""));
    } catch (_) { /* host offline — keep catalog description */ }
  }

  async function loadRepoAboutRelease(repo) {
    try {
      let tree;
      try {
        tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: "releases" }));
      } catch (_) { return; }
      const channels = (Array.isArray(tree?.entries) ? tree.entries : [])
        .filter((entry) => entry.type === "tree" && entry.name)
        .map((entry) => String(entry.name));
      if (!channels.length) return;
      const paths = channels.map((channel) => `.forkmesh/releases/${channel}/release.json`);
      const blobs = await fetchRepoBlobs(repo, paths);
      let latest = null;
      paths.forEach((path) => {
        const blob = blobs[path];
        if (!blob) return;
        let manifest;
        try { manifest = JSON.parse(blobText(blob)); } catch (_) { return; }
        if (manifest?.tag &&
            (!latest || Number(manifest.published_at || 0) > Number(latest.published_at || 0))) {
          latest = manifest;
        }
      });
      if (!latest || !repoAboutStillCurrent(repo)) return;
      const section = $("[data-repo-about-release]");
      const body = $("[data-repo-about-release-body]");
      if (!section || !body) return;
      const when = relativeTimeLabel(Number(latest.published_at || 0));
      body.innerHTML = `
        <span class="inline-flex items-center gap-1.5 rounded-md bg-primary/15 px-2 py-0.5 font-mono text-[11px] font-semibold text-primary"><i data-lucide="tag" class="h-3 w-3"></i>${escapeHtml(String(latest.tag))}</span>
        ${when ? `<p class="mt-1.5">released ${escapeHtml(when)}</p>` : ""}`;
      section.classList.remove("hidden");
      window.lucide?.createIcons();
    } catch (_) { /* no releases — section stays hidden */ }
  }

  async function loadRepoAboutFilesAndLanguages(repo) {
    try {
      const files = await buildRepoFileIndex(repo);
      if (!files.length || !repoAboutStillCurrent(repo)) return;
      const partial = Boolean(state.repoFileFinder?.partial);
      const filesSection = $("[data-repo-about-files]");
      const filesCount = $("[data-repo-about-files-count]");
      if (filesSection && filesCount) {
        filesCount.textContent = `${files.length.toLocaleString()}${partial ? "+" : ""} files`;
        filesSection.classList.remove("hidden");
      }
      const tally = {};
      let categorized = 0;
      files.forEach((path) => {
        const name = String(path).split("/").pop() || "";
        const ext = name.includes(".") ? name.split(".").pop().toLowerCase() : "";
        const language = REPO_LANGUAGE_EXTENSIONS[ext];
        if (!language) return;
        tally[language] = (tally[language] || 0) + 1;
        categorized += 1;
      });
      const ranked = Object.entries(tally).sort((a, b) => b[1] - a[1]).slice(0, 6);
      if (!ranked.length || !categorized) return;
      const bar = $("[data-repo-about-langs-bar]");
      const legend = $("[data-repo-about-langs-legend]");
      const section = $("[data-repo-about-langs]");
      if (!bar || !legend || !section) return;
      bar.innerHTML = ranked.map(([language, count]) => {
        const color = REPO_LANGUAGE_COLORS[language] || "#8a8a8a";
        const pct = (count / categorized) * 100;
        return `<span title="${escapeHtml(language)}" style="width:${pct.toFixed(1)}%;background-color:${color}"></span>`;
      }).join("");
      legend.innerHTML = ranked.map(([language, count]) => {
        const color = REPO_LANGUAGE_COLORS[language] || "#8a8a8a";
        const pct = ((count / categorized) * 100).toFixed(1);
        return `<span class="inline-flex items-center gap-1.5"><span class="h-2 w-2 rounded-full" style="background-color:${color}"></span><span class="font-medium text-foreground">${escapeHtml(language)}</span> ${pct}%</span>`;
      }).join("");
      section.classList.remove("hidden");
    } catch (_) { /* host offline — sections stay hidden */ }
  }

  async function loadRepoAboutContributors(repo) {
    try {
      const data = await fetchRepoJson(repoLiveUrl(repo, "history"));
      const commits = Array.isArray(data?.commits) ? data.commits : [];
      if (!commits.length || !repoAboutStillCurrent(repo)) return;
      const tally = {};
      commits.forEach((commit) => {
        const author = String(commit.author || "").trim();
        if (author) tally[author] = (tally[author] || 0) + 1;
      });
      const ranked = Object.entries(tally).sort((a, b) => b[1] - a[1]);
      if (!ranked.length) return;
      const section = $("[data-repo-about-contribs]");
      const count = $("[data-repo-about-contribs-count]");
      const list = $("[data-repo-about-contribs-list]");
      if (!section || !count || !list) return;
      count.textContent = String(ranked.length);
      list.innerHTML = ranked.slice(0, 14).map(([author, commitCount]) => {
        // Deterministic hue per author so avatars are stable across loads.
        let hash = 0;
        for (let i = 0; i < author.length; i += 1) hash = (hash * 31 + author.charCodeAt(i)) >>> 0;
        const hue = hash % 360;
        const initial = (author[0] || "?").toUpperCase();
        return `<span title="${escapeHtml(author)} — ${commitCount} commit${commitCount === 1 ? "" : "s"}" class="flex h-8 w-8 items-center justify-center rounded-full border border-background font-mono text-[11px] font-semibold text-white shadow-sm" style="background-color:hsl(${hue} 55% 42%)">${escapeHtml(initial)}</span>`;
      }).join("");
      section.classList.remove("hidden");
    } catch (_) { /* host offline — section stays hidden */ }
  }

  function loadRepoAboutRail(repo) {
    if (!repo || repo.isPrivate) {
      // Private repos have no fediverse presence; live sections still apply.
      loadRepoAboutInfo(repo);
      loadRepoAboutRelease(repo);
      loadRepoAboutFilesAndLanguages(repo);
      loadRepoAboutContributors(repo);
      return;
    }
    loadRepoFediverse(repo);
    loadRepoAboutInfo(repo);
    loadRepoAboutRelease(repo);
    loadRepoAboutFilesAndLanguages(repo);
    loadRepoAboutContributors(repo);
  }

  // --- Owner-only "Agents" tab (adhoc #182) -----------------------------
  //
  // Shows the desktop app's running/finished Claude Code agent sessions for
  // this repo, and lets the owner send a follow-up prompt to a live one. The
  // desktop pushes the session list; the browser has no signing key, so it
  // re-proves account ownership by node account name (no password, adhoc #225)
  // via the same ownerAccount check the worker uses for the "Assign to agent"
  // issue checkbox.

  // A session counts as still actionable (promptable) unless it's reached one
  // of these terminal states. Mirrors the worker/desktop's own status names;
  // kept as an allowlist-of-terminal-states so an unrecognized/future status
  // defaults to promptable rather than silently hiding the input.
  const AGENT_TERMINAL_STATUSES = new Set(["success", "failed", "stopped", "cleared"]);

  // Mirrors the desktop app's known model aliases (agentModelLabel in
  // MainWindowInternal.h) so a web-picked model renders the same short label
  // once the agent session shows up in the Agents tab. Empty value leaves the
  // provider's own default in place.
  const AGENT_MODEL_OPTIONS = [
    { value: "", label: "Provider default" },
    { value: "auto", label: "Auto" },
    { value: "opus", label: "Opus" },
    { value: "sonnet", label: "Sonnet" },
    { value: "haiku", label: "Haiku" },
    { value: "fable", label: "Fable" },
  ];

  // Agent-provider dropdown (adhoc #234): mirrors the desktop app's provider
  // picker so the owner can choose which agent the node auto-starts. Empty
  // value leaves the node's default provider in place.
  const AGENT_PROVIDER_OPTIONS = [
    { value: "", label: "Node default" },
    { value: "claude-code", label: "Claude Code" },
    { value: "claude-api", label: "Claude API" },
    { value: "openai", label: "OpenAI API" },
  ];

  // Agent-model dropdown for the "start a new agent" composer (adhoc #276):
  // lets the owner choose which Claude model the agent uses. Distinct from
  // AGENT_MODEL_OPTIONS above (used by the issue "assign to agent" checkbox,
  // adhoc #182) since that one mirrors the desktop app's short model aliases
  // while this one sends a full model id. Empty value leaves the provider's
  // own default in place.
  const AGENT_NEW_MODEL_OPTIONS = [
    { value: "", label: "Provider default" },
    { value: "claude-opus-4-8", label: "Claude Opus 4.8" },
    { value: "claude-sonnet-5", label: "Claude Sonnet 5" },
    { value: "claude-sonnet-4-6", label: "Claude Sonnet 4.6" },
    { value: "claude-haiku-4-5", label: "Claude Haiku 4.5" },
    { value: "fable-5", label: "Fable 5" },
  ];

  function repoAgentsCanPrompt(status) {
    return !AGENT_TERMINAL_STATUSES.has(String(status || "").toLowerCase());
  }

  function repoAgentStatusTone(status) {
    const value = String(status || "").toLowerCase();
    if (value === "success") return "text-primary";
    if (value === "failed" || value === "stopped") return "text-destructive";
    return "text-yellow-500";
  }

  function repoAgentIssueLabel(agent) {
    return agent.issueNumber
      ? `#${agent.issueNumber} ${agent.issueTitle || ""}`.trim()
      : (agent.issueTitle || "");
  }

  // A summary row (adhoc #259): the inline prompt moved to the detail page, so
  // the whole row is now a click target that opens the transcript + prompt view.
  function renderRepoAgentRow(agent) {
    const issueLabel = repoAgentIssueLabel(agent);
    return `
      <button type="button" data-repo-agent-open data-repo-agent-id="${escapeHtml(String(agent.id ?? ""))}" class="grid w-full gap-2 px-4 py-3 text-left text-xs transition-colors hover:bg-secondary/50">
        <div class="flex flex-wrap items-center gap-2">
          <span class="rounded-full border border-border px-2 py-0.5 font-mono ${repoAgentStatusTone(agent.status)}">${escapeHtml(agent.status || "unknown")}</span>
          ${issueLabel ? `<span class="min-w-0 truncate font-medium text-foreground">${escapeHtml(issueLabel)}</span>` : ""}
          <span class="ml-auto font-mono text-muted-foreground">${escapeHtml(agent.model || "")}</span>
          <i data-lucide="chevron-right" class="h-3.5 w-3.5 shrink-0 text-muted-foreground"></i>
        </div>
        <div class="flex flex-wrap items-center gap-3 text-muted-foreground">
          ${agent.provider ? `<span>${escapeHtml(agent.provider)}</span>` : ""}
          <span>${formatCount(agent.numTurns)} turns</span>
          ${agent.durationMs ? `<span>${escapeHtml(formatServeSpeed(agent.durationMs))}</span>` : ""}
          <span>${escapeHtml(formatUsd(agent.costUsd))}</span>
          ${agent.branchName ? `<span class="font-mono">${escapeHtml(agent.branchName)}</span>` : ""}
        </div>
        ${agent.lastError ? `<div class="text-destructive">${escapeHtml(agent.lastError)}</div>` : ""}
      </button>`;
  }

  // Detail page (adhoc #259): live transcript pane + a prompt area to interact
  // with one agent. The wrapper carries data-repo-agent-id so the shared prompt
  // submit / hint plumbing resolves the same way it did for an inline list row.
  function renderRepoAgentDetail(agent) {
    const promptable = repoAgentsCanPrompt(agent.status);
    const issueLabel = repoAgentIssueLabel(agent);
    return `
      <div data-repo-agent-detail data-repo-agent-id="${escapeHtml(String(agent.id ?? ""))}" class="grid gap-3 px-4 py-3 text-xs">
        <div class="flex flex-wrap items-center gap-2">
          <button type="button" data-repo-agent-back class="inline-flex h-7 items-center gap-1.5 rounded-md border border-border px-2 text-xs font-medium text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="arrow-left" class="h-3.5 w-3.5"></i>Agents</button>
          <span data-repo-agent-status class="rounded-full border border-border px-2 py-0.5 font-mono ${repoAgentStatusTone(agent.status)}">${escapeHtml(agent.status || "unknown")}</span>
          ${issueLabel ? `<span class="min-w-0 truncate font-medium text-foreground">${escapeHtml(issueLabel)}</span>` : ""}
          <span class="ml-auto font-mono text-muted-foreground">${escapeHtml(agent.model || "")}</span>
        </div>
        <div class="flex flex-wrap items-center gap-3 text-muted-foreground">
          ${agent.provider ? `<span>${escapeHtml(agent.provider)}</span>` : ""}
          <span>${formatCount(agent.numTurns)} turns</span>
          ${agent.durationMs ? `<span>${escapeHtml(formatServeSpeed(agent.durationMs))}</span>` : ""}
          <span>${escapeHtml(formatUsd(agent.costUsd))}</span>
          ${agent.branchName ? `<span class="font-mono">${escapeHtml(agent.branchName)}</span>` : ""}
        </div>
        ${agent.lastError ? `<div class="text-destructive">${escapeHtml(agent.lastError)}</div>` : ""}
        <div class="flex items-center gap-2 text-[11px] font-medium text-muted-foreground">
          <i data-lucide="terminal" class="h-3.5 w-3.5"></i>Live transcript
        </div>
        <pre data-repo-agent-transcript class="max-h-[420px] min-h-[160px] overflow-auto whitespace-pre-wrap break-words rounded-md border border-border bg-secondary/40 p-3 font-mono text-[11px] leading-relaxed text-foreground">Loading transcript…</pre>
        ${promptable ? `
        <form data-repo-agent-prompt-form class="flex items-center gap-2">
          <input data-repo-agent-prompt-input type="text" maxlength="8000" placeholder="Send a message to this agent" class="h-8 min-w-0 flex-1 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary" />
          <button type="submit" data-repo-agent-prompt-submit class="inline-flex h-8 shrink-0 items-center gap-1.5 rounded-md bg-primary px-2.5 text-xs font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="send" class="h-3.5 w-3.5"></i>Send</button>
        </form>
        <span data-repo-agent-prompt-hint class="text-[11px] text-muted-foreground"></span>`
          : '<div class="text-[11px] text-muted-foreground">This session has finished - you can no longer send it messages.</div>'}
      </div>`;
  }

  // The "start a new agent" composer lives in a single top modal opened from
  // the robot button in the header nav (adhoc #62) - it was previously pinned
  // above the repo tab bar (adhoc #278). Because the header is global, the
  // modal carries its own repository picker instead of relying on a repo page
  // being open. The node's prompt drain still recognises the "new" sentinel
  // agent id and spins up an ad-hoc run.
  function agentModalRepoChoices() {
    const seen = new Set();
    const choices = [];
    for (const repo of state.repositories) {
      if (!repo?.owner || !repo?.name || !sessionCanAssignAgent(repo)) continue;
      const key = repoKey(repo).toLowerCase();
      if (seen.has(key)) continue;
      seen.add(key);
      choices.push(repo);
    }
    return choices.sort((a, b) => repoKey(a).localeCompare(repoKey(b)));
  }

  function agentModalSelectedRepo() {
    const value = String($("#agentModal [data-agent-modal-repo]")?.value || "");
    if (!value.includes("/")) return null;
    return state.repositories.find((repo) => repoKey(repo).toLowerCase() === value.toLowerCase()) || null;
  }

  function setAgentModalOpen(open) {
    const modal = $("#agentModal");
    if (!modal) return;
    if (open) {
      // Provider/model options are JS constants shared with the desktop app's
      // picker, so the static modal markup gets them filled in on first open.
      const providerSelect = modal.querySelector("[data-repo-agent-new-provider]");
      if (providerSelect && !providerSelect.options.length) {
        providerSelect.innerHTML = AGENT_PROVIDER_OPTIONS.map((opt) => `<option value="${escapeHtml(opt.value)}">${escapeHtml(opt.label)}</option>`).join("");
      }
      const modelSelect = modal.querySelector("[data-repo-agent-new-model]");
      if (modelSelect && !modelSelect.options.length) {
        modelSelect.innerHTML = AGENT_NEW_MODEL_OPTIONS.map((opt) => `<option value="${escapeHtml(opt.value)}">${escapeHtml(opt.label)}</option>`).join("");
      }
      const repoSelect = modal.querySelector("[data-agent-modal-repo]");
      if (repoSelect) {
        const choices = agentModalRepoChoices();
        const currentKey = state.selectedRepo && sessionCanAssignAgent(state.selectedRepo)
          ? repoKey(state.selectedRepo).toLowerCase()
          : "";
        repoSelect.innerHTML = choices.length
          ? choices.map((repo) => `<option value="${escapeHtml(repoKey(repo))}"${repoKey(repo).toLowerCase() === currentKey ? " selected" : ""}>${escapeHtml(repoKey(repo))}</option>`).join("")
          : '<option value="">No repositories you can start agents on</option>';
        repoSelect.disabled = !choices.length;
      }
      const hint = modal.querySelector("[data-repo-agent-new-hint]");
      if (hint) hint.textContent = "";
      window.lucide?.createIcons();
    }
    if (open) {
      // Screenshot paste/attach (adhoc #78): wire the modal's file input, paste
      // handler and chip list once, then reset any leftover attachments so each
      // fresh open starts clean.
      wireAgentModalImages(modal);
      const form = modal.querySelector("[data-repo-agent-new-form]");
      if (form) {
        form._pendingAgentImages = [];
        renderAgentModalChips(form);
      }
    }
    modal.classList.toggle("hidden", !open);
    if (open) modal.querySelector("[data-repo-agent-new-input]")?.focus();
  }

  // Pasted/attached screenshots queued alongside a "start agent" prompt
  // (adhoc #78). They ride to the node as data: URLs; the node writes them into
  // the agent's working tree so a coding agent can actually see the screenshot.
  function agentModalImages(form) {
    if (!form) return [];
    if (!form._pendingAgentImages) form._pendingAgentImages = [];
    return form._pendingAgentImages;
  }

  function renderAgentModalChips(form) {
    const list = form?.querySelector("[data-repo-agent-new-attachments]");
    if (!list) return;
    list.innerHTML = agentModalImages(form).map((img) => `
      <span class="inline-flex items-center gap-1.5 rounded-md border border-border bg-secondary/50 px-2 py-1 text-[11px] text-foreground">
        <i data-lucide="image" class="h-3 w-3 text-muted-foreground"></i>${escapeHtml(img.name)}
        <button type="button" data-repo-agent-new-attachment-remove="${img.id}" class="text-muted-foreground hover:text-destructive" aria-label="Remove ${escapeHtml(img.name)}">&times;</button>
      </span>`).join("");
    window.lucide?.createIcons();
  }

  async function addAgentModalImage(form, file, setHint) {
    const images = agentModalImages(form);
    if (!file || !file.type.startsWith("image/")) {
      setHint?.("That's not an image.", "bad");
      return;
    }
    if (images.length >= AGENT_IMAGE_MAX_COUNT) {
      setHint?.(`You can attach up to ${AGENT_IMAGE_MAX_COUNT} screenshots.`, "bad");
      return;
    }
    if (file.size > ISSUE_IMAGE_RAW_MAX_BYTES) {
      setHint?.(`That image is too large (max ${formatSize(ISSUE_IMAGE_RAW_MAX_BYTES)}).`, "bad");
      return;
    }
    const used = images.reduce((sum, img) => sum + img.size, 0);
    const budget = Math.min(AGENT_IMAGE_MAX_BYTES, AGENT_IMAGE_MAX_TOTAL_BYTES - used);
    if (budget <= 0) {
      setHint?.("Attached screenshots already use the size limit - remove one to add another.", "bad");
      return;
    }
    let dataUrl;
    let size;
    if (file.size <= budget) {
      try {
        dataUrl = await readAsDataUrl(file);
        size = file.size;
      } catch (_) {
        setHint?.("Could not read that image.", "bad");
        return;
      }
    } else {
      // Oversized paste/pick: reuse the issue composer's crop/compress modal to
      // squeeze it under the per-attachment budget.
      setHint?.(`Compressing to fit under ${formatSize(budget)}…`);
      const result = await openImageResizeModal(file, budget);
      if (!result) {
        setHint?.("");
        return;
      }
      dataUrl = result.dataUrl;
      size = result.size;
    }
    const id = `agent-image:${Date.now().toString(36)}${images.length}`;
    const name = (file.name || "screenshot.png").replace(/[[\]]/g, "_");
    images.push({ id, name, dataUrl, size });
    setHint?.(`Attached ${name}.`, "good");
    renderAgentModalChips(form);
  }

  function wireAgentModalImages(modal) {
    if (!modal || modal._agentImagesWired) return;
    modal._agentImagesWired = true;
    const form = modal.querySelector("[data-repo-agent-new-form]");
    const input = modal.querySelector("[data-repo-agent-new-input]");
    const fileInput = modal.querySelector("[data-repo-agent-new-file-input]");
    const attachBtn = modal.querySelector("[data-repo-agent-new-attach]");
    const chips = modal.querySelector("[data-repo-agent-new-attachments]");
    const hint = modal.querySelector("[data-repo-agent-new-hint]");
    const setHint = (text, tone) => {
      if (!hint) return;
      hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      hint.textContent = text;
    };
    attachBtn?.addEventListener("click", () => fileInput?.click());
    fileInput?.addEventListener("change", async () => {
      const files = Array.from(fileInput.files || []);
      fileInput.value = "";
      for (const file of files) await addAgentModalImage(form, file, setHint);
    });
    input?.addEventListener("paste", async (event) => {
      const imageItems = Array.from(event.clipboardData?.items || [])
        .filter((it) => it.kind === "file" && it.type.startsWith("image/"));
      if (!imageItems.length) return;
      // A screenshot in the clipboard shouldn't also dump its (empty) text into
      // the prompt field - claim the paste for the attachment instead.
      event.preventDefault();
      for (const it of imageItems) {
        const file = it.getAsFile();
        if (file) await addAgentModalImage(form, file, setHint);
      }
    });
    chips?.addEventListener("click", (event) => {
      const button = event.target.closest("[data-repo-agent-new-attachment-remove]");
      if (!button) return;
      const images = agentModalImages(form);
      const index = images.findIndex((img) => img.id === button.dataset.repoAgentNewAttachmentRemove);
      if (index >= 0) images.splice(index, 1);
      renderAgentModalChips(form);
    });
  }

  function renderRepoAgentsList(agents) {
    const container = $("[data-repo-agents]");
    if (!container) return;
    // When a detail page is open for a still-present agent, render that instead
    // of the list (adhoc #259). If the selected agent has vanished from a fresh
    // fetch, fall back to the list so we never strand the user on a dead page.
    const selectedId = state.agentsView.selectedAgentId;
    const selected = selectedId != null
      ? agents.find((a) => String(a.id ?? "") === String(selectedId))
      : null;
    if (selected) {
      container.innerHTML = renderRepoAgentDetail(selected);
      window.lucide?.createIcons();
      return;
    }
    if (selectedId != null) state.agentsView.selectedAgentId = null;
    // The "start a new agent" composer now lives in the header robot-button
    // modal (adhoc #62), so this list is just the sessions.
    container.innerHTML = agents.length
      ? `<div class="divide-y divide-border">${agents.map(renderRepoAgentRow).join("")}</div>`
      : '<div class="px-4 py-3 text-sm text-muted-foreground">No agent sessions yet. Start one from the robot button in the header, or from the desktop app.</div>';
    window.lucide?.createIcons();
  }

  // Open / close the detail page for one agent. Transcript refresh is explicit:
  // opening the page, clicking Refresh, or sending a prompt triggers a fetch.
  function openRepoAgentDetail(repo, agentId) {
    state.agentsView.selectedAgentId = agentId;
    renderRepoAgentsList(state.agentsView.agents);
    loadRepoAgentTranscript(repo, agentId);
  }

  function closeRepoAgentDetail(repo) {
    state.agentsView.selectedAgentId = null;
    renderRepoAgentsList(state.agentsView.agents);
  }

  // Fetch + render one agent's transcript tail. Preserves the reader's scroll
  // position unless they're already pinned to the bottom, in which case it keeps
  // following the tail as new output streams in.
  async function loadRepoAgentTranscript(repo, agentId) {
    const pre = $("[data-repo-agent-transcript]");
    if (!pre || !repo) return;
    try {
      const response = await fetch(`${repoApiBase(repo)}/agents/${encodeURIComponent(agentId)}/transcript`, {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({ ownerAccount: state.session?.nodeName || "", sessionToken: state.session?.sessionToken || "" }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) throw new Error(data.error || `HTTP ${response.status}`);
      // Only touch the pane if it's still the open agent - a slow response that
      // lands after the user navigated away must not clobber the new view.
      if (String(state.agentsView.selectedAgentId ?? "") !== String(agentId)) return;
      const current = $("[data-repo-agent-transcript]");
      if (!current) return;
      const atBottom = current.scrollHeight - current.scrollTop - current.clientHeight < 24;
      const text = String(data.transcript || "");
      current.textContent = text || "No transcript yet - waiting for the agent to produce output.";
      if (atBottom) current.scrollTop = current.scrollHeight;
      const statusEl = $("[data-repo-agent-status]");
      if (statusEl && data.status) {
        statusEl.textContent = data.status;
        statusEl.className = `rounded-full border border-border px-2 py-0.5 font-mono ${repoAgentStatusTone(data.status)}`;
      }
    } catch (error) {
      const current = $("[data-repo-agent-transcript]");
      if (current && (!current.textContent.trim().length || current.textContent === "Loading transcript…")) {
        current.textContent = "Could not load the transcript. Retrying…";
      }
    }
  }

  async function requestRepoAgentsList(repo) {
    const response = await fetch(`${repoApiBase(repo)}/agents/list`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify({
        ownerAccount: state.session?.nodeName || "",
        sessionToken: state.session?.sessionToken || "",
      }),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return Array.isArray(data.agents) ? data.agents : [];
  }

  async function loadRepoAgents(repo) {
    const container = $("[data-repo-agents]");
    if (!container || !repo) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading agent sessions...")}</div>`;
    try {
      const agents = await requestRepoAgentsList(repo);
      state.agentsView.agents = agents;
      renderRepoAgentsList(agents);
    } catch (error) {
      const code = String(error?.message || "");
      container.innerHTML = `<div class="px-4 py-3 text-sm text-destructive">${
        code === "not_authorized" ? "You don't have permission to view agents for this repository."
          : "Could not load agent sessions. Please try again."}</div>`;
      window.lucide?.createIcons();
    }
  }


  async function handleRepoAgentPromptSubmit(repo, form) {
    // The prompt form now lives inside the detail page (adhoc #259); resolve the
    // agent id from the nearest element carrying it (detail wrapper or, for any
    // legacy inline row, the row itself).
    const row = form.closest("[data-repo-agent-id]");
    const agentId = row?.dataset.repoAgentId || "";
    const input = form.querySelector("[data-repo-agent-prompt-input]");
    const submit = form.querySelector("[data-repo-agent-prompt-submit]");
    const hint = row?.querySelector("[data-repo-agent-prompt-hint]");
    const setHint = (text, tone) => {
      if (!hint) return;
      hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      hint.textContent = text;
    };
    const text = String(input?.value || "").trim();
    if (!agentId) return;
    if (!text) {
      setHint("Write a message before sending.", "bad");
      return;
    }
    if (submit) submit.disabled = true;
    setHint("Sending...");
    try {
      const response = await fetch(`${repoApiBase(repo)}/agents/${encodeURIComponent(agentId)}/prompt`, {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          ownerAccount: state.session?.nodeName || "",
          sessionToken: state.session?.sessionToken || "",
          text,
        }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) {
        throw new Error(data.error || `HTTP ${response.status}`);
      }
      if (input) input.value = "";
      setHint("Sent to the agent.", "good");
      loadRepoAgentTranscript(repo, agentId);
    } catch (error) {
      const code = String(error?.message || "");
      setHint(
        code === "text_required" ? "Write a message before sending."
          : code === "text_too_long" ? "Message is too long."
          : code === "prompt_queue_full" ? "Too many pending messages for this repository - try again shortly."
          : code === "not_authorized" ? "You don't have permission to send messages."
            : "Could not send the message. Please try again.",
        "bad");
    } finally {
      if (submit) submit.disabled = false;
    }
  }

  // Header-modal composer: queue a "new agent" prompt for the picked repo
  // (adhoc #266, moved into the modal by adhoc #62). Reuses the per-agent
  // prompt endpoint with the "new" sentinel agent id, which the owner's node
  // turns into a fresh ad-hoc agent run on drain.
  async function handleRepoAgentNewSubmit(repo, form) {
    const input = form.querySelector("[data-repo-agent-new-input]");
    const providerSelect = form.querySelector("[data-repo-agent-new-provider]");
    const modelSelect = form.querySelector("[data-repo-agent-new-model]");
    const submit = form.querySelector("[data-repo-agent-new-submit]");
    const hint = form.querySelector("[data-repo-agent-new-hint]");
    const setHint = (text, tone) => {
      if (!hint) return;
      hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      hint.textContent = text;
    };
    if (!repo) {
      setHint("Pick a repository you own to start an agent.", "bad");
      return;
    }
    const text = String(input?.value || "").trim();
    if (!text) {
      setHint("Enter a prompt to start an agent.", "bad");
      return;
    }
    // Pasted/attached screenshots (adhoc #78) ride along as data: URLs.
    const images = agentModalImages(form).map((img) => img.dataUrl).filter(Boolean);
    if (submit) submit.disabled = true;
    setHint("Starting…");
    try {
      const response = await fetch(`${repoApiBase(repo)}/agents/new/prompt`, {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          ownerAccount: state.session?.nodeName || "",
          sessionToken: state.session?.sessionToken || "",
          text,
          provider: String(providerSelect?.value || ""),
          model: String(modelSelect?.value || ""),
          images,
        }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) {
        throw new Error(data.error || `HTTP ${response.status}`);
      }
      if (input) input.value = "";
      form._pendingAgentImages = [];
      renderAgentModalChips(form);
      setHint("Sent - the node will start a new agent shortly.", "good");
      // Refresh the Agents tab only when it's showing the repo we just
      // prompted - the modal can target any owned repo from any page.
      if (state.activeRepoTab === "agents" && state.selectedRepo && repoKey(state.selectedRepo).toLowerCase() === repoKey(repo).toLowerCase()) {
        loadRepoAgents(repo);
      }
      // Close the modal now that the prompt is queued (adhoc #80).
      setAgentModalOpen(false);
    } catch (error) {
      const code = String(error?.message || "");
      setHint(
        code === "text_required" ? "Enter a prompt to start an agent."
          : code === "text_too_long" ? "Prompt is too long."
          : code === "prompt_queue_full" ? "Too many pending prompts for this repository - try again shortly."
          : code === "image_too_large" || code === "images_too_large" ? "The attached screenshot is too large - remove or shrink it."
          : code === "not_authorized" ? "You don't have permission to start agents."
            : "Could not start the agent. Please try again.",
        "bad");
    } finally {
      if (submit) submit.disabled = false;
    }
  }
  function openIssueCompose(repo) {
    const container = $("[data-repo-issues]");
    if (!container || !repo) return;
    const canAssignAgent = sessionCanAssignAgent(repo);
    container.innerHTML = `
      <form data-repo-issue-form class="grid gap-3 border-t border-border bg-background p-4">
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span class="inline-flex items-center gap-2 text-sm font-semibold text-foreground"><i data-lucide="circle-dot" class="h-4 w-4 text-primary"></i>New issue</span>
          <button type="button" data-repo-issue-cancel class="inline-flex h-8 items-center gap-2 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary"><i data-lucide="arrow-left" class="h-3.5 w-3.5"></i>Back to issues</button>
        </div>
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Title
          <input data-repo-issue-title type="text" required maxlength="240" placeholder="Short, descriptive title" class="h-9 rounded-md border border-border bg-background px-3 text-sm text-foreground outline-none focus:border-primary" />
        </label>
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Description
          <textarea data-repo-issue-body rows="6" placeholder="Describe the issue. Markdown is supported." class="rounded-md border border-border bg-background px-3 py-2 text-sm text-foreground outline-none focus:border-primary"></textarea>
        </label>
        <div class="grid gap-2">
          <div class="flex flex-wrap items-center gap-2">
            <button type="button" data-repo-issue-attach-image class="inline-flex h-8 items-center gap-2 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary"><i data-lucide="paperclip" class="h-3.5 w-3.5"></i>Attach images</button>
            <span data-repo-issue-attach-hint class="text-[11px] text-muted-foreground"></span>
          </div>
          <input type="file" data-repo-issue-file-input multiple accept="image/png,image/jpeg,image/gif,image/webp" class="hidden" />
          <div data-repo-issue-attachments class="flex flex-wrap gap-2"></div>
        </div>
        ${canAssignAgent ? `
        <div class="grid gap-2">
          <label class="flex items-center gap-2 text-xs text-muted-foreground">
            <input type="checkbox" data-repo-issue-assign-agent class="h-3.5 w-3.5 rounded border-border" />
            <span>Assign to agent - once filed, ${sessionOwnsRepo(repo) ? "your" : escapeHtml(repo.owner || "the owner") + "'s"} node starts a coding agent on it automatically</span>
          </label>
          <div class="ml-5 flex flex-wrap items-center gap-4">
            <label class="flex items-center gap-2 text-xs text-muted-foreground">
              Agent
              <select data-repo-issue-agent-provider disabled class="h-7 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary disabled:opacity-50">
                ${AGENT_PROVIDER_OPTIONS.map((opt) => `<option value="${escapeHtml(opt.value)}">${escapeHtml(opt.label)}</option>`).join("")}
              </select>
            </label>
            <label class="flex items-center gap-2 text-xs text-muted-foreground">
              Model
              <select data-repo-issue-agent-model disabled class="h-7 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary disabled:opacity-50">
                ${AGENT_MODEL_OPTIONS.map((opt) => `<option value="${escapeHtml(opt.value)}">${escapeHtml(opt.label)}</option>`).join("")}
              </select>
            </label>
          </div>
        </div>` : ""}
        <div class="flex flex-wrap items-center justify-between gap-3">
          <div class="flex min-w-0 flex-col gap-1">
            ${composeIdentityHtml(state.session, "Filing")}
            <span data-repo-issue-hint class="text-[11px] text-muted-foreground">Sent to the maintainer's inbox for review.</span>
          </div>
          <button type="submit" data-repo-issue-submit class="inline-flex h-9 items-center gap-2 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="send" class="h-4 w-4"></i>Submit issue</button>
        </div>
      </form>`;
    window.lucide?.createIcons();

    const form = container.querySelector("[data-repo-issue-form]");
    const attachButton = container.querySelector("[data-repo-issue-attach-image]");
    const fileInput = container.querySelector("[data-repo-issue-file-input]");
    const bodyInput = container.querySelector("[data-repo-issue-body]");
    const attachHint = container.querySelector("[data-repo-issue-attach-hint]");
    const attachmentsList = container.querySelector("[data-repo-issue-attachments]");
    const assignAgentInput = container.querySelector("[data-repo-issue-assign-agent]");
    const agentModelInput = container.querySelector("[data-repo-issue-agent-model]");
    const agentProviderInput = container.querySelector("[data-repo-issue-agent-provider]");
    assignAgentInput?.addEventListener("change", () => {
      if (agentModelInput) agentModelInput.disabled = !assignAgentInput.checked;
      if (agentProviderInput) agentProviderInput.disabled = !assignAgentInput.checked;
    });
    // Queued images: a short placeholder (not the data URL) is inserted into
    // the body textarea so it stays readable/editable; the real data: URL is
    // swapped in right before signing (handleIssueComposeSubmit).
    const images = [];
    if (form) form._pendingIssueImages = images;

    const setAttachHint = (text, tone) => {
      if (!attachHint) return;
      attachHint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : "text-muted-foreground"}`;
      attachHint.textContent = text;
    };
    const renderAttachmentChips = () => {
      if (!attachmentsList) return;
      attachmentsList.innerHTML = images.map((img) => `
        <span class="inline-flex items-center gap-1.5 rounded-md border border-border bg-secondary/50 px-2 py-1 text-[11px] text-foreground">
          <i data-lucide="image" class="h-3 w-3 text-muted-foreground"></i>${escapeHtml(img.name)}
          <button type="button" data-repo-issue-attachment-remove="${img.id}" class="text-muted-foreground hover:text-destructive" aria-label="Remove ${escapeHtml(img.name)}">&times;</button>
        </span>`).join("");
      window.lucide?.createIcons();
    };
    attachmentsList?.addEventListener("click", (event) => {
      const removeButton = event.target.closest("[data-repo-issue-attachment-remove]");
      if (!removeButton) return;
      const id = removeButton.dataset.repoIssueAttachmentRemove;
      const index = images.findIndex((img) => img.id === id);
      if (index < 0) return;
      const [removed] = images.splice(index, 1);
      if (bodyInput) bodyInput.value = bodyInput.value.split(`\n![${removed.name}](${removed.id})\n`).join("\n");
      renderAttachmentChips();
    });

    // Shared by both the file picker and clipboard paste: validate, embed (or
    // crop/compress if oversized), then insert a placeholder into the body at
    // the caret so pasted screenshots land right where the cursor was.
    const addIssueImageFile = async (file, rawName) => {
      const name = rawName.replace(/[[\]]/g, "_");
      if (!file.type.startsWith("image/")) {
        setAttachHint(`${name}: not an image.`, "bad");
        return;
      }
      if (images.length >= ISSUE_IMAGE_MAX_COUNT) {
        setAttachHint(`You can attach up to ${ISSUE_IMAGE_MAX_COUNT} images.`, "bad");
        return;
      }
      if (file.size > ISSUE_IMAGE_RAW_MAX_BYTES) {
        setAttachHint(`${name} is too large to attach (max ${formatSize(ISSUE_IMAGE_RAW_MAX_BYTES)}).`, "bad");
        return;
      }
      const total = images.reduce((sum, img) => sum + img.size, 0);
      const budget = Math.min(ISSUE_IMAGE_MAX_BYTES, ISSUE_IMAGE_MAX_TOTAL_BYTES - total);
      if (budget <= 0) {
        setAttachHint("Attached images already use up the issue's size limit - remove one to add another.", "bad");
        return;
      }
      let dataUrl;
      let size;
      if (file.size <= budget) {
        try {
          dataUrl = await readAsDataUrl(file);
          size = file.size;
        } catch (_) {
          setAttachHint(`Could not read ${name}.`, "bad");
          return;
        }
      } else {
        setAttachHint(`${name} is ${formatSize(file.size)} - crop or compress it to fit under ${formatSize(budget)}.`);
        const result = await openImageResizeModal(file, budget);
        if (!result) {
          setAttachHint("");
          return;
        }
        dataUrl = result.dataUrl;
        size = result.size;
      }
      const id = `forkmesh-pending-image:${Date.now().toString(36)}${images.length}`;
      images.push({ id, name, dataUrl, size });
      const start = bodyInput?.selectionStart ?? bodyInput?.value.length ?? 0;
      const end = bodyInput?.selectionEnd ?? start;
      if (bodyInput) {
        const insertion = `\n![${name}](${id})\n`;
        bodyInput.value = bodyInput.value.slice(0, start) + insertion + bodyInput.value.slice(end);
        const cursor = start + insertion.length;
        bodyInput.selectionStart = bodyInput.selectionEnd = cursor;
      }
      setAttachHint("");
    };

    if (attachButton && fileInput) {
      attachButton.addEventListener("click", () => fileInput.click());
      fileInput.addEventListener("change", async () => {
        const files = Array.from(fileInput.files || []);
        fileInput.value = "";
        for (const file of files) await addIssueImageFile(file, file.name);
        renderAttachmentChips();
      });
    }
    bodyInput?.addEventListener("paste", async (event) => {
      const imageItems = Array.from(event.clipboardData?.items || [])
        .filter((item) => item.kind === "file" && item.type.startsWith("image/"));
      if (!imageItems.length) return;
      // A pasted screenshot has no useful text form, so claim the paste instead
      // of letting the browser also dump it in as an inline object/blank text.
      event.preventDefault();
      for (const item of imageItems) {
        const file = item.getAsFile();
        if (file) await addIssueImageFile(file, file.name || "screenshot.png");
      }
      renderAttachmentChips();
    });
    container.querySelector("[data-repo-issue-title]")?.focus();
  }

  async function handleIssueComposeSubmit(repo, form) {
    if (!repo || !form) return;
    const titleInput = form.querySelector("[data-repo-issue-title]");
    const bodyInput = form.querySelector("[data-repo-issue-body]");
    const submit = form.querySelector("[data-repo-issue-submit]");
    const assignAgentInput = form.querySelector("[data-repo-issue-assign-agent]");
    const agentModelInput = form.querySelector("[data-repo-issue-agent-model]");
    const agentProviderInput = form.querySelector("[data-repo-issue-agent-provider]");
    const hint = form.querySelector("[data-repo-issue-hint]");
    const setHint = (text, tone) => {
      if (hint) hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      if (hint) hint.textContent = text;
    };
    const title = String(titleInput?.value || "").trim();
    if (!title) {
      setHint("Enter a title for the issue.", "bad");
      titleInput?.focus();
      return;
    }
    const assignAgent = Boolean(assignAgentInput?.checked);
    const agentModel = assignAgent ? String(agentModelInput?.value || "") : "";
    const agentProvider = assignAgent ? String(agentProviderInput?.value || "") : "";
    // Swap each attached image's short placeholder back out for its real
    // data: URL now, right before signing - the signed content hash has to
    // cover exactly what gets sent.
    let body = String(bodyInput?.value || "");
    const images = form._pendingIssueImages || [];
    for (const img of images) body = body.split(img.id).join(img.dataUrl);
    if (submit) submit.disabled = true;
    setHint("Signing and sending…");
    try {
      await submitWebIssue(repo, title, body, assignAgent, agentModel, agentProvider);
      // Submissions land in the maintainer's inbox, not the public mirror, so it
      // won't be visible there until they drain it - but show it locally, on
      // top of this session's issue list, so the submitter sees it right away.
      const pendingItem = {
        number: null,
        localId: `pending-${Date.now().toString(36)}`,
        title,
        status: "open",
        author: state.session?.nodeName || "you",
        date: "just now",
        meta: "",
        body,
        wantsAgent: assignAgent,
        pending: true,
      };
      state.issuesView.items = [pendingItem, ...state.issuesView.items];
      // Issue #379: when the owner files an issue while their source-of-truth
      // node is offline but a mirror is serving the repo, persist it locally so
      // it keeps showing up across reloads - fully, not just this session -
      // until the node comes back online and drains it to the mirror.
      const ownerOffline = isRepoOwner(repo) && repoServedByMirror(repo);
      if (ownerOffline) savePendingIssue(repo, pendingItem);
      setRepoTabCount("issues", state.issuesView.items.filter((issue) => issue.status === "open").length);
      if (titleInput) titleInput.value = "";
      if (bodyInput) bodyInput.value = "";
      images.length = 0;
      form.querySelector("[data-repo-issue-attachments]")?.replaceChildren();
      if (submit) submit.disabled = false;
      setHint(
        ownerOffline
          ? "Your source-of-truth node is offline, so this issue is held on a mirror and will sync to your node when it comes back online."
          : "Issue sent to the maintainer's inbox for review. Submit another or go back.",
        "good");
    } catch (error) {
      if (submit) submit.disabled = false;
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "The maintainer's inbox is full. Try again later."
          : code === "author_quota" ? "You've reached the submission limit for this repository."
          : code === "issue_too_large" ? "The description is too large - please shorten it or attach smaller images."
          : code === "not_authorized" ? "Only the repository owner or an admin can assign issues to an agent."
          : "Could not send the issue. Please try again.",
        "bad");
    }
  }

  // Branch-referencing web pull requests: no client-side diff computation
  // (deliberately - that duplicates what git already does correctly). The
  // desktop client reconstructs the patch from base/head on drain, same as any
  // other branch-backed pull request (see renderRepoPullPatch's "Branch-backed
  // PRs are reconstructed by the desktop client" copy).
  function openPullCompose(repo) {
    const container = $("[data-repo-pulls]");
    if (!container || !repo) return;
    const branches = repoBranchList(repo);
    const defaultBranch = repoDefaultBranch(repo);
    const branchOptions = branches.map((branch) => `<option value="${escapeHtml(branch.name)}">${escapeHtml(branch.name)}</option>`).join("");
    const headDefault = branches.find((branch) => branch.name !== defaultBranch)?.name || defaultBranch;
    container.innerHTML = `
      <form data-repo-pull-new-form class="grid gap-3 border-t border-border bg-background p-4">
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span class="inline-flex items-center gap-2 text-sm font-semibold text-foreground"><i data-lucide="git-pull-request" class="h-4 w-4 text-primary"></i>New pull request</span>
          <button type="button" data-repo-pull-cancel class="inline-flex h-8 items-center gap-2 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary"><i data-lucide="arrow-left" class="h-3.5 w-3.5"></i>Back to pull requests</button>
        </div>
        <div class="grid gap-2 sm:grid-cols-2">
          <label class="grid gap-1 text-xs font-medium text-muted-foreground">Base
            <select data-repo-pull-base class="h-9 rounded-md border border-border bg-background px-3 text-sm text-foreground outline-none focus:border-primary">${branchOptions}</select>
          </label>
          <label class="grid gap-1 text-xs font-medium text-muted-foreground">Head
            <select data-repo-pull-head class="h-9 rounded-md border border-border bg-background px-3 text-sm text-foreground outline-none focus:border-primary">${branchOptions}</select>
          </label>
        </div>
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Title
          <input data-repo-pull-title type="text" required maxlength="240" placeholder="Short, descriptive title" class="h-9 rounded-md border border-border bg-background px-3 text-sm text-foreground outline-none focus:border-primary" />
        </label>
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Description
          <textarea data-repo-pull-body rows="6" placeholder="Describe the change. Markdown is supported." class="rounded-md border border-border bg-background px-3 py-2 text-sm text-foreground outline-none focus:border-primary"></textarea>
        </label>
        <div class="rounded-md border border-dashed border-border bg-secondary/20 px-3 py-2 text-[11px] text-muted-foreground">The diff isn't computed here - the maintainer's desktop client reconstructs it from the base and head branches when it drains this submission.</div>
        <div class="flex flex-wrap items-center justify-between gap-3">
          <div class="flex min-w-0 flex-col gap-1">
            ${composeIdentityHtml(state.session, "Filing")}
            <span data-repo-pull-hint class="text-[11px] text-muted-foreground">Sent to the maintainer's inbox for review.</span>
          </div>
          <button type="submit" data-repo-pull-submit class="inline-flex h-9 items-center gap-2 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="send" class="h-4 w-4"></i>Create pull request</button>
        </div>
      </form>`;
    window.lucide?.createIcons();
    const baseSelect = container.querySelector("[data-repo-pull-base]");
    const headSelect = container.querySelector("[data-repo-pull-head]");
    if (baseSelect) baseSelect.value = defaultBranch;
    if (headSelect) headSelect.value = headDefault;
    container.querySelector("[data-repo-pull-title]")?.focus();
  }

  async function handlePullComposeSubmit(repo, form) {
    if (!repo || !form) return;
    const titleInput = form.querySelector("[data-repo-pull-title]");
    const bodyInput = form.querySelector("[data-repo-pull-body]");
    const baseSelect = form.querySelector("[data-repo-pull-base]");
    const headSelect = form.querySelector("[data-repo-pull-head]");
    const submit = form.querySelector("[data-repo-pull-submit]");
    const hint = form.querySelector("[data-repo-pull-hint]");
    const setHint = (text, tone) => {
      if (hint) hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      if (hint) hint.textContent = text;
    };
    const title = String(titleInput?.value || "").trim();
    const base = String(baseSelect?.value || "").trim();
    const head = String(headSelect?.value || "").trim();
    if (!title) {
      setHint("Enter a title for the pull request.", "bad");
      titleInput?.focus();
      return;
    }
    if (!base || !head) {
      setHint("Choose a base and head branch.", "bad");
      return;
    }
    if (base === head) {
      setHint("Base and head must be different branches.", "bad");
      return;
    }
    const body = String(bodyInput?.value || "");
    if (submit) submit.disabled = true;
    setHint("Signing and sending…");
    try {
      await submitWebPullOpen(repo, title, body, base, head);
      if (titleInput) titleInput.value = "";
      if (bodyInput) bodyInput.value = "";
      if (submit) submit.disabled = false;
      setHint("Pull request sent to the maintainer's inbox for review.", "good");
    } catch (error) {
      if (submit) submit.disabled = false;
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "The maintainer's inbox is full. Try again later."
          : code === "author_quota" ? "You've reached the submission limit for this repository."
          : code === "pull_too_large" ? "The submission is too large."
          : code === "bad_signature" ? "Could not verify the pull request's signature."
          : "Could not send the pull request. Please try again.",
        "bad");
    }
  }

  async function loadRepoCommits(repo) {
    const container = $("[data-repo-commits]");
    if (!container) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading commits from the live mirror...")}</div>`;
    try {
      const data = await fetchRepoJson(repoLiveUrl(repo, "history"));
      const commits = Array.isArray(data.commits) ? data.commits : [];
      if (!commits.length) {
        container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">No commits are available from this live mirror yet.</div>';
        return;
      }
      container.innerHTML = commits.slice(0, 60).map((commit) => {
        const hash = String(commit.hash || "");
        const shortHash = hash.slice(0, 7) || "unknown";
        return `
        <div data-dashboard-commit-row class="grid w-full grid-cols-[minmax(0,1fr)_auto] items-stretch gap-3 border-t border-border hover:bg-secondary/30 transition-colors">
          <button type="button" data-dashboard-commit-hash="${escapeHtml(hash)}" class="min-w-0 px-4 py-3 text-left">
            <span class="block truncate text-sm font-medium text-foreground">${escapeHtml(commit.subject || "Commit")}</span>
            <span class="mt-1 block truncate text-xs text-muted-foreground">${escapeHtml(commit.author || "unknown")} committed ${escapeHtml(commit.date || "")}</span>
          </button>
          <div class="flex items-center justify-end gap-2 px-4 py-3">
            <button type="button" data-dashboard-commit-hash="${escapeHtml(hash)}" data-dashboard-commit-short-hash class="font-mono text-xs font-semibold text-foreground hover:underline">${escapeHtml(shortHash)}</button>
            <button type="button" data-dashboard-copy="${escapeHtml(hash)}" data-dashboard-copy-kind="commit-hash" aria-label="Copy commit hash ${escapeHtml(shortHash)}" class="copy-button inline-flex h-7 w-7 items-center justify-center rounded-md border border-border bg-background p-0 text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors">
              <i data-lucide="copy" class="copy-icon h-3.5 w-3.5"></i>
              <i data-lucide="check" class="copy-check h-3.5 w-3.5 text-foreground"></i>
            </button>
          </div>
        </div>`;
      }).join("");
    } catch (_) {
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Commit history is unavailable until a live desktop host serves this repository.</div>';
    } finally {
      window.lucide?.createIcons();
    }
  }

  function renderRepoCommitFiles(files) {
    const rows = Array.isArray(files) ? files : [];
    if (!rows.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No file summary is available for this commit.</div>';
    return rows.slice(0, 100).map((file) => `
      <div class="grid grid-cols-[minmax(0,1fr)_auto_auto] gap-3 border-t border-border px-4 py-2 text-xs">
        <span class="min-w-0 truncate font-mono text-foreground">${escapeHtml(file.path || "file")}</span>
        <span class="font-mono text-primary">+${escapeHtml(file.adds ?? "0")}</span>
        <span class="font-mono text-destructive">-${escapeHtml(file.dels ?? "0")}</span>
      </div>`).join("");
  }

  function renderRepoCommitDiff(diff, imageDiffs, key = "") {
    if (!String(diff || "").trim()) return '<div class="px-4 py-3 text-sm text-muted-foreground">No textual diff is available for this commit.</div>';
    if (!shouldRenderLongDiff(diff, key)) return renderLongDiffNotice("commit diff", key, diff);
    return `<div data-repo-commit-diff>${renderDiffFiles(parseDiffFiles(diff), imageDiffs)}</div>`;
  }

  function renderRepoCommitDetail(repo, data) {
    const commit = data.commit || {};
    const hash = String(commit.hash || "");
    const parents = String(commit.parents || "").split(/\s+/).filter(Boolean);
    return `
      <article data-repo-commit-detail class="grid gap-4 border-t border-border bg-background p-4">
        <div class="flex flex-wrap items-center justify-between gap-3">
          <button type="button" data-repo-commit-back class="inline-flex h-8 items-center gap-2 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary"><i data-lucide="arrow-left" class="h-3.5 w-3.5"></i>Back to commits</button>
          <span class="font-mono text-xs text-muted-foreground">${escapeHtml(hash)}</span>
        </div>
        <header class="rounded-lg border border-border bg-secondary/30 p-4">
          <h3 class="text-base font-semibold text-foreground">${escapeHtml(commit.subject || "Commit")}</h3>
          ${commit.body ? `<p class="mt-3 whitespace-pre-wrap text-sm leading-6 text-muted-foreground">${escapeHtml(commit.body)}</p>` : ""}
          <dl class="mt-4 grid gap-2 text-xs text-muted-foreground sm:grid-cols-2">
            <div><dt class="font-semibold text-foreground">Author</dt><dd>${escapeHtml(commit.author || "unknown")}</dd></div>
            <div><dt class="font-semibold text-foreground">Date</dt><dd>${escapeHtml(commit.date || "unknown")}</dd></div>
            <div><dt class="font-semibold text-foreground">Parents</dt><dd class="font-mono">${parents.length ? parents.map((parent) => escapeHtml(parent.slice(0, 12))).join(", ") : "root commit"}</dd></div>
            <div><dt class="font-semibold text-foreground">Repository</dt><dd class="font-mono">${escapeHtml(repo.owner || "owner")}/${escapeHtml(repo.name || "repo")}</dd></div>
          </dl>
        </header>
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="files" class="h-3.5 w-3.5 text-primary"></i>Files changed</span>
            <span class="font-mono text-[10px] text-muted-foreground">${formatCount(Array.isArray(data.files) ? data.files.length : 0)} files</span>
          </div>
          <div data-repo-commit-files>${renderRepoCommitFiles(data.files)}</div>
        </section>
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="git-compare-arrows" class="h-3.5 w-3.5 text-primary"></i>Diff</div>
          ${renderRepoCommitDiff(data.diff, data.imageDiffs, `commit:${repoKey(repo)}:${hash}`)}
        </section>
      </article>`;
  }

  async function loadRepoCommitDetail(repo, hash) {
    const container = $("[data-repo-commits]");
    if (!container || !repo || !hash) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading commit from the live mirror...")}</div>`;
    try {
      const data = await fetchRepoJson(repoLiveUrl(repo, "commit", { path: hash }));
      state.repoCommitDetail = { repo, data };
      container.innerHTML = renderRepoCommitDetail(repo, data);
    } catch (_) {
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Commit detail is unavailable until a live desktop host serves this commit.</div>';
    } finally {
      window.lucide?.createIcons();
    }
  }

  // A mirror row is ringed green when it's the node that answered the most
  // recent live-mirror fetch (data.servedBy from the tunnel round-robin), with
  // its response time alongside so it's obvious which mirror served the page
  // and how fast.
  function mirrorRowIsServing(mirror, servedBy) {
    const name = String(mirror.owner || mirror.node || mirror.name || "").trim().toLowerCase();
    const servedName = String(servedBy?.name || "").trim().toLowerCase();
    return Boolean(name && servedName && name === servedName);
  }

  function renderMirrorRow(mirror, servedBy) {
    const online = mirror.status === "online";
    const isServing = online && mirrorRowIsServing(mirror, servedBy);
    const rowClass = isServing
      ? "grid grid-cols-[1.25rem_minmax(0,1fr)_auto] items-center gap-3 border-t border-border px-4 py-3 text-sm ring-1 ring-inset ring-primary bg-primary/5"
      : "grid grid-cols-[1.25rem_minmax(0,1fr)_auto] items-center gap-3 border-t border-border px-4 py-3 text-sm hover:bg-secondary/40 transition-colors";
    const speed = isServing ? formatServeSpeed(servedBy.tookMs) : "";
    const rawVersion = String(mirror.version || mirror.appVersion || mirror.clientVersion || "").trim();
    const version = rawVersion
      ? (rawVersion[0].toLowerCase() === "v" ? rawVersion : `v${rawVersion}`)
      : "";
    return `
        <div class="${rowClass}">
          <i data-lucide="${online ? "radio" : "circle"}" class="mt-0.5 h-4 w-4 ${online ? "text-primary" : "text-muted-foreground"}"></i>
          <span class="min-w-0">
            <span class="block min-w-0 truncate text-foreground font-mono">${escapeHtml(mirror.owner || mirror.node || mirror.name || "mirror")}</span>
            ${version ? `<span class="mt-0.5 block min-w-0 truncate text-[10px] text-muted-foreground font-mono">${escapeHtml(version)}</span>` : ""}
          </span>
          <span class="flex shrink-0 items-center gap-2 text-xs font-mono ${online ? "text-primary" : "text-muted-foreground"}">
            ${speed ? `<span class="rounded-full border border-primary/40 bg-primary/10 px-1.5 py-0.5 text-[10px] text-primary">${escapeHtml(speed)}</span>` : ""}
            ${escapeHtml(mirror.status || "unknown")}
          </span>
        </div>`;
  }

  // The "Live mirror" summary in the About aside gets its own compact list of
  // every mirror currently online for this repo (the full tab-level list
  // lives under the Mirrors tab and includes offline ones too).
  function renderRepoLiveMirrorList(mirrors, servedBy) {
    const container = $("[data-repo-live-mirror-list]");
    if (!container) return;
    const online = mirrors.filter((mirror) => mirror.status === "online");
    container.innerHTML = online.length
      ? online.map((mirror) => renderMirrorRow(mirror, servedBy)).join("")
      : '<div class="border-t border-border px-3 py-2 text-xs text-muted-foreground">No mirrors online right now.</div>';
  }

  function renderRepoMirrorLists(mirrors, servedBy) {
    const tabContainer = $("[data-repo-mirrors]");
    if (tabContainer && mirrors.length) {
      tabContainer.innerHTML = mirrors.map((mirror) => renderMirrorRow(mirror, servedBy)).join("");
    }
    renderRepoLiveMirrorList(mirrors, servedBy);
    window.lucide?.createIcons();
  }

  async function loadRepoMirrors(repo) {
    const container = $("[data-repo-mirrors]");
    if (container) container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading mirrors...")}</div>`;
    try {
      const data = await fetchJson(`${repoApiBase(repo)}/mirrors`);
      const mirrors = Array.isArray(data.mirrors) ? data.mirrors : [];
      const mirrorCount = normalizedCount(data.summary?.mirrors) ?? mirrors.length;
      updateRepoLiveCounts(repo, { mirrors: mirrorCount });
      setRepoTabCount("mirrors", mirrors.length);
      state.repoMirrors = mirrors;
      if (state.repoServedBy) {
        renderRepoServedBy(state.repoServedBy.name, state.repoServedBy.tookMs);
      }
      if (!mirrors.length) {
        if (container) container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">No mirrors reported yet.</div>';
        renderRepoLiveMirrorList([], state.repoServedBy);
        return;
      }
      renderRepoMirrorLists(mirrors, state.repoServedBy);
    } catch (_) {
      if (container) container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Mirror health is unavailable right now.</div>';
      renderRepoLiveMirrorList([], state.repoServedBy);
    } finally {
      window.lucide?.createIcons();
    }
  }

  function renderRepoRelease(repo, release, downloads) {
    const tag = String(release.tag || "untagged");
    const channel = String(release.channel || "");
    const created = release.created_at ? formatDate(release.created_at) : "";
    const commit = String(release.tag_commit || "").slice(0, 7);
    const notes = String(release.body || release.name || "");
    const assets = Array.isArray(release.assets) ? release.assets : [];
    const isLatest = channel === "latest";
    const assetRows = assets.length
      ? assets.map((asset) => {
          const name = String(asset.name || "asset");
          const sha = String(asset.blob_sha256 || "").toLowerCase();
          const count = Number(downloads[sha]);
          const platform = [asset.os, asset.arch].filter(Boolean).join("/");
          const href = sha ? `${repoApiBase(repo)}/releases/blob/sha256/${encodeURIComponent(sha)}` : "";
          return `
            <div class="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-3 border-t border-border px-4 py-2.5 text-sm">
              <div class="flex min-w-0 items-center gap-2">
                <i data-lucide="package" class="h-3.5 w-3.5 shrink-0 text-muted-foreground"></i>
                ${href
                  ? `<a href="${escapeHtml(href)}" download="${escapeHtml(name)}" class="dashboard-accent-link min-w-0 truncate font-mono text-foreground hover:underline">${escapeHtml(name)}</a>`
                  : `<span class="min-w-0 truncate font-mono text-foreground">${escapeHtml(name)}</span>`}
                ${platform ? `<span class="shrink-0 rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${escapeHtml(platform)}</span>` : ""}
              </div>
              <div class="flex shrink-0 items-center gap-3 text-xs text-muted-foreground">
                ${Number.isFinite(count) ? `<span class="inline-flex items-center gap-1"><i data-lucide="download" class="h-3 w-3"></i>${formatCount(count)}</span>` : ""}
                <span class="font-mono">${escapeHtml(formatSize(asset.size))}</span>
              </div>
            </div>`;
        }).join("")
      : '<div class="border-t border-border px-4 py-3 text-xs text-muted-foreground">No downloadable assets are attached to this release.</div>';
    return `
      <article class="mt-4 overflow-hidden rounded-lg border border-border bg-background first:mt-0">
        <div class="flex flex-wrap items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
          <div class="flex min-w-0 flex-wrap items-center gap-2">
            <i data-lucide="tag" class="h-3.5 w-3.5 text-primary"></i>
            <span class="font-mono text-sm font-semibold text-foreground">${escapeHtml(tag)}</span>
            ${isLatest
              ? '<span class="rounded-full border border-primary/40 bg-primary/10 px-2 py-0.5 text-[10px] font-medium text-primary">Latest</span>'
              : channel ? `<span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${escapeHtml(channel)}</span>` : ""}
          </div>
          <div class="flex shrink-0 flex-wrap items-center gap-3 text-[11px] text-muted-foreground">
            ${commit ? `<span class="inline-flex items-center gap-1 font-mono"><i data-lucide="git-commit-horizontal" class="h-3 w-3"></i>${escapeHtml(commit)}</span>` : ""}
            ${created ? `<span class="inline-flex items-center gap-1"><i data-lucide="calendar" class="h-3 w-3"></i>${escapeHtml(created)}</span>` : ""}
          </div>
        </div>
        ${notes ? `<p class="whitespace-pre-wrap border-b border-border px-4 py-3 text-sm leading-6 text-muted-foreground">${escapeHtml(notes)}</p>` : ""}
        <div>${assetRows}</div>
      </article>`;
  }

  async function loadRepoReleases(repo) {
    const container = $("[data-repo-releases]");
    if (!container) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading releases from the live mirror...")}</div>`;
    const empty = '<div class="px-4 py-3 text-sm text-muted-foreground">No releases have been published to this mirror yet.</div>';
    try {
      // Release manifests live in the git tree at .forkmesh/releases/<channel>/release.json
      // (issue #304). List the channels, then batch-read every manifest in one
      // tunnel round-trip so opening the tab doesn't fan out N blob requests.
      let tree;
      try {
        tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: "releases" }));
      } catch (error) {
        if (isMissingMirrorFolder(error)) {
          container.innerHTML = empty;
          return;
        }
        throw error;
      }
      const channels = (Array.isArray(tree.entries) ? tree.entries : [])
        .filter((entry) => entry.type === "tree" && entry.name)
        .map((entry) => String(entry.name));
      if (!channels.length) {
        container.innerHTML = empty;
        return;
      }
      const paths = channels.map((channel) => `.forkmesh/releases/${channel}/release.json`);
      const blobs = await fetchRepoBlobs(repo, paths);
      const releases = [];
      channels.forEach((channel, index) => {
        const blob = blobs[paths[index]];
        if (!blob) return;
        let manifest;
        try {
          manifest = JSON.parse(blobText(blob));
        } catch (_) {
          return;
        }
        if (manifest && typeof manifest === "object") {
          manifest.channel = manifest.channel || channel;
          releases.push(manifest);
        }
      });
      if (!releases.length) {
        container.innerHTML = empty;
        return;
      }
      // Per-asset download counts (keyed by sha256) are a best-effort adornment.
      let downloads = {};
      try {
        const data = await fetchJson(`${repoApiBase(repo)}/releases/downloads`);
        downloads = (data && data.counts) || {};
      } catch (_) {}
      releases.sort((a, b) => (Number(b.created_at) || 0) - (Number(a.created_at) || 0));
      container.innerHTML = releases.map((release) => renderRepoRelease(repo, release, downloads)).join("");
    } catch (_) {
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Releases are unavailable until a live desktop host serves the .forkmesh/releases/ folder.</div>';
    } finally {
      window.lucide?.createIcons();
    }
  }

  function repoInsightsContributorRows(commits) {
    const byAuthor = new Map();
    (Array.isArray(commits) ? commits : []).forEach((commit) => {
      const author = String(commit.author || "unknown").trim() || "unknown";
      const current = byAuthor.get(author) || { author, commits: 0, latest: "" };
      current.commits += 1;
      current.latest = current.latest || commit.date || "";
      byAuthor.set(author, current);
    });
    return [...byAuthor.values()].sort((a, b) => b.commits - a.commits || a.author.localeCompare(b.author));
  }

  function renderRepoInsights(repo, commits = [], unavailable = false) {
    const commitTotal = repoCount(repo, ["commits", "commitCount", "commitHistory"]);
    const contributors = repoInsightsContributorRows(commits);
    const top = contributors.slice(0, 10);
    const rows = top.length
      ? top.map((item) => `
        <div class="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-3 border-t border-border px-4 py-3 text-sm">
          <div class="min-w-0">
            <div class="truncate font-medium text-foreground">${escapeHtml(item.author)}</div>
            <div class="mt-1 text-xs text-muted-foreground">${escapeHtml(item.latest || "recent activity")}</div>
          </div>
          <span class="font-mono text-xs text-foreground">${formatCount(item.commits)}</span>
        </div>`)
      : ['<div class="border-t border-border px-4 py-3 text-sm text-muted-foreground">No contributor activity is available yet.</div>'];
    return `
      <div class="grid gap-4 p-4">
        ${unavailable ? '<div class="rounded-md border border-amber-500/30 bg-amber-500/10 px-3 py-2 text-xs text-amber-200">Live commit history is unavailable; showing catalog activity where available.</div>' : ""}
        <div class="grid gap-3 md:grid-cols-3">
          <div class="rounded-md border border-border bg-secondary/30 p-3">
            <div class="text-[10px] uppercase tracking-wide text-muted-foreground">Commits</div>
            <div class="mt-2 font-mono text-lg text-foreground">${tabCountLabel(commitTotal)}</div>
          </div>
          <div class="rounded-md border border-border bg-secondary/30 p-3">
            <div class="text-[10px] uppercase tracking-wide text-muted-foreground">Contributors</div>
            <div class="mt-2 font-mono text-lg text-foreground">${formatCount(contributors.length)}</div>
          </div>
          <div class="rounded-md border border-border bg-secondary/30 p-3">
            <div class="text-[10px] uppercase tracking-wide text-muted-foreground">Data hosted</div>
            <div class="mt-2 font-mono text-lg text-foreground">${escapeHtml(formatSize(repo.sizeBytes))}</div>
          </div>
        </div>
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground">Activity</div>
          <div class="p-4">${repoActivitySparkline(repo.activityWeeks, { totalHint: commitTotal })}</div>
        </section>
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="text-xs font-medium text-foreground">Contributors</span>
            <span class="font-mono text-[10px] text-muted-foreground">${formatCount(contributors.length)}</span>
          </div>
          ${rows.join("")}
        </section>
      </div>`;
  }

  async function loadRepoInsights(repo) {
    const container = $("[data-repo-insights]");
    if (!container || !repo) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading insights from the live mirror...")}</div>`;
    try {
      const data = await fetchRepoJson(repoLiveUrl(repo, "history"));
      const commits = Array.isArray(data.commits) ? data.commits : [];
      container.innerHTML = renderRepoInsights(repo, commits);
    } catch (_) {
      container.innerHTML = renderRepoInsights(repo, [], true);
    } finally {
      window.lucide?.createIcons();
    }
  }

  function loadRepoFeaturePanels(repo, recordRoute = null) {
    loadRepoMirrors(repo);
    // Feature panels load on their FIRST tab view (and reload here after a
    // repo/branch switch if that tab is already active): fetching hidden live
    // data for every repo open fires Worker/host reads for tabs nobody opened.
    // The tab badges stay filled meanwhile from the root tree's bundled counts.
    state.loadedRepoTabs = {};
    const active = state.activeRepoTab || "code";
    if (active === "commits") {
      state.loadedRepoTabs.commits = true;
      loadRepoCommits(repo);
    } else if (active === "issues") {
      state.loadedRepoTabs.issues = true;
      loadRepoIssues(repo);
    } else if (active === "pulls" || active === "discussions") {
      state.loadedRepoTabs[active] = true;
      // A record deep link (/owner/repo/pulls/<N> — e.g. the desktop client's
      // "View on website" button) opens the record's detail page directly
      // instead of the list; Back to the list loads it lazily from there.
      if (recordRoute && recordRoute.kind === active && recordRoute.number) {
        loadRepoRecordDetail(repo, active, recordRoute.number);
      } else {
        loadRepoCollection(repo, active, `[data-repo-${active}]`);
      }
    } else if (active === "releases") {
      state.loadedRepoTabs.releases = true;
      loadRepoReleases(repo);
    } else if (active === "insights") {
      state.loadedRepoTabs.insights = true;
      loadRepoInsights(repo);
    } else if (active === "agents") {
      state.loadedRepoTabs.agents = true;
      loadRepoAgents(repo);
    }
  }

  function updateRepoLiveCounts(repo, counts) {
    if (!counts || typeof counts !== "object") return;
    (["commits", "issues", "pulls", "discussions", "mirrors"]).forEach((key) => {
      const value = Number(counts[key]);
      if (!Number.isFinite(value) || value < 0) return;
      repo[key] = value;
      $$(`[data-dashboard-repo-count="${key}"]`).forEach((badge) => {
        badge.textContent = formatCount(value);
      });
    });
  }

  function repoCount(repo, keys) {
    for (const key of keys) {
      const value = Number(repo[key]);
      if (Number.isFinite(value) && value >= 0) return value;
    }
    return null;
  }

  function repoDefaultBranch(repo) {
    return repo.defaultBranch || repo.branch || repo.rootBranch || "main";
  }

  function normalizeRepoBranch(branch) {
    if (!branch || typeof branch !== "object") {
      const name = String(branch || "").trim();
      return name ? { name } : null;
    }
    const name = String(branch.name || branch.branch || branch.ref || "").replace(/^refs\/heads\//, "").trim();
    if (!name) return null;
    return {
      name,
      commit: branch.commit || branch.hash || branch.sha || "",
      updatedAt: branch.updatedAt || branch.date || branch.committedAt || "",
    };
  }

  function repoSelectedBranch(repo) {
    const key = repoKey(repo);
    return state.selectedBranches[key] || repoDefaultBranch(repo);
  }

  function repoBranchList(repo) {
    const loaded = state.repoBranches[repoKey(repo)]?.branches;
    const source = Array.isArray(loaded) && loaded.length
      ? loaded
      : Array.isArray(repo.branches)
        ? repo.branches
        : [repoDefaultBranch(repo)];
    const seen = new Set();
    const branches = [];
    source.forEach((branch) => {
      const normalized = normalizeRepoBranch(branch);
      if (!normalized || seen.has(normalized.name)) return;
      seen.add(normalized.name);
      branches.push(normalized);
    });
    const current = repoSelectedBranch(repo);
    if (current && !seen.has(current)) branches.unshift({ name: current });
    return branches;
  }

  function repoBranchCount(repo) {
    return repoBranchList(repo).length;
  }

  function repoBranchQuery(repo) {
    return state.repoBranchQueries[repoKey(repo)] || "";
  }

  function setRepoBranchQuery(repo, query) {
    if (!repo) return;
    state.repoBranchQueries[repoKey(repo)] = String(query || "");
  }

  function filterRepoBranches(repo, branches) {
    const query = repoBranchQuery(repo).trim().toLowerCase();
    if (!query) return branches;
    return branches.filter((branch) => String(branch.name || "").toLowerCase().includes(query));
  }

  function setRepoSelectedBranch(repo, branch) {
    const name = String(branch || "").trim();
    if (!repo || !name) return;
    state.selectedBranches[repoKey(repo)] = name;
  }

  function renderRepoBranchButton(branch) {
    return `<button type="button" data-repo-branch-button aria-haspopup="menu" aria-expanded="false" class="inline-flex h-9 w-full max-w-full items-center justify-center gap-2 rounded-md border border-border bg-secondary px-3 text-xs font-medium text-foreground hover:bg-secondary/80 transition-colors"><i data-lucide="git-branch" class="h-3.5 w-3.5 shrink-0"></i><span data-repo-branch-current class="min-w-0 truncate">${escapeHtml(branch)}</span><i data-lucide="chevron-down" class="h-3 w-3 shrink-0 text-muted-foreground"></i></button>`;
  }

  function renderRepoBranchSummary(repo) {
    const count = repoBranchCount(repo);
    return `<span data-repo-branch-summary class="inline-flex h-9 items-center gap-2 whitespace-nowrap text-xs font-semibold text-muted-foreground"><i data-lucide="git-branch" class="h-3.5 w-3.5"></i><span data-repo-branch-count class="text-foreground">${formatCount(count)}</span><span>${count === 1 ? "Branch" : "Branches"}</span></span>`;
  }

  function renderRepoBranchToolbar(repo, branch) {
    return `<div data-repo-branch-toolbar class="flex min-w-0 flex-nowrap items-center gap-3"><div data-repo-branch-control class="relative inline-flex min-w-0 max-w-64 shrink">${renderRepoBranchButton(branch)}${renderRepoBranchMenu(repo, repoBranchList(repo), false)}</div>${renderRepoBranchSummary(repo)}</div>`;
  }

  function renderRepoBranchMenu(repo, branches, open) {
    const current = repoSelectedBranch(repo);
    const query = repoBranchQuery(repo);
    const defaultBranch = repoDefaultBranch(repo);
    const filtered = new Set(filterRepoBranches(repo, branches).map((branch) => branch.name || ""));
    const rows = branches.map((branch) => {
      const name = branch.name || "main";
      if (!filtered.has(name)) return "";
      const checked = name === current;
      const commit = String(branch.commit || "").slice(0, 7);
      const updated = branch.updatedAt ? formatDate(branch.updatedAt) : "";
      return `
        <button type="button" role="menuitemradio" aria-checked="${checked ? "true" : "false"}" data-repo-branch-name="${escapeHtml(name)}" class="grid w-full grid-cols-[1.25rem_minmax(0,1fr)_auto] items-center gap-2 px-4 py-2.5 text-left text-sm transition-colors ${checked ? "text-foreground" : "text-muted-foreground hover:bg-secondary/70 hover:text-foreground"}">
          <i data-lucide="${checked ? "check" : "git-branch"}" class="h-4 w-4 shrink-0 ${checked ? "text-foreground" : "text-muted-foreground"}"></i>
          <span class="min-w-0 truncate font-semibold">${escapeHtml(name)}</span>
          <span class="flex min-w-0 items-center gap-2">
            ${name === defaultBranch ? '<span data-repo-branch-default class="rounded-full border border-border px-2 py-0.5 text-[10px] font-semibold text-foreground">default</span>' : ""}
            ${commit || updated ? `<span class="hidden max-w-32 truncate font-mono text-[10px] text-muted-foreground sm:inline">${escapeHtml([commit, updated].filter(Boolean).join(" · "))}</span>` : ""}
          </span>
        </button>`;
    }).join("");
    return `
      <div data-repo-branch-menu role="menu" class="${open ? "" : "hidden"} absolute left-0 top-full z-30 mt-2 w-[30rem] max-w-[calc(100vw-2rem)] overflow-hidden rounded-xl border border-border bg-background shadow-2xl">
        <div class="flex items-center justify-between gap-3 px-4 py-3">
          <h3 class="text-sm font-semibold text-foreground">Switch branches</h3>
          <button type="button" data-repo-branch-close aria-label="Close branch picker" class="inline-flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors"><i data-lucide="x" class="h-4 w-4"></i></button>
        </div>
        <div class="px-3 pb-3">
          <label class="flex h-10 items-center gap-2 rounded-md border border-border bg-card px-3 text-sm text-muted-foreground focus-within:ring-1 focus-within:ring-foreground/20">
            <i data-lucide="search" class="h-4 w-4 shrink-0"></i>
            <input data-repo-branch-search type="search" autocomplete="off" spellcheck="false" value="${escapeHtml(query)}" placeholder="Find a branch..." class="min-w-0 flex-1 bg-transparent text-foreground outline-none placeholder:text-muted-foreground" />
          </label>
        </div>
        <div class="flex border-y border-border">
          <span class="inline-flex h-12 items-center border-r border-border bg-secondary px-4 text-sm font-semibold text-foreground">Branches</span>
        </div>
        <div class="max-h-72 overflow-auto py-4">
          ${rows || '<div class="px-4 py-3 text-sm text-muted-foreground">No branches match this search.</div>'}
        </div>
        <button type="button" data-repo-branch-view-all class="flex h-12 w-full items-center border-t border-border px-4 text-left text-sm font-semibold text-foreground hover:bg-secondary/60 transition-colors">View all branches</button>
      </div>`;
  }

  function closeRepoBranchMenus() {
    $$("[data-repo-branch-menu]").forEach((menu) => menu.classList.add("hidden"));
    $$("[data-repo-branch-button]").forEach((button) => {
      button.setAttribute("aria-expanded", "false");
    });
  }

  function updateRepoBranchControls(repo, openControl = null) {
    const restoreSearchFocus = openControl && document.activeElement?.matches?.("[data-repo-branch-search]");
    $$("[data-repo-branch-control]").forEach((control) => {
      const open = openControl === control;
      const branch = repoSelectedBranch(repo);
      control.innerHTML = `${renderRepoBranchButton(branch)}${renderRepoBranchMenu(repo, repoBranchList(repo), open)}`;
      control.querySelector("[data-repo-branch-button]")?.setAttribute("aria-expanded", open ? "true" : "false");
    });
    $$("[data-repo-branch-summary]").forEach((summary) => {
      summary.outerHTML = renderRepoBranchSummary(repo);
    });
    window.lucide?.createIcons();
    if (restoreSearchFocus) {
      const input = openControl.querySelector("[data-repo-branch-search]");
      input?.focus();
      input?.setSelectionRange?.(input.value.length, input.value.length);
    }
  }

  async function toggleRepoBranchMenu(repo, button) {
    if (!repo || !button) return;
    const control = button.closest("[data-repo-branch-control]");
    const wasOpen = control?.querySelector("[data-repo-branch-menu]")?.classList.contains("hidden") === false;
    closeRepoBranchMenus();
    if (!control || wasOpen) return;
    button.setAttribute("aria-expanded", "true");
    control.querySelector("[data-repo-branch-menu]")?.classList.remove("hidden");

    const key = repoKey(repo);
    if (state.repoBranches[key]?.loaded) return;
    try {
      const data = await fetchRepoJson(`${repoApiBase(repo)}/branches`);
      const branches = (Array.isArray(data.branches) ? data.branches : [])
        .map(normalizeRepoBranch)
        .filter(Boolean);
      state.repoBranches[key] = { branches, loaded: true };
    } catch (_) {
      state.repoBranches[key] = { branches: repoBranchList(repo), loaded: false };
    }
    updateRepoBranchControls(repo, control);
  }

  function renderRepoCollectionFilter(label, options) {
    return `
      <label class="relative inline-flex h-9 shrink-0 items-center gap-1.5 rounded-md border border-border bg-background px-3 text-xs font-medium text-muted-foreground hover:bg-secondary hover:text-foreground transition-colors">
        <span>${label}</span>
        <i data-lucide="chevron-down" class="h-3 w-3"></i>
        <select aria-label="${label}" class="absolute inset-0 cursor-pointer opacity-0">
          ${options.map((option) => `<option>${escapeHtml(option)}</option>`).join("")}
        </select>
      </label>`;
  }

  function renderRepoCollectionPanel(kind, repo, openCount, closedCount) {
    const isPulls = kind === "pulls";
    const config = repoCollectionConfig[kind];
    const icon = isPulls ? "git-pull-request" : "circle-dot";
    const title = config?.label || (isPulls ? "Pull requests" : "Issues");
    const filters = [
      ["Author", ["Any author", repo.owner || "owner", repo.maintainer || "maintainer"]],
      ["Labels", ["Any label", "bug", "enhancement", "documentation"]],
      ["Projects", ["Any project", "No project"]],
      ["Milestones", ["Any milestone", "No milestone"]],
      ...(isPulls ? [["Reviews", ["Any review", "Review required", "Approved", "Changes requested"]]] : []),
      [isPulls ? "Assignee" : "Assignees", ["Anyone", "Assigned to me", "Unassigned"]],
      ["Sort", ["Newest", "Oldest", "Recently updated", "Most commented"]],
    ];

    return `
      <section data-dashboard-repo-tab-panel="${kind}" class="hidden">
        <div class="mt-4">
          <div class="min-w-0">
            ${isPulls ? `
              <div class="mb-4 rounded-lg border border-border bg-background px-4 py-5 text-center">
                <p class="text-sm font-semibold text-foreground">First time contributing to ${escapeHtml(repo.owner || "owner")}/${escapeHtml(repo.name || "repository")}?</p>
                <p class="mx-auto mt-2 max-w-xl text-xs leading-5 text-muted-foreground">Review this repository's <a href="${escapeHtml(repoPathUrl(repo, "blob", "CONTRIBUTING.md"))}" class="font-medium text-accent hover:underline">contribution notes</a> before opening a pull request.</p>
              </div>` : ""}
            <div data-repo-collection-toolbar="${kind}" class="mb-3 grid gap-2 lg:grid-cols-[auto_minmax(0,1fr)_auto]">
              <label class="inline-flex h-9 min-w-0 items-center overflow-hidden rounded-md border border-border bg-background text-xs lg:col-span-2">
                <span class="relative inline-flex h-full items-center gap-1.5 border-r border-border bg-secondary px-3 font-medium text-foreground">
                  <span>Filters</span>
                  <i data-lucide="chevron-down" class="h-3 w-3 text-muted-foreground"></i>
                  <select data-repo-filter-menu="${kind}" aria-label="${title} filters" class="absolute inset-0 cursor-pointer opacity-0">
                    <option>Open ${title.toLowerCase()}</option>
                    <option>Your ${title.toLowerCase()}</option>
                    <option>Everything assigned</option>
                    <option>Recently updated</option>
                  </select>
                </span>
                <span class="inline-flex min-w-0 flex-1 items-center gap-2 px-3">
                  <i data-lucide="search" class="h-3.5 w-3.5 shrink-0 text-muted-foreground"></i>
                  <input data-repo-filter-query="${kind}" type="search" spellcheck="false" value="${isPulls ? "is:pr is:open" : escapeHtml(state.issuesView.query || "")}" placeholder="${kind === "pulls" ? "is:pr is:open" : "Search issues by title, body, author, or #number"}" class="min-w-0 flex-1 bg-transparent font-mono text-xs text-foreground outline-none placeholder:text-muted-foreground" />
                </span>
              </label>
              ${isPulls
                ? `<button type="button" data-repo-pull-new class="inline-flex h-9 items-center justify-center gap-2 rounded-md bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90"><i data-lucide="git-pull-request" class="h-3.5 w-3.5"></i>New pull request</button>`
                : `<button type="button" data-repo-issue-new class="inline-flex h-9 items-center justify-center gap-2 rounded-md bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90"><i data-lucide="plus" class="h-3.5 w-3.5"></i>New issue</button>`}
              <div class="flex min-w-0 flex-wrap items-center gap-2 lg:col-span-3">
                ${filters.map(([label, options]) => renderRepoCollectionFilter(label, options)).join("")}
              </div>
            </div>
            <div class="overflow-hidden rounded-lg border border-border bg-background">
              <div class="flex min-w-0 flex-wrap items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
                <div class="flex min-w-0 flex-wrap items-center gap-3 text-xs">
                  <span class="inline-flex items-center gap-2 font-semibold text-foreground"><i data-lucide="${icon}" class="h-3.5 w-3.5 text-primary"></i><span data-repo-collection-open-count="${kind}">${tabCountLabel(openCount)}</span> Open</span>
                  <span class="inline-flex items-center gap-2 text-muted-foreground"><i data-lucide="check" class="h-3.5 w-3.5"></i><span data-repo-collection-closed-count="${kind}">${tabCountLabel(closedCount)}</span> Closed</span>
                </div>
                <span class="text-[10px] text-muted-foreground">${isPulls ? "Review and merge signed patches" : "Track signed issues from the live mirror"}</span>
              </div>
              <div data-repo-${kind}></div>
            </div>
          </div>
        </div>
      </section>`;
  }
  function renderRepoDetail(repo) {
    const detail = $("[data-repo-detail]");
    if (!detail || !repo) return;
    state.selectedRepo = repo;
    state.repoCollectionPages = { issues: 1, pulls: 1 };
    state.repoMirrors = [];
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
    // the number in the address bar and open that PR's detail page below.
    const recordRoute = ["pulls", "discussions"].includes(routeKind) && /^\d+$/.test(routePath)
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
    const commitId = String(repo.rootCommit || repo.latestCommit || repo.commit || "").slice(0, 7) || "live";
    const updatedAt = formatTimeAgo(repo.updatedAt || repo.lastSync);
    const live = repoIsLive(repo);
    const viaMirror = repoServedByMirror(repo);
    const canEditAbout = sessionOwnsRepo(repo);
    const readmePath = "README.md";
    const readmeHref = repoPathUrl(repo, "blob", readmePath);
    const tabMeta = {
      code: { label: "Code", icon: "code-2", count: "" },
      commits: { label: "Commits", icon: "git-commit-horizontal", count: commitsCount },
      insights: { label: "Insights", icon: "chart-no-axes-combined", count: "" },
      releases: { label: "Releases", icon: "tag", count: "" },
      issues: { label: "Issues", icon: "circle-dot", count: issuesCount },
      projects: { label: "Projects", icon: "chart-gantt", count: "" },
      pulls: { label: "Pull requests", icon: "git-pull-request", count: pullsCount },
      discussions: { label: "Discussions", icon: "message-square", count: discussionsCount },
      mirrors: { label: "Mirrors", icon: "radio", count: mirrorsCount },
      agents: { label: "Agents", icon: "bot", count: "" },
    };
    const canSeeAgentsTab = sessionCanAssignAgent(repo);
    const actionSeed = repoKey(repo);
    const forkCount = stableMockNumber(`${actionSeed}:fork`, 0, 12);
    // Watch is real: it is the repo's fediverse follower count (see
    // loadRepoFediverse), and the button opens the follow-from-Mastodon card.
    const fediHandle = `@${(repo.owner || "").toLowerCase()}.${(repo.name || "").toLowerCase()}@${location.host}`;
    // Deep link that opens this repo's fediverse actor on Mastodon (any
    // instance resolves a remote acct handle; mastodon.social is the default).
    const mastodonUrl = `https://mastodon.social/${fediHandle}`;
    detail.innerHTML = `
      <div data-repo-layout="github-like" class="min-w-0">
        <div data-repo-github-header class="rounded-t-lg border border-border bg-background">
          <div class="grid gap-4 border-b border-border p-4 lg:grid-cols-[minmax(0,1fr)_auto]">
            <div class="min-w-0">
              <div class="flex min-w-0 flex-wrap items-center gap-2">
                <i data-lucide="book-marked" class="h-4 w-4 text-muted-foreground"></i>
                <h2 class="min-w-0 truncate text-lg font-semibold text-foreground"><span class="text-muted-foreground"><a href="/@${encodeURIComponent(String(repo.owner || "").toLowerCase())}" data-repo-owner-link class="hover:text-foreground hover:underline">${escapeHtml(repo.owner || "owner")}</a>/</span>${escapeHtml(repo.name || "repository")}</h2>
                <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${repo.isPrivate ? "private" : "public"}</span>
                <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono ${live ? "text-primary" : "text-muted-foreground"}">${viaMirror ? "served by mirror" : live ? "host online" : "host offline"}</span>
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
              <span class="inline-flex h-8 items-center gap-1.5 rounded-md border border-border bg-secondary px-3 text-xs text-muted-foreground"><i data-lucide="radio" class="h-3.5 w-3.5"></i>Mirrors <span data-dashboard-repo-count="mirrors" class="font-mono text-foreground">${tabCountLabel(mirrorsCount)}</span></span>
            </div>
          </div>
          <div class="flex min-w-0 overflow-x-auto px-3" role="tablist">
            ${["code", "commits", "insights", "releases", "issues", "projects", "pulls", "discussions", "mirrors", ...(canSeeAgentsTab ? ["agents"] : [])].map((tab) => {
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
	                        <span data-repo-commit-author class="min-w-0 truncate text-foreground font-medium">${escapeHtml(repo.maintainer || repo.owner || "maintainer")}</span>
	                        <span data-repo-commit-message class="min-w-0 truncate text-muted-foreground">published latest mirror metadata</span>
	                      </div>
	                      <span data-repo-commit-hash class="font-mono text-muted-foreground">${escapeHtml(commitId)}</span>
	                      <span data-repo-commit-date class="font-mono text-muted-foreground">${escapeHtml(updatedAt)}</span>
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
            <section data-dashboard-repo-tab-panel="mirrors" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="radio" class="h-3.5 w-3.5 text-primary"></i>Mirrors</span><span class="font-mono text-[10px] text-muted-foreground">live host health</span></div><div data-repo-mirrors></div></div></section>
            ${canSeeAgentsTab ? `<section data-dashboard-repo-tab-panel="agents" class="hidden"><div class="mt-4 overflow-hidden rounded-lg border border-border bg-background"><div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3"><span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="bot" class="h-3.5 w-3.5 text-primary"></i>Agents</span><button type="button" data-repo-agents-refresh class="inline-flex h-7 items-center gap-1.5 rounded-md border border-border px-2.5 text-xs font-medium text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="refresh-cw" class="h-3.5 w-3.5"></i>Refresh</button></div><div data-repo-agents></div></div></section>` : ""}
          </div>
          <aside data-repo-about data-repo-about-rail class="min-w-0 rounded-lg border border-border bg-background p-4">
            ${repo.isPrivate ? "" : `
            <div data-repo-social-badge class="-mx-4 -mt-4 mb-4 overflow-hidden rounded-t-lg border-b border-border">
              <div data-repo-social-banner class="h-20 w-full bg-secondary bg-cover bg-center" style="background-image:url('/assets/fediverse-banner.png')"></div>
              <div class="flex items-end gap-3 px-4 pb-3">
                <img data-repo-social-logo src="/assets/fediverse-avatar.png" alt="Repository logo" class="-mt-7 h-14 w-14 shrink-0 rounded-xl border-2 border-background bg-background object-cover shadow" />
                <div class="min-w-0 pb-0.5">
                  <a data-repo-social-handle data-repo-mastodon-link href="${escapeHtml(mastodonUrl)}" target="_blank" rel="noopener noreferrer" title="${escapeHtml(fediHandle)}" class="block min-w-0 truncate font-mono text-[11px] text-foreground hover:underline">${escapeHtml(fediHandle)}</a>
                  <div class="text-[11px] text-muted-foreground"><span data-repo-social-followers class="font-mono text-foreground">–</span> fediverse watchers</div>
                </div>
              </div>
            </div>`}
            <div class="flex items-center justify-between gap-3">
              <h3 class="text-sm font-semibold text-foreground">About</h3>
              ${canEditAbout
                ? `<button type="button" data-repo-about-edit aria-label="Edit About" class="inline-flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="settings" class="h-3.5 w-3.5"></i></button>`
                : `<i data-lucide="settings" class="h-3.5 w-3.5 text-muted-foreground"></i>`}
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
              <fieldset class="grid gap-1.5 rounded-md border border-border p-2 text-[11px] text-muted-foreground">
                <legend class="px-1 text-[10px] font-semibold uppercase tracking-wide">ActivityPub federation</legend>
                <label class="inline-flex items-start gap-1.5"><input data-repo-ap-federate type="checkbox" class="mt-0.5 h-3 w-3" checked />Federate this repository (fediverse actor and handle)</label>
                <label class="inline-flex items-start gap-1.5"><input data-repo-ap-broadcast type="checkbox" class="mt-0.5 h-3 w-3" checked />Post new issues, pull requests, discussions and releases to followers</label>
                <label class="inline-flex items-start gap-1.5"><input data-repo-ap-comments type="checkbox" class="mt-0.5 h-3 w-3" checked />Accept fediverse replies as federated comments</label>
              </fieldset>
              <p class="text-[10px] leading-4 text-muted-foreground">Saved to the relay now and written into the repo's committed <span class="font-mono">.forkmesh/info.json</span> (what the desktop app shows) the next time the owner's node syncs.</p>
              <div class="flex flex-wrap items-center justify-between gap-2">
                <span data-repo-about-status class="text-[11px] text-muted-foreground"></span>
                <span class="inline-flex items-center gap-2">
                  <button type="button" data-repo-about-cancel class="inline-flex h-8 items-center rounded-md border border-border px-3 text-xs font-medium text-muted-foreground hover:bg-secondary hover:text-foreground">Cancel</button>
                  <button type="submit" class="inline-flex h-8 items-center rounded-md bg-primary px-3 text-xs font-medium text-primary-foreground hover:bg-primary/90">Save</button>
                </span>
              </div>
            </form>
            <div class="mt-4 grid gap-2 text-xs text-muted-foreground">
              <a href="${escapeHtml(cloneUrl(repo))}" class="dashboard-accent-link inline-flex min-w-0 items-center gap-2 hover:underline"><i data-lucide="link" class="h-3.5 w-3.5 shrink-0"></i><span class="min-w-0 truncate">Open clean URL</span></a>
              <a href="${escapeHtml(readmeHref)}" data-repo-readme-link data-repo-readme-path="${escapeHtml(readmePath)}" class="inline-flex min-w-0 items-center gap-2 hover:text-foreground hover:underline"><i data-lucide="book-open" class="h-3.5 w-3.5"></i><span>Readme</span></a>
              <a href="${escapeHtml(`${repoPathUrl(repo)}/insights`)}" data-repo-activity-link class="inline-flex min-w-0 items-center gap-2 hover:text-foreground hover:underline"><i data-lucide="activity" class="h-3.5 w-3.5"></i><span>Activity</span></a>
            </div>
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
    setRepoTab(repoTabRoutesFor(repo).includes(routeKind) ? routeKind : "code");
    window.lucide?.createIcons();
    loadRepositoryTree(repo, routeKind === "tree" ? routePath : "");
    if (routeKind === "blob" && routePath) loadRepositoryBlob(repo, routePath);
    loadRepoFeaturePanels(repo, recordRoute);
    loadRepoPendingCounts(repo);
    loadRepoAboutRail(repo);
    loadRepoStarState(repo, $("[data-repo-action-star]"));
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
    const btnHeight = compact ? "h-8" : "h-9";
    const copyBtn = (value, label) =>
      `<button type="button" data-dashboard-copy="${escapeHtml(value)}" aria-label="${label}" class="copy-button inline-flex h-7 w-7 shrink-0 items-center justify-center rounded-md border border-border text-muted-foreground hover:bg-secondary hover:text-foreground"><i data-lucide="copy" class="copy-icon h-3.5 w-3.5"></i><i data-lucide="check" class="copy-check h-3.5 w-3.5"></i></button>`;
    return `
      <div data-repo-code-wrap class="relative">
        <button type="button" data-repo-code-button aria-haspopup="true" aria-expanded="false" class="inline-flex ${btnHeight} items-center justify-center gap-2 rounded-md border border-primary/40 bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90 transition-colors">
          <i data-lucide="code" class="h-3.5 w-3.5"></i><span>Code</span><i data-lucide="chevron-down" class="h-3 w-3"></i>
        </button>
        <div data-repo-code-menu class="absolute right-0 z-40 mt-1 hidden w-80 max-w-[calc(100vw-2rem)] rounded-lg border border-border bg-background p-3 text-left shadow-xl">
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

    renderProfile(session || { nodeName: "guest" });
    if (session?.nodeName) {
      if (grant) offerLinkGrant(grant);
      hydrateCanonicalProfile(session).then(() => loadNotifications()).catch(() => {});
    }
    repositoriesReady = loadRepositories();
    return true;
  }

  function initHomePage() {
    renderHomeChangelog();
    // Feed + top repositories fill in when loadRepositories()/loadNotifications()
    // resolve — both re-render the home containers.
    // Active agent sessions (adhoc #81) need the catalog first so we know which
    // repos to poll; fetch them once repositories are loaded.
    repositoriesReady?.then(() => loadHomeAgentSessions()).catch(() => {});
  }

  function initReposPage() {
    // The catalog fetch from initSharedChrome() owns this page's content; the
    // baked markup already shows the loading state.
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
    const repo = requested ? findRepository(requested) : null;
    if (repo) {
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

    const appearanceThemeButton = event.target.closest("[data-appearance-theme]");
    if (appearanceThemeButton) {
      saveDashboardTheme(appearanceThemeButton.dataset.appearanceTheme);
      return;
    }

    const longDiffToggle = event.target.closest("[data-long-diff-toggle]");
    if (longDiffToggle) {
      saveDashboardLongDiffs(Boolean(longDiffToggle.checked));
      return;
    }

    const showFullDiff = event.target.closest("[data-show-full-diff]");
    if (showFullDiff) {
      const key = showFullDiff.dataset.showFullDiff || "";
      if (key) state.longDiffOverrides[key] = true;
      if (key.startsWith("commit:") && state.repoCommitDetail) {
        const { repo, data } = state.repoCommitDetail;
        const container = $("[data-repo-commits]");
        if (container) container.innerHTML = renderRepoCommitDetail(repo, data);
      } else if (key.startsWith("pull:") && state.repoRecordDetail) {
        const { repo, kind, number, parsed } = state.repoRecordDetail;
        const container = $(`[data-repo-${kind}]`);
        if (container) container.innerHTML = renderRepoRecordDetail(repo, kind, number, parsed);
      }
      window.lucide?.createIcons();
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

    const contributionYear = event.target.closest("[data-profile-contribution-year]");
    if (contributionYear) {
      state.profileContributions.year = Number(contributionYear.dataset.profileContributionYear) || new Date().getFullYear();
      renderProfileContributionGraph();
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

      if (event.target.closest("[data-repo-about-cancel]")) {
        setRepoAboutStatus("");
        setRepoAboutEditing(false);
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
        if (["pulls", "discussions"].includes(kind) && /^\d+$/.test(number)) {
          navigateHistory(`${repoPathUrl(state.selectedRepo)}/${kind}/${number}`);
        }
        loadRepoRecordDetail(state.selectedRepo, kind, number);
        return;
      }

      const recordBackButton = event.target.closest("[data-repo-record-back]");
      if (recordBackButton && state.selectedRepo) {
        const kind = recordBackButton.dataset.repoRecordBack || "";
        if (["pulls", "discussions"].includes(kind)) {
          navigateHistory(`${repoPathUrl(state.selectedRepo)}/${kind}`);
        }
        state.repoRecordDetail = null;
        if (kind === "issues") {
          renderRepoIssues();
        } else {
          loadRepoCollection(state.selectedRepo, kind, `[data-repo-${kind}]`);
        }
        return;
      }

      // PR detail section tabs (Conversation / Commits / Files changed): all
      // sections render on the one page, so a tab click highlights itself and
      // scrolls its section into view.
      const recordTabButton = event.target.closest("[data-repo-record-tab]");
      if (recordTabButton) {
        const article = recordTabButton.closest("[data-repo-record-detail]");
        if (article) {
          article.querySelectorAll("[data-repo-record-tab]").forEach((button) => {
            const active = button === recordTabButton;
            button.classList.toggle("border-primary", active);
            button.classList.toggle("border-transparent", !active);
            button.classList.toggle("text-foreground", active);
            button.classList.toggle("text-muted-foreground", !active);
          });
          const targetSelector = {
            conversation: "[data-repo-record-conversation]",
            commits: "[data-repo-record-patch-panel]",
            files: "[data-repo-record-files-panel]",
            badge: "[data-repo-record-badge-panel]",
          }[recordTabButton.dataset.repoRecordTab || ""];
          const target = targetSelector ? article.querySelector(targetSelector) : null;
          target?.scrollIntoView({ behavior: "smooth", block: "start" });
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
        // Per-repo federation switches ride the same save (loadRepoFediverse
        // seeded the checkboxes with the stored values, so an untouched form
        // round-trips unchanged and the worker writes nothing).
        const federateBox = aboutForm.querySelector("[data-repo-ap-federate]");
        if (federateBox) {
          media.fediverse = {
            federate: federateBox.checked,
            broadcastEvents: Boolean(aboutForm.querySelector("[data-repo-ap-broadcast]")?.checked),
            acceptComments: Boolean(aboutForm.querySelector("[data-repo-ap-comments]")?.checked),
          };
        }
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
  $("[data-profile-page-password]")?.addEventListener("input", updateRenameButton);
  $("[data-profile-delete-confirm]")?.addEventListener("input", () => {
    const button = $("[data-profile-delete-account]");
    if (!button) return;
    button.disabled = ($("[data-profile-delete-confirm]")?.value || "").trim() !== "DELETE";
    button.classList.toggle("opacity-40", button.disabled);
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
          if (["pulls", "discussions"].includes(kind)) {
            if (/^\d+$/.test(path)) {
              loadRepoRecordDetail(repo, kind, path);
            } else if (state.repoRecordDetail?.kind === kind) {
              state.repoRecordDetail = null;
              loadRepoCollection(repo, kind, `[data-repo-${kind}]`);
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
  // ---------------------------------------------------------------------------
  // Boot. Every page document ships this same bundle; <body data-page="..."> is
  // baked at build time (dashboard_shell.PAGES) and picks which init runs. The
  // page's own markup is the only view in the document and is active at parse
  // time, so the right page paints immediately — data fills in afterwards.
  const PAGE_INITS = {
    "home": initHomePage,
    "repos": initReposPage,
    "network": initNetworkPage,
    "chat": initChatPage,
    "settings": initSettingsPage,
    "profile": initProfileOverviewPage,
    "profile-repositories": initProfileOverviewPage,
    "repo": initRepoPage,
  };

  applyDashboardTheme(readDashboardTheme());
  renderLongDiffPreference();
  if (initSharedChrome()) {
    (PAGE_INITS[currentPage()] || initHomePage)();
    initPageHistory();
  }
})();
