"""Mirror grouping, clone-fallback selection, and state-pin helpers.

Pure (builtin-only, no ``js``/``workers`` runtime, no ``env``/DB access) functions
pulled out of the ~11k-line ``entry.py`` so the logic that decides how repo
mirrors group together, which healthy mirror should serve a clone when the named
source-of-truth host is offline, and which attested repo states a mirror may
serve, all live in one small, independently-testable module. Imported back into
``entry.py`` like ``urls.py``/``solana.py``/``git_http.py`` (adhoc #215); the AST
tests load these helpers straight from this file.
"""


def _mirror_ms(value):
    try:
        n = int(value)
    except (TypeError, ValueError):
        return None
    return n if n > 0 else None


def repo_mirror_group_key(record):
    root = str((record or {}).get("rootCommit") or "").strip().lower()
    if root:
        return "root:" + root
    name = str((record or {}).get("name") or "").strip().lower()
    return "name:" + name


def repo_mirror_same_group(target, record):
    # Whether `record` mirrors the same logical repo as `target`. They match when
    # they share a root (first) commit. A mirror cloned from the relay can have an
    # unset HEAD and so publish an empty rootCommit (issue #243); when either side
    # lacks a root, fall back to matching the repo name so such a mirror still
    # groups with its source of truth instead of vanishing from the owner's
    # mirror-nodes list. Two differing non-empty roots mean a genuine fork with
    # rewritten history, which stays in its own group.
    troot = str((target or {}).get("rootCommit") or "").strip().lower()
    rroot = str((record or {}).get("rootCommit") or "").strip().lower()
    if troot and rroot:
        return troot == rroot
    tname = str((target or {}).get("name") or "").strip().lower()
    rname = str((record or {}).get("name") or "").strip().lower()
    return bool(tname) and tname == rname


def mirroring_owner_set(records):
    # Owners (node names) that host at least one repo ALSO hosted by a DIFFERENT
    # owner — i.e. a repo genuinely mirrored across nodes. Repos that only one
    # owner hosts (a single repo with no mirror elsewhere) don't qualify their
    # owner. Grouping reuses repo_mirror_group_key (root commit, name fallback)
    # so two nodes serving the same logical repo land in one group. Used to keep
    # nodes that mirror nothing for anyone out of the donation split (issue #94).
    groups = {}
    for rec in records or []:
        owner = str((rec or {}).get("owner") or "").strip()
        if not owner:
            continue
        groups.setdefault(repo_mirror_group_key(rec), set()).add(owner.lower())
    owners = set()
    for members in groups.values():
        if len(members) > 1:
            owners |= members
    return owners


def served_mirror_groups(records):
    # Mirror groups (root-commit / name keyed) that have at least one online,
    # PUBLIC host right now. `records` are catalog records already annotated with
    # rec["liveHost"] (the named node's own host presence). A group lands here as
    # soon as ANY node mirroring that logical repo is live, which is what lets a
    # repo stay cloneable/browsable in place through its own URL while its named
    # source of truth is down (adhoc #61). Private rows never serve a public
    # group, matching the clone fallback in select_clone_fallback.
    groups = set()
    for rec in records or []:
        if (rec or {}).get("liveHost") and (rec or {}).get("visibility") != "private":
            groups.add(repo_mirror_group_key(rec))
    return groups


def repo_clone_online(rec, served_groups):
    # Whether a repo is actually reachable for clone/browse right now: its own
    # named host is live, OR — for a public repo — a peer mirroring the same
    # logical repo is online and the relay will serve it in place (adhoc #61).
    # Private repos get no mirror fallback, so they depend on their own host.
    if (rec or {}).get("liveHost"):
        return True
    if (rec or {}).get("visibility") == "private":
        return False
    return repo_mirror_group_key(rec) in (served_groups or set())


