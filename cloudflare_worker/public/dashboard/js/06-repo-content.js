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

  // A discussion's replies are an append-only, signed event log stored as
  // discussions/<N>/NNNN-comment.md files alongside discussion.md (see
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
      tree = await fetchRepoJson(repoLiveUrl(repo, "tree", { path: `discussions/${number}` }));
    } catch (_) {
      return [];
    }
    const files = (Array.isArray(tree.entries) ? tree.entries : [])
      .filter((entry) => entry.type !== "tree" && /^\d+-comment\.md$/.test(String(entry.name || "")))
      .sort((a, b) => String(a.name).localeCompare(String(b.name)));
    if (!files.length) return [];
    const blobs = await fetchRepoBlobs(repo, files.map((entry) => `discussions/${number}/${entry.name}`));
    return files.map((entry) => {
      const blob = blobs[`discussions/${number}/${entry.name}`];
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
    const who = escapeHtml(state.session.nodeName);
    return `
      <form data-repo-discussion-reply-form data-repo-discussion-reply-number="${escapeHtml(number)}" class="grid gap-2 border-t border-border bg-secondary/20 p-4">
        <label class="grid gap-1 text-xs font-medium text-muted-foreground">Reply
          <textarea data-repo-discussion-reply-body rows="3" placeholder="Write a reply. Markdown is supported." class="rounded-md border border-border bg-background px-3 py-2 text-sm text-foreground outline-none focus:border-primary"></textarea>
        </label>
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span data-repo-discussion-reply-hint class="text-[11px] text-muted-foreground">Replying as ${who}. Sent to the maintainer's inbox for review.</span>
          <button type="submit" data-repo-discussion-reply-submit class="inline-flex h-9 items-center gap-2 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="send" class="h-4 w-4"></i>Reply</button>
        </div>
      </form>`;
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
    const discussionConversation = parsed.discussionConversation || [];
    const discussionConversationSection = kind === "discussions" ? `
        <section class="overflow-hidden rounded-lg border border-border">
          <div class="flex items-center justify-between gap-3 border-b border-border bg-secondary/50 px-4 py-3">
            <span class="inline-flex items-center gap-2 text-xs font-medium text-foreground"><i data-lucide="message-square" class="h-3.5 w-3.5 text-primary"></i>Replies</span>
            <span class="font-mono text-[10px] text-muted-foreground">${formatCount(discussionConversation.length)} ${discussionConversation.length === 1 ? "reply" : "replies"}</span>
          </div>
          <div data-repo-discussion-conversation data-empty="${discussionConversation.length ? "false" : "true"}">${renderRepoDiscussionConversation(discussionConversation)}</div>
          ${renderDiscussionReplyForm(number)}
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
        ${discussionConversationSection}
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
      if (kind === "discussions") parsed.discussionConversation = await loadRepoDiscussionConversation(repo, number);
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
  // re-checks the account name against owner/admin status (no password,
  // adhoc #225), so this is only the client-side gate for showing the checkbox.
  function sessionCanAssignAgent(repo) {
    return sessionOwnsRepo(repo) || Boolean(state.session?.isAdmin);
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
          : '<div class="text-[11px] text-muted-foreground">This session has finished — you can no longer send it messages.</div>'}
      </div>`;
  }

  // Composer pinned above the tab bar on every repo tab, not just Agents
  // (adhoc #278): type a prompt and send it to a brand-new agent on the
  // owner's node from wherever they're browsing the repo. The node's prompt
  // drain recognises the "new" sentinel agent id and spins up an ad-hoc run.
  function renderRepoAgentNewComposer() {
    return `
      <div class="mt-4 overflow-hidden rounded-lg border border-border bg-background">
        <form data-repo-agent-new-form class="flex flex-col gap-2 bg-secondary/30 px-4 py-3">
          <input data-repo-agent-new-input type="text" maxlength="8000" placeholder="Start a new agent — enter a prompt" class="h-8 min-w-0 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary" />
          <div class="flex items-center gap-2">
            <select data-repo-agent-new-provider title="Agent provider" class="h-8 shrink-0 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary">
              ${AGENT_PROVIDER_OPTIONS.map((opt) => `<option value="${escapeHtml(opt.value)}">${escapeHtml(opt.label)}</option>`).join("")}
            </select>
            <select data-repo-agent-new-model title="Agent model" class="h-8 shrink-0 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary">
              ${AGENT_NEW_MODEL_OPTIONS.map((opt) => `<option value="${escapeHtml(opt.value)}">${escapeHtml(opt.label)}</option>`).join("")}
            </select>
            <button type="submit" data-repo-agent-new-submit class="inline-flex h-8 shrink-0 items-center gap-1.5 rounded-md bg-primary px-2.5 text-xs font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50"><i data-lucide="plus" class="h-3.5 w-3.5"></i>Start agent</button>
            <span data-repo-agent-new-hint class="text-[11px] text-muted-foreground"></span>
          </div>
        </form>
      </div>`;
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
    // The "start a new agent" composer now lives above the tab bar on every
    // tab (adhoc #278), not just here, so this list is just the sessions.
    container.innerHTML = agents.length
      ? `<div class="divide-y divide-border">${agents.map(renderRepoAgentRow).join("")}</div>`
      : '<div class="px-4 py-3 text-sm text-muted-foreground">No agent sessions yet. Start one above, or from the desktop app.</div>';
    window.lucide?.createIcons();
  }

  // Open / close the detail page for one agent, wiring up the faster transcript
  // poll so the pane stays live while it's on screen (adhoc #259).
  function openRepoAgentDetail(repo, agentId) {
    state.agentsView.selectedAgentId = agentId;
    renderRepoAgentsList(state.agentsView.agents);
    loadRepoAgentTranscript(repo, agentId);
    startRepoAgentTranscriptRefresh(repo, agentId);
  }

  function closeRepoAgentDetail(repo) {
    stopRepoAgentTranscriptRefresh();
    state.agentsView.selectedAgentId = null;
    renderRepoAgentsList(state.agentsView.agents);
  }

  function stopRepoAgentTranscriptRefresh() {
    if (repoAgentTranscriptTimer) {
      window.clearInterval(repoAgentTranscriptTimer);
      repoAgentTranscriptTimer = null;
    }
  }

  function startRepoAgentTranscriptRefresh(repo, agentId) {
    stopRepoAgentTranscriptRefresh();
    if (!repo || agentId == null) return;
    repoAgentTranscriptTimer = window.setInterval(() => {
      if (!state.selectedRepo || repoKey(state.selectedRepo) !== repoKey(repo) ||
          state.activeRepoTab !== "agents" ||
          String(state.agentsView.selectedAgentId ?? "") !== String(agentId)) {
        stopRepoAgentTranscriptRefresh();
        return;
      }
      loadRepoAgentTranscript(repo, agentId);
    }, 4000);
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
        body: JSON.stringify({ ownerAccount: state.session?.nodeName || "" }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) throw new Error(data.error || `HTTP ${response.status}`);
      // Only touch the pane if it's still the open agent — a slow response that
      // lands after the user navigated away must not clobber the new view.
      if (String(state.agentsView.selectedAgentId ?? "") !== String(agentId)) return;
      const current = $("[data-repo-agent-transcript]");
      if (!current) return;
      const atBottom = current.scrollHeight - current.scrollTop - current.clientHeight < 24;
      const text = String(data.transcript || "");
      current.textContent = text || "No transcript yet — waiting for the agent to produce output.";
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
      }),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return Array.isArray(data.agents) ? data.agents : [];
  }

  function stopRepoAgentsAutoRefresh() {
    if (repoAgentsRefreshTimer) {
      window.clearInterval(repoAgentsRefreshTimer);
      repoAgentsRefreshTimer = null;
    }
    // The detail page's live-transcript poll is only meaningful while the Agents
    // tab is showing, so tear it down alongside the list poll (adhoc #259).
    stopRepoAgentTranscriptRefresh();
  }

  // Re-arms a fresh ~10s interval after every successful load, and self-stops
  // the moment it's no longer applicable (repo switched, tab left) rather than
  // trusting whoever started it to remember to clean up on every possible exit path.
  function startRepoAgentsAutoRefresh(repo) {
    stopRepoAgentsAutoRefresh();
    if (!repo) return;
    repoAgentsRefreshTimer = window.setInterval(() => {
      if (!state.selectedRepo || repoKey(state.selectedRepo) !== repoKey(repo) ||
          state.activeRepoTab !== "agents") {
        stopRepoAgentsAutoRefresh();
        return;
      }
      loadRepoAgents(repo, { silent: true });
    }, 10000);
  }

  // silent=true is used by the auto-refresh poll: it re-fetches and re-renders
  // the list in place without ever wiping it back to a "Loading..." placeholder
  // first — doing that unconditionally every 10s was the source of the Agents
  // tab's constant flicker, since the whole panel blanked out and popped back
  // in on every poll even when nothing had changed.
  async function loadRepoAgents(repo, { silent = false } = {}) {
    const container = $("[data-repo-agents]");
    if (!container || !repo) return;
    if (!silent) container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading agent sessions...</div>';
    try {
      const agents = await requestRepoAgentsList(repo);
      const unchanged = silent && JSON.stringify(agents) === JSON.stringify(state.agentsView.agents);
      state.agentsView.agents = agents;
      // Skip the re-render entirely when a background poll comes back
      // identical to what's already on screen — rebuilding the same DOM every
      // 10s still repaints (and can drop focus/caret out of an open prompt
      // input) even though nothing actually changed. Also skip while a detail
      // page is open (adhoc #259): its own transcript poll keeps it live, and
      // rebuilding here would wipe the transcript scroll and prompt caret.
      if (!unchanged && state.agentsView.selectedAgentId == null) renderRepoAgentsList(agents);
      startRepoAgentsAutoRefresh(repo);
    } catch (error) {
      const code = String(error?.message || "");
      stopRepoAgentsAutoRefresh();
      // A background poll failing shouldn't blow away an already-rendered
      // list with an error message — just stop polling quietly and leave the
      // last good render on screen.
      if (silent) return;
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
          text,
        }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) {
        throw new Error(data.error || `HTTP ${response.status}`);
      }
      if (input) input.value = "";
      setHint("Sent to the agent.", "good");
    } catch (error) {
      const code = String(error?.message || "");
      setHint(
        code === "text_required" ? "Write a message before sending."
          : code === "text_too_long" ? "Message is too long."
          : code === "prompt_queue_full" ? "Too many pending messages for this repository — try again shortly."
          : code === "not_authorized" ? "You don't have permission to send messages."
            : "Could not send the message. Please try again.",
        "bad");
    } finally {
      if (submit) submit.disabled = false;
    }
  }

  // Top-of-list composer: queue a "new agent" prompt for this repo (adhoc #266).
  // Reuses the per-agent prompt endpoint with the "new" sentinel agent id, which
  // the owner's node turns into a fresh ad-hoc agent run on drain.
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
    const text = String(input?.value || "").trim();
    if (!text) {
      setHint("Enter a prompt to start an agent.", "bad");
      return;
    }
    if (submit) submit.disabled = true;
    setHint("Starting…");
    try {
      const response = await fetch(`${repoApiBase(repo)}/agents/new/prompt`, {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          ownerAccount: state.session?.nodeName || "",
          text,
          provider: String(providerSelect?.value || ""),
          model: String(modelSelect?.value || ""),
        }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || data.ok === false) {
        throw new Error(data.error || `HTTP ${response.status}`);
      }
      if (input) input.value = "";
      setHint("Sent — the node will start a new agent shortly.", "good");
    } catch (error) {
      const code = String(error?.message || "");
      setHint(
        code === "text_required" ? "Enter a prompt to start an agent."
          : code === "text_too_long" ? "Prompt is too long."
          : code === "prompt_queue_full" ? "Too many pending prompts for this repository — try again shortly."
          : code === "not_authorized" ? "You don't have permission to start agents."
            : "Could not start the agent. Please try again.",
        "bad");
    } finally {
      if (submit) submit.disabled = false;
    }
  }

