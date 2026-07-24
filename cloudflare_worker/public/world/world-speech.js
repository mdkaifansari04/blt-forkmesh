(() => {
  "use strict";

  const allowedDestinations = new Set(["chat", "issue", "note"]);
  const state = {
    baseUrl: "",
    sessionToken: "",
    expiresAt: 0,
    destination: "",
    captureId: "",
    lastRevision: "",
    pollTimer: 0,
    paired: false,
  };

  const escapeText = (value) => String(value ?? "").replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g, "");
  const requestId = () =>
    globalThis.crypto?.randomUUID?.() ||
    `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}-request`;

  function bridgeUrl(port, path) {
    const parsed = Number.parseInt(String(port), 10);
    if (!Number.isInteger(parsed) || parsed < 1 || parsed > 65535) {
      throw new Error("Enter the local port shown by the ForkMesh desktop client.");
    }
    return `http://127.0.0.1:${parsed}${path}`;
  }

  async function bridgeFetch(path, options = {}) {
    if (!state.baseUrl) throw new Error("Pair the desktop client first.");
    const headers = new Headers(options.headers || {});
    if (state.sessionToken) headers.set("Authorization", `Bearer ${state.sessionToken}`);
    if (options.mutation) headers.set("X-ForkMesh-Request-Id", requestId());
    if (options.body) headers.set("Content-Type", "application/json");
    const response = await fetch(`${state.baseUrl}${path}`, {
      method: options.method || "GET",
      headers,
      body: options.body ? JSON.stringify(options.body) : undefined,
      cache: "no-store",
      credentials: "omit",
      redirect: "error",
      referrerPolicy: "no-referrer",
      mode: "cors",
      signal: AbortSignal.timeout(7000),
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      const code = escapeText(payload.error || `local_bridge_${response.status}`);
      throw new Error(code.replaceAll("_", " "));
    }
    return payload;
  }

  function install() {
    if (document.querySelector(".world-speech-launcher")) return;
    const launcher = document.createElement("button");
    launcher.type = "button";
    launcher.className = "world-speech-launcher";
    launcher.setAttribute("aria-expanded", "false");
    launcher.setAttribute("aria-controls", "world-speech-panel");
    launcher.setAttribute("aria-label", "Open local voice input");
    launcher.title = "Local Qt speech-to-text";
    launcher.textContent = "♩";

    const panel = document.createElement("section");
    panel.className = "world-speech-panel";
    panel.id = "world-speech-panel";
    panel.hidden = true;
    panel.setAttribute("aria-label", "Local voice input");
    panel.innerHTML = `
      <div class="world-speech-heading">
        <h2>Local voice input</h2>
        <button class="world-speech-close" type="button" aria-label="Close voice input">×</button>
      </div>
      <p>Pair this tab with your installed Qt client. Qt records and transcribes locally; ForkMesh never relays microphone audio.</p>
      <div class="world-speech-row">
        <label>Local port
          <input class="world-speech-port" inputmode="numeric" autocomplete="off" placeholder="Shown in Qt" />
        </label>
        <label>One-time pairing code
          <input class="world-speech-code" type="password" autocomplete="off" spellcheck="false" placeholder="Expires shortly" />
        </label>
      </div>
      <div class="world-speech-controls">
        <button class="world-speech-pair" type="button">Pair desktop</button>
        <button class="world-speech-revoke" type="button" disabled>Revoke</button>
      </div>
      <p><strong>Choose where the transcript goes before starting:</strong></p>
      <div class="world-speech-tabs" role="tablist" aria-label="Transcript destination">
        <button class="world-speech-tab" type="button" role="tab" aria-selected="false" data-destination="chat">Chat</button>
        <button class="world-speech-tab" type="button" role="tab" aria-selected="false" data-destination="issue">Issue</button>
        <button class="world-speech-tab" type="button" role="tab" aria-selected="false" data-destination="note">Note</button>
      </div>
      <label class="world-speech-composer" data-composer="chat" hidden>Chat composer
        <textarea aria-label="Chat composer" placeholder="Transcript will appear here; it is never sent automatically."></textarea>
      </label>
      <label class="world-speech-composer" data-composer="issue" hidden>Issue composer
        <textarea aria-label="Issue composer" placeholder="Transcript will appear here; review it before creating an issue."></textarea>
      </label>
      <label class="world-speech-composer" data-composer="note" hidden>Private local note
        <textarea aria-label="Note composer" placeholder="Transcript remains in this tab unless you choose to copy it."></textarea>
      </label>
      <div class="world-speech-controls">
        <button class="world-speech-start" type="button" disabled>Start local mic</button>
        <button class="world-speech-stop" type="button" disabled>Stop & transcribe</button>
        <button class="world-speech-cancel" type="button" disabled>Cancel</button>
        <button class="world-speech-copy" type="button" disabled>Copy draft</button>
      </div>
      <div class="world-speech-status" role="status" aria-live="polite" data-state="idle">Not paired.</div>
      <p class="world-speech-privacy">Pairing/session capabilities stay in memory, never in URLs or persistent browser storage. Closing or reloading this tab forgets them. Transcript drafts are not submitted automatically.</p>
    `;
    document.body.append(panel, launcher);

    const $ = (selector) => panel.querySelector(selector);
    const portInput = $(".world-speech-port");
    const codeInput = $(".world-speech-code");
    const pairButton = $(".world-speech-pair");
    const revokeButton = $(".world-speech-revoke");
    const startButton = $(".world-speech-start");
    const stopButton = $(".world-speech-stop");
    const cancelButton = $(".world-speech-cancel");
    const copyButton = $(".world-speech-copy");
    const status = $(".world-speech-status");
    const tabs = [...panel.querySelectorAll(".world-speech-tab")];
    const composers = [...panel.querySelectorAll(".world-speech-composer")];

    const setStatus = (message, statusState = "idle") => {
      status.textContent = message;
      status.dataset.state = statusState;
    };

    const selectedComposer = () =>
      panel.querySelector(`[data-composer="${state.destination}"] textarea`);

    const setActive = (active) => {
      startButton.disabled = !state.paired || !state.destination || active;
      stopButton.disabled = !active;
      cancelButton.disabled = !active;
      pairButton.disabled = active || state.paired;
      revokeButton.disabled = !state.paired || active;
      tabs.forEach((tab) => {
        tab.disabled = active;
      });
      composers.forEach((composer) => {
        const textarea = composer.querySelector("textarea");
        textarea.disabled = active;
      });
    };

    const forgetSession = () => {
      if (state.pollTimer) window.clearTimeout(state.pollTimer);
      state.pollTimer = 0;
      state.sessionToken = "";
      state.expiresAt = 0;
      state.captureId = "";
      state.lastRevision = "";
      state.paired = false;
      state.baseUrl = "";
      codeInput.value = "";
      copyButton.disabled = !selectedComposer()?.value;
      setActive(false);
    };

    async function pollTranscript() {
      if (!state.paired || !state.captureId) return;
      const captureId = state.captureId;
      try {
        const payload = await bridgeFetch("/v1/transcription");
        // Cancel/restart may happen while the request is in flight. A stale
        // response must never restore a cleared draft or overwrite the
        // explicit cancellation status.
        if (state.captureId !== captureId) return;
        if (payload.captureId !== captureId) {
          throw new Error("The desktop capture changed; start again.");
        }
        if (payload.revision !== state.lastRevision) {
          state.lastRevision = payload.revision;
          const composer = selectedComposer();
          if (composer && typeof payload.transcript === "string") {
            composer.value = escapeText(payload.transcript);
            composer.dispatchEvent(new Event("input", { bubbles: true }));
            copyButton.disabled = !composer.value;
          }
        }
        const next = escapeText(payload.state || "error");
        if (next === "recording") {
          setStatus("Recording locally in Qt…", "recording");
        } else if (next === "transcribing") {
          setStatus("Qt is transcribing locally…", "transcribing");
        } else if (next === "complete") {
          setStatus("Transcript ready. Review it before using it.", "complete");
          state.captureId = "";
          setActive(false);
          return;
        } else if (next === "cancelled" || next === "no-speech") {
          setStatus(next === "cancelled" ? "Capture cancelled." : "No speech detected.", next);
          state.captureId = "";
          setActive(false);
          return;
        } else if (next === "error") {
          throw new Error(escapeText(payload.error || "Local transcription failed."));
        }
        state.pollTimer = window.setTimeout(pollTranscript, 450);
      } catch (error) {
        state.captureId = "";
        setActive(false);
        setStatus(error.message || "Could not reach the local Qt bridge.", "error");
      }
    }

    launcher.addEventListener("click", () => {
      panel.hidden = !panel.hidden;
      launcher.setAttribute("aria-expanded", String(!panel.hidden));
      if (!panel.hidden) (state.paired ? startButton : portInput).focus();
    });
    $(".world-speech-close").addEventListener("click", () => {
      panel.hidden = true;
      launcher.setAttribute("aria-expanded", "false");
      launcher.focus();
    });

    tabs.forEach((tab) => {
      tab.addEventListener("click", () => {
        const destination = tab.dataset.destination || "";
        if (!allowedDestinations.has(destination) || state.captureId) return;
        state.destination = destination;
        tabs.forEach((item) =>
          item.setAttribute("aria-selected", String(item === tab)),
        );
        composers.forEach((composer) => {
          composer.hidden = composer.dataset.composer !== destination;
        });
        copyButton.disabled = !selectedComposer()?.value;
        setActive(false);
        selectedComposer()?.focus();
      });
    });

    pairButton.addEventListener("click", async () => {
      const code = codeInput.value.trim();
      if (!code) {
        setStatus("Enter the one-time code shown in Qt.", "error");
        return;
      }
      pairButton.disabled = true;
      setStatus("Pairing with the local desktop client…", "transcribing");
      try {
        state.baseUrl = bridgeUrl(portInput.value, "");
        const response = await fetch(`${state.baseUrl}/v1/pair`, {
          method: "POST",
          headers: { Authorization: `Bearer ${code}` },
          cache: "no-store",
          credentials: "omit",
          redirect: "error",
          referrerPolicy: "no-referrer",
          mode: "cors",
          signal: AbortSignal.timeout(7000),
        });
        codeInput.value = "";
        const payload = await response.json().catch(() => ({}));
        if (!response.ok || !payload.sessionToken) {
          throw new Error(escapeText(payload.error || "Pairing was rejected."));
        }
        state.sessionToken = payload.sessionToken;
        state.expiresAt = Date.parse(payload.expiresAt || "");
        state.paired = true;
        revokeButton.disabled = false;
        setStatus("Paired. Choose Chat, Issue, or Note, then start the local mic.", "complete");
        setActive(false);
      } catch (error) {
        forgetSession();
        pairButton.disabled = false;
        setStatus(
          `${error.message || "Could not pair."} Keep Qt open and verify its exact World origin.`,
          "error",
        );
      }
    });

    startButton.addEventListener("click", async () => {
      if (!state.paired || !allowedDestinations.has(state.destination)) {
        setStatus("Pair Qt and choose a destination first.", "error");
        return;
      }
      if (Number.isFinite(state.expiresAt) && state.expiresAt <= Date.now()) {
        forgetSession();
        setStatus("The local pairing expired. Pair again.", "error");
        return;
      }
      try {
        const composer = selectedComposer();
        if (!composer) throw new Error("Choose a composer first.");
        composer.value = "";
        copyButton.disabled = true;
        const payload = await bridgeFetch("/v1/transcription/start", {
          method: "POST",
          mutation: true,
          body: { destination: state.destination },
        });
        state.captureId = payload.captureId;
        state.lastRevision = "";
        setActive(true);
        setStatus("Recording locally in Qt…", "recording");
        state.pollTimer = window.setTimeout(pollTranscript, 450);
      } catch (error) {
        setActive(false);
        setStatus(error.message || "Could not start local capture.", "error");
      }
    });

    stopButton.addEventListener("click", async () => {
      try {
        await bridgeFetch("/v1/transcription/stop", {
          method: "POST",
          mutation: true,
        });
        stopButton.disabled = true;
        setStatus("Qt is transcribing locally…", "transcribing");
      } catch (error) {
        setStatus(error.message || "Could not stop local capture.", "error");
      }
    });

    cancelButton.addEventListener("click", async () => {
      if (state.pollTimer) window.clearTimeout(state.pollTimer);
      state.pollTimer = 0;
      // Invalidate an already-running poll before awaiting the local bridge.
      // Its response is ignored by pollTranscript's capture-id check.
      state.captureId = "";
      const composer = selectedComposer();
      if (composer) composer.value = "";
      copyButton.disabled = true;
      setActive(false);
      setStatus("Cancelling local capture…", "transcribing");
      try {
        await bridgeFetch("/v1/transcription/cancel", {
          method: "POST",
          mutation: true,
        });
      } catch (error) {
        setStatus(
          `${error.message || "Could not cancel local capture."} Use the Qt stop control if its microphone is still active.`,
          "error",
        );
        return;
      }
      setStatus("Capture cancelled; no transcript was submitted.", "cancelled");
    });

    revokeButton.addEventListener("click", async () => {
      try {
        await bridgeFetch("/v1/session/revoke", {
          method: "POST",
          mutation: true,
        });
      } catch (_) {
        // Forgetting the in-memory token is still a safe local revocation path.
      }
      forgetSession();
      setStatus("Pairing revoked.", "idle");
    });

    copyButton.addEventListener("click", async () => {
      const draft = selectedComposer()?.value || "";
      if (!draft) return;
      try {
        await navigator.clipboard.writeText(draft);
        setStatus(`${state.destination} draft copied. Review before submitting.`, "complete");
      } catch (_) {
        selectedComposer()?.select();
        setStatus("Clipboard permission was denied; the draft is selected for manual copy.", "error");
      }
    });

    window.addEventListener(
      "pagehide",
      () => {
        if (!state.paired) return;
        // Best-effort revocation when a paired tab closes. The local expiry is
        // still authoritative if the browser cannot complete an unload request.
        fetch(`${state.baseUrl}/v1/session/revoke`, {
          method: "POST",
          headers: {
            Authorization: `Bearer ${state.sessionToken}`,
            "X-ForkMesh-Request-Id": requestId(),
          },
          cache: "no-store",
          credentials: "omit",
          redirect: "error",
          referrerPolicy: "no-referrer",
          mode: "cors",
          keepalive: true,
        }).catch(() => {});
        state.sessionToken = "";
      },
      { once: true },
    );
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", install, { once: true });
  } else {
    install();
  }
})();
