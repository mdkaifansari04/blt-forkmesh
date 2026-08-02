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
    // Last rendered pull/discussion list per kind, and the last rendered code
    // tree listing. Both are held so the header search box can re-filter what
    // is already on screen (adhoc #37) without a second mirror read.
    repoCollectionItems: {},
    repoTreeView: null,
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
    // /dashboard/tasks (adhoc #24): the organization-private task catalog, read
    // whole and then filtered and paged in the browser. `actor` is the relay's
    // name for the signed-in member, used by the "Assigned to me" filter.
    tasksView: {
      items: [],
      filter: "open",
      query: "",
      page: 1,
      loading: true,
      error: "",
      actor: "",
    },
    globalSearch: {
      open: false,
      selectedIndex: 0,
      results: [],
      // The header search box also filters the page you are on, live, on top of
      // whatever that page's own filter box holds (adhoc #37). Kept here rather
      // than read off the input so a re-render after navigation still sees it.
      pageQuery: "",
    },
  };

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const MAX_REPO_FILE_FINDER_RESULTS = 500;
  const MAX_REPO_FILE_FINDER_SECONDS = 6;
  const REPO_COLLECTION_PAGE_SIZE = 25;
  const DASHBOARD_THEME_KEY = "forkmesh.dashboard.theme";
