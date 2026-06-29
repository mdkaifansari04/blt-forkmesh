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

  // The last-known counts are cached so the header pill and stat cards render
  // instantly on a cold load instead of sitting at "Checking…"/"—" until the
  // first poll returns (stale-while-revalidate). Values are non-negative numbers.
  function readCachedStat(key) {
    try {
      const raw = localStorage.getItem(`forkmesh.stats.${key}`);
      if (raw === null || raw === "") return null;
      const n = Number(raw);
      return Number.isFinite(n) && n >= 0 ? n : null;
    } catch (error) {
      return null;
    }
  }
  function writeCachedStat(key, value) {
    try {
      localStorage.setItem(`forkmesh.stats.${key}`, String(value));
    } catch (error) {
      /* storage disabled or quota exceeded — caching is best-effort */
    }
  }

  function shortWallet(address) {
    if (!address) return "No payout wallet";
    return address.length > 16 ? `${address.slice(0, 8)}…${address.slice(-6)}` : address;
  }

  function eligibilityLabel(node) {
    if (node.payoutEligible) return "Eligible";
    if (node.eligibilityReason === "offline") return "Offline";
    if (node.eligibilityReason === "duplicate_wallet") return "Duplicate wallet";
    return "No wallet";
  }

  function renderPayoutNodes(nodes) {
    const root = document.querySelector("#network-wallets");
    if (!root) return;
    const list = Array.isArray(nodes) ? nodes : [];
    if (!list.length) {
      root.innerHTML = '<div class="online-graph-empty">No active payout wallets yet.</div>';
      return;
    }
    root.innerHTML = "";
    const headings = ["Node", "Wallet", "Balance", "Payouts"];
    const heading = document.createElement("div");
    heading.className = "wallet-row wallet-heading";
    headings.forEach((text) => {
      const cell = document.createElement("div");
      cell.textContent = text;
      heading.appendChild(cell);
    });
    root.appendChild(heading);
    list.forEach((node) => {
      const row = document.createElement("div");
      row.className = "wallet-row";

      const name = document.createElement("div");
      name.className = "wallet-node";
      name.textContent = node.name || "node";
      row.appendChild(name);

      const wallet = document.createElement("div");
      wallet.className = "wallet-address";
      wallet.textContent = shortWallet(node.wallet || "");
      if (node.wallet) wallet.title = node.wallet;
      row.appendChild(wallet);

      const balance = document.createElement("div");
      balance.className = "wallet-balance";
      balance.textContent = node.balanceSol ? `${node.balanceSol} SOL` : "Unavailable";
      row.appendChild(balance);

      const status = document.createElement("div");
      status.className = "wallet-status";
      status.dataset.eligible = node.payoutEligible ? "true" : "false";
      status.textContent = eligibilityLabel(node);
      row.appendChild(status);

      root.appendChild(row);
    });
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
  const payoutWalletsEl = document.querySelector("#network-wallets");

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
      renderPayoutNodes([]);
      return;
    }
    try {
      const statsPath = payoutWalletsEl ? `${STATS_PATH}?payouts=1` : STATS_PATH;
      const response = await fetch(statsPath, { headers: { accept: "application/json" } });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      const clients = Number(data.clients) || 0;
      const hosts = Number(data.hosts) || 0;
      const repos = Number(data.repos) || 0;
      renderOnline(clients + hosts);
      setText("#network-clients", String(clients));
      if (reposEl) reposEl.textContent = String(repos);
      if (hostsEl) hostsEl.textContent = String(hosts);
      writeCachedStat("clients", clients);
      writeCachedStat("hosts", hosts);
      writeCachedStat("repos", repos);
      renderPayoutNodes(data.payoutNodes);
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

  // Render the last-known counts immediately (stale-while-revalidate) so the
  // header pill and stat cards aren't blank "Checking…"/"—" placeholders on a
  // cold load; the poll below refreshes them as soon as live data arrives.
  function primeCachedStats() {
    if (location.protocol === "file:") return;
    const clients = readCachedStat("clients");
    const hosts = readCachedStat("hosts");
    const repos = readCachedStat("repos");
    if (clients !== null && hosts !== null) renderOnline(clients + hosts);
    if (clients !== null) setText("#network-clients", String(clients));
    if (hosts !== null) setText("#network-hosts", String(hosts));
    if (repos !== null) setText("#network-repos", String(repos));
  }

  // Don't poll while the tab is hidden; resume (and refresh immediately) on focus.
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) stopStats();
    else startStats();
  });

  primeCachedStats();
  startStats();
})();
