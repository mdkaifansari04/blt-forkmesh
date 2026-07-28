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
    # mirror-nodes list. If both roots are present but differ, trust a ForkMesh
    # clone URL that points at the other row's owner/name: older mirror records
    # could publish the wrong root while still clearly being clones of the source
    # repo, and those must still show up and serve as live mirrors. Otherwise two
    # differing non-empty roots mean a genuine fork with rewritten history, which
    # stays in its own group.
    def owner_name(rec):
        return (
            str((rec or {}).get("owner") or "").strip().lower(),
            str((rec or {}).get("name") or "").strip().lower(),
        )

    def clone_target(rec):
        raw = str((rec or {}).get("cloneUrl") or "").strip()
        if not raw:
            return "", ""
        path = raw
        if "://" in path:
            path = path.split("://", 1)[1]
            path = path.split("/", 1)[1] if "/" in path else ""
        elif path.startswith("git@") and ":" in path:
            path = path.split(":", 1)[1]
        path = path.split("?", 1)[0].split("#", 1)[0].strip("/")
        parts = [p for p in path.split("/") if p]
        if len(parts) < 2:
            return "", ""
        owner = parts[-2].strip().lower()
        name = parts[-1].strip().lower()
        if name.endswith(".git"):
            name = name[:-4]
        return owner, name

    def clone_points_to(src, dst):
        target_owner, target_name = clone_target(src)
        dst_owner, dst_name = owner_name(dst)
        return bool(
            target_owner and target_name and dst_owner and dst_name and
            target_owner == dst_owner and target_name == dst_name
        )

    troot = str((target or {}).get("rootCommit") or "").strip().lower()
    rroot = str((record or {}).get("rootCommit") or "").strip().lower()
    if troot and rroot:
        return (
            troot == rroot or
            clone_points_to(record, target) or
            clone_points_to(target, record)
        )
    tname = str((target or {}).get("name") or "").strip().lower()
    rname = str((record or {}).get("name") or "").strip().lower()
    return bool(tname) and tname == rname