def build_repo_mirrors_payload(
    owner, repo, rows, presence, first_hosted, now, stale_ms, sync_tolerance_ms,
    history=None,
):
    owner_l = (owner or "").strip().lower()
    repo_l = (repo or "").strip().lower()
    public_rows = []
    target = None
    for row in rows or []:
        rec = row.get("data") or {}
        if row.get("is_private") or rec.get("visibility") == "private":
            continue
        rec_owner = str(rec.get("owner") or "").strip()
        rec_name = str(rec.get("name") or "").strip()
        if not rec_owner or not rec_name:
            continue
        item = {"key_bi": row.get("key_bi"), "data": rec}
        public_rows.append(item)
        if rec_owner.lower() == owner_l and rec_name.lower() == repo_l:
            target = item
    if not target:
        return None

    group_key = repo_mirror_group_key(target["data"])
    members = [
        r for r in public_rows if repo_mirror_same_group(target["data"], r["data"])
    ]
    freshest_sync = 0
    for row in members:
        freshest_sync = max(freshest_sync, _mirror_ms(row["data"].get("lastSync")) or 0)

    # Is the logical repo's source of truth (a working-copy holder — "local-node")
    # online right now? While it is, a clone of a mirror whose refs fail the
    # integrity pin is transparently served from the source instead of the mirror
    # (see Default._online_source_of_truth), so that mirror is auto-healing, not
    # blocking — reported as "healing" rather than "rejected". The hard reject (and
    # its tamper protection) still applies when the source is offline.
    source_online = False
    for row in members:
        rec = row["data"]
        if str(rec.get("source") or "local-node") != "local-node":
            continue
        seen = _mirror_ms((presence or {}).get(row.get("key_bi")))
        if seen and now - seen <= stale_ms:
            source_online = True
            break

    def _int_field(rec, name):
        try:
            return int(rec.get(name))
        except (TypeError, ValueError):
            return -1

    mirrors = []
    for row in members:
        rec = row["data"]
        key = row.get("key_bi")
        seen = _mirror_ms((presence or {}).get(key))
        online = bool(seen and now - seen <= stale_ms)
        hosted = _mirror_ms(rec.get("hostedSince")) or _mirror_ms((first_hosted or {}).get(key))
        last_sync = _mirror_ms(rec.get("lastSync"))
        try:
            size_bytes = max(0, int(rec.get("sizeBytes") or 0))
        except (TypeError, ValueError):
            size_bytes = 0
        behind = bool(
            last_sync and freshest_sync and freshest_sync - last_sync > sync_tolerance_ms
        )
        issue_count = _int_field(rec, "issueCount")
        # Clones / website serves this node has provided; -1 == not advertised
        # (older peer or a record predating the counters), shown as an em-dash.
        clones_served = _int_field(rec, "clonesServed")
        website_served = _int_field(rec, "websiteServed")
        # Would the clone integrity gate serve this node right now? Its published
        # refs fingerprint (stateHash, the same one it signs on publish) must be
        # a state some working-copy holder in the group attested — current pin or
        # recent history — or every clone it serves is rejected with "repository
        # failed integrity check" (see clone_state_pins). Verdicts: "ok" (matches
        # a pin, or nothing is pinned and the gate fails open), "rejected" (its
        # fingerprint matches no attested state AND the source is offline, so the
        # tamper gate is actively blocking its clones), "healing" (fingerprint
        # matches nothing yet, but the source of truth is online, so clones are
        # served from the source and the mirror clears once it re-syncs — not a
        # failure), "unknown" (legacy record with no fingerprint; the gate checks
        # its live refs, which we can't see here).
        state_hash = str(rec.get("stateHash") or "").strip().lower()
        pins = clone_state_pins(rec, key, public_rows, history)
        if not pins or state_hash in pins:
            integrity = "ok"
        elif not state_hash:
            integrity = "unknown"
        elif source_online:
            integrity = "healing"
        else:
            integrity = "rejected"
        mirrors.append({
            "node": str(rec.get("owner") or "").strip(),
            "owner": str(rec.get("owner") or "").strip(),
            "repo": str(rec.get("name") or "").strip(),
            "status": "online" if online else "offline",
            "lastSeen": seen if online else None,
            "hostedSince": hosted,
            "syncAgeMs": max(0, now - last_sync) if last_sync else None,
            "lastSync": last_sync,
            "behind": behind,
            "cloneAvailable": online,
            "sizeBytes": size_bytes,
            "source": str(rec.get("source") or "").strip(),
            # Node facts the publishing node mirrored into its catalog record, so the
            # Mirror nodes view fills these columns even for an offline node (adhoc #56).
            "commit": str(rec.get("commit") or "").strip(),
            "branch": str(rec.get("branch") or "").strip(),
            "issueCount": issue_count,
            "commitCount": _int_field(rec, "commitCount"),
            "branchCount": _int_field(rec, "branchCount"),
            "pullCount": _int_field(rec, "pullCount"),
            "discussionCount": _int_field(rec, "discussionCount"),
            "worktreeCount": _int_field(rec, "worktreeCount"),
            "artifactCount": _int_field(rec, "artifactCount"),
            "platform": str(rec.get("platform") or "").strip(),
            "version": str(rec.get("version") or "").strip(),
            "id": str(rec.get("nodeId") or "").strip(),
            "clonesServed": clones_served,
            "websiteServed": website_served,
            "integrity": integrity,
        })

    mirrors.sort(
        key=lambda m: (
            0 if m["status"] == "online" else 1,
            -(m["lastSync"] or 0),
            m["node"].lower(),
        )
    )
    hosted_values = [m["hostedSince"] for m in mirrors if m["hostedSince"]]
    return {
        "ok": True,
        "owner": target["data"].get("owner"),
        "repo": target["data"].get("name"),
        "groupKey": group_key,
        "generatedAt": now,
        "summary": {
            "mirrors": len(mirrors),
            "online": sum(1 for m in mirrors if m["status"] == "online"),
            "cloneAvailable": sum(1 for m in mirrors if m["cloneAvailable"]),
            "dataHostedBytes": sum(m["sizeBytes"] for m in mirrors),
            "longestHostedSince": min(hosted_values) if hosted_values else None,
            "freshestSync": freshest_sync or None,
        },
        "mirrors": mirrors,
    }


