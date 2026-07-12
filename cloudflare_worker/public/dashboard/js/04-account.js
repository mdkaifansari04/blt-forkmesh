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
          const data = await fetchJson(repoLiveUrl(repo, "history"));
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
