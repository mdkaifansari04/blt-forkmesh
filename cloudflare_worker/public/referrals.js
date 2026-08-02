(() => {
  const boardRoot = () => document.getElementById("referral-board");
  const siteBoardRoot = () => document.getElementById("site-referral-board");
  const linkRoot = () => document.getElementById("referral-link-area");

  // Same acceptance rule as site-header.js: only real user accounts (never
  // node sessions) count as signed in on the website.
  function readSession() {
    try {
      const session = JSON.parse(localStorage.getItem("forkmesh.session") || "null");
      if (!session || !session.nodeName) return null;
      if (session.kind === "node") return null;
      if (session.kind === "user" || session.email) return session;
      return null;
    } catch (_) {
      return null;
    }
  }

  function referralLink(session) {
    return `${location.origin}/r/${encodeURIComponent(
      String(session.nodeName).toLowerCase(),
    )}`;
  }

  async function copyText(text, button) {
    try {
      await navigator.clipboard.writeText(text);
    } catch (_) {
      const area = document.createElement("textarea");
      area.value = text;
      area.setAttribute("readonly", "");
      area.style.position = "absolute";
      area.style.left = "-9999px";
      document.body.appendChild(area);
      area.select();
      try { document.execCommand("copy"); } catch (_) {}
      area.remove();
    }
    if (button) {
      const original = button.textContent;
      button.textContent = "Copied";
      setTimeout(() => { button.textContent = original; }, 1500);
    }
  }

  function plural(n, word) {
    return `${n} ${word}${n === 1 ? "" : "s"}`;
  }

  function renderBoard(rows) {
    const root = boardRoot();
    if (!root) return;
    root.innerHTML = "";
    if (!rows.length) {
      const empty = document.createElement("div");
      empty.className = "board-empty";
      empty.textContent = "No referrals counted yet - be the first on the board.";
      root.appendChild(empty);
      return;
    }
    const you = String(readSession()?.nodeName || "").toLowerCase();
    rows.forEach((row, i) => {
      const r = document.createElement("div");
      r.className = "board-row";
      const name = String(row.name || "user");
      if (you && name.toLowerCase() === you) r.dataset.you = "true";
      const rank = document.createElement("span");
      rank.className = "board-rank";
      rank.textContent = `${i + 1}`;
      const label = document.createElement("span");
      label.className = "board-name";
      label.textContent = name;
      const value = document.createElement("span");
      value.className = "board-value";
      value.textContent = `${plural(Number(row.clicks) || 0, "click")} · ${plural(
        Number(row.signups) || 0,
        "signup",
      )}`;
      r.append(rank, label, value);
      root.appendChild(r);
    });
  }

  async function loadBoard() {
    const root = boardRoot();
    if (!root) return;
    try {
      const res = await fetch("/api/referrals/leaderboard", {
        headers: { accept: "application/json" },
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await res.json();
      renderBoard(Array.isArray(data.board) ? data.board : []);
    } catch (_) {
      root.innerHTML =
        '<div class="board-empty">The leaderboard is unavailable right now.</div>';
    }
  }

  // Referring hostnames come from other people's Referer headers, so they are
  // rendered as plain text - never as a link the board could be spammed into
  // handing out.
  function renderSiteBoard(rows) {
    const root = siteBoardRoot();
    if (!root) return;
    root.innerHTML = "";
    if (!rows.length) {
      const empty = document.createElement("div");
      empty.className = "board-empty";
      empty.textContent = "No website referrals counted yet.";
      root.appendChild(empty);
      return;
    }
    rows.forEach((row, i) => {
      const r = document.createElement("div");
      r.className = "board-row";
      const rank = document.createElement("span");
      rank.className = "board-rank";
      rank.textContent = `${i + 1}`;
      const label = document.createElement("span");
      label.className = "board-name";
      label.textContent = String(row.host || "");
      const value = document.createElement("span");
      value.className = "board-value";
      value.textContent = plural(Number(row.visits) || 0, "visit");
      r.append(rank, label, value);
      root.appendChild(r);
    });
  }

  function renderSiteSummary(data) {
    const sub = document.getElementById("site-board-sub");
    if (!sub) return;
    const sites = Number(data.sites) || 0;
    const visits = Number(data.visits) || 0;
    sub.textContent = sites
      ? `${plural(sites, "site")} · ${plural(visits, "visit")} · top 10 by visits`
      : "Which sites link visitors to ForkMesh · top 10 by visits";
  }

  async function loadSiteBoard() {
    const root = siteBoardRoot();
    if (!root) return;
    try {
      const res = await fetch("/api/referrals/sites", {
        headers: { accept: "application/json" },
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await res.json();
      renderSiteBoard(Array.isArray(data.board) ? data.board : []);
      renderSiteSummary(data);
    } catch (_) {
      root.innerHTML =
        '<div class="board-empty">The referring-site board is unavailable right now.</div>';
    }
  }

  function renderLinkArea() {
    const root = linkRoot();
    if (!root) return;
    root.innerHTML = "";
    const session = readSession();
    if (!session) {
      const note = document.createElement("p");
      note.className = "referral-signed-out";
      const login = document.createElement("a");
      login.href = "/login";
      login.textContent = "Log in";
      const signup = document.createElement("a");
      signup.href = "/signup";
      signup.textContent = "create an account";
      note.append(login, " or ", signup, " to get your personal referral link.");
      root.appendChild(note);
      return;
    }
    const row = document.createElement("div");
    row.className = "referral-link-row";
    const input = document.createElement("input");
    input.type = "text";
    input.readOnly = true;
    input.value = referralLink(session);
    input.setAttribute("aria-label", "Your referral link");
    input.addEventListener("focus", () => input.select());
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = "Copy link";
    button.addEventListener("click", () => copyText(input.value, button));
    row.append(input, button);
    root.appendChild(row);
  }

  function boot() {
    renderLinkArea();
    if (location.protocol === "file:") {
      const preview = '<div class="board-empty">Preview only.</div>';
      const root = boardRoot();
      if (root) root.innerHTML = preview;
      const sites = siteBoardRoot();
      if (sites) sites.innerHTML = preview;
      return;
    }
    loadBoard();
    loadSiteBoard();
  }

  window.addEventListener("storage", (event) => {
    if (event.key === "forkmesh.session" || event.key === null) renderLinkArea();
  });

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", boot);
  } else {
    boot();
  }
})();
