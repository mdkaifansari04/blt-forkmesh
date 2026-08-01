(() => {












  const NETWORKS = [
    { key: "reddit", label: "Reddit" },
    { key: "mastodon", label: "Mastodon" },
    { key: "twitter", label: "X (Twitter)" },
  ];

  const EMPTY_LABEL = "Not posted yet";
  const MASTODON_LIMIT = 500;
  const TWITTER_LIMIT = 250;



  function permalink(host, key) {
    const raw = (host.dataset[key] || "").trim();
    return /^https?:\/\//i.test(raw) ? raw : "";
  }




  function linkText(url) {
    try {
      const parsed = new URL(url);
      const path = parsed.pathname.replace(/\/+$/, "");
      return parsed.host.replace(/^www\./, "") + path;
    } catch (error) {
      return url;
    }
  }

  function renderItem(host, network) {
    const item = document.createElement("li");
    item.className = "blog-social-item";
    item.dataset.network = network.key;

    const label = document.createElement("span");
    label.className = "blog-social-label";
    label.textContent = network.label;
    item.append(label);

    const url = permalink(host, network.key);
    if (!url) {
      const empty = document.createElement("span");
      empty.className = "blog-social-empty";
      empty.textContent = EMPTY_LABEL;
      item.append(empty);
      item.classList.add("is-empty");
      return item;
    }

    const link = document.createElement("a");
    link.className = "blog-social-link";
    link.href = url;
    link.rel = "noopener noreferrer me";
    link.target = "_blank";
    link.textContent = linkText(url);
    item.append(link);
    return item;
  }

  function cleanText(value) {
    return String(value || "").replace(/\s+/g, " ").trim();
  }

  function clipped(value, limit) {
    const text = String(value || "");
    if (text.length <= limit) return text;
    return text.slice(0, Math.max(0, limit - 1)).trimEnd() + "…";
  }

  function postContext() {
    const title = cleanText(
      document.querySelector("article h1, main h1")?.textContent ||
      document.querySelector('meta[property="og:title"]')?.content ||
      document.title,
    );
    const description = cleanText(
      document.querySelector(".lede")?.textContent ||
      document.querySelector('meta[name="description"]')?.content ||
      document.querySelector("article p")?.textContent,
    );
    const image = document.querySelector(
      'main img[alt], article img[alt], meta[property="og:image"]',
    );
    const alt = cleanText(
      image?.getAttribute?.("alt") ||
      document.querySelector('meta[property="og:image:alt"]')?.content ||
      `ForkMesh artwork for “${title}”`,
    );
    const canonical =
      document.querySelector('link[rel="canonical"]')?.href ||
      location.href.split("#", 1)[0];
    const article = cleanText(document.querySelector("article")?.innerText);
    return { title, description, alt, url: canonical, article };
  }

  function socialDrafts() {
    const context = postContext();
    const mastodonTail =
      `\n\nImage alt: ${context.alt}` +
      `\n\n#ForkMesh #OpenSource #DevTools\n${context.url}`;
    const mastodonPrefix = `${context.title}\n\n`;
    const mastodon = mastodonPrefix +
      clipped(
        context.description,
        MASTODON_LIMIT - mastodonPrefix.length - mastodonTail.length,
      ) +
      mastodonTail;
    const twitterTail = "\n\n#ForkMesh #OpenSource";
    const twitterPrefix = `${context.title} — `;
    const twitter = twitterPrefix +
      clipped(
        context.description,
        TWITTER_LIMIT - twitterPrefix.length - twitterTail.length,
      ) +
      twitterTail;
    const reddit =
      `${context.article || context.description}\n\nRead the post: ${context.url}`;
    return {
      context,
      mastodon: clipped(mastodon, MASTODON_LIMIT),
      twitter: clipped(twitter, TWITTER_LIMIT),
      reddit,
    };
  }

  function shareHref(network, drafts) {
    if (network === "mastodon") {
      return "https://mastodon.social/share?" +
        new URLSearchParams({ text: drafts.mastodon }).toString();
    }
    if (network === "twitter") {
      return "https://x.com/intent/post?" +
        new URLSearchParams({
          text: drafts.twitter,
          url: drafts.context.url,
        }).toString();
    }
    return "https://www.reddit.com/r/forkmesh/submit?" +
      new URLSearchParams({
        title: drafts.context.title,
        text: drafts.reddit,
      }).toString();
  }

  function renderDraft(network, label, text, href, limit = 0) {
    const section = document.createElement("section");
    section.className = "blog-social-draft";
    const heading = document.createElement("h3");
    heading.textContent = label;
    const count = document.createElement("span");
    count.className = "blog-social-count";
    count.textContent = limit
      ? `${text.length}/${limit} characters`
      : `${text.length.toLocaleString()} characters · full length`;
    const textarea = document.createElement("textarea");
    textarea.readOnly = true;
    textarea.rows = network === "reddit" ? 10 : 7;
    textarea.value = text;
    textarea.setAttribute("aria-label", `${label} draft`);
    const link = document.createElement("a");
    link.className = "blog-social-prefill";
    link.href = href;
    link.target = "_blank";
    link.rel = "noopener noreferrer";
    link.textContent = `Open ${label} with this post prefilled ↗`;
    section.append(heading, count, textarea, link);
    return section;
  }

  function blogSlug() {
    const match = location.pathname.match(/^\/blog\/([a-z0-9-]+)\/?$/);
    return match ? match[1] : "";
  }

  function renderMetrics(host, payload) {
    if (!payload?.ok) return;
    const section = document.createElement("section");
    section.className = "blog-social-metrics";
    const title = document.createElement("h2");
    title.textContent = "Post reach";
    const totals = document.createElement("p");
    totals.className = "blog-social-metric-counts";
    totals.textContent =
      `${Number(payload.views || 0).toLocaleString()} views · ` +
      `${Number(payload.uniqueViews || 0).toLocaleString()} approximate unique views`;
    section.append(title, totals);
    const board = Array.isArray(payload.referrers) ? payload.referrers : [];
    if (board.length) {
      const subtitle = document.createElement("h3");
      subtitle.textContent = "HTTP referrer leaderboard";
      const list = document.createElement("ol");
      list.className = "blog-social-referrers";
      board.forEach((row) => {
        const item = document.createElement("li");
        const link = document.createElement("a");
        link.href = String(row.url || "");
        link.target = "_blank";
        link.rel = "noopener noreferrer";
        link.textContent = String(row.host || "");
        const visits = document.createElement("span");
        visits.textContent = `${Number(row.visits || 0).toLocaleString()} visits`;
        item.append(link, visits);
        list.append(item);
      });
      section.append(subtitle, list);
    }
    host.prepend(section);
  }

  async function hydrateMetrics(host) {
    const slug = blogSlug();
    if (!slug || location.protocol === "file:") return;
    try {
      const response = await fetch(
        `/api/blog/metrics/${encodeURIComponent(slug)}`,
        { headers: { accept: "application/json" }, cache: "no-store" },
      );
      if (response.ok) renderMetrics(host, await response.json());
    } catch (_) {}
  }

  function render(host) {
    host.classList.add("blog-social");
    host.textContent = "";

    const title = document.createElement("h2");
    title.className = "blog-social-title";
    title.textContent = "Posted on";
    host.append(title);

    const list = document.createElement("ul");
    list.className = "blog-social-list";
    NETWORKS.forEach((network) => list.append(renderItem(host, network)));
    host.append(list);

    const drafts = socialDrafts();
    const draftTitle = document.createElement("h2");
    draftTitle.className = "blog-social-title blog-social-draft-title";
    draftTitle.textContent = "Share this post";
    const grid = document.createElement("div");
    grid.className = "blog-social-drafts";
    grid.append(
      renderDraft(
        "mastodon",
        "Mastodon",
        drafts.mastodon,
        shareHref("mastodon", drafts),
        MASTODON_LIMIT,
      ),
      renderDraft(
        "twitter",
        "X (Twitter)",
        drafts.twitter,
        shareHref("twitter", drafts),
        TWITTER_LIMIT,
      ),
      renderDraft(
        "reddit",
        "Reddit",
        drafts.reddit,
        shareHref("reddit", drafts),
      ),
    );
    host.append(draftTitle, grid);
    void hydrateMetrics(host);
  }

  function renderAll() {
    document.querySelectorAll("[data-blog-social]").forEach(render);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", renderAll);
  } else {
    renderAll();
  }
})();
