(() => {
  // Illustrative payout figures, mirrored from the signup funnel. The reward
  // engine is still WIP; these numbers estimate, they don't promise.
  const PAYOUT_PER_JOIN_SOL = 0.0001;
  // Mirror of TREASURY_SPLIT_NUMERATOR / TREASURY_SPLIT_DENOMINATOR in
  // cloudflare_worker/src/entry.py: half of each join donation funds ForkMesh's
  // servers, the other half is shared evenly across online payout nodes.
  const TREASURY_SHARE = 0.5;

  const $ = (sel) => document.querySelector(sel);
  const isLive = location.protocol !== "file:";
  let minDonationUsd = 0;
  let nodesOnline = 0;

  function setText(sel, value) {
    const el = $(sel);
    if (el) el.textContent = value;
  }

  function readSession() {
    try { return JSON.parse(localStorage.getItem("forkmesh.session") || "null"); }
    catch (_) { return null; }
  }

  async function api(path) {
    const res = await fetch(path, {
      headers: { accept: "application/json" },
    });
    let body = {};
    try { body = await res.json(); } catch (_) { /* ignore */ }
    return { ok: res.ok, status: res.status, body };
  }

  function money(n) {
    return "$" + n.toLocaleString(undefined,
      { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  }

  // Drop trailing zeros from a decimal SOL amount ("0.005000000" -> "0.005").
  function trimAmount(s) {
    s = String(s || "");
    return s.indexOf(".") >= 0 ? s.replace(/0+$/, "").replace(/\.$/, "") : s;
  }

  // Show exactly how a join donation is divided: half to ForkMesh's servers,
  // half split evenly across the mirror nodes online right now.
  function renderSplit() {
    const usd = minDonationUsd;
    const serversUsd = usd * TREASURY_SHARE;
    const nodesUsd = usd * (1 - TREASURY_SHARE);
    const online = Math.max(nodesOnline, 0);
    const perNode = online > 0 ? nodesUsd / online : 0;
    setText("#split-node-count", String(online));
    setText("#split-node-count-2", String(online));
    setText("#split-servers-usd",
      serversUsd > 0 ? "≈ " + money(serversUsd) : "infrastructure & relay");
    setText("#split-nodes-usd",
      nodesUsd > 0 ? "≈ " + money(nodesUsd) : "shared by uptime & data");
    setText("#split-per-node",
      online <= 0 ? "no nodes yet"
        : perNode > 0 ? "≈ " + money(perNode) : "...");
  }

  async function loadStats() {
    if (!isLive) return;
    try {
      const { ok, body } = await api("/api/network/stats");
      if (!ok) return;
      const nodes = Number(body.hosts) || 0;
      nodesOnline = nodes;
      setText("#calc-nodes", String(nodes));
      const earn = (Math.max(nodes, 1) * PAYOUT_PER_JOIN_SOL).toFixed(9);
      setText("#calc-earn", trimAmount(earn) + " SOL");
      const minUsd = Number(body.minUsd) || 0;
      if (minUsd > 0) minDonationUsd = minUsd;
      renderSplit();
    } catch (_) { /* leave placeholders */ }
  }

  function renderSession() {
    const session = readSession();
    const name = session && (session.nodeName || session.email);
    const el = $("#mp-node");
    if (!el) return;
    if (name) {
      el.textContent = session.nodeName
        ? "Signed in as node “" + session.nodeName + "”."
        : "Signed in as " + session.email + ".";
    } else {
      el.innerHTML = 'You are not signed in. <a href="/login">Log in</a> to link payouts to your node.';
    }
  }

  renderSession();
  loadStats();
  setInterval(loadStats, 30000);
})();