def agent_provider_mirror_candidates(
    records, target, source_node, provider, now, fresh_ms,
):
    """Return fresh same-repo headless nodes that signed the required runtime.

    Agent jobs are claimed over the polling API, so a direct HTTPS clone
    endpoint is neither required nor sufficient. The catalog-v2 record is the
    account-signed capability lease; the node still performs a local binary
    and login check before it executes a claimed job.
    """
    provider = str(provider or "").strip().lower()
    if provider not in {"claude-code", "codex"}:
        return []
    source = str(source_node or "").strip().lower()
    try:
        now_ms = int(now)
        lease_ms = max(1, int(fresh_ms))
    except (TypeError, ValueError):
        return []
    eligible = []
    seen_nodes = set()
    for rec in records or []:
        rec = rec if isinstance(rec, dict) else {}
        if rec.get("visibility") == "private":
            continue
        if not repo_mirror_same_group(target, rec):
            continue
        node = (
            str(rec.get("machineName") or "").strip()
            or str(rec.get("owner") or "").strip()
        ).lower()
        if not node or node == source or node in seen_nodes:
            continue
        providers = rec.get("agentProviders")
        if not isinstance(providers, list) or provider not in {
            str(value or "").strip().lower() for value in providers
        }:
            continue
        last_sync = _mirror_ms(rec.get("lastSync"))
        if not last_sync or now_ms - last_sync > lease_ms:
            continue
        seen_nodes.add(node)
        eligible.append((last_sync, node))
    eligible.sort(key=lambda item: (-item[0], item[1]))
    return [node for _last_sync, node in eligible]


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
    history=None, linked_canonical=False, reachable_nodes=None,
):
    def clone_target(rec):
        raw = str((rec or {}).get("cloneUrl") or "").strip()
        if not raw:
            return "", ""
        path = raw
        if "://" in path:
            path = path.split("://", 1)[1]
            path = path.split("/", 1)[1] if "/" in path else ""
        elif path.startswith("git@") and ":" in path:
            path = path.split(":", 1)[1]
        path = path.split("?", 1)[0].split("#", 1)[0].strip("/")
        parts = [p for p in path.split("/") if p]
        if len(parts) < 2:
            return "", ""
        clone_owner = parts[-2].strip().lower()
        clone_name = parts[-1].strip().lower()
        if clone_name.endswith(".git"):
            clone_name = clone_name[:-4]
        return clone_owner, clone_name

    def canonical_owner_name(rec):
        source = str((rec or {}).get("source") or "").strip()
        if source == "remote-clone":
            clone_owner, clone_name = clone_target(rec)
            if clone_owner and clone_name:
                return clone_owner, clone_name
        return (
            str((rec or {}).get("owner") or "").strip().lower(),
            str((rec or {}).get("name") or "").strip().lower(),
        )

    owner_l = (owner or "").strip().lower()
    repo_l = (repo or "").strip().lower()
    public_rows = []
    target = None
    canonical_target = None
    inferred_target = False
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
        if canonical_target is None and canonical_owner_name(rec) == (owner_l, repo_l):
            canonical_target = item
    if not target:
        if not canonical_target:
            return None
        inferred_target = True
        target = {
            "key_bi": canonical_target.get("key_bi"),
            "data": {
                **canonical_target["data"],
                "owner": owner,
                "name": repo,
                "liveHost": False,
            },
        }

    group_key = repo_mirror_group_key(target["data"])
    if inferred_target:
        members = [
            r for r in public_rows
            if canonical_owner_name(r["data"]) == (owner_l, repo_l)
        ]
    else:
        members = [
            r for r in public_rows if repo_mirror_same_group(target["data"], r["data"])
        ]

    def exact_state_hash(rec):
        value = str((rec or {}).get("stateHash") or "").strip().lower()
        if (
            len(value) == 64
            and all(ch in "0123456789abcdef" for ch in value)
        ):
            return value
        return ""

    freshest_sync = 0
    for row in members:
        freshest_sync = max(freshest_sync, _mirror_ms(row["data"].get("lastSync")) or 0)
    freshest_states = {
        exact_state_hash(row["data"])
        for row in members
        if (
            (_mirror_ms(row["data"].get("lastSync")) or 0) == freshest_sync
            and exact_state_hash(row["data"])
        )
    }

    # Is the logical repo's source of truth (a working-copy holder —
    # "local-node") reachable through a fresh, healthy direct-HTTPS endpoint?
    # While it is, a mirror with stale refs can re-sync from the source, so it is
    # auto-healing rather than blocked. The hard reject still applies when no
    # healthy source endpoint is available.
    source_online = False
    for row in members:
        rec = row["data"]
        if str(rec.get("source") or "local-node") != "local-node":
            continue
        seen = _mirror_ms((presence or {}).get(row.get("key_bi")))
        if seen and now - seen <= stale_ms:
            source_online = True
            break

    # Organization aliases explicitly appoint one account-owned node as the
    # canonical backing node for /<org>/<repo>. The HTTPS router therefore
    # treats a linked remote-clone target's signed state as the organization's
    # attestation instead of letting an unrelated same-name local-node record
    # override it. Mirror status must use that same trust anchor or it can label
    # the node actively serving a verified org route "rejected".
    #
    # `linked_canonical` is supplied only after the caller verifies the
    # org_repos link in D1. It is deliberately ignored for a local-node target,
    # whose normal source-pin policy is already the stronger/correct one.
    target_record = target["data"]
    canonical_link_mode = bool(
        linked_canonical
        and str(target_record.get("source") or "").strip().lower()
        == "remote-clone"
    )
    canonical_pins = set()
    canonical_online = False
    if canonical_link_mode:
        target_state = str(target_record.get("stateHash") or "").strip().lower()
        if (
            len(target_state) == 64
            and all(ch in "0123456789abcdef" for ch in target_state)
        ):
            canonical_pins.add(target_state)
        for state in (history or {}).get(str(target.get("key_bi") or ""), []) or []:
            state = str(state or "").strip().lower()
            if (
                len(state) == 64
                and all(ch in "0123456789abcdef" for ch in state)
            ):
                canonical_pins.add(state)
        target_seen = _mirror_ms((presence or {}).get(target.get("key_bi")))
        canonical_online = bool(target_seen and now - target_seen <= stale_ms)

    def _int_field(rec, name):
        try:
            return int(rec.get(name))
        except (TypeError, ValueError):
            return -1

    def _optional_metric(rec, name, maximum):
        value = rec.get(name)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            return None
        return min(value, maximum)

    def _optional_usage(rec, used_name, total_name):
        used = _optional_metric(rec, used_name, 1 << 50)
        total = _optional_metric(rec, total_name, 1 << 50)
        if used is None or total is None or total <= 0:
            return None, None
        return min(used, total), total

    mirrors = []
    for row in members:
        rec = row["data"]
        node_name = (
            str(rec.get("machineName") or "").strip()
            or str(rec.get("owner") or "").strip()
        )
        key = row.get("key_bi")
        seen = _mirror_ms((presence or {}).get(key))
        hosted = _mirror_ms(rec.get("hostedSince")) or _mirror_ms((first_hosted or {}).get(key))
        last_sync = _mirror_ms(rec.get("lastSync"))
        # Public byte serving no longer keeps the legacy host WebSocket open.
        # A source-of-truth desktop still signs and publishes its local-node
        # catalog while it is alive, so that fresh publication is its bounded
        # liveness lease. Remote clones keep using independently challenged
        # endpoint presence and cannot self-declare online this way.
        local_publication_seen = (
            last_sync
            if str(rec.get("source") or "local-node") == "local-node"
            else None
        )
        effective_seen = seen or local_publication_seen
        effective_stale_ms = (
            max(stale_ms, 10 * 60 * 1000)
            if local_publication_seen and not seen
            else stale_ms
        )
        online = bool(
            effective_seen and now - effective_seen <= effective_stale_ms)
        if reachable_nodes is not None:
            online = online and node_name.lower() in reachable_nodes
        try:
            size_bytes = max(0, int(rec.get("sizeBytes") or 0))
        except (TypeError, ValueError):
            size_bytes = 0
        exact_current_state = exact_state_hash(rec)
        state_hash = str(rec.get("stateHash") or "").strip().lower()
        sync_timestamp_behind = bool(
            last_sync and freshest_sync and freshest_sync - last_sync > sync_tolerance_ms
        )
        # `lastSync` is a publication timestamp, not repository content. Two
        # independently renewed mirrors can publish the same signed all-refs
        # state minutes apart; the older timestamp must not label exact,
        # integrity-equivalent content as behind. Legacy records without a
        # valid state hash retain the conservative timestamp fallback.
        behind = bool(
            sync_timestamp_behind
            and not (
                exact_current_state
                and exact_current_state in freshest_states
            )
        )
        issue_count = _int_field(rec, "issueCount")
        # Clones / website serves this node has provided; -1 == not advertised
        # (older peer or a record predating the counters), shown as an em-dash.
        clones_served = _int_field(rec, "clonesServed")
        website_served = _int_field(rec, "websiteServed")
        cpu_percent = _optional_metric(rec, "cpuPercent", 100)
        mem_used, mem_total = _optional_usage(
            rec, "memUsedBytes", "memTotalBytes")
        disk_used, disk_total = _optional_usage(
            rec, "diskUsedBytes", "diskTotalBytes")
        actions_enabled = rec.get("actionsEnabled") is True
        actions_state = str(rec.get("actionsState") or "").strip()
        if (
            (not actions_enabled and actions_state != "disabled")
            or (actions_enabled and actions_state not in {"enabled", "running"})
        ):
            actions_enabled = False
            actions_state = "disabled"
        # Would the clone integrity gate serve this node right now? Its published
        # refs fingerprint (stateHash, the same one it signs on publish) must be
        # a state the group's trust anchor attested: normally a working-copy
        # holder's current/recent pin, or the explicitly appointed backing
        # node's pin for an organization alias. Otherwise every clone it serves
        # is rejected with "repository failed integrity check" (see
        # clone_state_pins). Verdicts: "ok" (matches a pin, or the legacy
        # unlinked gate is unpinned), "rejected" (matches no attested state AND
        # the anchor is offline), "healing" (matches nothing yet, but the anchor
        # is online, so it can re-sync), "unknown" (no fingerprint published).
        pins = (
            canonical_pins
            if canonical_link_mode
            else clone_state_pins(rec, key, public_rows, history)
        )
        integrity_anchor_online = (
            canonical_online if canonical_link_mode else source_online
        )
        if canonical_link_mode and not pins:
            # Unlike the legacy unlinked path, an explicit organization route
            # is fail-closed until its appointed backing node publishes an
            # authenticated refs digest.
            integrity = "unknown" if not state_hash else "rejected"
        elif not pins or state_hash in pins:
            integrity = "ok"
        elif not state_hash:
            integrity = "unknown"
        elif integrity_anchor_online:
            integrity = "healing"
        else:
            integrity = "rejected"
        if not online:
            activity = "offline"
        elif actions_state == "running":
            activity = "running-actions"
        elif behind:
            # The node is live but its last signed publication is not the
            # freshest exact refs state. This is a truthful, Worker-observable
            # "sync pending/in progress" signal without guessing which local
            # process is currently consuming CPU.
            activity = "syncing"
        elif integrity == "healing":
            activity = "verifying"
        elif integrity == "rejected":
            activity = "integrity-blocked"
        elif integrity == "unknown":
            activity = "awaiting-verification"
        else:
            activity = "serving"
        serving = online and integrity == "ok"
        mirrors.append({
            # A node is a machine, not the user/account that owns its catalog
            # row. Older publishers did not advertise machineName, so retain
            # owner only as a compatibility fallback.
            "node": node_name,
            "owner": str(rec.get("owner") or "").strip(),
            "ownerUser": str(rec.get("ownerUser") or "").strip(),
            # The publishing machine's advertised node name (may differ from
            # the owning account); display-only, never an identity key.
            "machineName": str(rec.get("machineName") or "").strip(),
            "repo": str(rec.get("name") or "").strip(),
            "status": "online" if serving else "offline",
            "lastSeen": effective_seen if online else None,
            "hostedSince": hosted,
            "syncAgeMs": max(0, now - last_sync) if last_sync else None,
            "lastSync": last_sync,
            "behind": behind,
            "cloneAvailable": serving,
            "sizeBytes": size_bytes,
            "source": str(rec.get("source") or "").strip(),
            # Node facts the publishing node mirrored into its catalog record, so the
            # Mirror nodes view fills these columns even for an offline node (adhoc #56).
            "commit": str(rec.get("commit") or "").strip(),
            "branch": str(rec.get("branch") or "").strip(),
            # What that commit says and who wrote it, mirrored from the node's
            # signed record, so a hash alone isn't all a viewer gets. Optional
            # catalog-v2 extensions: None when the node never published them.
            "lastCommitMessage": (
                str(rec.get("commitSubject") or "").strip() or None),
            "lastCommitAuthorName": (
                str(rec.get("commitAuthorName") or "").strip() or None),
            "lastCommitAt": _mirror_ms(rec.get("commitAt")),
            "issueCount": issue_count,
            "commitCount": _int_field(rec, "commitCount"),
            "branchCount": _int_field(rec, "branchCount"),
            "pullCount": _int_field(rec, "pullCount"),
            "discussionCount": _int_field(rec, "discussionCount"),
            "worktreeCount": _int_field(rec, "worktreeCount"),
            "artifactCount": _int_field(rec, "artifactCount"),
            "platform": str(rec.get("platform") or "").strip(),
            "version": str(rec.get("version") or "").strip(),
            "agentProviders": [
                provider
                for provider in ("claude-code", "codex")
                if provider in {
                    str(value or "").strip().lower()
                    for value in (
                        rec.get("agentProviders")
                        if isinstance(rec.get("agentProviders"), list)
                        else []
                    )
                }
            ],
            # Missing and malformed legacy records fail closed to disabled.
            # Only the bounded status pair signed into catalog-v2 is exposed.
            "actionsEnabled": actions_enabled,
            "actionsState": actions_state,
            "id": str(rec.get("nodeId") or "").strip(),
            "clonesServed": clones_served,
            "websiteServed": website_served,
            # Optional, operator-approved public host telemetry. These values
            # were bounded and signed as part of catalog-v2; preserve None as
            # "not shared" rather than inventing a zero for an older/opted-out
            # node.
            "cpuPercent": cpu_percent,
            "memUsedBytes": mem_used,
            "memTotalBytes": mem_total,
            "diskUsedBytes": disk_used,
            "diskTotalBytes": disk_total,
            "integrity": integrity,
            "activity": activity,
            "activityUpdatedAt": effective_seen or last_sync,
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


def release_blob_mirror_candidates(owner, repo, rows, presence, now, stale_ms):
    # Ordered list of online nodes likely to serve a release CAS blob for
    # owner/repo. This is intentionally close to browse_mirror_candidates, but
    # release bytes live outside git, so prefer nodes that have published a
    # positive artifactCount before falling back to legacy/unknown records. The
    # caller still verifies by hash and retries on 404; artifactCount is a
    # priority signal, not proof that this exact sha256 is present.
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
            continue
        sync = _mirror_ms(rec.get("lastSync")) or 0
        try:
            artifacts = int(rec.get("artifactCount") or 0)
        except (TypeError, ValueError):
            artifacts = 0
        candidates.append((artifacts > 0, sync, seen, rec_owner))
    candidates.sort(key=lambda c: (0 if c[0] else 1, -c[1], -c[2], c[3].lower()))
    ordered = []
    seen_owners = set()
    for _has_artifacts, _sync, _seen, name in candidates:
        low = name.lower()
        if low in seen_owners:
            continue
        seen_owners.add(low)
        ordered.append(name)
    return ordered


# --- Peer mirror requests (issue #385) --------------------------------------
# A repo owner can ask another node to mirror their repo ("ask node to mirror
# your repo"): the target gets a notification and, if its holder accepts, its
# node starts mirroring. The request lifecycle (pending -> accepted/rejected)
# is parked as a small bounded list on the TARGET account record (the same
# heartbeat-delivered-marker pattern as claim_pending / ownership_transfer),
# and accepted requests ride back on that node's signed heartbeat so the
# desktop node clones the repo. These helpers are pure list/dict manipulation
# (no I/O), unit-testable like the mirror-grouping functions above; entry.py
# imports them and persists the list inside the encrypted account blob.
MAX_MIRROR_REQUESTS = 50
MIRROR_REQUEST_STATUSES = ("pending", "accepted", "rejected")


def mirror_request_id(owner, repo, target):
    # Stable id so re-asking the same node to mirror the same repo updates the
    # one pending request instead of piling up duplicates.
    owner_l = str(owner or "").strip().lower()
    repo_l = str(repo or "").strip().lower()
    target_l = str(target or "").strip().lower()
    return owner_l + "/" + repo_l + "@" + target_l


def add_mirror_request(requests, entry):
    # Insert (or replace, by id) a request into the bounded, newest-first list.
    # Returns a new list; the caller assigns it back onto the account record.
    entry_id = str((entry or {}).get("id") or "").strip()
    if not entry_id:
        return list(requests or [])
    kept = [r for r in (requests or []) if str((r or {}).get("id") or "") != entry_id]
    kept.insert(0, dict(entry))
    return kept[:MAX_MIRROR_REQUESTS]


def find_mirror_request(requests, entry_id):
    entry_id = str(entry_id or "").strip()
    for r in requests or []:
        if str((r or {}).get("id") or "") == entry_id:
            return r
    return None


def set_mirror_request_status(requests, entry_id, status, ts=0):
    # Mutate the matching request's status in place (pending -> accepted/rejected).
    # Returns (new_list, updated_entry_or_None).
    entry_id = str(entry_id or "").strip()
    status = status if status in MIRROR_REQUEST_STATUSES else "pending"
    out = []
    updated = None
    for r in requests or []:
        rec = dict(r or {})
        if str(rec.get("id") or "") == entry_id:
            rec["status"] = status
            if ts:
                rec["resolvedAt"] = int(ts)
            updated = rec
        out.append(rec)
    return out, updated


def accepted_mirror_requests(requests):
    # The accepted-and-not-yet-acknowledged repos this node should mirror, as the
    # minimal {id, owner, repo} the heartbeat reply carries to the desktop node.
    out = []
    for r in requests or []:
        rec = r or {}
        if str(rec.get("status") or "") != "accepted":
            continue
        owner = str(rec.get("owner") or "").strip()
        repo = str(rec.get("repo") or "").strip()
        entry_id = str(rec.get("id") or "").strip()
        if not owner or not repo or not entry_id:
            continue
        out.append({"id": entry_id, "owner": owner, "repo": repo})
    return out


def ack_mirror_requests(requests, ids):
    # Drop the accepted requests the node has confirmed it acted on (it reports
    # their ids in its next signed heartbeat), so they stop being redelivered.
    ack = {str(i or "").strip() for i in (ids or []) if str(i or "").strip()}
    if not ack:
        return list(requests or [])
    return [
        r for r in (requests or [])
        if not (str((r or {}).get("id") or "") in ack
                and str((r or {}).get("status") or "") == "accepted")
    ]


# How many recent owner-attested state pins are kept (and accepted) per repo.
# The window is the availability/rollback trade: a mirror may lag the source by
# up to this many publishes and still serve, while a rollback older than the
# window is rejected.
#
# Every issue/PR/discussion action commits to a served branch and therefore
# republishes the catalog with a fresh state hash, so a repo with active
# collaboration churns pins fast. A 10-deep window let a mirror fall out of the
# accepted set after only a handful of issue edits between its ~5-minute
# re-syncs, at which point the relay refused to route ANY read to it and the
# web UI reported "Issues are unavailable until a live desktop host serves the
# .forkmesh/issues/ folder" even though healthy mirrors held the folder. A
# deeper window keeps those mirrors serving issues (and every other read) across
# far more source publishes. Only states the owner genuinely attested are ever
# admitted, so widening the window does not weaken the anti-tamper guarantee —
# it only accepts older-but-real rollbacks over a longer span.
STATE_PIN_HISTORY = 100


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
