(() => {
  function applyTheme(mode) {
    const chosen = mode || localStorage.getItem("forkmesh.theme") ||
      (window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
    document.documentElement.classList.toggle("dark", chosen === "dark");
    localStorage.setItem("forkmesh.theme", chosen);
  }

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

  function setText(selector, value) {
    const el = document.querySelector(selector);
    if (el) el.textContent = value;
  }

  applyTheme();

  const themeToggle = document.querySelector("#theme-toggle");
  if (themeToggle) {
    themeToggle.addEventListener("click", () => {
      applyTheme(document.documentElement.classList.contains("dark") ? "light" : "dark");
    });
  }

  const installCopy = document.querySelector("#install-copy");
  if (installCopy) {
    installCopy.addEventListener("click", () => {
      const cmd = document.querySelector("#install-cmd");
      copyText(cmd ? cmd.textContent : "", installCopy);
    });
  }

  const clientsCount = document.querySelector("#clients-count");
  const clientsDot = document.querySelector("#clients-dot");
  // Live counts come from a single cached aggregate endpoint, polled on an
  // interval. This replaces a per-visitor WebSocket (which pinned a Durable
  // Object in memory for the life of every open tab) and an 80-way per-visit
  // fan-out to each repo's host — both of which dominated Durable Object cost.
  const STATS_PATH = "/api/network/stats";
  const STATS_INTERVAL_MS = 30000;
  let statsTimer = null;

  // The header pill reflects everything that is live on the network — open host
  // tunnels plus chat clients — so it doesn't read "0 online" while a host is
  // clearly up. The per-metric breakdown stays in the network stat cards.
  function renderOnline(online) {
    const label = online === 1 ? "1 node online" : `${online} nodes online`;
    if (clientsCount) clientsCount.textContent = label;
    if (clientsDot) clientsDot.classList.toggle("online", online > 0);
  }

  async function pollStats() {
    const reposEl = document.querySelector("#network-repos");
    const hostsEl = document.querySelector("#network-hosts");
    if (location.protocol === "file:") {
      if (clientsCount) clientsCount.textContent = "Preview only";
      setText("#network-clients", "—");
      if (reposEl) reposEl.textContent = "—";
      if (hostsEl) hostsEl.textContent = "—";
      return;
    }
    try {
      const response = await fetch(STATS_PATH, { headers: { accept: "application/json" } });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      const clients = Number(data.clients) || 0;
      const hosts = Number(data.hosts) || 0;
      renderOnline(clients + hosts);
      setText("#network-clients", String(clients));
      if (reposEl) reposEl.textContent = String(Number(data.repos) || 0);
      if (hostsEl) hostsEl.textContent = String(hosts);
    } catch (error) {
      if (clientsCount) clientsCount.textContent = "Reconnecting…";
      if (clientsDot) clientsDot.classList.remove("online");
    }
  }

  function startStats() {
    if (statsTimer !== null) return;
    pollStats();
    statsTimer = setInterval(pollStats, STATS_INTERVAL_MS);
  }

  function stopStats() {
    if (statsTimer === null) return;
    clearInterval(statsTimer);
    statsTimer = null;
  }

  // Don't poll while the tab is hidden; resume (and refresh immediately) on focus.
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) stopStats();
    else startStats();
  });

  startStats();
})();
