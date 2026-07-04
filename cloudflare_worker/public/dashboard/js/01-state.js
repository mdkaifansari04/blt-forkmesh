(() => {
  const state = {
    repositories: [],
    filteredRepositories: [],
    filteredGroups: [],
    page: 1,
    pageSize: 5,
    selectedRepo: null,
    selectedBranches: {},
    repoBranches: {},
    repoBranchQueries: {},
    repoCollectionPages: {},
    session: null,
    profileSyncTimer: null,
    repoFileFinder: {
      repoKey: "",
      files: [],
      indexed: false,
      indexing: false,
      partial: false,
      error: "",
      selectedIndex: 0,
    },
    nodeNameAvailability: {
      candidate: "",
      available: false,
      checking: false,
      seq: 0,
    },
    notifications: [],
    notificationUnread: 0,
    pollProfileToken: null,
    pollNotifToken: null,
    selectedNotificationId: "",
    issuesView: { filter: "open", items: [] },
    claimNode: { pendingNodeId: "" },
    linkGrant: null,
    repoMirrors: [],
    repoServedBy: null,
    // Owner-only "Agents" tab (adhoc #225): owner verification by node account,
    // no password required.
    agentsView: { agents: [] },
  };

  // Auto-refresh timer for the Agents tab (adhoc #182): polls the session list
  // every ~10s while that tab is active and unlocked, so status/turn/cost
  // updates and prompt replies show up without a manual click. Cleared on tab
  // switch and on repo (re)open — see stopRepoAgentsAutoRefresh callers below.
  let repoAgentsRefreshTimer = null;

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const MAX_REPO_FILE_FINDER_RESULTS = 500;
  const MAX_REPO_FILE_FINDER_SECONDS = 6;
  const REPO_COLLECTION_PAGE_SIZE = 5;
  const PROFILE_SYNC_INTERVAL_MS = 60000;
  const DASHBOARD_THEME_KEY = "forkmesh.dashboard.theme";

