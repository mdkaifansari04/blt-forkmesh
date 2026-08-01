



















export const WEB_ISSUE_KEY_STORAGE = "forkmesh.webIssueKey";

const encoder = new TextEncoder();

function bytesToB64url(bytes) {
  const array = new Uint8Array(bytes);
  let binary = "";
  for (let index = 0; index < array.length; index += 1) {
    binary += String.fromCharCode(array[index]);
  }
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

async function sha256HexLower(text) {
  const digest = await crypto.subtle.digest("SHA-256", encoder.encode(text));
  return [...new Uint8Array(digest)]
    .map((byte) => byte.toString(16).padStart(2, "0"))
    .join("");
}



async function webIssueKey() {
  let stored = null;
  try {
    stored = JSON.parse(localStorage.getItem(WEB_ISSUE_KEY_STORAGE) || "null");
  } catch (_) {}
  if (stored && stored.jwk && stored.pub) {
    try {
      return {
        privateKey: await crypto.subtle.importKey(
          "jwk", stored.jwk, { name: "Ed25519" }, false, ["sign"]),
        pub: stored.pub,
      };
    } catch (_) {   }
  }
  const pair = await crypto.subtle.generateKey(
    { name: "Ed25519" }, true, ["sign", "verify"]);
  const jwk = await crypto.subtle.exportKey("jwk", pair.privateKey);
  const pub = bytesToB64url(await crypto.subtle.exportKey("raw", pair.publicKey));
  try {
    localStorage.setItem(WEB_ISSUE_KEY_STORAGE, JSON.stringify({ jwk, pub }));
  } catch (_) {}
  return {
    privateKey: await crypto.subtle.importKey(
      "jwk", jwk, { name: "Ed25519" }, false, ["sign"]),
    pub,
  };
}



export function issueTitleFromMessage(text) {
  const first = String(text || "").split(/\r?\n/)[0].trim().replace(/\s+/g, " ");
  return first.length > 200 ? `${first.slice(0, 199)}…` : first;
}



export function issueBodyFromMessage({ text, who, tsMs, channelLabel } = {}) {
  const when = new Date(Number(tsMs) || Date.now()).toISOString();
  const channel = String(channelLabel || "").trim();
  return `${String(text || "").trim()}\n\n---\nFiled from a ${
    channel ? `${channel} ` : ""
  }chat message by ${String(who || "someone")} at ${when}.`;
}

let repositoriesPromise = null;



export function loadIssueRepositories() {
  if (repositoriesPromise) return repositoriesPromise;
  repositoriesPromise = (async () => {
    const response = await fetch("/api/repositories", {
      cache: "no-store",
      credentials: "same-origin",
      headers: { accept: "application/json" },
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error("repository_catalog_unavailable");
    const seen = new Set();
    const entries = [];
    for (const repository of Array.isArray(payload?.repositories)
      ? payload.repositories
      : []) {
      const routeOwner = String(repository?.owner || "");
      const name = String(repository?.name || repository?.repo || "");



      const logicalOwners = Array.isArray(repository?.logicalOwners)
        ? repository.logicalOwners
        : ["user", "organization"].includes(String(repository?.ownerKind || "")) &&
            repository?.logicalOwner
          ? [{ kind: repository.ownerKind, owner: repository.logicalOwner }]
          : [];
      for (const identity of logicalOwners) {
        const logicalOwner = String(identity?.owner || "").toLowerCase();
        const value = `${logicalOwner}/${name}`;
        if (!logicalOwner || !name || !routeOwner || seen.has(value)) continue;
        seen.add(value);
        entries.push({
          value,
          label: `${value}${
            String(identity?.kind || "") === "organization" ? " · Organization" : ""
          }`,
          owner: routeOwner,
          name,
          repo: name,
        });
      }
    }
    entries.sort((a, b) => a.value.localeCompare(b.value));
    return entries;
  })().catch((error) => {
    repositoriesPromise = null;
    throw error;
  });
  return repositoriesPromise;
}

export async function fileWebIssue(repository, title, body, authorName = "") {
  const { privateKey, pub } = await webIssueKey();
  const ts = Math.floor(Date.now() / 1000);
  const cleanBody = String(body || "").replace(/[\r\n]+$/, "");

  const NUL = String.fromCharCode(0);
  const contentHash = await sha256HexLower(`${title}${NUL}${cleanBody}${NUL}`);
  const canonical =
    `forkmesh-issue-event-v1\nopen\n0\n${pub}\n${ts}\n${contentHash}`;
  const sig = bytesToB64url(await crypto.subtle.sign(
    { name: "Ed25519" }, privateKey, encoder.encode(canonical)));
  const payload = {
    owner: repository.owner,
    repo: repository.name,
    number: 0,
    titleIfNew: title,
    event: {
      type: "open",
      id: "open-web-" + ts,
      title,
      body: cleanBody,
      attachments: [],
      author: pub,
      authorName: String(authorName || ""),
      ts,
      sig,
    },
    meta: {
      labels: [],
      milestone: "",
      project: "",
      priority: 0,
      assignees: [],
      wantsAgent: false,
      model: "",
      provider: "",
    },
  };
  const response = await fetch(
    `/api/repo/${encodeURIComponent(repository.owner)}/${
      encodeURIComponent(repository.name)}/issues`,
    {
      method: "POST",
      headers: { "content-type": "application/json", accept: "application/json" },
      body: JSON.stringify(payload),
    },
  );
  const data = await response.json().catch(() => ({}));
  if (!response.ok || data.ok === false) {
    throw new Error(data.error || `HTTP ${response.status}`);
  }
  return data;
}
