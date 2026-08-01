  // ---------------------------------------------------------------------------
  // Tasks page (/dashboard/tasks). The organization-private task catalog the
  // desktop app and the World office board already show, on the web. The relay
  // returns the whole catalog in one authorized read, so the filtering and the
  // paging both happen here: TASKS_PAGE_SIZE rows a page, every page reachable.
  const TASKS_PAGE_SIZE = 100;

  function taskStatusLabel(task) {
    if (task.completedAt > 0 || task.status === "done") return "Done";
    if (task.status === "active") return "In progress";
    if (task.assigneeKind === "agent" && task.agentSessionId) return "Queued";
    return "Ready";
  }

  function taskAssigneeLabel(task) {
    if (task.assigneeKind === "agent") return "Bot";
    return task.assignee || "Unassigned";
  }

  function taskMatchesFilter(task, filter) {
    const done = task.completedAt > 0 || task.status === "done";
    if (filter === "open") return !done;
    if (filter === "done") return done;
    if (filter === "active") return task.status === "active";
    if (filter === "mine") {
      return (
        !done &&
        task.assigneeKind === "user" &&
        task.assignee === state.tasksView.actor
      );
    }
    return true;
  }

  function filteredTasks() {
    const query = String(state.tasksView.query || "").trim().toLowerCase();
    return state.tasksView.items.filter((task) => {
      if (!taskMatchesFilter(task, state.tasksView.filter)) return false;
      if (!query) return true;
      return [
        task.title,
        task.details,
        task.repository,
        task.department,
        task.team,
        taskAssigneeLabel(task),
        task.createdBy,
        task.id,
      ].some((value) => String(value || "").toLowerCase().includes(query));
    });
  }

  function taskRowHtml(task) {
    const status = taskStatusLabel(task);
    const tone = {
      "Done": "border-border bg-secondary text-muted-foreground",
      "In progress": "border-primary/40 bg-primary/10 text-primary",
      "Queued": "border-border bg-secondary text-foreground",
      "Ready": "border-border bg-background text-foreground",
    }[status];
    const updated = relativeTimeLabel(task.updatedAt || task.createdAt);
    const meta = [
      task.department ? `#${task.department}` : "",
      task.repository || "",
      task.qa?.status && task.qa.status !== "none" ? `QA: ${task.qa.status}` : "",
      updated ? `updated ${updated}` : "",
    ].filter(Boolean);
    return `
      <article class="px-4 sm:px-5 py-3 flex items-start gap-3">
        <span class="mt-0.5 shrink-0 rounded-full border px-2 py-0.5 font-mono text-[10px] ${tone}">${escapeHtml(status)}</span>
        <div class="min-w-0 flex-1">
          <p class="truncate text-sm font-medium text-foreground">${escapeHtml(task.title)}</p>
          <p class="mt-1 truncate text-xs text-muted-foreground">${escapeHtml(meta.join(" · "))}</p>
        </div>
        <span class="shrink-0 text-xs text-muted-foreground">${escapeHtml(taskAssigneeLabel(task))}</span>
        <span class="shrink-0 rounded border border-border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground" title="Priority">${escapeHtml(String(task.priority ?? 50))}</span>
      </article>`;
  }

  function renderTasksPage() {
    const list = $("[data-tasks-list]");
    const summary = $("[data-tasks-summary]");
    const count = $("[data-tasks-count]");
    const prev = $("[data-tasks-prev]");
    const next = $("[data-tasks-next]");
    const pageList = $("[data-tasks-pages]");
    if (!list) return;

    for (const button of $$("[data-tasks-filter]")) {
      const active = button.dataset.tasksFilter === state.tasksView.filter;
      button.setAttribute("aria-pressed", String(active));
      button.classList.toggle("bg-secondary", active);
      button.classList.toggle("text-foreground", active);
    }

    if (state.tasksView.loading || state.tasksView.error) {
      list.innerHTML = state.tasksView.loading
        ? `<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">${loadingHtml("Loading organization tasks...")}</div>`
        : `<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">${escapeHtml(state.tasksView.error)}</div>`;
      if (summary) summary.textContent = "";
      if (count) count.textContent = state.tasksView.loading ? "Loading" : "0";
      if (pageList) pageList.innerHTML = "";
      for (const button of [prev, next]) {
        if (!button) continue;
        button.disabled = true;
        button.classList.add("opacity-40");
      }
      return;
    }

    const matches = filteredTasks();
    const pages = Math.max(1, Math.ceil(matches.length / TASKS_PAGE_SIZE));
    state.tasksView.page = Math.min(Math.max(1, state.tasksView.page), pages);
    const start = (state.tasksView.page - 1) * TASKS_PAGE_SIZE;
    const end = Math.min(start + TASKS_PAGE_SIZE, matches.length);

    list.innerHTML = matches.length
      ? matches.slice(start, end).map(taskRowHtml).join("")
      : '<div class="px-4 sm:px-5 py-8 text-sm text-muted-foreground">No tasks match this filter.</div>';
    if (count) {
      count.textContent = `${formatCount(state.tasksView.items.length)} total`;
    }
    if (summary) {
      summary.textContent = matches.length
        ? `Showing ${start + 1}-${end} of ${formatCount(matches.length)} tasks · page ${state.tasksView.page} of ${pages}`
        : "No tasks match this filter";
    }
    if (prev) {
      prev.disabled = state.tasksView.page <= 1;
      prev.classList.toggle("opacity-40", prev.disabled);
    }
    if (next) {
      next.disabled = state.tasksView.page >= pages;
      next.classList.toggle("opacity-40", next.disabled);
    }
    if (pageList) {
      pageList.innerHTML = "";
      for (let page = 1; page <= pages; page += 1) {
        const button = document.createElement("button");
        const active = page === state.tasksView.page;
        button.type = "button";
        button.textContent = String(page);
        button.dataset.tasksPage = String(page);
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

  async function loadOrganizationTasks() {
    // The catalog is organization-private: a signed-out visitor has no session
    // token, so the read can only ever come back 401. Say so instead of
    // flashing a spinner and then an error.
    if (!state.session) {
      state.tasksView.loading = false;
      state.tasksView.error =
        "Sign in with an organization member account to see the private task catalog.";
      renderTasksPage();
      return;
    }
    state.tasksView.loading = true;
    state.tasksView.error = "";
    renderTasksPage();
    try {
      const payload = await fetchJson("/api/tasks", { fresh: true });
      state.tasksView.items = (Array.isArray(payload?.tasks) ? payload.tasks : [])
        .filter((task) => task && typeof task === "object");
      state.tasksView.actor = String(payload?.actor || "").toLowerCase();
      state.tasksView.error = "";
    } catch (error) {
      state.tasksView.items = [];
      state.tasksView.error =
        error?.status === 401 || error?.status === 403
          ? "This task catalog is private to organization members."
          : `Tasks are unavailable: ${error?.message || "request failed"}`;
    } finally {
      state.tasksView.loading = false;
      renderTasksPage();
    }
  }

  function initTasksPage() {
    const search = $("[data-tasks-search]");
    if (search) {
      search.addEventListener("input", () => {
        state.tasksView.query = search.value;
        state.tasksView.page = 1;
        renderTasksPage();
      });
    }
    const panel = $("[data-tasks-panel]");
    panel?.addEventListener("click", (event) => {
      const filter = event.target.closest("[data-tasks-filter]");
      if (filter) {
        state.tasksView.filter = filter.dataset.tasksFilter;
        state.tasksView.page = 1;
        renderTasksPage();
        return;
      }
      const page = event.target.closest("[data-tasks-page]");
      if (page) {
        state.tasksView.page = Number(page.dataset.tasksPage) || 1;
        renderTasksPage();
        return;
      }
      if (event.target.closest("[data-tasks-prev]")) {
        state.tasksView.page = Math.max(1, state.tasksView.page - 1);
        renderTasksPage();
        return;
      }
      if (event.target.closest("[data-tasks-next]")) {
        state.tasksView.page += 1;
        renderTasksPage();
        return;
      }
      if (event.target.closest("[data-tasks-refresh]")) {
        void loadOrganizationTasks();
      }
    });
    void loadOrganizationTasks();
  }

