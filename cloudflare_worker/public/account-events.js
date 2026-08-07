// One authenticated, payload-free event channel per signed-in account.
//
// This is the browser half of the same push channel the desktop node holds:
// the per-owner ForkMeshNodes Durable Object behind /api/nodes/events. The
// relay pushes {"type":"event","topic"} frames — and nothing else — when
// account-scoped state changes that no repository owns: "pings" for the alert
// inbox, "direct-messages" for a conversation's unread count. The page answers
// with the one authenticated read it would otherwise have made on a timer.
//
// That is the whole point: pages read their unread counts ONCE on open and
// then sit here (docs/operations/polling-elimination.md). There is no fallback
// poll behind this socket, so liveness is the socket's own job — reconnect is
// unconditional with bounded backoff, and every successful (re)connect fires
// one catch-up so anything raised while the channel was down is not lost.
//
// A WebSocket upgrade cannot carry an Authorization header, so each attempt
// trades the session for a fresh 60s ticket from /api/accounts/event-ticket.

const TICKET_ENDPOINT = "/api/accounts/event-ticket";
const INITIAL_RECONNECT_MS = 2000;
const MAX_RECONNECT_MS = 30000;
// Matches the desktop node's cadence and stays well inside the Durable
// Object's 15-minute staleness reaper.
const KEEPALIVE_MS = 4 * 60 * 1000;

export function createAccountEventChannel({
  sessionToken,
  onTopic = () => {},
  onConnected = () => {},
  fetchImpl = globalThis.fetch.bind(globalThis),
  WebSocketImpl = globalThis.WebSocket,
  locationLike = globalThis.location,
  setTimeoutImpl = globalThis.setTimeout.bind(globalThis),
  clearTimeoutImpl = globalThis.clearTimeout.bind(globalThis),
  setIntervalImpl = globalThis.setInterval.bind(globalThis),
  clearIntervalImpl = globalThis.clearInterval.bind(globalThis),
} = {}) {
  if (typeof sessionToken !== "function") {
    throw new TypeError("createAccountEventChannel requires sessionToken()");
  }

  let socket = null;
  let reconnectTimer = null;
  let keepaliveTimer = null;
  let reconnectDelayMs = INITIAL_RECONNECT_MS;
  let generation = 0;
  let started = false;
  let disposed = false;

  function clearTimers() {
    if (reconnectTimer !== null) {
      clearTimeoutImpl(reconnectTimer);
      reconnectTimer = null;
    }
    if (keepaliveTimer !== null) {
      clearIntervalImpl(keepaliveTimer);
      keepaliveTimer = null;
    }
  }

  function scheduleReconnect() {
    if (disposed || !started || reconnectTimer !== null) return;
    const delay = reconnectDelayMs;
    reconnectDelayMs = Math.min(MAX_RECONNECT_MS, reconnectDelayMs * 2);
    reconnectTimer = setTimeoutImpl(() => {
      reconnectTimer = null;
      void connect();
    }, delay);
  }

  function closeSocket() {
    const current = socket;
    socket = null;
    if (!current) return;
    try {
      current.close(1000, "channel closed");
    } catch (_) {}
  }

  async function requestTicket() {
    let response;
    try {
      response = await fetchImpl(TICKET_ENDPOINT, {
        headers: {
          accept: "application/json",
          Authorization: `Bearer ${sessionToken()}`,
        },
        cache: "no-store",
      });
    } catch (_) {
      return "";
    }
    if (!response.ok) return "";
    const data = await response.json().catch(() => null);
    return String(data?.ticket || "");
  }

  async function connect() {
    if (disposed || !started) return;
    if (socket) return;
    // Signed out: stay dark instead of retrying forever, which is just a poll
    // of the ticket endpoint. restart() brings the channel up after a sign-in.
    if (!sessionToken()) {
      started = false;
      return;
    }
    const attempt = ++generation;
    const ticket = await requestTicket();
    if (!ticket) {
      // A session that could not be exchanged is a transient relay failure
      // (or a session that just expired, in which case the next attempt sees
      // no token and stops). Back off and try again.
      if (attempt === generation && started) scheduleReconnect();
      return;
    }
    if (disposed || !started || attempt !== generation) return;
    const scheme = locationLike.protocol === "https:" ? "wss:" : "ws:";
    const url = new URL(
      "/api/nodes/events",
      `${scheme}//${locationLike.host}`,
    );
    url.searchParams.set("ticket", ticket);
    let opened;
    try {
      opened = new WebSocketImpl(url.href);
    } catch (_) {
      scheduleReconnect();
      return;
    }
    socket = opened;
    opened.addEventListener("open", () => {
      if (socket !== opened) return;
      reconnectDelayMs = INITIAL_RECONNECT_MS;
      if (keepaliveTimer !== null) clearIntervalImpl(keepaliveTimer);
      keepaliveTimer = setIntervalImpl(() => {
        if (socket !== opened || opened.readyState !== 1) return;
        try {
          opened.send(JSON.stringify({ type: "ping" }));
        } catch (_) {}
      }, KEEPALIVE_MS);
      try {
        onConnected();
      } catch (_) {}
    });
    opened.addEventListener("message", (event) => {
      if (socket !== opened) return;
      let frame = null;
      try {
        frame = JSON.parse(event.data);
      } catch (_) {
        return;
      }
      if (!frame || frame.type !== "event") return;
      const topic = String(frame.topic || "");
      if (!topic) return;
      try {
        onTopic(topic);
      } catch (_) {}
    });
    opened.addEventListener("close", () => {
      if (socket !== opened) return;
      socket = null;
      if (keepaliveTimer !== null) {
        clearIntervalImpl(keepaliveTimer);
        keepaliveTimer = null;
      }
      scheduleReconnect();
    });
    opened.addEventListener("error", () => {
      if (socket === opened) {
        try {
          opened.close();
        } catch (_) {}
      }
    });
  }

  return {
    start() {
      if (disposed || started) return;
      started = true;
      reconnectDelayMs = INITIAL_RECONNECT_MS;
      void connect();
    },
    stop() {
      started = false;
      generation += 1;
      clearTimers();
      closeSocket();
    },
    // A sign-in or account switch invalidates the ticket this socket was
    // opened with; drop it and come back under the new identity.
    restart() {
      if (disposed) return;
      const wasStarted = started;
      this.stop();
      if (wasStarted || sessionToken()) this.start();
    },
    dispose() {
      disposed = true;
      this.stop();
    },
    get connected() {
      return Boolean(socket) && socket.readyState === 1;
    },
  };
}
