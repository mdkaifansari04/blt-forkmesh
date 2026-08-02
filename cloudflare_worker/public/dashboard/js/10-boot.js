  // ---------------------------------------------------------------------------
  // Boot. Every page document ships this same bundle; <body data-page="..."> is
  // baked at build time (dashboard_shell.PAGES) and picks which init runs. The
  // page's own markup is the only view in the document and is active at parse
  // time, so the right page paints immediately — data fills in afterwards.
  const PAGE_INITS = {
    "home": initHomePage,
    "repos": initReposPage,
    "network": initNetworkPage,
    "chat": initChatPage,
    "tasks": initTasksPage,
    "settings": initSettingsPage,
    "profile": initProfileOverviewPage,
    "profile-repositories": initProfileOverviewPage,
    "repo": initRepoPage,
  };

  applyDashboardTheme(readDashboardTheme());
  if (initSharedChrome()) {
    startAccountSessionWatch();
    (PAGE_INITS[currentPage()] || initHomePage)();
    initPageHistory();
  }
})();
