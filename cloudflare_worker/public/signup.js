(() => {
  // Must match valid_node_name in cloudflare_worker/src/entry.py and the Qt
  // client: a single DNS-like label, lowercase, hyphens allowed, no underscores.
  const NAME_RE = /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/;

  const $ = (sel) => document.querySelector(sel);
  const isLive = location.protocol !== "file:";
  let nodeName = "";
  let nameOk = false;

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

  // --- Step 1: node name -----------------------------------------------------
  const nameInput = $("#node-name");
  const nameHint = $("#name-hint");
  const nameContinue = $("#name-continue");
  const termsAgree = $("#terms-agree");
  let availTimer = null;

  function setHint(text, cls) {
    nameHint.textContent = text;
    nameHint.className = "hint" + (cls ? " " + cls : "");
  }

  // Continue is enabled only when the name is valid/available and the user has
  // agreed to the Terms and Privacy.
  function updateContinue() {
    nameContinue.disabled = !(nameOk && termsAgree.checked);
  }

  function validateName() {
    const value = nameInput.value.trim().toLowerCase();
    nameOk = false;
    updateContinue();
    if (!value) {
      setHint("Lowercase letters, numbers and hyphens. Start with a letter, end with a letter or number. This name is public.", "");
      return;
    }
    if (!NAME_RE.test(value)) {
      setHint("Use lowercase letters, numbers and hyphens - start with a letter, end with a letter or number, no spaces or underscores.", "bad");
      return;
    }
    setHint("Checking availability…", "");
    clearTimeout(availTimer);
    availTimer = setTimeout(() => checkAvailability(value), 350);
  }

  async function checkAvailability(value) {
    if (!isLive) { setHint("Looks good (preview).", "good"); nameOk = true; updateContinue(); return; }
    try {
      const { body } = await api("/api/accounts/" + encodeURIComponent(value));
      if (body.exists && body.available === false) {
        setHint("That name is already taken - try another.", "bad");
        nameOk = false;
      } else {
        setHint("“" + value + "” is available.", "good");
        nameOk = true;
      }
    } catch (_) {
      setHint("Couldn’t check availability - you can still continue.", "");
      nameOk = true;
    }
    updateContinue();
  }

  async function reserveName() {
    const value = nameInput.value.trim().toLowerCase();
    if (!NAME_RE.test(value) || !termsAgree.checked) return;
    nameContinue.disabled = true;
    nameContinue.textContent = "Reserving…";
    const { ok, body } = await api("/api/accounts/reserve", {
      method: "POST",
      body: JSON.stringify({ nodeName: value }),
    });
    nameContinue.textContent = "Continue";
    if (!ok) {
      setHint(body.error === "node_name_taken"
        ? "That name was just taken - try another."
        : "Could not reserve that name. Please try again.", "bad");
      nameContinue.disabled = false;
      return;
    }
    nodeName = value;
    showStep("step-account");
  }

  // --- Step 2: create account ------------------------------------------------
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
        : "Could not create the account. Please try again.";
      hint.className = "hint bad";
      return;
    }
    $("#done-name").textContent = "Node “" + (body.nodeName || nodeName) +
      "” is registered. Check " + (body.email || email) +
      " to verify your address.";
    showStep("step-done");
  }

  // --- Wiring ----------------------------------------------------------------
  nameInput.addEventListener("input", validateName);
  nameInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !nameContinue.disabled) reserveName();
  });
  termsAgree.addEventListener("change", updateContinue);
  nameContinue.addEventListener("click", reserveName);
  $("#acct-create").addEventListener("click", createAccount);
})();
