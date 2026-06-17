const list = document.querySelector("#catalog-list");
const count = document.querySelector("#catalog-count");

// Install one-liner: copy the command to the clipboard.
const installCopyBtn = document.querySelector("#install-copy");
if (installCopyBtn) {
  installCopyBtn.addEventListener("click", async () => {
    const cmd = document.querySelector("#install-cmd");
    const text = cmd ? cmd.textContent : "";
    try {
      await navigator.clipboard.writeText(text);
    } catch (error) {
      const range = document.createRange();
      range.selectNodeContents(cmd);
      const sel = window.getSelection();
      sel.removeAllRanges();
      sel.addRange(range);
    }
    const original = installCopyBtn.textContent;
    installCopyBtn.textContent = "Copied";
    setTimeout(() => (installCopyBtn.textContent = original), 1500);
  });
}

// Catalog records keyed "owner/name", so the repo page can show description
// and clone URL for whichever repository is open.
const repoIndex = new Map();

function text(value, fallback = "") {
  return typeof value === "string" && value.trim() ? value.trim() : fallback;
}

function repoRoute(owner, name) {
  return `#repo/${encodeURIComponent(owner)}/${encodeURIComponent(name)}`;
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
  repoIndex.clear();

  if (!repos.length) {
    list.innerHTML = `<div class="catalog-empty">No repositories have been published yet.</div>`;
    return;
  }

  list.replaceChildren(
    ...repos.map((repo) => {
      repoIndex.set(`${repo.owner}/${repo.name}`, repo);

      // The whole card is a link to the repository's own page.
      const item = document.createElement("a");
      item.className = "catalog-item";
      item.href = repoRoute(repo.owner, repo.name);

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

      const open = document.createElement("span");
      open.className = "catalog-open";
      open.textContent = "Open repository →";

      item.append(header, description, meta, open);
      return item;
    })
  );

  // If a repo page is already open (deep link / reload), fill in its details
  // now that the catalog has loaded.
  if (currentRepo) openRepoPage(currentRepo.owner, currentRepo.name);
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

function showViewerMessage(message) {
  if (!viewerBodyEl) return;
  viewerBodyEl.replaceChildren();
  const note = document.createElement("div");
  note.className = "file-viewer-message";
  note.textContent = message;
  viewerBodyEl.append(note);
}

function appendViewerNotice(message) {
  if (!viewerBodyEl) return;
  const note = document.createElement("div");
  note.className = "file-viewer-notice";
  note.textContent = message;
  viewerBodyEl.append(note);
}

function renderCode(content) {
  if (!viewerBodyEl) return;
  viewerBodyEl.replaceChildren();
  let lines = String(content || "").split("\n");
  if (lines.length > 1 && lines[lines.length - 1] === "") lines = lines.slice(0, -1);
  if (!lines.length) lines = [""];
  const fragment = document.createDocumentFragment();
  lines.forEach((line, index) => {
    const row = document.createElement("div");
    row.className = "code-line";
    const number = document.createElement("span");
    number.className = "line-number";
    number.textContent = String(index + 1);
    const code = document.createElement("span");
    code.className = "line-code";
    code.textContent = line || " ";
    row.append(number, code);
    fragment.append(row);
  });
  viewerBodyEl.append(fragment);
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
  showViewerMessage("Loading...");
  showViewer();
  const { data, source, error } = await pullPath("blob", path);
  if (token !== navToken) return;
  setSource(source);
  if (!data) {
    showViewerMessage(unavailableMessage(error));
    return;
  }
  if (data.encoding === "base64") {
    showViewerMessage(`[binary file - ${formatSize(data.size)}, not shown]`);
    return;
  }
  renderCode(data.content || "");
  if (data.truncated) {
    appendViewerNotice("File truncated by the host.");
  }
}

if (viewerBackEl) {
  viewerBackEl.addEventListener("click", () => {
    navToken += 1; // cancel any in-flight blob load
    setSource(null);
    openDir(fileState ? fileState.dir : "");
  });
}

// ---- Issues tab: list the repo's issues from its git issues/ folder ---------

const tabCodeEl = document.querySelector("#tab-code");
const tabIssuesEl = document.querySelector("#tab-issues");
const tabIssuesCountEl = document.querySelector("#tab-issues-count");
const codeSectionEl = document.querySelector("#code");
const issuesSectionEl = document.querySelector("#issues");
const issueListEl = document.querySelector("#issue-list");
const issuesMetaEl = document.querySelector("#issues-meta");
let issuesLoadedFor = null;
let issuesToken = 0;

// Parse the leading "---\nkey: value\n---" frontmatter block of an issue.md.
function parseFrontmatter(content) {
  const out = {};
  if (!content) return out;
  const lines = String(content).split("\n");
  if (lines[0].trim() !== "---") return out;
  for (let i = 1; i < lines.length; i++) {
    if (lines[i].trim() === "---") break;
    const idx = lines[i].indexOf(":");
    if (idx < 0) continue;
    out[lines[i].slice(0, idx).trim()] = lines[i].slice(idx + 1).trim();
  }
  return out;
}

function issueRow(number, title, status) {
  const row = document.createElement("button");
  row.className = "file-row issue-row";
  row.type = "button";
  const dot = document.createElement("span");
  dot.className = `issue-dot ${status === "closed" ? "is-closed" : "is-open"}`;
  dot.title = status === "closed" ? "Closed" : "Open";
  const label = document.createElement("span");
  label.className = "file-name";
  label.textContent = `#${number}  ${title}`;
  const state = document.createElement("span");
  state.className = "issue-state";
  state.textContent = status === "closed" ? "Closed" : "Open";
  row.append(dot, label, state);
  // Open the issue's source markdown in the code viewer.
  row.addEventListener("click", () => {
    showRepoTab("code");
    openBlob(`issues/${number}/issue.md`);
  });
  return row;
}

async function loadIssues(owner, name) {
  if (!issueListEl) return;
  const token = ++issuesToken;
  issueListEl.innerHTML = `<div class="file-empty">Loading…</div>`;
  if (issuesMetaEl) issuesMetaEl.textContent = "";

  const { data, source, error } = await pullPath("tree", "issues");
  if (token !== issuesToken) return;
  if (!data || !Array.isArray(data.entries)) {
    issueListEl.replaceChildren();
    const note = document.createElement("div");
    note.className = "file-empty";
    // Only host-availability errors are "unavailable"; any git/tree error
    // (e.g. the repo simply has no issues/ folder) means none have been filed.
    const hostDown = ["no_host", "unreachable", "timeout"].includes(error);
    note.textContent = hostDown
      ? unavailableMessage(error)
      : "No issues have been filed for this repository yet.";
    issueListEl.append(note);
    if (tabIssuesCountEl && !hostDown) tabIssuesCountEl.textContent = "0";
    return;
  }
  if (issuesMetaEl) {
    issuesMetaEl.className = "file-meta";
    if (source === "live") {
      issuesMetaEl.textContent = "● live from host";
      issuesMetaEl.classList.add("source-live");
    } else if (source === "cached") {
      issuesMetaEl.textContent = "● cached · host offline";
      issuesMetaEl.classList.add("source-cached");
    }
  }

  const numbers = data.entries
    .filter((e) => e.type === "tree" && /^\d+$/.test(e.name))
    .map((e) => parseInt(e.name, 10))
    .sort((a, b) => b - a);
  if (!numbers.length) {
    issueListEl.replaceChildren();
    const note = document.createElement("div");
    note.className = "file-empty";
    note.textContent = "No issues have been filed for this repository yet.";
    issueListEl.append(note);
    if (tabIssuesCountEl) tabIssuesCountEl.textContent = "0";
    return;
  }

  const issues = await Promise.all(
    numbers.map(async (n) => {
      const { data: blob } = await pullPath("blob", `issues/${n}/issue.md`);
      const fm = parseFrontmatter(blob && blob.content);
      let status = fm.status || "open";
      // The current status may have changed via a later NNNN-status.md event.
      const { data: dir } = await pullPath("tree", `issues/${n}`);
      if (dir && Array.isArray(dir.entries)) {
        const statusFiles = dir.entries
          .filter((e) => e.type === "blob" && /^\d+-status\.md$/.test(e.name))
          .map((e) => e.name)
          .sort();
        if (statusFiles.length) {
          const last = statusFiles[statusFiles.length - 1];
          const { data: sb } = await pullPath("blob", `issues/${n}/${last}`);
          const sfm = parseFrontmatter(sb && sb.content);
          if (sfm.status) status = sfm.status;
        }
      }
      return { number: n, title: fm.title || `Issue #${n}`, status };
    })
  );
  if (token !== issuesToken) return;

  if (tabIssuesCountEl) tabIssuesCountEl.textContent = String(issues.length);
  issueListEl.replaceChildren(
    ...issues.map((i) => issueRow(i.number, i.title, i.status))
  );
}

function showRepoTab(tab) {
  const code = tab !== "issues";
  if (codeSectionEl) codeSectionEl.hidden = !code;
  if (issuesSectionEl) issuesSectionEl.hidden = code;
  if (tabCodeEl) tabCodeEl.classList.toggle("is-active", code);
  if (tabIssuesEl) tabIssuesEl.classList.toggle("is-active", !code);
  if (!code && fileState) {
    const repoKey = `${fileState.owner}/${fileState.name}`;
    if (issuesLoadedFor !== repoKey) {
      issuesLoadedFor = repoKey;
      loadIssues(fileState.owner, fileState.name);
    }
  }
}

if (tabCodeEl) tabCodeEl.addEventListener("click", () => showRepoTab("code"));
if (tabIssuesEl) tabIssuesEl.addEventListener("click", () => showRepoTab("issues"));

// ---- Routing: home (repo list) vs. a single repository page ----------------

const homeView = document.querySelector("#home-view");
const repoView = document.querySelector("#repo-view");
let currentRepo = null;

function parseRoute() {
  const hash = location.hash.replace(/^#/, "");
  if (hash.startsWith("repo/")) {
    const rest = hash.slice(5);
    const slash = rest.indexOf("/");
    if (slash > 0) {
      return {
        view: "repo",
        owner: decodeURIComponent(rest.slice(0, slash)),
        name: decodeURIComponent(rest.slice(slash + 1)),
      };
    }
  }
  return { view: "home" };
}

function route() {
  const r = parseRoute();
  if (r.view === "repo") {
    currentRepo = { owner: r.owner, name: r.name };
    if (homeView) homeView.hidden = true;
    if (repoView) repoView.hidden = false;
    window.scrollTo(0, 0);
    openRepoPage(r.owner, r.name);
  } else {
    currentRepo = null;
    if (repoView) repoView.hidden = true;
    if (homeView) homeView.hidden = false;
  }
}

function openRepoPage(owner, name) {
  const titleEl = document.querySelector("#repo-page-title");
  const descEl = document.querySelector("#repo-page-desc");
  const cloneInput = document.querySelector("#repo-clone-input");
  const cloneNote = document.querySelector("#repo-clone-note");
  if (titleEl) {
    titleEl.textContent = "";
    const ownerSpan = document.createElement("span");
    ownerSpan.textContent = owner + " / ";
    const nameStrong = document.createElement("strong");
    nameStrong.textContent = name;
    titleEl.append(ownerSpan, nameStrong);
  }
  const repo = repoIndex.get(`${owner}/${name}`);
  if (descEl) descEl.textContent = repo ? text(repo.description, "") : "";
  if (cloneInput && cloneNote) {
    // Clone straight from the mainnode; it streams live from the hosting client.
    cloneInput.value = `${location.origin}/${owner}/${name}`;
    cloneInput.placeholder = "";
    cloneNote.textContent =
      "git clone streams live from the client hosting this repo (it must be online).";
  }
  resetDownloadProgress();
  // Reset to the Code tab; issues lazy-load when the Issues tab is opened.
  issuesLoadedFor = null;
  if (tabIssuesCountEl) tabIssuesCountEl.textContent = "";
  loadFiles(owner, name);
  showRepoTab("code");
}

window.addEventListener("hashchange", route);

// ---- Download the whole repo into the browser (localStorage) with progress --

const downloadBtn = document.querySelector("#download-repo");
const downloadProgress = document.querySelector("#download-progress");
const downloadFill = document.querySelector("#download-bar-fill");
const downloadText = document.querySelector("#download-progress-text");
let downloading = false;

function resetDownloadProgress() {
  if (downloadProgress) downloadProgress.hidden = true;
  if (downloadFill) downloadFill.style.width = "0%";
  if (downloadText) downloadText.textContent = "";
  if (downloadBtn) downloadBtn.disabled = false;
}

function setDownloadProgress(fraction, message) {
  if (downloadProgress) downloadProgress.hidden = false;
  const safeFraction = Math.max(0, Math.min(1, Number(fraction) || 0));
  if (downloadFill) downloadFill.style.width = `${Math.round(safeFraction * 100)}%`;
  if (downloadText) downloadText.textContent = message;
}

// Walk every directory live to enumerate all file paths (each tree is cached).
async function collectBlobPaths() {
  const blobs = [];
  const queue = [""];
  let first = true;
  let rootOk = false;
  while (queue.length) {
    const dir = queue.shift();
    const { data } = await pullPath("tree", dir);
    if (first) {
      rootOk = !!(data && Array.isArray(data.entries));
      first = false;
    }
    if (!data || !Array.isArray(data.entries)) continue;
    for (const entry of data.entries) {
      const path = dir ? `${dir}/${entry.name}` : entry.name;
      if (entry.type === "tree") queue.push(path);
      else blobs.push(path);
    }
  }
  return { blobs, rootOk };
}

async function downloadRepo() {
  if (downloading || !fileState) return;
  downloading = true;
  if (downloadBtn) downloadBtn.disabled = true;
  setDownloadProgress(0, "Scanning files…");

  const { blobs, rootOk } = await collectBlobPaths();
  if (!rootOk && !blobs.length) {
    setDownloadProgress(0, "No client is hosting this repo right now — can't download.");
    downloading = false;
    if (downloadBtn) downloadBtn.disabled = false;
    return;
  }
  if (!blobs.length) {
    setDownloadProgress(1, "Saved the empty repository tree in browser local storage.");
    downloading = false;
    if (downloadBtn) downloadBtn.disabled = false;
    if (fileState) openDir(fileState.dir);
    return;
  }

  let done = 0;
  let failed = 0;
  for (const path of blobs) {
    const { data } = await pullPath("blob", path); // caches each blob
    if (!data) failed += 1;
    done += 1;
    setDownloadProgress(
      done / blobs.length,
      `Downloading ${done} / ${blobs.length} files into browser local storage…`
    );
  }

  setDownloadProgress(
    1,
    failed
      ? `Saved ${blobs.length - failed} of ${blobs.length} files (${failed} unavailable) in browser local storage.`
      : `Saved ${blobs.length} files in browser local storage — browsable offline.`
  );
  downloading = false;
  if (downloadBtn) downloadBtn.disabled = false;
  if (fileState) openDir(fileState.dir); // refresh from cache
}

if (downloadBtn) downloadBtn.addEventListener("click", downloadRepo);

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
