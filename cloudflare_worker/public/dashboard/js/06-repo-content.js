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
    const matchingMirror = (state.repoMirrors || []).find((mirror) => {
      const mirrorCommit = String(mirror?.commit || "").trim().toLowerCase();
      return hash && mirrorCommit === hash.trim().toLowerCase()
        && Number(mirror?.lastCommitAt) > 0;
    });
    const exactMirrorDate = matchingMirror?.lastCommitAt || "";
    const date = exactMirrorDate
      || commitSummaryField(data, "date", "committedAt", "updatedAt")
      || String(repo.updatedAt || repo.lastSync || "");
    if (commit && typeof commit === "object") state.repoLatestCommit = commit;
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

  // Skeleton placeholder for the file tree while the live mirror answers. Mirrors
  // the real row grid (icon / name / commit message / date) so the panel keeps
  // its shape and shimmers into the content, instead of a bare one-line spinner.
  // Bars use muted-foreground/20 so they stay visible in both light and dark.
  function repoTreeSkeletonHtml(rows = 8) {
    const nameWidths = ["w-40", "w-28", "w-52", "w-36", "w-44", "w-24", "w-48", "w-32"];
    const bar = "rounded bg-muted-foreground/20";
    const cells = Array.from({ length: rows }).map((_, i) => `
        <div class="grid w-full grid-cols-[1.5rem_minmax(0,1fr)_auto] items-center gap-3 border-t border-border px-4 py-2.5 sm:grid-cols-[1.5rem_minmax(9rem,0.8fr)_minmax(0,1fr)_auto]">
          <span class="h-4 w-4 shrink-0 ${bar}"></span>
          <span class="h-3.5 ${nameWidths[i % nameWidths.length]} max-w-full ${bar}"></span>
          <span class="hidden h-3 w-3/4 ${bar} sm:block"></span>
          <span class="h-3 w-16 shrink-0 ${bar}"></span>
        </div>`).join("");
    return `<div class="animate-pulse" role="status" aria-label="Loading tree…">${cells}<span class="sr-only">Loading tree…</span></div>`;
  }

  async function loadRepositoryTree(repo, path = "", options = {}) {
    // background: warm the Code tab's tree/README underneath another visible
    // tab. A refresh on /owner/repo/issues restores the Issues tab first and
    // then preloads the tree — that preload must not steal the visible tab
    // (setRepoTab) or rewrite the address bar back to the repo root
    // (navigateHistory), which is what used to snap every refreshed feature
    // tab back to the main repo page.
    const background = options.background === true;
    const detail = $("[data-repo-detail]");
    if (!detail) return;
    const treeBody = detail.querySelector("[data-repo-tree]");
    const treePanel = detail.querySelector("[data-repo-tree-panel]");
    const viewer = detail.querySelector("[data-repo-blob]");
    const readmePanel = detail.querySelector("[data-repo-readme]");
    if (!treeBody) return;

    if (!background) setRepoTab("code");
    treePanel?.classList.remove("hidden");
    viewer?.classList.add("hidden");
    readmePanel?.classList.toggle("hidden", Boolean(path));
    setRepoExplorerFocusMode(Boolean(path));
    // The root/README view keeps the two-column layout: the About rail is a
    // permanent right-hand column (the old full-width README override that
    // dropped it below the content was removed, owner decision 2026-07-11).
    renderRepoBreadcrumb(repo, path);

    treeBody.innerHTML = repoTreeSkeletonHtml();
    renderRepoExplorer(repo, path, []);
    try {
      const requestedAt = performance.now();
      // A live-mirror read: fetch fresh (fetchRepoJson, no-store) like every
      // other HTTPS data surface (issues/pulls/releases/README), never the browser-
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
        if (!background) navigateHistory(repoPathUrl(repo, "tree", path));
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
      if (!background) navigateHistory(repoPathUrl(repo, "tree", path));
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
      // The tree fetch drives the commit summary; if no mirror answered, still
      // resolve the loading skeleton to the repo-derived fallback metadata.
      updateRepoCommitSummary(null, repo);
      treeBody.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">No reachable mirror host is serving this repository tree right now.</div>';
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

  // Issues are split by status into .forkmesh/issues/open/<n>/ and
  // .forkmesh/issues/closed/<n>/ (adhoc #14); pre-split mirrors keep the
  // numbered folder directly under the root (no subdir).
  function issueJsonPath(number, subdir) {
    const base = subdir ? `.forkmesh/issues/${subdir}` : ".forkmesh/issues";
    return `${base}/${Number(number)}/issue-${Number(number)}.json`;
  }

  // Every path issue <number>'s record may live at, most likely first.
  function issueJsonCandidatePaths(number) {
    return ["open", "closed", ""].map((subdir) => issueJsonPath(number, subdir));
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
    // Deletion is shown, not hidden (adhoc #16), and classified by who signed it,
    // mirroring the desktop (Issue::isDeleted / hasUnauthorizedDeleteAttempt).
    // A delete/self signed by the issue's own creator deletes it; one signed by
    // anyone else is an unauthorized attempt that leaves the issue open but
    // flagged.
    const creator = open.author || "";
    const deletes = events.filter(
      (event) => event && event.type === "delete" && event.target === "self");
    const deleted = deletes.some((event) => event.author && event.author === creator);
    const deleteAttempted = !deleted &&
      deletes.some((event) => event.author && event.author !== creator);
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
      events,
      creator,
      title: issue.title || open.title || `issue #${number}`,
      status,
      deleted,
      deleteAttempted,
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

  // Stand-in row for an issue folder the mirror is counting but whose JSON we
  // could not read (relay hiccup, unreadable blob). The desktop's
  // countOpenIssues treats an unreadable blob as open too, so we keep the count
  // honest and show the row (flagged) instead of silently dropping it.
  function placeholderIssue(number) {
    return {
      number: Number(number),
      title: `issue #${Number(number)}`,
      status: "open",
      author: "unknown",
      date: "",
      labels: [],
      meta: "open · couldn't load from mirror",
      body: "",
      wantsAgent: false,
      milestone: "",
      progress: 0,
      startDate: 0,
      endDate: 0,
      createdAtMs: 0,
      loadFailed: true,
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
      // Every signed event on the issue (comment, status, labels, milestone,
      // assignees, agent, title, dates, progress, bounty, edit, delete, vote)
      // so the detail view can render the full activity timeline, not just the
      // opening comment.
      issueEvents: issue.events,
    };
  }

  // Icon + human-readable description for a non-comment issue event, mirroring
  // the desktop timeline (MainWindowIssues.cpp addActivity). The default branch
  // still surfaces unknown/future event types so the detail view shows every
  // action the JSON carries rather than silently dropping it.
  function issueEventIcon(type) {
    return ({
      status: "circle-dot",
      labels: "tag",
      milestone: "milestone",
      priority: "flag",
      assignees: "user-plus",
      agent: "bot",
      title: "pencil",
      progress: "gauge",
      dates: "calendar",
      bounty: "coins",
      edit: "pencil",
      delete: "trash-2",
      vote: "thumbs-up",
    })[type] || "activity";
  }

  function issueEventDescription(ev) {
    switch (ev.type) {
      case "status":
        return ev.status === "closed" ? "closed this issue" : "reopened this issue";
      case "labels":
        return Array.isArray(ev.labels) && ev.labels.length
          ? `set labels: ${ev.labels.join(", ")}` : "cleared the labels";
      case "milestone":
        return ev.milestone ? `set milestone: ${ev.milestone}` : "cleared the milestone";
      case "priority":
        return Number(ev.priority) > 0 ? `set priority: ${Number(ev.priority)}` : "cleared the priority";
      case "assignees":
        return Array.isArray(ev.assignees) && ev.assignees.length
          ? `set assignees: ${ev.assignees.join(", ")}` : "cleared the assignees";
      case "title":
        return ev.title ? `changed the title to "${ev.title}"` : "cleared the title";
      case "progress":
        return `set progress to ${Number(ev.progress) || 0}%`;
      case "dates":
        return "updated the schedule dates";
      case "bounty":
        return Number(ev.bountyUsd) > 0
          ? `set a $${Number(ev.bountyUsd)} bounty${ev.bountyStatus ? ` (${ev.bountyStatus})` : ""}`
          : "cleared the bounty";
      case "agent": {
        const sid = Number(ev.agentSessionId) || 0;
        if (sid <= 0 || ev.agentStatus === "cleared") return "cleared the agent assignment";
        let text = `assigned ${ev.agentProvider || "an"} agent session #${sid}`;
        if (ev.agentCreatePr) text += " with PR creation requested";
        if (ev.agentStatus) text += ` (${ev.agentStatus})`;
        return text;
      }
      case "edit":
        return ev.target ? "edited a comment" : "edited the description";
      case "delete":
        return ev.target === "self" ? "deleted this issue" : "deleted a comment";
      case "vote":
        return "voted on this issue";
      default:
        return ev.type ? `recorded a ${ev.type} action` : "recorded an action";
    }
  }

  function renderIssueTimelineComment(ev) {
    const who = ev.authorName || ev.author || "unknown";
    const when = formatRecordDate(ev.ts);
    const body = String(ev.body || "").trim();
    return `
      <div class="border-t border-border px-4 py-3 text-sm first:border-t-0">
        <div class="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
          <span class="font-medium text-foreground">${escapeHtml(who)}</span>
          <span>commented</span><span>&middot;</span><span>${escapeHtml(when)}</span>
        </div>
        ${body ? `<div class="mt-2 whitespace-pre-wrap text-sm leading-6 text-foreground">${escapeHtml(body)}</div>` : ""}
      </div>`;
  }

  // Full issue activity timeline: comment events render as bodied cards and
  // every other event as an activity line, in chronological order, so the
  // detail view shows every action stored in the issue JSON (adhoc #45). The
  // opening event is omitted here since it's already shown as the issue body.
  function renderIssueTimeline(events) {
    const rows = (Array.isArray(events) ? events : [])
      .filter(Boolean)
      .slice()
      .sort((a, b) => (Number(a.ts) || 0) - (Number(b.ts) || 0));
    const deletedComments = new Set(
      rows.filter((ev) => ev.type === "delete" && ev.target && ev.target !== "self")
        .map((ev) => ev.target));
    const items = [];
    for (const ev of rows) {
      if (ev.type === "open") continue;
      const who = ev.authorName || ev.author || "unknown";
      const when = formatRecordDate(ev.ts);
      if (ev.type === "comment") {
        if (deletedComments.has(ev.id)) continue;
        items.push(renderIssueTimelineComment(ev));
        continue;
      }
      items.push(`
        <div class="flex flex-wrap items-center gap-1.5 border-t border-border px-4 py-2.5 text-xs text-muted-foreground first:border-t-0">
          <i data-lucide="${issueEventIcon(ev.type)}" class="h-3.5 w-3.5 text-primary"></i>
          <span class="font-medium text-foreground">${escapeHtml(who)}</span>
          <span>${escapeHtml(issueEventDescription(ev))}</span>
          <span>&middot;</span><span>${escapeHtml(when)}</span>
        </div>`);
    }
    return items.join("");
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
    const servedBy = String(
      response.headers.get("X-ForkMesh-Served-By") || "",
    ).trim();
    if (data && typeof data === "object" && !Array.isArray(data)) {
      // Trust routing provenance from the Worker's response header, never an
      // upstream JSON field. A metadata-cache hit intentionally has no header,
      // so remove any body claim instead of presenting it as a live selection.
      if (servedBy) data.servedBy = servedBy;
      else delete data.servedBy;
    }
    if (!response.ok || data.ok === false) {
      const error = new Error(data.error || `HTTP ${response.status}`);
      error.status = response.status;
      error.code = data.error || "";
      throw error;
    }
    return data;
  }

  const PULL_METADATA_BRANCH = "forkmesh/pulls";

  function immutableGitCommit(value) {
    const commit = String(value || "").trim();
    return /^(?:[0-9a-f]{40}|[0-9a-f]{64})$/i.test(commit) ? commit : "";
  }

  function repoPullMetadataCacheKey(repo) {
    return [
      repoKey(repo).toLowerCase(),
      repoDataVersion(repo),
    ].join("@");
  }

  async function resolveRepoPullMetadataCommit(repo) {
    // Named private repository routes intentionally remain inert. Private
    // bytes use the opaque encrypted-replica transport and must never become
    // discoverable through this public branch lookup.
    if (!repo || repo.isPrivate) {
      throw new Error("pull_metadata_unavailable");
    }
    const key = repoPullMetadataCacheKey(repo);
    const cached = immutableGitCommit(state.repoPullMetadataCommits[key]);
    if (cached) return cached;
    if (state.repoPullMetadataInflight[key]) {
      return state.repoPullMetadataInflight[key];
    }
    const pending = (async () => {
      const data = await fetchRepoJson(`${repoApiBase(repo)}/branches`);
      const branch = (Array.isArray(data?.branches) ? data.branches : [])
        .map(normalizeRepoBranch)
        .filter(Boolean)
        .find((candidate) => candidate.name === PULL_METADATA_BRANCH);
      const commit = immutableGitCommit(branch?.commit);
      if (!commit) throw new Error("pull_metadata_unavailable");
      state.repoPullMetadataCommits[key] = commit;
      return commit;
    })();
    state.repoPullMetadataInflight[key] = pending;
    try {
      return await pending;
    } finally {
      if (state.repoPullMetadataInflight[key] === pending) {
        delete state.repoPullMetadataInflight[key];
      }
    }
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
      dir: ".forkmesh/discussions", file: "discussion.md",
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
    // params); the Worker fans the reads out over authenticated HTTPS mirrors.
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
    // Same ref override contract as repoLiveUrl. Pull readers pass the exact
    // immutable forkmesh/pulls OID resolved by
    // resolveRepoPullMetadataCommit().
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
    // (issue #399). Resolve it once, then pin the tree and every batched blob
    // read to the same immutable commit. Never fall back to main: a missing
    // metadata branch is unavailable, not an empty or stale pull collection.
    const refParams = config.dir === "pulls"
      ? { ref: await resolveRepoPullMetadataCommit(repo) }
      : {};
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
              ${item.deleted ? '<span class="rounded-full border border-border bg-secondary px-2 py-0.5 text-[10px] font-medium text-muted-foreground" title="Deleted by its author.">deleted</span>' : ""}
              ${item.deleteAttempted ? '<span class="inline-flex items-center gap-1 rounded-full border border-yellow-500/40 bg-yellow-500/10 px-2 py-0.5 text-[10px] font-medium text-yellow-600" title="Someone who did not open this issue tried to delete it; the deletion was not applied."><i data-lucide="triangle-alert" class="h-3 w-3"></i>unauthorized deletion</span>' : ""}
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

  function pullViewedStorageKey(repo, number, metadataCommit) {
    const commit = immutableGitCommit(metadataCommit);
    const owner = String(repo?.owner || "").trim().toLowerCase();
    const name = String(repo?.name || "").trim().toLowerCase();
    return owner && name && Number(number) > 0 && commit
      ? `forkmesh.pull-viewed.v1:${owner}/${name}@${commit}#${Number(number)}`
      : "";
  }

  function loadRepoPullViewed(repo, number, metadataCommit) {
    const key = pullViewedStorageKey(repo, number, metadataCommit);
    if (!key) return new Set();
    try {
      const value = JSON.parse(localStorage.getItem(key) || "[]");
      return new Set(
        (Array.isArray(value) ? value : [])
          .filter((path) => typeof path === "string" && path.length <= 500)
          .slice(0, 1000),
      );
    } catch (_) {
      return new Set();
    }
  }

  function saveRepoPullViewed(repo, number, metadataCommit, viewed) {
    const key = pullViewedStorageKey(repo, number, metadataCommit);
    if (!key) return;
    try {
      localStorage.setItem(key, JSON.stringify([...viewed].slice(0, 1000)));
    } catch (_) {}
  }

  function toggleRepoPullViewed(repo, path) {
    const detail = state.repoRecordDetail;
    if (!repo || detail?.kind !== "pulls" || !path) return;
    const metadataCommit = detail.parsed?.pullMetadataCommit || "";
    const viewed = loadRepoPullViewed(repo, detail.number, metadataCommit);
    if (viewed.has(path)) viewed.delete(path);
    else viewed.add(path);
    saveRepoPullViewed(repo, detail.number, metadataCommit, viewed);

    const article = document.querySelector("[data-repo-record-detail='pulls']");
    if (!article) return;
    article.querySelectorAll("[data-repo-pull-file]").forEach((row) => {
      if (row.dataset.repoPullFile !== path) return;
      const selected = viewed.has(path);
      row.dataset.viewed = selected ? "true" : "false";
      const icon = row.querySelector("[data-lucide]");
      if (icon) {
        icon.setAttribute("data-lucide", selected ? "check-circle-2" : "circle");
        icon.classList.toggle("text-primary", selected);
        icon.classList.toggle("text-muted-foreground", !selected);
      }
    });
    article.querySelectorAll("[data-repo-pull-diff-file]").forEach((block) => {
      if (block.dataset.repoPullDiffFile !== path) return;
      const selected = viewed.has(path);
      block.dataset.viewed = selected ? "true" : "false";
      block.classList.toggle("border-primary/40", selected);
      block.classList.toggle("border-border", !selected);
      const button = block.querySelector("[data-repo-pull-viewed]");
      if (button) {
        button.setAttribute("aria-pressed", selected ? "true" : "false");
        button.classList.toggle("text-primary", selected);
        button.classList.toggle("text-muted-foreground", !selected);
        button.innerHTML = `<i data-lucide="${selected ? "check-circle-2" : "circle"}" class="h-3.5 w-3.5"></i>${selected ? "Viewed" : "Mark viewed"}`;
      }
    });
    const summary = article.querySelector("[data-repo-pull-viewed-summary]");
    if (summary) {
      const fileCount = detail.parsed?.pullPatch?.files?.length || 0;
      summary.textContent = `${formatCount(viewed.size)} of ${formatCount(fileCount)} viewed`;
    }
    window.lucide?.createIcons();
  }

  function renderRepoPullFiles(files, viewed = new Set()) {
    const rows = Array.isArray(files) ? files : [];
    if (!rows.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No committed patch file summary is available for this pull request.</div>';
    return rows.slice(0, 100).map((file) => {
      const path = file.path || "file";
      const isViewed = viewed.has(path);
      return `
      <button type="button" data-repo-pull-file="${escapeHtml(path)}" data-viewed="${isViewed ? "true" : "false"}" class="grid w-full grid-cols-[1rem_minmax(0,1fr)_auto_auto] items-center gap-2 border-t border-border px-3 py-2 text-left text-xs transition-colors hover:bg-secondary/50 first:border-t-0">
        <i data-lucide="${isViewed ? "check-circle-2" : "circle"}" class="h-3.5 w-3.5 ${isViewed ? "text-primary" : "text-muted-foreground"}"></i>
        <span class="min-w-0 truncate font-mono text-foreground">${escapeHtml(file.path || "file")}</span>
        <span class="font-mono text-primary">+${formatCount(file.adds || 0)}</span>
        <span class="font-mono text-destructive">-${formatCount(file.dels || 0)}</span>
      </button>`;
    }).join("");
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
    if (type === "add") return "bg-primary/15 text-primary";
    if (type === "del") return "bg-destructive/15 text-destructive";
    if (type === "hunk") return "bg-accent/10 text-accent";
    if (type === "meta") return "text-muted-foreground";
    return "text-foreground";
  }

  function renderDiffFileRows(rows) {
    if (!rows.length) return '<div class="px-3 py-2 text-xs text-muted-foreground">No line changes.</div>';
    return rows.map((row) => {
      const full = row.type === "hunk" || row.type === "meta";
      return `<div class="grid grid-cols-[3rem_3rem_minmax(0,1fr)] ${diffRowClass(row.type)}">
        <span class="select-none border-r border-border/60 px-2 text-right font-mono text-muted-foreground">${full ? "" : (row.oldLine ?? "")}</span>
        <span class="select-none border-r border-border/60 px-2 text-right font-mono text-muted-foreground">${full ? "" : (row.newLine ?? "")}</span>
        <span class="whitespace-pre-wrap break-words px-3 font-mono">${escapeHtml(row.text || " ")}</span>
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

  // Large diffs used to be replaced by a "Diff hidden for speed" notice because
  // laying out every changed file at once is what made a big PR page crawl. Each
  // file block instead opts into `content-visibility: auto`, so the browser lays
  // out and paints only the files inside (or near) the visible window and skips
  // the rest until they scroll in — the whole diff is present for find-in-page
  // and anchors, and nothing offscreen costs layout (adhoc #421). The intrinsic
  // size keeps the scrollbar honest: ~1.25rem per rendered row, capped at the
  // block's own max height so the estimate never runs away on a huge file.
  function diffFileBlockIntrinsicSize(file) {
    const rows = file.binary ? 6 : Math.max(1, (file.rows || []).length);
    return `${Math.min(36, 2.5 + rows * 1.25).toFixed(2)}rem`;
  }

  function renderDiffFileBlock(file, image, viewed = new Set()) {
    const path = file.newPath || file.oldPath || "file";
    const isViewed = viewed.has(path);
    return `
      <div data-repo-pull-diff-file="${escapeHtml(path)}" data-viewed="${isViewed ? "true" : "false"}" class="overflow-hidden rounded-lg border ${isViewed ? "border-primary/40" : "border-border"}" style="content-visibility:auto;contain-intrinsic-size:auto ${diffFileBlockIntrinsicSize(file)}">
        <div class="flex flex-wrap items-center justify-between gap-2 border-b border-border bg-secondary/50 px-3 py-2">
          <span class="inline-flex min-w-0 items-center truncate font-mono text-xs font-medium text-foreground">${diffFileHeaderPath(file)}${diffFileStatusBadge(file)}</span>
          <span class="inline-flex shrink-0 items-center gap-3">
            <span class="font-mono text-[10px]"><span class="text-primary">+${formatCount(file.adds)}</span><span class="ml-1 text-destructive">-${formatCount(file.dels)}</span></span>
            <button type="button" data-repo-pull-viewed="${escapeHtml(path)}" aria-pressed="${isViewed ? "true" : "false"}" class="inline-flex h-7 items-center gap-1.5 rounded-md border border-border bg-background px-2 text-[10px] font-semibold ${isViewed ? "text-primary" : "text-muted-foreground"} hover:bg-secondary"><i data-lucide="${isViewed ? "check-circle-2" : "circle"}" class="h-3.5 w-3.5"></i>${isViewed ? "Viewed" : "Mark viewed"}</button>
          </span>
        </div>
        ${image ? renderDiffImagePreview(image) : ""}
        ${file.binary
          ? (image ? "" : '<div class="px-3 py-3 text-xs text-muted-foreground">Binary file not shown.</div>')
          : `<div class="max-h-[34rem] overflow-auto text-xs leading-5">${renderDiffFileRows(file.rows)}</div>`}
      </div>`;
  }

  function renderDiffFiles(parsed, imageDiffs, viewed = new Set()) {
    const { files } = parsed;
    if (!files.length) return '<div class="px-4 py-3 text-sm text-muted-foreground">No changes to display.</div>';
    const images = new Map();
    (Array.isArray(imageDiffs) ? imageDiffs : []).forEach((item) => {
      if (item?.path && item?.mime) images.set(item.path, item);
    });
    return `<div class="grid gap-3 p-3">${files.map((file) => renderDiffFileBlock(file, images.get(file.newPath) || images.get(file.oldPath), viewed)).join("")}</div>`;
  }

  function renderRepoPullPatch(patch, viewed = new Set()) {
    if (!String(patch || "").trim()) return '<div class="px-4 py-3 text-sm text-muted-foreground">No textual patch is committed for this pull request. Branch-backed PRs are reconstructed by the desktop client.</div>';
    return `<div data-repo-pull-patch>${renderDiffFiles(parseDiffFiles(patch), [], viewed)}</div>`;
  }

  async function loadRepoPullPatch(repo, number, metadataCommit = "") {
    const patchPath = `pulls/${number}/changes.patch`;
    try {
      const commit = immutableGitCommit(metadataCommit)
        || await resolveRepoPullMetadataCommit(repo);
      const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", {
        path: patchPath,
        ref: commit,
      }));
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

  function pullReviewPolicy(events, pullAuthor = "") {
    const authorKey = String(pullAuthor || "").trim();
    const latest = new Map();
    for (const ev of Array.isArray(events) ? events : []) {
      // Inbox reviews are not authoritative merge evidence until the owner
      // node drains, validates, commits, and republishes them on the pinned
      // pull-metadata ref.
      if (ev?.type !== "review" || ev.pending === true) continue;
      const key = String(ev.author || "").trim();
      if (!key || key === authorKey) continue;
      if (ev.state === "approved" || ev.state === "changes_requested") {
        latest.set(key, ev.state);
      } else {
        latest.delete(key);
      }
    }
    const approvals = Array.from(latest.values())
      .filter((stateName) => stateName === "approved").length;
    const blockers = Array.from(latest.values())
      .filter((stateName) => stateName === "changes_requested").length;
    return {
      approvals,
      blockers,
      ready: approvals >= 1 && blockers === 0,
    };
  }

  function renderPullReviewers(events, pullAuthor = "") {
    const reviewers = pullReviewSummary(events);
    const policy = pullReviewPolicy(events, pullAuthor);
    const gateTone = policy.ready ? "text-emerald-400" : "text-amber-400";
    const gate = `<span class="mb-2 flex items-center justify-between gap-2"><strong class="${gateTone}">${policy.ready ? "Peer gate met" : "Peer approval required"}</strong><span>${policy.approvals} approved${policy.blockers ? ` · ${policy.blockers} blocking` : ""}</span></span>`;
    if (!reviewers.length) return `${gate}<span>No reviews yet</span>`;
    // sidebarSection wraps this in a <p>, so rows must stay phrasing content
    // (span, not div) or the browser silently closes the paragraph early.
    return gate + reviewers.map((reviewer) => {
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

  async function loadRepoPullConversation(repo, number, metadataCommit = "") {
    const commit = immutableGitCommit(metadataCommit)
      || await resolveRepoPullMetadataCommit(repo);
    let tree;
    try {
      tree = await fetchRepoJson(repoLiveUrl(repo, "tree", {
        path: `pulls/${number}`,
        ref: commit,
      }));
    } catch (_) {
      return [];
    }
    const files = (Array.isArray(tree.entries) ? tree.entries : [])
      .filter((entry) => entry.type !== "tree" && /^\d+-/.test(String(entry.name || "")))
      .sort((a, b) => String(a.name).localeCompare(String(b.name)));
    if (!files.length) return [];
    const blobs = await fetchRepoBlobs(
      repo,
      files.map((entry) => `pulls/${number}/${entry.name}`),
      { ref: commit },
    );
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

  function renderIssueCommentForm(number) {
    if (!state.session?.nodeName) {
      return `<div class="border-t border-border bg-secondary/20 px-4 py-3 text-xs text-muted-foreground"><a href="/login" class="font-medium text-primary hover:underline">Log in</a> to comment on this issue.</div>`;
    }
    return `
      <form data-repo-issue-comment-form data-repo-issue-comment-number="${escapeHtml(number)}" class="grid gap-2 border-t border-border bg-secondary/20 p-4">
        ${composeIdentityHtml(state.session, "Commenting")}
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Comment
          <textarea data-repo-issue-comment-body rows="3" placeholder="Leave a comment. Markdown is supported." class="rounded-md border border-border bg-background px-3 py-2 text-sm text-foreground outline-none focus:border-primary"></textarea>
        </label>
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span data-repo-issue-comment-hint class="text-[11px] text-muted-foreground">Sent to the maintainer's inbox for review.</span>
          <button type="submit" data-repo-issue-comment-submit class="inline-flex h-9 items-center gap-2 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="send" class="h-4 w-4"></i>Comment</button>
        </div>
      </form>`;
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
      let result;
      if (isReview) {
        result = await submitWebPullReview(repo, number, action, body);
      } else {
        result = await submitWebPullComment(repo, number, body);
      }
      const signedEvent = result?.event || {};
      const newEvent = {
        type: isReview ? "review" : "comment",
        state: isReview ? action : "",
        authorName: state.session?.nodeName || "you",
        author: signedEvent.author || "",
        ts: Math.floor(Date.now() / 1000),
        body,
        pending: true,
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
        if (reviewers) {
          reviewers.innerHTML = renderPullReviewers(
            conversation,
            state.repoRecordDetail.parsed.values?.author || "",
          );
        }
        updateRepoPullMergePanel(
          repo,
          number,
          state.repoRecordDetail.parsed,
        );
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

  async function loadRepoPullMergeAuthorization(repo) {
    if (!repo || !state.session?.sessionToken) return false;
    try {
      const profile = await orgApiRequest(
        "GET",
        `/api/orgs/${encodeURIComponent(repo.owner || "")}`,
      );
      return profile?.viewerRole === "owner";
    } catch (_) {
      return false;
    }
  }

  function repoPullMergeContext(repo, number, values, metadataCommit) {
    const pullNumber = Number(number);
    const expectedBaseOid = immutableGitCommit(
      repo?.commit || repo?.rootCommit || repo?.latestCommit,
    );
    const expectedHeadOid = immutableGitCommit(values?.creationHeadOid);
    const expectedPullsOid = immutableGitCommit(metadataCommit);
    if (
      !Number.isInteger(pullNumber)
      || pullNumber <= 0
      || values?.status !== "open"
      || values?.derive !== "branch"
      || expectedBaseOid !== immutableGitCommit(values?.creationBaseOid)
      || !expectedHeadOid
      || !expectedPullsOid
      || new Set([
        expectedBaseOid.length,
        expectedHeadOid.length,
        expectedPullsOid.length,
      ]).size !== 1
    ) {
      return null;
    }
    return {
      number: pullNumber,
      expectedBaseOid,
      expectedHeadOid,
      expectedPullsOid,
    };
  }

  function createRepoPullMergeRequestId() {
    const bytes = new Uint8Array(18);
    crypto.getRandomValues(bytes);
    return `dashboard_${Array.from(bytes, (value) =>
      value.toString(16).padStart(2, "0")).join("")}`;
  }

  function renderRepoPullMergeControl(repo, number, parsed) {
    if (!parsed.pullMergeAuthorized) return "";
    const merge = parsed.pullMerge || {};
    const context = repoPullMergeContext(
      repo,
      number,
      parsed.values || {},
      parsed.pullMetadataCommit || "",
    );
    const reviewPolicy = pullReviewPolicy(
      parsed.pullConversation || [],
      parsed.values?.author || "",
    );
    if (!context) {
      return `
        <div data-repo-pull-merge-panel class="rounded-lg border border-border bg-secondary/30 p-4 text-xs text-muted-foreground">
          Owner-only mirror merge is unavailable until the open pull request exposes matching immutable base, head, and pull-metadata commits.
        </div>`;
    }
    if (!reviewPolicy.ready) {
      const reason = reviewPolicy.blockers
        ? `${reviewPolicy.blockers} independent reviewer${reviewPolicy.blockers === 1 ? " has" : "s have"} requested changes.`
        : "At least one approval from a peer other than the pull-request author is required.";
      return `
        <div data-repo-pull-merge-panel class="flex flex-wrap items-center justify-between gap-3 rounded-lg border border-amber-500/40 bg-amber-500/10 p-4">
          <span class="min-w-0 text-xs leading-5 text-muted-foreground"><strong class="text-amber-300">Independent review required.</strong> ${escapeHtml(reason)} Additional distinct approvals strengthen the review signal.</span>
          <button type="button" disabled class="inline-flex h-9 shrink-0 items-center gap-2 rounded-md bg-primary px-4 text-sm font-semibold text-primary-foreground disabled:cursor-not-allowed disabled:opacity-50"><i data-lucide="shield-check" class="h-4 w-4"></i>Merge locked</button>
        </div>`;
    }
    const stateName = String(merge.state || "ready");
    const processing = stateName === "processing";
    const merged = stateName === "merged";
    const failed = stateName === "failed";
    const label = merged
      ? "Merged into mirror"
      : processing
        ? "Merging into mirror…"
        : failed
          ? "Retry mirror merge"
          : "Merge into mirror";
    const status = merged
      ? "The selected mirror published the merge. Normal repository sync will carry it back to the source of truth."
      : processing
        ? "The selected online mirror is checking the exact reviewed commits."
        : failed
          ? escapeHtml(merge.message || "The mirror could not confirm the merge. Retrying reuses the same idempotent request.")
          : "Organization owners can merge these exact commits into an online mirror; the mirror is then synchronized back to the source of truth.";
    return `
      <div data-repo-pull-merge-panel class="flex flex-wrap items-center justify-between gap-3 rounded-lg border ${failed ? "border-destructive/40" : "border-border"} bg-secondary/30 p-4">
        <span class="min-w-0 text-xs leading-5 text-muted-foreground">${status}</span>
        <button type="button" data-repo-pull-merge ${processing || merged ? "disabled" : ""} class="inline-flex h-9 shrink-0 items-center gap-2 rounded-md bg-primary px-4 text-sm font-semibold text-primary-foreground hover:bg-primary/90 disabled:cursor-not-allowed disabled:opacity-60"><i data-lucide="${merged ? "check-circle-2" : "git-merge"}" class="h-4 w-4"></i>${label}</button>
      </div>`;
  }

  function updateRepoPullMergePanel(repo, number, parsed) {
    const current = document.querySelector("[data-repo-pull-merge-panel]");
    if (!current) return;
    const wrapper = document.createElement("div");
    wrapper.innerHTML = renderRepoPullMergeControl(repo, number, parsed);
    const replacement = wrapper.firstElementChild;
    if (replacement) current.replaceWith(replacement);
    window.lucide?.createIcons();
  }

  async function handleRepoPullMerge(repo) {
    const detail = state.repoRecordDetail;
    if (
      !repo
      || detail?.kind !== "pulls"
      || !detail.parsed?.pullMergeAuthorized
    ) {
      return;
    }
    const parsed = detail.parsed;
    const context = repoPullMergeContext(
      repo,
      detail.number,
      parsed.values || {},
      parsed.pullMetadataCommit || "",
    );
    if (!context) return;
    const existing = parsed.pullMerge || {};
    const requestId = existing.requestId || createRepoPullMergeRequestId();
    const request = {
      schemaVersion: 1,
      type: "forkmesh.pull-merge-v1",
      pullNumber: context.number,
      requestId,
      expectedBaseOid: context.expectedBaseOid,
      expectedHeadOid: context.expectedHeadOid,
      expectedPullsOid: context.expectedPullsOid,
    };
    parsed.pullMerge = { state: "processing", requestId };
    updateRepoPullMergePanel(repo, detail.number, parsed);
    const endpoint =
      `${repoApiBase(repo)}/pulls/${context.number}/merge`;
    for (let attempt = 0; attempt < 8; attempt += 1) {
      try {
        const response = await fetch(endpoint, {
          method: "POST",
          headers: {
            accept: "application/json",
            authorization: `Bearer ${state.session?.sessionToken || ""}`,
            "content-type": "application/json",
          },
          cache: "no-store",
          body: JSON.stringify(request),
        });
        const payload = await response.json().catch(() => ({}));
        if (
          response.status === 202
          && payload?.status === "processing"
          && payload?.requestId === requestId
        ) {
          await new Promise((resolve) => window.setTimeout(resolve, 750));
          continue;
        }
        if (
          response.ok
          && payload?.status === "merged"
          && payload?.published === true
          && payload?.requestId === requestId
          && immutableGitCommit(payload.baseBefore) === context.expectedBaseOid
          && immutableGitCommit(payload.head) === context.expectedHeadOid
          && immutableGitCommit(payload.pullsBefore) === context.expectedPullsOid
        ) {
          parsed.pullMerge = { state: "merged", requestId };
        } else {
          const messages = {
            forbidden: "Only an organization owner can merge through a mirror.",
            merge_conflict: "The exact reviewed commits have a merge conflict.",
            stale_base: "The base branch changed; reload and review the new state.",
            stale_head: "The pull-request head changed; reload and review it again.",
            stale_pull_metadata: "The pull-request metadata changed; reload before merging.",
            merge_node_unavailable: "No eligible online mirror can merge this pull request right now.",
            review_required: "At least one independent peer approval is required, with no unresolved request for changes.",
          };
          parsed.pullMerge = {
            state: "failed",
            requestId,
            message: messages[payload?.error] || "The mirror merge could not be confirmed.",
          };
        }
        updateRepoPullMergePanel(repo, detail.number, parsed);
        return;
      } catch (_) {
        parsed.pullMerge = {
          state: "failed",
          requestId,
          message: "The mirror merge request could not reach the relay.",
        };
        updateRepoPullMergePanel(repo, detail.number, parsed);
        return;
      }
    }
    parsed.pullMerge = {
      state: "failed",
      requestId,
      message: "The mirror is still processing. Retry to check the same merge request.",
    };
    updateRepoPullMergePanel(repo, detail.number, parsed);
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

  function safeFederatedUrl(value) {
    try {
      const url = new URL(String(value || ""));
      return url.protocol === "https:" && !url.username && !url.password
        ? url.href
        : "";
    } catch (_) {
      return "";
    }
  }

  function renderFederatedReplies(items) {
    if (!items.length) {
      return '<p class="px-4 py-4 text-sm text-muted-foreground">No remote ActivityPub replies yet.</p>';
    }
    return items.map((item) => {
      const lifecycle = String(item.lifecycle || (item.tombstone ? "tombstoned" : item.moderated ? "moderated" : item.edited ? "edited" : "active"));
      const author = item.authorName || item.author || "remote participant";
      const instance = item.sourceInstance || item.provenance?.instance || "";
      const software = item.sourceSoftware || item.provenance?.software || "ActivityPub";
      const backlink = safeFederatedUrl(item.url || item.backlink || item.remoteId);
      const depth = Math.min(8, Math.max(0, Number(item.depth) || 0));
      const status = lifecycle === "tombstoned"
        ? "Deleted on the remote instance"
        : lifecycle === "moderated"
          ? "Hidden by remote moderation"
          : lifecycle === "awaiting-redelivery"
            ? "Restored remotely; awaiting a safe redelivery"
            : "";
      const body = status || String(item.body || "");
      const edited = lifecycle === "edited" ? '<span class="text-[10px] text-muted-foreground">(edited)</span>' : "";
      const backlinkHtml = backlink
        ? `<a href="${escapeHtml(backlink)}" target="_blank" rel="noopener noreferrer" class="inline-flex items-center gap-1 text-primary hover:underline">Open original <i data-lucide="external-link" class="h-3 w-3"></i></a>`
        : "";
      return `
        <article class="border-t border-border px-4 py-3 first:border-t-0" style="margin-left:${depth * 1.25}rem" data-federated-reply data-native-event="false">
          <div class="flex flex-wrap items-center justify-between gap-2 text-[11px]">
            <span class="font-semibold text-foreground">${escapeHtml(author)} ${edited}</span>
            <span class="font-mono text-muted-foreground">${escapeHtml(software)}${instance ? ` · ${escapeHtml(instance)}` : ""}</span>
          </div>
          <p class="mt-2 whitespace-pre-wrap text-sm leading-6 ${status ? "italic text-muted-foreground" : "text-foreground"}">${escapeHtml(body)}</p>
          <div class="mt-2 flex flex-wrap items-center gap-3 text-[10px] text-muted-foreground">
            <span>Remote ActivityPub reply · not a signed ForkMesh event</span>
            ${backlinkHtml}
          </div>
        </article>`;
    }).join("");
  }

  async function loadFederatedReplies(repo, kind, number, root) {
    const container = root?.querySelector?.("[data-repo-federated-replies]");
    if (!container || !repo || !number) return;
    const apiKind = kind === "issues" ? "issue" : kind === "pulls" ? "pull" : "discussion";
    container.innerHTML = '<p class="px-4 py-4 text-sm text-muted-foreground">Loading remote ActivityPub replies…</p>';
    try {
      const response = await fetch(
        `${repoApiBase(repo)}/fedi-comments?kind=${encodeURIComponent(apiKind)}&number=${encodeURIComponent(number)}`,
        { headers: { accept: "application/json" } },
      );
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) throw new Error(data.error || `HTTP ${response.status}`);
      const comments = Array.isArray(data.comments) ? data.comments : Array.isArray(data.items) ? data.items : [];
      container.innerHTML = renderFederatedReplies(comments);
    } catch (_) {
      container.innerHTML = '<p class="px-4 py-4 text-sm text-muted-foreground">Remote replies are currently unavailable.</p>';
    } finally {
      window.lucide?.createIcons();
    }
  }

  async function moveIssueToMarketingInitiatives(button) {
    const detail = state.repoRecordDetail;
    const repo = state.selectedRepo;
    if (
      !button ||
      !repo ||
      detail?.kind !== "issues" ||
      !state.session?.sessionToken
    ) {
      if (!state.session?.sessionToken) location.href = "/login";
      return false;
    }
    const title = String(
      detail.parsed?.values?.title || `Issue #${detail.number}`,
    ).slice(0, 160);
    const original = button.innerHTML;
    button.disabled = true;
    button.innerHTML =
      '<span class="h-3.5 w-3.5 animate-spin rounded-full border-2 border-current border-r-transparent"></span>Moving…';
    try {
      const response = await fetch(
        "/api/world/office/marketing-tasks/initiatives",
        {
          method: "POST",
          credentials: "same-origin",
          cache: "no-store",
          headers: {
            accept: "application/json",
            authorization: `Bearer ${state.session.sessionToken}`,
            "content-type": "application/json",
          },
          body: JSON.stringify({
            owner: repo.owner,
            repo: repo.name,
            number: Number(detail.number),
            title,
            sessionToken: state.session.sessionToken,
          }),
        },
      );
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) {
        throw new Error(data.error || `HTTP ${response.status}`);
      }
      button.dataset.moved = "true";
      button.innerHTML =
        '<i data-lucide="check" class="h-3.5 w-3.5"></i>In Marketing initiatives';
      window.lucide?.createIcons();
      return true;
    } catch (error) {
      button.disabled = false;
      button.innerHTML = original;
      button.title = String(error?.message || "Unable to move issue").slice(
        0,
        160,
      );
      window.lucide?.createIcons();
      return false;
    }
  }

  // The connector token is a locally minted bearer string, so nothing has to
  // be fetched to have one: the first copy mints it right here in the desktop's
  // own format, remembers it, and the copied prompt carries the lines that
  // install it on the node. Copying is one click and never opens a dialog.
  // Shift-clicking the button still opens the paste box, which is what a node
  // that already published a connector needs (its own token wins).
  const MCP_CONNECTOR_TOKEN_KEY = "forkmesh.mcpConnectorToken";
  const MCP_CONNECTOR_TOKEN_PLACEHOLDER =
    "PASTE_CONNECTOR_TOKEN_FROM_FORKMESH_DESKTOP_SETTINGS_MCP";
  // Matches qt_client/src/McpConnector.cpp: "fmcp_" + 32 CSPRNG bytes,
  // base64url, unpadded — the shape isWellFormedToken() accepts.
  const MCP_CONNECTOR_TOKEN_BYTES = 32;

  function generateMcpConnectorToken() {
    try {
      const bytes = new Uint8Array(MCP_CONNECTOR_TOKEN_BYTES);
      window.crypto.getRandomValues(bytes);
      let binary = "";
      bytes.forEach((byte) => {
        binary += String.fromCharCode(byte);
      });
      return (
        "fmcp_" +
        btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "")
      );
    } catch (_) {
      // No CSPRNG (ancient or locked-down browser): fall back to the
      // placeholder rather than to a guessable token.
      return "";
    }
  }

  function loadMcpConnectorToken() {
    try {
      return String(localStorage.getItem(MCP_CONNECTOR_TOKEN_KEY) || "").trim().slice(0, 200);
    } catch (_) {
      return "";
    }
  }

  function saveMcpConnectorToken(token) {
    try {
      if (token) localStorage.setItem(MCP_CONNECTOR_TOKEN_KEY, token);
      else localStorage.removeItem(MCP_CONNECTOR_TOKEN_KEY);
    } catch (_) {}
  }

  // The token for this copy. A plain click never asks anything: a remembered
  // token is reused, otherwise one is generated on the spot and kept. Only a
  // shift-click opens the paste box, for the node that already published its
  // own connector; an empty answer there rotates to a freshly generated token
  // rather than leaving the prompt carrying a placeholder.
  function ensureMcpConnectorToken(options) {
    let token = loadMcpConnectorToken();
    if (options?.replaceToken) {
      const entered = window.prompt(
        "Paste the connector token this node already published (ForkMesh desktop app -> Settings -> MCP). Leave it empty to generate a fresh one instead - the copied prompt tells the agent how to install it.",
        token,
      );
      if (entered === null) return token;
      token = String(entered).trim().slice(0, 200) || generateMcpConnectorToken();
      saveMcpConnectorToken(token);
      return token;
    }
    if (!token) {
      token = generateMcpConnectorToken();
      saveMcpConnectorToken(token);
    }
    return token;
  }

  // "Copy MCP prompt" on the issue detail page: one paste block that points an
  // MCP-capable coding agent at the forkmesh MCP server so it pulls this issue,
  // works it on a dedicated branch, submits the pull request, and reports back
  // which model it ran as and its thinking setting. The block carries the whole
  // connection — the mcpServers entry the desktop's Settings -> MCP tab shows
  // (python3, tools/forkmesh_mcp_server.py, the repo checkout) plus the
  // connector token the signed write tools require — so nothing has to be
  // assembled by hand before the agent can run.
  function issueMcpPrompt(repo, number, values, token) {
    const repoSlug = `${repo.owner || "owner"}/${repo.name || "repo"}`;
    const title = String(values?.title || "").trim();
    const titleSlug = title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 40);
    const branch = titleSlug ? `issue-${number}-${titleSlug}` : `issue-${number}`;
    const issueUrl = `${location.origin}${repoPathUrl(repo)}/issues/${number}`;
    const repoCloneUrl = cloneUrl(repo);
    const forkmeshCloneUrl = `${location.origin}/forkmesh/forkmesh`;
    const serverScript = "<FORKMESH_CHECKOUT>/tools/forkmesh_mcp_server.py";
    const repoCheckout = `<CHECKOUT_OF_${repoSlug}>`;
    const connectorToken = token || MCP_CONNECTOR_TOKEN_PLACEHOLDER;
    // The same file tools/forkmesh_mcp_server.py reads (FORKMESH_MCP_CONNECTOR
    // overrides it) and the desktop's Settings -> MCP tab writes.
    const connectorFile =
      "$XDG_DATA_HOME/ForkMesh/ForkMesh/mcp/connector.json (default ~/.local/share/ForkMesh/ForkMesh/mcp/connector.json)";
    const configuration = {
      mcpServers: {
        forkmesh: {
          command: "python3",
          args: [serverScript],
          env: {
            FORKMESH_REPO: repoCheckout,
            FORKMESH_MCP_TOKEN: connectorToken,
          },
        },
      },
    };
    // The token was minted in the browser, so it only unlocks the write tools
    // once this machine's connector file holds it. An existing connector is
    // never overwritten: that would demote every other agent config still
    // holding the old string, and the node owner already has a token to use.
    const tokenNote = token
      ? [
          `FORKMESH_MCP_TOKEN above is a connector token minted for this prompt. Activate it before step 1: if ${connectorFile} does not exist, create it (mode 0600) containing {"version": 1, "token": "${connectorToken}", "node": "", "label": "copied MCP prompt", "created_ms": <epoch milliseconds>} - the same record the desktop app writes. If that file already exists, leave it exactly as it is and use its own "token" value in the configuration above instead - the node already published a connector and overwriting it would revoke every other agent.`,
          "Treat the token as a secret - never print it in logs, commits, pull requests, or chat.",
        ].join("\n")
      : `FORKMESH_MCP_TOKEN above is a placeholder: replace ${MCP_CONNECTOR_TOKEN_PLACEHOLDER} with the connector token the ForkMesh desktop app mints under Settings -> MCP -> Generate token, otherwise the write tools stay read-only. Treat it as a secret - never print it in logs, commits, pull requests, or chat.`;
    return [
      `Work ForkMesh issue #${number} in ${repoSlug} end to end through the "forkmesh" MCP server.`,
      "",
      `Issue: ${title || `#${number}`}`,
      `Issue page: ${issueUrl}`,
      `Repository: git clone ${repoCloneUrl}`,
      "",
      'Connect the "forkmesh" MCP server first - it is the stdio server that carries every ForkMesh tool (whoami, search_issues, read_file, comment_on_issue, open_pr_from_branch). Add this to your agent\'s MCP configuration, replacing each <...> with an absolute path on this machine and cloning whatever is missing:',
      "",
      JSON.stringify(configuration, null, 2),
      "",
      `<FORKMESH_CHECKOUT> is a checkout of ForkMesh itself, which ships the server script: git clone ${forkmeshCloneUrl}. ${repoCheckout} is your local checkout of ${repoSlug}: git clone ${repoCloneUrl}.`,
      `In Claude Code that is one command: claude mcp add forkmesh --scope user --env FORKMESH_REPO=${repoCheckout} --env FORKMESH_MCP_TOKEN=${connectorToken} -- python3 ${serverScript}`,
      tokenNote,
      "",
      `1. Call whoami on the forkmesh MCP server to confirm it answers and reaches ${repoSlug} with write access. If it is missing or read-only, fix the configuration above and retry; use the forkmesh MCP server for every ForkMesh read and write rather than any other issue tracker.`,
      "2. Pull this issue with search_issues and read every file it references with read_file before planning.",
      `3. Work in the local checkout of ${repoSlug} that the connector exposes. Create a dedicated branch named ${branch} from the latest main; never commit to main directly.`,
      `4. Implement the smallest complete fix, run the repository's tests and required checks, and commit with a clear message referencing issue #${number}.`,
      `5. Submit the work with open_pr_from_branch: branch ${branch}, base main, a title referencing issue #${number}, and a description that summarizes the change and the test results.`,
      `6. Report back with comment_on_issue on issue #${number}: link the new pull request, then state exactly which model you ran as (model name/id) and your thinking setting (extended thinking on or off, or the reasoning-effort level). Repeat the same model and thinking report in the pull request description. Do not skip this report.`,
      "",
      "If the connector is read-only, tests fail, or anything else blocks a safe submission, stop and report the blocker instead of forcing the pull request.",
    ].join("\n");
  }

  async function copyIssueMcpPrompt(button, options) {
    const detail = state.repoRecordDetail;
    const repo = state.selectedRepo;
    if (!button || !repo || detail?.kind !== "issues") return false;
    // The button renders for signed-out visitors too; the prompt itself is
    // only handed out to an authenticated account. Bounce through login and
    // land back on this exact issue.
    if (!state.session?.sessionToken) {
      location.href = "/login?next=" + encodeURIComponent(`${location.pathname}${location.search}`);
      return false;
    }
    const token = ensureMcpConnectorToken(options);
    const prompt = issueMcpPrompt(repo, detail.number, detail.parsed?.values, token);
    const copied = await copyTextToClipboard(prompt);
    if (!copied) {
      button.title = "Could not access the clipboard";
      return false;
    }
    const original = button.innerHTML;
    button.innerHTML = '<i data-lucide="check" class="h-3.5 w-3.5"></i>Prompt copied';
    window.lucide?.createIcons();
    window.setTimeout(() => {
      if (!document.body.contains(button)) return;
      button.innerHTML = original;
      window.lucide?.createIcons();
    }, 1600);
    return true;
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
        <div class="rounded-lg border border-dashed border-border bg-secondary/30 px-4 py-3 text-xs text-muted-foreground">This ${escapeHtml(config.itemLabel)} is waiting for an eligible online mirror to commit it to the repository, so it does not have a number yet.</div>` : "";
    const isIssues = !isPulls && !isDiscussions;
    const marketingInitiativeAction =
      isIssues && !options.pending
        ? `<button type="button" data-repo-marketing-initiative class="inline-flex h-8 items-center gap-2 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-secondary/70"><i data-lucide="megaphone" class="h-3.5 w-3.5 text-primary"></i>Move to Marketing initiatives</button>`
        : "";
    // Always rendered, signed in or not: a signed-out click bounces through
    // /login and returns here, so the visitor discovers the workflow either way.
    const mcpPromptAction =
      isIssues && !options.pending
        ? `<button type="button" data-repo-issue-mcp-prompt title="${state.session?.sessionToken ? "Copy a ready-to-paste agent prompt that works this issue end to end, MCP server configuration and a generated connector token included (shift-click to paste a token this node already published)" : "Sign in to copy the agent prompt for this issue"}" class="inline-flex h-8 items-center gap-2 rounded-md border border-border bg-secondary px-3 text-xs font-semibold text-foreground hover:bg-secondary/70"><i data-lucide="bot" class="h-3.5 w-3.5 text-primary"></i>Copy MCP prompt</button>`
        : "";
    const issueTimeline = isIssues ? renderIssueTimeline(parsed.issueEvents) : "";
    // Always mount the timeline container for issues so a comment posted from
    // the form below has somewhere to land, but keep it borderless while empty.
    const issueTimelineSection = isIssues
      ? `<div data-repo-issue-timeline data-empty="${issueTimeline ? "false" : "true"}" class="${issueTimeline ? "border-t border-border" : ""}">${issueTimeline}</div>`
      : "";
    // A pending issue is still awaiting its mirror commit and has no number yet,
    // so there's nothing for a comment's signature to bind to.
    const issueCommentSection = isIssues && !options.pending
      ? renderIssueCommentForm(number)
      : "";
    const pullPatch = parsed.pullPatch || { patch: "", files: [], unavailable: false };
    const pullConversation = parsed.pullConversation || [];
    const pullViewed = isPulls
      ? loadRepoPullViewed(repo, number, parsed.pullMetadataCommit || "")
      : new Set();
    const pullConversationSection = isPulls ? `
          <div data-repo-pull-conversation data-empty="${pullConversation.length ? "false" : "true"}" class="border-t border-border">${renderRepoPullConversation(pullConversation)}</div>
          ${renderPullReviewForm(number)}` : "";
    const pullFilesSection = isPulls ? `
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex flex-wrap items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="files" class="h-3.5 w-3.5 text-primary"></i>${formatCount(pullPatch.files.length)} files changed</span>
            <span data-repo-pull-viewed-summary class="font-mono text-[10px] text-muted-foreground">${formatCount(pullViewed.size)} of ${formatCount(pullPatch.files.length)} viewed</span>
          </div>
          <div class="grid min-w-0 lg:grid-cols-[16rem_minmax(0,1fr)]">
            <nav data-repo-pull-file-list aria-label="Changed files" class="max-h-[70vh] overflow-auto border-b border-border bg-secondary/20 lg:sticky lg:top-0 lg:border-b-0 lg:border-r">${renderRepoPullFiles(pullPatch.files, pullViewed)}</nav>
            <div data-repo-pull-diff-list class="min-w-0">${renderRepoPullPatch(pullPatch.patch, pullViewed)}</div>
          </div>
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
          <button type="button" data-repo-record-tab="conversation" aria-selected="true" class="inline-flex h-11 items-center gap-2 border-b-2 border-primary px-3 text-xs font-semibold text-foreground"><i data-lucide="message-square" class="h-3.5 w-3.5"></i>Conversation<span class="rounded-full bg-secondary px-1.5 py-0.5 font-mono text-[10px]">${formatCount(pullConversation.length)}</span></button>
          <button type="button" data-repo-record-tab="commits" aria-selected="false" class="inline-flex h-11 items-center gap-2 border-b-2 border-transparent px-3 text-xs font-semibold text-muted-foreground"><i data-lucide="git-commit-horizontal" class="h-3.5 w-3.5"></i>Commits<span class="rounded-full bg-secondary px-1.5 py-0.5 font-mono text-[10px]">1</span></button>
          <button type="button" data-repo-record-tab="files" aria-selected="false" class="inline-flex h-11 items-center gap-2 border-b-2 border-transparent px-3 text-xs font-semibold text-muted-foreground"><i data-lucide="files" class="h-3.5 w-3.5"></i>Files changed<span class="rounded-full bg-secondary px-1.5 py-0.5 font-mono text-[10px]">${formatCount(pullPatch.files.length)}</span></button>
          <button type="button" data-repo-record-tab="badge" aria-selected="false" class="inline-flex h-11 items-center gap-2 border-b-2 border-transparent px-3 text-xs font-semibold text-muted-foreground"><i data-lucide="fingerprint" class="h-3.5 w-3.5"></i>Badge</button>
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
          <div class="flex flex-wrap items-center justify-end gap-2">
            ${mcpPromptAction}
            ${marketingInitiativeAction}
            <span class="font-mono text-xs text-muted-foreground">${escapeHtml(repo.owner || "owner")}/${escapeHtml(repo.name || "repo")} · ${recordLabel}</span>
          </div>
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
            <div data-repo-record-panel="conversation" class="grid min-w-0 gap-4">
              ${isDiscussions ? discussionConversationSection : `
                <section data-repo-record-conversation class="overflow-hidden rounded-lg border border-border">
                  <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
                    <span class="inline-flex min-w-0 items-center gap-2 text-xs text-muted-foreground"><span class="font-semibold text-foreground">${escapeHtml(author)}</span> commented ${escapeHtml(date)}</span>
                    <span class="rounded-full border border-border px-2 py-0.5 text-[10px] font-semibold text-muted-foreground">Contributor</span>
                  </div>
                  <div data-repo-record-body class="whitespace-pre-wrap px-4 py-4 text-sm leading-6 text-foreground">${escapeHtml(body)}</div>
                  ${issueTimelineSection}
                  ${issueCommentSection}
                  ${pullConversationSection}
                </section>`}
              ${isPulls ? renderRepoPullMergeControl(repo, number, parsed) : ""}
              <section class="overflow-hidden rounded-lg border border-border" data-repo-federated-thread>
                <div class="flex flex-wrap items-center justify-between gap-2 border-b border-border bg-secondary/50 px-4 py-3">
                  <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="radio" class="h-3.5 w-3.5 text-primary"></i>Fediverse thread</span>
                  <span class="text-[10px] text-muted-foreground">Remote provenance · separate from signed native history</span>
                </div>
                <div data-repo-federated-replies></div>
              </section>
            </div>
            ${isPulls ? `
              <div data-repo-record-panel="commits" class="hidden min-w-0">
                <section class="overflow-hidden rounded-lg border border-border">
                  <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="git-commit-horizontal" class="h-3.5 w-3.5 text-primary"></i>Reviewed commit</div>
                  <div class="grid gap-2 px-4 py-4">
                    <p class="text-sm font-semibold text-foreground">${escapeHtml(title)}</p>
                    <span class="break-all font-mono text-xs text-muted-foreground">${escapeHtml(values.creationHeadOid || "Commit unavailable from this mirror")}</span>
                  </div>
                </section>
              </div>
              <div data-repo-record-panel="files" class="hidden min-w-0">${pullFilesSection}</div>
              <div data-repo-record-panel="badge" class="hidden min-w-0">
                <section class="overflow-hidden rounded-lg border border-border">
                  <div class="flex items-center gap-2 border-b border-border bg-secondary/50 px-4 py-3 text-xs font-medium text-foreground"><i data-lucide="fingerprint" class="h-3.5 w-3.5 text-primary"></i>Badge</div>
                  ${renderRepoPullBadge(title, options.pending ? 0 : number, author, pullPatch.files)}
                </section>
              </div>` : ""}
          </div>
          <aside data-repo-record-sidebar class="min-w-0 text-xs">
            ${isPulls ? sidebarSection("Reviewers", `<span data-repo-pull-reviewers class="grid gap-0.5">${renderPullReviewers(pullConversation, values.author || "")}</span>`) : ""}
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
    // Issues just submitted from this session await their mirror commit, so
    // there's nothing to fetch from the mirror yet - render
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
      const pullMetadataCommit = kind === "pulls"
        ? await resolveRepoPullMetadataCommit(repo)
        : "";
      const refParams = pullMetadataCommit
        ? { ref: pullMetadataCommit }
        : {};
      let blob;
      if (kind === "issues") {
        // The record lives under open/<n>/ or closed/<n>/ (pre-split mirrors:
        // <n>/ at the root); probe the candidates until one answers.
        let lastError = null;
        for (const path of issueJsonCandidatePaths(Number(number))) {
          try {
            blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path }));
            break;
          } catch (error) {
            lastError = error;
          }
        }
        if (!blob) throw lastError || new Error("not_found");
      } else {
        blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: recordPath, ...refParams }));
      }
      // Issues are signed-event JSON (issue-N.json), not markdown front matter,
      // so parse them the same way the list does and map into the shape the
      // detail renderer expects. Any live mirror serving .forkmesh/issues/
      // answers this blob read, so the detail loads whenever the list does.
      const parsed = kind === "issues"
        ? issueDetailParsed(blobText(blob), number)
        : parseFrontMatter(blobText(blob));
      const pullPatch = kind === "pulls"
        ? await loadRepoPullPatch(repo, number, pullMetadataCommit)
        : null;
      if (pullPatch) parsed.pullPatch = pullPatch;
      if (kind === "pulls") {
        parsed.pullMetadataCommit = pullMetadataCommit;
        parsed.pullConversation = await loadRepoPullConversation(
          repo,
          number,
          pullMetadataCommit,
        );
        parsed.pullMergeAuthorized = await loadRepoPullMergeAuthorization(repo);
      }
      if (kind === "discussions") parsed.discussionConversation = await loadRepoDiscussionConversation(repo, number);
      state.repoRecordDetail = { repo, kind, number, parsed };
      container.innerHTML = renderRepoRecordDetail(repo, kind, number, parsed);
      loadFederatedReplies(repo, kind, number, container);
    } catch (_) {
      container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">This ${escapeHtml(config.itemLabel)} is unavailable until a reachable mirror host serves ${escapeHtml(recordPath)}.</div>`;
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
  // fetch, not the caching fetchJson — the counts drop as soon as any online
  // node (the source of truth or an approved mirror) merges the submissions,
  // so they must refresh on every repo open. Best-effort: a miss just leaves
  // the badges hidden.
  async function loadRepoPendingCounts(repo) {
    try {
      const response = await fetch(`${repoApiBase(repo)}/pending`, {
        headers: { accept: "application/json" },
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data || data.ok === false) return;
      const pending = data.pending || {};
      ["issues", "pulls", "discussions"].forEach((tab) => {
        setRepoTabPending(tab, pending[tab]);
      });
      // Remember the server-side issue tally so the Issues list can show the
      // pending submissions as rows (adhoc #97), not just as a tab badge - the
      // count is the only thing we can surface publicly, since the items
      // themselves stay encrypted and owner-gated in the relay's inbox.
      state.pendingIssueCounts = state.pendingIssueCounts || {};
      state.pendingIssueCounts[pendingIssuesRepoKey(repo)] =
        Number(pending.issues) || 0;
      applyRemotePendingIssueCount(repo);
    } catch (_) {
      /* offline relay — badges stay hidden */
    }
  }

  // The relay only reports a COUNT of issue submissions awaiting an eligible
  // mirror (their contents are encrypted). Surface that count in the Issues
  // list as "syncing..." placeholder rows until a mirror commits them.
  function remotePendingIssuePlaceholders(count) {
    const list = [];
    for (let i = 0; i < count; i += 1) {
      list.push({
        number: null,
        localId: `pending-remote-${i}`,
        title: "Pending issue submission",
        status: "open",
        author: "a contributor",
        date: "waiting to sync",
        meta: "",
        body: "Submitted for direct delivery. It will appear in full when an eligible online mirror commits it to the repository.",
        pending: true,
        remotePlaceholder: true,
      });
    }
    return list;
  }

  function applyRemotePendingIssueCount(repo) {
    const view = state.issuesView;
    if (!view || !view.repo) return;
    if (pendingIssuesRepoKey(view.repo) !== pendingIssuesRepoKey(repo)) return;
    const count = Number(state.pendingIssueCounts?.[pendingIssuesRepoKey(repo)]) || 0;
    const items = view.items.filter((item) => !item.remotePlaceholder);
    // Items already shown as pending (this session's optimistic add and the
    // session's optimistic add) covers part of the server tally; only pad
    // the remainder so we never double-count a submission we can already show.
    const pendingReals = items.filter((item) => item.pending);
    const rest = items.filter((item) => !item.pending);
    const need = Math.max(0, count - pendingReals.length);
    const next = [...pendingReals, ...remotePendingIssuePlaceholders(need), ...rest];
    // Skip the re-render when nothing changed (placeholders already correct).
    if (next.length === view.items.length &&
        view.items.filter((item) => item.remotePlaceholder).length === need)
      return;
    view.items = next;
    renderRepoIssues();
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

  // The tab badges are seeded from the catalog's last-published tallies, which
  // lag the live mirror (e.g. issues opened since the owner node last
  // republished). The authoritative served counts ride only on the ROOT tree
  // reply (RepoHost::rootCountsFor, gated on path.isEmpty()), so a deep link
  // straight into a subfolder — e.g. browsing .forkmesh/issues/open — fetches a
  // subpath tree that carries no counts, leaving the Issues badge stuck on the
  // stale seed (showing 7 while the open/ folder holds 11). Refresh from the
  // mirror's root counts in that one case; best-effort, so an offline mirror
  // just keeps the catalog seed.
  async function refreshServedCounts(repo) {
    try {
      const data = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: "" }));
      if (data && data.counts) applyServedCounts(data.counts);
    } catch (_) {
      /* offline mirror — badges keep their catalog seed */
    }
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
      // A deleted issue (its creator tombstoned it) is shown only under "All",
      // badged, so it doesn't pad the Open/Closed lists the tab count tracks
      // (adhoc #16). An unauthorized deletion attempt leaves the issue in its
      // normal Open/Closed list, flagged.
      if (issue.deleted && issuesView.filter !== "all") return false;
      if (issuesView.filter === "all") return true;
      // "Open" means "not closed", matching the desktop advert and served counts
      // (RepoHost::countOpenIssues / mirrorOpenIssueCount both use status !=
      // "closed"). A strict status === "open" test dropped issues with any other
      // non-closed status (e.g. "reopened") from the Open view, so the tab and
      // list showed fewer issues than the Mirror nodes count (adhoc #96).
      if (issuesView.filter === "open") return issue.status !== "closed";
      return issue.status === "closed";
    }).filter((issue) => issueMatchesQuery(issue, issuesView.query));
    const config = repoCollectionConfig.issues;
    const filterBar = `<div class="flex items-center gap-1 border-b border-border px-4 py-2">
      ${["open", "closed", "all"].map((stateName) => `<button type="button" data-dashboard-issue-filter="${stateName}" aria-pressed="${stateName === "open" ? "true" : "false"}" class="inline-flex h-7 items-center rounded-md px-2.5 text-xs font-medium transition-colors ${stateName === issuesView.filter ? "bg-secondary text-foreground" : "text-muted-foreground hover:text-foreground"}">${stateName[0].toUpperCase() + stateName.slice(1)}</button>`).join("")}
    </div>`;
    // When the mirror is counting issues we couldn't fetch (unreadable blobs)
    // or is holding more than the 50 we page in, surface the gap with a Retry
    // button instead of letting the tab quietly disagree with the Mirror nodes
    // count.
    const missing = issuesView.missing || [];
    const truncated = issuesView.truncated || 0;
    const warnParts = [];
    if (missing.length) {
      warnParts.push(`${missing.length} issue${missing.length === 1 ? "" : "s"} (#${missing.slice(0, 12).join(", #")}${missing.length > 12 ? ", ..." : ""}) couldn't be read from the live mirror`);
    }
    if (truncated) warnParts.push(`${truncated} older issue${truncated === 1 ? "" : "s"} beyond the first 50 aren't shown`);
    const warnBar = warnParts.length
      ? `<div class="flex flex-wrap items-center justify-between gap-2 border-b border-border bg-yellow-500/10 px-4 py-2 text-xs text-foreground">
          <span class="inline-flex items-center gap-1.5"><i data-lucide="triangle-alert" class="h-3.5 w-3.5 shrink-0 text-yellow-500"></i>${escapeHtml(warnParts.join(" · "))}.</span>
          <button type="button" data-repo-issues-reload class="inline-flex h-7 items-center gap-1 rounded-md border border-border bg-background px-2.5 font-medium text-foreground hover:bg-secondary"><i data-lucide="refresh-cw" class="h-3 w-3"></i>Retry</button>
        </div>`
      : "";
    // While the closed history is still being paged in (issue #427), show a
    // loader in place of the "No issues" empty state so the Closed view doesn't
    // flash empty before its rows arrive.
    const emptyLabel = issuesView.closedLoading
      ? loadingHtml("Loading closed issues from the live mirror...")
      : issuesView.query
        ? `No issues matching "${escapeHtml(issuesView.query)}".`
        : `No ${issuesView.filter === "all" ? "" : issuesView.filter + " "}issues.`;
    container.innerHTML = filterBar + warnBar + (filtered.length
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
    // Closed issues are paged in lazily (issue #427); fetch them the first time
    // the Closed or All view is opened. loadClosedIssues renders a loading state
    // and then the results itself.
    if ((filter === "closed" || filter === "all")
        && !state.issuesView.closedLoaded && !state.issuesView.closedLoading) {
      loadClosedIssues();
      return;
    }
    renderRepoIssues();
  }

  async function loadRepoIssues(repo) {
    const container = $("[data-repo-issues]");
    if (!container) return;
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${loadingHtml("Loading issues from the live mirror...")}</div>`;
    try {
      let tree;
      try {
        // List the git tree under .forkmesh/issues/ (which also reveals the
        // open//closed/ status subdirs), then read one issue-${number}.json
        // blob for each issue from wherever it lives.
        tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: ".forkmesh/issues" }));
      } catch (error) {
        if (isMissingMirrorFolder(error)) {
          // No issues have been committed to the mirror yet.
          state.issuesView.items = [];
          state.issuesView.filter = "open";
          state.issuesView.query = "";
          state.issuesView.missing = [];
          state.issuesView.truncated = 0;
          state.issuesView.repo = repo;
          state.issuesView.closedPaths = new Map();
          state.issuesView.closedLoaded = true;
          state.issuesView.closedLoading = false;
          renderRepoIssues();
          applyRemotePendingIssueCount(repo);
          return;
        }
        throw error;
      }
      const rootEntries = Array.isArray(tree.entries) ? tree.entries : [];
      // number -> record path, split by status. The open/ and closed/ subdirs
      // (adhoc #14) hold the numbered folders; pre-split legacy mirrors keep
      // them directly under the root with no status until their blob is read.
      // A number living in a status subdir supersedes a stale legacy copy.
      const openPaths = new Map();
      const closedPaths = new Map();
      const legacyPaths = new Map();
      rootEntries
        .filter((entry) => entry.type === "tree" && /^\d+$/.test(String(entry.name || "")))
        .forEach((entry) => legacyPaths.set(Number(entry.name), issueJsonPath(Number(entry.name))));
      const statusDirs = rootEntries
        .filter((entry) => entry.type === "tree" && ["open", "closed"].includes(String(entry.name || "")))
        .map((entry) => String(entry.name));
      for (const statusDir of statusDirs) {
        let subTree = null;
        try {
          subTree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: `.forkmesh/issues/${statusDir}` }));
        } catch (error) {
          if (!isMissingMirrorFolder(error)) throw error;
        }
        const target = statusDir === "closed" ? closedPaths : openPaths;
        (Array.isArray(subTree?.entries) ? subTree.entries : [])
          .filter((entry) => entry.type === "tree" && /^\d+$/.test(String(entry.name || "")))
          .forEach((entry) => {
            const number = Number(entry.name);
            target.set(number, issueJsonPath(Number(entry.name), statusDir));
            legacyPaths.delete(number);
          });
      }
      // The Issues tab defaults to Open (issue #427): page in only the open
      // (and unknown-status legacy) titles now so they show fast, and defer the
      // closed history - often far larger - to loadClosedIssues, run the first
      // time the Closed/All filter is opened. Legacy folders ride the open page
      // and get reclassified once their blob reveals a status.
      const pathByNumber = new Map([...legacyPaths, ...openPaths]);
      const numbered = Array.from(pathByNumber.keys()).sort((a, b) => b - a);
      // Cap the page at 50 folders, but remember when the mirror holds more so
      // the panel can warn instead of silently hiding them.
      const dirs = numbered.slice(0, 50);
      const { items, missing } = await fetchIssuePage(repo, pathByNumber, dirs);
      items.sort((a, b) => Number(b.number) - Number(a.number));
      state.issuesView.items = items;
      state.issuesView.filter = "open";
      state.issuesView.query = "";
      state.issuesView.missing = missing;
      state.issuesView.truncated = Math.max(0, numbered.length - dirs.length);
      state.issuesView.repo = repo;
      state.issuesView.closedPaths = closedPaths;
      // Closed folders live only in closed/; there is nothing left to lazy-load
      // once that subdir is empty (a pure-legacy mirror keeps its closed items
      // in `items` already, classified by status).
      state.issuesView.closedLoaded = closedPaths.size === 0;
      state.issuesView.closedLoading = false;
      // Counts come from the folder listing, not the paged-in blobs, so they
      // stay right past the 50-per-page cap and match the folder-based tally the
      // Mirror nodes tab shows (adhoc #96 / issue #397). Legacy pre-split folders
      // carry no status in the tree, so fold in their loaded split - a
      // creator-deleted issue is excluded, an unauthorized deletion attempt
      // still counts (adhoc #16).
      const loadedLegacy = items.filter((issue) => legacyPaths.has(Number(issue.number)));
      const legacyOpen = loadedLegacy.filter(
        (issue) => issue.status !== "closed" && !issue.deleted).length;
      const legacyClosed = loadedLegacy.filter(
        (issue) => issue.status === "closed" && !issue.deleted).length;
      const openIssues = openPaths.size + legacyOpen;
      const closedIssues = closedPaths.size + legacyClosed;
      setRepoTabCount("issues", openIssues);
      setRepoCollectionCounts("issues", openIssues, closedIssues);
      renderRepoIssues();
      applyRemotePendingIssueCount(repo);
    } catch (_) {
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Issues are unavailable until a reachable mirror host serves the .forkmesh/issues/ folder.</div>';
    }
  }

  // Reads and parses the issue-N.json blobs for a page of numbered folders in
  // ONE batched request. A batched read can drop entries under relay load, so
  // retry just the misses once - a transient gap must not quietly shrink the
  // count vs the Mirror nodes tab (whose issueCount lists every folder).
  // Anything still unreadable becomes a flagged placeholder row instead of
  // vanishing. Returns { items, missing } (missing = still-unreadable numbers).
  async function fetchIssuePage(repo, pathByNumber, dirs) {
    const items = [];
    let missing = [];
    if (!dirs.length) return { items, missing };
    const blobs = await fetchRepoBlobs(
      repo, dirs.map((number) => pathByNumber.get(number)));
    dirs.forEach((number) => {
      const blob = blobs[pathByNumber.get(number)];
      if (blob) items.push(parseIssueJson(blobText(blob), number));
      else missing.push(number);
    });
    if (missing.length) {
      const retry = await fetchRepoBlobs(
        repo, missing.map((number) => pathByNumber.get(number))).catch(() => ({}));
      const stillMissing = [];
      missing.forEach((number) => {
        const blob = retry[pathByNumber.get(number)];
        if (blob) items.push(parseIssueJson(blobText(blob), number));
        else stillMissing.push(number);
      });
      missing = stillMissing;
    }
    missing.forEach((number) => items.push(placeholderIssue(number)));
    return { items, missing };
  }

  // Lazily pages in the closed issues the first time the Closed or All filter is
  // opened (issue #427). The initial Issues load only pages the open set for
  // speed, so this fills the closed history in on demand, appends it to the
  // already-loaded open items, and re-renders. The open/closed counts are not
  // touched - they were already derived from the full folder listing.
  async function loadClosedIssues() {
    const view = state.issuesView;
    if (view.closedLoaded || view.closedLoading) return;
    const closedPaths = view.closedPaths instanceof Map ? view.closedPaths : new Map();
    const numbers = Array.from(closedPaths.keys()).sort((a, b) => b - a);
    const dirs = numbers.slice(0, 50);
    if (!dirs.length) {
      view.closedLoaded = true;
      renderRepoIssues();
      return;
    }
    view.closedLoading = true;
    renderRepoIssues();
    try {
      const { items, missing } = await fetchIssuePage(
        view.repo || state.selectedRepo, closedPaths, dirs);
      // A number already loaded (e.g. a legacy closed copy) keeps its existing
      // row; the status-subdir copy would be identical.
      const have = new Set((view.items || []).map((issue) => Number(issue.number)));
      const fresh = items.filter((issue) => !have.has(Number(issue.number)));
      view.items = [...(view.items || []), ...fresh];
      view.items.sort((a, b) => Number(b.number) - Number(a.number));
      if (missing.length) view.missing = [...(view.missing || []), ...missing];
      view.truncated = (view.truncated || 0) + Math.max(0, numbers.length - dirs.length);
      view.closedLoaded = true;
    } catch (_) {
      /* leave closedLoaded false so a later toggle retries */
    } finally {
      view.closedLoading = false;
      renderRepoIssues();
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
        // Each record lives at open/<n>/, closed/<n>/, or the pre-split legacy
        // <n>/; ask for every candidate in the one batch and keep whichever
        // answered.
        const issueBlobs = await fetchRepoBlobs(
          repo, linkedNumbers.flatMap((number) => issueJsonCandidatePaths(number)));
        issues = linkedNumbers
          .map((number) => {
            const path = issueJsonCandidatePaths(number).find((candidate) => issueBlobs[candidate]);
            return path ? parseIssueJson(blobText(issueBlobs[path]), number) : null;
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
      container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Projects are unavailable until a reachable mirror host serves the .forkmesh/projects/ folder.</div>';
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
      container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">${escapeHtml(config.label)} are unavailable until a reachable mirror host serves the ${escapeHtml(config.dir)}/ folder.</div>`;
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

  function setRepoSettingsStatus(message, tone = "") {
    const status = $("[data-repo-settings-status]");
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

  async function saveRepoSettingsFromWeb(repo, settings = {}) {
    const response = await fetch(`${repoApiBase(repo)}/about`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        ownerAccount: state.session?.nodeName || "",
        sessionToken: state.session?.sessionToken || "",
        ...settings,
      }),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body?.ok === false) {
      throw new Error(body?.error || "repo_settings_update_failed");
    }
    return body;
  }

  async function deleteRepoFromWeb(repo) {
    const token = state.session?.sessionToken || "";
    const query = new URLSearchParams({
      owner: String(repo?.owner || ""),
      name: String(repo?.name || ""),
    });
    const response = await fetch(`/api/repositories?${query}`, {
      method: "DELETE",
      headers: {
        accept: "application/json",
        ...(token ? { authorization: "Bearer " + token } : {}),
      },
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body?.ok === false) {
      throw new Error(body?.error || "repository_delete_failed");
    }
    return body;
  }

  // --- Manage federated posts (issue #426) ---------------------------------
  //
  // The owner-only "Fediverse posts" dropdown in the About rail lists the
  // repo actor's posts and lets the owner delete one. A delete makes the relay
  // broadcast a Delete(Tombstone) to every follower's server, so the post also
  // disappears from Mastodon — not just the repo's own profile feed. The
  // backend re-checks ownership from the session token, so this is only the
  // client surface (same session-auth shape as saveRepoAboutFromWeb).

  async function fetchRepoFediPosts(repo) {
    const response = await fetch(`${repoApiBase(repo)}/ap-posts`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        ownerAccount: state.session?.nodeName || "",
        sessionToken: state.session?.sessionToken || "",
        action: "list",
      }),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body?.ok === false) {
      throw new Error(body?.error || "fedi_posts_failed");
    }
    return Array.isArray(body.posts) ? body.posts : [];
  }

  async function deleteRepoFediPost(repo, id) {
    const response = await fetch(`${repoApiBase(repo)}/ap-posts`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        ownerAccount: state.session?.nodeName || "",
        sessionToken: state.session?.sessionToken || "",
        action: "delete",
        id,
      }),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body?.ok === false) {
      throw new Error(body?.error || "fedi_post_delete_failed");
    }
    return body;
  }

  function renderRepoFediPosts(posts) {
    const list = $("[data-repo-fedi-posts-list]");
    if (!list) return;
    if (!posts.length) {
      list.innerHTML = `<p class="text-[11px] text-muted-foreground">No federated posts yet — new issues, pull requests, discussions and releases will appear here.</p>`;
      return;
    }
    list.innerHTML = posts.map((post) => {
      const id = escapeHtml(String(post.id || ""));
      const when = relativeTimeLabel(Number(post.published || 0));
      // Strip our own generated HTML down to a plain-text preview.
      const preview = String(post.content || "")
        .replace(/<[^>]*>/g, " ").replace(/\s+/g, " ").trim();
      const text = escapeHtml(preview.slice(0, 140) || "(no text)");
      const link = String(post.url || "");
      const view = /^https?:\/\//i.test(link)
        ? `<a href="${escapeHtml(link)}" target="_blank" rel="noopener noreferrer" class="dashboard-accent-link hover:underline">View</a>`
        : "";
      return `<div data-repo-fedi-post class="grid gap-1 rounded-md border border-border p-2">
        <p class="text-[11px] leading-4 text-foreground">${text}</p>
        <div class="flex items-center justify-between gap-2 text-[10px] text-muted-foreground">
          <span>${escapeHtml(when)}</span>
          <span class="inline-flex items-center gap-2">${view}<button type="button" data-repo-fedi-post-delete="${id}" class="inline-flex items-center gap-1 rounded-md border border-destructive/40 px-2 py-0.5 font-medium text-destructive hover:bg-destructive/10"><i data-lucide="trash-2" class="h-3 w-3"></i>Delete</button></span>
        </div>
      </div>`;
    }).join("");
    window.lucide?.createIcons();
  }

  async function loadRepoFediPosts(repo) {
    const list = $("[data-repo-fedi-posts-list]");
    if (!list) return;
    list.innerHTML = `<p class="text-[11px] text-muted-foreground">Loading…</p>`;
    try {
      const posts = await fetchRepoFediPosts(repo);
      if (!repoAboutStillCurrent(repo)) return;
      renderRepoFediPosts(posts);
    } catch (_) {
      list.innerHTML = `<p class="text-[11px] text-destructive">Couldn't load federated posts.</p>`;
    }
  }

  async function removeRepoFediPost(repo, id, trigger) {
    if (!id) return;
    if (!window.confirm("Delete this post? It will be removed from your Mastodon followers' timelines.")) return;
    if (trigger) trigger.disabled = true;
    try {
      await deleteRepoFediPost(repo, id);
      // Drop the row in place; the follower count/profile feed catch up on
      // the next reload.
      trigger?.closest("[data-repo-fedi-post]")?.remove();
      const list = $("[data-repo-fedi-posts-list]");
      if (list && !list.querySelector("[data-repo-fedi-post]")) {
        renderRepoFediPosts([]);
      }
    } catch (_) {
      if (trigger) trigger.disabled = false;
      window.alert("Couldn't delete that post. Please try again.");
    }
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

  function setRepoLogoSuggestionStatus(message, tone = "") {
    const target = $("[data-repo-logo-suggestion-status]");
    if (!target) return;
    target.textContent = message || "";
    target.className = `text-[11px] ${
      tone === "bad"
        ? "text-destructive"
        : tone === "good"
          ? "text-primary"
          : "text-muted-foreground"
    }`;
  }

  async function loadRepoLogoSuggestions(repo) {
    const target = $("[data-repo-logo-suggestions]");
    if (!target || repo.isPrivate) return;
    try {
      const headers = {};
      if (state.session?.sessionToken) {
        headers.Authorization = `Bearer ${state.session.sessionToken}`;
      }
      const response = await fetch(`${repoApiBase(repo)}/logo-suggestions`, {
        headers,
        cache: "no-store",
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || !repoAboutStillCurrent(repo)) return;
      const suggestions = Array.isArray(body?.suggestions)
        ? body.suggestions
        : [];
      const mayReview = sessionOwnsRepo(repo) || Boolean(state.session?.isAdmin);
      target.innerHTML = suggestions.length
        ? suggestions
            .map((suggestion) => {
              const image = String(suggestion?.image?.dataUrl || "");
              const id = String(suggestion?.id || "");
              const pending = suggestion?.status === "pending";
              return `<article class="flex items-center gap-2 rounded-md border border-border p-2">
                ${
                  image.startsWith("data:image/")
                    ? `<img src="${escapeHtml(image)}" alt="" class="h-10 w-10 rounded-md border border-border object-cover" />`
                    : ""
                }
                <div class="min-w-0 flex-1 text-[11px]">
                  <strong class="block truncate text-foreground">${escapeHtml(
                    suggestion?.official
                      ? "Official logo"
                      : `${suggestion?.status || "pending"} suggestion`,
                  )}</strong>
                  <span class="block truncate text-muted-foreground">by ${escapeHtml(
                    suggestion?.proposer || "community member",
                  )}${suggestion?.aiGenerated ? " · AI-generated" : ""}</span>
                </div>
                ${
                  mayReview && pending
                    ? `<span class="inline-flex gap-1">
                        <button type="button" data-repo-logo-review="approve" data-repo-logo-suggestion-id="${escapeHtml(
                          id,
                        )}" class="rounded border border-border px-2 py-1 text-[10px] text-foreground hover:bg-secondary">Approve</button>
                        <button type="button" data-repo-logo-review="reject" data-repo-logo-suggestion-id="${escapeHtml(
                          id,
                        )}" class="rounded border border-border px-2 py-1 text-[10px] text-muted-foreground hover:bg-secondary">Reject</button>
                      </span>`
                    : ""
                }
              </article>`;
            })
            .join("")
        : `<p class="text-[11px] text-muted-foreground">No community suggestions yet.</p>`;
    } catch (_) {
      target.innerHTML = `<p class="text-[11px] text-muted-foreground">Suggestions are temporarily unavailable.</p>`;
    }
  }

  async function submitRepoLogoSuggestion(repo, form) {
    if (!state.session?.sessionToken) {
      throw new Error("sign_in_required");
    }
    const file = form.querySelector("[data-repo-logo-suggestion-file]")?.files?.[0];
    if (!file || file.size > 256 * 1024) throw new Error("logo_too_large");
    if (!form.querySelector("[data-repo-logo-suggestion-rights]")?.checked) {
      throw new Error("rights_required");
    }
    const imageData = await new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(String(reader.result || ""));
      reader.onerror = () => reject(new Error("image_read_failed"));
      reader.readAsDataURL(file);
    });
    const response = await fetch(`${repoApiBase(repo)}/logo-suggestions`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        sessionToken: state.session.sessionToken,
        imageData,
        rightsConfirmed: true,
        attribution: String(
          form.querySelector("[data-repo-logo-suggestion-attribution]")?.value ||
            "",
        ).trim(),
        aiGenerated: Boolean(
          form.querySelector("[data-repo-logo-suggestion-ai]")?.checked,
        ),
      }),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(body?.error || "logo_suggestion_failed");
    form.reset();
    await loadRepoLogoSuggestions(repo);
  }

  async function reviewRepoLogoSuggestion(repo, suggestionId, action) {
    const response = await fetch(`${repoApiBase(repo)}/logo-suggestions`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        sessionToken: state.session?.sessionToken || "",
        suggestionId,
        action,
      }),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(body?.error || "logo_review_failed");
    await Promise.all([
      loadRepoLogoSuggestions(repo),
      loadRepoFediverse(repo),
    ]);
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
      // Admin-only operational-alert switches, seeded the same way. Absent or
      // never saved reads as off, which is the stored default.
      // Mirrors may report their own actor handle, but the repository UI
      // advertises the canonical ForkMesh actor everywhere.
      const handle = "@forkmesh.forkmesh@forkmesh.com";
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
      if (logo) {
        const logoUrl = await loadNativeRepositoryLogo(
          nativeRepositoryLogoEndpoint(repo),
        );
        if (logoUrl) {
          logo.src = logoUrl;
          logo.classList.remove("hidden");
        } else {
          logo.removeAttribute("src");
          logo.classList.add("hidden");
        }
      }
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
      loadRepoLogoSuggestions(repo);
    } catch (_) { /* fediverse card is an adornment, never an error */ }
  }

  async function loadRepoDigestPreview(repo) {
    const target = $("[data-repo-digest-preview]");
    if (!target || !sessionOwnsRepo(repo) || repo.isPrivate) return;
    target.textContent = "Loading preview…";
    try {
      const token = state.session?.sessionToken || "";
      const response = await fetch(
        `${repoApiBase(repo)}/fediverse-digest`, {
          headers: {
            accept: "application/json",
            ...(token ? { authorization: "Bearer " + token } : {}),
          },
          cache: "no-store",
        });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || !body?.ok) {
        throw new Error(body?.error || "preview_failed");
      }
      if (!repoAboutStillCurrent(repo)) return;
      const text = String(body.preview?.text || "").trim();
      target.textContent = text || "No meaningful public updates are queued.";
      target.dataset.pending = String(Number(body.pending || 0));
      target.dataset.nextPublishAt = String(Number(body.nextPublishAt || 0));
    } catch (_) {
      if (repoAboutStillCurrent(repo)) {
        target.textContent = "Digest preview is unavailable.";
      }
    }
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
        tree = await fetchRepoJson(repoLiveUrl(
          repo,
          "tree",
          { path: ".forkmesh/releases" },
        ));
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

  function renderRepoAboutFileCount(count, partial) {
    const section = $("[data-repo-about-files]");
    const label = $("[data-repo-about-files-count]");
    if (!section || !label || !count) return;
    label.textContent = `${Number(count).toLocaleString()}${partial ? "+" : ""} files`;
    section.classList.remove("hidden");
  }

  // ranked: [[language, weight], ...] already sorted desc. weight is bytes when
  // the host provided sizes (accurate), else a plain file count (fallback).
  function renderRepoAboutLanguages(ranked) {
    const total = ranked.reduce((sum, [, weight]) => sum + weight, 0);
    if (!ranked.length || !total) return;
    const bar = $("[data-repo-about-langs-bar]");
    const legend = $("[data-repo-about-langs-legend]");
    const section = $("[data-repo-about-langs]");
    if (!bar || !legend || !section) return;
    bar.innerHTML = ranked.map(([language, weight]) => {
      const color = REPO_LANGUAGE_COLORS[language] || "#8a8a8a";
      const pct = (weight / total) * 100;
      return `<span title="${escapeHtml(language)}" style="width:${pct.toFixed(1)}%;background-color:${color}"></span>`;
    }).join("");
    legend.innerHTML = ranked.map(([language, weight]) => {
      const color = REPO_LANGUAGE_COLORS[language] || "#8a8a8a";
      const pct = ((weight / total) * 100).toFixed(1);
      return `<span class="inline-flex items-center gap-1.5"><span class="h-2 w-2 rounded-full" style="background-color:${color}"></span><span class="font-medium text-foreground">${escapeHtml(language)}</span> ${pct}%</span>`;
    }).join("");
    section.classList.remove("hidden");
  }

  // The identicon shown for a contributor. Keyed on the git email (falling back
  // to the name) so two distinct people never collapse to the same swatch the
  // way a name-initial + single hue did — a two-tone gradient plus up-to-two
  // initials keeps each user visually distinct and stable across loads.
  function repoContributorAvatar(contributor) {
    const name = String(contributor?.name || contributor?.email || "?").trim();
    const email = String(contributor?.email || "").trim().toLowerCase();
    const commits = Number(contributor?.commits) || 0;
    const key = email || name.toLowerCase();
    let hash = 0;
    for (let i = 0; i < key.length; i += 1) hash = (hash * 31 + key.charCodeAt(i)) >>> 0;
    const hue = hash % 360;
    const hue2 = (hue + 40 + ((hash >> 8) % 90)) % 360;
    const words = name.split(/[\s._@-]+/).filter(Boolean);
    const initials = ((words[0]?.[0] || "?") + (words[1]?.[0] || "")).toUpperCase();
    const title = `${name}${email ? ` <${email}>` : ""}` +
      (commits ? ` — ${commits} commit${commits === 1 ? "" : "s"}` : "");
    return `<span title="${escapeHtml(title)}" class="flex h-8 w-8 items-center justify-center rounded-full border border-background font-mono text-[10px] font-semibold uppercase text-white shadow-sm" style="background-image:linear-gradient(135deg, hsl(${hue} 60% 46%), hsl(${hue2} 58% 38%))">${escapeHtml(initials)}</span>`;
  }

  // contributors: [{name, email, commits}, ...] sorted desc. total overrides the
  // rendered count (the host knows the full-history total even though only the
  // top few avatars are shown).
  function renderRepoAboutContributors(contributors, total) {
    const rows = (Array.isArray(contributors) ? contributors : [])
      .filter((c) => c && (c.name || c.email));
    if (!rows.length) return;
    const section = $("[data-repo-about-contribs]");
    const count = $("[data-repo-about-contribs-count]");
    const list = $("[data-repo-about-contribs-list]");
    if (!section || !count || !list) return;
    count.textContent = String(Number(total) || rows.length);
    list.innerHTML = rows.slice(0, 14).map(repoContributorAvatar).join("");
    section.classList.remove("hidden");
  }

  // Preferred path: one /stats round-trip the host answers from `git ls-tree -r`
  // + `git shortlog`, so the file count is exact, languages are byte-weighted,
  // and contributors span the whole history. Returns false (→ fall back to the
  // per-directory walk + capped history) when the host is offline or too old to
  // know the op.
  async function loadRepoAboutStats(repo) {
    let data;
    try {
      data = await fetchRepoJson(repoLiveUrl(repo, "stats"));
    } catch (_) {
      return false; // host offline, or an old node that answered bad_op as a 5xx
    }
    if (!data || data.ok === false) return false;
    if (!repoAboutStillCurrent(repo)) return true; // served, but the user navigated away
    renderRepoAboutFileCount(Number(data.fileCount) || 0, false);
    const tally = {};
    Object.entries(data.extensions || {}).forEach(([ext, info]) => {
      const language = REPO_LANGUAGE_EXTENSIONS[String(ext).toLowerCase()];
      if (!language) return;
      const bytes = Number(info?.bytes) || 0;
      const weight = bytes > 0 ? bytes : Number(info?.files) || 0;
      if (weight > 0) tally[language] = (tally[language] || 0) + weight;
    });
    renderRepoAboutLanguages(Object.entries(tally).sort((a, b) => b[1] - a[1]).slice(0, 6));
    renderRepoAboutContributors(data.contributors, Number(data.contributorCount) || 0);
    return true;
  }

  async function loadRepoAboutFilesAndLanguages(repo) {
    try {
      const files = await buildRepoFileIndex(repo);
      if (!files.length || !repoAboutStillCurrent(repo)) return;
      renderRepoAboutFileCount(files.length, Boolean(state.repoFileFinder?.partial));
      const tally = {};
      files.forEach((path) => {
        const name = String(path).split("/").pop() || "";
        const ext = name.includes(".") ? name.split(".").pop().toLowerCase() : "";
        const language = REPO_LANGUAGE_EXTENSIONS[ext];
        if (language) tally[language] = (tally[language] || 0) + 1;
      });
      renderRepoAboutLanguages(Object.entries(tally).sort((a, b) => b[1] - a[1]).slice(0, 6));
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
      renderRepoAboutContributors(
        ranked.map(([name, commitCount]) => ({ name, commits: commitCount })),
        ranked.length);
    } catch (_) { /* host offline — section stays hidden */ }
  }

  // --- Size map (adhoc #189) --------------------------------------------
  //
  // A multi-level pie (sunburst) of the repo's directory sizes, fed by the
  // host's one-round-trip /sizes op (git ls-tree -r -l folded into a nested
  // directory tree). Ring 1 holds the repo root's directories and each deeper
  // ring subdivides its parent; a directory's direct files are the unfilled
  // span at the end of its arc, so "mostly loose files" reads as mostly-empty
  // arc (the HDGraph convention). The About rail shows a small static chart;
  // clicking it opens the fullscreen interactive one (hover details,
  // click-to-zoom, breadcrumb). Hosts too old to know the op just leave the
  // section hidden.

  // Top-level directories take these hues in size order (fixed slots, never
  // cycled — everything past eight goes muted gray); descendants inherit the
  // parent hue stepped toward the surface so depth reads as shade. Both
  // variants validated against the site's dark (#010409) / light (#ffffff)
  // surfaces.
  const REPO_SIZEMAP_DARK = ["#3987e5", "#199e70", "#c98500", "#008300", "#9085e9", "#e66767", "#d55181", "#d95926"];
  const REPO_SIZEMAP_LIGHT = ["#2a78d6", "#1baf7a", "#eda100", "#008300", "#4a3aa7", "#e34948", "#e87ba4", "#eb6834"];
  const REPO_SIZEMAP_GRAY = "#8a8a8a";

  function repoSizeMapShade(hex, depth, lighten, extra) {
    const toward = lighten ? 255 : 0;
    const f = Math.min(0.55, Math.max(0, (depth - 1) * 0.16 + extra));
    const channel = (i) => {
      const v = parseInt(hex.slice(1 + i * 2, 3 + i * 2), 16);
      return Math.round(v + (toward - v) * f);
    };
    return `rgb(${channel(0)} ${channel(1)} ${channel(2)})`;
  }

  // One walk over the full tree assigns every directory its color, so a node
  // keeps its hue in the mini chart, in the modal, and at every zoom level
  // (color follows the entity, never its current rank on screen).
  function repoSizeMapColors(root) {
    const dark = document.documentElement.dataset.dashboardTheme !== "light";
    const palette = dark ? REPO_SIZEMAP_DARK : REPO_SIZEMAP_LIGHT;
    const colors = new Map();
    const walk = (node, depth, base, index) => {
      const hue = depth === 1
        ? (node.name !== "…" && index < palette.length ? palette[index] : REPO_SIZEMAP_GRAY)
        : base;
      colors.set(node, repoSizeMapShade(hue, depth, dark, depth > 1 ? (index % 2) * 0.06 : 0));
      (node.children || []).forEach((child, i) => walk(child, depth + 1, hue, i));
    };
    (root.children || []).forEach((child, i) => walk(child, 1, REPO_SIZEMAP_GRAY, i));
    return colors;
  }

  // Flattens the tree, re-rooted at `focus`, into drawable ring segments. Each
  // segment keeps `nodes` (the chain from focus down to it) so a click can
  // extend the zoom trail without re-walking the tree.
  function repoSizeMapSegments(focus, colors, rings) {
    if (!(Number(focus?.size) > 0)) return [];
    const segments = [];
    const minSpan = (2 * Math.PI) / 900; // skip sub-0.4-degree slivers
    const walk = (node, depth, from, span, chain) => {
      if (depth > rings) return;
      let at = from;
      const size = Number(node.size) || 0;
      (node.children || []).forEach((child) => {
        const childSize = Number(child.size) || 0;
        if (!(childSize > 0) || !(size > 0)) return;
        const childSpan = span * Math.min(1, childSize / size);
        if (childSpan >= minSpan) {
          const nodes = [...chain, child];
          segments.push({
            node: child,
            nodes,
            path: nodes.map((n) => String(n.name || "")).join("/"),
            depth,
            a0: at,
            a1: at + childSpan,
            color: colors.get(child) || REPO_SIZEMAP_GRAY,
            hasChildren: Array.isArray(child.children) && child.children.length > 0,
            isFile: child.type === "file" && typeof child.path === "string",
            filePath: child.type === "file" ? String(child.path || "") : "",
          });
          walk(child, depth + 1, at, childSpan, nodes);
        }
        at += childSpan;
      });
    };
    walk(focus, 1, -Math.PI / 2, 2 * Math.PI, []);
    return segments;
  }

  function repoSizeMapArc(c, r0, r1, a0, a1) {
    const span = Math.min(a1 - a0, 2 * Math.PI - 0.0004);
    const end = a0 + span;
    const large = span > Math.PI ? 1 : 0;
    const px = (r, a) => (c + r * Math.cos(a)).toFixed(2);
    const py = (r, a) => (c + r * Math.sin(a)).toFixed(2);
    return `M ${px(r1, a0)} ${py(r1, a0)} A ${r1} ${r1} 0 ${large} 1 ${px(r1, end)} ${py(r1, end)} ` +
      `L ${px(r0, end)} ${py(r0, end)} A ${r0} ${r0} 0 ${large} 0 ${px(r0, a0)} ${py(r0, a0)} Z`;
  }

  // Renders the sunburst as an SVG string. Every <path> carries
  // data-seg="<index>" into the returned segments array so callers can wire
  // hover/click; non-interactive charts get native <title> tooltips instead.
  function repoSizeMapSvg(focus, colors, opts) {
    const view = opts.view;
    const hole = opts.hole;
    const c = view / 2;
    const ringWidth = (c - 6 - hole) / opts.rings;
    const segments = repoSizeMapSegments(focus, colors, opts.rings);
    const gap = "rgb(var(--dashboard-background-rgb))";
    const parts = [];
    segments.forEach((seg, i) => {
      const r0 = hole + (seg.depth - 1) * ringWidth;
      const r1 = r0 + ringWidth;
      parts.push(
        `<path data-seg="${i}" d="${repoSizeMapArc(c, r0, r1, seg.a0, seg.a1)}" fill="${seg.color}" ` +
        `stroke="${gap}" stroke-width="2"${opts.interactive
          ? ` class="cursor-pointer" tabindex="0" role="button" aria-label="${escapeHtml(
              `${seg.isFile ? "Open file" : "Zoom directory"} ${seg.path}`,
            )}"`
          : ""}>` +
        (opts.interactive ? "" : `<title>${escapeHtml(`${seg.path} — ${formatSize(seg.node.size)}`)}</title>`) +
        `</path>`);
    });
    if (opts.labels) {
      // Direct labels only where they comfortably fit (span over ~18 degrees);
      // the hover tooltip carries everything else.
      const labelSize = opts.labelSize || 12;
      segments.forEach((seg) => {
        if (seg.a1 - seg.a0 < 0.32) return;
        const mid = (seg.a0 + seg.a1) / 2;
        const r = hole + (seg.depth - 1) * ringWidth + ringWidth / 2;
        const x = (c + r * Math.cos(mid)).toFixed(1);
        const y = (c + r * Math.sin(mid)).toFixed(1);
        const name = String(seg.node.name || "");
        const shown = name.length > 14 ? `${name.slice(0, 13)}…` : name;
        parts.push(
          `<text x="${x}" y="${y}" text-anchor="middle" fill="#ffffff" font-size="${labelSize}" ` +
          `style="pointer-events:none;paint-order:stroke;stroke:rgb(0 0 0 / 0.5);stroke-width:2.5px">` +
          `<tspan x="${x}" dy="-0.15em">${escapeHtml(shown)}</tspan>` +
          `<tspan x="${x}" dy="1.2em" font-size="${labelSize - 1}">${escapeHtml(formatSize(seg.node.size))}</tspan></text>`);
      });
    }
    // Center hub: a transparent circle catches zoom-out clicks over the whole
    // hole; the two text lines ride on top with pointer events off.
    parts.push(`<circle data-sizemap-center="1" cx="${c}" cy="${c}" r="${hole - 2}" fill="transparent"${opts.canGoUp ? ' class="cursor-pointer" tabindex="0" role="button" aria-label="Zoom out one directory"' : ""}></circle>`);
    const title = String(opts.centerTitle || "");
    const shownTitle = title.length > 16 ? `${title.slice(0, 15)}…` : title;
    parts.push(
      `<text x="${c}" y="${c}" text-anchor="middle" style="pointer-events:none">` +
      `<tspan x="${c}" dy="-0.15em" fill="rgb(var(--dashboard-foreground-rgb))" font-size="${opts.centerTitleSize || 13}" font-weight="600">${escapeHtml(shownTitle)}</tspan>` +
      `<tspan x="${c}" dy="1.35em" fill="rgb(var(--dashboard-muted-foreground-rgb))" font-size="${(opts.centerTitleSize || 13) - 2}">${escapeHtml(String(opts.centerSub || ""))}</tspan></text>`);
    const svg =
      `<svg viewBox="0 0 ${view} ${view}" role="img" aria-label="Directory size sunburst" class="block h-auto w-full">${parts.join("")}</svg>`;
    return { svg, segments };
  }

  function renderRepoAboutSizeMap(repo, tree) {
    const section = $("[data-repo-about-sizemap]");
    const chart = $("[data-repo-about-sizemap-chart]");
    const open = $("[data-repo-about-sizemap-open]");
    if (!section || !chart || !open) return;
    if (!(Number(tree?.size) > 0) || !(tree.children || []).length) return;
    const colors = repoSizeMapColors(tree);
    chart.innerHTML = repoSizeMapSvg(tree, colors, {
      view: 260,
      hole: 42,
      rings: 3,
      centerTitle: formatSize(tree.size),
      centerSub: `${Number(tree.fileCount || 0).toLocaleString()} files`,
      centerTitleSize: 14,
    }).svg;
    open.onclick = () => activateRepoTab("sizemap");
    section.classList.remove("hidden");
  }

  // --- Size map tab (adhoc #198) ----------------------------------------
  //
  // The interactive size map now lives on its own repo tab instead of a modal
  // reached from the About rail. The tab lazy-loads /sizes on first open, then
  // mounts the same zoomable sunburst explorer full-width.
  async function loadRepoSizeMapTab(repo) {
    const body = $("[data-repo-sizemap]");
    if (!body) return;
    body.innerHTML = `<p class="text-xs text-muted-foreground">${loadingHtml("Loading size map...")}</p>`;
    try {
      const data = await fetchRepoJson(repoLiveUrl(repo, "sizes"));
      if (!data || data.ok === false || !(Number(data.size) > 0) || !(data.children || []).length) {
        body.innerHTML = `<p class="text-xs text-muted-foreground">No directory size data is available for this repository yet — it appears once an online mirror node reports it.</p>`;
        return;
      }
      renderRepoSizeMapTab(repo, data);
    } catch (_) {
      body.innerHTML = `<p class="text-xs text-muted-foreground">Couldn't load the size map. Try again once a mirror is online.</p>`;
    }
  }

  function renderRepoSizeMapTab(repo, root) {
    const body = $("[data-repo-sizemap]");
    if (!body) return;
    body.innerHTML = `
      <div class="mb-3 min-w-0">
        <div data-sizemap-crumbs class="flex min-w-0 flex-wrap items-center gap-1 text-sm text-foreground"></div>
        <p data-sizemap-summary class="mt-0.5 text-[11px] text-muted-foreground"></p>
      </div>
      <div class="relative">
        <div data-sizemap-chart class="mx-auto max-w-xl"></div>
        <div data-sizemap-tip class="pointer-events-none absolute z-20 hidden max-w-xs rounded-md border border-border bg-popover px-2.5 py-1.5 font-mono text-[11px] text-foreground shadow-lg"></div>
      </div>
      <p class="mt-3 border-t border-border pt-2 text-[11px] text-muted-foreground">Activate a directory to zoom in · activate a leaf file to open it at the selected ref · activate the center to zoom out. Pointer, touch, Enter, and Space are supported.</p>`;
    mountRepoSizeMapExplorer({
      repo,
      root,
      chart: body.querySelector("[data-sizemap-chart]"),
      crumbs: body.querySelector("[data-sizemap-crumbs]"),
      summary: body.querySelector("[data-sizemap-summary]"),
      tip: body.querySelector("[data-sizemap-tip]"),
    });
  }

  async function loadRepoAboutSizeMap(repo) {
    try {
      const data = await fetchRepoJson(repoLiveUrl(repo, "sizes"));
      if (!data || data.ok === false) return; // host offline, or a pre-sizes node
      if (!repoAboutStillCurrent(repo)) return;
      renderRepoAboutSizeMap(repo, data);
    } catch (_) { /* section stays hidden */ }
  }

  // The detailed, interactive size map: click a directory to zoom in, the
  // center (or a breadcrumb) to zoom back out, hover for exact sizes. Mounts
  // into the caller's {chart, crumbs, summary, tip} elements (the Size map tab
  // panel) — it carries no overlay/dialog chrome of its own.
  function mountRepoSizeMapExplorer({ repo, root, chart, crumbs, summary, tip }) {
    if (!chart || !crumbs || !summary || !tip) return;
    const colors = repoSizeMapColors(root);
    let trail = [root]; // root … focus
    let segments = [];

    const render = () => {
      const focus = trail[trail.length - 1];
      const out = repoSizeMapSvg(focus, colors, {
        view: 720,
        hole: 86,
        rings: 4,
        labels: true,
        interactive: true,
        labelSize: 13,
        canGoUp: trail.length > 1,
        centerTitle: trail.length > 1 ? String(focus.name || "") : String(repo.name || "repository"),
        centerSub: formatSize(Number(focus.size) || 0),
        centerTitleSize: 16,
      });
      segments = out.segments;
      chart.innerHTML = out.svg;
      crumbs.innerHTML = trail.map((node, i) => {
        const label = String(i === 0 ? repo.name || "repo" : node.name || "");
        const item = i === trail.length - 1
          ? `<span class="font-semibold">${escapeHtml(label)}</span>`
          : `<button type="button" data-sizemap-crumb="${i}" class="dashboard-accent-link hover:underline">${escapeHtml(label)}</button>`;
        return (i ? '<span class="text-muted-foreground">/</span>' : "") + item;
      }).join("");
      const focusSize = Number(trail[trail.length - 1].size) || 0;
      const rootSize = Number(root.size) || 0;
      summary.textContent = trail.length > 1
        ? `${formatSize(focusSize)} · ${rootSize ? ((focusSize / rootSize) * 100).toFixed(1) : "0"}% of the repository`
        : `${formatSize(rootSize)} across ${Number(root.fileCount || 0).toLocaleString()} files`;
      crumbs.querySelectorAll("[data-sizemap-crumb]").forEach((button) => {
        button.onclick = () => {
          trail = trail.slice(0, Number(button.dataset.sizemapCrumb) + 1);
          render();
        };
      });
      window.lucide?.createIcons();
    };

    const activateSizeMapTarget = (target) => {
      if (target?.closest?.("[data-sizemap-center]")) {
        if (trail.length > 1) {
          trail = trail.slice(0, -1);
          render();
        }
        return;
      }
      const hit = target?.closest?.("[data-seg]");
      const seg = hit ? segments[Number(hit.dataset.seg)] : null;
      if (seg?.isFile && seg.filePath) {
        tip.classList.add("hidden");
        loadRepositoryBlob(repo, seg.filePath);
        return;
      }
      if (seg?.hasChildren) {
        trail = [...trail, ...seg.nodes];
        tip.classList.add("hidden");
        render();
      }
    };
    chart.addEventListener("click", (event) => {
      activateSizeMapTarget(event.target);
    });
    chart.addEventListener("keydown", (event) => {
      if (!["Enter", " "].includes(event.key)) return;
      if (!event.target.closest("[data-seg],[data-sizemap-center]")) return;
      event.preventDefault();
      activateSizeMapTarget(event.target);
    });
    chart.addEventListener("mousemove", (event) => {
      const hit = event.target.closest("[data-seg]");
      const seg = hit ? segments[Number(hit.dataset.seg)] : null;
      if (!seg) {
        tip.classList.add("hidden");
        return;
      }
      const prefix = trail.slice(1).map((n) => String(n.name || "")).join("/");
      const full = prefix ? `${prefix}/${seg.path}` : seg.path;
      const rootSize = Number(root.size) || 0;
      const pct = rootSize ? ((Number(seg.node.size) / rootSize) * 100).toFixed(1) : "0";
      tip.textContent = `${full} — ${formatSize(seg.node.size)} · ${pct}% of repo`;
      const host = tip.parentElement.getBoundingClientRect();
      tip.style.left = `${Math.max(8, Math.min(event.clientX - host.left + 14, host.width - 160))}px`;
      tip.style.top = `${event.clientY - host.top + 14}px`;
      tip.classList.remove("hidden");
    });
    chart.addEventListener("mouseleave", () => tip.classList.add("hidden"));

    render();
  }

  // The About rail's file/language/contributor sections come from /stats in one
  // request; only when that host op is unavailable do we fall back to the slower
  // directory walk and capped commit history. The size map rides its own /sizes
  // op in parallel (hosts predating it just leave that section hidden).
  async function loadRepoAboutInsights(repo) {
    loadRepoAboutSizeMap(repo);
    if (await loadRepoAboutStats(repo)) return;
    loadRepoAboutFilesAndLanguages(repo);
    loadRepoAboutContributors(repo);
  }

  function loadRepoAboutRail(repo) {
    if (!repo || repo.isPrivate) {
      // Private repos have no fediverse presence; live sections still apply.
      loadRepoAboutInfo(repo);
      loadRepoAboutRelease(repo);
      loadRepoAboutInsights(repo);
      return;
    }
    loadRepoFediverse(repo);
    loadRepoAboutInfo(repo);
    loadRepoAboutRelease(repo);
    loadRepoAboutInsights(repo);
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

  // Agent plaintext is intentionally unavailable in the browser.  The hybrid
  // recipient private key lives only in the owner's desktop vault, so even a
  // stale legacy row must not reactivate browser transcript/prompt surfaces.
  function renderRepoAgentDetail(agent) {
    return `
      <div data-repo-agent-detail data-repo-agent-id="${escapeHtml(String(agent.id ?? ""))}" class="grid gap-3 px-4 py-3 text-xs">
        <div class="flex items-center gap-2 font-medium text-foreground">
          <i data-lucide="shield-check" class="h-4 w-4 text-primary"></i>
          Owner-device encrypted
        </div>
        <p class="leading-5 text-muted-foreground">This session, its transcript, and its prompts can be opened only by the owner's ForkMesh desktop key. The browser and relay do not have that key.</p>
        <a href="/desktop" class="inline-flex h-8 w-fit items-center gap-1.5 rounded-md bg-primary px-3 font-medium text-primary-foreground hover:bg-primary/90"><i data-lucide="monitor-down" class="h-3.5 w-3.5"></i>Open desktop downloads</a>
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

  // The browser intentionally has no owner recipient private key.  Never fetch
  // a legacy plaintext transcript or pretend an opaque envelope is readable.
  async function loadRepoAgentTranscript(repo, agentId) {
    const pre = $("[data-repo-agent-transcript]");
    if (!pre || !repo || !agentId) return;
    pre.textContent =
      "Open this transcript in the ForkMesh desktop app on the owner device.";
  }

  async function requestRepoAgentsList(repo) {
    if (!repo) return [];
    throw new Error("owner_device_only");
  }

  function orgAgentEndpoint(repo, sessionId = "") {
    const base = `/api/orgs/${encodeURIComponent(repo.owner || "")}/repos/${encodeURIComponent(repo.name || "")}/agent-bots`;
    return sessionId ? `${base}/${encodeURIComponent(sessionId)}` : base;
  }

  function renderOrgAgentSession(session) {
    const history = Array.isArray(session.history) ? session.history : [];
    const promptable = ["running", "queued"].includes(String(session.status || ""));
    return `
      <article class="grid gap-3 border-t border-border px-4 py-4" data-org-agent-session="${escapeHtml(session.id || "")}">
        <div class="flex flex-wrap items-center gap-2">
          <span class="rounded-full border border-border px-2 py-0.5 font-mono text-[11px] ${repoAgentStatusTone(session.status)}">${escapeHtml(session.status || "unknown")}</span>
          <strong class="min-w-0 flex-1 truncate text-sm text-foreground">${escapeHtml(session.title || `${session.provider || "Agent"} session`)}</strong>
          <span class="font-mono text-[11px] text-muted-foreground">${escapeHtml(session.provider === "codex" ? "Codex" : "Claude Code")}</span>
        </div>
        <div class="flex flex-wrap gap-x-4 gap-y-1 text-[11px] text-muted-foreground">
          <span>mirror <span class="font-mono text-foreground">${escapeHtml(session.targetNode || "pending")}</span></span>
          <span>started by @${escapeHtml(session.createdBy || "member")}</span>
          <span>Haiku gate: ${escapeHtml(session.security?.state || "pending")}</span>
          ${session.taskKey ? `<span>board <span class="font-mono text-foreground">${escapeHtml(session.taskKey)}</span></span>` : ""}
        </div>
        <div class="max-h-72 space-y-2 overflow-auto rounded-md border border-border bg-secondary/20 p-3">
          ${history.length ? history.map((item) => `
            <div class="grid gap-1 text-xs">
              <div class="flex items-center gap-2 text-[10px] uppercase tracking-wide text-muted-foreground">
                <span>${escapeHtml(item.role || "event")}</span>
                <span>${escapeHtml(item.author || "")}</span>
                <span class="ml-auto">${escapeHtml(item.state || "")}</span>
              </div>
              <p class="whitespace-pre-wrap break-words leading-5 text-foreground">${escapeHtml(item.text || "")}</p>
            </div>`).join("") : '<p class="text-xs text-muted-foreground">Waiting for the mirror.</p>'}
        </div>
        ${promptable ? `
          <form data-org-agent-followup="${escapeHtml(session.id || "")}" class="flex gap-2">
            <input name="prompt" required maxlength="8000" class="h-9 min-w-0 flex-1 rounded-md border border-input bg-background px-3 text-xs text-foreground" placeholder="Revise or send another prompt…" />
            <button type="submit" class="h-9 rounded-md bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90">Send</button>
          </form>` : ""}
      </article>`;
  }

  async function orgAgentPost(path, body) {
    const token = state.session?.sessionToken || "";
    const response = await fetch(path, {
      method: "POST",
      cache: "no-store",
      headers: {
        "content-type": "application/json",
        accept: "application/json",
        ...(token ? { authorization: `Bearer ${token}` } : {}),
      },
      body: JSON.stringify({ ...body, sessionToken: token }),
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok || payload.ok === false) {
      const error = new Error(payload.error || `HTTP ${response.status}`);
      error.status = response.status;
      throw error;
    }
    return payload;
  }

  function wireOrgAgentPanel(repo, container) {
    container.querySelector("[data-org-agent-start]")?.addEventListener("submit", async (event) => {
      event.preventDefault();
      const form = event.currentTarget;
      const prompt = String(new FormData(form).get("prompt") || "").trim();
      const taskKey = String(new FormData(form).get("taskKey") || "").trim();
      const provider = event.submitter?.value || "";
      const hint = form.querySelector("[data-org-agent-hint]");
      if (!prompt || !["claude-code", "codex"].includes(provider)) return;
      if (hint) hint.textContent = "Selecting an eligible mirror and queuing the Haiku security check…";
      Array.from(form.elements).forEach((element) => { element.disabled = true; });
      try {
        await orgAgentPost(orgAgentEndpoint(repo), { provider, prompt, taskKey });
        await loadRepoAgents(repo);
      } catch (error) {
        if (hint) {
          hint.className = "text-[11px] text-destructive";
          hint.textContent = error.message === "no_eligible_headless_mirror"
            ? "No integrity-approved headless mirror is online for this repository."
            : `Could not start the agent: ${error.message}`;
        }
        Array.from(form.elements).forEach((element) => { element.disabled = false; });
      }
    });
    container.querySelectorAll("[data-org-agent-followup]").forEach((form) => {
      form.addEventListener("submit", async (event) => {
        event.preventDefault();
        const prompt = String(new FormData(form).get("prompt") || "").trim();
        if (!prompt) return;
        Array.from(form.elements).forEach((element) => { element.disabled = true; });
        try {
          await orgAgentPost(orgAgentEndpoint(repo, form.dataset.orgAgentFollowup), { prompt });
          await loadRepoAgents(repo);
        } catch (error) {
          const input = form.querySelector("input");
          if (input) {
            input.disabled = false;
            input.setCustomValidity(`Could not send: ${error.message}`);
            input.reportValidity();
            input.setCustomValidity("");
          }
          form.querySelector("button")?.removeAttribute("disabled");
        }
      });
    });
    container.querySelector("[data-org-agent-refresh]")?.addEventListener("click", () => loadRepoAgents(repo));
  }

  async function loadRepoAgents(repo) {
    const container = $("[data-repo-agents]");
    if (!container || !repo) return;
    state.agentsView.agents = [];
    state.agentsView.selectedAgentId = null;
    if (state.session?.sessionToken) {
      try {
        const payload = await fetchJson(orgAgentEndpoint(repo), { fresh: true });
        const sessions = Array.isArray(payload.sessions) ? payload.sessions : [];
        container.innerHTML = `
          <section class="grid gap-0">
            <header class="grid gap-3 px-4 py-4">
              <div class="flex flex-wrap items-center gap-2">
                <i data-lucide="bot" class="h-4 w-4 text-primary"></i>
                <strong class="text-sm text-foreground">Organization agents</strong>
                <span class="rounded-full border border-border px-2 py-0.5 text-[10px] text-muted-foreground">${escapeHtml(payload.memberRole || "member")}</span>
                <button type="button" data-org-agent-refresh class="ml-auto inline-flex h-8 items-center gap-1.5 rounded-md border border-border px-2.5 text-xs hover:bg-secondary"><i data-lucide="refresh-cw" class="h-3.5 w-3.5"></i>Refresh</button>
              </div>
              <p class="max-w-3xl text-xs leading-5 text-muted-foreground">Start Claude Code or Codex on an integrity-approved headless mirror. Every new or revised prompt must receive an exact tool-free Claude Haiku approval before the coding agent runs. Sessions, transcripts, controls, and audit history are restricted to current Engineering team members.</p>
              <form data-org-agent-start class="grid gap-2 rounded-md border border-border bg-secondary/20 p-3">
                <textarea name="prompt" required maxlength="8000" rows="3" class="w-full resize-y rounded-md border border-input bg-background px-3 py-2 text-xs text-foreground" placeholder="Describe the repository task…"></textarea>
                <input name="taskKey" maxlength="96" pattern="(?:task:[a-z0-9-]{1,48}|issue:[a-z0-9-]{1,40}/[a-z0-9._-]{1,60}#[1-9][0-9]{0,8})" class="h-9 rounded-md border border-input bg-background px-3 font-mono text-xs text-foreground" placeholder="Optional tracked board key, e.g. task:codex-world" />
                <div class="flex flex-wrap items-center gap-2">
                  <button type="submit" name="provider" value="claude-code" class="h-9 rounded-md bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90">Start Claude Code</button>
                  <button type="submit" name="provider" value="codex" class="h-9 rounded-md border border-border bg-background px-3 text-xs font-semibold text-foreground hover:bg-secondary">Start Codex</button>
                  <span data-org-agent-hint class="text-[11px] text-muted-foreground">Prompt text is encrypted at rest; the selected mirror performs the Haiku preflight.</span>
                </div>
              </form>
            </header>
            <div>${sessions.length ? sessions.map(renderOrgAgentSession).join("") : '<div class="border-t border-border px-4 py-4 text-sm text-muted-foreground">No organization agent sessions yet.</div>'}</div>
          </section>`;
        wireOrgAgentPanel(repo, container);
        window.lucide?.createIcons();
        return;
      } catch (error) {
        if (
          Number(error.status || 0) === 403 &&
          error.message === "engineering_team_required"
        ) {
          container.innerHTML = `
            <div class="grid gap-3 px-4 py-4 text-sm text-muted-foreground">
              <div class="flex items-center gap-2 font-medium text-foreground"><i data-lucide="shield-alert" class="h-4 w-4 text-amber-500"></i>Engineering access required</div>
              <p class="max-w-2xl leading-6">Only members of this organization’s <strong class="text-foreground">engineering</strong> team can view agent transcripts or start, revise, and re-prompt Claude Code and Codex sessions.</p>
            </div>`;
          window.lucide?.createIcons();
          return;
        }
        if (![403, 404].includes(Number(error.status || 0))) {
          container.innerHTML = `<div class="px-4 py-3 text-sm text-destructive">Organization agents are unavailable: ${escapeHtml(error.message)}</div>`;
          return;
        }
      }
    }
    container.innerHTML = `
      <div class="grid gap-3 px-4 py-4 text-sm text-muted-foreground">
        <div class="flex items-center gap-2 font-medium text-foreground"><i data-lucide="shield-check" class="h-4 w-4 text-primary"></i>Owner-device encrypted agents</div>
        <p class="max-w-2xl leading-6">Agent session snapshots, transcripts, results, and prompts are encrypted to a hybrid key whose private half stays in the owner's desktop vault. This browser cannot decrypt them, and ForkMesh administrators do not receive an override.</p>
        <a href="/desktop" class="inline-flex h-9 w-fit items-center gap-2 rounded-md bg-primary px-3 text-xs font-semibold text-primary-foreground hover:bg-primary/90"><i data-lucide="monitor-down" class="h-4 w-4"></i>Open desktop downloads</a>
      </div>`;
    window.lucide?.createIcons();
  }

  function workshopAgentDeepLink(repo) {
    if (!repo || !sessionCanAssignAgent(repo)) return null;
    const params = new URLSearchParams(location.search || "");
    const sessionId = String(params.get("workshopSession") || "").toLowerCase();
    const resultId = String(params.get("workshopResult") || "").toLowerCase();
    const runId = String(params.get("run") || "");
    const commit = String(params.get("commit") || "").toLowerCase();
    if (
      !/^[a-f0-9]{32}$/.test(sessionId) ||
      !/^[a-f0-9]{32}$/.test(resultId) ||
      !/^[A-Za-z0-9_-]{16,80}$/.test(runId) ||
      !/^[a-f0-9]{40,64}$/.test(commit)
    ) {
      return null;
    }
    return { sessionId, resultId, runId, commit };
  }

  function setWorkshopAgentContext(message, tone = "") {
    const target = $("[data-workshop-agent-context]");
    if (!target) return;
    target.hidden = !message;
    target.textContent = message || "";
    target.className = `border-b border-border px-4 py-3 text-xs ${
      tone === "bad"
        ? "text-destructive"
        : tone === "good"
          ? "text-primary"
          : "text-muted-foreground"
    }`;
  }

  function workshopAgentPrompt(savedSession, result) {
    const report =
      result?.report && typeof result.report === "object" ? result.report : {};
    const cleanItems = (value, limit) =>
      (Array.isArray(value) ? value : [])
        .map((item) =>
          String(item || "")
            .replace(/[\u0000-\u001f\u007f]/g, " ")
            .replace(/\s+/g, " ")
            .trim(),
        )
        .filter(Boolean)
        .slice(0, limit);
    const findings = cleanItems(report.findings, 20);
    const recommendations = cleanItems(report.recommendations, 12);
    const references = cleanItems(report.references, 30);
    return [
      "Authorized workshop context",
      `Repository: ${String(savedSession.repository || "")}`,
      `Commit: ${String(savedSession.commit || "")}`,
      `Run: ${String(savedSession.runId || "")}`,
      `Workshop: ${String(savedSession.workshopType || "")}`,
      `Saved result: ${String(result.id || "")}`,
      "",
      "Findings to verify:",
      ...(findings.length ? findings.map((item) => `- ${item}`) : ["- No candidate findings were saved."]),
      "",
      "Recommendations:",
      ...(recommendations.length
        ? recommendations.map((item) => `- ${item}`)
        : ["- Re-run the bounded analysis and verify the referenced code."]),
      "",
      "Supporting file references:",
      ...(references.length
        ? references.map((item) => `- ${item}`)
        : ["- No supporting paths were saved."]),
      "",
      "Continue this analysis only against the repository and exact commit above. Treat prior findings as recommendations, not guaranteed facts.",
    ]
      .join("\n")
      .slice(0, 8000);
  }

  async function consumeWorkshopAgentDeepLink(repo, deepLink) {
    if (!deepLink || !sessionCanAssignAgent(repo)) return;
    setWorkshopAgentContext("Verifying the authorized workshop result…");
    try {
      const payload = await fetchJson(
        `/api/world/workshops/${encodeURIComponent(deepLink.sessionId)}`,
        { fresh: true },
      );
      const savedSession = payload?.session;
      const result = Array.isArray(savedSession?.results)
        ? savedSession.results.find((item) => item?.id === deepLink.resultId)
        : null;
      if (
        !savedSession ||
        !repoMatchesKey(repo, savedSession?.repository) ||
        savedSession?.runId !== deepLink.runId ||
        savedSession?.commit !== deepLink.commit ||
        result?.runId !== savedSession?.runId ||
        result?.commit !== savedSession?.commit
      ) {
        throw new Error("workshop_scope_mismatch");
      }
      setAgentModalOpen(true);
      const modal = $("#agentModal");
      const repoSelect = modal?.querySelector("[data-agent-modal-repo]");
      const prompt = modal?.querySelector("[data-repo-agent-new-input]");
      const hint = modal?.querySelector("[data-repo-agent-new-hint]");
      if (repoSelect) repoSelect.value = repoKey(repo);
      if (prompt) prompt.value = workshopAgentPrompt(savedSession, result);
      if (hint) {
        hint.textContent =
          "Verified repository, run, result, and commit. Review the prompt before starting an agent.";
        hint.className = "text-[11px] text-primary";
      }
      setWorkshopAgentContext(
        `Authorized workshop context loaded for commit ${String(
          savedSession.commit || "",
        ).slice(0, 12)}. No agent has been started.`,
        "good",
      );
    } catch (_) {
      setWorkshopAgentContext(
        "Workshop context could not be verified for this repository. No agent was started.",
        "bad",
      );
    }
  }


  async function handleRepoAgentPromptSubmit(repo, form) {
    void repo;
    const hint = form?.querySelector("[data-repo-agent-prompt-hint]");
    if (hint) {
      hint.className = "text-[11px] text-muted-foreground";
      hint.textContent =
        "Send prompts from the ForkMesh desktop app; this browser has no owner encryption key.";
    }
  }

  // Kept as a defensive no-op for stale cached markup.  The current dashboard
  // renders an owner-device information panel instead of a browser prompt form.
  async function handleRepoAgentNewSubmit(repo, form) {
    void repo;
    const hint = form?.querySelector("[data-repo-agent-new-hint]");
    if (hint) {
      hint.className = "text-[11px] text-muted-foreground";
      hint.textContent =
        "Start agents from the owner device so no plaintext prompt crosses the relay.";
    }
  }
