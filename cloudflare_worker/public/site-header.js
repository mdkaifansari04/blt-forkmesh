// Universal site header, mounted wherever a page places
// <div data-forkmesh-header="simple"></div>. One renderer for every page
// (except the home page, which keeps its own hero header): brand, the primary
// pages, a "More" menu for the rest, and a session-aware account area — a
// logged-in visitor sees their account chip (Dashboard / Profile / Log out)
// instead of the old hardcoded "Sign Up / Log In" links.
(() => {
  const NAV_HTML = `
    <nav class="forkmesh-simple-header-nav" aria-label="Primary">
      <a href="/docs">Docs</a>
      <a href="/chat">Chat</a>
      <a href="/network">Network</a>
      <a href="/pricing">Pricing</a>
      <a href="/blog">Blog</a>
      <a href="/status">Status</a>
      <div class="fm-header-menu">
        <button type="button" class="fm-header-menu-btn" aria-haspopup="true" aria-expanded="false">More <span aria-hidden="true">▾</span></button>
        <div class="fm-header-dropdown" hidden>
          <a href="/features">Features</a>
          <a href="/desktop">Desktop app</a>
          <a href="/about">About</a>
          <a href="/changelog">Changelog</a>
          <a href="/careers">Careers</a>
          <a href="/press">Press</a>
          <a href="/mirror-payouts">Mirror payouts</a>
          <a href="/security-report">Security</a>
          <a href="/privacy">Privacy</a>
          <a href="/terms">Terms</a>
        </div>
      </div>
    </nav>
  `;

  const HEADER_HTML = `
    <header class="forkmesh-simple-header">
      <a href="/" class="brand forkmesh-simple-brand" aria-label="ForkMesh home">
        <img class="brand-mark forkmesh-simple-brand-mark" src="/assets/logo.png" alt="" aria-hidden="true" />
        <span class="forkmesh-simple-brand-name">ForkMesh</span>
      </a>
      ${NAV_HTML}
      <div class="fm-header-right">
        <div class="fm-header-account"></div>
        <button type="button" class="fm-header-burger" aria-label="Open menu" aria-expanded="false">
          <span></span><span></span><span></span>
        </button>
      </div>
    </header>
    <div class="fm-header-mobile" hidden></div>
  `;

  // Static markup only — user data never goes through innerHTML.
  const SIGNED_OUT_HTML = `
    <a class="fm-header-login" href="/login">Login</a>
    <a class="fm-header-signup" href="/signup">Sign Up</a>
  `;

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

  function logout() {
    try {
      localStorage.removeItem("forkmesh.session");
    } catch (_) {}
    // Clear the presence cookie too — the Worker 302s / to the dashboard while
    // it is set, so a logout from a marketing page must drop it or the
    // homepage would keep redirecting.
    document.cookie = "forkmesh_session=; Path=/; Max-Age=0; SameSite=Lax";
    location.reload();
  }

  function makeAccountAvatar(session) {
    const avatar = document.createElement("span");
    avatar.className = "fm-header-avatar";
    const name = String(session.nodeName || "?");
    if (session.avatarPng) {
      const img = document.createElement("img");
      img.alt = "";
      img.src = String(session.avatarPng).startsWith("data:")
        ? session.avatarPng
        : "data:image/png;base64," + session.avatarPng;
      avatar.append(img);
    } else {
      avatar.textContent = name.charAt(0).toUpperCase();
    }
    return avatar;
  }

  // Session-dependent right side. The signed-in branch is built with DOM
  // methods (never innerHTML) so a stored account name can't inject markup.
  function buildAccountArea(container, session, options) {
    const stacked = Boolean(options && options.stacked);
    container.textContent = "";
    if (!session) {
      container.innerHTML = SIGNED_OUT_HTML;
      return;
    }
    const name = String(session.nodeName).slice(0, 32);
    if (stacked) {
      const dash = document.createElement("a");
      dash.href = "/dashboard";
      dash.textContent = "Dashboard";
      const profile = document.createElement("a");
      profile.href = "/@" + encodeURIComponent(name.toLowerCase());
      profile.textContent = "Public profile (@" + name + ")";
      const edit = document.createElement("a");
      edit.href = "/dashboard/settings";
      edit.textContent = "Edit profile";
      const out = document.createElement("button");
      out.type = "button";
      out.textContent = "Log out";
      out.addEventListener("click", logout);
      container.append(dash, profile, edit, out);
      return;
    }
    const wrap = document.createElement("div");
    wrap.className = "fm-header-menu fm-header-account-menu";
    const chip = document.createElement("button");
    chip.type = "button";
    chip.className = "fm-header-account-chip";
    chip.setAttribute("aria-haspopup", "true");
    chip.setAttribute("aria-expanded", "false");
    chip.append(makeAccountAvatar(session));
    const label = document.createElement("span");
    label.className = "fm-header-account-name";
    label.textContent = name;
    chip.append(label);
    const dropdown = document.createElement("div");
    dropdown.className = "fm-header-dropdown fm-header-dropdown-right";
    dropdown.hidden = true;
    const dash = document.createElement("a");
    dash.href = "/dashboard";
    dash.textContent = "Dashboard";
    // Both sides of the profile: /@name is the page everyone else sees;
    // the dashboard's profile section is the private, editable view
    // (avatar, bio, email, account status).
    const profile = document.createElement("a");
    profile.href = "/@" + encodeURIComponent(name.toLowerCase());
    profile.textContent = "Public profile";
    const edit = document.createElement("a");
    edit.href = "/dashboard/settings";
    edit.textContent = "Edit profile";
    const out = document.createElement("button");
    out.type = "button";
    out.textContent = "Log out";
    out.addEventListener("click", logout);
    dropdown.append(dash, profile, edit, out);
    wrap.append(chip, dropdown);
    container.append(wrap);
  }

  function markCurrentPage(scope) {
    const path = location.pathname.replace(/\.html$/, "").replace(/\/$/, "") || "/";
    scope.querySelectorAll("a[href]").forEach((link) => {
      const href = link.getAttribute("href");
      if (href !== "/" && (path === href || path.startsWith(href + "/"))) {
        link.setAttribute("aria-current", "page");
      }
    });
  }

  function wireDropdown(menu) {
    const btn = menu.querySelector("button");
    const dropdown = menu.querySelector(".fm-header-dropdown");
    if (!btn || !dropdown) return;
    btn.addEventListener("click", (event) => {
      event.stopPropagation();
      const open = dropdown.hidden;
      document.querySelectorAll(".fm-header-dropdown").forEach((el) => {
        el.hidden = true;
      });
      dropdown.hidden = !open;
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });
  }

  function mountHeader(mount) {
    const holder = document.createElement("div");
    holder.innerHTML = HEADER_HTML;
    const header = holder.querySelector("header");
    const mobile = holder.querySelector(".fm-header-mobile");
    mount.replaceWith(header, mobile);

    const session = readSession();
    buildAccountArea(header.querySelector(".fm-header-account"), session);

    // Mobile panel: the primary links + the "More" links + the account rows,
    // stacked. Cloned from the same nav so the two stay in sync.
    const nav = header.querySelector(".forkmesh-simple-header-nav");
    nav.querySelectorAll(":scope > a, .fm-header-dropdown > a").forEach((link) => {
      mobile.append(link.cloneNode(true));
    });
    const mobileAccount = document.createElement("div");
    mobileAccount.className = "fm-header-mobile-account";
    buildAccountArea(mobileAccount, session, { stacked: true });
    mobile.append(mobileAccount);

    markCurrentPage(header);
    markCurrentPage(mobile);
    header.querySelectorAll(".fm-header-menu").forEach(wireDropdown);

    const burger = header.querySelector(".fm-header-burger");
    burger.addEventListener("click", (event) => {
      event.stopPropagation();
      const open = mobile.hidden;
      mobile.hidden = !open;
      burger.setAttribute("aria-expanded", open ? "true" : "false");
      burger.classList.toggle("is-open", open);
    });

    document.addEventListener("click", (event) => {
      document.querySelectorAll(".fm-header-dropdown").forEach((dropdown) => {
        if (!dropdown.hidden && !dropdown.parentElement.contains(event.target)) {
          dropdown.hidden = true;
        }
      });
    });
  }

  document.querySelectorAll("[data-forkmesh-header]").forEach(mountHeader);
})();
