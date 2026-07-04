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
    // no password required. selectedAgentId (adhoc #259) is the id of the agent
    // whose detail page — live transcript + prompt — is currently open, or null
    // for the session list.
    agentsView: { agents: [], selectedAgentId: null },
  };

  // Auto-refresh timer for the Agents tab (adhoc #182): polls the session list
  // every ~10s while that tab is active and unlocked, so status/turn/cost
  // updates and prompt replies show up without a manual click. Cleared on tab
  // switch and on repo (re)open — see stopRepoAgentsAutoRefresh callers below.
  let repoAgentsRefreshTimer = null;

  // Faster poll (adhoc #259) that keeps the open agent detail page's transcript
  // live while it's on screen. Separate from the list poll so it can refresh
  // just the transcript pane without rebuilding the prompt input or losing the
  // caret. Cleared whenever the detail page closes — see stopRepoAgentTranscriptRefresh.
  let repoAgentTranscriptTimer = null;

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const MAX_REPO_FILE_FINDER_RESULTS = 500;
  const MAX_REPO_FILE_FINDER_SECONDS = 6;
  const REPO_COLLECTION_PAGE_SIZE = 5;
  const PROFILE_SYNC_INTERVAL_MS = 60000;
  const DASHBOARD_THEME_KEY = "forkmesh.dashboard.theme";

