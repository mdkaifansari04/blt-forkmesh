// Universal site header, mounted wherever a page places
// <div data-forkmesh-header="simple"></div>. One renderer for every page
// (except the home page, which keeps its own hero header), styled to match
// the dashboard chrome: a hamburger menu holding every site link (grouped,
// fully expanded), the brand mark with the live release version, the current
// page title, a "Get paid to mirror" shortcut, the theme toggle, and a
// session-aware account area — a logged-in visitor sees their account chip
// (Dashboard / Profile / Log out) instead of hardcoded "Sign Up / Log In".
(() => {
  // ---- Site-wide light/dark theme -----------------------------------------
  // The header is the one script every page already loads, so it doubles as
  // the theme engine: stamp html.light / html.dark from the visitor's saved
  // choice (same localStorage keys the dashboard and docs use) or, absent a
  // choice, their OS preference. styles.css keys its palettes off these
  // classes; pages without JavaScript simply stay dark.
  const THEME_KEYS = ["forkmesh.dashboard.theme", "forkmesh.theme"];

  function storedTheme() {
    try {
      for (const key of THEME_KEYS) {
        const value = localStorage.getItem(key);
        if (value === "light" || value === "dark") return value;
      }
    } catch (_) {}
    return "";
  }

  function resolveTheme() {
    const saved = storedTheme();
    if (saved) return saved;
    try {
      if (window.matchMedia("(prefers-color-scheme: light)").matches) return "light";
    } catch (_) {}
    return "dark";
  }

  function applySiteTheme(theme) {
    const light = theme === "light";
    const root = document.documentElement;
    root.classList.toggle("light", light);
    root.classList.toggle("dark", !light);
    root.style.colorScheme = light ? "light" : "dark";
    document.querySelectorAll(".fm-header-theme").forEach((button) => {
      button.textContent = light ? "☾" : "☀";
      button.setAttribute("aria-label",
                          light ? "Switch to dark mode" : "Switch to light mode");
      button.title = light ? "Switch to dark mode" : "Switch to light mode";
    });
  }

  function saveTheme(theme) {
    try {
      THEME_KEYS.forEach((key) => localStorage.setItem(key, theme));
    } catch (_) {}
    applySiteTheme(theme);
  }

  // Apply immediately at parse time (the script loads early on every page) so
  // light-mode visitors don't get a dark flash, and follow OS changes live
  // while the visitor hasn't made an explicit choice.
  applySiteTheme(resolveTheme());
  try {
    window.matchMedia("(prefers-color-scheme: light)")
      .addEventListener("change", () => {
        if (!storedTheme()) applySiteTheme(resolveTheme());
      });
  } catch (_) {}

  // Every site link lives in the hamburger menu, grouped and fully expanded —
  // no nested "More" submenu to open.
  const NAV_HTML = `
    <nav class="forkmesh-simple-header-nav" aria-label="Primary">
      <div class="fm-nav-group">
        <span class="fm-nav-group-title">Product</span>
        <a href="/features">Features</a>
        <a href="/desktop">Desktop app</a>
        <a href="/pricing">Pricing</a>
        <a href="/mirror-payouts">Mirror payouts</a>
      </div>
      <div class="fm-nav-group">
        <span class="fm-nav-group-title">Resources</span>
        <a href="/docs">Docs</a>
        <a href="/blog">Blog</a>
        <a href="/changelog">Changelog</a>
        <a href="/status">Status</a>
      </div>
      <div class="fm-nav-group">
        <span class="fm-nav-group-title">Community</span>
        <a href="/chat">Chat</a>
        <a href="/network">Network</a>
      </div>
      <div class="fm-nav-group">
        <span class="fm-nav-group-title">Company</span>
        <a href="/about">About</a>
        <a href="/careers">Careers</a>
        <a href="/press">Press</a>
      </div>
      <div class="fm-nav-group">
        <span class="fm-nav-group-title">Legal &amp; security</span>
        <a href="/security-report">Security</a>
        <a href="/privacy">Privacy</a>
        <a href="/terms">Terms</a>
      </div>
    </nav>
  `;

  const HEADER_HTML = `
    <header class="forkmesh-simple-header">
      <button type="button" class="fm-header-burger" aria-label="Open site menu" aria-haspopup="true" aria-expanded="false">
        <span></span><span></span><span></span>
      </button>
      <a href="/" class="forkmesh-simple-brand" aria-label="ForkMesh home">
        <img class="forkmesh-simple-brand-mark" src="/assets/logo.png" alt="" aria-hidden="true" />
        <span class="fm-header-version" hidden></span>
      </a>
      <span class="fm-header-context"></span>
      <div class="fm-header-right">
        <a class="fm-header-payout" href="/mirror-payouts" title="Get paid to mirror code" aria-label="Get paid to mirror code">
          <img src="/assets/sol.png" alt="" aria-hidden="true" />
          <span>Get paid to mirror</span>
        </a>
        <button type="button" class="fm-header-theme" aria-label="Switch color theme">☀</button>
        <div class="fm-header-account"></div>
      </div>
      <div class="fm-header-mobile" hidden>
        ${NAV_HTML}
        <div class="fm-nav-group">
          <span class="fm-nav-group-title">Account</span>
          <div class="fm-header-mobile-account"></div>
        </div>
      </div>
    </header>
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

  // Live release version pill next to the logo, same as the dashboard header
  // (and the same sessionStorage cache, so the two never disagree or double-
  // fetch). Best-effort: stay hidden if /api/version is unavailable.
  const APP_VERSION_STORAGE = "forkmesh.appVersion";
  const APP_VERSION_TTL_MS = 60 * 60 * 1000;

  async function renderAppVersion(el) {
    if (!el || location.protocol === "file:") return;
    const show = (version) => {
      el.textContent = version[0] === "v" ? version : "v" + version;
      el.hidden = false;
    };
    try {
      const cached = JSON.parse(sessionStorage.getItem(APP_VERSION_STORAGE) || "null");
      if (cached && cached.version && Number(cached.expiresAt) > Date.now()) {
        show(String(cached.version));
        return;
      }
    } catch (_) {}
    try {
      const response = await fetch("/api/version", { headers: { accept: "application/json" } });
      if (!response.ok) return;
      const data = await response.json();
      const version = (data && data.version ? String(data.version) : "").trim();
      if (!version) return;
      try {
        sessionStorage.setItem(APP_VERSION_STORAGE, JSON.stringify({
          version,
          expiresAt: Date.now() + APP_VERSION_TTL_MS,
        }));
      } catch (_) {}
      show(version);
    } catch (_) {}
  }

  function mountHeader(mount) {
    const holder = document.createElement("div");
    holder.innerHTML = HEADER_HTML;
    const header = holder.querySelector("header");
    const panel = header.querySelector(".fm-header-mobile");
    mount.replaceWith(header);

    const session = readSession();
    buildAccountArea(header.querySelector(".fm-header-account"), session);
    buildAccountArea(panel.querySelector(".fm-header-mobile-account"), session,
                     { stacked: true });

    markCurrentPage(header);

    // Current page title next to the brand, mirroring the dashboard's header
    // context ("Dashboard"). Prefer the menu link for this path; fall back to
    // the leading segment of the document title ("Pricing - ForkMesh").
    const context = header.querySelector(".fm-header-context");
    const current = panel.querySelector('a[aria-current="page"]');
    const fromTitle = (document.title || "").split(/\s+[·|\-–—]\s+/)[0].trim();
    context.textContent = current
      ? current.textContent.trim()
      : (fromTitle && fromTitle !== "ForkMesh" ? fromTitle : "");

    header.querySelectorAll(".fm-header-menu").forEach(wireDropdown);

    const themeButton = header.querySelector(".fm-header-theme");
    themeButton.addEventListener("click", () => {
      saveTheme(document.documentElement.classList.contains("light")
                ? "dark" : "light");
    });
    applySiteTheme(resolveTheme()); // sync the freshly-rendered button's icon

    const burger = header.querySelector(".fm-header-burger");
    const setMenuOpen = (open) => {
      panel.hidden = !open;
      burger.setAttribute("aria-expanded", open ? "true" : "false");
      burger.classList.toggle("is-open", open);
    };
    burger.addEventListener("click", (event) => {
      event.stopPropagation();
      setMenuOpen(panel.hidden);
    });

    document.addEventListener("click", (event) => {
      if (!panel.hidden && !header.contains(event.target)) setMenuOpen(false);
      document.querySelectorAll(".fm-header-dropdown").forEach((dropdown) => {
        if (!dropdown.hidden && !dropdown.parentElement.contains(event.target)) {
          dropdown.hidden = true;
        }
      });
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape") setMenuOpen(false);
    });

    renderAppVersion(header.querySelector(".fm-header-version"));
  }

  document.querySelectorAll("[data-forkmesh-header]").forEach(mountHeader);
})();
