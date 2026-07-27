import {
  canAccessOfficeFloor,
  normalizeOfficeFloorAccess,
  officeFloorById,
} from "./world-office-tower.js";

export const OFFICE_ENTER_DISTANCE = 6.5;
export const OFFICE_EXIT_DISTANCE = 7.5;

const OFFICE_CHAT_PATH = "/chat?embed=office";
const OFFICE_UNLOAD_DELAY_MS = 2000;
const OFFICE_ENTRY_PATH = "/api/world/office/general/entry";
const OFFICE_FLOORS_PATH = "/api/world/office/floors";
const LOGIN_REQUIRED_MESSAGE =
  "Maya and Noah: please log in to visit the ForkMesh offices.";

export function nextOfficeZoneState(currentState, distance) {
  const threshold =
    currentState === "nearby"
      ? OFFICE_EXIT_DISTANCE
      : OFFICE_ENTER_DISTANCE;
  return Number.isFinite(distance) && distance <= threshold
    ? "nearby"
    : "distant";
}

function isTypingTarget(target) {
  if (!(target instanceof Element)) return false;
  return Boolean(
    target.closest("input, textarea, select, button, [contenteditable='true']"),
  );
}

function isAuthenticationError(error) {
  const status = Number(error?.status || error?.response?.status || 0);
  if (status === 401) return true;
  return /(?:^|\D)401(?:\D|$)|invalid_session|login_required|registered_user_required/i
    .test(String(error?.message || error || ""));
}

