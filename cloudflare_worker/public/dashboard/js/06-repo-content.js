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

    treeBody.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading tree...</div>';
    renderRepoExplorer(repo, path, []);
    try {
      const requestedAt = performance.now();
      const data = await fetchJson(repoLiveUrl(repo, "tree", { path }));
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
              <i data-lucide="${isTree ? "folder" : "file"}" class="h-4 w-4 shrink-0 text-muted-foreground"></i>
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
    const labels = Array.isArray(issue.labels)
      ? issue.labels.slice(0, 3).join(", ")
      : parseFrontMatterList(issue.labels).slice(0, 3).join(", ");
    return {
      number,
      title: issue.title || open.title || `issue #${number}`,
      status,
      author: issue.authorName || open.authorName || issue.author || open.author || "unknown",
      date: formatRecordDate(updatedAt || issue.createdAt || open.ts),
      meta: [status, labels, issue.milestone].filter(Boolean).join(" · "),
      body: open.body || issue.body || "",
      wantsAgent: Boolean(issue.wantsAgent),
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
    container.innerHTML = `<div class="px-4 py-3 text-sm text-muted-foreground">Loading ${escapeHtml(config.itemLabel)} #${escapeHtml(number)} from the live mirror...</div>`;
    try {
      // Pulls read with ref:"" so the host serves the forkmesh/pulls branch.
      const refParams = kind === "pulls" ? { ref: "" } : {};
      const blob = await fetchRepoJson(repoLiveUrl(repo, "blob", { path: recordPath, ...refParams }));
      const parsed = parseFrontMatter(blobText(blob));
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
    container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading issues from the live mirror...</div>';
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
      const data = await fetchJson(repoLiveUrl(repo, "history"));
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

  // Composer pinned above the tab bar on every repo tab, not just Agents
  // (adhoc #278): type a prompt and send it to a brand-new agent on the
  // owner's node from wherever they're browsing the repo. The node's prompt
  // drain recognises the "new" sentinel agent id and spins up an ad-hoc run.
  function renderRepoAgentNewComposer() {
    return `
      <div class="mt-4 overflow-hidden rounded-lg border border-border bg-background">
        <form data-repo-agent-new-form class="flex flex-col gap-2 bg-secondary/30 px-4 py-3">
          <input data-repo-agent-new-input type="text" maxlength="8000" placeholder="Start a new agent - enter a prompt" class="h-8 min-w-0 rounded-md border border-border bg-background px-2 text-xs text-foreground outline-none focus:border-primary" />
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
    container.innerHTML = '<div class="px-4 py-3 text-sm text-muted-foreground">Loading agent sessions...</div>';
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
          sessionToken: state.session?.sessionToken || "",
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
      setHint("Sent - the node will start a new agent shortly.", "good");
      if (state.activeRepoTab === "agents") loadRepoAgents(repo);
    } catch (error) {
      const code = String(error?.message || "");
      setHint(
        code === "text_required" ? "Enter a prompt to start an agent."
          : code === "text_too_long" ? "Prompt is too long."
          : code === "prompt_queue_full" ? "Too many pending prompts for this repository - try again shortly."
          : code === "not_authorized" ? "You don't have permission to start agents."
            : "Could not start the agent. Please try again.",
        "bad");
    } finally {
      if (submit) submit.disabled = false;
    }
  }
