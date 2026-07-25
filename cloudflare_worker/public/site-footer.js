(() => {
  const FOOTER_COLUMNS = [
    {
      title: "Explore",
      links: [
        { href: "/features", label: "Features" },
        { href: "/network", label: "Network" },
        { href: "/desktop", label: "Desktop app" },
      ],
    },
    {
      title: "Company",
      links: [
        { href: "/about", label: "About" },
        { href: "/press", label: "Press" },
        { href: "/careers", label: "Careers" },
        { href: "/changelog", label: "Changelog" },
        { href: "/terms", label: "Terms" },
        { href: "/privacy", label: "Privacy" },
      ],
    },
    {
      title: "Plans",
      links: [
        { href: "/pricing", label: "Pricing" },
        { href: "/signup", label: "Create account" },
        { href: "/login", label: "Log In" },
        { href: "/mirror-payouts", label: "Mirror payouts" },
      ],
    },
    {
      title: "Learn",
      links: [
        { href: "/#faq", label: "FAQ" },
        { href: "/blog", label: "Blog" },
        { href: "/docs", label: "Documentation" },
        { href: "/security-report", label: "Security report" },
      ],
    },
    {
      title: "Community",
      links: [
        { href: "/status", label: "System status" },
        { href: "/chat", label: "Community chat" },
        {
          href: "https://reddit.com/r/forkmesh",
          label: "Reddit",
          external: true,
        },
        {
          href: "https://x.com/forkmesh",
          label: "X (Twitter)",
          external: true,
        },
      ],
    },
  ];

  const SOCIAL_LINKS = [
    {
      href: "https://x.com/forkmesh",
      label: "X (Twitter)",
      external: true,
      icon:
        '<path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-5.214-6.817L4.99 21.75H1.68l7.73-8.835L1.254 2.25H8.08l4.713 6.231zm-1.161 17.52h1.833L7.084 4.126H5.117z"></path>',
    },
    {
      href: "https://reddit.com/r/forkmesh",
      label: "Reddit",
      external: true,
      icon:
        '<path d="M12 0A12 12 0 0 0 0 12a12 12 0 0 0 12 12 12 12 0 0 0 12-12A12 12 0 0 0 12 0zm5.01 4.744c.688 0 1.25.561 1.25 1.249a1.25 1.25 0 0 1-2.498.056l-2.597-.547-.8 3.747c1.824.07 3.48.632 4.674 1.488.308-.309.73-.491 1.207-.491.968 0 1.754.786 1.754 1.754 0 .716-.435 1.333-1.01 1.614a3.111 3.111 0 0 1 .042.52c0 2.694-3.13 4.87-7.004 4.87-3.874 0-7.004-2.176-7.004-4.87 0-.183.015-.366.043-.534A1.748 1.748 0 0 1 4.028 12c0-.968.786-1.754 1.754-1.754.463 0 .898.196 1.207.49 1.207-.883 2.878-1.43 4.744-1.487l.885-4.182a.342.342 0 0 1 .14-.197.35.35 0 0 1 .238-.042l2.906.617a1.214 1.214 0 0 1 1.108-.701z"></path>',
    },
    {
      href: "/chat",
      label: "Chat",
      icon:
        '<path d="M4.804 21.644A6.707 6.707 0 0 0 6 21.75a6.721 6.721 0 0 0 3.583-1.029c.774.182 1.584.279 2.417.279 5.322 0 9.75-3.97 9.75-9 0-5.03-4.428-9-9.75-9s-9.75 3.97-9.75 9c0 2.409 1.025 4.587 2.674 6.192.232.226.277.428.254.543a3.73 3.73 0 0 1-.814 1.686.75.75 0 0 0 .44 1.223Z"></path>',
    },
  ];

  function attrsForLink(link) {
    return link.external ? ' target="_blank" rel="noopener noreferrer"' : "";
  }

  function renderColumn(column) {
    return `
          <div>
            <h3 class="site-footer-column-title">${column.title}</h3>
            <ul class="site-footer-link-list">
              ${column.links
                .map(
                  (link) => `
              <li><a class="site-footer-column-link" href="${link.href}"${attrsForLink(link)}>${link.label}</a></li>`,
                )
                .join("")}
            </ul>
          </div>`;
  }

  function renderSocialLink(link) {
    return `
              <a
                href="${link.href}"
                aria-label="${link.label}"
                class="site-footer-social-link"
                ${attrsForLink(link)}
              >
                <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
                  ${link.icon}
                </svg>
              </a>`;
  }

  // ForkMesh World band pinned to the very bottom of every page: a short intro
  // strip, then the live Town Square embedded edge-to-edge with no border
  // below it. The "full screen" link navigates the same window to /world/.
  function renderWorldBand() {
    return `
      <section class="site-footer-world" aria-labelledby="site-footer-world-title">
        <div class="site-footer-world-intro">
          <div class="site-footer-world-heading">
            <p class="site-footer-world-kicker">ForkMesh World</p>
            <h2 id="site-footer-world-title" class="site-footer-world-title">Step into the developer city</h2>
            <p class="site-footer-world-copy">
              The multiplayer Town Square is one part of ForkMesh, not a
              replacement for the website. Explore it here, then keep browsing
              repositories, source code, documentation, pricing, and the
              operations dashboard through normal web pages.
            </p>
          </div>
          <a href="/world/" class="site-footer-world-open">Open World full screen</a>
        </div>
        <iframe
          src="/world/"
          title="Interactive ForkMesh World"
          loading="lazy"
          referrerpolicy="same-origin"
          allow="fullscreen"
          class="site-footer-world-frame"
        ></iframe>
      </section>`;
  }

  function renderFooter(variant) {
    const isLanding = variant === "landing";
    const footerId = isLanding ? ' id="signup"' : "";

    return `
    <footer${footerId} class="forkmesh-footer forkmesh-footer-${isLanding ? "landing" : "standard"}" data-rendered-footer="${isLanding ? "landing" : "standard"}">
      <div aria-hidden="true" class="site-footer-word-wrap">
        <div
          id="footer-glow"
          class="site-footer-glow"
          style="--footer-glow-opacity: 0; --footer-glow-x: 50%; --footer-glow-y: 50%;"
        >
          <span class="site-footer-word footer-glow-base">forkmesh</span>
          <span class="site-footer-word footer-glow-text">forkmesh</span>
        </div>
      </div>
      <div class="site-footer-panel">
        <div class="site-footer-links-grid">
          <div class="site-footer-brand">
            <a href="/" aria-label="home" class="site-footer-home-link">
              <img src="/assets/logo.png" alt="ForkMesh logo" class="site-footer-logo" />
            </a>
            <p class="site-footer-brand-copy">
              Local-first distributed source-code preservation and collaboration infrastructure.
            </p>
            <div class="site-footer-socials">
              ${SOCIAL_LINKS.map(renderSocialLink).join("")}
            </div>
            <a href="/status" class="site-footer-status-pill">
              <span class="site-footer-status-dot" aria-hidden="true"></span>
              System status
            </a>
          </div>
          ${FOOTER_COLUMNS.map(renderColumn).join("")}
        </div>
        <div class="site-footer-copyright">
          &copy; 2026 ForkMesh. Built for local-first Git collaboration.
        </div>
      </div>
      ${renderWorldBand()}
    </footer>`;
  }

  function initFooterGlow(root) {
    const glow = root.querySelector(".site-footer-glow");
    if (!glow) return;

    const move = (event) => {
      const rect = glow.getBoundingClientRect();
      glow.style.setProperty("--footer-glow-x", `${event.clientX - rect.left}px`);
      glow.style.setProperty("--footer-glow-y", `${event.clientY - rect.top}px`);
      glow.style.setProperty("--footer-glow-opacity", "1");
    };

    glow.addEventListener("pointerenter", move);
    glow.addEventListener("pointermove", move);
    glow.addEventListener("pointerleave", () => {
      glow.style.setProperty("--footer-glow-opacity", "0");
    });
  }

  function mountFooter(host) {
    const variant = host.dataset.forkmeshFooter || "standard";
    const template = document.createElement("template");
    template.innerHTML = renderFooter(variant).trim();
    const footer = template.content.firstElementChild;
    host.replaceWith(footer);
    initFooterGlow(footer);
  }

  function mountFooters() {
    document
      .querySelectorAll("[data-forkmesh-footer]")
      .forEach((host) => mountFooter(host));
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", mountFooters);
  } else {
    mountFooters();
  }

  window.ForkMeshFooter = {
    renderFooter,
    mountFooters,
  };
})();