export function createWorldOfficeController({
  root,
  world,
  meeting,
  tasks = null,
  chatPath = OFFICE_CHAT_PATH,
  getSession = () => null,
}) {
  const prompt = root.querySelector("[data-world-office-prompt]");
  const promptLight = root.querySelector("[data-world-office-prompt-light]");
  const promptStatus = root.querySelector("[data-world-office-prompt-status]");
  const enterButton = root.querySelector("[data-world-office-enter]");
  const fallbackButton = root.querySelector("[data-world-office-fallback]");
  const panel = root.querySelector("[data-world-office-chat]");
  const heading = root.querySelector("#world-office-chat-title");
  const loading = root.querySelector("[data-world-office-loading]");
  const frame = root.querySelector("[data-world-office-frame]");

  let proximity = "distant";
  let active = false;
  let fallbackActive = false;
  let returnFocus = null;
  let unloadTimer = null;
  let frameSuspended = false;
  let entryPending = false;
  let exitPending = false;
  let officeEntryTicket = "";
  let officeEntryExpiresAt = 0;
  let officeAccess = normalizeOfficeFloorAccess({});
  let attendanceAccount = "";

  const resolvedChatURL = new URL(chatPath, window.location.origin);
  if (
    resolvedChatURL.origin !== window.location.origin ||
    resolvedChatURL.pathname !== "/chat" ||
    resolvedChatURL.searchParams.get("embed") !== "office"
  ) {
    throw new Error("ForkMesh Office chat must use the same-origin /chat embed.");
  }
  const safeChatPath = `${resolvedChatURL.pathname}${resolvedChatURL.search}`;

  function session() {
    try {
      const value = getSession();
      return value && typeof value === "object" ? value : null;
    } catch (_) {
      return null;
    }
  }

  function authenticatedSession() {
    const value = session();
    return String(value?.sessionToken || "") ? value : null;
  }

  function greetGuest() {
    world.greetOfficeGuest?.(LOGIN_REQUIRED_MESSAGE);
    root.toast?.(LOGIN_REQUIRED_MESSAGE);
    root.toggleWorldAccount?.(true, "login", enterButton);
    return false;
  }

  function clearUnloadTimer() {
    if (!unloadTimer) return;
    window.clearTimeout(unloadTimer);
    unloadTimer = null;
  }

  function renderPrompt() {
    if (!prompt) return;
    const signedIn = Boolean(authenticatedSession());
    prompt.hidden = proximity !== "nearby" || active;
    prompt.dataset.available = "true";
    if (promptStatus) {
      promptStatus.textContent = signedIn
        ? "Office entrance ready · signed-in members only"
        : "Please log in to visit the offices";
    }
    if (promptLight) {
      promptLight.setAttribute(
        "aria-label",
        signedIn ? "Office entrance ready" : "Login required",
      );
    }
    if (enterButton) {
      enterButton.disabled = entryPending;
      if (enterButton.firstChild) {
        enterButton.firstChild.textContent = signedIn
          ? "Enter ForkMesh Office "
          : "Log in to enter Office ";
      }
    }
  }

  function setEntryPending(pending) {
    entryPending = pending === true;
    renderPrompt();
  }

  function closeFallback({ restoreFocus = true } = {}) {
    if (!fallbackActive && !frame?.hasAttribute("src")) return false;
    fallbackActive = false;
    if (panel) {
      panel.dataset.open = "false";
      panel.setAttribute("aria-hidden", "true");
    }
    if (frame?.contentWindow && frame.hasAttribute("src")) {
      frameSuspended = true;
      frame.contentWindow.postMessage(
        { type: "office-chat-suspend" },
        window.location.origin,
      );
    }
    clearUnloadTimer();
    unloadTimer = window.setTimeout(() => {
      frame?.removeAttribute("src");
      frameSuspended = false;
      unloadTimer = null;
    }, OFFICE_UNLOAD_DELAY_MS);
    if (restoreFocus) {
      window.requestAnimationFrame(() => fallbackButton?.focus());
    }
    return true;
  }

  function openFallback(trigger = null) {
    if (!active || meeting.inRoom || fallbackActive) return false;
    if (trigger instanceof HTMLElement) returnFocus = trigger;
    clearUnloadTimer();
    fallbackActive = true;
    if (panel) {
      panel.dataset.open = "true";
      panel.setAttribute("aria-hidden", "false");
    }
    if (loading) loading.hidden = false;
    if (frameSuspended) {
      frame?.removeAttribute("src");
      frameSuspended = false;
    }
    if (frame && frame.getAttribute("src") !== safeChatPath) {
      frame.setAttribute("src", safeChatPath);
    }
    window.requestAnimationFrame(() => heading?.focus());
    return true;
  }

  function setProximity(nextState) {
    proximity = nextState === "nearby" ? "nearby" : "distant";
    renderPrompt();
  }

  function focusOffice(trigger = null) {
    if (trigger instanceof HTMLElement) returnFocus = trigger;
    world.focusLandmark("office");
  }

  async function loadFloorAccess(activeSession) {
    let payload;
    try {
      payload = await root.fetchJSON(OFFICE_FLOORS_PATH, {
        timeout: 8000,
        cache: "no-store",
      });
    } catch (error) {
      if (isAuthenticationError(error)) throw error;
      // The three shared member floors are safe to expose after the entry
      // endpoint has authenticated this session. Team floors remain locked
      // until the authoritative floor projection is available.
      payload = {
        authenticated: true,
        account: String(activeSession?.nodeName || ""),
        allowedFloorIds: ["lobby", "marketing", "rooftop"],
        teams: [],
      };
    }
    const normalized = normalizeOfficeFloorAccess({
      ...(payload && typeof payload === "object" ? payload : {}),
      account:
        String(payload?.account || "") ||
        String(activeSession?.nodeName || ""),
    });
    if (!normalized.authenticated) {
      const error = new Error("login_required");
      error.status = 401;
      throw error;
    }
    officeAccess = {
      ...(payload && typeof payload === "object" ? payload : {}),
      ...normalized,
    };
    attendanceAccount = normalized.account;
    world.setOfficeAccess?.(officeAccess);
    return officeAccess;
  }

  function requestedFloorId(value) {
    const requested =
      value && typeof value === "object"
        ? value.floorId || value.id
        : value;
    return officeFloorById(requested)?.id || "";
  }

  function travelToOfficeFloor(value) {
    const floorId = requestedFloorId(value);
    const floor = officeFloorById(floorId);
    if (!active || !floor || !canAccessOfficeFloor(officeAccess, floorId)) {
      root.toast?.(
        floor
          ? `${floor.label} is available only to authorized members of that team.`
          : "That Office floor is unavailable.",
      );
      return false;
    }
    const travelled = world.travelToOfficeFloor?.(floorId);
    if (travelled === false) {
      root.toast?.(`${floor.label} is temporarily unavailable.`);
      return false;
    }
    return travelled !== undefined ? travelled : true;
  }

  function recordAttendance(direction) {
    world.setOfficeAttendance?.({
      type: direction === "out" ? "out" : "in",
      at: Date.now(),
      account: attendanceAccount,
    });
  }

  async function completeOfficeEntry(payload, activeSession) {
    await loadFloorAccess(activeSession);
    if (!world.enterOffice()) return false;
    officeEntryTicket = String(payload?.entryTicket || "").slice(0, 2048);
    officeEntryExpiresAt = Number(payload?.expiresAt) || 0;
    meeting.setEntryTicket?.(officeEntryTicket, officeEntryExpiresAt);
    if (!returnFocus) returnFocus = enterButton;
    active = true;
    exitPending = false;
    recordAttendance("in");
    meeting.openLobby();
    tasks?.setActive?.(true);
    renderPrompt();
    return true;
  }

  async function requestOfficeEntry() {
    if (entryPending || active) return false;
    const activeSession = authenticatedSession();
    if (!activeSession) return greetGuest();
    if (
      typeof root.postJSON !== "function" ||
      typeof root.fetchJSON !== "function"
    ) {
      root.toast?.("Office entry is temporarily unavailable.");
      return false;
    }
    setEntryPending(true);
    try {
      const payload = await root.postJSON(
        OFFICE_ENTRY_PATH,
        {},
        { timeout: 8000 },
      );
      if (
        payload?.ok !== true ||
        !String(payload?.entryTicket || "") ||
        !Number.isFinite(Number(payload?.expiresAt))
      ) {
        throw new Error(String(payload?.error || "office_entry_unavailable"));
      }
      return await completeOfficeEntry(payload, activeSession);
    } catch (error) {
      if (isAuthenticationError(error)) return greetGuest();
      root.toast?.("Office entry is temporarily unavailable.");
      return false;
    } finally {
      setEntryPending(false);
    }
  }

  async function enterOffice(entry = {}) {
    const doorwayEntry = entry?.source === "doorway";
    try {
      if (proximity !== "nearby" || active) return false;
      return await requestOfficeEntry();
    } finally {
      if (doorwayEntry) world.setOfficeDoorwayEntryPending?.(false);
    }
  }

  function completeOfficeExit() {
    if (!active) return false;
    closeFallback({ restoreFocus: false });
    tasks?.setActive?.(false);
    recordAttendance("out");
    meeting.leaveOffice();
    meeting.setEntryTicket?.("", 0);
    officeEntryTicket = "";
    officeEntryExpiresAt = 0;
    officeAccess = normalizeOfficeFloorAccess({});
    attendanceAccount = "";
    world.setOfficeAccess?.(officeAccess);
    active = false;
    exitPending = false;
    renderPrompt();
    const focusTarget = returnFocus?.isConnected ? returnFocus : enterButton;
    window.requestAnimationFrame(() => focusTarget?.focus());
    return true;
  }

  function collapse() {
    if (!active || exitPending) return false;
    closeFallback({ restoreFocus: false });
    if (meeting.inRoom) meeting.leaveRoom();
    exitPending = true;
    if (!world.beginOfficeExit?.()) {
      exitPending = false;
      return false;
    }
    root.toast?.("Walk through the Office entrance to return outside.");
    return true;
  }

  function onClick(event) {
    const focusControl = event.target.closest("[data-world-office-focus]");
    if (focusControl) {
      event.preventDefault();
      focusOffice(focusControl);
      return;
    }
    const enterControl = event.target.closest("[data-world-office-enter]");
    if (enterControl) {
      event.preventDefault();
      returnFocus = enterControl;
      void enterOffice({ source: "prompt" });
      return;
    }
    const fallbackControl = event.target.closest("[data-world-office-fallback]");
    if (fallbackControl) {
      event.preventDefault();
      openFallback(fallbackControl);
      return;
    }
    if (event.target.closest("[data-world-office-close]")) {
      event.preventDefault();
      closeFallback();
      return;
    }
    if (event.target.closest("[data-world-office-exit]")) {
      event.preventDefault();
      collapse();
      return;
    }
    if (event.target.closest("[data-world-office-lobby-exit]")) {
      event.preventDefault();
      completeOfficeExit();
    }
  }

  function onKeyDown(event) {
    if (event.key === "Escape" && fallbackActive) {
      event.preventDefault();
      closeFallback();
      return;
    }
    if (event.key === "Escape" && active) {
      event.preventDefault();
      if (meeting.inRoom) meeting.leaveRoom();
      else collapse();
      return;
    }
    if (
      event.key.toLowerCase() === "e" &&
      proximity === "nearby" &&
      !active &&
      !event.repeat &&
      !isTypingTarget(event.target)
    ) {
      event.preventDefault();
      returnFocus = enterButton;
      void enterOffice({ source: "keyboard" });
    }
  }

  function onMessage(event) {
    if (event.origin !== window.location.origin) return;
    if (!frame || event.source !== frame.contentWindow) return;
    const type = String(event.data?.type || "");
    if (type === "office-chat-ready" && loading) loading.hidden = true;
    if (type !== "office-chat-ready" && type !== "office-chat-room-changed") {
      return;
    }
  }

  function destroy() {
    clearUnloadTimer();
    tasks?.setActive?.(false);
    world.setOfficeExitHandler?.(null);
    world.setOfficeDoorwayEntryPending?.(false);
    world.setOfficeFloorHandler?.(null);
    root.removeEventListener("click", onClick);
    window.removeEventListener("keydown", onKeyDown);
    window.removeEventListener("message", onMessage);
    meeting.setEntryTicket?.("", 0);
    officeEntryTicket = "";
    officeEntryExpiresAt = 0;
    officeAccess = normalizeOfficeFloorAccess({});
    world.setOfficeAccess?.(officeAccess);
    closeFallback({ restoreFocus: false });
    frame?.removeAttribute("src");
  }

  root.addEventListener("click", onClick);
  world.setOfficeExitHandler?.(completeOfficeExit);
  world.setOfficeFloorHandler?.(travelToOfficeFloor);
  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("message", onMessage);
  renderPrompt();

  return {
    setProximity,
    focusOffice,
    enterOffice,
    openFallback,
    collapse,
    destroy,
    get active() {
      return active;
    },
    get proximity() {
      return proximity;
    },
  };
}
