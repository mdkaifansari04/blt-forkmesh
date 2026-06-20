(() => {
  // Must match valid_node_name in cloudflare_worker/src/entry.py and the Qt
  // client: a single DNS-like label, lowercase, hyphens allowed, no underscores.
  const NAME_RE = /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/;
  const PAYOUT_PER_JOIN_BCH = 0.0001; // illustrative only — reward engine WIP
  const POLL_MS = 5000;

  const $ = (sel) => document.querySelector(sel);
  const isLive = location.protocol !== "file:";
  let nodeName = "";
  let payAddress = "";
  let statusTimer = null;

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
      const earn = (Math.max(nodes, 1) * PAYOUT_PER_JOIN_BCH).toFixed(8);
      $("#calc-earn").textContent = earn + " BCH";
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
  async function loadDonationAddress() {
    const { ok, body } = await api("/api/accounts/donation-address", {
      method: "POST",
      body: JSON.stringify({ nodeName }),
    });
    if (!ok) {
      $("#pay-addr").textContent = "Could not generate an address. Reload and retry.";
      return;
    }
    payAddress = body.address || "";
    $("#pay-addr").textContent = payAddress;
    $("#pay-amount").textContent = (body.amountBch || "0.00500000") + " BCH";
    // Linkify so a wallet app can pick it up.
    $("#pay-addr").innerHTML =
      '<a href="' + body.uri + '" style="color:inherit;">' + payAddress + "</a>";
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
    if (!ok) return;
    if (body.paid) {
      clearInterval(statusTimer);
      statusTimer = null;
      $("#pay-status").textContent = "Donation received!";
      $("#pay-status").className = "pay-status paid";
      setTimeout(() => showStep("step-account"), 600);
    } else {
      const got = ((Number(body.receivedSats) || 0) / 1e8).toFixed(8);
      $("#pay-status").textContent = "Waiting for your donation… (received " + got + " BCH)";
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

  // --- Wiring ----------------------------------------------------------------
  nameInput.addEventListener("input", validateName);
  nameInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !nameContinue.disabled) reserveName();
  });
  nameContinue.addEventListener("click", reserveName);
  $("#pay-copy").addEventListener("click", async () => {
    try { await navigator.clipboard.writeText(payAddress); } catch (_) {}
    const b = $("#pay-copy");
    const t = b.textContent;
    b.textContent = "Copied";
    setTimeout(() => (b.textContent = t), 1500);
  });
  $("#acct-create").addEventListener("click", createAccount);

  loadStats();
  setInterval(loadStats, 30000);
})();
