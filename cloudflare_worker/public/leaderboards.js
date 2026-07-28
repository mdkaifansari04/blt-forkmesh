(() => {
  const grid = document.querySelector("[data-leaderboards-grid]");
  if (!grid) return;

  let snapshot = null;
  let activeCategory = "all";
  let searchTerm = "";
  let refreshTimer = 0;

  const plural = (value, word) => {
    const number = Math.max(0, Number(value) || 0);
    return `${number.toLocaleString()} ${word}${number === 1 ? "" : "s"}`;
  };

  function formatBytes(value) {
    let bytes = Math.max(0, Number(value) || 0);
    const units = ["B", "KB", "MB", "GB", "TB", "PB"];
    let unit = 0;
    while (bytes >= 1024 && unit < units.length - 1) {
      bytes /= 1024;
      unit += 1;
    }
    return `${unit ? bytes.toFixed(bytes >= 100 ? 0 : 1) : Math.round(bytes)} ${units[unit]}`;
  }

  function formatDuration(value) {
    let seconds = Math.floor(Math.max(0, Number(value) || 0) / 1000);
    const days = Math.floor(seconds / 86400);
    seconds %= 86400;
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    if (days) return `${days}d ${hours}h`;
    if (hours) return `${hours}h ${minutes}m`;
    return `${minutes}m`;
  }

  function formatAge(value) {
    const milliseconds = Math.max(0, Number(value) || 0);
    const days = Math.floor(milliseconds / 86400000);
    if (days) return plural(days, "day");
    return plural(Math.floor(milliseconds / 3600000), "hour");
  }

  function formatValue(board, row) {
    switch (board.valueKind) {
      case "duration":
        return formatDuration(row.totalActiveMs);
      case "minutes": {
        const minutes = Math.max(0, Number(row.minutes) || 0);
        return minutes >= 60
          ? `${Math.floor(minutes / 60)}h ${minutes % 60}m`
          : `${minutes}m`;
      }
      case "bytes":
        return formatBytes(row.bytes ?? row.sizeBytes);
      case "repos":
        return plural(row.repos, "repo");
      case "mirrors":
        return plural(row.mirrors, "owner");
      case "age":
        return formatAge(row.ageMs);
      case "contributions":
        return plural(row.total, "contribution");
      case "referrals":
        return `${plural(row.signups, "signup")} · ${plural(row.clicks, "click")}`;
      case "visits":
        return plural(row.visits, "visit");
      case "sol": {
        const sol = Number(row.sol) || (Number(row.lamports) || 0) / 1e9;
        return `${sol.toFixed(sol >= 1 ? 2 : 4)} SOL`;
      }
      default:
        return String(row.value ?? "—");
    }
  }

  function rowDetail(board, row) {
    if (board.id === "contributors") {
      return `${plural(row.issues, "issue")} · ${plural(row.pulls, "PR")} · ${plural(row.commits, "commit")}`;
    }
    if (board.id === "node-storage") {
      return [
        row.platform,
        row.version,
        row.commit ? String(row.commit).slice(0, 10) : "",
      ].filter(Boolean).join(" · ");
    }
    if (board.id === "referring-sites") {
      return "Aggregate HTTP referrals";
    }
    return "";
  }

  function makeCard(board) {
    const card = document.createElement("article");
    card.className = "leaderboard-card";
    card.dataset.category = String(board.category || "");
    card.dataset.search = [
      board.title,
      board.subtitle,
      ...(Array.isArray(board.rows)
        ? board.rows.flatMap((row) => [row.name, row.host])
        : []),
    ].join(" ").toLowerCase();

    const header = document.createElement("header");
    header.className = "leaderboard-card-header";
    const category = document.createElement("p");
    category.className = "leaderboard-category";
    category.textContent = String(board.category || "public");
    const title = document.createElement("h2");
    title.textContent = String(board.title || "Leaderboard");
    const subtitle = document.createElement("p");
    subtitle.className = "leaderboard-card-subtitle";
    subtitle.textContent = String(board.subtitle || "");
    header.append(category, title, subtitle);
    card.appendChild(header);

    const rows = Array.isArray(board.rows) ? board.rows : [];
    if (!rows.length) {
      const empty = document.createElement("p");
      empty.className = "leaderboard-empty";
      empty.textContent = "No public data yet.";
      card.appendChild(empty);
      return card;
    }
    rows.forEach((row, index) => {
      const item = document.createElement("div");
      item.className = "leaderboard-row";
      const rank = document.createElement("span");
      rank.className = "leaderboard-rank";
      rank.textContent = String(index + 1).padStart(2, "0");
      const identity = document.createElement("span");
      identity.className = "leaderboard-name";
      identity.textContent = String(row.name || row.host || "participant");
      const detailText = rowDetail(board, row);
      if (detailText) {
        const detail = document.createElement("small");
        detail.className = "leaderboard-detail";
        detail.textContent = detailText;
        identity.appendChild(detail);
      }
      const value = document.createElement("strong");
      value.className = "leaderboard-value";
      value.textContent = formatValue(board, row);
      item.append(rank, identity, value);
      card.appendChild(item);
    });
    return card;
  }

  function applyFilters() {
    let visible = 0;
    grid.querySelectorAll(".leaderboard-card").forEach((card) => {
      const categoryMatches =
        activeCategory === "all" || card.dataset.category === activeCategory;
      const searchMatches =
        !searchTerm || String(card.dataset.search || "").includes(searchTerm);
      card.hidden = !(categoryMatches && searchMatches);
      if (!card.hidden) visible += 1;
    });
    let empty = grid.querySelector("[data-leaderboards-filter-empty]");
    if (!visible) {
      if (!empty) {
        empty = document.createElement("p");
        empty.className = "leaderboards-empty";
        empty.dataset.leaderboardsFilterEmpty = "true";
        empty.textContent = "No boards match those filters.";
        grid.appendChild(empty);
      }
    } else {
      empty?.remove();
    }
  }

  function render(data) {
    snapshot = data;
    const boards = Array.isArray(data?.boards) ? data.boards : [];
    grid.replaceChildren(...boards.map(makeCard));
    document.querySelector("[data-leaderboards-count]").textContent =
      boards.length.toLocaleString();
    document.querySelector("[data-leaderboards-summary]").textContent =
      `${boards.reduce((sum, board) => sum + (Array.isArray(board.rows) ? board.rows.length : 0), 0).toLocaleString()} ranked public entries across the network and community.`;
    const disclosure = document.querySelector("[data-leaderboards-disclosure]");
    if (disclosure && data?.fundsNotice) disclosure.textContent = data.fundsNotice;
    applyFilters();
  }

  async function load({ quiet = false } = {}) {
    const status = document.querySelector("[data-leaderboards-status]");
    if (!quiet) status.textContent = "Refreshing…";
    try {
      const response = await fetch("/api/leaderboards", {
        headers: { accept: "application/json" },
        cache: "no-store",
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      render(data);
      const observedAt = Number(data.observedAt) || Date.now();
      status.textContent = `Updated ${new Date(observedAt).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })}`;
      return true;
    } catch (_) {
      status.textContent = snapshot ? "Refresh failed · showing last result" : "Unavailable";
      if (!snapshot) {
        grid.innerHTML =
          '<p class="leaderboards-empty">Leaderboard data is temporarily unavailable.</p>';
      }
      return false;
    }
  }

  document.querySelector("[data-leaderboards-search]")?.addEventListener(
    "input",
    (event) => {
      searchTerm = String(event.currentTarget.value || "").trim().toLowerCase();
      applyFilters();
    },
  );
  document.querySelectorAll("[data-category]").forEach((button) => {
    button.addEventListener("click", () => {
      activeCategory = button.dataset.category || "all";
      document.querySelectorAll("[data-category]").forEach((candidate) => {
        candidate.setAttribute(
          "aria-pressed",
          String(candidate === button),
        );
      });
      applyFilters();
    });
  });
  document.querySelector("[data-leaderboards-refresh]")?.addEventListener(
    "click",
    () => void load(),
  );

  function scheduleRefresh() {
    window.clearTimeout(refreshTimer);
    if (document.hidden) return;
    refreshTimer = window.setTimeout(async () => {
      await load({ quiet: true });
      scheduleRefresh();
    }, 60_000);
  }
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) {
      void load({ quiet: true });
      scheduleRefresh();
    } else {
      window.clearTimeout(refreshTimer);
    }
  });
  void load().then(scheduleRefresh);
})();
