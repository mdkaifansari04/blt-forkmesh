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
    // data: URL now, right before signing — the signed content hash has to
    // cover exactly what gets sent.
    let body = String(bodyInput?.value || "");
    const images = form._pendingIssueImages || [];
    for (const img of images) body = body.split(img.id).join(img.dataUrl);
    if (submit) submit.disabled = true;
    setHint("Signing and sending…");
    try {
      await submitWebIssue(repo, title, body, assignAgent, agentModel, agentProvider);
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
          : code === "not_authorized" ? "Only the repository owner or an admin can assign issues to an agent."
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

