export const OFFICE_ENTER_DISTANCE = 6.5;
export const OFFICE_EXIT_DISTANCE = 7.5;
const OFFICE_CHAT_PATH = "/chat?embed=office";
const OFFICE_UNLOAD_DELAY_MS = 2000;
const OFFICE_CODE_KEY = "forkmesh.office.entry-code.v1";

function officeEntryCode() {
  try {
    let code = localStorage.getItem(OFFICE_CODE_KEY) || "";
    if (!/^\d{10}$/.test(code)) {
      code = Array.from({ length: 10 }, () => Math.floor(Math.random() * 10)).join("");
      localStorage.setItem(OFFICE_CODE_KEY, code);
    }
    return code;
  } catch (_) {
    return "0000000000";
  }
}

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
  chatPath = OFFICE_CHAT_PATH,
}) {
  const prompt = root.querySelector("[data-world-office-prompt]");
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
    renderPrompt();
  }

  function focusOffice(trigger = null) {
    if (trigger instanceof HTMLElement) returnFocus = trigger;
    world.focusLandmark("office");
  }

  function enterOffice() {
    if (proximity !== "nearby" || active) return false;
    const code = officeEntryCode();
    const entered = window.prompt(
      `ForkMesh Office door code (10 digits). Share this code with other visitors:\n${code}`,
      "",
    );
    for (const digit of String(entered || "").replace(/\D/g, "")) playOfficeTone(digit);
    if (String(entered || "").trim() !== code) {
      prompt?.querySelector("p")?.replaceChildren(
        document.createTextNode("Door locked · enter the 10-digit office code"),
      );
      return false;
    }
    if (!world.enterOffice()) return false;
    if (!returnFocus) returnFocus = enterButton;
    active = true;
    meeting.openLobby();
    renderPrompt();
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

  function collapse() {
    if (!active) return false;
    closeFallback({ restoreFocus: false });
    meeting.leaveOffice();
    active = false;
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
      returnFocus = enterControl;
      enterOffice();
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
      enterOffice();
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
    root.removeEventListener("click", onClick);
    window.removeEventListener("keydown", onKeyDown);
    window.removeEventListener("message", onMessage);
    closeFallback({ restoreFocus: false });
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
