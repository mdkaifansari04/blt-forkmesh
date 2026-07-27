(() => {
  // Where a post was announced on the social networks we run. Each blog post
  // carries its own permalinks as data attributes on a placeholder element:
  //
  //   <section data-blog-social
  //            data-reddit=""
  //            data-mastodon=""
  //            data-twitter=""></section>
  //
  // Filling one in is pasting the permalink into the matching attribute of
  // that post's index.html — there is no second list to keep in sync. The
  // ones left blank still render, as an explicit empty slot rather than a
  // silent gap, so an unannounced post reads as unannounced.
  const NETWORKS = [
    { key: "reddit", label: "Reddit" },
    { key: "mastodon", label: "Mastodon" },
    { key: "twitter", label: "X (Twitter)" },
  ];

  const EMPTY_LABEL = "Not posted yet";

  // Only real web permalinks become links: a blank attribute and anything
  // that is not http(s) (a stray note, a javascript: URL) both read as empty.
  function permalink(host, key) {
    const raw = (host.dataset[key] || "").trim();
    return /^https?:\/\//i.test(raw) ? raw : "";
  }

  // Show the permalink itself rather than a generic "View post", trimmed to
  // the part that identifies it: forkmesh.com's own network handles are the
  // point of the row.
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
