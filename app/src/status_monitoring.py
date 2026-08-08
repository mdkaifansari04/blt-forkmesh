"""On-demand /status page sampler and history projection.

Cloudflare validates a Python Worker by compiling and executing its
entrypoint under the isolate memory limit.  The minute sampler runs from the
Cron Trigger / ForkMeshCronRunner alarm and the history projection only
serves the /status page, so entry.py loads this module on first use instead
of spending scarce Pyodide startup memory on every isolate.
"""


def _bind_runtime(runtime):
    """Supply the entrypoint primitives used by the status implementation."""
    namespace = globals()
    for name, value in runtime.items():
        if not name.startswith("__") and name not in namespace:
            namespace[name] = value


async def _record_status_monitor_transitions(
        env, ok, reason, now, status_systems=None):
    """Deduplicate outage/recovery mail for every system shown on /status."""
    status_systems = status_systems or STATUS_SYSTEMS
    rows = await d1_all(
        env,
        "SELECT monitor_id,is_up,changed_at,outage_started_at,notified_state,"
        "pinged_state,reason FROM repository_monitor_state "
        "WHERE monitor_id LIKE 'status:%'",
    )
    prior = {str(row.get("monitor_id") or ""): row for row in (rows or [])}
    alert_owner, _, alert_repo = FLAGSHIP_MONITOR_ID.partition("/")
    alert_settings = await _repo_alert_settings_get(
        env, alert_owner, alert_repo)
    pending = []
    ping_pending = []
    values = []
    for system_id, label in status_systems:
        monitor_id = "status:" + system_id
        is_up = bool(ok.get(system_id, True))
        state = "up" if is_up else "down"
        row = prior.get(monitor_id)
        previous_up = bool(row.get("is_up")) if row else True
        previous_changed_at = (
            int(row.get("changed_at") or now) if row else int(now))
        prior_outage = (
            int(row.get("outage_started_at") or 0) if row else 0)
        prior_notified = (
            str(row.get("notified_state") or "") if row else "")
        prior_pinged = (
            str(row.get("pinged_state") or "") if row else "")
        changed = not row or previous_up != is_up
        changed_at = int(now) if changed else previous_changed_at
        outage_started_at = (
            0 if is_up else int(now) if not row or previous_up
            else prior_outage or previous_changed_at)
        notified = (
            ("up" if is_up else "") if not row else
            "" if previous_up != is_up else
            prior_notified)
        pinged = (
            ("up" if is_up else "") if not row else
            "" if previous_up != is_up else
            prior_pinged)
        should_notify = notified != state
        should_ping = pinged != state
        continual_ping = (
            not is_up
            and _status_monitor_alert_setting(
                alert_settings, system_id, "continual")
            and int(now) // STATUS_ALERT_CONTINUAL_INTERVAL_MS !=
                (int(now) - STATUS_SAMPLE_WINDOW_MS) //
                STATUS_ALERT_CONTINUAL_INTERVAL_MS
        )
        should_ping = should_ping or continual_ping
        if system_id == "flagship_repository" and is_up and row and not previous_up:
            # A recovery is only useful on a channel that announced the
            # outage. Deployment handoffs are already made green before they
            # reach this transition recorder, so do not apply a second
            # five-minute grace here: it hid genuine short repository outages
            # and their Pings.
            if prior_notified != "down":
                notified = "up"
                should_notify = False
            if prior_pinged != "down":
                pinged = "up"
                should_ping = False
        if should_notify or should_ping:
            alert = {
                "system": system_id, "label": label, "state": state,
                "is_up": is_up, "reason": clean_string(
                    reason.get(system_id, "") or "", 240),
                "outage_started_at": outage_started_at,
                "previous_changed_at": previous_changed_at,
                "changed_at": changed_at,
                # The last failure this system recorded. On a recovery the
                # current sample has no reason of its own, so this is the only
                # description of what actually went wrong.
                "prior_reason": clean_string(
                    (str(row.get("reason") or "") if row else ""), 240),
                # The outage this sample closed, on the flip itself and never
                # on a later re-send of an undelivered recovery.
                "recovered_outage_started_at": (
                    (prior_outage or previous_changed_at)
                    if is_up and row and not previous_up else 0),
                # Set only when a sampled outage closes without its own ping
                # having been delivered (a failed enqueue during the incident,
                # or Pings switched on mid-outage).
                "missed_outage_started_at": (
                    prior_outage
                    if is_up and prior_pinged != "down" and prior_outage
                    else 0),
                "continual": bool(continual_ping),
            }
            if should_notify:
                pending.append(alert)
            if should_ping:
                ping_pending.append(alert)
        values.extend([
            monitor_id, 1 if is_up else 0, changed_at, outage_started_at,
            int(now), clean_string(reason.get(system_id, "") or "", 240),
            notified, pinged,
        ])

    # Eight parameters per system crosses D1's ~100-bound-parameter ceiling as
    # soon as the public fleet grows beyond twelve rows. The status samples are
    # intentionally persisted before this follow-up, so that failure looked
    # like a healthy sampler with mysteriously absent outage pings. Keep each
    # transition upsert to ten systems / eighty parameters.
    for offset in range(0, len(values), 80):
        batch = values[offset:offset + 80]
        count = len(batch) // 8
        await d1_run(
            env,
            "INSERT INTO repository_monitor_state "
            "(monitor_id,is_up,changed_at,outage_started_at,checked_at,reason,"
            "notified_state,pinged_state) VALUES "
            + ", ".join(["(?,?,?,?,?,?,?,?)"] * count) + " "
            "ON CONFLICT(monitor_id) DO UPDATE SET "
            "is_up=excluded.is_up,changed_at=excluded.changed_at,"
            "outage_started_at=excluded.outage_started_at,"
            "checked_at=excluded.checked_at,reason=excluded.reason,"
            "notified_state=excluded.notified_state,"
            "pinged_state=excluded.pinged_state",
            *batch,
        )
    if ping_pending:
        try:
            if await _enqueue_operational_alert_pings(env, ping_pending, now):
                # Same rule as the mail path: only a delivered transition is
                # recorded, so a deployment with Pings switched off still gets
                # one alert for whatever is red when it switches them on.
                await _record_operational_alert_pings_sent(env, ping_pending)
        except BaseException:
            # Pings are a secondary delivery channel and must never block status
            # sampling or the independently configured email path.
            pass
    if not pending:
        return
    # notified_state is deliberately left untouched while alert mail is off:
    # whatever is red when an admin turns it on gets one email then, instead
    # of the switch silently swallowing the transition that is still current.
    pending = [
        alert for alert in pending
        if _status_monitor_alert_setting(
            alert_settings, alert.get("system", ""), "emails")
    ]
    if not pending:
        return
    recipients = await _repository_monitor_admin_emails(env)
    if not recipients:
        return
    attention_log_tail = None
    if any(not alert["is_up"] for alert in pending):
        attention_log_tail = await _cloudflare_attention_log_tail(env, now)
    for alert in pending:
        system_id = alert["system"]
        label = alert["label"]
        if system_id.startswith(STATUS_MIRROR_PREFIX):
            mirror_name = system_id[len(STATUS_MIRROR_PREFIX):]
            where, fix = (
                "the signed direct-HTTPS health record for " + mirror_name,
                "Check the node process and tunnel, then confirm its signed "
                "health proof is fresh, integrity is ok, and forkmesh/forkmesh "
                "matches an accepted source revision.",
            )
        else:
            where, fix = STATUS_MONITOR_GUIDANCE.get(
                system_id,
                ("Cloudflare Worker logs", "Inspect the failing check."))
        if alert["is_up"]:
            duration = _flagship_monitor_duration(
                int(now) - (
                    alert["outage_started_at"] or
                    alert["previous_changed_at"]))
            subject = "[ForkMesh recovered] " + label + " is green"
            lead = label + " is passing again after " + duration + "."
            text = (
                lead + "\n\nThe current /status probe is green.\n\n"
                "Status: https://forkmesh.com/status")
            heading = label + " recovered"
            body = (
                "<div class=\"fm-item\" style=\"border:1px solid #d4d4d8;"
                "border-radius:8px;background:#fafafa;padding:14px;"
                "margin:0 0 18px\"><p class=\"fm-item-title\" style=\"margin:0;"
                "color:#18181b;font-size:14px;font-weight:800\">Outage "
                "duration</p><p class=\"fm-item-body\" style=\"margin:6px 0 0;"
                "color:#3f3f46;font-size:14px\">" +
                _html_escape(duration) + "</p></div>")
        else:
            observed = alert["reason"] or "The current health check failed."
            subject = "[ForkMesh outage] " + label + " is not green"
            lead = label + " failed its /status health check."
            text = (
                lead + "\n\nObserved: " + observed + "\n\nLook here: " +
                where + "\n\nSuggested first step: " + fix +
                "\n\nA recovery email will be sent when this check is green "
                "again.\n\nStatus: https://forkmesh.com/status")
            heading = label + " needs attention"
            body = (
                "<div class=\"fm-item\" style=\"border:1px solid #d4d4d8;"
                "border-radius:8px;background:#fafafa;padding:14px;"
                "margin:0 0 12px\"><p class=\"fm-item-title\" style=\"margin:0;"
                "color:#18181b;font-size:14px;font-weight:800\">Observed</p>"
                "<p class=\"fm-item-body\" style=\"margin:6px 0 0;color:#3f3f46;"
                "font-size:14px\">" + _html_escape(observed) + "</p></div>"
                "<p class=\"fm-text\" style=\"margin:0 0 8px;color:#3f3f46;"
                "font-size:14px\"><strong>Look here:</strong> " +
                _html_escape(where) + "</p><p class=\"fm-text\" style=\"margin:"
                "0 0 18px;color:#3f3f46;font-size:14px\"><strong>Suggested "
                "first step:</strong> " + _html_escape(fix) + "</p>")
        html = _forkmesh_email_card_html(
            _html_escape(heading), _html_escape(lead), body,
            "<p class=\"fm-muted\" style=\"margin:20px 0 0;color:#71717a;"
            "font-size:12px\"><a class=\"fm-link\" style=\"color:#15803d\" "
            "href=\"https://forkmesh.com/status\">Open ForkMesh status</a>"
            "</p>")
        if not alert["is_up"]:
            text, html = _attention_email_with_logs(
                text, html, attention_log_tail)
        text, html = _email_with_status_alert_manage_link(
            env, text, html, system_id)
        delivered = True
        for email in recipients:
            delivered = bool(await _send_email(
                env, email, subject, text, html)) and delivered
        if delivered:
            await d1_run(
                env,
                "UPDATE repository_monitor_state SET notified_state=? "
                "WHERE monitor_id=? AND is_up=?",
                alert["state"], "status:" + system_id,
                1 if alert["is_up"] else 0,
            )


