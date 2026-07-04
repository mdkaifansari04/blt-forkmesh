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
      return normalizeDashboardTheme(localStorage.getItem(DASHBOARD_THEME_KEY));
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
    } catch (_) {}
    applyDashboardTheme(nextTheme);
  }

  function writeSession(nextSession) {
    state.session = nextSession;
    try {
      localStorage.setItem("forkmesh.session", JSON.stringify(nextSession));
      document.cookie = "forkmesh_session=1; Path=/; Max-Age=2592000; SameSite=Lax";
    } catch (_) {}
  }

  function logout() {
    if (state.profileSyncTimer) {
      window.clearInterval(state.profileSyncTimer);
      state.profileSyncTimer = null;
    }
    state.pollProfileToken = null;
    state.pollNotifToken = null;
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
    if (value === undefined || value === null || value === "") return "unknown";
    const numeric = Number(value);
    const date = Number.isFinite(numeric) && numeric > 0
      ? new Date(numeric < 1000000000000 ? numeric * 1000 : numeric)
      : new Date(value);
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
      return { primary: sorted[0], members: sorted };
    });
  }

  function sourceOfTruth(group) {
    return group.members.find((m) => (m.source || "").trim() === "local-node") || group.primary;
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

  // Feature-tab route segments (mirrors 404.html's `featureTabs` list) — tells
  // a tab route (e.g. /owner/repo/issues) apart from a tree/blob code deep link.
  const REPO_TAB_ROUTES = ["commits", "releases", "issues", "pulls", "discussions", "mirrors"];

  // The owner-only "Agents" tab (adhoc #182) is only ever a recognized route
  // for the account that can actually see it — sessionCanAssignAgent gates it
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
  // Raw files can be much bigger than the final embedded size — anything under
  // this is accepted into the crop/compress modal rather than rejected outright.
  const ISSUE_IMAGE_RAW_MAX_BYTES = 20 * 1024 * 1024;

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
