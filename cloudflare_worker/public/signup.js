(() => {
  // Must match valid_node_name in cloudflare_worker/src/entry.py and the Qt
  // client: a single DNS-like label, lowercase, hyphens allowed, no underscores.
  const NAME_RE = /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/;
  const PAYOUT_PER_JOIN_SOL = 0.0001; // illustrative only — reward engine WIP
  const POLL_MS = 5000;
  const ADDRESS_DELETE_GRACE_MS = 5 * 60 * 1000;

  const $ = (sel) => document.querySelector(sel);
  const isLive = location.protocol !== "file:";
  let nodeName = "";
  let payAddress = "";
  let payUri = "";       // full Solana Pay URI (with label/message) for the copy link
  let payQrUri = "";     // compact URI for the QR, so it fits a small QR version
  let expiresAt = 0;
  let deleteAt = 0;
  let lastReceivedLamports = 0;
  let addressHidden = false;
  let statusTimer = null;
  let expiryTimer = null;
  let solUsd = 0;                 // SOL→USD spot price, 0 until fetched
  let currentDonationSol = 0;     // required donation for this signup, in SOL
  let currentDonationUsd = 0;     // server-computed USD value of the requirement

  function showStep(id) {
    for (const el of document.querySelectorAll(".step")) {
      el.classList.toggle("active", el.id === id);
    }
    window.scrollTo({ top: 0, behavior: "smooth" });
  }

  async function api(path, options) {
    const res = await fetch(path, {
      headers: { "content-type": "application/json", accept: "application/json" },
      ...options,
    });
    let body = {};
    try { body = await res.json(); } catch (_) { /* ignore */ }
    return { ok: res.ok, status: res.status, body };
  }

  // --- Network stats + earnings estimate -------------------------------------
  async function loadStats() {
    if (!isLive) return;
    try {
      const { body } = await api("/api/network/stats");
      const nodes = Number(body.hosts) || 0;
      $("#stat-nodes").textContent = String(nodes);
      $("#stat-repos").textContent = String(Number(body.repos) || 0);
      $("#stat-clients").textContent = String(Number(body.clients) || 0);
      $("#calc-nodes").textContent = String(nodes);
      const earn = (Math.max(nodes, 1) * PAYOUT_PER_JOIN_SOL).toFixed(9);
      $("#calc-earn").textContent = earn + " SOL";
      // Live minimum donation (~$1), computed server-side from the SOL price.
      const minSol = Number(body.minSol) || 0;
      const minUsd = Number(body.minUsd) || 0;
      if (Number(body.solUsd) > 0) solUsd = Number(body.solUsd);
      const minSolEl = $("#min-donation-sol");
      if (minSolEl && minSol > 0) minSolEl.textContent = trimAmount(minSol.toFixed(9)) + " SOL";
      const minUsdEl = $("#min-donation-usd");
      if (minUsdEl) {
        minUsdEl.textContent = minUsd > 0
          ? "≈ $" + minUsd.toLocaleString(undefined,
              { minimumFractionDigits: 2, maximumFractionDigits: 2 })
          : "";
      }
    } catch (_) { /* leave placeholders */ }
  }

  // --- Step 1: node name -----------------------------------------------------
  const nameInput = $("#node-name");
  const nameHint = $("#name-hint");
  const nameContinue = $("#name-continue");
  let availTimer = null;

  function setHint(text, cls) {
    nameHint.textContent = text;
    nameHint.className = "hint" + (cls ? " " + cls : "");
  }

  function validateName() {
    const value = nameInput.value.trim().toLowerCase();
    nameContinue.disabled = true;
    if (!value) {
      setHint("Lowercase letters, numbers and hyphens. Start with a letter, end with a letter or number. This name is public.", "");
      return;
    }
    if (!NAME_RE.test(value)) {
      setHint("Use lowercase letters, numbers and hyphens — start with a letter, end with a letter or number, no spaces or underscores.", "bad");
      return;
    }
    setHint("Checking availability…", "");
    clearTimeout(availTimer);
    availTimer = setTimeout(() => checkAvailability(value), 350);
  }

  async function checkAvailability(value) {
    if (!isLive) { setHint("Looks good (preview).", "good"); nameContinue.disabled = false; return; }
    try {
      const { body } = await api("/api/accounts/" + encodeURIComponent(value));
      if (body.exists && body.available === false) {
        setHint("That name is already taken — try another.", "bad");
        nameContinue.disabled = true;
      } else {
        setHint("“" + value + "” is available.", "good");
        nameContinue.disabled = false;
      }
    } catch (_) {
      setHint("Couldn’t check availability — you can still continue.", "");
      nameContinue.disabled = false;
    }
  }

  async function reserveName() {
    const value = nameInput.value.trim().toLowerCase();
    if (!NAME_RE.test(value)) return;
    nameContinue.disabled = true;
    nameContinue.textContent = "Reserving…";
    const { ok, body } = await api("/api/accounts/reserve", {
      method: "POST",
      body: JSON.stringify({ nodeName: value }),
    });
    nameContinue.textContent = "Continue";
    if (!ok) {
      setHint(body.error === "node_name_taken"
        ? "That name was just taken — try another."
        : "Could not reserve that name. Please try again.", "bad");
      nameContinue.disabled = false;
      return;
    }
    nodeName = value;
    showStep("step-join");
    loadDonationAddress();
  }

  // --- Step 2: donation ------------------------------------------------------
  function stopStatusPolling() {
    if (!statusTimer) return;
    clearInterval(statusTimer);
    statusTimer = null;
  }

  function stopExpiryTimer() {
    if (!expiryTimer) return;
    clearInterval(expiryTimer);
    expiryTimer = null;
  }

  function setPayStatus(text, cls) {
    const el = $("#pay-status");
    el.textContent = text;
    el.className = "pay-status " + (cls || "waiting");
  }

  function setAddressVisible(visible) {
    const wrap = $("#pay-visible");
    if (wrap) wrap.hidden = !visible;
    const copy = $("#pay-copy");
    if (copy) copy.disabled = !visible || !payAddress;
  }

  function formatDuration(ms) {
    const total = Math.max(0, Math.ceil(ms / 1000));
    const hours = Math.floor(total / 3600);
    const minutes = Math.floor((total % 3600) / 60);
    const seconds = total % 60;
    if (hours > 0) return hours + "h " + String(minutes).padStart(2, "0") + "m";
    return minutes + ":" + String(seconds).padStart(2, "0");
  }

  function renderQr() {
    const qr = $("#pay-qr");
    if (!qr) return;
    qr.innerHTML = "";
    if (!payQrUri && !payUri && !payAddress) return;
    if (window.ForkMeshQR) {
      // Encode the compact URI: label/message bloat the payload past the QR's
      // capacity, and a wallet shows its own label anyway.
      window.ForkMeshQR.render(payQrUri || payUri || payAddress, qr, 236);
    } else {
      qr.textContent = "QR unavailable";
    }
  }

  // Drop trailing zeros from a decimal SOL amount ("0.005000000" -> "0.005").
  function trimAmount(s) {
    s = String(s || "");
    return s.indexOf(".") >= 0 ? s.replace(/0+$/, "").replace(/\.$/, "") : s;
  }

  function renderAddress(body) {
    payAddress = body.address || "";
    payUri = body.uri || payAddress;
    payQrUri = (payAddress && body.reference)
      ? "solana:" + payAddress + "?amount=" + trimAmount(body.amountSol) +
        "&reference=" + body.reference
      : payUri;
    addressHidden = false;
    setAddressVisible(true);
    $("#pay-renew").hidden = true;
    $("#pay-addr").textContent = "";
    const link = document.createElement("a");
    link.href = payUri || "#";
    link.style.color = "inherit";
    link.textContent = payAddress;
    $("#pay-addr").append(link);
    renderQr();
  }

  function hideExpiredAddress(deleted) {
    addressHidden = true;
    payAddress = "";
    payUri = "";
    payQrUri = "";
    setAddressVisible(false);
    const qr = $("#pay-qr");
    if (qr) qr.innerHTML = "";
    $("#pay-renew").hidden = false;
    setPayStatus(
      deleted
        ? "This payment request was removed. Generate a new request to continue."
        : "This payment request is expiring. Do not send SOL to it.",
      "waiting"
    );
    updateExpiryText();
  }

  function updateExpiryText() {
    const el = $("#pay-expiry");
    if (!el) return;
    if (!expiresAt) {
      el.textContent = "";
      el.className = "pay-expiry";
      return;
    }
    const now = Date.now();
    if (!addressHidden && lastReceivedLamports <= 0 && now >= expiresAt) {
      hideExpiredAddress(false);
      return;
    }
    if (addressHidden) {
      const remaining = Math.max(0, (deleteAt || now) - now);
      el.className = "pay-expiry danger";
      el.textContent = remaining > 0
        ? "We are expiring this payment request. Do not send SOL to it. It will be deleted in " + formatDuration(remaining) + "."
        : "This payment request has been removed from the page. Generate a new request to continue.";
      return;
    }
    const remaining = expiresAt - now;
    el.className = remaining <= 10 * 60 * 1000 ? "pay-expiry warn" : "pay-expiry";
    el.textContent = remaining > 0
      ? "This payment request expires in " + formatDuration(remaining) + " if it receives no transactions."
      : "";
  }

  function applyExpiry(body) {
    expiresAt = Number(body.expiresAt) || 0;
    deleteAt = Number(body.deleteAt) ||
      (expiresAt ? expiresAt + ADDRESS_DELETE_GRACE_MS : 0);
    updateExpiryText();
    if (expiresAt && !expiryTimer) expiryTimer = setInterval(updateExpiryText, 1000);
    if (!expiresAt) stopExpiryTimer();
  }

  async function loadDonationAddress(renewExpired) {
    const renew = Boolean(renewExpired);
    const renewBtn = $("#pay-renew");
    if (renewBtn) {
      renewBtn.disabled = true;
      renewBtn.textContent = "Generating…";
    }
    setPayStatus(renew ? "Generating a new payment request…" : "Generating payment request…", "waiting");
    const payload = { nodeName };
    if (renew) payload.renewExpired = true;
    const { ok, body } = await api("/api/accounts/donation-address", {
      method: "POST",
      body: JSON.stringify(payload),
    });
    if (renewBtn) {
      renewBtn.disabled = false;
      renewBtn.textContent = "Generate a new request";
    }
    if (!ok) {
      setAddressVisible(false);
      setPayStatus(body.error === "solana_rpc_unavailable"
        ? "Signup is temporarily unavailable because Solana payment verification is offline. Please try again later."
        : "Could not generate a payment request. Reload and retry.", "waiting");
      return;
    }
    lastReceivedLamports = Number(body.receivedLamports) || 0;
    $("#pay-amount").textContent = trimAmount(body.amountSol || "0") + " SOL";
    currentDonationSol = Number(body.amountSol) || 0;
    currentDonationUsd = Number(body.amountUsd) || 0;
    if (Number(body.solUsd) > 0) solUsd = Number(body.solUsd);
    renderUsd();
    applyExpiry(body);
    if (body.hidden || body.expired || body.deleted) {
      hideExpiredAddress(Boolean(body.deleted));
      startStatusPolling();
      return;
    }
    renderAddress(body);
    setPayStatus("Waiting for your donation…", "waiting");
    startStatusPolling();
  }

  function startStatusPolling() {
    if (!isLive || statusTimer) return;
    pollStatus();
    statusTimer = setInterval(pollStatus, POLL_MS);
  }

  async function pollStatus() {
    const { ok, body } = await api(
      "/api/accounts/donation-status?nodeName=" + encodeURIComponent(nodeName));
    if (!ok) {
      if (body.error === "solana_rpc_unavailable") {
        setPayStatus("Solana payment verification is temporarily offline. Please wait before sending SOL.", "waiting");
      }
      return;
    }
    lastReceivedLamports = Number(body.receivedLamports) || 0;
    applyExpiry(body);
    if (body.deleted) {
      hideExpiredAddress(true);
      stopStatusPolling();
      return;
    }
    if (body.hidden || body.expired) {
      hideExpiredAddress(false);
      return;
    }
    if (body.checking && !body.paid) {
      // RPC was momentarily unreachable; keep the address up and keep polling.
      setPayStatus("Checking the network for your donation…", "waiting");
      return;
    }
    if (body.paid) {
      stopStatusPolling();
      stopExpiryTimer();
      $("#pay-status").textContent = "Donation received!";
      $("#pay-status").className = "pay-status paid";
      setTimeout(() => showStep("step-account"), 600);
    } else {
      const got = (lastReceivedLamports / 1e9).toFixed(9);
      setPayStatus("Waiting for your donation… (received " + got + " SOL)", "waiting");
    }
  }

  // --- Step 3: create account ------------------------------------------------
  async function createAccount() {
    const email = $("#acct-email").value.trim();
    const password = $("#acct-pass").value;
    const hint = $("#acct-hint");
    if (!/.+@.+\..+/.test(email)) {
      hint.textContent = "Enter a valid email address.";
      hint.className = "hint bad";
      return;
    }
    if (password.length < 8) {
      hint.textContent = "Password must be at least 8 characters.";
      hint.className = "hint bad";
      return;
    }
    const btn = $("#acct-create");
    btn.disabled = true;
    btn.textContent = "Creating…";
    const { ok, body } = await api("/api/accounts/finalize", {
      method: "POST",
      body: JSON.stringify({ nodeName, email, password }),
    });
    btn.disabled = false;
    btn.textContent = "Create account";
    if (!ok) {
      hint.textContent = body.error === "email_taken"
        ? "That email is already registered."
        : body.error === "donation_required"
          ? "We haven’t confirmed your donation yet — please wait a moment."
          : "Could not create the account. Please try again.";
      hint.className = "hint bad";
      return;
    }
    $("#done-name").textContent = "Node “" + (body.nodeName || nodeName) + "” is registered.";
    showStep("step-done");
  }

  // --- SOL→USD price ---------------------------------------------------------
  function fmtUsd(sol) {
    if (!solUsd || !sol) return "";
    return "≈ $" + (sol * solUsd).toLocaleString(undefined, {
      minimumFractionDigits: 2, maximumFractionDigits: 2,
    });
  }

  function renderUsd() {
    // Prefer the server's authoritative USD value (derived from the live rate
    // and rounded up to ~$1); fall back to the client spot price for display.
    const usd = currentDonationUsd > 0
      ? "≈ $" + currentDonationUsd.toLocaleString(undefined,
          { minimumFractionDigits: 2, maximumFractionDigits: 2 })
      : fmtUsd(currentDonationSol);
    // The "Min. donation" stat card is owned by loadStats; here we only set the
    // USD value next to the amount the user is being asked to send.
    const pay = $("#pay-amount-usd");
    if (pay) pay.textContent = usd;
  }

  // The minimum is computed and enforced server-side; this is best-effort and
  // only used to show a $ value before the deposit address has been requested.
  async function loadSolPrice() {
    try {
      const res = await fetch(
        "https://api.coingecko.com/api/v3/simple/price?ids=solana&vs_currencies=usd");
      const j = await res.json();
      const p = Number(j && j.solana && j.solana.usd);
      if (p > 0) { solUsd = p; renderUsd(); }
    } catch (_) { /* price is best-effort; leave blank on failure */ }
  }

  // --- Wiring ----------------------------------------------------------------
  nameInput.addEventListener("input", validateName);
  nameInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !nameContinue.disabled) reserveName();
  });
  nameContinue.addEventListener("click", reserveName);
  $("#pay-copy").addEventListener("click", async () => {
    if (!payUri && !payAddress) return;
    try { await navigator.clipboard.writeText(payUri || payAddress); } catch (_) {}
    const b = $("#pay-copy");
    const t = b.textContent;
    b.textContent = "Copied";
    setTimeout(() => (b.textContent = t), 1500);
  });
  $("#pay-renew").addEventListener("click", () => loadDonationAddress(true));
  $("#acct-create").addEventListener("click", createAccount);

  loadStats();
  setInterval(loadStats, 30000);
  loadSolPrice();
  setInterval(loadSolPrice, 60000);
})();
