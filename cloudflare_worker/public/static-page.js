(() => {
  function applyTheme(mode) {
    const chosen = mode ||
      localStorage.getItem("forkmesh.dashboard.theme") ||
      localStorage.getItem("forkmesh.theme") ||
      (window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");



    document.documentElement.classList.toggle("dark", chosen === "dark");
    document.documentElement.classList.toggle("light", chosen !== "dark");
    document.documentElement.style.colorScheme =
      chosen === "dark" ? "dark" : "light";
    localStorage.setItem("forkmesh.theme", chosen);
    localStorage.setItem("forkmesh.dashboard.theme", chosen);
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
    document.querySelectorAll(selector).forEach((el) => {
      el.textContent = value;
    });
  }




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





  (function initMobileNav() {
    const header = document.querySelector(".site-header");
    const nav = header && header.querySelector(".global-nav");
    if (!header || !nav) return;
    const host = header.querySelector(".header-actions") ||
      header.querySelector(".header-inner");
    if (!host) return;
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "nav-toggle";
    toggle.setAttribute("aria-label", "Toggle navigation menu");
    toggle.setAttribute("aria-expanded", "false");
    toggle.innerHTML =
      '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 6h18M3 12h18M3 18h18"></path></svg>';
    host.appendChild(toggle);
    function setOpen(open) {
      header.classList.toggle("nav-open", open);
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    }
    toggle.addEventListener("click", (event) => {
      event.stopPropagation();
      setOpen(!header.classList.contains("nav-open"));
    });

    nav.addEventListener("click", (event) => {
      if (event.target.closest("a")) setOpen(false);
    });
    document.addEventListener("click", (event) => {
      if (header.classList.contains("nav-open") && !header.contains(event.target)) {
        setOpen(false);
      }
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape") setOpen(false);
    });
  })();





  function readStoredSession() {
    try {
      return JSON.parse(localStorage.getItem("forkmesh.session") || "null");
    } catch (error) {
      return null;
    }
  }

  function applyAdminLink(session) {
    const link = document.querySelector("[data-admin-link]");
    if (!link) return;
    let adminUrl = session && session.isAdmin ? (session.adminUrl || "") : "";
    if (adminUrl && session.nodeName && !/[?&]admin=/.test(adminUrl)) {
      adminUrl += (adminUrl.includes("?") ? "&" : "?") +
        "admin=" + encodeURIComponent(session.nodeName);
    }
    if (adminUrl) {
      link.href = adminUrl;
      link.removeAttribute("hidden");
    } else {
      link.href = "#";
      link.setAttribute("hidden", "");
    }
    return adminUrl;
  }

  async function hydrateAdminLink() {
    const session = readStoredSession();
    const adminUrl = applyAdminLink(session);


    if (session && session.isAdmin && !adminUrl && session.nodeName &&
        location.protocol !== "file:") {
      try {
        const response = await fetch(
          `/api/accounts/${encodeURIComponent(session.nodeName)}`,
          { headers: { accept: "application/json" } },
        );
        if (!response.ok) return;
        const body = await response.json();
        if (body && body.adminUrl) {
          const next = { ...session, isAdmin: Boolean(body.isAdmin), adminUrl: body.adminUrl };
          try { localStorage.setItem("forkmesh.session", JSON.stringify(next)); } catch (error) {}
          applyAdminLink(next);
        }
      } catch (error) {

      }
    }
  }

  hydrateAdminLink();

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


  const STATS_PATH = "/api/network/stats";
  const payoutWalletsEl = document.querySelector("#network-wallets");




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
      setText("#network-clients, [data-network-clients]", "—");
      if (reposEl) reposEl.textContent = "—";
      setText("[data-network-repos]", "—");
      if (hostsEl) hostsEl.textContent = "—";
      setText("[data-network-hosts]", "—");
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
      setText("#network-clients, [data-network-clients]", String(clients));
      if (reposEl) reposEl.textContent = String(repos);
      setText("[data-network-repos]", String(repos));
      if (hostsEl) hostsEl.textContent = String(hosts);
      setText("[data-network-hosts]", String(hosts));
      writeCachedStat("clients", clients);
      writeCachedStat("hosts", hosts);
      writeCachedStat("repos", repos);
      renderPayoutNodes(data.payoutNodes);
    } catch (error) {
      if (clientsCount) clientsCount.textContent = "Reconnecting…";
      if (clientsDot) clientsDot.classList.remove("online");
      renderPayoutNodes([]);
    }
  }





  function primeCachedStats() {
    if (location.protocol === "file:") return;
    const clients = readCachedStat("clients");
    const hosts = readCachedStat("hosts");
    const repos = readCachedStat("repos");
    if (clients !== null && hosts !== null) renderOnline(clients + hosts);
    if (clients !== null) setText("#network-clients, [data-network-clients]", String(clients));
    if (hosts !== null) setText("#network-hosts, [data-network-hosts]", String(hosts));
    if (repos !== null) setText("#network-repos, [data-network-repos]", String(repos));
  }

  primeCachedStats();
  pollStats();
})();
