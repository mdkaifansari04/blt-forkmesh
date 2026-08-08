function copyRecord(value) {
  return value == null ? null : structuredClone(value);
}

function recordId(value) {
  return String(value || "").trim();
}

function replyOrder(left, right) {
  return (Number(left.ts) || 0) - (Number(right.ts) || 0) ||
    String(left.id).localeCompare(String(right.id));
}

export function createThreadStore() {
  const roots = new Map();
  const repliesById = new Map();
  const replyIdsByRoot = new Map();

  function replyRecords(rootId) {
    const ids = replyIdsByRoot.get(recordId(rootId)) || new Set();
    return [...ids]
      .map((id) => repliesById.get(id))
      .filter(Boolean)
      .sort(replyOrder);
  }

  function registerRoot(record) {
    const id = recordId(record?.id);
    if (!id) return null;
    const existing = roots.get(id);
    const next = {
      ...(existing || {}),
      ...copyRecord(record),
      id,
      deleted: Boolean(existing?.deleted || record?.deleted),
    };
    roots.set(id, next);
    return copyRecord(next);
  }

  function addReply(record) {
    const id = recordId(record?.id);
    const rootId = recordId(record?.rootId);
    if (!id || !rootId) return null;
    if (repliesById.has(id) || roots.has(id)) return copyRecord(repliesById.get(id));
    const next = { ...copyRecord(record), id, rootId };
    repliesById.set(id, next);
    const bucket = replyIdsByRoot.get(rootId) || new Set();
    bucket.add(id);
    replyIdsByRoot.set(rootId, bucket);
    return copyRecord(next);
  }

  function updateTarget(idValue, patch) {
    const id = recordId(idValue);
    const current = roots.get(id) || repliesById.get(id);
    if (!current || !patch || typeof patch !== "object") return null;
    const next = { ...current, ...copyRecord(patch), id: current.id };
    if (current.rootId) next.rootId = current.rootId;
    if (roots.has(id)) roots.set(id, next);
    else repliesById.set(id, next);
    return copyRecord(next);
  }

  function deleteTarget(idValue) {
    const id = recordId(idValue);
    const reply = repliesById.get(id);
    if (reply) {
      repliesById.delete(id);
      const bucket = replyIdsByRoot.get(reply.rootId);
      bucket?.delete(id);
      if (bucket && !bucket.size) replyIdsByRoot.delete(reply.rootId);
      return true;
    }
    const root = roots.get(id);
    if (!root) return false;
    if (replyRecords(id).length) {
      roots.set(id, {
        ...root,
        text: "",
        richText: null,
        attachment: null,
        deleted: true,
      });
    } else {
      roots.delete(id);
      replyIdsByRoot.delete(id);
    }
    return true;
  }

  function root(id) {
    return copyRecord(roots.get(recordId(id)) || null);
  }

  function target(idValue) {
    const id = recordId(idValue);
    return copyRecord(roots.get(id) || repliesById.get(id) || null);
  }

  function replies(rootId) {
    return replyRecords(rootId).map(copyRecord);
  }

  function summary(rootId) {
    const records = replyRecords(rootId);
    const participants = [...new Set(records
      .map((reply) => String(reply.sender || "").trim())
      .filter(Boolean))];
    const latest = records.at(-1) || null;
    return {
      count: records.length,
      participants,
      latestReplyTs: latest ? Number(latest.ts) || 0 : 0,
    };
  }

  function clearChannel(channelValue) {
    const channel = String(channelValue || "");
    const removedRoots = new Set();
    for (const [id, record] of roots) {
      if (record.channel !== channel) continue;
      roots.delete(id);
      removedRoots.add(id);
    }
    for (const [id, reply] of repliesById) {
      if (reply.channel !== channel && !removedRoots.has(reply.rootId)) continue;
      repliesById.delete(id);
      replyIdsByRoot.get(reply.rootId)?.delete(id);
    }
    for (const [rootId, bucket] of replyIdsByRoot) {
      if (removedRoots.has(rootId) || !bucket.size) replyIdsByRoot.delete(rootId);
    }
  }

  return {
    registerRoot,
    addReply,
    updateTarget,
    deleteTarget,
    root,
    target,
    replies,
    summary,
    clearChannel,
  };
}
