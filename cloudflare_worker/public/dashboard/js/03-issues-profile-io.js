  function openImageResizeModal(file, maxBytes) {
    return new Promise((resolve) => {
      const objectUrl = URL.createObjectURL(file);
      let settled = false;
      const finish = (result) => {
        if (settled) return;
        settled = true;
        document.removeEventListener("keydown", onKeydown);
        URL.revokeObjectURL(objectUrl);
        overlay.remove();
        resolve(result);
      };
      const onKeydown = (event) => {
        if (event.key === "Escape") { event.preventDefault(); finish(null); }
      };

      const overlay = document.createElement("div");
      overlay.className = "fixed inset-0 z-50 flex items-start justify-center overflow-auto bg-background/80 p-4 pt-10 backdrop-blur-sm";
      overlay.setAttribute("role", "dialog");
      overlay.setAttribute("aria-modal", "true");
      overlay.setAttribute("aria-label", "Resize image");
      overlay.innerHTML = `
        <div class="flex max-h-full w-full max-w-2xl flex-col overflow-hidden rounded-lg border border-border bg-card shadow-xl">
          <div class="flex items-center justify-between gap-3 border-b border-border px-4 py-3">
            <span class="min-w-0 truncate text-sm font-semibold text-foreground">Resize "${escapeHtml(file.name)}"</span>
            <button type="button" data-resize-cancel class="shrink-0 rounded-md border border-border px-2 py-1 text-xs text-muted-foreground hover:bg-secondary hover:text-foreground">Cancel</button>
          </div>
          <div class="grid gap-3 overflow-auto p-4">
            <p class="text-xs text-muted-foreground">Drag on the image to crop it, then it's auto-compressed to fit. Attached images share the issue's size limit — redraw a smaller crop if it still doesn't fit.</p>
            <div data-resize-stage class="relative mx-auto inline-block max-h-[24rem] max-w-full touch-none select-none overflow-hidden rounded-md border border-border bg-secondary/30">
              <img data-resize-image src="${objectUrl}" class="block max-h-[24rem] max-w-full select-none" alt="" draggable="false" />
              <div data-resize-crop class="absolute hidden border-2 border-primary bg-primary/10"></div>
            </div>
            <div class="flex flex-wrap items-center justify-between gap-2 text-xs">
              <span data-resize-status class="text-muted-foreground">Estimating size…</span>
              <button type="button" data-resize-reset class="rounded-md border border-border px-2 py-1 text-muted-foreground hover:bg-secondary hover:text-foreground">Reset crop</button>
            </div>
          </div>
          <div class="flex items-center justify-end gap-2 border-t border-border px-4 py-3">
            <button type="button" data-resize-add disabled class="inline-flex h-8 items-center gap-2 rounded-md bg-primary px-3 text-xs font-medium text-primary-foreground hover:bg-primary/90 disabled:cursor-not-allowed disabled:opacity-50"><i data-lucide="check" class="h-3.5 w-3.5"></i>Add image</button>
          </div>
        </div>`;
      document.body.appendChild(overlay);
      window.lucide?.createIcons();
      document.addEventListener("keydown", onKeydown);

      const stage = overlay.querySelector("[data-resize-stage]");
      const modalImg = overlay.querySelector("[data-resize-image]");
      const cropDiv = overlay.querySelector("[data-resize-crop]");
      const status = overlay.querySelector("[data-resize-status]");
      const addButton = overlay.querySelector("[data-resize-add]");
      const resetButton = overlay.querySelector("[data-resize-reset]");
      const cancelButton = overlay.querySelector("[data-resize-cancel]");

      let cropRectCss = null;
      let dragStart = null;
      let pendingBlob = null;
      let compressToken = 0;

      const clamp = (value, min, max) => Math.min(Math.max(value, min), max);

      const updateCropVisual = () => {
        if (!cropRectCss) {
          cropDiv.classList.add("hidden");
          return;
        }
        cropDiv.classList.remove("hidden");
        cropDiv.style.left = `${cropRectCss.left}px`;
        cropDiv.style.top = `${cropRectCss.top}px`;
        cropDiv.style.width = `${cropRectCss.width}px`;
        cropDiv.style.height = `${cropRectCss.height}px`;
      };

      const attemptCompress = async () => {
        if (!modalImg.naturalWidth) return;
        const attemptId = (compressToken += 1);
        pendingBlob = null;
        addButton.disabled = true;
        status.className = "text-muted-foreground";
        status.textContent = "Compressing…";

        const scaleX = modalImg.naturalWidth / modalImg.clientWidth;
        const scaleY = modalImg.naturalHeight / modalImg.clientHeight;
        let sx = 0;
        let sy = 0;
        let sw = modalImg.naturalWidth;
        let sh = modalImg.naturalHeight;
        if (cropRectCss) {
          sx = Math.round(cropRectCss.left * scaleX);
          sy = Math.round(cropRectCss.top * scaleY);
          sw = Math.max(1, Math.round(cropRectCss.width * scaleX));
          sh = Math.max(1, Math.round(cropRectCss.height * scaleY));
        }

        const qualities = [0.82, 0.65, 0.5, 0.35, 0.22, 0.12];
        let scale = 1;
        for (let round = 0; round < 6; round += 1) {
          const outW = Math.max(24, Math.round(sw * scale));
          const outH = Math.max(24, Math.round(sh * scale));
          const canvas = document.createElement("canvas");
          canvas.width = outW;
          canvas.height = outH;
          const ctx = canvas.getContext("2d");
          ctx.fillStyle = "#fff";
          ctx.fillRect(0, 0, outW, outH);
          ctx.drawImage(modalImg, sx, sy, sw, sh, 0, 0, outW, outH);
          for (const quality of qualities) {
            const blob = await canvasToBlob(canvas, "image/jpeg", quality);
            if (attemptId !== compressToken) return;
            if (blob && blob.size <= maxBytes) {
              pendingBlob = blob;
              status.className = "text-primary";
              status.textContent = `Ready — ${formatSize(blob.size)} (fits under ${formatSize(maxBytes)}).`;
              addButton.disabled = false;
              return;
            }
          }
          scale *= 0.65;
        }
        status.className = "text-destructive";
        status.textContent = "Still too large after compressing — drag to crop a smaller area and it'll retry.";
      };

      stage.addEventListener("pointerdown", (event) => {
        const rect = stage.getBoundingClientRect();
        dragStart = {
          x: clamp(event.clientX - rect.left, 0, rect.width),
          y: clamp(event.clientY - rect.top, 0, rect.height),
        };
        stage.setPointerCapture(event.pointerId);
      });
      stage.addEventListener("pointermove", (event) => {
        if (!dragStart) return;
        const rect = stage.getBoundingClientRect();
        const x = clamp(event.clientX - rect.left, 0, rect.width);
        const y = clamp(event.clientY - rect.top, 0, rect.height);
        cropRectCss = {
          left: Math.min(dragStart.x, x),
          top: Math.min(dragStart.y, y),
          width: Math.abs(x - dragStart.x),
          height: Math.abs(y - dragStart.y),
        };
        updateCropVisual();
      });
      stage.addEventListener("pointerup", () => {
        if (!dragStart) return;
        dragStart = null;
        if (!cropRectCss || cropRectCss.width < 8 || cropRectCss.height < 8) cropRectCss = null;
        updateCropVisual();
        attemptCompress();
      });

      resetButton.addEventListener("click", () => {
        cropRectCss = null;
        updateCropVisual();
        attemptCompress();
      });
      cancelButton.addEventListener("click", () => finish(null));
      addButton.addEventListener("click", async () => {
        if (!pendingBlob) return;
        const dataUrl = await readAsDataUrl(pendingBlob);
        finish({ dataUrl, size: pendingBlob.size });
      });

      modalImg.addEventListener("load", () => attemptCompress());
      modalImg.addEventListener("error", () => finish(null));
      if (modalImg.complete && modalImg.naturalWidth) attemptCompress();
    });
  }

  function bytesToB64url(bytes) {
    const arr = new Uint8Array(bytes);
    let bin = "";
    for (let i = 0; i < arr.length; i += 1) bin += String.fromCharCode(arr[i]);
    return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  }

  async function sha256HexLower(text) {
    const digest = await crypto.subtle.digest("SHA-256", ISSUE_TEXT_ENCODER.encode(text));
    return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, "0")).join("");
  }

  async function getWebIssueKey() {
    let stored = null;
    try { stored = JSON.parse(localStorage.getItem(WEB_ISSUE_KEY_STORAGE) || "null"); } catch (_) {}
    if (stored && stored.jwk && stored.pub) {
      try {
        const privateKey = await crypto.subtle.importKey("jwk", stored.jwk, { name: "Ed25519" }, false, ["sign"]);
        return { privateKey, pub: stored.pub };
      } catch (_) { /* fall through and mint a fresh key */ }
    }
    const pair = await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]);
    const rawPub = await crypto.subtle.exportKey("raw", pair.publicKey);
    const jwk = await crypto.subtle.exportKey("jwk", pair.privateKey);
    const pub = bytesToB64url(rawPub);
    try { localStorage.setItem(WEB_ISSUE_KEY_STORAGE, JSON.stringify({ jwk, pub })); } catch (_) {}
    const privateKey = await crypto.subtle.importKey("jwk", jwk, { name: "Ed25519" }, false, ["sign"]);
    return { privateKey, pub };
  }

  // Mirrors IssueStore::contentForSigning + canonicalString and the desktop's
  // inbox POST (verify_issue_event in the worker). New issues are signed with
  // number 0; the maintainer assigns the durable number on drain.
  async function submitWebIssue(repo, title, body, assignAgent = false, agentModel = "", agentProvider = "") {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const cleanBody = String(body || "").replace(/[\r\n]+$/, "");
    // open event content = title \0 body \0 attachments(joined by ","; empty here)
    const NUL = String.fromCharCode(0);
    const content = title + NUL + cleanBody + NUL;
    const contentHash = await sha256HexLower(content);
    const canonical = `forkmesh-issue-event-v1\nopen\n0\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const event = {
      type: "open",
      id: "open-web-" + ts,
      title,
      body: cleanBody,
      attachments: [],
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    const payload = {
      owner: repo.owner,
      repo: repo.name,
      number: 0,
      titleIfNew: title,
      event,
      meta: { labels: [], milestone: "", priority: 0, assignees: [], wantsAgent: Boolean(assignAgent), model: assignAgent ? String(agentModel || "") : "", provider: assignAgent ? String(agentProvider || "") : "" },
    };
    // When assignAgent is true, include ownerAccount so the server verifies
    // it's the repo owner or an admin (adhoc #225).
    if (assignAgent) {
      payload.ownerAccount = state.session?.nodeName || "";
    }
    const response = await fetch(`${repoApiBase(repo)}/issues`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  // Mirrors DiscussionStore::contentForSigning's "comment" case (just the
  // reply body) and the desktop's discussion inbox POST (verify_discussion_event
  // in the worker). Replies are signed against the discussion's real number —
  // unlike a new discussion's "open" event, they don't use the placeholder 0.
  async function submitWebDiscussionComment(repo, number, body) {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const cleanBody = String(body || "").replace(/[\r\n]+$/, "");
    const contentHash = await sha256HexLower(cleanBody);
    const canonical = `forkmesh-discussion-event-v1\ncomment\n${number}\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const event = {
      type: "comment",
      id: "comment-web-" + ts,
      body: cleanBody,
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    const payload = { owner: repo.owner, repo: repo.name, number, event };
    const response = await fetch(`${repoApiBase(repo)}/discussions`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  async function handleDiscussionReplySubmit(repo, form) {
    if (!repo || !form) return;
    const number = Number(form.dataset.repoDiscussionReplyNumber || 0);
    const bodyInput = form.querySelector("[data-repo-discussion-reply-body]");
    const submit = form.querySelector("[data-repo-discussion-reply-submit]");
    const hint = form.querySelector("[data-repo-discussion-reply-hint]");
    const setHint = (text, tone) => {
      if (hint) hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      if (hint) hint.textContent = text;
    };
    const body = String(bodyInput?.value || "").trim();
    if (!number) {
      setHint("This discussion hasn't finished loading yet.", "bad");
      return;
    }
    if (!body) {
      setHint("Write a reply before sending.", "bad");
      bodyInput?.focus();
      return;
    }
    if (submit) submit.disabled = true;
    setHint("Signing and sending…");
    try {
      await submitWebDiscussionComment(repo, number, body);
      const list = form.parentElement?.querySelector("[data-repo-discussion-conversation]");
      if (list) {
        if (list.dataset.empty === "true") list.innerHTML = "";
        list.dataset.empty = "false";
        list.insertAdjacentHTML("beforeend", renderPullConversationEvent({
          type: "comment", authorName: state.session?.nodeName || "you",
          ts: Math.floor(Date.now() / 1000), body,
        }));
      }
      if (bodyInput) bodyInput.value = "";
      if (submit) submit.disabled = false;
      setHint("Reply sent to the maintainer's inbox for review.", "good");
    } catch (error) {
      if (submit) submit.disabled = false;
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "The maintainer's inbox is full. Try again later."
          : code === "author_quota" ? "You've reached the submission limit for this repository."
          : code === "discussion_too_large" ? "The reply is too large - please shorten it."
          : code === "bad_signature" ? "Could not verify the reply's signature."
          : "Could not send the reply. Please try again.",
        "bad");
    }
  }

  function sessionFromAccountPayload(body, base = {}) {
    const nextSession = {
      ...(base || {}),
      nodeName: body.nodeName || body.name || base.nodeName || "",
      email: body.email ?? base.email ?? "",
      status: body.status || base.status || "active",
      pubkey: body.pubkey || base.pubkey || "",
      emailVerified: Object.prototype.hasOwnProperty.call(body, "emailVerified")
        ? Boolean(body.emailVerified)
        : Boolean(base.emailVerified),
      isAdmin: Object.prototype.hasOwnProperty.call(body, "isAdmin")
        ? Boolean(body.isAdmin)
        : Boolean(base.isAdmin),
      adminUrl: body.adminUrl || base.adminUrl || "",
      solana: body.solana || base.solana || "",
      hasPayoutAddress: Object.prototype.hasOwnProperty.call(body, "hasPayoutAddress")
        ? Boolean(body.hasPayoutAddress)
        : Boolean(base.hasPayoutAddress),
      avatarPng: body.avatarPng || "",
      avatarUpdatedAt: Number(body.avatarUpdatedAt) || 0,
      kind: body.kind || base.kind || "",
      owner: body.owner ?? base.owner ?? "",
      nodes: Array.isArray(body.nodes) ? body.nodes : (base.nodes || []),
      at: Date.now(),
    };
    if (!Object.prototype.hasOwnProperty.call(body, "avatarPng")) {
      nextSession.avatarPng = base.avatarPng || "";
    }
    if (!Object.prototype.hasOwnProperty.call(body, "avatarUpdatedAt")) {
      nextSession.avatarUpdatedAt = Number(base.avatarUpdatedAt) || 0;
    }
    return nextSession;
  }

  function applyAvatar(container, session) {
    if (!container) return;
    const name = session?.nodeName || session?.email || "ForkMesh";
    const initial = (name[0] || "F").toUpperCase();
    const avatarPng = session?.avatarPng || "";
    const image = container.querySelector("img");
    const initialEl = container.querySelector("[data-avatar-initial]");
    if (image) {
      if (avatarPng) {
        image.src = `data:image/png;base64,${avatarPng}`;
        image.alt = `${name} profile picture`;
      } else {
        image.removeAttribute("src");
        image.alt = "";
      }
      image.classList.toggle("hidden", !avatarPng);
    }
    if (initialEl) {
      initialEl.textContent = initial;
      initialEl.classList.toggle("hidden", Boolean(avatarPng));
    } else if (!image) {
      container.textContent = initial;
    }
    container.classList.toggle("font-mono", !avatarPng);
  }

  async function refreshPublicProfile(session = state.session) {
    const nodeName = String(session?.nodeName || "").trim().toLowerCase();
    if (!validNodeName(nodeName)) return session;
    try {
      const body = await fetchJson(`/api/accounts/${encodeURIComponent(nodeName)}`);
      if (body.exists === false) return session;
      const nextSession = sessionFromAccountPayload(body, session);
      writeSession(nextSession);
      renderProfile(nextSession);
      return nextSession;
    } catch (_) {
      return session;
    }
  }

  // One lightweight /api/poll instead of re-fetching the full profile (avatar
  // and all) and the full notification list every tick. The server returns
  // cheap change tokens; we only fire the heavier fetches when a token moved.
  // The unread count rides along, so the bell badge updates from the poll alone.
  async function pollStatus(force = false) {
    const node = String(state.session?.nodeName || "").trim().toLowerCase();
    if (!validNodeName(node)) return;
    let data;
    try {
      data = await fetchJson(`/api/poll?node=${encodeURIComponent(node)}`);
    } catch (_) {
      return;
    }
    const profileToken = data.profile?.token ?? null;
    if (force || (profileToken !== null && profileToken !== state.pollProfileToken)) {
      await refreshPublicProfile(state.session);
    }
    if (profileToken !== null) state.pollProfileToken = profileToken;

    const notif = data.notif;
    if (notif && typeof notif.unread === "number") {
      state.notificationUnread = notif.unread;
      renderNotificationPreview();
    }
    const notifToken = notif?.token ?? null;
    if (force || (notifToken !== null && notifToken !== state.pollNotifToken)) {
      await loadNotifications();
    }
    if (notifToken !== null) state.pollNotifToken = notifToken;
  }

  function startProfileSync() {
    if (state.profileSyncTimer || !state.session?.nodeName) return;
    state.profileSyncTimer = window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      pollStatus();
    }, PROFILE_SYNC_INTERVAL_MS);
  }

