const list = document.querySelector("#catalog-list");
const count = document.querySelector("#catalog-count");
const searchInputs = [
  document.querySelector("#repo-search"),
  document.querySelector("#repo-search-inline"),
].filter(Boolean);
const statRepos = document.querySelector("#stat-repos");
const statHosts = document.querySelector("#stat-hosts");
const statIssues = document.querySelector("#stat-issues");
const statPulls = document.querySelector("#stat-pulls");
const viewList = document.querySelector("#view-list");
const viewGrid = document.querySelector("#view-grid");
let allRepositories = [];
let catalogQuery = "";
let catalogView = localStorage.getItem("forkmesh.catalog.view") || "list";

// Theme: mirrors the redesign behavior without adding a framework.
function applyTheme(mode) {
  const chosen =
    mode ||
    localStorage.getItem("forkmesh.theme") ||
    (window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches
      ? "dark"
      : "light");
  document.documentElement.classList.toggle("dark", chosen === "dark");
  localStorage.setItem("forkmesh.theme", chosen);
}
applyTheme();
const themeToggle = document.querySelector("#theme-toggle");
if (themeToggle) {
  themeToggle.addEventListener("click", () => {
    applyTheme(document.documentElement.classList.contains("dark") ? "light" : "dark");
  });
}

// Slash focuses repository search, like modern forge UIs.
document.addEventListener("keydown", (event) => {
  if (event.key !== "/" || event.metaKey || event.ctrlKey || event.altKey) return;
  const active = document.activeElement;
  if (active && ["INPUT", "TEXTAREA"].includes(active.tagName)) return;
  const target = searchInputs[0];
  if (target) {
    event.preventDefault();
    target.focus();
  }
});

async function copyText(text, button) {
  try {
    await navigator.clipboard.writeText(text);
  } catch (error) {
    const area = document.createElement("textarea");
    area.value = text;
    area.style.position = "fixed";
    area.style.left = "-9999px";
    document.body.append(area);
    area.select();
    document.execCommand("copy");
    area.remove();
  }
  if (button) {
    const original = button.textContent;
    button.textContent = "Copied";
    setTimeout(() => (button.textContent = original), 1500);
  }
}

// Install one-liner banner.
const installBanner = document.querySelector("#install-banner");
const installCopyBtn = document.querySelector("#install-copy");
const installDismissBtn = document.querySelector("#install-dismiss");
if (installBanner && localStorage.getItem("forkmesh.install.dismissed") === "1") {
  installBanner.hidden = true;
}
if (installCopyBtn) {
  installCopyBtn.addEventListener("click", () => {
    const cmd = document.querySelector("#install-cmd");
    copyText(cmd ? cmd.textContent : "", installCopyBtn);
  });
}
if (installDismissBtn) {
  installDismissBtn.addEventListener("click", () => {
    if (installBanner) installBanner.hidden = true;
    localStorage.setItem("forkmesh.install.dismissed", "1");
  });
}

// Catalog records keyed "owner/name", so the repo page can show description
// and clone URL for whichever repository is open.
const repoIndex = new Map();
// Logical repos keyed by group key (root commit, else name) -> array of records,
// so the same repo mirrored by several nodes renders as one card and the detail
// page can list every mirror node.
const repoGroups = new Map();

// Two catalog records belong to the same logical repo when they share a root
// commit (published by mirrors of the same repo); fall back to the repo name.
function repoGroupKey(repo) {
  const root = text(repo && repo.rootCommit);
  return root ? "root:" + root : "name:" + text(repo && repo.name).toLowerCase();
}

// Group records into logical repos. Within each group the "primary" is a live
// host if any (so Browse/Clone hit an online node), else the most recently synced.
function groupRepositories(repos) {
  const groups = new Map();
  for (const repo of repos) {
    const key = repoGroupKey(repo);
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(repo);
  }
  return [...groups.values()].map((members) => {
    const sorted = [...members].sort((a, b) => {
      if (!!b.liveHost !== !!a.liveHost) return (b.liveHost ? 1 : 0) - (a.liveHost ? 1 : 0);
      return Number(b.lastSync || 0) - Number(a.lastSync || 0);
    });
    return { primary: sorted[0], members: sorted };
  });
}

function text(value, fallback = "") {
  return typeof value === "string" && value.trim() ? value.trim() : fallback;
}

// Encode a "/"-separated repo path, keeping the slashes but escaping segments.
function encPath(path) {
  return String(path || "")
    .split("/")
    .filter(Boolean)
    .map(encodeURIComponent)
    .join("/");
}

