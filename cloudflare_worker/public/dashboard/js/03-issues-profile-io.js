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
            <p class="text-xs text-muted-foreground">Drag on the image to crop it, then it's auto-compressed to fit. Attached images share the issue's size limit - redraw a smaller crop if it still doesn't fit.</p>
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
              status.textContent = `Ready - ${formatSize(blob.size)} (fits under ${formatSize(maxBytes)}).`;
              addButton.disabled = false;
              return;
            }
          }
          scale *= 0.65;
        }
        status.className = "text-destructive";
        status.textContent = "Still too large after compressing - drag to crop a smaller area and it'll retry.";
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

  function webIssuePublicKey() {
    try {
      const stored = JSON.parse(
        localStorage.getItem(WEB_ISSUE_KEY_STORAGE) || "null",
      );
      return String(stored?.pub || "");
    } catch (_) {
      return "";
    }
  }

  function pendingIssuesRepoKey(repo) {
    return `${String(repo?.owner || "").toLowerCase()}/${String(repo?.name || "").toLowerCase()}`;
  }

  // Mirrors IssueStore::contentForSigning + canonicalString and the desktop's
  // submission POST (verify_issue_event in the worker). New issues are signed
  // with number 0; the first eligible mirror assigns the durable number when
  // it commits the issue to the repository it serves.
  async function submitWebIssue(repo, title, body, assignAgent = false, agentModel = "", agentProvider = "", extraMeta = {}) {
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
      meta: {
        labels: Array.isArray(extraMeta.labels) ? extraMeta.labels.map((x) => String(x)) : [],
        milestone: String(extraMeta.milestone || ""),
        project: String(extraMeta.project || ""),
        priority: Number.isFinite(extraMeta.priority) ? extraMeta.priority : 0,
        assignees: Array.isArray(extraMeta.assignees) ? extraMeta.assignees.map((x) => String(x)) : [],
        wantsAgent: Boolean(assignAgent),
        model: assignAgent ? String(agentModel || "") : "",
        provider: assignAgent ? String(agentProvider || "") : "",
      },
    };
    // When assignAgent is true, include the account session so the server can
    // verify the caller really is the repo owner or an admin (adhoc #225). The
    // session token is the proof; ownerAccount stays only as a display hint.
    if (assignAgent) {
      payload.ownerAccount = state.session?.nodeName || "";
      payload.sessionToken = state.session?.sessionToken || "";
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
    return { ...data, event };
  }

  // The compact World chat composer is a second presentation of the canonical
  // dashboard actions. Expose the signed issue operation narrowly so the chat
  // bundle can file through the same eligible-mirror delivery path without duplicating key,
  // signature, authorization, or payload logic.
  window.ForkMeshDashboardActions = Object.assign(
    window.ForkMeshDashboardActions || {},
    { submitWebIssue },
  );

  // Mirrors IssueStore::contentForSigning's "comment" case (body NUL
  // attachments) and the desktop's inbox POST (verify_issue_event in the
  // worker). Unlike a new issue's "open" event, a comment is signed against the
  // issue's real number - the maintainer's node appends it to that issue when it
  // drains the inbox (IssueStore::applyRemoteEvent).
  async function submitWebIssueComment(repo, number, body) {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const cleanBody = String(body || "").replace(/[\r\n]+$/, "");
    // comment event content = body \0 attachments(joined by ","; empty here)
    const NUL = String.fromCharCode(0);
    const contentHash = await sha256HexLower(cleanBody + NUL);
    const canonical = `forkmesh-issue-event-v1\ncomment\n${number}\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const event = {
      type: "comment",
      id: "comment-web-" + ts,
      body: cleanBody,
      attachments: [],
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    const payload = { owner: repo.owner, repo: repo.name, number, event };
    const response = await fetch(`${repoApiBase(repo)}/issues`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return { ...data, event };
  }

  function webIssueEventContent(type, fields = {}) {
    const NUL = String.fromCharCode(0);
    if (type === "edit") {
      return String(fields.body || "") + NUL
        + (Array.isArray(fields.attachments) ? fields.attachments.join(",") : "");
    }
    if (type === "title") return String(fields.title || "");
    if (type === "status") return String(fields.status || "");
    if (type === "labels") {
      return (Array.isArray(fields.labels) ? fields.labels : []).join(",");
    }
    if (type === "milestone") return String(fields.milestone || "");
    if (type === "dates") {
      return `${Number(fields.startDate) || 0}${NUL}${Number(fields.endDate) || 0}`;
    }
    if (type === "priority") return String(Number(fields.priority) || 0);
    if (type === "progress") return String(Number(fields.progress) || 0);
    if (type === "assignees") {
      return (Array.isArray(fields.assignees) ? fields.assignees : []).join(",");
    }
    if (type === "delete") return String(fields.target || "");
    if (type === "vote") return "";
    throw new Error("unsupported_issue_action");
  }

  function webIssueEventId(type, ts) {
    const suffix = new Uint32Array(1);
    crypto.getRandomValues(suffix);
    return `${type}-web-${ts}-${suffix[0].toString(36)}`;
  }

  // Owner-side issue actions use the same append-only event model as Qt. The
  // event is signed by this browser key, while the account session separately
  // proves repository authority to the Worker before it enters the mirror
  // inbox. Votes remain available to any signed-in contributor.
  async function submitWebIssueEvent(repo, number, type, fields = {}) {
    const issueNumber = Number(number);
    if (!Number.isInteger(issueNumber) || issueNumber <= 0) {
      throw new Error("invalid_issue_number");
    }
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const event = {
      type,
      id: webIssueEventId(type, ts),
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
    };
    const fieldNames = {
      edit: ["target", "body", "attachments"],
      title: ["title"],
      status: ["status"],
      labels: ["labels"],
      milestone: ["milestone"],
      dates: ["startDate", "endDate"],
      priority: ["priority"],
      progress: ["progress"],
      assignees: ["assignees"],
      delete: ["target"],
      vote: [],
    }[type];
    if (!fieldNames) throw new Error("unsupported_issue_action");
    fieldNames.forEach((name) => {
      event[name] = fields[name];
    });
    if (type === "edit") {
      event.body = String(event.body || "").replace(/[\r\n]+$/, "");
      event.attachments = Array.isArray(event.attachments)
        ? event.attachments.map((value) => String(value))
        : [];
    }
    if (type === "labels" || type === "assignees") {
      event[type] = (Array.isArray(event[type]) ? event[type] : [])
        .map((value) => String(value).trim())
        .filter(Boolean)
        .slice(0, 20);
    }
    const contentHash = await sha256HexLower(
      webIssueEventContent(type, event),
    );
    const canonical = `forkmesh-issue-event-v1\n${type}\n${issueNumber}\n${pub}\n${ts}\n${contentHash}`;
    event.sig = bytesToB64url(await crypto.subtle.sign(
      { name: "Ed25519" },
      privateKey,
      ISSUE_TEXT_ENCODER.encode(canonical),
    ));
    const payload = {
      owner: repo.owner,
      repo: repo.name,
      number: issueNumber,
      event,
      ownerAccount: state.session?.nodeName || "",
      sessionToken: state.session?.sessionToken || "",
    };
    const response = await fetch(`${repoApiBase(repo)}/issues`, {
      method: "POST",
      headers: {
        "content-type": "application/json",
        accept: "application/json",
      },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return { ...data, event };
  }

  async function setWebIssueSubscription(repo, number, subscribed) {
    const response = await fetch(`${repoApiBase(repo)}/subscribe`, {
      method: "POST",
      headers: {
        "content-type": "application/json",
        accept: "application/json",
      },
      body: JSON.stringify({
        node: state.session?.nodeName || "",
        sessionToken: state.session?.sessionToken || "",
        source: "issue",
        number: Number(number),
        subscribed: subscribed === true,
      }),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  }

  async function loadWebIssueSubscription(repo, number) {
    if (!state.session?.nodeName || !state.session?.sessionToken) return false;
    const response = await fetch(`${repoApiBase(repo)}/subscribe`, {
      method: "POST",
      headers: {
        "content-type": "application/json",
        accept: "application/json",
      },
      body: JSON.stringify({
        action: "status",
        node: state.session.nodeName,
        sessionToken: state.session.sessionToken,
        source: "issue",
        number: Number(number),
      }),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) return false;
    return data.subscribed === true;
  }

  // Mirrors DiscussionStore::contentForSigning's "comment" case (just the
  // reply body) and the desktop's discussion inbox POST (verify_discussion_event
  // in the worker). Replies are signed against the discussion's real number -
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

  // Mirrors PullStore::contentForSigning's per-type field order (comment,
  // review, line-comment, thread-comment, thread-reply, thread-state,
  // suggestion-state) and the desktop's pull inbox POST
  // (verify_pull_comment_event/pull_comment_content in the worker). Covers
  // every conversation-shaped pull event; opening a brand new pull request is
  // a distinct signed shape (submitWebPullOpen, below).
  async function submitWebPullEvent(repo, number, type, fields = {}) {
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const NUL = String.fromCharCode(0);
    const cleanBody = String(fields.body || "").replace(/[\r\n]+$/, "");
    let content;
    if (type === "comment") {
      content = cleanBody;
    } else if (type === "review") {
      content = [fields.state || "", cleanBody].join(NUL);
    } else if (type === "line-comment") {
      content = [fields.path || "", fields.side || "", String(fields.line || 0), cleanBody].join(NUL);
    } else if (type === "thread-comment") {
      content = [
        fields.threadId || "", fields.path || "", fields.side || "",
        String(fields.lineStart || 0), String(fields.lineEnd || 0),
        cleanBody, fields.suggestionPatch || "",
      ].join(NUL);
    } else if (type === "thread-reply") {
      content = [fields.threadId || "", fields.parentId || "", cleanBody].join(NUL);
    } else if (type === "thread-state") {
      content = [fields.threadId || "", fields.state || "", cleanBody].join(NUL);
    } else if (type === "suggestion-state") {
      content = [fields.threadId || "", fields.state || "", fields.appliedCommit || "", cleanBody].join(NUL);
    } else {
      throw new Error("unsupported_pull_event_type");
    }
    const contentHash = await sha256HexLower(content);
    const canonical = `forkmesh-pull-comment-v1\n${type}\n${number}\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const event = {
      type,
      id: `${type}-web-${ts}`,
      body: cleanBody,
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    for (const key of ["state", "path", "side", "line", "threadId", "parentId", "lineStart", "lineEnd", "suggestionPatch", "appliedCommit"]) {
      if (fields[key] !== undefined && fields[key] !== "") event[key] = fields[key];
    }
    const payload = { owner: repo.owner, repo: repo.name, number, event };
    const response = await fetch(`${repoApiBase(repo)}/pulls`, {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return { ...data, event };
  }

  async function submitWebPullComment(repo, number, body) {
    return submitWebPullEvent(repo, number, "comment", { body });
  }

  async function submitWebPullReview(repo, number, reviewState, body) {
    return submitWebPullEvent(repo, number, "review", { state: reviewState, body });
  }

  // Mirrors PullStore::canonicalString for a new pull (verify_pull_event in
  // the Worker). Branch names alone are mutable and may not exist on the
  // owner's node, so resolve them through an attested mirror once and sign the
  // resulting portable binary patch + commit series.
  async function submitWebPullOpen(repo, title, body, base, head) {
    const comparison = await fetchRepoJson(repoLiveUrl(repo, "compare", {
      base,
      head,
      ref: "",
    }));
    const patch = String(comparison.patch || "");
    const commits = String(comparison.commits || "");
    const baseOid = String(comparison.baseOid || "").toLowerCase();
    const headOid = String(comparison.headOid || "").toLowerCase();
    const validOid = (value) => /^[0-9a-f]{40}(?:[0-9a-f]{24})?$/.test(value);
    if (!validOid(baseOid) || !validOid(headOid)) {
      throw new Error("invalid_comparison");
    }
    if (!patch.trim()) {
      throw new Error("no_changes");
    }
    if (ISSUE_TEXT_ENCODER.encode(patch).byteLength
        + ISSUE_TEXT_ENCODER.encode(commits).byteLength > 4 * 1024 * 1024) {
      throw new Error("pull_too_large");
    }
    const { privateKey, pub } = await getWebIssueKey();
    const ts = Math.floor(Date.now() / 1000);
    const cleanBody = String(body || "").replace(/[\r\n]+$/, "");
    const NUL = String.fromCharCode(0);
    const content = [title, base, head, patch, commits].join(NUL);
    const contentHash = await sha256HexLower(content);
    const canonical = `forkmesh-pull-event-v1\n${pub}\n${ts}\n${contentHash}`;
    const sig = bytesToB64url(await crypto.subtle.sign({ name: "Ed25519" }, privateKey, ISSUE_TEXT_ENCODER.encode(canonical)));
    const pull = {
      title,
      description: cleanBody,
      base,
      head,
      patch,
      commits,
      author: pub,
      authorName: state.session?.nodeName || "",
      ts,
      sig,
    };
    const payload = { owner: repo.owner, repo: repo.name, pull };
    const response = await fetch(`${repoApiBase(repo)}/pulls`, {
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

  async function handleIssueCommentSubmit(repo, form) {
    if (!repo || !form) return;
    const number = Number(form.dataset.repoIssueCommentNumber || 0);
    const bodyInput = form.querySelector("[data-repo-issue-comment-body]");
    const submit = form.querySelector("[data-repo-issue-comment-submit]");
    const actionButtons = form.querySelectorAll(
      "[data-repo-issue-comment-action]",
    );
    const hint = form.querySelector("[data-repo-issue-comment-hint]");
    const setHint = (text, tone) => {
      if (hint) hint.className = `text-[11px] ${tone === "bad" ? "text-destructive" : tone === "good" ? "text-primary" : "text-muted-foreground"}`;
      if (hint) hint.textContent = text;
    };
    const body = String(bodyInput?.value || "").trim();
    if (!number) {
      setHint("This issue hasn't finished loading yet.", "bad");
      return;
    }
    if (!body) {
      setHint("Write a comment before sending.", "bad");
      bodyInput?.focus();
      return;
    }
    actionButtons.forEach((button) => { button.disabled = true; });
    setHint("Signing and sending…");
    try {
      const result = await submitWebIssueComment(repo, number, body);
      // Show the comment straight away: it only reaches the mirror once the
      // maintainer's node drains the inbox, so the timeline can't reload it yet.
      const timeline = form.parentElement?.querySelector("[data-repo-issue-timeline]");
      if (timeline) {
        if (timeline.dataset.empty === "true") {
          timeline.innerHTML = "";
          timeline.dataset.empty = "false";
          timeline.className = "border-t border-border";
        }
        timeline.insertAdjacentHTML("beforeend", renderIssueTimelineComment({
          authorName: state.session?.nodeName || "you",
          ts: Math.floor(Date.now() / 1000), body,
        }));
      }
      if (bodyInput) bodyInput.value = "";
      optimisticWebIssueEvent(result.event);
      const action = String(form._issueSubmitAction || "comment");
      form._issueSubmitAction = "comment";
      if (action === "close") {
        const statusResult = await submitWebIssueEvent(
          repo,
          number,
          "status",
          { status: "closed" },
        );
        optimisticWebIssueEvent(statusResult.event);
      }
      actionButtons.forEach((button) => { button.disabled = false; });
      setHint(
        action === "close"
          ? "Comment sent and issue closure accepted."
          : "Comment accepted for direct delivery to an eligible mirror.",
        "good",
      );
      renderCurrentWebIssueDetail();
    } catch (error) {
      actionButtons.forEach((button) => { button.disabled = false; });
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "Comment delivery is temporarily full. Try again later."
          : code === "author_quota" ? "You've reached the submission limit for this repository."
          : code === "issue_too_large" ? "The comment is too large - please shorten it."
          : code === "bad_signature" ? "Could not verify the comment's signature."
          : "Could not send the comment. Please try again.",
        "bad");
    }
  }

  function optimisticWebIssueEvent(event) {
    const detail = state.repoRecordDetail;
    if (!event || detail?.kind !== "issues") return;
    const parsed = detail.parsed || {};
    const values = parsed.values || (parsed.values = {});
    const events = Array.isArray(parsed.issueEvents)
      ? parsed.issueEvents
      : (parsed.issueEvents = []);
    if (!events.some((existing) => existing?.id === event.id)) events.push(event);
    if (event.type === "title") values.title = event.title;
    if (event.type === "status") values.status = event.status;
    if (event.type === "labels") values.labels = `[${event.labels.join(", ")}]`;
    if (event.type === "milestone") values.milestone = event.milestone;
    if (event.type === "priority") values.priority = Number(event.priority) || 0;
    if (event.type === "progress") values.progress = Number(event.progress) || 0;
    if (event.type === "assignees") {
      values.assignees = `[${event.assignees.join(", ")}]`;
    }
    if (event.type === "dates") {
      values.startDate = Number(event.startDate) || 0;
      values.endDate = Number(event.endDate) || 0;
    }
    if (event.type === "vote") values.votes = (Number(values.votes) || 0) + 1;
    if (event.type === "edit" && event.target === parsed.issueOpenEventId) {
      parsed.body = event.body;
    }
    if (event.type === "delete" && event.target === "self") {
      parsed.issueDeleted = true;
    }
  }

  function renderCurrentWebIssueDetail() {
    const detail = state.repoRecordDetail;
    if (detail?.kind !== "issues" || !detail.repo) return;
    const container = $("[data-repo-issues]");
    if (!container) return;
    container.innerHTML = renderRepoRecordDetail(
      detail.repo,
      "issues",
      detail.number,
      detail.parsed,
    );
    window.lucide?.createIcons();
  }

  function webIssueActionError(error) {
    const code = String(error?.message || "");
    return ({
      not_authorized: "Only the repository owner or an organization admin can do that.",
      bad_signature: "The signed issue action could not be verified.",
      inbox_full: "Issue delivery is temporarily full. Try again later.",
      author_quota: "Too many issue changes are waiting to sync. Try again after the owner node drains them.",
      issue_too_large: "That issue update is too large.",
    })[code] || "The issue could not be updated. Please try again.";
  }

  function splitWebIssueList(value) {
    return [...new Set(String(value || "").split(",")
      .map((item) => item.trim())
      .filter(Boolean))].slice(0, 20);
  }

  async function handleWebIssueAction(action, target = "") {
    const detail = state.repoRecordDetail;
    const repo = detail?.repo;
    const number = Number(detail?.number || 0);
    if (!repo || detail?.kind !== "issues" || !number) return;
    if (!state.session?.nodeName) {
      location.href = "/login?next="
        + encodeURIComponent(`${location.pathname}${location.search}`);
      return;
    }
    let type = "";
    let fields = {};
    if (action === "subscribe" || action === "unsubscribe") {
      try {
        await setWebIssueSubscription(repo, number, action === "subscribe");
        detail.parsed.issueSubscribed = action === "subscribe";
        renderCurrentWebIssueDetail();
      } catch (error) {
        window.alert(webIssueActionError(error));
      }
      return;
    }
    if (action === "vote") type = "vote";
    if (action === "close" || action === "open") {
      type = "status";
      fields = { status: action };
    }
    if (action === "delete-issue") {
      const title = String(detail.parsed?.values?.title || `issue #${number}`);
      if (!window.confirm(
        `Delete issue #${number} “${title}”?\n\nThis hides the issue everywhere after the signed deletion syncs. Its Git history remains recoverable.`,
      )) return;
      type = "delete";
      fields = { target: "self" };
    }
    if (action === "delete-comment") {
      if (!window.confirm("Delete this comment? Its Git history remains recoverable.")) return;
      type = "delete";
      fields = { target };
    }
    if (action === "edit-comment") {
      const events = detail.parsed?.issueEvents || [];
      const original = events.find((event) => event?.id === target);
      if (!original) return;
      const latest = events.filter(
        (event) => event?.type === "edit" && event.target === target,
      ).at(-1);
      const nextBody = window.prompt("Edit comment", latest?.body ?? original.body ?? "");
      if (nextBody === null) return;
      type = "edit";
      fields = { target, body: nextBody, attachments: latest?.attachments || original.attachments || [] };
    }
    if (!type) return;
    try {
      const result = await submitWebIssueEvent(repo, number, type, fields);
      optimisticWebIssueEvent(result.event);
      if (action === "delete-issue") {
        state.issuesView.items = (state.issuesView.items || []).filter(
          (issue) => Number(issue.number) !== number,
        );
        state.repoRecordDetail = null;
        navigateHistory(`${repoPathUrl(repo)}/issues`);
        renderRepoIssues();
        return;
      }
      renderCurrentWebIssueDetail();
    } catch (error) {
      window.alert(webIssueActionError(error));
    }
  }

  async function handleWebIssueTitleSubmit(form) {
    const detail = state.repoRecordDetail;
    const title = String(
      form.querySelector("[data-repo-issue-title-input]")?.value || "",
    ).trim();
    if (!title) return;
    try {
      const result = await submitWebIssueEvent(
        detail.repo,
        detail.number,
        "title",
        { title },
      );
      optimisticWebIssueEvent(result.event);
      renderCurrentWebIssueDetail();
    } catch (error) {
      window.alert(webIssueActionError(error));
    }
  }

  async function handleWebIssueDescriptionSubmit(form) {
    const detail = state.repoRecordDetail;
    const body = String(
      form.querySelector("[data-repo-issue-description-input]")?.value || "",
    );
    try {
      const result = await submitWebIssueEvent(
        detail.repo,
        detail.number,
        "edit",
        {
          target: detail.parsed.issueOpenEventId,
          body,
          attachments: detail.parsed.issueOpenAttachments || [],
        },
      );
      optimisticWebIssueEvent(result.event);
      renderCurrentWebIssueDetail();
    } catch (error) {
      window.alert(webIssueActionError(error));
    }
  }

  async function handleWebIssueMetadataSubmit(form) {
    const detail = state.repoRecordDetail;
    const values = detail?.parsed?.values || {};
    const hint = form.querySelector("[data-repo-issue-metadata-hint]");
    const submit = form.querySelector("[data-repo-issue-metadata-save]");
    const labels = splitWebIssueList(form.querySelector("[data-repo-issue-labels]")?.value);
    const assignees = splitWebIssueList(form.querySelector("[data-repo-issue-assignees]")?.value);
    const milestone = String(form.querySelector("[data-repo-issue-milestone-edit]")?.value || "").trim();
    const priority = Math.min(99, Math.max(0, Number(form.querySelector("[data-repo-issue-priority]")?.value) || 0));
    const progress = Math.min(100, Math.max(0, Number(form.querySelector("[data-repo-issue-progress]")?.value) || 0));
    const dateValue = (selector) => {
      const value = String(form.querySelector(selector)?.value || "");
      return value ? Date.parse(`${value}T00:00:00Z`) || 0 : 0;
    };
    const startDate = dateValue("[data-repo-issue-start-date]");
    const endDate = dateValue("[data-repo-issue-end-date]");
    const actions = [];
    if (labels.join(",") !== parseFrontMatterList(values.labels).join(",")) actions.push(["labels", { labels }]);
    if (assignees.join(",") !== parseFrontMatterList(values.assignees).join(",")) actions.push(["assignees", { assignees }]);
    if (milestone !== String(values.milestone || "")) actions.push(["milestone", { milestone }]);
    if (priority !== (Number(values.priority) || 0)) actions.push(["priority", { priority }]);
    if (progress !== (Number(values.progress) || 0)) actions.push(["progress", { progress }]);
    if (startDate !== (Number(values.startDate) || 0) || endDate !== (Number(values.endDate) || 0)) actions.push(["dates", { startDate, endDate }]);
    if (!actions.length) {
      if (hint) hint.textContent = "No changes";
      return;
    }
    if (submit) submit.disabled = true;
    if (hint) hint.textContent = "Signing…";
    try {
      for (const [type, fields] of actions) {
        const result = await submitWebIssueEvent(
          detail.repo,
          detail.number,
          type,
          fields,
        );
        optimisticWebIssueEvent(result.event);
      }
      if (hint) hint.textContent = "Saved";
      renderCurrentWebIssueDetail();
    } catch (error) {
      if (submit) submit.disabled = false;
      if (hint) {
        hint.className = "text-[10px] text-destructive";
        hint.textContent = webIssueActionError(error);
      }
    }
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
      setHint("Reply accepted for direct delivery to an eligible mirror.", "good");
    } catch (error) {
      if (submit) submit.disabled = false;
      const code = String(error?.message || "");
      setHint(
        code === "inbox_full" ? "Reply delivery is temporarily full. Try again later."
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
      sessionToken: body.sessionToken ?? base.sessionToken ?? "",
      avatarPng: body.avatarPng || "",
      avatarUpdatedAt: Number(body.avatarUpdatedAt) || 0,
      profileBio: body.profileBio ?? base.profileBio ?? "",
      profileAbout: body.profileAbout ?? body.profileReadme ?? base.profileAbout ?? base.profileReadme ?? "",
      profileReadme: body.profileReadme ?? body.profileAbout ?? base.profileReadme ?? base.profileAbout ?? "",
      profileLocation: body.profileLocation ?? base.profileLocation ?? "",
      profileTimezone: body.profileTimezone ?? base.profileTimezone ?? "",
      profileFollowers: Number(body.followers ?? body.profileFollowers ?? base.profileFollowers ?? 0) || 0,
      profileFollowing: Number(body.following ?? body.profileFollowing ?? base.profileFollowing ?? 0) || 0,
      profileMirrorCount: Number(body.mirrorCount ?? body.profileMirrorCount ?? base.profileMirrorCount ?? 0) || 0,
      profilePrivate: Object.prototype.hasOwnProperty.call(body, "profilePrivate")
        ? Boolean(body.profilePrivate)
        : Boolean(base.profilePrivate),
      followersPublic: Object.prototype.hasOwnProperty.call(body, "followersPublic")
        ? Boolean(body.followersPublic)
        : Boolean(base.followersPublic),
      mastodon: body.mastodon ?? base.mastodon ?? "",
      mastodonUrl: body.mastodonUrl ?? base.mastodonUrl ?? "",
      profileLinks: Array.isArray(body.profileLinks)
        ? body.profileLinks
        : (base.profileLinks || []),
      emailNotifications: Object.prototype.hasOwnProperty.call(body, "emailNotifications")
        ? Boolean(body.emailNotifications)
        : Boolean(base.emailNotifications ?? true),
      notificationPreferences: body.notificationPreferences && typeof body.notificationPreferences === "object"
        ? body.notificationPreferences
        : (base.notificationPreferences || {}),
      kind: body.kind || base.kind || "",
      owner: body.owner ?? base.owner ?? "",
      online: Object.prototype.hasOwnProperty.call(body, "online")
        ? Boolean(body.online)
        : Boolean(base.online),
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
    return hydrateCanonicalProfile(session);
  }

  async function hydrateCanonicalProfile(session) {
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
