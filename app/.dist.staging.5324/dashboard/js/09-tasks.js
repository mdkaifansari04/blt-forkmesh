  // ---------------------------------------------------------------------------
  // Organization tasks. The dashboard uses the same encrypted catalog and
  // lifecycle endpoints as desktop and World; authorization always remains on
  // the server. Filtering and paging stay local because the authorized list is
  // returned as one bounded catalog snapshot.
  const TASKS_PAGE_SIZE = 100;
  const TASK_IMAGE_TYPES = new Set([
    "image/gif", "image/jpeg", "image/png", "image/webp",
  ]);
  const taskAvatarProfiles = new Map();
  const taskAttachmentUrls = new Map();
  const taskDetailsLoaded = new Set();
  let taskToastTimer = 0;
  // Comments and check-in notes being written, keyed by task, so a repaint of
  // the detail pane (a catalog refresh, another member's change) cannot eat an
  // unposted draft.
  const taskCommentDrafts = new Map();
  const taskCheckinNotes = new Map();
  // Tasks whose activity was re-read because a catalog refresh replaced the
  // detail copy; recorded so a relay that returns no history is asked once.
  const taskHistoryRequested = new Set();
  let taskCheckinOpenId = "";
  let taskCheckinBusy = false;
  // query null = the caret is not inside an @token, so no suggestions are open.
  const taskMentionState = { query: null, index: 0, matches: [] };

  function taskAssigneeKind(task) {
    const kind = String(task?.assigneeKind || "").trim().toLowerCase();
    if (["agent", "bot", "claude", "codex"].includes(kind)) return "agent";
    return kind === "unassigned" ? "unassigned" : "user";
  }

  function taskIsDone(task) {
    return task.completedAt > 0 || task.status === "done";
  }

  function taskStatusKey(task) {
    if (taskIsDone(task)) return "done";
    if (task?.status === "active") return "active";
    if (taskAssigneeKind(task) === "agent" && task?.agentSessionId) return "queued";
    return "ready";
  }

  function taskStatusLabel(task) {
    return { done: "Done", active: "In progress", queued: "Queued", ready: "Ready" }[taskStatusKey(task)];
  }

  function taskStatusIcon(task) {
    return { done: "check-circle-2", active: "loader-circle", queued: "bot", ready: "circle-dot" }[taskStatusKey(task)];
  }

  function taskStatusTone(task) {
    return {
      done: "text-emerald-400 border-border bg-background",
      active: "text-primary border-primary/40 bg-primary/10",
      queued: "text-amber-400 border-border bg-background",
      ready: "text-muted-foreground border-border bg-background",
    }[taskStatusKey(task)];
  }

  function taskAssigneeLabel(task) {
    const kind = taskAssigneeKind(task);
    if (kind === "agent") return "Bot";
    if (kind === "unassigned") return "Unassigned";
    return String(task?.assignee || "Unassigned");
  }

  function validTaskAvatarPng(value) {
    const png = String(value || "").trim();
    return png.length <= 90_000 && /^[A-Za-z0-9+/]+={0,2}$/.test(png) ? png : "";
  }

  function taskAvatarHtml(task, size = "h-7 w-7") {
    const kind = taskAssigneeKind(task);
    if (kind === "agent") {
      return `<span class="${size} flex shrink-0 items-center justify-center overflow-hidden rounded-full border border-border bg-secondary text-sm" title="Bot assignee" aria-label="Bot assignee avatar">🤖</span>`;
    }
    if (kind === "unassigned") {
      return `<span class="${size} flex shrink-0 items-center justify-center overflow-hidden rounded-full border border-border bg-secondary text-xs font-semibold text-muted-foreground" title="Unassigned" aria-label="Unassigned">?</span>`;
    }
    const name = String(task?.assignee || "").trim().toLowerCase();
    const initial = Array.from(name)[0]?.toUpperCase() || "?";
    return `<span class="${size} flex shrink-0 items-center justify-center overflow-hidden rounded-full border border-border bg-secondary text-[10px] font-semibold text-muted-foreground" data-task-assignee-avatar="${escapeHtml(name)}" title="@${escapeHtml(name || "member")}" aria-label="@${escapeHtml(name || "member")} avatar">${escapeHtml(initial)}</span>`;
  }

  function taskAvatarForName(name) {
    const account = String(name || "").trim().toLowerCase();
    if (!account) return Promise.resolve("");
    const sessionName = String(state.session?.nodeName || state.session?.email || "").trim().toLowerCase();
    const ownAvatar = validTaskAvatarPng(state.session?.avatarPng);
    if (account === sessionName && ownAvatar) return Promise.resolve(ownAvatar);
    let pending = taskAvatarProfiles.get(account);
    if (!pending) {
      pending = fetchJson(`/api/accounts/${encodeURIComponent(account)}`)
        .then((profile) => {
          if (profile?.exists !== true || profile?.profilePrivate === true) return "";
          return validTaskAvatarPng(profile.avatarPng);
        })
        .catch(() => "");
      taskAvatarProfiles.set(account, pending);
    }
    return pending;
  }

  function hydrateTaskAvatars() {
    document.querySelectorAll("[data-task-assignee-avatar]").forEach((avatar) => {
      const name = String(avatar.dataset.taskAssigneeAvatar || "").trim().toLowerCase();
      if (!name || avatar.dataset.taskAvatarLoading || avatar.dataset.taskAvatarHydrated) return;
      avatar.dataset.taskAvatarLoading = "true";
      void taskAvatarForName(name).then((png) => {
        delete avatar.dataset.taskAvatarLoading;
        if (!avatar.isConnected || !png) return;
        const image = document.createElement("img");
        image.src = `data:image/png;base64,${png}`;
        image.alt = "";
        image.className = "h-full w-full object-cover";
        image.decoding = "async";
        image.addEventListener("error", () => image.remove(), { once: true });
        avatar.replaceChildren(image);
        avatar.dataset.taskAvatarHydrated = "true";
      });
    });
  }

  function taskDate(value, fallback = "—") {
    const milliseconds = Number(value || 0);
    if (!milliseconds) return fallback;
    const date = new Date(milliseconds);
    return Number.isNaN(date.getTime()) ? fallback : date.toLocaleString([], {
      year: "numeric", month: "short", day: "numeric", hour: "numeric", minute: "2-digit",
    });
  }

  function taskElapsed(milliseconds) {
    let seconds = Math.max(0, Math.floor(Number(milliseconds || 0) / 1000));
    const days = Math.floor(seconds / 86400); seconds %= 86400;
    const hours = Math.floor(seconds / 3600); seconds %= 3600;
    const minutes = Math.floor(seconds / 60);
    if (days) return `${days}d ${hours}h`;
    if (hours) return `${hours}h ${minutes}m`;
    return `${minutes}m`;
  }

  function taskFileSize(bytes) {
    const size = Math.max(0, Number(bytes || 0));
    return size >= 1024 * 1024 ? `${(size / 1024 / 1024).toFixed(1)} MiB`
      : size >= 1024 ? `${Math.round(size / 1024)} KiB` : `${size} B`;
  }

  function taskFriendlyError(error) {
    const raw = String(error?.message || error || "Task request failed");
    const messages = {
      active_task_exists: "You already have another task in progress. Stop it before starting this one.",
      active_task_cannot_be_updated: "Stop this task before editing it.",
      assignee_not_team_member: "That assignee is not a member of the selected team.",
      assignee_or_manager_only: "Only the assignee or an organization manager can complete this task.",
      completed_task_cannot_be_updated: "Reopen this task before editing it.",
      invalid_repository: "Use a repository in owner/name format.",
      repository_required: "A repository is required for bot work.",
      task_image_too_large: "Each task image must be 256 KiB or smaller.",
      task_images_too_large: "Task images must total 1 MiB or less.",
      too_many_task_images: "A task can include up to four images.",
    };
    return messages[raw] || raw.replaceAll("_", " ").replace(/^./, (letter) => letter.toUpperCase());
  }

  async function taskRequest(method, path, body = null) {
    const token = state.session?.sessionToken || "";
    const headers = { accept: "application/json" };
    if (token) headers.authorization = `Bearer ${token}`;
    if (body !== null) headers["content-type"] = "application/json";
    const response = await fetch(path, {
      method,
      cache: "no-store",
      credentials: "same-origin",
      headers,
      body: body === null ? undefined : JSON.stringify(body),
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok || payload.ok === false) {
      const error = new Error(payload.error || `HTTP ${response.status}`);
      error.status = response.status;
      error.payload = payload;
      throw error;
    }
    return payload;
  }

  function showTaskToast(message, tone = "good") {
    const toast = $("[data-task-toast]");
    if (!toast) return;
    window.clearTimeout(taskToastTimer);
    toast.textContent = message;
    toast.dataset.tone = tone;
    toast.hidden = false;
    taskToastTimer = window.setTimeout(() => { toast.hidden = true; }, 4000);
  }

  function taskMatchesFilter(task, filter) {
    const done = taskIsDone(task);
    if (filter === "open") return !done;
    if (filter === "done") return done;
    if (filter === "active") return task.status === "active";
    if (filter === "qa") return Number(task.qa?.requestedAt || 0) > 0 && task.qa?.status === "unknown";
    if (filter === "mine") return !done && taskAssigneeKind(task) === "user" && task.assignee === state.tasksView.actor;
    return true;
  }

  function taskMatchesAssignee(task, filter) {
    if (!filter || filter === "all") return true;
    const kind = taskAssigneeKind(task);
    if (filter === "agent" || filter === "unassigned") return kind === filter;
    if (!filter.startsWith("user:")) return true;
    return kind === "user" && task.assignee === filter.slice(5);
  }

  function taskStateCounts() {
    const result = { open: 0, closed: 0, active: 0, queued: 0, qa: 0 };
    for (const task of state.tasksView.items) {
      if (taskIsDone(task)) result.closed += 1; else result.open += 1;
      if (task.status === "active") result.active += 1;
      if (taskStatusKey(task) === "queued") result.queued += 1;
      if (Number(task.qa?.requestedAt || 0) > 0 && task.qa?.status === "unknown") result.qa += 1;
    }
    return result;
  }

  function filteredTasks() {
    const query = String(state.tasksView.query || "").trim().toLowerCase();
    return state.tasksView.items.filter((task) => {
      if (!taskMatchesFilter(task, state.tasksView.filter)) return false;
      if (!taskMatchesAssignee(task, state.tasksView.assignee)) return false;
      if (!query) return true;
      const qa = task.qa || {};
      const agent = task.agent || {};
      return [
        task.title, task.details, task.repository, task.department, task.team,
        task.destination, taskAssigneeLabel(task), task.createdBy, task.id,
        task.completionNote, qa.status, qa.reviewer, qa.howToTest,
        agent.provider, agent.model, agent.mode, agent.strength,
      ].some((value) => String(value || "").toLowerCase().includes(query));
    });
  }

  function taskRowHtml(task) {
    const selected = task.id === state.tasksView.selectedId;
    const updated = relativeTimeLabel(task.updatedAt || task.createdAt);
    const route = [task.department ? `#${task.department}` : "", task.team, task.repository].filter(Boolean);
    const details = String(task.details || "").trim();
    return `<button type="button" class="task-row px-4 py-3.5 sm:px-5" data-task-select="${escapeHtml(task.id)}" aria-current="${selected}">
      <span class="flex items-start gap-3">
        <span class="mt-1.5 flex h-7 w-7 shrink-0 items-center justify-center rounded-md border ${taskStatusTone(task)}"><i data-lucide="${taskStatusIcon(task)}" class="h-3.5 w-3.5${task.status === "active" ? " task-active-icon" : ""}"></i></span>
        <span class="min-w-0 flex-1">
          <span class="flex items-start gap-2">
            <strong class="min-w-0 flex-1 truncate text-sm font-semibold text-foreground">${escapeHtml(task.title)}</strong>
            <span class="shrink-0 rounded border border-border bg-background px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground" title="Priority ${escapeHtml(String(task.priority ?? 50))}">P${escapeHtml(String(task.priority ?? 50))}</span>
          </span>
          ${details ? `<span class="mt-1 block truncate text-xs text-muted-foreground">${escapeHtml(details)}</span>` : ""}
          <span class="mt-2 flex flex-wrap items-center gap-x-2 gap-y-1 text-[11px] text-muted-foreground">
            <span class="inline-flex items-center gap-1">${taskAvatarHtml(task)}<span>${escapeHtml(taskAssigneeLabel(task))}</span></span>
            ${route.map((part) => `<span class="truncate">${escapeHtml(part)}</span>`).join('<span aria-hidden="true">·</span>')}
            ${updated ? `<span class="ml-auto shrink-0">${escapeHtml(updated)}</span>` : ""}
          </span>
        </span>
      </span>
    </button>`;
  }

  function taskDetailCard(icon, label, copy, wide = false) {
    return `<section class="task-detail-card"${wide ? ' data-wide="true"' : ""}><h3 class="task-detail-label"><i data-lucide="${icon}" class="h-3.5 w-3.5"></i>${escapeHtml(label)}</h3><div class="task-detail-copy">${copy || '<span class="text-muted-foreground">—</span>'}</div></section>`;
  }

  function taskRepositoryHtml(repository) {
    const value = String(repository || "").trim();
    const parts = value.split("/");
    return parts.length === 2 && parts.every((part) => /^[A-Za-z0-9._-]{1,100}$/.test(part))
      ? `<a class="text-accent hover:underline" href="/${encodeURIComponent(parts[0])}/${encodeURIComponent(parts[1])}">${escapeHtml(value)}</a>`
      : escapeHtml(value || "—");
  }

  function taskAttachmentsHtml(task) {
    const attachments = Array.isArray(task.attachments) ? task.attachments : [];
    if (!attachments.length) return "";
    const cards = attachments.map((attachment) => {
      const url = String(attachment.url || "");
      const preview = attachment.thumbnail
        ? `<img src="${escapeHtml(attachment.thumbnail)}" alt="" loading="lazy" />`
        : url ? `<span data-task-attachment-preview="${escapeHtml(url)}"><i data-lucide="image" class="h-5 w-5"></i></span>`
          : '<i data-lucide="file-image" class="h-5 w-5"></i>';
      return `<a class="task-attachment" data-task-attachment-link="${escapeHtml(url)}" data-task-attachment-name="${escapeHtml(attachment.name || "Task image")}" ${url ? `href="${escapeHtml(url)}"` : ""} target="_blank" rel="noopener">
        <span class="task-attachment-preview">${preview}</span>
        <span class="block p-2"><strong class="block truncate text-xs text-foreground">${escapeHtml(attachment.name || "Task image")}</strong><small class="mt-0.5 block text-[10px] text-muted-foreground">${escapeHtml(taskFileSize(attachment.size))} · ${escapeHtml(attachment.mime || "image")}</small></span>
      </a>`;
    }).join("");
    return taskDetailCard("paperclip", `Attachments (${attachments.length})`, `<div class="task-attachment-grid">${cards}</div>`, true);
  }

  async function taskAttachmentObjectUrl(path) {
    if (taskAttachmentUrls.has(path)) return taskAttachmentUrls.get(path);
    const token = state.session?.sessionToken || "";
    const response = await fetch(path, {
      credentials: "same-origin",
      cache: "no-store",
      headers: token ? { authorization: `Bearer ${token}` } : {},
    });
    if (!response.ok) throw new Error("Attachment unavailable");
    const objectUrl = URL.createObjectURL(await response.blob());
    taskAttachmentUrls.set(path, objectUrl);
    while (taskAttachmentUrls.size > 32) {
      const oldest = taskAttachmentUrls.keys().next().value;
      URL.revokeObjectURL(taskAttachmentUrls.get(oldest));
      taskAttachmentUrls.delete(oldest);
    }
    return objectUrl;
  }

  function hydrateTaskAttachments(root) {
    root.querySelectorAll?.("[data-task-attachment-preview]").forEach((preview) => {
      const path = preview.dataset.taskAttachmentPreview;
      if (!path || preview.dataset.loading) return;
      preview.dataset.loading = "true";
      void taskAttachmentObjectUrl(path).then((objectUrl) => {
        if (!preview.isConnected) return;
        const image = document.createElement("img");
        image.src = objectUrl;
        image.alt = "";
        image.loading = "lazy";
        preview.replaceWith(image);
        const link = image.closest("[data-task-attachment-link]");
        if (link) link.href = objectUrl;
      }).catch(() => { preview.dataset.loading = "failed"; });
    });
  }

  function openTaskImagePreview(src, name) {
    let dialog = document.querySelector("[data-task-image-preview]");
    if (!dialog) {
      dialog = document.createElement("dialog");
      dialog.className = "task-image-modal p-3 sm:p-5";
      dialog.dataset.taskImagePreview = "true";
      dialog.innerHTML = '<header class="mb-3 flex items-center justify-between gap-3"><strong class="min-w-0 truncate text-sm text-foreground" data-task-image-preview-name></strong><button type="button" class="rounded-md border border-border px-2 py-1 text-xs text-muted-foreground hover:bg-secondary hover:text-foreground" data-task-image-preview-close>Close</button></header><img data-task-image-preview-image alt="" /><p class="mt-3 text-center text-xs text-muted-foreground">Click outside the image or press Escape to close.</p>';
      dialog.querySelector("[data-task-image-preview-close]").addEventListener("click", () => dialog.close());
      dialog.addEventListener("click", (event) => {
        if (event.target === dialog) dialog.close();
      });
      document.body.append(dialog);
    }
    dialog.querySelector("[data-task-image-preview-name]").textContent = name || "Task image";
    const image = dialog.querySelector("[data-task-image-preview-image]");
    image.src = src;
    image.alt = name || "Task image";
    if (!dialog.open) dialog.showModal();
  }

  async function expandTaskAttachment(link) {
    const path = String(link?.dataset.taskAttachmentLink || "");
    let src = link?.querySelector("img")?.src || "";
    if (path) {
      try {
        src = await taskAttachmentObjectUrl(path);
      } catch (_) {}
    }
    if (src) openTaskImagePreview(src, link?.dataset.taskAttachmentName || "Task image");
  }

  function taskHistoryHtml(task) {
    const history = Array.isArray(task.completionHistory) ? task.completionHistory : [];
    if (!history.length) return "";
    const items = [...history].reverse().map((entry) => `<li class="border-t border-border pt-3 first:border-t-0 first:pt-0">
      <div class="flex flex-wrap items-center gap-2 text-xs"><strong class="text-foreground">${escapeHtml(entry.completedBy ? `@${entry.completedBy}` : "Completed")}</strong><time class="text-muted-foreground">${escapeHtml(taskDate(entry.completedAt))}</time><span class="rounded border border-border px-1.5 py-0.5 text-[10px] text-muted-foreground">QA: ${escapeHtml(entry.qaStatus || "unknown")}</span>${entry.qaReviewer ? `<span class="text-muted-foreground">reviewed by @${escapeHtml(entry.qaReviewer)}${entry.qaReviewedAt ? ` · ${escapeHtml(taskDate(entry.qaReviewedAt))}` : ""}</span>` : ""}${entry.undoneAt ? `<span class="rounded border border-border px-1.5 py-0.5 text-[10px] text-amber-400">Reopened by ${escapeHtml(entry.undoneBy ? `@${entry.undoneBy}` : "a member")} · ${escapeHtml(taskDate(entry.undoneAt))}</span>` : ""}</div>
      <p class="mt-2 whitespace-pre-wrap text-xs leading-5 text-muted-foreground">${escapeHtml(entry.note || "No completion note")}</p>
    </li>`).join("");
    return taskDetailCard("history", `Completion history (${history.length})`, `<ol class="grid gap-3">${items}</ol>`, true);
  }

  // ---- Comments, @mentions, check-ins and the activity timeline -------------
  // Every recorded thing that has happened to a task, on one rail. The relay
  // returns lifecycle events in `history` and comment bodies in `replies`; a
  // "replied" event and its reply row share a timestamp, so the row wins and
  // the full comment is shown in place of the event's excerpt.
  const TASK_EVENT_STYLES = {
    created: { icon: "sparkles", label: "created this task", tone: "" },
    updated: { icon: "pencil", label: "edited this task", tone: "" },
    started: { icon: "play", label: "started work", tone: "primary" },
    stopped: { icon: "pause", label: "paused work", tone: "" },
    checkin: { icon: "radio", label: "checked in", tone: "primary" },
    completed: { icon: "check-circle-2", label: "completed this task", tone: "primary" },
    reopened: { icon: "history", label: "reopened this task", tone: "warning" },
    returned: { icon: "undo-2", label: "returned this task to the list", tone: "" },
    qa_requested: { icon: "shield-question", label: "sent this to QA", tone: "" },
    qa_reviewed: { icon: "shield-check", label: "reviewed this in QA", tone: "primary" },
    deleted: { icon: "trash-2", label: "deleted this task", tone: "danger" },
  };
  const TASK_CHECKIN_STATES = [
    ["going_well", "thumbs-up", "Going well", "primary"],
    ["blocked", "circle-alert", "Blocked", "danger"],
    ["needs_help", "life-buoy", "Need help", ""],
  ];
  const TASK_CHECKIN_NOTE_MAX = 500;
  // The same handle shape the relay pings on (its MENTION_RE): a handle, never
  // the local part of an email address.
  const TASK_MENTION_SOURCE = "(^|[^A-Za-z0-9._%+-])@([a-z](?:[a-z0-9-]{0,61}[a-z0-9])?)";

  function taskMentionNames(value) {
    const names = [];
    for (const match of String(value || "").matchAll(new RegExp(TASK_MENTION_SOURCE, "gi"))) {
      const name = match[2].toLowerCase();
      if (!names.includes(name)) names.push(name);
    }
    return names;
  }

  function taskMentionHtml(value) {
    const text = String(value || "");
    let out = "";
    let cursor = 0;
    for (const match of text.matchAll(new RegExp(TASK_MENTION_SOURCE, "gi"))) {
      const start = match.index + match[1].length;
      const name = match[2].toLowerCase();
      out += escapeHtml(text.slice(cursor, start));
      out += `<span class="task-mention"${name === state.tasksView.actor ? ' data-me="true"' : ""}>@${escapeHtml(name)}</span>`;
      cursor = start + 1 + match[2].length;
    }
    return out + escapeHtml(text.slice(cursor));
  }

  function taskTimelineEntries(task) {
    const replies = Array.isArray(task.replies) ? task.replies : [];
    const replyTimes = new Set(replies.map((reply) => Number(reply.createdAt) || 0));
    const events = (Array.isArray(task.history) ? task.history : [])
      .filter((entry) => entry && !(entry.kind === "replied" && replyTimes.has(Number(entry.createdAt) || 0)))
      .map((entry) => ({
        id: String(entry.id || ""),
        // A reply that has scrolled out of the replies window still has its
        // event, so it is drawn as the comment it was rather than dropped.
        kind: entry.kind === "replied" ? "comment" : String(entry.kind || ""),
        actor: String(entry.actor || "").toLowerCase(),
        body: String(entry.summary || ""),
        at: Number(entry.createdAt) || 0,
      }));
    const comments = replies.map((reply) => ({
      id: String(reply.id || ""),
      kind: "comment",
      actor: String(reply.author || "").toLowerCase(),
      body: String(reply.body || ""),
      at: Number(reply.createdAt) || 0,
    }));
    return [...events, ...comments].sort((left, right) => left.at - right.at || left.id.localeCompare(right.id));
  }

  function taskTimeHtml(at) {
    return `<time title="${escapeHtml(taskDate(at))}">${escapeHtml(relativeTimeLabel(at) || taskDate(at))}</time>`;
  }

  function taskCheckinChipHtml(value) {
    const text = String(value || "").trim();
    const key = text.toLowerCase().replace(/[\s-]+/g, "_");
    const known = TASK_CHECKIN_STATES.find(([stateKey]) => key === stateKey || key.startsWith(`${stateKey}_`));
    if (!known) return text ? ` — <span class="task-timeline-copy">${taskMentionHtml(text)}</span>` : "";
    const note = text.slice(known[0].length).replace(/^[\s:·—-]+/, "");
    return ` <span class="task-checkin-chip" data-state="${known[0]}">${escapeHtml(known[2])}</span>${note ? ` <span class="task-timeline-copy">${taskMentionHtml(note)}</span>` : ""}`;
  }

  function taskTimelineEntryHtml(entry, task) {
    const who = entry.actor ? `@${entry.actor}` : "Someone";
    const initial = escapeHtml(Array.from(entry.actor || "?")[0].toUpperCase());
    if (entry.kind === "comment") {
      return `<li class="task-timeline-item" data-kind="comment">
        <span class="task-timeline-marker" data-task-assignee-avatar="${escapeHtml(entry.actor)}" title="${escapeHtml(who)}">${initial}</span>
        <div class="task-comment"><div class="task-comment-head"><strong>${escapeHtml(who)}</strong><span>commented</span>${taskTimeHtml(entry.at)}</div><p class="task-comment-body">${taskMentionHtml(entry.body)}</p></div>
      </li>`;
    }
    const style = TASK_EVENT_STYLES[entry.kind] || { icon: "dot", label: entry.kind.replaceAll("_", " ") || "updated this task", tone: "" };
    let detail = "";
    if (entry.kind === "checkin") detail = taskCheckinChipHtml(entry.body);
    else if (entry.body && entry.body !== task.title) detail = ` — <span class="task-timeline-copy">${taskMentionHtml(entry.body)}</span>`;
    return `<li class="task-timeline-item" data-kind="${escapeHtml(entry.kind)}">
      <span class="task-timeline-marker"${style.tone ? ` data-tone="${style.tone}"` : ""}><i data-lucide="${escapeHtml(style.icon)}" class="h-3 w-3"></i></span>
      <p class="task-timeline-line"><strong>${escapeHtml(who)}</strong> ${escapeHtml(style.label)}${detail} · ${taskTimeHtml(entry.at)}</p>
    </li>`;
  }

  function taskActivityHtml(task) {
    const entries = taskTimelineEntries(task);
    const pending = !Array.isArray(task.history) && !taskDetailsLoaded.has(task.id);
    const timeline = entries.length
      ? `<ol class="task-timeline">${entries.map((entry) => taskTimelineEntryHtml(entry, task)).join("")}</ol>`
      : `<p class="task-comment-hint mb-3">${pending ? "Loading activity…" : "Nothing has happened to this task yet."}</p>`;
    const count = Number(task.replyCount || (Array.isArray(task.replies) ? task.replies.length : 0));
    const composer = `<form data-task-reply-form="${escapeHtml(task.id)}" class="task-comment-form">
      <div class="task-mention-field"><textarea data-task-reply-input data-task-mention-input="${escapeHtml(task.id)}" maxlength="2000" rows="3" required autocomplete="off" placeholder="Leave a comment — type @ to ping a teammate" class="task-input">${escapeHtml(taskCommentDrafts.get(task.id) || "")}</textarea><div data-task-mention-list class="task-mention-list" role="listbox" aria-label="Mention a teammate" hidden></div></div>
      <div class="task-comment-footer"><span class="task-comment-hint">Private to your organization. Everyone on the task is pinged; Ctrl+Enter posts.</span><button type="submit" class="task-action-button" data-tone="primary"><i data-lucide="send" class="h-3.5 w-3.5"></i>Comment</button></div>
    </form>`;
    return taskDetailCard("messages-square", `Activity · ${count} comment${count === 1 ? "" : "s"}`, timeline + composer, true);
  }

  function taskCanCheckIn(task) {
    return Boolean(task) && task.status === "active" && !taskIsDone(task)
      && taskAssigneeKind(task) === "user" && task.assignee === state.tasksView.actor;
  }

  function taskCheckinHtml(task) {
    if (taskCheckinOpenId !== task.id || !taskCanCheckIn(task)) return "";
    const buttons = TASK_CHECKIN_STATES.map(([key, icon, label, tone]) => `<button type="button" class="task-action-button" data-task-checkin-state="${key}"${tone ? ` data-tone="${tone}"` : ""}${taskCheckinBusy ? " disabled" : ""}><i data-lucide="${icon}" class="h-3.5 w-3.5"></i>${escapeHtml(label)}</button>`).join("");
    return taskDetailCard("radio", "Check in", `<p class="task-comment-hint">Tell your team how this is going. Everyone on the task is pinged, and so is anyone you @mention.</p>
      <div class="task-mention-field mt-3"><input data-task-checkin-note data-task-mention-input="${escapeHtml(task.id)}" maxlength="${TASK_CHECKIN_NOTE_MAX}" autocomplete="off" value="${escapeHtml(taskCheckinNotes.get(task.id) || "")}" placeholder="Optional note — @mention whoever should know" class="task-input" /><div data-task-mention-list class="task-mention-list" role="listbox" aria-label="Mention a teammate" hidden></div></div>
      <div class="task-checkin-states mt-3">${buttons}<button type="button" class="task-action-button" data-task-checkin-cancel>Cancel</button></div>`, true);
  }

  // Candidates come from what this viewer can already read — the roster the
  // editor loads, assignees, authors and timeline actors — so the composer
  // needs no organization lookup of its own.
  function taskMentionCandidates() {
    const names = new Set();
    const add = (value) => {
      const raw = value && typeof value === "object" ? (value.name || value.username || value.account) : value;
      const name = String(raw || "").trim().toLowerCase();
      if (name !== state.tasksView.actor && /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(name)) names.add(name);
    };
    (Array.isArray(state.tasksView.members) ? state.tasksView.members : []).forEach(add);
    for (const task of state.tasksView.items) {
      if (taskAssigneeKind(task) === "user") add(task.assignee);
      add(task.createdBy);
      (Array.isArray(task.replies) ? task.replies : []).forEach((reply) => add(reply.author));
      (Array.isArray(task.history) ? task.history : []).forEach((entry) => add(entry.actor));
    }
    return [...names].sort();
  }

  function taskMentionQueryAt(input) {
    const caret = input.selectionStart ?? input.value.length;
    const match = /(^|[^A-Za-z0-9._%+-])@([a-z0-9-]*)$/i.exec(input.value.slice(0, caret));
    return match ? match[2].toLowerCase() : null;
  }

  function renderTaskMentionList(input) {
    const list = input.closest(".task-mention-field")?.querySelector("[data-task-mention-list]");
    if (!list) return;
    const query = taskMentionState.query;
    const matches = query === null ? [] : taskMentionCandidates().filter((name) => name.startsWith(query)).slice(0, 6);
    taskMentionState.matches = matches;
    if (!matches.length) { list.hidden = true; list.innerHTML = ""; return; }
    taskMentionState.index = Math.min(taskMentionState.index, matches.length - 1);
    list.innerHTML = matches.map((name, index) => `<button type="button" role="option" tabindex="-1" class="task-mention-option" data-task-mention-pick="${escapeHtml(name)}" aria-selected="${index === taskMentionState.index}">@${escapeHtml(name)}</button>`).join("");
    list.hidden = false;
  }

  function rememberTaskDraft(input) {
    const store = input.matches("[data-task-checkin-note]") ? taskCheckinNotes : taskCommentDrafts;
    const taskId = input.dataset.taskMentionInput || "";
    if (input.value) store.set(taskId, input.value);
    else store.delete(taskId);
  }

  function insertTaskMention(input, name) {
    const caret = input.selectionStart ?? input.value.length;
    const before = input.value.slice(0, caret).replace(/@([a-z0-9-]*)$/i, `@${name} `);
    input.value = before + input.value.slice(caret);
    input.focus();
    input.setSelectionRange(before.length, before.length);
    rememberTaskDraft(input);
    taskMentionState.query = null;
    renderTaskMentionList(input);
  }

  function taskPingedCopy(lead, body) {
    const named = taskMentionNames(body).filter((name) => name !== state.tasksView.actor);
    return named.length
      ? `${lead}. The task's team was pinged, and so was ${named.map((name) => `@${name}`).join(", ")}.`
      : `${lead}. The task's team was pinged.`;
  }

  function taskActionHtml(action, icon, label, enabled = true, tone = "") {
    return `<button type="button" class="task-action-button" data-task-action="${action}"${tone ? ` data-tone="${tone}"` : ""}${enabled ? "" : " disabled"}><i data-lucide="${icon}" class="h-3.5 w-3.5"></i>${escapeHtml(label)}</button>`;
  }

  function taskActionsHtml(task) {
    const actor = state.tasksView.actor;
    const manager = state.tasksView.canManage;
    const done = taskIsDone(task);
    const active = task.status === "active";
    const mine = taskAssigneeKind(task) === "user" && task.assignee === actor;
    const creator = task.createdBy === actor;
    const reviewer = task.qa?.reviewer === actor;
    const agent = taskAssigneeKind(task) === "agent";
    return [
      taskActionHtml("edit", "pencil", "Edit", manager && !active && !done),
      taskActionHtml("assign", "bot", agent ? "Bot assigned" : "Assign bot", manager && !active && !done && Boolean(task.repository) && !agent),
      taskActionHtml(active ? "stop" : "start", active ? "square" : "play", active ? "Stop" : "Start", mine && !done, active ? "" : "primary"),
      taskActionHtml("checkin", "radio", "Check in", taskCanCheckIn(task)),
      taskActionHtml("complete", "check-circle-2", "Complete", !done && (mine || manager), "primary"),
      taskActionHtml("qa", "shield-check", "Send to QA", !done && manager),
      taskActionHtml("return", "undo-2", "Return to list", !done && agent && (manager || creator)),
      taskActionHtml("undo", "history", "Mark undone", done && (manager || mine || creator || reviewer)),
      taskActionHtml("followup", "git-branch", "Follow up", manager || mine || creator),
      taskActionHtml("copy", "copy", "Copy prompt", true),
      taskActionHtml("delete", "trash-2", "Delete", manager, "danger"),
    ].join("");
  }

  function renderTaskDetail() {
    const detail = $("[data-tasks-detail]");
    if (!detail) return;
    const task = state.tasksView.items.find((item) => item.id === state.tasksView.selectedId);
    if (!task) {
      detail.innerHTML = '<div class="flex min-h-[28rem] items-center justify-center p-8 text-center text-sm text-muted-foreground"><div><i data-lucide="clipboard-list" class="mx-auto mb-3 h-8 w-8 opacity-60"></i><p>Select a task to inspect every detail and manage its lifecycle.</p></div></div>';
      window.lucide?.createIcons();
      return;
    }
    const qa = task.qa || {};
    const agent = task.agent || {};
    const routing = [task.department && `#${task.department}`, task.team && `team:${task.team}`, task.destination && `to:${task.destination}`].filter(Boolean);
    const updated = relativeTimeLabel(task.updatedAt || task.createdAt);
    const checkin = task.lastCheckin;
    const agentLines = [
      agent.provider && `Provider: ${agent.provider}`, agent.model && `Model: ${agent.model}`,
      agent.mode && `Mode: ${agent.mode}`, agent.strength && `Reasoning: ${agent.strength}`,
      agent.status && `Run status: ${agent.status}`, agent.startedBy && `Started by: ${agent.startedBy}`,
      agent.finishedBy && `Finished by: ${agent.finishedBy}`,
      (task.agentSessionId || agent.sessionId) && `Session: ${task.agentSessionId || agent.sessionId}`,
    ].filter(Boolean);
    const qaLines = [
      `Status: ${qa.status || "unknown"}`, qa.reviewer && `Reviewer: @${qa.reviewer}`,
      qa.requestedAt && `Requested: ${taskDate(qa.requestedAt)}`, qa.reviewedAt && `Reviewed: ${taskDate(qa.reviewedAt)}`,
    ].filter(Boolean);
    detail.innerHTML = `<article data-task-detail-id="${escapeHtml(task.id)}">
      <header class="border-b border-border p-4 sm:p-5">
        <div class="flex items-start gap-3">${taskAvatarHtml(task, "h-10 w-10")}<div class="min-w-0 flex-1"><div class="flex flex-wrap items-center gap-2"><span class="inline-flex items-center gap-1.5 rounded-full border px-2 py-1 text-[10px] font-semibold ${taskStatusTone(task)}"><i data-lucide="${taskStatusIcon(task)}" class="h-3 w-3"></i>${escapeHtml(taskStatusLabel(task))}</span><span class="rounded border border-border bg-background px-2 py-1 font-mono text-[10px] text-muted-foreground">Priority ${escapeHtml(String(task.priority ?? 50))}</span>${task.kind === "bid" ? '<span class="rounded border border-border px-2 py-1 text-[10px] text-amber-400">Bounty bid</span>' : ""}</div><h2 class="mt-2 text-lg font-semibold leading-6 text-foreground">${escapeHtml(task.title || "Organization task")}</h2><p class="mt-1 font-mono text-[10px] text-muted-foreground">${escapeHtml(task.id)}</p></div></div>
        <div class="task-action-bar mt-4">${taskActionsHtml(task)}</div>
      </header>
      <div class="task-detail-grid p-4 sm:p-5">
        ${taskCheckinHtml(task)}
        ${taskDetailCard("align-left", "Details", `<p>${task.details ? taskMentionHtml(task.details) : "No details provided."}</p>`, true)}
        ${taskDetailCard("route", "Routing", `<div class="flex flex-wrap gap-1.5">${routing.map((value) => `<span class="rounded border border-border bg-background px-2 py-1 text-xs">${escapeHtml(value)}</span>`).join("") || "—"}</div><p class="mt-2 text-xs text-muted-foreground">Repository: ${taskRepositoryHtml(task.repository)}</p>`)}
        ${taskDetailCard("user-round", "Ownership", `<div class="flex items-center gap-2">${taskAvatarHtml(task, "h-7 w-7")}<span>${escapeHtml(taskAssigneeLabel(task))}</span></div><p class="mt-2 text-xs text-muted-foreground">Created by ${task.createdBy ? `@${escapeHtml(task.createdBy)}` : "—"}${task.parentTaskId ? `<br>Follows task <span class="font-mono">${escapeHtml(task.parentTaskId)}</span>` : ""}</p>`)}
        ${taskDetailCard("clock-3", "Timing", `<p>Created ${escapeHtml(taskDate(task.createdAt))}</p><p class="mt-1 text-xs text-muted-foreground">Updated ${escapeHtml(taskDate(task.updatedAt))}${updated ? ` · ${escapeHtml(updated)}` : ""}<br>Tracked time ${escapeHtml(taskElapsed(task.elapsedMs))}${task.startedAt ? `<br>Started ${escapeHtml(taskDate(task.startedAt))}` : ""}${task.nextCheckinAt ? `<br>Next check-in ${escapeHtml(taskDate(task.nextCheckinAt))}` : ""}${task.completedAt ? `<br>Completed ${escapeHtml(taskDate(task.completedAt))}` : ""}</p>`)}
        ${taskDetailCard("shield-check", "Quality assurance", `<p>${qaLines.map(escapeHtml).join("<br>") || "Not requested"}</p>${qa.howToTest ? `<p class="mt-3 border-t border-border pt-3 text-xs text-muted-foreground"><strong class="text-foreground">How to test</strong><br>${escapeHtml(qa.howToTest)}</p>` : ""}`)}
        ${agentLines.length ? taskDetailCard("bot", "Agent run", agentLines.map(escapeHtml).join("<br>")) : ""}
        ${task.bountyRequest ? taskDetailCard("badge-dollar-sign", "Bounty request", `<p>${escapeHtml(task.bountyRequest.amountSol || "—")} ${escapeHtml(task.bountyRequest.currency || "SOL")}</p><p class="mt-1 text-xs capitalize text-muted-foreground">${escapeHtml(task.bountyRequest.status || "requested")}</p>`) : ""}
        ${checkin ? taskDetailCard("activity", "Latest check-in", `<p class="capitalize">${escapeHtml(String(checkin.state || "").replaceAll("_", " "))}</p><p class="mt-2 text-xs text-muted-foreground">${escapeHtml(checkin.note || "No note")} · ${escapeHtml(taskDate(checkin.at))}</p>`) : ""}
        ${task.completionNote ? taskDetailCard("badge-check", "Completion note", `<p>${escapeHtml(task.completionNote)}</p>`, true) : ""}
        ${taskAttachmentsHtml(task)}
        ${taskHistoryHtml(task)}
        ${taskActivityHtml(task)}
      </div>
    </article>`;
    window.lucide?.createIcons();
    hydrateTaskAvatars();
    hydrateTaskAttachments(detail);
    // A catalog refresh swaps in the list copy, which carries no timeline.
    if (!Array.isArray(task.history) && taskDetailsLoaded.has(task.id) && !taskHistoryRequested.has(task.id)) {
      taskHistoryRequested.add(task.id);
      taskDetailsLoaded.delete(task.id);
      void loadSelectedTaskDetail();
    }
  }

  function renderTasksPage() {
    const list = $("[data-tasks-list]");
    if (!list) return;
    const summary = $("[data-tasks-summary]");
    const count = $("[data-tasks-count]");
    const prev = $("[data-tasks-prev]");
    const next = $("[data-tasks-next]");
    const pageList = $("[data-tasks-pages]");
    for (const button of $$("[data-tasks-filter]")) {
      const active = button.dataset.tasksFilter === state.tasksView.filter;
      button.setAttribute("aria-pressed", String(active));
      button.classList.toggle("bg-secondary", active);
      button.classList.toggle("text-foreground", active);
    }
    const assigneeFilter = $("[data-tasks-assignee-filter]");
    if (assigneeFilter) {
      const choices = [
        ["all", "All assignees"],
        ...state.tasksView.members.map((name) => [`user:${name}`, `@${name}`]),
        ["agent", "Bot"],
        ["unassigned", "Unassigned"],
      ];
      if (!choices.some(([value]) => value === state.tasksView.assignee)) {
        state.tasksView.assignee = "all";
      }
      assigneeFilter.innerHTML = choices
        .map(([value, label]) => `<option value="${escapeHtml(value)}">${escapeHtml(label)}</option>`)
        .join("");
      assigneeFilter.value = state.tasksView.assignee;
    }
    const manager = $("[data-tasks-manager]");
    const create = $("[data-tasks-new]");
    const clear = $("[data-tasks-delete-closed]");
    if (manager) manager.hidden = !state.tasksView.canManage;
    if (create) create.hidden = !state.tasksView.canManage;

    if (state.tasksView.loading || state.tasksView.error) {
      list.innerHTML = state.tasksView.loading
        ? `<div class="px-4 py-8 text-sm text-muted-foreground sm:px-5">${loadingHtml("Loading organization tasks...")}</div>`
        : `<div class="px-4 py-8 text-sm text-muted-foreground sm:px-5"><i data-lucide="lock-keyhole" class="mb-3 h-5 w-5"></i>${escapeHtml(state.tasksView.error)}</div>`;
      if (summary) summary.textContent = "";
      if (count) count.textContent = state.tasksView.loading ? "Loading" : "Unavailable";
      if (pageList) pageList.innerHTML = "";
      [prev, next].forEach((button) => { if (button) button.disabled = true; });
      if (clear) clear.hidden = true;
      $$("[data-tasks-stat]").forEach((node) => { node.textContent = "—"; });
      renderTaskDetail();
      window.lucide?.createIcons();
      return;
    }

    const counts = taskStateCounts();
    if (clear) clear.hidden = !state.tasksView.canManage || counts.closed === 0;
    if (count) count.textContent = `${formatCount(counts.open)} open · ${formatCount(counts.closed)} closed`;
    for (const key of ["open", "active", "queued", "qa"]) {
      const node = $(`[data-tasks-stat="${key}"]`);
      if (node) node.textContent = formatCount(counts[key]);
    }

    const matches = filteredTasks();
    const pages = Math.max(1, Math.ceil(matches.length / TASKS_PAGE_SIZE));
    state.tasksView.page = Math.min(Math.max(1, state.tasksView.page), pages);
    const start = (state.tasksView.page - 1) * TASKS_PAGE_SIZE;
    const end = Math.min(start + TASKS_PAGE_SIZE, matches.length);
    const visibleMatches = matches.slice(start, end);
    if (visibleMatches.length && !visibleMatches.some((task) => task.id === state.tasksView.selectedId)) {
      state.tasksView.selectedId = visibleMatches[0].id;
    }
    list.innerHTML = matches.length ? visibleMatches.map(taskRowHtml).join("")
      : '<div class="px-4 py-10 text-center text-sm text-muted-foreground sm:px-5"><i data-lucide="search-x" class="mx-auto mb-3 h-6 w-6"></i>No tasks match this view.</div>';
    if (summary) summary.textContent = matches.length
      ? `${formatCount(counts.open)} open · ${formatCount(counts.closed)} closed · showing ${start + 1}–${end} of ${formatCount(matches.length)} · page ${state.tasksView.page} of ${pages}`
      : "No matching tasks";
    if (prev) prev.disabled = state.tasksView.page <= 1;
    if (next) next.disabled = state.tasksView.page >= pages;
    if (pageList) {
      const visiblePages = [...new Set([1, state.tasksView.page - 1, state.tasksView.page, state.tasksView.page + 1, pages])]
        .filter((page) => page >= 1 && page <= pages).sort((a, b) => a - b);
      pageList.innerHTML = visiblePages.map((page, index) => {
        const gap = index && page - visiblePages[index - 1] > 1 ? '<span class="px-1 text-xs text-muted-foreground">…</span>' : "";
        const active = page === state.tasksView.page;
        return `${gap}<button type="button" data-tasks-page="${page}" class="h-8 min-w-8 rounded-md border px-2 font-mono text-xs ${active ? "border-border bg-secondary text-foreground" : "border-transparent text-muted-foreground hover:border-border hover:bg-secondary"}"${active ? ' aria-current="page"' : ""}>${page}</button>`;
      }).join("");
    }
    renderTaskDetail();
    window.lucide?.createIcons();
    hydrateTaskAvatars();
    void loadSelectedTaskDetail();
  }

  function replaceTask(task, select = true) {
    if (!task?.id) return;
    const index = state.tasksView.items.findIndex((item) => item.id === task.id);
    if (index >= 0) state.tasksView.items[index] = task;
    else state.tasksView.items.unshift(task);
    if (select) state.tasksView.selectedId = task.id;
  }

  async function loadSelectedTaskDetail() {
    const taskId = state.tasksView.selectedId;
    if (!taskId || taskDetailsLoaded.has(taskId)) return;
    taskDetailsLoaded.add(taskId);
    try {
      const payload = await taskRequest("GET", `/api/tasks/${taskId}`);
      replaceTask(payload.task, false);
      renderTasksPage();
    } catch (error) {
      taskDetailsLoaded.delete(taskId);
      showTaskToast(`Could not load task activity: ${taskFriendlyError(error)}`, "bad");
    }
  }

  async function fetchOrganizationTaskCatalog() {
    let payload = null;
    const tasks = [];
    let offset = 0;
    do {
      const separator = offset ? `?offset=${offset}` : "";
      const page = await fetchJson(`/api/tasks${separator}`, { fresh: true });
      if (!payload) payload = page;
      tasks.push(...(Array.isArray(page?.tasks) ? page.tasks : []));
      const next = Number(page?.nextOffset);
      if (page?.hasMore !== true || !Number.isInteger(next) || next <= offset) break;
      offset = next;
    } while (tasks.length < Number(payload?.taskLimit || 2000));
    return { ...(payload || {}), tasks };
  }

  async function loadOrganizationTasks({ quiet = false } = {}) {
    if (!state.session) {
      state.tasksView.loading = false;
      state.tasksView.error = "Sign in with an organization member account to manage the private task catalog.";
      renderTasksPage();
      return;
    }
    if (!quiet) state.tasksView.loading = true;
    state.tasksView.error = "";
    renderTasksPage();
    try {
      const payload = await fetchOrganizationTaskCatalog();
      state.tasksView.items = (Array.isArray(payload?.tasks) ? payload.tasks : []).filter((task) => task && typeof task === "object");
      taskDetailsLoaded.clear();
      state.tasksView.actor = String(payload?.actor || "").toLowerCase();
      state.tasksView.canManage = payload?.canManage === true;
      state.tasksView.members = Array.isArray(payload?.members) ? payload.members : [];
      state.tasksView.departments = Array.isArray(payload?.departments) ? payload.departments : [];
      state.tasksView.teams = Array.isArray(payload?.teams) ? payload.teams : [];
      state.tasksView.serverNow = Number(payload?.serverNow || 0);
      if (!state.tasksView.items.some((task) => task.id === state.tasksView.selectedId)) state.tasksView.selectedId = "";
    } catch (error) {
      if (!quiet) state.tasksView.items = [];
      state.tasksView.error = error?.status === 401 || error?.status === 403
        ? "This task catalog is private to current organization members."
        : `Tasks are unavailable: ${taskFriendlyError(error)}`;
    } finally {
      state.tasksView.loading = false;
      renderTasksPage();
      if (!state.tasksView.error) void loadSelectedTaskDetail();
    }
  }

  function fillTaskEditorOptions() {
    const department = $('[data-task-field="department"]');
    const team = $('[data-task-field="team"]');
    const assignee = $('[data-task-field="assignee"]');
    if (department) department.innerHTML = (state.tasksView.departments.length ? state.tasksView.departments : ["general", "engineering"])
      .map((value) => `<option value="${escapeHtml(value)}">${escapeHtml(value.replaceAll("-", " "))}</option>`).join("");
    if (team) team.innerHTML = '<option value="">No specific team</option>' + state.tasksView.teams
      .map((value) => `<option value="${escapeHtml(value)}">${escapeHtml(value)}</option>`).join("");
    if (assignee) assignee.innerHTML = '<option value="unassigned">Unassigned</option><option value="agent">Bot</option>' + state.tasksView.members
      .map((value) => `<option value="user:${escapeHtml(value)}">@${escapeHtml(value)}</option>`).join("");
  }

  function taskField(name) { return $(`[data-task-field="${name}"]`); }

  function openTaskEditor(mode, task = null) {
    const dialog = $("[data-task-editor]");
    const form = $("[data-task-editor-form]");
    if (!dialog || !form) return;
    fillTaskEditorOptions();
    form.reset();
    dialog.dataset.mode = mode;
    dialog.dataset.taskId = task?.id || "";
    const followup = mode === "followup";
    $("[data-task-editor-title]").textContent = mode === "edit" ? "Edit organization task" : followup ? "Create follow-up task" : "New organization task";
    $("[data-task-editor-subtitle]").textContent = followup
      ? "Routing and assignment are inherited securely from the parent task."
      : mode === "edit" ? "Update task copy, assignment, repository, and priority." : "Route work to a member, the shared bot queue, or leave it unassigned.";
    $("[data-task-editor-save]").textContent = mode === "edit" ? "Save changes" : followup ? "Create follow-up" : "Create task";
    for (const item of $$('[data-task-create-only]')) item.hidden = mode === "edit" || followup;
    for (const name of ["assignee", "priority", "repository"]) {
      const field = taskField(name)?.closest("label");
      if (field) field.hidden = followup;
    }
    taskField("title").value = mode === "edit" ? task?.title || "" : "";
    taskField("details").value = mode === "edit" ? task?.details || "" : "";
    taskField("department").value = task?.department || state.tasksView.departments[0] || "general";
    taskField("team").value = task?.team || "";
    // No flagship repository to prefill here: BLT hosts many, so a new task
    // starts empty and the field's own "owner/repository" placeholder leads.
    taskField("repository").value = task?.repository || "";
    taskField("priority").value = String(task?.priority ?? 50);
    taskField("howToTest").value = task?.qa?.howToTest || "";
    const assignment = taskAssigneeKind(task) === "user" ? `user:${task?.assignee || state.tasksView.actor}` : taskAssigneeKind(task);
    taskField("assignee").value = mode === "create" ? "unassigned" : assignment;
    const status = $("[data-task-editor-status]");
    status.hidden = true; status.textContent = "";
    dialog.showModal();
    taskField("title")?.focus();
    window.lucide?.createIcons();
  }

  function fileAsBase64(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.addEventListener("load", () => resolve(String(reader.result || "").split(",", 2)[1] || ""), { once: true });
      reader.addEventListener("error", () => reject(new Error("Could not read task image.")), { once: true });
      reader.readAsDataURL(file);
    });
  }

  async function taskEditorAttachments() {
    const files = [...(taskField("attachments")?.files || [])];
    if (files.length > 4) throw new Error("A task can include up to four images.");
    let total = 0;
    return Promise.all(files.map(async (file) => {
      if (!TASK_IMAGE_TYPES.has(file.type)) throw new Error("Tasks accept PNG, JPEG, GIF, or WebP images.");
      if (file.size > 256 * 1024) throw new Error("Each task image must be 256 KiB or smaller.");
      total += file.size;
      if (total > 1024 * 1024) throw new Error("Task images must total 1 MiB or less.");
      return { name: file.name, mime: file.type, size: file.size, file: await fileAsBase64(file) };
    }));
  }

  async function saveTaskEditor() {
    const dialog = $("[data-task-editor]");
    const save = $("[data-task-editor-save]");
    const status = $("[data-task-editor-status]");
    const mode = dialog?.dataset.mode || "create";
    const taskId = dialog?.dataset.taskId || "";
    const title = String(taskField("title")?.value || "").trim();
    if (!title) return;
    save.disabled = true; save.textContent = mode === "edit" ? "Saving…" : "Creating…";
    status.hidden = true;
    try {
      let body;
      if (mode === "followup") {
        body = { title, details: String(taskField("details")?.value || "").trim(), parentTaskId: taskId };
      } else {
        const assignment = String(taskField("assignee")?.value || "unassigned");
        const kind = assignment === "agent" ? "agent" : assignment.startsWith("user:") ? "user" : "unassigned";
        body = {
          title,
          details: String(taskField("details")?.value || "").trim(),
          repository: String(taskField("repository")?.value || "").trim(),
          priority: Math.max(1, Math.min(99, Number(taskField("priority")?.value || 50))),
          assigneeKind: kind,
          ...(kind === "user" ? { assignee: assignment.slice(5) } : {}),
        };
        if (mode === "create") Object.assign(body, {
          department: String(taskField("department")?.value || "general"),
          team: String(taskField("team")?.value || ""),
          destination: kind === "agent" ? "agent" : "department",
          howToTest: String(taskField("howToTest")?.value || "").trim(),
          attachments: await taskEditorAttachments(),
        });
      }
      const payload = await taskRequest(mode === "edit" ? "PATCH" : "POST", mode === "edit" ? `/api/tasks/${taskId}` : "/api/tasks", body);
      replaceTask(payload.task);
      taskDetailsLoaded.add(payload.task.id);
      dialog.close();
      state.tasksView.filter = mode === "edit" ? state.tasksView.filter : "all";
      state.tasksView.page = 1;
      renderTasksPage();
      showTaskToast(mode === "edit" ? "Task updated." : mode === "followup" ? "Follow-up task created." : "Task created.");
    } catch (error) {
      status.textContent = taskFriendlyError(error);
      status.hidden = false;
    } finally {
      save.disabled = false;
      save.textContent = mode === "edit" ? "Save changes" : mode === "followup" ? "Create follow-up" : "Create task";
    }
  }

  function selectedTask() {
    return state.tasksView.items.find((task) => task.id === state.tasksView.selectedId) || null;
  }

  function openTaskActionDialog(action, task = selectedTask()) {
    const dialog = $("[data-task-action-dialog]");
    if (!dialog) return;
    const config = {
      complete: ["Complete task", task?.title || "", "Completion note and evidence", task?.completionNote || "", "Complete task", ""],
      qa: ["Send task to QA", task?.title || "", "Exact QA steps", task?.qa?.howToTest || "", "Send to QA", ""],
      delete: ["Delete task", task?.title || "", "", "", "Delete permanently", "This permanently removes the task, its replies, check-ins, QA reviews, and attachments."],
      clear: ["Clear closed tasks", "Remove every completed task", "", "", "Delete closed tasks", `This permanently removes ${taskStateCounts().closed} closed task${taskStateCounts().closed === 1 ? "" : "s"}.`],
    }[action];
    if (!config) return;
    dialog.dataset.action = action;
    dialog.dataset.taskId = task?.id || "";
    $("[data-task-action-title]").textContent = config[0];
    $("[data-task-action-subtitle]").textContent = config[1];
    $("[data-task-action-label]").textContent = config[2];
    $("[data-task-action-copy]").value = config[3];
    $("[data-task-action-copy]").closest("label").hidden = !config[2];
    $("[data-task-action-submit]").textContent = config[4];
    $("[data-task-action-submit]").dataset.tone = action === "delete" || action === "clear" ? "danger" : "primary";
    const warning = $("[data-task-action-warning]"); warning.textContent = config[5]; warning.hidden = !config[5];
    const status = $("[data-task-action-status]"); status.textContent = ""; status.hidden = true;
    dialog.showModal();
    if (config[2]) $("[data-task-action-copy]").focus();
    window.lucide?.createIcons();
  }

  async function mutateTask(action, task = selectedTask()) {
    if (!task || state.tasksView.busyId) return;
    state.tasksView.busyId = task.id;
    renderTaskDetail();
    try {
      let method = "POST";
      let path = `/api/tasks/${task.id}/${action}`;
      let body = {};
      if (action === "assign") {
        method = "PATCH"; path = `/api/tasks/${task.id}`;
        body = { assigneeKind: "agent", repository: task.repository };
      }
      const payload = await taskRequest(method, path, body);
      replaceTask(payload.task);
      taskDetailsLoaded.add(payload.task.id);
      renderTasksPage();
      showTaskToast({ start: "Task started.", stop: "Task stopped.", assign: "Task added to the bot queue.", return: "Task returned to the list.", undo: "Task reopened." }[action] || "Task updated.");
    } catch (error) {
      showTaskToast(taskFriendlyError(error), "bad");
    } finally {
      state.tasksView.busyId = "";
      renderTaskDetail();
    }
  }

  async function submitTaskActionDialog() {
    const dialog = $("[data-task-action-dialog]");
    const action = dialog?.dataset.action;
    const taskId = dialog?.dataset.taskId;
    const submit = $("[data-task-action-submit]");
    const status = $("[data-task-action-status]");
    submit.disabled = true; status.hidden = true;
    try {
      if (action === "clear") {
        const closed = state.tasksView.items.filter(taskIsDone);
        for (let index = 0; index < closed.length; index += 1) {
          submit.textContent = `Deleting ${index + 1} of ${closed.length}…`;
          await taskRequest("DELETE", `/api/tasks/${closed[index].id}`);
          state.tasksView.items = state.tasksView.items.filter((task) => task.id !== closed[index].id);
        }
        if (!state.tasksView.items.some((task) => task.id === state.tasksView.selectedId)) state.tasksView.selectedId = "";
        showTaskToast(`Deleted ${closed.length} closed task${closed.length === 1 ? "" : "s"}.`);
      } else if (action === "delete") {
        await taskRequest("DELETE", `/api/tasks/${taskId}`);
        state.tasksView.items = state.tasksView.items.filter((task) => task.id !== taskId);
        if (state.tasksView.selectedId === taskId) state.tasksView.selectedId = "";
        showTaskToast("Task deleted.");
      } else {
        const field = action === "complete" ? "completionNote" : "howToTest";
        const payload = await taskRequest("POST", `/api/tasks/${taskId}/${action}`, { [field]: String($("[data-task-action-copy]").value || "").trim() });
        replaceTask(payload.task);
        taskDetailsLoaded.add(payload.task.id);
        showTaskToast(action === "complete" ? "Task completed and sent to QA." : "Task sent to QA.");
      }
      dialog.close();
      renderTasksPage();
    } catch (error) {
      status.textContent = taskFriendlyError(error); status.hidden = false;
    } finally {
      submit.disabled = false;
      submit.textContent = { clear: "Delete closed tasks", delete: "Delete permanently", complete: "Complete task", qa: "Send to QA" }[action] || "Continue";
    }
  }

  async function submitTaskReply(form) {
    const input = form.querySelector("[data-task-reply-input]");
    const button = form.querySelector('button[type="submit"]');
    const taskId = form.dataset.taskReplyForm;
    const body = String(input?.value || "").trim();
    if (!body || button.disabled) return;
    button.disabled = true;
    try {
      const payload = await taskRequest("POST", `/api/tasks/${taskId}/replies`, { body });
      taskCommentDrafts.delete(taskId);
      replaceTask(payload.task);
      taskDetailsLoaded.add(payload.task.id);
      renderTasksPage();
      showTaskToast(taskPingedCopy("Comment posted", body));
    } catch (error) {
      showTaskToast(taskFriendlyError(error), "bad");
    } finally {
      button.disabled = false;
    }
  }

  async function submitTaskCheckin(task, checkinState) {
    if (!task || taskCheckinBusy) return;
    const note = String(taskCheckinNotes.get(task.id) || "").trim();
    taskCheckinBusy = true;
    renderTaskDetail();
    try {
      const payload = await taskRequest("POST", `/api/tasks/${task.id}/checkin`, note ? { state: checkinState, note } : { state: checkinState });
      taskCheckinNotes.delete(task.id);
      taskCheckinOpenId = "";
      if (payload.task) {
        replaceTask(payload.task, false);
        taskDetailsLoaded.add(payload.task.id);
      } else {
        taskDetailsLoaded.delete(task.id);
        void loadSelectedTaskDetail();
      }
      renderTasksPage();
      showTaskToast(taskPingedCopy("Check-in sent", note));
    } catch (error) {
      showTaskToast(taskFriendlyError(error), "bad");
    } finally {
      taskCheckinBusy = false;
      renderTaskDetail();
    }
  }

  function taskPromptText(task) {
    const lines = [`[task:${task.id}] ${task.title}`];
    if (task.repository) lines.push(`Repository: ${task.repository}`);
    if (task.details) lines.push("", task.details);
    if (task.qa?.howToTest) lines.push("", `How to test: ${task.qa.howToTest}`);
    return lines.join("\n");
  }

  function initTasksPage() {
    const search = $("[data-tasks-search]");
    search?.addEventListener("input", () => {
      state.tasksView.query = search.value;
      state.tasksView.page = 1;
      renderTasksPage();
    });
    $("[data-tasks-assignee-filter]")?.addEventListener("change", (event) => {
      state.tasksView.assignee = String(event.target.value || "all");
      if (state.tasksView.assignee !== "all" && state.tasksView.filter === "mine") {
        state.tasksView.filter = "open";
      }
      state.tasksView.page = 1;
      renderTasksPage();
    });
    const panel = $("[data-tasks-panel]");
    panel?.addEventListener("click", (event) => {
      const filter = event.target.closest("[data-tasks-filter]");
      if (filter) {
        state.tasksView.filter = filter.dataset.tasksFilter;
        if (state.tasksView.filter === "mine") state.tasksView.assignee = "all";
        state.tasksView.page = 1;
        renderTasksPage();
        return;
      }
      const selected = event.target.closest("[data-task-select]");
      if (selected) { state.tasksView.selectedId = selected.dataset.taskSelect; renderTasksPage(); void loadSelectedTaskDetail(); return; }
      const page = event.target.closest("[data-tasks-page]");
      if (page) { state.tasksView.page = Number(page.dataset.tasksPage) || 1; renderTasksPage(); return; }
      if (event.target.closest("[data-tasks-prev]")) { state.tasksView.page = Math.max(1, state.tasksView.page - 1); renderTasksPage(); return; }
      if (event.target.closest("[data-tasks-next]")) { state.tasksView.page += 1; renderTasksPage(); return; }
      if (event.target.closest("[data-tasks-refresh]")) { void loadOrganizationTasks(); return; }
      if (event.target.closest("[data-tasks-new]")) { openTaskEditor("create"); return; }
      if (event.target.closest("[data-tasks-delete-closed]")) openTaskActionDialog("clear");
    });

    const detailPane = $("[data-tasks-detail]");
    detailPane?.addEventListener("click", async (event) => {
      const attachment = event.target.closest("[data-task-attachment-link]");
      if (attachment) {
        event.preventDefault();
        void expandTaskAttachment(attachment);
        return;
      }
      const checkinState = event.target.closest("[data-task-checkin-state]");
      if (checkinState) {
        if (!checkinState.disabled) void submitTaskCheckin(selectedTask(), checkinState.dataset.taskCheckinState);
        return;
      }
      if (event.target.closest("[data-task-checkin-cancel]")) {
        taskCheckinOpenId = "";
        renderTaskDetail();
        return;
      }
      const button = event.target.closest("[data-task-action]");
      if (!button || button.disabled) return;
      const task = selectedTask();
      const action = button.dataset.taskAction;
      if (action === "checkin") {
        taskCheckinOpenId = taskCheckinOpenId === task?.id ? "" : task?.id || "";
        renderTaskDetail();
        detailPane.querySelector("[data-task-checkin-note]")?.focus();
      } else if (action === "edit") openTaskEditor("edit", task);
      else if (action === "followup") openTaskEditor("followup", task);
      else if (["complete", "qa", "delete"].includes(action)) openTaskActionDialog(action, task);
      else if (action === "copy") {
        const copied = await copyTextToClipboard(taskPromptText(task));
        showTaskToast(copied ? "Task prompt copied." : "Could not copy the task prompt.", copied ? "good" : "bad");
      } else void mutateTask(action, task);
    });
    $("[data-tasks-detail]")?.addEventListener("submit", (event) => {
      const form = event.target.closest("[data-task-reply-form]");
      if (!form) return;
      event.preventDefault();
      void submitTaskReply(form);
    });
    // @mention suggestions. Picking one happens on mousedown so the field never
    // loses focus (and its caret) to the suggestion button.
    detailPane?.addEventListener("mousedown", (event) => {
      const pick = event.target.closest("[data-task-mention-pick]");
      const input = pick?.closest(".task-mention-field")?.querySelector("[data-task-mention-input]");
      if (!input) return;
      event.preventDefault();
      insertTaskMention(input, pick.dataset.taskMentionPick);
    });
    detailPane?.addEventListener("input", (event) => {
      const input = event.target.closest?.("[data-task-mention-input]");
      if (!input) return;
      rememberTaskDraft(input);
      const query = taskMentionQueryAt(input);
      if (query !== taskMentionState.query) taskMentionState.index = 0;
      taskMentionState.query = query;
      renderTaskMentionList(input);
    });
    detailPane?.addEventListener("keydown", (event) => {
      const input = event.target.closest?.("[data-task-mention-input]");
      if (!input) return;
      const matches = taskMentionState.query === null ? [] : taskMentionState.matches;
      if (matches.length && (event.key === "ArrowDown" || event.key === "ArrowUp")) {
        event.preventDefault();
        const step = event.key === "ArrowDown" ? 1 : -1;
        taskMentionState.index = (taskMentionState.index + step + matches.length) % matches.length;
        renderTaskMentionList(input);
      } else if (matches.length && (event.key === "Enter" || event.key === "Tab")) {
        event.preventDefault();
        insertTaskMention(input, matches[taskMentionState.index]);
      } else if (matches.length && event.key === "Escape") {
        event.preventDefault();
        taskMentionState.query = null;
        renderTaskMentionList(input);
      } else if (event.key === "Enter" && (event.ctrlKey || event.metaKey) && input.form) {
        event.preventDefault();
        input.form.requestSubmit();
      }
    });
    detailPane?.addEventListener("focusout", (event) => {
      const input = event.target.closest?.("[data-task-mention-input]");
      if (!input) return;
      taskMentionState.query = null;
      renderTaskMentionList(input);
    });
    $$('[data-task-dialog-close]').forEach((button) => button.addEventListener("click", () => button.closest("dialog")?.close()));
    $("[data-task-editor-form]")?.addEventListener("submit", (event) => { event.preventDefault(); void saveTaskEditor(); });
    $("[data-task-action-form]")?.addEventListener("submit", (event) => { event.preventDefault(); void submitTaskActionDialog(); });
    void loadOrganizationTasks();
  }
