  function repositoryMatchesQuery(repo, query) {
    if (!query) return true;
    const canonical = repoCanonicalIdentity(repo);
    const haystack = [
      repo.owner,
      repo.name,
      canonical.owner,
      canonical.name,
      // Cards are labelled with the logical owner, so filtering by the
      // organization (or account) name has to match too.
      ...repoLogicalOwners(repo).map((value) => value.owner),
      repo.description,
      repo.channel,
      repo.source,
    ].join(" ").toLowerCase();
    return haystack.includes(query);
  }

  function repositoryTermsBadge(repo, compact = false) {
    if (repo?.termsFlagged !== true) return "";
    const category = String(repo.termsCategory || "policy").replace(
      /[^a-z-]/gi,
      "",
    ).slice(0, 20);
    return `<span title="This repository has an active ForkMesh Terms of Service moderation flag${category ? `: ${escapeHtml(category)}` : ""}." class="inline-flex shrink-0 items-center gap-1 rounded-full border border-destructive/60 bg-destructive/10 ${compact ? "px-1 py-0.5 text-[8px]" : "px-2 py-0.5 text-[10px]"} font-semibold text-destructive"><i data-lucide="flag" class="${compact ? "h-2.5 w-2.5" : "h-3 w-3"}"></i>${compact ? "ToS" : "Terms flag"}</span>`;
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
            <span class="block truncate font-medium text-foreground">${escapeHtml(`${groupDisplayOwner(group)}/${repo.name || ""}`)}</span>
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

  const nativeRepositoryLogoCache = new Map();
  let nativeRepositoryLogoSessionToken = "";
  let nativeRepositoryLogoSessionEpoch = 0;

  function nativeRepositoryLogoEndpoint(repo) {
    const owner = String(repo?.owner || "").trim();
    const name = String(repo?.name || "").trim();
    if (!owner || !name) return "";
    return `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(name)}/logo`;
  }

  function nativeRepositoryLogoMarkup(repo, sizeClass = "h-10 w-10") {
    const endpoint = nativeRepositoryLogoEndpoint(repo);
    if (!endpoint) return "";
    return `
      <span data-native-repo-logo-frame class="relative flex ${sizeClass} shrink-0 items-center justify-center overflow-hidden rounded-xl border border-border bg-secondary text-muted-foreground">
        <i data-native-repo-logo-fallback data-lucide="book-marked" class="h-4 w-4"></i>
        <img data-native-repo-logo data-logo-endpoint="${escapeHtml(endpoint)}" alt="" loading="lazy" decoding="async" class="absolute inset-0 hidden h-full w-full object-cover" />
      </span>
    `;
  }

  function nativeRepositoryLogoDataUrl(value) {
    const url = String(value || "");
    return (
      /^data:image\/(?:svg\+xml|png|jpeg|webp)(?:;|,)/i.test(url)
      || /^\/api\/repo\/[^/?#]+\/[^/?#]+\/raw\?[^#]+$/i.test(url)
    )
      ? url
      : "";
  }

  function nativeRepositoryLogoCacheKey(endpoint) {
    const token = String(state.session?.sessionToken || "");
    if (token !== nativeRepositoryLogoSessionToken) {
      // A guest miss must not hide a private logo after login, and an
      // owner-authorized logo must never survive logout/account switching.
      nativeRepositoryLogoCache.clear();
      nativeRepositoryLogoSessionToken = token;
      nativeRepositoryLogoSessionEpoch += 1;
    }
    return `${nativeRepositoryLogoSessionEpoch}:${endpoint}`;
  }

  function loadNativeRepositoryLogo(endpoint) {
    const cacheKey = nativeRepositoryLogoCacheKey(endpoint);
    if (!nativeRepositoryLogoCache.has(cacheKey)) {
      const token = String(state.session?.sessionToken || "");
      const headers = { Accept: "application/json" };
      if (token) headers.Authorization = `Bearer ${token}`;
      const pending = fetch(endpoint, {
        headers,
        credentials: "same-origin",
      }).then(async (response) => {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const body = await response.json();
        if (String(state.session?.sessionToken || "") !== token) return null;
        return {
          dataUrl: nativeRepositoryLogoDataUrl(body?.logo?.dataUrl),
          // The repository's own root logo streams through a mirror, so it can
          // fail long after the card rendered. The generated/approved artwork
          // ships with it so the card swaps instead of showing a broken image.
          fallbackDataUrl: nativeRepositoryLogoDataUrl(
            body?.logo?.fallbackDataUrl),
        };
      }).catch(() => {
        // Do not negatively cache authorization failures or transient errors.
        // The same card can be retried after login or on a later render.
        if (nativeRepositoryLogoCache.get(cacheKey) === pending) {
          nativeRepositoryLogoCache.delete(cacheKey);
        }
        return null;
      });
      nativeRepositoryLogoCache.set(cacheKey, pending);
    }
    return nativeRepositoryLogoCache.get(cacheKey);
  }

  function showNativeRepositoryLogo(image) {
    image.classList.remove("hidden");
    image.parentElement?.querySelector("[data-native-repo-logo-fallback]")
      ?.classList.add("hidden");
  }

  function hideNativeRepositoryLogo(image) {
    image.classList.add("hidden");
    image.removeAttribute("src");
    image.parentElement?.querySelector("[data-native-repo-logo-fallback]")
      ?.classList.remove("hidden");
  }

  function hydrateNativeRepositoryLogos(root) {
    if (!root) return;
    root.querySelectorAll("[data-native-repo-logo]").forEach(async (image) => {
      const endpoint = image.getAttribute("data-logo-endpoint") || "";
      if (!endpoint || image.dataset.logoHydrated === "true") return;
      image.dataset.logoHydrated = "true";
      const logo = await loadNativeRepositoryLogo(endpoint);
      const dataUrl = String(logo?.dataUrl || "");
      const fallbackDataUrl = String(logo?.fallbackDataUrl || "");
      if (!dataUrl || !image.isConnected) return;
      // The committed root logo is served by a mirror, so it can fail after the
      // card rendered (offline or lagging host). Swap to the generated artwork,
      // then to the repository icon — never leave a broken image behind.
      image.onerror = () => {
        if (fallbackDataUrl && image.dataset.logoFallbackUsed !== "true") {
          image.dataset.logoFallbackUsed = "true";
          image.src = fallbackDataUrl;
          return;
        }
        image.onerror = null;
        hideNativeRepositoryLogo(image);
      };
      image.src = dataUrl;
      showNativeRepositoryLogo(image);
    });
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
    const displayOwner = groupDisplayOwner(group);
    const servedNote = displayOwner.toLowerCase() !== String(origin.owner || "").toLowerCase()
      ? ` (published from ${origin.owner || "a node"})`
      : "";
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
      <div data-repo="${escapeHtml(key.toLowerCase())}" data-dashboard-open-repo="${escapeHtml(key)}" data-clone-url="${escapeHtml(cloneUrl(origin))}" role="link" tabindex="0" aria-label="Open ${escapeHtml(`${displayOwner || "owner"}/${origin.name || "repository"}`)}" class="repo-card group cursor-pointer px-4 py-3 hover:bg-secondary/40 transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary/60">
        <div class="repo-layout grid gap-3 md:grid-cols-[minmax(0,1fr)_minmax(10rem,12rem)] md:items-center">
          <div class="flex min-w-0 items-start gap-3">
            ${nativeRepositoryLogoMarkup(origin)}
            <div class="min-w-0 flex-1">
            <div class="flex min-w-0 items-center gap-2">
              <p class="min-w-0 truncate text-sm font-medium text-foreground" title="${escapeHtml(`${displayOwner || "owner"}/${origin.name || "repository"}${servedNote}`)}">
                <span class="text-muted-foreground">${escapeHtml(displayOwner || "owner")}/</span>${escapeHtml(origin.name || "repository")}
              </p>
              <span class="shrink-0 rounded-full border border-border px-2 py-0.5 text-[10px] font-mono ${statusClass}">
                ${statusText}
              </span>
              ${repositoryTermsBadge(origin)}
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
                <i data-lucide="radio" class="w-3 h-3"></i>${nodeCount > 1 ? `${nodeCount} mirrors` : (viaMirror ? "served by mirror" : live ? "mirror online" : "mirror offline")}
              </span>
              <span class="text-xs text-muted-foreground font-mono">updated ${escapeHtml(formatDate(repo.updatedAt || repo.lastSync))}</span>
            </div>
            <div class="mt-3 grid grid-cols-2 gap-1.5 sm:grid-cols-4">${metrics}</div>
            </div>
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
      <a href="${escapeHtml(repoPathUrl(repo))}" class="flex min-w-0 items-start gap-3 text-left">
        ${nativeRepositoryLogoMarkup(repo, "h-12 w-12")}
        <span class="block min-w-0 flex-1">
        <span class="flex min-w-0 flex-wrap items-center gap-2">
          <span class="min-w-0 truncate text-lg font-semibold text-accent hover:underline">${escapeHtml(repo.name || "repository")}</span>
          <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-mono text-muted-foreground">${escapeHtml(visibility)}</span>
          ${repositoryTermsBadge(repo)}
        </span>
        <span class="mt-1 block text-xs text-muted-foreground">Published from ${escapeHtml(repo.owner || "owner")}/${escapeHtml(repo.name || "repository")}</span>
        <span class="mt-2 line-clamp-2 text-sm text-muted-foreground">${escapeHtml(repo.description || "No description published.")}</span>
        <span class="mt-3 flex flex-wrap items-center gap-4 text-xs text-muted-foreground">
          <span class="inline-flex items-center gap-1.5"><span data-repo-language-dot class="h-2 w-2 rounded-full bg-primary"></span>${escapeHtml(language)}</span>
          <span class="inline-flex items-center gap-1.5"><i data-lucide="scale" class="h-3.5 w-3.5"></i>${escapeHtml(license)}</span>
          <span>Updated ${escapeHtml(formatDate(repo.updatedAt || repo.lastSync))}</span>
        </span>
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
    hydrateNativeRepositoryLogos(list);
  }

  function renderSidebarRepositories(session) {
    const list = $("[data-sidebar-repo-list]");
    const count = $("[data-sidebar-repo-count]");
    const groups = Array.isArray(state.filteredGroups)
      ? state.filteredGroups
      : groupRepositories(state.repositories || []);
    const organizationRepositories = Array.isArray(
      state.homeOrganizationRepositories,
    )
      ? state.homeOrganizationRepositories
      : [];
    const entries = [
      ...organizationRepositories.map((repo) => ({
        repo,
        key: `${repo.owner}/${repo.name}`,
        href: `/${encodeURIComponent(repo.owner)}/${encodeURIComponent(repo.name)}`,
        organization: true,
      })),
      ...groups.map((group) => {
        const repo = sourceOfTruth(group);
        return {
          repo,
          key: repoKey(repo),
          href: repoPathUrl(repo),
          organization: false,
        };
      }),
    ].filter((entry, index, values) =>
      values.findIndex((candidate) =>
        candidate.key.toLowerCase() === entry.key.toLowerCase()) === index);
    if (count) count.textContent = formatCount(entries.length);
    if (!list) return;
    if (!entries.length) {
      list.innerHTML = '<div class="rounded-md border border-border bg-background px-2.5 py-2 text-xs text-muted-foreground">No mirrored repositories yet.</div>';
      return;
    }
    list.innerHTML = entries.slice(0, 12).map((entry) => {
      const repo = entry.repo;
      const live = repoIsLive(repo);
      return `
        <a href="${escapeHtml(entry.href)}" class="group flex min-w-0 items-center gap-2 rounded-md px-2.5 py-2 text-left text-xs text-muted-foreground transition-colors hover:bg-secondary hover:text-foreground">
          <span class="h-2 w-2 shrink-0 rounded-full ${live ? "bg-primary" : "bg-muted-foreground/40"}"></span>
          <span class="min-w-0 flex-1 truncate"><span class="text-muted-foreground">${escapeHtml(repoDisplayOwner(repo) || "owner")}/</span><span class="text-foreground">${escapeHtml(repo.name || "repository")}</span></span>
          ${repositoryTermsBadge(repo, true)}
          ${entry.organization ? '<span class="shrink-0 rounded border border-border px-1 py-0.5 font-mono text-[8px] uppercase text-muted-foreground">org</span>' : ""}
        </a>`;
    }).join("");
  }

  function renderHomeRepositories() {
    const container = $("[data-home-top-repositories]");
    if (!container) return;
    const query = ($("[data-home-repo-search]")?.value || "").trim().toLowerCase();
    const entries = [
      ...(Array.isArray(state.homeOrganizationRepositories)
        ? state.homeOrganizationRepositories
        : []).map((repo) => ({
          repo,
          href: `/${encodeURIComponent(repo.owner)}/${encodeURIComponent(repo.name)}`,
          organization: true,
        })),
      ...groupRepositories(state.repositories || []).map((group) => {
        const repo = sourceOfTruth(group);
        return { repo, href: repoPathUrl(repo), organization: false };
      }),
    ].filter((entry) => repositoryMatchesQuery(entry.repo, query))
      .filter((entry, index, values) =>
        values.findIndex((candidate) =>
          repoKey(candidate.repo).toLowerCase() ===
          repoKey(entry.repo).toLowerCase()) === index)
      .slice(0, 8);
    container.innerHTML = `
      ${entries.length
        ? `<div class="grid gap-1">${entries.map((entry) => {
            const repo = entry.repo;
            return `<a href="${escapeHtml(entry.href)}" class="flex min-w-0 items-center gap-2 rounded-md px-2 py-1.5 text-left text-sm text-muted-foreground hover:bg-secondary hover:text-foreground">
              <i data-lucide="${entry.organization ? "building-2" : "book-marked"}" class="h-3.5 w-3.5 shrink-0"></i>
              <span class="min-w-0 truncate">${escapeHtml(repoDisplayKey(repo))}</span>
              ${repositoryTermsBadge(repo, true)}
              ${entry.organization ? '<span class="ml-auto shrink-0 text-[9px] uppercase text-muted-foreground">organization</span>' : ""}
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
    const key = `${groupDisplayOwner(group)}/${repo.name || ""}`;
    const live = repoIsLive(repo);
    const viaMirror = repoServedByMirror(repo);
    const description = repo.description || "No description published.";
    const updated = repo.updatedAt || repo.lastSync;
    return `
      <article class="overflow-hidden rounded-lg border border-border bg-card">
        <div class="flex min-w-0 items-start gap-3 px-4 py-4">
          ${nativeRepositoryLogoMarkup(repo)}
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
    hydrateNativeRepositoryLogos(container);
  }

  // Home right-rail "Latest from the blog" (adhoc #381): the newest feature
  // posts with their artwork, read from the blog's own RSS document. The feed
  // is derived from the shipped blog index and edge-cached for thirty minutes
  // (see blog_feed.py), so this is one cheap same-origin read per dashboard
  // load rather than a second hand-maintained copy of the post list.
  const HOME_BLOG_POST_LIMIT = 3;
  const HOME_BLOG_FEED_URL = "/rss.xml";

  // Feed URLs are absolute against forkmesh.com; keep only the path so the
  // dashboard links and paints artwork from whatever origin it is served on
  // (and never loads an image from a foreign host).
  function homeBlogUrl(value) {
    const raw = String(value || "").trim();
    if (!raw) return "";
    try {
      const parsed = new URL(raw, window.location.origin);
      return parsed.pathname + parsed.search;
    } catch (_) {
      return "";
    }
  }

  function parseHomeBlogFeed(xml) {
    const doc = new DOMParser().parseFromString(String(xml || ""), "application/xml");
    if (doc.querySelector("parsererror")) return [];
    return Array.from(doc.querySelectorAll("item"))
      .map((item) => ({
        title: (item.querySelector("title")?.textContent || "").trim(),
        href: homeBlogUrl(item.querySelector("link")?.textContent),
        meta: (item.querySelector("category")?.textContent || "").trim(),
        image: homeBlogUrl(item.querySelector("enclosure")?.getAttribute("url")),
      }))
      .filter((post) => post.title && post.href)
      .slice(0, HOME_BLOG_POST_LIMIT);
  }

  function homeBlogPostCard(post) {
    return `
      <a href="${escapeHtml(post.href)}" class="group block overflow-hidden rounded-md border border-border hover:bg-secondary">
        ${post.image ? `<img src="${escapeHtml(post.image)}" alt="" loading="lazy" class="block aspect-[16/9] w-full object-cover" />` : ""}
        <span class="block px-3 py-2.5">
          ${post.meta ? `<span class="block truncate font-mono text-[10px] uppercase text-muted-foreground">${escapeHtml(post.meta)}</span>` : ""}
          <span class="mt-1 block text-sm font-semibold leading-5 text-foreground group-hover:text-accent">${escapeHtml(post.title)}</span>
        </span>
      </a>
    `;
  }

  function renderHomeBlogPosts() {
    const container = $("[data-home-blog-list]");
    if (!container) return;
    const posts = state.homeBlogPosts;
    if (posts === null) {
      container.innerHTML = '<div class="text-sm text-muted-foreground"><span class="fm-spinner" aria-hidden="true"></span>Loading blog posts...</div>';
      return;
    }
    container.innerHTML = posts.length
      ? posts.map(homeBlogPostCard).join("")
      : '<div class="text-sm text-muted-foreground">Blog posts are unavailable right now.</div>';
  }

  async function loadHomeBlogPosts() {
    if (!$("[data-home-blog-list]")) return;
    try {
      const response = await fetch(HOME_BLOG_FEED_URL, {
        credentials: "omit",
        headers: { accept: "application/rss+xml, application/xml" },
      });
      if (!response.ok) throw new Error(`blog feed returned ${response.status}`);
      state.homeBlogPosts = parseHomeBlogFeed(await response.text());
    } catch (_) {
      state.homeBlogPosts = [];
    }
    renderHomeBlogPosts();
  }

  async function loadHomeOrganizationRepositories() {
    if (!state.session?.sessionToken) {
      state.homeOrganizationRepositories = [];
      return;
    }
    try {
      const organizations = await fetchJson("/api/orgs");
      const orgs = Array.isArray(organizations?.orgs)
        ? organizations.orgs.slice(0, 20)
        : [];
      const linked = await Promise.all(orgs.map(async (organization) => {
        const owner = String(organization?.name || "").trim().toLowerCase();
        if (!owner) return [];
        try {
          const data = await fetchJson(
            `/api/orgs/${encodeURIComponent(owner)}/repos`,
          );
          return (Array.isArray(data?.repos) ? data.repos : []).map((item) => ({
            owner,
            name: String(item?.repo || "").trim(),
            linkedNode: String(item?.node || "").trim(),
            source: "organization-alias",
            organizationOwned: true,
            cloneOnline: true,
          })).filter((repo) => repo.name);
        } catch (_) {
          return [];
        }
      }));
      state.homeOrganizationRepositories = linked.flat();
    } catch (_) {
      state.homeOrganizationRepositories = [];
    }
    renderSidebarRepositories(state.session);
    renderHomeRepositories();
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
      const aliases = profileRepositoryAliases(state.publicProfile);
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
    hydrateNativeRepositoryLogos(container);
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
    renderHomeBlogPosts();
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
    renderGlobalSearchResults();
  }

  function externalRepositoryCard(repository) {
    const logo = String(repository?.logo?.dataUrl || "");
    const hosted = Boolean(repository?.mirrored && repository?.mirror?.owner && repository?.mirror?.name);
    const status = hosted
      ? "Fully hosted by ForkMesh"
      : String(repository?.statusLabel || "External repository");
    const provider = String(repository?.attribution?.provider || repository?.provider || "Provider");
    const original = String(repository?.originalUrl || "");
    const forkmeshUrl = hosted
      ? `/${encodeURIComponent(repository.targetOwner)}/${encodeURIComponent(repository.name)}`
      : original;
    const canVolunteer = Boolean(state.session?.sessionToken) &&
      !["actively_mirrored", "archived"].includes(String(repository?.status || ""));
    const manageable = Boolean(repository?.canManage);
    const selected = state.externalRepositorySelection.has(String(repository?.id || ""));
    const incomplete = Array.isArray(repository?.metadataIncomplete) && repository.metadataIncomplete.length
      ? `<p class="mt-2 text-[11px] text-amber-300">Some metadata was unavailable or rate-limited during import.</p>`
      : "";
    return `
      <article class="flex flex-col gap-3 px-4 py-4 sm:flex-row sm:items-start sm:px-5" data-external-repo-id="${escapeHtml(repository?.id || "")}">
        ${manageable ? `<label class="mt-3 inline-flex shrink-0 items-center" title="Select ${escapeHtml(repository?.fullName || repository?.name || "repository")}">
          <input data-external-repo-select="${escapeHtml(repository?.id || "")}" type="checkbox" ${selected ? "checked" : ""} class="h-4 w-4 rounded border-border bg-card accent-primary" />
          <span class="sr-only">Select ${escapeHtml(repository?.fullName || repository?.name || "repository")}</span>
        </label>` : ""}
        <img src="${escapeHtml(logo)}" alt="" class="h-12 w-12 shrink-0 rounded-xl border border-border bg-secondary object-cover" />
        <div class="min-w-0 flex-1">
          <div class="flex flex-wrap items-center gap-2">
            <a href="${escapeHtml(forkmeshUrl)}" ${hosted ? "" : 'target="_blank" rel="noopener noreferrer"'} class="truncate font-mono text-sm font-semibold text-foreground hover:text-primary hover:underline">${escapeHtml(repository?.fullName || repository?.name || "External repository")}</a>
            <span class="rounded-full border border-border bg-secondary px-2 py-0.5 text-[10px] font-semibold text-muted-foreground">${escapeHtml(status)}</span>
            ${repository?.isPrivate ? '<span class="rounded-full border border-border bg-secondary px-2 py-0.5 text-[10px] font-semibold text-muted-foreground">Private · owner only</span>' : ""}
          </div>
          <p class="mt-1 text-xs leading-5 text-muted-foreground">${escapeHtml(repository?.metadata?.description || "No provider description.")}</p>
          <p class="mt-2 text-[11px] leading-4 text-muted-foreground">${escapeHtml(hosted ? "The complete Git repository is synced, cloneable, and hosted by ForkMesh." : repository?.mirrorNotice || "This external entry is not mirrored by ForkMesh.")}</p>
          ${incomplete}
          <div class="mt-3 flex flex-wrap items-center gap-2 text-[11px] text-muted-foreground">
            <a href="${escapeHtml(original)}" target="_blank" rel="noopener noreferrer" class="font-medium text-primary hover:underline">Open on ${escapeHtml(provider)}</a>
            ${repository?.targetOwner ? `<span aria-hidden="true">·</span><span>Listed under ${repository?.targetOwnerType === "organization" ? "organization " : ""}<span class="font-mono text-foreground">${escapeHtml(repository.targetOwner)}</span></span>` : ""}
            <span aria-hidden="true">·</span>
            <span>${hosted ? "Full Git data hosted by ForkMesh" : "ForkMesh does not own or control this repository"}</span>
          </div>
        </div>
        ${canVolunteer ? `
          <button type="button" data-external-mirror-volunteer="${escapeHtml(repository?.id || "")}" class="inline-flex h-8 shrink-0 items-center justify-center gap-1.5 rounded-md border border-border px-3 text-xs font-semibold text-foreground hover:bg-secondary">
            <i data-lucide="copy-plus" class="h-3.5 w-3.5"></i>
            Volunteer to mirror
          </button>` : ""}
      </article>`;
  }

  function renderExternalRepositories(repositories) {
    state.externalRepositoriesLoading = false;
    state.externalRepositories = Array.isArray(repositories) ? repositories : [];
    const list = $("[data-external-repo-list]");
    const count = $("[data-external-repo-count]");
    if (count) count.textContent = `${formatCount(state.externalRepositories.length)} external`;
    if (!list) return;
    list.innerHTML = state.externalRepositories.length
      ? state.externalRepositories.map(externalRepositoryCard).join("")
      : '<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">No external repositories or stubs have been listed yet. Use “New repository” to import one.</div>';
    syncExternalRepositoryActions();
    window.lucide?.createIcons();
  }

  function syncExternalRepositoryActions() {
    const manageable = state.externalRepositories.filter((repository) => repository?.canManage);
    const manageableIds = new Set(manageable.map((repository) => String(repository.id || "")));
    for (const id of [...state.externalRepositorySelection]) {
      if (!manageableIds.has(id)) state.externalRepositorySelection.delete(id);
    }
    const actions = $("[data-external-repo-actions]");
    actions?.classList.toggle("hidden", manageable.length === 0);
    actions?.classList.toggle("flex", manageable.length > 0);
    const all = $("[data-external-repo-select-all]");
    if (all) {
      all.checked = manageable.length > 0 &&
        manageable.every((repository) => state.externalRepositorySelection.has(String(repository.id || "")));
      all.indeterminate = state.externalRepositorySelection.size > 0 && !all.checked;
    }
    const button = $("[data-external-repo-delete-selected]");
    if (button) {
      const count = state.externalRepositorySelection.size;
      button.disabled = count === 0;
      button.lastChild.textContent = count ? ` Delete selected (${count})` : " Delete selected";
    }
  }

  function toggleAllExternalRepositories(checked) {
    state.externalRepositorySelection.clear();
    if (checked) {
      state.externalRepositories
        .filter((repository) => repository?.canManage)
        .forEach((repository) => state.externalRepositorySelection.add(String(repository.id || "")));
    }
    renderExternalRepositories(state.externalRepositories);
  }

  async function deleteSelectedExternalRepositories() {
    const ids = [...state.externalRepositorySelection];
    if (!ids.length || !state.session?.sessionToken) return;
    if (!window.confirm(`Delete ${ids.length} selected external ${ids.length === 1 ? "repository" : "repositories"}? This removes ForkMesh metadata and does not delete anything from the source provider.`)) return;
    const button = $("[data-external-repo-delete-selected]");
    if (button) button.disabled = true;
    try {
      for (const id of ids) {
        const response = await fetch(`/api/repository-imports/${encodeURIComponent(id)}`, {
          method: "DELETE",
          body: JSON.stringify({ sessionToken: state.session.sessionToken }),
          headers: {
            accept: "application/json",
            authorization: `Bearer ${state.session.sessionToken}`,
            "content-type": "application/json",
          },
          cache: "no-store",
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
        state.externalRepositorySelection.delete(id);
      }
      await loadExternalRepositories({ fresh: true });
    } catch (error) {
      setNewRepoHint(String(error?.message || "Could not delete selected imports."), "bad");
      await loadExternalRepositories({ fresh: true });
    }
  }

  async function loadExternalRepositories({ fresh = false } = {}) {
    const list = $("[data-external-repo-list]");
    if (!list) return;
    state.externalRepositoriesLoading = true;
    try {
      const data = await fetchJson("/api/repository-imports", { fresh });
      renderExternalRepositories(data.repositories);
    } catch (_) {
      state.externalRepositoriesLoading = false;
      const count = $("[data-external-repo-count]");
      if (count) count.textContent = "Unavailable";
      list.innerHTML = '<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">External repository metadata is temporarily unavailable.</div>';
    }
  }

  function externalMirrorNodeName() {
    const nodes = Array.isArray(state.session?.nodes) ? state.session.nodes : [];
    const first = nodes.find((node) => String(node?.name || node || "").trim());
    return String(first?.name || first || state.session?.nodeName || "").trim();
  }

  async function volunteerForExternalMirror(repositoryId, button) {
    if (!state.session?.sessionToken) {
      setNewRepoHint("Sign in before volunteering a mirror node.", "bad");
      return;
    }
    const node = externalMirrorNodeName();
    if (!node) return;
    if (button) button.disabled = true;
    try {
      const response = await fetch(
        `/api/repository-imports/${encodeURIComponent(repositoryId)}/mirror-volunteers`,
        {
          method: "POST",
          headers: { "content-type": "application/json", accept: "application/json" },
          body: JSON.stringify({ node, sessionToken: state.session.sessionToken }),
        },
      );
      const body = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(body.error || `HTTP ${response.status}`);
      await loadExternalRepositories({ fresh: true });
    } catch (error) {
      if (button) {
        button.textContent = String(error?.message || "") === "eligible_owned_mirror_node_required"
          ? "Owned mirror node required"
          : "Could not volunteer";
      }
    } finally {
      if (button) button.disabled = false;
    }
  }

  // New-repository flow (adhoc #30). Publishing a signed catalog record and
  // running the git mirror both require the account's Ed25519 key, which lives
  // on the desktop node — the browser only holds a separate web-issue identity.
  // So this "Create & mirror" modal collects the repo details on the web, then
  // hands off the concrete steps to complete it in the desktop node's Repos
  // page, rather than pretending the pure-web path can publish.
  function setNewRepoModalOpen(open) {
    const modal = $("[data-new-repo-modal]");
    if (!modal) return;
    modal.classList.toggle("hidden", !open);
    modal.classList.toggle("flex", open);
    if (open) {
      setNewRepoHint("");
      $("[data-new-repo-steps]")?.classList.add("hidden");
      window.setTimeout(() => $("[data-new-repo-name]")?.focus(), 0);
      window.lucide?.createIcons();
    } else {
      const token = $("[data-new-repo-provider-token]");
      if (token) token.value = "";
    }
  }

  function newRepoSource() {
    return $('[data-new-repo-source][aria-pressed="true"]')?.dataset.newRepoSource || "remote";
  }

  function setNewRepoSource(source) {
    $$("[data-new-repo-source]").forEach((btn) => {
      const active = btn.dataset.newRepoSource === source;
      btn.setAttribute("aria-pressed", active ? "true" : "false");
      btn.classList.toggle("bg-secondary", active);
      btn.classList.toggle("text-foreground", active);
      btn.classList.toggle("text-muted-foreground", !active);
    });
    const value = $("[data-new-repo-source-value]");
    const hint = $("[data-new-repo-source-hint]");
    const provider = source === "provider";
    $("[data-new-repo-provider-options]")?.classList.toggle("hidden", !provider);
    $("[data-new-repo-name-wrap]")?.classList.toggle("hidden", provider);
    $("[data-new-repo-visibility-wrap]")?.classList.toggle("hidden", provider);
    $("[data-new-repo-description-wrap]")?.classList.toggle("hidden", provider);
    const name = $("[data-new-repo-name]");
    if (name) name.required = !provider;
    const submitLabel = $("[data-new-repo-submit-label]");
    if (submitLabel) submitLabel.textContent = provider ? "Import metadata" : "Create & mirror";
    if (!provider) {
      const token = $("[data-new-repo-provider-token]");
      if (token) token.value = "";
    }
    if (source === "local") {
      if (value) value.placeholder = "/home/you/code/my-project";
      if (hint) hint.textContent = "The desktop node reads this local repo directly — the path never leaves your machine.";
    } else if (provider) {
      if (value) value.placeholder = "https://github.com/owner/repo, https://gitlab.com/group/repo, or https://codeberg.org/owner/repo";
      if (hint) hint.textContent = "ForkMesh reads bounded metadata from the provider. Importing does not claim ownership or create a mirror.";
    } else {
      if (value) value.placeholder = "https://github.com/owner/repo.git";
      if (hint) hint.textContent = "ForkMesh clones this URL into a bare mirror you then keep in sync.";
    }
  }

  function setNewRepoHint(text, cls) {
    const hint = $("[data-new-repo-hint]");
    if (!hint) return;
    hint.textContent = text || "";
    hint.className = "min-h-4 text-xs " + (cls === "bad" ? "text-destructive" : cls === "good" ? "text-primary" : "text-muted-foreground");
  }

  function renderNewRepoSteps(details) {
    const list = $("[data-new-repo-steps-list]");
    const panel = $("[data-new-repo-steps]");
    if (!list || !panel) return;
    const sourceLabel = details.source === "local" ? "Local repository" : "Remote clone URL";
    const pick = details.source === "local" ? "Select the local repository" : "Paste the clone URL";
    const sourceValue = details.sourceValue
      ? ` (<span class="font-mono text-foreground">${escapeHtml(details.sourceValue)}</span>)` : "";
    const steps = [
      `Open the ForkMesh desktop node and go to the <span class="text-foreground">Repos</span> page.`,
      `Click <span class="text-foreground">+ Add</span>, then choose <span class="text-foreground">${sourceLabel}</span>.`,
      `${pick}${sourceValue} and name it <span class="font-mono text-foreground">${escapeHtml(details.name)}</span>.`,
      `Set visibility to <span class="text-foreground">${details.visibility === "private" ? "Private" : "Public"}</span>${details.description ? ` and add your description` : ""}.`,
      `Publish — the node mirrors it and it appears here in your repositories.`,
    ];
    list.innerHTML = steps
      .map((step, index) => `<li class="flex gap-2"><span class="shrink-0 font-mono text-foreground">${index + 1}.</span><span>${step}</span></li>`)
      .join("");
    panel.classList.remove("hidden");
    window.lucide?.createIcons();
  }

  async function handleNewRepoSubmit() {
    const source = newRepoSource();
    const sourceValue = String($("[data-new-repo-source-value]")?.value || "").trim();
    if (source === "provider") {
      if (!state.session?.sessionToken) {
        setNewRepoHint("Sign in to import a GitHub, GitLab, or Codeberg repository.", "bad");
        return;
      }
      if (!sourceValue) {
        setNewRepoHint("Enter a GitHub, GitLab, or Codeberg repository URL.", "bad");
        $("[data-new-repo-source-value]")?.focus();
        return;
      }
      const tokenInput = $("[data-new-repo-provider-token]");
      const providerToken = String(tokenInput?.value || "").trim();
      const mode = $("[data-new-repo-import-mode]")?.value === "stub" ? "stub" : "import";
      const submit = $("[data-new-repo-submit]");
      if (submit) submit.disabled = true;
      setNewRepoHint("Reading provider metadata…");
      try {
        const response = await fetch("/api/repository-imports", {
          method: "POST",
          headers: { "content-type": "application/json", accept: "application/json" },
          body: JSON.stringify({
            sourceUrl: sourceValue,
            providerToken,
            mode,
            sessionToken: state.session.sessionToken,
          }),
        });
        const body = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(body.error || `HTTP ${response.status}`);
        setNewRepoHint(
          `${body.repository?.statusLabel || "External repository"} created. It remains separate from live mirrors.`,
          "good",
        );
        await loadExternalRepositories({ fresh: true });
      } catch (error) {
        const code = String(error?.message || "");
        setNewRepoHint(
          code === "provider_rate_limited" ? "The provider rate limit was reached. Try again after its reset time."
            : code.includes("authorization") ? "The provider rejected access. Private repositories require a valid scoped token."
            : code === "unsupported_provider" ? "Use a github.com, gitlab.com, or codeberg.org repository URL."
            : "Could not import provider metadata.",
          "bad",
        );
      } finally {
        // The credential is request-only: remove it from the form immediately,
        // regardless of success or provider failure.
        if (tokenInput) tokenInput.value = "";
        if (submit) submit.disabled = false;
      }
      return;
    }
    const name = String($("[data-new-repo-name]")?.value || "").trim();
    const visibility = $("[data-new-repo-visibility]")?.value === "private" ? "private" : "public";
    const description = String($("[data-new-repo-description]")?.value || "").trim();
    if (!name) {
      setNewRepoHint("Enter a repository name.", "bad");
      $("[data-new-repo-name]")?.focus();
      return;
    }
    if (!/^[A-Za-z0-9._-]+$/.test(name)) {
      setNewRepoHint("Use letters, numbers, dots, dashes, or underscores in the name.", "bad");
      $("[data-new-repo-name]")?.focus();
      return;
    }
    if (!sourceValue) {
      setNewRepoHint(source === "local" ? "Enter the local repository path." : "Enter a clone URL.", "bad");
      $("[data-new-repo-source-value]")?.focus();
      return;
    }
    setNewRepoHint("Ready — finish the create & mirror from your desktop node.", "good");
    renderNewRepoSteps({ name, source, sourceValue, visibility, description });
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
    // agents, settings — goes full-width instead of leaving a rail with nothing beside
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
    // Mirror health loads once with the repository summary, then refreshes
    // only when its own tab is actually opened (plus the bounded visible-tab
    // fallback and coalesced socket signal below).
    if (tab === "mirrors") {
      void loadRepoMirrors(state.selectedRepo, { background: true });
    }
    if (tab === "settings") {
      loadRepoFediverse(state.selectedRepo);
      loadRepoDigestPreview(state.selectedRepo);
    }
    if (["commits", "issues", "projects", "pulls", "discussions", "releases", "insights", "sizemap", "agents"].includes(tab) && !state.loadedRepoTabs?.[tab]) {
      if (!state.loadedRepoTabs) state.loadedRepoTabs = {};
      state.loadedRepoTabs[tab] = true;
      if (tab === "commits") loadRepoCommits(state.selectedRepo);
      else if (tab === "issues") loadRepoIssues(state.selectedRepo);
      else if (tab === "projects") loadRepoProjects(state.selectedRepo);
      else if (tab === "releases") loadRepoReleases(state.selectedRepo);
      else if (tab === "insights") loadRepoInsights(state.selectedRepo);
      else if (tab === "sizemap") loadRepoSizeMapTab(state.selectedRepo);
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
      status.textContent = "No reachable mirror host is available to index files.";
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
    // Callers reading the dedicated pull-metadata branch first resolve its
    // immutable OID with resolveRepoPullMetadataCommit(), then pass that exact
    // value here. Explicitly empty refs retain the generic no-ref URL contract
    // for compatibility callers; pull readers never rely on that ambiguity.
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
