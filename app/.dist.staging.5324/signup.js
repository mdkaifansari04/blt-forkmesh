(() => {
  // Must match valid_node_name in app/src/entry.py and the desktop
  // client: a single DNS-like label, lowercase, hyphens allowed, no underscores.
  const NAME_RE = /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/;

  const $ = (sel) => document.querySelector(sel);
  const isLive = location.protocol !== "file:";

  function readSession() {
    try {
      const session = JSON.parse(localStorage.getItem("forkmesh.session") || "null");
      if (!session || !session.nodeName) return null;
      if (session.kind === "node") return null;
      if (session.kind === "user" || session.email) return session;
    } catch (_) {}
    return null;
  }

  // Organization invite handoff: the emailed link lands on
  // /signup?invite=<id>&org=<org>&token=<tok>. The token is the whole
  // authorization (holding it proves holding the mailbox), so the flow is:
  // signed in -> accept immediately; not signed in -> normal signup, then
  // accept with the fresh session cookie, then land on the org page.
  function inviteContext() {
    try {
      const params = new URLSearchParams(location.search);
      const invite = params.get("invite") || "";
      const org = (params.get("org") || "").toLowerCase();
      const token = params.get("token") || "";
      if (invite && token && NAME_RE.test(org)) return { invite, org, token };
    } catch (_) {}
    return null;
  }

  const orgInvite = inviteContext();

  function inviteAcceptPath(ctx) {
    return "/api/orgs/" + encodeURIComponent(ctx.org) + "/invitations/" +
      encodeURIComponent(ctx.invite) + "/accept?token=" +
      encodeURIComponent(ctx.token);
  }

  async function acceptInviteAndEnterOrg(ctx) {
    try {
      const res = await fetch(inviteAcceptPath(ctx), {
        method: "POST",
        headers: { accept: "application/json" },
      });
      if (res.ok) {
        location.href = "/" + ctx.org;
        return true;
      }
    } catch (_) {}
    return false;
  }

  // Already signed in: signup is not useful - send them into the app (via
  // the invite accept when one is present, so an existing account clicking
  // the emailed link still joins the org).
  if (readSession()) {
    if (orgInvite) {
      acceptInviteAndEnterOrg(orgInvite).then((joined) => {
        if (!joined) location.replace("/dashboard");
      });
      return;
    }
    location.replace("/dashboard");
    return;
  }

  // Referral attribution: /r/<name> bounces here with ?ref=<name>. Remember it
  // so the credit survives a detour (pricing, docs) before the form is sent.
  function referralCode() {
    let ref = "";
    try {
      ref = (new URLSearchParams(location.search).get("ref") || "").toLowerCase();
      if (ref && NAME_RE.test(ref)) {
        localStorage.setItem("forkmesh.referral", ref);
      } else {
        ref = localStorage.getItem("forkmesh.referral") || "";
      }
    } catch (_) {}
    return NAME_RE.test(ref) ? ref : "";
  }
  let nameOk = false;
  let availTimer = null;

  // Invite banner + email prefill: the GET preview mutates nothing (the
  // Worker's scanner defence) and returns org/role/inviter/email only to a
  // caller holding the emailed token.
  function primeInviteView() {
    if (!orgInvite || !isLive) return;
    const banner = $("#org-invite-banner");
    fetch(inviteAcceptPath(orgInvite), {
      headers: { accept: "application/json" },
    }).then(async (res) => {
      const body = await res.json().catch(() => ({}));
      if (!banner) return;
      if (res.ok && body.confirmationRequired) {
        banner.textContent =
          (body.inviter ? body.inviter + " invited you" : "You've been invited") +
          " to join the " + body.org + " organization" +
          (body.role ? " as " + body.role : "") + ". Create your account to accept.";
        banner.hidden = false;
        const emailField = $("#acct-email");
        if (emailField && !emailField.value && body.email) {
          emailField.value = body.email;
        }
      } else {
        banner.textContent =
          "This invitation link is no longer valid - you can still create an account.";
        banner.hidden = false;
      }
    }).catch(() => {});
  }
  primeInviteView();

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
        sessionToken: (
          location.protocol === "https:" &&
          body.sessionToken &&
          window.ForkMeshAPI?.isSameSite !== false
            ? "cookie"
            : body.sessionToken || ""
        ),
        kind: body.kind || "user",
        owner: body.owner || "",
        nodes: Array.isArray(body.nodes) ? body.nodes : [],
        at: Date.now(),
      }));
      document.cookie = "forkmesh_session=1; Path=/; Max-Age=2592000; SameSite=Lax"
        + (location.protocol === "https:" ? "; Secure" : "");
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
    let result = null;
    let created = false;
    try {
      result = await api("/api/accounts/signup", {
        method: "POST",
        body: JSON.stringify({ nodeName, email, password, ref: referralCode() }),
      });
      created = Boolean(result.ok);
    } catch (_) {
      setSignupHint("Network error - please try again.", "bad");
      return;
    } finally {
      if (!created) {
        createButton.disabled = false;
        createButton.textContent = "Create account";
      }
    }
    const { ok, body } = result;
    if (!ok) {
      setSignupHint(
        body.error === "node_name_taken" ? "That username was just taken - try another."
          : body.error === "inappropriate_node_name" ? "Choose a username without offensive language."
          : body.error === "email_taken" ? "That email is already registered."
          : body.error === "password_too_short" ? "Password must be at least 8 characters."
          // 503s from the router: the account was NOT created, and a quota
          // ceiling only clears at midnight UTC, so do not invite an
          // immediate retry that cannot succeed.
          : body.error === "database_quota_exceeded" ? "Signups are paused - the database is over its daily limit. Please try again later."
          : body.error === "database_unavailable" ? "The database is temporarily unavailable - please try again in a moment."
          : "Could not create the account. Please try again.",
        "bad");
      return;
    }
    storeSession(body);
    if (orgInvite) {
      // Signup came from an org invite link: join the org with the fresh
      // session cookie and land on the org page. The verification email is
      // already on its way regardless; the dashboard keeps nudging until
      // the address is confirmed.
      const joined = await acceptInviteAndEnterOrg(orgInvite);
      if (joined) return;
    }
    showVerifyView(nodeName, email);
  }

  // Popular webmail providers keyed by email domain, so the matching inbox
  // link is surfaced first on the "check your inbox" screen.
  const MAIL_PROVIDERS = {
    "gmail.com": "https://mail.google.com/",
    "googlemail.com": "https://mail.google.com/",
    "outlook.com": "https://outlook.live.com/mail/",
    "hotmail.com": "https://outlook.live.com/mail/",
    "live.com": "https://outlook.live.com/mail/",
    "yahoo.com": "https://mail.yahoo.com/",
    "proton.me": "https://mail.proton.me/",
    "protonmail.com": "https://mail.proton.me/",
    "icloud.com": "https://www.icloud.com/mail/",
    "me.com": "https://www.icloud.com/mail/",
    "aol.com": "https://mail.aol.com/",
  };

  function showVerifyView(nodeName, email) {
    const signupView = $("#signup-view");
    const verifyView = $("#verify-view");
    if (!verifyView) {
      // Fallback if markup is missing: land on the dashboard as before.
      location.href = "/dashboard";
      return;
    }
    $("#recap-name").textContent = nodeName;
    $("#recap-email").textContent = email;
    // Move the provider matching the user's email domain to the front.
    const domain = (email.split("@")[1] || "").toLowerCase();
    const inbox = MAIL_PROVIDERS[domain];
    const links = $("#mail-links");
    if (inbox && links) {
      const match = Array.prototype.find.call(
        links.querySelectorAll("a"), (a) => a.getAttribute("href") === inbox);
      if (match) links.insertBefore(match, links.firstChild);
    }
    if (signupView) signupView.hidden = true;
    verifyView.hidden = false;
    document.title = "Verify your email · BLT";
    window.scrollTo(0, 0);
  }

  nameInput.addEventListener("input", validateName);
  emailInput.addEventListener("input", updateCreateState);
  passwordInput.addEventListener("input", updateCreateState);
  termsAgree.addEventListener("change", updateCreateState);
  form.addEventListener("submit", createAccount);
})();
