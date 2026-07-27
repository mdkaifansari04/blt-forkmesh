(() => {
  const state = {
    repositories: [],
    externalRepositories: [],
    externalRepositoriesLoading: false,
    externalRepositorySelection: new Set(),
    filteredRepositories: [],
    filteredGroups: [],
    repositoriesLoading: true,
    page: 1,
    pageSize: 5,
    selectedRepo: null,
    fetchJsonInflight: {},
    fetchJsonCache: {},
    selectedBranches: {},
    repoBranches: {},
    repoBranchQueries: {},
    repoPullMetadataCommits: {},
    repoPullMetadataInflight: {},
    repoCollectionPages: {},
    session: null,
    // Public-profile mode (/@name): the FOREIGN account whose profile the
    // page is showing, or null when the profile pages show the session user.
    publicProfile: null,
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
    selectedNotificationId: "",
    issuesView: { filter: "open", items: [], query: "" },
    // Projects tab (issue #384): projects link issues + a milestone and carry
    // start/end dates; "gantt" is the default sub-view, "list" the fallback.
    projectsView: { filter: "open", mode: "gantt", items: [] },
    claimNode: { pendingNodeId: "" },
    linkGrant: null,
    repoMirrors: [],
    repoLatestCommit: null,
    repoServedBy: null,
    // Owner-only "Agents" tab (adhoc #225): owner verification by node account,
    // no password required. selectedAgentId (adhoc #259) is the id of the agent
    // whose detail page - live transcript + prompt - is currently open, or null
    // for the session list.
    agentsView: { agents: [], selectedAgentId: null },
    // Home left-rail "Active agent sessions" list (adhoc #81): aggregated,
    // non-terminal agent runs across the repos the session can assign agents
    // to. null until the first cross-repo fetch resolves so the panel can tell
    // "loading" apart from "no active sessions".
    homeAgentSessions: null,
    // Home right-rail "Latest from the blog" (adhoc #381): the newest posts
    // parsed from /blog/rss.xml. null until the feed read resolves so the card
    // can tell "loading" apart from "feed unavailable".
    homeBlogPosts: null,
    repoCommitDetail: null,
    repoRecordDetail: null,
    profileContributions: {
      range: null,
      data: null,
      loading: false,
      error: "",
      requestKey: "",
      selectedDay: "",
      cache: {},
    },
    settingsView: {
      section: "public-profile",
    },
    globalSearch: {
      open: false,
      selectedIndex: 0,
      results: [],
    },
  };

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const MAX_REPO_FILE_FINDER_RESULTS = 500;
  const MAX_REPO_FILE_FINDER_SECONDS = 6;
  const REPO_COLLECTION_PAGE_SIZE = 25;
  const DASHBOARD_THEME_KEY = "forkmesh.dashboard.theme";
