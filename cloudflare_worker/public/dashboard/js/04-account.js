  // Every top-level page is its own document now (/dashboard, /dashboard/repos,
  // /dashboard/network, ...) — navigation between them is a real page load via
  // plain <a href> links, so there is no client-side section router anymore.
  // The only client-routed state left is within-page: repo tabs/tree/blob on
  // the repo page, and the settings sub-tabs below.

  const SETTINGS_SECTIONS = ["public-profile", "account", "ssh-keys", "appearance", "notifications", "payout", "nodes", "organizations", "danger"];

  function normalizeSettingsSection(section) {
    return SETTINGS_SECTIONS.includes(section) ? section : "public-profile";
  }

  // The settings sub-tab addressed by the URL (/dashboard/settings/<tab>), so a
  // refresh keeps the tab instead of snapping back to public-profile.
  function settingsSectionFromPath() {
    const parts = location.pathname.split("/").filter(Boolean);
    return normalizeSettingsSection(parts[0] === "dashboard" && parts[1] === "settings" ? parts[2] || "" : "");
  }

  function setSettingsSection(section, { scroll = true, push = false } = {}) {
    const activeSection = normalizeSettingsSection(section);
    if (!state.settingsView) state.settingsView = {};
    state.settingsView.section = activeSection;
    if (push) {
      // Reflect the tab in the URL so refresh/back keep it. public-profile is
      // the default, so it stays on the bare /dashboard/settings URL.
      navigateHistory(activeSection === "public-profile"
        ? "/dashboard/settings"
        : `/dashboard/settings/${activeSection}`);
    }

    $$("[data-settings-section]").forEach((panel) => {
      const active = panel.dataset.settingsSection === activeSection;
      panel.classList.toggle("hidden", !active);
    });

    $$("[data-settings-section-link]").forEach((button) => {
      const active = button.dataset.settingsSectionLink === activeSection;
      button.setAttribute("aria-current", active ? "page" : "false");
      button.className = active
        ? "relative flex h-10 items-center gap-3 rounded-md bg-secondary px-3 pl-4 text-left font-semibold text-foreground"
        : "relative flex h-10 items-center gap-3 rounded-md px-4 text-left text-muted-foreground hover:bg-secondary hover:text-foreground";
      button.querySelector("[data-settings-active-indicator]")?.classList.toggle("hidden", !active);
    });

    const settingsMain = $("[data-settings-main]");
    if (scroll && settingsMain) {
      settingsMain.scrollIntoView({ block: "start", behavior: "smooth" });
    }

    // The Organizations tab is data-driven and only fetched when first opened.
    if (activeSection === "organizations") initOrgsSection();
    if (activeSection === "ssh-keys") loadSshKeys();
    // Session state changes on other devices, so re-entering this tab always
    // performs a fresh no-store read instead of keeping a page-lifetime copy.
    if (activeSection === "account") loadAccountSessions({ force: true });
  }

  // ---- Active account sessions ---------------------------------------------
  let accountSessionsLoaded = false;
  let accountSessionsLoading = null;

  function setAccountSessionStatus(message, kind = "") {
    const target = $("[data-account-session-status]");
    if (!target) return;
    target.textContent = message || "";
    target.className = "min-h-4 text-xs " + (
      kind === "bad" ? "text-red-400"
        : kind === "good" ? "text-emerald-400"
          : "text-muted-foreground"
    );
  }

  async function accountSessionApi(method, path = "") {
    const token = state.session?.sessionToken || "";
    if (!token) throw new Error("invalid_session");
    const response = await fetch("/api/accounts/sessions" + path, {
      method,
      headers: {
        accept: "application/json",
        authorization: "Bearer " + token,
      },
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `http_${response.status}`);
    }
    return data;
  }

  function renderAccountSessions(data) {
    const list = $("[data-account-session-list]");
    if (!list) return;
    const sessions = Array.isArray(data?.sessions) ? data.sessions : [];
    if (!sessions.length) {
      list.innerHTML = '<p class="p-4 text-sm text-muted-foreground">No active sessions were returned.</p>';
      return;
    }
    list.innerHTML = sessions.map((session, index) => `
      <article class="flex flex-col gap-3 p-4 sm:flex-row sm:items-center sm:justify-between ${index ? "border-t border-border" : ""}">
        <div class="min-w-0">
          <p class="text-sm font-semibold text-foreground">
            ${escapeHtml(session.deviceLabel || "Unknown device")}
            ${session.current ? '<span class="ml-2 rounded-full border border-emerald-500/50 px-2 py-0.5 text-[11px] text-emerald-300">This device</span>' : ""}
          </p>
          <p class="mt-1 text-xs text-muted-foreground">${escapeHtml(String(session.ipAddress || "address unavailable"))} · last active ${escapeHtml(formatTimeAgo(Number(session.lastSeenAt || 0)))} · signed in ${escapeHtml(formatDate(Number(session.createdAt || 0)))} · expires ${escapeHtml(formatDate(Number(session.expiresAt || 0)))}</p>
        </div>
        <button type="button" data-account-session-revoke="${escapeHtml(session.id || "")}" data-account-session-current="${session.current ? "true" : "false"}" class="inline-flex h-8 shrink-0 items-center justify-center rounded-md border border-red-500/50 px-3 text-xs font-semibold text-red-300 hover:bg-red-500/10">
          ${session.current ? "Sign out here" : "Sign out"}
        </button>
      </article>`).join("");
  }

  // Coarse kinds only — the Worker stores when an account was emailed and
  // whether the provider accepted it, never the subject or body.
  const ACCOUNT_EMAIL_KIND_LABELS = {
    verification: "Email verification",
    password_reset: "Password reset",
    feedback: "Founder feedback",
    notifications: "Notification digest",
    general_chat: "#general digest",
  };

  function renderAccountActivity(data) {
    const account = data?.account || {};
    const lastSeen = $("[data-account-last-seen]");
    if (lastSeen) {
      const seenAt = Number(account.lastSeenAt || 0);
      lastSeen.textContent = seenAt ? formatTimeAgo(seenAt) : "Never";
      lastSeen.title = seenAt ? formatDate(seenAt) : "";
    }
    const emailedAt = Number(account.lastEmailAt || 0);
    const lastEmail = $("[data-account-last-email]");
    if (lastEmail) {
      lastEmail.textContent = emailedAt ? formatTimeAgo(emailedAt) : "Never emailed";
      lastEmail.title = emailedAt ? formatDate(emailedAt) : "";
    }
    const detail = $("[data-account-last-email-detail]");
    if (detail) {
      const status = String(account.lastEmailStatus || "");
      const kind = ACCOUNT_EMAIL_KIND_LABELS[String(account.lastEmailKind || "")] || "Email";
      detail.textContent = !emailedAt ? "" : kind + " · " + (
        status === "delivered" ? "Delivered to the mail provider"
          : status === "failed" ? "The mail provider rejected it"
            : "Delivery status unknown");
      detail.className = "mt-1 text-xs " + (
        status === "delivered" && emailedAt ? "text-emerald-400"
          : status === "failed" && emailedAt ? "text-red-400"
            : "text-muted-foreground");
    }
  }

  function bindAccountSessionControls() {
    const list = $("[data-account-session-list]");
    if (list && list.dataset.controlsBound !== "true") {
      list.dataset.controlsBound = "true";
      list.addEventListener("click", (event) => {
        const button = event.target.closest("[data-account-session-revoke]");
        if (!button) return;
        revokeAccountSession(
          button.dataset.accountSessionRevoke || "",
          button.dataset.accountSessionCurrent === "true");
      });
    }
    const others = $("[data-account-sessions-revoke-others]");
    if (others && others.dataset.controlsBound !== "true") {
      others.dataset.controlsBound = "true";
      others.addEventListener("click", () => revokeOtherAccountSessions());
    }
  }

  async function loadAccountSessions({ force = false } = {}) {
    if (!$("[data-account-session-list]")) return;
    bindAccountSessionControls();
    if (accountSessionsLoaded && !force) return;
    if (accountSessionsLoading) return accountSessionsLoading;
    accountSessionsLoading = (async () => {
      try {
        const data = await accountSessionApi("GET");
        renderAccountSessions(data);
        renderAccountActivity(data);
        accountSessionsLoaded = true;
        setAccountSessionStatus(data.privacyNotice || "");
      } catch (error) {
        const list = $("[data-account-session-list]");
        if (list) {
          list.innerHTML = `<p class="p-4 text-sm text-red-400">${error.message === "invalid_session" ? "Sign in to manage active sessions." : "Could not load active sessions."}</p>`;
        }
      } finally {
        accountSessionsLoading = null;
      }
    })();
    return accountSessionsLoading;
  }

  async function revokeAccountSession(sessionId, current) {
    if (!sessionId || !window.confirm(current
      ? "Sign out this device now?"
      : "Sign out that device?")) return;
    try {
      const result = await accountSessionApi(
        "DELETE", "/" + encodeURIComponent(sessionId));
      if (current || result.currentRevoked) {
        logout();
        return;
      }
      accountSessionsLoaded = false;
      await loadAccountSessions({ force: true });
      setAccountSessionStatus("That device was signed out.", "good");
    } catch (_) {
      setAccountSessionStatus("Could not sign out that device.", "bad");
    }
  }

  async function revokeOtherAccountSessions() {
    if (!window.confirm("Sign out every other active device?")) return;
    const button = $("[data-account-sessions-revoke-others]");
    if (button) button.disabled = true;
    try {
      await accountSessionApi("DELETE", "/others");
      accountSessionsLoaded = false;
      await loadAccountSessions({ force: true });
      setAccountSessionStatus("All other devices were signed out.", "good");
    } catch (_) {
      setAccountSessionStatus("Could not sign out the other devices.", "bad");
    } finally {
      if (button) button.disabled = false;
    }
  }

  // ---- SSH public keys (settings tab) ---------------------------------------
  // Public keys are account-scoped and encrypted at rest by the Worker. The
  // browser never handles a private key; every push is authorized again by the
  // Worker and executed by a separately operated node-side SSH gateway.
  let sshKeysLoaded = false;
  let sshKeysLoading = null;

  function setSshKeyStatus(message, kind = "") {
    const target = $("[data-ssh-key-status]");
    if (!target) return;
    target.textContent = message || "";
    target.className = "min-h-4 text-xs " + (
      kind === "bad" ? "text-red-400"
        : kind === "good" ? "text-emerald-400"
          : "text-muted-foreground"
    );
  }

  async function sshKeyApi(method, path = "", body) {
    const token = state.session?.sessionToken || "";
    if (!token) throw new Error("invalid_session");
    const headers = {
      accept: "application/json",
      authorization: "Bearer " + token,
    };
    if (body !== undefined) headers["content-type"] = "application/json";
    const response = await fetch("/api/accounts/ssh-keys" + path, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      throw new Error(data.error || `http_${response.status}`);
    }
    return data;
  }

  function renderSshKeys(data) {
    const list = $("[data-ssh-key-list]");
    if (!list) return;
    const keys = Array.isArray(data?.keys) ? data.keys : [];
    const gateway = $("[data-ssh-gateway-status]");
    if (gateway) {
      gateway.textContent = data?.gatewayConfigured
        ? `SSH gateway: ${data.gatewayHost}${Number(data.gatewayPort) === 22 ? "" : `:${data.gatewayPort}`}`
        : "SSH gateway is not configured";
    }
    if (!keys.length) {
      list.innerHTML = '<p class="p-4 text-sm text-muted-foreground">No SSH keys registered.</p>';
      return;
    }
    list.innerHTML = keys.map((key, index) => {
      const used = Number(key.lastUsedAt || 0);
      return `
        <article class="flex flex-col gap-3 p-4 sm:flex-row sm:items-center sm:justify-between ${index ? "border-t border-border" : ""}">
          <div class="min-w-0">
            <p class="truncate text-sm font-semibold text-foreground">${escapeHtml(key.label || "SSH key")}</p>
            <p class="mt-1 break-all font-mono text-xs text-muted-foreground">${escapeHtml(key.fingerprint || "")}</p>
            <p class="mt-1 text-xs text-muted-foreground">${escapeHtml(key.keyType || "SSH")} · added ${escapeHtml(formatDate(key.createdAt))}${used ? ` · last used ${escapeHtml(formatTimeAgo(used))}` : " · never used"}</p>
          </div>
          <button type="button" data-ssh-key-revoke="${escapeHtml(key.id || "")}" class="inline-flex h-8 shrink-0 items-center justify-center rounded-md border border-red-500/50 px-3 text-xs font-semibold text-red-300 hover:bg-red-500/10">
            Revoke
          </button>
        </article>`;
    }).join("");
  }

  async function loadSshKeys({ force = false } = {}) {
    if (!$("[data-ssh-key-list]")) return;
    if (sshKeysLoaded && !force) return;
    if (sshKeysLoading) return sshKeysLoading;
    sshKeysLoading = (async () => {
      try {
        const data = await sshKeyApi("GET");
        renderSshKeys(data);
        sshKeysLoaded = true;
      } catch (error) {
        const list = $("[data-ssh-key-list]");
        if (list) {
          list.innerHTML = `<p class="p-4 text-sm text-red-400">${error.message === "invalid_session" ? "Sign in to manage SSH keys." : "Could not load SSH keys."}</p>`;
        }
      } finally {
        sshKeysLoading = null;
      }
    })();
    return sshKeysLoading;
  }

  async function addSshKey() {
    const publicKey = ($("[data-ssh-public-key]")?.value || "").trim();
    const label = ($("[data-ssh-key-label]")?.value || "").trim();
    if (!publicKey) {
      setSshKeyStatus("Paste one OpenSSH public key.", "bad");
      return;
    }
    const button = $("[data-ssh-key-add]");
    if (button) {
      button.disabled = true;
      button.textContent = "Adding…";
    }
    try {
      await sshKeyApi("POST", "", { publicKey, label });
      if ($("[data-ssh-public-key]")) $("[data-ssh-public-key]").value = "";
      if ($("[data-ssh-key-label]")) $("[data-ssh-key-label]").value = "";
      setSshKeyStatus("SSH public key registered.", "good");
      sshKeysLoaded = false;
      await loadSshKeys({ force: true });
    } catch (error) {
      const messages = {
        unsupported_key_type: "Use an Ed25519, ECDSA, security-key, or RSA (3072-bit or stronger) public key.",
        rsa_key_too_small: "RSA keys must be at least 3072 bits.",
        ssh_key_already_registered: "That public key is already registered or was previously revoked. Use a fresh keypair.",
        ssh_key_limit_reached: "Revoke an unused key before adding another.",
        one_public_key_required: "Paste exactly one public key.",
        invalid_session: "Sign in to manage SSH keys.",
      };
      setSshKeyStatus(messages[error.message] || "That SSH public key could not be registered.", "bad");
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = "Add SSH key";
      }
    }
  }

  async function revokeSshKey(keyId) {
    if (!keyId || !window.confirm("Revoke this SSH key? It will stop authenticating immediately.")) return;
    try {
      await sshKeyApi("DELETE", "/" + encodeURIComponent(keyId));
      setSshKeyStatus("SSH key revoked.", "good");
      sshKeysLoaded = false;
      await loadSshKeys({ force: true });
    } catch (_) {
      setSshKeyStatus("Could not revoke that SSH key.", "bad");
    }
  }

  // ---- Organizations (settings tab) -----------------------------------------
  // Client for the /api/orgs endpoints (mirrors the Flutter app's org UI):
  // list/create the orgs you belong to, then a master/detail panel to manage
  // one org's members, teams, and linked repos. All rendered lazily into
  // [data-orgs-root] the first time the Organizations settings tab is opened.
  const ORG_ROLE_OPTIONS = ["owner", "admin", "member"];
  const ORG_TEAM_PERMISSIONS = ["read", "write", "maintain", "admin"];
  const ORG_WORLD_ACCESS_OPTIONS = ["public", "restricted", "private"];
  // Keep these aliases aligned with world-office-tower.js and the Worker's
  // OFFICE_FLOOR_TEAM_ALIASES. The server remains authoritative for elevator
  // access; this organization-admin matrix explains every floor group a user
  // can belong to and highlights access granted by their current teams.
  const ORG_OFFICE_FLOOR_GROUPS = Object.freeze([
    { id: "marketing", label: "Marketing", aliases: ["marketing", "marketing-team", "growth", "brand", "comms", "communications"] },
    { id: "engineering", label: "Engineering", aliases: ["engineering", "engineers", "development", "developers", "platform", "frontend", "backend"] },
    { id: "product-design", label: "Product & Design", aliases: ["product-design", "product", "design", "ux", "ui-ux"] },
    { id: "security", label: "Security", aliases: ["security", "security-team", "trust-safety", "trust-and-safety"] },
    { id: "infrastructure", label: "Infrastructure", aliases: ["infrastructure", "infra", "devops", "site-reliability", "sre"] },
    { id: "community", label: "Community", aliases: ["community", "community-team", "developer-relations", "devrel"] },
    { id: "partnerships", label: "Partnerships", aliases: ["partnerships", "partnership", "business-development", "bizdev"] },
    { id: "operations", label: "Operations", aliases: ["operations", "ops", "people-operations", "finance-operations"] },
    { id: "executive", label: "Executive", aliases: ["executive", "executives", "leadership", "organization-leadership", "org-leadership"] },
  ]);

  // Worker org-endpoint error codes -> human text. Unknown codes fall through
  // to a generic message so the UI never shows a raw slug.
  const ORG_ERROR_TEXT = {
    invalid_org_name: "Invalid name. Use lowercase letters, numbers and dashes.",
    invalid_team_name: "Invalid team name. Use lowercase letters, numbers and dashes.",
    org_name_taken: "That name is already taken.",
    too_many_orgs: "You have reached the organization limit for this account.",
    too_many_members: "This organization has reached its member limit.",
    too_many_teams: "This organization has reached its team limit.",
    too_many_repos: "This organization has reached its linked-repo limit.",
    invalid_session: "Sign in to manage organizations.",
    forbidden: "You do not have permission to do that.",
    not_found: "Not found.",
    last_owner: "An organization must keep at least one owner.",
    unknown_account: "No account exists with that name.",
    not_a_member: "That account is not a member of this organization.",
    bad_role: "Invalid role.",
    bad_permission: "Invalid permission.",
    member_required: "Enter a member account name.",
    repo_required: "Enter a repository name.",
    not_your_node: "You can only link repositories from your own node.",
    unknown_repo: "That repository is not published on your node.",
    enabled_required: "Choose whether organization digests are enabled.",
    invalid_logo_url: "Logo URLs must use HTTPS and cannot contain credentials or fragments.",
    invalid_world_access: "Choose a valid access level for every organization space.",
  };

  function orgErrorText(code) {
    return ORG_ERROR_TEXT[code] || (code ? "Request failed (" + code + ")." : "Request failed.");
  }

  // fetchJson only does cached GETs; org writes need POST/DELETE with the
  // session bearer token and must surface the structured {error} body, so they
  // go through this dedicated helper instead.
  async function orgApiRequest(method, path, body) {
    const token = state.session?.sessionToken || "";
    const headers = { accept: "application/json" };
    if (body !== undefined) headers["content-type"] = "application/json";
    if (token) headers.authorization = "Bearer " + token;
    const response = await fetch(path, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.ok === false) {
      const error = new Error(orgErrorText(data.error || ""));
      error.code = data.error || "http_" + response.status;
      throw error;
    }
    return data;
  }

  function orgsRoot() {
    return $("[data-orgs-root]");
  }

  let orgsSectionLoaded = false;
  function initOrgsSection() {
    if (orgsSectionLoaded) return;
    orgsSectionLoaded = true;
    showOrgsList();
  }

  function orgSignedIn() {
    return Boolean(state.session?.nodeName) && state.session?.kind !== "preview";
  }

  function orgRoleBadge(role) {
    const clean = String(role || "member").toLowerCase();
    const tone = clean === "owner"
      ? "border-[#2f81f7]/40 bg-[#2f81f7]/10 text-[#2f81f7]"
      : clean === "admin"
        ? "border-amber-500/40 bg-amber-500/10 text-amber-500"
        : "border-border bg-secondary text-muted-foreground";
    return '<span class="inline-flex items-center rounded-full border px-2 py-0.5 text-xs font-semibold ' +
      tone + '">' + escapeHtml(clean) + "</span>";
  }

  function orgMemberFloorGroups(member) {
    const teams = new Set(
      (Array.isArray(member?.teams) ? member.teams : [])
        .map((team) => String(team || "").trim().toLowerCase())
        .filter(Boolean),
    );
    const groups = ORG_OFFICE_FLOOR_GROUPS.map((group) => {
      const matchingTeams = group.aliases.filter((team) => teams.has(team));
      const active = matchingTeams.length > 0;
      const state = active ? "In group" : "Available";
      const title = active
        ? group.label + " floor through " + matchingTeams.join(", ")
        : group.label + " floor group is available";
      return '<span title="' + escapeHtml(title) + '" aria-label="' +
        escapeHtml(group.label + ": " + state) + '" class="inline-flex items-center gap-1 rounded-full border px-2 py-1 text-[11px] font-semibold ' +
        (active
          ? "border-[#238636]/50 bg-[#238636]/10 text-[#238636]"
          : "border-border bg-background text-muted-foreground") + '">' +
        '<span aria-hidden="true" class="h-1.5 w-1.5 rounded-full ' +
        (active ? "bg-[#238636]" : "bg-muted-foreground/40") + '"></span>' +
        escapeHtml(group.label) + "</span>";
    }).join("");
    const activeCount = ORG_OFFICE_FLOOR_GROUPS.filter(
      (group) => group.aliases.some((team) => teams.has(team)),
    ).length;
    return '<div data-org-member-floor-groups class="mt-2 border-t border-border/70 pt-2">' +
      '<div class="mb-1.5 flex items-center justify-between gap-2 text-[11px] text-muted-foreground">' +
        '<span class="font-semibold text-foreground">Office floor groups</span>' +
        '<span>' + activeCount + " of " + ORG_OFFICE_FLOOR_GROUPS.length + " active</span>" +
      '</div><div class="flex flex-wrap gap-1.5">' + groups + "</div></div>";
  }

  function orgOptionTags(options, selected) {
    return options.map((opt) =>
      '<option value="' + escapeHtml(opt) + '"' + (opt === selected ? " selected" : "") + ">" +
      escapeHtml(opt) + "</option>").join("");
  }

  function orgStatus(root, message, ok) {
    const status = root.querySelector("[data-org-status]");
    if (!status) return;
    status.textContent = message || "";
    status.className = "min-h-4 text-xs " + (ok ? "text-[#238636]" : "text-red-500");
  }

  const ORG_INPUT_CLASS = "h-10 rounded-md border border-border bg-card px-3 text-sm font-normal " +
    "text-foreground placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-[#2f81f7]";
  const ORG_BTN_PRIMARY = "inline-flex h-9 items-center justify-center rounded-md bg-[#238636] px-4 " +
    "text-sm font-semibold text-white hover:bg-[#2ea043] transition-colors";
  const ORG_BTN_SECONDARY = "inline-flex h-9 items-center justify-center rounded-md border border-border " +
    "bg-secondary px-4 text-sm font-semibold text-foreground hover:bg-muted transition-colors";

  async function showOrgsList() {
    const root = orgsRoot();
    if (!root) return;
    if (!orgSignedIn()) {
      root.innerHTML = '<p class="text-sm text-muted-foreground">Sign in to create and manage organizations.</p>';
      return;
    }
    root.innerHTML = '<p class="text-sm text-muted-foreground">' + loadingHtml("Loading organizations…") + "</p>";
    let orgs = [];
    try {
      const data = await orgApiRequest("GET", "/api/orgs");
      orgs = Array.isArray(data.orgs) ? data.orgs : [];
    } catch (error) {
      root.innerHTML = '<p class="text-sm text-red-500">' + escapeHtml(error.message) + "</p>";
      return;
    }
    const rows = orgs.length
      ? orgs.map((org) => {
          const name = escapeHtml(org.name || "");
          return '<button type="button" data-org-open="' + name + '" ' +
            'class="flex w-full items-center gap-3 rounded-md border border-border bg-card px-4 py-3 text-left hover:bg-secondary transition-colors">' +
            '<i data-lucide="building-2" class="h-5 w-5 text-muted-foreground"></i>' +
            '<span class="min-w-0 flex-1 truncate text-sm font-semibold text-foreground">' + name + "</span>" +
            orgRoleBadge(org.role) +
            '<i data-lucide="chevron-right" class="h-4 w-4 text-muted-foreground"></i>' +
            "</button>";
        }).join("")
      : '<p class="text-sm text-muted-foreground">No organizations yet. Create one below.</p>';
    root.innerHTML =
      '<div class="grid gap-2">' + rows + "</div>" +
      '<form data-org-create class="mt-6 grid max-w-md gap-3 border-t border-border pt-6">' +
        '<h3 class="text-sm font-semibold text-foreground">New organization</h3>' +
        '<input data-org-name required placeholder="name (lowercase, dashes)" autocomplete="off" class="' + ORG_INPUT_CLASS + '" />' +
        '<input data-org-display placeholder="Display name (optional)" autocomplete="off" class="' + ORG_INPUT_CLASS + '" />' +
        '<input data-org-description placeholder="Description (optional)" autocomplete="off" class="' + ORG_INPUT_CLASS + '" />' +
        '<p data-org-status class="min-h-4 text-xs text-muted-foreground"></p>' +
        '<button type="submit" class="' + ORG_BTN_PRIMARY + ' w-fit">Create organization</button>' +
      "</form>";
    window.lucide?.createIcons();
    root.querySelector("[data-org-create]")?.addEventListener("submit", onCreateOrgSubmit);
    root.querySelectorAll("[data-org-open]").forEach((button) => {
      button.addEventListener("click", () => showOrgDetail(button.dataset.orgOpen));
    });
  }

  async function onCreateOrgSubmit(event) {
    event.preventDefault();
    const root = orgsRoot();
    if (!root) return;
    const name = (root.querySelector("[data-org-name]")?.value || "").trim().toLowerCase();
    if (!name) {
      orgStatus(root, "Enter an organization name.", false);
      return;
    }
    const body = { name };
    const display = (root.querySelector("[data-org-display]")?.value || "").trim();
    const description = (root.querySelector("[data-org-description]")?.value || "").trim();
    if (display) body.displayName = display;
    if (description) body.description = description;
    orgStatus(root, "Creating…", true);
    try {
      const created = await orgApiRequest("POST", "/api/orgs", body);
      showOrgDetail(created.org || name);
    } catch (error) {
      orgStatus(root, error.message, false);
    }
  }

  async function showOrgDetail(name) {
    const root = orgsRoot();
    if (!root || !name) return;
    root.innerHTML = '<p class="text-sm text-muted-foreground">' + loadingHtml("Loading organization…") + "</p>";
    let profile;
    let members = [];
    let teams = [];
    let repos = [];
    let fediverse = { controls: { enabled: true }, repos: [] };
    try {
      const [profileData, membersData, teamsData, reposData] = await Promise.all([
        orgApiRequest("GET", "/api/orgs/" + encodeURIComponent(name)),
        orgApiRequest("GET", "/api/orgs/" + encodeURIComponent(name) + "/members"),
        orgApiRequest("GET", "/api/orgs/" + encodeURIComponent(name) + "/teams").catch(() => ({ teams: [] })),
        orgApiRequest("GET", "/api/orgs/" + encodeURIComponent(name) + "/repos"),
      ]);
      profile = profileData;
      members = membersData.members || [];
      teams = teamsData.teams || [];
      repos = reposData.repos || [];
      if (profile.viewerRole === "owner" || profile.viewerRole === "admin") {
        fediverse = await orgApiRequest(
          "GET", "/api/orgs/" + encodeURIComponent(name) + "/fediverse");
      }
    } catch (error) {
      root.innerHTML =
        '<button type="button" data-org-back class="mb-4 inline-flex items-center gap-1 text-sm text-muted-foreground hover:text-foreground">' +
        '<i data-lucide="arrow-left" class="h-4 w-4"></i> Back</button>' +
        '<p class="text-sm text-red-500">' + escapeHtml(error.message) + "</p>";
      window.lucide?.createIcons();
      root.querySelector("[data-org-back]")?.addEventListener("click", showOrgsList);
      return;
    }
    renderOrgDetail(root, name, profile, members, teams, repos, fediverse);
  }

  function renderOrgDetail(root, name, profile, members, teams, repos, fediverse) {
    const canManage = profile.viewerRole === "owner" || profile.viewerRole === "admin";
    const isOwner = profile.viewerRole === "owner";
    const title = escapeHtml(profile.displayName || profile.org || name);
    const worldAccess = {
      lobby: profile.worldAccess?.lobby || "public",
      floors: profile.worldAccess?.floors || "restricted",
      offices: profile.worldAccess?.offices || "restricted",
    };
    const worldSettings = canManage
      ? '<form data-org-world-settings class="mt-5 grid gap-3 rounded-md border border-border bg-card p-4">' +
          '<div><h4 class="text-sm font-semibold text-foreground">World building</h4>' +
          '<p class="mt-1 text-xs leading-5 text-muted-foreground">Set the public identity of this building and independently control its lobby, repository floors, and personal offices. Restricted spaces are visible to organization members; private spaces are limited to owners and administrators.</p></div>' +
          '<div class="grid gap-3 md:grid-cols-2">' +
            '<label class="grid gap-1 text-xs font-semibold text-foreground">Display name' +
              '<input data-org-world-display maxlength="80" value="' + escapeHtml(profile.displayName || "") + '" class="' + ORG_INPUT_CLASS + '" /></label>' +
            '<label class="grid gap-1 text-xs font-semibold text-foreground">HTTPS logo URL' +
              '<input data-org-world-logo type="url" inputmode="url" maxlength="500" value="' + escapeHtml(profile.logoUrl || "") + '" placeholder="https://…" class="' + ORG_INPUT_CLASS + '" /></label>' +
          '</div>' +
          '<label class="grid gap-1 text-xs font-semibold text-foreground">Description' +
            '<textarea data-org-world-description maxlength="500" rows="3" class="' + ORG_INPUT_CLASS + ' h-auto py-2">' + escapeHtml(profile.description || "") + "</textarea></label>" +
          '<div class="grid gap-3 sm:grid-cols-3">' +
            '<label class="grid gap-1 text-xs font-semibold text-foreground">Lobby access<select data-org-world-lobby class="' + ORG_INPUT_CLASS + '">' +
              orgOptionTags(ORG_WORLD_ACCESS_OPTIONS, worldAccess.lobby) + "</select></label>" +
            '<label class="grid gap-1 text-xs font-semibold text-foreground">Repository floors<select data-org-world-floors class="' + ORG_INPUT_CLASS + '">' +
              orgOptionTags(ORG_WORLD_ACCESS_OPTIONS, worldAccess.floors) + "</select></label>" +
            '<label class="grid gap-1 text-xs font-semibold text-foreground">Personal offices<select data-org-world-offices class="' + ORG_INPUT_CLASS + '">' +
              orgOptionTags(ORG_WORLD_ACCESS_OPTIONS, worldAccess.offices) + "</select></label>" +
          "</div>" +
          '<button type="submit" class="' + ORG_BTN_PRIMARY + ' w-fit">Save world settings</button>' +
        "</form>"
      : '<section class="mt-5 rounded-md border border-border bg-card p-4">' +
          '<h4 class="text-sm font-semibold text-foreground">World building access</h4>' +
          '<p class="mt-1 text-xs text-muted-foreground">Lobby: ' + escapeHtml(worldAccess.lobby) +
          " · floors: " + escapeHtml(worldAccess.floors) +
          " · offices: " + escapeHtml(worldAccess.offices) + "</p></section>";

    const memberRows = members.map((member) => {
      const memberName = escapeHtml(member.name || "");
      const controls = canManage
        ? '<select data-org-member-role="' + memberName + '" class="' + ORG_INPUT_CLASS + ' h-8 py-0">' +
            orgOptionTags(ORG_ROLE_OPTIONS, member.role) + "</select>" +
          '<button type="button" data-org-member-remove="' + memberName + '" title="Remove from org" ' +
            'class="inline-flex h-8 w-8 items-center justify-center rounded-md border border-border text-muted-foreground hover:text-red-500 hover:border-red-500/50">' +
            '<i data-lucide="user-minus" class="h-4 w-4"></i></button>'
        : orgRoleBadge(member.role);
      return '<div class="rounded-md border border-border bg-card px-3 py-2">' +
        '<div class="flex items-center gap-2">' +
          '<span class="min-w-0 flex-1 truncate text-sm font-medium text-foreground">' + memberName + "</span>" +
          controls +
        "</div>" +
        (canManage ? orgMemberFloorGroups(member) : "") +
      "</div>";
    }).join("") || '<p class="text-sm text-muted-foreground">No members.</p>';

    const teamRows = teams.map((team) => {
      const teamName = escapeHtml(team.team || "");
      const controls = canManage
        ? '<select data-org-team-perm="' + teamName + '" class="' + ORG_INPUT_CLASS + ' h-8 py-0">' +
            orgOptionTags(ORG_TEAM_PERMISSIONS, team.permission) + "</select>" +
          '<button type="button" data-org-team-delete="' + teamName + '" title="Delete team" ' +
            'class="inline-flex h-8 w-8 items-center justify-center rounded-md border border-border text-muted-foreground hover:text-red-500 hover:border-red-500/50">' +
            '<i data-lucide="trash-2" class="h-4 w-4"></i></button>'
        : '<span class="inline-flex items-center rounded-full border border-border bg-secondary px-2 py-0.5 text-xs font-semibold text-muted-foreground">' +
            escapeHtml(team.permission || "") + "</span>";
      return '<div class="flex items-center gap-2 rounded-md border border-border bg-card px-3 py-2">' +
        '<button type="button" data-org-team-open="' + teamName + '" class="min-w-0 flex-1 truncate text-left text-sm font-medium text-foreground hover:underline">' +
          teamName + ' <span class="text-xs text-muted-foreground">· ' + (team.members || 0) + ' member' + ((team.members === 1) ? "" : "s") + "</span></button>" +
        controls + "</div>";
    }).join("") || '<p class="text-sm text-muted-foreground">No teams.</p>';

    const repoRows = repos.map((repo) => {
      const repoName = escapeHtml(repo.repo || "");
      const serves = repo.node ? '<span class="text-xs text-muted-foreground">serves ' + escapeHtml(repo.node) + "/" + repoName + "</span>" : "";
      const control = canManage
        ? '<button type="button" data-org-repo-unlink="' + repoName + '" title="Unlink repo" ' +
            'class="inline-flex h-8 w-8 items-center justify-center rounded-md border border-border text-muted-foreground hover:text-red-500 hover:border-red-500/50">' +
            '<i data-lucide="unlink" class="h-4 w-4"></i></button>'
        : "";
      return '<div class="flex items-center gap-2 rounded-md border border-border bg-card px-3 py-2">' +
        '<div class="min-w-0 flex-1"><div class="truncate text-sm font-medium text-foreground">' + escapeHtml(name) + "/" + repoName + "</div>" + serves + "</div>" +
        control + "</div>";
    }).join("") || '<p class="text-sm text-muted-foreground">No repos linked.</p>';

    const memberAdd = canManage
      ? '<form data-org-member-add class="mt-2 flex flex-wrap items-start gap-2">' +
          '<div class="min-w-[14rem] flex-1">' +
            '<input data-member-name list="org-member-suggestions" role="combobox" aria-autocomplete="list" aria-controls="org-member-suggestions" placeholder="Start typing an account name" autocomplete="off" class="' + ORG_INPUT_CLASS + ' w-full" />' +
            '<datalist id="org-member-suggestions" data-org-member-suggestions></datalist>' +
            '<p data-org-member-search-status class="mt-1 min-h-4 text-[11px] text-muted-foreground">Type a name to search active ForkMesh accounts.</p>' +
          "</div>" +
          '<select data-member-role class="' + ORG_INPUT_CLASS + '">' + orgOptionTags(ORG_ROLE_OPTIONS, "member") + "</select>" +
          '<button type="submit" class="' + ORG_BTN_SECONDARY + '">Add member</button></form>'
      : "";
    const teamAdd = canManage
      ? '<form data-org-team-add class="mt-2 flex flex-wrap items-center gap-2">' +
          '<input data-team-name placeholder="team name" autocomplete="off" class="' + ORG_INPUT_CLASS + ' flex-1 min-w-[10rem]" />' +
          '<select data-team-perm class="' + ORG_INPUT_CLASS + '">' + orgOptionTags(ORG_TEAM_PERMISSIONS, "read") + "</select>" +
          '<button type="submit" class="' + ORG_BTN_SECONDARY + '">Add team</button></form>'
      : "";
    const account = String(state.session?.nodeName || "").toLowerCase();
    const repoAdd = canManage
      ? '<form data-org-repo-add class="mt-2 flex flex-wrap items-center gap-2">' +
          '<input data-repo-name placeholder="repo name" autocomplete="off" class="' + ORG_INPUT_CLASS + ' flex-1 min-w-[10rem]" />' +
          '<input data-repo-node value="' + escapeHtml(account) + '" placeholder="your node" autocomplete="off" class="' + ORG_INPUT_CLASS + '" />' +
          '<button type="submit" class="' + ORG_BTN_SECONDARY + '">Link repo</button></form>'
      : "";
    const digestRows = (fediverse?.repos || []).map((item) => {
      const repoName = escapeHtml(item.repo || "");
      const preview = String(item.preview?.text || "").trim();
      const status = !item.ownerEnabled
        ? "Disabled by repository owner"
        : item.enabled ? "Eligible for daily digest" : "Disabled for organization";
      return '<details class="rounded-md border border-border bg-card p-3">' +
        '<summary class="cursor-pointer text-sm font-medium text-foreground">' +
          escapeHtml(name) + "/" + repoName +
          ' <span class="ml-1 text-xs font-normal text-muted-foreground">· ' +
          escapeHtml(status) + "</span></summary>" +
        '<pre class="mt-2 max-h-52 overflow-auto whitespace-pre-wrap break-words rounded bg-background p-2 font-mono text-[10px] leading-4 text-foreground">' +
          escapeHtml(preview || "No meaningful public updates are queued.") +
        "</pre></details>";
    }).join("") || '<p class="text-sm text-muted-foreground">No public linked repositories have queued updates.</p>';
    const digestControls = canManage
      ? '<section class="mt-6" data-org-fediverse-controls>' +
          '<div class="flex items-start justify-between gap-4">' +
            '<div><h4 class="text-sm font-semibold text-foreground">Fediverse daily digests</h4>' +
            '<p class="mt-1 max-w-2xl text-xs leading-5 text-muted-foreground">Combine meaningful public updates for each organization alias into at most one clearly automated post per 24 hours. Repository-owner federation settings remain authoritative; empty posts are never sent.</p></div>' +
            '<label class="inline-flex shrink-0 items-center gap-2 text-xs font-semibold text-foreground"><input data-org-fediverse-enabled type="checkbox" class="h-4 w-4"' +
              (fediverse?.controls?.enabled !== false ? " checked" : "") +
              " />Enabled</label>" +
          "</div>" +
          '<div class="mt-3 grid gap-2">' + digestRows + "</div>" +
        "</section>"
      : "";
    const accessSummary =
      '<aside class="rounded-md border border-[#2f81f7]/30 bg-[#2f81f7]/5 p-4 lg:sticky lg:top-4 lg:self-start" data-org-access-summary>' +
        '<div class="flex items-center gap-2"><i data-lucide="shield-check" class="h-4 w-4 text-[#2f81f7]"></i>' +
          '<h4 class="text-sm font-semibold text-foreground">What organization access means</h4></div>' +
        '<div class="mt-3 grid gap-3 text-xs leading-5 text-muted-foreground">' +
          '<section><strong class="block text-foreground">Every organization member can</strong>' +
            '<ul class="mt-1 list-disc space-y-1 pl-5">' +
              '<li>View the organization, linked repositories, member directory, and teams.</li>' +
              '<li>Enter member-restricted World floors and see non-private organization activity.</li>' +
            "</ul></section>" +
          '<section><strong class="block text-foreground">Engineering team members can</strong>' +
            '<p class="mt-1">View organization agents and start, review, re-prompt, and continue Claude or Codex sessions on approved headless mirrors. This is checked server-side on every agent request; organization role alone does not grant access.</p></section>' +
          '<section><strong class="block text-foreground">Member role cannot</strong>' +
            '<p class="mt-1">Change organization settings, membership, teams, or repository links. Members outside Engineering also cannot view or control Claude/Codex sessions. Team permissions can grant repository work, but protected mirror approval and merge remain owner-only.</p></section>' +
          '<section><strong class="block text-foreground">Admin role adds</strong>' +
            '<p class="mt-1">Organization settings, members, teams, linked repositories, World access, and federation controls. Admins cannot remove the final owner.</p></section>' +
          '<section><strong class="block text-foreground">Owner role adds</strong>' +
            '<p class="mt-1">Owner-only protected mirror approval/merge controls and organization deletion. The organization must always retain one owner.</p></section>' +
          '<section><strong class="block text-foreground">Team permission</strong>' +
            '<p class="mt-1"><span class="font-mono text-foreground">read</span> views; <span class="font-mono text-foreground">write</span> changes content; <span class="font-mono text-foreground">maintain</span> manages repository work; <span class="font-mono text-foreground">admin</span> manages repository access. Team permission does not change the member’s organization role.</p></section>' +
        "</div>" +
      "</aside>";
    const memberAndTeamSettings =
      '<div class="mt-5 grid gap-6 lg:grid-cols-[minmax(0,1fr)_22rem]">' +
        '<div>' +
          '<section><h4 class="text-sm font-semibold text-foreground">Members</h4>' +
            '<div class="mt-2 grid gap-2">' + memberRows + "</div>" + memberAdd + "</section>" +
          '<section class="mt-6"><h4 class="text-sm font-semibold text-foreground">Teams</h4>' +
            '<div class="mt-2 grid gap-2">' + teamRows + "</div>" + teamAdd + "</section>" +
        "</div>" +
        accessSummary +
      "</div>";

    root.innerHTML =
      '<div class="flex items-center gap-3">' +
        '<button type="button" data-org-back class="inline-flex items-center gap-1 text-sm text-muted-foreground hover:text-foreground">' +
          '<i data-lucide="arrow-left" class="h-4 w-4"></i> Organizations</button>' +
        '<span class="flex-1"></span>' +
        (isOwner ? '<button type="button" data-org-delete class="inline-flex h-8 items-center gap-1 rounded-md border border-red-500/40 px-3 text-xs font-semibold text-red-500 hover:bg-red-500/10"><i data-lucide="trash-2" class="h-4 w-4"></i> Delete org</button>' : "") +
      "</div>" +
      '<div class="mt-4 flex items-center gap-3">' +
        '<h3 class="text-xl font-semibold text-foreground">' + title + "</h3>" +
        (profile.viewerRole ? orgRoleBadge(profile.viewerRole) : "") +
      "</div>" +
      (profile.description ? '<p class="mt-1 text-sm text-muted-foreground">' + escapeHtml(profile.description) + "</p>" : "") +
      worldSettings +
      '<p data-org-status class="mt-2 min-h-4 text-xs text-muted-foreground"></p>' +
      memberAndTeamSettings +
      '<section class="mt-6"><h4 class="text-sm font-semibold text-foreground">Linked repos</h4>' +
        '<div class="mt-2 grid gap-2">' + repoRows + "</div>" + repoAdd + "</section>" +
      digestControls;

    window.lucide?.createIcons();
    wireOrgDetail(root, name, members);
  }

  function wireOrgDetail(root, name, members) {
    const reload = () => showOrgDetail(name);
    const guard = async (fn) => {
      try {
        await fn();
        reload();
      } catch (error) {
        orgStatus(root, error.message, false);
      }
    };

    root.querySelector("[data-org-back]")?.addEventListener("click", showOrgsList);

    root.querySelector("[data-org-delete]")?.addEventListener("click", () => {
      if (!window.confirm("Delete " + name + "? This dissolves the org, its members, teams, and repo links. Linked repos are not deleted.")) return;
      guard(() => orgApiRequest("DELETE", "/api/orgs/" + encodeURIComponent(name), {}).then(showOrgsList));
    });

    root.querySelector("[data-org-world-settings]")?.addEventListener("submit", (event) => {
      event.preventDefault();
      const worldAccess = {
        lobby: root.querySelector("[data-org-world-lobby]")?.value || "public",
        floors: root.querySelector("[data-org-world-floors]")?.value || "restricted",
        offices: root.querySelector("[data-org-world-offices]")?.value || "restricted",
      };
      guard(() => orgApiRequest("PATCH", "/api/orgs/" + encodeURIComponent(name), {
        displayName: (root.querySelector("[data-org-world-display]")?.value || "").trim(),
        description: (root.querySelector("[data-org-world-description]")?.value || "").trim(),
        logoUrl: (root.querySelector("[data-org-world-logo]")?.value || "").trim(),
        worldAccess,
      }));
    });

    root.querySelector("[data-org-member-add]")?.addEventListener("submit", (event) => {
      event.preventDefault();
      const member = (root.querySelector("[data-member-name]")?.value || "").trim().toLowerCase();
      const role = root.querySelector("[data-member-role]")?.value || "member";
      if (!member) return;
      guard(() => orgApiRequest("POST", "/api/orgs/" + encodeURIComponent(name) + "/members", { member, role }));
    });
    wireOrgMemberAutocomplete(root, members);
    root.querySelectorAll("[data-org-member-role]").forEach((select) => {
      select.addEventListener("change", () => {
        const member = select.dataset.orgMemberRole;
        guard(() => orgApiRequest("POST", "/api/orgs/" + encodeURIComponent(name) + "/members", { member, role: select.value }));
      });
    });
    root.querySelectorAll("[data-org-member-remove]").forEach((button) => {
      button.addEventListener("click", () => {
        const member = button.dataset.orgMemberRemove;
        if (!window.confirm("Remove " + member + " from " + name + "?")) return;
        guard(() => orgApiRequest("DELETE", "/api/orgs/" + encodeURIComponent(name) + "/members", { member }));
      });
    });

    root.querySelector("[data-org-team-add]")?.addEventListener("submit", (event) => {
      event.preventDefault();
      const team = (root.querySelector("[data-team-name]")?.value || "").trim().toLowerCase();
      const permission = root.querySelector("[data-team-perm]")?.value || "read";
      if (!team) return;
      guard(() => orgApiRequest("POST", "/api/orgs/" + encodeURIComponent(name) + "/teams", { team, permission }));
    });
    root.querySelectorAll("[data-org-team-perm]").forEach((select) => {
      select.addEventListener("change", () => {
        const team = select.dataset.orgTeamPerm;
        guard(() => orgApiRequest("POST", "/api/orgs/" + encodeURIComponent(name) + "/teams", { team, permission: select.value }));
      });
    });
    root.querySelectorAll("[data-org-team-delete]").forEach((button) => {
      button.addEventListener("click", () => {
        const team = button.dataset.orgTeamDelete;
        if (!window.confirm("Delete team " + team + "?")) return;
        guard(() => orgApiRequest("DELETE", "/api/orgs/" + encodeURIComponent(name) + "/teams", { team }));
      });
    });
    root.querySelectorAll("[data-org-team-open]").forEach((button) => {
      button.addEventListener("click", () => showOrgTeamDetail(name, button.dataset.orgTeamOpen, members));
    });

    root.querySelector("[data-org-repo-add]")?.addEventListener("submit", (event) => {
      event.preventDefault();
      const repo = (root.querySelector("[data-repo-name]")?.value || "").trim().toLowerCase();
      const node = (root.querySelector("[data-repo-node]")?.value || "").trim().toLowerCase();
      if (!repo) return;
      const body = { repo };
      if (node) body.node = node;
      guard(() => orgApiRequest("POST", "/api/orgs/" + encodeURIComponent(name) + "/repos", body));
    });
    root.querySelectorAll("[data-org-repo-unlink]").forEach((button) => {
      button.addEventListener("click", () => {
        const repo = button.dataset.orgRepoUnlink;
        if (!window.confirm("Unlink " + name + "/" + repo + "?")) return;
        guard(() => orgApiRequest("DELETE", "/api/orgs/" + encodeURIComponent(name) + "/repos", { repo }));
      });
    });
    root.querySelector("[data-org-fediverse-enabled]")?.addEventListener(
      "change", (event) => {
        guard(() => orgApiRequest(
          "POST", "/api/orgs/" + encodeURIComponent(name) + "/fediverse",
          { enabled: Boolean(event.target.checked) }));
      });
  }

  function wireOrgMemberAutocomplete(root, members) {
    const input = root.querySelector("[data-member-name]");
    const suggestions = root.querySelector("[data-org-member-suggestions]");
    const status = root.querySelector("[data-org-member-search-status]");
    if (!input || !suggestions || !status) return;
    const existing = new Set(
      (Array.isArray(members) ? members : [])
        .map((member) => String(member?.name || "").trim().toLowerCase())
        .filter(Boolean),
    );
    let timer = 0;
    let generation = 0;
    input.addEventListener("input", () => {
      window.clearTimeout(timer);
      const query = String(input.value || "").trim().toLowerCase();
      const currentGeneration = ++generation;
      suggestions.replaceChildren();
      if (!query) {
        status.textContent = "Type a name to search active ForkMesh accounts.";
        return;
      }
      status.textContent = "Searching active accounts…";
      timer = window.setTimeout(async () => {
        try {
          const data = await orgApiRequest(
            "GET",
            "/api/chat/direct-messages/users?query=" +
              encodeURIComponent(query),
          );
          if (currentGeneration !== generation) return;
          const names = (Array.isArray(data?.users) ? data.users : [])
            .map((user) => String(user?.name || "").trim().toLowerCase())
            .filter((account, index, all) =>
              account &&
              !existing.has(account) &&
              all.indexOf(account) === index
            )
            .slice(0, 12);
          const fragment = document.createDocumentFragment();
          names.forEach((account) => {
            const option = document.createElement("option");
            option.value = account;
            fragment.appendChild(option);
          });
          suggestions.replaceChildren(fragment);
          status.textContent = names.length
            ? `${names.length} matching active account${names.length === 1 ? "" : "s"}.`
            : "No matching account outside this organization.";
        } catch (_) {
          if (currentGeneration !== generation) return;
          status.textContent =
            "Account search is unavailable; an exact account name still works.";
        }
      }, 180);
    });
  }

  async function showOrgTeamDetail(name, team, orgMembers) {
    const root = orgsRoot();
    if (!root || !team) return;
    root.innerHTML = '<p class="text-sm text-muted-foreground">' + loadingHtml("Loading team…") + "</p>";
    let data;
    try {
      data = await orgApiRequest("GET", "/api/orgs/" + encodeURIComponent(name) + "/teams/" + encodeURIComponent(team) + "/members");
    } catch (error) {
      root.innerHTML =
        '<button type="button" data-org-team-back class="mb-4 inline-flex items-center gap-1 text-sm text-muted-foreground hover:text-foreground">' +
        '<i data-lucide="arrow-left" class="h-4 w-4"></i> Back</button>' +
        '<p class="text-sm text-red-500">' + escapeHtml(error.message) + "</p>";
      window.lucide?.createIcons();
      root.querySelector("[data-org-team-back]")?.addEventListener("click", () => showOrgDetail(name));
      return;
    }
    const teamMembers = data.members || [];
    const onTeam = new Set(teamMembers.map((member) => member.name));
    const candidates = (orgMembers || []).filter((member) => !onTeam.has(member.name));
    const rows = teamMembers.map((member) => {
      const memberName = escapeHtml(member.name || "");
      return '<div class="flex items-center gap-2 rounded-md border border-border bg-card px-3 py-2">' +
        '<span class="min-w-0 flex-1 truncate text-sm font-medium text-foreground">' + memberName + "</span>" +
        '<button type="button" data-org-team-member-remove="' + memberName + '" title="Remove from team" ' +
          'class="inline-flex h-8 w-8 items-center justify-center rounded-md border border-border text-muted-foreground hover:text-red-500 hover:border-red-500/50">' +
          '<i data-lucide="x" class="h-4 w-4"></i></button></div>';
    }).join("") || '<p class="text-sm text-muted-foreground">No members on this team yet.</p>';
    const addForm = candidates.length
      ? '<form data-org-team-member-add class="mt-3 flex flex-wrap items-center gap-2">' +
          '<select data-team-member-name class="' + ORG_INPUT_CLASS + ' flex-1 min-w-[10rem]">' +
            candidates.map((member) => '<option value="' + escapeHtml(member.name) + '">' + escapeHtml(member.name) + "</option>").join("") +
          "</select>" +
          '<button type="submit" class="' + ORG_BTN_SECONDARY + '">Add to team</button></form>'
      : '<p class="mt-3 text-xs text-muted-foreground">Every org member is already on this team.</p>';
    root.innerHTML =
      '<button type="button" data-org-team-back class="inline-flex items-center gap-1 text-sm text-muted-foreground hover:text-foreground">' +
        '<i data-lucide="arrow-left" class="h-4 w-4"></i> ' + escapeHtml(name) + "</button>" +
      '<div class="mt-4 flex items-center gap-3"><h3 class="text-xl font-semibold text-foreground">' + escapeHtml(team) + "</h3>" +
        '<span class="inline-flex items-center rounded-full border border-border bg-secondary px-2 py-0.5 text-xs font-semibold text-muted-foreground">' + escapeHtml(data.permission || "") + " permission</span></div>" +
      '<p data-org-status class="mt-2 min-h-4 text-xs text-muted-foreground"></p>' +
      '<div class="mt-4 grid gap-2">' + rows + "</div>" + addForm;
    window.lucide?.createIcons();

    const guard = async (fn) => {
      try {
        await fn();
        showOrgTeamDetail(name, team, orgMembers);
      } catch (error) {
        orgStatus(root, error.message, false);
      }
    };
    root.querySelector("[data-org-team-back]")?.addEventListener("click", () => showOrgDetail(name));
    root.querySelector("[data-org-team-member-add]")?.addEventListener("submit", (event) => {
      event.preventDefault();
      const member = root.querySelector("[data-team-member-name]")?.value || "";
      if (!member) return;
      guard(() => orgApiRequest("POST", "/api/orgs/" + encodeURIComponent(name) + "/teams/" + encodeURIComponent(team) + "/members", { member }));
    });
    root.querySelectorAll("[data-org-team-member-remove]").forEach((button) => {
      button.addEventListener("click", () => {
        const member = button.dataset.orgTeamMemberRemove;
        guard(() => orgApiRequest("DELETE", "/api/orgs/" + encodeURIComponent(name) + "/teams/" + encodeURIComponent(team) + "/members", { member }));
      });
    });
  }

  function setMobileSidebarOpen(open) {
    document.body.classList.toggle("dashboard-sidebar-open", open);
    $$("[data-mobile-menu-toggle]").forEach((button) => {
      button.setAttribute("aria-expanded", open ? "true" : "false");
      button.setAttribute("aria-label", open ? "Close dashboard menu" : "Open dashboard menu");
    });
  }

  function closeMobileDrawers() {
    setMobileSidebarOpen(false);
  }

  function currentSection() {
    // The legacy section name is baked into the page document at build time
    // (dashboard_shell.PAGES[page]["section"] -> data-dashboard-section).
    return $("[data-dashboard-root]")?.dataset?.dashboardSection
      || $("[data-view].active")?.dataset?.view
      || "home";
  }

  function renderHeaderContext(section = currentSection()) {
    const headerContext = $("[data-dashboard-header-context]");
    if (!headerContext) return;
    if (section === "profile-overview" || section === "profile-repositories") {
      const renderedName = ($("[data-profile-page-node-name]")?.textContent || "").trim();
      headerContext.textContent = state.session?.nodeName || state.session?.email || renderedName || "Profile";
      return;
    }
    if (section === "profile") {
      headerContext.textContent = "Settings";
      return;
    }
    if (section === "repos") {
      headerContext.textContent = "Repositories";
      return;
    }
    if (section === "network") {
      headerContext.textContent = "Network";
      return;
    }
    if (section === "chat") {
      headerContext.textContent = "Chat";
      return;
    }
    if (section === "explore" && state.selectedRepo) {
      headerContext.textContent = repoKey(state.selectedRepo);
      return;
    }
    headerContext.textContent = "Dashboard";
  }

  // ---- Public-profile mode (/@name) ---------------------------------------
  // The worker serves the SAME prebuilt profile documents at /@name; the
  // profile-page machinery renders whatever profileSubject() returns, so
  // public mode is: fetch the named account's public payload, park it in
  // state.publicProfile, and strip the owner-only chrome.

  function publicProfileNameFromPath() {
    const match = /^\/@([a-z][a-z0-9-]{0,62})(?:\/repositories)?\/?$/
      .exec(location.pathname.toLowerCase());
    return match ? match[1] : "";
  }

  function profileSubject() {
    return state.publicProfile || state.session;
  }

  // On /@name pages the profile markup belongs to the fetched PUBLIC profile;
  // the shared-chrome boot path still calls the profile renderers with the
  // session, which must not overwrite (or briefly flash) the wrong identity.
  function profileMarkupOwnedByPublicProfile(session) {
    const publicName = publicProfileNameFromPath();
    return Boolean(publicName) && session !== state.publicProfile &&
      publicName !== String(session?.nodeName || "").toLowerCase();
  }

  async function loadPublicProfile(name) {
    document.body.classList.add("public-profile-mode");
    let body;
    try {
      const viewer = state.session?.nodeName
        ? "?viewer=" + encodeURIComponent(state.session.nodeName) : "";
      body = await fetchJson("/api/accounts/" + encodeURIComponent(name) + viewer);
    } catch (_) {
      $$("[data-profile-page-node-name]").forEach((el) => {
        el.textContent = "@" + name + " was not found";
      });
      return;
    }
    const profile = sessionFromAccountPayload(body, { nodeName: name });
    profile.nodeName = profile.nodeName || name;
    // Never show a mailbox on someone else's page — the handle is the
    // public identity here.
    profile.email = "@" + profile.nodeName;
    profile.isFollowing = Boolean(body?.social?.isFollowing);
    state.publicProfile = profile;
    renderProfilePage(profile);
    applyPublicProfileChrome(profile);
    // Repository lists still use the catalog, while the native contribution
    // card reads its own account ledger after the public subject is installed.
    renderProfileContributionGraph();
    renderProfileRepositories();
  }

  function applyPublicProfileChrome(profile) {
    const name = profile.nodeName;
    // Tabs point at the public URLs, not the session dashboard pages.
    $$("[data-profile-tabs] a[href='/dashboard/profile']").forEach((a) => {
      a.href = "/@" + encodeURIComponent(name);
    });
    $$("[data-profile-tabs] a[href='/dashboard/profile/repositories']").forEach((a) => {
      a.href = "/@" + encodeURIComponent(name) + "/repositories";
    });
    // Owner-only affordances become a Follow button (or disappear).
    $$("[data-profile-about-edit]").forEach((el) => el.classList.add("hidden"));
    $$("[data-profile-about-owner]").forEach((el) => { el.textContent = name; });
    $$("[data-profile-sidebar-slot] a[href='/dashboard/settings']").forEach((edit) => {
      const wrap = edit.parentElement;
      edit.remove();
      if (!wrap || !state.session?.nodeName ||
          state.session.nodeName.toLowerCase() === name) return;
      const follow = document.createElement("button");
      follow.type = "button";
      follow.setAttribute("data-profile-follow", name);
      follow.className = "flex w-full items-center justify-center rounded-md " +
        "border border-border bg-secondary px-3 py-1.5 text-sm font-semibold " +
        "text-foreground hover:bg-background";
      follow.textContent = profile.isFollowing ? "Following" : "Follow";
      follow.addEventListener("click", async () => {
        const following = follow.textContent === "Following";
        follow.disabled = true;
        try {
          const response = await fetch(
            "/api/accounts/" + encodeURIComponent(name) + "/follow", {
              method: following ? "DELETE" : "POST",
              headers: {
                "Content-Type": "application/json",
                Authorization: "Bearer " + (state.session?.sessionToken || ""),
              },
              body: JSON.stringify({
                sessionToken: state.session?.sessionToken || "",
              }),
            });
          if (response.ok) {
            follow.textContent = following ? "Follow" : "Following";
            const count = $("[data-profile-followers-count]");
            if (count) {
              const current = parseInt(count.textContent, 10) || 0;
              count.textContent = String(Math.max(0, current + (following ? -1 : 1)));
            }
          }
        } catch (_) {}
        follow.disabled = false;
      });
      wrap.append(follow);
    });
  }

  function nodeNeedsReconnect(session) {
    // A guest has nothing to reconnect; a node-kind session IS the node.
    const signedIn = Boolean(session && (session.nodeName || session.email));
    if (!signedIn || session.kind === "node") return false;
    // Only flag when the account affirmatively has zero linked nodes. An absent
    // list means "unknown" (older payload), not "none" — stay quiet then.
    return Array.isArray(session.nodes) && session.nodes.length === 0;
  }

  function renderProfile(session) {
    const name = session?.nodeName || session?.email || "My Profile";
    const nameEl = $("[data-dashboard-profile-name]");
    const statusEl = $("[data-dashboard-profile-status]");
    const avatar = $("[data-dashboard-profile-avatar]");
    const adminButton = $("[data-admin-button]");
    const homeName = $("[data-home-user-name]");
    const composeName = $("[data-home-compose-name]");
    const sidebarName = $("[data-sidebar-user-name]");

    if (nameEl) nameEl.textContent = name;
    if (homeName) homeName.textContent = name;
    if (composeName) composeName.textContent = name;
    if (sidebarName) sidebarName.textContent = name;
    if (statusEl) {
      statusEl.textContent = session?.emailVerified
        ? "Email verified"
        : "Verify email in profile";
    }
    applyAvatar(avatar, session);
    applyAvatar($("[data-home-user-avatar]"), session);
    applyAvatar($("[data-home-compose-avatar]"), session);
    const publicProfileLink = $("[data-account-menu-public-profile]");
    if (publicProfileLink && session?.nodeName) {
      publicProfileLink.href =
        "/@" + encodeURIComponent(String(session.nodeName).toLowerCase());
    }
    if (adminButton) {
      let adminUrl = session?.isAdmin ? (session?.adminUrl || "") : "";
      if (adminUrl && session?.nodeName && !/[?&]admin=/.test(adminUrl)) {
        adminUrl += (adminUrl.includes("?") ? "&" : "?") +
          "admin=" + encodeURIComponent(session.nodeName);
      }
      // Only show the button once we actually have somewhere to send it -
      // an admin session without adminUrl (ADMIN_PATH not picked up from the
      // Worker env yet) would otherwise show a button that links to "#".
      adminButton.classList.toggle("hidden", !adminUrl);
      adminButton.href = adminUrl || "#";
    }
    // Reconnect affordance next to the avatar: a signed-in user account with no
    // node linked has nothing authenticated to drain its issues/chats/etc. to a
    // desktop, so surface the re-link flow (the Settings > Nodes claim/link
    // panel) instead of leaving the data stuck online. Shown only when we can
    // affirmatively tell there are zero linked nodes (session.nodes present and
    // empty) for a user-like account — never for a node session or when the
    // link state is simply unknown, to avoid a false alarm.
    const reconnect = $("[data-node-reconnect]");
    if (reconnect) {
      reconnect.classList.toggle("hidden", !nodeNeedsReconnect(session));
    }
    renderProfileModal(session);
    renderProfilePage(session);
    renderProfileAbout(session);
    renderHeaderContext();
  }

  function dashboardMockRepositoriesEnabled() {
    const value = new URLSearchParams(location.search).get("mockRepos");
    return ["1", "true", "yes"].includes(String(value || "").trim().toLowerCase());
  }

  function dashboardMockRepositories() {
    const now = Date.now();
    const day = 24 * 60 * 60 * 1000;
    return [
      {
        owner: "demo-alice",
        name: "mesh-workbench",
        description: "A busy collaboration workspace with issues, pulls, discussions, releases, and active mirrors.",
        source: "local-node",
        liveHost: true,
        cloneOnline: true,
        isPrivate: false,
        language: "TypeScript",
        license: "Apache-2.0",
        channel: "stable",
        updatedAt: now - day,
        lastSync: now - 38 * 60 * 1000,
        sizeBytes: 18_400_000,
        issueCount: 14,
        openIssues: 14,
        closedIssueCount: 31,
        pullCount: 5,
        openPulls: 5,
        closedPullCount: 18,
        discussionCount: 9,
        commitCount: 428,
        branchCount: 7,
        releaseCount: 4,
        activityWeeks: [0, 1, 3, 2, 5, 1, 0, 4, 6, 2, 8, 3, 5, 9, 4, 2, 7, 10, 6, 5, 12, 8, 4, 9, 13, 7, 15, 10, 8, 12, 16, 11, 9, 14, 18, 12, 16, 20, 13, 15, 19, 17, 12, 10, 14, 9, 8, 11, 7, 6, 4, 8],
      },
      {
        owner: "demo-bravo",
        name: "mobile-mirror-client",
        description: "Mobile-first mirror node shell with offline queueing and handoff screens.",
        source: "remote-clone",
        liveHost: false,
        cloneOnline: true,
        isPrivate: false,
        language: "Swift",
        license: "MIT License",
        channel: "beta",
        updatedAt: now - 4 * day,
        lastSync: now - 2 * 60 * 60 * 1000,
        cloneUrl: `${location.origin}/demo-bravo/mobile-mirror-client.git`,
        sizeBytes: 9_800_000,
        issueCount: 7,
        openIssues: 7,
        closedIssueCount: 12,
        pullCount: 2,
        openPulls: 2,
        closedPullCount: 6,
        discussionCount: 4,
        commitCount: 156,
        branchCount: 4,
        releaseCount: 2,
        activityWeeks: [0, 0, 1, 0, 2, 1, 3, 0, 2, 4, 1, 3, 5, 2, 4, 3, 6, 2, 5, 4, 7, 3, 4, 6, 8, 5, 7, 6, 4, 8, 9, 5, 7, 6, 10, 8, 5, 7, 9, 4, 6, 8, 5, 7, 6, 4, 5, 3, 6, 4, 2, 5],
      },
      {
        owner: "demo-cora",
        name: "security-review-lab",
        description: "Private security review sandbox showing locked repository states and quieter activity.",
        source: "local-node",
        liveHost: false,
        cloneOnline: false,
        isPrivate: true,
        language: "Rust",
        license: "Proprietary",
        channel: "internal",
        updatedAt: now - 13 * day,
        lastSync: now - 8 * day,
        sizeBytes: 4_200_000,
        issueCount: 3,
        openIssues: 3,
        closedIssueCount: 8,
        pullCount: 1,
        openPulls: 1,
        closedPullCount: 3,
        discussionCount: 2,
        commitCount: 64,
        branchCount: 3,
        releaseCount: 0,
        activityWeeks: [0, 0, 0, 1, 0, 0, 2, 0, 1, 0, 3, 1, 0, 2, 0, 1, 3, 0, 2, 1, 0, 4, 1, 0, 2, 0, 3, 1, 0, 2, 4, 1, 0, 2, 1, 3, 0, 1, 2, 0, 3, 1, 0, 2, 0, 1, 3, 0, 1, 0, 2, 0],
      },
    ];
  }

  function setProfileModalOpen(open) {
    const modal = $("[data-profile-modal]");
    if (!modal) return;
    modal.classList.toggle("hidden", !open);
    modal.classList.toggle("flex", open);
    if (open) {
      renderProfileModal(state.session);
      window.setTimeout(() => $("[data-profile-password]")?.focus(), 0);
    }
  }

  function setProfileHint(text, cls) {
    const hint = $("[data-profile-hint]");
    if (!hint) return;
    hint.textContent = text || "";
    hint.className = "min-h-4 text-xs " + (cls === "bad" ? "text-destructive" : cls === "good" ? "text-primary" : "text-muted-foreground");
  }

  function setProfilePageHint(selector, text, cls) {
    const hint = $(selector);
    if (!hint) return;
    hint.textContent = text || "";
    hint.className = "min-h-4 text-xs " + (cls === "bad" ? "text-destructive" : cls === "good" ? "text-primary" : "text-muted-foreground");
  }

  function profilePassword(selector) {
    return ($(selector || "[data-profile-password]")?.value || "").trim();
  }

  function profileTxtValue(session = state.session) {
    const name = String(session?.nodeName || "").trim().toLowerCase();
    return name ? `forkmesh-profile=${name}` : "forkmesh-profile=username";
  }

  function profilePublicUrl(session = profileSubject()) {
    const name = String(session?.nodeName || "").trim().toLowerCase();
    return name ? `${location.origin}/@${name}` : `${location.origin}/@username`;
  }

  const PROFILE_TIMEZONE_FALLBACKS = [
    "UTC",
    "Africa/Cairo",
    "Africa/Johannesburg",
    "America/Anchorage",
    "America/Argentina/Buenos_Aires",
    "America/Bogota",
    "America/Chicago",
    "America/Denver",
    "America/Los_Angeles",
    "America/Mexico_City",
    "America/New_York",
    "America/Phoenix",
    "America/Sao_Paulo",
    "America/Toronto",
    "Asia/Bangkok",
    "Asia/Dubai",
    "Asia/Hong_Kong",
    "Asia/Kolkata",
    "Asia/Seoul",
    "Asia/Singapore",
    "Asia/Tokyo",
    "Australia/Melbourne",
    "Australia/Sydney",
    "Europe/Amsterdam",
    "Europe/Berlin",
    "Europe/London",
    "Europe/Madrid",
    "Europe/Paris",
    "Pacific/Auckland",
  ];

  function profileTimezoneOptions() {
    try {
      if (typeof Intl.supportedValuesOf === "function") {
        const zones = Intl.supportedValuesOf("timeZone");
        if (Array.isArray(zones) && zones.length) {
          return ["UTC", ...zones.filter((zone) => zone !== "UTC")];
        }
      }
    } catch {
      // Use the curated fallback below when browser support is unavailable.
    }
    return PROFILE_TIMEZONE_FALLBACKS;
  }

  function profileTimezoneGmtOffset(zone) {
    if (!zone) return "";
    try {
      const parts = new Intl.DateTimeFormat("en", {
        hour: "2-digit",
        minute: "2-digit",
        hour12: false,
        timeZone: zone,
        timeZoneName: "shortOffset",
      }).formatToParts(new Date());
      const value = parts.find((part) => part.type === "timeZoneName")?.value || "";
      if (value === "GMT") return "GMT+00:00";
      const match = value.match(/^GMT([+-])(\d{1,2})(?::?(\d{2}))?$/);
      if (!match) return value.startsWith("GMT") ? value : "";
      return `GMT${match[1]}${match[2].padStart(2, "0")}:${match[3] || "00"}`;
    } catch {
      return "";
    }
  }

  function profileTimezoneGmtLabel(zone) {
    const offset = profileTimezoneGmtOffset(zone);
    return offset ? `${offset} - ${zone}` : zone.replace(/_/g, " ");
  }

  function renderProfileTimezoneOptions(session = state.session) {
    const select = $("[data-profile-page-timezone]");
    if (!select) return;
    if (document.activeElement === select && select.options.length > 1) return;
    const current = String(session?.profileTimezone || select.value || "").trim();
    const zones = profileTimezoneOptions();
    const values = current && !zones.includes(current) ? [current, ...zones] : zones;
    const signature = values.join("\n");
    if (select.dataset.timezoneOptionsKey !== signature) {
      const placeholder = document.createElement("option");
      placeholder.value = "";
      placeholder.textContent = "Use browser time zone";
      select.replaceChildren(placeholder);
      values.forEach((zone) => {
        const option = document.createElement("option");
        option.value = zone;
        option.textContent = profileTimezoneGmtLabel(zone);
        select.append(option);
      });
      select.dataset.timezoneOptionsKey = signature;
    }
    select.value = current;
  }

  function defaultProfileAbout(session = profileSubject()) {
    const name = String(session?.nodeName || session?.email || "ForkMesh").trim() || "ForkMesh";
    if (session?.kind === "node") {
      const owner = String(session?.owner || "").trim();
      return `# ${name} is a ForkMesh node\n\nIt mirrors Git repositories and helps serve them to the network.` +
        (owner ? `\n\nOperated by @${owner}.` : "");
    }
    return `# Hi, I'm ${name}\n\nPinned profile content and public activity live here.`;
  }

  function profileAboutMarkdown(session = profileSubject()) {
    const value = String(session?.profileAbout || session?.profileReadme || "");
    return value || defaultProfileAbout(session);
  }

  function flushProfileAboutParagraph(out, paragraph) {
    if (!paragraph.length) return;
    out.push(`<p>${paragraph.map(escapeHtml).join("<br>")}</p>`);
    paragraph.length = 0;
  }

  function flushProfileAboutList(out, list) {
    if (!list.length) return;
    out.push(`<ul class="list-disc space-y-1 pl-5">${list.map((item) => `<li>${escapeHtml(item)}</li>`).join("")}</ul>`);
    list.length = 0;
  }

  function renderProfileMarkdown(markdown) {
    const lines = String(markdown || "").replace(/\r\n/g, "\n").split("\n");
    const out = [];
    const paragraph = [];
    const list = [];
    let inCode = false;
    let code = [];
    const flushText = () => {
      flushProfileAboutParagraph(out, paragraph);
      flushProfileAboutList(out, list);
    };
    for (const line of lines) {
      if (/^```/.test(line.trim())) {
        if (inCode) {
          out.push(`<pre class="overflow-auto rounded-md border border-border bg-background p-3 font-mono text-xs leading-5 text-muted-foreground"><code>${escapeHtml(code.join("\n"))}</code></pre>`);
          code = [];
          inCode = false;
        } else {
          flushText();
          inCode = true;
          code = [];
        }
        continue;
      }
      if (inCode) {
        code.push(line);
        continue;
      }
      if (!line.trim()) {
        flushText();
        continue;
      }
      const heading = line.match(/^(#{1,3})\s+(.+)$/);
      if (heading) {
        flushText();
        const level = heading[1].length;
        const size = level === 1 ? "text-2xl" : level === 2 ? "text-xl" : "text-base";
        out.push(`<h${level} class="${size} font-semibold text-foreground">${escapeHtml(heading[2])}</h${level}>`);
        continue;
      }
      const item = line.match(/^\s*[-*]\s+(.+)$/);
      if (item) {
        flushProfileAboutParagraph(out, paragraph);
        list.push(item[1]);
        continue;
      }
      flushProfileAboutList(out, list);
      paragraph.push(line);
    }
    flushText();
    if (inCode && code.length) {
      out.push(`<pre class="overflow-auto rounded-md border border-border bg-background p-3 font-mono text-xs leading-5 text-muted-foreground"><code>${escapeHtml(code.join("\n"))}</code></pre>`);
    }
    return out.length ? out.join("") : `<p>${escapeHtml(defaultProfileAbout()).replace(/\n/g, "<br>")}</p>`;
  }

  function renderProfileAbout(session) {
    if (profileMarkupOwnedByPublicProfile(session)) return;
    const owner = $("[data-profile-about-owner]");
    const label = $("[data-profile-about-label]");
    const body = $("[data-profile-about-body]");
    const name = String(session?.nodeName || session?.email || "forkmesh").trim() || "forkmesh";
    if (owner) owner.textContent = name;
    if (label) label.textContent = session?.kind === "node" ? "About this node" : "About yourself";
    if (body) {
      body.className = "grid gap-4 p-5 text-sm leading-6 text-foreground";
      body.innerHTML = renderProfileMarkdown(profileAboutMarkdown(session));
    }
  }

  function repoBelongsToProfile(repo, aliases) {
    const owner = String(repo?.owner || "").trim().toLowerCase();
    const canonical = repoCanonicalIdentity(repo);
    const canonicalOwner = String(canonical.owner || "").trim().toLowerCase();
    return Boolean((owner && aliases.has(owner)) || (canonicalOwner && aliases.has(canonicalOwner)));
  }

  const PROFILE_CONTRIBUTION_DAY_MS = 24 * 60 * 60 * 1000;
  const PROFILE_CONTRIBUTION_MAX_LEVEL = 6;
  const PROFILE_CONTRIBUTION_CACHE_PREFIX = "forkmesh.profileContributions:v1:";
  let profileContributionRequestSequence = 0;
  const PROFILE_CONTRIBUTION_CATEGORIES = [
    { key: "commits", label: "Commits", singular: "commit", color: "var(--contribution-commits)", icon: "git-commit-horizontal" },
    { key: "issues", label: "Issues", singular: "issue", color: "var(--contribution-issues)", icon: "circle-dot" },
    { key: "pulls", label: "Pull requests", singular: "pull request", color: "var(--contribution-pulls)", icon: "git-pull-request" },
    { key: "reviews", label: "Reviews", singular: "review", color: "var(--contribution-reviews)", icon: "message-square-check" },
    { key: "repositories", label: "Repositories", singular: "repository publication", color: "var(--contribution-repositories)", icon: "book-marked" },
  ];

  function profileRepositoryAliases(session = profileSubject()) {
    const aliases = new Set();
    const add = (value) => {
      const text = String(value || "").trim().toLowerCase();
      if (text) aliases.add(text);
    };
    add(session?.nodeName);
    add(session?.email);
    (Array.isArray(session?.nodes) ? session.nodes : []).forEach(add);
    return aliases;
  }

  function profileContributionIsoDate(date) {
    return new Date(date).toISOString().slice(0, 10);
  }

  function profileContributionDate(value) {
    const text = String(value || "");
    if (!/^\d{4}-\d{2}-\d{2}$/.test(text)) return null;
    const date = new Date(`${text}T00:00:00.000Z`);
    return Number.isNaN(date.getTime()) || profileContributionIsoDate(date) !== text ? null : date;
  }

  function profileContributionRange(period = "rolling", now = new Date()) {
    const value = String(period || "rolling");
    const yearMatch = /^(?:year:)?(\d{4})$/.exec(value);
    if (yearMatch) {
      const year = Math.max(1971, Math.min(9998, Number(yearMatch[1])));
      return {
        value: String(year),
        from: `${year}-01-01`,
        to: `${year}-12-31`,
        label: String(year),
      };
    }
    const end = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate()));
    const start = new Date(end.getTime() - (364 * PROFILE_CONTRIBUTION_DAY_MS));
    return {
      value: "rolling",
      from: profileContributionIsoDate(start),
      to: profileContributionIsoDate(end),
      label: "the last year",
    };
  }

  function profileContributionCount(value) {
    const number = Number(value);
    return Number.isSafeInteger(number) && number > 0 ? number : 0;
  }

  function profileContributionSubjectName() {
    const subject = profileSubject();
    return String(publicProfileNameFromPath() || subject?.nodeName || subject?.email || "")
      .trim().replace(/^@/, "");
  }

  function profileContributionRequestKey(name, range) {
    return `${String(name).toLowerCase()}:${range.from}:${range.to}`;
  }

  function profileContributionCacheKey(requestKey) {
    return PROFILE_CONTRIBUTION_CACHE_PREFIX + requestKey;
  }

  function readProfileContributionCache(requestKey, range) {
    try {
      const parsed = JSON.parse(sessionStorage.getItem(profileContributionCacheKey(requestKey)) || "null");
      const data = parsed?.data;
      if (!data || data.ok !== true || data.range?.from !== range.from || data.range?.to !== range.to) return null;
      return data;
    } catch (_) {
      return null;
    }
  }

  function writeProfileContributionCache(requestKey, data) {
    try {
      sessionStorage.setItem(profileContributionCacheKey(requestKey), JSON.stringify({ data, savedAt: Date.now() }));
    } catch (_) {
      // A disabled or full session store only removes the offline fallback.
    }
  }

  function removeProfileContributionCache(requestKey) {
    try {
      sessionStorage.removeItem(profileContributionCacheKey(requestKey));
    } catch (_) {}
    delete state.profileContributions.cache[requestKey];
  }

  function profileContributionCategoryKnown(coverage, category, dateValue = "") {
    if (category === "repositories") return true;
    if (coverage?.status === "complete") return true;
    if (coverage?.status === "unavailable") return false;
    const missing = new Set(Array.isArray(coverage?.missing) ? coverage.missing : []);
    if (category === "commits" && missing.has("commits")) return false;
    if (["issues", "pulls", "reviews"].includes(category) && missing.has("collaboration")) return false;
    if (category === "languages" && missing.has("languages")) return false;
    if (["commits", "issues", "pulls", "reviews"].includes(category) &&
        missing.has("history")) {
      const verifiedFrom = profileContributionDate(coverage?.verifiedFrom);
      const date = profileContributionDate(dateValue);
      if (!verifiedFrom || !date || date < verifiedFrom) return false;
    }
    return true;
  }

  function normalizeProfileContributionDays(data, range = state.profileContributions.range) {
    const start = profileContributionDate(range?.from);
    const end = profileContributionDate(range?.to);
    if (!start || !end || end < start) return [];
    const sparse = new Map();
    (Array.isArray(data?.days) ? data.days : []).forEach((row) => {
      const date = profileContributionDate(row?.date);
      if (!date || date < start || date > end) return;
      const normalized = { date: profileContributionIsoDate(date) };
      PROFILE_CONTRIBUTION_CATEGORIES.forEach(({ key }) => {
        normalized[key] = profileContributionCount(row?.[key]);
      });
      sparse.set(normalized.date, normalized);
    });
    const coverage = data?.coverage || {};
    const length = Math.min(366, Math.floor((end - start) / PROFILE_CONTRIBUTION_DAY_MS) + 1);
    return Array.from({ length }, (_, index) => {
      const date = new Date(start.getTime() + (index * PROFILE_CONTRIBUTION_DAY_MS));
      const key = profileContributionIsoDate(date);
      const explicit = sparse.get(key);
      const day = explicit || { date: key };
      PROFILE_CONTRIBUTION_CATEGORIES.forEach(({ key: category }) => {
        day[category] = profileContributionCount(day[category]);
      });
      day.known = Object.fromEntries(PROFILE_CONTRIBUTION_CATEGORIES.map(({ key: category }) => [
        category,
        profileContributionCategoryKnown(coverage, category, key),
      ]));
      day.total = PROFILE_CONTRIBUTION_CATEGORIES.reduce(
        (sum, { key: category }) => sum + day[category], 0,
      );
      day.verified = PROFILE_CONTRIBUTION_CATEGORIES.every(
        ({ key: category }) => day.known[category],
      );
      return day;
    });
  }

  function profileContributionStackLevel(count) {
    const value = profileContributionCount(count);
    return value ? Math.min(PROFILE_CONTRIBUTION_MAX_LEVEL, 1 + Math.floor(Math.log2(value))) : 0;
  }

  function profileContributionCubeFaces(x, y, height, halfWidth = 7, depth = 4) {
    const topY = y - height;
    return {
      top: `${x},${topY} ${x + halfWidth},${topY + depth} ${x},${topY + (2 * depth)} ${x - halfWidth},${topY + depth}`,
      left: `${x - halfWidth},${topY + depth} ${x},${topY + (2 * depth)} ${x},${y + (2 * depth)} ${x - halfWidth},${y + depth}`,
      right: `${x + halfWidth},${topY + depth} ${x},${topY + (2 * depth)} ${x},${y + (2 * depth)} ${x + halfWidth},${y + depth}`,
    };
  }

  function profileContributionTooltipText(day) {
    const date = profileContributionDate(day?.date);
    const dateLabel = date
      ? new Intl.DateTimeFormat(undefined, { dateStyle: "long", timeZone: "UTC" }).format(date)
      : "Unknown date";
    const counts = PROFILE_CONTRIBUTION_CATEGORIES.map(({ key, label, singular }) => {
      const count = profileContributionCount(day?.[key]);
      const known = day?.known?.[key] ?? Boolean(day?.verified);
      if (!known) {
        return count
          ? `${formatCount(count)} known ${singular}${count === 1 ? "" : "s"} (partial)`
          : `${label.toLowerCase()} not fully verified`;
      }
      return `${formatCount(count)} ${singular}${count === 1 ? "" : "s"}`;
    });
    return `${dateLabel}: ${counts.join(", ")}`;
  }

  function profileContributionRadarPoints(typeTotals, coverage = {}, radius = 62, centerX = 120, centerY = 88) {
    const counts = PROFILE_CONTRIBUTION_CATEGORIES.map(({ key }) => profileContributionCount(typeTotals?.[key]));
    const total = counts.reduce((sum, count) => sum + count, 0);
    const shares = counts.map((count) => total ? count / total : 0);
    return PROFILE_CONTRIBUTION_CATEGORIES.map((category, index) => {
      const angle = (-Math.PI / 2) + ((Math.PI * 2 * index) / PROFILE_CONTRIBUTION_CATEGORIES.length);
      const visualShare = shares[index];
      return {
        ...category,
        count: counts[index],
        share: shares[index],
        known: profileContributionCategoryKnown(coverage, category.key),
        x: centerX + (Math.cos(angle) * radius * visualShare),
        y: centerY + (Math.sin(angle) * radius * visualShare),
        axisX: centerX + (Math.cos(angle) * radius),
        axisY: centerY + (Math.sin(angle) * radius),
        labelX: centerX + (Math.cos(angle) * (radius + 24)),
        labelY: centerY + (Math.sin(angle) * (radius + 19)),
      };
    });
  }

  function profileContributionLanguageSegments(languages) {
    const byName = new Map();
    (Array.isArray(languages) ? languages : []).forEach((language) => {
      const name = String(language?.name || "").trim().slice(0, 48);
      const bytes = profileContributionCount(language?.bytes);
      if (!name || !bytes) return;
      const previous = byName.get(name) || { name, bytes: 0, color: "" };
      previous.bytes += bytes;
      if (/^#[0-9a-f]{6}$/i.test(String(language?.color || ""))) previous.color = language.color;
      byName.set(name, previous);
    });
    const explicitOther = byName.get("Other");
    byName.delete("Other");
    const ranked = [...byName.values()].sort((a, b) => b.bytes - a.bytes || a.name.localeCompare(b.name));
    const top = ranked.slice(0, 5);
    const otherBytes = ranked.slice(5).reduce((sum, item) => sum + item.bytes, 0) + (explicitOther?.bytes || 0);
    if (otherBytes) top.push({ name: "Other", bytes: otherBytes, color: "var(--contribution-unverified)" });
    const total = top.reduce((sum, item) => sum + item.bytes, 0);
    let offset = 0;
    return top.map((item) => {
      const percentage = total ? (item.bytes / total) * 100 : 0;
      const segment = {
        ...item,
        color: item.color || "var(--contribution-unverified)",
        percentage,
        offset,
      };
      offset += percentage;
      return segment;
    });
  }

  function profileContributionCoverageMessage(coverage, range, { cached = false } = {}) {
    if (cached) return "Showing saved verified data because the live ledger is unavailable.";
    const verifiedFrom = profileContributionDate(coverage?.verifiedFrom);
    const verifiedLabel = verifiedFrom
      ? new Intl.DateTimeFormat(undefined, { dateStyle: "medium", timeZone: "UTC" }).format(verifiedFrom)
      : range?.from;
    const missing = (Array.isArray(coverage?.missing) ? coverage.missing : [])
      .map((item) => String(item || "").trim()).filter(Boolean).slice(0, 4);
    const missingLabels = missing.map((item) => ({
      commits: "commit history",
      collaboration: "collaboration events",
      languages: "language footprint",
      history: "earlier history",
    })[item] || item);
    if (coverage?.status === "partial") {
      return `Partial verified history from ${verifiedLabel || "the available sources"}${missingLabels.length ? `. Missing ${missingLabels.join(", ")}.` : "."}`;
    }
    if (coverage?.status === "unavailable") return "Verified contribution history is not available yet.";
    return `Complete verified public history for ${range?.label || "this period"}.`;
  }

  function renderProfileContributionStatus(status, { hasData = false } = {}) {
    const card = $("[data-profile-contribution-card]");
    const loading = $("[data-profile-contribution-loading]");
    const empty = $("[data-profile-contribution-empty]");
    const error = $("[data-profile-contribution-error]");
    const panorama = $("[data-profile-contribution-panorama]");
    const activityItems = $("[data-profile-activity-items]");
    const activityEmpty = $("[data-profile-activity-empty]");
    if (card) card.setAttribute("aria-busy", status === "loading" ? "true" : "false");
    if (loading) loading.hidden = status !== "loading";
    if (empty) empty.hidden = status !== "empty";
    if (error) error.hidden = status !== "error";
    if (panorama) panorama.hidden = !hasData;
    if (status === "loading" || status === "error") {
      if (activityItems) activityItems.innerHTML = "";
      if (activityEmpty) {
        activityEmpty.classList.remove("hidden");
        activityEmpty.textContent = status === "loading"
          ? "Loading verified contribution activity..."
          : "Contribution activity is unavailable right now.";
      }
    }
  }

  function profileContributionPeriods(data, activeValue) {
    const periods = [{ value: "rolling", label: "Last year" }];
    const currentYear = new Date().getUTCFullYear();
    for (let year = currentYear - 1; year >= currentYear - 5; year -= 1) {
      periods.push({ value: String(year), label: String(year) });
    }
    if (/^\d{4}$/.test(String(activeValue)) && !periods.some((period) => period.value === String(activeValue))) {
      periods.push({ value: String(activeValue), label: String(activeValue) });
    }
    return periods;
  }

  function renderProfileContributionPeriods(data, activeValue) {
    const container = $("[data-profile-contribution-periods]");
    if (!container) return;
    container.innerHTML = profileContributionPeriods(data, activeValue).map((period) => {
      const active = period.value === activeValue;
      return `<button data-profile-contribution-period="${escapeHtml(period.value)}" type="button" aria-current="${active ? "true" : "false"}" class="shrink-0 rounded-md px-3 py-1.5 font-semibold ${active ? "bg-secondary text-foreground" : "text-muted-foreground hover:bg-secondary hover:text-foreground"}">${escapeHtml(period.label)}</button>`;
    }).join("");
  }

  function showProfileContributionTooltip(day, target, event) {
    const tooltip = $("[data-profile-contribution-tooltip]");
    const panel = $("[data-profile-contribution-skyline-panel]");
    if (!tooltip || !panel) return;
    tooltip.textContent = profileContributionTooltipText(day);
    tooltip.hidden = false;
    const panelRect = panel.getBoundingClientRect();
    const targetRect = target?.getBoundingClientRect?.() || panelRect;
    const pointerX = Number(event?.clientX) || targetRect.left + (targetRect.width / 2);
    const pointerY = Number(event?.clientY) || targetRect.top;
    const left = Math.max(12, Math.min(panelRect.width - tooltip.offsetWidth - 12, pointerX - panelRect.left + 10));
    const top = Math.max(56, Math.min(panelRect.height - tooltip.offsetHeight - 12, pointerY - panelRect.top - tooltip.offsetHeight - 8));
    tooltip.style.left = `${left}px`;
    tooltip.style.top = `${top}px`;
    state.profileContributions.selectedDay = day.date;
  }

  function hideProfileContributionTooltip() {
    const tooltip = $("[data-profile-contribution-tooltip]");
    if (tooltip) tooltip.hidden = true;
    state.profileContributions.selectedDay = "";
  }

  function renderProfileContributionSkyline(data, range) {
    const svg = $("[data-profile-contribution-skyline]");
    if (!svg) return;
    const days = normalizeProfileContributionDays(data, range).map((day, index) => {
      const week = Math.floor(index / 7);
      const weekday = index % 7;
      return {
        ...day,
        week,
        weekday,
        x: 96 + (week * 15) - (weekday * 8),
        y: 78 + (week * 3.4) + (weekday * 4.5),
        depth: week + weekday,
      };
    }).sort((a, b) => a.y - b.y || a.x - b.x);
    const content = days.map((day) => {
      const base = profileContributionCubeFaces(day.x, day.y, 0);
      const fill = day.verified ? "var(--contribution-empty)" : "var(--contribution-unverified)";
      let height = 0;
      const segments = PROFILE_CONTRIBUTION_CATEGORIES.map((category) => {
        const level = profileContributionStackLevel(day[category.key]);
        if (!level) return "";
        const segmentHeight = level * 2.5;
        const faces = profileContributionCubeFaces(day.x, day.y - height, segmentHeight);
        height += segmentHeight;
        return `<g data-contribution-kind="${category.key}"><polygon points="${faces.left}" fill="${category.color}" opacity="0.62"></polygon><polygon points="${faces.right}" fill="${category.color}" opacity="0.82"></polygon><polygon points="${faces.top}" fill="${category.color}"></polygon></g>`;
      }).join("");
      const focus = day.total ? ` tabindex="0" role="img" aria-label="${escapeHtml(profileContributionTooltipText(day))}"` : "";
      return `<g data-profile-contribution-day="${day.date}"${focus}><polygon points="${base.top}" fill="${fill}" stroke="var(--contribution-grid-edge)" stroke-width="0.65"></polygon>${segments}</g>`;
    }).join("");
    svg.innerHTML = `<title id="profile-contribution-skyline-title">Daily contribution skyline</title><desc id="profile-contribution-skyline-desc">Verified commits, issues, pull requests, reviews, and repository publications from ${escapeHtml(range.from)} through ${escapeHtml(range.to)}.</desc>${content}`;
    const byDate = new Map(days.map((day) => [day.date, day]));
    svg.querySelectorAll("[data-profile-contribution-day][tabindex='0']").forEach((group) => {
      const day = byDate.get(group.dataset.profileContributionDay);
      group.addEventListener("mouseenter", (event) => showProfileContributionTooltip(day, group, event));
      group.addEventListener("mousemove", (event) => showProfileContributionTooltip(day, group, event));
      group.addEventListener("mouseleave", hideProfileContributionTooltip);
      group.addEventListener("focus", () => showProfileContributionTooltip(day, group));
      group.addEventListener("blur", hideProfileContributionTooltip);
    });
    const viewport = $("[data-profile-contribution-skyline-viewport]");
    window.requestAnimationFrame(() => {
      const scrollToRecent = () => {
        if (viewport && viewport.scrollWidth > viewport.clientWidth) {
          viewport.scrollLeft = viewport.scrollWidth - viewport.clientWidth;
        }
      };
      scrollToRecent();
      window.requestAnimationFrame(scrollToRecent);
    });
  }

  function renderProfileContributionRadar(data) {
    const svg = $("[data-profile-contribution-radar]");
    if (!svg) return;
    const points = profileContributionRadarPoints(
      data?.typeTotals || {}, data?.coverage || {},
    );
    const rings = [0.25, 0.5, 0.75, 1].map((scale) => {
      const ring = points.map((point) => {
        const x = 120 + ((point.axisX - 120) * scale);
        const y = 88 + ((point.axisY - 88) * scale);
        return `${x.toFixed(1)},${y.toFixed(1)}`;
      }).join(" ");
      return `<polygon points="${ring}" fill="none" stroke="var(--contribution-grid-edge)" stroke-width="1"></polygon>`;
    }).join("");
    const axes = points.map((point) => `<line x1="120" y1="88" x2="${point.axisX.toFixed(1)}" y2="${point.axisY.toFixed(1)}" stroke="var(--contribution-grid-edge)" stroke-width="1"></line>`).join("");
    const shape = points.map((point) => `${point.x.toFixed(1)},${point.y.toFixed(1)}`).join(" ");
    const labels = points.map((point) => {
      const anchor = point.labelX < 112 ? "end" : point.labelX > 128 ? "start" : "middle";
      const countLabel = `${formatCount(point.count)}${point.known ? "" : "+"}`;
      return `<text x="${point.labelX.toFixed(1)}" y="${point.labelY.toFixed(1)}" text-anchor="${anchor}" fill="currentColor" font-size="9"><tspan x="${point.labelX.toFixed(1)}">${escapeHtml(point.label)}</tspan><tspan x="${point.labelX.toFixed(1)}" dy="11" fill="rgb(var(--dashboard-muted-foreground-rgb))">${countLabel}</tspan></text>`;
    }).join("");
    const description = points.map((point) => point.known
      ? `${point.label}: ${formatCount(point.count)}`
      : `${point.label}: at least ${formatCount(point.count)}, partial coverage`).join(", ");
    svg.innerHTML = `<title id="profile-contribution-radar-title">Contribution activity mix</title><desc id="profile-contribution-radar-desc">${escapeHtml(description)}</desc><g>${rings}${axes}<polygon points="${shape}" fill="var(--contribution-commits)" fill-opacity="0.24" stroke="var(--contribution-commits)" stroke-width="2"></polygon>${labels}</g>`;
  }

  function renderProfileContributionLanguages(data) {
    const svg = $("[data-profile-contribution-languages]");
    const legend = $("[data-profile-contribution-language-legend]");
    if (!svg || !legend) return;
    const segments = profileContributionLanguageSegments(data?.languages);
    const complete = profileContributionCategoryKnown(data?.coverage, "languages");
    if (!segments.length) {
      const message = complete ? "No language data" : "Language data partial";
      const description = complete
        ? "No supported language bytes are available."
        : "Language coverage is not fully verified for this profile.";
      svg.innerHTML = `<title id="profile-contribution-languages-title">Contribution language donut</title><desc id="profile-contribution-languages-desc">${description}</desc><circle cx="120" cy="82" r="48" fill="none" stroke="var(--contribution-empty)" stroke-width="18"></circle><text x="120" y="86" text-anchor="middle" fill="currentColor" font-size="11">${message}</text>`;
      legend.innerHTML = complete ? "" : '<p class="text-[10px] font-semibold uppercase tracking-wide text-muted-foreground">Partial language data</p>';
      return;
    }
    const circles = segments.map((segment) => `<circle cx="120" cy="82" r="48" pathLength="100" fill="none" stroke="${segment.color}" stroke-width="18" stroke-dasharray="${segment.percentage.toFixed(4)} ${(100 - segment.percentage).toFixed(4)}" stroke-dashoffset="${(-segment.offset).toFixed(4)}" transform="rotate(-90 120 82)"><title>${escapeHtml(segment.name)}: ${segment.percentage.toFixed(1)}%, ${formatCount(segment.bytes)} bytes</title></circle>`).join("");
    const description = `${complete ? "" : "Partial language coverage. "}${segments.map((segment) => `${segment.name}: ${segment.percentage.toFixed(1)} percent`).join(", ")}`;
    svg.innerHTML = `<title id="profile-contribution-languages-title">Contribution language donut</title><desc id="profile-contribution-languages-desc">${escapeHtml(description)}</desc>${circles}<text x="120" y="78" text-anchor="middle" fill="currentColor" font-size="11">Public code</text><text x="120" y="94" text-anchor="middle" fill="rgb(var(--dashboard-muted-foreground-rgb))" font-size="9">by bytes</text>`;
    legend.innerHTML = `${complete ? "" : '<p class="mb-1 text-[10px] font-semibold uppercase tracking-wide text-muted-foreground">Partial language data</p>'}${segments.map((segment) => `<div class="flex min-w-0 items-center justify-between gap-2"><span class="flex min-w-0 items-center gap-2"><span aria-hidden="true" class="h-2.5 w-2.5 shrink-0 rounded-full" style="background:${segment.color}"></span><span class="truncate">${escapeHtml(segment.name)}</span></span><span class="shrink-0 tabular-nums">${segment.percentage.toFixed(1)}%</span></div>`).join("")}`;
  }

  function renderProfileContributionMetrics(data) {
    const metrics = $("[data-profile-contribution-metrics]");
    if (!metrics) return;
    metrics.innerHTML = PROFILE_CONTRIBUTION_CATEGORIES.map((category) => {
      const known = profileContributionCategoryKnown(data?.coverage, category.key);
      const count = formatCount(profileContributionCount(data?.typeTotals?.[category.key]));
      return `<div><dt class="truncate text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">${escapeHtml(category.label)}</dt><dd class="mt-1 text-lg font-semibold tabular-nums text-foreground">${count}${known ? "" : "+"}</dd>${known ? "" : '<span class="text-[10px] text-muted-foreground">partial</span>'}</div>`;
    }).join("");
  }

  function renderProfileContributionCoverage(data, range, { cached = false } = {}) {
    const coverage = $("[data-profile-contribution-coverage]");
    if (!coverage) return;
    coverage.textContent = profileContributionCoverageMessage(data?.coverage, range, { cached });
    if (cached) {
      const retry = document.createElement("button");
      retry.type = "button";
      retry.setAttribute("data-profile-contribution-retry", "");
      retry.className = "ml-2 font-semibold text-accent hover:underline";
      retry.textContent = "Retry live data";
      coverage.append(retry);
    }
  }

  function profileContributionRepositoryUrl(repository) {
    const supplied = String(repository?.url || "");
    if (supplied.startsWith("/") && !supplied.startsWith("//")) return supplied;
    const owner = String(repository?.owner || "").trim();
    const name = String(repository?.name || "").trim();
    return owner && name ? `/${encodeURIComponent(owner)}/${encodeURIComponent(name)}` : "/dashboard/repos";
  }

  function renderProfileActivity(data) {
    const container = $("[data-profile-activity-items]");
    const empty = $("[data-profile-activity-empty]");
    if (!container || !empty) return;
    const events = (Array.isArray(data?.recentActivity) ? data.recentActivity : []).slice(0, 20);
    empty.classList.toggle("hidden", Boolean(events.length));
    empty.textContent = "No contribution activity found for this period yet.";
    container.innerHTML = events.map((event) => {
      const category = PROFILE_CONTRIBUTION_CATEGORIES.find((item) => item.key === event?.kind) || PROFILE_CONTRIBUTION_CATEGORIES[0];
      const count = profileContributionCount(event?.count);
      const repository = event?.repository || {};
      const repoLabel = [repository.owner, repository.name].filter(Boolean).join("/") || "Public repository";
      const date = profileContributionDate(event?.date);
      const dateLabel = date
        ? new Intl.DateTimeFormat(undefined, { month: "short", day: "numeric", year: "numeric", timeZone: "UTC" }).format(date)
        : String(event?.date || "");
      return `<div data-profile-activity-item class="grid grid-cols-[2.5rem_minmax(0,1fr)] gap-4 md:grid-cols-[2.5rem_minmax(0,1fr)_8rem]"><div class="flex flex-col items-center"><span class="flex h-10 w-10 items-center justify-center rounded-full bg-secondary" style="color:${category.color}"><i data-lucide="${category.icon}" class="h-5 w-5"></i></span><span class="h-12 w-px bg-border"></span></div><div class="min-w-0"><p class="text-base font-semibold leading-6 text-foreground">${formatCount(count)} ${escapeHtml(category.singular)}${count === 1 ? "" : "s"}</p><p class="mt-2 flex min-w-0 items-center gap-2 text-sm"><i data-lucide="git-fork" class="h-4 w-4 shrink-0 text-muted-foreground"></i><a href="${escapeHtml(profileContributionRepositoryUrl(repository))}" class="truncate text-accent hover:underline">${escapeHtml(repoLabel)}</a></p><time datetime="${escapeHtml(event?.date || "")}" class="mt-2 block text-xs text-muted-foreground md:hidden">${escapeHtml(dateLabel)}</time></div><div class="hidden items-start justify-end text-sm text-muted-foreground md:flex"><time datetime="${escapeHtml(event?.date || "")}">${escapeHtml(dateLabel)}</time></div></div>`;
    }).join("");
    window.lucide?.createIcons();
  }

  function renderProfileContributionData(data, range, { cached = false } = {}) {
    const total = profileContributionCount(data?.total);
    const repositoryCount = profileContributionCount(data?.repositoryCount);
    const summary = $("[data-profile-contribution-summary]");
    const rangeLabel = $("[data-profile-contribution-range]");
    if (summary) {
      summary.textContent = `${formatCount(total)} verified public contribution${total === 1 ? "" : "s"} recorded across ${formatCount(repositoryCount)} repositor${repositoryCount === 1 ? "y" : "ies"} in ${range.label}`;
    }
    if (rangeLabel) rangeLabel.textContent = `${range.from} to ${range.to} · UTC`;
    renderProfileContributionPeriods(data, range.value);
    renderProfileContributionSkyline(data, range);
    renderProfileContributionRadar(data);
    renderProfileContributionLanguages(data);
    renderProfileContributionMetrics(data);
    renderProfileContributionCoverage(data, range, { cached });
    renderProfileActivity(data);
    const coverageStatus = ["complete", "partial", "unavailable"].includes(data?.coverage?.status)
      ? data.coverage.status
      : "unavailable";
    const status = cached ? "cached" : (!total && coverageStatus === "complete" ? "empty" : coverageStatus);
    renderProfileContributionStatus(status, { hasData: true });
  }

  function renderProfileContributionPlaceholder(range) {
    const placeholder = {
      typeTotals: Object.fromEntries(PROFILE_CONTRIBUTION_CATEGORIES.map(({ key }) => [key, 0])),
      days: [],
      languages: [],
      coverage: {
        status: "unavailable",
        verifiedFrom: null,
        missing: ["commits", "collaboration", "languages", "history"],
      },
    };
    const summary = $("[data-profile-contribution-summary]");
    const rangeLabel = $("[data-profile-contribution-range]");
    const coverage = $("[data-profile-contribution-coverage]");
    if (summary) summary.textContent = `Loading verified contributions for ${range.label}`;
    if (rangeLabel) rangeLabel.textContent = `${range.from} to ${range.to} · UTC`;
    if (coverage) coverage.textContent = "Waiting for the verified public ledger.";
    renderProfileContributionSkyline(placeholder, range);
    renderProfileContributionRadar(placeholder);
    renderProfileContributionLanguages(placeholder);
    renderProfileContributionMetrics(placeholder);
  }

  async function renderProfileContributionGraph({ period, force = false } = {}) {
    if (!$("[data-profile-contribution-card]")) return;
    const activeValue = String(period || state.profileContributions.range?.value || "rolling");
    const range = profileContributionRange(activeValue);
    const name = profileContributionSubjectName();
    renderProfileContributionPeriods(state.profileContributions.data, range.value);
    renderProfileContributionPlaceholder(range);
    if (!name) {
      state.profileContributions.error = "Profile identity is unavailable.";
      renderProfileContributionStatus("error", { hasData: true });
      return;
    }
    const requestKey = profileContributionRequestKey(name, range);
    const memory = state.profileContributions.cache[requestKey];
    state.profileContributions.range = range;
    if (!force && memory) {
      profileContributionRequestSequence += 1;
      state.profileContributions.requestKey = requestKey;
      state.profileContributions.data = memory;
      state.profileContributions.loading = false;
      state.profileContributions.error = "";
      renderProfileContributionData(memory, range);
      return;
    }
    if (!force && state.profileContributions.loading && state.profileContributions.requestKey === requestKey) return;
    const requestSequence = ++profileContributionRequestSequence;
    state.profileContributions.requestKey = requestKey;
    state.profileContributions.loading = true;
    state.profileContributions.error = "";
    renderProfileContributionStatus("loading", { hasData: true });
    const path = "/api/accounts/" + encodeURIComponent(name) + "/contributions?from=" +
      encodeURIComponent(range.from) + "&to=" + encodeURIComponent(range.to);
    try {
      const data = await fetchJson(path, { fresh: force, cacheBust: false });
      if (state.profileContributions.requestKey !== requestKey || requestSequence !== profileContributionRequestSequence) return;
      if (data?.ok !== true || data?.range?.from !== range.from || data?.range?.to !== range.to) {
        throw new Error("Contribution response did not match the requested period.");
      }
      state.profileContributions.cache[requestKey] = data;
      state.profileContributions.data = data;
      state.profileContributions.loading = false;
      writeProfileContributionCache(requestKey, data);
      renderProfileContributionData(data, range);
    } catch (error) {
      if (state.profileContributions.requestKey !== requestKey || requestSequence !== profileContributionRequestSequence) return;
      state.profileContributions.loading = false;
      state.profileContributions.error = String(error?.message || "Contribution activity is unavailable.");
      const status = Number(error?.status) || 0;
      const temporaryFailure = status === 0 || status === 408 || status === 425 ||
        status === 429 || status >= 500;
      if (!temporaryFailure) removeProfileContributionCache(requestKey);
      const cached = temporaryFailure
        ? readProfileContributionCache(requestKey, range)
        : null;
      if (cached) {
        state.profileContributions.data = cached;
        renderProfileContributionData(cached, range, { cached: true });
      } else {
        const summary = $("[data-profile-contribution-summary]");
        if (summary) summary.textContent = `Contribution activity is unavailable for ${range.label}`;
        renderProfileContributionStatus("error", { hasData: true });
      }
    }
  }

  function setProfileAboutHint(text, cls) {
    setProfilePageHint("[data-profile-about-hint]", text, cls);
  }

  function setProfileAboutModalOpen(open) {
    const modal = $("[data-profile-about-modal]");
    if (!modal) return;
    modal.classList.toggle("hidden", !open);
    modal.classList.toggle("flex", open);
    if (open) {
      const textarea = $("[data-profile-about-textarea]");
      if (textarea) textarea.value = profileAboutMarkdown(state.session);
      setProfileAboutHint("", "");
      window.setTimeout(() => textarea?.focus(), 0);
    }
  }

  function validNodeName(value) {
    return /^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(String(value || ""));
  }

  // A node's Ed25519 public key (raw 32 bytes, base64url, unpadded) - the
  // value the desktop app's own profile card labels "Node ID" (issue #351),
  // so the claim-node input below must accept it alongside the account name.
  function validNodePubkey(value) {
    return /^[A-Za-z0-9_-]{43}$/.test(String(value || ""));
  }

  const NOTIFICATION_PREFERENCE_DEFAULTS = {
    mention: true,
    subscribed: true,
    pull_submitted: true,
    issue_assigned: true,
    repo_shared: true,
    bounty_funded: true,
    bounty_paid: true,
    release_published: true,
    pending_inbox: true,
    credits_refilled: true,
    general_chat: true,
    host_online: false,
    host_offline: false,
  };

  function normalizedNotificationPreferences(session = state.session) {
    const raw = session?.notificationPreferences;
    const prefs = { ...NOTIFICATION_PREFERENCE_DEFAULTS };
    if (raw && typeof raw === "object") {
      Object.keys(prefs).forEach((key) => {
        if (Object.prototype.hasOwnProperty.call(raw, key)) prefs[key] = Boolean(raw[key]);
      });
    }
    return prefs;
  }

  function renderNotificationPreferences(session = state.session) {
    const prefs = normalizedNotificationPreferences(session);
    $$("[data-notification-pref]").forEach((input) => {
      const key = input.dataset.notificationPref || "";
      if (Object.prototype.hasOwnProperty.call(prefs, key)) {
        input.checked = Boolean(prefs[key]);
      }
    });
  }

  function collectNotificationPreferences() {
    const prefs = { ...NOTIFICATION_PREFERENCE_DEFAULTS };
    $$("[data-notification-pref]").forEach((input) => {
      const key = input.dataset.notificationPref || "";
      if (Object.prototype.hasOwnProperty.call(prefs, key)) {
        prefs[key] = Boolean(input.checked);
      }
    });
    return prefs;
  }

  function setRenameStatus(text, cls) {
    setProfilePageHint("[data-profile-rename-status]", text, cls);
  }

  function updateRenameButton() {
    const button = $("[data-profile-rename-save]");
    if (!button) return;
    button.disabled = !(
      state.nodeNameAvailability.available &&
      Boolean(state.session?.emailVerified) &&
      Boolean(profilePassword("[data-profile-rename-password]"))
    );
    button.classList.toggle("opacity-40", button.disabled);
  }

  function renderProfileModal(session) {
    const emailStatus = $("[data-profile-email-status]");
    const verifyButton = $("[data-profile-verify-email]");
    const solanaInput = $("[data-profile-solana]");
    if (emailStatus) {
      emailStatus.textContent = session?.emailVerified
        ? `${session.email || "Email"} is verified.`
        : `${session?.email || "Your email"} is not verified yet.`;
    }
    if (verifyButton) {
      verifyButton.disabled = Boolean(session?.emailVerified);
      verifyButton.classList.toggle("opacity-40", Boolean(session?.emailVerified));
      verifyButton.textContent = session?.emailVerified ? "Verified" : "Send link";
    }
    if (solanaInput && document.activeElement !== solanaInput) {
      solanaInput.value = session?.solana || "";
    }
  }

  function profileSidebarMarkup(session) {
    const template = $("[data-profile-sidebar-template]");
    return template ? template.innerHTML.trim() : "";
  }

  function renderProfileSidebars(session) {
    const markup = profileSidebarMarkup(session);
    $$("[data-profile-sidebar-slot]").forEach((slot) => {
      if (slot.dataset.profileSidebarRendered === "true" && slot.dataset.profileSidebarMarkup === markup) return;
      slot.innerHTML = markup;
      slot.dataset.profileSidebarRendered = "true";
      slot.dataset.profileSidebarMarkup = markup;
    });
  }

  function renderProfilePage(session) {
    if (profileMarkupOwnedByPublicProfile(session)) return;
    const name = session?.nodeName || "My Profile";
    const email = session?.email || "No email on file";
    renderProfileSidebars(session);
    const avatars = $$('[data-profile-page-avatar]');
    const nameEls = $$('[data-profile-page-node-name]');
    const emailEls = $$('[data-profile-page-email]');
    const bioEls = $$("[data-profile-bio]");
    const followersEls = $$("[data-profile-followers-count]");
    const followingEls = $$("[data-profile-following-count]");
    const mirrorsEls = $$("[data-profile-mirrors-count]");
    const locationEls = $$("[data-profile-location-text]");
    const localTimeEls = $$("[data-profile-local-time]");
    const websiteEls = $$("[data-profile-website]");
    const accountStatus = $("[data-profile-page-account-status]");
    const payoutStatus = $("[data-profile-page-payout-status]");
    const adminStatus = $("[data-profile-page-admin-status]");
    const emailStatus = $("[data-profile-page-email-status]");
    const verifyButton = $("[data-profile-page-verify-email]");
    const solanaInput = $("[data-profile-page-solana]");
    const renameInput = $("[data-profile-rename-input]");
    const bioInput = $("[data-profile-page-bio]");
    const locationInput = $("[data-profile-page-location]");
    const timezoneInput = $("[data-profile-page-timezone]");
    const mastodonInput = $("[data-profile-page-mastodon]");
    const privateInput = $("[data-profile-page-private]");
    const followersPublicInput = $("[data-profile-page-followers-public]");
    const publicUrl = $("[data-profile-public-url]");
    const txtValue = $("[data-profile-txt-value]");

    const isNode = session?.kind === "node";
    avatars.forEach((avatar) => applyAvatar(avatar, session));
    nameEls.forEach((nameEl) => {
      nameEl.textContent = name;
    });
    emailEls.forEach((emailEl) => {
      emailEl.textContent = email;
    });
    $$("[data-profile-kind-badge]").forEach((badge) => {
      badge.classList.toggle("hidden", !isNode);
    });
    $$("[data-profile-node-status]").forEach((row) => {
      row.classList.toggle("hidden", !isNode);
      const text = row.querySelector("[data-profile-node-status-text]");
      if (text) text.textContent = session?.online ? "Online" : "Offline";
    });
    bioEls.forEach((bioEl) => {
      bioEl.textContent = session?.profileBio || "No bio yet.";
    });
    followersEls.forEach((followersEl) => {
      followersEl.textContent = String(Number(session?.profileFollowers ?? session?.followers ?? 0).toLocaleString());
    });
    followingEls.forEach((followingEl) => {
      followingEl.textContent = String(Number(session?.profileFollowing ?? session?.following ?? 0).toLocaleString());
    });
    mirrorsEls.forEach((mirrorsEl) => {
      mirrorsEl.textContent = String(Number(session?.profileMirrorCount ?? session?.mirrorCount ?? 0).toLocaleString());
    });
    locationEls.forEach((locationEl) => {
      locationEl.textContent = session?.profileLocation || "No location";
    });
    localTimeEls.forEach((localTimeEl) => {
      const profileTimezone = session?.profileTimezone || "";
      try {
        localTimeEl.textContent = new Intl.DateTimeFormat([], {
          hour: "2-digit",
          minute: "2-digit",
          hour12: false,
          ...(profileTimezone ? { timeZone: profileTimezone } : {}),
          timeZoneName: "shortOffset",
        }).format(new Date());
      } catch {
        localTimeEl.textContent = profileTimezone || "No time zone";
      }
    });
    websiteEls.forEach((websiteEl) => {
      const firstLink = Array.isArray(session?.profileLinks) ? session.profileLinks.find((link) => link?.url) : null;
      const url = firstLink?.url || profilePublicUrl(session);
      websiteEl.textContent = url;
      websiteEl.href = url;
    });
    if (accountStatus) accountStatus.textContent = session?.status || "active";
    if (payoutStatus) payoutStatus.textContent = session?.hasPayoutAddress ? "Configured" : "Not configured";
    if (adminStatus) adminStatus.textContent = session?.isAdmin ? "Yes" : "No";
    if (emailStatus) {
      emailStatus.textContent = session?.emailVerified
        ? `${email} is verified.`
        : `${email} is not verified yet.`;
    }
    if (verifyButton) {
      verifyButton.disabled = Boolean(session?.emailVerified);
      verifyButton.classList.toggle("opacity-40", Boolean(session?.emailVerified));
      verifyButton.textContent = session?.emailVerified ? "Verified" : "Send link";
    }
    if (solanaInput && document.activeElement !== solanaInput) {
      solanaInput.value = session?.solana || "";
    }
    if (renameInput) {
      renameInput.placeholder = name;
    }
    if (bioInput && document.activeElement !== bioInput) {
      bioInput.value = session?.profileBio || "";
    }
    if (locationInput && document.activeElement !== locationInput) {
      locationInput.value = session?.profileLocation || "";
    }
    renderProfileTimezoneOptions(session);
    if (timezoneInput && document.activeElement !== timezoneInput) {
      timezoneInput.value = session?.profileTimezone || "";
    }
    if (mastodonInput && document.activeElement !== mastodonInput) {
      mastodonInput.value = session?.mastodon || "";
    }
    if (privateInput) {
      privateInput.checked = Boolean(session?.profilePrivate);
    }
    if (followersPublicInput) {
      followersPublicInput.checked = Boolean(session?.followersPublic);
    }
    if (publicUrl) publicUrl.textContent = profilePublicUrl(session);
    if (txtValue) txtValue.textContent = profileTxtValue(session);
    renderProfileLinksEditor(session);
    renderNotificationPreferences(session);
    if (!session?.emailVerified) {
      state.nodeNameAvailability.available = false;
      setRenameStatus("Verify your email before changing your username.", "bad");
    } else if (!($("[data-profile-rename-input]")?.value || "").trim()) {
      state.nodeNameAvailability.available = false;
      setRenameStatus("Enter a new username to check availability.", "");
    }
    updateRenameButton();
    renderClaimNodePanel(session);
    renderProfileContributionGraph();
    renderProfileFediverse(session);
  }

  // ---- Fediverse presence ------------------------------------------------
  // A profile with a linked Mastodon handle surfaces that account here: the
  // account's header image becomes a banner across the overview page and the
  // newest public posts fill a sidebar card. The browser talks straight to
  // the user's home instance (public CORS API, credentials omitted) so none
  // of this spends the worker's request quota, and a 10-minute localStorage
  // snapshot keeps repeat visits from hammering small instances.
  const PROFILE_FEDIVERSE_CACHE_PREFIX = "forkmesh.profileFediverse:v1:";
  const PROFILE_FEDIVERSE_CACHE_TTL_MS = 10 * 60 * 1000;
  const PROFILE_FEDIVERSE_POST_LIMIT = 3;
  const profileFediverseLoading = new Set();

  function parseMastodonHandle(raw) {
    const match = /^@?([\w.-]{1,80})@([a-z0-9-]+(?:\.[a-z0-9-]+)+)$/i
      .exec(String(raw || "").trim());
    return match ? { user: match[1], domain: match[2].toLowerCase() } : null;
  }

  function fediverseHttpsUrl(value) {
    try {
      const url = new URL(String(value || "").trim());
      return url.protocol === "https:" && !url.username && !url.password
        ? url.href : "";
    } catch {
      return "";
    }
  }

  // Mastodon serves statuses and notes as sanitized HTML; the card renders
  // plain text only. A detached textarea decodes entities without ever
  // constructing elements from the remote markup.
  function fediversePlainText(value, limit = 280) {
    const stripped = String(value ?? "")
      .replace(/<br\s*\/?>/gi, "\n")
      .replace(/<\/(?:p|div|blockquote|li)>/gi, "\n")
      .replace(/<[^>]*>/g, "");
    const decoder = document.createElement("textarea");
    decoder.innerHTML = stripped;
    const text = decoder.value
      .replace(/[\u0000-\u0008\u000b-\u001f\u007f-\u009f\u202a-\u202e\u2066-\u2069]/g, " ")
      .replace(/[ \t]+/g, " ")
      .replace(/ ?\n ?/g, "\n")
      .replace(/\n{2,}/g, "\n")
      .trim();
    return text.length > limit ? text.slice(0, limit - 1).trimEnd() + "…" : text;
  }

  function profileFediverseCacheKey(handle) {
    return PROFILE_FEDIVERSE_CACHE_PREFIX + handle;
  }

  function readProfileFediverseCache(handle) {
    try {
      const data = JSON.parse(localStorage.getItem(profileFediverseCacheKey(handle)) || "");
      return data && typeof data === "object" && Array.isArray(data.posts) ? data : null;
    } catch {
      return null;
    }
  }

  function writeProfileFediverseCache(handle, data) {
    try {
      localStorage.setItem(profileFediverseCacheKey(handle), JSON.stringify(data));
    } catch {}
  }

  async function fetchFediverseJson(url) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch(url, {
        credentials: "omit",
        headers: { Accept: "application/json" },
        signal: controller.signal,
      });
      if (!response.ok) throw new Error("fediverse HTTP " + response.status);
      return await response.json();
    } finally {
      clearTimeout(timer);
    }
  }

  async function loadProfileFediverse(parsed) {
    const base = "https://" + parsed.domain;
    const account = await fetchFediverseJson(
      base + "/api/v1/accounts/lookup?acct=" + encodeURIComponent(parsed.user));
    const id = String(account?.id || "").trim().slice(0, 64);
    if (!id || account?.suspended) return null;
    let statuses = [];
    try {
      statuses = await fetchFediverseJson(
        base + "/api/v1/accounts/" + encodeURIComponent(id) +
        "/statuses?limit=10&exclude_replies=true");
    } catch {
      // The account still renders; the posts list just stays empty.
    }
    const posts = (Array.isArray(statuses) ? statuses : [])
      .map((status) => {
        const boost = status?.reblog && typeof status.reblog === "object"
          ? status.reblog : null;
        const source = boost || status || {};
        const imageCount = (Array.isArray(source.media_attachments)
          ? source.media_attachments : [])
          .filter((media) => String(media?.type || "") === "image").length;
        return {
          url: fediverseHttpsUrl(source.url || status?.url),
          text: fediversePlainText(source.content),
          imageCount,
          boosted: Boolean(boost),
          createdAt: String(source.created_at || status?.created_at || ""),
        };
      })
      .filter((post) => post.url && (post.text || post.imageCount))
      .slice(0, PROFILE_FEDIVERSE_POST_LIMIT);
    // Instances answer with a placeholder "missing.png" header when the
    // account never uploaded one — that is not a banner worth showing.
    const header = fediverseHttpsUrl(account.header_static || account.header);
    return {
      at: Date.now(),
      url: fediverseHttpsUrl(account.url) || base + "/@" + parsed.user,
      acct: String(account.acct || parsed.user).slice(0, 120),
      banner: /\/missing\.png$/i.test(header) ? "" : header,
      posts,
    };
  }

  function renderProfileFediverseData(handle, data) {
    const stamp = handle + ":" + String(data?.at || 0);
    const banner = $("[data-profile-fediverse-banner]");
    if (banner) {
      if (data?.banner) {
        banner.style.backgroundImage = 'url("' + data.banner + '")';
        banner.href = data.url;
        banner.hidden = false;
      } else {
        banner.hidden = true;
      }
    }
    $$("[data-profile-fediverse]").forEach((card) => {
      if (!data) {
        card.hidden = true;
        return;
      }
      if (card.dataset.fediverseStamp === stamp) {
        card.hidden = false;
        return;
      }
      card.dataset.fediverseStamp = stamp;
      const link = card.querySelector("[data-profile-fediverse-link]");
      if (link) {
        link.textContent = "@" + data.acct;
        link.href = data.url;
      }
      // Pops the feed out into its own window so it can sit beside the
      // dashboard instead of replacing the tab.
      const popout = card.querySelector("[data-profile-fediverse-popout]");
      if (popout) {
        popout.onclick = () => {
          window.open(
            data.url, "forkmesh-fediverse-feed",
            "noopener,width=520,height=860");
        };
      }
      const list = card.querySelector("[data-profile-fediverse-posts]");
      if (list) {
        list.textContent = "";
        data.posts.forEach((post, index) => {
          const item = document.createElement("a");
          item.href = post.url;
          item.target = "_blank";
          item.rel = "noopener noreferrer";
          item.className = "block px-3 py-2 hover:bg-secondary" +
            (index ? " border-t border-border" : "");
          const text = document.createElement("p");
          text.className = "whitespace-pre-wrap break-words leading-5 text-foreground";
          text.textContent = post.text ||
            (post.imageCount === 1 ? "Shared an image." : "Shared " + post.imageCount + " images.");
          const meta = document.createElement("p");
          meta.className = "mt-1 text-xs text-muted-foreground";
          meta.textContent = (post.boosted ? "Boosted · " : "") + formatTimeAgo(post.createdAt);
          item.append(text, meta);
          list.append(item);
        });
        if (!data.posts.length) {
          const empty = document.createElement("p");
          empty.className = "px-3 py-2 text-muted-foreground";
          empty.textContent = "No public posts yet.";
          list.append(empty);
        }
      }
      card.hidden = false;
      window.lucide?.createIcons();
    });
  }

  function renderProfileFediverse(session) {
    if (profileMarkupOwnedByPublicProfile(session)) return;
    const parsed = parseMastodonHandle(session?.mastodon);
    if (!parsed) {
      renderProfileFediverseData("", null);
      return;
    }
    const handle = parsed.user + "@" + parsed.domain;
    const cached = readProfileFediverseCache(handle);
    if (cached) renderProfileFediverseData(handle, cached);
    const fresh = cached && Date.now() - Number(cached.at || 0) < PROFILE_FEDIVERSE_CACHE_TTL_MS;
    if (fresh || profileFediverseLoading.has(handle)) return;
    profileFediverseLoading.add(handle);
    loadProfileFediverse(parsed)
      .then((data) => {
        if (!data) return;
        writeProfileFediverseCache(handle, data);
        renderProfileFediverseData(handle, data);
      })
      .catch(() => {
        // Instance unreachable (CORS, rate limit, downtime): keep whatever
        // the stale snapshot already painted instead of flashing it away.
      })
      .finally(() => profileFediverseLoading.delete(handle));
  }

  function renderProfileLinksEditor(session = state.session) {
    const links = Array.isArray(session?.profileLinks) ? session.profileLinks : [];
    $$("[data-profile-link-row]").forEach((row, index) => {
      const link = links[index] || {};
      const label = row.querySelector("[data-profile-link-label]");
      const url = row.querySelector("[data-profile-link-url]");
      const status = row.querySelector("[data-profile-link-status]");
      if (label && document.activeElement !== label) label.value = link.label || "";
      if (url && document.activeElement !== url) url.value = link.url || "";
      if (status) {
        if (link.url) {
          status.textContent = link.verified
            ? `Verified for ${link.domain || "domain"}.`
            : `Add TXT ${link.txtValue || profileTxtValue(session)} on ${link.txtName || link.domain || "the domain"} to verify.`;
          status.className = "sm:col-span-2 text-[11px] " +
            (link.verified ? "text-primary" : "text-muted-foreground");
        } else {
          status.textContent = "";
          status.className = "sm:col-span-2 text-[11px] text-muted-foreground";
        }
      }
    });
  }

  function profilePayload(extra = {}, passwordOverride) {
    const password = passwordOverride ?? ($("[data-profile-password]")?.value || "");
    return {
      nodeName: state.session?.nodeName || "",
      email: state.session?.email || "",
      sessionToken: state.session?.sessionToken || "",
      password,
      ...extra,
    };
  }

  async function postProfile(extra, passwordOverride) {
    const response = await fetch("/api/accounts/profile", {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(profilePayload(extra, passwordOverride)),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body.ok === false) {
      throw new Error(body.error || `HTTP ${response.status}`);
    }
    const nextSession = sessionFromAccountPayload(body, state.session || {});
    writeSession(nextSession);
    renderProfile(nextSession);
    return body;
  }

  async function refreshRepositories() {
    const data = await fetchJson("/api/repositories", { fresh: true });
    renderRepositories(data.repositories, state.session);
  }

  async function saveProfile(options = {}) {
    const passwordSelector = options.passwordSelector || "[data-profile-password]";
    const solanaSelector = options.solanaSelector || "[data-profile-solana]";
    const hintSelector = options.hintSelector || "[data-profile-hint]";
    const buttonSelector = options.buttonSelector || "[data-profile-save]";
    const password = profilePassword(passwordSelector);
    const solana = ($(solanaSelector)?.value || "").trim();
    if (!password) {
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint("Enter your current password to save payout changes.", "bad");
      } else {
        setProfilePageHint(hintSelector, "Enter your current password to save payout changes.", "bad");
      }
      return;
    }
    const button = $(buttonSelector);
    if (button) { button.disabled = true; button.textContent = "Saving…"; }
    try {
      await postProfile({ solana }, password);
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(solana ? "Payout address saved." : "Payout address cleared.", "good");
      } else {
        setProfilePageHint(hintSelector, solana ? "Payout address saved." : "Payout address cleared.", "good");
      }
    } catch (error) {
      const message = error.message === "bad_solana"
        ? "Enter a valid public Solana address."
        : "Could not save payout address. Check your password and try again.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "bad");
      } else {
        setProfilePageHint(hintSelector, message, "bad");
      }
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = options.buttonText || "Save profile";
      }
    }
  }

  function collectProfileLinks() {
    const links = [];
    $$("[data-profile-link-row]").forEach((row) => {
      const label = (row.querySelector("[data-profile-link-label]")?.value || "").trim();
      const url = (row.querySelector("[data-profile-link-url]")?.value || "").trim();
      if (!label && !url) return;
      links.push({ label, url });
    });
    return links;
  }

  async function savePublicProfile() {
    const button = $("[data-profile-public-save]");
    if (button) { button.disabled = true; button.textContent = "Saving…"; }
    try {
      await postProfile({
        profileBio: ($("[data-profile-page-bio]")?.value || "").trim(),
        profileLocation: ($("[data-profile-page-location]")?.value || "").trim(),
        profileTimezone: ($("[data-profile-page-timezone]")?.value || "").trim(),
        mastodon: ($("[data-profile-page-mastodon]")?.value || "").trim(),
        profilePrivate: Boolean($("[data-profile-page-private]")?.checked),
        followersPublic: Boolean($("[data-profile-page-followers-public]")?.checked),
        profileLinks: collectProfileLinks(),
      });
      setProfilePageHint("[data-profile-public-hint]", "Public profile saved.", "good");
    } catch (error) {
      const messages = {
        bad_mastodon: "Enter a Mastodon handle like @you@example.social.",
        bad_profile_timezone: "Enter a valid IANA time zone like Asia/Kolkata.",
        bad_profile_links: "Check your profile links and try again.",
        bad_profile_link_url: "Profile links must be http or https URLs on a real domain.",
      };
      setProfilePageHint(
        "[data-profile-public-hint]",
        messages[error.message] || "Could not save public profile. Sign in again and try once more.",
        "bad");
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = "Save public profile";
      }
    }
  }

  async function saveProfileAbout() {
    const profileAbout = String($("[data-profile-about-textarea]")?.value || "").replace(/\r\n/g, "\n");
    const button = $("[data-profile-about-save]");
    if (button) { button.disabled = true; button.textContent = "Saving..."; }
    try {
      const body = await postProfile({ profileAbout });
      const nextSession = sessionFromAccountPayload(body, state.session || {});
      renderProfileAbout(nextSession);
      setProfileAboutHint("About saved.", "good");
      setProfileAboutModalOpen(false);
    } catch (error) {
      const message = error.message === "profile_about_too_large" || error.message === "profile_readme_too_large"
        ? "About is too large. Keep it under 32 KB."
        : "Could not save about. Sign in again and try once more.";
      setProfileAboutHint(message, "bad");
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = "Save about";
      }
    }
  }

  async function resendVerification(options = {}) {
    const passwordSelector = options.passwordSelector || "[data-profile-password]";
    const hintSelector = options.hintSelector || "[data-profile-hint]";
    const buttonSelector = options.buttonSelector || "[data-profile-verify-email]";
    const password = profilePassword(passwordSelector);
    if (!password) {
      const message = "Enter your current password first, then send a verification link.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "bad");
      } else {
        setProfilePageHint(hintSelector, message, "bad");
      }
      return;
    }
    const button = $(buttonSelector);
    if (button) { button.disabled = true; button.textContent = "Sending…"; }
    try {
      const body = await postProfile({ resendVerification: true }, password);
      const message = body.verificationSent
        ? "Verification email sent."
        : "Verification request queued for manual follow-up.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "good");
      } else {
        setProfilePageHint(hintSelector, message, "good");
      }
    } catch (_) {
      const message = "Could not send verification. Check your password and try again.";
      if (hintSelector === "[data-profile-hint]") {
        setProfileHint(message, "bad");
      } else {
        setProfilePageHint(hintSelector, message, "bad");
      }
    } finally {
      renderProfileModal(state.session);
      renderProfilePage(state.session);
    }
  }

  async function checkNodeNameAvailability() {
    const input = $("[data-profile-rename-input]");
    const candidate = (input?.value || "").trim().toLowerCase();
    state.nodeNameAvailability.candidate = candidate;
    state.nodeNameAvailability.available = false;
    if (input && input.value !== candidate) input.value = candidate;
    if (!candidate) {
      setRenameStatus("Enter a new username to check availability.", "");
      updateRenameButton();
      return;
    }
    if (!validNodeName(candidate)) {
      setRenameStatus("Use lowercase letters, numbers, and hyphens. Start with a letter.", "bad");
      updateRenameButton();
      return;
    }
    if (candidate === String(state.session?.nodeName || "").toLowerCase()) {
      setRenameStatus("This is already your current username.", "bad");
      updateRenameButton();
      return;
    }
    if (!state.session?.emailVerified) {
      setRenameStatus("Verify your email before changing your username.", "bad");
      updateRenameButton();
      return;
    }

    const seq = state.nodeNameAvailability.seq + 1;
    state.nodeNameAvailability.seq = seq;
    state.nodeNameAvailability.checking = true;
    setRenameStatus("Checking availability…", "");
    updateRenameButton();
    try {
      const response = await fetch(`/api/accounts/${encodeURIComponent(candidate)}`, {
        headers: { accept: "application/json" },
      });
      const body = await response.json().catch(() => ({}));
      if (seq !== state.nodeNameAvailability.seq) return;
      state.nodeNameAvailability.available = Boolean(response.ok && body.available);
      setRenameStatus(
        state.nodeNameAvailability.available
          ? "Username is available."
          : "That username is already taken.",
        state.nodeNameAvailability.available ? "good" : "bad",
      );
    } catch (_) {
      if (seq !== state.nodeNameAvailability.seq) return;
      setRenameStatus("Could not check availability right now.", "bad");
    } finally {
      if (seq === state.nodeNameAvailability.seq) {
        state.nodeNameAvailability.checking = false;
        updateRenameButton();
      }
    }
  }

  async function renameNodeName() {
    const input = $("[data-profile-rename-input]");
    const newNodeName = (input?.value || "").trim().toLowerCase();
    const password = profilePassword("[data-profile-rename-password]");
    if (!state.nodeNameAvailability.available || !newNodeName) {
      setRenameStatus("Choose an available username first.", "bad");
      return;
    }
    if (!password) {
      setRenameStatus("Enter your current password to update your username.", "bad");
      updateRenameButton();
      return;
    }
    const button = $("[data-profile-rename-save]");
    if (button) { button.disabled = true; button.textContent = "Updating…"; }
    try {
      await postProfile({ newNodeName }, password);
      if (input) input.value = "";
      $("[data-profile-rename-password]") && ($("[data-profile-rename-password]").value = "");
      state.nodeNameAvailability.available = false;
      setRenameStatus("Username updated. Repositories are refreshing.", "good");
      await refreshRepositories();
      renderProfilePage(state.session);
    } catch (error) {
      const messages = {
        email_not_verified: "Verify your email before changing your username.",
        invalid_node_name: "Enter a valid username.",
        node_name_taken: "That username is already taken.",
        node_name_unchanged: "Enter a different username.",
        repo_namespace_conflict: "That namespace already has repository data.",
      };
      setRenameStatus(messages[error.message] || "Could not update username. Check your password and try again.", "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Update username"; }
      updateRenameButton();
    }
  }

  // Users vs nodes (adhoc #53): claim a node (e.g. a headless mirror you
  // installed) by its node ID, then confirm the code that appears on that
  // node itself to complete the link.
  function renderClaimNodePanel(session) {
    const list = $("[data-claim-node-list]");
    if (!list) return;
    const nodes = Array.isArray(session?.nodes) ? session.nodes : [];
    list.innerHTML = "";
    if (!nodes.length) {
      const li = document.createElement("li");
      li.className = "text-muted-foreground";
      li.textContent = "No linked nodes yet.";
      list.appendChild(li);
      return;
    }
    for (const node of nodes) {
      const li = document.createElement("li");
      li.className = "font-mono";
      li.textContent = node;
      list.appendChild(li);
    }
  }

  async function claimNode() {
    const input = $("[data-claim-node-input]");
    // Not lowercased up front: a node's public-key ID is case-sensitive, and
    // only the plain-name form is meant to be case-insensitive.
    const nodeId = (input?.value || "").trim();
    const password = profilePassword("[data-claim-node-password]");
    if (!validNodeName(nodeId.toLowerCase()) && !validNodePubkey(nodeId)) {
      setProfilePageHint("[data-claim-node-status]", "Enter a valid node ID.", "bad");
      return;
    }
    if (!password) {
      setProfilePageHint("[data-claim-node-status]", "Enter your current password to claim a node.", "bad");
      return;
    }
    const button = $("[data-claim-node-send]");
    if (button) { button.disabled = true; button.textContent = "Sending…"; }
    try {
      const response = await fetch("/api/accounts/claim-node", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          identifier: state.session?.email || state.session?.nodeName || "",
          password, nodeId,
        }),
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || body.ok === false) throw new Error(body.error || `HTTP ${response.status}`);
      const codeRow = $("[data-claim-code-row]");
      if (body.alreadyLinked) {
        state.claimNode.pendingNodeId = "";
        if (codeRow) codeRow.classList.add("hidden");
        setProfilePageHint("[data-claim-node-status]", `"${nodeId}" is already linked to your account.`, "good");
        await refreshPublicProfile();
      } else {
        state.claimNode.pendingNodeId = nodeId;
        if (codeRow) codeRow.classList.remove("hidden");
        setProfilePageHint(
          "[data-claim-node-status]",
          `Confirmation code sent to "${nodeId}". Check that node's app for the code, then enter it below.`,
          "good");
      }
    } catch (error) {
      const messages = {
        invalid_credentials: "Incorrect password.",
        invalid_node_id: "Enter a valid node ID.",
        cannot_claim_self: "You can't claim your own account.",
        no_such_node: "No node with that ID was found.",
        not_a_node: "That ID belongs to a user account, not a claimable node.",
        node_already_owned: "That node is already linked to another account.",
      };
      setProfilePageHint(
        "[data-claim-node-status]",
        messages[error.message] || "Could not send a claim code. Check the node ID and your password.",
        "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Send claim code"; }
    }
  }

  async function confirmClaimCode() {
    const nodeId = state.claimNode.pendingNodeId;
    const code = ($("[data-claim-code-input]")?.value || "").trim();
    const password = profilePassword("[data-claim-node-password]");
    if (!nodeId) {
      setProfilePageHint("[data-claim-node-status]", "Send a claim code first.", "bad");
      return;
    }
    if (!/^[0-9]{6}$/.test(code)) {
      setProfilePageHint("[data-claim-node-status]", "Enter the 6-digit code shown on the node.", "bad");
      return;
    }
    if (!password) {
      setProfilePageHint("[data-claim-node-status]", "Enter your current password to link this node.", "bad");
      return;
    }
    const button = $("[data-claim-code-confirm]");
    if (button) { button.disabled = true; button.textContent = "Linking…"; }
    try {
      const response = await fetch("/api/accounts/claim-confirm", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          identifier: state.session?.email || state.session?.nodeName || "",
          password, nodeId, code,
        }),
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || body.ok === false) throw new Error(body.error || `HTTP ${response.status}`);
      const nextSession = {
        ...(state.session || {}),
        nodes: Array.isArray(body.nodes) ? body.nodes : state.session?.nodes,
      };
      writeSession(nextSession);
      renderProfile(nextSession);
      state.claimNode.pendingNodeId = "";
      const codeRow = $("[data-claim-code-row]");
      if (codeRow) codeRow.classList.add("hidden");
      const nodeInput = $("[data-claim-node-input]");
      if (nodeInput) nodeInput.value = "";
      const codeInput = $("[data-claim-code-input]");
      if (codeInput) codeInput.value = "";
      setProfilePageHint("[data-claim-node-status]", `Linked "${nodeId}" to your account.`, "good");
    } catch (error) {
      const messages = {
        invalid_credentials: "Incorrect password.",
        no_such_node: "No node with that ID was found.",
        no_pending_claim: "No pending claim for that node. Send a new claim code.",
        bad_code: "That code is incorrect.",
      };
      setProfilePageHint(
        "[data-claim-node-status]",
        messages[error.message] || "Could not link the node. Check the code and try again.",
        "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Link node"; }
    }
  }

  // "Link this node to your account" (adhoc #120): the desktop app opens
  // /dashboard?link_node=<node>&link_ts=<ts>&link_sig=<sig> - a short-lived
  // grant signed with the node's own key. The signature proves node-key
  // control and consents to the link, so whoever is logged in HERE becomes the
  // owner with no password re-entry or confirmation code.
  function pendingLinkGrant() {
    const params = new URLSearchParams(location.search);
    const nodeName = (params.get("link_node") || "").trim();
    const ts = (params.get("link_ts") || "").trim();
    const sig = (params.get("link_sig") || "").trim();
    if (!validNodeName(nodeName.toLowerCase()) || !ts || !sig) return null;
    return { nodeName, ts, sig };
  }

  function offerLinkGrant(grant) {
    // Strip the one-time grant from the address bar first so refresh/back
    // can't replay it (and it doesn't linger in the visible URL), then show
    // the settings page's Nodes panel and ask for one explicit "Authenticate &
    // link" click. The grant overrides any existing association, so the click
    // is the moment of consent on the browser side. (Boot redirects the grant
    // to the settings document before calling this, so the panel exists here.)
    const params = new URLSearchParams(location.search);
    for (const key of ["link_node", "link_ts", "link_sig"]) params.delete(key);
    const rest = params.toString();
    window.history.replaceState(null, "", location.pathname + (rest ? `?${rest}` : ""));
    state.linkGrant = grant;
    setSettingsSection("nodes", { scroll: false });
    const row = $("[data-link-grant-row]");
    if (row) row.classList.remove("hidden");
    const text = $("[data-link-grant-text]");
    if (text) {
      text.textContent =
        `Link node "${grant.nodeName}" to this account (` +
        `${state.session?.nodeName || "you"})? This node will belong to you - ` +
        "any existing link is replaced.";
    }
  }

  async function redeemLinkGrant() {
    const grant = state.linkGrant;
    if (!grant) return;
    const button = $("[data-link-grant-confirm]");
    if (button) { button.disabled = true; button.textContent = "Linking…"; }
    setProfilePageHint("[data-claim-node-status]", `Linking "${grant.nodeName}" to your account…`, "");
    try {
      const response = await fetch("/api/accounts/link-grant", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({
          nodeName: grant.nodeName,
          ts: grant.ts,
          sig: grant.sig,
          user: state.session?.nodeName || "",
        }),
      });
      const body = await response.json().catch(() => ({}));
      if (!response.ok || body.ok === false) throw new Error(body.error || `HTTP ${response.status}`);
      state.linkGrant = null;
      $("[data-link-grant-row]")?.classList.add("hidden");
      const nextSession = {
        ...(state.session || {}),
        nodes: Array.isArray(body.nodes) ? body.nodes : state.session?.nodes,
      };
      writeSession(nextSession);
      renderProfile(nextSession);
      setProfilePageHint(
        "[data-claim-node-status]",
        body.selfAccount
          ? `"${body.nodeId || grant.nodeName}" is this account - already yours.`
          : body.alreadyLinked
            ? `"${body.nodeId || grant.nodeName}" is already linked to your account.`
            : `Linked "${body.nodeId || grant.nodeName}" to your account.`,
        "good");
    } catch (error) {
      state.linkGrant = null;
      $("[data-link-grant-row]")?.classList.add("hidden");
      const messages = {
        unauthorized: "The link expired - click \"Link this node to your account\" in the node's app again.",
        bad_signature: "The link couldn't be verified - click the button in the node's app again.",
        grant_used: "That link was already used - click the button in the node's app again.",
        no_such_node: "That node isn't registered with the relay yet.",
        not_a_user: "This login can't own nodes - sign up as a user (email + password) first.",
        no_such_user: "Log in with a user account first, then open the link again.",
      };
      setProfilePageHint(
        "[data-claim-node-status]",
        messages[error.message] || "Could not link the node. Click the button in the node's app and try again.",
        "bad");
    } finally {
      if (button) { button.disabled = false; button.textContent = "Authenticate & link"; }
    }
  }

  async function saveNotificationPreferences() {
    const password = profilePassword("[data-notification-preferences-password]");
    if (!password) {
      setProfilePageHint("[data-notification-preferences-hint]", "Enter your current password to save notification settings.", "bad");
      return;
    }
    const button = $("[data-notification-preferences-save]");
    if (button) { button.disabled = true; button.textContent = "Saving..."; }
    try {
      await postProfile({
        emailNotifications: true,
        notificationPreferences: collectNotificationPreferences(),
      }, password);
      if ($("[data-notification-preferences-password]")) $("[data-notification-preferences-password]").value = "";
      setProfilePageHint("[data-notification-preferences-hint]", "Notification settings saved.", "good");
    } catch (_) {
      setProfilePageHint("[data-notification-preferences-hint]", "Could not save notification settings. Check your password and try again.", "bad");
    } finally {
      if (button) {
        button.disabled = false;
        button.textContent = "Save notifications";
      }
    }
  }

  async function deleteAccount() {
    const password = profilePassword("[data-profile-delete-password]");
    const confirm = ($("[data-profile-delete-confirm]")?.value || "").trim();
    if (!password) {
      setProfilePageHint("[data-profile-delete-hint]", "Enter your current password to delete this account.", "bad");
      return;
    }
    if (confirm !== "DELETE") {
      setProfilePageHint("[data-profile-delete-hint]", "Type DELETE to confirm account deletion.", "bad");
      return;
    }
    const button = $("[data-profile-delete-account]");
    if (button) { button.disabled = true; button.textContent = "Deleting..."; }
    try {
      await postProfile({ deleteAccount: true }, password);
      logout();
    } catch (_) {
      setProfilePageHint("[data-profile-delete-hint]", "Could not delete account. Check your password and try again.", "bad");
      if (button) { button.disabled = false; button.textContent = "Delete account"; }
    }
  }
