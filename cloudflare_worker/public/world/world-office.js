export const OFFICE_ENTER_DISTANCE = 6.5;
export const OFFICE_EXIT_DISTANCE = 7.5;
const OFFICE_CHAT_PATH = "/chat?embed=office";
const OFFICE_UNLOAD_DELAY_MS = 2000;

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
  chatPath = OFFICE_CHAT_PATH,
}) {
  const prompt = root.querySelector("[data-world-office-prompt]");
  const enterButton = root.querySelector("[data-world-office-enter]");
  const panel = root.querySelector("[data-world-office-chat]");
  const heading = root.querySelector("#world-office-chat-title");
  const loading = root.querySelector("[data-world-office-loading]");
  const frame = root.querySelector("[data-world-office-frame]");
  let proximity = "distant";
  let active = false;
  let returnFocus = null;
  let unloadTimer = null;
  let frameSuspended = false;

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
  }

  function setProximity(nextState) {
    proximity = nextState === "nearby" ? "nearby" : "distant";
    if (proximity === "distant" && active) collapse();
    renderPrompt();
  }

  function focusOffice(trigger = null) {
    if (trigger instanceof HTMLElement) returnFocus = trigger;
    world.focusLandmark("office");
  }

  function enterOffice(trigger = null) {
    if (proximity !== "nearby" || active) return false;
    if (!world.enterOffice()) return false;
    if (trigger instanceof HTMLElement) returnFocus = trigger;
    else if (!returnFocus) returnFocus = enterButton;
    clearUnloadTimer();
    active = true;
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
    renderPrompt();
    window.requestAnimationFrame(() => heading?.focus());
    return true;
  }

  function collapse() {
    if (!active) return false;
    active = false;
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
    renderPrompt();
    const focusTarget = returnFocus?.isConnected ? returnFocus : enterButton;
    window.requestAnimationFrame(() => focusTarget?.focus());
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
      enterOffice(enterControl);
      return;
    }
    if (event.target.closest("[data-world-office-close]")) {
      event.preventDefault();
      collapse();
    }
  }

  function onKeyDown(event) {
    if (event.key === "Escape" && active) {
      event.preventDefault();
      collapse();
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
      enterOffice(enterButton);
    }
  }

  function onMessage(event) {
    if (event.origin !== window.location.origin) return;
    if (event.source !== frame.contentWindow) return;
    const type = String(event.data?.type || "");
    if (type === "office-chat-ready" && loading) loading.hidden = true;
    if (type !== "office-chat-ready" && type !== "office-chat-room-changed") {
      return;
    }
  }

  function destroy() {
    clearUnloadTimer();
    root.removeEventListener("click", onClick);
    window.removeEventListener("keydown", onKeyDown);
    window.removeEventListener("message", onMessage);
    if (frame?.contentWindow && frame.hasAttribute("src")) {
      frame.contentWindow.postMessage(
        { type: "office-chat-suspend" },
        window.location.origin,
      );
    }
    frame?.removeAttribute("src");
  }

  root.addEventListener("click", onClick);
  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("message", onMessage);
  renderPrompt();

  return {
    setProximity,
    focusOffice,
    enterOffice,
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
