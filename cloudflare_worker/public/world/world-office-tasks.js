const OFFICE_TASKS_PATH = "/api/world/office/marketing-tasks";
// Mutations refresh immediately and opening the board refreshes after five
// seconds, so a 60-second foreground poll keeps the wall current without
// making three D1-backed requests a minute per visitor. If an assignee leaves
// the Office with a timer running, only a five-minute reconciliation poll is
// needed; elapsed time continues locally from the server clock.
const OFFICE_TASKS_POLL_MS = 60_000;
const OFFICE_TASKS_BACKGROUND_POLL_MS = 5 * 60_000;
const OFFICE_TASKS_TICK_MS = 1_000;
const OFFICE_CHECKIN_MIN_MS = 4 * 60 * 1_000;
const OFFICE_CHECKIN_MAX_MS = 9 * 60 * 1_000;

function text(value, limit = 160) {
  return String(value || "")
    .replace(/[\u0000-\u001f\u007f]/g, " ")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, limit);
}

function escapeHTML(value) {
  return text(value, 500)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

function safeTaskId(value) {
  const candidate = String(value || "").trim().toLowerCase();
  return /^[a-f0-9-]{16,64}$/.test(candidate) ? candidate : "";
}

function timestampMs(value) {
  if (typeof value === "string" && value.trim() && !/^\d+$/.test(value.trim())) {
    const parsed = Date.parse(value);
    return Number.isFinite(parsed) ? Math.max(0, parsed) : 0;
  }
  const numeric = Number(value) || 0;
  return Math.max(0, numeric > 0 && numeric < 10 ** 11 ? numeric * 1000 : numeric);
}

function normalizedTask(task) {
  if (!task || typeof task !== "object") return null;
  const id = safeTaskId(task.id);
  const title = text(task.title);
  const assignee = text(task.assignee, 64).toLowerCase();
  const status = task.status === "active" ? "active" : "idle";
  if (!id || !title || !assignee) return null;
  return {
    id,
    title,
    assignee,
    status,
    elapsedMs: Math.max(0, Number(task.elapsedMs) || 0),
    startedAt: timestampMs(task.startedAt),
    updatedAt: timestampMs(task.updatedAt),
    nextCheckinAt: timestampMs(task.nextCheckinAt),
    lastCheckin:
      task.lastCheckin && typeof task.lastCheckin === "object"
        ? {
            state: ["going_well", "blocked", "needs_help"].includes(
              task.lastCheckin.state,
            )
              ? task.lastCheckin.state
              : "",
            at: timestampMs(task.lastCheckin.at),
          }
        : null,
  };
}

export function formatOfficeTaskElapsed(value) {
  const totalSeconds = Math.max(0, Math.floor((Number(value) || 0) / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (hours) {
    return `${hours}:${String(minutes).padStart(2, "0")}:${String(
      seconds,
    ).padStart(2, "0")}`;
  }
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

export function officeTaskCheckinDelay(randomValue = Math.random()) {
  const ratio = Math.min(1, Math.max(0, Number(randomValue) || 0));
  return Math.round(
    OFFICE_CHECKIN_MIN_MS +
      (OFFICE_CHECKIN_MAX_MS - OFFICE_CHECKIN_MIN_MS) * ratio,
  );
}

export function createWorldOfficeTasksController({
  root,
  world,
  fetchJSON,
  postJSON,
  getSession = () => null,
  toast = () => {},
  random = Math.random,
}) {
  const panel = root.querySelector("[data-world-office-task-panel]");
  const heading = root.querySelector("#world-office-task-title");
  const status = root.querySelector("[data-world-office-task-status]");
  const list = root.querySelector("[data-world-office-task-list]");
  const manager = root.querySelector("[data-world-office-task-manager]");
  const form = root.querySelector("[data-world-office-task-form]");
  const titleInput = root.querySelector("[data-world-office-task-name]");
  const assigneeInput = root.querySelector("[data-world-office-task-assignee]");
  const assigneeOptions = root.querySelector(
    "[data-world-office-task-assignees]",
  );
  const checkin = root.querySelector("[data-world-office-task-checkin]");
  // The same board is mirrored into the Local controls "Work" tab so an
  // assignee can start and stop their own work without entering the Office.
  const workList = root.querySelector("[data-world-work-list]");
  const workStatus = root.querySelector("[data-world-work-status]");
  const workTotal = root.querySelector("[data-world-work-total]");
  const workActive = root.querySelector("[data-world-work-active]");
  const workTracked = root.querySelector("[data-world-work-tracked]");
  const checkinCopy = root.querySelector(
    "[data-world-office-task-checkin-copy]",
  );
  let officeActive = false;
  let personalView = false;
  let monitoring = false;
  let opened = false;
  let loading = false;
  let busyTaskId = "";
  let actor = "";
  let canManage = false;
  let authorized = false;
  let tasks = [];
  let assignable = [];
  let syncedAt = performance.now();
  let serverNowAtSync = Date.now();
  let lastRefreshAt = 0;
  let refreshPromise = null;
  let pollTimer = 0;
  let tickTimer = 0;
  let checkinTimer = 0;
  let checkinTaskId = "";
  let physicalTickBucket = -1;

  function ownTasks() {
    return tasks.filter((task) => task.assignee === actor);
  }

  function ownActiveTasks() {
    return ownTasks().filter((task) => task.status === "active");
  }

  function keepMonitoring() {
    return officeActive || personalView || ownActiveTasks().length > 0;
  }

  function currentElapsed(task) {
    return Math.max(
      0,
      task.elapsedMs +
        (task.status === "active" ? performance.now() - syncedAt : 0),
    );
  }

  function currentServerNow() {
    return serverNowAtSync + Math.max(0, performance.now() - syncedAt);
  }

  function setStatus(message, tone = "") {
    if (!status) return;
    status.textContent = text(message, 240);
    if (tone) status.dataset.tone = tone;
    else delete status.dataset.tone;
  }

  function physicalState(state, message = "") {
    world.updateOfficeMarketingTasks?.({
      authorized,
      state,
      message: text(message, 80),
      tasks: tasks.slice(0, 8).map((task) => ({
        title: task.title,
        assignee: task.assignee,
        status: task.status,
        elapsed: formatOfficeTaskElapsed(currentElapsed(task)),
      })),
    });
    // The owner-only plate on the back of your avatar carries the same work,
    // narrowed to your assignments. It never enters a presence frame.
    const own = ownTasks();
    world.setSelfWorkBoard?.({
      state: ["ready", "loading"].includes(state) ? state : "locked",
      message: text(message, 64),
      total: own.length,
      active: own.filter((task) => task.status === "active").length,
      tracked: formatOfficeTaskElapsed(
        own.reduce((sum, task) => sum + currentElapsed(task), 0),
      ),
      items: own.slice(0, 6).map((task) => ({
        title: task.title,
        status: task.status,
        elapsed: formatOfficeTaskElapsed(currentElapsed(task)),
        checkin: checkinLabel(task.lastCheckin?.state),
      })),
    });
  }

  function checkinLabel(value) {
    if (value === "blocked") return "Blocked";
    if (value === "needs_help") return "Needs help";
    if (value === "going_well") return "Going well";
    return "";
  }

  function taskHTML(task) {
    const own = task.assignee === actor;
    const activeTask = task.status === "active";
    const checkinState = checkinLabel(task.lastCheckin?.state);
    return `
      <li class="world-office-task" data-status="${activeTask ? "active" : "idle"}">
        <div class="world-office-task-copy">
          <span class="world-office-task-state" aria-hidden="true"></span>
          <div>
            <strong>${escapeHTML(task.title)}</strong>
            <small>
              @${escapeHTML(task.assignee)}
              ${checkinState ? ` · last check-in: ${escapeHTML(checkinState)}` : ""}
            </small>
          </div>
        </div>
        <div class="world-office-task-controls">
          <time
            data-world-office-task-elapsed="${task.id}"
            datetime="PT${Math.floor(currentElapsed(task) / 1000)}S"
          >${formatOfficeTaskElapsed(currentElapsed(task))}</time>
          ${
            own
              ? `<button
                  type="button"
                  data-world-office-task-action="${activeTask ? "stop" : "start"}"
                  data-world-office-task-id="${task.id}"
                  ${busyTaskId === task.id ? "disabled" : ""}
                >${activeTask ? "Stop" : "Start"}</button>`
              : ""
          }
        </div>
      </li>`;
  }

  function render() {
    if (manager) manager.hidden = !canManage;
    if (assigneeOptions) {
      const selected = text(assigneeInput?.value, 64).toLowerCase();
      assigneeOptions.innerHTML =
        `<option value="">Select an active ForkMesh user</option>` +
        assignable
          .map(
            (name) =>
              `<option value="${escapeHTML(name)}">${escapeHTML(name)}</option>`,
          )
          .join("");
      if (selected && assignable.includes(selected)) {
        assigneeInput.value = selected;
      }
    }
    if (list) {
      list.innerHTML = loading
        ? `<li class="world-office-task-empty">Loading marketing tasks…</li>`
        : tasks.length
          ? tasks.map(taskHTML).join("")
          : `<li class="world-office-task-empty">No marketing tasks are assigned here yet.</li>`;
    }
    if (panel) {
      panel.dataset.open = String(opened);
      panel.setAttribute("aria-hidden", String(!opened));
    }
    renderWorkPane();
    if (!loading) {
      setStatus(
        canManage
          ? `${tasks.length} task${tasks.length === 1 ? "" : "s"} · managers can assign any active ForkMesh user`
          : tasks.length
            ? "Only tasks assigned to you are shown."
            : "No tasks are currently assigned to you.",
      );
    }
  }

  // Stats only. The per-second tick calls this instead of renderWorkPane so
  // the row markup — and the Start/Stop button under the pointer — survives.
  function updateWorkStats() {
    const own = ownTasks();
    if (workTotal) workTotal.textContent = String(own.length);
    if (workActive) {
      workActive.textContent = String(
        own.filter((task) => task.status === "active").length,
      );
    }
    if (workTracked) {
      workTracked.textContent = formatOfficeTaskElapsed(
        own.reduce((sum, task) => sum + currentElapsed(task), 0),
      );
    }
    return own;
  }

  function renderWorkPane() {
    const own = updateWorkStats();
    if (workList) {
      workList.innerHTML = loading
        ? `<li class="world-office-task-empty">Loading your assigned work\u2026</li>`
        : own.length
          ? own.map(taskHTML).join("")
          : `<li class="world-office-task-empty">${
              authorized
                ? "Nothing is assigned to you right now."
                : "Sign in to load the work assigned to you."
            }</li>`;
    }
    if (workStatus && !loading) {
      workStatus.textContent = own.length
        ? `${own.length} assigned \u00b7 ${
            own.filter((task) => task.status === "active").length
          } running`
        : "";
    }
  }

  function updateElapsedLabels() {
    tasks.forEach((task) => {
      const elapsed = currentElapsed(task);
      // The same row can be painted twice (Office board plus Work tab), so
      // every matching clock is advanced, not just the first one found.
      root
        .querySelectorAll(`[data-world-office-task-elapsed="${task.id}"]`)
        .forEach((node) => {
          node.textContent = formatOfficeTaskElapsed(elapsed);
          node.setAttribute("datetime", `PT${Math.floor(elapsed / 1000)}S`);
        });
    });
    if (personalView) updateWorkStats();
    const nextBucket = Math.floor(performance.now() / 5000);
    if (
      officeActive &&
      ownActiveTasks().length &&
      nextBucket !== physicalTickBucket
    ) {
      physicalTickBucket = nextBucket;
      physicalState("ready");
    }
  }

  function clearCheckinTimer() {
    if (checkinTimer) window.clearTimeout(checkinTimer);
    checkinTimer = 0;
  }

  function closeCheckin() {
    checkinTaskId = "";
    if (checkin) {
      checkin.dataset.open = "false";
      checkin.setAttribute("aria-hidden", "true");
    }
  }

  function showCheckin(taskId = "") {
    const candidates = ownActiveTasks();
    const task =
      candidates.find((item) => item.id === safeTaskId(taskId)) ||
      candidates[0];
    if (!monitoring || !task) return false;
    checkinTaskId = task.id;
    if (checkinCopy) {
      checkinCopy.textContent = `How is “${task.title}” going?`;
    }
    if (checkin) {
      checkin.dataset.open = "true";
      checkin.setAttribute("aria-hidden", "false");
      window.requestAnimationFrame(() =>
        checkin
          .querySelector("[data-world-office-task-checkin-state]")
          ?.focus(),
      );
    }
    return true;
  }

  function scheduleCheckin() {
    clearCheckinTimer();
    if (!monitoring || !ownActiveTasks().length) return;
    const serverDueAt = ownActiveTasks()
      .map((task) => task.nextCheckinAt)
      .filter((value) => value > 0)
      .sort((left, right) => left - right)[0];
    const delay = serverDueAt
      ? Math.max(500, serverDueAt - currentServerNow())
      : officeTaskCheckinDelay(random());
    checkinTimer = window.setTimeout(() => {
      checkinTimer = 0;
      if (document.hidden || !showCheckin()) {
        scheduleCheckin();
      }
    }, Math.min(delay, 2_147_000_000));
  }

  function schedulePoll() {
    if (pollTimer) window.clearTimeout(pollTimer);
    pollTimer = 0;
    if (!monitoring) return;
    const delay =
      document.hidden || (!officeActive && !opened && !personalView)
        ? OFFICE_TASKS_BACKGROUND_POLL_MS
        : OFFICE_TASKS_POLL_MS;
    pollTimer = window.setTimeout(async () => {
      pollTimer = 0;
      if (!document.hidden) await refresh({ quiet: true });
      schedulePoll();
    }, delay);
  }

  function onVisibilityChange() {
    if (!monitoring || document.hidden) return;
    if (Date.now() - lastRefreshAt < OFFICE_TASKS_POLL_MS) {
      schedulePoll();
      return;
    }
    if (pollTimer) window.clearTimeout(pollTimer);
    pollTimer = 0;
    void refresh({ quiet: true }).finally(schedulePoll);
  }

  async function refresh({ quiet = false } = {}) {
    if (!monitoring || typeof fetchJSON !== "function") return false;
    if (refreshPromise) return refreshPromise;
    if (!getSession()?.sessionToken) {
      tasks = [];
      actor = "";
      canManage = false;
      authorized = false;
      assignable = [];
      loading = false;
      if (!quiet) setStatus("Sign in to access organization marketing tasks.");
      physicalState("locked", "Organization access required");
      render();
      if (!officeActive) stopMonitoring();
      return false;
    }
    refreshPromise = (async () => {
      loading = !quiet;
      if (!quiet) {
        physicalState("loading", "Syncing organization tasks");
        render();
      }
      try {
        const payload = await fetchJSON(OFFICE_TASKS_PATH, {
          cache: "no-store",
          timeout: 8000,
        });
        actor = text(payload?.actor, 64).toLowerCase();
        canManage = payload?.canManage === true;
        authorized = payload?.authorized !== false;
      assignable = Array.isArray(payload?.members)
          ? payload.members
              .map((name) => text(name, 64).toLowerCase())
              .filter(Boolean)
            .slice(0, 1000)
          : [];
        tasks = Array.isArray(payload?.tasks)
          ? payload.tasks.map(normalizedTask).filter(Boolean).slice(0, 100)
          : [];
        syncedAt = performance.now();
        serverNowAtSync = timestampMs(payload?.serverNow) || Date.now();
        lastRefreshAt = Date.now();
        loading = false;
        physicalState("ready");
        render();
        scheduleCheckin();
        if (!keepMonitoring()) stopMonitoring();
        return true;
      } catch (_) {
        tasks = [];
        canManage = false;
        authorized = false;
        assignable = [];
        loading = false;
        render();
        setStatus(
          "Marketing tasks are unavailable or your account is not an authorized organization member.",
          "error",
        );
        physicalState("locked", "Organization access required");
        if (!officeActive) stopMonitoring();
        return false;
      } finally {
        refreshPromise = null;
      }
    })();
    return refreshPromise;
  }

  async function mutate(path, body, taskId = "") {
    if (typeof postJSON !== "function") return false;
    busyTaskId = safeTaskId(taskId);
    render();
    let errorMessage = "";
    try {
      await postJSON(path, body, { timeout: 10_000 });
      await refresh({ quiet: true });
      return true;
    } catch (error) {
      errorMessage =
        text(error?.message, 160) || "The task change could not be saved.";
      return false;
    } finally {
      busyTaskId = "";
      render();
      if (errorMessage) setStatus(errorMessage, "error");
    }
  }

  async function onSubmit(event) {
    if (event.target === form) {
      event.preventDefault();
      const title = text(titleInput?.value);
      const assignee = text(assigneeInput?.value, 64).toLowerCase();
      if (!title || !assignee) {
        setStatus("Enter a task and an assignee.", "error");
        return;
      }
      const saved = await mutate(OFFICE_TASKS_PATH, { title, assignee });
      if (saved) {
        form.reset();
        toast("Marketing task added to the Office wall.");
      }
    }
  }

  async function onClick(event) {
    if (event.target.closest("[data-world-office-task-close]")) {
      close();
      return;
    }
    if (event.target.closest("[data-world-office-task-checkin-later]")) {
      closeCheckin();
      scheduleCheckin();
      return;
    }
    const checkinButton = event.target.closest(
      "[data-world-office-task-checkin-state]",
    );
    if (checkinButton && checkinTaskId) {
      const state = checkinButton.dataset.worldOfficeTaskCheckinState;
      const id = checkinTaskId;
      closeCheckin();
      const saved = await mutate(
        `${OFFICE_TASKS_PATH}/${encodeURIComponent(id)}/checkin`,
        { state },
        id,
      );
      toast(
        saved
          ? "Progress check-in recorded."
          : "That check-in could not be recorded.",
      );
      scheduleCheckin();
      return;
    }
    const actionButton = event.target.closest(
      "[data-world-office-task-action]",
    );
    if (!actionButton) return;
    const action = actionButton.dataset.worldOfficeTaskAction;
    const id = safeTaskId(actionButton.dataset.worldOfficeTaskId);
    if (!id || !["start", "stop"].includes(action)) return;
    const saved = await mutate(
      `${OFFICE_TASKS_PATH}/${encodeURIComponent(id)}/${action}`,
      {},
      id,
    );
    if (saved) {
      toast(action === "start" ? "Task timer started." : "Task timer stopped.");
    }
  }

  function open() {
    if (!officeActive) {
      toast("Enter the ForkMesh Office to use the marketing task board.");
      return false;
    }
    opened = true;
    render();
    if (heading) window.requestAnimationFrame(() => heading.focus());
    if (Date.now() - lastRefreshAt > 5000) {
      void refresh({ quiet: tasks.length > 0 });
    }
    return true;
  }

  function close() {
    opened = false;
    render();
    return true;
  }

  function stopMonitoring() {
    monitoring = false;
    closeCheckin();
    clearCheckinTimer();
    if (pollTimer) window.clearTimeout(pollTimer);
    pollTimer = 0;
    if (tickTimer) window.clearInterval(tickTimer);
    tickTimer = 0;
  }

  // The Local controls "Work" tab is reachable anywhere in the World, so it
  // drives the same monitoring loop the Office board uses.
  function setPersonalView(open) {
    personalView = open === true;
    if (!personalView) {
      if (!keepMonitoring()) stopMonitoring();
      return false;
    }
    monitoring = true;
    if (!tickTimer) {
      tickTimer = window.setInterval(updateElapsedLabels, OFFICE_TASKS_TICK_MS);
    }
    void refresh({ quiet: tasks.length > 0 });
    schedulePoll();
    return true;
  }

  function setActive(next) {
    officeActive = next === true;
    if (!officeActive) {
      close();
      // An assignee's active timer survives leaving the Office. Keep only its
      // low-frequency HTTPS poll and private check-in prompt alive; no task
      // data enters the Town presence socket.
      if (!keepMonitoring()) stopMonitoring();
      return;
    }
    monitoring = true;
    physicalTickBucket = -1;
    if (!tickTimer) {
      tickTimer = window.setInterval(updateElapsedLabels, OFFICE_TASKS_TICK_MS);
    }
    void refresh();
    schedulePoll();
  }

  function stopActiveForDeparture() {
    if (!ownActiveTasks().length) return false;
    const sessionToken = String(getSession()?.sessionToken || "");
    if (!sessionToken) return false;
    try {
      // `keepalive` lets this bounded, same-origin server-time stop finish
      // while pagehide tears the World down. The token stays in the request
      // header and no task copy enters the URL, beacon body, or sockets.
      void fetch(`${OFFICE_TASKS_PATH}/stop-active`, {
        method: "POST",
        credentials: "same-origin",
        cache: "no-store",
        keepalive: true,
        headers: {
          accept: "application/json",
          authorization: `Bearer ${sessionToken}`,
          "content-type": "application/json",
        },
        body: "{}",
      }).catch(() => {});
      return true;
    } catch (_) {
      return false;
    }
  }

  function destroy() {
    stopActiveForDeparture();
    officeActive = false;
    personalView = false;
    stopMonitoring();
    document.removeEventListener("visibilitychange", onVisibilityChange);
    root.removeEventListener("click", onClick);
    root.removeEventListener("submit", onSubmit);
    world.updateOfficeMarketingTasks?.({
      authorized: false,
      state: "locked",
      message: "Organization access required",
      tasks: [],
    });
  }

  document.addEventListener("visibilitychange", onVisibilityChange);
  root.addEventListener("click", onClick);
  root.addEventListener("submit", onSubmit);
  physicalState("locked", "Enter Office to sync tasks");

  return {
    close,
    destroy,
    open,
    refresh,
    setActive,
    setPersonalView,
    showCheckin,
    stopActiveForDeparture,
  };
}
