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
