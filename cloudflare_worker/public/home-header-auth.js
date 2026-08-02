// Session-aware auth buttons for the home page's hero header. Every other
// page gets this for free from the shared site-header.js renderer, but the
// home page keeps its own hero header — so before this script a logged-in
// visitor still saw "Login / Get Started" on /, disagreeing with the
// dashboard, chat and marketing pages. Reads the same forkmesh.session
// marker the rest of the site uses and swaps the two hero buttons for
// Dashboard + account chip; re-renders on storage events so a login or
// logout in another open section is reflected here without a reload.
(() => {
  // Same acceptance rule as site-header.js / chat.js: a user session carries
  // kind "user" (or a legacy pre-kind session with an email); node sessions
  // never render as a logged-in website account.
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

  function makeAvatar(session) {
    const avatar = document.createElement("span");
    avatar.className = "home-auth-avatar";
    if (session.avatarPng) {
      const img = document.createElement("img");
      img.alt = "";
      img.src = String(session.avatarPng).startsWith("data:")
        ? session.avatarPng
        : "data:image/png;base64," + session.avatarPng;
      avatar.append(img);
    } else {
      avatar.textContent = String(session.nodeName || "?").charAt(0).toUpperCase();
    }
    return avatar;
  }

  function render() {
    const secondary = document.querySelector("[data-home-auth-secondary]");
    const primary = document.querySelector("[data-home-auth-primary]");
    if (!secondary || !primary) return;
    const session = readSession();
    if (!session) {
      secondary.textContent = "Login";
      secondary.href = "/login";
      primary.textContent = "Get Started";
      primary.href = "/signup";
      return;
    }
    // Account name is DOM-built (never innerHTML) so a stored name can't
    // inject markup.
    secondary.textContent = "Dashboard";
    secondary.href = "/dashboard";
    primary.textContent = "";
    primary.append(makeAvatar(session));
    const label = document.createElement("span");
    label.textContent = String(session.nodeName).slice(0, 32);
    primary.append(label);
    primary.href = "/dashboard/settings";
    primary.title = "Account settings";
  }

  render();
  window.addEventListener("storage", (event) => {
    if (event.key === "forkmesh.session" || event.key === null) render();
  });
})();
