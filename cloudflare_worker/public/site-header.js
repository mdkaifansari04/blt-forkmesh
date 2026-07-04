(() => {
  const SIMPLE_HEADER_HTML = `
    <header class="forkmesh-simple-header relative z-10 mx-auto flex w-full max-w-7xl items-center justify-between px-5 py-6 lg:px-8">
      <a href="/" class="brand forkmesh-simple-brand inline-flex items-center gap-3" aria-label="ForkMesh home">
        <img class="brand-mark forkmesh-simple-brand-mark h-10 w-auto" src="/assets/logo.png" alt="" aria-hidden="true" />
      </a>
      <nav class="forkmesh-simple-header-nav hidden items-center gap-6 text-sm text-muted-foreground md:flex" aria-label="Primary">
        <a class="transition-colors hover:text-foreground" href="/docs">Docs</a>
        <a class="transition-colors hover:text-foreground" href="/blogs">Blog</a>
        <a class="transition-colors hover:text-foreground" href="/login">Login</a>
      </nav>
    </header>
  `;

  document.querySelectorAll("[data-forkmesh-header]").forEach((mount) => {
    if (mount.dataset.forkmeshHeader !== "simple") return;
    mount.outerHTML = SIMPLE_HEADER_HTML;
  });
})();
