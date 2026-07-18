(() => {
  const $ = (sel) => document.querySelector(sel);
  const hint = $("#login-hint");
  const btn = $("#login-btn");

  function setHint(text, cls) {
    hint.textContent = text || "";
    hint.className = "hint" + (cls ? " " + cls : "");
  }

  const DEMO_EMAIL = "demo@forkmesh.local";
  const DEMO_PASSWORD = "forkmesh-demo";

  // Where to land after a successful login. Only same-site paths are honored
  // (an absolute URL here would be an open redirect). Used by flows that bounce
  // through login, e.g. the desktop app's "Link this node to your account"
  // grant URL (adhoc #120), so they resume exactly where they left off.
  function nextPath() {
    let value = "";
    try {
      value = new URLSearchParams(location.search).get("next") || "";
    } catch (_) {}
    // Must be a same-origin path. Reject "//host" and "/\host" (browsers
    // normalize the backslash to "/", so "/\evil.com" resolves off-origin) and
    // confirm the resolved origin matches ours before honoring it.
    if (!value.startsWith("/") || value.startsWith("//") || value.startsWith("/\\")) {
      return "";
    }
    try {
      if (new URL(value, location.origin).origin !== location.origin) return "";
    } catch (_) {
      return "";
    }
    return value;
  }

  function demoLoginAllowed() {
    return location.hostname === "localhost" || location.hostname === "127.0.0.1";
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
        solana: body.solana || "",
        hasPayoutAddress: Boolean(body.hasPayoutAddress),
        sessionToken: body.sessionToken || "",
        avatarPng: body.avatarPng || "",
        avatarUpdatedAt: Number(body.avatarUpdatedAt) || 0,
        profileBio: body.profileBio || "",
        profileAbout: body.profileAbout || body.profileReadme || "",
        profileReadme: body.profileReadme || body.profileAbout || "",
        profileLocation: body.profileLocation || "",
        profileTimezone: body.profileTimezone || "",
        profileLinks: Array.isArray(body.profileLinks) ? body.profileLinks : [],
        profileFollowers: Number(body.followers) || 0,
        profileFollowing: Number(body.following) || 0,
        profileMirrorCount: Number(body.mirrorCount) || 0,
        kind: body.kind || "",
        owner: body.owner || "",
        nodes: Array.isArray(body.nodes) ? body.nodes : [],
        at: Date.now(),
      }));
      document.cookie = "forkmesh_session=1; Path=/; Max-Age=2592000; SameSite=Lax"
        + (location.protocol === "https:" ? "; Secure" : "");
    } catch (_) {}
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
    if (demoLoginAllowed() && email.toLowerCase() === DEMO_EMAIL && password === DEMO_PASSWORD) {
      storeSession({
        nodeName: "demo-node",
        email: DEMO_EMAIL,
        status: "active",
        pubkey: "",
        emailVerified: true,
        isAdmin: false,
      });
      setHint("Logged in with local demo credentials.", "good");
      setTimeout(() => (location.href = nextPath() || "/dashboard"), 500);
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
      setHint("Network error - please try again.", "bad");
      return;
    }
    btn.disabled = false;
    btn.textContent = "Log in";

    if (res.ok) {
      setHint("Logged in as “" + (body.nodeName || email) + "”.", "good");
      // Persist a minimal, non-secret session marker for the static site.
      storeSession(body);
      setTimeout(() => (location.href = nextPath() || "/"), 700);
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
        : body.error === "account_disabled"
          ? "This account has been disabled."
        : "Could not log in. Please try again.", "bad");
  }

  // If we were bounced here from a gated page (e.g. the admin dashboard, whose
  // short-lived HttpOnly cookie lapses well before the 30-day login session
  // does) and we still hold a valid account session, re-establish the admin
  // page cookie from that session token instead of forcing a password re-entry
  // (adhoc #163). On any failure we silently fall back to the normal form.
  async function resumeAdminSession() {
    const next = nextPath();
    if (!next) return;
    let session = null;
    try {
      session = JSON.parse(localStorage.getItem("forkmesh.session") || "null");
    } catch (_) {}
    if (!session || !session.isAdmin || !session.sessionToken) return;
    setHint("Resuming your session…", "");
    try {
      const res = await fetch("/api/accounts/admin-session", {
        method: "POST",
        headers: {
          "content-type": "application/json",
          accept: "application/json",
          authorization: "Bearer " + session.sessionToken,
        },
        body: JSON.stringify({ sessionToken: session.sessionToken }),
      });
      if (res.ok) {
        location.href = next;
        return;
      }
    } catch (_) {}
    setHint("", "");
  }
  resumeAdminSession();

  btn.addEventListener("click", login);
  if (demoLoginAllowed()) {
    const demoBox = $("#demo-credentials");
    const demoFill = $("#demo-fill-btn");
    if (demoBox) demoBox.style.display = "block";
    if (demoFill) {
      demoFill.addEventListener("click", () => {
        $("#email").value = DEMO_EMAIL;
        $("#password").value = DEMO_PASSWORD;
        setHint("Demo credentials filled. Click Log in.", "");
      });
    }
  }
  for (const id of ["email", "password", "totp"]) {
    $("#" + id).addEventListener("keydown", (e) => { if (e.key === "Enter") login(); });
  }
})();
