const list = document.querySelector("#catalog-list");
const count = document.querySelector("#catalog-count");
const tabCount = document.querySelector("#catalog-tab-count");

function text(value, fallback = "") {
  return typeof value === "string" && value.trim() ? value.trim() : fallback;
}

// Mirror the desktop client's repoSegment() so deep-link anchors match the
// URLs it generates (lowercase, [a-z0-9_-], collapsed dashes, max 48 chars).
function slug(value, fallback) {
  let out = "";
  let lastDash = false;
  for (const ch of String(value || "").toLowerCase()) {
    if (/[a-z0-9_-]/.test(ch)) {
      out += ch;
      lastDash = false;
    } else if (!lastDash) {
      out += "-";
      lastDash = true;
    }
  }
  out = out.replace(/^-+/, "").replace(/-+$/, "");
  return (out || fallback).slice(0, 48);
}

function repoAnchor(repo) {
  return `repo-${slug(repo.owner, "owner")}-${slug(repo.name, "repository")}`;
}

// After the async catalog renders, jump to and highlight a linked repository.
function highlightHash() {
  if (!location.hash) return;
  const target = document.getElementById(location.hash.slice(1));
  if (!target) return;
  target.scrollIntoView({ behavior: "smooth", block: "center" });
  target.classList.add("catalog-item-highlight");
}

function formatDate(value) {
  if (!value) return "not synced yet";
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0) return "not synced yet";
  return new Intl.DateTimeFormat(undefined, {
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
  }).format(new Date(number));
}

function render(repositories) {
  const repos = Array.isArray(repositories) ? repositories : [];
  count.textContent = `${repos.length} mirrored`;
  tabCount.textContent = String(repos.length);

  if (!repos.length) {
    list.innerHTML = `<div class="catalog-empty">No repositories have been published yet.</div>`;
    return;
  }

  list.replaceChildren(
    ...repos.map((repo) => {
      const item = document.createElement("article");
      item.className = "catalog-item";
      item.id = repoAnchor(repo);

      const header = document.createElement("div");
      header.className = "catalog-item-header";

      const title = document.createElement("h3");
      title.textContent = `${text(repo.owner, "owner")}/${text(repo.name, "repository")}`;
      header.append(title);

      const status = document.createElement("span");
      status.className = "catalog-status";
      status.textContent = text(repo.source, "local-node");
      header.append(status);

      const description = document.createElement("p");
      description.textContent = text(repo.description, "No description published.");

      const meta = document.createElement("div");
      meta.className = "catalog-meta";
      const channel = document.createElement("span");
      channel.textContent = text(repo.channel, "#general");
      const synced = document.createElement("span");
      synced.textContent = `Last sync ${formatDate(repo.lastSync)}`;
      const maintainer = document.createElement("span");
      maintainer.textContent = `Maintainer ${text(repo.maintainer).slice(0, 12)}...`;
      meta.append(channel, synced, maintainer);

      item.append(header, description, meta);

      const actions = document.createElement("div");
      actions.className = "catalog-actions";
      const browse = document.createElement("button");
      browse.type = "button";
      browse.className = "catalog-browse";
      browse.textContent = "Browse files";
      browse.addEventListener("click", () => {
        loadFiles(repo.owner, repo.name).then(() => {
          document.querySelector("#code").scrollIntoView({
            behavior: "smooth",
            block: "start",
          });
        });
      });
      actions.append(browse);

      if (text(repo.cloneUrl)) {
        const link = document.createElement("a");
        link.className = "catalog-clone";
        link.href = repo.cloneUrl;
        link.textContent = repo.cloneUrl;
        actions.append(link);
      }
      item.append(actions);

      return item;
    })
  );

  highlightHash();

  // Show the most recently updated repository's files by default.
  if (repos.length && !fileState) {
    loadFiles(repos[0].owner, repos[0].name);
  }
}

// ---- Live file browser (tunnels to a connected client; caches in the browser)
// The website pulls the tree and file contents live from whichever desktop
// client is hosting the repo. Each response is cached in localStorage so the
// repo stays browsable even after the host disconnects.

const fileListEl = document.querySelector("#file-list");
const breadcrumbEl = document.querySelector("#file-breadcrumb");
const codeTitle = document.querySelector("#code-title");
const fileMetaEl = document.querySelector("#file-meta");
const viewerEl = document.querySelector("#file-viewer");
const viewerPathEl = document.querySelector("#file-viewer-path");
const viewerBodyEl = document.querySelector("#file-viewer-body");
const viewerBackEl = document.querySelector("#file-viewer-back");

