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
  const CLIENTS_PATH = "/api/repo/mainnode/forkmesh/rooms/general/clients";
  let clientsRetry = 0;

  function renderClients(online) {
    const label = online === 1 ? "1 client online" : `${online} clients online`;
    if (clientsCount) clientsCount.textContent = label;
    setText("#network-clients", String(online));
    if (clientsDot) clientsDot.classList.toggle("online", online > 0);
  }

  function scheduleReconnect() {
    if (!clientsCount) return;
    clientsCount.textContent = "Reconnecting…";
    if (clientsDot) clientsDot.classList.remove("online");
    const delay = Math.min(30000, 1000 * 2 ** clientsRetry);
    clientsRetry += 1;
    setTimeout(watchClients, delay);
  }

  function watchClients() {
    if (!clientsCount) return;
    if (location.protocol === "file:") {
      clientsCount.textContent = "Preview only";
      setText("#network-clients", "—");
      return;
    }
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

  async function loadNetworkStats() {
    const reposEl = document.querySelector("#network-repos");
    const hostsEl = document.querySelector("#network-hosts");
    if (!reposEl && !hostsEl) return;
    if (location.protocol === "file:") {
      if (reposEl) reposEl.textContent = "—";
      if (hostsEl) hostsEl.textContent = "—";
      return;
    }
    try {
      const response = await fetch("/api/repositories", { headers: { accept: "application/json" } });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      const repos = Array.isArray(data.repositories) ? data.repositories : [];
      if (reposEl) reposEl.textContent = String(repos.length);
      if (!hostsEl || !repos.length) {
        if (hostsEl) hostsEl.textContent = "0";
        return;
      }
      const checks = await Promise.allSettled(repos.slice(0, 80).map(async (repo) => {
        if (!repo || !repo.owner || !repo.name) return 0;
        const url = `/api/repo/${encodeURIComponent(repo.owner)}/${encodeURIComponent(repo.name)}/host`;
        const hostResponse = await fetch(url, { headers: { accept: "application/json" } });
        if (!hostResponse.ok) return 0;
        const hostData = await hostResponse.json();
        return Number(hostData.hosts) || 0;
      }));
      const live = checks.reduce((sum, result) => sum + (result.status === "fulfilled" && result.value > 0 ? 1 : 0), 0);
      hostsEl.textContent = String(live);
    } catch (error) {
      if (reposEl) reposEl.textContent = "0";
      if (hostsEl) hostsEl.textContent = "0";
    }
  }

  watchClients();
  loadNetworkStats();
})();
