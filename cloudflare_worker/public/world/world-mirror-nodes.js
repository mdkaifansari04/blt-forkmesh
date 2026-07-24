const COMMIT_PATTERN = /^[0-9a-f]{40,64}$/;

function text(value, fallback = "", limit = 80) {
  const cleaned = String(value ?? "")
    .replace(/[\u0000-\u001f\u007f]/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  return (cleaned || fallback).slice(0, limit);
}

function knownInteger(value, maximum = Number.MAX_SAFE_INTEGER) {
  if (value === null || value === undefined || value === "") return null;
  const number = Number(value);
  if (!Number.isSafeInteger(number) || number < 0) return null;
  return Math.min(number, maximum);
}

function knownNumber(value, minimum, maximum) {
  if (value === null || value === undefined || value === "") return null;
  const number = Number(value);
  if (!Number.isFinite(number) || number < minimum || number > maximum) {
    return null;
  }
  return number;
}

function commitHash(value) {
  const commit = String(value || "").trim().toLowerCase();
  return COMMIT_PATTERN.test(commit) ? commit : "";
}

function timestamp(value) {
  const number = Number(value);
  return Number.isSafeInteger(number) && number > 0 ? number : null;
}

function publicRepositoryRecord(mirror, payload) {
  return {
    owner: text(payload?.requestedOwner || payload?.owner, "", 80),
    name: text(
      payload?.requestedRepo || payload?.repo || mirror?.repo,
      "repository",
      120,
    ),
    commit: commitHash(mirror?.commit),
    branch: text(mirror?.branch, "", 120),
    status: text(mirror?.status, "unknown", 24).toLowerCase(),
    integrity: text(mirror?.integrity, "unknown", 24).toLowerCase(),
    cloneAvailable: mirror?.cloneAvailable === true,
    behind: mirror?.behind === true,
    lastSeen: timestamp(mirror?.lastSeen),
    lastSync: timestamp(mirror?.lastSync),
    syncAgeMs: knownInteger(mirror?.syncAgeMs),
    hostedSince: timestamp(mirror?.hostedSince),
    sizeBytes: knownInteger(mirror?.sizeBytes, 2 ** 50),
    issueCount: knownInteger(mirror?.issueCount, 1_000_000_000),
    commitCount: knownInteger(mirror?.commitCount, 1_000_000_000),
    branchCount: knownInteger(mirror?.branchCount, 1_000_000_000),
    pullCount: knownInteger(mirror?.pullCount, 1_000_000_000),
    discussionCount: knownInteger(mirror?.discussionCount, 1_000_000_000),
    worktreeCount: knownInteger(mirror?.worktreeCount, 1_000_000_000),
    artifactCount: knownInteger(mirror?.artifactCount, 1_000_000_000),
    clonesServed: knownInteger(mirror?.clonesServed, 1_000_000_000),
    websiteServed: knownInteger(mirror?.websiteServed, 1_000_000_000),
    platform: text(mirror?.platform, "", 24).toLowerCase(),
    version: text(mirror?.version, "", 32),
    id: text(mirror?.id, "", 120),
  };
}

function nodeAggregateRecord(node) {
  return {
    sizeBytes: knownInteger(node?.sizeBytes, 2 ** 50),
    issueCount: knownInteger(node?.issueCount, 1_000_000_000),
    commitCount: knownInteger(node?.commitCount, 1_000_000_000),
    branchCount: knownInteger(node?.branchCount, 1_000_000_000),
    pullCount: knownInteger(node?.pullCount, 1_000_000_000),
    discussionCount: knownInteger(node?.discussionCount, 1_000_000_000),
    worktreeCount: knownInteger(node?.worktreeCount, 1_000_000_000),
    artifactCount: knownInteger(node?.artifactCount, 1_000_000_000),
    clonesServed: knownInteger(node?.clonesServed, 1_000_000_000),
    websiteServed: knownInteger(node?.websiteServed, 1_000_000_000),
    commit: commitHash(node?.commit),
    branch: text(node?.branch, "", 120),
    lastSync: timestamp(node?.lastSync),
    platform: text(node?.platform, "", 24).toLowerCase(),
    version: text(node?.version, "", 32),
    nodeId: text(node?.nodeId || node?.id, "", 120),
  };
}

function publicResourceRecord(record) {
  const memoryTotalBytes = knownInteger(
    record?.memTotalBytes ?? record?.memoryTotalBytes,
    2 ** 50,
  );
  const memoryUsedBytes = knownInteger(
    record?.memUsedBytes ?? record?.memoryUsedBytes,
    2 ** 50,
  );
  const diskTotalBytes = knownInteger(record?.diskTotalBytes, 2 ** 50);
  const diskUsedBytes = knownInteger(record?.diskUsedBytes, 2 ** 50);
  return {
    cpuPercent: knownNumber(record?.cpuPercent, 0, 100),
    memoryUsedBytes:
      memoryTotalBytes !== null &&
      memoryUsedBytes !== null &&
      memoryUsedBytes <= memoryTotalBytes
        ? memoryUsedBytes
        : null,
    memoryTotalBytes,
    diskUsedBytes:
      diskTotalBytes !== null &&
      diskUsedBytes !== null &&
      diskUsedBytes <= diskTotalBytes
        ? diskUsedBytes
        : null,
    diskTotalBytes,
  };
}

/**
 * Build the bounded, truthful records consumed by the 3D server cabinets.
 *
 * Online membership comes from the live network snapshot. Repository facts come
 * from the public signed mirror payload, with the network leaderboard used only
 * as a fallback. Unknown and opted-out metrics remain null.
 */
export function buildLiveMirrorNodes(network, mirrorPayloads = []) {
  const stats = network?.stats || network || {};
  const onlineNames = Array.isArray(stats?.onlineNodes)
    ? stats.onlineNodes.map((name) => text(name, "", 80)).filter(Boolean)
    : [];
  const onlineLookup = new Map(
    onlineNames.map((name) => [name.toLowerCase(), name]),
  );
  const details = Array.isArray(network?.leaderboards?.nodes)
    ? network.leaderboards.nodes
    : Array.isArray(network?.nodes)
      ? network.nodes
      : [];
  const detailByName = new Map();
  details.slice(0, 100).forEach((node) => {
    const name = text(node?.name || node?.label, "", 80);
    if (name) detailByName.set(name.toLowerCase(), node);
  });

  const payloadList = Array.isArray(mirrorPayloads)
    ? mirrorPayloads
    : [mirrorPayloads];
  const repositoriesByNode = new Map();
  payloadList.slice(0, 100).forEach((payload) => {
    if (!Array.isArray(payload?.mirrors)) return;
    payload.mirrors.slice(0, 100).forEach((mirror) => {
      const name = text(mirror?.node || mirror?.owner, "", 80);
      if (!name) return;
      const key = name.toLowerCase();
      if (String(mirror?.status || "").toLowerCase() === "online") {
        onlineLookup.set(key, name);
      }
      const items = repositoriesByNode.get(key) || [];
      items.push(publicRepositoryRecord(mirror, payload));
      repositoriesByNode.set(key, items);
    });
  });

  return [...onlineLookup.entries()]
    .sort((left, right) => left[1].localeCompare(right[1]))
    .slice(0, 64)
    .map(([key, displayName]) => {
      const detail = detailByName.get(key) || {};
      const repositories = (repositoriesByNode.get(key) || [])
        .sort((left, right) =>
          `${left.owner}/${left.name}`.localeCompare(
            `${right.owner}/${right.name}`,
          ),
        )
        .slice(0, 32);
      const primary = repositories[0] || {};
      const hasPrimary = repositories.length > 0;
      const resources = publicResourceRecord({
        ...detail,
        ...(repositoriesByNode.get(key)?.length
          ? payloadList
              .flatMap((payload) =>
                Array.isArray(payload?.mirrors) ? payload.mirrors : [],
              )
              .find(
                (mirror) =>
                  text(mirror?.node || mirror?.owner, "", 80).toLowerCase() ===
                  key,
              )
          : {}),
      });
      const aggregate = nodeAggregateRecord(detail);
      return {
        name: displayName,
        online: true,
        healthy:
          primary.status === "online" &&
          primary.integrity === "ok",
        status: primary.status || "online",
        integrity: primary.integrity || "unknown",
        cloneAvailable:
          primary.cloneAvailable === true && primary.integrity === "ok",
        behind: primary.behind === true,
        platform: text(primary.platform || detail?.platform, "", 24).toLowerCase(),
        version: text(primary.version || detail?.version, "", 32),
        nodeId: text(primary.id || detail?.nodeId, "", 120),
        commit: hasPrimary ? primary.commit : aggregate.commit,
        branch: hasPrimary ? primary.branch : aggregate.branch,
        lastSeen: primary.lastSeen || null,
        lastSync: hasPrimary ? primary.lastSync : aggregate.lastSync,
        syncAgeMs: primary.syncAgeMs ?? null,
        sizeBytes: hasPrimary ? primary.sizeBytes : aggregate.sizeBytes,
        issueCount: hasPrimary ? primary.issueCount : aggregate.issueCount,
        commitCount: hasPrimary ? primary.commitCount : aggregate.commitCount,
        branchCount: hasPrimary ? primary.branchCount : aggregate.branchCount,
        pullCount: hasPrimary ? primary.pullCount : aggregate.pullCount,
        discussionCount: hasPrimary
          ? primary.discussionCount
          : aggregate.discussionCount,
        worktreeCount: hasPrimary
          ? primary.worktreeCount
          : aggregate.worktreeCount,
        artifactCount: hasPrimary
          ? primary.artifactCount
          : aggregate.artifactCount,
        clonesServed: hasPrimary
          ? primary.clonesServed
          : aggregate.clonesServed,
        websiteServed: hasPrimary
          ? primary.websiteServed
          : aggregate.websiteServed,
        repositories,
        ...resources,
      };
    });
}
