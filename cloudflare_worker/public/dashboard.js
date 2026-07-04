(() => {
  const state = {
    repositories: [],
    filteredRepositories: [],
    filteredGroups: [],
    page: 1,
    pageSize: 5,
    selectedRepo: null,
    selectedBranches: {},
    repoBranches: {},
    repoBranchQueries: {},
    repoCollectionPages: {},
    session: null,
    profileSyncTimer: null,
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
    pollProfileToken: null,
    pollNotifToken: null,
    selectedNotificationId: "",
    issuesView: { filter: "open", items: [] },
    claimNode: { pendingNodeId: "" },
    linkGrant: null,
    repoMirrors: [],
    repoServedBy: null,
  };

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const MAX_REPO_FILE_FINDER_RESULTS = 500;
  const MAX_REPO_FILE_FINDER_SECONDS = 6;
  const REPO_COLLECTION_PAGE_SIZE = 5;
  const PROFILE_SYNC_INTERVAL_MS = 60000;
  const DASHBOARD_THEME_KEY = "forkmesh.dashboard.theme";

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
            <p class="text-xs text-muted-foreground">Drag on the image to crop it, then it's auto-compressed to fit. Attached images share the issue's size limit — redraw a smaller crop if it still doesn't fit.</p>
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
              status.textContent = `Ready — ${formatSize(blob.size)} (fits under ${formatSize(maxBytes)}).`;
              addButton.disabled = false;
              return;
            }
          }
          scale *= 0.65;
        }
        status.className = "text-destructive";
        status.textContent = "Still too large after compressing — drag to crop a smaller area and it'll retry.";
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

  // Mirrors IssueStore::contentForSigning + canonicalString and the desktop's
  // inbox POST (verify_issue_event in the worker). New issues are signed with
  // number 0; the maintainer assigns the durable number on drain.
  async function submitWebIssue(repo, title, body, assignAgent = false, ownerPassword = "") {
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
      meta: { labels: [], milestone: "", priority: 0, assignees: [], wantsAgent: Boolean(assignAgent) },
    };
    // The worker only honors wantsAgent when this re-proves account ownership
    // (password check) — a raw client-side checkbox isn't enough, since it
    // makes the owner's node start a coding agent unattended (adhoc #105).
    // ownerAccount names which account is re-proving itself: the repo owner, or
    // an admin acting on the owner's behalf (adhoc #141). The worker verifies
    // that account's password and that it's the owner or an admin.
    if (assignAgent) {
      payload.ownerPassword = ownerPassword;
      payload.ownerAccount = state.session?.nodeName || "";
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
      avatarPng: body.avatarPng || "",
      avatarUpdatedAt: Number(body.avatarUpdatedAt) || 0,
      kind: body.kind || base.kind || "",
      owner: body.owner ?? base.owner ?? "",
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

  // One lightweight /api/poll instead of re-fetching the full profile (avatar
  // and all) and the full notification list every tick. The server returns
  // cheap change tokens; we only fire the heavier fetches when a token moved.
  // The unread count rides along, so the bell badge updates from the poll alone.
  async function pollStatus(force = false) {
    const node = String(state.session?.nodeName || "").trim().toLowerCase();
    if (!validNodeName(node)) return;
    let data;
    try {
      data = await fetchJson(`/api/poll?node=${encodeURIComponent(node)}`);
    } catch (_) {
      return;
    }
    const profileToken = data.profile?.token ?? null;
    if (force || (profileToken !== null && profileToken !== state.pollProfileToken)) {
      await refreshPublicProfile(state.session);
    }
    if (profileToken !== null) state.pollProfileToken = profileToken;

    const notif = data.notif;
    if (notif && typeof notif.unread === "number") {
      state.notificationUnread = notif.unread;
      renderNotificationPreview();
    }
    const notifToken = notif?.token ?? null;
    if (force || (notifToken !== null && notifToken !== state.pollNotifToken)) {
      await loadNotifications();
    }
    if (notifToken !== null) state.pollNotifToken = notifToken;
  }

  function startProfileSync() {
    if (state.profileSyncTimer || !state.session?.nodeName) return;
    state.profileSyncTimer = window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      pollStatus();
    }, PROFILE_SYNC_INTERVAL_MS);
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

  // Top-level sections that get their own address-bar entry (?section=network,
  // ?section=profile, ...) so a refresh or Back/Forward restores whichever page
  // you were on instead of always dropping you back on the repos list. "repos"
  // is the default, so it stays on the bare /dashboard URL. The repo-detail view
  // ("explore") is addressed by the /owner/repo path instead, not here.
  const SECTION_ROUTES = ["home", "repos", "network", "profile", "chat"];

  function requestedSection() {
    const value = (new URLSearchParams(location.search).get("section") || "").trim();
    return SECTION_ROUTES.includes(value) ? value : "";
  }

  function sectionUrl(section) {
    return section && section !== "repos" && SECTION_ROUTES.includes(section)
      ? `/dashboard?section=${section}`
      : "/dashboard";
  }

  // Switch to a top-level section AND reflect it in the URL (plus run any
  // per-section load hooks) so the choice survives a refresh. Pass push:false
  // when restoring from the URL (init/popstate) so we don't re-push it.
  function showSection(section, { push = true } = {}) {
    if (push) {
      state.selectedRepo = null;
      navigateHistory(sectionUrl(section));
    }
    setSection(section);
    if (section === "profile") {
      renderProfilePage(state.session);
      refreshPublicProfile(state.session);
    }
  }

  function setMobileSidebarOpen(open) {
    document.body.classList.toggle("dashboard-sidebar-open", open);
    if (open) document.body.classList.remove("network-drawer-open");
    $$("[data-mobile-menu-toggle]").forEach((button) => {
      button.setAttribute("aria-expanded", open ? "true" : "false");
      button.setAttribute("aria-label", open ? "Close dashboard menu" : "Open dashboard menu");
    });
    $$("[data-mobile-network-toggle]").forEach((button) => {
      if (open) {
        button.setAttribute("aria-expanded", "false");
        button.setAttribute("aria-label", "Open network drawer");
      }
    });
  }

  function setMobileNetworkOpen(open) {
    document.body.classList.toggle("network-drawer-open", open);
    if (open) document.body.classList.remove("dashboard-sidebar-open");
    $$("[data-mobile-network-toggle]").forEach((button) => {
      button.setAttribute("aria-expanded", open ? "true" : "false");
      button.setAttribute("aria-label", open ? "Close network drawer" : "Open network drawer");
    });
    $$("[data-mobile-menu-toggle]").forEach((button) => {
      if (open) {
        button.setAttribute("aria-expanded", "false");
        button.setAttribute("aria-label", "Open dashboard menu");
      }
    });
  }

  function closeMobileDrawers() {
    setMobileSidebarOpen(false);
    setMobileNetworkOpen(false);
  }

  const dashboardDesktopMedia = window.matchMedia("(min-width: 1024px)");
  function closeDrawersOnDesktopChange(event) {
    if (event.matches) closeMobileDrawers();
  }
  if (dashboardDesktopMedia.addEventListener) {
    dashboardDesktopMedia.addEventListener("change", closeDrawersOnDesktopChange);
  } else if (dashboardDesktopMedia.addListener) {
    dashboardDesktopMedia.addListener(closeDrawersOnDesktopChange);
  }

  function renderProfile(session) {
    const name = session?.nodeName || session?.email || "My Profile";
    const nameEl = $("[data-dashboard-profile-name]");
    const statusEl = $("[data-dashboard-profile-status]");
    const avatar = $("[data-dashboard-profile-avatar]");
    const adminButton = $("[data-admin-button]");

    if (nameEl) nameEl.textContent = name;
    if (statusEl) {
      statusEl.textContent = session?.emailVerified
        ? "Email verified"
        : "Verify email in profile";
    }
    applyAvatar(avatar, session);
    if (adminButton) {
      adminButton.classList.toggle("hidden", !session?.isAdmin);
      if (session?.isAdmin && session?.adminUrl) {
        adminButton.href = session.adminUrl;
      }
    }
    renderProfileModal(session);
    renderProfilePage(session);
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

  function validNodeName(value) {
    return /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(String(value || ""));
  }

  // A node's Ed25519 public key (raw 32 bytes, base64url, unpadded) — the
  // value the desktop app's own profile card labels "Node ID" (issue #351),
  // so the claim-node input below must accept it alongside the account name.
  function validNodePubkey(value) {
    return /^[A-Za-z0-9_-]{43}$/.test(String(value || ""));
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

  function renderProfilePage(session) {
    const name = session?.nodeName || "My Profile";
    const email = session?.email || "No email on file";
    const avatar = $("[data-profile-page-avatar]");
    const nameEl = $("[data-profile-page-node-name]");
    const emailEl = $("[data-profile-page-email]");
    const accountStatus = $("[data-profile-page-account-status]");
    const payoutStatus = $("[data-profile-page-payout-status]");
    const adminStatus = $("[data-profile-page-admin-status]");
    const emailStatus = $("[data-profile-page-email-status]");
    const verifyButton = $("[data-profile-page-verify-email]");
    const solanaInput = $("[data-profile-page-solana]");
    const renameInput = $("[data-profile-rename-input]");

    applyAvatar(avatar, session);
    if (nameEl) nameEl.textContent = name;
    if (emailEl) emailEl.textContent = email;
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
    if (!session?.emailVerified) {
      state.nodeNameAvailability.available = false;
      setRenameStatus("Verify your email before changing your node name.", "bad");
    } else if (!($("[data-profile-rename-input]")?.value || "").trim()) {
      state.nodeNameAvailability.available = false;
      setRenameStatus("Enter a new node name to check availability.", "");
    }
    updateRenameButton();
    renderClaimNodePanel(session);
  }

  function profilePayload(extra = {}, passwordOverride) {
    const password = passwordOverride ?? ($("[data-profile-password]")?.value || "");
    return {
      nodeName: state.session?.nodeName || "",
      email: state.session?.email || "",
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
    const data = await fetchJson("/api/repositories");
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
        setProfileHint("Enter your current password to save profile changes.", "bad");
      } else {
        setProfilePageHint(hintSelector, "Enter your current password to save profile changes.", "bad");
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
        : "Could not save profile. Check your password and try again.";
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
  // /dashboard?link_node=<node>&link_ts=<ts>&link_sig=<sig> — a short-lived
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
    // can't replay it (and it doesn't linger in the visible URL), then land on
    // the profile's Nodes panel and ask for one explicit "Authenticate & link"
    // click. The grant overrides any existing association, so the click is the
    // moment of consent on the browser side.
    const params = new URLSearchParams(location.search);
    for (const key of ["link_node", "link_ts", "link_sig"]) params.delete(key);
    const rest = params.toString();
    window.history.replaceState(null, "", location.pathname + (rest ? `?${rest}` : ""));
    state.linkGrant = grant;
    setSection("profile");
    const row = $("[data-link-grant-row]");
    if (row) row.classList.remove("hidden");
    const text = $("[data-link-grant-text]");
    if (text) {
      text.textContent =
        `Link node "${grant.nodeName}" to this account (` +
        `${state.session?.nodeName || "you"})? This node will belong to you — ` +
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
          ? `"${body.nodeId || grant.nodeName}" is this account — already yours.`
          : body.alreadyLinked
            ? `"${body.nodeId || grant.nodeName}" is already linked to your account.`
            : `Linked "${body.nodeId || grant.nodeName}" to your account.`,
        "good");
    } catch (error) {
      state.linkGrant = null;
      $("[data-link-grant-row]")?.classList.add("hidden");
      const messages = {
        unauthorized: "The link expired — click \"Link this node to your account\" in the node's app again.",
        bad_signature: "The link couldn't be verified — click the button in the node's app again.",
        grant_used: "That link was already used — click the button in the node's app again.",
        no_such_node: "That node isn't registered with the relay yet.",
        not_a_user: "This login can't own nodes — sign up as a user (email + password) first.",
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
        <i data-lucide="circle" class="w-1.5 h-1.5 ml-auto fill-current shrink-0 ${repoIsLive(repo) ? "text-primary" : "text-muted-foreground"}"></i>
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

    renderSidebarRepositories(session);
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
    if (!state.selectedRepo) return;
    navigateHistory(tab === "code"
      ? (state.repoCodeUrl || repoPathUrl(state.selectedRepo))
      : `${repoPathUrl(state.selectedRepo)}/${tab}`);
    if (["issues", "pulls", "discussions", "releases"].includes(tab) && !state.loadedRepoTabs?.[tab]) {
      if (!state.loadedRepoTabs) state.loadedRepoTabs = {};
      state.loadedRepoTabs[tab] = true;
      if (tab === "issues") loadRepoIssues(state.selectedRepo);
      else if (tab === "releases") loadRepoReleases(state.selectedRepo);
      else loadRepoCollection(state.selectedRepo, tab, `[data-repo-${tab}]`);
    } else if (tab === "issues") {
      // Re-selecting the tab should return to the issues list even if the
      // new-issue compose form was left open.
      renderRepoIssues();
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
    renderRepoBreadcrumb(repo, path);

    treeBody.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading tree...</div>';
    renderRepoExplorer(repo, path, []);
    try {
      const requestedAt = performance.now();
      const data = await fetchJson(repoLiveUrl(repo, "tree", { path }));
      renderRepoServedBy(data.servedBy, performance.now() - requestedAt);
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
        return `
            <button data-dashboard-${isTree ? "tree" : "blob"}-path="${escapeHtml(childPath)}" class="grid w-full grid-cols-[1.5rem_minmax(0,1fr)_auto] items-center gap-3 border-t border-border px-4 py-2.5 text-left text-sm hover:bg-secondary/40 transition-colors sm:grid-cols-[1.5rem_minmax(9rem,0.8fr)_minmax(0,1fr)_auto]">
              <i data-lucide="${isTree ? "folder" : "file"}" class="h-4 w-4 shrink-0 text-muted-foreground"></i>
              <span class="min-w-0 truncate font-medium text-foreground">${escapeHtml(entry.name || "entry")}</span>
              <span class="hidden min-w-0 truncate text-xs text-muted-foreground sm:block">${escapeHtml(entry.message || entry.commitMessage || "mirrored repository object")}</span>
              <span class="shrink-0 text-xs text-muted-foreground font-mono">${isTree ? "dir" : escapeHtml(formatSize(entry.size))}</span>
            </button>`;
      }).join("");
      navigateHistory(repoPathUrl(repo, "tree", path));
      window.lucide?.createIcons();
      if (!path) {
        const readmeEntry = entries.find((e) => e.type === "blob" && /^readme(\.md|\.txt|\.rst)?$/i.test(String(e.name || "")));
        const readmeBody = detail.querySelector("[data-repo-readme-body]");
        const readmeFilename = detail.querySelector("[data-repo-readme-filename]");
        if (readmeBody) {
          if (readmeEntry) {
            if (readmeFilename) readmeFilename.innerHTML = `<i data-lucide="book-open" class="h-3.5 w-3.5 text-muted-foreground"></i>${escapeHtml(readmeEntry.name)}`;
            try {
              const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: readmeEntry.name }));
              const text = blobText(blob);
              readmeBody.className = "whitespace-pre-wrap p-4 font-mono text-xs leading-5 text-foreground overflow-auto max-h-[40rem]";
              readmeBody.textContent = text;
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
    viewer.innerHTML = '<div class="py-3 text-sm text-muted-foreground">Loading file...</div>';
    renderRepoBreadcrumb(repo, path, "blob");
    setRepoExplorerSelection(path, "blob");
    try {
      const pathPreview = repoPreviewKindFromPath(path);
      if (pathPreview && pathPreview.kind !== "csv") {
        if (renderRepoPreview(viewer, repo, path, {})) return;
      }
      const requestedAt = performance.now();
      const data = await fetchJson(repoLiveUrl(repo, "blob", { path }));
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

  function formatRecordDate(value) {
    const number = Number(value);
    if (Number.isFinite(number) && number > 0) {
      return formatDate(number < 1000000000000 ? number * 1000 : number);
    }
    return value ? formatDate(value) : "unknown";
  }

  async function fetchRepoJson(path) {
    const response = await fetch(path, { headers: { accept: "application/json" } });
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
      dir: "issues", file: "issue.md",
      icon: "circle-dot",
      tone: "text-primary",
      empty: "No issues have been committed to this mirror yet.",
      meta(values) {
        const labels = parseFrontMatterList(values.labels).slice(0, 3).join(", ");
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

  async function fetchRepoBlobs(repo, paths) {
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
    query.set("ref", repoSelectedBranch(repo));
    const requestedAt = performance.now();
    const data = await fetchRepoJson(`${repoApiBase(repo)}/blobs?${query.toString()}`);
    renderRepoServedBy(data.servedBy, performance.now() - requestedAt);
    return data.blobs || {};
  }

  async function loadRepoRecordsFromMirror(repo, config) {
    let tree;
    try {
      tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: config.dir }));
    } catch (error) {
      if (isMissingMirrorFolder(error)) return [];
      throw error;
    }
    const dirs = (Array.isArray(tree.entries) ? tree.entries : [])
      .filter((entry) => entry.type === "tree" && /^\d+$/.test(String(entry.name || "")))
      .sort((a, b) => Number(b.name) - Number(a.name))
      .slice(0, 50);
    // One batched request for every record file instead of a per-record fan-out.
    const blobs = await fetchRepoBlobs(
      repo, dirs.map((entry) => `${config.dir}/${entry.name}/${config.file}`));
    const records = dirs.map((entry) => {
      const number = Number(entry.name);
      const blob = blobs[`${config.dir}/${entry.name}/${config.file}`];
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
    return pageItems.map((item) => `
      <button type="button" data-repo-record-kind="${escapeHtml(kind)}" data-repo-record-number="${escapeHtml(item.pending ? item.localId : item.number)}" class="grid w-full grid-cols-[1.25rem_minmax(0,1fr)_auto] gap-3 border-t border-border px-4 py-3 text-left transition-colors hover:bg-secondary/40">
        <i data-lucide="${config.icon}" class="mt-0.5 h-4 w-4 ${config.tone}"></i>
        <span class="min-w-0">
          <span class="flex min-w-0 flex-wrap items-center gap-2">
            <span class="font-mono text-xs text-muted-foreground">${item.pending ? "pending" : `#${formatCount(item.number)}`}</span>
            <span class="min-w-0 truncate text-sm font-medium text-foreground">${escapeHtml(item.title)}</span>
          </span>
          <span class="mt-1 line-clamp-2 text-xs text-muted-foreground">${escapeHtml(item.body || `${item.author} opened this signed ${config.itemLabel}`)}</span>
          <span class="mt-1 block truncate text-[10px] font-mono text-muted-foreground">${escapeHtml(item.author)} · ${escapeHtml(item.date)}${item.meta ? ` · ${escapeHtml(item.meta)}` : ""}</span>
        </span>
        <span class="self-start shrink-0 flex items-center gap-2">
          ${item.wantsAgent ? '<i data-lucide="zap" class="h-4 w-4 text-yellow-500" title="Assigned to agent"></i>' : ''}
          <span data-repo-record-state class="rounded-md border border-border bg-secondary/60 px-2 py-0.5 text-[10px] font-mono text-foreground">${escapeHtml(item.pending ? "syncing…" : (item.state || "open"))}</span>
        </span>
      </button>`).join("") + (["issues", "pulls"].includes(kind) ? renderRepoCollectionPagination(kind, safePage, totalPages, items.length) : "");
  }

  // Shared unified-diff parser used by both the commit diff and the pull
  // patch views. Groups lines per file and tracks real old/new line numbers
  // per hunk so the viewer can render a GitHub-style dual gutter instead of
  // a single running index.
  const MAX_DIFF_LINES = 4000;

  function parseDiffFiles(rawText) {
    const allLines = String(rawText || "").split("\n");
    if (allLines.length && allLines[allLines.length - 1] === "") allLines.pop();
    const truncated = allLines.length > MAX_DIFF_LINES;
    const lines = truncated ? allLines.slice(0, MAX_DIFF_LINES) : allLines;
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
    return { files, truncated };
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
    const { files, truncated } = parsed;
    if (!files.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No changes to display.</div>';
    const images = new Map();
    (Array.isArray(imageDiffs) ? imageDiffs : []).forEach((item) => {
      if (item?.path && item?.mime) images.set(item.path, item);
    });
    const note = truncated ? '<div class="border-t border-amber-500/30 bg-amber-500/10 px-3 py-2 text-xs text-amber-200">Diff truncated for display; showing the first portion of this change.</div>' : "";
    return `<div class="grid gap-3 p-3">${files.map((file) => renderDiffFileBlock(file, images.get(file.newPath) || images.get(file.oldPath))).join("")}</div>${note}`;
  }

  function renderRepoPullPatch(patch) {
    if (!String(patch || "").trim()) return '<div class="px-4 py-3 text-sm text-muted-foreground">No textual patch is committed for this pull request. Branch-backed PRs are reconstructed by the desktop client.</div>';
    return `<div data-repo-pull-patch>${renderDiffFiles(parseDiffFiles(patch))}</div>`;
  }

  async function loadRepoPullPatch(repo, number) {
    const patchPath = `pulls/${number}/changes.patch`;
    try {
      const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: patchPath }));
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
      tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: `pulls/${number}` }));
    } catch (_) {
      return [];
    }
    const files = (Array.isArray(tree.entries) ? tree.entries : [])
      .filter((entry) => entry.type !== "tree" && /^\d+-/.test(String(entry.name || "")))
      .sort((a, b) => String(a.name).localeCompare(String(b.name)));
    if (!files.length) return [];
    const blobs = await fetchRepoBlobs(repo, files.map((entry) => `pulls/${number}/${entry.name}`));
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

  function renderRepoRecordDetail(repo, kind, number, parsed, options = {}) {
    const config = repoCollectionConfig[kind] || repoCollectionConfig.issues;
    const values = parsed.values || {};
    const title = values.title || `${config.itemLabel} #${number}`;
    const state = values.status || values.state || values.category || "open";
    const author = values.authorName || values.author || "unknown";
    const date = formatRecordDate(values.updatedAt || values.createdAt || values.ts);
    const body = parsed.body || "No description was committed for this record.";
    const recordLabel = options.pending ? "pending" : `#${escapeHtml(number)}`;
    const pendingNotice = options.pending ? `
        <div class="rounded-lg border border-dashed border-border bg-secondary/30 px-4 py-3 text-xs text-muted-foreground">This ${escapeHtml(config.itemLabel)} is still syncing to the maintainer's inbox and hasn't been drained to the public mirror yet, so it doesn't have a number assigned.</div>` : "";
    const pullPatch = parsed.pullPatch || { patch: "", files: [], unavailable: false };
    const pullConversation = parsed.pullConversation || [];
    const pullConversationSection = kind === "pulls" ? `
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="message-square" class="h-3.5 w-3.5 text-primary"></i>Conversation</span>
            <span class="font-mono text-[10px] text-muted-foreground">${formatCount(pullConversation.length)} ${pullConversation.length === 1 ? "event" : "events"}</span>
          </div>
          <div data-repo-pull-conversation>${renderRepoPullConversation(pullConversation)}</div>
        </section>` : "";
    const pullFilesSection = kind === "pulls" ? `
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="files" class="h-3.5 w-3.5 text-primary"></i>Files changed</span>
            <span class="font-mono text-[10px] text-muted-foreground">${formatCount(pullPatch.files.length)} files</span>
          </div>
          <div data-repo-pull-files>${renderRepoPullFiles(pullPatch.files)}</div>
        </section>
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="git-compare-arrows" class="h-3.5 w-3.5 text-primary"></i>Patch</div>
          ${renderRepoPullPatch(pullPatch.patch)}
        </section>` : "";
    return `
      <article data-repo-record-detail="${escapeHtml(kind)}" class="grid gap-4 border-t border-border bg-background p-4">
        <div class="flex flex-wrap items-center justify-between gap-3">
          <button type="button" data-repo-record-back="${escapeHtml(kind)}" class="inline-flex h-8 items-center gap-2 rounded-md border border-border px-3 text-xs font-medium text-foreground hover:bg-secondary"><i data-lucide="arrow-left" class="h-3.5 w-3.5"></i>Back to ${escapeHtml(config.label)}</button>
          <span class="font-mono text-xs text-muted-foreground">${escapeHtml(repo.owner || "owner")}/${escapeHtml(repo.name || "repo")} · ${recordLabel}</span>
        </div>
        ${pendingNotice}
        <header class="rounded-lg border border-border bg-secondary/30 p-4">
          <div class="flex min-w-0 flex-wrap items-center gap-2">
            <i data-lucide="${config.icon}" class="h-4 w-4 ${config.tone}"></i>
            <h3 class="min-w-0 text-base font-semibold text-foreground">${escapeHtml(title)}</h3>
            <span data-repo-record-state class="rounded-md border border-border bg-secondary/60 px-2 py-0.5 text-[10px] font-mono text-foreground">${escapeHtml(state)}</span>
          </div>
          <p class="mt-2 text-xs text-muted-foreground">${escapeHtml(author)} · ${escapeHtml(date)}</p>
          <dl class="mt-4 grid gap-2 text-xs text-muted-foreground sm:grid-cols-2">
            ${recordDetailMeta(kind, values).map(([label, value]) => `<div><dt class="font-semibold text-foreground">${escapeHtml(label)}</dt><dd class="font-mono">${escapeHtml(value)}</dd></div>`).join("")}
          </dl>
        </header>
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="file-text" class="h-3.5 w-3.5 text-primary"></i>Body</div>
          <div data-repo-record-body class="whitespace-pre-wrap px-4 py-4 text-sm leading-6 text-foreground">${escapeHtml(body)}</div>
        </section>
        ${pullConversationSection}
        ${pullFilesSection}
      </article>`;
  }

  async function loadRepoRecordDetail(repo, kind, number) {
    const config = repoCollectionConfig[kind];
    const container = $(`[data-repo-${kind}]`);
    if (!repo || !config || !container || !number) return;
    // Issues just submitted from this session sit in the maintainer's inbox
    // until drained, so there's nothing to fetch from the mirror yet — render
    // the detail straight from the local placeholder instead.
    const pendingItem = kind === "issues"
      ? state.issuesView.items.find((item) => item.pending && item.localId === number)
      : null;
    if (pendingItem) {
      container.innerHTML = renderRepoRecordDetail(repo, kind, number, {
        values: { title: pendingItem.title, status: pendingItem.status, authorName: pendingItem.author },
        body: pendingItem.body,
      }, { pending: true });
      window.lucide?.createIcons();
      return;
    }
    const recordPath = `${config.dir}/${number}/${config.file}`;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">Loading ${escapeHtml(config.itemLabel)} #${escapeHtml(number)} from the live mirror...</div>`;
    try {
      const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: recordPath }));
      const parsed = parseFrontMatter(blobText(blob));
      const pullPatch = kind === "pulls" ? await loadRepoPullPatch(repo, number) : null;
      if (pullPatch) parsed.pullPatch = pullPatch;
      if (kind === "pulls") parsed.pullConversation = await loadRepoPullConversation(repo, number);
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
    // Total issue count from the root tree's bundled tallies; the first view of
    // the Issues tab refines it to the OPEN count (issues load lazily now).
    if (Number.isFinite(Number(counts.issues))) setRepoTabCount("issues", Number(counts.issues));
    if (Number.isFinite(Number(counts.pulls))) setRepoTabCount("pulls", Number(counts.pulls));
    if (Number.isFinite(Number(counts.discussions))) setRepoTabCount("discussions", Number(counts.discussions));
  }

  function renderRepoIssues() {
    const container = $("[data-repo-issues]");
    if (!container) return;
    const issuesView = state.issuesView;
    const filtered = issuesView.items.filter((issue) => {
      if (issuesView.filter === "all") return true;
      if (issuesView.filter === "open") return issue.status === "open";
      return issue.status !== "open";
    });
    const config = repoCollectionConfig.issues;
    const filterBar = `<div class="flex items-center gap-1 border-b border-border px-4 py-2">
      ${["open", "closed", "all"].map((stateName) => `<button type="button" data-dashboard-issue-filter="${stateName}" aria-pressed="${stateName === "open" ? "true" : "false"}" class="inline-flex h-7 items-center rounded-md px-2.5 text-xs font-medium transition-colors ${stateName === issuesView.filter ? "bg-secondary text-foreground" : "text-muted-foreground hover:text-foreground"}">${stateName[0].toUpperCase() + stateName.slice(1)}</button>`).join("")}
    </div>`;
    container.innerHTML = filterBar + (filtered.length
      ? renderRepoRecordList(filtered, config, "issues")
      : `<div class="px-4 py-3 text-sm text-muted-foreground">No ${issuesView.filter === "all" ? "" : issuesView.filter + " "}issues.</div>`);
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
    container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading issues from the live mirror...</div>';
    try {
      let tree;
      try {
        // List the git tree under issues/ (the mirror browse endpoint
        // /tree?path=issues) then read one issues/${number}/issue.md blob each.
        tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: "issues" }));
      } catch (error) {
        if (isMissingMirrorFolder(error)) {
          state.issuesView.items = [];
          state.issuesView.filter = "open";
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
        repo, dirs.map((entry) => `issues/${Number(entry.name)}/issue.md`));
      const items = dirs.map((entry) => {
        const number = Number(entry.name);
        const blob = blobs[`issues/${number}/issue.md`];
        if (!blob) return null;
        const parsed = parseFrontMatter(blobText(blob));
        const values = parsed.values || {};
        return {
          number,
          title: values.title || `issue #${number}`,
          status: values.status || values.state || "open",
          author: values.authorName || values.author || "unknown",
          date: formatRecordDate(values.updatedAt || values.createdAt || values.ts),
          meta: repoCollectionConfig.issues.meta(values),
          body: parsed.body || "",
          wantsAgent: Boolean(values.wantsAgent),
        };
      }).filter(Boolean);
      state.issuesView.items = items;
      state.issuesView.filter = "open";
      setRepoTabCount("issues", items.filter((issue) => issue.status === "open").length);
      const openIssues = items.filter((issue) => issue.status === "open").length;
      setRepoCollectionCounts("issues", openIssues, items.length - openIssues);
      renderRepoIssues();
    } catch (_) {
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Issues are unavailable until a live desktop host serves the issues/ folder.</div>';
    }
  }

  async function loadRepoCollection(repo, kind, containerSelector) {
    const container = $(containerSelector);
    const config = repoCollectionConfig[kind];
    if (!container || !config) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">Loading ${escapeHtml(config.label.toLowerCase())} from the live mirror...</div>`;
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

  // True when the logged-in account is the node that owns (hosts) this repo —
  // the only account whose node can actually pick an "assign to agent" issue up
  // and run a coding agent on it.
  function sessionOwnsRepo(repo) {
    const owner = String(state.session?.nodeName || "").toLowerCase();
    return Boolean(owner) && owner === String(repo?.owner || "").toLowerCase();
  }

  // True when the logged-in account may assign an issue to a coding agent on
  // this repo's node: the repo owner itself, or an admin acting on the owner's
  // behalf (admin node-ownership, adhoc #141). The backend independently
  // re-checks both the account password and admin status, so this is only the
  // client-side gate for showing the checkbox.
  function sessionCanAssignAgent(repo) {
    return sessionOwnsRepo(repo) || Boolean(state.session?.isAdmin);
  }

  function openIssueCompose(repo) {
    const container = $("[data-repo-issues]");
    if (!container || !repo) return;
    const who = escapeHtml(state.session?.nodeName || "you");
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
            <span>Assign to agent — once filed, ${sessionOwnsRepo(repo) ? "your" : escapeHtml(repo.owner || "the owner") + "'s"} node starts a coding agent on it automatically</span>
          </label>
          <label data-repo-issue-agent-password-row class="hidden grid gap-1 text-xs font-medium text-muted-foreground">Confirm it's you
            <input data-repo-issue-agent-password type="password" autocomplete="current-password" placeholder="Account password" class="h-9 rounded-md border border-border bg-background px-3 text-sm text-foreground outline-none focus:border-primary" />
          </label>
        </div>` : ""}
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span data-repo-issue-hint class="text-[11px] text-muted-foreground">Filed as ${who}. Sent to the maintainer's inbox for review.</span>
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
    const agentPasswordRow = container.querySelector("[data-repo-issue-agent-password-row]");
    assignAgentInput?.addEventListener("change", () => {
      agentPasswordRow?.classList.toggle("hidden", !assignAgentInput.checked);
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

    if (attachButton && fileInput) {
      attachButton.addEventListener("click", () => fileInput.click());
      fileInput.addEventListener("change", async () => {
        const files = Array.from(fileInput.files || []);
        fileInput.value = "";
        for (const file of files) {
          if (!file.type.startsWith("image/")) {
            setAttachHint(`${file.name}: not an image.`, "bad");
            continue;
          }
          if (images.length >= ISSUE_IMAGE_MAX_COUNT) {
            setAttachHint(`You can attach up to ${ISSUE_IMAGE_MAX_COUNT} images.`, "bad");
            break;
          }
          if (file.size > ISSUE_IMAGE_RAW_MAX_BYTES) {
            setAttachHint(`${file.name} is too large to attach (max ${formatSize(ISSUE_IMAGE_RAW_MAX_BYTES)}).`, "bad");
            continue;
          }
          const total = images.reduce((sum, img) => sum + img.size, 0);
          const budget = Math.min(ISSUE_IMAGE_MAX_BYTES, ISSUE_IMAGE_MAX_TOTAL_BYTES - total);
          if (budget <= 0) {
            setAttachHint("Attached images already use up the issue's size limit — remove one to add another.", "bad");
            continue;
          }
          let dataUrl;
          let size;
          if (file.size <= budget) {
            try {
              dataUrl = await readAsDataUrl(file);
              size = file.size;
            } catch (_) {
              setAttachHint(`Could not read ${file.name}.`, "bad");
              continue;
            }
          } else {
            setAttachHint(`${file.name} is ${formatSize(file.size)} — crop or compress it to fit under ${formatSize(budget)}.`);
            const result = await openImageResizeModal(file, budget);
            if (!result) {
              setAttachHint("");
              continue;
            }
            dataUrl = result.dataUrl;
            size = result.size;
          }
          const id = `forkmesh-pending-image:${Date.now().toString(36)}${images.length}`;
          const name = file.name.replace(/[[\]]/g, "_");
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
        }
        renderAttachmentChips();
      });
    }
    container.querySelector("[data-repo-issue-title]")?.focus();
  }

  async function handleIssueComposeSubmit(repo, form) {
    if (!repo || !form) return;
    const titleInput = form.querySelector("[data-repo-issue-title]");
    const bodyInput = form.querySelector("[data-repo-issue-body]");
    const submit = form.querySelector("[data-repo-issue-submit]");
    const assignAgentInput = form.querySelector("[data-repo-issue-assign-agent]");
    const agentPasswordInput = form.querySelector("[data-repo-issue-agent-password]");
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
    const ownerPassword = String(agentPasswordInput?.value || "");
    if (assignAgent && !ownerPassword) {
      setHint("Enter your account password to confirm assigning this to an agent.", "bad");
      agentPasswordInput?.focus();
      return;
    }
    // Swap each attached image's short placeholder back out for its real
    // data: URL now, right before signing — the signed content hash has to
    // cover exactly what gets sent.
    let body = String(bodyInput?.value || "");
    const images = form._pendingIssueImages || [];
    for (const img of images) body = body.split(img.id).join(img.dataUrl);
    if (submit) submit.disabled = true;
    setHint("Signing and sending…");
    try {
      await submitWebIssue(repo, title, body, assignAgent, ownerPassword);
      // Submissions land in the maintainer's inbox, not the public mirror, so it
      // won't be visible there until they drain it — but show it locally, on
      // top of this session's issue list, so the submitter sees it right away.
      state.issuesView.items = [{
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
      }, ...state.issuesView.items];
      setRepoTabCount("issues", state.issuesView.items.filter((issue) => issue.status === "open").length);
      if (titleInput) titleInput.value = "";
      if (bodyInput) bodyInput.value = "";
      if (agentPasswordInput) agentPasswordInput.value = "";
      images.length = 0;
      form.querySelector("[data-repo-issue-attachments]")?.replaceChildren();
      if (submit) submit.disabled = false;
      setHint("Issue sent to the maintainer's inbox for review. Submit another or go back.", "good");
    } catch (error) {
      if (submit) submit.disabled = false;
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "The maintainer's inbox is full. Try again later."
          : code === "author_quota" ? "You've reached the submission limit for this repository."
          : code === "issue_too_large" ? "The description is too large - please shorten it or attach smaller images."
          : code === "bad_owner_password" ? "That account password isn't correct."
          : code === "not_authorized" ? "Only the repository owner or an admin can assign issues to an agent."
          : code === "too_many_attempts" ? "Too many password attempts. Try again later."
          : "Could not send the issue. Please try again.",
        "bad");
    }
  }

  async function loadRepoCommits(repo) {
    const container = $("[data-repo-commits]");
    if (!container) return;
    container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading commits from the live mirror...</div>';
    try {
      const data = await fetchJson(repoLiveUrl(repo, "history"));
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

  function renderRepoCommitDiff(diff, imageDiffs) {
    if (!String(diff || "").trim()) return '<div class="px-4 py-3 text-sm text-muted-foreground">No textual diff is available for this commit.</div>';
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
        ${data.truncated ? '<div data-repo-commit-truncated class="rounded-md border border-amber-500/30 bg-amber-500/10 px-4 py-3 text-sm text-amber-200">Large diff truncated by the live desktop host.</div>' : '<div data-repo-commit-truncated class="hidden"></div>'}
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="git-compare-arrows" class="h-3.5 w-3.5 text-primary"></i>Diff</div>
          ${renderRepoCommitDiff(data.diff, data.imageDiffs)}
        </section>
      </article>`;
  }

  async function loadRepoCommitDetail(repo, hash) {
    const container = $("[data-repo-commits]");
    if (!container || !repo || !hash) return;
    container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading commit from the live mirror...</div>';
    try {
      const data = await fetchJson(repoLiveUrl(repo, "commit", { path: hash }));
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
    return `
        <div class="${rowClass}">
          <i data-lucide="${online ? "radio" : "circle"}" class="mt-0.5 h-4 w-4 ${online ? "text-primary" : "text-muted-foreground"}"></i>
          <span class="min-w-0 truncate text-foreground font-mono">${escapeHtml(mirror.owner || mirror.node || mirror.name || "mirror")}</span>
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
    if (container) container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading mirrors...</div>';
    try {
      const data = await fetchJson(`${repoApiBase(repo)}/mirrors`);
      const mirrors = Array.isArray(data.mirrors) ? data.mirrors : [];
      const mirrorCount = normalizedCount(data.summary?.mirrors) ?? mirrors.length;
      updateRepoLiveCounts(repo, { mirrors: mirrorCount });
      setRepoTabCount("mirrors", mirrors.length);
      state.repoMirrors = mirrors;
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
    container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading releases from the live mirror...</div>';
    const empty = '<div class="px-4 py-3 text-sm text-muted-foreground">No releases have been published to this mirror yet.</div>';
    try {
      // Release manifests live in the git tree at releases/<channel>/release.json
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
      const paths = channels.map((channel) => `releases/${channel}/release.json`);
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
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Releases are unavailable until a live desktop host serves the releases/ folder.</div>';
    } finally {
      window.lucide?.createIcons();
    }
  }

  function loadRepoFeaturePanels(repo) {
    loadRepoCommits(repo);
    loadRepoMirrors(repo);
    // Issue/PR/discussion lists load on their FIRST tab view (and reload here
    // after a repo/branch switch): fetching them eagerly for every repo open
    // fired blob reads for tabs nobody was looking at. The tab badges stay
    // filled meanwhile from the root tree's bundled counts.
    state.loadedRepoTabs = {};
    const active = state.activeRepoTab || "code";
    if (active === "issues") {
      state.loadedRepoTabs.issues = true;
      loadRepoIssues(repo);
    } else if (active === "pulls" || active === "discussions") {
      state.loadedRepoTabs[active] = true;
      loadRepoCollection(repo, active, `[data-repo-${active}]`);
    } else if (active === "releases") {
      state.loadedRepoTabs.releases = true;
      loadRepoReleases(repo);
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
      const data = await fetchJson(`${repoApiBase(repo)}/branches`);
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
        <div class="mt-4 grid gap-3">
          <div data-repo-collection-toolbar="${kind}" class="grid gap-2 lg:grid-cols-[auto_minmax(0,1fr)]">
            <label class="inline-flex h-9 min-w-0 items-center overflow-hidden rounded-md border border-border bg-background text-xs">
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
                <input data-repo-filter-query="${kind}" type="search" spellcheck="false" value="${isPulls ? "is:pr is:open" : "is:issue is:open"}" placeholder="${kind === "pulls" ? "is:pr is:open" : "is:issue is:open"}" class="min-w-0 flex-1 bg-transparent font-mono text-xs text-foreground outline-none placeholder:text-muted-foreground" />
              </span>
            </label>
            <div class="flex min-w-0 flex-wrap items-center gap-2 lg:justify-end">
              ${filters.map(([label, options]) => renderRepoCollectionFilter(label, options)).join("")}
            </div>
          </div>
          <div class="overflow-hidden rounded-lg border border-border bg-background">
            <div class="flex min-w-0 flex-wrap items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
              <div class="flex min-w-0 flex-wrap items-center gap-3 text-xs">
                <span class="inline-flex items-center gap-2 font-semibold text-foreground"><i data-lucide="${icon}" class="h-3.5 w-3.5 text-primary"></i><span data-repo-collection-open-count="${kind}">${tabCountLabel(openCount)}</span> Open</span>
                <span class="inline-flex items-center gap-2 text-muted-foreground"><i data-lucide="check" class="h-3.5 w-3.5"></i><span data-repo-collection-closed-count="${kind}">${tabCountLabel(closedCount)}</span> Closed</span>
              </div>
              ${kind === "issues" && state.session?.nodeName
                ? `<button type="button" data-repo-issue-new class="inline-flex h-7 items-center gap-1.5 rounded-md border border-primary/40 bg-primary/10 px-2.5 text-[11px] font-medium text-primary transition-colors hover:bg-primary/20"><i data-lucide="plus" class="h-3.5 w-3.5"></i>New issue</button>`
                : `<span class="text-[10px] text-muted-foreground">Create from desktop client for signed submissions</span>`}
            </div>
            <div data-repo-${kind}></div>
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
    // Pull requests and discussions load lazily the first time their tab is
    // opened rather than on every page load. Eagerly fetching every record's
    // blob up front is what flooded the host with requests and tripped the rate
    // limit after a few refreshes; this map remembers which tabs have loaded.
    state.loadedRepoTabs = {};
    // Show the clean, shareable /owner/name URL in the address bar instead of
    // the /dashboard?repo=... target that 404.html bounces repo links to (and
    // instead of a bare /dashboard when the repo is opened from the list). Leave
    // a URL that already points inside this repo (e.g. an /owner/name/tree/...
    // deep link) untouched so the tree/blob restore below still sees its path.
    const detailPath = repoPathUrl(repo);
    if (!location.pathname.startsWith(detailPath)) {
      navigateHistory(detailPath);
    }
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
    };
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
            ${["code", "commits", "releases", "issues", "pulls", "discussions", "mirrors"].map((tab) => {
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
    const parts = repoRouteParts();
    const kind = parts[2];
    const path = parts.length > 3 ? parts.slice(3).map(decodeURIComponent).join("/") : "";
    // Restore whichever tab the URL points at (e.g. a refresh on
    // /owner/repo/issues) instead of always defaulting back to Code.
    setRepoTab(REPO_TAB_ROUTES.includes(kind) ? kind : "code");
    loadRepositoryTree(repo, kind === "tree" ? path : "");
    if (kind === "blob" && path) loadRepositoryBlob(repo, path);
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
          <div class="px-4 py-3 flex items-center gap-4 hover:bg-secondary/50 transition-colors">
            <div class="flex items-center gap-2 flex-1 min-w-0">
              <i data-lucide="circle" class="${nodeDotClass(row, "w-2 h-2")}"></i>
              <span class="text-sm ${row.online ? "text-foreground" : "text-muted-foreground"} font-medium truncate font-mono">${escapeHtml(row.name || "node")}</span>
            </div>
            <span class="text-xs text-muted-foreground w-20 text-right font-mono">${escapeHtml(nodeMetaLabel(row))}</span>
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
        nodeRows.set(name.toLowerCase(), {
          name,
          minutes: Number(row.minutes) || 0,
          online: onlineSet.has(name.toLowerCase()),
        });
      });
      onlineNames.forEach((name) => {
        const clean = String(name || "").trim();
        if (!clean) return;
        const key = clean.toLowerCase();
        if (!nodeRows.has(key)) nodeRows.set(key, { name: clean, minutes: 0, online: true });
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

  async function init() {
    const session = readSession();
    state.session = session;
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
  });

  document.addEventListener("submit", (event) => {
    const issueForm = event.target.closest("[data-repo-issue-form]");
    if (issueForm && state.selectedRepo) {
      event.preventDefault();
      handleIssueComposeSubmit(state.selectedRepo, issueForm);
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
      if (REPO_TAB_ROUTES.includes(kind)) {
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
