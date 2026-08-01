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







    groups = set()
    for rec in records or []:
        if (rec or {}).get("liveHost") and (rec or {}).get("visibility") != "private":
            groups.add(repo_mirror_group_key(rec))
    return groups


def repo_clone_online(rec, served_groups):




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






    source_online = False
    for row in members:
        rec = row["data"]
        if str(rec.get("source") or "local-node") != "local-node":
            continue
        seen = _mirror_ms((presence or {}).get(row.get("key_bi")))
        if seen and now - seen <= stale_ms:
            source_online = True
            break











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
        agent_providers = [
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
        ]





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





        agent_runtime_online = bool(
            agent_providers
            and last_sync
            and now - last_sync <= 10 * 60 * 1000
        )
        node_online = online or agent_runtime_online
        try:
            size_bytes = max(0, int(rec.get("sizeBytes") or 0))
        except (TypeError, ValueError):
            size_bytes = 0
        exact_current_state = exact_state_hash(rec)
        state_hash = str(rec.get("stateHash") or "").strip().lower()
        sync_timestamp_behind = bool(
            last_sync and freshest_sync and freshest_sync - last_sync > sync_tolerance_ms
        )





        behind = bool(
            sync_timestamp_behind
            and not (
                exact_current_state
                and exact_current_state in freshest_states
            )
        )
        issue_count = _int_field(rec, "issueCount")


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










        pins = (
            canonical_pins
            if canonical_link_mode
            else clone_state_pins(rec, key, public_rows, history)
        )
        integrity_anchor_online = (
            canonical_online if canonical_link_mode else source_online
        )
        if canonical_link_mode and not pins:



            integrity = "unknown" if not state_hash else "rejected"
        elif not pins or state_hash in pins:
            integrity = "ok"
        elif not state_hash:
            integrity = "unknown"
        elif integrity_anchor_online:
            integrity = "healing"
        else:
            integrity = "rejected"
        if not node_online:
            activity = "offline"
        elif actions_state == "running":
            activity = "running-actions"
        elif behind:




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



            "node": node_name,
            "owner": str(rec.get("owner") or "").strip(),
            "ownerUser": str(rec.get("ownerUser") or "").strip(),


            "machineName": str(rec.get("machineName") or "").strip(),
            "repo": str(rec.get("name") or "").strip(),
            "status": "online" if node_online else "offline",
            "lastSeen": (
                effective_seen
                if online
                else last_sync if agent_runtime_online else None
            ),
            "hostedSince": hosted,
            "syncAgeMs": max(0, now - last_sync) if last_sync else None,
            "lastSync": last_sync,
            "behind": behind,
            "cloneAvailable": serving,
            "sizeBytes": size_bytes,
            "source": str(rec.get("source") or "").strip(),


            "commit": str(rec.get("commit") or "").strip(),
            "branch": str(rec.get("branch") or "").strip(),



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
            "runtimeMode": (
                str(rec.get("runtimeMode") or "").strip().lower()
                if str(rec.get("runtimeMode") or "").strip().lower()
                in {"desktop", "headless"}
                else ""
            ),
            "version": str(rec.get("version") or "").strip(),
            "agentProviders": agent_providers,


            "actionsEnabled": actions_enabled,
            "actionsState": actions_state,
            "id": str(rec.get("nodeId") or "").strip(),
            "clonesServed": clones_served,
            "websiteServed": website_served,




            "cloneServedAt": _mirror_ms(rec.get("cloneServedAt")),
            "cloneServedAgent": (
                str(rec.get("cloneServedAgent") or "").strip() or None),
            "websiteServedAt": _mirror_ms(rec.get("websiteServedAt")),
            "websiteServedAgent": (
                str(rec.get("websiteServedAgent") or "").strip() or None),




            "cpuPercent": cpu_percent,
            "memUsedBytes": mem_used,
            "memTotalBytes": mem_total,
            "diskUsedBytes": disk_used,
            "diskTotalBytes": disk_total,
            "integrity": integrity,
            "activity": activity,
            "activityUpdatedAt": (
                effective_seen if online else last_sync
            ),
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


    target_data = target["data"] if target else {
        "owner": owner, "name": repo, "rootCommit": ""}
    candidates = []
    for item in public:
        rec = item["data"]
        rec_owner = str(rec.get("owner") or "").strip()
        if not rec_owner or rec_owner.lower() == owner_l:
            continue
        if not repo_mirror_same_group(target_data, rec):
            continue
        seen = _mirror_ms(presence.get(item.get("key_bi")))
        if not (seen and now - seen <= stale_ms):
            continue
        sync = _mirror_ms(rec.get("lastSync")) or 0
        candidates.append((sync, seen, rec_owner))
    if not candidates:
        return None

    candidates.sort(key=lambda c: (-c[0], -c[1], c[2].lower()))


    return candidates[rotate % len(candidates)][2]


def browse_mirror_candidates(owner, repo, rows, presence, now, stale_ms):








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
        candidates.append((sync, seen, rec_owner))
    candidates.sort(key=lambda c: (-c[0], -c[1], c[2].lower()))
    ordered = []
    seen_owners = set()
    for _sync, _seen, name in candidates:
        low = name.lower()
        if low in seen_owners:
            continue
        seen_owners.add(low)
        ordered.append(name)
    return ordered


def release_blob_mirror_candidates(owner, repo, rows, presence, now, stale_ms):






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












MAX_MIRROR_REQUESTS = 50
MIRROR_REQUEST_STATUSES = ("pending", "accepted", "rejected")


def mirror_request_id(owner, repo, target):


    owner_l = str(owner or "").strip().lower()
    repo_l = str(repo or "").strip().lower()
    target_l = str(target or "").strip().lower()
    return owner_l + "/" + repo_l + "@" + target_l


def add_mirror_request(requests, entry):


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


    ack = {str(i or "").strip() for i in (ids or []) if str(i or "").strip()}
    if not ack:
        return list(requests or [])
    return [
        r for r in (requests or [])
        if not (str((r or {}).get("id") or "") in ack
                and str((r or {}).get("status") or "") == "accepted")
    ]


















STATE_PIN_HISTORY = 100


def clone_state_pins(target, target_key, rows, history=None):




























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
