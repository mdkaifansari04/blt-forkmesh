(() => {
  const $ = (selector) => document.querySelector(selector);
  const isLive = location.protocol !== "file:";

  function setText(selector, value) {
    const element = $(selector);
    if (element) element.textContent = value;
  }

  function readSession() {
    try {
      return JSON.parse(localStorage.getItem("forkmesh.session") || "null");
    } catch (_) {
      return null;
    }
  }

  async function api(path) {
    const response = await fetch(path, {
      headers: { accept: "application/json" },
      cache: "no-store",
    });
    let body = {};
    try {
      body = await response.json();
    } catch (_) {}
    return { ok: response.ok, body };
  }

  function trimSol(lamports) {
    const value = (Number(lamports || 0) / 1_000_000_000).toFixed(9);
    return value.replace(/0+$/, "").replace(/\.$/, "") || "0";
  }

  async function loadStatus() {
    if (!isLive) return;
    const [network, pool] = await Promise.allSettled([
      api("/api/network/stats"),
      api("/api/rewards/pool"),
    ]);
    if (network.status === "fulfilled" && network.value.ok) {
      setText("#calc-nodes", String(Number(network.value.body.hosts) || 0));
    }
    if (pool.status === "fulfilled" && pool.value.ok) {
      const state = pool.value.body || {};
      setText(
        "#calc-reward",
        `${trimSol(state.rewardAmountLamports)} SOL`,
      );
      setText("#pool-network", String(state.network || "mainnet-beta"));
      setText(
        "#pool-balance",
        `${String(state.balanceSol || trimSol(state.balanceLamports))} SOL`,
      );
      setText(
        "#pool-transfers",
        String(Array.isArray(state.transactions) ? state.transactions.length : 0),
      );
    }
  }

  function renderSession() {
    const session = readSession();
    const name = session && (session.nodeName || session.email);
    const element = $("#mp-node");
    if (!element) return;
    if (name) {
      element.textContent = session.nodeName
        ? `Signed in as node “${session.nodeName}”.`
        : `Signed in as ${session.email}.`;
    } else {
      element.innerHTML =
        'You are not signed in. <a href="/login">Log in</a> to manage a public payout address.';
    }
  }

  renderSession();
  loadStatus();
})();
