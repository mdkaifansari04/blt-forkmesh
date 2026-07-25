export const OFFICE_ENTER_DISTANCE = 6.5;
export const OFFICE_EXIT_DISTANCE = 7.5;
const OFFICE_CHAT_PATH = "/chat?embed=office";
const OFFICE_UNLOAD_DELAY_MS = 2000;
const OFFICE_STATUS_PATH = "/api/world/office/general/status";
const OFFICE_ENTRY_PATH = "/api/world/office/general/entry";
const OFFICE_CODE_PATH = "/api/world/office/general/code";
const OFFICE_ENTRY_HEADER = "X-ForkMesh-Office-Entry";
const OFFICE_STATUS_POLL_MS = 12000;

function playOfficeTone(digit) {
  try {
    const context = new AudioContext();
    const oscillator = context.createOscillator();
    const gain = context.createGain();
    oscillator.frequency.value = 620 + Number(digit || 0) * 38;
    gain.gain.setValueAtTime(0.035, context.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, context.currentTime + 0.09);
    oscillator.connect(gain).connect(context.destination);
    oscillator.start();
    oscillator.stop(context.currentTime + 0.1);
    oscillator.addEventListener("ended", () => context.close(), { once: true });
  } catch (_) {}
}

export function nextOfficeZoneState(currentState, distance) {
  const threshold = currentState === "nearby"
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

export function createWorldOfficeController({
  root,
  world,
  meeting,
  tasks = null,
  chatPath = OFFICE_CHAT_PATH,
}) {
  const prompt = root.querySelector("[data-world-office-prompt]");
  const promptLight = root.querySelector("[data-world-office-prompt-light]");
  const promptStatus = root.querySelector("[data-world-office-prompt-status]");
  const enterButton = root.querySelector("[data-world-office-enter]");
  const keypad = root.querySelector("[data-world-office-keypad]");
  const keypadForm = root.querySelector("[data-world-office-keypad-form]");
  const keypadTitle = root.querySelector("#world-office-keypad-title");
  const keypadInput = root.querySelector("[data-world-office-keypad-input]");
  const keypadStatus = root.querySelector("[data-world-office-keypad-status]");
  const keypadSubmit = root.querySelector("[data-world-office-keypad-submit]");
  const keypadHelp = keypad?.querySelector("small");
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
  let keypadOpen = false;
  let keypadMode = "entry";
  let keypadLocation = "exterior";
  let entryPending = false;
  let exitPending = false;
  let officeEntryTicket = "";
  let officeEntryExpiresAt = 0;
  let occupancy = {
    available: false,
    occupied: true,
    canSetCode: false,
  };
  let occupancyRequest = null;
  let occupancyTimer = null;

  const resolvedChatURL = new URL(chatPath, window.location.origin);
  if (
    resolvedChatURL.origin !== window.location.origin ||
    resolvedChatURL.pathname !== "/chat" ||
    resolvedChatURL.searchParams.get("embed") !== "office"
  ) {
    throw new Error("ForkMesh Office chat must use the same-origin /chat embed.");
  }
  const safeChatPath = `${resolvedChatURL.pathname}${resolvedChatURL.search}`;

  function clearUnloadTimer() {
    if (!unloadTimer) return;
    window.clearTimeout(unloadTimer);
    unloadTimer = null;
  }

  function renderPrompt() {
    if (!prompt) return;
    prompt.hidden = proximity !== "nearby" || active;
    prompt.dataset.available = String(occupancy.available);
    prompt.dataset.occupied = String(occupancy.occupied);
    if (promptStatus) {
      promptStatus.textContent = !occupancy.available
        ? "Office door status unavailable"
        : occupancy.occupied
          ? "Office occupied · four-digit code required"
          : "Office empty · door open";
    }
    if (promptLight) {
      promptLight.setAttribute(
        "aria-label",
        !occupancy.available
          ? "Door status unavailable"
          : occupancy.occupied
            ? "Office occupied"
            : "Office empty",
      );
    }
    if (enterButton) {
      enterButton.disabled = !occupancy.available || entryPending;
      enterButton.firstChild.textContent = occupancy.occupied
        ? "Use Office keypad "
        : "Enter ForkMesh Office ";
    }
  }

  function setKeypadStatus(message, tone = "") {
    if (!keypadStatus) return;
    keypadStatus.textContent = String(message || "");
    if (tone) keypadStatus.dataset.tone = tone;
    else delete keypadStatus.dataset.tone;
  }

  function setEntryPending(pending) {
    entryPending = pending === true;
    if (keypadInput) keypadInput.disabled = entryPending;
    if (keypadSubmit) keypadSubmit.disabled = entryPending;
    renderPrompt();
  }

  function closeKeypad({ restoreFocus = true } = {}) {
    if (!keypadOpen) return false;
    const previousMode = keypadMode;
    const previousLocation = keypadLocation;
    keypadOpen = false;
    keypadMode = "entry";
    keypadLocation = "exterior";
    if (keypad) {
      keypad.dataset.open = "false";
      keypad.setAttribute("aria-hidden", "true");
    }
    if (keypadInput) keypadInput.value = "";
    world.setOfficeKeypadDigits?.(
      "",
      previousMode,
      previousLocation,
    );
    world.blurOfficeKeypad?.();
    if (restoreFocus) {
      window.requestAnimationFrame(() => enterButton?.focus());
    }
    return true;
  }

  function openKeypad(mode = "entry", location = "exterior") {
    const safeMode = mode === "set" ? "set" : "entry";
    const safeLocation = location === "interior" ? "interior" : "exterior";
    const ticketValid =
      Boolean(officeEntryTicket) && officeEntryExpiresAt > Date.now();
    const entryAllowed =
      safeMode === "entry" &&
      safeLocation === "exterior" &&
      !active &&
      occupancy.available &&
      occupancy.occupied;
    const managementAllowed =
      safeMode === "set" &&
      safeLocation === "interior" &&
      active &&
      meeting.inRoom &&
      occupancy.canSetCode &&
      ticketValid;
    if (!entryAllowed && !managementAllowed) return false;
    keypadOpen = true;
    keypadMode = safeMode;
    keypadLocation = safeLocation;
    if (keypad) {
      keypad.dataset.open = "true";
      keypad.setAttribute("aria-hidden", "false");
    }
    if (keypadInput) keypadInput.value = "";
    if (keypadTitle) {
      keypadTitle.textContent =
        safeMode === "set"
          ? "Set a new four-digit Office code"
          : "Enter the four-digit code";
    }
    if (keypadHelp) {
      keypadHelp.textContent =
        safeMode === "set"
          ? "The code is sent only to the same-origin management endpoint. ForkMesh never stores it in this browser."
          : "This four-digit code is an Office-door coordination check, not account authentication. Room membership and encrypted-channel permissions are still enforced separately.";
    }
    setKeypadStatus(
      safeMode === "set"
        ? "Choose four digits. The backend will still verify your management permission."
        : "The Office is occupied. Enter the shared four-digit coordination code.",
    );
    world.focusOfficeKeypad?.(safeMode, safeLocation);
    world.setOfficeKeypadDigits?.("", safeMode, safeLocation);
    window.requestAnimationFrame(() => keypadInput?.focus());
    return true;
  }

  function setOccupancy(next = {}) {
    occupancy = {
      available: next.available === true,
      occupied: next.occupied !== false,
      canSetCode:
        next.canSetCode === undefined
          ? occupancy.canSetCode === true
          : next.canSetCode === true,
    };
    world.setOfficeOccupancy?.(occupancy);
    if (
      occupancy.available &&
      !occupancy.occupied &&
      keypadOpen &&
      keypadMode === "entry"
    ) {
      closeKeypad({ restoreFocus: false });
    }
    if (
      keypadOpen &&
      keypadMode === "set" &&
      (!active || !meeting.inRoom || !occupancy.canSetCode)
    ) {
      closeKeypad({ restoreFocus: false });
    }
    renderPrompt();
    return { ...occupancy };
  }

  async function refreshOccupancy() {
    if (occupancyRequest) return occupancyRequest;
    occupancyRequest = (async () => {
      try {
        let payload = {};
        if (typeof root.fetchJSON === "function") {
          payload = await root.fetchJSON(OFFICE_STATUS_PATH, {
            method: "GET",
            cache: "no-store",
            timeout: 8000,
            headers: { accept: "application/json" },
          });
        } else {
          const response = await fetch(OFFICE_STATUS_PATH, {
            method: "GET",
            credentials: "same-origin",
            cache: "no-store",
            headers: { accept: "application/json" },
          });
          payload = await response.json().catch(() => ({}));
          if (!response.ok) throw new Error("office_status_unavailable");
        }
        if (
          payload?.ok !== true ||
          typeof payload.occupied !== "boolean"
        ) {
          throw new Error("office_status_unavailable");
        }
        return setOccupancy({
          available: true,
          occupied: payload.occupied,
          canSetCode:
            payload.canSetCode === true ||
            payload.codeManagement?.canSetCode === true,
        });
      } catch (_) {
        return setOccupancy({
          available: false,
          occupied: true,
          canSetCode: false,
        });
      } finally {
        occupancyRequest = null;
      }
    })();
    return occupancyRequest;
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

  function setProximity(nextState) {
    proximity = nextState === "nearby" ? "nearby" : "distant";
    if (proximity === "distant" && active) collapse();
    if (proximity === "distant" && keypadOpen) {
      closeKeypad({ restoreFocus: false });
    }
    if (proximity === "nearby") void refreshOccupancy();
    renderPrompt();
  }

  function focusOffice(trigger = null) {
    if (trigger instanceof HTMLElement) returnFocus = trigger;
    world.focusLandmark("office");
  }

  function completeOfficeEntry(entryTicket, expiresAt) {
    if (!world.enterOffice()) return false;
    officeEntryTicket = String(entryTicket || "").slice(0, 2048);
    officeEntryExpiresAt = Number(expiresAt) || 0;
    meeting.setEntryTicket?.(entryTicket, expiresAt);
    if (!returnFocus) returnFocus = enterButton;
    active = true;
    exitPending = false;
    closeKeypad({ restoreFocus: false });
    meeting.openLobby();
    tasks?.setActive?.(true);
    renderPrompt();
    return true;
  }

  async function requestOfficeEntry(code = "") {
    if (entryPending || active) return false;
    setEntryPending(true);
    try {
      const response = await fetch(OFFICE_ENTRY_PATH, {
        method: "POST",
        credentials: "same-origin",
        cache: "no-store",
        headers: {
          accept: "application/json",
          "content-type": "application/json",
        },
        body: JSON.stringify(code ? { code } : {}),
      });
      const payload = await response.json().catch(() => ({}));
      if (
        !response.ok ||
        payload?.ok !== true ||
        !String(payload.entryTicket || "") ||
        !Number.isFinite(Number(payload.expiresAt))
      ) {
        const error = new Error(String(payload?.error || "entry_denied"));
        error.status = response.status;
        throw error;
      }
      setOccupancy({
        available: true,
        occupied: payload.occupied === true,
      });
      return completeOfficeEntry(
        String(payload.entryTicket).slice(0, 2048),
        Number(payload.expiresAt),
      );
    } catch (error) {
      if (error?.status === 403 && !code) {
        setOccupancy({ available: true, occupied: true });
        openKeypad();
        setKeypadStatus(
          "The Office became occupied. Enter the shared four-digit code.",
        );
        return false;
      }
      const message =
        error?.status === 429
          ? "Too many attempts. Wait briefly before trying again."
          : error?.status === 403
            ? "That four-digit code was not accepted."
            : "Office entry is temporarily unavailable.";
      setKeypadStatus(message, "error");
      if (keypadInput) {
        keypadInput.value = "";
        world.setOfficeKeypadDigits?.(
          "",
          keypadMode,
          keypadLocation,
        );
        window.requestAnimationFrame(() => keypadInput.focus());
      }
      return false;
    } finally {
      setEntryPending(false);
    }
  }

  async function requestOfficeCodeUpdate(code) {
    if (
      entryPending ||
      !active ||
      !meeting.inRoom ||
      !occupancy.canSetCode ||
      !officeEntryTicket ||
      officeEntryExpiresAt <= Date.now() ||
      !/^\d{4}$/.test(String(code || "")) ||
      typeof root.postJSON !== "function"
    ) {
      setKeypadStatus(
        "This deployment has not enabled authorized Office code management.",
        "error",
      );
      return false;
    }
    setEntryPending(true);
    try {
      await root.postJSON(
        OFFICE_CODE_PATH,
        { code: String(code) },
        {
          timeout: 8000,
          headers: {
            [OFFICE_ENTRY_HEADER]: officeEntryTicket,
          },
        },
      );
      setKeypadStatus("Office code updated.", "success");
      root.toast?.("Office code updated.");
      closeKeypad({ restoreFocus: false });
      await refreshOccupancy();
      return true;
    } catch (_) {
      setKeypadStatus(
        "The server did not accept that code-management request.",
        "error",
      );
      return false;
    } finally {
      setEntryPending(false);
    }
  }

  async function enterOffice(entry = {}) {
    if (proximity !== "nearby" || active) return false;
    if (!occupancy.available) {
      await refreshOccupancy();
    }
    if (!occupancy.available) {
      setKeypadStatus("Office door status is unavailable.", "error");
      return false;
    }
    if (occupancy.occupied) {
      return openKeypad("entry", "exterior");
    }
    return requestOfficeEntry("");
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

  function completeOfficeExit() {
    if (!active) return false;
    closeFallback({ restoreFocus: false });
    closeKeypad({ restoreFocus: false });
    tasks?.setActive?.(false);
    meeting.leaveOffice();
    meeting.setEntryTicket?.("", 0);
    officeEntryTicket = "";
    officeEntryExpiresAt = 0;
    active = false;
    exitPending = false;
    void refreshOccupancy();
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
    setKeypadStatus(
      "Walk through the open Office door to return to Town Square.",
    );
    root.toast?.("Walk through the open Office door to leave.");
    return true;
  }

  async function onSceneKeypadKey(key, context = {}) {
    const normalized = String(key || "");
    const location =
      context?.location === "interior" ? "interior" : "exterior";
    if (!keypadOpen && location === "interior") {
      if (!active || !meeting.inRoom) {
        root.toast?.("Join an Office room before changing its code.");
        return false;
      }
      await refreshOccupancy();
      if (!openKeypad("set", "interior")) {
        root.toast?.(
          officeEntryExpiresAt <= Date.now()
            ? "The Office entry proof expired. Leave and re-enter before changing the code."
            : "Only the current authenticated Office occupant can change this code.",
        );
        return false;
      }
    } else if (!keypadOpen) {
      await enterOffice({ source: "keypad" });
    }
    if (
      !keypadOpen ||
      keypadLocation !== location ||
      entryPending ||
      !keypadInput
    ) {
      return false;
    }
    if (normalized === "focus") return true;
    if (normalized === "clear") {
      keypadInput.value = "";
      onKeypadInput();
      keypadInput.focus();
      return true;
    }
    if (normalized === "enter") {
      keypadForm?.requestSubmit();
      return true;
    }
    if (/^\d$/.test(normalized) && keypadInput.value.length < 4) {
      keypadInput.value += normalized;
      playOfficeTone(normalized);
      onKeypadInput();
      keypadInput.focus();
      return true;
    }
    return false;
  }

  function onClick(event) {
    const keypadDigit = event.target.closest(
      "[data-world-office-keypad-digit]",
    );
    if (keypadDigit) {
      event.preventDefault();
      if (!keypadInput || entryPending) return;
      const digit = String(keypadDigit.dataset.worldOfficeKeypadDigit || "");
      if (/^\d$/.test(digit) && keypadInput.value.length < 4) {
        keypadInput.value += digit;
        playOfficeTone(digit);
        onKeypadInput();
      }
      return;
    }
    if (event.target.closest("[data-world-office-keypad-clear]")) {
      event.preventDefault();
      if (keypadInput) keypadInput.value = "";
      onKeypadInput();
      keypadInput?.focus();
      return;
    }
    if (event.target.closest("[data-world-office-keypad-cancel]")) {
      event.preventDefault();
      closeKeypad();
      return;
    }
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
    }
  }

  function onKeyDown(event) {
    if (event.key === "Escape" && keypadOpen) {
      event.preventDefault();
      closeKeypad();
      return;
    }
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

  function onKeypadInput() {
    if (!keypadInput) return;
    keypadInput.value = keypadInput.value.replace(/\D/g, "").slice(0, 4);
    world.setOfficeKeypadDigits?.(
      keypadInput.value,
      keypadMode,
      keypadLocation,
    );
  }

  function onKeypadSubmit(event) {
    event.preventDefault();
    if (!keypadOpen || entryPending || !keypadInput) return;
    const code = keypadInput.value.replace(/\D/g, "").slice(0, 4);
    // Clear the secret from the DOM before the network request starts. It is
    // sent only in the same-origin POST body and never enters URLs, storage,
    // analytics, logs, multiplayer presence, or a meeting frame.
    keypadInput.value = "";
    world.setOfficeKeypadDigits?.("", keypadMode, keypadLocation);
    if (!/^\d{4}$/.test(code)) {
      setKeypadStatus("Enter exactly four digits.", "error");
      keypadInput.focus();
      return;
    }
    for (const digit of code) playOfficeTone(digit);
    if (keypadMode === "set") {
      void requestOfficeCodeUpdate(code);
      return;
    }
    void requestOfficeEntry(code);
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
    window.clearInterval(occupancyTimer);
    occupancyTimer = null;
    tasks?.setActive?.(false);
    world.setOfficeExitHandler?.(null);
    world.setOfficeKeypadHandler?.(null);
    root.removeEventListener("click", onClick);
    window.removeEventListener("keydown", onKeyDown);
    window.removeEventListener("message", onMessage);
    keypadForm?.removeEventListener("submit", onKeypadSubmit);
    keypadInput?.removeEventListener("input", onKeypadInput);
    closeKeypad({ restoreFocus: false });
    meeting.setEntryTicket?.("", 0);
    officeEntryTicket = "";
    officeEntryExpiresAt = 0;
    closeFallback({ restoreFocus: false });
    frame?.removeAttribute("src");
  }

  root.addEventListener("click", onClick);
  world.setOfficeExitHandler?.(completeOfficeExit);
  world.setOfficeKeypadHandler?.((key, context) => {
    void onSceneKeypadKey(key, context);
  });
  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("message", onMessage);
  keypadForm?.addEventListener("submit", onKeypadSubmit);
  keypadInput?.addEventListener("input", onKeypadInput);
  occupancyTimer = window.setInterval(() => {
    if (
      document.visibilityState === "visible" &&
      (proximity === "nearby" || active)
    ) {
      void refreshOccupancy();
    }
  }, OFFICE_STATUS_POLL_MS);
  void refreshOccupancy();
  renderPrompt();

  return {
    setProximity,
    setOccupancy,
    refreshOccupancy,
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
