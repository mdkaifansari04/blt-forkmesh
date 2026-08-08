(() => {
  "use strict";

  if (window.ForkMeshAPI?.installed) return;

  const STORAGE_KEY = "forkmesh.apiOrigin";
  const OFFICIAL_APP_ORIGIN = "https://app.forkmesh.com";
  const LEGACY_OFFICIAL_API_ORIGIN = "https://api.forkmesh.com";
  const NativeWebSocket = window.WebSocket;
  const nativeFetch = window.fetch.bind(window);
  const canonicalFrontendHosts = new Set([
    "forkmesh.com",
    "www.forkmesh.com",
    "app.forkmesh.com",
    "world.forkmesh.com",
  ]);

  function normalizeOrigin(value) {
    const raw = String(value || "").trim();
    if (!raw) return "";
    let url;
    try {
      url = new URL(raw);
    } catch (_) {
      return "";
    }
    if (url.username || url.password || url.pathname !== "/" || url.search || url.hash) {
      return "";
    }
    if (url.protocol === "https:") {
      return url.origin === LEGACY_OFFICIAL_API_ORIGIN
        ? OFFICIAL_APP_ORIGIN
        : url.origin;
    }
    if (
      url.protocol === "http:" &&
      ["localhost", "127.0.0.1", "[::1]"].includes(url.hostname)
    ) {
      return url.origin;
    }
    return "";
  }

  function storedOrigin() {
    try {
      const value = localStorage.getItem(STORAGE_KEY);
      const origin = normalizeOrigin(value);
      if (origin === OFFICIAL_APP_ORIGIN && String(value || "").trim()) {
        const legacy = new URL(String(value).trim());
        if (legacy.origin === LEGACY_OFFICIAL_API_ORIGIN) {
          localStorage.setItem(STORAGE_KEY, OFFICIAL_APP_ORIGIN);
        }
      }
      return origin;
    } catch (_) {
      return "";
    }
  }

  function metadataOrigin() {
    try {
      return normalizeOrigin(
        document.querySelector('meta[name="forkmesh-api-origin"]')?.content,
      );
    } catch (_) {
      return "";
    }
  }

  function resolveOrigin() {
    const configured =
      normalizeOrigin(window.FORKMESH_API_ORIGIN) ||
      metadataOrigin() ||
      storedOrigin();
    if (configured) return configured;
    if (canonicalFrontendHosts.has(location.hostname.toLowerCase())) {
      return OFFICIAL_APP_ORIGIN;
    }
    return location.origin;
  }

  function isApiPath(pathname) {
    return pathname.startsWith("/api/");
  }

  function routedHttpURL(value) {
    const raw = value instanceof Request ? value.url : value;
    let url;
    try {
      url = new URL(raw, location.href);
    } catch (_) {
      return null;
    }
    if (url.origin === location.origin && isApiPath(url.pathname)) {
      const api = new URL(resolveOrigin());
      url.protocol = api.protocol;
      url.host = api.host;
    }
    return url;
  }

  function apiTarget(url) {
    return Boolean(
      url && url.origin === resolveOrigin() && isApiPath(url.pathname),
    );
  }

  function routedFetch(input, init) {
    const url = routedHttpURL(input);
    if (!url) return nativeFetch(input, init);
    const original = input instanceof Request ? new URL(input.url) : new URL(input, location.href);
    let routedInput = input;
    if (url.href !== original.href) {
      routedInput = input instanceof Request
        ? new Request(url.href, input)
        : input instanceof URL
          ? url
          : url.href;
    }
    if (!apiTarget(url)) return nativeFetch(routedInput, init);
    const options = init ? { ...init } : {};
    const credentials = options.credentials || (
      input instanceof Request ? input.credentials : ""
    );
    if (!credentials || credentials === "same-origin") {
      options.credentials = "include";
    }
    if (options.mode === "same-origin") options.mode = "cors";
    return nativeFetch(routedInput, options);
  }

  function websocketURL(value) {
    let url;
    try {
      url = new URL(String(value || ""), location.href);
    } catch (_) {
      return String(value || "");
    }
    const httpProtocol = url.protocol === "wss:" ? "https:" : "http:";
    const current = `${httpProtocol}//${url.host}`;
    if (current === location.origin && isApiPath(url.pathname)) {
      const api = new URL(resolveOrigin());
      url.protocol = api.protocol === "https:" ? "wss:" : "ws:";
      url.host = api.host;
    }
    return url.href;
  }

  function RoutedWebSocket(url, protocols) {
    if (!new.target) throw new TypeError("WebSocket constructor requires new");
    const target = websocketURL(url);
    return protocols === undefined
      ? new NativeWebSocket(target)
      : new NativeWebSocket(target, protocols);
  }

  if (NativeWebSocket) {
    RoutedWebSocket.prototype = NativeWebSocket.prototype;
    Object.setPrototypeOf(RoutedWebSocket, NativeWebSocket);
    for (const name of ["CONNECTING", "OPEN", "CLOSING", "CLOSED"]) {
      Object.defineProperty(RoutedWebSocket, name, {
        value: NativeWebSocket[name],
        enumerable: true,
      });
    }
    window.WebSocket = RoutedWebSocket;
  }

  function sameSite() {
    const api = new URL(resolveOrigin());
    if (api.origin === location.origin) return true;
    if (api.protocol !== location.protocol) return false;
    const currentHost = location.hostname.toLowerCase();
    const apiHost = api.hostname.toLowerCase();
    if (canonicalFrontendHosts.has(currentHost) && canonicalFrontendHosts.has(apiHost)) {
      return true;
    }
    return currentHost === apiHost;
  }

  function configure(value, persist = true) {
    const origin = normalizeOrigin(value);
    if (!origin) throw new TypeError("ForkMesh API origin must be HTTPS");
    if (persist) localStorage.setItem(STORAGE_KEY, origin);
    window.FORKMESH_API_ORIGIN = origin;
    window.FORKMESH_RELAY_HOST = new URL(origin).host;
    window.dispatchEvent(new CustomEvent("forkmesh:api-origin", {
      detail: { origin },
    }));
    return origin;
  }

  function clearConfiguredOrigin() {
    try {
      localStorage.removeItem(STORAGE_KEY);
    } catch (_) {}
    delete window.FORKMESH_API_ORIGIN;
    window.FORKMESH_RELAY_HOST = new URL(resolveOrigin()).host;
  }

  const api = {
    installed: true,
    storageKey: STORAGE_KEY,
    configure,
    clearConfiguredOrigin,
    fetch: routedFetch,
    url(path = "/api") {
      return new URL(String(path || "/api"), resolveOrigin()).href;
    },
    websocketURL,
  };
  Object.defineProperties(api, {
    origin: { enumerable: true, get: resolveOrigin },
    isSameSite: { enumerable: true, get: sameSite },
  });
  window.ForkMeshAPI = Object.freeze(api);
  window.FORKMESH_RELAY_HOST = new URL(resolveOrigin()).host;
  window.fetch = routedFetch;
})();