def select_clone_fallback(owner, repo, rows, presence, now, stale_ms, source_online,
                          rotate=0):
    # Pick a healthy, online mirror to serve a clone of owner/repo from when the
    # named owner's own host is offline. This is what keeps a repo cloneable when
    # the source of truth goes down: a clone of /owner/repo is redirected to a peer
    # that mirrors the SAME logical repo (grouped by root commit, name fallback) and
    # is online right now. Returns the fallback owner's name, or None to fall
    # through to the normal named-owner host route.
    #
    # When several mirrors qualify, `rotate` (a per-repo counter the caller bumps
    # on every fallback) spreads clone traffic across them round-robin instead of
    # always hammering the single freshest mirror. rotate=0 keeps the
    # freshest-first pick.
    #
    # Pure (no I/O) so it is unit-testable like build_repo_mirrors_payload; the
    # caller gathers the catalog rows + host_presence map and whether the named
    # owner is currently online. We never redirect away from an online source.
    owner_l = (owner or "").strip().lower()
    repo_l = (repo or "").strip().lower()
    if not owner_l or not repo_l or source_online:
        return None
    public = []
    target = None
    for row in rows or []:
        rec = row.get("data") or {}
        if row.get("is_private") or rec.get("visibility") == "private":
            continue
        rec_owner = str(rec.get("owner") or "").strip()
        rec_name = str(rec.get("name") or "").strip()
        if not rec_owner or not rec_name:
            continue
        item = {"key_bi": row.get("key_bi"), "data": rec}
        public.append(item)
        if rec_owner.lower() == owner_l and rec_name.lower() == repo_l:
            target = item
    # When the source itself never published a record we can still group by name so
    # an offline-but-unpublished source can fall back to a name-matching mirror.
    target_data = target["data"] if target else {
        "owner": owner, "name": repo, "rootCommit": ""}
    candidates = []
    for item in public:
        rec = item["data"]
        rec_owner = str(rec.get("owner") or "").strip()
        if not rec_owner or rec_owner.lower() == owner_l:
            continue  # never redirect to the (offline) source owner itself
        if not repo_mirror_same_group(target_data, rec):
            continue
        seen = _mirror_ms(presence.get(item.get("key_bi")))
        if not (seen and now - seen <= stale_ms):
            continue  # only redirect to a mirror that is actually online
        sync = _mirror_ms(rec.get("lastSync")) or 0
        candidates.append((sync, seen, rec_owner))
    if not candidates:
        return None
    # Freshest-synced first, then most-recently-seen, then name for a stable order.
    candidates.sort(key=lambda c: (-c[0], -c[1], c[2].lower()))
    # Round-robin across the eligible online mirrors so the load of serving a
    # downed repo is spread over all of them rather than landing on one mirror.
    return candidates[rotate % len(candidates)][2]


