(() => {
  const $ = (sel) => document.querySelector(sel);
  const hint = $("#login-hint");
  const btn = $("#login-btn");

  function setHint(text, cls) {
    hint.textContent = text || "";
    hint.className = "hint" + (cls ? " " + cls : "");
  }

  async function login() {
    const identifier = $("#identifier").value.trim();
    const password = $("#password").value;
    const totp = $("#totp").value.trim();
    if (!identifier || !password) {
      setHint("Enter your node name (or email) and password.", "bad");
      return;
    }
    btn.disabled = true;
    btn.textContent = "Logging in…";
    let res, body = {};
    try {
      res = await fetch("/api/accounts/login", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({ identifier, password, totp }),
      });
      body = await res.json();
    } catch (_) {
      btn.disabled = false; btn.textContent = "Log in";
      setHint("Network error — please try again.", "bad");
      return;
    }
    btn.disabled = false;
    btn.textContent = "Log in";

    if (res.ok) {
      setHint("Logged in as “" + (body.nodeName || identifier) + "”.", "good");
      // Persist a minimal, non-secret session marker for the static site.
      try {
        localStorage.setItem("forkmesh.session", JSON.stringify({
          nodeName: body.nodeName, email: body.email, at: Date.now(),
        }));
      } catch (_) {}
      setTimeout(() => (location.href = "/"), 700);
      return;
    }
    if (body.error === "bad_totp") {
      $("#totp-field").style.display = "block";
      setHint("Enter your authenticator code.", "");
      return;
    }
    setHint(
      body.error === "no_such_account" ? "No account found for that name or email."
        : body.error === "account_not_active" ? "That account hasn’t finished signup yet."
        : body.error === "bad_password" ? "Incorrect password."
        : "Could not log in. Please try again.", "bad");
  }

  btn.addEventListener("click", login);
  for (const id of ["identifier", "password", "totp"]) {
    $("#" + id).addEventListener("keydown", (e) => { if (e.key === "Enter") login(); });
  }
})();
