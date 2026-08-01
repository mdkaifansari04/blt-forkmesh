




  const PAGE_INITS = {
    "home": initHomePage,
    "repos": initReposPage,
    "network": initNetworkPage,
    "chat": initChatPage,
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
