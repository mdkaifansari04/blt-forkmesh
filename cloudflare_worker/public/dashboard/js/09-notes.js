  // Collaborative Markdown notes. Saves are version-checked; the socket only
  // announces persisted changes and lightweight presence/cursors.
  async function noteRequest(path, method = "GET", body = null) {
    const headers = { accept: "application/json" };
    const token = state.session?.sessionToken || "";
    if (token) headers.authorization = `Bearer ${token}`;
    if (body !== null) headers["content-type"] = "application/json";
    const response = await fetch(path, {
      method, headers, cache: "no-store",
      body: body === null ? undefined : JSON.stringify(body),
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      const error = new Error(payload.error || `HTTP ${response.status}`);
      error.status = response.status; error.payload = payload; throw error;
    }
    return payload;
  }

  function notesError(message = "") {
    const box = $("[data-notes-error]");
    if (!box) return;
    box.textContent = message; box.classList.toggle("hidden", !message);
  }

  // "Public" / "Shared" / "Private" — the one word that answers "can anyone
  // else see this?", which is the question the sidebar exists to answer.
  function noteStatusLabel(note) {
    if (note.visibility === "public") return "Public";
    return (note.shares || []).length ? "Shared" : "Private";
  }

  // "12 views" / "12 views from 4 readers", or "" for a note nobody has read
  // — an unpublished draft should not be labelled "0 views".
  function noteViewsLabel(note) {
    const views = Number(note.views || 0), readers = Number(note.readers || 0);
    if (views <= 0) return "";
    const counted = views === 1 ? "1 view" : `${views} views`;
    return readers > 1 ? `${counted} from ${readers} readers` : counted;
  }

  function noteSharedWithLabel(note) {
    const names = (note.shares || [])
      .filter((share) => share.name)
      .map((share) => `${share.name} (${share.role || "viewer"})`);
    return names.length ? `Shared with ${names.join(", ")}` : "Not shared with anyone";
  }

  function renderNoteList() {
    const list = $("[data-note-list]"); if (!list) return;
    const query = String($("[data-note-search]")?.value || "").toLowerCase();
    const notes = state.notesView.items.filter((note) =>
      String(note.title || "").toLowerCase().includes(query));
    list.innerHTML = notes.length ? notes.map((note) => {
      const status = [noteStatusLabel(note), noteViewsLabel(note),
        `v${note.version}`, note.role, relativeTimeLabel(note.updatedAt)]
        .filter(Boolean);
      return `
      <button data-note-id="${escapeHtml(note.id)}" class="block w-full border-b border-border px-3 py-3 text-left hover:bg-secondary ${state.notesView.selected?.id === note.id ? "bg-secondary" : ""}">
        <span class="block truncate text-sm font-medium text-foreground">${escapeHtml(note.title)}</span>
        <span class="mt-1 block text-[11px] ${note.visibility === "public" ? "text-primary" : "text-muted-foreground"}">${escapeHtml(status.join(" · "))}</span>
        <span class="mt-0.5 block truncate text-[11px] text-muted-foreground">${escapeHtml(noteSharedWithLabel(note))}</span>
      </button>`;
    }).join("") : '<p class="p-4 text-sm text-muted-foreground">No notes yet.</p>';
  }

  function renderNoteEditor(note) {
    $("[data-note-empty]")?.classList.toggle("hidden", Boolean(note));
    $("[data-note-editor]")?.classList.toggle("hidden", !note);
    if (!note) return;
    $("[data-note-title]").value = note.title || "";
    $("[data-note-markdown]").value = note.markdown || "";
    $("[data-note-public]").checked = note.visibility === "public";
    $("[data-note-public]").disabled = note.role !== "owner";
    $("[data-note-delete]").classList.toggle("hidden", note.role !== "owner");
    $("[data-note-preview]").innerHTML = renderMarkdown(note.markdown || "");
    $("[data-note-shares]").innerHTML = (note.shares || []).map((share) =>
      `<div class="flex justify-between gap-2"><span>${escapeHtml(share.type)}:${escapeHtml(share.name)} · ${escapeHtml(share.role)}</span>${note.role === "owner" ? `<button data-note-unshare="${escapeHtml(share.type)}:${escapeHtml(share.name)}" class="hover:text-destructive">Remove</button>` : ""}</div>`).join("") || "Private to you";
    $("[data-note-links]").innerHTML = (note.links || []).map((link) => {
      const section = link.kind === "pull" ? "pulls" : `${link.kind}s`;
      return `<div class="flex justify-between gap-2"><a class="truncate text-primary underline" href="/${encodeURIComponent(link.owner)}/${encodeURIComponent(link.repo)}?tab=${section}#${link.number}">${escapeHtml(link.owner)}/${escapeHtml(link.repo)} · ${escapeHtml(link.kind)} #${link.number}</a><button data-note-unlink='${escapeHtml(JSON.stringify(link))}' class="hover:text-destructive">Remove</button></div>`;
    }).join("") || "No linked conversations";
    const publicLink = $("[data-note-public-link]");
    if (publicLink) {
      publicLink.href = `/notes/${note.id}`;
      publicLink.classList.toggle("hidden", note.visibility !== "public");
    }
    state.notesView.dirty = false;
    renderNoteList();
    connectNoteSocket(note);
    void loadNoteVersions();
  }

  async function loadNotes(selectId = "") {
    if (!state.session?.sessionToken) {
      state.notesView.loading = false; notesError("Sign in to use cloud notes."); renderNoteList(); return;
    }
    try {
      const payload = await noteRequest("/api/notes");
      state.notesView.items = payload.notes || []; notesError(""); renderNoteList();
      const id = selectId || state.notesView.selected?.id || state.notesView.items[0]?.id;
      if (id) await selectNote(id);
    } catch (error) { notesError(`Notes are unavailable: ${error.message}`); }
  }

  async function selectNote(id) {
    try {
      const payload = await noteRequest(`/api/notes/${id}`);
      state.notesView.selected = payload.note;
      // Adding or removing a share re-selects the note; folding the reply
      // back into the listing is what keeps the sidebar row's status and
      // "shared with" line honest without refetching the whole list.
      state.notesView.items = state.notesView.items.map((item) =>
        item.id === payload.note.id ? { ...item, ...payload.note } : item);
      renderNoteEditor(payload.note);
    } catch (error) { notesError(error.message); }
  }

  async function createNote() {
    try {
      const payload = await noteRequest("/api/notes", "POST", { title: "Untitled note", markdown: "" });
      state.notesView.items.unshift(payload.note); state.notesView.selected = payload.note;
      renderNoteEditor(payload.note); $("[data-note-title]")?.focus();
    } catch (error) { notesError(error.message); }
  }

  async function saveNote() {
    const note = state.notesView.selected; if (!note) return;
    try {
      const payload = await noteRequest(`/api/notes/${note.id}`, "PATCH", {
        baseVersion: note.version, title: $("[data-note-title]").value,
        markdown: $("[data-note-markdown]").value,
        visibility: $("[data-note-public]").checked ? "public" : "private",
      });
      state.notesView.selected = payload.note;
      state.notesView.items = state.notesView.items.map((item) => item.id === note.id ? payload.note : item);
      renderNoteEditor(payload.note); notesError("");
    } catch (error) {
      if (error.status === 409 && error.payload?.note) {
        notesError("This note changed elsewhere. The latest version was loaded; reapply your edit and save again.");
        state.notesView.selected = error.payload.note; renderNoteEditor(error.payload.note);
      } else notesError(error.message);
    }
  }

  async function loadNoteVersions() {
    const note = state.notesView.selected; if (!note) return;
    try {
      const payload = await noteRequest(`/api/notes/${note.id}/versions`);
      $("[data-note-versions]").innerHTML = (payload.versions || []).map((version) =>
        `<div class="flex justify-between"><span>Version ${version.version} · ${escapeHtml(relativeTimeLabel(version.createdAt))}</span>${version.version !== note.version && ["owner", "editor"].includes(note.role) ? `<button data-note-restore="${version.version}" class="text-primary">Restore</button>` : "<span>Current</span>"}</div>`).join("");
    } catch (error) { notesError(error.message); }
  }

  function connectNoteSocket(note) {
    state.notesView.socket?.close(); state.notesView.socket = null;
    const token = state.session?.sessionToken; if (!token) return;
    const url = new URL(note.webSocketUrl, location.href); url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    url.searchParams.set("sessionToken", token);
    const socket = new WebSocket(url); state.notesView.socket = socket;
    const badge = $("[data-note-live]");
    state.notesView.clientId = state.notesView.clientId || crypto.randomUUID();
    socket.onopen = () => { if (badge) badge.textContent = "live"; socket.send(JSON.stringify({ type: "presence", clientId: state.notesView.clientId, name: state.session?.nodeName || "collaborator" })); };
    socket.onclose = () => { if (badge) badge.textContent = "offline"; };
    socket.onmessage = (event) => {
      let frame; try { frame = JSON.parse(event.data); } catch (_) { return; }
      if (frame.type === "note-updated" && frame.version > (state.notesView.selected?.version || 0)) {
        if (state.notesView.dirty) notesError("A collaborator saved a newer version. Save to review the conflict, or reload the note.");
        else void selectNote(note.id);
      }
      if (frame.type === "draft" && frame.clientId !== state.notesView.clientId &&
          frame.baseVersion === state.notesView.selected?.version) {
        $("[data-note-title]").value = String(frame.title || "Untitled note");
        $("[data-note-markdown]").value = String(frame.markdown || "");
        $("[data-note-preview]").innerHTML = renderMarkdown(frame.markdown || "");
        state.notesView.dirty = true;
        if (badge) badge.textContent = `live · ${String(frame.name || "collaborator").slice(0, 32)}`;
      }
      if (frame.type === "note-deleted") void loadNotes();
      if (frame.type === "access-changed") socket.close();
    };
  }

  function sendNoteDraft() {
    clearTimeout(state.notesView.draftTimer);
    state.notesView.draftTimer = setTimeout(() => {
      const socket = state.notesView.socket, note = state.notesView.selected;
      if (!note || socket?.readyState !== WebSocket.OPEN) return;
      socket.send(JSON.stringify({
        type: "draft", clientId: state.notesView.clientId,
        name: state.session?.nodeName || "collaborator",
        baseVersion: note.version, title: $("[data-note-title]").value,
        markdown: $("[data-note-markdown]").value,
      }));
    }, 180);
  }

  function initNotesPage() {
    $("[data-note-new]")?.addEventListener("click", () => void createNote());
    $("[data-note-search]")?.addEventListener("input", renderNoteList);
    $("[data-note-list]")?.addEventListener("click", (event) => { const row = event.target.closest("[data-note-id]"); if (row) void selectNote(row.dataset.noteId); });
    $("[data-note-markdown]")?.addEventListener("input", () => { state.notesView.dirty = true; $("[data-note-preview]").innerHTML = renderMarkdown($("[data-note-markdown]").value); sendNoteDraft(); });
    $("[data-note-title]")?.addEventListener("input", () => { state.notesView.dirty = true; sendNoteDraft(); });
    $("[data-note-save]")?.addEventListener("click", () => void saveNote());
    $("[data-note-delete]")?.addEventListener("click", async () => { const note = state.notesView.selected; if (!note || !confirm(`Delete “${note.title}”?`)) return; await noteRequest(`/api/notes/${note.id}`, "DELETE", {}); state.notesView.selected = null; await loadNotes(); });
    $("[data-note-share-add]")?.addEventListener("click", async () => { const note = state.notesView.selected; if (!note) return; await noteRequest(`/api/notes/${note.id}/shares`, "POST", { type: $("[data-note-share-type]").value, name: $("[data-note-share-name]").value, role: $("[data-note-share-role]").value }); await selectNote(note.id); });
    $("[data-note-shares]")?.addEventListener("click", async (event) => { const button = event.target.closest("[data-note-unshare]"); if (!button) return; const [type, name] = button.dataset.noteUnshare.split(":"); await noteRequest(`/api/notes/${state.notesView.selected.id}/shares`, "DELETE", { type, name }); await selectNote(state.notesView.selected.id); });
    $("[data-note-link-add]")?.addEventListener("click", async () => { const note = state.notesView.selected; if (!note) return; await noteRequest(`/api/notes/${note.id}/links`, "POST", { owner: $("[data-note-link-owner]").value, repo: $("[data-note-link-repo]").value, kind: $("[data-note-link-kind]").value, number: Number($("[data-note-link-number]").value) }); await selectNote(note.id); });
    $("[data-note-links]")?.addEventListener("click", async (event) => { const button = event.target.closest("[data-note-unlink]"); if (!button) return; await noteRequest(`/api/notes/${state.notesView.selected.id}/links`, "DELETE", JSON.parse(button.dataset.noteUnlink)); await selectNote(state.notesView.selected.id); });
    $("[data-note-versions-refresh]")?.addEventListener("click", () => void loadNoteVersions());
    $("[data-note-versions]")?.addEventListener("click", async (event) => { const button = event.target.closest("[data-note-restore]"); if (!button) return; const note = state.notesView.selected; const payload = await noteRequest(`/api/notes/${note.id}/versions`, "POST", { version: Number(button.dataset.noteRestore), baseVersion: note.version }); state.notesView.selected = payload.note; renderNoteEditor(payload.note); });
    void loadNotes();
  }