async def record_status_sample(env, source="trigger"):
    # Called once a minute by the platform Cron Trigger (scheduled()) and by
    # the ForkMeshCronRunner alarm batch; _claim_status_sample_minute lets
    # exactly one of them record each minute, and tags the claim with the
    # caller so the trigger can tell whether the runner is still landing
    # samples (see _runner_status_sample_is_stale). Best-effort per system
    # so one failing check can't blank the rest of the page.
    await ensure_schema(env)
    now = int(Date.now())
    if not await _claim_status_sample_minute(env, now, source):
        return
    if await _status_deploy_semaphore_active(env, now):
        await _record_status_deploy_sample(env, now)
        return
    day_ts = (now // 86400000) * 86400000
    hour_ts = (now // 3600000) * 3600000
    minute_ts = (now // 60000) * 60000
    ok = {}
    reason = {}

    try:
        website_ok, website_reason = await _homepage_status_probe(env)
        ok["website"] = website_ok
        if not website_ok:
            reason["website"] = website_reason
    except Exception as exc:
        ok["website"] = False
        reason["website"] = "Homepage probe failed: " + str(exc)[:160]

    try:
        status_page_ok, status_page_reason = await _status_page_api_probe(env)
        ok["status_page"] = status_page_ok
        if not status_page_ok:
            reason["status_page"] = status_page_reason
    except Exception as exc:
        ok["status_page"] = False
        reason["status_page"] = "Status API probe failed: " + str(exc)[:160]

    try:
        await d1_first(env, "SELECT 1 AS ok")
        ok["database"] = True
    except Exception as exc:
        ok["database"] = False
        reason["database"] = "Database query failed: " + str(exc)[:160]

    try:
        email_ok, email_reason = await _email_delivery_status(env, now)
        ok["email"] = email_ok
        if not email_ok:
            reason["email"] = email_reason
    except Exception as exc:
        ok["email"] = False
        reason["email"] = (
            "Email delivery health query failed: " + str(exc)[:160])

    try:
        repository_ok, repository_reason = await _flagship_repository_probe(env)
        if (
            not repository_ok
            and _deployment_status_grace_active(env, now)
        ):
            repository_ok = True
            repository_reason = ""
        ok["flagship_repository"] = repository_ok
        if not repository_ok:
            reason["flagship_repository"] = repository_reason
    except Exception as exc:
        if _deployment_status_grace_active(env, now):
            ok["flagship_repository"] = True
        else:
            ok["flagship_repository"] = False
            reason["flagship_repository"] = (
                "Repository availability probe failed: " + str(exc)[:160])

    try:
        installer_ok, installer_reason = await _installer_delivery_status(
            env, now)
        ok["installer"] = installer_ok
        if not installer_ok:
            reason["installer"] = installer_reason
    except Exception as exc:
        ok["installer"] = False
        reason["installer"] = (
            "Installer delivery probe failed: " + str(exc)[:160])

    try:
        cutoff = now - HTTPS_MIRROR_STATUS_FRESH_MS
        row = await d1_first(
            env,
            "SELECT COUNT(*) AS n FROM mirror_https_endpoints "
            "WHERE checked_at>=? AND forkmesh_verified_at>=? "
            "AND healthy=1 AND forkmesh_active=1 "
            "AND integrity='ok' AND abuse_blocked=0",
            cutoff, cutoff,
        )
        ok["git_hosting"] = int((row or {}).get("n", 0) or 0) > 0
        if not ok["git_hosting"]:
            reason["git_hosting"] = (
                "No healthy direct HTTPS mirror has supplied a valid ForkMesh "
                "repository proof within the last 10 minutes")
    except Exception as exc:
        ok["git_hosting"] = False
        reason["git_hosting"] = (
            "Direct HTTPS mirror health query failed: " + str(exc)[:160])

    try:
        rows = await d1_all(
            env, "SELECT path, status, message FROM error_log WHERE ts >= ?",
            now - STATUS_SAMPLE_WINDOW_MS,
        )
        buckets = (
            "website", "api", "errors", "realtime", "durable_objects")
        failed = {bucket: False for bucket in buckets}
        first_hit = {bucket: None for bucket in buckets}
        hit_count = {bucket: 0 for bucket in buckets}
        for row in rows:
            path = str(row.get("path") or "")
            message = str(row.get("message") or "")
            try:
                row_status = int(row.get("status") or 0)
            except (TypeError, ValueError):
                row_status = 0
            # A DO duration-cap abort is a real room failure, so it still
            # counts toward "realtime" below — but it also gets its own
            # bucket so free-tier plan-limit aborts have a dedicated, visible
            # history instead of being buried among other realtime incidents.
            # Matches legacy tagged 503 rows and raw AbortError tracebacks from
            # before expected aborts moved to their minute aggregate (same
            # substring the doDurationAborts24h snapshot below keys on).
            if "Exceeded allowed duration" in message:
                failed["durable_objects"] = True
                hit_count["durable_objects"] += 1
                if first_hit["durable_objects"] is None:
                    first_hit["durable_objects"] = (row.get("status"), path, message.strip())
            # 502/503/504 on a direct-mirror content path (release blob, repo
            # browse, git clone) means the selected endpoint is unreachable —
            # upstream availability, which the signed HTTPS health check
            # (the git_hosting signal) already tracks. It is not evidence the
            # Worker/API is down, and a single offline node's blob being
            # re-requested every few minutes used to paint the whole "api"
            # system red on /status. A 500 on the same path still counts.
            if row_status in (502, 503, 504) and (
                    RELEASE_BLOB_RE.match(path) or REPO_HOST_RE.match(path) or
                    GIT_INFO_RE.match(path) or GIT_PACK_RE.match(path)):
                continue
            failed["errors"] = True
            hit_count["errors"] += 1
            if first_hit["errors"] is None:
                first_hit["errors"] = (
                    row.get("status"), path, message.strip())
            if (path.rstrip("/") == "/api/world/ws" or
                    ROOM_RE.match(path) or REPO_ROOM_RE.match(path) or
                    GIT_INFO_RE.match(path) or GIT_PACK_RE.match(path)):
                bucket = "realtime"
            elif path.startswith("/api/"):
                bucket = "api"
            else:
                bucket = "website"
            failed[bucket] = True
            hit_count[bucket] += 1
            if first_hit[bucket] is None:
                first_hit[bucket] = (row.get("status"), path, message.strip())

        # Expected platform aborts are intentionally kept out of error_log:
        # every reconnecting room client can see the same DO failure, and one
        # actionable error row per client obscures real Worker bugs. Preserve
        # the operational signal with one content-free aggregate row per
        # minute. Sample the completed preceding minute so an abort that lands
        # just before this cron tick is neither missed nor counted again by the
        # next tick.
        abort_rows = await d1_all(
            env,
            "SELECT aborts,duration_aborts "
            "FROM durable_object_abort_minute WHERE minute_ts=?",
            minute_ts - STATUS_SAMPLE_WINDOW_MS,
        )
        abort_count = sum(
            max(0, int(row.get("aborts") or 0)) for row in abort_rows)
        duration_abort_count = sum(
            max(0, int(row.get("duration_aborts") or 0))
            for row in abort_rows)
        if abort_count:
            summary = "%d Durable Object request%s aborted" % (
                abort_count, "" if abort_count == 1 else "s")
            if duration_abort_count:
                summary += " (%d exceeded allowed duration)" % (
                    duration_abort_count)
            for bucket in ("durable_objects", "realtime"):
                failed[bucket] = True
                # The summary already carries the aggregate event count; this
                # is one status signal, not N separate error rows.
                hit_count[bucket] += 1
                if first_hit[bucket] is None:
                    first_hit[bucket] = (
                        503, "/durable-object-rooms", summary)
        ok["website"] = bool(ok.get("website", False)) and not failed["website"]
        ok["api"] = not failed["api"]
        ok["errors"] = not failed["errors"]
        ok["realtime"] = not failed["realtime"]
        ok["durable_objects"] = not failed["durable_objects"]
        for bucket in buckets:
            if failed[bucket] and first_hit[bucket]:
                status_code, path, message = first_hit[bucket]
                text = (str(status_code) + " on " + path) if status_code else path
                if message:
                    text += ": " + message[:120]
                if hit_count[bucket] > 1:
                    text += " (+%d more)" % (hit_count[bucket] - 1)
                if bucket != "website" or ok["website"] or not reason.get("website"):
                    reason[bucket] = text
    except Exception:
        # A query hiccup here is not itself evidence of an outage — don't
        # fabricate a false incident from it.
        ok["api"] = ok["errors"] = True
        ok["realtime"] = ok["durable_objects"] = True

    # Every registered mirror* node gets its own /status row, including nodes
    # that have never managed to publish a valid endpoint proof. The roster is
    # the union of account-bound node registrations and signed direct-HTTPS
    # endpoint registrations; chat/user presence can never invent a row.
    # Missing, stale, unhealthy, and unverified endpoints stay visible as red.
    status_systems = list(STATUS_SYSTEMS)
    try:
        mirror_rows = await d1_all(
            env,
            """WITH mirror_signals AS (
                   SELECT lower(name) AS node_name,last_seen AS seen_at
                     FROM nodes
                    WHERE name IS NOT NULL AND lower(name) LIKE 'mirror%'
                   UNION ALL
                   SELECT lower(node_name) AS node_name,checked_at AS seen_at
                     FROM mirror_https_endpoints
                    WHERE lower(node_name) LIKE 'mirror%'
               ), mirror_names AS (
                   SELECT node_name,MAX(seen_at) AS last_seen
                     FROM mirror_signals
                    GROUP BY node_name
               )
               SELECT names.node_name,names.last_seen,
                      endpoint.checked_at,endpoint.healthy,
                      endpoint.integrity,endpoint.abuse_blocked,
                      endpoint.forkmesh_active,endpoint.forkmesh_verified_at
                 FROM mirror_names names
                 LEFT JOIN mirror_https_endpoints endpoint
                   ON lower(endpoint.node_name)=names.node_name
                WHERE names.last_seen>=?
                ORDER BY names.node_name LIMIT ?""",
            now - STATUS_MIRROR_ROSTER_FRESH_MS, STATUS_MIRROR_MAX,
        )
        seen_mirrors = set()
        for row in mirror_rows:
            mirror_name = str(row.get("node_name") or "").strip().lower()
            if (
                mirror_name in seen_mirrors
                or mirror_name in STATUS_RETIRED_MIRRORS
                or not mirror_name.startswith("mirror")
                or not re.fullmatch(
                    r"[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?", mirror_name)
            ):
                continue
            seen_mirrors.add(mirror_name)
            system_id = STATUS_MIRROR_PREFIX + mirror_name
            status_systems.append(
                (system_id, "Mirror node — " + mirror_name))
            checked_at = int(row.get("checked_at") or 0)
            verified_at = int(row.get("forkmesh_verified_at") or 0)
            is_fresh = (
                checked_at >= now - HTTPS_MIRROR_STATUS_FRESH_MS
                and verified_at >= now - HTTPS_MIRROR_STATUS_FRESH_MS)
            is_healthy = int(row.get("healthy") or 0) == 1
            integrity_ok = str(row.get("integrity") or "") == "ok"
            is_active = int(row.get("forkmesh_active") or 0) == 1
            is_allowed = int(row.get("abuse_blocked") or 0) == 0
            mirror_ok = (
                is_fresh and is_healthy and integrity_ok and is_active
                and is_allowed)
            ok[system_id] = mirror_ok
            if not mirror_ok:
                if not is_fresh:
                    reason[system_id] = (
                        mirror_name + " has not supplied a fresh signed "
                        "ForkMesh repository proof within the last 10 minutes")
                elif not is_healthy:
                    reason[system_id] = (
                        mirror_name + " failed its signed HTTPS health check")
                elif not integrity_ok:
                    reason[system_id] = (
                        mirror_name + " reported repository integrity " +
                        (str(row.get("integrity") or "unknown")))
                elif not is_allowed:
                    reason[system_id] = (
                        mirror_name + " is blocked from serving traffic")
                else:
                    reason[system_id] = (
                        mirror_name + " is reachable but is not serving an "
                        "accepted forkmesh/forkmesh source revision")
    except Exception as exc:
        # The aggregate Git-hosting row already reports a registry query
        # failure. Do not fabricate per-node identities when the authoritative
        # registry itself could not be read. Workers Logs stays off, so the
        # failure goes straight to Sentry (best-effort — never let telemetry
        # sink the sample; an error_log row here would also turn a registry
        # hiccup into a red "errors" minute).
        try:
            await capture_sentry_error(
                env, 500, "scheduled", "/cron/record-status-sample",
                "status mirror registry query failed: "
                + _safe_error_text(exc), error=exc)
        except BaseException:
            pass

    # One multi-row upsert per table (3 statements total) instead of the old
    # 3-statements-per-system loop (15): the per-minute cron runs in a Pyodide
    # worker where every awaited D1 round trip counts against tight per-
    # invocation limits — blowing them killed the whole tick and the /status
    # page recorded the missing sample as downtime.
    daily_args = []
    hourly_args = []
    minute_args = []
    for system_id, _label in status_systems:
        failure = 0 if ok.get(system_id, True) else 1
        daily_args.extend([day_ts, system_id, failure])
        # reason is only set when this sample failed; on success it's left NULL
        # so the COALESCE below keeps whatever failure reason was last recorded
        # this hour, rather than blanking it out.
        hourly_args.extend([hour_ts, system_id, failure, reason.get(system_id)])
        # A minute is the raw sample itself (not a counter): one cron tick per
        # minute per system, so ON CONFLICT just overwrites in the rare case a
        # tick somehow re-fires for the same minute.
        minute_args.extend(
            [minute_ts, system_id, 0 if failure else 1, reason.get(system_id)])
    n = len(status_systems)
    await d1_run(
        env,
        "INSERT INTO system_status_daily (day_ts, system, checks, failures) "
        "VALUES " + ", ".join(["(?, ?, 1, ?)"] * n) + " "
        "ON CONFLICT(day_ts, system) DO UPDATE SET "
        "checks = checks + excluded.checks, "
        "failures = failures + excluded.failures",
        *daily_args,
    )
    await d1_run(
        env,
        "INSERT INTO system_status_hourly (hour_ts, system, checks, failures, reason) "
        "VALUES " + ", ".join(["(?, ?, 1, ?, ?)"] * n) + " "
        "ON CONFLICT(hour_ts, system) DO UPDATE SET "
        "checks = checks + excluded.checks, "
        "failures = failures + excluded.failures, "
        "reason = COALESCE(excluded.reason, system_status_hourly.reason)",
        *hourly_args,
    )
    await d1_run(
        env,
        "INSERT INTO system_status_minute (minute_ts, system, ok, reason) "
        "VALUES " + ", ".join(["(?, ?, ?, ?)"] * n) + " "
        "ON CONFLICT(minute_ts, system) DO UPDATE SET "
        "ok = excluded.ok, reason = excluded.reason",
        *minute_args,
    )
    # Alerts are optional follow-up work. Persist the minute first so a slow
    # mail provider or notification failure cannot erase public status data.
    try:
        await _record_status_monitor_transitions(
            env, ok, reason, now, status_systems)
    except Exception as exc:
        # Same reasoning as the registry read above: Sentry-only visibility,
        # never an error_log row and never a raised sample.
        try:
            await capture_sentry_error(
                env, 500, "scheduled", "/cron/record-status-sample",
                "record_status_monitor_transitions failed: "
                + _safe_error_text(exc), error=exc)
        except BaseException:
            pass
    # Retention prunes only need to run occasionally, not 60x/hour: sweep on
    # the first sample of each hour.
    if now - hour_ts < STATUS_SAMPLE_WINDOW_MS:
        await d1_run(
            env, "DELETE FROM system_status_daily WHERE day_ts < ?",
            day_ts - STATUS_HISTORY_RETAIN_MS,
        )
        await d1_run(
            env, "DELETE FROM system_status_hourly WHERE hour_ts < ?",
            day_ts - STATUS_HISTORY_RETAIN_MS,
        )
        await d1_run(
            env, "DELETE FROM system_status_minute WHERE minute_ts < ?",
            minute_ts - STATUS_MINUTE_RETAIN_MS,
        )
        await d1_run(
            env,
            "DELETE FROM system_status_sample_claim WHERE minute_ts < ?",
            minute_ts - STATUS_MINUTE_RETAIN_MS,
        )
        await d1_run(
            env,
            "DELETE FROM durable_object_abort_minute WHERE minute_ts < ?",
            minute_ts - STATUS_HISTORY_RETAIN_MS,
        )


async def status_history(env, view="full"):
    await ensure_schema(env)
    now = int(Date.now())
    cur_day = (now // 86400000) * 86400000
    start = cur_day - (STATUS_HISTORY_DAYS - 1) * 86400000
    # Day cells, uptime and coverage all come from the hourly buckets (which
    # sum to the same recorded counts the daily table holds, with per-hour
    # granularity the 24h window needs); the daily table remains as the
    # cron's cheap long-term aggregate.
    hour_rows = await d1_all(
        env,
        "SELECT hour_ts, system, checks, failures, reason FROM system_status_hourly "
        "WHERE hour_ts >= ?",
        start,
    )
    by_system_hour = {}
    for row in hour_rows:
        system_id = str(row.get("system") or "")
        by_system_hour.setdefault(system_id, {})[int(row["hour_ts"])] = (
            int(row.get("checks") or 0), int(row.get("failures") or 0),
            row.get("reason") or None,
        )

    current_minute = (now // 60000) * 60000
    minute_start = current_minute - (STATUS_MINUTES_SHOWN - 1) * 60000
    minute_rows = await d1_all(
        env,
        "SELECT minute_ts, system, ok, reason FROM system_status_minute "
        "WHERE minute_ts >= ?",
        minute_start,
    )
    mirror_last_online_rows = []
    try:
        mirror_last_online_rows = await d1_all(
            env,
            "SELECT lower(name) AS node_name,last_seen AS seen_at FROM nodes "
            "WHERE name IS NOT NULL AND lower(name) LIKE 'mirror%' UNION ALL "
            "SELECT lower(node_name) AS node_name,checked_at AS seen_at "
            "FROM mirror_https_endpoints WHERE lower(node_name) LIKE 'mirror%'",
        )
    except Exception:
        mirror_last_online_rows = []
    mirror_last_online = {}
    for row in mirror_last_online_rows:
        mirror_name = str(row.get("node_name") or "").strip().lower()
        if not mirror_name.startswith("mirror"):
            continue
        mirror_last_online[mirror_name] = max(
            int(mirror_last_online.get(mirror_name) or 0),
            int(row.get("seen_at") or row.get("checked_at") or 0),
        )

    by_system_minute = {}
    last_sample_ts = 0
    for row in minute_rows:
        system_id = str(row.get("system") or "")
        by_system_minute.setdefault(system_id, {})[int(row["minute_ts"])] = row
        last_sample_ts = max(last_sample_ts, int(row["minute_ts"]))

    # Start with recorded mirror history, then union current registered
    # mirror* names so a newly registered but broken node appears immediately
    # as down instead of disappearing until its first successful proof.
    recorded_system_ids = set(by_system_hour) | set(by_system_minute)
    try:
        registered_mirrors = await d1_all(
            env,
            """SELECT lower(name) AS node_name
                 FROM nodes
                WHERE name IS NOT NULL AND lower(name) LIKE 'mirror%'
                  AND last_seen>=?
               UNION
               SELECT lower(node_name) AS node_name
                 FROM mirror_https_endpoints
                WHERE lower(node_name) LIKE 'mirror%'
                  AND checked_at>=?
               ORDER BY node_name LIMIT ?""",
            now - STATUS_MIRROR_ROSTER_FRESH_MS,
            now - STATUS_MIRROR_ROSTER_FRESH_MS,
            STATUS_MIRROR_MAX,
        )
        for row in registered_mirrors or []:
            mirror_name = str(row.get("node_name") or "").strip().lower()
            if (
                mirror_name.startswith("mirror")
                and mirror_name not in STATUS_RETIRED_MIRRORS
                and re.fullmatch(
                    r"[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?", mirror_name)
            ):
                recorded_system_ids.add(STATUS_MIRROR_PREFIX + mirror_name)
    except Exception:
        pass
    mirror_systems = []
    for system_id in recorded_system_ids:
        if not system_id.startswith(STATUS_MIRROR_PREFIX):
            continue
        mirror_name = system_id[len(STATUS_MIRROR_PREFIX):]
        if mirror_name in STATUS_RETIRED_MIRRORS or not re.fullmatch(
                r"[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?", mirror_name):
            continue
        mirror_systems.append(
            (system_id, "Mirror node — " + mirror_name))
    mirror_systems.sort(key=lambda item: item[1].lower())
    status_systems = list(STATUS_SYSTEMS) + mirror_systems

    systems = []
    for system_id, label in status_systems:
        if system_id.startswith(STATUS_MIRROR_PREFIX):
            mirror_name = system_id[len(STATUS_MIRROR_PREFIX):]
            last_seen_at = int(mirror_last_online.get(mirror_name) or 0)
            if (
                not last_seen_at
                or now - last_seen_at > STATUS_MIRROR_ROSTER_FRESH_MS
            ):
                continue
        # Uptime is computed over RECORDED samples only; expected-but-missing
        # samples (the sampling cron didn't run) are reported separately as
        # coverage, never counted as downtime. See _status_effective_hour.
        days = []
        total_checks = total_failures = total_expected = 0
        checks_24h = failures_24h = expected_24h = 0
        current_hour = (now // STATUS_HOUR_MS) * STATUS_HOUR_MS
        uptime_24h_start = current_hour - 23 * STATUS_HOUR_MS
        for i in range(STATUS_HISTORY_DAYS):
            this_day = start + i * STATUS_DAY_MS
            day_checks = day_failures = day_expected = 0
            hours = []
            elapsed_hours = 0
            for h in range(24):
                hour_ts = this_day + h * STATUS_HOUR_MS
                if hour_ts > now:
                    break
                elapsed_hours += 1
                hour = _status_effective_hour(
                    hour_ts, now, by_system_hour.get(system_id, {}).get(hour_ts),
                )
                day_expected += hour["expectedChecks"]
                day_checks += hour["checks"]
                day_failures += hour["failures"]
                if hour_ts >= uptime_24h_start:
                    expected_24h += hour["expectedChecks"]
                    checks_24h += hour["checks"]
                    failures_24h += hour["failures"]
                # Thirty days of hourly dictionaries made a cold /api/status
                # response several megabytes and could exceed the Worker CPU
                # limit (Cloudflare 1101). Daily aggregates retain the entire
                # history; only the latest 24 hourly buckets are needed by the
                # public detail bar and are serialized.
                if hour_ts >= uptime_24h_start:
                    hours.append(hour)
            total_expected += day_expected
            total_checks += day_checks
            total_failures += day_failures
            uptime = (
                round(((day_checks - day_failures) / day_checks) * 100, 2)
                if day_checks else None
            )
            coverage = (
                round((min(day_checks, day_expected) / day_expected) * 100, 2)
                if day_expected else None
            )
            days.append({
                "dayTs": this_day, "checks": day_checks,
                "failures": day_failures,
                "expectedChecks": day_expected,
                "missingChecks": max(0, day_expected - day_checks),
                "uptimePct": uptime, "coveragePct": coverage,
                "hours": hours, "hoursElapsed": elapsed_hours,
                "hoursCompacted": elapsed_hours > len(hours),
            })
        # Current state comes from the newest RAW MINUTE probe. An hourly
        # aggregate answers "did anything fail during this hour?", not "is it
        # reachable now"; using it for the badge left recovered systems yellow
        # and displayed an hour-old reason as though it were current. A failed
        # latest probe is a hard red/down state. A passing latest probe is
        # green and clears the old reason immediately.
        latest_minute_row = None
        minute_records = by_system_minute.get(system_id, {})
        if minute_records:
            latest_minute_ts = max(minute_records)
            latest_minute_row = minute_records[latest_minute_ts]
        reason_text = reason_ts = None
        if (
            latest_minute_row is not None
            and latest_minute_ts >= current_minute - 2 * STATUS_SAMPLE_WINDOW_MS
        ):
            if int(latest_minute_row.get("ok") or 0) == 1:
                status = "operational"
            else:
                status = "down"
                reason_text = (
                    latest_minute_row.get("reason")
                    or "The latest reachability check failed")
                reason_ts = latest_minute_ts
        elif latest_minute_row is not None:
            status = "down"
            reason_text = (
                "Monitoring failed: no health sample has been recorded in the "
                "last two minutes.")
            reason_ts = latest_minute_ts
        else:
            # Backward-compatible fallback for an existing deployment while
            # minute sampling is first being populated.
            status = None

        latest_hour = None
        for d in reversed(days):
            for h in reversed(d["hours"]):
                if h["checks"]:
                    latest_hour = h
                    break
            if latest_hour:
                break
        if status is None:
            if latest_hour is not None and (
                    latest_hour["hourTs"] >= current_hour - STATUS_HOUR_MS):
                status = latest_hour["status"]
                if status != "operational":
                    reason_text = latest_hour.get("reason")
                    reason_ts = latest_hour.get("hourTs")
            elif latest_hour is not None:
                status = "down"
                reason_text = (
                    "Monitoring failed: no recent health samples have been "
                    "recorded.")
                reason_ts = latest_hour.get("hourTs")
            else:
                status = "down"
                reason_text = (
                    "Monitoring failed: no health samples have been recorded.")
        overall_uptime = (
            round(((total_checks - total_failures) / total_checks) * 100, 2)
            if total_checks else None
        )
        uptime_24h = (
            round(((checks_24h - failures_24h) / checks_24h) * 100, 2)
            if checks_24h else None
        )
        coverage_30d = (
            round((min(total_checks, total_expected) / total_expected) * 100, 2)
            if total_expected else None
        )
        coverage_24h = (
            round((min(checks_24h, expected_24h) / expected_24h) * 100, 2)
            if expected_24h else None
        )
        minutes = [
            _status_minute(
                minute_start + i * 60000, current_minute,
                by_system_minute.get(system_id, {}).get(minute_start + i * 60000),
            )
            for i in range(STATUS_MINUTES_SHOWN)
        ]

        check_description = STATUS_SYSTEM_CHECKS.get(system_id, "")
        if system_id.startswith(STATUS_MIRROR_PREFIX):
            mirror_name = system_id[len(STATUS_MIRROR_PREFIX):]
            check_description = (
                "Checks " + mirror_name + " independently once a minute "
                "using its account-bound direct HTTPS endpoint. Passes only "
                "when its signed health and forkmesh/forkmesh repository proof "
                "are fresh, endpoint health is good, integrity is ok, and the "
                "served refs match an accepted source revision.")
        systems.append({
            "id": system_id, "label": label, "status": status,
            "checkDescription": check_description,
            "uptimePct": overall_uptime, "uptime24hPct": uptime_24h,
            "coveragePct": coverage_30d, "coverage24hPct": coverage_24h,
            "checks24h": checks_24h, "failures24h": failures_24h,
            "missing24h": max(0, expected_24h - checks_24h),
            "days": days, "minutes": minutes,
            "reason": reason_text,
            "reasonTs": reason_ts,
        })

    # Current-state snapshot (issue #356): the headline health metrics rendered
    # at the top of the page — flagship repository directly reachable, the
    # distinct online node count (same signal as the /network/ headline),
    # catalog size, and errors logged in the last 24h. Each
    # read is best-effort so one failing query can't blank the summary, and it
    # all rides on the single /api/status fetch a page view already makes.
    current = {}
    # When the last recorded health sample was taken. A stale value means the
    # sampling CRON is down (not the site) — the page uses this to say so
    # instead of letting missing samples masquerade as an outage. 0 = no
    # samples in the visible window at all.
    current["lastCronSampleTs"] = last_sample_ts or None
    try:
        repo_row = await d1_first(env, "SELECT COUNT(*) AS n FROM repositories")
        current["catalogRepos"] = int((repo_row or {}).get("n", 0) or 0)
    except Exception:
        current["catalogRepos"] = None
    try:
        online = {
            label for label in (await _live_online_nodes(env, now)).values() if label
        }
        current["onlineNodes"] = len(online)
    except Exception:
        current["onlineNodes"] = None
    try:
        err_row = await d1_first(
            env, "SELECT COUNT(*) AS n FROM error_log WHERE ts >= ?",
            now - 24 * 60 * 60 * 1000,
        )
        current["errors24h"] = int((err_row or {}).get("n", 0) or 0)
    except Exception:
        current["errors24h"] = None
    try:
        # Dedicated counter for the platform killing a Durable Object request
        # mid-flight ("Exceeded allowed duration in Durable Objects free
        # tier."). Add the new minute aggregates to legacy/raw error rows so
        # deployments retain a continuous 24-hour total while routine aborts
        # stop inflating the generic errors24h count.
        do_row = await d1_first(
            env,
            "SELECT "
            "(SELECT COUNT(*) FROM error_log "
            " WHERE ts>=? AND message LIKE ?) + "
            "(SELECT COALESCE(SUM(duration_aborts),0) "
            " FROM durable_object_abort_minute WHERE minute_ts>=?) AS n",
            now - 24 * 60 * 60 * 1000, "%Exceeded allowed duration%",
            now - 24 * 60 * 60 * 1000,
        )
        current["doDurationAborts24h"] = int((do_row or {}).get("n", 0) or 0)
    except Exception:
        current["doDurationAborts24h"] = None
    try:
        # forkmesh_verified_at is written only after an account-bound endpoint
        # signs a fresh challenge whose refs hash matches the owner-attested
        # public forkmesh/forkmesh catalog state. This avoids treating a
        # control WebSocket heartbeat as proof that Git bytes are available.
        endpoint_row = await d1_first(
            env,
            "SELECT MAX(forkmesh_verified_at) AS ts "
            "FROM mirror_https_endpoints WHERE healthy=1 "
            "AND forkmesh_active=1 AND integrity='ok' "
            "AND abuse_blocked=0",
        )
        last_ts = int((endpoint_row or {}).get("ts") or 0) or None
        current["mainnodeLastSeenTs"] = last_ts
        current["mainnodeOnline"] = (
            last_ts is not None
            and now - last_ts < HTTPS_MIRROR_STATUS_FRESH_MS
        )
        current["mainnodeStaleMs"] = HTTPS_MIRROR_STATUS_FRESH_MS
    except Exception:
        current["mainnodeOnline"] = None
        current["mainnodeLastSeenTs"] = None
        current["mainnodeStaleMs"] = HTTPS_MIRROR_STATUS_FRESH_MS

    if view == "world":
        # The in-world canvas needs the same current state, 30 daily cells,
        # latest 24 hourly cells, and latest 60 minute cells as /status. It
        # does not need every hour nested beneath every one of the 30 days.
        # Projecting that history here cuts the recurring browser payload by
        # an order of magnitude without weakening the public status page.
        projected = []
        for system in systems:
            days = list(system.get("days") or [])
            hours = [
                hour
                for day in days
                for hour in (day.get("hours") or [])
            ][-24:]
            compact_system = {
                key: value
                for key, value in system.items()
                if key not in ("days", "minutes", "checkDescription")
            }
            compact_system["days"] = []
            for day in days:
                compact_day = {
                    key: value
                    for key, value in day.items()
                    if key not in ("hours",)
                }
                checks = int(day.get("checks") or 0)
                failures = int(day.get("failures") or 0)
                compact_day["status"] = (
                    "unknown" if checks <= 0
                    else "down" if failures >= checks
                    else "degraded" if failures
                    else "operational"
                )
                compact_system["days"].append(compact_day)
            compact_system["hours"] = hours
            compact_system["minutes"] = list(system.get("minutes") or [])[-60:]
            projected.append(compact_system)
        systems = projected

    return json_response(
        {"ok": True, "now": now, "systems": systems, "current": current},
        cache_seconds=STATUS_EDGE_CACHE_TTL,
    )
