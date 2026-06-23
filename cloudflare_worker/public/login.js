(() => {
  const $ = (sel) => document.querySelector(sel);
  const hint = $("#login-hint");
  const btn = $("#login-btn");

  function setHint(text, cls) {
    hint.textContent = text || "";
    hint.className = "hint" + (cls ? " " + cls : "");
  }

  async function login() {
    const email = $("#email").value.trim();
    const password = $("#password").value;
    const totp = $("#totp").value.trim();
    if (!email || !password) {
      setHint("Enter your email and password.", "bad");
      return;
    }
    if (!email.includes("@")) {
      setHint("Enter a valid email address.", "bad");
      return;
    }
    btn.disabled = true;
    btn.textContent = "Logging in…";
    let res, body = {};
    try {
      res = await fetch("/api/accounts/login", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({ email, password, totp }),
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
      setHint("Logged in as “" + (body.nodeName || email) + "”.", "good");
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
      // The relay returns one generic code for a bad email/password/unknown
      // account so attackers can't enumerate which accounts exist.
      body.error === "invalid_credentials" ? "Incorrect email or password."
        : body.error === "too_many_attempts"
          ? "Too many failed attempts. Wait a few minutes and try again."
        : "Could not log in. Please try again.", "bad");
  }

  btn.addEventListener("click", login);
  for (const id of ["email", "password", "totp"]) {
    $("#" + id).addEventListener("keydown", (e) => { if (e.key === "Enter") login(); });
  }
})();
