import {
  decryptObject,
  derivePassphraseKey,
  encryptObject,
} from "./chat-crypto.js";

const INITIAL_RECONNECT_MS = 2000;
const MAX_RECONNECT_MS = 30000;

function websocketUrl(value, locationLike) {
  const path = String(value || "");
  if (/^wss?:\/\//i.test(path)) return path;
  const scheme = locationLike.protocol === "https:" ? "wss:" : "ws:";
  return new URL(path, `${scheme}//${locationLike.host}`).href;
}

function roomKeyForAccess(access) {
  if (access?.key) return Promise.resolve(access.key);
  if (!access?.passphrase || !access?.roomName) {
    return Promise.reject(new Error("Room access did not include encryption data"));
  }
  return derivePassphraseKey(access.passphrase, access.roomName);
}

export function createChatRoomTransport({
  authorize,
  onPlain = () => {},
  onState = () => {},
  WebSocketImpl = globalThis.WebSocket,
  locationLike = globalThis.location,
  setTimeoutImpl = globalThis.setTimeout.bind(globalThis),
  clearTimeoutImpl = globalThis.clearTimeout.bind(globalThis),
} = {}) {
  if (typeof authorize !== "function") {
    throw new TypeError("createChatRoomTransport requires authorize(room)");
  }
  if (typeof WebSocketImpl !== "function") {
    throw new TypeError("WebSocket is unavailable");
  }

  let currentRoom = null;
  let currentAccess = null;
  let currentKey = null;
  let socket = null;
  let timer = null;
  let generation = 0;
  let reconnectDelayMs = INITIAL_RECONNECT_MS;
  let transportState = "idle";
  let suspended = false;
  let disposed = false;

  function transition(next, detail = {}) {
    transportState = next;
    onState(next, { ...detail, room: currentRoom });
  }

  function clearReconnect() {
    if (timer === null) return;
    clearTimeoutImpl(timer);
    timer = null;
  }

  function detachSocket(code, reason) {
    const previous = socket;
    socket = null;
    currentKey = null;
    currentAccess = null;
    if (!previous) return;
    try {
      previous.close(code, reason);
    } catch (_) {}
  }

  function scheduleReconnect() {
    if (disposed || suspended || timer !== null || !currentRoom) return;
    transition("reconnecting", { delayMs: reconnectDelayMs });
    const delay = reconnectDelayMs;
    reconnectDelayMs = Math.min(reconnectDelayMs * 2, MAX_RECONNECT_MS);
    timer = setTimeoutImpl(() => {
      timer = null;
      openCurrentRoom(true);
    }, delay);
  }

  async function openCurrentRoom(isReconnect = false) {
    if (disposed || suspended || !currentRoom) return false;
    clearReconnect();
    const attempt = ++generation;
    const room = currentRoom;
    detachSocket(1000, isReconnect ? "reconnecting" : "room changed");
    transition("authorizing");

    let access;
    let key;
    try {
      access = await authorize(room);
      if (attempt !== generation || disposed || suspended || room !== currentRoom) {
        return false;
      }
      key = await roomKeyForAccess(access);
      if (attempt !== generation || disposed || suspended || room !== currentRoom) {
        return false;
      }
    } catch (error) {
      if (attempt !== generation || disposed || suspended) return false;
      transition(error?.code === "auth" ? "unauthorized" : "unavailable", { error });
      if (error?.code !== "auth") scheduleReconnect();
      return false;
    }

    currentAccess = access;
    currentKey = key;
    transition("connecting");
    let roomSocket;
    try {
      roomSocket = new WebSocketImpl(
        websocketUrl(access.webSocketUrl, locationLike),
      );
    } catch (error) {
      currentAccess = null;
      currentKey = null;
      transition("unavailable", { error });
      scheduleReconnect();
      return false;
    }
    socket = roomSocket;

    roomSocket.addEventListener("open", () => {
      if (attempt !== generation || roomSocket !== socket) {
        try {
          roomSocket.close(1000, "stale room connection");
        } catch (_) {}
        return;
      }
      reconnectDelayMs = INITIAL_RECONNECT_MS;
      transition("connected", { access });
    });

    roomSocket.addEventListener("message", async (event) => {
      if (attempt !== generation || roomSocket !== socket) return;
      if (typeof event.data !== "string") return;
      let envelope;
      try {
        envelope = JSON.parse(event.data);
      } catch (_) {
        return;
      }
      const plain = await decryptObject(envelope, key);
      if (!plain || attempt !== generation || roomSocket !== socket) return;
      onPlain(plain, room, access);
    });

    roomSocket.addEventListener("close", (event = {}) => {
      if (attempt !== generation || roomSocket !== socket) return;
      socket = null;
      currentAccess = null;
      currentKey = null;
      if (disposed || suspended) return;
      if (Number(event.code) === 1008) {
        transition("unauthorized", { closeCode: 1008 });
        return;
      }
      scheduleReconnect();
    });

    roomSocket.addEventListener("error", (error) => {
      if (attempt !== generation || roomSocket !== socket) return;
      transition("unavailable", { error });
      try {
        roomSocket.close();
      } catch (_) {}
    });
    return true;
  }

  return {
    get state() {
      return transportState;
    },
    get room() {
      return currentRoom;
    },
    get access() {
      return currentAccess;
    },
    get connected() {
      return Boolean(
        socket && socket.readyState === WebSocketImpl.OPEN && currentKey,
      );
    },
    async connect(room) {
      if (!room || typeof room !== "object") {
        throw new TypeError("connect(room) requires a room descriptor");
      }
      disposed = false;
      suspended = false;
      currentRoom = room;
      reconnectDelayMs = INITIAL_RECONNECT_MS;
      return openCurrentRoom(false);
    },
    async send(plain, { persist = false } = {}) {
      if (!this.connected) return false;
      const activeSocket = socket;
      const envelope = await encryptObject(plain, currentKey);
      if (persist) envelope.persist = true;
      if (activeSocket !== socket || !this.connected) return false;
      activeSocket.send(JSON.stringify(envelope));
      return true;
    },
    suspend() {
      if (disposed) return;
      suspended = true;
      generation += 1;
      clearReconnect();
      detachSocket(1000, "room suspended");
      transition("suspended");
    },
    resume() {
      if (disposed || !currentRoom || !suspended) return Promise.resolve(false);
      suspended = false;
      return openCurrentRoom(false);
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      suspended = false;
      generation += 1;
      clearReconnect();
      detachSocket(1000, "transport disposed");
      currentRoom = null;
      transition("disposed");
    },
  };
}
