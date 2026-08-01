







(() => {



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