// Clean, shareable URLs (History API, no "#repo/" hash):
//   /<owner>/<name>                     repo home
//   /<owner>/<name>/tree/<dir>          a sub-directory
//   /<owner>/<name>/blob/<file>         a file
function repoUrl(owner, name) {
  return `/${encodeURIComponent(owner)}/${encodeURIComponent(name)}`;
}
function treeUrl(owner, name, dir) {
  const d = encPath(dir);
  return d ? `${repoUrl(owner, name)}/tree/${d}` : repoUrl(owner, name);
}
function blobUrl(owner, name, path) {
  return `${repoUrl(owner, name)}/blob/${encPath(path)}`;
}
function repoRoute(owner, name) {
  return repoUrl(owner, name);
}
function profileUrl(owner) {
  return `/profile/${encodeURIComponent(owner)}`;
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

function repoCloneUrl(owner, name) {
  const origin = location.protocol === "file:" ? "https://forkmesh.com" : location.origin;
  return `${origin}/${owner}/${name}`;
}

function updateCatalogStats(repos) {
  const logical = groupRepositories(repos).length; // distinct repos, not mirrors
  if (count) count.textContent = `${logical} mirrored`;
  if (statRepos) statRepos.textContent = String(logical);
  if (statIssues) statIssues.textContent = "Git";
  if (statPulls) statPulls.textContent = "Patch";
  if (statHosts) statHosts.textContent = "0";
}

function setCatalogView(view) {
  catalogView = view;
  localStorage.setItem("forkmesh.catalog.view", view);
  if (list) list.classList.toggle("grid-mode", view === "grid");
  if (viewList) viewList.classList.toggle("is-active", view !== "grid");
  if (viewGrid) viewGrid.classList.toggle("is-active", view === "grid");
}

function filteredRepositories() {
  const q = catalogQuery.trim().toLowerCase();
  if (!q) return allRepositories;
  return allRepositories.filter((repo) => {
    return [repo.owner, repo.name, repo.description, repo.channel, repo.source].some((value) =>
      text(value).toLowerCase().includes(q)
    );
  });
}

function routeCatalogClick(event, owner, name) {
  if (event.metaKey || event.ctrlKey || event.shiftKey || event.button !== 0) return;
  event.preventDefault();
  go(repoUrl(owner, name));
}

function routeProfileClick(event, owner) {
  if (event.metaKey || event.ctrlKey || event.shiftKey || event.button !== 0) return;
  event.preventDefault();
  go(profileUrl(owner));
}

function createCatalogGroupItem(group) {
  const repo = group.primary;            // a live host if any, else newest
  const members = group.members;
  for (const m of members) repoIndex.set(`${m.owner}/${m.name}`, m);
  repoGroups.set(repoGroupKey(repo), members);
  const liveHosts = members.reduce((n, m) => n + (m.liveHost ? 1 : 0), 0);

  const item = document.createElement("article");
  item.className = "catalog-item";

  const header = document.createElement("div");
  header.className = "catalog-item-header";

  const title = document.createElement("h3");
  const link = document.createElement("a");
  link.href = repoRoute(repo.owner, repo.name);
  link.textContent = `${text(repo.owner, "owner")}/${text(repo.name, "repository")}`;
  link.addEventListener("click", (event) => routeCatalogClick(event, repo.owner, repo.name));
  title.append(link);
  header.append(title);

  const status = document.createElement("span");
  // Combined live-host count across every node mirroring this logical repo.
  renderHostPill(status, liveHosts);
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
  maintainer.textContent = `Maintainer ${text(repo.maintainer).slice(0, 12)}…`;
  meta.append(channel, synced, maintainer);
  if (members.length > 1) {
    const mirrors = document.createElement("span");
    mirrors.textContent = `${members.length} mirror nodes`;
    meta.append(mirrors);
  } else {
    const source = document.createElement("span");
    source.textContent = text(repo.source, "local-node");
    meta.append(source);
  }

  const actions = document.createElement("div");
  actions.className = "repo-card-actions";
  const copy = document.createElement("button");
  copy.className = "copy-button";
  copy.type = "button";
  copy.textContent = "Copy clone";
  copy.addEventListener("click", (event) => {
    event.preventDefault();
    event.stopPropagation();
    copyText(`git clone ${repoCloneUrl(repo.owner, repo.name)}`, copy);
  });
  const open = document.createElement("a");
  open.className = "catalog-open";
  open.href = repoRoute(repo.owner, repo.name);
  open.textContent = "Browse repository →";
  open.addEventListener("click", (event) => routeCatalogClick(event, repo.owner, repo.name));
  actions.append(copy, open);

  item.append(header, description, meta, actions);
  return item;
}

function renderHostPill(statusEl, hosts) {
  const online = Number(hosts) || 0;
  statusEl.className = online > 0 ? "status-pill online" : "status-pill offline";
  statusEl.replaceChildren(
    document.createElement("span"),
    document.createTextNode(
      online > 0 ? `${online} live host${online === 1 ? "" : "s"}` : "host offline"
    )
  );
}

function renderCatalogList() {
  if (!list) return;
  const repos = filteredRepositories();
  repoIndex.clear();
  repoGroups.clear();
  if (!repos.length) {
    list.innerHTML = allRepositories.length
      ? `<div class="catalog-empty">No repositories match your search.</div>`
      : `<div class="catalog-empty">No repositories have been published yet.</div>`;
    return;
  }
  // Collapse mirrors of the same logical repo into one card.
  list.replaceChildren(...groupRepositories(repos).map(createCatalogGroupItem));
  updateLiveHostStat();
  if (currentRepo) {
    const r = parseRoute();
    if (!catalogLoaded || findPublishedRepo(r.owner, r.name)) {
      openRepoPage(r.owner, r.name, r.mode, r.filePath);
    }
  }
}

// A single live probe for the repo the user actually opened (the catalog list
// itself renders from the cached liveHost flag, not per-card probes).
async function checkHost(owner, name, statusEl) {
  try {
    const response = await fetch(`/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(name)}/host`, {
      headers: { accept: "application/json" },
    });
    const data = await response.json();
    renderHostPill(statusEl, Number(data.hosts) || 0);
  } catch (error) {
    statusEl.className = "status-pill offline";
    statusEl.replaceChildren(document.createElement("span"), document.createTextNode("host unknown"));
  }
}

function updateLiveHostStat() {
  if (!statHosts) return;
  // Total live hosts across every mirror (not the number of cards).
  const live = allRepositories.reduce((n, r) => n + (r.liveHost ? 1 : 0), 0);
  statHosts.textContent = String(live);
}

function render(repositories) {
  allRepositories = Array.isArray(repositories) ? repositories : [];
  catalogLoaded = true;
  updateCatalogStats(allRepositories);
  renderCatalogList();
  route();
}

for (const input of searchInputs) {
  input.addEventListener("input", (event) => {
    catalogQuery = event.target.value;
    for (const other of searchInputs) {
      if (other !== event.target) other.value = catalogQuery;
    }
    renderCatalogList();
  });
}
if (viewList) viewList.addEventListener("click", () => setCatalogView("list"));
if (viewGrid) viewGrid.addEventListener("click", () => setCatalogView("grid"));
setCatalogView(catalogView);

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
      `/api/repo/${encodeURIComponent(owner)}/${encodeURIComponent(name)}/${kind}?path=${encodeURIComponent(path)}`,
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
  root.addEventListener("click", () =>
    go(repoUrl(fileState.owner, fileState.name)));
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
    crumb.addEventListener("click", () =>
      go(treeUrl(fileState.owner, fileState.name, target)));
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
        const parent = cut === -1 ? "" : path.slice(0, cut);
        go(treeUrl(fileState.owner, fileState.name, parent));
      })
    );
  }
  for (const entry of entries) {
    const childPath = path ? `${path}/${entry.name}` : entry.name;
    if (entry.type === "tree") {
      rows.push(
        fileRow("folder", entry.name, "", () =>
          go(treeUrl(fileState.owner, fileState.name, childPath))
        )
      );
    } else {
      rows.push(
        fileRow("file", entry.name, formatSize(entry.size), () =>
          go(blobUrl(fileState.owner, fileState.name, childPath))
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
    if (fileState) go(treeUrl(fileState.owner, fileState.name, fileState.dir));
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

const tabPullsEl = document.querySelector("#tab-pulls");
const tabPullsCountEl = document.querySelector("#tab-pulls-count");
const pullsSectionEl = document.querySelector("#pulls");
const pullListEl = document.querySelector("#pull-list");
const pullsMetaEl = document.querySelector("#pulls-meta");
const tabDiscussionsEl = document.querySelector("#tab-discussions");
const discussionsSectionEl = document.querySelector("#discussions");
const tabNetworkEl = document.querySelector("#tab-network");
const repoNetworkSectionEl = document.querySelector("#repo-network");
let pullsLoadedFor = null;
let pullsToken = 0;
let pullsCountToken = 0;

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
    if (fileState)
      go(blobUrl(fileState.owner, fileState.name, `issues/${number}/issue.md`));
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


function pullRow(number, title, status, base, head, signed) {
  const row = document.createElement("button");
  row.type = "button";
  row.className = "file-row issue-row";
  row.addEventListener("click", () => {
    if (fileState) go(blobUrl(fileState.owner, fileState.name, `pulls/${number}/pull.md`));
  });
  const dot = document.createElement("span");
  dot.className = `issue-dot ${status === "merged" ? "is-closed" : "is-open"}`;
  const main = document.createElement("span");
  main.className = "file-name";
  main.textContent = `#${number} ${title}`;
  const state = document.createElement("span");
  state.className = "issue-state";
  state.textContent = `${status || "open"}${signed ? " · signed" : ""}${base || head ? ` · ${head || "head"} → ${base || "base"}` : ""}`;
  row.append(dot, main, state);
  return row;
}

async function loadPullCount() {
  if (!tabPullsCountEl) return;
  const token = ++pullsCountToken;
  const { data, error } = await pullPath("tree", "pulls");
  if (token !== pullsCountToken) return;
  const hostDown = ["no_host", "unreachable", "timeout"].includes(error);
  if (!data || !Array.isArray(data.entries)) {
    if (!hostDown) tabPullsCountEl.textContent = "0";
    return;
  }
  const count = data.entries.filter((e) => e.type === "tree" && /^\d+$/.test(e.name)).length;
  tabPullsCountEl.textContent = String(count);
}

async function loadPulls(owner, name) {
  if (!pullListEl) return;
  const token = ++pullsToken;
  pullListEl.innerHTML = `<div class="file-empty">Loading…</div>`;
  if (pullsMetaEl) pullsMetaEl.textContent = "";

  const { data, source, error } = await pullPath("tree", "pulls");
  if (token !== pullsToken) return;
  if (!data || !Array.isArray(data.entries)) {
    pullListEl.replaceChildren();
    const note = document.createElement("div");
    note.className = "file-empty";
    const hostDown = ["no_host", "unreachable", "timeout"].includes(error);
    note.textContent = hostDown
      ? unavailableMessage(error)
      : "No pull requests have been published for this repository yet.";
    pullListEl.append(note);
    if (tabPullsCountEl && !hostDown) tabPullsCountEl.textContent = "0";
    return;
  }

  if (pullsMetaEl) {
    pullsMetaEl.className = "file-meta";
    if (source === "live") {
      pullsMetaEl.textContent = "● live from host";
      pullsMetaEl.classList.add("source-live");
    } else if (source === "cached") {
      pullsMetaEl.textContent = "● cached · host offline";
      pullsMetaEl.classList.add("source-cached");
    }
  }

  const numbers = data.entries
    .filter((e) => e.type === "tree" && /^\d+$/.test(e.name))
    .map((e) => parseInt(e.name, 10))
    .sort((a, b) => b - a);
  if (!numbers.length) {
    pullListEl.replaceChildren();
    const note = document.createElement("div");
    note.className = "file-empty";
    note.textContent = "No pull requests have been published for this repository yet.";
    pullListEl.append(note);
    if (tabPullsCountEl) tabPullsCountEl.textContent = "0";
    return;
  }

  const pulls = await Promise.all(
    numbers.map(async (n) => {
      const { data: blob } = await pullPath("blob", `pulls/${n}/pull.md`);
      const fm = parseFrontmatter(blob && blob.content);
      return {
        number: n,
        title: fm.title || `Pull request #${n}`,
        status: fm.status || "open",
        base: fm.base || "",
        head: fm.head || "",
        signed: !!fm.sig,
      };
    })
  );
  if (token !== pullsToken) return;
  if (tabPullsCountEl) tabPullsCountEl.textContent = String(pulls.length);
  pullListEl.replaceChildren(
    ...pulls.map((p) => pullRow(p.number, p.title, p.status, p.base, p.head, p.signed))
  );
}

// ---- Commits tab: list commits and show a commit's diff --------------------

const tabCommitsEl = document.querySelector("#tab-commits");
const tabCommitsCountEl = document.querySelector("#tab-commits-count");
const commitsSectionEl = document.querySelector("#commits");
const commitListEl = document.querySelector("#commit-list");
const commitsMetaEl = document.querySelector("#commits-meta");
const commitDetailEl = document.querySelector("#commit-detail");
const commitDetailTitleEl = document.querySelector("#commit-detail-title");
const commitDetailMetaEl = document.querySelector("#commit-detail-meta");
const commitDiffEl = document.querySelector("#commit-diff");
const commitBackEl = document.querySelector("#commit-back");
const latestCommitEl = document.querySelector("#latest-commit");
let commitsLoadedFor = null;
let commitToken = 0;

function shortHash(h) {
  return String(h || "").slice(0, 7);
}

// The newest commit, shown as a bar above the file list on the Code tab.
async function loadLatestCommit(owner, name) {
  if (!latestCommitEl) return;
  latestCommitEl.hidden = true;
  const { data } = await pullPath("commits", "");
  if (!data || !Array.isArray(data.commits) || !data.commits.length) return;
  if (!fileState || fileState.owner !== owner || fileState.name !== name) return;
  const c = data.commits[0];
  latestCommitEl.replaceChildren();
  const msg = document.createElement("span");
  msg.className = "latest-commit-msg";
  msg.textContent = c.subject;
  const meta = document.createElement("span");
  meta.className = "latest-commit-meta";
  meta.textContent = `${c.author} · ${c.date} · ${shortHash(c.hash)}`;
  latestCommitEl.append(msg, meta);
  latestCommitEl.href = "#";
  latestCommitEl.onclick = (e) => {
    e.preventDefault();
    showRepoTab("commits");
    showCommitDiff(c.hash);
  };
  latestCommitEl.hidden = false;
}

function commitRow(c) {
  const row = document.createElement("button");
  row.className = "file-row commit-row";
  row.type = "button";
  const main = document.createElement("span");
  main.className = "file-name";
  main.textContent = c.subject;
  const note = document.createElement("span");
  note.className = "file-note";
  note.textContent = `${c.author} · ${c.date} · ${shortHash(c.hash)}`;
  row.append(main, note);
  row.addEventListener("click", () => showCommitDiff(c.hash));
  return row;
}

async function loadCommits(owner, name) {
  if (!commitListEl) return;
  if (commitDetailEl) commitDetailEl.hidden = true;
  commitListEl.hidden = false;
  commitListEl.innerHTML = `<div class="file-empty">Loading…</div>`;
  const token = ++commitToken;
  const { data, source, error } = await pullPath("commits", "");
  if (token !== commitToken) return;
  if (commitsMetaEl)
    commitsMetaEl.textContent =
      source === "live" ? "● live from host" : source === "cached" ? "○ cached" : "";
  if (!data || !Array.isArray(data.commits)) {
    commitListEl.replaceChildren();
    const note = document.createElement("div");
    note.className = "file-empty";
    note.textContent = unavailableMessage(error);
    commitListEl.append(note);
    return;
  }
  if (tabCommitsCountEl) tabCommitsCountEl.textContent = String(data.commits.length);
  commitListEl.replaceChildren(...data.commits.map(commitRow));
}

function imageDiffFigure(label, item, key) {
  const figure = document.createElement("figure");
  figure.className = "diff-image-figure";
  const caption = document.createElement("figcaption");
  caption.textContent = label;
  if (item[key]) {
    const img = document.createElement("img");
    img.alt = `${label}: ${item.path}`;
    img.src = `data:${item.mime};base64,${item[key]}`;
    figure.append(img, caption);
  } else {
    const empty = document.createElement("div");
    empty.className = "diff-image-empty";
    empty.textContent = "Not present";
    figure.append(empty, caption);
  }
  return figure;
}

function appendImageDiff(block, item) {
  const preview = document.createElement("div");
  preview.className = "diff-image-preview";
  preview.append(imageDiffFigure("Before", item, "old"), imageDiffFigure("After", item, "new"));
  block.append(preview);
}

// Render a unified diff into DOM rows with an old/new line-number gutter.
function renderDiff(container, diffText, imageDiffs) {
  container.replaceChildren();
  const lines = String(diffText || "").split("\n");
  const images = new Map();
  for (const item of Array.isArray(imageDiffs) ? imageDiffs : []) {
    if (item && item.path && item.mime) images.set(item.path, item);
  }
  const shownImages = new Set();
  let table = null;
  let oldNo = 0;
  let newNo = 0;
  const startFile = (path) => {
    const block = document.createElement("div");
    block.className = "diff-file";
    const head = document.createElement("div");
    head.className = "diff-file-head";
    head.textContent = path;
    table = document.createElement("div");
    table.className = "diff-body";
    block.append(head);
    const image = images.get(path);
    if (image) {
      appendImageDiff(block, image);
      shownImages.add(path);
    }
    block.append(table);
    container.append(block);
  };
  const addRow = (cls, oldn, newn, code) => {
    if (!table) startFile("");
    const row = document.createElement("div");
    row.className = "diff-row " + cls;
    const o = document.createElement("span");
    o.className = "diff-ln";
    o.textContent = oldn;
    const n = document.createElement("span");
    n.className = "diff-ln";
    n.textContent = newn;
    const c = document.createElement("span");
    c.className = "diff-code";
    c.textContent = code;
    row.append(o, n, c);
    table.append(row);
  };
  for (const line of lines) {
    if (line.startsWith("diff --git ")) {
      const b = line.indexOf(" b/");
      startFile(b >= 0 ? line.slice(b + 3) : line);
      continue;
    }
    if (
      line.startsWith("index ") || line.startsWith("--- ") ||
      line.startsWith("+++ ") || line.startsWith("new file") ||
      line.startsWith("deleted file") || line.startsWith("similarity ") ||
      line.startsWith("rename ") || line.startsWith("old mode") ||
      line.startsWith("new mode")
    )
      continue;
    if (line.startsWith("@@")) {
      const m = /@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@/.exec(line);
      if (m) {
        oldNo = +m[1];
        newNo = +m[2];
      }
      addRow("diff-hunk", "", "", line);
      continue;
    }
    const ch = line[0];
    if (ch === "+") addRow("diff-add", "", String(newNo++), line.slice(1) || " ");
    else if (ch === "-") addRow("diff-del", String(oldNo++), "", line.slice(1) || " ");
    else if (ch === "\\") addRow("diff-ctx", "", "", line);
    else addRow("diff-ctx", String(oldNo++), String(newNo++), line.slice(1) || " ");
  }
  for (const [path, image] of images.entries()) {
    if (!shownImages.has(path)) startFile(path);
  }
  if (!container.childNodes.length) {
    const empty = document.createElement("div");
    empty.className = "file-empty";
    empty.textContent = "No changes in this commit.";
    container.append(empty);
  }
}

async function showCommitDiff(hash) {
  if (!commitDetailEl) return;
  if (commitListEl) commitListEl.hidden = true;
  commitDetailEl.hidden = false;
  commitDetailTitleEl.textContent = "Commit " + shortHash(hash);
  commitDetailMetaEl.textContent = "Loading…";
  commitDiffEl.replaceChildren();
  const token = ++commitToken;
  const { data, error } = await pullPath("commit", hash);
  if (token !== commitToken) return;
  if (!data || !data.commit) {
    commitDetailMetaEl.textContent = unavailableMessage(error);
    return;
  }
  const c = data.commit;
  commitDetailTitleEl.textContent = "Commit " + shortHash(c.hash);
  const files = Array.isArray(data.files) ? data.files : [];
  let adds = 0;
  let dels = 0;
  for (const f of files) {
    adds += Number(f.adds) || 0;
    dels += Number(f.dels) || 0;
  }
  commitDetailMetaEl.replaceChildren();
  const subj = document.createElement("div");
  subj.className = "commit-subject";
  subj.textContent = c.subject;
  const info = document.createElement("div");
  info.className = "commit-info";
  info.textContent = `${c.author} committed on ${c.date} · ${files.length} file${
    files.length === 1 ? "" : "s"
  } changed · +${adds} −${dels}`;
  commitDetailMetaEl.append(subj, info);
  if (c.body) {
    const body = document.createElement("div");
    body.className = "commit-body";
    body.textContent = c.body;
    commitDetailMetaEl.append(body);
  }
  renderDiff(commitDiffEl, data.diff, data.imageDiffs);
  if (data.truncated) {
    const t = document.createElement("div");
    t.className = "file-empty";
    t.textContent = "Diff truncated by the host.";
    commitDiffEl.append(t);
  }
}

if (commitBackEl)
  commitBackEl.addEventListener("click", () => {
    commitDetailEl.hidden = true;
    if (commitListEl) commitListEl.hidden = false;
  });

function showRepoTab(tab) {
  const sections = {
    code: codeSectionEl,
    commits: commitsSectionEl,
    issues: issuesSectionEl,
    pulls: pullsSectionEl,
    discussions: discussionsSectionEl,
    network: repoNetworkSectionEl,
  };
  for (const [name, section] of Object.entries(sections)) {
    if (section) section.hidden = name !== tab;
  }
  const tabs = {
    code: tabCodeEl,
    commits: tabCommitsEl,
    issues: tabIssuesEl,
    pulls: tabPullsEl,
    discussions: tabDiscussionsEl,
    network: tabNetworkEl,
  };
  for (const [name, button] of Object.entries(tabs)) {
    if (button) button.classList.toggle("is-active", name === tab);
  }
  if (!fileState) return;
  const repoKey = `${fileState.owner}/${fileState.name}`;
  if (tab === "issues" && issuesLoadedFor !== repoKey) {
    issuesLoadedFor = repoKey;
    loadIssues(fileState.owner, fileState.name);
  }
  if (tab === "commits" && commitsLoadedFor !== repoKey) {
    commitsLoadedFor = repoKey;
    loadCommits(fileState.owner, fileState.name);
  }
  if (tab === "pulls" && pullsLoadedFor !== repoKey) {
    pullsLoadedFor = repoKey;
    loadPulls(fileState.owner, fileState.name);
  }
}

if (tabCodeEl) tabCodeEl.addEventListener("click", () => showRepoTab("code"));
if (tabCommitsEl) tabCommitsEl.addEventListener("click", () => showRepoTab("commits"));
if (tabIssuesEl) tabIssuesEl.addEventListener("click", () => showRepoTab("issues"));
if (tabPullsEl) tabPullsEl.addEventListener("click", () => showRepoTab("pulls"));
if (tabDiscussionsEl) tabDiscussionsEl.addEventListener("click", () => showRepoTab("discussions"));
if (tabNetworkEl) tabNetworkEl.addEventListener("click", () => showRepoTab("network"));

// ---- Routing: home (repo list) vs. a single repository page ----------------

const homeView = document.querySelector("#home-view");
const profileView = document.querySelector("#profile-view");
const repoView = document.querySelector("#repo-view");
const notFoundView = document.querySelector("#not-found-view");
let currentRepo = null;
let currentProfile = null;
let catalogLoaded = false;

function decodeSeg(s) {
  try {
    return decodeURIComponent(s);
  } catch (error) {
    return s;
  }
}

function parseRoute() {
  const path = location.pathname.replace(/^\/+|\/+$/g, "");
  if (!path) return { view: "home" };
  const segs = path.split("/").map(decodeSeg);
  if (segs.length === 2 && segs[0] === "profile" && segs[1]) {
    return { view: "profile", owner: segs[1] };
  }
  if (segs.length >= 2 && segs[0] && segs[1]) {
    let mode = null;
    let filePath = "";
    if (segs.length >= 3 && (segs[2] === "tree" || segs[2] === "blob")) {
      mode = segs[2];
      filePath = segs.slice(3).join("/");
    } else if (segs.length >= 3) {
      return { view: "not-found" };
    }
    return { view: "repo", owner: segs[0], name: segs[1], mode, filePath };
  }
  return { view: "not-found" };
}

function findPublishedRepo(owner, name) {
  return allRepositories.find((repo) => repo.owner === owner && repo.name === name);
}

function showNotFound() {
  currentRepo = null;
  currentProfile = null;
  if (homeView) homeView.hidden = true;
  if (profileView) profileView.hidden = true;
  if (repoView) repoView.hidden = true;
  if (notFoundView) notFoundView.hidden = false;
  document.title = "Page not found · ForkMesh";
}

function route() {
  const r = parseRoute();
  if (r.view === "profile") {
    currentRepo = null;
    currentProfile = r.owner;
    if (homeView) homeView.hidden = true;
    if (repoView) repoView.hidden = true;
    if (profileView) profileView.hidden = false;
    if (notFoundView) notFoundView.hidden = true;
    window.scrollTo(0, 0);
    openProfilePage(r.owner);
  } else if (r.view === "repo") {
    if (catalogLoaded && !findPublishedRepo(r.owner, r.name)) {
      showNotFound();
      return;
    }
    // Navigating within the same repo (folder/file/Back) only re-renders the
    // file area, so the page header and latest-commit aren't rebuilt each time.
    const sameRepo =
      currentRepo &&
      currentRepo.owner === r.owner &&
      currentRepo.name === r.name &&
      repoView &&
      !repoView.hidden;
    currentRepo = { owner: r.owner, name: r.name };
    currentProfile = null;
    if (homeView) homeView.hidden = true;
    if (profileView) profileView.hidden = true;
    if (repoView) repoView.hidden = false;
    if (notFoundView) notFoundView.hidden = true;
    document.title = `${r.owner}/${r.name} · ForkMesh`;
    if (sameRepo) {
      renderRepoPath(r.owner, r.name, r.mode, r.filePath);
    } else {
      window.scrollTo(0, 0);
      openRepoPage(r.owner, r.name, r.mode, r.filePath);
    }
  } else if (r.view === "not-found") {
    showNotFound();
  } else {
    currentRepo = null;
    currentProfile = null;
    if (repoView) repoView.hidden = true;
    if (profileView) profileView.hidden = true;
    if (homeView) homeView.hidden = false;
    if (notFoundView) notFoundView.hidden = true;
    document.title = "ForkMesh";
  }
}

// Navigate in-app: push a new history entry (so Back works) and render it.
function go(url) {
  history.pushState({}, "", url);
  route();
}

// List every node mirroring this logical repo on the detail page, with each
// node's live/offline state and last sync.
function renderMirrorNodes(owner, name, repo) {
  const el = document.querySelector("#repo-mirror-nodes");
  if (!el) return;
  const ref = repo || repoIndex.get(`${owner}/${name}`) || { name };
  const key = repoGroupKey(ref);
  const members = allRepositories
    .filter((r) => repoGroupKey(r) === key)
    .sort((a, b) =>
      (b.liveHost ? 1 : 0) - (a.liveHost ? 1 : 0) ||
      Number(b.lastSync || 0) - Number(a.lastSync || 0));
  if (members.length <= 1) {
    el.hidden = true;
    el.replaceChildren();
    return;
  }
  const live = members.reduce((n, m) => n + (m.liveHost ? 1 : 0), 0);
  const heading = document.createElement("div");
  heading.className = "mirror-nodes-title";
  heading.textContent = `Mirror nodes · ${members.length} (${live} live)`;
  const ul = document.createElement("ul");
  ul.className = "mirror-nodes-list";
  for (const m of members) {
    const li = document.createElement("li");
    const a = document.createElement("a");
    a.href = repoRoute(m.owner, m.name);
    a.textContent = `${m.owner}/${m.name}`;
    a.addEventListener("click", (event) => routeCatalogClick(event, m.owner, m.name));
    const pill = document.createElement("span");
    pill.className = m.liveHost ? "status-pill online" : "status-pill offline";
    pill.replaceChildren(
      document.createElement("span"),
      document.createTextNode(m.liveHost ? "live" : "offline"));
    const synced = document.createElement("span");
    synced.className = "mirror-node-sync";
    synced.textContent = `synced ${formatDate(m.lastSync)}`;
    li.append(a, pill, synced);
    ul.append(li);
  }
  el.replaceChildren(heading, ul);
  el.hidden = false;
}

function openRepoPage(owner, name, mode = null, filePath = "") {
  const titleEl = document.querySelector("#repo-page-title");
  const profileLink = document.querySelector("#repo-owner-profile");
  const descEl = document.querySelector("#repo-page-desc");
  const cloneInput = document.querySelector("#repo-clone-input");
  const cloneNote = document.querySelector("#repo-clone-note");
  const cloneCopy = document.querySelector("#repo-clone-copy");
  const repoStatus = document.querySelector("#repo-status");
  if (titleEl) {
    titleEl.textContent = "";
    const ownerSpan = document.createElement("span");
    ownerSpan.textContent = owner + " / ";
    const nameStrong = document.createElement("strong");
    nameStrong.textContent = name;
    titleEl.append(ownerSpan, nameStrong);
  }
  if (profileLink) {
    profileLink.href = profileUrl(owner);
    profileLink.setAttribute("aria-label", `Open ${owner} profile`);
    profileLink.onclick = (event) => routeProfileClick(event, owner);
  }
  const repo = findPublishedRepo(owner, name);
  if (descEl) descEl.textContent = repo ? text(repo.description, "") : "";
  renderMirrorNodes(owner, name, repo);
  if (cloneInput && cloneNote) {
    // Clone straight from the mainnode; it streams live from the hosting client.
    cloneInput.value = repoCloneUrl(owner, name);
    cloneInput.placeholder = "";
    cloneNote.textContent =
      "git clone streams live from the client hosting this repo (it must be online).";
  }
  if (cloneCopy && cloneInput) {
    cloneCopy.onclick = () => copyText(`git clone ${cloneInput.value}`, cloneCopy);
  }
  if (repoStatus) {
    repoStatus.className = "status-pill cached";
    repoStatus.replaceChildren(document.createElement("span"), document.createTextNode("checking host"));
    checkHost(owner, name, repoStatus);
  }
  resetDownloadProgress();
  // Reset to the Code tab; issues/commits/pulls lazy-load when their tab is opened.
  issuesLoadedFor = null;
  commitsLoadedFor = null;
  pullsLoadedFor = null;
  if (tabIssuesCountEl) tabIssuesCountEl.textContent = "";
  if (tabCommitsCountEl) tabCommitsCountEl.textContent = "";
  if (tabPullsCountEl) tabPullsCountEl.textContent = "";
  if (latestCommitEl) latestCommitEl.hidden = true;
  renderRepoPath(owner, name, mode, filePath);
  loadPullCount();
  loadLatestCommit(owner, name);
}

async function loadProfileAccount(owner) {
  try {
    const response = await fetch(`/api/accounts/${encodeURIComponent(owner)}`, {
      headers: { accept: "application/json" },
    });
    if (!response.ok) return null;
    const data = await response.json();
    return data && data.exists ? data : null;
  } catch (error) {
    return null;
  }
}

function profileMetaItem(label, value) {
  const item = document.createElement("div");
  item.className = "mini-metric";
  const labelEl = document.createElement("span");
  labelEl.textContent = label;
  const valueEl = document.createElement("strong");
  valueEl.textContent = value;
  item.append(labelEl, valueEl);
  return item;
}

async function openProfilePage(owner) {
  const title = document.querySelector("#profile-title");
  const subtitle = document.querySelector("#profile-subtitle");
  const meta = document.querySelector("#profile-meta");
  const reposList = document.querySelector("#profile-repos-list");
  const reposCount = document.querySelector("#profile-repos-count");
  if (title) title.textContent = owner;
  if (subtitle) subtitle.textContent = "ForkMesh profile";
  document.title = `${owner} · ForkMesh`;

  const repos = allRepositories.filter((repo) => repo.owner === owner);
  const groups = groupRepositories(repos);
  if (reposCount) reposCount.textContent = `${groups.length} mirrored`;
  if (reposList) {
    reposList.replaceChildren(
      ...(groups.length
        ? groups.map(createCatalogGroupItem)
        : [Object.assign(document.createElement("div"), {
            className: "catalog-empty",
            textContent: catalogLoaded
              ? "This profile has not published any repositories."
              : "Loading profile repositories…",
          })])
    );
  }

  if (meta) {
    meta.replaceChildren(
      profileMetaItem("Published repos", String(groups.length)),
      profileMetaItem("Live hosts", String(repos.reduce((n, repo) => n + (repo.liveHost ? 1 : 0), 0))),
      profileMetaItem("Account", "checking")
    );
    const account = await loadProfileAccount(owner);
    if (currentProfile !== owner || !meta) return;
    meta.lastElementChild.replaceWith(
      profileMetaItem("Account", account ? text(account.status, "registered") : "not registered")
    );
  }
}

// Render just the Code tab's file view for a route (root, a tree dir, or a blob).
function renderRepoPath(owner, name, mode, filePath) {
  showRepoTab("code");
  fileState = { owner, name, dir: "" };
  if (codeTitle) codeTitle.textContent = `${owner}/${name}`;
  if (mode === "blob" && filePath) {
    const cut = filePath.lastIndexOf("/");
    fileState.dir = cut === -1 ? "" : filePath.slice(0, cut);
    showList();
    openBlob(filePath);
  } else {
    showList();
    openDir(mode === "tree" ? filePath : "");
  }
}

// Back/forward buttons (and our go() pushes) re-render from the URL.
window.addEventListener("popstate", route);

// Home/section links route back home via the History API (no full reload), then
// scroll to the requested section. Targeted so file rows / latest-commit aren't
// hijacked.
function goHome(fragment) {
  history.pushState({}, "", "/");
  route();
  if (fragment && fragment !== "#") {
    const el = document.querySelector(fragment);
    if (el) {
      el.scrollIntoView({ behavior: "smooth" });
      return;
    }
  }
  window.scrollTo(0, 0);
}
document.querySelectorAll(".brand, .back-link").forEach((a) =>
  a.addEventListener("click", (e) => {
    e.preventDefault();
    goHome("");
  })
);
document.querySelectorAll('header nav a[href^="#"]').forEach((a) =>
  a.addEventListener("click", (e) => {
    e.preventDefault();
    goHome(a.getAttribute("href"));
  })
);
document.querySelectorAll('header nav a[href^="/#"], .install-link[href^="/#"]').forEach((a) =>
  a.addEventListener("click", (e) => {
    e.preventDefault();
    goHome(a.getAttribute("href").slice(1));
  })
);

// ---- Download the whole repo into the browser (localStorage) with progress --

const downloadBtn = document.querySelector("#download-repo");
const downloadProgress = document.querySelector("#download-progress");
const downloadFill = document.querySelector("#download-bar-fill");
const downloadText = document.querySelector("#download-progress-text");
let downloading = false;

// Route on first load so deep links (e.g. /owner/name/blob/file) render.
route();

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
// The live client count comes from a single cached aggregate endpoint, polled on
// an interval and paused while the tab is hidden. This replaces a per-visitor
// observer WebSocket that kept the mainnode room's Durable Object resident in
// memory for the life of every open tab — the dominant Durable Object cost.
const STATS_PATH = "/api/network/stats";
const STATS_INTERVAL_MS = 30000;
let statsTimer = null;

function renderClients(online) {
  if (!clientsCount) return;
  clientsCount.textContent =
    online === 1 ? "1 client online" : `${online} clients online`;
  if (clientsDot) clientsDot.classList.toggle("online", online > 0);
}

async function pollClients() {
  if (!clientsCount) return;
  try {
    const response = await fetch(STATS_PATH, { headers: { accept: "application/json" } });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    renderClients(Number(data.clients) || 0);
  } catch (error) {
    clientsCount.textContent = "Reconnecting…";
    if (clientsDot) clientsDot.classList.remove("online");
  }
}

function startClients() {
  if (statsTimer !== null || !clientsCount) return;
  pollClients();
  statsTimer = setInterval(pollClients, STATS_INTERVAL_MS);
}

function stopClients() {
  if (statsTimer === null) return;
  clearInterval(statsTimer);
  statsTimer = null;
}

document.addEventListener("visibilitychange", () => {
  if (document.hidden) stopClients();
  else startClients();
});

loadCatalog();
startClients();