def browse_mirror_candidates(owner, repo, rows, presence, now, stale_ms):
    # Ordered list of node owners that can serve a website browse of owner/repo
    # right now: every ONLINE mirror of the same logical repo, INCLUDING the named
    # source itself. Freshest-synced first, then most-recently-seen, then name, so
    # the round-robin order is stable. Unlike select_clone_fallback (which only
    # diverts when the named host is offline and never lists the source), this
    # spreads normal page loads across all live mirrors so the website can rotate
    # over them and show which node served each request. Returns [] when nothing is
    # online. Pure (no I/O) so it is unit-testable like build_repo_mirrors_payload.
    owner_l = (owner or "").strip().lower()
    repo_l = (repo or "").strip().lower()
    if not owner_l or not repo_l:
        return []
    public = []
    target = None
    for row in rows or []:
        rec = row.get("data") or {}
        if row.get("is_private") or rec.get("visibility") == "private":
            continue
        rec_owner = str(rec.get("owner") or "").strip()
        rec_name = str(rec.get("name") or "").strip()
        if not rec_owner or not rec_name:
            continue
        item = {"key_bi": row.get("key_bi"), "data": rec}
        public.append(item)
        if rec_owner.lower() == owner_l and rec_name.lower() == repo_l:
            target = item
    # When the source itself never published a record we can still group by name.
    target_data = target["data"] if target else {
        "owner": owner, "name": repo, "rootCommit": ""}
    candidates = []
    for item in public:
        rec = item["data"]
        rec_owner = str(rec.get("owner") or "").strip()
        if not rec_owner:
            continue
        if not repo_mirror_same_group(target_data, rec):
            continue
        seen = _mirror_ms(presence.get(item.get("key_bi")))
        if not (seen and now - seen <= stale_ms):
            continue  # only serve from a mirror that is actually online
        sync = _mirror_ms(rec.get("lastSync")) or 0
        candidates.append((sync, seen, rec_owner))
    candidates.sort(key=lambda c: (-c[0], -c[1], c[2].lower()))
    ordered = []
    seen_owners = set()
    for _sync, _seen, name in candidates:
        low = name.lower()
        if low in seen_owners:
            continue  # one live node serves a logical repo once
        seen_owners.add(low)
        ordered.append(name)
    return ordered


# How many recent owner-attested state pins are kept (and accepted) per repo.
# The window is the availability/rollback trade: a mirror may lag the source by
# up to this many publishes and still clone, while a rollback older than the
# window is rejected.
STATE_PIN_HISTORY = 10


def clone_state_pins(target, target_key, rows, history=None):
    # The set of repo-state hashes (sha256 over the canonical heads+tags
    # advertisement) the relay accepts from the node serving a clone of the
    # `target` catalog record, or None when the repo is unpinned (fail-open, e.g.
    # a never-attested legacy repo). `rows` are decrypted catalog records
    # [{"key_bi", "data"}]; `history` maps key_bi -> recent attested hashes from
    # repo_state_history.
    #
    # A working-copy holder ("local-node" — the source of truth for its
    # namespace) is validated against ITS OWN attestations: current pin plus
    # recent history. Self-attestation is fine there — the node holds the signing
    # key either way, so the pin is a consistency check, and the history absorbs
    # the push-to-republish lag.
    #
    # A MIRROR ("remote-clone") must serve a state some working-copy holder in
    # its logical-repo group (root commit, name fallback — see
    # repo_mirror_same_group) actually attested. Its own self-signed pin proves
    # nothing: a tampered mirror can always republish a hash matching its forged
    # refs. Mirrors sync with `fetch --prune refs/heads/* refs/tags/*`, so a
    # faithful mirror's full ref set — and therefore its hash — equals the
    # source's. Only when NO group source has ever attested (legacy clients)
    # does the mirror's own pin still apply, preserving the old behaviour.
    #
    # Residual limits, by design: a registered account that publishes a
    # local-node record grouped with the repo can inject acceptable pins (raising
    # the bar from "compromise one mirror" to "register a visible fake source"),
    # and a mirror-run agent's local branches make its ref set diverge from the
    # source until the next pruning sync (~5 min) — clones of that mirror fail
    # the check for that window.
    if not isinstance(target, dict) or not target:
        return None

    def _clean(value):
        return str(value or "").strip().lower()

    hist = history or {}

    def _pins_for(key, rec):
        pins = set()
        if cur := _clean(rec.get("stateHash")):
            pins.add(cur)
        for h in hist.get(str(key or ""), []) or []:
            if h := _clean(h):
                pins.add(h)
        return pins

    # Records published before the field existed default to "local-node" (the
    # same default safe_catalog_record applies on write).
    if _clean(target.get("source") or "local-node") == "local-node":
        return _pins_for(target_key, target) or None

    source_pins = set()
    for row in rows or []:
        rec = (row or {}).get("data") or {}
        if _clean(rec.get("source") or "local-node") != "local-node":
            continue
        if not repo_mirror_same_group(target, rec):
            continue
        source_pins |= _pins_for(row.get("key_bi"), rec)
    if source_pins:
        return source_pins
    return _pins_for(target_key, target) or None
