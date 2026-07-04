(() => {
  // Must match valid_node_name in cloudflare_worker/src/entry.py and the Qt
  // client: a single DNS-like label, lowercase, hyphens allowed, no underscores.
  const NAME_RE = /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/;

  const $ = (sel) => document.querySelector(sel);
  const isLive = location.protocol !== "file:";
  let nameOk = false;
  let availTimer = null;

  const form = $("#signup-form");
  const nameInput = $("#node-name");
  const emailInput = $("#acct-email");
  const passwordInput = $("#acct-pass");
  const termsAgree = $("#terms-agree");
  const createButton = $("#signup-create");
  const nameHint = $("#name-hint");
  const signupHint = $("#signup-hint");

  function setNameHint(text, cls) {
    // Only surface this hint for an actual error; success/neutral states stay quiet.
    nameHint.textContent = cls === "bad" ? text : "";
    nameHint.className = "hint" + (cls ? " " + cls : "");
  }

  function setSignupHint(text, cls) {
    signupHint.textContent = text || "";
    signupHint.className = "hint" + (cls ? " " + cls : "");
  }

  function validEmail(value) {
    return /.+@.+\..+/.test(value);
  }

  function updateCreateState() {
    const email = emailInput.value.trim();
    const password = passwordInput.value;
    createButton.disabled = !(nameOk && validEmail(email) && password.length >= 8 && termsAgree.checked);
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

  function storeSession(body) {
    try {
      localStorage.setItem("forkmesh.session", JSON.stringify({
        nodeName: body.nodeName,
        email: body.email,
        status: body.status,
        pubkey: body.pubkey,
        emailVerified: Boolean(body.emailVerified),
        isAdmin: Boolean(body.isAdmin),
        adminUrl: body.adminUrl || "",
        hasPayoutAddress: Boolean(body.hasPayoutAddress),
        at: Date.now(),
      }));
      document.cookie = "forkmesh_session=1; Path=/; Max-Age=2592000; SameSite=Lax";
    } catch (_) {}
  }

  function validateName() {
    const value = nameInput.value.trim().toLowerCase();
    nameInput.value = value;
    nameOk = false;
    updateCreateState();
    if (!value) {
      setNameHint("Lowercase letters, numbers and hyphens. Start with a letter, end with a letter or number. Your username is public.", "");
      return;
    }
    if (!NAME_RE.test(value)) {
      setNameHint("Use lowercase letters, numbers and hyphens - start with a letter, end with a letter or number, no spaces or underscores.", "bad");
      return;
    }
    setNameHint("Checking availability…", "");
    clearTimeout(availTimer);
    availTimer = setTimeout(() => checkAvailability(value), 300);
  }

  async function checkAvailability(value) {
    if (!isLive) {
      setNameHint("Looks good (preview).", "good");
      nameOk = true;
      updateCreateState();
      return;
    }
    try {
      const { body } = await api("/api/accounts/" + encodeURIComponent(value));
      if (body.exists && body.available === false) {
        setNameHint("That username is already taken - try another.", "bad");
        nameOk = false;
      } else {
        setNameHint("“" + value + "” is available.", "good");
        nameOk = true;
      }
    } catch (_) {
      setNameHint("Couldn’t check availability - you can still create the account.", "");
      nameOk = true;
    }
    updateCreateState();
  }

  async function createAccount(event) {
    event.preventDefault();
    const nodeName = nameInput.value.trim().toLowerCase();
    const email = emailInput.value.trim();
    const password = passwordInput.value;
    setSignupHint("", "");
    if (!NAME_RE.test(nodeName)) {
      setNameHint("Choose a valid username first.", "bad");
      return;
    }
    if (!validEmail(email)) {
      setSignupHint("Enter a valid email address.", "bad");
      return;
    }
    if (password.length < 8) {
      setSignupHint("Password must be at least 8 characters.", "bad");
      return;
    }
    if (!termsAgree.checked) {
      setSignupHint("Accept the Terms and Privacy policy to continue.", "bad");
      return;
    }

    createButton.disabled = true;
    createButton.textContent = "Creating…";
    const { ok, body } = await api("/api/accounts/signup", {
      method: "POST",
      body: JSON.stringify({ nodeName, email, password }),
    });
    createButton.textContent = "Create account";
    if (!ok) {
      createButton.disabled = false;
      setSignupHint(
        body.error === "node_name_taken" ? "That username was just taken - try another."
          : body.error === "email_taken" ? "That email is already registered."
          : body.error === "password_too_short" ? "Password must be at least 8 characters."
          : "Could not create the account. Please try again.",
        "bad");
      return;
    }
    storeSession(body);
    setSignupHint("Account created. Opening your dashboard…", "good");
    window.setTimeout(() => { location.href = "/dashboard"; }, 500);
  }

  nameInput.addEventListener("input", validateName);
  emailInput.addEventListener("input", updateCreateState);
  passwordInput.addEventListener("input", updateCreateState);
  termsAgree.addEventListener("change", updateCreateState);
  form.addEventListener("submit", createAccount);
})();