let fileState = null; // { owner, name, dir }
let navToken = 0; // guards against out-of-order async navigations

function formatSize(bytes) {
  const n = Number(bytes) || 0;
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}

function cacheKey(kind, owner, name, path) {
  return `fm:${kind}:${owner}/${name}:${path}`;
}

function readCache(key) {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : null;
  } catch (error) {
    return null;
  }
}

function writeCache(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch (error) {
    /* quota exceeded or storage disabled — caching is best-effort */
  }
}

// Pull a path live from the host; fall back to the browser cache when no host
// is connected. Returns { data, source: "live" | "cached" | null }.
async function pullPath(kind, path) {
  const { owner, name } = fileState;
  const key = cacheKey(kind, owner, name, path);
  let liveError = null;
  try {
    const response = await fetch(
      `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(name)}/${kind === "tree" ? "tree" : "blob"}?path=${encodeURIComponent(path)}`,
      { headers: { accept: "application/json" } }
    );
    const data = await response.json();
    if (response.ok && data.ok) {
      writeCache(key, data);
      return { data, source: "live" };
    }
    liveError = (data && data.error) || `HTTP ${response.status}`;
  } catch (error) {
    liveError = "unreachable";
  }
  const cached = readCache(key);
  if (cached) return { data: cached, source: "cached" };
  return { data: null, source: null, error: liveError };
}

function unavailableMessage(error) {
  const who = `${fileState.owner}/${fileState.name}`;
  if (error === "no_host") {
    return `No client hosting ${who} is online right now, and nothing is cached yet. Open the desktop client that mirrors this repo to browse it live.`;
  }
  if (error === "empty_repo") {
    return `The host for ${who} has no commits to show yet.`;
  }
  if (error === "timeout") {
    return `The host for ${who} did not respond in time. Try again.`;
  }
  return `Could not load this from the host${error ? ` (${error})` : ""}.`;
}

function setSource(source) {
  if (!fileMetaEl) return;
  fileMetaEl.className = "file-meta";
  if (source === "live") {
    fileMetaEl.textContent = "● live from host";
    fileMetaEl.classList.add("source-live");
  } else if (source === "cached") {
    fileMetaEl.textContent = "● cached · host offline";
    fileMetaEl.classList.add("source-cached");
  } else {
    fileMetaEl.textContent = "";
  }
}

function showList() {
  if (viewerEl) viewerEl.hidden = true;
  if (fileListEl) fileListEl.hidden = false;
}

function showViewer() {
  if (fileListEl) fileListEl.hidden = true;
  if (viewerEl) viewerEl.hidden = false;
}

function loadFiles(owner, name) {
  if (!fileListEl) return Promise.resolve();
  fileState = { owner, name, dir: "" };
  if (codeTitle) codeTitle.textContent = `${owner}/${name}`;
  showList();
  return openDir("");
}

function fileRow(kind, label, note, onClick) {
  const tag = onClick ? "button" : "div";
  const row = document.createElement(tag);
  row.className = "file-row";
  if (onClick) {
    row.type = "button";
    row.addEventListener("click", onClick);
  }
  const icon = document.createElement("span");
  icon.className = `file-icon ${kind}`;
  icon.setAttribute("aria-hidden", "true");
  const nameEl = document.createElement("span");
  nameEl.className = "file-name";
  nameEl.textContent = label;
  const noteEl = document.createElement("span");
  noteEl.className = "file-note";
  noteEl.textContent = note;
  row.append(icon, nameEl, noteEl);
  return row;
}

function renderBreadcrumb() {
  if (!breadcrumbEl || !fileState) return;
  breadcrumbEl.replaceChildren();
  const root = document.createElement("button");
  root.type = "button";
  root.className = "crumb";
  root.textContent = `${fileState.owner}/${fileState.name}`;
  root.addEventListener("click", () => openDir(""));
  breadcrumbEl.append(root);

  let acc = "";
  for (const segment of fileState.dir ? fileState.dir.split("/") : []) {
    acc = acc ? `${acc}/${segment}` : segment;
    const sep = document.createElement("span");
    sep.className = "crumb-sep";
    sep.textContent = "/";
    const crumb = document.createElement("button");
    crumb.type = "button";
    crumb.className = "crumb";
    crumb.textContent = segment;
    const target = acc;
    crumb.addEventListener("click", () => openDir(target));
    breadcrumbEl.append(sep, crumb);
  }
}

