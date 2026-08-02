(() => {
  // Founders outreach console: admins (and accounts an admin added to the
  // outreach team) compose and send one-off emails from the shared founders
  // address. All state comes from /api/outreach; auth is the same signed
  // session token the dashboard stores in localStorage.
  const $ = (sel) => document.querySelector(sel);

  let access = null;
  let templates = [];

  function readSession() {
    try { return JSON.parse(localStorage.getItem("forkmesh.session") || "null"); }
    catch (_) { return null; }
  }

  function sessionToken() {
    const session = readSession();
    return session && session.sessionToken ? session.sessionToken : "";
  }

  async function api(path, body) {
    const token = sessionToken();
    const options = {
      method: body ? "POST" : "GET",
      headers: {
        accept: "application/json",
        authorization: "Bearer " + token,
      },
    };
    if (body) {
      options.headers["content-type"] = "application/json";
      options.body = JSON.stringify(Object.assign({ sessionToken: token }, body));
    }
    const res = await fetch(path, options);
    let payload = {};
    try { payload = await res.json(); } catch (_) { /* ignore */ }
    return { ok: res.ok, status: res.status, body: payload };
  }

  function setStatus(text) {
    const el = $("#or-status");
    if (el) el.textContent = text;
  }

  function setHint(text, kind) {
    const el = $("#or-hint");
    if (!el) return;
    el.textContent = text || "";
    el.className = "hint" + (kind ? " " + kind : "");
  }

  function quotaLine() {
    if (!access) return "";
    return "Sending as “" + access.name + "” from " + access.fromEmail +
      " · " + (access.sentToday || 0) + "/" + access.dailyLimit + " sent today" +
      (access.configured ? "" : " · email provider not configured");
  }

  function renderTemplates() {
    const select = $("#or-template");
    if (!select) return;
    select.innerHTML = "";
    for (const t of templates) {
      const option = document.createElement("option");
      option.value = t.key;
      option.textContent = t.label;
      select.appendChild(option);
    }
    select.onchange = () => {
      const t = templates.find((x) => x.key === select.value);
      if (!t) return;
      $("#or-subject").value = t.subject || "";
      $("#or-body").value = t.body || "";
      setHint("");
    };
  }

  function renderTeam() {
    const list = $("#or-team-list");
    if (!list || !access) return;
    const members = access.members || [];
    list.innerHTML = "";
    if (!members.length) {
      const empty = document.createElement("div");
      empty.className = "sub";
      empty.textContent = "No extra members yet — only admins can send.";
      list.appendChild(empty);
    }
    for (const m of members) {
      const row = document.createElement("div");
      row.className = "team-row";
      const who = document.createElement("div");
      const name = document.createElement("span");
      name.className = "who";
      name.textContent = m.name;
      who.appendChild(name);
      const sub = document.createElement("div");
      sub.className = "sub";
      sub.textContent = "added by " + (m.addedBy || "?") +
        (m.addedAt ? " · " + new Date(m.addedAt).toLocaleDateString() : "");
      who.appendChild(sub);
      const remove = document.createElement("button");
      remove.className = "btn-sm";
      remove.type = "button";
      remove.textContent = "Remove";
      remove.onclick = async () => {
        remove.disabled = true;
        const res = await api("/api/outreach/team", { action: "remove", name: m.name });
        if (res.ok) {
          access.members = res.body.members || [];
          renderTeam();
        } else {
          remove.disabled = false;
        }
      };
      row.appendChild(who);
      row.appendChild(remove);
      list.appendChild(row);
    }
  }

  function renderLog() {
    const list = $("#or-log-list");
    if (!list || !access) return;
    const recent = access.recent || [];
    list.innerHTML = "";
    if (!recent.length) {
      const empty = document.createElement("div");
      empty.className = "sub";
      empty.textContent = "Nothing sent yet.";
      list.appendChild(empty);
      return;
    }
    for (const entry of recent) {
      const row = document.createElement("div");
      row.className = "log-row";
      const what = document.createElement("div");
      what.className = "what";
      const to = document.createElement("div");
      to.className = "to";
      to.textContent = (entry.to || "?") + " — " + (entry.subject || "(no subject)");
      const sub = document.createElement("div");
      sub.className = "sub";
      sub.textContent = "by " + (entry.sender || "?") +
        (entry.ts ? " · " + new Date(entry.ts).toLocaleString() : "");
      what.appendChild(to);
      what.appendChild(sub);
      const mark = document.createElement("span");
      mark.className = entry.ok ? "log-ok" : "log-fail";
      mark.textContent = entry.ok ? "sent" : "failed";
      row.appendChild(what);
      row.appendChild(mark);
      list.appendChild(row);
    }
  }

  // Status pipeline: for each template type, how far the sends got —
  // contacted (attempted), sent (delivered ok) and failed. Counts are derived
  // from the same recent-sends log the left-hand list renders.
  function renderPipeline() {
    const wrap = $("#or-pipeline");
    if (!wrap || !access) return;
    wrap.innerHTML = "";
    const recent = access.recent || [];
    const order = [];
    const stats = new Map();
    const ensure = (key, label) => {
      if (!stats.has(key)) { stats.set(key, { label, ok: 0, fail: 0 }); order.push(key); }
      return stats.get(key);
    };
    for (const t of templates) {
      if (t.key === "blank") continue;
      ensure(t.key, t.label);
    }
    for (const entry of recent) {
      const key = entry.template || "blank";
      const known = templates.find((t) => t.key === key);
      const label = known ? known.label : (key === "blank" ? "Blank / custom" : key);
      const s = ensure(key, label);
      if (entry.ok) s.ok += 1; else s.fail += 1;
    }
    if (!order.length) {
      const empty = document.createElement("div");
      empty.className = "pipe-empty";
      empty.textContent = "No templates to track yet.";
      wrap.appendChild(empty);
      return;
    }
    const stage = (cls, n, lbl) => {
      const st = document.createElement("div");
      st.className = "pipe-stage" + (cls ? " " + cls : "");
      const num = document.createElement("span");
      num.className = "n";
      num.textContent = String(n);
      const cap = document.createElement("span");
      cap.textContent = lbl;
      st.appendChild(num);
      st.appendChild(cap);
      return st;
    };
    for (const key of order) {
      const s = stats.get(key);
      const total = s.ok + s.fail;
      const row = document.createElement("div");
      row.className = "pipe-row";
      const name = document.createElement("div");
      name.className = "pipe-name";
      const label = document.createElement("span");
      label.textContent = s.label;
      const count = document.createElement("span");
      count.className = "count";
      count.textContent = total ? total + " sent" : "none yet";
      name.appendChild(label);
      name.appendChild(count);
      const stages = document.createElement("div");
      stages.className = "pipe-stages";
      stages.appendChild(stage("", total, "Contacted"));
      stages.appendChild(stage("ok", s.ok, "Sent"));
      stages.appendChild(stage("fail", s.fail, "Failed"));
      row.appendChild(name);
      row.appendChild(stages);
      wrap.appendChild(row);
    }
  }

  async function send(event) {
    event.preventDefault();
    const button = $("#or-send");
    const payload = {
      to: $("#or-to").value.trim(),
      subject: $("#or-subject").value.trim(),
      body: $("#or-body").value.trim(),
      template: $("#or-template").value,
    };
    if (!payload.to || !payload.subject || !payload.body) {
      setHint("Recipient, subject and message are all required.", "bad");
      return;
    }
    if (/\[[^\]]*\]/.test(payload.subject + payload.body) &&
        !window.confirm("The email still contains [bracketed] placeholders. Send anyway?")) {
      return;
    }
    if (!window.confirm("Send this email to " + payload.to + " from " +
        access.fromEmail + "?")) {
      return;
    }
    button.disabled = true;
    setHint("Sending…");
    const res = await api("/api/outreach/send", payload);
    button.disabled = false;
    if (res.ok && res.body.ok) {
      access.sentToday = res.body.sentToday;
      setStatus(quotaLine());
      setHint("Sent to " + res.body.to + ".", "good");
      $("#or-to").value = "";
      await refresh(true);
    } else if (res.body.error === "daily_limit_reached") {
      setHint("Daily send limit reached (" + res.body.dailyLimit + "/day). Try again tomorrow.", "bad");
    } else if (res.body.error === "send_failed") {
      setHint(res.body.detail || "The email provider rejected the send.", "bad");
    } else {
      setHint("Send failed: " + (res.body.error || res.status), "bad");
    }
  }

  async function addMember() {
    const input = $("#or-team-name");
    const name = (input.value || "").trim().toLowerCase();
    if (!name) return;
    const res = await api("/api/outreach/team", { action: "add", name });
    if (res.ok) {
      input.value = "";
      access.members = res.body.members || [];
      renderTeam();
    } else if (res.body.error === "no_such_account") {
      window.alert("No active account named “" + name + "”.");
    } else {
      window.alert("Could not add “" + name + "”: " + (res.body.error || res.status));
    }
  }

  async function refresh(quiet) {
    if (!sessionToken()) {
      setStatus("You're not logged in.");
      $("#or-login").hidden = false;
      return;
    }
    const res = await api("/api/outreach");
    if (res.status === 401) {
      setStatus("Your session has expired — log in again.");
      $("#or-login").hidden = false;
      return;
    }
    if (!res.ok) {
      setStatus("Could not load the outreach console (" + res.status + ").");
      return;
    }
    access = res.body;
    if (!access.allowed) {
      setStatus("Signed in as “" + access.name + "”.");
      $("#or-denied").hidden = false;
      return;
    }
    templates = access.templates || [];
    setStatus(quotaLine());
    $("#or-denied").hidden = true;
    $("#or-compose").hidden = false;
    $("#or-log").hidden = false;
    if (access.isAdmin) $("#or-team").hidden = false;
    if (!quiet) {
      renderTemplates();
      const select = $("#or-template");
      if (select && templates.length) select.onchange();
    }
    renderTeam();
    renderLog();
    renderPipeline();
  }

  document.addEventListener("DOMContentLoaded", () => {
    $("#or-compose").addEventListener("submit", send);
    $("#or-team-add").addEventListener("click", addMember);
    refresh(false);
  });
})();
