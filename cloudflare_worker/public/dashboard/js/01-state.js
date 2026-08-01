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


    projectsView: { filter: "open", mode: "gantt", items: [] },
    claimNode: { pendingNodeId: "" },
    linkGrant: null,
    repoMirrors: [],
    repoLatestCommit: null,
    repoServedBy: null,




    agentsView: { agents: [], selectedAgentId: null },




    homeAgentSessions: null,



    homeBlogPosts: null,



    homeOrganizationRepositories: [],
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