async function openDir(path) {
  if (!fileState) return;
  const token = ++navToken;
  fileState.dir = path;
  showList();
  renderBreadcrumb();
  fileListEl.innerHTML = `<div class="file-empty">Loading…</div>`;
  const { data, source, error } = await pullPath("tree", path);
  if (token !== navToken) return; // a newer navigation superseded this one
  setSource(source);
  if (!data || !Array.isArray(data.entries)) {
    fileListEl.replaceChildren();
    const note = document.createElement("div");
    note.className = "file-empty";
    note.textContent = unavailableMessage(error);
    fileListEl.append(note);
    return;
  }

  const entries = data.entries.slice();
  entries.sort((a, b) => {
    if (a.type !== b.type) return a.type === "tree" ? -1 : 1;
    return String(a.name).localeCompare(String(b.name));
  });

  const rows = [];
  if (path) {
    rows.push(
      fileRow("up", "..", "", () => {
        const cut = path.lastIndexOf("/");
        openDir(cut === -1 ? "" : path.slice(0, cut));
      })
    );
  }
  for (const entry of entries) {
    const childPath = path ? `${path}/${entry.name}` : entry.name;
    if (entry.type === "tree") {
      rows.push(fileRow("folder", entry.name, "", () => openDir(childPath)));
    } else {
      rows.push(
        fileRow("file", entry.name, formatSize(entry.size), () =>
          openBlob(childPath)
        )
      );
    }
  }
  fileListEl.replaceChildren(...rows);
}

async function openBlob(path) {
  if (!fileState) return;
  const token = ++navToken;
  if (viewerPathEl) viewerPathEl.textContent = path;
  if (viewerBodyEl) viewerBodyEl.textContent = "Loading…";
  showViewer();
  const { data, source, error } = await pullPath("blob", path);
  if (token !== navToken) return;
  setSource(source);
  if (!data) {
    viewerBodyEl.textContent = unavailableMessage(error);
    return;
  }
  if (data.encoding === "base64") {
    viewerBodyEl.textContent = `[binary file — ${formatSize(data.size)}, not shown]`;
    return;
  }
  viewerBodyEl.textContent = data.content || "";
  if (data.truncated) {
    viewerBodyEl.textContent += "\n\n… file truncated by the host.";
  }
}

if (viewerBackEl) {
  viewerBackEl.addEventListener("click", () => {
    navToken += 1; // cancel any in-flight blob load
    setSource(null);
    openDir(fileState ? fileState.dir : "");
  });
}

async function loadCatalog() {
  try {
    const response = await fetch("/api/repositories", {
      headers: { accept: "application/json" },
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    render(data.repositories);
  } catch (error) {
    count.textContent = "Unavailable";
    list.innerHTML = `<div class="catalog-empty">Repository catalog is temporarily unavailable.</div>`;
  }
}

const clientsCount = document.querySelector("#clients-count");
const clientsDot = document.querySelector("#clients-dot");
// The mainnode general room the desktop client joins by default. Connecting as
// a read-only observer, the Durable Object pushes the live client count over a
// WebSocket on every join/leave — no polling.
const CLIENTS_PATH = "/api/repo/mainnode/forkmesh/rooms/general/clients";

function renderClients(online) {
  if (!clientsCount) return;
  clientsCount.textContent =
    online === 1 ? "1 client online" : `${online} clients online`;
  if (clientsDot) clientsDot.classList.toggle("online", online > 0);
}

let clientsRetry = 0;

function watchClients() {
  if (!clientsCount) return;
  const scheme = location.protocol === "https:" ? "wss:" : "ws:";
  let socket;
  try {
    socket = new WebSocket(`${scheme}//${location.host}${CLIENTS_PATH}`);
  } catch (error) {
    scheduleReconnect();
    return;
  }

  socket.addEventListener("message", (event) => {
    try {
      const data = JSON.parse(event.data);
      renderClients(Number(data.clients) || 0);
      clientsRetry = 0;
    } catch (error) {
      /* ignore malformed frames */
    }
  });
  socket.addEventListener("close", scheduleReconnect);
  socket.addEventListener("error", () => socket.close());
}

function scheduleReconnect() {
  if (!clientsCount) return;
  clientsCount.textContent = "Reconnecting…";
  if (clientsDot) clientsDot.classList.remove("online");
  // Back off the reconnect so a downed relay doesn't hammer the network.
  const delay = Math.min(30000, 1000 * 2 ** clientsRetry);
  clientsRetry += 1;
  setTimeout(watchClients, delay);
}

loadCatalog();
watchClients();
